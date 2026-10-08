#include "tools/battery_preview.hpp"
#ifdef _WIN32
#include "core/motion.hpp"
#include "native/module_registration.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::tools {namespace {
namespace gpu=native;using Matrix=core::Matrix4;using Json=ehud::data::Json;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
bool inside(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Json empty(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}};}
}
struct BatteryPreview::Impl {
    struct Track{double from{},target{},start{},duration{};double value(double now)const{return duration>0?from+(target-from)*core::CubicTiming{0,0,.58,1}.value((now-start)/duration):target;}bool active(double now)const{return duration>0&&from!=target&&now<start+duration;}};
    modules::BatteryPresentation state;gpu::LayerRasterOptions options;gpu::LayerScene scene,geometry;gpu::NativeModuleSurface surface;gpu::NativeModuleRegistration registration{"battery.registration"};std::function<void()>openSettings;
    modules::BatteryArtwork artwork;std::array<gpu::LayerPlacement,10>placements;std::array<gpu::LayerCompositionEntry,1>composed;std::array<gpu::PlaneMask,1>masks;
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;std::array<Track,2>feedback;std::uint64_t revision{};bool acceptsInput{},pressed{},reduced{};double time{};
    Impl(gpu::LayerRasterizer&r,gpu::LayerRasterOptions o,modules::BatteryReading value,modules::BatteryAppearance appearance,std::function<void()>action):state(std::move(value),appearance),options(std::move(o)),scene(r),geometry(r),surface([&]()->gpu::LayerScene&{geometry.load(empty(),options);return geometry;}(),core::Module::power),openSettings(std::move(action)){
        artwork=modules::prepareBatteryArtwork(state);scene.load(artwork.layers,options);need(scene.report().unsupported.empty()&&scene.draws().size()==placements.size(),"Unsupported source battery artwork");for(std::size_t n=0;n<placements.size();++n){const auto index=scene.surfaceIndex(artwork.surfaces[n].id);need(index.has_value(),"Missing battery surface");placements[n].surface=*index;}feedback[1].from=feedback[1].target=.28;revision=state.revision();composed[0]={&scene,registration.draws()};
    }
    void clock(double t){need(std::isfinite(t),"Invalid battery owner time");time=std::max(time,t);}
    void content(){if(revision==state.revision())return;auto next=modules::prepareBatteryArtwork(state);const auto&nodes=next.layers["children"].array();for(std::size_t n=0;n<nodes.size();++n)if(nodes[n]!=artwork.layers["children"].array()[n])scene.updateLocalContent(next.surfaces[n].id,state.revision(),nodes[n],options);artwork=std::move(next);revision=state.revision();}
    std::optional<core::Point>local(core::Point p)const{return acceptsInput?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    void highlight(std::optional<core::Point>p){bool hit=p&&modules::BatteryPresentation::hitsSettings(*p);if(hit){const auto r=modules::BatteryPresentation::settingsRect();const double x=p->x-r.x,y=p->y-r.y;hit=x+y>=4&&(r.width-x)+(r.height-y)>=4;}
        const double targets[]{hit?(pressed?1.:.62):0,hit?1.:.28};for(std::size_t n=0;n<2;++n){auto&t=feedback[n];if(t.target!=targets[n]||(reduced&&t.duration))t={t.value(time),targets[n],time,reduced?0.:pressed&&hit?.06:.14};}
    }
};
BatteryPreview::BatteryPreview(gpu::LayerRasterizer&r,gpu::LayerRasterOptions o,modules::BatteryReading value,modules::BatteryAppearance a,std::function<void()>action):impl_(std::make_unique<Impl>(r,std::move(o),std::move(value),a,std::move(action))){}
BatteryPreview::~BatteryPreview()=default;
void BatteryPreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid battery viewport");impl_->metrics=m;}
bool BatteryPreview::receive(modules::BatteryReading r){return impl_->state.receive(std::move(r));}
bool BatteryPreview::receive(const gpu::BatterySnapshot&s){modules::BatteryReading r;if(s.available){r.present=s.present==true;r.percentage=s.percent;r.pluggedIn=s.ac_connected==true;r.charging=s.charging==true;r.fullyCharged=s.fully_charged==true;}return receive(std::move(r));}
bool BatteryPreview::setAppearance(modules::BatteryAppearance a){return impl_->state.setAppearance(a);}
void BatteryPreview::setReduceMotion(bool value,double now){auto&i=*impl_;i.clock(now);i.reduced=value;if(value)for(auto&t:i.feedback){t.from=t.target;t.duration=0;}}
void BatteryPreview::cancelInteraction(double now){auto&i=*impl_;i.clock(now);i.pressed=false;i.highlight({});}
void BatteryPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double now){auto&i=*impl_;i.clock(now);const auto*shown=sample.current.module==core::Module::power?&sample.current:sample.incoming&&sample.incoming->module==core::Module::power?&*sample.incoming:nullptr;i.acceptsInput=shown&&sample.requested==core::Module::power&&sample.acceptsModuleInput;if(!i.acceptsInput){i.pressed=false;i.highlight({});}if(!shown){i.pose.reset();i.registration.update({});return;}i.content();i.surface.update(center,settings,*shown,opacity);i.pose=i.surface.pose();const auto&p=*i.pose;i.masks[0]=p.hostClip;const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);i.projection=core::Projection::viewport(camera*p.contentWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);
 for(std::size_t n=0;n<i.placements.size();++n){auto&placed=i.placements[n];const auto&s=i.artwork.surfaces[n];placed.world=p.contentWorld*s.local;placed.opacity=p.opacity*static_cast<float>(s.tint?i.feedback[0].value(i.time):s.rim?i.feedback[1].value(i.time):1.);placed.masks=i.masks;}i.scene.setPlacements(i.placements);i.scene.setGroupShutter(p.shutter);i.registration.update(p.registration);
}
bool BatteryPreview::requiresFrames(double now)const{const auto&i=*impl_;return i.pose&&!i.reduced&&(i.feedback[0].active(std::max(now,i.time))||i.feedback[1].active(std::max(now,i.time)));}
bool BatteryPreview::covers(core::Point p)const{const auto q=impl_->local(p);return q&&inside(modules::BatteryPresentation::bounds(),*q);}
bool BatteryPreview::pointer(const app::PointerEvent&e,double now){auto&i=*impl_;i.clock(now);const auto q=i.local({e.x,e.y});if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost){const bool used=i.pressed;i.pressed=false;i.highlight({});return used;}if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool was=i.pressed;i.pressed=false;i.highlight(q);return was;}if(!i.acceptsInput)return false;if(e.kind==app::PointerKind::move){i.highlight(q);return q&&inside(modules::BatteryPresentation::bounds(),*q);}if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left||!q||!inside(modules::BatteryPresentation::bounds(),*q))return false;i.pressed=true;i.highlight(q);if(modules::BatteryPresentation::hitsSettings(*q)&&i.openSettings)i.openSettings();return true;}
const modules::BatteryPresentation&BatteryPreview::state()const noexcept{return impl_->state;}
void BatteryPreview::upload(gpu::Renderer&r){if(impl_->pose){impl_->scene.uploadResources(r);impl_->registration.uploadGeometry(r);}}
std::span<const gpu::LayerCompositionEntry>BatteryPreview::entries(){return impl_->pose?std::span<const gpu::LayerCompositionEntry>(impl_->composed):std::span<const gpu::LayerCompositionEntry>{};}
void BatteryPreview::collected(gpu::Renderer&r){impl_->scene.collectRetiredResources(r);}
void BatteryPreview::release(gpu::Renderer&r){need(impl_->scene.releaseResources(r)&&impl_->registration.releaseResources(r),"Detach battery composition before release");}
}
#endif
