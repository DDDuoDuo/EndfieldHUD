#include "tools/clipboard_preview.hpp"
#ifdef _WIN32
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::tools {
namespace {using Matrix=core::Matrix4;using Json=ehud::data::Json;namespace gpu=native;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
bool contains(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Json blank(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
}
struct ClipboardPreview::Impl {
    gpu::ClipboardState state;gpu::NativeClipboardScene scene;gpu::LayerScene geometry;gpu::NativeModuleSurface surface;gpu::NativeModuleRegistration registration{"clipboard.registration"};
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;bool active{},input{},pressed{};std::optional<core::Point>pointerPoint;double time{};unsigned eventDepth{};std::vector<gpu::LayerCompositionEntry>composed;
    struct Event {Impl&i;Event(Impl&v,double t):i(v){need(std::isfinite(t),"Clipboard needs finite owner time");if(!i.eventDepth)i.time=std::max(i.time,t);++i.eventDepth;}~Event(){--i.eventDepth;}};
    Impl(gpu::LayerRasterizer&r,gpu::LayerRasterOptions options,gpu::ClipboardActions a,gpu::ClipboardStrings strings,gpu::ClipboardAppearance appearance,gpu::ClipboardImages images,std::shared_ptr<const core::SubsectionMaskSampler>mask):state(std::move(a),std::move(strings)),scene(state,r,options,appearance,std::move(images),std::move(mask)),geometry(r),surface(prepareGeometry(options),core::Module::clipboard){composed.reserve(12);}
    gpu::LayerScene&prepareGeometry(const gpu::LayerRasterOptions&o){geometry.load(blank(),o);return geometry;}
    std::optional<core::Point>local(core::Point p)const{return input?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
};
ClipboardPreview::ClipboardPreview(gpu::LayerRasterizer&r,gpu::LayerRasterOptions o,gpu::ClipboardActions a,gpu::ClipboardStrings s,gpu::ClipboardAppearance appearance,gpu::ClipboardImages images,std::shared_ptr<const core::SubsectionMaskSampler>mask):impl_(std::make_unique<Impl>(r,std::move(o),std::move(a),std::move(s),appearance,std::move(images),std::move(mask))){}
ClipboardPreview::~ClipboardPreview()=default;
void ClipboardPreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Clipboard viewport");impl_->metrics=m;}
void ClipboardPreview::refresh(){impl_->state.refresh();}
void ClipboardPreview::setAppearance(gpu::ClipboardAppearance a,gpu::ClipboardImages b){impl_->scene.setAppearance(a,std::move(b));}
void ClipboardPreview::setReduceMotion(bool value){impl_->state.setReduceMotion(value);}
void ClipboardPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto&i=*impl_;const Impl::Event event(i,t);
    const auto*shown=sample.current.module==core::Module::clipboard?&sample.current:sample.incoming&&sample.incoming->module==core::Module::clipboard?&*sample.incoming:nullptr;const bool active=shown&&sample.requested==core::Module::clipboard;
    if(active!=i.active){i.active=active;if(active)i.state.activate();else{i.state.deactivate();i.pressed=false;i.scene.setFeedback({},false,i.time);}}i.input=active&&sample.acceptsModuleInput;
    if(!shown){i.pose.reset();i.registration.update({});return;}i.scene.syncContent(i.time);i.surface.update(center,settings,*shown,opacity);i.pose=i.surface.pose();const auto&p=*i.pose;
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
bool ClipboardPreview::key(const app::KeyEvent&e,double t){auto&i=*impl_;const Impl::Event event(i,t);if(!i.input||e.kind!=app::KeyKind::down||e.alt||GetKeyState(VK_SHIFT)<0||GetKeyState(VK_CONTROL)<0||GetKeyState(VK_MENU)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0)return false;
    switch(e.value){case VK_DOWN:i.state.selectNext(1);break;case VK_UP:i.state.selectNext(-1);break;case VK_RETURN:i.state.copySelection();break;case VK_DELETE:case VK_BACK:i.state.deleteSelection();break;default:if(e.value<'1'||e.value>'6')return false;i.state.copyVisibleItem(e.value-'1');}return true;
}
void ClipboardPreview::cancelInteraction(){impl_->pressed=false;impl_->pointerPoint.reset();if(impl_->pose)impl_->scene.setFeedback({},false,impl_->time);}bool ClipboardPreview::pointerLocked()const{return impl_->pressed;}
const gpu::ClipboardState&ClipboardPreview::state()const{return impl_->state;}
void ClipboardPreview::upload(gpu::Renderer&r){auto&i=*impl_;if(!i.pose)return;i.registration.uploadGeometry(r);i.scene.uploadAnimations(r);}
std::span<const gpu::LayerCompositionEntry>ClipboardPreview::entries(){auto&i=*impl_;i.composed.clear();if(i.pose){for(const auto&e:i.scene.entries())i.composed.push_back(e);i.composed.push_back({&i.geometry,i.registration.draws()});}return i.composed;}
void ClipboardPreview::collected(gpu::Renderer&r){need(impl_->scene.collectRetired(r),"Clipboard old artwork still published");}
void ClipboardPreview::release(gpu::Renderer&r){auto&i=*impl_;need(i.scene.releaseResources(r)&&i.registration.releaseResources(r)&&i.geometry.releaseResources(r),"Clipboard resources still published at teardown");}
gpu::ClipboardSceneStats ClipboardPreview::stats()const{return impl_->scene.stats();}
}
#endif
