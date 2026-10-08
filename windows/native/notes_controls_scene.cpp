#include "native/notes_controls_scene.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;using Matrix=core::Matrix4;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
double number(const Json&j,double fallback=0){if(j.isNull())return fallback;need(j.isNumber()&&std::isfinite(j.number()),"Invalid Notes control number");return j.number();}
std::string text(const Json&j){return j.isNull()?std::string{}:j.string();}
bool flag(const Json&j,bool fallback=false){return j.isNull()?fallback:j.boolean();}
const Json::Array&children(const Json&j){static const Json::Array empty;return j["children"].isNull()?empty:j["children"].array();}
core::Rect rect(const Json&j){need(j.isArray()&&j.array().size()==4,"Invalid Notes control bounds");const auto&a=j.array();return {number(a[0]),number(a[1]),number(a[2]),number(a[3])};}
bool drawing(const Json&j){const auto k=text(j["kind"]);return !j["contents"].isNull()||!j["backgroundColor"].isNull()||number(j["borderWidth"])>0||k=="shape"||k=="text"||k=="gradient";}
bool sameDependency(const modules::NotesControlsImage&a,const modules::NotesControlsImage&b){return a.layerID==b.layerID&&a.sourceResource==b.sourceResource&&a.rect==b.rect&&a.tint==b.tint&&a.requestedPixels==b.requestedPixels&&a.sourceInTint==b.sourceInTint&&a.resizeAspect==b.resizeAspect;}
constexpr std::array<std::string_view,4> tools{"tool:text","tool:todo","tool:image","tool:drawing"};
struct Compiler {
    const modules::NotesControls&source;std::span<const NativeNotesControlsImage>images;
    NotesControlsScenePlan result;Json::Array leaves;std::set<std::string,std::less<>>ids;std::size_t visited{};
    Compiler(const modules::NotesControls&s,std::span<const NativeNotesControlsImage>i):source(s),images(i){}
    void emit(Json node,Matrix world,double opacity,std::size_t toolbar,std::size_t feedback,bool rim){
        const auto id=text(node["id"]);need(!id.empty()&&ids.insert(id).second,"Repeated retained Notes control identity");
        const auto bounds=rect(node["bounds"]);
        node["position"]=Json::Array{world.values[12]+bounds.x,world.values[13]+bounds.y};node["anchorPoint"]=Json::Array{0,0};
        node["transform"]=nullptr;node["zPosition"]=0;node["opacity"]=1;node["children"]=Json::Array{};
        leaves.push_back(std::move(node));result.surfaces.push_back({id,world,static_cast<float>(opacity),toolbar,feedback,rim});
    }
    void visit(Json node,const Matrix&parent,double parentOpacity,std::size_t toolbar,unsigned depth){
        need(depth<=32&&++visited<=LayerRasterizer::maximumNodes,"Notes control descriptor exceeds bounds");
        need(node.isObject()&&!flag(node["hidden"]),"Hidden Notes control structure is unsupported");
        need(node["transform"].isNull()&&node["sublayerTransform"].isNull()&&node["mask"].isNull()&&!flag(node["masksToBounds"]),"Notes controls require their original unclipped local translation tree");
        need(depth==0||number(node["zPosition"])==0,"Notes child paint order requires explicit source ordering");
        const auto b=rect(node["bounds"]);need(b.width>=0&&b.height>=0&&b.width<=8192&&b.height<=8192,"Notes control dimensions exceed source bounds");
        const auto&p=node["position"].array();const auto&a=node["anchorPoint"].array();need(p.size()==2&&a.size()==2&&number(a[0])==0&&number(a[1])==0,"Notes control placement differs from source origin anchoring");
        const auto world=parent*Matrix::translation(number(p[0])-b.x,number(p[1])-b.y);
        const auto ownOpacity=number(node["opacity"],1);need(ownOpacity>=0&&ownOpacity<=1,"Invalid Notes control opacity");
        need(ownOpacity==1||children(node).empty()||!flag(node["allowsGroupOpacity"],true),"Nested grouped control opacity needs a GPU group pass");
        const auto id=text(node["id"]);for(std::size_t n=0;n<tools.size();++n)if(id==tools[n])toolbar=n;
        std::size_t feedback=NotesControlSurface::none;bool rim{};
        for(std::size_t n=0;n<source.feedback().size();++n){const auto&f=source.feedback()[n];if(id==f.tintLayerID||id==f.rimLayerID){need(feedback==NotesControlSurface::none,"Ambiguous Notes feedback identity");feedback=n;rim=id==f.rimLayerID;}}
        if(!node["requiredSourceImage"].isNull()){
            const auto dependency=std::find_if(source.images().begin(),source.images().end(),[&](const auto&d){return d.layerID==id;});need(dependency!=source.images().end(),"Unknown Notes image dependency");
            const auto supplied=std::find_if(images.begin(),images.end(),[&](const auto&i){return i.dependency.layerID==id;});need(supplied!=images.end()&&sameDependency(*dependency,supplied->dependency),"Missing or mismatched source-prepared Notes artwork");
            const auto asset=text(supplied->contents["asset"]),sha=text(supplied->contents["sha256"]);
            need(supplied->contents.isObject()&&!asset.empty()&&Json::validUtf8(asset)&&sha.size()==64&&sha.find_first_not_of("0123456789abcdef")==std::string::npos,"Notes image binding requires an exact asset and SHA-256");
            node["contents"]=supplied->contents;node.erase("requiredSourceImage");
        }
        const auto savedChildren=children(node);const auto border=number(node["borderWidth"]);const bool splitBorder=border>0&&!savedChildren.empty();
        auto own=node;if(splitBorder)own["borderWidth"]=0;
        if(drawing(own))emit(std::move(own),world,parentOpacity*(feedback==NotesControlSurface::none?ownOpacity:1),toolbar,feedback,rim);
        for(const auto&child:savedChildren)visit(child,world,parentOpacity*ownOpacity,toolbar,depth+1);
        // CALayer paints its border AFTER children, unlike background/contents.
        if(splitBorder){auto edge=node;edge["id"]=id+"/native-border";edge["kind"]="layer";edge["backgroundColor"]=nullptr;edge["contents"]=nullptr;edge.erase("shape");edge.erase("text");edge.erase("gradient");emit(std::move(edge),world,parentOpacity*ownOpacity,toolbar,NotesControlSurface::none,false);}
    }
};
}
NotesControlsScenePlan prepareNotesControlsScene(const modules::NotesControls& source,std::span<const NativeNotesControlsImage>images){
    need(source.contentRevision()!=0,"Initialize Notes controls before compiling artwork");
    need(images.size()==source.images().size(),"Notes image bindings must exactly cover source dependencies");
    std::set<std::string,std::less<>>imageIDs;for(const auto&i:images)need(imageIDs.insert(i.dependency.layerID).second,"Repeated Notes image binding");
    Compiler c{source,images};c.result.requiresGroupOpacity=text(source.artwork()["id"])=="notes.menu"||text(source.artwork()["id"])=="notes.confirmation";
    c.visit(source.artwork(),{},1,NotesControlSurface::none,0);
    need(c.result.surfaces.size()<=LayerRasterizer::maximumEntries,"Notes controls exceed retained surface capacity");
    for(std::size_t n=0;n<source.feedback().size();++n)for(bool rim:{false,true})need(std::count_if(c.result.surfaces.begin(),c.result.surfaces.end(),[&](const auto&s){return s.feedback==n&&s.rim==rim;})==1,"Source Notes feedback does not resolve exactly once");
    // Source zPosition selects sibling paint order; caller composition owns that
    // order. It must not become a two-million-point depth displacement.
    c.result.layers=Json::Object{{"bounds",source.artwork()["bounds"]},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",std::move(c.leaves)}};
    return std::move(c.result);
}

#ifdef _WIN32
struct NativeNotesControlsScene::Impl {
    struct Track{double from{},target{},start{},duration{};bool active{};};
    modules::NotesControls*source;LayerScene scene;LayerRasterOptions options;
    std::vector<NotesControlSurface>surfaces;std::vector<LayerPlacement>placements;
    std::vector<std::array<PlaneMask,8>>masks;std::vector<std::array<Track,2>>tracks;
    std::uint64_t sourceRevision{},imageRevision{},feedbackRevision{};std::optional<double>lastTime;
    bool group{},posed{};Matrix previousWorld;float previousOpacity{};std::array<double,4>previousOffsets{};
    std::array<PlaneMask,8>previousMasks{};std::size_t previousMaskCount{};std::optional<PlaneShutter>previousShutter;
    NativeNotesControlsSceneStats stats;
    Impl(modules::NotesControls&s,LayerRasterizer&r,LayerRasterOptions o):source(&s),scene(r),options(std::move(o)){}
    void time(double t)const{need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Notes controls require a finite monotonic owner clock");}
    static double sample(const Track&t,double now){if(!t.active||t.duration<=0)return t.target;return t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value((now-t.start)/t.duration);}
};
NativeNotesControlsScene::NativeNotesControlsScene(modules::NotesControls&s,LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(s,r,std::move(o))){}
NativeNotesControlsScene::~NativeNotesControlsScene()=default;
bool NativeNotesControlsScene::syncContent(std::span<const NativeNotesControlsImage>images,std::uint64_t imageRevision){
    auto&i=*impl_;if(i.sourceRevision==i.source->contentRevision()&&i.sourceRevision&&i.imageRevision==imageRevision)return false;
    auto plan=prepareNotesControlsScene(*i.source,images);std::vector<LayerPlacement>placements(plan.surfaces.size());std::vector<std::array<PlaneMask,8>>masks(plan.surfaces.size());
    std::vector<std::array<Impl::Track,2>>tracks(i.source->feedback().size());for(std::size_t n=0;n<tracks.size();++n){const auto&f=i.source->feedback()[n];tracks[n][0].from=tracks[n][0].target=f.tintOpacity;tracks[n][1].from=tracks[n][1].target=f.rimOpacity;}
    for(std::size_t n=0;n<placements.size();++n){const auto&s=plan.surfaces[n];auto&p=placements[n];p.surface=n;p.world=s.local;p.opacity=s.opacity;
        if(s.feedback!=NotesControlSurface::none)p.opacity*=static_cast<float>(tracks[s.feedback][s.rim?1:0].target);}
    i.scene.load(plan.layers,i.options);need(i.scene.report().unsupported.empty()&&i.scene.draws().size()==plan.surfaces.size(),"Notes controls contain unsupported raster artwork");
    for(std::size_t n=0;n<plan.surfaces.size();++n)need(i.scene.surfaceIndex(plan.surfaces[n].id)==n,"Notes control paint order changed during rasterization");
    i.scene.setPlacements(placements);i.scene.prepareDraws();
    i.surfaces=std::move(plan.surfaces);i.placements=std::move(placements);i.masks=std::move(masks);i.tracks=std::move(tracks);i.group=plan.requiresGroupOpacity;
    i.sourceRevision=i.source->contentRevision();i.imageRevision=imageRevision;i.feedbackRevision=i.source->feedbackRevision();i.posed=false;++i.stats.contentUpdates;return true;
}
bool NativeNotesControlsScene::setFeedback(std::optional<std::string_view>action,bool pressed,bool reduced,double time){
    auto&i=*impl_;i.time(time);need(i.sourceRevision&&i.sourceRevision==i.source->contentRevision(),"Synchronize Notes controls before feedback");
    const bool changed=i.source->setFeedback(action,pressed,reduced);if(!changed&&i.feedbackRevision==i.source->feedbackRevision())return false;
    for(std::size_t n=0;n<i.tracks.size();++n){const auto&f=i.source->feedback()[n];for(unsigned k=0;k<2;++k){auto&t=i.tracks[n][k];const auto target=k?f.rimOpacity:f.tintOpacity;
        // A changed control restarts both source animations from presentation
        // values (even an unchanged rim target). Unrelated controls keep their
        // already-running release rather than jumping to their model targets.
        if(t.target==target&&!reduced&&f.duration==0)continue;
        const auto value=Impl::sample(t,time);t={value,target,time,f.duration,f.duration>0&&value!=target};}}
    i.feedbackRevision=i.source->feedbackRevision();i.lastTime=time;i.posed=false;++i.stats.feedbackChanges;return true;
}
bool NativeNotesControlsScene::updatePose(const Matrix&world,float opacity,double time,std::array<double,4>offsets,std::span<const PlaneMask>ownerMasks,std::optional<PlaneShutter>shutter){
    auto&i=*impl_;i.time(time);need(i.sourceRevision&&i.sourceRevision==i.source->contentRevision()&&i.feedbackRevision==i.source->feedbackRevision(),"Synchronize Notes controls before placement");
    need(world.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1&&ownerMasks.size()<=8,"Invalid Notes control placement");
    need(!i.group||opacity==0||opacity==1,"Menu opacity requires retained group composition; per-leaf fading is not equivalent");
    for(double y:offsets)need(std::isfinite(y)&&std::abs(y)<=8192,"Invalid Notes toolbar displacement");
    if(shutter)validatePlaneShutter(*shutter);
    bool active{};for(const auto&pair:i.tracks)for(const auto&t:pair)active|=t.active;
    bool sameMasks=i.previousMaskCount==ownerMasks.size();for(std::size_t n=0;sameMasks&&n<ownerMasks.size();++n)sameMasks=i.previousMasks[n].worldToLocal==ownerMasks[n].worldToLocal&&i.previousMasks[n].bounds==ownerMasks[n].bounds&&i.previousMasks[n].cornerRadius==ownerMasks[n].cornerRadius;
    // Shutter is fixed-sized numeric data. Comparison needs no heap/string work.
    const auto sameShutter=[&]{return i.previousShutter==shutter;};
    if(i.posed&&!active&&i.previousWorld==world&&i.previousOpacity==opacity&&i.previousOffsets==offsets&&sameMasks&&sameShutter()){i.lastTime=time;return false;}
    for(std::size_t n=0;n<i.surfaces.size();++n){const auto&s=i.surfaces[n];auto&p=i.placements[n];const auto move=s.toolbar==NotesControlSurface::none?0:offsets[s.toolbar];
        p.world=world*Matrix::translation(0,move)*s.local;p.opacity=s.opacity*opacity;
        if(s.feedback!=NotesControlSurface::none)p.opacity*=static_cast<float>(Impl::sample(i.tracks[s.feedback][s.rim?1:0],time));
        std::copy(ownerMasks.begin(),ownerMasks.end(),i.masks[n].begin());p.masks={i.masks[n].data(),ownerMasks.size()};
    }
    i.scene.setPlacements(i.placements);i.scene.setGroupShutter(shutter);
    for(auto&pair:i.tracks)for(auto&t:pair)if(time>=t.start+t.duration)t.active=false;
    i.previousWorld=world;i.previousOpacity=opacity;i.previousOffsets=offsets;i.previousMaskCount=ownerMasks.size();std::copy(ownerMasks.begin(),ownerMasks.end(),i.previousMasks.begin());i.previousShutter=shutter;
    i.lastTime=time;i.posed=true;++i.stats.poseUpdates;return true;
}
bool NativeNotesControlsScene::requiresFrames(double time)const{const auto&i=*impl_;i.time(time);for(const auto&pair:i.tracks)for(const auto&t:pair)if(t.active&&time<t.start+t.duration)return true;return false;}
LayerScene&NativeNotesControlsScene::scene()noexcept{return impl_->scene;}
const LayerScene&NativeNotesControlsScene::scene()const noexcept{return impl_->scene;}
bool NativeNotesControlsScene::requiresGroupOpacity()const noexcept{return impl_->group;}
NativeNotesControlsSceneStats NativeNotesControlsScene::stats()const noexcept{return impl_->stats;}
#endif
} // namespace endfield::native
