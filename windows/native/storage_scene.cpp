#include "native/storage_scene.hpp"
#ifdef _WIN32
#include "core/motion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::native {
namespace {using J=ehud::data::Json;using Matrix=core::Matrix4;using Point=core::Point;using Rect=core::Rect;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}
bool rounded(Rect r,Point p){if(!r.contains(p))return false;const auto x=std::clamp(p.x,r.x+3,r.x+r.width-3),y=std::clamp(p.y,r.y+3,r.y+r.height-3);return (p.x-x)*(p.x-x)+(p.y-y)*(p.y-y)<=9;}
J payload(J j){j["position"]=J::Array{0,0};return j;}
}
struct NativeStorageScene::Impl {
    struct Track {double from{},target{},began{},duration{};bool active{};};
    modules::StoragePresentation&presentation;LayerScene scene;LayerRasterOptions options;modules::StorageSourcePaths paths;modules::StorageArtwork artwork;
    std::vector<J>payloads;std::vector<std::uint64_t>revisions;std::vector<LayerPlacement>placements;std::vector<std::array<PlaneMask,8>>masks;std::vector<Track>tracks;
    modules::StorageRefreshMotion rotation;bool active{},reduced{};std::optional<double>time;std::uint64_t revision{};NativeStorageSceneStats stats;
    Impl(modules::StoragePresentation&p,LayerRasterizer&r,LayerRasterOptions o,modules::StorageSourcePaths data):presentation(p),scene(r),options(std::move(o)),paths(std::move(data)){}
    void clock(double t){need(std::isfinite(t)&&(!time||t>=*time),"Storage scene requires monotonic caller time");time=t;}
    static double sample(const Track&t,double now){return !t.active||t.duration==0?t.target:t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value(std::clamp((now-t.began)/t.duration,0.,1.));}
};
NativeStorageScene::NativeStorageScene(modules::StoragePresentation&p,LayerRasterizer&r,LayerRasterOptions o,modules::StorageSourcePaths paths):impl_(std::make_unique<Impl>(p,r,std::move(o),std::move(paths))){}
NativeStorageScene::~NativeStorageScene()=default;
bool NativeStorageScene::syncContent(double t){auto&i=*impl_;i.clock(t);if(i.revision==i.presentation.revision())return false;
    auto next=modules::prepareStorageArtwork(i.presentation,i.paths);std::vector<J>payloads;payloads.reserve(next.surfaces.size());for(const auto&v:next.layers["children"].array())payloads.push_back(payload(v));
    // A virtual root keeps independently animated leaves retained separately;
    // a finite 2D root would flatten the whole module into one bitmap.
    if(!i.revision){auto root=next.layers;root["bounds"]=J::Array{0,0,0,0};i.scene.load(root,i.options);need(i.scene.report().unsupported.empty()&&i.scene.draws().size()==next.surfaces.size(),"Unsupported retained Storage artwork");
        i.placements.resize(next.surfaces.size());i.masks.resize(next.surfaces.size());i.tracks.resize(next.surfaces.size());i.revisions.assign(next.surfaces.size(),1);
        for(std::size_t n=0;n<next.surfaces.size();++n){need(i.scene.surfaceIndex(next.surfaces[n].id)==n,"Storage leaf order changed");i.placements[n].surface=n;}++i.stats.structures;
    }else{need(next.surfaces.size()==i.artwork.surfaces.size(),"Storage source topology changed");for(std::size_t n=0;n<next.surfaces.size();++n){need(next.surfaces[n].id==i.artwork.surfaces[n].id,"Storage source identity changed");if(payloads[n]!=i.payloads[n]){i.scene.updateLocalContent(next.surfaces[n].id,++i.revisions[n],next.layers["children"].array()[n],i.options);++i.stats.localUpdates;}}}
    i.artwork=std::move(next);i.payloads=std::move(payloads);i.revision=i.presentation.revision();i.rotation.update(i.active,i.presentation.snapshot().isLoading,i.reduced,t);
    // setEnabled(false) clears the highlighted model but retains the source
    // .14s ease-out from current presentation opacity (unless reduced).
    if(i.presentation.snapshot().isLoading)for(std::size_t n=0;n<i.artwork.surfaces.size();++n)if(i.artwork.surfaces[n].action=="storage:refresh"&&i.tracks[n].target!=0){const auto from=Impl::sample(i.tracks[n],t);i.tracks[n]={from,0,t,i.reduced?0:.14,!i.reduced&&from!=0};}return true;
}
void NativeStorageScene::setActivity(bool active,bool reduced,double t){auto&i=*impl_;i.clock(t);i.active=active;i.reduced=reduced;i.rotation.update(active,i.presentation.snapshot().isLoading,reduced,t);if(!active)for(auto&track:i.tracks)track={};}
void NativeStorageScene::manualRefreshTurn(double t){auto&i=*impl_;i.clock(t);i.rotation.manualTurn(t);}
bool NativeStorageScene::setFeedback(std::optional<Point>point,bool pressed,double t){auto&i=*impl_;i.clock(t);need(i.revision==i.presentation.revision(),"Synchronize Storage content before feedback");std::optional<std::string_view>hit;
    if(i.active&&point&&std::isfinite(point->x)&&std::isfinite(point->y))for(const auto&a:i.presentation.actions())if(rounded(a.rect,*point))hit=a.id;
    bool changed{};for(std::size_t n=0;n<i.artwork.surfaces.size();++n){const auto&s=i.artwork.surfaces[n];if(s.action.empty())continue;auto&track=i.tracks[n];const bool selected=hit&&*hit==s.action;const double target=selected?(s.rim?1:pressed?1:.62):0;
        if(track.target==target&&!i.reduced)continue;const auto current=Impl::sample(track,t);const auto duration=i.reduced?0:pressed&&selected?.06:.14;track={current,target,t,duration,duration>0&&current!=target};changed=true;}
    if(changed)++i.stats.feedbackChanges;return changed;
}
void NativeStorageScene::updatePose(const Matrix&world,float opacity,double t,std::span<const PlaneMask>masks,const PlaneShutter*shutter){auto&i=*impl_;i.clock(t);need(i.revision&&world.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1&&masks.size()<=8,"Invalid retained Storage placement");if(shutter)validatePlaneShutter(*shutter);
    const auto rotation=i.rotation.sample(t);for(std::size_t n=0;n<i.artwork.surfaces.size();++n){const auto&s=i.artwork.surfaces[n];auto&p=i.placements[n];p.world=world*s.local;
        if(s.rotates)p.world=p.world*Matrix::translation(11,11)*Matrix::rotation(0,0,rotation.angle)*Matrix::translation(-11,-11);
        p.opacity=opacity*(s.action.empty()?s.opacity:static_cast<float>(Impl::sample(i.tracks[n],t)));std::copy(masks.begin(),masks.end(),i.masks[n].begin());p.masks={i.masks[n].data(),masks.size()};}
    i.scene.setPlacements(i.placements);i.scene.setGroupShutter(shutter?std::optional<PlaneShutter>{*shutter}:std::nullopt);for(auto&track:i.tracks)if(t>=track.began+track.duration)track.active=false;++i.stats.poses;
}
bool NativeStorageScene::requiresFrames(double t)const{const auto&i=*impl_;need(std::isfinite(t)&&(!i.time||t>=*i.time),"Invalid Storage frame clock");if(!i.active)return false;if(i.rotation.running())return true;for(const auto&track:i.tracks)if(track.active&&t<track.began+track.duration)return true;return false;}
LayerScene&NativeStorageScene::scene()noexcept{return impl_->scene;}
NativeStorageSceneStats NativeStorageScene::stats()const noexcept{return impl_->stats;}

