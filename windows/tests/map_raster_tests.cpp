#include "native/map_raster.hpp"
#include "modules/map_geography.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <thread>

namespace {std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{::operator delete(p);}void operator delete[](void*p,std::size_t)noexcept{::operator delete(p);}
namespace n=endfield::native;namespace m=endfield::modules;namespace a=endfield::app;
namespace {
std::size_t checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F&&f,const char*why){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,why);}
auto geography(){auto g=std::make_shared<m::MapGeography>();g->terrain=m::MapTerrain{};return g;}
m::MapViewport camera(double x){auto v=m::MapViewport{};v.centerX=x;return m::mapConstrained(v);}
struct Paint {
    std::mutex mutex;std::condition_variable condition;bool blocked{},entered{},released{},fail{},malformed{};
    std::vector<n::MapPaintRequest>calls;unsigned cacheClears{},cancellations{};double seconds{.004};
    void block(){std::lock_guard lock(mutex);blocked=true;entered=false;released=false;}
    void wait(){std::unique_lock lock(mutex);if(!condition.wait_for(lock,std::chrono::seconds(3),[&]{return entered;}))throw std::runtime_error("Synthetic worker did not enter bounded gate");}
    void release(){std::lock_guard lock(mutex);released=true;condition.notify_all();}
    n::MapPaintResult run(const n::MapPaintRequest&q,const n::MapPaintCancelled&cancel){
        {std::unique_lock lock(mutex);calls.push_back(q);if(blocked){entered=true;condition.notify_all();condition.wait(lock,[&]{return released;});blocked=false;}}
        if(cancel()){++cancellations;return {{},{},{},seconds};}if(fail)return {{},{},{},seconds};
        const auto g=*m::mapRasterGeometry(q.viewport,q.contentsScale);auto image=std::make_shared<n::MapPaintImage>();image->width=image->height=g.pixelDimension;image->straightRGBA.resize(std::size_t(g.pixelDimension)*g.pixelDimension*4,127);
        n::MapPaintResult result{image,{},m::MapRasterFrame{g.viewport,g.screenRect,g.pixelsPerPoint},seconds};
        if(q.backdrop){auto b=std::make_shared<n::MapPaintImage>();b->width=1024;b->height=512;b->straightRGBA.resize(1024*512*4,255);result.backdrop=b;}
        if(malformed)result.frame->pixelsPerPoint=-1;return result;
    }
};
struct Fixture {
    double time{1};std::atomic<unsigned>wakes{};unsigned changes{};std::shared_ptr<Paint>paint=std::make_shared<Paint>();a::UtilityExecutor utility{[&]{++wakes;}};
    std::unique_ptr<n::NativeMapRaster>raster;
    Fixture(){raster=std::make_unique<n::NativeMapRaster>(utility,[&]{return time;},[&]{++changes;},[p=paint](const auto&q,const auto&c){return p->run(q,c);},[p=paint]{++p->cacheClears;});raster->configure(true,{.98,.87,.13,1},1,time);raster->setGeography(geography(),time);}
    ~Fixture(){paint->release();raster.reset();utility.shutdown();}
    void drain(){utility.waitIdle();utility.drain();}
    void initial(){raster->setActive(true,time);drain();check(raster->settled(),"Initial exact image settles through shared executor completion");}
};
void scheduling(){
    Fixture f;check(f.utility.stats().accepted==0&&!f.raster->nextWakeTime(),"Inactive geography/configuration starts no worker or timer");f.initial();
    check(f.paint->calls.size()==1&&f.paint->calls.front().backdrop&&f.raster->backdrop(),"First paint generates exactly one source world backdrop");
    const auto image=f.raster->detail();const auto base=allocations.load();bool idle=true;for(unsigned k=0;k<1000;++k)idle=idle&&!f.raster->advance(f.time)&&!f.raster->nextWakeTime()&&f.raster->settled();
    const auto end=allocations.load();check(idle&&end==base&&f.utility.stats().accepted==1,"1000 settled samples allocate and submit nothing");
    auto v=f.raster->frame()->viewport;v.centerX+=.00001;f.time=1.01;f.raster->update(v,false,f.time);check(!f.raster->nextWakeTime()&&f.raster->detail()==image,"Padded cached image handles a tiny pan without new paint");
    f.raster->finishGesture(f.time);check(f.raster->nextWakeTime()&&std::abs(*f.raster->nextWakeTime()-1.09)<1e-12,"Source final-exact refinement waits only80ms shared deadline");
    f.time=1.089;check(!f.raster->advance(f.time),"Early deadline does not paint");f.time=1.0901;check(f.raster->advance(f.time),"Exact final camera paints once when due");f.drain();
    check(f.raster->settled()&&f.paint->calls.size()==2&&!f.paint->calls.back().backdrop,"Settled refinement preserves original backdrop identity");
    f.paint->seconds=.2;f.time=2;f.raster->update(camera(.2),false,f.time);f.drain();check(std::abs(f.raster->stats().cooldown-3.2)<1e-12,"Whole worker cost sets original16x cooldown");
    f.time=2.1;f.raster->update(camera(.3),false,f.time);f.time=2.2;f.raster->update(camera(.4),false,f.time);check(f.paint->calls.size()==3&&std::abs(*f.raster->nextWakeTime()-5.2)<1e-12,"Camera updates coalesce behind one source budget deadline");
    f.time=5.2;f.raster->advance(f.time);f.drain();check(f.paint->calls.size()==4&&f.paint->calls.back().viewport==camera(.4),"Deadline captures latest camera, never a queue of old poses");
}
void inFlightAndAwayBack(){
    Fixture f;f.initial();const auto original=f.raster->detail();const auto home=f.raster->frame()->viewport;
    f.paint->block();f.time=2;f.raster->update(camera(.3),false,f.time);f.paint->wait();f.time=2.01;f.raster->update(home,false,f.time);f.paint->release();f.time=2.02;f.drain();
    check(f.raster->detail()==original&&f.raster->frame()->viewport==home&&f.raster->stats().discarded==1,"Away-and-back retains already exact image over an actual obsolete in-flight paint");
    check(!f.raster->nextWakeTime()&&f.raster->stats().submitted==2,"Away-and-back regression schedules no redundant replacement");
    f.paint->block();f.time=3;f.raster->update(camera(.2),false,f.time);f.paint->wait();f.time=3.01;f.raster->update(camera(.4),false,f.time);f.time=3.02;f.raster->update(camera(.6),false,f.time);
    check(f.utility.stats().pending==0&&f.raster->stats().submitted==3,"A running paint is the only accepted map task");f.paint->release();f.time=3.03;f.drain();
    check(f.raster->frame()->viewport==camera(.2)&&f.raster->nextWakeTime(),"Source may display bounded intermediate image while latest camera waits");f.time=*f.raster->nextWakeTime();f.raster->advance(f.time);f.drain();
    check(f.paint->calls.back().viewport==camera(.6)&&f.raster->frame()->viewport==camera(.6),"Latest retained camera wins after single worker completes");
}
void timingAndQuality(){
    Fixture f;f.raster->configure(true,{.98,.87,.13,1},2.2,f.time);f.initial();f.paint->block();f.time=2;f.raster->update(camera(.2),false,f.time);f.paint->wait();f.time=2.01;f.raster->update(camera(.4),true,f.time);f.paint->release();f.time=2.02;f.drain();
    check(f.paint->calls.back().contentsScale==1.4&&f.raster->stats().discarded==1,"Source motion caps detail at1.4 and discards paint arriving during180ms camera animation");
    check(std::abs(*f.raster->nextWakeTime()-2.19)<1e-12,"Animated camera expiration uses caller clock only");f.time=2.19001;f.raster->advance(f.time);f.drain();
    check(f.paint->calls.back().contentsScale==2.2&&f.raster->settled(),"Animated final camera restores source2.2 quality");
    f.time=3;f.raster->update(camera(.6),false,f.time);f.drain();check(f.raster->stats().interacting&&f.paint->calls.back().contentsScale==1.4,"Direct camera input enters low-resolution motion mode");
    f.time=3.01;f.raster->finishGesture(f.time);f.time=3.1;f.raster->advance(f.time);f.drain();check(f.paint->calls.back().contentsScale==2.2&&f.raster->settled(),"Gesture end refines a same-camera lower quality frame");
}
void cancellationAndFailure(){
    Fixture f;f.initial();auto old=f.raster->detail();f.paint->block();f.time=2;f.raster->update(camera(.2),false,f.time);f.paint->wait();f.time=2.01;f.raster->configure(false,{.1,.3,.6,1},1,f.time);f.paint->release();f.time=2.02;f.drain();f.drain();
    check(f.paint->cancellations==1&&f.raster->detail()!=old&&f.paint->calls.back().dark==false&&f.raster->settled(),"Theme invalidation cancels stale work and publishes only current source palette");
    old=f.raster->detail();f.paint->block();f.time=3;f.raster->update(camera(.3),false,f.time);f.paint->wait();f.time=3.01;f.raster->update(camera(.4),true,f.time);f.raster->setActive(false,f.time);f.paint->release();f.time=3.02;f.drain();f.drain();
    check(f.raster->detail()==old&&!f.raster->nextWakeTime()&&f.paint->cacheClears==1&&!f.raster->stats().waiting,"Conceal retains closing pixels and releases worker caches even before camera animation ends");
    f.time=4;f.raster->setActive(true,f.time);f.drain();check(f.raster->settled(),"Reactivation reuses data and repaints current camera on same executor");
    f.paint->fail=true;f.time=5;f.raster->update(camera(.1),false,f.time);f.drain();const auto submitted=f.raster->stats().submitted;check(f.raster->stats().failed==1&&!f.raster->nextWakeTime(),"Null worker result retains prior pixels without a failure retry loop");
    for(unsigned k=0;k<100;++k)f.raster->advance(f.time);check(f.raster->stats().submitted==submitted,"Failed paint creates no idle retry work");
    f.paint->fail=false;f.paint->malformed=true;f.time=6;f.raster->update(camera(.2),false,f.time);f.drain();check(f.raster->stats().failed==2,"Malformed injected bitmap metadata is rejected before publication");
    f.paint->malformed=false;f.time=7;f.raster->update(camera(.3),false,f.time);f.drain();check(f.raster->frame()->viewport==camera(.3),"Later genuine camera event recovers after painter failure");
    const auto frame=f.raster->frame();rejects([&]{f.raster->update({0,0,std::numeric_limits<double>::infinity()},false,f.time);},"Non-finite camera cannot reach worker");rejects([&]{f.raster->configure(true,{0,0,0,2},1,f.time);},"Invalid color rejected");rejects([&]{f.raster->advance(0);},"Backward owner clock rejected");check(f.raster->frame()->viewport==frame->viewport&&f.raster->frame()->screenRect==frame->screenRect&&f.raster->frame()->pixelsPerPoint==frame->pixelsPerPoint,"Rejected inputs retain previous image metadata");
}
void backpressureAndTeardown(){
    double time=1;std::atomic<unsigned>wakes{};a::UtilityExecutor utility([&]{++wakes;},1);const auto other=utility.makeRoute();check(utility.submit(other,[]{},[](auto){}),"Synthetic sibling reserves queue capacity");utility.waitIdle();
    auto paint=std::make_shared<Paint>();unsigned changes{};auto raster=std::make_unique<n::NativeMapRaster>(utility,[&]{return time;},[&]{++changes;},[paint](const auto&q,const auto&c){return paint->run(q,c);});raster->configure(true,{1,1,0,1},1,time);raster->setGeography(geography(),time);raster->setActive(true,time);
    check(raster->stats().backpressure==1&&!raster->nextWakeTime()&&paint->calls.empty(),"Full shared executor retains request without retry timer or lost input");const auto allocated=allocations.load();for(unsigned k=0;k<100;++k)raster->update(camera(.2+double(k)*.001),false,time);const auto after=allocations.load();check(after==allocated&&raster->stats().backpressure==1,"Saturated shared queue retains changed camera without repeated submissions or allocations");raster->update(camera(.3),false,time);utility.drain();check(raster->submitPending(time),"Sibling drain explicitly retries the latest map request");utility.waitIdle();utility.drain();check(raster->frame()->viewport==camera(.3),"Backpressure does not revert latest camera");
    paint->block();time=2;raster->update(camera(.4),false,time);paint->wait();const auto before=changes;raster.reset();paint->release();utility.waitIdle();utility.drain();check(changes==before&&paint->cancellations==1,"Destroyed owner cancels running work and invalidates completion route");
    unsigned callbacks{};std::unique_ptr<n::NativeMapRaster>reentrant;reentrant=std::make_unique<n::NativeMapRaster>(utility,[&]{return time;},[&]{++callbacks;reentrant.reset();},[paint](const auto&q,const auto&c){return paint->run(q,c);});reentrant->setGeography(geography(),time);check(!reentrant&&callbacks==1,"Invalidation callback can synchronously destroy raster owner");
    utility.invalidate(other);utility.shutdown();
}
}
int main(){try{scheduling();inFlightAndAwayBack();timingAndQuality();cancellationAndFailure();backpressureAndTeardown();std::cout<<"Map shared raster: "<<checks<<" checks passed (synthetic data, single borrowed executor)\n";return 0;}catch(const std::exception&e){std::cerr<<"Map shared raster failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
