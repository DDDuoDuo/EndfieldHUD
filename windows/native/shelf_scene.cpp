#include "native/shelf_scene.hpp"
#include "core/motion.hpp"
#include "core/source_camera.hpp"
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
        leaves.push_back(std::move(node));result.surfaces.push_back({id,world,static_cast<float>(opacity),feedbackIndex,rim,toolbar});
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
            const auto&first=result.surfaces[begin];if(first.feedback==ShelfSceneSurface::none)while(end<leaves.size()&&result.surfaces[end].feedback==ShelfSceneSurface::none&&result.surfaces[end].toolbar==first.toolbar)++end;
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
    Impl(modules::ShelfPresentation&s,LayerRasterizer&r,LayerRasterOptions o):source(&s),raster(&r),options(std::move(o)){cards.reserve(8);retired.reserve(11);entries.reserve(11);validation.sourceID="shelf.pose";validation.masks.reserve(8);}
    void time(double value)const{need(std::isfinite(value)&&(!lastTime||value>=*lastTime),"Shelf feedback requires a finite monotonic owner clock");}
    static bool sameArtwork(const ShelfScenePart&a,const ShelfScenePart&b){
        if(a.layers!=b.layers||a.surfaces.size()!=b.surfaces.size()||a.feedback.size()!=b.feedback.size())return false;
        for(std::size_t n=0;n<a.surfaces.size();++n){const auto&x=a.surfaces[n];const auto&y=b.surfaces[n];if(x.id!=y.id||x.local!=y.local||x.opacity!=y.opacity||x.feedback!=y.feedback||x.rim!=y.rim||x.toolbar!=y.toolbar)return false;}
        for(std::size_t n=0;n<a.feedback.size();++n){const auto&x=a.feedback[n];const auto&y=b.feedback[n];if(x.actionID!=y.actionID||x.tintLayerID!=y.tintLayerID||x.rimLayerID!=y.rimLayerID||x.framed!=y.framed)return false;}return true;
    }
    static double sample(const Track&t,double now){return !t.active||t.duration<=0?t.target:t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value((now-t.start)/t.duration);}
    template<class F>void each(F&&f){if(collection)f(*collection);for(auto&c:cards)f(*c);if(scrollbar)f(*scrollbar);if(foreground)f(*foreground);}
    template<class F>void each(F&&f)const{if(collection)f(*collection);for(const auto&c:cards)f(*c);if(scrollbar)f(*scrollbar);if(foreground)f(*foreground);}
    void updatePositions(){for(std::size_t n=0;n<cards.size();++n){const auto&c=source->cards()[n];cards[n]->full=c.full;cards[n]->selectionY=c.selectionY;cards[n]->selectionZ=c.selectionZ;cards[n]->sourceRevision=c.contentRevision;}if(scrollbar)scrollbar->full=source->scrollIndicator().value_or(core::Rect{});placementRevision=source->placementRevision();}
    const Feedback*feedback(std::string_view id)const{for(const auto&f:source->chromeFeedback())if(f.actionID==id)return &f;for(const auto&c:source->cards())for(const auto&f:c.feedback)if(f.actionID==id)return &f;return nullptr;}
    bool current()const {if(!chromeRevision||chromeRevision!=source->chromeRevision()||placementRevision!=source->placementRevision()||cards.size()!=source->cards().size())return false;for(std::size_t n=0;n<cards.size();++n)if(cards[n]->plan.itemID!=source->cards()[n].itemID||cards[n]->sourceRevision!=source->cards()[n].contentRevision)return false;return true;}
};
NativeShelfScene::NativeShelfScene(modules::ShelfPresentation&s,LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(s,r,std::move(o))){}
NativeShelfScene::~NativeShelfScene()=default;
bool NativeShelfScene::syncContent(std::span<const NativeShelfImage>supplied,std::uint64_t imageRevision){
    auto&i=*impl_;bool sameCards=i.cards.size()==i.source->cards().size();for(std::size_t n=0;sameCards&&n<i.cards.size();++n)sameCards=i.cards[n]->plan.itemID==i.source->cards()[n].itemID&&i.cards[n]->sourceRevision==i.source->cards()[n].contentRevision;
    if(i.chromeRevision&&i.chromeRevision==i.source->chromeRevision()&&sameCards&&i.imageRevision==imageRevision){const bool changed=i.placementRevision!=i.source->placementRevision();i.updatePositions();if(changed)++i.stats.contentSynchronizations;return changed;}
    need(i.retired.empty(),"Publish and collect prior Shelf parts before another content replacement");
    auto images=supplied.empty()?i.images:std::vector<NativeShelfImage>(supplied.begin(),supplied.end());auto plan=prepareShelfScene(*i.source,images);
    std::size_t required{};const auto reservePart=[&](const ShelfScenePart&p,const std::unique_ptr<Impl::Part>&existing){if(!existing||!Impl::sameArtwork(existing->plan,p))required+=p.surfaces.size();};
    reservePart(plan.collection,i.collection);reservePart(plan.scrollbar,i.scrollbar);reservePart(plan.foreground,i.foreground);
    for(const auto&p:plan.cards){const auto found=std::find_if(i.cards.begin(),i.cards.end(),[&](const auto&old){return old->plan.itemID==p.itemID&&Impl::sameArtwork(old->plan,p);});if(found==i.cards.end())required+=p.surfaces.size();}
    need(required<=LayerRasterizer::maximumEntries-i.raster->stats().entries,"Shelf replacement exceeds shared raster capacity; retire unused module artwork first");
    const auto build=[&](ShelfScenePart&p,const std::unique_ptr<Impl::Part>&existing){if(existing&&Impl::sameArtwork(existing->plan,p))return std::unique_ptr<Impl::Part>{};return std::make_unique<Impl::Part>(*i.raster,std::move(p),i.options);};
    auto collection=build(plan.collection,i.collection),scrollbar=build(plan.scrollbar,i.scrollbar),foreground=build(plan.foreground,i.foreground);
    std::array<std::size_t,8>old{};old.fill(8);std::array<std::unique_ptr<Impl::Part>,8>built;
    for(std::size_t n=0;n<plan.cards.size();++n){for(std::size_t k=0;k<i.cards.size();++k)if(i.cards[k]->plan.itemID==plan.cards[n].itemID&&Impl::sameArtwork(i.cards[k]->plan,plan.cards[n])){old[n]=k;break;}if(old[n]==8)built[n]=std::make_unique<Impl::Part>(*i.raster,std::move(plan.cards[n]),i.options);}
    std::vector<std::unique_ptr<Impl::Part>>next;next.reserve(8);
    // All parsing/rasterization completed before publication/lifetime mutation.
    const auto replace=[&](auto&current,auto&candidate){if(candidate){if(current)i.retired.push_back(std::move(current));current=std::move(candidate);++i.stats.partBuilds;}};
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
    need(pose.collectionSublayerTransform==Matrix{}&&!pose.collectionReveal&&pose.dropStrokeEnd==1,"Animated Shelf subsection/drop trace requires the exact retained path adapter");
    need(std::isfinite(pose.toolbarY)&&std::isfinite(pose.toolbarZ)&&std::abs(pose.toolbarY)<=8192&&std::abs(pose.toolbarZ)<=8192,"Invalid Shelf toolbar translation");
    need(pose.selections.size()<=i.cards.size(),"Shelf selection pose exceeds visible capacity");std::array<double,8>ys{},zs{};std::array<bool,8>seen{};
    for(std::size_t n=0;n<i.cards.size();++n){ys[n]=i.cards[n]->selectionY;zs[n]=i.cards[n]->selectionZ;}
    for(const auto&s:pose.selections){need(std::isfinite(s.y)&&std::isfinite(s.z)&&std::abs(s.y)<=8192&&std::abs(s.z)<=8192,"Invalid Shelf selection translation");std::size_t index=i.cards.size();for(std::size_t n=0;n<i.cards.size();++n)if(i.cards[n]->plan.itemID==s.itemID){index=n;break;}need(index<i.cards.size()&&!seen[index],"Stale or repeated Shelf selection pose");seen[index]=true;ys[index]=s.y;zs[index]=s.z;}
    const PlaneMask collectionClip{core::source::inverseSourceMatrix(pose.contentWorld),modules::ShelfPresentation::contentClip(),0};
    if(pose.moduleShutter)validatePlaneShutter(*pose.moduleShutter);
    for(const auto&m:pose.ownerMasks)need(m.worldToLocal.finite()&&std::isfinite(m.bounds.x)&&std::isfinite(m.bounds.y)&&std::isfinite(m.bounds.width)&&std::isfinite(m.bounds.height)&&m.bounds.width>0&&m.bounds.height>0&&std::isfinite(m.cornerRadius)&&m.cornerRadius>=0&&m.cornerRadius<=std::min(m.bounds.width,m.bounds.height)*.5,"Invalid Shelf owner clip");
    std::size_t cardIndex{};i.each([&](auto&part){const bool card=!part.plan.itemID.empty(),collection=&part==i.collection.get()||card;Matrix base=pose.contentWorld;
        if(card){base=base*Matrix::translation(part.full.x,part.full.y+ys[cardIndex],zs[cardIndex]);++cardIndex;}else if(&part==i.scrollbar.get())base=base*Matrix::translation(part.full.x,part.full.y);
        std::copy(pose.ownerMasks.begin(),pose.ownerMasks.end(),part.masks.begin());const auto count=pose.ownerMasks.size()+static_cast<std::size_t>(collection);if(collection)part.masks[pose.ownerMasks.size()]=collectionClip;
        for(std::size_t n=0;n<part.plan.surfaces.size();++n){const auto&s=part.plan.surfaces[n];auto&p=part.placements[n];p.world=base*(s.toolbar?Matrix::translation(0,pose.toolbarY,pose.toolbarZ):Matrix{})*s.local;p.opacity=s.opacity*pose.opacity;p.masks={part.masks.data(),count};if(s.feedback!=ShelfSceneSurface::none)p.opacity*=static_cast<float>(Impl::sample(part.tracks[s.feedback][s.rim?1:0],pose.time));i.validation.world=p.world;i.validation.opacity=p.opacity;i.validation.masks.assign(p.masks.begin(),p.masks.end());i.validation.shutter=pose.moduleShutter;validateDrawObject(i.validation);}
    });
    // Numeric staging above covers every part before changing any LayerScene.
    i.each([&](auto&part){part.activePlacements.clear();for(const auto&p:part.placements)if(p.surface!=ShelfSceneSurface::none)part.activePlacements.push_back(p);part.scene.setPlacements(part.activePlacements);part.scene.setGroupShutter(pose.moduleShutter);for(auto&pair:part.tracks)for(auto&t:pair)if(pose.time>=t.start+t.duration)t.active=false;});
    i.lastTime=pose.time;++i.stats.poseUpdates;return true;
}
bool NativeShelfScene::requiresFrames(double time)const{const auto&i=*impl_;i.time(time);bool active{};i.each([&](const auto&p){for(const auto&pair:p.tracks)for(const auto&t:pair)active|=t.active&&time<t.start+t.duration;});return active;}
std::span<const LayerCompositionEntry>NativeShelfScene::entries()const noexcept{return impl_->entries;}
std::uint64_t NativeShelfScene::compositionRevision()const noexcept{return impl_->compositionRevision;}
LayerScene*NativeShelfScene::cardScene(std::string_view id)noexcept{for(auto&p:impl_->cards)if(p->plan.itemID==id)return &p->scene;return nullptr;}
LayerScene&NativeShelfScene::foregroundScene(){need(bool(impl_->foreground),"Synchronize Shelf before borrowing foreground");return impl_->foreground->scene;}
bool NativeShelfScene::collectRetired(Renderer&r){auto&i=*impl_;for(const auto&part:i.retired)if(!part->scene.releaseResources(r))return false;i.retired.clear();return true;}
bool NativeShelfScene::releaseResources(Renderer&r){auto&i=*impl_;bool complete=collectRetired(r);i.each([&](auto&p){complete=p.scene.releaseResources(r)&&complete;});return complete;}
NativeShelfSceneStats NativeShelfScene::stats()const noexcept{auto result=impl_->stats;result.cards=impl_->cards.size();result.retiredParts=impl_->retired.size();return result;}
#endif
} // namespace endfield::native