struct NativeStorageOverlay::Impl {
    struct Track {double from{},target{},began{},duration{};bool ease{};};
    LayerRasterizer&raster;LayerRasterOptions options;std::unique_ptr<LayerScene>scene;std::vector<std::unique_ptr<LayerScene>>retired;
    modules::StorageOverlayArtwork artwork;std::vector<LayerPlacement>placements;std::vector<std::array<PlaneMask,8>>masks;std::vector<Track>tracks;
    bool leaving{};std::optional<double>enter,leave;double time{};std::uint64_t builds{};
    Impl(LayerRasterizer&r,LayerRasterOptions o):raster(r),options(std::move(o)){}
    static double sample(const Track&t,double now){if(t.duration<=0)return t.target;const auto p=std::clamp((now-t.began)/t.duration,0.,1.);return t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value(p);}
    double rest(std::size_t n)const{return artwork.framed[n]?.28:0;}
    // The appearance (opacity, y offset) of the whole overlay at `now`.
    std::pair<double,double>presence(double now)const{
        if(leave){const auto p=std::clamp((now-*leave)/.12,0.,1.);const auto e=core::CubicTiming{.42,0,1,1}.value(p);return {1-e,4*e};}
        if(enter){const auto p=std::clamp((now-*enter)/.14,0.,1.);const auto e=core::CubicTiming{0,0,.58,1}.value(p);return {e,6*(1-e)};}
        return {1,0};}
};
NativeStorageOverlay::NativeStorageOverlay(LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}
NativeStorageOverlay::~NativeStorageOverlay()=default;
void NativeStorageOverlay::show(modules::StorageOverlayArtwork artwork,double t,bool animated){auto&i=*impl_;need(std::isfinite(t),"Storage overlay needs a finite time");
    need(artwork.surfaces.size()==artwork.layers["children"].array().size()&&artwork.framed.size()==artwork.surfaces.size()&&artwork.surfaces.size()<=64,"Invalid Storage overlay artwork");
    auto next=std::make_unique<LayerScene>(i.raster);next->load(artwork.layers,i.options);need(next->report().unsupported.empty()&&next->draws().size()==artwork.surfaces.size(),"Unsupported Storage overlay artwork");
    for(std::size_t n=0;n<artwork.surfaces.size();++n)need(next->surfaceIndex(artwork.surfaces[n].id)==n,"Storage overlay leaf order");
    const bool wasShown=i.scene&&!i.leaving;if(i.scene)i.retired.push_back(std::move(i.scene));i.scene=std::move(next);i.artwork=std::move(artwork);++i.builds;
    i.placements.assign(i.artwork.surfaces.size(),{});i.masks.resize(i.artwork.surfaces.size());i.tracks.assign(i.artwork.surfaces.size(),{});
    for(std::size_t n=0;n<i.placements.size();++n){i.placements[n].surface=n;i.tracks[n].from=i.tracks[n].target=i.rest(n);}
    i.leaving=false;i.leave.reset();if(!wasShown)i.enter=animated?std::optional(t):std::nullopt;i.time=std::max(i.time,t);}
