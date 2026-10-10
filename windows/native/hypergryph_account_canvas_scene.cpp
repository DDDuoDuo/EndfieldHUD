#include "native/hypergryph_account_canvas_scene.hpp"
#ifdef _WIN32
#include "core/motion.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::native {
namespace {
namespace h=modules::hypergryph;
using Json=ehud::data::Json;using Matrix=core::Matrix4;using Rect=core::Rect;using Point=core::Point;
void need(bool value,const char* why) {if(!value) throw std::invalid_argument(why);}
constexpr core::CubicTiming easeOut{0,0,.58,1};
constexpr double openDuration=.16,closeDuration=.14,retireLifetime=.15,scrollDuration=.08;
double progress(std::optional<double> start,double duration,double time) {
    if(!start) return 1;return easeOut.value(std::clamp((time-*start)/duration,0.0,1.0));
}
bool insideCut(Rect r,Point p) {
    const double c=std::min(4.0,std::min(r.width,r.height)/3);
    const std::array<Point,6> polygon{{{r.x+c,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height-c},{r.x+r.width-c,r.y+r.height},{r.x,r.y+r.height},{r.x,r.y+c}}};
    return core::polygonContains(polygon,p);
}
bool inside(Rect r,Point p) {return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Json payload(Json node) {node["position"]=Json::Array{0,0};return node;}
}
struct NativeAccountCanvasScene::Impl {
    struct Surface {h::CanvasSurface plan;Rect bounds;bool retiring{};};
    struct Track {double from{},target{},start{},duration{};bool active{};};
    h::AccountCanvasModel* model;LayerScene scene;LayerRasterOptions options;h::CanvasStyle style;
    std::vector<Surface> surfaces;std::vector<Json> leaves,payloads;std::vector<LayerPlacement> placements;std::vector<std::array<PlaneMask,8>> masks;
    std::vector<Track> tracks;std::vector<std::uint64_t> localRevisions;
    std::uint64_t seenRevision{},seenMenuRevision{};bool styleDirty{true},loaded{};
    std::optional<double> menuStart,retireStart,rowStart,lastTime;double rowFrom{},rowTo{},retireOffset{};
    std::vector<Surface> retiringSurfaces;std::vector<Json> retiringLeaves;bool retireExpired{};
    NativeAccountSceneStats stats;
    // Retained encoded-sRGB group over the canvas-space scene (declared after
    // the scene: it borrows it and is destroyed first).
    std::unique_ptr<NativeLayerGroup> group;Renderer* renderer{};
    std::uint64_t uploadedStructure{},uploadedResources{};bool uploaded{},localDirty{},forcePlacement{true},placed{},posed{};
    Matrix world;float opacity{1};std::array<PlaneMask,8> poseMasks{};std::size_t poseMaskCount{};std::optional<PlaneShutter> shutter;
    Impl(h::AccountCanvasModel& m,LayerRasterizer& r,LayerRasterOptions o,h::CanvasStyle s):model(&m),scene(r),options(std::move(o)),style(s) {
        group=std::make_unique<NativeLayerGroup>(scene,"account.canvas",options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB);
    }
    // Canvas-space placement of every leaf (menu lift/fade, row scroll,
    // feedback opacity). Changes only when local motion/content changes; the
    // module pose never touches it.
    void placeLocal(double t) {
        const double open=progress(menuStart,openDuration,t);
        const double retire=retireStart?progress(retireStart,closeDuration,t):1;
        const double rows=rowOffset(t);bool changed=forcePlacement;
        for(std::size_t n=0;n<surfaces.size();++n) {
            const auto& s=surfaces[n];double alpha=1,dy=0;
            if(s.retiring) {alpha=1-retire;dy=-5*retire;}
            else if(s.plan.menu) {alpha=open;dy=-5*(1-open);}
            auto local=s.plan.local;if(s.plan.rows) local=Matrix::translation(0,-(s.retiring?retireOffset:rows))*local;
            const auto placedWorld=Matrix::translation(0,dy)*local;
            const double base=s.plan.feedback.empty()?s.plan.opacity:sample(tracks[n],t);
            const auto alphaValue=static_cast<float>(base*alpha);
            std::size_t count{};
            if(s.plan.clip) {const PlaneMask clip{Matrix::translation(0,-dy),*s.plan.clip};changed|=masks[n][0].worldToLocal!=clip.worldToLocal||masks[n][0].bounds!=clip.bounds;masks[n][count++]=clip;}
            auto& p=placements[n];
            changed|=p.world!=placedWorld||p.opacity!=alphaValue||p.masks.size()!=count;
            p.world=placedWorld;p.opacity=alphaValue;p.masks={masks[n].data(),count};
        }
        if(changed) {scene.setPlacements(placements);scene.setGroupShutter(std::nullopt);localDirty=true;forcePlacement=false;}
        placed=true;
    }
    void applyPose() {if(uploaded&&posed) group->setPose(world,opacity,std::span(poseMasks).first(poseMaskCount),shutter);}
    void time(double t) {need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Account scene requires a finite monotonic owner clock");lastTime=t;}
    double rowOffset(double t) const {
        if(!rowStart) return rowTo;return rowFrom+(rowTo-rowFrom)*progress(rowStart,scrollDuration,t);
    }
    static double sample(const Track& track,double time) {
        if(!track.active||track.duration<=0) return track.target;
        return track.from+(track.target-track.from)*easeOut.value(std::clamp((time-track.start)/track.duration,0.0,1.0));
    }
    void rebuild() {
        auto plan=model->plan(style);
        std::vector<Surface> next;std::vector<Json> nextLeaves;
        const auto& children=plan.layers["children"].array();
        for(std::size_t n=0;n<plan.surfaces.size();++n) {
            const auto& b=children[n]["bounds"].array();
            next.push_back({plan.surfaces[n],Rect{plan.surfaces[n].local.values[12],plan.surfaces[n].local.values[13],b[2].number(),b[3].number()},false});
            nextLeaves.push_back(children[n]);
        }
        if(retireStart) for(std::size_t n=0;n<retiringSurfaces.size();++n) {next.push_back(retiringSurfaces[n]);nextLeaves.push_back(retiringLeaves[n]);}
        bool structure=!loaded||next.size()!=surfaces.size();
        for(std::size_t n=0;!structure&&n<next.size();++n) {
            const auto& a=next[n].plan;const auto& b=surfaces[n].plan;
            structure=a.id!=b.id||a.feedback!=b.feedback||a.rim!=b.rim||a.menu!=b.menu||a.rows!=b.rows||next[n].retiring!=surfaces[n].retiring;
        }
        std::vector<Json> nextPayloads;nextPayloads.reserve(nextLeaves.size());for(const auto& leaf:nextLeaves) nextPayloads.push_back(payload(leaf));
        if(structure) {
            Json root=Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"allowsGroupOpacity",false},{"children",Json::Array(nextLeaves.begin(),nextLeaves.end())}};
            scene.load(root,options);
            need(scene.report().unsupported.empty()&&scene.draws().size()==next.size(),"Account artwork contains unsupported retained raster state");
            for(std::size_t n=0;n<next.size();++n) need(scene.surfaceIndex(next[n].plan.id)==n,"Account surface order changed");
            placements.assign(next.size(),{});masks.assign(next.size(),{});localRevisions.assign(next.size(),1);
            std::vector<Track> nextTracks(next.size());
            for(std::size_t n=0;n<next.size();++n) {placements[n].surface=n;nextTracks[n]={next[n].plan.opacity,next[n].plan.opacity,0,0,false};}
            tracks=std::move(nextTracks);loaded=true;forcePlacement=true;++stats.structureUpdates;
        } else {
            for(std::size_t n=0;n<next.size();++n) if(styleDirty||nextPayloads[n]!=payloads[n]) {
                scene.updateLocalContent(next[n].plan.id,++localRevisions[n],nextLeaves[n],options);++stats.localUpdates;
                if(!next[n].plan.feedback.empty()) {tracks[n].target=next[n].plan.opacity;tracks[n].active=false;}
            }
        }
        surfaces=std::move(next);leaves=std::move(nextLeaves);payloads=std::move(nextPayloads);styleDirty=false;
    }
};
NativeAccountCanvasScene::NativeAccountCanvasScene(h::AccountCanvasModel& model,LayerRasterizer& r,LayerRasterOptions options,h::CanvasStyle style)
    :impl_(std::make_unique<Impl>(model,r,std::move(options),style)) {}
