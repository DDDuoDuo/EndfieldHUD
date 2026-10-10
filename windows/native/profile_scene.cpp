#include "native/profile_scene.hpp"
#ifdef _WIN32
#include "core/motion.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
namespace m=modules;using J=ehud::data::Json;using M=core::Matrix4;using R=core::Rect;using P=core::Point;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}
constexpr core::CubicTiming easeOut{0,0,.58,1};
constexpr double updateDuration=.18,dismissDuration=.14,visibilityDuration=.18,backdropDuration=.18;
constexpr double backdropWidth=900,canvasHeight=334;
// animateUpdate: opacity .3 -> 1 and translation.y 3 -> 0, 0.18 s easeOut.
struct Update {
    std::optional<double>start;
    double phase(double now)const{return start?easeOut.value(std::clamp((now-*start)/updateDuration,0.,1.)):1;}
    bool active(double now)const{return start&&now<*start+updateDuration;}
};
struct Track {
    double from{},target{},start{},duration{};bool eased{true};
    double value(double now)const{if(duration<=0)return target;const double t=std::clamp((now-start)/duration,0.,1.);return from+(target-from)*(eased?easeOut.value(t):t);}
    bool active(double now)const{return duration>0&&from!=target&&now<start+duration;}
    void to(double next,double now,double length,bool ease=true){if(target==next&&!active(now))return;from=value(now);target=next;start=now;duration=length;eased=ease;}
    void snap(double next){from=target=next;duration=0;}
};
R lerp(R a,R b,double t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.width+(b.width-a.width)*t,a.height+(b.height-a.height)*t};}
std::vector<std::string>ids(const m::ProfileArtworkPart&p){std::vector<std::string>v;v.reserve(p.surfaces.size());for(const auto&s:p.surfaces)v.push_back(s.id);return v;}
}
struct NativeProfileScene::Impl {
    struct Part {
        LayerScene scene;m::ProfileArtworkPart plan;std::vector<std::optional<std::size_t>>index;std::vector<Update>updates;
        std::vector<LayerPlacement>placements;Update whole;std::uint64_t uploaded{};bool loaded{};
        explicit Part(LayerRasterizer&r):scene(r){}
    };
    LayerRasterizer&raster;LayerRasterOptions options;std::string ns;
    m::ProfileArtwork art;std::uint64_t revision{},fontRevision{},localRevision{};bool synced{};
    std::unique_ptr<Part>fields,toolbar,popover,departing;std::vector<std::unique_ptr<Part>>retired;std::optional<double>departStart;
    std::vector<std::array<Track,2>>feedback;std::optional<P>hover;bool pressed{},reduce{};
    Track visibility,shade;std::optional<double>backdropMove;R backdropFrom{},cropFrom{};Update backdropUpdate;
    LayerScene carrier;std::array<LayerCompositionEntry,5>composed{};std::size_t count{};
    std::array<DrawObject,2>after{},children{};bool groupRegistered{},groupDirty{true},backdropPosed{};
    std::array<Vertex,4>quad{};std::array<std::uint32_t,6>quadIndices{0,1,2,0,2,3};
    std::vector<std::uint8_t>photo;unsigned photoWidth{1},photoHeight{1};std::uint64_t photoRevision{1},uploadedPhoto{};bool photoPresent{};
    m::ProfileBitmap shadeBitmap,fadeBitmap,contrastBitmap,contrastMaskBitmap;bool staticUploaded{};
    std::array<PlaneMask,6>masks{};std::size_t maskCount{};std::array<PlaneMask,1>photoClip{};std::optional<PlaneShutter>shutter;
    Renderer*renderer{};double time{};NativeProfileSceneStats stats;
    Impl(LayerRasterizer&r,LayerRasterOptions o):raster(r),options(std::move(o)),carrier(r){
        static std::atomic<std::uint64_t>serial{};ns="profile.scene."+std::to_string(serial.fetch_add(1));
        fields=std::make_unique<Part>(r);toolbar=std::make_unique<Part>(r);popover=std::make_unique<Part>(r);
        carrier.load(J::Object{{"id",ns+".carrier"},{"bounds",J::Array{0,0,0,0}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}},options);
        quad={Vertex{{0,0,0},{0,0},{1,1,1,1}},Vertex{{1,0,0},{1,0},{1,1,1,1}},Vertex{{1,1,0},{1,1},{1,1,1,1}},Vertex{{0,1,0},{0,1},{1,1,1,1}}};
        photo={0,0,0,0};retired.reserve(4);
        // Fixed original gradients: shade, the background fades and the text contrast.
        shadeBitmap=m::profileGradientBitmap({{{0,0,0,.70},{0,0,0,.38},{0,0,0,.03}},{0,.60,1},{0,.5},{1,.5}},256,1);
        const m::ProfileGradient vertical{{{0,0,0,0},{0,0,0,1},{0,0,0,1},{0,0,0,0}},{0,.08,.90,1},{.5,0},{.5,1}};
        fadeBitmap=m::profileFadeBitmap({{{0,0,0,0},{0,0,0,1},{0,0,0,1},{0,0,0,0}},{0,.08,.92,1},{0,.5},{1,.5}},&vertical,256,256);
        contrastBitmap=m::profileGradientBitmap({{{0,0,0,.52},{0,0,0,.46},{0,0,0,0}},{0,.80,1},{0,.5},{1,.5}},256,1);
        contrastMaskBitmap=m::profileFadeBitmap({{{0,0,0,0},{0,0,0,1},{0,0,0,1},{0,0,0,0}},{0,.04,.95,1},{.5,0},{.5,1}},nullptr,1,256);
        for(auto*d:{&after[0],&after[1],&children[0],&children[1]})d->masks.reserve(8);
        const auto bind=[&](DrawObject&d,std::string source,std::string texture){d.sourceID=std::move(source);d.meshID=ns+".quad";d.textureID=std::move(texture);};
        bind(children[0],ns+".backdrop.photo",ns+".backdrop.photo");bind(children[1],ns+".backdrop.shade",ns+".backdrop.shade");
        bind(after[1],ns+".contrast",ns+".contrast.color");after[1].opacity=0;
        after[1].alphaMask=PlaneAlphaMask{M{},{0,0,272,canvasHeight},ns+".contrast.mask"};outMask=PlaneAlphaMask{M{},{0,0,backdropWidth,canvasHeight},ns+".backdrop.fade"};
        visibility.snap(1);shade.snap(1);
    }
    void clock(double t)const{need(std::isfinite(t)&&t+1e-9>=time,"Profile scene requires a finite monotonic owner clock");}
    // Structure-preserving content events update changed leaves only.
    void apply(Part&part,const m::ProfileArtworkPart&plan){
        if(part.loaded&&ids(part.plan)==ids(plan)){
            const auto&before=part.plan.layers["children"].array();const auto&next=plan.layers["children"].array();
            for(std::size_t n=0;n<next.size();++n)if(before[n]!=next[n]&&part.index[n]){part.scene.updateLocalContent(plan.surfaces[n].id,++localRevision,next[n],options);++stats.localUpdates;}
            part.plan=plan;return;
        }
        part.scene.load(plan.layers,options);need(part.scene.report().unsupported.empty(),"Unsupported Personal Profile artwork");++stats.loads;
        part.plan=plan;part.index.assign(plan.surfaces.size(),std::nullopt);part.updates.assign(plan.surfaces.size(),{});
        for(std::size_t n=0;n<plan.surfaces.size();++n)part.index[n]=part.scene.surfaceIndex(plan.surfaces[n].id);
        part.placements.clear();part.placements.reserve(plan.surfaces.size());part.loaded=true;
    }
    double rest(std::size_t h,bool rim)const{const auto&v=art.highlights[h];return rim&&v.framed&&v.enabled?.28:0;}
    void feedbackTo(std::optional<P>point,bool down,double now){
        const auto hit=point?m::profileHighlightAt(art,*point):std::nullopt;
        for(std::size_t h=0;h<feedback.size();++h){const bool on=hit==h;
            const std::array<double,2>targets{on?(down?1.:.62):0.,on?1.:rest(h,true)};
            for(unsigned k=0;k<2;++k)feedback[h][k].to(targets[k],now,reduce?0:(down&&on?.06:.14));}
    }
    // A replaced part may still be published: it is released only by
    // collectRetired(), after the owner republished entries without it.
    void retire(std::unique_ptr<Part>&p){if(!p)return;if(!p->loaded||!renderer){p.reset();return;}need(retired.size()<4,"Publish and collect earlier Personal Profile generations");retired.push_back(std::move(p));}
    void rebuildEntries(){
        count=0;composed[count++]={&carrier,std::span<const DrawObject>(after)};
        for(auto*p:{fields.get(),toolbar.get(),departing.get(),popover.get()})if(p&&p->loaded)composed[count++]={&p->scene,{}};
    }
    M outWorld;float outOpacity{};PlaneAlphaMask outMask;
    // Numeric fields only on frames: the resident texture ID string is bound once.
    void applyBackdrop(){auto&out=after[0];out.world=outWorld;out.opacity=outOpacity;out.masks.clear();out.shutter.reset();
        if(!out.alphaMask)out.alphaMask=outMask;else{out.alphaMask->worldToLocal=outMask.worldToLocal;out.alphaMask->bounds=outMask.bounds;}validateDrawObject(out);}
    R backdropFrame(double now)const{
        if(!backdropMove)return art.backdrop.frame;
        return lerp(backdropFrom,art.backdrop.frame,easeOut.value(std::clamp((now-*backdropMove)/backdropDuration,0.,1.)));
    }
    R cropAt(double now)const{
        if(!backdropMove)return art.backdrop.contentsRect;
        return lerp(cropFrom,art.backdrop.contentsRect,easeOut.value(std::clamp((now-*backdropMove)/backdropDuration,0.,1.)));
    }
    void place(Part&part,const NativeProfilePose&pose,double alpha,double dy,bool highlights){
        part.placements.clear();
        const double whole=part.whole.phase(pose.time);alpha*=part.whole.start?.3+.7*whole:1;dy+=part.whole.start?3*(1-whole):0;
        for(std::size_t n=0;n<part.plan.surfaces.size();++n){
            if(!part.index[n])continue;const auto&s=part.plan.surfaces[n];double factor=1,shift=0;
            if(s.highlight&&highlights&&*s.highlight<feedback.size())factor=feedback[*s.highlight][s.rim?1:0].value(pose.time);
            else if(s.highlight)factor=0;
            else if(part.updates[n].start){const double phase=part.updates[n].phase(pose.time);factor=.3+.7*phase;shift=3*(1-phase);}
            part.placements.push_back({*part.index[n],pose.world*M::translation(s.frame.x,s.frame.y+dy+shift),float(pose.opacity*alpha*factor),std::span<const PlaneMask>(masks.data(),maskCount)});
        }
        part.scene.setPlacements(part.placements);part.scene.setGroupShutter(pose.shutter);
    }
};
NativeProfileScene::NativeProfileScene(LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}
NativeProfileScene::~NativeProfileScene(){
    auto&i=*impl_;if(!i.renderer)return;
    try{if(i.groupRegistered)(void)i.renderer->removeNativeGroup(i.ns+".backdrop");}catch(...){}
}
bool NativeProfileScene::syncContent(const m::ProfileArtwork&next,std::uint64_t revision,double now,const NativeProfileChange&change,bool reduce){
    auto&i=*impl_;i.clock(now);
    if(i.synced&&i.revision==revision&&i.fontRevision==i.raster.fontRevision())return false;
    const bool animate=!reduce;const auto previous=i.art;const bool hadPopover=i.popover->loaded&&!i.popover->plan.surfaces.empty();
    // A dismissed popover leaves a departing copy; switching removes it at once.
    if(i.departing&&(change.popoverOpened||!animate)){i.retire(i.departing);i.departStart.reset();}
    if(change.popoverDismissed&&animate&&hadPopover&&next.popover.surfaces.empty()){
        i.retire(i.departing);i.departing=std::move(i.popover);i.departStart=now;i.popover=std::make_unique<Impl::Part>(i.raster);
    }
    i.apply(*i.fields,next.fields);i.apply(*i.toolbar,next.toolbar);i.apply(*i.popover,next.popover);
    // Highlights keep their numeric feedback across a repaint of the same control.
    std::vector<std::array<Track,2>>feedback(next.highlights.size());
    for(std::size_t h=0;h<next.highlights.size();++h){
        const auto&v=next.highlights[h];bool carried{};
        for(std::size_t k=0;k<previous.highlights.size()&&k<i.feedback.size();++k){const auto&o=previous.highlights[k];
            if(o.rect==v.rect&&o.shape==v.shape&&o.framed==v.framed&&o.tint.substr(0,15)==v.tint.substr(0,15)&&o.enabled==v.enabled){feedback[h]=i.feedback[k];carried=true;break;}}
        if(!carried){feedback[h][0].snap(0);feedback[h][1].snap(v.framed&&v.enabled?.28:0);}
    }
    i.art=next;i.feedback=std::move(feedback);i.reduce=reduce;
    i.feedbackTo(i.hover,i.pressed,now);
    if(animate){
        for(auto*p:{i.fields.get(),i.toolbar.get()})for(std::size_t n=0;n<p->plan.surfaces.size();++n){
            const auto&role=p->plan.surfaces[n].animation;
            if(!role.empty()&&std::find(change.update.roles.begin(),change.update.roles.end(),role)!=change.update.roles.end())p->updates[n].start=now;}
        if(change.update.fields)i.fields->whole.start=now;
        if(change.update.toolbar)i.toolbar->whole.start=now;
        if(change.popoverOpened)i.popover->whole.start=now;
        if(change.update.backdrop)i.backdropUpdate.start=now;
        if(change.visibilityToggled){i.visibility.from=1-next.fieldsOpacity;i.visibility.target=next.fieldsOpacity;i.visibility.start=now;i.visibility.duration=visibilityDuration;i.visibility.eased=false;
            i.shade.from=1-next.backdrop.shadeOpacity;i.shade.target=next.backdrop.shadeOpacity;i.shade.start=now;i.shade.duration=visibilityDuration;i.shade.eased=false;}
        else{i.visibility.snap(next.fieldsOpacity);i.shade.snap(next.backdrop.shadeOpacity);}
        if(change.backdropMoved&&(previous.backdrop.frame!=next.backdrop.frame||previous.backdrop.contentsRect!=next.backdrop.contentsRect)){
            i.backdropFrom=i.backdropFrame(now);i.cropFrom=i.cropAt(now);i.backdropMove=now;}
        else if(!change.backdropMoved)i.backdropMove.reset();
    }else{
        i.visibility.snap(next.fieldsOpacity);i.shade.snap(next.backdrop.shadeOpacity);i.backdropMove.reset();
        for(auto*p:{i.fields.get(),i.toolbar.get(),i.popover.get()}){p->whole.start.reset();for(auto&u:p->updates)u.start.reset();}
    }
    i.revision=revision;i.fontRevision=i.raster.fontRevision();i.synced=true;i.time=now;i.groupDirty=true;i.rebuildEntries();i.stats.surfaces=0;
    for(auto*p:{i.fields.get(),i.toolbar.get(),i.popover.get()})i.stats.surfaces+=p->scene.draws().size();
    return true;
}
bool NativeProfileScene::animateWork(double now,bool reduce){
    auto&i=*impl_;i.clock(now);if(reduce||!i.fields->loaded)return false;bool any{};
    for(std::size_t n=0;n<i.fields->plan.surfaces.size();++n)if(i.fields->plan.surfaces[n].animation=="work"){i.fields->updates[n].start=now;any=true;}
    return any;
}
bool NativeProfileScene::setBackdropImage(std::optional<NativeProfileBackdropImage>image){
    auto&i=*impl_;
    if(!image){if(!i.photoPresent)return false;i.photo={0,0,0,0};i.photoWidth=i.photoHeight=1;i.photoPresent=false;++i.photoRevision;i.groupDirty=true;return true;}
    need(image->width&&image->height&&image->width<=4096&&image->height<=4096&&image->straightRGBA.size()==std::size_t(image->width)*image->height*4,"Invalid profile background bitmap");
    if(i.photoPresent&&image->revision==i.photoRevision)return false;
    i.photo.assign(image->straightRGBA.begin(),image->straightRGBA.end());i.photoWidth=image->width;i.photoHeight=image->height;i.photoPresent=true;
    i.photoRevision=std::max(i.photoRevision+1,image->revision);i.groupDirty=true;return true;
}
bool NativeProfileScene::setFeedback(std::optional<P>point,bool down,double now,bool reduce){
    auto&i=*impl_;i.clock(now);if(i.hover==point&&i.pressed==down&&i.reduce==reduce)return false;
    i.hover=point;i.pressed=down;i.reduce=reduce;i.feedbackTo(point,down,now);i.time=now;return true;
}
void NativeProfileScene::updatePose(const NativeProfilePose&pose){
    auto&i=*impl_;i.clock(pose.time);need(i.synced,"Synchronize Personal Profile content before posing");
    need(pose.world.finite()&&pose.backdropWorld.finite()&&std::isfinite(pose.opacity)&&pose.opacity>=0&&pose.opacity<=1&&
         std::isfinite(pose.backdropOpacity)&&pose.backdropOpacity>=0&&pose.backdropOpacity<=1&&pose.ownerMasks.size()<=i.masks.size(),"Invalid Personal Profile pose");
    if(pose.shutter)validatePlaneShutter(*pose.shutter);
    std::copy(pose.ownerMasks.begin(),pose.ownerMasks.end(),i.masks.begin());i.maskCount=pose.ownerMasks.size();i.shutter=pose.shutter;i.time=pose.time;
    const double t=pose.time;const double visible=i.visibility.value(t);
    i.place(*i.fields,pose,visible,0,true);i.place(*i.toolbar,pose,1,0,true);i.place(*i.popover,pose,1,0,true);
    if(i.departing){
        const double e=easeOut.value(std::clamp((t-*i.departStart)/dismissDuration,0.,1.));
        if(t>=*i.departStart+dismissDuration+.01){i.retire(i.departing);i.departStart.reset();i.rebuildEntries();}
        else i.place(*i.departing,pose,1-e,-5*e,false);
    }
    // Text contrast: a child of the fields layer, below every field leaf.
    {   const double whole=i.fields->whole.phase(t),alpha=visible*(i.fields->whole.start?.3+.7*whole:1),dy=i.fields->whole.start?3*(1-whole):0;
        auto&d=i.after[1];const auto base=pose.world*M::translation(0,dy);d.world=base*M::scale(272,canvasHeight);
        d.opacity=i.art.contrast?float(pose.opacity*alpha):0.f;d.masks.assign(i.masks.begin(),i.masks.begin()+static_cast<std::ptrdiff_t>(i.maskCount));d.shutter=pose.shutter;
        d.alphaMask->worldToLocal=core::source::inverseSourceMatrix(base);validateDrawObject(d);
    }
    // Background plane: group-local photo/shade, then fade-masked output.
    {   const auto frame=i.backdropFrame(t);const auto crop=i.cropAt(t);const double update=i.backdropUpdate.phase(t);
        const double alpha=i.backdropUpdate.start?.3+.7*update:1,dy=i.backdropUpdate.start?3*(1-update):0;
        const double width=std::clamp(frame.width,1.,backdropWidth);
        auto&photo=i.children[0];const double w=width/std::max(1e-6,crop.width),h=canvasHeight/std::max(1e-6,crop.height);
        const auto photoWorld=M::translation(-crop.x*w,-crop.y*h)*M::scale(w,h);
        const float photoOpacity=i.photoPresent&&i.art.backdrop.photo?1.f:0.f,shadeOpacity=photoOpacity*float(i.shade.value(t));
        i.photoClip[0]={M{},{0,0,width,canvasHeight},0};
        if(photo.world!=photoWorld||photo.opacity!=photoOpacity||photo.masks.size()!=1||photo.masks[0].bounds!=i.photoClip[0].bounds){photo.world=photoWorld;photo.opacity=photoOpacity;photo.masks.assign(i.photoClip.begin(),i.photoClip.end());i.groupDirty=true;}
        auto&shadeDraw=i.children[1];const auto shadeWorld=M::scale(width,canvasHeight);
        if(shadeDraw.world!=shadeWorld||shadeDraw.opacity!=shadeOpacity){shadeDraw.world=shadeWorld;shadeDraw.opacity=shadeOpacity;i.groupDirty=true;}
        const auto layer=pose.backdropWorld*M::translation(frame.x,frame.y+dy);
        // Without a photo the source backdrop has clear contents and a clear shade.
        i.outWorld=layer;i.outOpacity=photoOpacity>0?float(pose.backdropOpacity*alpha):0.f;
        i.outMask.worldToLocal=core::source::inverseSourceMatrix(layer);i.outMask.bounds={0,0,width,canvasHeight};
        if(i.groupRegistered)i.applyBackdrop();
        i.backdropPosed=true;
    }
    ++i.stats.poses;
}
bool NativeProfileScene::uploadResources(Renderer&r){
    auto&i=*impl_;need(!i.renderer||i.renderer==&r,"Personal Profile resources belong to another renderer");i.renderer=&r;bool changed{};
    if(!i.staticUploaded){
        r.setMesh(i.ns+".quad",1,{i.quad,i.quadIndices});
        r.setTexture(i.ns+".backdrop.shade",1,{i.shadeBitmap.width,i.shadeBitmap.height,i.shadeBitmap.rgba,TextureColorSpace::sRGB,TextureFilter::linear});
        r.setTexture(i.ns+".backdrop.fade",1,{i.fadeBitmap.width,i.fadeBitmap.height,i.fadeBitmap.rgba,TextureColorSpace::linear,TextureFilter::linear});
        r.setTexture(i.ns+".contrast.color",1,{i.contrastBitmap.width,i.contrastBitmap.height,i.contrastBitmap.rgba,TextureColorSpace::sRGB,TextureFilter::linear});
        r.setTexture(i.ns+".contrast.mask",1,{i.contrastMaskBitmap.width,i.contrastMaskBitmap.height,i.contrastMaskBitmap.rgba,TextureColorSpace::linear,TextureFilter::linear});
        i.staticUploaded=true;changed=true;i.stats.textureUploads+=4;
    }
    if(i.uploadedPhoto!=i.photoRevision){
        r.setTexture(i.ns+".backdrop.photo",i.photoRevision,{i.photoWidth,i.photoHeight,i.photo,TextureColorSpace::sRGB,TextureFilter::linear});
        i.uploadedPhoto=i.photoRevision;changed=true;++i.stats.textureUploads;i.groupDirty=true;
    }
    if(i.backdropPosed&&(!i.groupRegistered||i.groupDirty)){
        if(!i.groupRegistered){
            r.configureNativeGroup(i.ns+".backdrop",{{0,0,backdropWidth,canvasHeight},i.options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB},i.children);
            auto output=r.nativeGroupOutput(i.ns+".backdrop");output.masks.reserve(8);i.after[0]=std::move(output);i.groupRegistered=true;i.applyBackdrop();i.rebuildEntries();
        }else r.setNativeGroupDraws(i.ns+".backdrop",i.children);
        i.groupDirty=false;changed=true;++i.stats.groupDraws;
    }
    for(auto*p:{i.fields.get(),i.toolbar.get(),i.popover.get(),i.departing.get()})
        if(p&&p->loaded&&p->uploaded!=p->scene.resourceRevision()){p->scene.uploadResources(r);p->uploaded=p->scene.resourceRevision();changed=true;}
    return changed;
}
bool NativeProfileScene::requiresFrames(double now)const{
    const auto&i=*impl_;if(i.reduce)return i.departing!=nullptr;
    if(i.departing||i.visibility.active(now)||i.shade.active(now)||i.backdropUpdate.active(now))return true;
    if(i.backdropMove&&now<*i.backdropMove+backdropDuration)return true;
    for(const auto&pair:i.feedback)for(const auto&t:pair)if(t.active(now))return true;
    for(const auto*p:{i.fields.get(),i.toolbar.get(),i.popover.get()}){if(p->whole.active(now))return true;for(const auto&u:p->updates)if(u.active(now))return true;}
    return false;
}
std::span<const LayerCompositionEntry>NativeProfileScene::entries()const noexcept{
    const auto&i=*impl_;if(!i.groupRegistered)return {i.composed.data()+1,i.count?i.count-1:0};
    return {i.composed.data(),i.count};
}
bool NativeProfileScene::collectRetired(Renderer&r){
    auto&i=*impl_;for(auto it=i.retired.begin();it!=i.retired.end();){if((*it)->scene.releaseResources(r))it=i.retired.erase(it);else return false;}
    for(auto*p:{i.fields.get(),i.toolbar.get(),i.popover.get(),i.departing.get()})if(p&&p->loaded)p->scene.collectRetiredResources(r);
    return true;
}
bool NativeProfileScene::releaseResources(Renderer&r){
    auto&i=*impl_;bool ok=collectRetired(r);
    for(auto*p:{i.fields.get(),i.toolbar.get(),i.popover.get(),i.departing.get()})if(p&&p->loaded)ok=p->scene.releaseResources(r)&&ok;
    if(i.groupRegistered){if(r.removeNativeGroup(i.ns+".backdrop"))i.groupRegistered=false;else ok=false;}
    if(ok){for(const auto*id:{".backdrop.photo",".backdrop.shade",".backdrop.fade",".contrast.color",".contrast.mask"})ok=r.removeTexture(i.ns+id)&&ok;ok=r.removeMesh(i.ns+".quad")&&ok;}
    if(ok){i.staticUploaded=false;i.uploadedPhoto=0;i.renderer=nullptr;i.count=0;}
    return ok;
}
const m::ProfileArtwork&NativeProfileScene::artwork()const noexcept{return impl_->art;}
NativeProfileSceneStats NativeProfileScene::stats()const noexcept{return impl_->stats;}
}
#endif
