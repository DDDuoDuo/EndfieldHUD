#include "native/shelf_scene.hpp"
#include "core/motion.hpp"
#include "core/source_camera.hpp"
#include "core/subsection_transition.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;using Matrix=core::Matrix4;using Feedback=modules::ShelfPresentationFeedback;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
double number(const Json&j,double d=0){if(j.isNull())return d;need(j.isNumber()&&std::isfinite(j.number()),"Invalid Shelf layer number");return j.number();}
bool flag(const Json&j,bool d=false){return j.isNull()?d:j.boolean();}
std::string text(const Json&j){return j.isNull()?std::string{}:j.string();}
const Json::Array&children(const Json&j){static const Json::Array empty;return j["children"].isNull()?empty:j["children"].array();}
core::Rect rect(const Json&j){need(j.isArray()&&j.array().size()==4,"Invalid Shelf layer bounds");const auto&a=j.array();return {number(a[0]),number(a[1]),number(a[2]),number(a[3])};}
bool drawing(const Json&j){auto kind=text(j["kind"]);return kind=="shape"||kind=="text"||kind=="gradient"||!j["contents"].isNull()||!j["backgroundColor"].isNull()||number(j["borderWidth"])>0;}
bool sameDependency(const modules::ShelfPresentationImage&a,const modules::ShelfPresentationImage&b){return a.kind==b.kind&&a.layerID==b.layerID&&a.itemID==b.itemID&&a.sourceResource==b.sourceResource&&a.lastKnownPath==b.lastKnownPath&&a.rect==b.rect&&a.tint==b.tint&&a.requestedPixels==b.requestedPixels&&a.isDirectory==b.isDirectory&&a.unavailable==b.unavailable&&a.sourceInTint==b.sourceInTint&&a.resizeAspect==b.resizeAspect;}
Json root(Json::Array leaves={}){return Json::Object{{"bounds",Json::Array{0,0,0,0}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"allowsGroupOpacity",false},{"children",std::move(leaves)}};}
struct Compiler {
    std::span<const NativeShelfImage>images;std::span<const modules::ShelfPresentationImage>dependencies;std::span<const Feedback>feedback;
    ShelfScenePart result;Json::Array leaves;std::set<std::string,std::less<>>ids;std::size_t visited{};
    void emit(Json node,const Matrix&world,double opacity,bool toolbar,std::size_t feedbackIndex,bool rim){
        const auto id=text(node["id"]);need(!id.empty()&&ids.insert(id).second,"Repeated Shelf surface identity");const auto b=rect(node["bounds"]);
        node["position"]=Json::Array{world.values[12]+b.x,world.values[13]+b.y};node["anchorPoint"]=Json::Array{0,0};
        node["transform"]=nullptr;node["zPosition"]=0;node["opacity"]=1;node["children"]=Json::Array{};
        leaves.push_back(std::move(node));result.surfaces.push_back({id,world,static_cast<float>(opacity),feedbackIndex,rim,toolbar,id=="shelf.drop"});
    }
    void visit(Json node,const Matrix&parent={},double inheritedOpacity=1,bool toolbar=false,unsigned depth=0){
        need(depth<=32&&++visited<=LayerRasterizer::maximumNodes,"Shelf descriptor exceeds retained bounds");
        need(node.isObject()&&!flag(node["hidden"]),"Hidden Shelf layer requires explicit owner handling");
        need(node["transform"].isNull()&&node["sublayerTransform"].isNull()&&node["mask"].isNull()&&!flag(node["masksToBounds"]),"Shelf artwork must retain its original local translation tree");
        need(number(node["zPosition"])==0,"Shelf paint order must be supplied explicitly");
        const auto b=rect(node["bounds"]);need(b.width>=0&&b.height>=0&&b.width<=8192&&b.height<=8192,"Shelf dimensions exceed local raster bounds");
        const auto&p=node["position"].array();const auto&a=node["anchorPoint"].array();need(p.size()==2&&a.size()==2&&number(a[0])==0&&number(a[1])==0,"Shelf origin anchoring changed");
        const auto world=parent*Matrix::translation(number(p[0])-b.x,number(p[1])-b.y);
        const auto opacity=number(node["opacity"],1);need(opacity>=0&&opacity<=1,"Invalid Shelf opacity");
        need(opacity==1||children(node).empty()||!flag(node["allowsGroupOpacity"],true),"Nested Shelf group opacity requires a retained group");
        const auto id=text(node["id"]);toolbar|=id=="shelf.toolbar";
        std::size_t feedbackIndex=ShelfSceneSurface::none;bool rim{};
        for(const auto&f:feedback)if(id==f.tintLayerID||id==f.rimLayerID){
            need(feedbackIndex==ShelfSceneSurface::none,"Ambiguous Shelf highlight identity");rim=id==f.rimLayerID;
            auto existing=std::find_if(result.feedback.begin(),result.feedback.end(),[&](const auto&v){return v.actionID==f.actionID;});
            if(existing==result.feedback.end()){result.feedback.push_back(f);feedbackIndex=result.feedback.size()-1;}else feedbackIndex=static_cast<std::size_t>(existing-result.feedback.begin());
        }
        if(!node["requiredSourceImage"].isNull()||!node["requiredNativeFileIcon"].isNull()){
            const auto d=std::find_if(dependencies.begin(),dependencies.end(),[&](const auto&v){return v.layerID==id;});
            const auto image=std::find_if(images.begin(),images.end(),[&](const auto&v){return v.dependency.layerID==id;});
            need(d!=dependencies.end()&&image!=images.end()&&sameDependency(*d,image->dependency),"Missing or mismatched source-prepared Shelf image");
            if(image->contents.contains("memoryImage")){
                const auto key=text(image->contents["memoryImage"]);
                need(d->kind==modules::ShelfPresentationImage::Kind::nativeFileIcon&&image->contents.isObject()&&image->contents.object().size()==2&&
                     !key.empty()&&key.size()<=512&&Json::validUtf8(key)&&image->contents["revision"].isNumber()&&image->contents["revision"].integer()>0,
                     "Shelf memory artwork requires an exact native icon key/revision");
            }else{
                const auto asset=text(image->contents["asset"]),sha=text(image->contents["sha256"]);
                need(image->contents.isObject()&&image->contents.object().size()==2&&!asset.empty()&&Json::validUtf8(asset)&&sha.size()==64&&sha.find_first_not_of("0123456789abcdef")==std::string::npos,"Shelf image must provide pinned raster metadata");
            }
            node["contents"]=image->contents;node.erase("requiredSourceImage");node.erase("requiredNativeFileIcon");
        }
        const auto savedChildren=children(node);const bool splitBorder=number(node["borderWidth"])>0&&!savedChildren.empty();
        auto own=node;if(splitBorder)own["borderWidth"]=0;
        if(drawing(own))emit(std::move(own),world,inheritedOpacity*(feedbackIndex==ShelfSceneSurface::none?opacity:1),toolbar,feedbackIndex,rim);
        for(const auto&child:savedChildren)visit(child,world,inheritedOpacity*opacity,toolbar,depth+1);
        if(splitBorder){auto edge=node;edge["id"]=id+"/native-border";edge["kind"]="layer";edge["backgroundColor"]=nullptr;edge["contents"]=nullptr;edge.erase("shape");edge.erase("text");edge.erase("gradient");emit(std::move(edge),world,inheritedOpacity*opacity,toolbar,ShelfSceneSurface::none,false);}
    }
    ShelfScenePart finish(){
        need(result.surfaces.size()<=LayerRasterizer::maximumEntries,"Shelf part exceeds retained surface capacity");
        for(std::size_t n=0;n<result.feedback.size();++n)for(bool rim:{false,true})need(std::count_if(result.surfaces.begin(),result.surfaces.end(),[&](const auto&s){return s.feedback==n&&s.rim==rim;})==1,"Shelf highlight must resolve exactly once");
        // Consecutive immutable artwork shares one local bitmap. Highlights
        // remain separate numeric surfaces and never enter these groups. This
        // bounds two live generations of eight cards without deleting raster
        // metadata belonging to the still-published generation.
        Json::Array compact;std::vector<ShelfSceneSurface>surfaces;compact.reserve(leaves.size());surfaces.reserve(result.surfaces.size());
        for(std::size_t begin=0;begin<leaves.size();){std::size_t end=begin+1;
            const auto&first=result.surfaces[begin];if(first.feedback==ShelfSceneSurface::none&&!first.drop)while(end<leaves.size()&&result.surfaces[end].feedback==ShelfSceneSurface::none&&result.surfaces[end].toolbar==first.toolbar&&!result.surfaces[end].drop)++end;
            if(end-begin==1){compact.push_back(std::move(leaves[begin]));surfaces.push_back(std::move(result.surfaces[begin]));}
            else {Json::Array groupChildren;groupChildren.reserve(end-begin);for(std::size_t n=begin;n<end;++n){leaves[n]["opacity"]=result.surfaces[n].opacity;groupChildren.push_back(std::move(leaves[n]));}
                const auto id=first.id+"/retained-static";auto group=root(std::move(groupChildren));group["id"]=id;group["bounds"]=result.itemID.empty()?Json(Json::Array{0,0,400,334}):Json(Json::Array{0,0,184,74});
                compact.push_back(std::move(group));surfaces.push_back({id,{},1,ShelfSceneSurface::none,false,first.toolbar});}
            begin=end;
        }
        result.surfaces=std::move(surfaces);result.layers=root(std::move(compact));return std::move(result);
    }
};
}
namespace {
using Point=core::Point;
Point interpolate(Point a,Point b,double t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};}
Json pathCommand(const char*op,std::initializer_list<Point>points={}){Json::Array out;for(auto p:points)out.push_back(Json::Array{p.x,p.y});return Json::Object{{"op",op},{"points",std::move(out)}};}
struct DropSegment{Point start,c1,c2,end;bool cubic;};
std::array<DropSegment,9>dropSegments(){constexpr double w=382,h=248,r=5,k=r*.5522847498;
    return {{{{w,h/2},{},{},{w,h-r},false},{{w,h-r},{w,h-r+k},{w-r+k,h},{w-r,h},true},
        {{w-r,h},{},{},{r,h},false},{{r,h},{r-k,h},{0,h-r+k},{0,h-r},true},
        {{0,h-r},{},{},{0,r},false},{{0,r},{0,r-k},{r-k,0},{r,0},true},
        {{r,0},{},{},{w-r,0},false},{{w-r,0},{w-r+k,0},{w,r-k},{w,r},true},
        {{w,r},{},{},{w,h/2},false}}};
}
double cubicLength(const DropSegment&s,double end=1){
    // Composite Simpson integration of the smooth, nondegenerate 5pt source
    // quarter ellipse. Only four equal curves; no arbitrary path integration.
    constexpr unsigned steps=128;const double h=end/steps;double sum{};
    for(unsigned n=0;n<=steps;++n){const auto t=h*n,u=1-t;const auto x=3*(u*u*(s.c1.x-s.start.x)+2*u*t*(s.c2.x-s.c1.x)+t*t*(s.end.x-s.c2.x));const auto y=3*(u*u*(s.c1.y-s.start.y)+2*u*t*(s.c2.y-s.c1.y)+t*t*(s.end.y-s.c2.y));sum+=(n==0||n==steps?1:n%2?4:2)*std::hypot(x,y);}return sum*h/3;
}
[[maybe_unused]] Json curveLayer(const core::SubsectionCurvePath&path){
    Json::Array commands;std::size_t at{};const auto point=[&](){const Point p{path.coordinates[at],path.coordinates[at+1]};at+=2;return p;};
    for(std::size_t n=0;n<path.opcodeCount;++n)switch(path.opcodes[n]){
        case 0:commands.push_back(pathCommand("move",{point()}));break;
        case 1:commands.push_back(pathCommand("line",{point()}));break;
        case 2:{const auto a=point(),b=point();commands.push_back(pathCommand("quadratic",{a,b}));break;}
        case 3:{const auto a=point(),b=point(),c=point();commands.push_back(pathCommand("cubic",{a,b,c}));break;}
        case 4:commands.push_back(pathCommand("close"));break;
        default:need(false,"Invalid sampled source mask opcode");
    }
    need(at==path.coordinateCount,"Sampled source mask coordinate mismatch");
    Json result=Json::Object{{"id","shelf.curved-mask"},{"kind","shape"},{"bounds",Json::Array{9,40,382,248}},
        {"position",Json::Array{9,40}},{"anchorPoint",Json::Array{0,0}}};
    Json shape=Json::Object{{"path",std::move(commands)},{"fillRule","non-zero"}};
    shape["fillColor"]=Json::Object{{"sRGB",Json::Array{1,1,1,1}}};result["shape"]=std::move(shape);return result;
}
[[maybe_unused]] const Json*findLayer(const Json&node,std::string_view id){if(text(node["id"])==id)return &node;for(const auto&child:children(node))if(const auto*p=findLayer(child,id))return p;return nullptr;}
}
Json shelfDropStrokePath(double end){
    need(std::isfinite(end)&&end>=0&&end<=1,"Invalid source Shelf strokeEnd");const auto segments=dropSegments();
    const double curve=cubicLength(segments[1]);double total{};for(const auto&s:segments)total+=s.cubic?curve:std::hypot(s.end.x-s.start.x,s.end.y-s.start.y);
    double remaining=end*total;Json::Array out{pathCommand("move",{segments[0].start})};
    for(const auto&s:segments){if(remaining<=0)break;const double length=s.cubic?curve:std::hypot(s.end.x-s.start.x,s.end.y-s.start.y);
        if(remaining>=length){if(s.cubic)out.push_back(pathCommand("cubic",{s.c1,s.c2,s.end}));else out.push_back(pathCommand("line",{s.end}));remaining-=length;continue;}
        if(!s.cubic)out.push_back(pathCommand("line",{interpolate(s.start,s.end,remaining/length)}));
        else{double lo=0,hi=1;for(unsigned n=0;n<44;++n){const auto mid=(lo+hi)*.5;if(cubicLength(s,mid)<remaining)lo=mid;else hi=mid;}const auto t=(lo+hi)*.5;const auto a=interpolate(s.start,s.c1,t),b=interpolate(s.c1,s.c2,t),c=interpolate(s.c2,s.end,t),d=interpolate(a,b,t),e=interpolate(b,c,t);out.push_back(pathCommand("cubic",{a,d,interpolate(d,e,t)}));}
        remaining=0;
    }
    if(end==1){out.pop_back();out.push_back(pathCommand("close"));}return out;
}
ShelfScenePlan prepareShelfScene(const modules::ShelfPresentation&source,std::span<const NativeShelfImage>images){
    need(source.chromeRevision()!=0,"Initialize Shelf presentation before native preparation");need(source.cards().size()<=8,"Shelf native card capacity exceeded");
    std::size_t expected=source.chromeImages().size();for(const auto&card:source.cards())expected+=card.images.size();need(images.size()==expected,"Shelf images must exactly cover visible source dependencies");
    std::set<std::string,std::less<>>imageIDs;for(const auto&i:images)need(imageIDs.insert(i.dependency.layerID).second,"Repeated Shelf image binding");
    ShelfScenePlan result;const auto&parts=children(source.chrome());need(parts.size()==5&&text(parts[0]["id"])=="shelf.collection"&&text(parts[1]["id"])=="shelf.drop"&&text(parts[4]["id"])=="shelf.toolbar","Unexpected source Shelf paint structure");
    Compiler collection{images,source.chromeImages(),source.chromeFeedback(),{}, {}, {},0};auto tree=parts[0];need(!tree["mask"].isNull(),"Missing source Shelf collection clip");tree.erase("mask");collection.visit(std::move(tree));result.collection=collection.finish();
    Compiler foreground{images,source.chromeImages(),source.chromeFeedback(),{}, {}, {},0};for(std::size_t n=1;n<parts.size();++n)foreground.visit(parts[n]);result.foreground=foreground.finish();
    result.cards.reserve(source.cards().size());for(const auto&card:source.cards()){
        Compiler c{images,card.images,card.feedback,{}, {}, {},0};c.result.itemID=card.itemID;c.visit(card.artwork);result.cards.push_back(c.finish());
    }
    Compiler scrollbar{{},{},{},{},{},{},0};if(auto r=source.scrollIndicator()){
        const auto color=source.scrollIndicatorColor();Json node=Json::Object{{"id","shelf.scrollbar"},{"kind","layer"},{"bounds",Json::Array{0,0,r->width,r->height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"cornerRadius",1},{"backgroundColor",Json::Object{{"sRGB",Json::Array{color[0],color[1],color[2],color[3]}}}},{"children",Json::Array{}}};scrollbar.visit(std::move(node));
    }result.scrollbar=scrollbar.finish();return result;
}

#ifdef _WIN32
struct NativeShelfScene::Impl {
    struct Track{double from{},target{},start{},duration{};bool active{};};
    struct Part {
        ShelfScenePart plan;LayerScene scene;std::vector<LayerPlacement>placements,activePlacements;std::array<PlaneMask,8>masks{};
        std::vector<std::array<Track,2>>tracks;core::Rect full{};double selectionY{},selectionZ{};std::uint64_t sourceRevision{};
        Part(LayerRasterizer&r,ShelfScenePart p,const LayerRasterOptions&options):plan(std::move(p)),scene(r),placements(plan.surfaces.size()),tracks(plan.feedback.size()){
            activePlacements.reserve(placements.size());scene.load(plan.layers,options);need(scene.report().unsupported.empty(),"Shelf contains unsupported local raster artwork");
            // Empty strings / fully transparent paths may legitimately have no
            // ink. Their numeric surface is omitted, preserving all later IDs.
            for(std::size_t n=0;n<plan.surfaces.size();++n){const auto index=scene.surfaceIndex(plan.surfaces[n].id);placements[n].surface=index.value_or(ShelfSceneSurface::none);placements[n].world=plan.surfaces[n].local;placements[n].opacity=0;if(index)activePlacements.push_back(placements[n]);}
            // New content cannot flash unplaced cards or normalized highlights.
            // The owner supplies its first complete pose before publication.
            scene.setPlacements(activePlacements);scene.prepareDraws();
            for(std::size_t n=0;n<tracks.size();++n){tracks[n][0].from=tracks[n][0].target=plan.feedback[n].tintOpacity;tracks[n][1].from=tracks[n][1].target=plan.feedback[n].rimOpacity;}
        }
    };
    modules::ShelfPresentation*source;LayerRasterizer*raster;LayerRasterOptions options;
    std::unique_ptr<Part>collection,scrollbar,foreground;std::vector<std::unique_ptr<Part>>cards,retired;
    std::vector<LayerCompositionEntry>entries;std::vector<NativeShelfImage>images;
    std::uint64_t chromeRevision{},placementRevision{},imageRevision{},compositionRevision{};std::optional<double>lastTime;
    NativeShelfSceneStats stats;DrawObject validation;
    std::shared_ptr<const core::SubsectionMaskSampler>sampler;std::optional<core::SubsectionCurvePath>maskPath;
    std::optional<PlaneAlphaMask>alpha;std::shared_ptr<const LayerRasterImage>maskImage;
    std::string maskID;std::uint64_t maskRevision{},uploadedMask{};bool maskDirty{};Renderer*resourceOwner{};
    double dropEnd{1};std::optional<double>uploadedDrop;Part*dropPart{};std::uint64_t dropRevision{};
    bool revealActive{};double revealEnds{};
    Impl(modules::ShelfPresentation&s,LayerRasterizer&r,LayerRasterOptions o,std::shared_ptr<const core::SubsectionMaskSampler>samples):source(&s),raster(&r),options(std::move(o)),sampler(std::move(samples)){if(sampler)need(sampler->sourceViewport()==core::SubsectionMaskSampler::viewport,"Shelf mask must match original source viewport");static std::atomic<std::uint64_t>sequence{};maskID="shm"+std::to_string(sequence.fetch_add(1,std::memory_order_relaxed));cards.reserve(8);retired.reserve(11);entries.reserve(11);validation.sourceID="shelf.pose";validation.masks.reserve(8);}
    void time(double value)const{need(std::isfinite(value)&&(!lastTime||value>=*lastTime),"Shelf feedback requires a finite monotonic owner clock");}
    static bool sameArtwork(const ShelfScenePart&a,const ShelfScenePart&b){
        if(a.layers!=b.layers||a.surfaces.size()!=b.surfaces.size()||a.feedback.size()!=b.feedback.size())return false;
        for(std::size_t n=0;n<a.surfaces.size();++n){const auto&x=a.surfaces[n];const auto&y=b.surfaces[n];if(x.id!=y.id||x.local!=y.local||x.opacity!=y.opacity||x.feedback!=y.feedback||x.rim!=y.rim||x.toolbar!=y.toolbar||x.drop!=y.drop)return false;}
        for(std::size_t n=0;n<a.feedback.size();++n){const auto&x=a.feedback[n];const auto&y=b.feedback[n];if(x.actionID!=y.actionID||x.tintLayerID!=y.tintLayerID||x.rimLayerID!=y.rimLayerID||x.framed!=y.framed)return false;}return true;
    }
    static double sample(const Track&t,double now){return !t.active||t.duration<=0?t.target:t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value((now-t.start)/t.duration);}
    template<class F>void each(F&&f){if(collection)f(*collection);for(auto&c:cards)f(*c);if(scrollbar)f(*scrollbar);if(foreground)f(*foreground);}
    template<class F>void each(F&&f)const{if(collection)f(*collection);for(const auto&c:cards)f(*c);if(scrollbar)f(*scrollbar);if(foreground)f(*foreground);}
    void updatePositions(){for(std::size_t n=0;n<cards.size();++n){const auto&c=source->cards()[n];cards[n]->full=c.full;cards[n]->selectionY=c.selectionY;cards[n]->selectionZ=c.selectionZ;cards[n]->sourceRevision=c.contentRevision;}if(scrollbar)scrollbar->full=source->scrollIndicator().value_or(core::Rect{});placementRevision=source->placementRevision();}
    const Feedback*feedback(std::string_view id)const{for(const auto&f:source->chromeFeedback())if(f.actionID==id)return &f;for(const auto&c:source->cards())for(const auto&f:c.feedback)if(f.actionID==id)return &f;return nullptr;}
    bool current()const {if(!chromeRevision||chromeRevision!=source->chromeRevision()||placementRevision!=source->placementRevision()||cards.size()!=source->cards().size())return false;for(std::size_t n=0;n<cards.size();++n)if(cards[n]->plan.itemID!=source->cards()[n].itemID||cards[n]->sourceRevision!=source->cards()[n].contentRevision)return false;return true;}
};
NativeShelfScene::NativeShelfScene(modules::ShelfPresentation&s,LayerRasterizer&r,LayerRasterOptions o,std::shared_ptr<const core::SubsectionMaskSampler>samples):impl_(std::make_unique<Impl>(s,r,std::move(o),std::move(samples))){}
NativeShelfScene::~NativeShelfScene(){if(impl_)impl_->raster->remove(impl_->maskID);}
bool NativeShelfScene::syncContent(std::span<const NativeShelfImage>supplied,std::uint64_t imageRevision){
    auto&i=*impl_;bool sameCards=i.cards.size()==i.source->cards().size();for(std::size_t n=0;sameCards&&n<i.cards.size();++n)sameCards=i.cards[n]->plan.itemID==i.source->cards()[n].itemID&&i.cards[n]->sourceRevision==i.source->cards()[n].contentRevision;
    if(i.chromeRevision&&i.chromeRevision==i.source->chromeRevision()&&sameCards&&i.imageRevision==imageRevision){const bool changed=i.placementRevision!=i.source->placementRevision();i.updatePositions();if(changed)++i.stats.contentSynchronizations;return changed;}
    need(i.retired.empty(),"Publish and collect prior Shelf parts before another content replacement");
    auto images=supplied.empty()?i.images:std::vector<NativeShelfImage>(supplied.begin(),supplied.end());auto plan=prepareShelfScene(*i.source,images);
    std::size_t required{};const auto reservePart=[&](const ShelfScenePart&p,const std::unique_ptr<Impl::Part>&existing){if(!existing||!Impl::sameArtwork(existing->plan,p))required+=p.surfaces.size();};
    reservePart(plan.collection,i.collection);reservePart(plan.scrollbar,i.scrollbar);reservePart(plan.foreground,i.foreground);
    for(const auto&p:plan.cards){const auto found=std::find_if(i.cards.begin(),i.cards.end(),[&](const auto&old){return old->plan.itemID==p.itemID&&Impl::sameArtwork(old->plan,p);});if(found==i.cards.end())required+=p.surfaces.size();}
    need(required<=LayerRasterizer::maximumEntries-i.raster->stats().entries,"Shelf replacement exceeds shared raster capacity; retire unused module artwork first");
    const auto inherit=[](Impl::Part&next,const Impl::Part&prior){for(std::size_t n=0;n<next.plan.feedback.size();++n)for(std::size_t old=0;old<prior.plan.feedback.size();++old)if(next.plan.feedback[n].actionID==prior.plan.feedback[old].actionID)next.tracks[n]=prior.tracks[old];};
    const auto build=[&](ShelfScenePart&p,const std::unique_ptr<Impl::Part>&existing){if(existing&&Impl::sameArtwork(existing->plan,p))return std::unique_ptr<Impl::Part>{};auto next=std::make_unique<Impl::Part>(*i.raster,std::move(p),i.options);if(existing)inherit(*next,*existing);return next;};
    auto collection=build(plan.collection,i.collection),scrollbar=build(plan.scrollbar,i.scrollbar),foreground=build(plan.foreground,i.foreground);
    std::array<std::size_t,8>old{};old.fill(8);std::array<std::unique_ptr<Impl::Part>,8>built;
    for(std::size_t n=0;n<plan.cards.size();++n){for(std::size_t k=0;k<i.cards.size();++k)if(i.cards[k]->plan.itemID==plan.cards[n].itemID&&Impl::sameArtwork(i.cards[k]->plan,plan.cards[n])){old[n]=k;break;}if(old[n]==8){built[n]=std::make_unique<Impl::Part>(*i.raster,std::move(plan.cards[n]),i.options);for(const auto&prior:i.cards)if(prior->plan.itemID==built[n]->plan.itemID){inherit(*built[n],*prior);break;}}}
    std::vector<std::unique_ptr<Impl::Part>>next;next.reserve(8);
    // All parsing/rasterization completed before publication/lifetime mutation.
    const auto replace=[&](auto&current,auto&candidate){if(candidate){if(current)i.retired.push_back(std::move(current));current=std::move(candidate);++i.stats.partBuilds;}};
    if(foreground){i.dropPart=nullptr;i.uploadedDrop.reset();}
    replace(i.collection,collection);replace(i.scrollbar,scrollbar);replace(i.foreground,foreground);
    for(std::size_t n=0;n<plan.cards.size();++n){if(old[n]!=8)next.push_back(std::move(i.cards[old[n]]));else{next.push_back(std::move(built[n]));++i.stats.partBuilds;}}
    for(auto&card:i.cards)if(card)i.retired.push_back(std::move(card));i.cards=std::move(next);i.images=std::move(images);
    i.entries.clear();i.each([&](auto&part){i.entries.push_back({&part.scene,{}});});
    i.chromeRevision=i.source->chromeRevision();i.imageRevision=imageRevision;i.updatePositions();++i.compositionRevision;++i.stats.contentSynchronizations;return true;
}
bool NativeShelfScene::setFeedback(std::optional<std::string_view>action,bool pressed,bool reduced,double time){
    auto&i=*impl_;i.time(time);need(i.current(),"Synchronize Shelf content before feedback");
    const bool changed=i.source->setFeedback(action,pressed,reduced);bool retargeted{};
    i.each([&](auto&part){for(std::size_t n=0;n<part.tracks.size();++n){const auto*f=i.feedback(part.plan.feedback[n].actionID);need(f,"Shelf feedback identity changed");auto&pair=part.tracks[n];
        const bool targetsChanged=pair[0].target!=f->tintOpacity||pair[1].target!=f->rimOpacity;
        if(!targetsChanged&&!reduced)continue;
        for(unsigned k=0;k<2;++k){auto&t=pair[k];const auto target=k?f->rimOpacity:f->tintOpacity;const auto from=Impl::sample(t,time);const double duration=reduced?0:f->duration;t={from,target,time,duration,duration>0&&from!=target};}retargeted=true;
    }});i.lastTime=time;if(retargeted)++i.stats.feedbackChanges;return changed||retargeted;
}
bool NativeShelfScene::updatePose(const NativeShelfPose&pose){
    auto&i=*impl_;i.time(pose.time);need(i.current(),"Synchronize Shelf content before placement");
    need(pose.contentWorld.finite()&&std::isfinite(pose.opacity)&&pose.opacity>=0&&pose.opacity<=1&&pose.ownerMasks.size()<=7,"Invalid Shelf owner placement");
    need(pose.collectionSublayerTransform.finite()&&std::isfinite(pose.dropStrokeEnd)&&pose.dropStrokeEnd>=0&&pose.dropStrokeEnd<=1,"Invalid Shelf subsection/drop trace");
    need(!pose.reveal||(!pose.collectionReveal&&pose.collectionSublayerTransform==Matrix{}),"Ambiguous Shelf reveal inputs");
    Matrix sublayer=pose.collectionSublayerTransform;std::optional<core::SubsectionCurvePath>nextPath;bool revealActive{};double revealEnds{};
    if(pose.reveal){need(bool(i.sampler),"Active Shelf reveal needs pinned original CA samples");const auto&r=*pose.reveal;
        need(std::isfinite(r.direction)&&std::isfinite(r.elapsed),"Invalid Shelf reveal time");sublayer=core::sampleSubsectionTransform(r.direction,r.elapsed).sublayerTransform;
        revealActive=r.elapsed<core::SubsectionMaskSampler::duration;revealEnds=pose.time+std::max(0.,core::SubsectionMaskSampler::duration-r.elapsed);
        if(revealActive)nextPath=i.sampler->sample(r.direction,r.elapsed/core::SubsectionMaskSampler::duration);
    }else if(pose.collectionReveal){validatePlaneShutter(PlaneShutter{Matrix{},*pose.collectionReveal});core::SubsectionCurvePath path;
        for(const auto&strip:*pose.collectionReveal){for(std::size_t n=0;n<strip.size();++n){path.opcodes[path.opcodeCount++]=n?1:0;path.coordinates[path.coordinateCount++]=strip[n].x;path.coordinates[path.coordinateCount++]=strip[n].y;}path.opcodes[path.opcodeCount++]=4;}nextPath=path;
    }
    const auto collectionWorld=pose.contentWorld*Matrix::translation(200,167)*sublayer*Matrix::translation(-200,-167);
    std::optional<PlaneAlphaMask>nextAlpha;if(nextPath)nextAlpha=PlaneAlphaMask{core::source::inverseSourceMatrix(pose.contentWorld),i.maskImage?i.maskImage->bounds:modules::ShelfPresentation::contentClip(),i.maskID};
    need(std::isfinite(pose.toolbarY)&&std::isfinite(pose.toolbarZ)&&std::abs(pose.toolbarY)<=8192&&std::abs(pose.toolbarZ)<=8192,"Invalid Shelf toolbar translation");
    need(pose.selections.size()<=i.cards.size(),"Shelf selection pose exceeds visible capacity");std::array<double,8>ys{},zs{};std::array<bool,8>seen{};
    for(std::size_t n=0;n<i.cards.size();++n){ys[n]=i.cards[n]->selectionY;zs[n]=i.cards[n]->selectionZ;}
    for(const auto&s:pose.selections){need(std::isfinite(s.y)&&std::isfinite(s.z)&&std::abs(s.y)<=8192&&std::abs(s.z)<=8192,"Invalid Shelf selection translation");std::size_t index=i.cards.size();for(std::size_t n=0;n<i.cards.size();++n)if(i.cards[n]->plan.itemID==s.itemID){index=n;break;}need(index<i.cards.size()&&!seen[index],"Stale or repeated Shelf selection pose");seen[index]=true;ys[index]=s.y;zs[index]=s.z;}
    const PlaneMask collectionClip{core::source::inverseSourceMatrix(pose.contentWorld),modules::ShelfPresentation::contentClip(),0};
    if(pose.moduleShutter)validatePlaneShutter(*pose.moduleShutter);
    for(const auto&m:pose.ownerMasks)need(m.worldToLocal.finite()&&std::isfinite(m.bounds.x)&&std::isfinite(m.bounds.y)&&std::isfinite(m.bounds.width)&&std::isfinite(m.bounds.height)&&m.bounds.width>0&&m.bounds.height>0&&std::isfinite(m.cornerRadius)&&m.cornerRadius>=0&&m.cornerRadius<=std::min(m.bounds.width,m.bounds.height)*.5,"Invalid Shelf owner clip");
    std::size_t cardIndex{};i.each([&](auto&part){const bool card=!part.plan.itemID.empty(),collection=&part==i.collection.get()||card;Matrix base=pose.contentWorld;
        if(collection)base=collectionWorld;
        if(card){base=base*Matrix::translation(part.full.x,part.full.y+ys[cardIndex],zs[cardIndex]);++cardIndex;}else if(&part==i.scrollbar.get())base=base*Matrix::translation(part.full.x,part.full.y);
        std::copy(pose.ownerMasks.begin(),pose.ownerMasks.end(),part.masks.begin());const auto count=pose.ownerMasks.size()+static_cast<std::size_t>(collection);if(collection)part.masks[pose.ownerMasks.size()]=collectionClip;
        for(std::size_t n=0;n<part.plan.surfaces.size();++n){const auto&s=part.plan.surfaces[n];auto&p=part.placements[n];p.world=base*(s.toolbar?Matrix::translation(0,pose.toolbarY,pose.toolbarZ):Matrix{})*s.local;p.opacity=s.opacity*pose.opacity*(s.drop&&pose.dropStrokeEnd==0?0:1);p.masks={part.masks.data(),count};if(s.feedback!=ShelfSceneSurface::none)p.opacity*=static_cast<float>(Impl::sample(part.tracks[s.feedback][s.rim?1:0],pose.time));i.validation.world=p.world;i.validation.opacity=p.opacity;i.validation.masks.assign(p.masks.begin(),p.masks.end());i.validation.shutter=pose.moduleShutter;i.validation.alphaMask=collection?nextAlpha:std::nullopt;validateDrawObject(i.validation);}
    });
    // Numeric staging above covers every part before changing any LayerScene.
    i.each([&](auto&part){part.activePlacements.clear();for(const auto&p:part.placements)if(p.surface!=ShelfSceneSurface::none)part.activePlacements.push_back(p);part.scene.setPlacements(part.activePlacements);part.scene.setGroupShutter(pose.moduleShutter);part.scene.setGroupAlphaMask((&part==i.collection.get()||!part.plan.itemID.empty())?nextAlpha:std::nullopt);for(auto&pair:part.tracks)for(auto&t:pair)if(pose.time>=t.start+t.duration)t.active=false;});
    if(nextPath!=i.maskPath){i.maskPath=std::move(nextPath);i.maskDirty=bool(i.maskPath);if(i.maskPath&&i.maskPath->topologyGap)++i.stats.topologyGapSamples;}
    i.alpha=std::move(nextAlpha);i.dropEnd=pose.dropStrokeEnd;i.revealActive=revealActive;i.revealEnds=revealEnds;
    i.lastTime=pose.time;++i.stats.poseUpdates;return true;
}
bool NativeShelfScene::uploadAnimations(Renderer&r){
    auto&i=*impl_;need(!i.resourceOwner||i.resourceOwner==&r,"Shelf animation belongs to another renderer");bool changed{};
    if(i.maskPath){
        if(i.maskDirty){auto options=i.options;options.paddingPoints=0;auto image=i.raster->rasterize(i.maskID,++i.maskRevision,curveLayer(*i.maskPath),options);need(image->complete()&&image->width<=1600&&image->height<=1100,"Unsupported/unbounded Shelf curved mask raster");i.maskImage=std::move(image);i.maskDirty=false;++i.stats.maskRasters;}
        if(i.uploadedMask!=i.maskRevision){r.setTexture(i.maskID,i.maskRevision,{i.maskImage->width,i.maskImage->height,i.maskImage->straightRGBA,TextureColorSpace::linear,TextureFilter::linear});i.uploadedMask=i.maskRevision;i.resourceOwner=&r;++i.stats.maskUploads;changed=true;}
        i.alpha->bounds=i.maskImage->bounds;i.collection->scene.setGroupAlphaMask(i.alpha);for(auto&card:i.cards)card->scene.setGroupAlphaMask(i.alpha);
    }
    if(i.foreground&&(i.dropPart!=i.foreground.get()||i.uploadedDrop!=i.dropEnd)){
        if(i.foreground->scene.surfaceIndex("shelf.drop")){
            if(i.dropEnd>0){const auto*original=findLayer(i.foreground->plan.layers,"shelf.drop");need(original,"Source Shelf drop outline missing");auto layer=*original;
                if(i.dropEnd<1)layer["shape"]["path"]=shelfDropStrokePath(i.dropEnd);
                if(i.dropPart==i.foreground.get()||i.dropEnd<1){i.foreground->scene.updateLocalContent("shelf.drop",++i.dropRevision,layer,i.options);++i.stats.dropRasters;changed=true;}
            }
        }i.dropPart=i.foreground.get();i.uploadedDrop=i.dropEnd;
    }
    return changed;
}
bool NativeShelfScene::requiresFrames(double time)const{const auto&i=*impl_;i.time(time);bool active=i.revealActive&&time<i.revealEnds;i.each([&](const auto&p){for(const auto&pair:p.tracks)for(const auto&t:pair)active|=t.active&&time<t.start+t.duration;});return active;}
std::span<const LayerCompositionEntry>NativeShelfScene::entries()const noexcept{return impl_->entries;}
std::uint64_t NativeShelfScene::compositionRevision()const noexcept{return impl_->compositionRevision;}
LayerScene*NativeShelfScene::cardScene(std::string_view id)noexcept{for(auto&p:impl_->cards)if(p->plan.itemID==id)return &p->scene;return nullptr;}
LayerScene&NativeShelfScene::foregroundScene(){need(bool(impl_->foreground),"Synchronize Shelf before borrowing foreground");return impl_->foreground->scene;}
bool NativeShelfScene::collectRetired(Renderer&r){auto&i=*impl_;for(const auto&part:i.retired)if(!part->scene.releaseResources(r))return false;i.retired.clear();if(!i.maskPath&&i.uploadedMask&&(!r.stats().initialized||r.removeTexture(i.maskID))){i.uploadedMask=0;i.maskImage.reset();i.raster->remove(i.maskID);}return true;}
bool NativeShelfScene::releaseResources(Renderer&r){auto&i=*impl_;bool complete=collectRetired(r);i.each([&](auto&p){complete=p.scene.releaseResources(r)&&complete;});if(i.uploadedMask){if(!r.stats().initialized||r.removeTexture(i.maskID)){i.uploadedMask=0;i.maskImage.reset();i.raster->remove(i.maskID);}else complete=false;}return complete;}
NativeShelfSceneStats NativeShelfScene::stats()const noexcept{auto result=impl_->stats;result.cards=impl_->cards.size();result.retiredParts=impl_->retired.size();return result;}
#endif
} // namespace endfield::native
