#include "core/source_selectable_color.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

// Original ColorTween executes Float subtract, multiply, add separately.
// In particular an interrupted fade must not introduce contracted FMA results.
#if defined(_MSC_VER)
#pragma float_control(precise,on,push)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::core::source {
namespace {
void need(bool condition,const char* message){if(!condition)throw std::invalid_argument(message);}
float sourceFloat(const Json& value,const char* message){
    need(value.isNumber(),message);const auto out=static_cast<float>(value.number());need(std::isfinite(out),message);return out;
}
bool flag(const Json& value,bool fallback){return value.isBool()?value.boolean():value.isNumber()?value.number()!=0:fallback;}
std::optional<std::string> target(const Json& value){return value["target_id"].isString()?std::optional(value["target_id"].string()):std::nullopt;}
SelectableTint sourceColor(const Json& value){return {sourceFloat(value["r"],"Invalid source Selectable color"),
    sourceFloat(value["g"],"Invalid source Selectable color"),sourceFloat(value["b"],"Invalid source Selectable color"),sourceFloat(value["a"],"Invalid source Selectable color")};}
SelectableColorBlock colorBlock(const Json& value){
    SelectableColorBlock out;
    out.multiplier=sourceFloat(value["m_ColorMultiplier"],"Invalid source Selectable ColorBlock");
    out.fadeDuration=sourceFloat(value["m_FadeDuration"],"Invalid source Selectable ColorBlock");
    need(value["m_FadeDuration"].number()>=0,"Invalid source Selectable ColorBlock");
    constexpr const char* keys[]{"m_NormalColor","m_HighlightedColor","m_PressedColor","m_SelectedColor","m_DisabledColor"};
    for(unsigned i=0;i<5;++i)out.states[i]=sourceColor(value[keys[i]]);return out;
}
} // namespace
SelectableTint SelectableColorBlock::color(SelectableState state) const {
    const auto index=static_cast<unsigned>(state);need(index<states.size(),"Invalid Selectable state");
    auto out=states[index];for(auto& channel:out)channel*=multiplier;return out;
}
float SelectableColor::Tween::progress(double time) const noexcept {
    if(!(duration>0))return 1;
    return std::min(1.f,std::max(0.f,static_cast<float>((time-started)/duration)));
}
SelectableTint SelectableColor::Tween::color(double time) const noexcept {
    const auto t=progress(time);SelectableTint out;
    for(unsigned i=0;i<4;++i){const float difference=target[i]-start[i];const float scaled=difference*t;out[i]=start[i]+scaled;}
    return out;
}
SelectableColor::SelectableColor(const SceneDefinition& scene,const MountedLayoutDocument& document){
    SourceLayout layout(scene);std::map<std::string,std::string,std::less<>> targetNodes;
    for(const auto index:layout.traversalIndices()){
        const auto& node=scene.nodes()[index];const auto records=document.components.find(node.id);if(records==document.components.end())continue;
        for(const auto& component:records->second){need(targetNodes.size()<maximumComponents,"Source ColorTint component count exceeds bounds");
            need(targetNodes.emplace(component.id,node.id).second,"Duplicate source ColorTint component ID");}
    }
    std::map<std::string,std::size_t,std::less<>> targets;
    for(const auto index:layout.traversalIndices()){
        const auto& node=scene.nodes()[index];const auto records=document.components.find(node.id);if(records==document.components.end())continue;
        for(const auto& component:records->second){
            if(!component.enabled()||(component.kind()!="UIButton"&&component.kind()!="Button"&&component.kind()!="Selectable")||
                !component["m_Transition"].isNumber()||component["m_Transition"].number()!=1)continue;
            const auto graphic=target(component["m_TargetGraphic"]);if(!graphic){ignored_.push_back(node.id);continue;}
            const auto targetNode=targetNodes.find(*graphic);need(targetNode!=targetNodes.end(),"Missing source ColorTint target Graphic");
            auto colors=colorBlock(component["m_Colors"]);const bool interactable=flag(component["m_Interactable"],true);
            need(bindings_.size()<maximumBindings,"Source ColorTint binding count exceeds bounds");
            const auto bindingIndex=bindings_.size();need(byButton_.emplace(node.id,bindingIndex).second,"Several source ColorTint Selectables share one button node");
            bindings_.push_back({node.id,component.id,*graphic,targetNode->second,interactable,colors});instanceIDs_.push_back(node.id);
            const auto [entry,inserted]=targets.emplace(targetNode->second,renderers_.size());
            if(inserted){renderers_.push_back({});outputSlots_.push_back(output_.emplace(targetNode->second,SelectableTint{}).first);}
            const auto initial=interactable?SelectableState::normal:SelectableState::disabled;
            instances_.push_back({initial,true,entry->second});assign(entry->second,colors.color(initial),0);
        }
    }
    // Construction exposes the exact latest binding's initial shared tint.
    for(std::size_t i=0;i<renderers_.size();++i)outputSlots_[i]->second=renderers_[i].color(0);
}
std::optional<SelectableState> SelectableColor::state(std::string_view button) const noexcept {
    const auto found=byButton_.find(button);return found==byButton_.end()?std::nullopt:std::optional(instances_[found->second].state);
}
std::optional<double> SelectableColor::advance(double value) noexcept {
    if(!std::isfinite(value))return {};const auto time=std::max(clock_.value_or(value),value);clock_=time;return time;
}
void SelectableColor::assign(std::size_t renderer,SelectableTint color,double time) noexcept {renderers_[renderer]={color,color,time,0};}
void SelectableColor::reset(double value){
    const auto time=advance(value);if(!time)return;
    for(std::size_t i=0;i<bindings_.size();++i){const auto& binding=bindings_[i];auto& instance=instances_[i];
        instance.state=binding.sourceInteractable?SelectableState::normal:SelectableState::disabled;instance.enabled=true;
        assign(instance.renderer,binding.colors.color(instance.state),*time);}
}
void SelectableColor::setState(SelectableState state,std::string_view button,double value,bool reduceMotion){
    const auto time=advance(value);if(!time)return;const auto found=byButton_.find(button);if(found==byButton_.end())return;
    auto& instance=instances_[found->second];if(!instance.enabled)return;
    const auto& binding=bindings_[found->second];const auto target=binding.colors.color(state);
    instance.state=state;const auto current=renderers_[instance.renderer].color(*time);
    const auto duration=reduceMotion||current==target?0:binding.colors.fadeDuration;
    renderers_[instance.renderer]={current,target,*time,duration};
}
void SelectableColor::setEnabled(bool enabled,std::string_view button,double value){
    const auto time=advance(value);if(!time)return;const auto found=byButton_.find(button);if(found==byButton_.end())return;
    auto& instance=instances_[found->second];if(instance.enabled==enabled)return;instance.enabled=enabled;
    const auto& binding=bindings_[found->second];instance.state=binding.sourceInteractable?SelectableState::normal:SelectableState::disabled;
    assign(instance.renderer,enabled?binding.colors.color(instance.state):SelectableTint{1,1,1,1},*time);
}
const SelectableTints& SelectableColor::colors(double value,bool reduceMotion){
    const auto time=advance(value).value_or(clock_.value_or(0));
    for(std::size_t i=0;i<renderers_.size();++i){if(reduceMotion)assign(i,renderers_[i].target,time);outputSlots_[i]->second=renderers_[i].color(time);}
    return output_;
}
bool SelectableColor::requiresFrames(double value) const noexcept {
    if(!std::isfinite(value))return false;const auto time=std::max(clock_.value_or(value),value);
    return std::any_of(renderers_.begin(),renderers_.end(),[&](const auto& tween){return tween.progress(time)<1;});
}
std::optional<DesktopHoverProfile> DesktopHoverProfile::fromJson(const Json& value){
    if(value.isNull())return {};need(value.isObject()&&value["rootID"].isString(),"Invalid source profile hover descriptor");
    DesktopHoverProfile profile;profile.rootID=value["rootID"].string();need(!profile.rootID.empty(),"Missing profile root identity");
    need(value["buttonIDs"].isArray()&&value["nodeIDs"].isArray()&&value["buttonIDs"].array().size()<=4096&&value["nodeIDs"].array().size()<=65536,"Invalid source profile membership");
    for(const auto& id:value["buttonIDs"].array()){need(id.isString(),"Invalid profile button identity");profile.buttonIDs.push_back(id.string());}
    for(const auto& id:value["nodeIDs"].array()){need(id.isString(),"Invalid profile node identity");profile.nodeIDs.push_back(id.string());}return profile;
}
DesktopHoverFeedback::DesktopHoverFeedback(const SceneDefinition& scene,const MountedLayoutDocument& document,
    const SelectableColor& selectable,std::optional<DesktopHoverProfile> profile){
    for(const auto& node:scene.nodes())if(node.path.ends_with("/HoverHint/NaviHint/Img"))
        for(const auto& button:document.buttons)if(node.path.starts_with(button.path+"/")){sideEdges_.insert(node.id);break;}
    if(profile){
        need(scene.node(profile->rootID)!=nullptr,"Profile hover root is not in the mounted scene");profileRoot_=profile->rootID;
        for(const auto& id:profile->buttonIDs){need(scene.node(id)!=nullptr,"Profile hover button is not in the mounted scene");profileButtons_.insert(id);}
        for(const auto& binding:selectable.bindings())if(binding.buttonNodeID==*profileRoot_){profile_=Target{binding.targetNodeID,binding.colors};profileHighlight_=binding.targetNodeID;break;}
        for(const auto& id:profile->nodeIDs){const auto* node=scene.node(id);need(node!=nullptr,"Profile hover node is not in the mounted scene");
            if(node->path.find("/PlayerInfo/DecoNode/")==std::string::npos||
                (node->name!="LeftLineImage"&&node->name!="RightLineImage"&&node->name!="LineImage"&&node->name!="LeftBottomImage"))continue;
            const auto* image=document.component("UIImage",id);if(!image)continue;
            const auto& raw=(*image)["m_Color"]["a"];const auto alpha=raw.isNumber()?static_cast<float>(raw.number()):1.f;
            if(profile_&&alpha>0){const auto [out,inserted]=output_.emplace(id,1.f);need(inserted,"Duplicate profile decoration identity");decorations_.push_back({out,alpha});}
        }
    }
    for(const auto& binding:selectable.bindings()){
        const auto* node=scene.node(binding.buttonNodeID);if(!node||node->name!="QuitBtn")continue;
        const auto path=node->path+"/Bg";const auto found=std::find_if(scene.nodes().begin(),scene.nodes().end(),[&](const auto& n){return n.path==path;});
        if(found!=scene.nodes().end())quit_.push_back({output_.emplace(found->id,1.f).first,{binding.targetNodeID,binding.colors}});
    }
}
std::optional<std::string_view> DesktopHoverFeedback::groupedButton(std::optional<std::string_view> id) const noexcept {
    if(!id||!profileButtons_.contains(*id))return id;
    return profileRoot_?std::optional<std::string_view>(*profileRoot_):std::nullopt;
}
float DesktopHoverFeedback::progress(const Target& target,const SelectableTints& tints) noexcept {
    const float normal=target.colors.color(SelectableState::normal)[3];
    const float range=target.colors.color(SelectableState::highlighted)[3]-normal;
    const auto found=tints.find(target.targetNodeID);
    if(!(range>0)||found==tints.end()||!std::isfinite(found->second[3]))return 0;
    return std::min(1.f,std::max(0.f,(found->second[3]-normal)/range));
}
const std::map<std::string,float,std::less<>>& DesktopHoverFeedback::opacities(const SelectableTints& tints){
    if(profile_){const auto amount=progress(*profile_,tints);for(auto& decoration:decorations_){
        const float alpha=decoration.authoredAlpha;decoration.output->second=1.f+std::max(0.f,.62f-alpha)/alpha*amount;}}
    for(auto& quit:quit_)quit.output->second=1.f+.25f*progress(quit.target,tints);return output_;
}
} // namespace endfield::core::source

#if defined(_MSC_VER)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
