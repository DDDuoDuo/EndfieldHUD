#include "tools/storage_preview.hpp"
#ifdef _WIN32
#include "native/module_registration.hpp"
#include "native/storage_files.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <typeinfo>
namespace endfield::tools {
namespace {namespace gpu=native;using Matrix=core::Matrix4;using J=ehud::data::Json;
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
J empty(){return J::Object{{"bounds",J::Array{0,0,400,334}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
app::UtilityExecutor&queue(StoragePreviewOptions&o){need(o.utility&&bool(o.readCapacity),"Storage preview requires explicit shared capacity provider");return *o.utility;}
// The chosen volume, read by the capacity reader on the worker.
struct Selection {std::mutex mutex;std::string id;std::string get(){std::lock_guard lock(mutex);return id;}void set(std::string value){std::lock_guard lock(mutex);id=std::move(value);}};
bool chooserEnabled(const StoragePreviewOptions&o){
    if(!o.listVolumes||!o.readVolumeCapacity)return false;
    // The production volume readers only with the production capacity reader.
    const bool production=o.listVolumes.target_type()==typeid(WindowsStorageVolumeList)||o.readVolumeCapacity.target_type()==typeid(WindowsStorageVolumeCapacity);
    return !production||o.readCapacity.target_type()==typeid(WindowsStorageCapacityReader);
}
gpu::StorageCapacityProbe::Reader reader(StoragePreviewOptions&o,const std::shared_ptr<Selection>&selection,bool chooser){
    if(!chooser)return o.readCapacity;return [selection,read=o.readVolumeCapacity]{return read(selection->get());};
}
}
struct StoragePreview::Impl {
    modules::StorageController controller;modules::StoragePresentation presentation;std::shared_ptr<Selection>selection=std::make_shared<Selection>();bool chooser{};
    gpu::StorageCapacityProbe probe;std::unique_ptr<gpu::StorageDetailsProbe>details;gpu::NativeStorageScene scene;gpu::LayerScene geometry;
    gpu::NativeStorageOverlay button,menu;
    std::unique_ptr<gpu::NativeModuleSurface>surface;gpu::NativeModuleRegistration registration{"storage.registration"};
    std::array<gpu::LayerCompositionEntry,3>composed;std::size_t composedCount{};std::function<void()>settingsAction;
    std::function<std::vector<modules::StorageVolume>()>listVolumes;std::function<std::string()>loadVolume;std::function<void(const std::string&)>saveVolume;
    app::UtilityExecutor*utility{};app::UtilityExecutor::Route route{};bool listing{},listWanted{};std::vector<modules::StorageVolume>volumes;std::string chosen,shown;bool menuOpen{};
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;std::optional<core::Point>lastPointer;
    std::uint64_t observedController{};bool visible{true},requested{},active{},input{},pressed{},reduced{};double time{};
    // The artwork is released once the HUD closes while Storage is not shown
    // (setVisible(false), the next publication after it or conceal()), never
    // on a module switch inside an open HUD. renderer: the host's, from
    // upload()/collected(). overlaysParked: the chooser is shown again on the
    // next activation.
    gpu::Renderer*renderer{};bool parkWanted{},overlaysParked{};
    bool park(){if(pose||!renderer)return false;const bool artwork=scene.park(*renderer),chooser=button.park(*renderer),list=menu.park(*renderer);
        if(chooser||list)overlaysParked=true;if(!artwork&&!chooser&&!list)return false;(void)registration.releaseResources(*renderer);parkWanted=false;return true;}
    // The Mac artwork is synchronized only while it is shown or active; a
    // hidden Storage page rasterizes nothing (lazy first build).
    void sync(){if(active||pose)scene.syncContent(time);}
    Impl(gpu::LayerRasterizer&r,StoragePreviewOptions o):presentation({},o.appearance),chooser(chooserEnabled(o)),probe(controller,queue(o),reader(o,selection,chooser)),scene(presentation,r,[&]{gpu::LayerRasterOptions v;v.pixelsPerPoint=o.rasterDensity;return v;}(),std::move(o.paths)),geometry(r),
        button(r,[&]{gpu::LayerRasterOptions v;v.pixelsPerPoint=o.rasterDensity;return v;}()),menu(r,[&]{gpu::LayerRasterOptions v;v.pixelsPerPoint=o.rasterDensity;return v;}()),settingsAction(std::move(o.openSettings)),reduced(o.reduceMotion){
        need(std::isfinite(o.rasterDensity)&&o.rasterDensity>=.25&&o.rasterDensity<=4,"Invalid Storage raster density");need(bool(o.scanDetails)==bool(o.completionClock),"Storage details require both scanner and owner completion clock");if(o.scanDetails)details=std::make_unique<gpu::StorageDetailsProbe>(controller,*o.utility,std::move(o.scanDetails),std::move(o.completionClock));gpu::LayerRasterOptions ro;ro.pixelsPerPoint=o.rasterDensity;geometry.load(empty(),ro);surface=std::make_unique<gpu::NativeModuleSurface>(geometry,core::Module::storage);
        if(chooser){utility=o.utility;route=utility->makeRoute();listVolumes=std::move(o.listVolumes);loadVolume=std::move(o.loadVolume);saveVolume=std::move(o.saveVolume);remembered();shown=chosen;}
    }
    ~Impl(){if(utility)utility->invalidate(route);}
    void clock(double t){need(std::isfinite(t),"Storage owner needs finite caller time");time=std::max(time,t);}
    // The remembered choice, read at construction and each activation (the
    // settings record may load or be restored to defaults meanwhile).
    bool remembered(){if(!loadVolume)return false;auto saved=loadVolume();if(saved.size()>1024||!J::validUtf8(saved)||saved==chosen)return false;chosen=std::move(saved);selection->set(chosen);return true;}
    // The chosen volume when it is listed now, else the startup volume.
    const modules::StorageVolume*effective()const{for(const auto&v:volumes)if(v.id==chosen)return &v;for(const auto&v:volumes)if(v.startup)return &v;return nullptr;}
    bool available()const{return chooser&&volumes.size()>=2;}
    void overlays(){const auto*v=effective();if(available()){button.show(modules::prepareStorageChooser(v?modules::storageVolumeDrive(*v):std::string{},presentation.appearance()),time,false);}else{button.hide(time,false);closeMenu(false);}
        if(menuOpen&&available())menu.show(modules::prepareStorageVolumeMenu(volumes,v?v->id:std::string{},presentation.appearance()),time,!reduced);
        if(presentation.setVolumeTitle(chooser&&v?v->title:std::string{}))sync();}
    void closeMenu(bool animated){if(!menuOpen)return;menuOpen=false;menu.hide(time,animated&&!reduced);}
    void openMenu(){if(!available()||menuOpen)return;menuOpen=true;overlays();list();}
    // The volume list on the shared executor (each activation and menu opening).
    void list(){if(!chooser)return;listWanted=true;if(listing)return;struct Result{std::vector<modules::StorageVolume>volumes;};auto result=std::make_shared<Result>();
        if(!utility->submit(route,[read=listVolumes,result]{result->volumes=gpu::orderStorageVolumes(read());},[this,result](std::exception_ptr error){listing=false;if(!error&&result->volumes!=volumes){volumes=std::move(result->volumes);overlays();sourceChanged();}}))return;
        listing=true;listWanted=false;}
    // The volume actually shown changed (the chosen one was unplugged or came
    // back): read the capacity again instead of keeping the cached volume.
    void sourceChanged(){const auto*v=effective();const auto id=v&&!(chosen.empty()&&v->startup)?v->id:chosen;if(id==shown)return;shown=id;controller.changeSource(time);probe.submitPending();content();}
    void choose(std::size_t index){if(index>=volumes.size())return;const auto id=volumes[index].id;closeMenu(true);const auto*before=effective();if(before&&before->id==id&&chosen==id)return;
        chosen=id;shown=id;selection->set(id);if(saveVolume)saveVolume(id);controller.changeSource(time);overlays();probe.submitPending();scene.manualRefreshTurn(time);content();}
    void activate(){const bool next=visible&&requested;if(next==active)return;active=next;pressed=false;input=false;lastPointer.reset();
        if(active){const bool changed=chooser&&remembered();controller.activate(time);if(changed||overlaysParked){overlaysParked=false;if(changed){shown=chosen;controller.changeSource(time);}overlays();}probe.submitPending();list();content();}else{controller.deactivate();closeMenu(false);}scene.setActivity(active,reduced,time);}
    bool content(){if(!active)return false;bool changed{};if(observedController!=controller.revision()){changed=presentation.receive(controller.snapshot());observedController=controller.revision();}scene.syncContent(time);scene.setFeedback(menuOpen?std::nullopt:lastPointer,pressed,time);return changed;}
    std::optional<core::Point>local(core::Point p)const{return input?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    void feedback(std::optional<core::Point>point){lastPointer=point;if(!active)return;scene.setFeedback(menuOpen?std::nullopt:point,pressed,time);button.setFeedback(menuOpen?std::nullopt:point,pressed,reduced,time);menu.setFeedback(point,pressed,reduced,time);}
    bool act(std::string_view action){
        if(action==modules::storageVolumesAction){if(!available())return false;if(menuOpen)closeMenu(true);else openMenu();return true;}
        if(action.starts_with("storage:volume:")){if(!menuOpen)return false;std::size_t index{};const auto tail=action.substr(15);const auto parsed=std::from_chars(tail.data(),tail.data()+tail.size(),index);if(parsed.ec!=std::errc{}||parsed.ptr!=tail.data()+tail.size()||index>=volumes.size())return false;choose(index);return true;}
        const auto actions=presentation.actions();if(std::none_of(actions.begin(),actions.end(),[&](const auto&a){return a.id==action;}))return false;closeMenu(true);
        if(action=="storage:settings"){if(settingsAction)settingsAction();return true;}
        scene.manualRefreshTurn(time);controller.refresh(time);probe.submitPending();content();return true;}
};
StoragePreview::StoragePreview(gpu::LayerRasterizer&r,StoragePreviewOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}
StoragePreview::~StoragePreview()=default;
void StoragePreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Storage viewport");impl_->metrics=m;}
void StoragePreview::setVisible(bool value,double t){auto&i=*impl_;i.clock(t);i.visible=value;i.parkWanted=!value;i.activate();if(!value)(void)i.park();}
void StoragePreview::conceal()noexcept{try{(void)impl_->park();}catch(...){}}
StorageResourceStats StoragePreview::resources()const noexcept{auto&i=*impl_;StorageResourceStats out;out.parks=i.scene.stats().parks;out.loaded=i.scene.loaded();
    for(auto*s:{&i.scene.scene(),i.button.scene(),i.menu.scene()})if(s){out.surfaces+=s->report().surfaces;out.pixelBytes+=s->report().pixelBytes;}return out;}
void StoragePreview::setReduceMotion(bool value,double t){auto&i=*impl_;i.clock(t);i.reduced=value;i.scene.setActivity(i.active,value,i.time);i.feedback(i.lastPointer);}
bool StoragePreview::setAppearance(modules::StorageAppearance a,double t){auto&i=*impl_;i.clock(t);const auto changed=i.presentation.setAppearance(a);if(changed){i.sync();if(i.available())i.overlays();i.feedback(i.lastPointer);}return changed;}
void StoragePreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto&i=*impl_;i.clock(t);
    const auto*shown=sample.current.module==core::Module::storage?&sample.current:sample.incoming&&sample.incoming->module==core::Module::storage?&*sample.incoming:nullptr;i.requested=shown&&sample.requested==core::Module::storage;i.activate();i.input=i.active&&sample.acceptsModuleInput;if(!i.input){i.pressed=false;i.feedback({});}
    if(!shown){i.pose.reset();i.registration.update({});return;}if(i.requested)i.parkWanted=false; // the selected module keeps its artwork across a close
    i.content();i.scene.syncContent(i.time);i.surface->update(center,settings,*shown,opacity);i.pose=i.surface->pose();const auto&p=*i.pose;
    const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);i.projection=core::Projection::viewport(camera*p.contentWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);
    const auto*shutter=p.shutter?&*p.shutter:nullptr;i.scene.updatePose(p.contentWorld,p.opacity,i.time,std::span(&p.hostClip,1),shutter);
    i.button.updatePose(p.contentWorld,p.opacity,i.time,std::span(&p.hostClip,1),shutter);i.menu.updatePose(p.contentWorld,p.opacity,i.time,std::span(&p.hostClip,1),shutter);i.registration.update(p.registration);
}
bool StoragePreview::utilityCompleted(double t){auto&i=*impl_;i.clock(t);const bool submitted=i.probe.submitPending();const bool scan=i.details&&i.details->submitPending();if(i.listWanted&&!i.listing&&i.active)i.list();return i.content()||submitted||scan;}
bool StoragePreview::requestDetails(bool refresh,double t){auto&i=*impl_;i.clock(t);if(!i.active||!i.details)return false;i.controller.requestDetails(i.time,refresh);i.probe.submitPending();i.details->submitPending();i.content();return true;}
bool StoragePreview::deadline(double t){auto&i=*impl_;i.clock(t);const auto revision=i.controller.revision();i.controller.wake(i.time);i.probe.submitPending();i.content();return revision!=i.controller.revision();}
std::optional<double>StoragePreview::nextWakeTime()const noexcept{return impl_->controller.nextWakeTime();}
bool StoragePreview::requiresFrames(double t)const{const auto&i=*impl_;const auto now=std::max(i.time,t);return i.pose&&(i.scene.requiresFrames(now)||i.button.requiresFrames(now)||i.menu.requiresFrames(now));}
bool StoragePreview::covers(core::Point p)const{const auto q=impl_->local(p);return q&&modules::StoragePresentation::bounds().contains(*q);}
bool StoragePreview::perform(std::string_view action,double t){auto&i=*impl_;i.clock(t);if(!i.input)return false;return i.act(action);}
bool StoragePreview::pointer(const app::PointerEvent&e,double t){auto&i=*impl_;i.clock(t);const auto q=i.local({e.x,e.y});
    if(e.kind==app::PointerKind::captureLost){const bool was=i.pressed;i.pressed=false;i.feedback({});return was;}
    if(e.kind==app::PointerKind::leave){i.feedback({});return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool was=i.pressed;i.pressed=false;i.feedback(q);return was;}
    if(!i.input)return false;if(e.kind==app::PointerKind::move){i.feedback(q);return q&&modules::StoragePresentation::bounds().contains(*q);}
    if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left)return false;
    // An open menu is modal over the module: a row chooses, anywhere else closes it.
    if(i.menuOpen){if(q&&i.menu.contains(*q)){i.pressed=true;i.feedback(q);if(const auto action=i.menu.actionAt(*q))i.act(*action);return true;}i.closeMenu(true);i.feedback(q);return q&&modules::StoragePresentation::bounds().contains(*q);}
    if(!q||!modules::StoragePresentation::bounds().contains(*q))return false;
    i.pressed=true;i.feedback(q);if(const auto action=i.button.actionAt(*q))i.act(*action);else if(const auto source=i.presentation.actionAt(*q))i.act(*source);return true;
}
bool StoragePreview::pointerLocked()const noexcept{return impl_->pressed;}
void StoragePreview::cancelInteraction(double t){auto&i=*impl_;i.clock(t);i.pressed=false;i.closeMenu(false);i.feedback({});}
const modules::StorageController&StoragePreview::state()const noexcept{return impl_->controller;}
const modules::StoragePresentation&StoragePreview::presentation()const noexcept{return impl_->presentation;}
gpu::StorageCapacityProbe::Stats StoragePreview::probeStats()const noexcept{return impl_->probe.stats();}
std::span<const modules::StorageVolume>StoragePreview::volumes()const noexcept{return impl_->volumes;}
const std::string&StoragePreview::chosenVolume()const noexcept{return impl_->chosen;}
bool StoragePreview::volumeMenuOpen()const noexcept{return impl_->menuOpen;}
bool StoragePreview::volumeChooserAvailable()const noexcept{return impl_->available();}
void StoragePreview::upload(gpu::Renderer&r){auto&i=*impl_;i.renderer=&r;if(!i.pose)return;i.scene.scene().uploadResources(r);if(auto*s=i.button.scene())s->uploadResources(r);if(auto*s=i.menu.scene())s->uploadResources(r);i.registration.uploadGeometry(r);}
std::span<const gpu::LayerCompositionEntry>StoragePreview::entries(){auto&i=*impl_;if(!i.pose)return {};i.composed[0]={&i.scene.scene(),i.registration.draws()};i.composedCount=1;
    if(auto*s=i.button.scene())i.composed[i.composedCount++]={s,{}};if(auto*s=i.menu.scene())i.composed[i.composedCount++]={s,{}};return {i.composed.data(),i.composedCount};}
void StoragePreview::collected(gpu::Renderer&r){auto&i=*impl_;i.scene.scene().collectRetiredResources(r);i.button.collected(r);i.menu.collected(r);
    // After a HUD close began, a Storage page that this publication no longer
    // composes releases its artwork (showing it again rebuilds it).
    i.renderer=&r;if(i.parkWanted)(void)i.park();}
void StoragePreview::release(gpu::Renderer&r){auto&i=*impl_;need(i.scene.scene().releaseResources(r)&&i.button.release(r)&&i.menu.release(r)&&i.registration.releaseResources(r),"Detach Storage composition before retirement");}
}
#endif
