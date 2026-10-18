#include "core/module_presentation.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<std::size_t> allocations{};}
void*operator new(std::size_t size){allocations.fetch_add(1,std::memory_order_relaxed);if(auto*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace endfield::core;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
void checkNear(double value,double expected,const char*message){check(std::isfinite(value)&&std::abs(value-expected)<1e-12,message);}
bool offsetEqual(const ModuleOffset&a,const ModuleOffset&b){return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.rotationX==b.rotationX&&a.rotationY==b.rotationY&&a.perspective==b.perspective;}
bool frameEqual(MotionRect a,MotionRect b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
template<class Path>void pathEqual(const Path&a,const Path&b){for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<a[i].size();++j){checkNear(a[i][j].x,b[i][j].x,"Source path X retained");checkNear(a[i][j].y,b[i][j].y,"Source path Y retained");}}
void normalized(const ModulePresentationSample&s,Module module,std::uint64_t identity){
    check(s.selected==module&&s.requested==module&&s.current.module==module&&s.current.identity==identity,"Committed source screen and retained wrapper identity");
    check(!s.transitioning&&s.acceptsModuleInput&&!s.incoming,"Normalized host contains one interactive screen");
    check(!s.current.shutter&&!s.current.registration&&!s.current.transform.animated&&s.current.opacity==1,"Completion clears only transition presentation data");
    check(offsetEqual(s.current.transform.from,{})&&offsetEqual(s.current.transform.to,{}),"Normalized wrapper has identity transform endpoints");
}
void lifecycle(){
    ModulePresentation p;auto u=p.sample(0);const auto power=u.presentation.current.identity;
    normalized(u.presentation,Module::power,power);check(!p.requiresFrames()&&!u.change&&!u.completedRequest,"Stable module has no animation demand or repeated completion");
    u=p.select(Module::notes,1,true,false,10);const auto notes=u.presentation.incoming->identity;const auto first=p.generation();
    check(u.change==ModulePresentationChange{Module::power,Module::notes,true}&&p.selected()==Module::power&&p.requested()==Module::notes,"Selection starts a real two-screen swap without early commit");
    check(p.isPresenting(Module::power)&&p.isPresenting(Module::notes)&&!p.isPresenting(Module::map)&&p.requiresFrames(),"Only current and incoming modules are presenting");
    u=p.select(Module::map,1.05,true,false,11);check(!u.change&&!u.completedRequest&&p.pending()==Module::map&&u.presentation.incoming->identity==notes,"Queued request replaces destination without replacing active incoming screen");
    u=p.select(Module::calendar,1.06,true,false,12);check(p.pending()==Module::calendar,"Newest queued request wins");
    u=p.complete(first,1.1);check(!u.change&&!u.completedRequest&&p.selected()==Module::power,"Premature completion does not commit source content");
    u=p.sample(1.3);check(p.selected()==Module::notes&&p.requested()==Module::calendar&&p.pending()==std::nullopt,"First completion commits then begins queued destination");
    check(u.presentation.current.identity==notes&&u.change==ModulePresentationChange{Module::notes,Module::calendar,true}&&!u.completedRequest,"Queued begin emits actual presentation change and defers newest completion");
    const auto calendar=u.presentation.incoming->identity,second=p.generation();
    u=p.complete(first,1.31);check(p.generation()==second&&p.selected()==Module::notes&&!u.completedRequest,"Stale completion cannot commit a later generation");
    u=p.sample(1.6);normalized(u.presentation,Module::calendar,calendar);check(u.completedRequest==12&&!u.change,"Only latest completion fires after all swaps settle");
    u=p.sample(20);check(!u.completedRequest&&!p.requiresFrames(),"Idle sampling cannot replay completions or keep frames alive");
    u=p.select(Module::calendar,20,true,false,13);normalized(u.presentation,Module::calendar,calendar);check(u.completedRequest==13&&!u.change,"Same settled selection completes immediately without a presentation change");
}
void pendingCancelSettle(){
    ModulePresentation p;auto u=p.select(Module::notes,0,true,false,1);const auto notes=u.presentation.incoming->identity,power=u.presentation.current.identity;
    p.select(Module::map,.02,true,false,2);u=p.select(Module::notes,.03,true,false,3);
    check(!p.pending()&&p.requested()==Module::notes&&!u.change,"Requesting active destination clears pending module");
    u=p.settle(.1);normalized(u.presentation,Module::notes,notes);check(u.completedRequest==3&&u.change==ModulePresentationChange{Module::power,Module::notes,false},"Settle reuses incoming wrapper and delivers newest completion");
    p.select(Module::map,.2,true,false,4);p.select(Module::calendar,.21,true,false,5);const auto prior=p.generation();u=p.cancel(.22);
    normalized(u.presentation,Module::notes,notes);check(!u.completedRequest&&p.generation()>prior&&u.change==ModulePresentationChange{Module::notes,Module::notes,false},"Cancellation invalidates completion and restores last committed screen");
    u=p.complete(prior,1);normalized(u.presentation,Module::notes,notes);check(!u.completedRequest,"Completion after cancellation cannot revive incoming content");
    p.select(Module::map,2,true,false,6);p.select(Module::calendar,2.01,true,false,7);u=p.settle(2.02);
    check(u.presentation.current.module==Module::calendar&&u.presentation.current.identity!=notes&&u.completedRequest==7&&!p.requiresFrames(),"Settle creates most recent queued screen without animating intermediate destination");
    u=p.select(Module::power,3,false,false,8);check(u.presentation.current.module==Module::power&&u.presentation.current.identity!=power&&u.completedRequest==8,"Nonanimated select creates the source wrapper and completes immediately");
    p.select(Module::notes,4,true,false,9);u=p.select(Module::map,4.01,true,true,10);
    check(!u.presentation.transitioning&&u.presentation.current.module==Module::map&&u.completedRequest==10&&u.change==ModulePresentationChange{Module::power,Module::map,false},"Reduced-motion selection settles latest request immediately");
    p.select(Module::notes,5,true,false,11);p.select(Module::map,5.01,true,false);p.sample(5.3);u=p.sample(5.6);check(!u.completedRequest,"A newer nil completion replaces the earlier callback");
    p.select(Module::notes,6,true,false,12);p.select(Module::power,6.01,true,false,13);u=p.sample(60);
    check(u.presentation.current.module==Module::notes&&u.presentation.incoming->module==Module::power&&u.presentation.incoming->transform.easedProgress==0&&!u.completedRequest,"Delayed delivery starts queued transition now, without catch-up or skipped screen");
    u=p.sample(60.3);check(u.completedRequest==13&&!p.isTransitioning(),"Delayed queued transition keeps its full original duration");
}
void sourceTracks(){
    for(unsigned from=0;from<24;++from)for(unsigned to=0;to<24;++to){if(from==to)continue;
        const auto a=static_cast<Module>(from),b=static_cast<Module>(to);ModulePresentation p(a);auto u=p.select(b,0);const auto direction=moduleDirection(a,b);
        check(frameEqual(u.presentation.current.contentFrame,moduleLocalContentFrame(a))&&frameEqual(u.presentation.incoming->contentFrame,moduleLocalContentFrame(b)),"Every source module keeps its authored local content frame");
        check(offsetEqual(u.presentation.incoming->transform.from,ModuleTransitionStyle::incomingOffset(direction))&&offsetEqual(u.presentation.incoming->transform.to,{}),"Incoming transform preserves exact source endpoint construction");
        check(offsetEqual(u.presentation.current.transform.from,{})&&offsetEqual(u.presentation.current.transform.to,ModuleTransitionStyle::outgoingOffset(direction)),"Outgoing transform preserves exact opposite source endpoint");
        for(double time:{0.,.023,.054,.125,.21,.299}){
            u=p.sample(time);const auto&current=u.presentation.current;const auto&next=*u.presentation.incoming;
            check(current.opacity==1&&next.opacity==1&&!u.presentation.acceptsModuleInput,"Source wrappers never cross-fade or accept input mid-swap");
            checkNear(current.transform.easedProgress,ModuleTransitionStyle::timing.value(time/.3),"Outgoing uses source cubic timing");checkNear(next.transform.easedProgress,current.transform.easedProgress,"Both source transform tracks share one clock");
            pathEqual(*current.shutter,ModuleTransitionStyle::shutterAt(time,{-direction.x,-direction.y},false));pathEqual(*next.shutter,ModuleTransitionStyle::shutterAt(time,direction,true));
            pathEqual(next.registration->path,ModuleTransitionStyle::registrationAt(time,direction));checkNear(next.registration->opacity,ModuleTransitionStyle::registrationOpacityAt(time),"Registration uses its separate uneased opacity keyframes");
        }
        const auto identity=u.presentation.incoming->identity,generation=p.generation();p.setDark(false);u=p.sample(.299);
        check(u.presentation.incoming->registration->white==.22&&u.presentation.incoming->registration->colorAlpha==.38&&u.presentation.incoming->registration->lineWidth==.7,"Light theme preserves source neutral seam style");
        check(p.generation()==generation&&u.presentation.incoming->identity==identity,"Theme repaint preserves in-flight wrapper and generation");
        u=p.sample(.3);normalized(u.presentation,b,identity);
    }
    check(ModulePresentation::masksToBounds&&!ModulePresentation::allowsGroupOpacity&&frameEqual(ModulePresentation::hostFrame,{280,100,440,440})&&ModulePresentation::wrapperAnchor.x==.5&&ModulePresentation::wrapperPosition.x==220,"Original module host clipping, group opacity and center anchor stay explicit");
}
void guardsAndAllocation(){
    ModulePresentation p;p.select(Module::notes,1,true,false,99);const auto generation=p.generation();
    for(double time:{0.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        bool rejected=false;try{p.select(Module::map,time);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&p.generation()==generation&&p.requested()==Module::notes,"Invalid caller clock leaves selection and completion unchanged");
    }
    check(p.sample(1.3).completedRequest==99,"Rejected requests do not replace latest completion");
    const auto before=allocations.load();
    for(unsigned i=0;i<1000;++i)(void)p.sample(2+i/60.);
    p.select(Module::map,20);for(unsigned i=0;i<120;++i)(void)p.sample(20+i*.3/120.);p.sample(20.3);
    p.select(Module::calendar,21);p.select(Module::power,21.01);p.sample(21.3);p.cancel(21.4);
    const auto after=allocations.load();check(before==after,"Selection, steady frames, active samples, queue completion and cancellation allocate no heap storage");
}
}
int main(){try{lifecycle();pendingCancelSettle();sourceTracks();guardsAndAllocation();std::cout<<"PASS "<<checks<<" module presentation checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
