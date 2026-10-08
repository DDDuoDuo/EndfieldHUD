#include "tools/settings_preview.hpp"
#ifdef _WIN32
#include <windows.h>
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include "native/settings_safety.hpp"
#include <algorithm>
#include <cmath>
namespace endfield::tools {namespace {
using Matrix=core::Matrix4;using Json=ehud::data::Json;namespace gpu=native;
constexpr std::array<core::Module,4>modules{core::Module::system,core::Module::display,core::Module::hotkeys,core::Module::about};
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}Json blank(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}bool contains(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
}
struct SettingsPreview::Impl {
 struct Slot {modules::SettingsPresentation state;std::unique_ptr<gpu::NativeSettingsScene>scene;gpu::LayerScene geometry;gpu::NativeModuleSurface surface;gpu::NativeModuleRegistration registration;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;bool active{},input{};
 Slot(core::Module m,modules::SettingsController&c,gpu::LayerRasterizer&r,const SettingsPreviewOptions&o):state(m,c,o.icons,o.about,o.viewHooks),geometry(r),surface(prepare(o.raster),m),registration("settings.registration."+std::to_string(static_cast<unsigned>(m))){}
 gpu::LayerScene&prepare(const gpu::LayerRasterOptions&o){geometry.load(blank(),o);return geometry;}
 };
 gpu::LayerRasterizer&raster;SettingsPreviewOptions options;modules::SettingsController controller;modules::SettingsSafety safety;std::unique_ptr<gpu::NativeSettingsSafety>safetyScene;std::array<std::unique_ptr<Slot>,4>slots;app::ClientMetrics metrics;bool overlay{true},reduced{},pressed{};Slot*captured{};std::optional<core::Point>point;double time{};std::vector<gpu::LayerCompositionEntry>composed;std::array<unsigned,2>order{};std::size_t orderCount{};
 Impl(gpu::LayerRasterizer&r,SettingsPreviewOptions o):raster(r),options(std::move(o)),controller(options.initial,options.callbacks,options.language),safety(controller){for(std::size_t n=0;n<slots.size();++n)slots[n]=std::make_unique<Slot>(modules[n],controller,r,options);composed.reserve(44);}
 void clock(double t){need(std::isfinite(t),"Settings needs finite owner time");time=std::max(time,t);}
 std::optional<core::Point>local(const Slot&s,core::Point p)const{return s.input?s.projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
 Slot*input(){for(auto&s:slots)if(s->input)return s.get();return nullptr;}
 void cancel(){if(captured)captured->state.mouseUp(time);captured=nullptr;pressed=false;point.reset();for(auto&s:slots)if(s->scene)s->scene->setFeedback({},false,time);}
};
SettingsPreview::SettingsPreview(gpu::LayerRasterizer&r,SettingsPreviewOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}
SettingsPreview::~SettingsPreview()=default;
void SettingsPreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Settings viewport");impl_->metrics=m;}
void SettingsPreview::setAppearance(modules::SettingsAppearance a){auto&i=*impl_;i.options.appearance=a;for(auto&s:i.slots)if(s->scene)s->scene->setAppearance(a);if(i.safetyScene)i.safetyScene->setAppearance(a);}
void SettingsPreview::setLanguage(core::Language l){impl_->controller.setLanguage(l);}
void SettingsPreview::setReduceMotion(bool v,double t){auto&i=*impl_;i.clock(t);i.reduced=v;i.safety.setReduceMotion(v,i.time);for(auto&s:i.slots)s->state.setReduceMotion(v,i.time);}
void SettingsPreview::setOverlayVisible(bool value,double t){auto&i=*impl_;i.clock(t);if(i.overlay==value)return;i.overlay=value;if(!value){i.cancel();for(auto&s:i.slots){s->input=false;if(s->active){s->state.deactivate(i.time);s->active=false;}}i.controller.close();i.safety.hideImmediately(i.time);}}
void SettingsPreview::wake(double t){auto&i=*impl_;i.clock(t);i.controller.wake(i.time);i.safety.refresh(i.time);}
std::optional<double>SettingsPreview::nextWakeTime(double t)const{return impl_->controller.nextWakeTime(t);}
void SettingsPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto&i=*impl_;i.clock(t);i.orderCount=0;for(auto&s:i.slots){s->pose.reset();s->input=false;}
 for(std::size_t index=0;index<i.slots.size();++index){auto&s=*i.slots[index];const auto module=modules[index];const auto*shown=sample.current.module==module?&sample.current:sample.incoming&&sample.incoming->module==module?&*sample.incoming:nullptr;const bool active=i.overlay&&shown&&sample.requested==module;if(active!=s.active){s.active=active;if(active)s.state.activate(i.time);else{s.state.deactivate(i.time);if(i.captured==&s){i.captured=nullptr;i.pressed=false;}if(s.scene)s.scene->setFeedback({},false,i.time);}}s.input=active&&sample.acceptsModuleInput;
 if(!shown){s.registration.update({});continue;}s.state.refresh(i.time);if(!s.scene)s.scene=std::make_unique<gpu::NativeSettingsScene>(s.state,i.raster,i.options.raster,i.options.images,i.options.appearance);s.scene->syncContent(i.time);s.surface.update(center,settings,*shown,opacity);s.pose=s.surface.pose();const auto&p=*s.pose;const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);s.projection=core::Projection::viewport(camera*p.contentWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);if(i.point){const auto local=i.local(s,*i.point);s.scene->setFeedback(local?s.state.actionAt(*local):std::nullopt,i.pressed,i.time);}s.scene->updatePose({p.contentWorld,p.opacity,i.time,std::span(&p.hostClip,1),p.shutter});s.registration.update(p.registration);
 }
 // Preserve ModulePresentation's outgoing-then-incoming order, independent of
 // the four semantic page enumeration values.
 for(const auto*shown:{&sample.current,sample.incoming?&*sample.incoming:nullptr})if(shown)for(unsigned n=0;n<i.slots.size();++n)if(i.slots[n]->pose&&modules[n]==shown->module)i.order[i.orderCount++]=n;
 if(i.overlay)i.safety.refresh(i.time);if(i.safety.modal()){if(!i.safetyScene)i.safetyScene=std::make_unique<gpu::NativeSettingsSafety>(i.safety,i.raster,i.options.raster,i.options.appearance);const core::Rect viewport{0,0,i.metrics.width,i.metrics.height};i.safetyScene->syncContent(viewport,i.time);const auto layout=core::source::DesktopChromeLayout::make(settings,center,{});const auto transform=modules::SettingsSafety::centeredSourceTransform(center,layout.designScale);i.safetyScene->updatePose(viewport,transform,i.time);}

}
bool SettingsPreview::requiresFrames(double t)const{const auto&i=*impl_;if(!i.overlay)return false;if(i.safety.requiresFrames(std::max(t,i.time)))return true;for(const auto&s:i.slots)if(s->pose&&s->scene->requiresFrames(std::max(t,i.time)))return true;return false;}
bool SettingsPreview::covers(core::Point p)const{const auto&i=*impl_;if(i.safety.modal())return true;for(const auto&s:i.slots)if(const auto q=i.local(*s,p);q&&contains(modules::SettingsPresentation::bounds(),*q))return true;return false;}
bool SettingsPreview::pointer(const app::PointerEvent&e,double t){auto&i=*impl_;i.clock(t);i.point=core::Point{e.x,e.y};if(i.safety.modal()){const auto action=i.safetyScene?i.safetyScene->actionAt(*i.point):std::nullopt;if(e.kind==app::PointerKind::down&&e.button==app::PointerButton::left)i.safety.pointerDown(action,i.time);else if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left)i.safety.pointerUp(action,i.time);else if(e.kind==app::PointerKind::move)i.safety.pointerMove(action,i.time);else if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost){i.safety.pointerUp({},i.time);i.safety.pointerMove({},i.time);}return true;}if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost){i.cancel();return false;}auto*s=i.captured?i.captured:i.input();if(!s)return false;const auto q=i.local(*s,*i.point);
 if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool owned=i.pressed;s->state.mouseUp(i.time);i.captured=nullptr;i.pressed=false;if(s->scene)s->scene->setFeedback(q?s->state.actionAt(*q):std::nullopt,false,i.time);return owned;}
 if(e.kind==app::PointerKind::move){if(i.captured&&q)s->state.mouseDragged(*q,i.time);s->scene->setFeedback(q?s->state.actionAt(*q):std::nullopt,i.pressed,i.time);return q&&contains(modules::SettingsPresentation::bounds(),*q);}
 if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&e.button==app::PointerButton::left&&q&&contains(modules::SettingsPresentation::bounds(),*q)){const auto action=s->state.actionAt(*q);const auto owned=action?std::optional(std::string(*action)):std::nullopt;i.pressed=true;s->scene->setFeedback(owned?std::optional<std::string_view>(*owned):std::nullopt,true,i.time);s->state.mouseDown(*q,i.time);if(s->state.dragging())i.captured=s;return true;}return false;
}
bool SettingsPreview::wheel(const app::WheelEvent&e,double t){auto&i=*impl_;i.clock(t);if(i.safety.modal())return true;auto*s=i.input();if(!s)return false;const auto q=i.local(*s,{e.x,e.y});if(!q||!contains(modules::SettingsPresentation::viewport(),*q))return false;if(e.horizontal||!std::isfinite(e.steps)||!e.linesPerStep)return true;return s->state.scroll(*q,-e.steps*(e.linesPerStep==UINT32_MAX?250:12.*e.linesPerStep),i.time);}
bool SettingsPreview::key(const app::KeyEvent&e,double t){std::uint32_t modifiers{};if(GetKeyState(VK_MENU)<0)modifiers|=1;if(GetKeyState(VK_CONTROL)<0)modifiers|=2;if(GetKeyState(VK_SHIFT)<0)modifiers|=4;if(GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0)modifiers|=8;return key(e,modifiers,t);}
bool SettingsPreview::key(const app::KeyEvent&e,std::uint32_t modifiers,double t){auto&i=*impl_;i.clock(t);if(i.safety.modal()){if(e.kind==app::KeyKind::down)i.safety.key(e.value,e.previouslyDown,i.time);return true;}auto*s=i.input();if(!s||e.kind!=app::KeyKind::down)return false;if(i.controller.capturingShortcut()){if(e.value==VK_SHIFT||e.value==VK_CONTROL||e.value==VK_MENU||e.value==VK_LWIN||e.value==VK_RWIN)return true;return s->state.capture({e.value,modifiers},e.previouslyDown,e.value==VK_ESCAPE,i.time);}if(modifiers)return false;if(e.value==VK_ESCAPE)return s->state.escape(i.time);if(e.value==VK_LEFT||e.value==VK_RIGHT){s->state.nudgeSlider(e.value==VK_LEFT?-1:1,i.time);return true;}if(e.value==VK_NEXT||e.value==VK_PRIOR||e.value==VK_UP||e.value==VK_DOWN)return s->state.scroll({200,100},e.value==VK_NEXT?250:e.value==VK_PRIOR?-250:e.value==VK_UP?-32:32,i.time);return false;}
void SettingsPreview::cancelInteraction(double t){impl_->clock(t);impl_->cancel();}
bool SettingsPreview::pointerLocked()const noexcept{return impl_->captured||impl_->controller.capturingShortcut();}
modules::SettingsController&SettingsPreview::controller()noexcept{return impl_->controller;}
const modules::SettingsPresentation*SettingsPreview::view(core::Module m)const noexcept{for(const auto&s:impl_->slots)if(s->state.module()==m)return &s->state;return nullptr;}
void SettingsPreview::setCustomColor(std::array<double,3>color,double t){auto&i=*impl_;i.clock(t);i.slots[1]->state.setCustomColor(color,i.time);}
void SettingsPreview::setCustomLogo(std::string revision,double t){auto&i=*impl_;i.clock(t);i.slots[1]->state.setCustomLogo(std::move(revision),i.time);}
void SettingsPreview::showImportError(std::string error,double t){auto&i=*impl_;i.clock(t);i.slots[1]->state.showImportError(std::move(error),i.time);}
void SettingsPreview::upload(gpu::Renderer&r){auto&i=*impl_;for(std::size_t n=0;n<i.orderCount;++n){auto&s=*i.slots[i.order[n]];s.registration.uploadGeometry(r);s.scene->uploadAnimations(r);}if(i.safetyScene&&i.safety.modal())i.safetyScene->upload(r);}
std::span<const gpu::LayerCompositionEntry>SettingsPreview::entries(){auto&i=*impl_;i.composed.clear();for(std::size_t n=0;n<i.orderCount;++n){auto&s=*i.slots[i.order[n]];for(const auto&e:s.scene->entries())i.composed.push_back(e);i.composed.push_back({&s.geometry,s.registration.draws()});}if(i.safetyScene&&i.safety.modal())i.composed.push_back(i.safetyScene->entry());return i.composed;}
void SettingsPreview::collected(gpu::Renderer&r){auto&i=*impl_;for(auto&s:i.slots)if(s->scene){need(s->scene->collectRetired(r),"Settings old artwork still published");if(!s->pose){need(s->scene->releaseResources(r),"Settings inactive artwork still published");s->scene.reset();need(s->registration.releaseResources(r),"Settings inactive registration still published");}}if(i.safetyScene&&!i.safety.modal()){need(i.safetyScene->releaseResources(r),"Settings safety still published");i.safetyScene.reset();}}
void SettingsPreview::release(gpu::Renderer&r){auto&i=*impl_;for(auto&s:i.slots){if(s->scene)need(s->scene->releaseResources(r),"Settings artwork still published at teardown");need(s->registration.releaseResources(r)&&s->geometry.releaseResources(r),"Settings module resources still published");}if(i.safetyScene)need(i.safetyScene->releaseResources(r),"Settings safety still published at teardown");}
}
#endif
