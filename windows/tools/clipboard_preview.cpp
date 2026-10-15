#include "tools/clipboard_preview.hpp"
#include "modules/module_strings.hpp"
#ifdef _WIN32
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include "native/layer_image_source.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::tools {
namespace {using Matrix=core::Matrix4;using Json=ehud::data::Json;namespace gpu=native;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
bool contains(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Json blank(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
// Bounding rect of a canvas rect's projected corners, in logical client points.
std::optional<core::Rect>projected(const core::Projection&projection,double scale,core::Rect r){
    if(!std::isfinite(r.x)||!std::isfinite(r.y)||!std::isfinite(r.width)||!std::isfinite(r.height)||r.width<0||r.height<0||!(scale>0))return std::nullopt;
    double x0=INFINITY,y0=INFINITY,x1=-INFINITY,y1=-INFINITY;
    for(const core::Point c:{core::Point{r.x,r.y},core::Point{r.x+r.width,r.y},core::Point{r.x,r.y+r.height},core::Point{r.x+r.width,r.y+r.height}}){
        const auto q=projection.project(c);if(!q||!std::isfinite(q->x)||!std::isfinite(q->y))return std::nullopt;
        x0=std::min(x0,q->x);y0=std::min(y0,q->y);x1=std::max(x1,q->x);y1=std::max(y1,q->y);}
    return core::Rect{x0/scale,y0/scale,(x1-x0)/scale,(y1-y0)/scale};}
}
struct ClipboardPreview::Impl {
    gpu::ClipboardState state;gpu::NativeClipboardScene scene;gpu::LayerScene geometry;gpu::NativeModuleSurface surface;gpu::NativeModuleRegistration registration{"clipboard.registration"};
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;bool active{},input{},pressed{};std::optional<core::Point>pointerPoint;double time{};unsigned eventDepth{};std::vector<gpu::LayerCompositionEntry>composed;
    core::Language language{core::Language::simplifiedChinese}; // ClipboardStrings{} are the Chinese source strings
    struct Event {Impl&i;Event(Impl&v,double t):i(v){need(std::isfinite(t),"Clipboard needs finite owner time");if(!i.eventDepth)i.time=std::max(i.time,t);++i.eventDepth;}~Event(){--i.eventDepth;}};
    // The raster artwork (and the row thumbnails it borrowed from the
    // provider's image source) is released once the HUD closes while the
    // Clipboard is not shown (setOverlayVisible(false), the next publication
    // after it or conceal()), never on a module switch inside an open HUD.
    // renderer: the host's, from upload()/collected().
    gpu::LayerImageSource*thumbnails{};gpu::Renderer*renderer{};bool parkWanted{};
    bool park(){if(pose||!renderer||!scene.park(*renderer))return false;(void)registration.releaseResources(*renderer);parkWanted=false;
        // The rows rebuilt on the next showing publish their thumbnails again
        // (ClipboardActions::prepareImageRows) from the shared history.
        if(thumbnails)thumbnails->clear();return true;}
    Impl(gpu::LayerRasterizer&r,gpu::LayerRasterOptions options,gpu::ClipboardActions a,gpu::ClipboardStrings strings,gpu::ClipboardAppearance appearance,gpu::ClipboardImages images,std::shared_ptr<const core::SubsectionMaskSampler>mask):state(std::move(a),std::move(strings)),scene(state,r,options,appearance,std::move(images),std::move(mask)),geometry(r),surface(prepareGeometry(options),core::Module::clipboard),thumbnails(options.memoryImages){composed.reserve(12);}
    gpu::LayerScene&prepareGeometry(const gpu::LayerRasterOptions&o){geometry.load(blank(),o);return geometry;}
    std::optional<core::Point>local(core::Point p)const{return input?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
};
ClipboardPreview::ClipboardPreview(gpu::LayerRasterizer&r,gpu::LayerRasterOptions o,gpu::ClipboardActions a,gpu::ClipboardStrings s,gpu::ClipboardAppearance appearance,gpu::ClipboardImages images,std::shared_ptr<const core::SubsectionMaskSampler>mask):impl_(std::make_unique<Impl>(r,std::move(o),std::move(a),std::move(s),appearance,std::move(images),std::move(mask))){}
ClipboardPreview::~ClipboardPreview()=default;
void ClipboardPreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Clipboard viewport");impl_->metrics=m;}
void ClipboardPreview::refresh(){impl_->state.refresh();}
void ClipboardPreview::setLanguage(core::Language language){impl_->state.setStrings(modules::clipboardStrings(language));impl_->language=language;}
void ClipboardPreview::setAppearance(gpu::ClipboardAppearance a,gpu::ClipboardImages b){impl_->scene.setAppearance(a,std::move(b));}
void ClipboardPreview::setReduceMotion(bool value){impl_->state.setReduceMotion(value);}
void ClipboardPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto&i=*impl_;const Impl::Event event(i,t);
    const auto*shown=sample.current.module==core::Module::clipboard?&sample.current:sample.incoming&&sample.incoming->module==core::Module::clipboard?&*sample.incoming:nullptr;const bool active=shown&&sample.requested==core::Module::clipboard;
    if(active!=i.active){i.active=active;if(active)i.state.activate();else{i.state.deactivate();i.pressed=false;i.scene.setFeedback({},false,i.time);}}i.input=active&&sample.acceptsModuleInput;
    if(!shown){i.pose.reset();i.registration.update({});return;}if(sample.requested==core::Module::clipboard)i.parkWanted=false; // the selected module keeps its artwork across a close
    i.scene.syncContent(i.time);i.surface.update(center,settings,*shown,opacity);i.pose=i.surface.pose();const auto&p=*i.pose;
    const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);i.projection=core::Projection::viewport(camera*p.contentWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);
    if(i.pointerPoint){const auto local=i.local(*i.pointerPoint);i.scene.setFeedback(local?i.state.feedbackActionAt(*local):std::nullopt,i.pressed,i.time);}
    i.scene.updatePose({p.contentWorld,p.opacity,i.time,std::span(&p.hostClip,1),p.shutter});i.registration.update(p.registration);
}
bool ClipboardPreview::requiresFrames(double t)const{return impl_->pose&&impl_->scene.requiresFrames(std::max(t,impl_->time));}
bool ClipboardPreview::covers(core::Point p)const{const auto q=impl_->local(p);return q&&contains(gpu::ClipboardState::bounds(),*q);}
bool ClipboardPreview::pointer(const app::PointerEvent&e,double t){auto&i=*impl_;const Impl::Event event(i,t);i.pointerPoint=core::Point{e.x,e.y};const auto q=i.local(*i.pointerPoint);
    if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost){i.pressed=false;i.pointerPoint.reset();if(i.pose)i.scene.setFeedback({},false,i.time);return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool owned=i.pressed;i.pressed=false;if(i.pose)i.scene.setFeedback(q?i.state.feedbackActionAt(*q):std::nullopt,false,i.time);return owned;}
    if(!i.input)return false;if(e.kind==app::PointerKind::move){i.scene.setFeedback(q?i.state.feedbackActionAt(*q):std::nullopt,i.pressed,i.time);return q&&contains(gpu::ClipboardState::bounds(),*q);}
    if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&e.button==app::PointerButton::left&&q&&contains(gpu::ClipboardState::bounds(),*q)){i.pressed=true;const auto id=i.state.feedbackActionAt(*q);const auto owned=id?std::optional(std::string(*id)):std::nullopt;i.scene.setFeedback(owned?std::optional<std::string_view>(*owned):std::nullopt,true,i.time);i.state.mouseDown(*q);return true;}return false;
}
bool ClipboardPreview::wheel(const app::WheelEvent&e,double t){auto&i=*impl_;const Impl::Event event(i,t);const auto q=i.local({e.x,e.y});if(!q||!contains(gpu::ClipboardState::contentRect(),*q))return false;if(e.horizontal||!std::isfinite(e.steps)||!e.linesPerStep)return true;const auto travel=e.linesPerStep==UINT32_MAX?246:12.*e.linesPerStep;return i.state.scroll(*q,-e.steps*travel);}
bool ClipboardPreview::key(const app::KeyEvent&e,double t){return key(e,e.alt||GetKeyState(VK_SHIFT)<0||GetKeyState(VK_CONTROL)<0||GetKeyState(VK_MENU)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0,t);}
bool ClipboardPreview::key(const app::KeyEvent&e,bool modified,double t){auto&i=*impl_;const Impl::Event event(i,t);if(!i.input||e.kind!=app::KeyKind::down||modified)return false;
    switch(e.value){case VK_DOWN:i.state.selectNext(1);break;case VK_UP:i.state.selectNext(-1);break;case VK_RETURN:i.state.copySelection();break;case VK_DELETE:case VK_BACK:i.state.deleteSelection();break;default:if(e.value<'1'||e.value>'6')return false;i.state.copyVisibleItem(e.value-'1');}return true;
}
void ClipboardPreview::setOverlayVisible(bool value,double t){auto&i=*impl_;const Impl::Event event(i,t);i.parkWanted=!value;if(!value){cancelInteraction();(void)i.park();}}
void ClipboardPreview::conceal()noexcept{try{(void)impl_->park();}catch(...){}}
void ClipboardPreview::cancelInteraction(){impl_->pressed=false;impl_->pointerPoint.reset();if(impl_->pose)impl_->scene.setFeedback({},false,impl_->time);}bool ClipboardPreview::pointerLocked()const{return impl_->pressed;}
const gpu::ClipboardState&ClipboardPreview::state()const{return impl_->state;}
std::optional<core::Rect>ClipboardPreview::clientRect(core::Rect r)const{const auto&i=*impl_;if(!i.input||!i.pose)return std::nullopt;return projected(i.projection,i.metrics.scale,r);}
const app::ClientMetrics&ClipboardPreview::clientMetrics()const noexcept{return impl_->metrics;}
core::Language ClipboardPreview::language()const noexcept{return impl_->language;}
bool ClipboardPreview::perform(std::string_view id,double t){auto&i=*impl_;const Impl::Event event(i,t);if(!i.input)return false;const auto actions=i.state.actions();
    if(std::none_of(actions.begin(),actions.end(),[&](const auto&a){return a.id==id;}))return false;const std::string owned(id);i.state.perform(owned);return true;}
void ClipboardPreview::upload(gpu::Renderer&r){auto&i=*impl_;i.renderer=&r;if(!i.pose)return;i.registration.uploadGeometry(r);i.scene.uploadAnimations(r);}
std::span<const gpu::LayerCompositionEntry>ClipboardPreview::entries(){auto&i=*impl_;i.composed.clear();if(i.pose){for(const auto&e:i.scene.entries())i.composed.push_back(e);i.composed.push_back({&i.geometry,i.registration.draws()});}return i.composed;}
void ClipboardPreview::collected(gpu::Renderer&r){auto&i=*impl_;need(i.scene.collectRetired(r),"Clipboard old artwork still published");
    // After a HUD close began, a Clipboard that this publication no longer
    // composes releases its artwork (showing it again rebuilds it).
    i.renderer=&r;if(i.parkWanted)(void)i.park();}
void ClipboardPreview::release(gpu::Renderer&r){auto&i=*impl_;need(i.scene.releaseResources(r)&&i.registration.releaseResources(r)&&i.geometry.releaseResources(r),"Clipboard resources still published at teardown");}
gpu::ClipboardSceneStats ClipboardPreview::stats()const{return impl_->scene.stats();}
}
#endif
