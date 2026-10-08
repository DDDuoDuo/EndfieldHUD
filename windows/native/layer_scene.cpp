#include "native/layer_scene.hpp"
#include "native/layer_mask.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;
using Matrix=core::Matrix4;
// GPU objects already belong to one UI thread. This bounded-by-live-owners
// registry guards the single native publication slot, not scene content/state.
// Storage changes only on attach/detach; no timer, service or cross-thread lock.
thread_local std::vector<std::pair<Renderer*,LayerComposition*>> publicationOwners;
LayerComposition* publicationOwner(Renderer&renderer){for(const auto&entry:publicationOwners)if(entry.first==&renderer)return entry.second;return nullptr;}
void forgetPublication(LayerComposition*owner){publicationOwners.erase(std::remove_if(publicationOwners.begin(),publicationOwners.end(),[&](const auto&entry){return entry.second==owner;}),publicationOwners.end());}
void need(bool ok,const char*reason){if(!ok)throw std::invalid_argument(reason);}
double number(const Json&j,double fallback=0){if(j.isNull())return fallback;need(j.isNumber()&&std::isfinite(j.number()),"Invalid native layer number");return j.number();}
bool flag(const Json&j,bool fallback=false){return j.isNull()?fallback:j.boolean();}
std::string text(const Json&j){return j.isNull()?std::string{}:j.string();}
std::array<double,2> point(const Json&j,std::array<double,2> fallback={}){if(j.isNull())return fallback;need(j.array().size()==2,"Invalid native layer point");return {number(j.array()[0]),number(j.array()[1])};}
core::Rect rect(const Json&j){need(j.isArray()&&j.array().size()==4,"Invalid native layer bounds");const auto&a=j.array();return {number(a[0]),number(a[1]),number(a[2]),number(a[3])};}
Matrix matrix(const Json&j){Matrix out;if(j.isNull())return out;need(j.array().size()==4,"Invalid layer matrix columns");for(unsigned c=0;c<4;++c){need(j.array()[c].array().size()==4,"Invalid layer matrix rows");for(unsigned r=0;r<4;++r)out.values[c*4+r]=number(j.array()[c].array()[r]);}return out;}
const Json::Array& children(const Json&j){static const Json::Array empty;return j["children"].isNull()?empty:j["children"].array();}
Matrix local(const Json&j){
    if(j["position"].isNull())return matrix(j["transform"]); // exported virtual root
    const auto b=rect(j["bounds"]);const auto p=point(j["position"]),a=point(j["anchorPoint"],{.5,.5});
    return Matrix::translation(p[0],p[1],number(j["zPosition"]))*matrix(j["transform"])*
        Matrix::translation(-b.x-a[0]*b.width,-b.y-a[1]*b.height,-number(j["anchorPointZ"]));
}
bool affine2D(const Json&j){const auto m=matrix(j).values;return m[2]==0&&m[3]==0&&m[6]==0&&m[7]==0&&m[8]==0&&m[9]==0&&m[10]==1&&m[11]==0&&m[14]==0&&m[15]==1;}
bool localContent2D(const Json&j,unsigned depth){if(depth>64)return false;if(!affine2D(j["sublayerTransform"]))return false;for(const auto&c:children(j))if(!affine2D(c["transform"])||number(c["zPosition"])!=0||!localContent2D(c,depth+1))return false;return true;}
bool ownDrawing(const Json&j){return !j["contents"].isNull()||!j["backgroundColor"].isNull()||number(j["borderWidth"])>0||text(j["kind"])=="text"||text(j["kind"])=="shape"||text(j["kind"])=="gradient";}
bool anyDrawing(const Json&j,unsigned depth){if(depth>64)return false;if(ownDrawing(j))return true;return std::any_of(children(j).begin(),children(j).end(),[&](const Json&c){return anyDrawing(c,depth+1);});}
void validateIdentities(const Json& node,std::unordered_set<std::string>& ids,std::size_t& count,unsigned depth){
    need(depth<=64&&++count<=4096,"Native layer tree exceeds limits");
    const auto id=text(node["id"]);if(!id.empty())need(ids.insert(id).second,"Repeated native layer identity");
    for(const auto&child:children(node))validateIdentities(child,ids,count,depth+1);
}
}
LayerScene::LayerScene(LayerRasterizer& r):rasterizer_(&r){
    static std::atomic<std::uint64_t> identity{0};
    namespace_="native-scene:"+std::to_string(identity.fetch_add(1,std::memory_order_relaxed))+":";
}
LayerScene::~LayerScene(){for(const auto&id:rasterIDs_)rasterizer_->remove(id);}
void LayerScene::load(const Json& root,const LayerRasterOptions& options){
    std::unordered_set<std::string> ids;std::size_t count{};validateIdentities(root,ids,count,0);
    // Load is an explicit content revision, not the pointer/frame path. Keep the
    // previous scene intact if a malformed layer cannot be compiled.
    // Consume the attempt even when validation fails. Otherwise a retry with
    // different content could return an earlier partial attempt's cached raster.
    LayerScene next(*rasterizer_);next.namespace_=namespace_;next.revision_=++attempt_;
    next.append(root,{},1,{},options,0);
    next.report_.sourceNodes=count;
    next.structuralIssues_=std::move(next.report_.unsupported);
    next.rebuildReport();
    for(const auto&id:rasterIDs_)if(std::find(next.rasterIDs_.begin(),next.rasterIDs_.end(),id)==next.rasterIDs_.end())rasterizer_->remove(id);
    surfaces_=std::move(next.surfaces_);draws_=std::move(next.draws_);report_=std::move(next.report_);revision_=next.revision_;
    structuralIssues_=std::move(next.structuralIssues_);
    rasterIDs_=std::move(next.rasterIDs_);next.rasterIDs_.clear();next.surfaces_.clear();
    ++resourceRevision_;
}
void LayerScene::append(const Json& node,const Matrix&parent,float parentOpacity,const std::vector<PlaneMask>& masks,const LayerRasterOptions& options,unsigned depth){
    need(depth<=64&&++report_.sourceNodes<=4096,"Native layer tree exceeds limits");
    if(flag(node["hidden"]))return;
    const auto world=parent*local(node);const auto opacity=parentOpacity*float(number(node["opacity"],1));
    need(std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid native layer opacity");if(opacity==0)return;
    const auto b=rect(node["bounds"]);const auto identity=text(node["id"]);
    const bool rasterGroup=!identity.empty()&&b.width>0&&b.height>0&&b.width*b.height<=1'048'576&&localContent2D(node,depth)&&anyDrawing(node,depth);
    if(rasterGroup||ownDrawing(node)){
        need(!identity.empty(),"Drawable native layer has no identity");
        Json content=node;if(!rasterGroup)content["children"]=Json::Array{};
        auto settings=options;settings.includeRootOpacity=false;settings.includeRootMask=true;
        const auto id=namespace_+identity;
        rasterIDs_.push_back(id);
        auto image=rasterizer_->rasterize(id,revision_,content,settings);
        if(image->width&&image->height){
            DrawObject draw;draw.sourceID=id;draw.meshID=id;draw.textureID=id;draw.world=world;draw.opacity=opacity;draw.masks=masks;
            std::unordered_set<std::string> localIDs;std::size_t localCount{};validateIdentities(content,localIDs,localCount,0);
            surfaces_.push_back({id,image,draw,revision_,revision_,{},settings,localCount,rasterGroup});draws_.push_back(std::move(draw));
            surfaces_.back().draw.masks.reserve(8);draws_.back().masks.reserve(8);
        }else report_.unsupported.insert(report_.unsupported.end(),image->unsupported.begin(),image->unsupported.end());
        if(rasterGroup)return;
    }
    auto nextMasks=masks;
    if(flag(node["masksToBounds"])&&b.width>0&&b.height>0){
        const auto radius=number(node["cornerRadius"]);need(radius>=0,"Invalid projected corner radius");
        if(nextMasks.size()<8)nextMasks.push_back({core::source::inverseSourceMatrix(world),b,std::min(radius,std::min(b.width,b.height)*.5)});
        else report_.unsupported.push_back({identity,"more than eight projected clipping ancestors"});
    }
    if(!node["mask"].isNull()){
        const auto projected=projectLayerMask(node["mask"],world);
        report_.unsupported.insert(report_.unsupported.end(),projected.unsupported.begin(),projected.unsupported.end());
        if(projected.clipsAll)return;
        if(projected.plane){
            if(nextMasks.size()<8)nextMasks.push_back(*projected.plane);
            else report_.unsupported.push_back({identity,"more than eight projected clipping ancestors"});
        }
    }
    if(opacity<1&&children(node).size()>1&&flag(node["allowsGroupOpacity"],true))report_.unsupported.push_back({identity,"group opacity across separately projected children"});
    std::vector<const Json*> ordered;for(const auto&c:children(node))ordered.push_back(&c);
    std::stable_sort(ordered.begin(),ordered.end(),[](const Json*a,const Json*b){return number((*a)["zPosition"])<number((*b)["zPosition"]);});
    // Source shell parents use an identity sublayer transform. General affine
    // child transforms are retained; perspective stays in the GPU matrix.
    const auto anchor=point(node["anchorPoint"],{.5,.5});
    const double ax=b.x+anchor[0]*b.width,ay=b.y+anchor[1]*b.height,az=number(node["anchorPointZ"]);
    const auto childWorld=world*Matrix::translation(ax,ay,az)*matrix(node["sublayerTransform"])*Matrix::translation(-ax,-ay,-az);
    for(const auto*c:ordered)append(*c,childWorld,opacity,nextMasks,options,depth+1);
}
void LayerScene::rebuildReport(){
    report_.surfaces=surfaces_.size();report_.pixelBytes=0;
    report_.unsupported=structuralIssues_;report_.fontSubstitutions.clear();
    for(const auto& surface:surfaces_){const auto& image=*surface.image;
        report_.pixelBytes+=image.straightRGBA.size();
        report_.unsupported.insert(report_.unsupported.end(),image.unsupported.begin(),image.unsupported.end());
        report_.fontSubstitutions.insert(report_.fontSubstitutions.end(),image.fontSubstitutions.begin(),image.fontSubstitutions.end());
    }
}
bool LayerScene::updateLocalContent(std::string_view sourceID,std::uint64_t contentRevision,const Json& localContent,const LayerRasterOptions& options){
    const auto index=surfaceIndex(sourceID);need(index.has_value(),"Local content has no retained surface");
    auto& surface=surfaces_[*index];auto settings=options;settings.includeRootOpacity=false;settings.includeRootMask=true;
    if(surface.localRevision==contentRevision&&surface.options==settings)return false;
    need(text(localContent["id"])==sourceID,"Local content cannot replace another surface identity");
    std::unordered_set<std::string> ids;std::size_t count{};validateIdentities(localContent,ids,count,0);
    need(localContent2D(localContent,0)&&!flag(localContent["hidden"]),"Local content must stay in its existing visible plane");
    need(surface.grouped||children(localContent).empty(),"A separately projected parent cannot absorb its child surfaces");
    need(report_.sourceNodes-surface.nodeCount+count<=4096,"Updated native layer tree exceeds limits");
    const auto token=++attempt_; // consume even a failed raster attempt
    auto image=rasterizer_->rasterize(surface.id,token,localContent,settings);
    need(image->width&&image->height,"Local content cannot remove its retained surface");
    if(image->bounds!=surface.image->bounds)surface.meshRevision=token;
    report_.sourceNodes=report_.sourceNodes-surface.nodeCount+count;
    surface.image=std::move(image);surface.imageRevision=token;surface.localRevision=contentRevision;
    surface.options=std::move(settings);surface.nodeCount=count;rebuildReport();++resourceRevision_;return true;
}
void LayerScene::uploadResources(Renderer& renderer){
    need(!resourceOwner_||resourceOwner_==&renderer,"A native scene must release its previous renderer resources first");
    need(renderer.stats().initialized,"Native scene resources require an initialized renderer");
    resourceOwner_=&renderer;
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    for(const auto&s:surfaces_){const auto&b=s.image->bounds;const auto x=float(b.x),y=float(b.y),right=float(b.x+b.width),bottom=float(b.y+b.height);
        const std::array<Vertex,4> vertices{{{{x,y,0},{0,0},{1,1,1,1}},{{right,y,0},{1,0},{1,1,1,1}},{{right,bottom,0},{1,1},{1,1,1,1}},{{x,bottom,0},{0,1},{1,1,1,1}}}};
        auto resident=std::find_if(uploaded_.begin(),uploaded_.end(),[&](const Resident&r){return r.id==s.id;});
        if(resident==uploaded_.end()){uploaded_.push_back({s.id});resident=std::prev(uploaded_.end());}
        renderer.setMesh(s.id,s.meshRevision,{vertices,indices});resident->mesh=true;
        renderer.setTexture(s.id,s.imageRevision,{s.image->width,s.image->height,s.image->straightRGBA,TextureColorSpace::sRGB,TextureFilter::linear});resident->texture=true;
    }
    uploadedRevision_=resourceRevision_;
}
bool LayerScene::release(Renderer& renderer,bool onlyRetired){
    need(!resourceOwner_||resourceOwner_==&renderer,"Native scene resources belong to another renderer");
    if(!renderer.stats().initialized){uploaded_.clear();resourceOwner_=nullptr;uploadedRevision_=0;return true;}
    bool complete=true;
    for(auto&resident:uploaded_){
        if(onlyRetired&&std::any_of(surfaces_.begin(),surfaces_.end(),[&](const Surface&s){return s.id==resident.id;}))continue;
        if(resident.mesh&&renderer.removeMesh(resident.id))resident.mesh=false;
        if(resident.texture&&renderer.removeTexture(resident.id))resident.texture=false;
        if(resident.mesh||resident.texture)complete=false;
    }
    uploaded_.erase(std::remove_if(uploaded_.begin(),uploaded_.end(),[](const Resident&r){return !r.mesh&&!r.texture;}),uploaded_.end());
    if(uploaded_.empty()&&!onlyRetired){resourceOwner_=nullptr;uploadedRevision_=0;}
    return complete;
}
void LayerScene::collectRetiredResources(Renderer& renderer){(void)release(renderer,true);}
bool LayerScene::releaseResources(Renderer& renderer){
    need(!compositionOwner_,"Remove a native scene from its composition before releasing its resources");
    return release(renderer,false);
}
void LayerScene::upload(Renderer& renderer){
    need(!compositionOwner_&&!publicationOwner(renderer),"A composed renderer cannot publish an exclusive native draw list");
    uploadResources(renderer);renderer.setDrawList(draws_);collectRetiredResources(renderer);
}
void LayerScene::detach(Renderer& renderer){
    need(!compositionOwner_&&!publicationOwner(renderer),"Detach a composed renderer through its composition owner");
    need(!resourceOwner_||resourceOwner_==&renderer,"Native scene resources belong to another renderer");
    renderer.clearDrawList();(void)release(renderer,false);
}
std::optional<std::size_t> LayerScene::surfaceIndex(std::string_view sourceID)const noexcept{
    for(std::size_t i=0;i<surfaces_.size();++i)if(std::string_view(surfaces_[i].id).substr(namespace_.size())==sourceID)return i;
    return {};
}
std::shared_ptr<const PaintedTextLayout> LayerScene::paintedTextLayout(std::string_view sourceID)const{
    const auto index=surfaceIndex(sourceID);if(!index)return {};
    const auto& surface=surfaces_[*index];
    return rasterizer_->textLayout(surface.id,surface.imageRevision);
}
void LayerScene::setPlacements(std::span<const LayerPlacement> placements){
    std::array<bool,4096> supplied{};
    for(const auto&p:placements){
        need(p.surface<surfaces_.size()&&!supplied[p.surface],"Invalid or repeated native surface index");supplied[p.surface]=true;
        need(p.world.finite()&&std::isfinite(p.opacity)&&p.opacity>=0&&p.opacity<=1&&p.masks.size()<=8,"Invalid native surface placement");
        for(const auto&m:p.masks)need(m.worldToLocal.finite()&&std::isfinite(m.bounds.x)&&std::isfinite(m.bounds.y)&&std::isfinite(m.bounds.width)&&std::isfinite(m.bounds.height)&&m.bounds.width>0&&m.bounds.height>0&&std::isfinite(m.cornerRadius)&&m.cornerRadius>=0&&m.cornerRadius<=std::min(m.bounds.width,m.bounds.height)*.5,"Invalid native surface mask");
    }
    for(const auto&p:placements){auto&d=surfaces_[p.surface].draw;d.world=p.world;d.opacity=p.opacity;d.masks.assign(p.masks.begin(),p.masks.end());}
}
void LayerScene::setGroupShutter(std::optional<PlaneShutter> shutter){
    if(shutter)validatePlaneShutter(*shutter);
    groupShutter_=std::move(shutter);
}
Matrix LayerScene::validatePreparation(const Matrix& placement)const{
    need(placement.finite(),"Invalid native layer placement");const auto inverse=core::source::inverseSourceMatrix(placement);
    if(groupShutter_)need((groupShutter_->worldToLocal*inverse).finite(),"Invalid projected native shutter matrix");
    for(const auto&surface:surfaces_){need((placement*surface.draw.world).finite(),"Invalid projected native world matrix");for(const auto&mask:surface.draw.masks)need((mask.worldToLocal*inverse).finite(),"Invalid projected native mask matrix");}
    return inverse;
}
void LayerScene::prepare(const Matrix& placement,const Matrix& inverse){
    for(std::size_t i=0;i<surfaces_.size();++i){const auto&source=surfaces_[i].draw;auto&draw=draws_[i];draw.world=placement*source.world;draw.opacity=source.opacity;draw.masks.resize(source.masks.size());
        draw.shutter=groupShutter_;if(draw.shutter)draw.shutter->worldToLocal=groupShutter_->worldToLocal*inverse;
        for(std::size_t j=0;j<draw.masks.size();++j){draw.masks[j]=source.masks[j];draw.masks[j].worldToLocal=source.masks[j].worldToLocal*inverse;}}
}
std::span<const DrawObject> LayerScene::prepareDraws(const Matrix& placement){
    const auto inverse=validatePreparation(placement);prepare(placement,inverse);
    return draws_;
}
void LayerScene::present(Renderer& renderer,const Matrix& placement){
    need(!compositionOwner_&&!publicationOwner(renderer),"A composed renderer cannot publish an exclusive native draw list");
    renderer.setDrawList(prepareDraws(placement));
}
void LayerComposition::checkRenderer(Renderer& renderer)const{
    need(!renderer_||renderer_==&renderer,"A native composition belongs to another renderer");
    need(!publicationOwner(renderer)||publicationOwner(renderer)==this,"Renderer already has another native composition publisher");
    need(renderer.stats().initialized,"Native composition requires an initialized renderer");
}
LayerComposition::~LayerComposition(){
    // Borrowed objects outlive this publication owner by contract. Its RAII
    // cleanup removes the one list before touching any registered resources.
    if(renderer_)try{detach(*renderer_);}catch(...){
        for(const auto&entry:scenes_)if(entry.scene->compositionOwner_==this)entry.scene->compositionOwner_=nullptr;
        forgetPublication(this);
    }
}
void LayerComposition::setScenes(Renderer& renderer,std::span<LayerScene* const> order){
    std::vector<LayerCompositionEntry> entries;entries.reserve(order.size());
    for(auto* scene:order)entries.push_back({scene,{}});
    setEntries(renderer,entries);
}
void LayerComposition::setEntries(Renderer& renderer,std::span<const LayerCompositionEntry> order){
    checkRenderer(renderer);
    std::size_t count{},stageCount{};std::unordered_set<LayerScene*> unique;
    std::vector<Entry> next;next.reserve(order.size());
    for(const auto& input:order){const auto scene=input.scene;
        need(scene&&unique.insert(scene).second,"Null or repeated scene in native composition");
        need(!scene->compositionOwner_||scene->compositionOwner_==this,"Native scene already belongs to another composition");
        need(!scene->resourceOwner_||scene->resourceOwner_==&renderer,"Native scene resources belong to another renderer");
        need(scene->draws_.size()<=Renderer::maximumObjects-count,"Combined native draw count exceeds limits");
        need(input.after.size()<=Renderer::maximumObjects-count-scene->draws_.size(),"Supplemental native draw count exceeds limits");
        next.push_back({scene,scene->revision_,scene->resourceRevision_,count,scene->draws_.size(),input.after,stageCount});
        count+=scene->draws_.size()+input.after.size();stageCount+=input.after.size();
    }
    std::vector<DrawObject> nextDraws,nextStage;nextDraws.reserve(count);nextStage.reserve(stageCount);std::vector<Matrix> nextInverse(next.size());
    if(!renderer_)publicationOwners.reserve(publicationOwners.size()+1);
    // Stage CPU identities/capacities before GPU mutation. The last published
    // list keeps its assets resident throughout preparation, even on failure.
    for(const auto&entry:next){
        for(const auto&draw:entry.scene->draws()){nextDraws.push_back(draw);nextDraws.back().masks.reserve(8);}
        for(const auto&draw:entry.after){validateDrawObject(draw);nextDraws.push_back(draw);nextDraws.back().masks.reserve(8);nextStage.push_back(draw);nextStage.back().masks.reserve(8);}
    }
    try{
        for(const auto&entry:next)entry.scene->uploadResources(renderer);
        renderer.setDrawList(nextDraws);
    }catch(...){
        // Unpublished candidates cannot leak assets on a failed insertion.
        // Renderer refuses to remove a still-published reference, so even an
        // independently uploaded candidate leaves its old active list safe.
        for(const auto&entry:next)if(std::none_of(scenes_.begin(),scenes_.end(),[&](const Entry&old){return old.scene==entry.scene;}))try{(void)entry.scene->release(renderer,false);}catch(...){}
        throw;
    }
    // All old references are now gone. Do not clear the renderer list when
    // retiring one scene: siblings share the same publication point.
    for(const auto&old:scenes_)if(!unique.contains(old.scene)){
        old.scene->compositionOwner_=nullptr;(void)old.scene->release(renderer,false);
    }
    for(const auto&entry:next){entry.scene->compositionOwner_=this;entry.scene->collectRetiredResources(renderer);}
    scenes_=std::move(next);draws_=std::move(nextDraws);inverseTransforms_=std::move(nextInverse);stagedAfter_=std::move(nextStage);
    if(!renderer_)publicationOwners.emplace_back(&renderer,this);renderer_=&renderer;
}
void LayerComposition::upload(Renderer& renderer){
    checkRenderer(renderer);std::vector<LayerCompositionEntry> order;order.reserve(scenes_.size());for(const auto&entry:scenes_)order.push_back({entry.scene,entry.after});
    setEntries(renderer,order);
}
void LayerComposition::copyPrepared(const Entry&entry){
    const auto source=entry.scene->draws();
    for(std::size_t i=0;i<entry.count;++i){auto&target=draws_[entry.begin+i];const auto&draw=source[i];
        target.world=draw.world;target.linearTint=draw.linearTint;target.opacity=draw.opacity;
        target.shutter=draw.shutter;
        target.masks.resize(draw.masks.size());std::copy(draw.masks.begin(),draw.masks.end(),target.masks.begin());
    }
}
void LayerComposition::present(Renderer& renderer,std::span<const Matrix> transforms){
    checkRenderer(renderer);need(transforms.empty()||transforms.size()==scenes_.size(),"Native composition needs exactly one transform per scene");
    for(const auto&entry:scenes_){const auto&scene=*entry.scene;
        need(scene.compositionOwner_==this&&scene.revision_==entry.structureRevision&&scene.resourceRevision_==entry.resourceRevision&&scene.uploadedRevision_==scene.resourceRevision_,"Native scene changed; upload the complete composition before presenting");
        need(scene.draws_.size()==entry.count,"Native surface bindings changed before composition upload");
    }
    // Stage borrowed geometry before changing any scene/published CPU record.
    // Stable IDs are content, not a per-frame opportunity to replace resources.
    for(std::size_t i=0;i<scenes_.size();++i){const auto&entry=scenes_[i];const auto transform=transforms.empty()?Matrix{}:transforms[i];
        inverseTransforms_[i]=entry.scene->validatePreparation(transform);
        for(std::size_t j=0;j<entry.after.size();++j){const auto&input=entry.after[j];auto&stage=stagedAfter_[entry.stageBegin+j];
            need(input.sourceID==stage.sourceID&&input.meshID==stage.meshID&&input.textureID==stage.textureID,"Supplemental draw identity changed; replace the composition entries");
            need(input.masks.size()<=8,"Too many supplemental draw masks");
            stage.world=transform*input.world;stage.opacity=input.opacity;stage.linearTint=input.linearTint;stage.shutter=input.shutter;
            if(stage.shutter)stage.shutter->worldToLocal=stage.shutter->worldToLocal*inverseTransforms_[i];
            stage.masks.resize(input.masks.size());for(std::size_t k=0;k<input.masks.size();++k){stage.masks[k]=input.masks[k];stage.masks[k].worldToLocal=input.masks[k].worldToLocal*inverseTransforms_[i];}
            validateDrawObject(stage);
        }
    }
    for(std::size_t i=0;i<scenes_.size();++i){const auto&entry=scenes_[i];entry.scene->prepare(transforms.empty()?Matrix{}:transforms[i],inverseTransforms_[i]);copyPrepared(entry);
        for(std::size_t j=0;j<entry.after.size();++j){const auto&stage=stagedAfter_[entry.stageBegin+j];auto&out=draws_[entry.begin+entry.count+j];
            out.world=stage.world;out.opacity=stage.opacity;out.linearTint=stage.linearTint;out.shutter=stage.shutter;
            out.masks.resize(stage.masks.size());std::copy(stage.masks.begin(),stage.masks.end(),out.masks.begin());
        }
    }
    renderer.setDrawList(draws_);
}
void LayerComposition::detach(Renderer& renderer){
    need(!renderer_||renderer_==&renderer,"Detach native composition through its renderer owner");
    need(!publicationOwner(renderer)||publicationOwner(renderer)==this,"Cannot clear another native composition publisher");
    renderer.clearDrawList();
    for(const auto&entry:scenes_){entry.scene->compositionOwner_=nullptr;(void)entry.scene->release(renderer,false);}
    scenes_.clear();draws_.clear();inverseTransforms_.clear();stagedAfter_.clear();forgetPublication(this);renderer_=nullptr;
}
Matrix layerViewportProjection(unsigned width,unsigned height){
    need(width&&height,"Invalid native layer viewport");Matrix m;
    // Core Animation zPosition orders these already projected screen surfaces;
    // it is not a D3D normalized depth. Keep native paint order and prevent a
    // zPosition such as10 from clipping the whole caption outside [0,1].
    m.values={2./width,0,0,0, 0,-2./height,0,0, 0,0,0,0, -1,1,0,1};return m;
}
} // namespace endfield::native
