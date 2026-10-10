#include "tools/orbipom_preview.hpp"
#ifdef _WIN32
#include <windows.h>
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include <algorithm>
#include <cmath>
namespace endfield::tools {
namespace {namespace n=native;namespace m=modules;using M=core::Matrix4;using P=core::Point;using J=ehud::data::Json;void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}J blank(){return J::Object{{"bounds",J::Array{0,0,440,440}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}}
struct OrbiPomPreview::Impl {
    m::OrbiPomState state;n::NativeOrbiPomScene scene;n::LayerScene geometry;n::NativeModuleSurface surface;n::NativeModuleRegistration registration{"orbipom.registration"};m::OrbiPomAppearance appearance;
    app::ClientMetrics metrics;core::Projection projection;std::optional<n::ModuleSurfacePose>pose;std::optional<P>pointer;std::optional<double>baseline;std::array<n::LayerCompositionEntry,5>composed{};bool overlayVisible{true},foreground{true},suspended{},lowPower{},active{},input{},pressed{},alive{true};double time{};std::uint64_t revision{};
    Impl(m::OrbiPomSession&s,n::LayerRasterizer&r,OrbiPomPreviewOptions o):state(s),scene(s,state,r,o.raster,o.appearance),geometry(r),surface(prepare(o.raster),core::Module::minigame),appearance(o.appearance){}
    n::LayerScene&prepare(const n::LayerRasterOptions&o){geometry.load(blank(),o);return geometry;}
    std::uint64_t clock(double t){need(std::isfinite(t),"OrbiPom owner needs finite time");need(revision!=UINT64_MAX,"OrbiPom event revision exhausted");time=std::max(time,t);return ++revision;}bool current(std::uint64_t r)const{return alive&&revision==r;}
    std::optional<P>local(P p)const{return input?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    bool awake()const noexcept{return foreground&&!suspended;}
    // Low power: the source timer ticks every 1/30 s (tolerance .001). The
    // shared clock already paces at 1/30 s; a frame presented within half a
    // 60 Hz period of the next tick counts as that tick, earlier ones do not.
    double minimumTick()const noexcept{return lowPower?1./30-1./120:0;}
    void synchronize(){if(!pose)return;scene.syncContent(time);scene.setFeedback(pointer?local(*pointer):std::nullopt,pressed,time);const auto&p=*pose;scene.updatePose({p.contentWorld,p.opacity,time,std::span(&p.hostClip,1),p.shutter?&*p.shutter:nullptr});if(!state.requiresFrames())baseline.reset();else if(!baseline)baseline=time;}
    bool action(m::OrbiPomAction a){if(!input)return false;const auto generation=revision;const bool accepted=state.perform(a);if(!current(generation))return accepted;synchronize();return accepted;}
};
OrbiPomPreview::OrbiPomPreview(m::OrbiPomSession&s,n::LayerRasterizer&r,OrbiPomPreviewOptions o):impl_(std::make_shared<Impl>(s,r,std::move(o))){}
OrbiPomPreview::~OrbiPomPreview(){if(impl_){impl_->alive=false;impl_->state.setPresented(false);}}
void OrbiPomPreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid OrbiPom viewport");impl_->metrics=m;}
void OrbiPomPreview::setAppearance(m::OrbiPomAppearance a,double t){auto i=impl_;i->clock(t);i->scene.setAppearance(a);i->appearance=a;i->synchronize();}
void OrbiPomPreview::setLanguage(core::Language language,double t){auto a=impl_->appearance;a.language=language;setAppearance(a,t);}
void OrbiPomPreview::setOverlayVisible(bool visible,double t){auto i=impl_;const auto generation=i->clock(t);if(i->overlayVisible==visible)return;i->overlayVisible=visible;i->baseline.reset();if(!visible){i->active=i->input=i->pressed=false;i->state.setPresented(false);if(!i->current(generation))return;i->scene.settle();i->pointer.reset();}}
void OrbiPomPreview::setForeground(bool value,double t){auto i=impl_;const auto generation=i->clock(t);if(i->foreground==value)return;i->foreground=value;i->baseline.reset();i->state.setForeground(i->awake());if(i->current(generation))i->synchronize();}
void OrbiPomPreview::setSystemSuspended(bool value,double t){auto i=impl_;const auto generation=i->clock(t);if(i->suspended==value)return;i->suspended=value;i->baseline.reset();i->state.setForeground(i->awake());if(i->current(generation))i->synchronize();}
void OrbiPomPreview::setLowPowerVisualMode(bool value,double t){auto i=impl_;const auto generation=i->clock(t);if(i->lowPower==value)return;i->lowPower=value;i->baseline.reset();if(i->current(generation))i->synchronize();}
void OrbiPomPreview::update(const M&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto i=impl_;const auto generation=i->clock(t);const auto*shown=sample.current.module==core::Module::minigame?&sample.current:sample.incoming&&sample.incoming->module==core::Module::minigame?&*sample.incoming:nullptr;const bool prepared=shown&&sample.requested==core::Module::minigame&&i->overlayVisible;const bool active=prepared&&sample.acceptsModuleInput;
    if(!prepared){i->state.setPresented(false);if(!i->current(generation))return;i->baseline.reset();i->pressed=false;}else i->state.setPresented(true);if(!i->current(generation))return;i->state.setActive(active);if(!i->current(generation))return;i->active=active;i->input=active;i->state.setForeground(i->awake());if(!i->current(generation))return;
    if(!shown){i->pose.reset();i->registration.update({});return;}
    if(i->state.requiresFrames()){if(!i->baseline)i->baseline=i->time;else if(const double elapsed=i->time-*i->baseline;elapsed>0&&elapsed>=i->minimumTick()){i->state.advance(elapsed);if(!i->current(generation))return;i->baseline=i->time;}}else i->baseline.reset();
    i->surface.update(center,settings,*shown,opacity);i->pose=i->surface.pose();const auto&p=*i->pose;const auto camera=n::layerViewportProjection(i->metrics.pixelWidth,i->metrics.pixelHeight)*M::scale(i->metrics.scale,i->metrics.scale);i->projection=core::Projection::viewport(camera*p.contentWorld,i->metrics.pixelWidth,i->metrics.pixelHeight);i->synchronize();i->registration.update(p.registration);
}
bool OrbiPomPreview::requiresFrames(double t)const{const auto&i=*impl_;return i.overlayVisible&&i.pose&&(i.state.requiresFrames()||i.scene.requiresFrames(std::max(i.time,t)));}
bool OrbiPomPreview::covers(P point)const{const auto&i=*impl_;if(i.input&&i.state.rulesPresented())return true;const auto q=i.local(point);return q&&core::Rect{0,0,440,440}.contains(*q);}
bool OrbiPomPreview::pointer(const app::PointerEvent&e,double t){auto i=impl_;const auto generation=i->clock(t);i->pointer=P{e.x,e.y};const auto p=i->local(*i->pointer);
    if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost){i->pointer.reset();if(e.kind==app::PointerKind::captureLost){i->pressed=false;i->state.up({});}if(i->current(generation))i->synchronize();return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool owned=i->pressed;i->pressed=false;i->state.up(p);if(i->current(generation))i->synchronize();return owned;}
    if(!i->input)return false;if(e.kind==app::PointerKind::move){if(p)i->state.move(*p);if(i->current(generation))i->synchronize();return i->state.rulesPresented()||(p&&core::Rect{0,0,440,440}.contains(*p));}
    if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&e.button==app::PointerButton::left){if(i->state.rulesPresented()){i->pressed=true;const auto r=i->scene.rulesBounds();if(!p||!r.contains(*p)||core::Rect{r.x+291,r.y+8,23,23}.contains(*p))i->state.setRulesPresented(false);if(i->current(generation))i->synchronize();return true;}if(p&&core::Rect{0,0,440,440}.contains(*p)){i->pressed=true;const bool consumed=i->state.down(*p);if(i->current(generation))i->synchronize();return consumed;}}return false;
}
bool OrbiPomPreview::perform(m::OrbiPomAction action,double t){auto i=impl_;i->clock(t);return i->action(action);}
bool OrbiPomPreview::wheel(const app::WheelEvent&e,double t){auto i=impl_;i->clock(t);if(!i->input||!i->state.rulesPresented())return false;if(e.horizontal||!std::isfinite(e.steps)||!e.linesPerStep)return true;const double distance=e.linesPerStep==UINT32_MAX?i->scene.rulesBounds().height-49:12.*e.linesPerStep;i->scene.scrollRules(-e.steps*distance,i->time);i->synchronize();return true;}
bool OrbiPomPreview::key(const app::KeyEvent&e,double t){auto i=impl_;const auto generation=i->clock(t);if(!i->input||e.kind!=app::KeyKind::down)return false;if(i->state.rulesPresented()){if(e.value==VK_ESCAPE||e.value==VK_RETURN||e.value==VK_SPACE)i->state.setRulesPresented(false);else if(e.value==VK_DOWN||e.value==VK_UP)i->scene.scrollRules(e.value==VK_DOWN?24:-24,i->time);if(i->current(generation))i->synchronize();return true;}
    std::optional<m::OrbiPomKey>key;switch(e.value){case VK_ESCAPE:key=m::OrbiPomKey::escape;break;case VK_SPACE:key=m::OrbiPomKey::space;break;case VK_LEFT:key=m::OrbiPomKey::left;break;case VK_RIGHT:key=m::OrbiPomKey::right;break;case VK_UP:key=m::OrbiPomKey::up;break;case VK_DOWN:key=m::OrbiPomKey::down;break;case 'P':key=m::OrbiPomKey::pause;break;case '1':key=m::OrbiPomKey::clear;break;case '2':key=m::OrbiPomKey::wind;break;case '3':key=m::OrbiPomKey::shake;break;case '4':key=m::OrbiPomKey::swap;break;default:break;}if(!key)return false;const bool modified=e.alt||GetKeyState(VK_CONTROL)<0||GetKeyState(VK_MENU)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0;const bool accepted=i->state.key(*key,e.previouslyDown,modified);if(i->current(generation))i->synchronize();return accepted;
}
void OrbiPomPreview::cancelInteraction(double t){auto i=impl_;const auto generation=i->clock(t);i->pressed=false;i->pointer.reset();i->state.up({});if(i->current(generation))i->synchronize();}
const m::OrbiPomState&OrbiPomPreview::state()const noexcept{return impl_->state;}
void OrbiPomPreview::upload(n::Renderer&r){auto&i=*impl_;if(!i.pose)return;i.scene.uploadResources(r);i.registration.uploadGeometry(r);}
std::span<const n::LayerCompositionEntry>OrbiPomPreview::entries(){auto&i=*impl_;if(!i.pose)return {};const auto e=i.scene.entries();need(e.size()<i.composed.size(),"OrbiPom composition capacity");std::copy(e.begin(),e.end(),i.composed.begin());i.composed[e.size()]={&i.geometry,i.registration.draws()};return {i.composed.data(),e.size()+1};}
void OrbiPomPreview::collected(n::Renderer&r){need(impl_->scene.collectRetired(r),"OrbiPom retired pixels still published");}
void OrbiPomPreview::release(n::Renderer&r){auto&i=*impl_;need(i.scene.releaseResources(r)&&i.registration.releaseResources(r)&&i.geometry.releaseResources(r),"Detach OrbiPom from shared composition before releasing");}
n::OrbiPomSceneStats OrbiPomPreview::stats()const noexcept{return impl_->scene.stats();}
}
#endif
