#include "tools/activity_preview.hpp"
#ifdef _WIN32
#include <windows.h>
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include <cmath>
#include <algorithm>
namespace endfield::tools {
namespace {namespace gpu=native;using Matrix=core::Matrix4;using Json=ehud::data::Json;void need(bool b,const char*m){if(!b)throw std::invalid_argument(m);}bool contains(core::Rect r,core::Point p){return r.contains(p);}Json blank(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}}
struct ActivityPreview::Impl {
    modules::ActivityState state;gpu::NativeActivityScene scene;gpu::LayerScene geometry;gpu::NativeModuleSurface surface;gpu::NativeModuleRegistration registration{"activity.registration"};
    modules::ActivityAppearance appearance;std::function<void(bool,bool,double)>demand;std::vector<gpu::LayerCompositionEntry>composed;
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;std::optional<core::Point>pointerPoint;
    bool alive{true},active{},input{},pressed{},reduced{},overlayVisible{true},hasVisibleSample{};double lastTimestamp{},time{};unsigned eventDepth{};
    struct Event{Impl&i;Event(Impl&v,double t):i(v){need(std::isfinite(t),"Activity owner needs finite time");if(!i.eventDepth)i.time=std::max(i.time,t);++i.eventDepth;}~Event(){--i.eventDepth;}};
    Impl(gpu::LayerRasterizer&r,ActivityPreviewOptions o):state(std::move(o.compareNames)),scene(state,r,o.raster,o.appearance,std::move(o.sortSamples)),geometry(r),surface(prepare(o.raster),core::Module::activityMonitor),appearance(o.appearance),demand(std::move(o.demandChanged)),reduced(o.reduceMotion){state.receive(std::move(o.initial));state.receiveApps(std::move(o.apps));composed.reserve(49);}
    gpu::LayerScene&prepare(const gpu::LayerRasterOptions&o){geometry.load(blank(),o);return geometry;}
    std::optional<core::Point>local(core::Point point)const{return input?projection.unproject({point.x*metrics.scale,point.y*metrics.scale}):std::nullopt;}
    void demandChange(){if(demand)demand(active,active&&state.showingApps(),time);}
    void feedback(){if(!pose)return;const auto q=pointerPoint?local(*pointerPoint):std::nullopt;scene.setFeedback(q,pressed,reduced,time);}
    bool action(std::string_view id){if(!active||!input)return false;bool valid{};for(const auto&a:scene.actions())valid|=id==a.id;if(!valid)return false;
        if(id=="activity:overview"||id=="activity:apps"){const bool next=id=="activity:apps";if(!state.setApps(next))return true;scene.syncContent(time,false);scene.selectPage(next,time,!reduced);demandChange();return true;}
        static constexpr std::array keys{modules::ActivitySort::name,modules::ActivitySort::cpu,modules::ActivitySort::memory,modules::ActivitySort::network,modules::ActivitySort::disk};
        for(auto key:keys)if(id.substr(std::string_view("activity:sort:").size())==modules::activitySortKey(key)){state.sort(key);scene.syncContent(time,false);scene.revealSort(state.descending()?1:-1,time,!reduced);return true;}return false;
    }
};
ActivityPreview::ActivityPreview(gpu::LayerRasterizer&r,ActivityPreviewOptions o):impl_(std::make_shared<Impl>(r,std::move(o))){}
ActivityPreview::~ActivityPreview(){if(impl_){impl_->alive=false;impl_->active=false;impl_->input=false;auto callback=std::move(impl_->demand);if(callback)try{callback(false,false,impl_->time);}catch(...){}}}
void ActivityPreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Activity viewport");impl_->metrics=m;}
void ActivityPreview::receive(modules::ActivitySnapshot s){impl_->state.receive(std::move(s));}void ActivityPreview::receiveApps(modules::ActivityAppsSnapshot s){impl_->state.receiveApps(std::move(s));}
void ActivityPreview::setAppearance(modules::ActivityAppearance a){impl_->scene.setAppearance(a);impl_->appearance=a;}void ActivityPreview::setLanguage(core::Language language){auto a=impl_->appearance;a.language=language;setAppearance(a);}void ActivityPreview::setIcons(gpu::ActivityIcons icons){impl_->scene.setIcons(std::move(icons));}
void ActivityPreview::setReduceMotion(bool value){auto i=impl_;i->reduced=value;if(value)i->scene.settle();i->feedback();}
void ActivityPreview::setOverlayVisible(bool value,double t){auto i=impl_;const Impl::Event event(*i,t);if(i->overlayVisible==value)return;i->overlayVisible=value;if(!value){i->active=false;i->input=false;i->pressed=false;i->hasVisibleSample=false;i->scene.settle();i->feedback();i->demandChange();}}
void ActivityPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto i=impl_;const Impl::Event event(*i,t);const auto*shown=sample.current.module==core::Module::activityMonitor?&sample.current:sample.incoming&&sample.incoming->module==core::Module::activityMonitor?&*sample.incoming:nullptr;const bool active=shown&&sample.requested==core::Module::activityMonitor&&i->overlayVisible;
    if(active!=i->active){i->active=active;i->input=false;i->pressed=false;i->hasVisibleSample=false;if(!active)i->scene.settle();i->demandChange();if(!i->alive)return;}
    i->input=active&&sample.acceptsModuleInput;if(!shown){i->pose.reset();i->registration.update({});return;}
    if(active||i->scene.entries().empty()){const bool animate=i->hasVisibleSample&&i->lastTimestamp!=i->state.snapshot().timestamp&&!i->reduced;i->scene.syncContent(i->time,animate);i->lastTimestamp=i->state.snapshot().timestamp;i->hasVisibleSample=true;}
    i->surface.update(center,settings,*shown,opacity);i->pose=i->surface.pose();const auto&p=*i->pose;const auto camera=gpu::layerViewportProjection(i->metrics.pixelWidth,i->metrics.pixelHeight)*Matrix::scale(i->metrics.scale,i->metrics.scale);i->projection=core::Projection::viewport(camera*p.contentWorld,i->metrics.pixelWidth,i->metrics.pixelHeight);i->feedback();i->scene.updatePose({p.contentWorld,p.opacity,i->time,std::span(&p.hostClip,1),p.shutter?&*p.shutter:nullptr});i->registration.update(p.registration);
}
bool ActivityPreview::requiresFrames(double t)const{return impl_->pose&&impl_->scene.requiresFrames(std::max(t,impl_->time));}
bool ActivityPreview::covers(core::Point p)const{const auto q=impl_->local(p);return q&&contains({0,0,400,334},*q);}
bool ActivityPreview::pointer(const app::PointerEvent&e,double t){auto i=impl_;const Impl::Event event(*i,t);i->pointerPoint=core::Point{e.x,e.y};const auto q=i->local(*i->pointerPoint);
    if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost){i->pressed=false;i->pointerPoint.reset();i->feedback();return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool owned=i->pressed;i->pressed=false;i->feedback();return owned;}
    if(!i->input)return false;if(e.kind==app::PointerKind::move){i->feedback();return q&&contains({0,0,400,334},*q);}
    if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&e.button==app::PointerButton::left&&q&&contains({0,0,400,334},*q)){i->pressed=true;i->feedback();std::string action;for(const auto&a:i->scene.actions())if(contains(a.rect,*q)){action=a.id;break;}if(!action.empty())i->action(action);return true;}return false;
}
bool ActivityPreview::perform(std::string_view action,double t){auto i=impl_;const Impl::Event event(*i,t);return i->action(action);}
bool ActivityPreview::wheel(const app::WheelEvent&e,double t){auto i=impl_;const Impl::Event event(*i,t);const auto q=i->local({e.x,e.y});if(!q||!i->state.showingApps()||!contains(modules::ActivityState::appsViewport(),*q))return false;if(e.horizontal||!std::isfinite(e.steps)||!e.linesPerStep)return true;const auto travel=e.linesPerStep==UINT32_MAX?226:12.*e.linesPerStep;return i->state.scroll(*q,-e.steps*travel);}
bool ActivityPreview::key(const app::KeyEvent&e,double t){auto i=impl_;const Impl::Event event(*i,t);if(!i->input||e.kind!=app::KeyKind::down||e.value!=VK_ESCAPE||e.alt||GetKeyState(VK_SHIFT)<0||GetKeyState(VK_CONTROL)<0||GetKeyState(VK_MENU)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0)return false;return i->state.showingApps()&&i->action("activity:overview");}
void ActivityPreview::cancelInteraction(){auto i=impl_;i->pressed=false;i->pointerPoint.reset();i->feedback();}bool ActivityPreview::pointerLocked()const noexcept{return impl_->pressed;}
const modules::ActivityState&ActivityPreview::state()const noexcept{return impl_->state;}modules::ActivityState&ActivityPreview::providerState()noexcept{return impl_->state;}
void ActivityPreview::upload(gpu::Renderer&r){auto&i=*impl_;if(!i.pose)return;i.registration.uploadGeometry(r);i.scene.uploadAnimations(r);}
std::span<const gpu::LayerCompositionEntry>ActivityPreview::entries(){auto&i=*impl_;i.composed.clear();if(i.pose){for(const auto&e:i.scene.entries())i.composed.push_back(e);i.composed.push_back({&i.geometry,i.registration.draws()});}return i.composed;}
void ActivityPreview::collected(gpu::Renderer&r){need(impl_->scene.collectRetired(r),"Activity resources still published");}void ActivityPreview::release(gpu::Renderer&r){auto&i=*impl_;need(i.scene.releaseResources(r)&&i.registration.releaseResources(r)&&i.geometry.releaseResources(r),"Activity resources still published at teardown");}gpu::ActivitySceneStats ActivityPreview::stats()const{return impl_->scene.stats();}
}
#endif
