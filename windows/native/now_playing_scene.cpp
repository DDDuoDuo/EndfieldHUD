#include "native/now_playing_scene.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::native {
NowPlayingGroupOpacity nowPlayingGroupOpacity(double inherited,double own){
    if(!std::isfinite(inherited)||inherited<0||inherited>1||!std::isfinite(own)||own<0||own>1)throw std::invalid_argument("Invalid Now Playing group opacity");
    return std::lround(255*float(own))<255?NowPlayingGroupOpacity{1,inherited*own}:NowPlayingGroupOpacity{inherited*own,1};
}
}
#ifdef _WIN32
#include "core/source_camera.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace endfield::native {
namespace {
namespace m=modules;using J=ehud::data::Json;using M=core::Matrix4;using R=core::Rect;using Role=m::NowPlayingSurfaceRole;
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
constexpr std::array<Vertex,4>quad{{{{0,0,0},{0,0}},{{1,0,0},{1,0}},{{1,1,0},{1,1}},{{0,1,0},{0,1}}}};
constexpr std::array<std::uint32_t,6>indices{0,1,2,0,2,3};
constexpr std::array<std::uint8_t,4>transparent{};
float linear(double v){return float(v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4));}
std::array<float,4>tint(const J&value){const auto&c=value["sRGB"].array();need(c.size()==4,"Invalid source Now Playing color");return {linear(c[0].number()),linear(c[1].number()),linear(c[2].number()),float(c[3].number())};}
M rectPose(R r){return M::translation(r.x,r.y)*M::scale(r.width,r.height);}
R unionRect(R a,R b){const auto x=std::min(a.x,b.x),y=std::min(a.y,b.y);return {x,y,std::max(a.x+a.width,b.x+b.width)-x,std::max(a.y+a.height,b.y+b.height)-y};}
R placedBounds(const LayerRasterImage&image,const M&local){const auto&b=image.bounds;return {local.values[12]+b.x,local.values[13]+b.y,b.width,b.height};}
struct Track {
    double from{},to{},start{},duration{};
    double value(double t)const noexcept{return duration>0?from+(to-from)*core::CubicTiming{0,0,.58,1}.value(std::clamp((t-start)/duration,0.,1.)):to;}
    bool live(double t)const noexcept{return from!=to&&duration>0&&t<start+duration;}
    void set(double target,double t,double seconds){if(target==to)return;from=value(t);to=target;start=t;duration=seconds;}
    void settle()noexcept{from=to;duration=0;}
};
struct RasterSlot {
    std::string id;std::shared_ptr<const LayerRasterImage>image;std::uint64_t revision{},uploaded{};
    void install(LayerRasterizer&r,const J&content,const LayerRasterOptions&o,NowPlayingSceneStats&stats){auto next=r.rasterize(id,++revision,content,o);need(next&&next->complete()&&next->width&&next->height,"Unsupported original Now Playing artwork");image=std::move(next);++stats.localRasterUpdates;}
    void upload(Renderer&r){if(!image||uploaded==revision)return;r.setTexture(id,revision,{image->width,image->height,image->straightRGBA,TextureColorSpace::encodedSRGB});uploaded=revision;}
    bool release(Renderer&r){if(!uploaded)return true;if(!r.stats().initialized||r.removeTexture(id)){uploaded=0;return true;}return false;}
};
struct Leaf {
    m::NowPlayingSurface source;RasterSlot raster;unsigned group{},index{};Track feedback;std::array<float,4>numericTint{1,1,1,1};
    bool numeric()const noexcept{return source.role==Role::progressFill||source.role==Role::progressHandle||source.role==Role::volumeFill;}
};
// Two fixed slots retain previous/current *model* content. Interrupting a CA
// contents transition uses the prior model, not a snapshot of its partial mix.
struct Fade {
    std::array<RasterSlot,2>raster;std::array<std::shared_ptr<const NotesImageFrame>,2>cover;
    std::array<std::uint64_t,2>coverVersion{},coverUploaded{};
    std::array<DrawObject,2>draws;std::string id;R bounds;M local;J descriptor;
    unsigned current{};bool initialized{},registered{},targetDirty{true},coverKind{};
    std::uint64_t transition{};double start{},duration{};
    double phase(double t)const noexcept{return duration>0?core::CubicTiming{.42,0,.58,1}.value(std::clamp((t-start)/duration,0.,1.)):1;}
    bool live(double t)const noexcept{return duration>0&&t<start+duration;}
};
}
struct NativeNowPlayingScene::Impl {
    m::NowPlayingPresentation&presentation;LayerRasterizer&raster;LayerRasterOptions options;LayerScene carrier;
    std::string prefix,mesh,parentID,lyricsID,volumeID;std::vector<Leaf>leaves;std::array<Fade,3>fades;
    std::array<DrawObject,33>parent;std::array<DrawObject,3>lyrics;std::array<DrawObject,6>volume;std::array<DrawObject,1>output;DrawObject stagedPose;
    std::uint64_t contentRevision{},fontRevision{},coverRevision{},coverSerial{};std::shared_ptr<const NotesImageFrame>cover;
    std::optional<m::NowPlayingAction>highlight;bool pressed{},ready{},meshResident{},parentRegistered{},lyricsRegistered{},volumeRegistered{};
    Renderer*owner{};double time{};M world;float opacity{1};NowPlayingSceneStats counts;
    Impl(m::NowPlayingPresentation&p,LayerRasterizer&r,LayerRasterOptions o):presentation(p),raster(r),options(std::move(o)),carrier(r){
        need(std::isfinite(options.pixelsPerPoint)&&options.pixelsPerPoint>0&&options.pixelsPerPoint<=16,"Invalid Now Playing local content density");
        static std::atomic<std::uint64_t>sequence{};prefix="nowPlaying:"+std::to_string(sequence.fetch_add(1,std::memory_order_relaxed));mesh=prefix+"/quad";parentID=prefix+"/canvas";lyricsID=prefix+"/lyrics";volumeID=prefix+"/volume";
        carrier.load(J::Object{{"bounds",J::Array{0,0,0,0}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}},options);leaves.reserve(37);output[0].masks.reserve(8);stagedPose.sourceID=prefix+"/pose";stagedPose.meshID=mesh;stagedPose.masks.reserve(8);
        for(unsigned k=0;k<3;++k){auto&f=fades[k];f.id=prefix+"/fade"+std::to_string(k);f.coverKind=k==0;for(unsigned slot=0;slot<2;++slot){f.raster[slot].id=f.id+"/slot"+std::to_string(slot);auto&d=f.draws[slot];d.sourceID=f.raster[slot].id;d.meshID=mesh;d.textureID=f.raster[slot].id;d.blend=NativeBlend::weightedAdd;}}
        fades[0].bounds=m::NowPlayingGeometry::cover;
    }
    ~Impl(){for(const auto&l:leaves)raster.remove(l.raster.id);for(const auto&f:fades)for(const auto&s:f.raster)raster.remove(s.id);}
    void clock(double t){need(std::isfinite(t)&&t>=time,"Now Playing scene needs a finite monotonic owner clock");time=t;}
    DrawObject&draw(Leaf&l){return l.group==1?lyrics[l.index]:l.group==2?volume[l.index]:parent[l.index];}
    void identity(DrawObject&d,const std::string&id){d.sourceID=id;d.meshID=mesh;}
    void leaf(m::NowPlayingSurface value,unsigned group,unsigned index,bool fontChanged){
        auto found=std::find_if(leaves.begin(),leaves.end(),[&](const Leaf&l){return l.source.id==value.id;});
        if(found==leaves.end()){Leaf l;l.source=value;l.group=group;l.index=index;l.raster.id=prefix+"/"+value.id;leaves.push_back(std::move(l));found=std::prev(leaves.end());identity(draw(*found),found->raster.id);if(!found->numeric())draw(*found).textureID=found->raster.id;if(group==1)draw(*found).masks.push_back({{},m::NowPlayingGeometry::lyrics});else if(group==0&&index<6)draw(*found).masks.push_back({{},m::NowPlayingGeometry::cover});}
        auto&l=*found;need(l.group==group&&l.index==index,"Now Playing retained paint order changed");
        if(!l.numeric()&&(!l.raster.image||l.source.content!=value.content||(fontChanged&&value.content["kind"]==J("text"))))l.raster.install(raster,value.content,options,counts);
        l.source=std::move(value);if(l.numeric())l.numericTint=tint(l.source.content["backgroundColor"]);
    }
    void caption(Fade&f,const m::NowPlayingSurface&s,std::uint64_t transition,bool fontChanged){
        const bool changed=!f.initialized||f.descriptor!=s.content;const bool animate=f.initialized&&changed&&f.transition!=transition;
        if(changed||fontChanged){const auto slot=f.initialized?(animate?1-f.current:f.current):0;f.raster[slot].install(raster,s.content,options,counts);if(!f.initialized)f.raster[1].install(raster,s.content,options,counts);f.current=slot;f.local=s.local;f.descriptor=s.content;f.initialized=true;if(changed){f.start=time;f.duration=animate?m::NowPlayingPresentation::contentFadeDuration:0;}
            f.bounds=unionRect(placedBounds(*f.raster[0].image,f.local),placedBounds(*f.raster[1].image,f.local));f.targetDirty=true;
        }f.transition=transition;
    }
    void coverContent(std::shared_ptr<const NotesImageFrame>image){auto&f=fades[0];const auto transition=presentation.coverTransitionRevision();const bool changed=!f.initialized||cover!=image||coverRevision!=presentation.input().coverRevision;
        if(changed){const bool animate=f.initialized&&f.transition!=transition;const auto slot=f.initialized?1-f.current:0;f.cover[slot]=image;f.coverVersion[slot]=++coverSerial;if(!f.initialized){f.cover[1]=image;f.coverVersion[1]=++coverSerial;}f.current=slot;f.initialized=true;f.start=time;f.duration=animate?m::NowPlayingPresentation::contentFadeDuration:0;cover=std::move(image);coverRevision=presentation.input().coverRevision;}f.transition=transition;
    }
    bool feedback(std::optional<m::NowPlayingAction>action,bool down){if(action){const auto index=static_cast<std::size_t>(*action);need(index<5,"Unknown Now Playing feedback action");if(!presentation.actions()[index].enabled)action.reset();}down&=action.has_value();const bool changed=action!=highlight||down!=pressed;highlight=action;pressed=down;
        const bool reduced=presentation.appearance().reducedMotion||!presentation.active();for(auto&l:leaves){if(l.source.role!=Role::tint&&l.source.role!=Role::rim)continue;const bool selected=action==l.source.action;const double target=selected?(l.source.role==Role::rim?1:down?1:.62):0;l.feedback.set(target,time,reduced?0:down?.06:.14);if(reduced)l.feedback.settle();}if(changed)++counts.feedbackChanges;return changed;
    }
    void pose(){
        const auto p=presentation.sample(time);
        // Detached CA source-topology samples show that an own opacity rounded
        // to opaque bypasses group-opacity isolation. Inherited false-group
        // wrapper alpha then reaches each leaf (material for the overlapping
        // volume face/backing). During its own fade, the container composites
        // once. The opacity branch is measured at .998/.999 and both endpoints.
        const auto lyricsAlpha=nowPlayingGroupOpacity(opacity,std::clamp(p.lyricsOpacity,0.,1.));
        const auto volumeAlpha=nowPlayingGroupOpacity(opacity,std::clamp(p.volumeOpacity,0.,1.));
        const bool settled=presentation.appearance().reducedMotion||!presentation.active();
        for(auto&f:fades){if(settled)f.duration=0;const auto phase=f.phase(time);for(unsigned k=0;k<2;++k){auto&d=f.draws[k];d.opacity=float(k==f.current?phase:1-phase);if(f.coverKind){const auto&image=f.cover[k];if(image){const double scale=std::max(330./image->width,330./image->height),w=image->width*scale,h=image->height*scale;d.world=rectPose({55+(330-w)*.5,(330-h)*.5,w,h});}else{d.world=rectPose(m::NowPlayingGeometry::cover);}}else d.world=f.local*rectPose(f.raster[k].image->bounds);}}
        const auto v=m::nowPlayingVolumeGeometry(presentation.input().volume.value_or(presentation.input().volumeAvailable?1:0),presentation.input().volumeAvailable);
        for(auto&l:leaves){auto&d=draw(l);d.opacity=l.source.opacity;d.linearTint={1,1,1,1};const auto role=l.source.role;if(l.numeric()){d.linearTint=l.numericTint;R r;
                if(role==Role::progressFill)r={55,387,std::max(p.progressWidth,1.),2};else if(role==Role::progressHandle)r={p.progressHandleX-2.5,384,5,8};else r={v.fill.x,v.fill.y,v.fill.width,std::max(v.fill.height,1.)};d.world=rectPose(r);if((role==Role::progressFill&&p.progressWidth<=0)||(role==Role::volumeFill&&v.fill.height<=0))d.opacity=0;
            }else{d.world=l.source.local*rectPose(l.raster.image->bounds);if(role==Role::volumeHandle)d.world=M::translation(0,v.handle.y)*d.world;}
            if(role==Role::lyric){d.world=M::translation(0,p.lyricTranslationY)*d.world;d.opacity*=float(p.lyricOpacityFactor);}
            if(role==Role::tint||role==Role::rim){if(settled)l.feedback.settle();d.opacity=float(l.feedback.value(time));}
            if(l.group==0)d.opacity*=opacity;else if(l.group==1)d.opacity*=float(lyricsAlpha.leaf);else if(l.group==2)d.opacity*=float(volumeAlpha.leaf);
        }
        for(auto index:{0u,3u,4u})parent[index].opacity=opacity;parent[6].opacity=float(lyricsAlpha.output);parent[32].opacity=float(volumeAlpha.output);
        output[0].world=world;output[0].opacity=opacity>0?1:0;
    }
    void publishFade(Renderer&r,Fade&f){for(unsigned k=0;k<2;++k){if(f.coverKind){if(f.coverUploaded[k]!=f.coverVersion[k]){const auto&image=f.cover[k];if(image)r.setTexture(f.raster[k].id,f.coverVersion[k],{image->width,image->height,image->straightRGBA,TextureColorSpace::encodedSRGB});else r.setTexture(f.raster[k].id,f.coverVersion[k],{1,1,transparent,TextureColorSpace::encodedSRGB});f.coverUploaded[k]=f.coverVersion[k];++counts.coverUploads;}}else f.raster[k].upload(r);}
        if(!f.registered||f.targetDirty){r.configureNativeGroup(f.id,{f.bounds,options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB},f.draws);f.registered=true;f.targetDirty=false;}else r.setNativeGroupDraws(f.id,f.draws);
    }
    void bindOutput(Renderer&r,const std::string&id,DrawObject&d){const auto&native=r.nativeGroupOutput(id);if(d.sourceID.empty())d=native;else need(d.sourceID==native.sourceID&&d.meshID==native.meshID&&d.textureID==native.textureID,"Now Playing native group identity changed");}
};
NativeNowPlayingScene::NativeNowPlayingScene(m::NowPlayingPresentation&p,LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(p,r,std::move(o))){}
NativeNowPlayingScene::~NativeNowPlayingScene()=default;
bool NativeNowPlayingScene::syncContent(std::shared_ptr<const NotesImageFrame>image,double t){auto&i=*impl_;i.clock(t);need(bool(image)==i.presentation.input().coverAvailable,"Publish the actual Now Playing cover availability before its content");if(image)need(image->width&&image->height&&image->width<=512&&image->height<=512&&image->straightRGBA.size()==std::size_t(image->width)*image->height*4,"Now Playing requires a bounded immutable512px thumbnail");const auto revision=i.presentation.contentRevision(),font=i.raster.fontRevision();if(i.ready&&revision==i.contentRevision&&font==i.fontRevision&&i.cover==image)return false;
    const auto next=m::prepareNowPlayingArtwork(i.presentation);const bool typography=i.ready&&font!=i.fontRevision;i.coverContent(std::move(image));
    i.leaf(next.panel[0],0,1,typography);i.leaf(next.panel[1],0,2,typography);i.caption(i.fades[1],next.panel[2],i.presentation.titleTransitionRevision(),typography);i.caption(i.fades[2],next.panel[3],i.presentation.artistTransitionRevision(),typography);i.leaf(next.panel[4],0,5,typography);
    for(unsigned k=0;k<next.lyrics.size();++k)i.leaf(next.lyrics[k],1,k,typography);for(unsigned k=0;k<next.controls.size();++k)i.leaf(next.controls[k],0,7+k,typography);for(unsigned k=0;k<next.volume.size();++k)i.leaf(next.volume[k],2,k,typography);
    i.contentRevision=revision;i.fontRevision=font;i.ready=true;++i.counts.contentEvents;i.counts.rasterSurfaces=0;for(const auto&l:i.leaves)i.counts.rasterSurfaces+=!l.numeric();i.counts.rasterSurfaces+=4;i.feedback(i.highlight,i.pressed);i.pose();return true;
}
bool NativeNowPlayingScene::setFeedback(std::optional<m::NowPlayingAction>a,bool pressed,double t){auto&i=*impl_;i.clock(t);need(i.ready,"Synchronize Now Playing before feedback");const bool changed=i.feedback(a,pressed);i.pose();return changed;}
bool NativeNowPlayingScene::updatePose(const NowPlayingScenePose&p){auto&i=*impl_;need(i.ready,"Synchronize Now Playing before placement");need(p.world.finite()&&std::isfinite(p.opacity)&&p.opacity>=0&&p.opacity<=1&&p.masks.size()<=8,"Invalid Now Playing output pose");(void)core::source::inverseSourceMatrix(p.world);i.clock(p.time);
    // Validate all caller masks before publishing any numeric scene state.
    DrawObject&d=i.output[0];auto&validation=i.stagedPose;validation.world=p.world;validation.opacity=p.opacity;validation.masks.assign(p.masks.begin(),p.masks.end());validation.shutter=p.shutter?std::optional<PlaneShutter>(*p.shutter):std::nullopt;validateDrawObject(validation);
    const bool changed=i.world!=p.world||i.opacity!=p.opacity;i.world=p.world;i.opacity=p.opacity;d.masks.assign(p.masks.begin(),p.masks.end());d.shutter=validation.shutter;i.pose();++i.counts.poses;return changed;
}
void NativeNowPlayingScene::uploadResources(Renderer&r){auto&i=*impl_;need(i.ready&&(!i.owner||i.owner==&r)&&r.stats().initialized,"Now Playing needs its initialized renderer owner");i.owner=&r;if(!i.meshResident){r.setMesh(i.mesh,1,{quad,indices});i.meshResident=true;}for(auto&l:i.leaves)l.raster.upload(r);for(auto&f:i.fades)i.publishFade(r,f);
    if(!i.lyricsRegistered){r.configureNativeGroup(i.lyricsID,{m::NowPlayingGeometry::lyrics,i.options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB},i.lyrics);i.lyricsRegistered=true;}else r.setNativeGroupDraws(i.lyricsID,i.lyrics);
    if(!i.volumeRegistered){r.configureNativeGroup(i.volumeID,{{353,217,58,186},i.options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB},i.volume);i.volumeRegistered=true;}else r.setNativeGroupDraws(i.volumeID,i.volume);
    i.bindOutput(r,i.fades[0].id,i.parent[0]);i.bindOutput(r,i.fades[1].id,i.parent[3]);i.bindOutput(r,i.fades[2].id,i.parent[4]);i.bindOutput(r,i.lyricsID,i.parent[6]);i.bindOutput(r,i.volumeID,i.parent[32]);i.pose();
    if(!i.parentRegistered){r.configureNativeGroup(i.parentID,{m::NowPlayingGeometry::canvas,i.options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB},i.parent);i.parentRegistered=true;}else r.setNativeGroupDraws(i.parentID,i.parent);
    if(i.output[0].sourceID.empty()){const auto&native=r.nativeGroupOutput(i.parentID);i.output[0].sourceID=native.sourceID;i.output[0].meshID=native.meshID;i.output[0].textureID=native.textureID;}
}
LayerCompositionEntry NativeNowPlayingScene::entry(){auto&i=*impl_;need(i.parentRegistered,"Upload Now Playing before publication");return {&i.carrier,i.output};}
bool NativeNowPlayingScene::requiresFrames(double t)const noexcept{const auto&i=*impl_;if(!i.ready||!i.presentation.active()||i.presentation.appearance().reducedMotion)return false;if(i.presentation.requiresFrames(t))return true;for(const auto&f:i.fades)if(f.live(t))return true;for(const auto&l:i.leaves)if(l.feedback.live(t))return true;return false;}
bool NativeNowPlayingScene::releaseResources(Renderer&r){auto&i=*impl_;need(!i.owner||i.owner==&r,"Release Now Playing through its renderer owner");
    // This preflight rejects attached carriers BEFORE any group/resource is
    // retired. Partial release never invalidates a still-published scene.
    i.carrier.releaseResources(r);const bool initialized=r.stats().initialized;
    const auto removeGroup=[&](const std::string&id,bool&registered){if(!registered)return true;if(!initialized||r.removeNativeGroup(id)){registered=false;return true;}return false;};
    if(!removeGroup(i.parentID,i.parentRegistered))return false;bool complete=true;for(auto&f:i.fades)complete=removeGroup(f.id,f.registered)&&complete;complete=removeGroup(i.lyricsID,i.lyricsRegistered)&&complete;complete=removeGroup(i.volumeID,i.volumeRegistered)&&complete;
    for(auto&l:i.leaves)complete=l.raster.release(r)&&complete;for(auto&f:i.fades)for(unsigned k=0;k<2;++k){if(f.coverKind){if(f.coverUploaded[k]&&(!initialized||r.removeTexture(f.raster[k].id)))f.coverUploaded[k]=0;complete=!f.coverUploaded[k]&&complete;}else complete=f.raster[k].release(r)&&complete;}
    if(i.meshResident&&(!initialized||r.removeMesh(i.mesh)))i.meshResident=false;complete=!i.meshResident&&complete;if(complete)i.owner=nullptr;return complete;
}
NowPlayingSceneStats NativeNowPlayingScene::stats()const noexcept{return impl_->counts;}
}
#endif