NativeAccountCanvasScene::~NativeAccountCanvasScene()=default;
bool NativeAccountCanvasScene::setStyle(h::CanvasStyle style) {auto& i=*impl_;if(i.style==style) return false;i.style=style;i.styleDirty=true;return true;}
bool NativeAccountCanvasScene::syncContent(double t,bool reduced) {
    auto& i=*impl_;i.time(t);bool replan=i.styleDirty||!i.loaded||i.seenRevision!=i.model->contentRevision();
    if(i.seenMenuRevision!=i.model->menuRevision()) {
        if(i.model->isPopoverOpen()) {
            // show(): an immediate replacement drops any retiring copy first.
            i.retireStart.reset();i.retiringSurfaces.clear();i.retiringLeaves.clear();
            i.menuStart=reduced?std::nullopt:std::optional<double>(t);
        } else {
            std::vector<Impl::Surface> old;std::vector<Json> oldLeaves;
            for(std::size_t n=0;n<i.surfaces.size();++n) if(i.surfaces[n].plan.menu&&!i.surfaces[n].retiring) {
                auto s=i.surfaces[n];s.retiring=true;s.plan.id="account/retiring/"+s.plan.id;s.plan.feedback.clear();
                auto leaf=i.leaves[n];leaf["id"]=s.plan.id;old.push_back(std::move(s));oldLeaves.push_back(std::move(leaf));
            }
            if(i.model->lastDismissAnimated()&&!reduced&&!old.empty()) {
                i.retireOffset=i.rowOffset(t);i.retiringSurfaces=std::move(old);i.retiringLeaves=std::move(oldLeaves);i.retireStart=t;
            } else {i.retireStart.reset();i.retiringSurfaces.clear();i.retiringLeaves.clear();}
            i.menuStart.reset();
        }
        i.rowFrom=i.rowTo=i.model->menuScrollOffset();i.rowStart.reset();
        i.seenMenuRevision=i.model->menuRevision();replan=true;
    }
    if(i.retireExpired) {i.retireExpired=false;i.retireStart.reset();i.retiringSurfaces.clear();i.retiringLeaves.clear();replan=true;}
    const double offset=i.model->menuScrollOffset();
    if(offset!=i.rowTo) {i.rowFrom=i.rowOffset(t);i.rowTo=offset;i.rowStart=reduced?std::nullopt:std::optional<double>(t);}
    if(!replan) return false;
    i.rebuild();i.seenRevision=i.model->contentRevision();i.placeLocal(t);return true;
}
bool NativeAccountCanvasScene::setFeedback(std::optional<Point> point,bool pressed,bool reduced,double t) {
    auto& i=*impl_;i.time(t);need(i.loaded,"Synchronize Account artwork before feedback");
    std::optional<std::string_view> hit;double area=std::numeric_limits<double>::infinity();
    const double rows=i.rowOffset(t);
    if(point&&std::isfinite(point->x)&&std::isfinite(point->y)) for(const auto& s:i.surfaces) {
        if(s.retiring||s.plan.rim||s.plan.feedback.empty()) continue;
        auto r=s.bounds;if(s.plan.rows) r.y-=rows;
        if(s.plan.clip&&!inside(*s.plan.clip,*point)) continue;
        if(insideCut(r,*point)&&r.width*r.height<=area) {hit=s.plan.feedback;area=r.width*r.height;}
    }
    bool changed{};
    for(std::size_t n=0;n<i.surfaces.size();++n) {
        const auto& s=i.surfaces[n].plan;if(s.feedback.empty()) continue;auto& track=i.tracks[n];
        const bool selected=hit&&*hit==s.feedback;
        const double target=selected?(s.rim?1:pressed?1:.62):s.opacity;
        if(track.target==target&&!reduced) continue;
        const double current=Impl::sample(track,t);const double duration=reduced?0:pressed&&selected?.06:.14;
        track={current,target,t,duration,duration>0&&current!=target};changed=true;
    }
    if(changed) ++i.stats.feedbackChanges;
    return changed;
}
bool NativeAccountCanvasScene::updatePose(const Matrix& world,float opacity,double t,std::span<const PlaneMask> ownerMasks,const PlaneShutter* shutter) {
    auto& i=*impl_;i.time(t);need(i.loaded,"Synchronize Account artwork before placement");
    need(world.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1&&ownerMasks.size()<=6,"Invalid Account placement");
    if(shutter) validatePlaneShutter(*shutter);
    if(i.retireStart&&t>=*i.retireStart+retireLifetime) i.retireExpired=true;
    i.placeLocal(t);
    // Group root: module transform/fade, owner clips and the canvas bounds.
    std::copy(ownerMasks.begin(),ownerMasks.end(),i.poseMasks.begin());auto count=ownerMasks.size();
    i.poseMasks[count++]={core::source::inverseSourceMatrix(world),h::AccountCanvasModel::bounds};
    i.poseMaskCount=count;i.world=world;i.opacity=opacity;
    if(shutter) i.shutter=*shutter;else i.shutter.reset();
    i.posed=true;i.applyPose();
    for(auto& track:i.tracks) if(t>=track.start+track.duration) track.active=false;
    if(i.menuStart&&t>=*i.menuStart+openDuration) i.menuStart.reset();
    if(i.rowStart&&t>=*i.rowStart+scrollDuration) i.rowStart.reset();
    ++i.stats.poseUpdates;return true;
}
bool NativeAccountCanvasScene::requiresFrames(double t) const {
    const auto& i=*impl_;
    if(i.menuStart&&t<*i.menuStart+openDuration) return true;
    if(i.rowStart&&t<*i.rowStart+scrollDuration) return true;
    if(i.retireStart) return true; // until the retiring copy is removed by the next content sync
    for(const auto& track:i.tracks) if(track.active&&t<track.start+track.duration) return true;
    return false;
}
bool NativeAccountCanvasScene::uploadResources(Renderer& r) {
    auto& i=*impl_;need(i.loaded&&i.placed,"Synchronize and place Account artwork before upload");
    need(!i.renderer||i.renderer==&r,"Account artwork belongs to another renderer");
    bool changed{};
    if(i.uploaded&&i.scene.fontRevision()!=i.scene.currentFontRevision()) {
        changed=i.group->refreshTypography();i.uploadedStructure=i.scene.contentRevision();i.uploadedResources=i.scene.resourceRevision();
    }
    if(!i.uploaded||i.uploadedStructure!=i.scene.contentRevision()||i.uploadedResources!=i.scene.resourceRevision()) {
        // The visible region is the canvas bounds (the group pose clips to
        // it); reserve all of it so menu slides and row scrolls stay inside.
        const auto ink=i.group->localCoverageBounds();const auto& b=h::AccountCanvasModel::bounds;
        const double x=std::min(ink.x,b.x),y=std::min(ink.y,b.y);
        const Rect coverage{x,y,std::max(ink.x+ink.width,b.x+b.width)-x,std::max(ink.y+ink.height,b.y+b.height)-y};
        changed=i.group->uploadResources(r,coverage)||changed;
        i.uploaded=true;i.uploadedStructure=i.scene.contentRevision();i.uploadedResources=i.scene.resourceRevision();i.localDirty=false;++i.stats.groupUploads;
    } else if(i.localDirty) {changed=i.group->updateLocal(r)||changed;i.localDirty=false;++i.stats.groupRedraws;}
    i.renderer=&r;i.applyPose();return changed;
}
std::optional<LayerCompositionEntry> NativeAccountCanvasScene::entry() {
    auto& i=*impl_;if(!i.uploaded||!i.posed) return std::nullopt;return i.group->entry();
}
bool NativeAccountCanvasScene::releaseResources(Renderer& r) {
    auto& i=*impl_;if(!i.renderer) return true;
    need(i.renderer==&r,"Release Account artwork through its renderer");
    if(!i.group->releaseResources(r)) return false;
    i.renderer=nullptr;i.uploaded=false;i.uploadedStructure=i.uploadedResources=0;return true;
}
const LayerScene& NativeAccountCanvasScene::localScene() const noexcept {return impl_->scene;}
NativeAccountSceneStats NativeAccountCanvasScene::stats() const noexcept {return impl_->stats;}
}
#endif
