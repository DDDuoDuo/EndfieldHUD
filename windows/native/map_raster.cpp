#include "native/map_raster.hpp"
#include "modules/map_geography.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace endfield::native {
namespace {
void need(bool v,const char*message){if(!v)throw std::invalid_argument(message);}
bool imageValid(const std::shared_ptr<const MapPaintImage>&i,unsigned maximum){return i&&i->width&&i->height&&i->width<=maximum&&i->height<=maximum&&i->straightRGBA.size()==std::size_t(i->width)*i->height*4;}
bool validResult(const MapPaintResult&r,const MapPaintRequest&q){if(!std::isfinite(r.workSeconds)||r.workSeconds<0)return false;if(!r.detail)return !r.frame&&!r.backdrop;if(!r.frame||!imageValid(r.detail,1536)||r.detail->width!=r.detail->height)return false;const auto geometry=modules::mapRasterGeometry(q.viewport,q.contentsScale);return geometry&&r.frame->viewport==geometry->viewport&&r.frame->screenRect==geometry->screenRect&&r.frame->pixelsPerPoint==geometry->pixelsPerPoint&&r.detail->width==geometry->pixelDimension&&(!r.backdrop||(q.backdrop&&imageValid(r.backdrop,1024)&&r.backdrop->width==1024&&r.backdrop->height==512));}
}
struct NativeMapRaster::Impl : std::enable_shared_from_this<Impl> {
    app::UtilityExecutor&executor;app::UtilityExecutor::Route route;Clock clock;Changed changed;MapPaintFunction paint;std::function<void()>releaseCaches;
    const std::thread::id owner=std::this_thread::get_id();bool alive{true},active{},interacting{},exact{},busy{},backpressured{},cleanupPending{},cleanupBusy{};
    std::shared_ptr<const modules::MapGeography>geography;bool dark{true};modules::MapColor accent{.98,.87,.13,1};double scale{2},time{},lastFinished{},cameraEnds{},cooldown{.125};
    modules::MapViewport viewport;std::optional<modules::MapRasterFrame>frame;std::shared_ptr<const MapPaintImage>detail,backdrop;
    std::optional<double>deadline;std::uint64_t revision{},frameRevision{UINT64_MAX},backdropRevision{UINT64_MAX},generation{1};
    std::shared_ptr<std::atomic<std::uint64_t>>token=std::make_shared<std::atomic<std::uint64_t>>(1);Stats counts;
    Impl(app::UtilityExecutor&e,Clock c,Changed f,MapPaintFunction p,std::function<void()>release):executor(e),route(e.makeRoute()),clock(std::move(c)),changed(std::move(f)),paint(std::move(p)),releaseCaches(std::move(release)){}
    void check()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Map raster owner-thread operation");}
    void at(double now){check();need(std::isfinite(now)&&now>=time,"Map raster requires a finite monotonic clock");time=now;}
    void invalidate(){if(generation==UINT64_MAX||revision==UINT64_MAX)throw std::overflow_error("Map raster generation exhausted");++generation;++revision;token->store(generation,std::memory_order_release);exact=true;}
    bool hasData()const{return geography&&(geography->terrain||geography->countries);}
    bool needs()const{return modules::mapNeedsPaint(frame?&*frame:nullptr,viewport,frameRevision==revision,interacting,exact,scale);}
    void notify(){auto callback=changed;if(alive&&callback)callback();}
    void request(bool immediate=false){if(!alive||!active||!hasData())return;if(!needs()){deadline.reset();backpressured=false;return;}if(busy||cleanupBusy||cleanupPending||backpressured)return;if(immediate)deadline.reset();if(deadline)return;const auto delay=std::max(cameraEnds-time,immediate?0.:std::max(0.,cooldown-(time-lastFinished)));if(delay<.001)submit();else deadline=time+delay;}
    bool cleanup(){if(!alive||!cleanupPending||busy||cleanupBusy)return false;if(!releaseCaches){cleanupPending=false;return false;}const auto weak=weak_from_this();const bool accepted=executor.submit(route,releaseCaches,[weak](std::exception_ptr){if(auto self=weak.lock()){self->cleanupBusy=false;++self->counts.cacheReleases;if(!self->alive)return;self->at(self->clock());self->request(true);if(self->alive)self->notify();}});if(!accepted){++counts.backpressure;return false;}cleanupPending=false;cleanupBusy=true;return true;}
    bool submit(){if(!alive||busy||cleanupBusy||!active||!hasData()||!needs())return false;if(cameraEnds>time){deadline=cameraEnds;return false;}MapPaintRequest request{geography,viewport,dark,accent,modules::mapRenderScale(scale,interacting),backdropRevision!=revision,revision};const auto version=generation;auto result=std::make_shared<MapPaintResult>();const auto weak=weak_from_this();
        const bool accepted=executor.submit(route,[paint=paint,request,result,version,token=token]{const auto cancelled=[token,version]{return token->load(std::memory_order_acquire)!=version;};if(cancelled())return;*result=paint(request,cancelled);if(!cancelled()&&!validResult(*result,request))throw std::invalid_argument("Invalid bounded map painter result");},[weak,result,request,version](std::exception_ptr error){if(auto self=weak.lock()){
            self->busy=false;++self->counts.completed;if(!self->alive)return;self->at(self->clock());self->lastFinished=self->time;self->cooldown=modules::mapPaintCooldown(result->workSeconds);
            const bool cancelled=version!=self->generation;
            if(error||(!result->detail&&!cancelled)){++self->counts.failed;self->cleanup();self->notify();return;} // no failure retry spin
            if(self->cameraEnds>self->time){++self->counts.discarded;self->cleanup();self->request(true);self->notify();return;}
            if(self->active&&!cancelled&&request.revision==self->revision&&result->frame){
                if(self->frame&&self->frameRevision==request.revision&&self->frame->viewport==self->viewport&&self->frame->pixelsPerPoint+.002>=self->scale&&result->frame->viewport!=self->viewport){self->exact=false;++self->counts.discarded;}
                else{self->frame=result->frame;self->frameRevision=request.revision;self->detail=result->detail;++self->counts.published;if(result->backdrop){self->backdrop=result->backdrop;self->backdropRevision=request.revision;}if(self->frame->viewport==self->viewport)self->exact=false;}
            }else ++self->counts.discarded;
            self->cleanup();self->request(self->exact);if(self->alive)self->notify();
        }});
        if(!accepted){++counts.backpressure;backpressured=true;deadline.reset();return false;}backpressured=false;deadline.reset();busy=true;++counts.submitted;return true;
    }
};
NativeMapRaster::NativeMapRaster(app::UtilityExecutor&e,Clock clock,Changed changed,MapPaintFunction paint,std::function<void()>release){need(bool(clock)&&bool(changed)&&bool(paint),"Map raster needs explicit owner clock, invalidation and worker painter");impl_=std::make_shared<Impl>(e,std::move(clock),std::move(changed),std::move(paint),std::move(release));}
NativeMapRaster::~NativeMapRaster(){auto i=impl_;i->alive=false;i->token->store(0,std::memory_order_release);i->changed={};i->executor.invalidate(i->route);}
void NativeMapRaster::setGeography(std::shared_ptr<const modules::MapGeography>value,double t){auto i=impl_;i->at(t);if(i->geography==value)return;i->geography=std::move(value);i->invalidate();i->request(true);i->notify();}
void NativeMapRaster::configure(bool dark,modules::MapColor accent,double scale,double t){auto i=impl_;i->at(t);need(std::isfinite(scale)&&std::all_of(accent.begin(),accent.end(),[](double n){return std::isfinite(n)&&n>=0&&n<=1;}),"Invalid map style");scale=modules::mapRenderScale(scale,false);if(i->dark==dark&&i->accent==accent&&i->scale==scale)return;i->dark=dark;i->accent=accent;i->scale=scale;i->invalidate();i->request(true);i->notify();}
void NativeMapRaster::setActive(bool value,double t){auto i=impl_;i->at(t);if(i->active==value)return;i->active=value;if(value)i->request(true);else{i->interacting=false;i->deadline.reset();i->backpressured=false;need(i->generation!=UINT64_MAX,"Map cancellation generation exhausted");i->token->store(++i->generation,std::memory_order_release);i->cleanupPending=true;i->cleanup();}i->notify();}
void NativeMapRaster::update(modules::MapViewport value,bool animated,double t){auto i=impl_;i->at(t);need(modules::mapConstrained(value)==value,"Map raster camera must already be constrained");if(i->viewport!=value){i->exact=false;if(i->active)i->interacting=!animated;}i->viewport=value;i->cameraEnds=animated?t+.18:0;i->request();i->notify();}
void NativeMapRaster::finishGesture(double t){auto i=impl_;i->at(t);i->interacting=false;i->exact=!i->frame||i->frame->viewport!=i->viewport||i->frameRevision!=i->revision;i->cameraEnds=std::max(i->cameraEnds,t+.08);i->request(true);i->notify();}
bool NativeMapRaster::advance(double t){auto i=impl_;i->at(t);if(!i->deadline||t<*i->deadline)return false;i->deadline.reset();return i->submit();}
bool NativeMapRaster::submitPending(double t){auto i=impl_;i->at(t);const bool clean=i->cleanup();if(i->backpressured){i->backpressured=false;i->request(true);return clean||i->busy;}return clean;}
std::optional<double>NativeMapRaster::nextWakeTime()const noexcept{return impl_->deadline;}
bool NativeMapRaster::settled()const noexcept{const auto&i=*impl_;return i.frame&&i.frame->viewport==i.viewport&&i.frameRevision==i.revision&&!i.busy&&!i.deadline&&!i.needs();}
const std::optional<modules::MapRasterFrame>&NativeMapRaster::frame()const noexcept{return impl_->frame;}
std::shared_ptr<const MapPaintImage>NativeMapRaster::detail()const noexcept{return impl_->detail;}
std::shared_ptr<const MapPaintImage>NativeMapRaster::backdrop()const noexcept{return impl_->backdrop;}
NativeMapRaster::Stats NativeMapRaster::stats()const noexcept{const auto&i=*impl_;auto s=i.counts;s.painting=i.busy;s.waiting=i.deadline.has_value()||i.backpressured||i.cleanupPending||i.cleanupBusy;s.active=i.active;s.interacting=i.interacting;s.cooldown=i.cooldown;return s;}
}
