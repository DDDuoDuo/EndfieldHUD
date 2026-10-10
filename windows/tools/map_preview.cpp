#include "tools/map_preview.hpp"
#ifdef _WIN32
#include <windows.h>
#include "modules/map_geography.hpp"
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace gpu=native;using Matrix=core::Matrix4;using Point=core::Point;using Json=ehud::data::Json;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
Json blank(){return Json::Object{{"bounds",Json::Array{0,0,440,440}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
native::MapAppearance appearanceFor(native::MapAppearance a,bool terrain){a.terrainReady=terrain;if(!a.strings.heading.starts_with("//"))a.strings.heading="// "+a.strings.heading;return a;}
std::optional<double>earlier(std::optional<double>a,std::optional<double>b){return a&&b?std::min(*a,*b):a?a:b;}
bool hasTerrain(const std::shared_ptr<const modules::MapGeography>&g){return g&&g->terrain.has_value();}
}
struct MapPreview::Impl : std::enable_shared_from_this<Impl> {
    modules::MapState state;gpu::LayerRasterizer&paint;app::UtilityExecutor&utility;
    std::shared_ptr<const modules::MapGeography>geography;gpu::MapAppearance appearance;gpu::LayerRasterOptions options;
    std::function<void()>changed,recentered;std::function<void(modules::MapPinStyle)>pinStyleChanged;
    std::unique_ptr<gpu::NativeMapRaster>raster;std::unique_ptr<gpu::NativeMapScene>scene;
    gpu::LayerScene geometry;gpu::NativeModuleSurface surface;gpu::NativeModuleRegistration registration{"map.registration"};
    std::vector<gpu::LayerCompositionEntry>composed;
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;std::optional<Point>pointerPoint;
    bool alive{true},ready{},overlayVisible{true},prepared{},input{},pressed{};double time{};unsigned eventDepth{};
    std::uint64_t cameraRevision{},gestureRevision{},eventRevision{};
    struct Event {Impl&i;std::uint64_t revision;Event(Impl&v,double t):i(v),revision(0){need(std::isfinite(t),"Map owner needs finite time");need(i.eventRevision!=UINT64_MAX,"Map owner event revision exhausted");revision=++i.eventRevision;if(!i.eventDepth)i.time=std::max(i.time,t);++i.eventDepth;}~Event(){--i.eventDepth;}bool current()const{return i.alive&&i.eventRevision==revision;}};
    bool current(std::uint64_t revision)const{return alive&&eventRevision==revision;}
    Impl(gpu::LayerRasterizer&r,app::UtilityExecutor&e,MapPreviewOptions&o):state(std::move(o.initial),std::move(o.persistence)),paint(r),utility(e),geography(std::move(o.geography)),appearance(appearanceFor(o.appearance,hasTerrain(geography))),options(o.raster),changed(std::move(o.changed)),recentered(std::move(o.recentered)),pinStyleChanged(std::move(o.pinStyleChanged)),geometry(r),surface(prepare(),core::Module::map){state.setReducedMotion(appearance.reducedMotion);composed.reserve(2);}
    gpu::LayerScene&prepare(){geometry.load(blank(),options);return geometry;}
    void initialize(MapPreviewOptions&o){need(bool(o.clock),"Map owner needs the shared completion clock");time=o.clock();need(std::isfinite(time)&&time>=0,"Invalid initial Map clock");const auto weak=weak_from_this();
        raster=std::make_unique<gpu::NativeMapRaster>(utility,[weak,clock=std::move(o.clock)]{const auto now=clock();need(std::isfinite(now),"Invalid Map completion clock");if(auto self=weak.lock())return std::max(now,self->time);return now;},[weak]{if(auto self=weak.lock())self->notify();},std::move(o.paint),std::move(o.releaseWorkerCaches));
        raster->configure(appearance.dark,appearance.accent,options.pixelsPerPoint,time);raster->setGeography(geography,time);raster->update(state.viewport(),false,time);cameraRevision=state.cameraRevision();gestureRevision=state.gestureRevision();
        scene=std::make_unique<gpu::NativeMapScene>(state,*raster,paint,options,std::move(o.player),appearance);ready=true;
    }
    void notify(){if(!alive||!ready)return;auto callback=changed;if(callback)callback();}
    std::optional<Point>local(Point p)const{return input&&std::isfinite(p.x)&&std::isfinite(p.y)?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    void feedback(){if(!pose||!scene)return;scene->setFeedback(pointerPoint?local(*pointerPoint):std::nullopt,pressed,time);}
    void synchronize(const modules::MapChange&change,std::uint64_t revision){if(!current(revision))return;
        if(cameraRevision!=state.cameraRevision()){cameraRevision=state.cameraRevision();raster->update(state.viewport(),change.cameraAnimated,time);if(!current(revision))return;}
        if(gestureRevision!=state.gestureRevision()){gestureRevision=state.gestureRevision();raster->finishGesture(time);if(!current(revision))return;}
        if(pose)scene->syncContent(time,change.cameraAnimated);
        if(change.styleChanged){auto callback=pinStyleChanged;if(callback)callback(*change.styleChanged);if(!current(revision))return;}
        if(change.recentered){auto callback=recentered;if(callback)callback();if(!current(revision))return;}
        if(change.handled||change.changed)notify();
    }
    void activate(bool nextPrepared,bool nextInput){const auto revision=eventRevision;
        if(input!=nextInput){input=nextInput;pressed=false;if(!input){pointerPoint.reset();scene->settle();}synchronize(state.setActive(input),revision);if(!current(revision))return;}
        if(prepared!=nextPrepared){prepared=nextPrepared;raster->setActive(prepared,time);if(!current(revision))return;}
        feedback();
    }
};
MapPreview::MapPreview(gpu::LayerRasterizer&r,app::UtilityExecutor&e,MapPreviewOptions o):impl_(std::make_shared<Impl>(r,e,o)){impl_->initialize(o);}
MapPreview::~MapPreview(){if(impl_){impl_->alive=false;impl_->ready=false;impl_->changed={};impl_->pinStyleChanged={};impl_->recentered={};impl_->raster.reset();}}
void MapPreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Map viewport");impl_->metrics=m;}
void MapPreview::setGeography(std::shared_ptr<const modules::MapGeography>g,double t){auto i=impl_;const Impl::Event event(*i,t);if(i->geography==g)return;i->geography=std::move(g);i->appearance.terrainReady=hasTerrain(i->geography);i->scene->setAppearance(i->appearance);i->raster->setGeography(i->geography,i->time);}
bool MapPreview::setAppearance(gpu::MapAppearance a,double t){auto i=impl_;const Impl::Event event(*i,t);a=appearanceFor(std::move(a),hasTerrain(i->geography));if(!i->scene->setAppearance(a))return false;i->appearance=std::move(a);i->state.setReducedMotion(i->appearance.reducedMotion);if(i->appearance.reducedMotion)i->scene->settle();i->raster->configure(i->appearance.dark,i->appearance.accent,i->options.pixelsPerPoint,i->time);if(event.current()){i->feedback();i->notify();}return true;}
void MapPreview::setLanguage(core::Language language,double t){auto a=impl_->appearance;a.strings={core::localized("MAP","地图",language),core::localized("LOADING TERRAIN","载入地形",language),core::localized("PINS","标记",language)};setAppearance(std::move(a),t);}
void MapPreview::setOverlayVisible(bool value,double t){auto i=impl_;const Impl::Event event(*i,t);if(i->overlayVisible==value)return;i->overlayVisible=value;if(!value)i->activate(false,false);if(event.current())i->notify();}
void MapPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto i=impl_;const Impl::Event event(*i,t);need(i->metrics.pixelWidth&&i->metrics.pixelHeight,"Resize Map owner before presentation");
    const auto*shown=sample.current.module==core::Module::map?&sample.current:sample.incoming&&sample.incoming->module==core::Module::map?&*sample.incoming:nullptr;
    const bool prepared=shown&&sample.requested==core::Module::map&&i->overlayVisible;i->activate(prepared,prepared&&sample.acceptsModuleInput);if(!event.current())return;
    if(!shown){i->pose.reset();i->registration.update({});return;}
    i->scene->syncContent(i->time);i->surface.update(center,settings,*shown,opacity);i->pose=i->surface.pose();const auto&p=*i->pose;
    const auto camera=gpu::layerViewportProjection(i->metrics.pixelWidth,i->metrics.pixelHeight)*Matrix::scale(i->metrics.scale,i->metrics.scale);i->projection=core::Projection::viewport(camera*p.contentWorld,i->metrics.pixelWidth,i->metrics.pixelHeight);
    i->feedback();i->scene->updatePose({p.contentWorld,p.opacity,i->time,std::span(&p.hostClip,1),p.shutter?&*p.shutter:nullptr});i->registration.update(p.registration);
}
bool MapPreview::utilityCompleted(double t){auto i=impl_;const Impl::Event event(*i,t);return i->raster->submitPending(i->time);}
bool MapPreview::deadline(double t){auto i=impl_;const Impl::Event event(*i,t);const auto before=i->state.gestureRevision();i->synchronize(i->state.advance(i->time),event.revision);if(!event.current())return true;const bool submitted=i->raster->advance(i->time);return submitted||before!=i->state.gestureRevision();}
std::optional<double>MapPreview::nextWakeTime()const noexcept{return earlier(impl_->state.nextWakeTime(),impl_->raster->nextWakeTime());}
bool MapPreview::requiresFrames(double t)const{return impl_->pose&&impl_->overlayVisible&&impl_->scene->requiresFrames(std::max(t,impl_->time));}
bool MapPreview::covers(Point p)const{const auto q=impl_->local(p);return q&&modules::mapContains(*q);}
bool MapPreview::pointer(const app::PointerEvent&e,double t){auto i=impl_;const Impl::Event event(*i,t);i->pointerPoint=Point{e.x,e.y};const auto q=i->local(*i->pointerPoint);
    if(e.kind==app::PointerKind::captureLost){const bool owned=i->pressed||i->state.dragging();i->pressed=false;i->pointerPoint.reset();i->synchronize(i->state.up(true),event.revision);if(event.current())i->feedback();return owned;}
    if(e.kind==app::PointerKind::leave){i->pointerPoint.reset();i->feedback();return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool owned=i->pressed||i->state.dragging();i->pressed=false;i->synchronize(i->state.up(),event.revision);if(event.current())i->feedback();return owned;}
    if(!i->input)return false;
    if(e.kind==app::PointerKind::move){modules::MapChange change;if(q&&i->state.dragging())change=i->state.drag(*q);i->synchronize(change,event.revision);if(event.current())i->feedback();return change.handled||(q&&modules::mapContains(*q));}
    if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&q&&modules::mapContains(*q)){
        modules::MapChange change;if(e.button==app::PointerButton::left){i->pressed=true;i->feedback();change=i->state.down(*q);}else if(e.button==app::PointerButton::right)change=i->state.rightDown(*q);else return false;
        i->synchronize(change,event.revision);if(event.current())i->feedback();return change.handled;
    }return false;
}
bool MapPreview::wheel(const app::WheelEvent&e,double t){return scroll({e.x,e.y},e.horizontal?0:e.steps,false,false,t);}
bool MapPreview::scroll(Point p,double delta,bool precise,bool ended,double t){auto i=impl_;const Impl::Event event(*i,t);const auto q=i->local(p);if(!q&&!ended)return false;const auto result=i->state.wheel(q.value_or(Point{-1e6,-1e6}),delta,precise,ended,i->time);i->synchronize(result,event.revision);return result.handled;}
bool MapPreview::magnify(Point p,double amount,bool ended,double t){auto i=impl_;const Impl::Event event(*i,t);const auto q=i->local(p);if(!q&&!ended)return false;const auto result=i->state.magnify(q.value_or(Point{-1e6,-1e6}),amount,ended,i->time);i->synchronize(result,event.revision);return result.handled;}
bool MapPreview::key(const app::KeyEvent&e,double t){auto i=impl_;const Impl::Event event(*i,t);if(!i->input||e.kind!=app::KeyKind::down||e.alt||GetKeyState(VK_SHIFT)<0||GetKeyState(VK_CONTROL)<0||GetKeyState(VK_MENU)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0)return false;
    std::optional<modules::MapKey>key;switch(e.value){case VK_LEFT:key=modules::MapKey::left;break;case VK_RIGHT:key=modules::MapKey::right;break;case VK_UP:key=modules::MapKey::up;break;case VK_DOWN:key=modules::MapKey::down;break;case VK_ADD:case VK_OEM_PLUS:key=modules::MapKey::zoomIn;break;case VK_SUBTRACT:case VK_OEM_MINUS:key=modules::MapKey::zoomOut;break;case VK_DELETE:case VK_BACK:key=modules::MapKey::erase;break;case VK_ESCAPE:key=modules::MapKey::escape;break;default:return false;}
    const auto result=i->state.key(*key);i->synchronize(result,event.revision);return result.handled;
}
bool MapPreview::perform(modules::MapAction action,std::string_view pinID,double t){auto i=impl_;const Impl::Event event(*i,t);if(!i->input)return false;const auto result=i->state.perform(action,pinID);i->synchronize(result,event.revision);return result.handled;}
void MapPreview::cancelInteraction(double t){auto i=impl_;const Impl::Event event(*i,t);i->pressed=false;i->pointerPoint.reset();i->synchronize(i->state.up(true),event.revision);if(!event.current())return;const auto outsidePoint=Point{-1e6,-1e6};i->synchronize(i->state.wheel(outsidePoint,0,false,true,i->time),event.revision);if(event.current())i->feedback();}
bool MapPreview::pointerLocked()const noexcept{return impl_->state.pointerLocked();}
const modules::MapState&MapPreview::state()const noexcept{return impl_->state;}
gpu::NativeMapRaster::Stats MapPreview::rasterStats()const noexcept{return impl_->raster->stats();}
gpu::MapSceneStats MapPreview::sceneStats()const noexcept{return impl_->scene->stats();}
void MapPreview::upload(gpu::Renderer&r){auto&i=*impl_;if(!i.pose)return;i.registration.uploadGeometry(r);i.scene->uploadResources(r);}
std::span<const gpu::LayerCompositionEntry>MapPreview::entries(){auto&i=*impl_;i.composed.clear();if(i.pose){for(const auto&e:i.scene->entries())i.composed.push_back(e);i.composed.push_back({&i.geometry,i.registration.draws()});}return i.composed;}
void MapPreview::release(gpu::Renderer&r){auto&i=*impl_;need(i.scene->releaseResources(r)&&i.registration.releaseResources(r)&&i.geometry.releaseResources(r),"Map resources still published at teardown");}
}
#endif
