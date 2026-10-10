#include "tools/now_playing_preview.hpp"
#ifdef _WIN32
#include "tools/now_playing_session.hpp"
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::tools {namespace {
namespace gpu=native;using Point=core::Point;using Matrix=core::Matrix4;using Json=ehud::data::Json;
void need(bool b,const char*why){if(!b)throw std::invalid_argument(why);}
bool inside(core::Rect r,Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Json blank(){return Json::Object{{"bounds",Json::Array{0,0,440,440}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
}
struct NowPlayingPreview::Impl:std::enable_shared_from_this<Impl>{
 gpu::LayerRasterizer&raster;gpu::LayerRasterOptions options;std::function<void()>changed,onLock;
 std::unique_ptr<NowPlayingSession>session;std::unique_ptr<gpu::NativeNowPlayingScene>scene;
 gpu::LayerScene geometry;gpu::NativeModuleSurface surface;gpu::NativeModuleRegistration registration{"nowPlaying.registration"};std::vector<gpu::LayerCompositionEntry>composed;
 app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;std::optional<Point>pointerPoint;
 bool alive{true},ready{},overlayVisible{true},prepared{},input{},pressed{};double time{};std::uint64_t event{};
 struct Event{Impl&i;std::uint64_t token;Event(Impl&v,double t):i(v),token(++v.event){need(std::isfinite(t)&&t>=0,"Now Playing owner needs finite nonnegative time");i.time=std::max(i.time,t);}bool current()const{return i.alive&&i.event==token;}};
 Impl(gpu::LayerRasterizer&r,NowPlayingPreviewOptions&o):raster(r),options(o.raster),changed(std::move(o.changed)),onLock(std::move(o.onLock)),geometry(r),surface(prepare(),core::Module::nowPlaying){composed.reserve(2);}
 gpu::LayerScene&prepare(){geometry.load(blank(),options);return geometry;}
 void initialize(gpu::NativeNowPlayingService&s,app::UtilityExecutor&q,NowPlayingPreviewOptions&o){const auto weak=weak_from_this();session=std::make_unique<NowPlayingSession>(s,q,o.appearance,[weak]{if(auto i=weak.lock())i->notify();},std::move(o.decode));scene=std::make_unique<gpu::NativeNowPlayingScene>(session->presentation(),raster,options);ready=true;}
 void notify(){if(!alive||!ready)return;auto callback=changed;if(callback)callback();}
 bool lock(std::uint64_t token){auto callback=onLock;if(callback)callback();return alive&&event==token;}
 std::optional<Point>local(Point p)const{return input&&std::isfinite(p.x)&&std::isfinite(p.y)?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
 // Shared owner contract (Map/Activity): retained scene work happens only while
 // the module surface is placed. An unselected or departed owner neither
 // rasterizes nor drives feedback; update() synchronizes content before its
 // first placement, so hover/press state always targets a populated scene.
 void feedback(){if(!pose||!scene)return;std::optional<modules::NowPlayingAction>action;if(pointerPoint)if(auto p=local(*pointerPoint))for(const auto&a:session->presentation().actions())if(a.enabled&&inside(a.rect,*p)){action=a.action;break;}scene->setFeedback(action,pressed,time);}
 void sync(){if(!pose)return;scene->syncContent(session->artwork(),time);feedback();}
 void activate(bool nextPrepared,bool nextInput,std::uint64_t token){if(input!=nextInput){input=nextInput;pressed=false;if(!input){pointerPoint.reset();session->presentation().cancelDrag();}}
  if(prepared!=nextPrepared){prepared=nextPrepared;session->setActive(prepared,time);if(!alive||event!=token)return;}feedback();
 }
 void apply(std::optional<modules::NowPlayingIntent>intent,std::uint64_t token){if(intent)session->dispatch(*intent,time);if(!alive||event!=token)return;session->presentationChanged(time);sync();notify();}
};
NowPlayingPreview::NowPlayingPreview(gpu::NativeNowPlayingService&s,gpu::LayerRasterizer&r,app::UtilityExecutor&q,NowPlayingPreviewOptions o):impl_(std::make_shared<Impl>(r,o)){impl_->initialize(s,q,o);}
NowPlayingPreview::~NowPlayingPreview(){auto i=impl_;i->alive=false;i->ready=false;++i->event;i->changed={};i->onLock={};i->session->setActive(false,i->time);}
void NowPlayingPreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Now Playing viewport");impl_->metrics=m;}
bool NowPlayingPreview::setAppearance(modules::NowPlayingAppearance a,double t){auto i=impl_;const Impl::Event e(*i,t);if(!i->session->presentation().setAppearance(std::move(a),i->time))return false;i->session->presentationChanged(i->time);i->sync();i->notify();return true;}
void NowPlayingPreview::setLanguage(core::Language language,double t){auto a=impl_->session->presentation().appearance();a.language=language;setAppearance(std::move(a),t);}
void NowPlayingPreview::setOverlayVisible(bool value,double t){auto i=impl_;const Impl::Event e(*i,t);if(i->overlayVisible==value)return;i->overlayVisible=value;if(!value)i->activate(false,false,e.token);if(e.current()){i->sync();i->notify();}}
void NowPlayingPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto i=impl_;const Impl::Event e(*i,t);need(i->metrics.pixelWidth&&i->metrics.pixelHeight,"Resize Now Playing before presentation");
 const auto*shown=sample.current.module==core::Module::nowPlaying?&sample.current:sample.incoming&&sample.incoming->module==core::Module::nowPlaying?&*sample.incoming:nullptr;const bool prepared=shown&&sample.requested==core::Module::nowPlaying&&i->overlayVisible;i->activate(prepared,prepared&&sample.acceptsModuleInput,e.token);if(!e.current())return;
 if(!shown){i->pose.reset();i->registration.update({});return;}i->scene->syncContent(i->session->artwork(),i->time);i->surface.update(center,settings,*shown,opacity);i->pose=i->surface.pose();const auto&p=*i->pose;
 const auto camera=gpu::layerViewportProjection(i->metrics.pixelWidth,i->metrics.pixelHeight)*Matrix::scale(i->metrics.scale,i->metrics.scale);i->projection=core::Projection::viewport(camera*p.contentWorld,i->metrics.pixelWidth,i->metrics.pixelHeight);i->feedback();i->scene->updatePose({p.contentWorld,p.opacity,i->time,std::span(&p.hostClip,1),p.shutter?&*p.shutter:nullptr});i->registration.update(p.registration);
}
bool NowPlayingPreview::utilityCompleted(double t){auto i=impl_;const Impl::Event e(*i,t);const bool changed=i->session->utilityCompleted(i->time);if(changed&&e.current()){i->sync();i->notify();}return changed;}
bool NowPlayingPreview::deadline(double t){auto i=impl_;const Impl::Event e(*i,t);const bool changed=i->session->deadline(i->time);if(changed&&e.current()){i->sync();i->notify();}return changed;}
std::optional<double>NowPlayingPreview::nextWakeTime()const{return impl_->session->nextWakeTime(impl_->time);}
bool NowPlayingPreview::requiresFrames(double t)const{return impl_->pose&&impl_->overlayVisible&&impl_->scene->requiresFrames(std::max(t,impl_->time));}
bool NowPlayingPreview::covers(Point p)const{const auto q=impl_->local(p);return q&&inside(modules::NowPlayingGeometry::canvas,*q);}
bool NowPlayingPreview::containsControl(Point p,double t)const{const auto q=impl_->local(p);return q&&impl_->session->presentation().containsControl(*q,std::max(t,impl_->time));}
bool NowPlayingPreview::capturesPointer()const noexcept{return impl_->input&&impl_->session->presentation().capturesPointer();}
bool NowPlayingPreview::pointer(const app::PointerEvent&p,double t){auto i=impl_;const Impl::Event e(*i,t);i->pointerPoint=Point{p.x,p.y};auto&view=i->session->presentation();const auto q=i->local(*i->pointerPoint);
 if(p.kind==app::PointerKind::captureLost){const bool owned=i->pressed||view.dragging();i->pressed=false;i->pointerPoint.reset();if(!owned)return false;view.cancelDrag();view.sync(view.input(),i->time);i->apply({},e.token);return true;}
 if(p.kind==app::PointerKind::leave){i->pointerPoint.reset();i->feedback();return false;}
 if(p.kind==app::PointerKind::up&&p.button==app::PointerButton::left){const bool owned=i->pressed||view.dragging();i->pressed=false;if(!owned)return false;const auto intent=view.pointerUp(i->time);i->apply(intent,e.token);return true;}
 if(!i->input)return false;
 if(p.kind==app::PointerKind::move){if(q&&view.dragging())i->apply(view.pointerMove(*q,i->time),e.token);else i->feedback();return view.dragging()||(q&&inside(modules::NowPlayingGeometry::canvas,*q));}
 if((p.kind==app::PointerKind::down||p.kind==app::PointerKind::doubleClick)&&p.button==app::PointerButton::left){if(view.capturesPointer()&&(!q||!inside(modules::NowPlayingGeometry::canvas,*q))){view.dismissVolume(i->time);i->apply({},e.token);return true;}if(!q||!inside(modules::NowPlayingGeometry::canvas,*q))return false;if(!i->lock(e.token))return true;i->pressed=true;const auto result=view.pointerDown(*q,i->time);i->apply(result.intent,e.token);return result.handled;}return false;
}
bool NowPlayingPreview::key(const app::KeyEvent&key,bool modified,double t){auto i=impl_;const Impl::Event e(*i,t);if(!i->input||key.kind!=app::KeyKind::down||key.alt||modified)return false;auto&view=i->session->presentation();if(key.value==VK_ESCAPE&&view.capturesPointer()){view.dismissVolume(i->time);i->apply({},e.token);return true;}if(key.value==VK_SPACE){if(!i->lock(e.token))return true;i->apply(view.perform(modules::NowPlayingAction::playPause,i->time),e.token);return true;}if(key.value==VK_LEFT||key.value==VK_RIGHT){const auto seek=view.sliders(i->time)[0];if(!seek.enabled||!seek.value)return false;if(!i->lock(e.token))return true;i->apply(view.setSlider(modules::NowPlayingSliderKind::seek,*seek.value+(key.value==VK_LEFT?-5:5),i->time),e.token);return true;}return false;}
bool NowPlayingPreview::perform(modules::NowPlayingAction action,double t){auto i=impl_;const Impl::Event e(*i,t);if(!i->input)return false;auto&view=i->session->presentation();const auto n=static_cast<std::size_t>(action);if(n>=view.actions().size()||!view.actions()[n].enabled)return false;if(!i->lock(e.token))return true;i->apply(view.perform(action,i->time),e.token);return true;}
bool NowPlayingPreview::setSlider(modules::NowPlayingSliderKind kind,double value,double t){auto i=impl_;const Impl::Event e(*i,t);if(!i->input)return false;auto&view=i->session->presentation();const auto intent=view.setSlider(kind,value,i->time);if(!intent)return false;if(!i->lock(e.token))return true;i->apply(intent,e.token);return true;}
void NowPlayingPreview::cancelInteraction(double t){auto i=impl_;const Impl::Event e(*i,t);i->pressed=false;i->pointerPoint.reset();auto&view=i->session->presentation();view.cancelDrag();view.sync(view.input(),i->time);i->apply({},e.token);}
bool NowPlayingPreview::pointerLocked()const noexcept{return impl_->input&&impl_->session->presentation().dragging();}
const modules::NowPlayingPresentation&NowPlayingPreview::presentation()const noexcept{return impl_->session->presentation();}
gpu::NowPlayingSceneStats NowPlayingPreview::sceneStats()const noexcept{return impl_->scene->stats();}
gpu::NativeNowPlayingArtwork::Stats NowPlayingPreview::artworkStats()const{return impl_->session->artworkStats();}
void NowPlayingPreview::upload(gpu::Renderer&r){auto&i=*impl_;if(!i.pose)return;i.registration.uploadGeometry(r);i.scene->uploadResources(r);}
std::span<const gpu::LayerCompositionEntry>NowPlayingPreview::entries(){auto&i=*impl_;i.composed.clear();if(i.pose){i.composed.push_back(i.scene->entry());i.composed.push_back({&i.geometry,i.registration.draws()});}return i.composed;}
void NowPlayingPreview::release(gpu::Renderer&r){auto&i=*impl_;need(i.scene->releaseResources(r)&&i.registration.releaseResources(r)&&i.geometry.releaseResources(r),"Now Playing resources still published at teardown");}
}
#endif
