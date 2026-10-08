#include "native/watch_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
using Batch=core::source::SourceWatchBatch;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
SourceImageGeometryView image(const Batch&batch){const auto&m=batch.geometry->mesh;return {m.positions,m.uv,m.indices};}
std::string identity(const Batch&batch){return "watch:"+batch.stateID;}
}
WatchMaterialPresentation::WatchMaterialPresentation(SourceScene&scene):scene_(&scene){}
std::array<float,4> WatchMaterialPresentation::vertexTint(std::array<float,4> c,bool enabled,const std::optional<std::array<float,3>>& accent)noexcept{
    // HUDSourceDesktopAccent.replacingYellow: alpha and authored intensity are
    // independent; neutral artwork never becomes an accent-colored silhouette.
    if(!enabled||!accent||!std::isfinite(c[0])||!std::isfinite(c[1])||!std::isfinite(c[2])||
        !std::isfinite((*accent)[0])||!std::isfinite((*accent)[1])||!std::isfinite((*accent)[2])||
        !(c[0]>0&&c[1]>=c[0]*.25f&&c[1]<=c[0]*1.1f&&c[2]>=0&&c[2]<std::min(c[0],c[1])*.5f))return c;
    const float intensity=std::max(c[0],c[1]);for(unsigned i=0;i<3;++i)c[i]=(*accent)[i]*intensity;return c;
}
bool WatchMaterialPresentation::sameStructure(std::span<const Batch>batches)const{
    if(stats_.structuralCommits==0||batches.size()!=signature_.size())return false;
    for(std::size_t i=0;i<batches.size();++i){const auto&b=batches[i];const auto&s=signature_[i];
        if(s.stateID!=b.stateID||s.sourceNodeID!=b.sourceNodeID||s.mesh!=b.sourceMesh||s.material!=b.material||s.appliesAccent!=b.appliesDesktopAccent||s.image!=(b.geometry!=nullptr)||s.textures!=b.textureOverrides||s.uniforms.size()!=b.uniformOverrides.size())return false;
        std::size_t u=0;for(const auto&[name,value]:b.uniformOverrides){if(s.uniforms[u].first!=name||s.uniforms[u].second!=value.size())return false;++u;}
    }return true;
}
const SourceBatchTemplate& WatchMaterialPresentation::prototype(const Batch&batch)const{
    const SourceBatchTemplate*fallback{};
    for(const auto&t:scene_->templates()){
        if(t.material!=batch.material||!t.logicalMetadataComplete)continue;
        if(!batch.geometry&&t.originalState.sourceMesh!=batch.sourceMesh)continue;
        bool compatible=true;
        for(const auto&required:t.originalState.uniformOverrides){const auto found=batch.uniformOverrides.find(required.name);
            if(found==batch.uniformOverrides.end()||found->second.size()!=required.value.size()){compatible=false;break;}}
        if(!compatible)continue;
        for(const auto&[name,value]:batch.uniformOverrides){bool found=false;
            for(const auto&pass:t.passes)for(const auto&buffer:pass.buffers)for(const auto&field:buffer.fields)if(field.name==name){found=true;if(field.components!=value.size())compatible=false;}
            if(!found)for(const auto&original:t.originalState.uniformOverrides)if(original.name==name){found=original.value.size()==value.size();break;}
            if(!found||!compatible){compatible=false;break;}}
        if(!compatible)continue;
        for(const auto&[name,texture]:batch.textureOverrides){(void)texture;bool found=false;
            for(const auto&pass:t.passes)for(const auto&binding:pass.textures)found|=binding.name==name;
            if(!found){compatible=false;break;}}
        if(!compatible)continue;
        if(t.originalState.sourceNodeID==batch.sourceNodeID&&t.originalState.sourceMesh==batch.sourceMesh)return t;
        if(!fallback)fallback=&t;
    }
    if(!fallback)throw std::invalid_argument("Missing exact prepared source material/mesh template: "+batch.material+" / "+batch.sourceMesh);
    return *fallback;
}
void WatchMaterialPresentation::writeState(SourceBatchState&state,const Batch&batch,const SourceFrameParameters&params){
    state.world=batch.world;state.vertexColor=vertexTint(batch.color,batch.appliesDesktopAccent,params.desktopAccentLinear);state.visible=true;
    need(state.uniformOverrides.size()==batch.uniformOverrides.size(),"Source presentation uniform shape changed");std::size_t index=0;
    for(const auto&[name,value]:batch.uniformOverrides){auto&u=state.uniformOverrides[index++];need(u.name==name&&u.value.size()==value.size(),"Source presentation uniform schema changed");std::copy(value.begin(),value.end(),u.value.begin());}
}
void WatchMaterialPresentation::update(const core::source::SourceWatchFrame&frame,const SourceFrameParameters&params){
    const auto&batches=frame.batches;
    if(!sameStructure(batches)){
        std::vector<SourceAssembledBatch> submitted;submitted.reserve(batches.size());
        std::vector<Signature> nextSignature;nextSignature.reserve(batches.size());
        for(const auto&batch:batches){
            const auto&t=prototype(batch);SourceAssembledBatch out;out.stateID=identity(batch);out.prototypeID=t.id;out.appliesDesktopAccent=batch.appliesDesktopAccent;
            out.state.sourceNodeID=batch.sourceNodeID;out.state.sourceMesh=batch.sourceMesh;out.state.material=batch.material;
            Signature signature{batch.stateID,batch.sourceNodeID,batch.sourceMesh,batch.material,batch.appliesDesktopAccent,{},batch.textureOverrides,batch.geometry!=nullptr};
            for(const auto&[name,value]:batch.uniformOverrides){out.state.uniformOverrides.push_back({name,value});signature.uniforms.emplace_back(name,value.size());}
            writeState(out.state,batch,params);if(batch.geometry)out.imageGeometry=image(batch);
            for(const auto&[name,texture]:batch.textureOverrides)out.textureOverrides.push_back({name,texture});
            submitted.push_back(std::move(out));nextSignature.push_back(std::move(signature));
        }
        scene_->assembleFrame(submitted,params);
        signature_=std::move(nextSignature);states_.assign(scene_->batches().begin(),scene_->batches().end());frameSlots_.clear();geometryRevisions_.clear();
        frameSlots_.reserve(batches.size());geometryRevisions_.reserve(batches.size());const auto ids=scene_->stateIDs();
        for(const auto&batch:batches){const auto id=identity(batch);const auto i=std::find(ids.begin(),ids.end(),id);need(i!=ids.end(),"Committed source identity is missing");frameSlots_.push_back(static_cast<std::size_t>(i-ids.begin()));geometryRevisions_.push_back(batch.geometry?batch.geometry->revision:0);}
        ++stats_.structuralCommits;
    }else{
        // Retained inactive slots remain hidden; the exact original draw order
        // is fixed until structure changes. Strings and value storage persist.
        for(auto&s:states_)s.visible=false;
        for(std::size_t i=0;i<batches.size();++i){const auto&batch=batches[i];auto&state=states_[frameSlots_[i]];writeState(state,batch,params);
            if(batch.geometry&&geometryRevisions_[i]!=batch.geometry->revision){
                scene_->updateGeometry(scene_->stateIDs()[frameSlots_[i]],image(batch));geometryRevisions_[i]=batch.geometry->revision;++stats_.geometryUpdates;
            }
        }
        scene_->update(states_,params);
    }
    ++stats_.frames;
}
WatchLabelPresentation::WatchLabelPresentation(core::source::NativeLabelPlan&plan,LayerScene&layers):plan_(&plan),layers_(&layers){
    buttons_.resize(plan.buttonIDs().size());masks_.resize(buttons_.size());for(auto&m:masks_)m.reserve(core::source::NativeLabelPlan::maximumButtonMasks);
    placements_.reserve(plan.bindings().size());drawMasks_.resize(plan.bindings().size());priorBounds_.resize(plan.bindings().size());changedBounds_.reserve(plan.bindings().size());
    for(std::size_t i=0;i<plan.bindings().size();++i){const auto surface=layers.surfaceIndex(plan.bindings()[i].surfaceID);need(surface.has_value(),"Source native label has no retained local surface");drawMasks_[i].reserve(8);placements_.push_back({*surface,{},0,{}});}
    sceneRevision_=layers.contentRevision();
}
bool WatchLabelPresentation::update(const core::source::SourceWatchFrame&frame,const core::source::CameraFrame&camera,const core::Rect&viewport,std::span<const WatchButtonAvailability>availability){
    if(sceneRevision_!=layers_->contentRevision()){
        // A language/icon content revision may reorder local surfaces. Resolve
        // the complete binding set before replacing any cached numeric index.
        std::vector<std::size_t> rebound;rebound.reserve(placements_.size());
        for(const auto&binding:plan_->bindings()){const auto index=layers_->surfaceIndex(binding.surfaceID);need(index.has_value(),"Source native label is missing after a content revision");rebound.push_back(*index);}
        for(std::size_t i=0;i<rebound.size();++i)placements_[i].surface=rebound[i];
        sceneRevision_=layers_->contentRevision();initialized_=false;
    }
    for(std::size_t i=0;i<buttons_.size();++i){const auto&id=plan_->buttonIDs()[i];const auto action=std::find_if(availability.begin(),availability.end(),[&](const auto&a){return a.buttonID==id;});
        const auto hit=std::find_if(frame.hits.begin(),frame.hits.end(),[&](const auto&h){return h.buttonID==id;});auto&m=masks_[i];m.clear();
        if(hit!=frame.hits.end()){need(hit->masks.size()<=core::source::NativeLabelPlan::maximumButtonMasks,"Source button has too many mask ancestors");for(const auto&mask:hit->masks)m.push_back({mask.rect,mask.world});}
        buttons_[i]={action!=availability.end()&&action->enabled,hit!=frame.hits.end(),action!=availability.end()&&action->expandFileShelfCaption,m};
    }
    changedBounds_.clear();const bool changed=plan_->update(frame.resolved,frame.inheritedAlpha,buttons_,camera,viewport);
    if(!changed&&initialized_)return false;
    const auto source=plan_->placements();
    for(std::size_t i=0;i<source.size();++i){const auto&s=source[i];auto&p=placements_[i];auto&m=drawMasks_[i];m.clear();for(const auto&mask:s.masks)m.push_back({mask.worldToLocal,mask.bounds});p.world=s.world;p.opacity=s.opacity;p.masks=m;
        if(!initialized_||priorBounds_[i]!=s.contentBounds){priorBounds_[i]=s.contentBounds;changedBounds_.push_back(i);}
    }
    layers_->setPlacements(placements_);initialized_=true;return true;
}
void WatchLabelPresentation::present(Renderer&renderer){layers_->present(renderer);}
} // namespace endfield::native
