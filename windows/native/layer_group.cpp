#include "native/layer_group.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
void need(bool ok,const char* reason){if(!ok)throw std::invalid_argument(reason);}
bool valid(const core::Rect& r){return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>0&&r.height>0&&std::isfinite(r.x+r.width)&&std::isfinite(r.y+r.height);}
bool contains(const core::Rect& a,const core::Rect& b){return a.x<=b.x&&a.y<=b.y&&a.x+a.width>=b.x+b.width&&a.y+a.height>=b.y+b.height;}
}
NativeLayerGroup::NativeLayerGroup(LayerScene& scene,std::string id,double density,NativeGroupColorSpace colorSpace)
    :local_(&scene),carrier_(*scene.rasterizer_),id_(std::move(id)),density_(density),colorSpace_(colorSpace){
    need(colorSpace_==NativeGroupColorSpace::linear||colorSpace_==NativeGroupColorSpace::encodedSRGB,"Unknown retained native group color space");
    need(!id_.empty()&&id_.size()<=450&&std::isfinite(density_)&&density_>0&&density_<=16,"Invalid retained native group identity/density");
    need(!scene.groupOwner_&&!scene.compositionOwner_&&!scene.resourceOwner_,"Native group needs an unpublished local scene");
    static std::atomic<std::uint64_t> sequence{};
    id_="layer:"+std::to_string(sequence.fetch_add(1,std::memory_order_relaxed))+":"+id_;
    output_[0].masks.reserve(8);staged_.masks.reserve(8);scene.groupOwner_=this;carrier_.carrierGroup_=this;
}
NativeLayerGroup::~NativeLayerGroup(){
    if(renderer_)try{(void)releaseResources(*renderer_);}catch(...){}
    local_->groupOwner_=nullptr;carrier_.carrierGroup_=nullptr;
}
core::Rect NativeLayerGroup::localCoverageBounds()const{
    // Called only while content is being installed. Local menu leaves share a
    // 2D plane; perspective belongs to the single output. Preserve actual ink
    // extents instead of trusting the nominal root or each authored bounds.
    std::optional<core::Rect> result;
    for(const auto& surface:local_->surfaces_){const auto& m=surface.draw.world.values;const auto& b=surface.image->bounds;
        need(surface.draw.world.finite()&&m[2]==0&&m[3]==0&&m[6]==0&&m[7]==0&&m[8]==0&&m[9]==0&&m[11]==0&&m[14]==0&&m[15]==1,"Native group input must remain in its local 2D plane");
        need(valid(b),"Native group raster has invalid ink bounds");
        for(const auto p:std::array<core::Point,4>{{{b.x,b.y},{b.x+b.width,b.y},{b.x,b.y+b.height},{b.x+b.width,b.y+b.height}}}){
            const double x=m[0]*p.x+m[4]*p.y+m[12],y=m[1]*p.x+m[5]*p.y+m[13];
            need(std::isfinite(x)&&std::isfinite(y),"Native group ink exceeds local numeric bounds");
            if(!result)result=core::Rect{x,y,0,0};else{const auto left=std::min(result->x,x),top=std::min(result->y,y),right=std::max(result->x+result->width,x),bottom=std::max(result->y+result->height,y);*result={left,top,right-left,bottom-top};}
        }
    }
    need(result&&valid(*result),"Native group has no drawable local coverage");return *result;
}
std::span<const DrawObject> NativeLayerGroup::prepareChildren(std::optional<NativeGroupInsertion> insertion,bool installing){
    const auto local=local_->prepareDraws();const auto count=insertion?insertion->draws.size():0;
    need(!insertion||(insertion->before<=local.size()&&count>0&&count<=8&&valid(insertion->bounds)),"Invalid bounded native group insertion");
    if(!installing)need(count==insertedCount_&&(!count||insertion->before==insertionIndex_),"Native group insertion structure changed; upload first");
    if(!count)return local;
    for(const auto&draw:insertion->draws){validateDrawObject(draw);const auto&m=draw.world.values;need(m[2]==0&&m[3]==0&&m[6]==0&&m[7]==0&&m[8]==0&&m[9]==0&&m[11]==0&&m[14]==0&&m[15]==1,"Native group insertion must remain in the local 2D plane");}
    // Storage grows only on a content transaction. Frame updates reuse owned
    // strings, mask capacities and ordered slots; renderer mutation is last.
    if(installing)children_.resize(local.size()+count);
    need(children_.size()==local.size()+count,"Native group insertion local structure changed");
    std::size_t cursor{};for(std::size_t n=0;n<local.size()+1;++n){if(n==insertion->before)for(const auto&draw:insertion->draws)children_[cursor++]=draw;if(n<local.size())children_[cursor++]=local[n];}
    return children_;
}
bool NativeLayerGroup::uploadResources(Renderer& renderer,std::optional<core::Rect> coverage,std::optional<NativeGroupInsertion> insertion){
    need(!renderer_||renderer_==&renderer,"Native group belongs to another renderer");
    need(renderer.stats().initialized,"Native group requires an initialized renderer");
    auto ink=localCoverageBounds();if(insertion){need(valid(insertion->bounds),"Invalid native insertion coverage");const auto b=insertion->bounds;const auto x=std::min(ink.x,b.x),y=std::min(ink.y,b.y);ink={x,y,std::max(ink.x+ink.width,b.x+b.width)-x,std::max(ink.y+ink.height,b.y+b.height)-y};}const auto bounds=coverage.value_or(ink);
    need(valid(bounds)&&contains(bounds,ink),"Native group coverage must contain all source ink");
    const auto localDraws=prepareChildren(insertion,true);for(const auto& draw:localDraws)validateDrawObject(draw);
    // Stage resources without publishing. Retained GPU group references keep
    // the old local assets alive if a later candidate fails validation.
    renderer_=&renderer;local_->uploadResources(renderer);
    const bool changed=renderer.configureNativeGroup(id_,{bounds,density_,colorSpace_},localDraws);
    registered_=true;
    const auto& native=renderer.nativeGroupOutput(id_);
    if(output_[0].sourceID.empty()){
        auto nextOutput=native,nextStage=native;
        // Complete all allocating identity copies before changing either
        // fixed record. A failed first publication remains safe to retry.
        output_[0].sourceID.swap(nextOutput.sourceID);output_[0].meshID.swap(nextOutput.meshID);output_[0].textureID.swap(nextOutput.textureID);
        staged_.sourceID.swap(nextStage.sourceID);staged_.meshID.swap(nextStage.meshID);staged_.textureID.swap(nextStage.textureID);
    }else need(output_[0].sourceID==native.sourceID&&output_[0].meshID==native.meshID&&output_[0].textureID==native.textureID,"Retained native group output identities changed");
    structureRevision_=local_->revision_;resourceRevision_=local_->resourceRevision_;insertedCount_=insertion?insertion->draws.size():0;insertionIndex_=insertion?insertion->before:0;
    local_->collectRetiredResources(renderer);retainedCoverage_=coverage;retainedInsertion_=insertion;return changed;
}
bool NativeLayerGroup::updateLocal(Renderer& renderer,std::optional<NativeGroupInsertion> insertion){
    need(renderer_==&renderer&&local_->revision_==structureRevision_&&local_->resourceRevision_==resourceRevision_&&local_->uploadedRevision_==resourceRevision_,"Changed native group content needs upload before feedback");
    const bool changed=renderer.setNativeGroupDraws(id_,prepareChildren(insertion,false));retainedInsertion_=insertion;return changed;
}
void NativeLayerGroup::setPose(const core::Matrix4& world,float opacity,std::span<const PlaneMask> masks,std::optional<PlaneShutter> shutter){
    need(!output_[0].sourceID.empty(),"Upload native group before setting its pose");need(masks.size()<=8,"Too many outer group masks");
    staged_.world=world;staged_.opacity=opacity;staged_.masks.assign(masks.begin(),masks.end());staged_.shutter=std::move(shutter);validateDrawObject(staged_);
    auto& out=output_[0];out.world=staged_.world;out.opacity=staged_.opacity;out.masks.assign(staged_.masks.begin(),staged_.masks.end());out.shutter=staged_.shutter;
}
bool NativeLayerGroup::refreshTypography(){const bool changed=local_->refreshTypography();if(renderer_&&(changed||local_->resourceRevision_!=resourceRevision_))uploadResources(*renderer_,retainedCoverage_,retainedInsertion_);return changed;}
LayerCompositionEntry NativeLayerGroup::entry(){need(renderer_&&!output_[0].sourceID.empty(),"Upload native group before borrowing its composition entry");return {&carrier_,output_};}
bool NativeLayerGroup::releaseResources(Renderer& renderer){
    need(!renderer_||renderer_==&renderer,"Release native group through its renderer owner");if(carrier_.compositionOwner_)return false;
    if(registered_&&renderer.stats().initialized&&!renderer.removeNativeGroup(id_))return false;
    registered_=false;
    const bool released=local_->release(renderer,false);if(released){renderer_=nullptr;structureRevision_=resourceRevision_=0;}
    return released;
}
} // namespace endfield::native
