#include "tools/volume_preview.hpp"
#ifdef _WIN32
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::tools {
namespace {namespace gpu=native;using Matrix=core::Matrix4;using Json=ehud::data::Json;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}bool contains(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Json empty(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}}
struct VolumePreview::Impl {
    gpu::VolumeController controller;gpu::NativeVolumeScene scene;gpu::LayerScene geometry;
    std::unique_ptr<gpu::NativeModuleSurface>surface;gpu::NativeModuleRegistration registration{"volume.registration"};
    std::array<gpu::LayerCompositionEntry,1>composed;app::ClientMetrics metrics;core::Projection projection;
    std::optional<gpu::ModuleSurfacePose>pose;bool active{},acceptsInput{},pressed{},reduced{},released{};
    double time{};unsigned eventDepth{};
    struct Event {Impl&i;Event(Impl&v,double requested):i(v){need(std::isfinite(requested),"Volume owner needs a finite clock");if(!i.eventDepth)i.time=std::max(i.time,requested);++i.eventDepth;}~Event(){--i.eventDepth;}};
    Impl(gpu::LayerRasterizer&r,VolumePreviewOptions options):controller(std::move(options.initial),std::move(options.actions),std::move(options.strings)),scene(controller,r,[&]{gpu::LayerRasterOptions ro;ro.pixelsPerPoint=options.rasterDensity;return ro;}(),options.style),geometry(r),reduced(options.reduceMotion){
        need(std::isfinite(options.rasterDensity)&&options.rasterDensity>=.1&&options.rasterDensity<=16,"Invalid Volume raster density");
        gpu::LayerRasterOptions ro;ro.pixelsPerPoint=options.rasterDensity;geometry.load(empty(),ro);surface=std::make_unique<gpu::NativeModuleSurface>(geometry,core::Module::volume);scene.syncContent();composed[0]={&scene.scene(),registration.draws()};
    }
    std::optional<core::Point>local(core::Point p)const{return acceptsInput?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    void content(){if(!active)return;scene.syncContent();}
    void feedback(std::optional<core::Point>point){if(active){content();scene.setFeedback(point,pressed,reduced,time);}}
};
VolumePreview::VolumePreview(gpu::LayerRasterizer&r,VolumePreviewOptions options):impl_(std::make_unique<Impl>(r,std::move(options))){}
VolumePreview::~VolumePreview()=default;
void VolumePreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Volume viewport");impl_->metrics=m;}
bool VolumePreview::receiveSnapshot(gpu::VolumeSnapshot s){return impl_->controller.receiveSnapshot(std::move(s));}
bool VolumePreview::receiveAudio(const gpu::AudioSnapshot&s){return receiveSnapshot(gpu::volumeSnapshotFromSystemAudio(s));}
void VolumePreview::setStyle(gpu::VolumeStyle s){impl_->scene.setStyle(s);}void VolumePreview::setStrings(gpu::VolumeStrings s){impl_->controller.setStrings(std::move(s));}void VolumePreview::setReduceMotion(bool value)noexcept{impl_->reduced=value;}
void VolumePreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto&i=*impl_;const Impl::Event event(i,t);
    const auto*shown=sample.current.module==core::Module::volume?&sample.current:sample.incoming&&sample.incoming->module==core::Module::volume?&*sample.incoming:nullptr;
    const bool active=shown&&sample.requested==core::Module::volume;if(active!=i.active){i.active=active;i.acceptsInput=false;i.pressed=false;i.controller.mouseUp();if(active){i.scene.retainDepartingArtwork(false);i.controller.setActive(true);}else {i.scene.retainDepartingArtwork(true);i.controller.setActive(false);}}
    i.acceptsInput=active&&sample.acceptsModuleInput;if(!i.acceptsInput){i.controller.mouseUp();i.pressed=false;}
    if(!shown){i.pose.reset();i.registration.update({});return;}i.content();i.surface->update(center,settings,*shown,opacity);i.pose=i.surface->pose();const auto&p=*i.pose;
    const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);i.projection=core::Projection::viewport(camera*p.contentWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);
    i.scene.updatePose(p.contentWorld,p.opacity,i.time,std::span(&p.hostClip,1),p.shutter?&*p.shutter:nullptr);i.registration.update(p.registration);
}
bool VolumePreview::requiresFrames(double t)const{const auto&i=*impl_;return i.pose&&i.scene.requiresFrames(std::max(t,i.time));}
bool VolumePreview::covers(core::Point p)const{const auto q=impl_->local(p);return q&&contains(gpu::VolumeController::bounds(),*q);}
bool VolumePreview::pointer(const app::PointerEvent&e,double t){auto&i=*impl_;const Impl::Event event(i,t);const auto q=i.local({e.x,e.y});
    if(e.kind==app::PointerKind::captureLost){const bool was=i.controller.dragging()||i.pressed;i.controller.mouseUp();i.pressed=false;i.feedback(q);return was;}
    if(e.kind==app::PointerKind::leave){if(!i.controller.dragging())i.pressed=false;i.feedback({});return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool was=i.controller.dragging()||i.pressed;i.controller.mouseUp();i.pressed=false;i.feedback(q);return was;}
    if(!i.acceptsInput)return false;
    if(e.kind==app::PointerKind::move){const bool dragged=i.controller.dragging();if(dragged&&q)i.controller.mouseDragged(*q);i.feedback(q);return dragged||(q&&contains(gpu::VolumeController::bounds(),*q));}
    if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left||!q||!contains(gpu::VolumeController::bounds(),*q))return false;
    i.pressed=true;i.controller.mouseDown(*q,i.time,i.reduced);i.feedback(q);return true;
}
bool VolumePreview::wheel(const app::WheelEvent&e,double t){auto&i=*impl_;const Impl::Event event(i,t);const auto q=i.local({e.x,e.y});if(!q||!contains(gpu::VolumeController::bounds(),*q))return false;
    if(e.horizontal||!std::isfinite(e.steps)||!e.linesPerStep||i.controller.dragging())return true;const double travel=e.linesPerStep==UINT32_MAX?64:12.*e.linesPerStep;i.controller.scroll(*q,-e.steps*travel,i.time,i.reduced);i.feedback(q);return true;
}
bool VolumePreview::key(const app::KeyEvent&e,double t){return key(e,e.alt||GetKeyState(VK_SHIFT)<0||GetKeyState(VK_CONTROL)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0,t);}
bool VolumePreview::key(const app::KeyEvent&e,bool modified,double t){auto&i=*impl_;const Impl::Event event(i,t);if(!i.acceptsInput||e.kind!=app::KeyKind::down)return false;const bool used=i.controller.key(e.value,modified,i.time,i.reduced);i.content();return used;}
bool VolumePreview::pointerLocked()const noexcept{return impl_->controller.dragging();}
void VolumePreview::cancelInteraction(double t){auto&i=*impl_;const Impl::Event event(i,t);i.controller.mouseUp();i.pressed=false;i.feedback({});}
const gpu::VolumeController&VolumePreview::state()const noexcept{return impl_->controller;}
void VolumePreview::upload(gpu::Renderer&r){auto&i=*impl_;if(i.pose){i.scene.scene().uploadResources(r);i.registration.uploadGeometry(r);}}
std::span<const gpu::LayerCompositionEntry>VolumePreview::entries(){auto&i=*impl_;if(!i.pose)return {};return i.composed;}
void VolumePreview::collected(gpu::Renderer&r){impl_->scene.scene().collectRetiredResources(r);}
void VolumePreview::release(gpu::Renderer&r){auto&i=*impl_;need(i.scene.scene().releaseResources(r)&&i.registration.releaseResources(r),"Detach Volume composition before releasing borrowed resources");i.released=true;}
gpu::VolumeCallbacks volumeCallbacksFromSystemServices(gpu::SystemServices&service,std::function<void(bool)> activation){gpu::VolumeCallbacks c;c.setActive=std::move(activation);
    const auto current=[&service](std::string_view id){const auto&s=service.audio();if(!s.available||s.paused)return false;const auto&endpoint=s.controlled_device_id.empty()?s.default_device_id:s.controlled_device_id;if(endpoint.size()>32767||id.size()>4096)return false;std::array<char,4096>encoded{};const auto length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,endpoint.data(),static_cast<int>(endpoint.size()),encoded.data(),static_cast<int>(encoded.size()),nullptr,nullptr);return length>0&&std::string_view(encoded.data(),static_cast<std::size_t>(length))==id;};
    c.setVolume=[&service,current](auto endpoint,double value){return std::isfinite(value)&&value>=0&&value<=1&&current(endpoint)&&SUCCEEDED(service.set_master_volume(static_cast<float>(value)));};
    c.setMute=[&service,current](auto endpoint,bool value){return current(endpoint)&&SUCCEEDED(service.set_master_mute(value));};
    c.setBalance=[&service,current](auto endpoint,double value){return std::isfinite(value)&&value>=-1&&value<=1&&current(endpoint)&&SUCCEEDED(service.set_output_balance(static_cast<float>(value)));};
    c.setAppGain=[&service](auto application,double value){return std::isfinite(value)&&value>=0&&value<=1&&SUCCEEDED(service.set_application_gain(std::string(application),static_cast<float>(value)));};
    c.stopApp=[&service](auto application){return SUCCEEDED(service.stop_application(std::string(application)));};c.stopAllApps=[&service]{(void)service.stop_applications();};return c;
}
}
#endif
