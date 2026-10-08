#include "core/source_layout.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::core::source {
namespace {
constexpr auto noParent=std::numeric_limits<std::size_t>::max();
void need(bool condition,const char* message){if(!condition)throw std::invalid_argument(message);}
bool same(double a,double b) noexcept {return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
template<std::size_t N>bool same(const std::array<double,N>&a,const std::array<double,N>&b) noexcept {
    for(std::size_t i=0;i<N;++i)if(!same(a[i],b[i]))return false;return true;
}
template<class T,class Equal>bool sameOptional(const std::optional<T>&a,const std::optional<T>&b,Equal equal) noexcept {
    return a.has_value()==b.has_value()&&(!a||equal(*a,*b));
}
template<std::size_t N>bool same(const std::optional<std::array<double,N>>&a,const std::optional<std::array<double,N>>&b) noexcept {
    return sameOptional(a,b,[](const auto&x,const auto&y){return same(x,y);});
}
bool sameRect(const std::optional<SourceRect>&a,const std::optional<SourceRect>&b) noexcept {
    return sameOptional(a,b,[](const auto&x,const auto&y){return same(x.origin,y.origin)&&same(x.size,y.size);});
}
bool sameOverride(const TransformOverride*a,const TransformOverride*b) noexcept {
    if(!a||!b)return a==b;
    if(a->active!=b->active||!same(a->localPosition,b->localPosition)||!same(a->localScale,b->localScale)||
        !same(a->anchoredPosition3D,b->anchoredPosition3D)||!same(a->localRotation,b->localRotation)||
        !same(a->sizeDelta,b->sizeDelta)||!same(a->anchorMin,b->anchorMin)||!same(a->anchorMax,b->anchorMax)||
        !same(a->pivot,b->pivot)||a->positionComponents.size()!=b->positionComponents.size())return false;
    auto left=a->positionComponents.begin(),right=b->positionComponents.begin();
    for(;left!=a->positionComponents.end();++left,++right)if(left->first!=right->first||!same(left->second,right->second))return false;
    return true;
}
const TransformOverride* find(const Overrides&values,std::string_view id) noexcept {
    const auto i=values.find(id);return i==values.end()?nullptr:&i->second;
}
template<std::size_t N>bool finite(const std::array<double,N>&v) noexcept {return std::all_of(v.begin(),v.end(),[](double x){return std::isfinite(x);});}
}
const Overrides& emptyLayoutOverrides(){static const Overrides empty;return empty;}
SourceRect SourceRect::fromSizePivot(Vec2 size,Vec2 pivot) noexcept {return {{-size[0]*pivot[0],-size[1]*pivot[1]},size};}
std::array<Vec3,4> SourceRect::corners() const noexcept {
    return {{{origin[0],origin[1],0},{origin[0]+size[0],origin[1],0},
             {origin[0]+size[0],origin[1]+size[1],0},{origin[0],origin[1]+size[1],0}}};
}
bool SourceRect::contains(Vec2 point,double tolerance) const noexcept {
    if(!finite(point)||size[0]==0||size[1]==0)return false;
    for(unsigned i=0;i<2;++i){const auto end=origin[i]+size[i];if(!(point[i]>=std::min(origin[i],end)-tolerance&&point[i]<=std::max(origin[i],end)+tolerance))return false;}
    return true;
}
SourceLayout::SourceLayout(const SceneDefinition&scene):scene_(&scene) {
    const auto nodes=scene.nodes();parents_.assign(nodes.size(),noParent);children_.resize(nodes.size());
    for(std::size_t i=0;i<nodes.size();++i)indices_.emplace(nodes[i].id,i);
    for(std::size_t i=0;i<nodes.size();++i){
        if(nodes[i].parent)parents_[i]=indices_.at(*nodes[i].parent);
        auto&children=children_[i];children.reserve(nodes[i].children.size());for(const auto&id:nodes[i].children)children.push_back(indices_.at(id));
    }
    traversal_.reserve(nodes.size());std::vector<std::size_t> pending{indices_.at(scene.rootID())};pending.reserve(nodes.size());
    while(!pending.empty()){
        const auto i=pending.back();pending.pop_back();traversal_.push_back(i);
        const auto&children=children_[i];for(auto child=children.rbegin();child!=children.rend();++child)pending.push_back(*child);
    }
}
std::optional<std::size_t> SourceLayout::nodeIndex(std::string_view id) const noexcept {
    const auto i=indices_.find(id);return i==indices_.end()?std::nullopt:std::optional(i->second);
}
ResolvedNode SourceLayout::resolveNode(const Node&node,const TransformOverride*override,
    const std::optional<SourceRect>&parentRect,const Matrix4&parentWorld,bool parentActive) {
    auto position=node.position;std::optional<SourceRect> rect;
    if(node.rect){
        const auto&source=*node.rect;
        const auto parentSize=parentRect?parentRect->size:Vec2{},parentOrigin=parentRect?parentRect->origin:Vec2{};
        const auto anchorMin=override&&override->anchorMin?*override->anchorMin:source.anchorMin;
        const auto anchorMax=override&&override->anchorMax?*override->anchorMax:source.anchorMax;
        const auto pivot=override&&override->pivot?*override->pivot:source.pivot;
        const auto delta=override&&override->sizeDelta?*override->sizeDelta:source.sizeDelta;
        const auto anchored=override&&override->anchoredPosition3D?Vec2{(*override->anchoredPosition3D)[0],(*override->anchoredPosition3D)[1]}:source.anchoredPosition;
        Vec2 size{};
        for(unsigned i=0;i<2;++i){
            const auto span=anchorMax[i]-anchorMin[i];size[i]=parentSize[i]*span+delta[i];
            const auto reference=parentOrigin[i]+parentSize[i]*(anchorMin[i]+span*pivot[i]);
            position[i]=reference+anchored[i];
        }
        position[2]=override&&override->anchoredPosition3D?(*override->anchoredPosition3D)[2]:node.position[2];
        rect=SourceRect::fromSizePivot(size,pivot);
    }
    if(override&&override->localPosition)position=*override->localPosition;
    if(override)for(const auto&[axis,value]:override->positionComponents){need(axis<3&&std::isfinite(value),"Invalid source local axis override");position[axis]=value;}
    const auto rotation=quaternionMatrix(override&&override->localRotation?*override->localRotation:node.rotation);
    need(rotation.has_value(),"Invalid source local quaternion");
    const auto scale=override&&override->localScale?*override->localScale:node.scale;
    need(finite(position)&&finite(scale)&&(!rect||(finite(rect->origin)&&finite(rect->size))),"Nonfinite source local transform");
    const auto local=Matrix4::translation(position[0],position[1],position[2]) * *rotation * Matrix4::scale(scale[0],scale[1],scale[2]);
    const auto world=parentWorld*local;need(world.finite(),"Nonfinite source world transform");
    return {&node,local,world,rect,parentActive&&(override&&override->active?*override->active:node.active)};
}
std::vector<ResolvedNode> SourceLayout::resolve(std::optional<SourceRect>rootParentRect,const Overrides&overrides) const {
    std::vector<ResolvedNode> result(scene_->nodes().size());
    for(const auto i:traversal_){
        const auto&node=scene_->nodes()[i];const auto p=parents_[i];
        const auto*parent=p==noParent?nullptr:&result[p];
        result[i]=resolveNode(node,find(overrides,node.id),parent?parent->rect:rootParentRect,
                              parent?parent->worldMatrix:Matrix4{},parent?parent->activeInHierarchy:true);
    }
    return result;
}
IncrementalResolver::IncrementalResolver(const SceneDefinition&scene):layout_(scene),cached_(scene.nodes().size()),staged_(scene.nodes().size()),dirty_(scene.nodes().size()) {
    pending_.reserve(scene.nodes().size());
}
std::span<const ResolvedNode> IncrementalResolver::nodes() const noexcept {return initialized_?std::span<const ResolvedNode>(cached_):std::span<const ResolvedNode>{};}
const ResolvedNode* IncrementalResolver::node(std::string_view id) const noexcept {
    const auto i=layout_.nodeIndex(id);return initialized_&&i?&cached_[*i]:nullptr;
}
std::span<const ResolvedNode> IncrementalResolver::resolve(std::optional<SourceRect>rootParentRect,const Overrides&overrides) {
    std::fill(dirty_.begin(),dirty_.end(),static_cast<std::uint8_t>(initialized_?0:1));pending_.clear();bool snapshotChanged=!initialized_;
    auto mark=[&](std::size_t i){if(!dirty_[i]){dirty_[i]=1;pending_.push_back(i);}};
    if(initialized_){
        for(const auto&[id,value]:overrides)if(!sameOverride(find(previousOverrides_,id),&value)){
            snapshotChanged=true;if(const auto i=layout_.nodeIndex(id))mark(*i);
        }
        for(const auto&[id,value]:previousOverrides_)if(!overrides.contains(id)){
            (void)value;snapshotChanged=true;if(const auto i=layout_.nodeIndex(id))mark(*i);
        }
        if(!sameRect(previousRootParentRect_,rootParentRect))mark(*layout_.nodeIndex(layout_.scene().rootID()));
        while(!pending_.empty()){
            const auto i=pending_.back();pending_.pop_back();for(const auto child:layout_.children_[i])mark(child);
        }
    }
    std::size_t rebuilt{};
    for(const auto i:layout_.traversal_)if(dirty_[i]){
        const auto&node=layout_.scene().nodes()[i];const auto p=layout_.parents_[i];
        const auto*parent=p==noParent?nullptr:dirty_[p]?&staged_[p]:&cached_[p];
        staged_[i]=SourceLayout::resolveNode(node,find(overrides,node.id),parent?parent->rect:rootParentRect,
                                           parent?parent->worldMatrix:Matrix4{},parent?parent->activeInHierarchy:true);
        ++rebuilt;
    }
    // Copy may allocate/throw. Do it before publishing any staged output so even
    // allocation failure leaves the geometry and its dependency snapshot paired.
    std::optional<Overrides> snapshot;if(snapshotChanged)snapshot.emplace(overrides);
    for(std::size_t i=0;i<cached_.size();++i)if(dirty_[i])cached_[i]=staged_[i];
    if(snapshot)previousOverrides_.swap(*snapshot);
    previousRootParentRect_=rootParentRect;initialized_=true;lastRebuilt_=rebuilt;
    rebuilt_+=rebuilt;reused_+=cached_.size()-rebuilt;if(rebuilt)++revision_;
    return cached_;
}
} // namespace endfield::core::source