void NativeStorageOverlay::hide(double t,bool animated){auto&i=*impl_;if(!i.scene||i.leaving)return;i.leaving=true;i.enter.reset();if(animated)i.leave=t;else{i.retired.push_back(std::move(i.scene));i.leave.reset();i.leaving=false;}i.time=std::max(i.time,t);}
bool NativeStorageOverlay::accepting()const noexcept{return impl_->scene&&!impl_->leaving;}
LayerScene*NativeStorageOverlay::scene()noexcept{return impl_->scene.get();}
std::optional<std::string_view>NativeStorageOverlay::actionAt(Point p)const noexcept{const auto&i=*impl_;if(!accepting()||!std::isfinite(p.x)||!std::isfinite(p.y))return {};for(const auto&a:i.artwork.actions)if(a.rect.contains(p))return a.id;return {};}
bool NativeStorageOverlay::contains(Point p)const noexcept{return accepting()&&impl_->artwork.frame.contains(p);}
bool NativeStorageOverlay::setFeedback(std::optional<Point>point,bool pressed,bool reduced,double t){auto&i=*impl_;if(!i.scene)return false;const auto hit=point&&!i.leaving?actionAt(*point):std::nullopt;bool changed{};
    for(std::size_t n=0;n<i.artwork.surfaces.size();++n){const auto&s=i.artwork.surfaces[n];if(s.action.empty())continue;const bool selected=hit&&*hit==s.action;const double target=selected?(s.rim?1:pressed?1:.62):i.rest(n);auto&track=i.tracks[n];
        if(track.target==target&&!reduced)continue;const auto current=Impl::sample(track,t);const auto duration=reduced?0:pressed&&selected?.06:.14;track={current,target,t,duration,true};changed=true;}
    i.time=std::max(i.time,t);return changed;}
void NativeStorageOverlay::updatePose(const Matrix&world,float opacity,double t,std::span<const PlaneMask>masks,const PlaneShutter*shutter){auto&i=*impl_;need(world.finite()&&std::isfinite(opacity)&&masks.size()<=8,"Invalid Storage overlay placement");if(shutter)validatePlaneShutter(*shutter);i.time=std::max(i.time,t);
    if(i.leave&&t-*i.leave>=.12){i.retired.push_back(std::move(i.scene));i.leave.reset();i.leaving=false;}
    if(i.enter&&t-*i.enter>=.14)i.enter.reset();if(!i.scene)return;
    const auto [presence,offset]=i.presence(t);const auto origin=world*Matrix::translation(i.artwork.frame.x,i.artwork.frame.y+offset);
    for(std::size_t n=0;n<i.placements.size();++n){const auto&s=i.artwork.surfaces[n];auto&p=i.placements[n];p.world=origin*s.local;p.opacity=float(opacity*presence*(s.action.empty()?s.opacity:Impl::sample(i.tracks[n],t)));std::copy(masks.begin(),masks.end(),i.masks[n].begin());p.masks={i.masks[n].data(),masks.size()};}
    i.scene->setPlacements(i.placements);i.scene->setGroupShutter(shutter?std::optional<PlaneShutter>{*shutter}:std::nullopt);}
bool NativeStorageOverlay::requiresFrames(double t)const{const auto&i=*impl_;if(!i.scene)return false;if(i.enter||i.leave)return true;for(const auto&track:i.tracks)if(track.duration>0&&track.from!=track.target&&t<track.began+track.duration)return true;return false;}
void NativeStorageOverlay::collected(Renderer&r){auto&i=*impl_;for(auto&old:i.retired)need(old->releaseResources(r),"Storage overlay still published");i.retired.clear();if(i.scene)i.scene->collectRetiredResources(r);}
bool NativeStorageOverlay::release(Renderer&r){auto&i=*impl_;bool okay=true;for(auto&old:i.retired)okay=old->releaseResources(r)&&okay;if(okay)i.retired.clear();if(i.scene)okay=i.scene->releaseResources(r)&&okay;return okay;}
std::uint64_t NativeStorageOverlay::builds()const noexcept{return impl_->builds;}
}
#endif
