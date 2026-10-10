#include "native/reader_scene.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace endfield::native {
namespace {
using J=modules::ReaderJson;using R=core::Rect;using M=core::Matrix4;using P=core::Point;
void need(bool b,const char*m){if(!b)throw std::invalid_argument(m);}
J rect(R r){return J::Array{r.x,r.y,r.width,r.height};}
R bounds(const J&n){const auto&a=n["bounds"].array();return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
M local(const J&n){const auto r=bounds(n);const auto&p=n["position"].array();const auto&a=n["anchorPoint"].array();return M::translation(p[0].number()-a[0].number()*r.width,p[1].number()-a[1].number()*r.height,0);}
bool ink(const J&n){return !n["backgroundColor"].isNull()||!n["text"].isNull()||!n["shape"].isNull();}
struct Leaf {std::string id;J payload;M matrix;float opacity{1};std::optional<std::size_t>feedback;bool rim{},fill{},thumb{},viewport{};};
struct Track {double from{},target{},start{},duration{};};
double sample(const Track&t,double time){return t.duration<=0?t.target:t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value(std::clamp((time-t.start)/t.duration,0.,1.));}
struct Artwork {
    LayerScene scene;LayerRasterOptions options;std::unique_ptr<NativeLayerGroup>group;
    modules::ReaderArtwork plan;std::vector<Leaf>leaves;std::vector<LayerPlacement>placements;
    std::vector<std::array<PlaneMask,1>>masks;std::vector<Track>tracks;std::vector<std::uint64_t>revisions;
    std::optional<double>lastTime;std::uint64_t serial{};bool dirty{},posed{},groupUploaded{};
    DrawObject rootValidation;M rootWorld;float rootOpacity{1};std::array<PlaneMask,8>rootMasks;std::size_t rootMaskCount{};std::optional<PlaneShutter>rootShutter;
    Artwork(LayerRasterizer&r,LayerRasterOptions o,const char*id):scene(r),options(std::move(o)),group(std::make_unique<NativeLayerGroup>(scene,id,options.pixelsPerPoint)){rootValidation.sourceID="reader/root-pose";rootValidation.masks.reserve(8);}
    void rootPose(const M&w,float opacity,std::span<const PlaneMask>masks,std::optional<PlaneShutter>shutter){need(masks.size()<=8,"Too many Reader outer masks");rootValidation.world=w;rootValidation.opacity=opacity;rootValidation.masks.assign(masks.begin(),masks.end());rootValidation.shutter=shutter;validateDrawObject(rootValidation);rootWorld=w;rootOpacity=opacity;rootMaskCount=masks.size();std::copy(masks.begin(),masks.end(),rootMasks.begin());rootShutter=std::move(shutter);applyRoot();}
    void applyRoot(){if(groupUploaded)group->setPose(rootWorld,rootOpacity,std::span(rootMasks.data(),rootMaskCount),rootShutter);}
    void time(double t){need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Reader artwork requires a finite monotonic caller clock");lastTime=t;}
    void append(const J&node,const M&parent,float opacity,std::vector<Leaf>&next){const auto matrix=parent*local(node);const auto a=opacity*static_cast<float>(node["opacity"].isNull()?1:node["opacity"].number());const auto id=node["id"].string();
        if(ink(node)){auto payload=node;payload["children"]=J::Array{};payload["position"]=J::Array{0,0};payload["anchorPoint"]=J::Array{0,0};payload["opacity"]=1;payload["hidden"]=false;payload["borderWidth"]=0;payload["allowsGroupOpacity"]=false;
            next.push_back({id,std::move(payload),matrix,node["hidden"].isNull()||!node["hidden"].boolean()?a:0,{}});}
        for(const auto&child:node["children"].array())append(child,matrix,a,next);
        if(!node["borderWidth"].isNull()&&node["borderWidth"].number()>0){auto border=node;border["id"]=id+"/border";border["children"]=J::Array{};border["position"]=J::Array{0,0};border["anchorPoint"]=J::Array{0,0};border["backgroundColor"]=J{};border["text"]=J{};border["shape"]=J{};border["opacity"]=1;border["allowsGroupOpacity"]=false;next.push_back({id+"/border",std::move(border),matrix,a,{}});}}
    bool load(modules::ReaderArtwork p){std::vector<Leaf>next;for(const auto&child:p.root["children"].array())append(child,M{},1,next);
        // Fill keeps one full-width resident raster; a numeric mask exposes the
        // exact current fraction. Thumb geometry likewise remains unchanged.
        for(auto&l:next){l.fill=l.id=="reader/fill";l.thumb=l.id=="reader/thumb";l.viewport=l.id=="reader/viewport";
            if(l.fill){auto b=bounds(l.payload);b.width=p.progressRect.width;l.payload["bounds"]=rect(b);}
            if(l.thumb)l.matrix=M::translation(p.progressRect.x-2.5,p.progressRect.y+p.progressRect.height/2-3,0);
            for(std::size_t n=0;n<p.feedback.size();++n)if(l.id==p.feedback[n].tintID||l.id==p.feedback[n].rimID){l.feedback=n*2+(l.id==p.feedback[n].rimID?1:0);l.rim=l.id==p.feedback[n].rimID;}}
        bool structure=next.size()!=leaves.size();for(std::size_t n=0;!structure&&n<next.size();++n)structure=next[n].id!=leaves[n].id;
        bool changed=structure;for(std::size_t n=0;!changed&&n<next.size();++n)changed=next[n].payload!=leaves[n].payload||next[n].opacity!=leaves[n].opacity||next[n].matrix!=leaves[n].matrix;
        if(!changed){plan=std::move(p);return false;}
        if(structure){J::Array children;for(const auto&l:next)children.push_back(l.payload);J root=J::Object{{"bounds",rect(p.bounds)},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"allowsGroupOpacity",false},{"children",std::move(children)}};
            std::vector<LayerPlacement>pos(next.size());std::vector<std::array<PlaneMask,1>>clips(next.size());std::vector<std::uint64_t>rev(next.size(),1);scene.load(root,options);need(scene.report().unsupported.empty()&&scene.draws().size()==next.size(),"Unsupported original Reader leaf artwork");
            for(std::size_t n=0;n<next.size();++n)need(scene.surfaceIndex(next[n].id)==n,"Reader leaf binding changed");placements=std::move(pos);masks=std::move(clips);revisions=std::move(rev);
        }else for(std::size_t n=0;n<next.size();++n)if(next[n].payload!=leaves[n].payload)scene.updateLocalContent(next[n].id,++revisions[n],next[n].payload,options);
        tracks.assign(p.feedback.size()*2,{});for(std::size_t n=0;n<p.feedback.size();++n){tracks[n*2].target=tracks[n*2].from=0;tracks[n*2+1].target=tracks[n*2+1].from=p.feedback[n].restingRim;}
        leaves=std::move(next);plan=std::move(p);dirty=true;posed=false;++serial;return true;
    }
    bool feedback(std::optional<P>point,bool pressed,bool reduced,double t){time(t);std::optional<std::size_t>hit;double area=std::numeric_limits<double>::infinity();if(point)for(std::size_t n=0;n<plan.feedback.size();++n){const auto&f=plan.feedback[n];const auto&r=f.rect;const std::array<P,6>path{{{r.x+4,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height-4},{r.x+r.width-4,r.y+r.height},{r.x,r.y+r.height},{r.x,r.y+4}}};if(f.enabled&&core::polygonContains(path,*point)&&r.width*r.height<=area){hit=n;area=r.width*r.height;}}
        bool changed{};for(std::size_t n=0;n<tracks.size();++n){auto&t0=tracks[n];const bool over=hit&&*hit==n/2;const auto target=n%2?(over?1:plan.feedback[n/2].restingRim):(over?(pressed?1:.62):0);if(target==t0.target&&!reduced)continue;const auto value=sample(t0,t);t0={value,target,t,reduced?0:over&&pressed?.06:.14};changed=true;}return changed;}
    void pose(double t,double progress=0,bool pageTurn=false){time(t);for(std::size_t n=0;n<leaves.size();++n){const auto&l=leaves[n];auto&p=placements[n];p={n,l.matrix,l.feedback?static_cast<float>(sample(tracks[*l.feedback],t)):l.opacity,{}};if(l.thumb)p.world=M::translation(plan.progressRect.width*progress,0,0)*p.world;if(l.fill){if(progress<=0)p.opacity=0;masks[n][0]={core::source::inverseSourceMatrix(l.matrix),{0,0,std::max(.0001,plan.progressRect.width*progress),2}};p.masks=masks[n];}if(l.viewport&&pageTurn)p.opacity=0;}
        scene.setPlacements(placements);posed=true;}
    bool frames(double t)const{need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Reader frame clock moved backwards");for(const auto&v:tracks)if(v.duration>0&&t<v.start+v.duration)return true;return false;}
};
std::vector<std::uint8_t>straight(const modules::ReaderPagePixels&p){need(p.width&&p.height&&p.width<=900&&p.height<=1000&&p.bytes.size()==std::size_t(p.width)*p.height*4,"Invalid Reader page pixels");std::vector<std::uint8_t>out(p.bytes.size());for(std::size_t n=0;n<out.size();n+=4){const auto a=p.bytes[n+3];const auto red=p.format==modules::ReaderPagePixels::Format::rgba8Premultiplied?p.bytes[n]:p.bytes[n+2],blue=p.format==modules::ReaderPagePixels::Format::rgba8Premultiplied?p.bytes[n+2]:p.bytes[n];const std::array<unsigned,3>c{red,p.bytes[n+1],blue};for(unsigned k=0;k<3;++k)out[n+k]=static_cast<std::uint8_t>(a?std::min(255u,(c[k]*255u+a/2)/a):0);out[n+3]=a;}return out;}
constexpr std::array<Vertex,4>vertices{{{{0,0,0},{0,0}},{{1,0,0},{1,0}},{{1,1,0},{1,1}},{{0,1,0},{0,1}}}};
constexpr std::array<std::uint32_t,6>indices{0,1,2,0,2,3};
}
struct NativeReaderScene::Impl {
    Artwork art;Renderer*renderer{};std::string prefix,quadID;std::array<std::string,5>textureIDs;std::array<DrawObject,6>pages;
    std::array<std::shared_ptr<const modules::ReaderPagePixels>,5>pixels,uploadedPixels;std::array<std::uint64_t,5>pixelRevisions{};
    std::array<R,3>previousPlacement{},scrollFrom{},scrollTo{};std::array<bool,3>hidden{};R oldRect{};
    std::uint64_t turnSequence{},scrollEpoch{},scrollInputSequence{};double pageOrigin{},turnStart{},scrollStart{},scrollDuration{};int direction{};bool havePlacement{},cleared{},meshResident{},departing{};double progress{};
    NativeReaderSceneStats stats;std::optional<modules::ReaderCanvasInput>cachedInput;
    Impl(LayerRasterizer&r,LayerRasterOptions o):art(r,std::move(o),"reader/content"){
        static std::uint64_t ids{};prefix="reader/page/"+std::to_string(++ids);quadID=prefix+"/quad";for(unsigned n=0;n<5;++n)textureIDs[n]=prefix+"/texture/"+std::to_string(n);for(std::size_t n=0;n<pages.size();++n){auto&d=pages[n];d.sourceID=prefix+"/draw/"+std::to_string(n);d.meshID=quadID;d.masks.resize(2);d.opacity=0;}}
    void pagePose(const modules::ReaderViewport&v,double t,bool reduced){progress=v.displayedProgress();const auto placement=v.placements(false);
        // Reanchoring onto a cached neighbor changes the local page origin by
        // exactly one zoomed page height. Keep the same sampled physical path
        // across this rebase instead of snapping to the new residual offset.
        if(havePlacement&&scrollEpoch==v.scrollContinuityEpoch()){
            const auto shift=v.scrollPageOrigin()-pageOrigin;
            if(shift!=0)for(std::size_t n=0;n<3;++n){previousPlacement[n].y+=shift;scrollFrom[n].y+=shift;scrollTo[n].y+=shift;}
        }else if(havePlacement){scrollDuration=0;havePlacement=false;}
        scrollEpoch=v.scrollContinuityEpoch();pageOrigin=v.scrollPageOrigin();
        if(v.pageTurnSequence()!=turnSequence){turnSequence=v.pageTurnSequence();turnStart=t;direction=reduced?0:v.lastAnimatedTurnDirection();oldRect=previousPlacement[0];}const bool turn=direction&&t<turnStart+.26;
        bool moved{};for(std::size_t n=0;n<3;++n)moved|=placement[n].rect!=previousPlacement[n];
        // Core Animation retargets from the presentation position, not the
        // previous destination. Recycled page identities and precise pan/zoom
        // publish their new placement immediately rather than bouncing back.
        if(turn||reduced||v.nonPrecisionScrollDuration()==0)scrollDuration=0;
        if(havePlacement&&moved&&!turn&&v.nonPrecisionScrollDuration()>0&&!reduced){
            // Only an actual new wheel/key transaction gets a fresh .10s.
            // An anchor/cache update can retarget the remaining current curve,
            // but never extends its end or starts motion after it has settled.
            const double duration=v.scrollInputSequence()!=scrollInputSequence?v.nonPrecisionScrollDuration():std::max(0.,scrollStart+scrollDuration-t);
            for(std::size_t n=0;n<3;++n)scrollFrom[n]=sampledPlacement(n,t);for(std::size_t n=0;n<3;++n)scrollTo[n]=placement[n].rect;scrollStart=t;scrollDuration=duration;
        }else if(!moved&&t>=scrollStart+scrollDuration)scrollDuration=0;
        scrollInputSequence=v.scrollInputSequence();
        const double eased=turn?core::CubicTiming{.42,0,.58,1}.value(std::clamp((t-turnStart)/.26,0.,1.)):1;const double incoming=turn?direction*376*(1-eased):0,outgoing=turn?-direction*376*eased:0;
        for(std::size_t n=0;n<3;++n){R r=placement[n].rect;if(scrollDuration>0&&t<scrollStart+scrollDuration){const auto q=core::CubicTiming{0,0,.58,1}.value(std::clamp((t-scrollStart)/scrollDuration,0.,1.));r.x=scrollFrom[n].x+(scrollTo[n].x-scrollFrom[n].x)*q;r.y=scrollFrom[n].y+(scrollTo[n].y-scrollFrom[n].y)*q;}previousPlacement[n]=placement[n].rect;hidden[n]=placement[n].hidden;
            if(n==0&&matchingDetail){auto crop=v.placements(true)[0].rect;crop.x+=r.x-placement[n].rect.x;crop.y+=r.y-placement[n].rect.y;r=crop;}
            setPage(n,r,incoming,pixels[n]&&!hidden[n],incoming);}
        havePlacement=true;setPage(3,oldRect,outgoing,turn&&bool(pixels[3]),outgoing);
        // Two borrowed source viewport backgrounds participate in the push;
        // the original stationary background is hidden only during that pass.
        const auto&source=art.scene.prepareDraws()[0];for(std::size_t n=0;n<2;++n){auto&d=pages[4+n];d.meshID=source.meshID;d.textureID=source.textureID;d.world=M::translation(n?outgoing:incoming,0,0)*source.world;d.opacity=turn?1.f:0.f;d.masks[0]={M{},modules::ReaderViewport::viewport,3};d.masks[1]={M::translation(n?-outgoing:-incoming,0,0),modules::ReaderViewport::viewport,3};}
        art.pose(t,v.displayedProgress(),turn);
        // Source child order is background old/current followed by old/current
        // page. Group insertion reorders this fixed list without allocations.
        ordered[0]=&pages[4];ordered[1]=&pages[5];ordered[2]=&pages[3];ordered[3]=&pages[0];ordered[4]=&pages[1];ordered[5]=&pages[2];
        for(std::size_t n=0;n<6;++n)copyNumeric(insert[n],*ordered[n]);
    }
    R sampledPlacement(std::size_t n,double t)const{auto r=previousPlacement[n];if(scrollDuration>0&&t<scrollStart+scrollDuration){const auto q=core::CubicTiming{0,0,.58,1}.value(std::clamp((t-scrollStart)/scrollDuration,0.,1.));r.x=scrollFrom[n].x+(scrollTo[n].x-scrollFrom[n].x)*q;r.y=scrollFrom[n].y+(scrollTo[n].y-scrollFrom[n].y)*q;}return r;}
    bool matchingDetail{};std::array<const DrawObject*,6>ordered{};std::array<DrawObject,6>insert;
    static void copyNumeric(DrawObject&to,const DrawObject&from){if(to.sourceID.empty()){to=from;return;}to.world=from.world;to.opacity=from.opacity;to.masks=from.masks;to.textureID=from.textureID;to.meshID=from.meshID;}
    void setPage(std::size_t n,R r,double shift,bool visible,double clipShift){auto&d=pages[n];d.world=M::translation(12+r.x+shift,48+r.y,0)*M::scale(r.width,r.height,1);d.opacity=visible?1.f:0.f;d.masks[0]={M{},modules::ReaderViewport::viewport,3};d.masks[1]={M::translation(-clipShift,0,0),modules::ReaderViewport::viewport,3};}
    NativeGroupInsertion insertion(){return {1,insert,{12,48,376,334}};}
};
NativeReaderScene::NativeReaderScene(LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}
NativeReaderScene::~NativeReaderScene()=default;
bool NativeReaderScene::syncPages(std::span<const modules::ReaderPage>all,const modules::ReaderPage*current,const std::optional<modules::ReaderDetail>&detail,const modules::ReaderViewport&v,double t){auto&i=*impl_;i.art.time(t);std::array<std::shared_ptr<const modules::ReaderPagePixels>,5>next=i.pixels;for(unsigned n=0;n<3;++n)next[n].reset();const bool matching=detail&&current&&detail->view==v.imageView()&&detail->page.location==current->location;
    if(current){next[0]=current->pixels;for(const auto&p:all){if(current->next&&p.location==*current->next)next[1]=p.pixels;if(current->previous&&p.location==*current->previous)next[2]=p.pixels;}}
    next[4]=matching?detail->page.pixels:nullptr;
    if(v.pageTurnSequence()!=i.turnSequence)next[3]=i.matchingDetail&&i.pixels[4]?i.pixels[4]:i.pixels[0];else if(!i.direction||t>=i.turnStart+.26)next[3].reset();for(const auto&p:next)if(p)need(p->width&&p->height&&p->width<=900&&p->height<=1000&&p->bytes.size()==std::size_t(p->width)*p->height*4,"Invalid Reader pixels before artwork mutation");
    const bool changed=next!=i.pixels;
    // Rotate resident full-page identities along with the anchor. The entering
    // neighbor is already on the GPU; crossing uploads only a newly refilled
    // neighbor, never the two pages that are still moving in the viewport.
    if(i.havePlacement&&i.scrollEpoch==v.scrollContinuityEpoch()&&v.scrollPageOrigin()!=i.pageOrigin&&next[0]){
        const auto rotate=[&](auto&slots,bool forward){if(forward){std::swap(slots[0],slots[1]);std::swap(slots[1],slots[2]);}else{std::swap(slots[0],slots[2]);std::swap(slots[1],slots[2]);}};
        const bool forward=v.scrollPageOrigin()>i.pageOrigin;
        if(next[0]==i.pixels[forward?1:2]){rotate(i.textureIDs,forward);rotate(i.uploadedPixels,forward);rotate(i.pixelRevisions,forward);}
    }
    i.matchingDetail=matching;i.pixels=std::move(next);i.cleared=false;if(changed)++i.stats.contentUpdates;return changed;
}
bool NativeReaderScene::syncContent(const modules::ReaderCanvasInput&input,std::span<const modules::ReaderPage>all,const modules::ReaderPage*current,const std::optional<modules::ReaderDetail>&detail,const modules::ReaderViewport&v,double t){auto&i=*impl_;const bool pagesChanged=syncPages(all,current,detail,v,t);bool artworkChanged{};if(!i.cachedInput||*i.cachedInput!=input){auto accepted=input;artworkChanged=i.art.load(modules::prepareReaderCanvas(input));i.cachedInput=std::move(accepted);if(artworkChanged)++i.stats.contentUpdates;}return pagesChanged||artworkChanged;
}
bool NativeReaderScene::setFeedback(std::optional<P>p,bool pressed,bool reduced,double t){return impl_->art.feedback(p,pressed,reduced,t);}
void NativeReaderScene::updatePose(const M&w,float opacity,const modules::ReaderViewport&v,double t,bool reduced,std::span<const PlaneMask>masks,std::optional<PlaneShutter>shutter){auto&i=*impl_;need(w.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid Reader root pose");i.art.rootPose(w,opacity,masks,std::move(shutter));if(!i.departing)i.pagePose(v,t,reduced);else i.art.pose(t,i.progress);++i.stats.poseUpdates;}
bool NativeReaderScene::canAdvanceVertical(int direction,double t)const{const auto&i=*impl_;need(std::isfinite(t)&&(!i.art.lastTime||t>=*i.art.lastTime),"Reader seam requires a finite monotonic owner clock");if(!i.havePlacement)return false;const auto r=i.sampledPlacement(0,t);return direction>0?bool(i.pixels[1])&&r.y<-.000001:bool(i.pixels[2])&&r.y>=-.000001;}
bool NativeReaderScene::requiresFrames(double t)const{const auto&i=*impl_;return i.art.frames(t)||(i.direction&&t<i.turnStart+.26)||(i.scrollDuration>0&&t<i.scrollStart+i.scrollDuration);}
void NativeReaderScene::retainDepartingArtwork(bool value){auto&i=*impl_;i.departing=value;if(value){i.direction=0;i.scrollDuration=0;for(auto&t:i.art.tracks){t.from=t.target;t.duration=0;}for(std::size_t n=0;n<3;++n)i.setPage(n,i.previousPlacement[n],0,i.pixels[n]&&!i.hidden[n],0);for(std::size_t n=3;n<6;++n)i.pages[n].opacity=0;for(std::size_t n=0;n<6;++n)if(i.ordered[n])Impl::copyNumeric(i.insert[n],*i.ordered[n]);}}
std::span<const modules::ReaderAction>NativeReaderScene::actions()const noexcept{return impl_->art.plan.actions;}
void NativeReaderScene::upload(Renderer&r){auto&i=*impl_;need(!i.renderer||i.renderer==&r,"Reader renderer changed");need(i.art.posed,"Place Reader before upload");i.renderer=&r;if(!i.meshResident){r.setMesh(i.quadID,1,{vertices,indices});i.meshResident=true;}
    for(std::size_t n=0;n<5;++n){if(i.pixels[n]!=i.uploadedPixels[n]){if(i.pixels[n]){auto bytes=straight(*i.pixels[n]);r.setTexture(i.textureIDs[n],++i.pixelRevisions[n],{i.pixels[n]->width,i.pixels[n]->height,bytes});++i.stats.pixelUploads;}i.uploadedPixels[n]=i.pixels[n];}}
    for(std::size_t n=0;n<4;++n){const auto slot=n==0&&i.matchingDetail?4:n;if(i.pixels[slot])i.pages[n].textureID=i.textureIDs[slot];else i.pages[n].textureID.clear();}
    for(std::size_t n=0;n<6;++n)Impl::copyNumeric(i.insert[n],*i.ordered[n]);
    if(i.art.dirty){i.art.group->uploadResources(r,R{0,0,400,440},i.insertion());i.art.dirty=false;}else i.art.group->updateLocal(r,i.insertion());
    i.art.groupUploaded=true;i.art.applyRoot();for(unsigned n=0;n<5;++n)if(!i.pixels[n])r.removeTexture(i.textureIDs[n]);
}
LayerCompositionEntry NativeReaderScene::entry(){return impl_->art.group->entry();}
void NativeReaderScene::clearArtwork(){auto&i=*impl_;i.pixels={};i.cleared=true;for(auto&d:i.pages)d.opacity=0;for(auto&d:i.insert)d.opacity=0;}
bool NativeReaderScene::release(Renderer&r){auto&i=*impl_;if(!i.art.group->releaseResources(r))return false;for(unsigned n=0;n<5;++n)r.removeTexture(i.textureIDs[n]);r.removeMesh(i.quadID);i.renderer=nullptr;i.uploadedPixels={};i.meshResident=false;i.art.dirty=true;i.art.groupUploaded=false;return true;}
NativeReaderSceneStats NativeReaderScene::stats()const noexcept{return impl_->stats;}
double NativeReaderScene::scrollPresentation(double time)const{const auto&i=*impl_;need(std::isfinite(time)&&(!i.art.lastTime||time>=*i.art.lastTime),"Reader sampled scroll clock moved backwards");return -i.sampledPlacement(0,time).y;}
NativeReaderScrollSnapshot NativeReaderScene::scrollSnapshot()const noexcept{const auto&i=*impl_;NativeReaderScrollSnapshot out;out.epoch=i.scrollEpoch;out.pageOrigin=i.pageOrigin;out.animationStart=i.scrollStart;out.animationDuration=i.scrollDuration;for(std::size_t n=0;n<3;++n){const auto&m=i.pages[n].world.values;out.displayed[n]={m[12]-12,m[13]-48,m[0],m[5]};out.ready[n]=bool(i.pixels[n]);}return out;}
struct NativeReaderMenu::Impl {Artwork art;explicit Impl(LayerRasterizer&r,LayerRasterOptions o):art(r,std::move(o),"reader/menu") {}};
NativeReaderMenu::NativeReaderMenu(LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}NativeReaderMenu::~NativeReaderMenu()=default;
void NativeReaderMenu::syncContent(const modules::ReaderArtwork&a){impl_->art.load(a);}
bool NativeReaderMenu::setFeedback(std::optional<P>p,bool pressed,bool reduced,double t){return impl_->art.feedback(p,pressed,reduced,t);}
void NativeReaderMenu::updatePose(const M&w,float opacity,double t,std::span<const PlaneMask>masks,std::optional<PlaneShutter>shutter){auto&i=*impl_;i.art.rootPose(w,opacity,masks,std::move(shutter));i.art.pose(t);}
bool NativeReaderMenu::requiresFrames(double t)const{return impl_->art.frames(t);}
void NativeReaderMenu::upload(Renderer&r){auto&i=*impl_;need(i.art.posed,"Place Reader menu before upload");if(i.art.dirty){i.art.group->uploadResources(r);i.art.dirty=false;}else i.art.group->updateLocal(r);i.art.groupUploaded=true;i.art.applyRoot();}
LayerCompositionEntry NativeReaderMenu::entry(){return impl_->art.group->entry();}bool NativeReaderMenu::release(Renderer&r){const bool released=impl_->art.group->releaseResources(r);if(released){impl_->art.groupUploaded=false;impl_->art.dirty=true;}return released;}
}
#endif
