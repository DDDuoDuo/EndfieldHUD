#include "tools/storage_preview.hpp"
#ifdef _WIN32
#include "native/module_registration.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::tools {
namespace {namespace gpu=native;using Matrix=core::Matrix4;using J=ehud::data::Json;
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
J empty(){return J::Object{{"bounds",J::Array{0,0,400,334}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
app::UtilityExecutor&queue(StoragePreviewOptions&o){need(o.utility&&bool(o.readCapacity),"Storage preview requires explicit shared capacity provider");return *o.utility;}
}
struct StoragePreview::Impl {
    modules::StorageController controller;modules::StoragePresentation presentation;
    gpu::StorageCapacityProbe probe;std::unique_ptr<gpu::StorageDetailsProbe>details;gpu::NativeStorageScene scene;gpu::LayerScene geometry;
    std::unique_ptr<gpu::NativeModuleSurface>surface;gpu::NativeModuleRegistration registration{"storage.registration"};
    std::array<gpu::LayerCompositionEntry,1>composed;std::function<void()>settingsAction;
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;std::optional<core::Point>lastPointer;
    std::uint64_t observedController{};bool visible{true},requested{},active{},input{},pressed{},reduced{};double time{};
    Impl(gpu::LayerRasterizer&r,StoragePreviewOptions o):presentation({},o.appearance),probe(controller,queue(o),o.readCapacity),scene(presentation,r,[&]{gpu::LayerRasterOptions v;v.pixelsPerPoint=o.rasterDensity;return v;}(),std::move(o.paths)),geometry(r),settingsAction(std::move(o.openSettings)),reduced(o.reduceMotion){
        need(std::isfinite(o.rasterDensity)&&o.rasterDensity>=.25&&o.rasterDensity<=4,"Invalid Storage raster density");need(bool(o.scanDetails)==bool(o.completionClock),"Storage details require both scanner and owner completion clock");if(o.scanDetails)details=std::make_unique<gpu::StorageDetailsProbe>(controller,*o.utility,std::move(o.scanDetails),std::move(o.completionClock));gpu::LayerRasterOptions ro;ro.pixelsPerPoint=o.rasterDensity;geometry.load(empty(),ro);surface=std::make_unique<gpu::NativeModuleSurface>(geometry,core::Module::storage);scene.syncContent(0);composed[0]={&scene.scene(),registration.draws()};
    }
    void clock(double t){need(std::isfinite(t),"Storage owner needs finite caller time");time=std::max(time,t);}
    void activate(){const bool next=visible&&requested;if(next==active)return;active=next;pressed=false;input=false;lastPointer.reset();if(active){controller.activate(time);probe.submitPending();content();}else controller.deactivate();scene.setActivity(active,reduced,time);}
    bool content(){if(!active)return false;bool changed{};if(observedController!=controller.revision()){changed=presentation.receive(controller.snapshot());observedController=controller.revision();}scene.syncContent(time);scene.setFeedback(lastPointer,pressed,time);return changed;}
    std::optional<core::Point>local(core::Point p)const{return input?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    void feedback(std::optional<core::Point>point){lastPointer=point;if(active)scene.setFeedback(point,pressed,time);}
};
StoragePreview::StoragePreview(gpu::LayerRasterizer&r,StoragePreviewOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}
StoragePreview::~StoragePreview()=default;
void StoragePreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Storage viewport");impl_->metrics=m;}
void StoragePreview::setVisible(bool value,double t){auto&i=*impl_;i.clock(t);i.visible=value;i.activate();}
void StoragePreview::setReduceMotion(bool value,double t){auto&i=*impl_;i.clock(t);i.reduced=value;i.scene.setActivity(i.active,value,i.time);i.feedback(i.lastPointer);}
bool StoragePreview::setAppearance(modules::StorageAppearance a,double t){auto&i=*impl_;i.clock(t);const auto changed=i.presentation.setAppearance(a);if(changed){i.scene.syncContent(i.time);i.feedback(i.lastPointer);}return changed;}
void StoragePreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto&i=*impl_;i.clock(t);
    const auto*shown=sample.current.module==core::Module::storage?&sample.current:sample.incoming&&sample.incoming->module==core::Module::storage?&*sample.incoming:nullptr;i.requested=shown&&sample.requested==core::Module::storage;i.activate();i.input=i.active&&sample.acceptsModuleInput;if(!i.input){i.pressed=false;i.feedback({});}
    if(!shown){i.pose.reset();i.registration.update({});return;}i.content();i.surface->update(center,settings,*shown,opacity);i.pose=i.surface->pose();const auto&p=*i.pose;
    const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);i.projection=core::Projection::viewport(camera*p.contentWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);
    i.scene.updatePose(p.contentWorld,p.opacity,i.time,std::span(&p.hostClip,1),p.shutter?&*p.shutter:nullptr);i.registration.update(p.registration);
}
bool StoragePreview::utilityCompleted(double t){auto&i=*impl_;i.clock(t);const bool submitted=i.probe.submitPending();const bool scan=i.details&&i.details->submitPending();return i.content()||submitted||scan;}
bool StoragePreview::requestDetails(bool refresh,double t){auto&i=*impl_;i.clock(t);if(!i.active||!i.details)return false;i.controller.requestDetails(i.time,refresh);i.probe.submitPending();i.details->submitPending();i.content();return true;}
bool StoragePreview::deadline(double t){auto&i=*impl_;i.clock(t);const auto revision=i.controller.revision();i.controller.wake(i.time);i.probe.submitPending();i.content();return revision!=i.controller.revision();}
std::optional<double>StoragePreview::nextWakeTime()const noexcept{return impl_->controller.nextWakeTime();}
bool StoragePreview::requiresFrames(double t)const{const auto&i=*impl_;return i.pose&&i.scene.requiresFrames(std::max(i.time,t));}
bool StoragePreview::covers(core::Point p)const{const auto q=impl_->local(p);return q&&modules::StoragePresentation::bounds().contains(*q);}
bool StoragePreview::perform(std::string_view action,double t){auto&i=*impl_;i.clock(t);if(!i.input)return false;const auto actions=i.presentation.actions();if(std::none_of(actions.begin(),actions.end(),[&](const auto&a){return a.id==action;}))return false;
    if(action=="storage:settings"){if(i.settingsAction)i.settingsAction();return true;}
    i.scene.manualRefreshTurn(i.time);i.controller.refresh(i.time);i.probe.submitPending();i.content();return true;
}
bool StoragePreview::pointer(const app::PointerEvent&e,double t){auto&i=*impl_;i.clock(t);const auto q=i.local({e.x,e.y});
    if(e.kind==app::PointerKind::captureLost){const bool was=i.pressed;i.pressed=false;i.feedback({});return was;}
    if(e.kind==app::PointerKind::leave){i.feedback({});return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool was=i.pressed;i.pressed=false;i.feedback(q);return was;}
    if(!i.input)return false;if(e.kind==app::PointerKind::move){i.feedback(q);return q&&modules::StoragePresentation::bounds().contains(*q);}
    if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left||!q||!modules::StoragePresentation::bounds().contains(*q))return false;
    i.pressed=true;i.feedback(q);if(const auto action=i.presentation.actionAt(*q))perform(*action,i.time);return true;
}
bool StoragePreview::pointerLocked()const noexcept{return impl_->pressed;}
void StoragePreview::cancelInteraction(double t){auto&i=*impl_;i.clock(t);i.pressed=false;i.feedback({});}
const modules::StorageController&StoragePreview::state()const noexcept{return impl_->controller;}
const modules::StoragePresentation&StoragePreview::presentation()const noexcept{return impl_->presentation;}
gpu::StorageCapacityProbe::Stats StoragePreview::probeStats()const noexcept{return impl_->probe.stats();}
void StoragePreview::upload(gpu::Renderer&r){auto&i=*impl_;if(i.pose){i.scene.scene().uploadResources(r);i.registration.uploadGeometry(r);}}
std::span<const gpu::LayerCompositionEntry>StoragePreview::entries(){return impl_->pose?std::span<const gpu::LayerCompositionEntry>{impl_->composed}:std::span<const gpu::LayerCompositionEntry>{};}
void StoragePreview::collected(gpu::Renderer&r){impl_->scene.scene().collectRetiredResources(r);}
void StoragePreview::release(gpu::Renderer&r){auto&i=*impl_;need(i.scene.scene().releaseResources(r)&&i.registration.releaseResources(r),"Detach Storage composition before retirement");}
}
#endif
