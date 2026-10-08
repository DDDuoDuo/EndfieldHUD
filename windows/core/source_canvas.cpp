#include "core/source_canvas.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::core::source {
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
double number(const Json&v,double fallback=0){const auto value=v.isNumber()?v.number():fallback;need(std::isfinite(value),"Nonfinite source canvas value");return value;}
bool flag(const Json&v,bool fallback=false){return v.isBool()?v.boolean():v.isNumber()?v.number()!=0:fallback;}
std::int64_t integer(const Json&v){const auto n=number(v);need(n>=double(std::numeric_limits<std::int64_t>::min())&&n<double(std::numeric_limits<std::int64_t>::max()),"Source sorting order overflow");return static_cast<std::int64_t>(n);}
std::int64_t add(std::int64_t a,std::int64_t b){need(!(b>0&&a>std::numeric_limits<std::int64_t>::max()-b)&&!(b<0&&a<std::numeric_limits<std::int64_t>::min()-b),"Source sorting order overflow");return a+b;}
const std::vector<WatchComponent>& components(const MountedLayoutDocument&doc,std::string_view id){static const std::vector<WatchComponent> empty;const auto i=doc.components.find(id);return i==doc.components.end()?empty:i->second;}
std::array<double,4> vector(const Json&v){return {number(v["x"]),number(v["y"]),number(v["z"]),number(v["w"])};}
std::array<double,4> transform(const Matrix4&m,Vec3 p){std::array<double,4> v{p[0],p[1],p[2],1},out{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out[r]+=m.values[c*4+r]*v[c];return out;}
float checkedFloat(double v){need(std::isfinite(v)&&std::abs(v)<=std::numeric_limits<float>::max(),"Source mask value exceeds Float range");return static_cast<float>(v);}
}
SourceCanvasPlan::SourceCanvasPlan(const SceneDefinition&scene,const MountedLayoutDocument&doc,std::int64_t panelBase,
    const std::set<std::string,std::less<>>* registered):scene_(&scene),layout_(scene),parents_(scene.nodes().size()),sorting_(scene.nodes().size()),ancestry_(scene.nodes().size()),masks_(scene.nodes().size()),alpha_(scene.nodes().size(),1),nextAlpha_(scene.nodes().size(),1){
    for(const auto i:layout_.traversalIndices()){
        const auto&node=scene.nodes()[i];const auto parent=node.parent?layout_.nodeIndex(*node.parent):std::nullopt;parents_[i]=parent;
        const auto*inherited=parent?&sorting_[*parent]:nullptr;auto&s=sorting_[i];
        if(const auto*canvas=doc.component("Canvas",node.id)){
            auto local=node.id==scene.rootID()?panelBase:integer((*canvas)["m_SortingOrder"]);bool overrides=flag((*canvas)["m_OverrideSorting"]);
            // Registration survives disabling; source UISortingOrder acts on
            // this Canvas even when its own m_Enabled flag is false.
            for(const auto&c:components(doc,node.id))if(c.kind()=="UISortingOrder"&&number(c["_renderType"],-1)==1&&(!registered||registered->contains(c.id))){local=add(panelBase,integer(c["_sortingOrderOffset"]));overrides=true;}
            s={i,overrides||!inherited||!inherited->nearestCanvas?local:inherited->sortingOrder,local,overrides};
        }else{s.nearestCanvas=inherited?inherited->nearestCanvas:std::nullopt;s.sortingOrder=inherited?inherited->sortingOrder:0;}
        auto&a=ancestry_[i];const auto*previous=parent?&ancestry_[*parent]:nullptr;
        a.button=doc.component("UIButton",node.id)?std::optional(i):(previous?previous->button:std::nullopt);
        const WatchComponent*closestMask{};for(const auto&c:components(doc,node.id))if(c.kind()=="UISoftMask"){closestMask=&c;break;}
        // A disabled closest soft mask blocks inheritance from its parent.
        a.softMask=closestMask?(closestMask->enabled()?std::optional(i):std::nullopt):(previous?previous->softMask:std::nullopt);
        bool ignoresParent=false;
        for(const auto&g:components(doc,node.id))if(g.kind()=="CanvasGroup"&&g.enabled()){
            a.acceptsInput=a.acceptsInput&&flag(g["m_BlocksRaycasts"],true)&&flag(g["m_Interactable"],true);ignoresParent=ignoresParent||flag(g["m_IgnoreParentGroups"]);
            groups_.push_back({i,number(g["m_Alpha"],1)});
        }
        if(!ignoresParent&&previous)a.acceptsInput=a.acceptsInput&&previous->acceptsInput;
        if(previous&&!s.startsSortingBoundary())a.rectMasks=previous->rectMasks;
        if(const auto*mask=doc.component("RectMask2D",node.id)){
            a.rectMasks.push_back(i);auto&m=masks_[i];m.present=true;m.padding=vector((*mask)["m_Padding"]);
            m.parameters={0,checkedFloat(number((*mask)["m_Softness"]["x"])),checkedFloat(number((*mask)["m_Softness"]["y"])),0};
            const auto hg=vector((*mask)["m_HGSoftness"]);for(unsigned c=0;c<4;++c)m.hgSoftness[c]=checkedFloat(hg[c]);
        }
    }
    inputs_.resize(groups_.size());nextInputs_.resize(groups_.size());updateAlpha({});
}
bool SourceCanvasPlan::updateAlpha(const Pose&pose){
    bool changed=!initialized_;for(std::size_t i=0;i<groups_.size();++i){const auto&g=groups_[i];const auto value=pose.value("m_Alpha",scene_->nodes()[g.node].id,g.fallback);need(std::isfinite(value),"Nonfinite source CanvasGroup alpha");nextInputs_[i]=value;changed=changed||value!=inputs_[i];}
    if(!changed)return false;
    std::size_t group=0;
    for(const auto node:layout_.traversalIndices()){
        double alpha=parents_[node]?nextAlpha_[*parents_[node]]:1;
        while(group<groups_.size()&&groups_[group].node==node)alpha*=nextInputs_[group++];
        need(std::isfinite(alpha),"Source CanvasGroup alpha overflow");nextAlpha_[node]=alpha;
    }
    alpha_.swap(nextAlpha_);inputs_.swap(nextInputs_);initialized_=true;++alphaRebuilds_;return true;
}
std::optional<SourceClipUniforms> SourceCanvasPlan::clip(std::size_t node,std::span<const ResolvedNode> resolved,const Matrix4& inverseCanvas)const{
    need(node<ancestry_.size()&&resolved.size()==scene_->nodes().size()&&inverseCanvas.finite(),"Invalid source clip inputs");
    SourceClipUniforms out;std::array<double,2> low{-INFINITY,-INFINITY},high{INFINITY,INFINITY};bool found=false;
    for(const auto index:ancestry_[node].rectMasks){
        const auto&r=resolved[index];need(r.node==&scene_->nodes()[index],"Source clip node ordering mismatch");if(!r.rect)continue;
        const auto m=inverseCanvas*r.worldMatrix;need(m.finite(),"Nonfinite source clipping transform");
        std::array<double,2> minimum{INFINITY,INFINITY},maximum{-INFINITY,-INFINITY};
        for(const auto corner:r.rect->corners()){const auto p=transform(m,corner);for(unsigned axis=0;axis<2;++axis){need(std::isfinite(p[axis]),"Nonfinite source mask corner");minimum[axis]=std::min(minimum[axis],p[axis]);maximum[axis]=std::max(maximum[axis],p[axis]);}}
        const auto&padding=masks_[index].padding;for(unsigned axis=0;axis<2;++axis){low[axis]=std::max(low[axis],minimum[axis]+padding[axis]);high[axis]=std::min(high[axis],maximum[axis]-padding[axis+2]);}found=true;
    }
    if(!found)return {};
    out.rectangle={checkedFloat(low[0]),checkedFloat(low[1]),checkedFloat(high[0]),checkedFloat(high[1])};
    for(auto i=ancestry_[node].rectMasks.rbegin();i!=ancestry_[node].rectMasks.rend();++i)if(*i!=node&&masks_[*i].present){out.parameters=masks_[*i].parameters;out.hgSoftness=masks_[*i].hgSoftness;out.hasSoftness=true;break;}
    return out;
}
} // namespace endfield::core::source
