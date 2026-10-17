#include "core/source_button_motion.hpp"
#include "core/source_layout.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>
#include <variant>
#include <stdexcept>

namespace endfield::core::source {
namespace {
void need(bool condition,const char*message){if(!condition)throw std::invalid_argument(message);}
unsigned slot(ButtonState state){const auto i=static_cast<unsigned>(state);need(i<4,"Invalid source button state");return i;}
double number(const Json&v,double fallback=0){return v.isNumber()?v.number():fallback;}
bool flag(const Json&v,bool fallback=false){return v.isBool()?v.boolean():v.isNumber()?v.number()!=0:fallback;}
std::string text(const Json&v){return v.isString()?v.string():std::string{};}
const Json* first(const Json::Array&values,const auto&predicate){for(const auto&v:values)if(predicate(v))return &v;return nullptr;}
std::string lowerASCII(std::string value){for(auto&c:value)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return value;}
}
std::string_view buttonStateName(ButtonState state) noexcept {
    constexpr std::string_view names[]{"Normal","Highlighted","Pressed","Disabled"};const auto i=static_cast<unsigned>(state);return i<4?names[i]:std::string_view{};
}
std::optional<ButtonState> buttonState(std::string_view name) noexcept {
    for(unsigned i=0;i<4;++i)if(buttonStateName(static_cast<ButtonState>(i))==name)return static_cast<ButtonState>(i);return {};
}
std::vector<AnimatorBinding> AnimatorBinding::fromJson(const Json&array){
    need(array.array().size()<=4096,"Too many source button instances");std::vector<AnimatorBinding> result;
    for(const auto&a:array.array()){
        AnimatorBinding b;b.rootID=text(a["root_node_id"]);need(!b.rootID.empty(),"Missing animator root");
        for(const auto&s:a["states"].array()){
            const auto kind=buttonState(text(s["name"]));need(kind.has_value(),"Unknown animator state");const auto id=text(s["bound_clip_id"]);
            need(!id.empty()&&b.boundClipIDs[slot(*kind)].empty(),"Duplicate or missing bound animator state");b.boundClipIDs[slot(*kind)]=id;
        }
        for(const auto&id:b.boundClipIDs)need(!id.empty(),"Incomplete source animator states");result.push_back(std::move(b));
    }return result;
}
std::vector<AnimatorBinding> AnimatorBinding::fromTransitions(const Json&data){
    const auto&rows=data["instances"].array();need(rows.size()<=4096,"Too many source button instances");std::vector<AnimatorBinding> result;
    for(const auto&row:rows){
        AnimatorBinding b;b.rootID=text(row["root_node_id"]);need(!b.rootID.empty(),"Missing animator root");
        for(const auto&t:row["transitions"].array()){
            const auto state=buttonState(text(t["destination_name"]));need(state.has_value(),"Unknown transition destination");const auto id=text(t["destination_bound_clip_id"]);
            auto&binding=b.boundClipIDs[slot(*state)];need(!id.empty()&&(binding.empty()||binding==id),"Ambiguous transition state binding");binding=id;
        }
        for(const auto&id:b.boundClipIDs)need(!id.empty(),"Incomplete transition state bindings");result.push_back(std::move(b));
    }return result;
}
struct ButtonAnimation::Impl {
    struct Channel {
        std::string node,group,attribute;
        bool operator<(const Channel&other) const noexcept {return std::tie(node,group,attribute)<std::tie(other.node,other.group,other.attribute);}
    };
    struct Samples {std::map<Channel,Value> channels;std::set<std::string,std::less<>> unbound;};
    using Sample=std::shared_ptr<const Samples>;
    struct Template {const Clip*clip{};double speed{},offset{};};
    struct Transition {std::optional<ButtonState> source;ButtonState destination;double duration{};bool fixed{},canRepeat{};};
    struct Configuration {std::string root;std::array<Template,4> templates;std::vector<Transition> transitions;std::optional<std::string> hover;};
    struct Playback {ButtonState state{ButtonState::normal};double started{};bool endpoint{};};
    using Origin=std::variant<Playback,Sample>;
    struct Blend {Origin origin;double started{},duration{};};
    struct Cached {double time{};Sample samples;};
    struct Instance {Playback playback;std::optional<Blend> blend;bool hovered{};std::array<std::optional<Cached>,4> cache;};
    const SceneDefinition*scene;
    std::vector<std::string> order;
    std::map<std::string,std::size_t,std::less<>> indices;
    std::vector<Configuration> configurations;
    std::vector<Instance> instances;
    std::optional<double> clock;
    std::uint64_t generation{},sampledCurves{};
    std::optional<std::pair<std::uint64_t,double>> settledDemand;
    Impl(const SceneDefinition&s,const Library&library,std::span<const AnimatorBinding> animators,const Json&data):scene(&s){
        need(animators.size()<=4096,"Too many source button instances");
        std::map<std::string,std::pair<double,double>,std::less<>> settings;
        const auto&controllers=data["controllers"].array();const auto&metadata=data["instances"].array();
        for(const auto&controller:controllers)for(const auto&machine:controller["state_machines"].array())for(const auto&state:machine["states"].array())
            if(state["source_clip_id"].isString())settings[text(state["source_clip_id"])]=std::pair(number(state["speed"],1),number(state["cycle_offset"]));
        order.reserve(animators.size());configurations.reserve(animators.size());instances.reserve(animators.size());
        for(const auto&animator:animators){
            const auto*source=first(metadata,[&](const Json&v){return text(v["root_node_id"])==animator.rootID;});
            need(s.node(animator.rootID)&&source,"Missing source button instance metadata");
            need(indices.emplace(animator.rootID,instances.size()).second,"Repeated source button instance");
            Configuration config;config.root=animator.rootID;
            const auto&transitions=(*source)["transitions"].array();need(transitions.size()<=65536,"Too many source button transitions");
            for(unsigned state=0;state<4;++state){
                const auto*clip=library.clip(animator.boundClipIDs[state]);need(clip,"Missing source button state clip");
                const auto*t=first(transitions,[&](const Json&v){return text(v["destination_bound_clip_id"])==clip->id;});
                need(t,"Missing source button canonical binding");const auto timing=settings.find(text((*t)["destination_source_clip_id"]));
                need(timing!=settings.end()&&std::isfinite(timing->second.first)&&std::isfinite(timing->second.second)&&std::isfinite(clip->lastKeyTime)&&clip->lastKeyTime>=0,"Missing source button state timing");
                config.templates[state]={clip,timing->second.first,timing->second.second};
            }
            for(const auto&t:transitions){
                const auto destination=buttonState(text(t["destination_name"]));const auto duration=number(t["duration"]);
                need(destination&&std::isfinite(duration)&&duration>=0,"Invalid source button transition");std::optional<ButtonState> origin;
                if(t["source_state_index"].isNumber()){
                    const auto*controller=first(controllers,[&](const Json&v){return text(v["id"])==text((*source)["controller_id"]);});
                    if(controller){const auto*machine=first((*controller)["state_machines"].array(),[&](const Json&v){return number(v["index"])==number(t["state_machine_index"]);});
                        if(machine){const auto*state=first((*machine)["states"].array(),[&](const Json&v){return number(v["index"])==number(t["source_state_index"]);});if(state)origin=buttonState(text((*state)["name"]));}}
                }
                config.transitions.push_back({origin,*destination,duration,flag(t["has_fixed_duration"]),flag(t["can_transition_to_self"])});
            }
            if((*source)["hover_enable_node_id"].isString()){config.hover=text((*source)["hover_enable_node_id"]);need(s.node(*config.hover),"Unknown source hover-enable transform");}
            Instance instance;instance.playback.state=flag((*source)["source_interactable"],true)?ButtonState::normal:ButtonState::disabled;
            order.push_back(animator.rootID);configurations.push_back(std::move(config));instances.push_back(std::move(instance));
        }
    }
    std::optional<double> advance(double value){if(!std::isfinite(value))return {};clock=std::max(clock.value_or(value),value);return clock;}
    Sample sample(const Playback&playback,std::size_t index,double time){
        const auto state=slot(playback.state);const auto&t=configurations[index].templates[state];const auto&clip=*t.clip;
        const auto local=*buttonClipTime(time,playback.started,t.speed,t.offset,clip.lastKeyTime,playback.endpoint);
        auto&cached=instances[index].cache[state];if(cached&&cached->time==local)return cached->samples;
        auto samples=std::make_shared<Samples>();
        for(const auto&curve:clip.curves){
            if(curve.nodeIDs().empty()){samples->unbound.insert(curve.path());continue;}
            ++sampledCurves;const auto value=curve.sample(local);if(!value)continue;
            for(const auto&id:curve.nodeIDs())if(scene->node(id))samples->channels[{id,curve.group(),curve.attribute()}]=*value;
        }
        cached=Cached{local,samples};return samples;
    }
    Sample sample(const Instance&instance,std::size_t index,double time){
        const auto destination=sample(instance.playback,index,time);if(!instance.blend)return destination;
        const auto&blend=*instance.blend;const auto origin=std::holds_alternative<Playback>(blend.origin)?sample(std::get<Playback>(blend.origin),index,time):std::get<Sample>(blend.origin);
        const auto fraction=std::clamp((time-blend.started)/blend.duration,0.,1.);auto result=std::make_shared<Samples>();
        result->unbound=origin->unbound;result->unbound.insert(destination->unbound.begin(),destination->unbound.end());
        for(const auto&[channel,from]:origin->channels){const auto to=destination->channels.find(channel);result->channels[channel]=blendButtonValue(from,to==destination->channels.end()?from:to->second,fraction);}
        for(const auto&[channel,to]:destination->channels)if(!origin->channels.contains(channel))result->channels[channel]=to;
        return result;
    }
    void applyValue(const Channel&channel,const Value&value,Pose&pose){
        const auto*node=scene->node(channel.node);if(!node)return;auto&t=pose.transforms[channel.node];const auto&a=channel.attribute;const auto axis=a.ends_with(".x")?0u:a.ends_with(".y")?1u:2u;const auto&v=value.components;
        if(channel.group=="m_PositionCurves"&&value.kind==ValueKind::vector3)t.localPosition=Vec3{v[0],v[1],v[2]};
        else if(channel.group=="m_ScaleCurves"&&value.kind==ValueKind::vector3)t.localScale=Vec3{v[0],v[1],v[2]};
        else if(channel.group=="m_RotationCurves"&&value.kind==ValueKind::quaternion)t.localRotation=v;
        else if(channel.group=="m_FloatCurves"&&value.kind==ValueKind::scalar){
            if(a=="m_IsActive")t.active=v[0]>=.5;
            else if(a=="m_LocalPosition.x"||a=="m_LocalPosition.y"||a=="m_LocalPosition.z")t.positionComponents[axis]=v[0];
            else if(a=="m_LocalScale.x"||a=="m_LocalScale.y"||a=="m_LocalScale.z"){auto x=t.localScale.value_or(node->scale);x[axis]=v[0];t.localScale=x;}
            else if(a=="m_AnchoredPosition.x"||a=="m_AnchoredPosition.y"){
                auto x=t.anchoredPosition3D.value_or(Vec3{node->rect?node->rect->anchoredPosition[0]:0,node->rect?node->rect->anchoredPosition[1]:0,node->position[2]});x[axis]=v[0];t.anchoredPosition3D=x;
            }else if(a=="m_AnchorMin.x"||a=="m_AnchorMin.y"){auto x=t.anchorMin.value_or(node->rect?node->rect->anchorMin:Vec2{});x[axis]=v[0];t.anchorMin=x;}
            else if(a=="m_AnchorMax.x"||a=="m_AnchorMax.y"){auto x=t.anchorMax.value_or(node->rect?node->rect->anchorMax:Vec2{});x[axis]=v[0];t.anchorMax=x;}
            else pose.properties[channel.node][a]=v[0];
        }
    }
};
ButtonAnimation::ButtonAnimation(const SceneDefinition&s,const Library&l,std::span<const AnimatorBinding>a,const Json&t):impl_(std::make_unique<Impl>(s,l,a,t)){}
ButtonAnimation::~ButtonAnimation()=default;
ButtonAnimation::ButtonAnimation(ButtonAnimation&&) noexcept=default;
ButtonAnimation&ButtonAnimation::operator=(ButtonAnimation&&) noexcept=default;
std::span<const std::string> ButtonAnimation::instanceIDs() const noexcept{return impl_->order;}
std::optional<ButtonState> ButtonAnimation::state(std::string_view id) const noexcept {const auto i=impl_->indices.find(id);return i==impl_->indices.end()?std::nullopt:std::optional(impl_->instances[i->second].playback.state);}
std::uint64_t ButtonAnimation::stateGeneration() const noexcept{return impl_->generation;}
std::uint64_t ButtonAnimation::sampledCurveCount() const noexcept{return impl_->sampledCurves;}
std::size_t ButtonAnimation::cachedStateCount() const noexcept{std::size_t n{};for(const auto&i:impl_->instances)for(const auto&s:i.cache)if(s)++n;return n;}
bool ButtonAnimation::requiresFrames(double value){
    auto&p=*impl_;if(!std::isfinite(value))return false;const auto time=std::max(p.clock.value_or(value),value);
    if(p.settledDemand&&p.settledDemand->first==p.generation&&time>=p.settledDemand->second)return false;
    for(std::size_t index=0;index<p.instances.size();++index){const auto&i=p.instances[index];const auto&t=p.configurations[index].templates[slot(i.playback.state)];
        if(i.blend&&time-i.blend->started<i.blend->duration)return true;
        if(i.playback.endpoint||t.clip->curves.empty()||t.clip->lastKeyTime<=0||t.speed==0)continue;
        const auto local=std::max(0.,time-i.playback.started)*t.speed+t.offset*t.clip->lastKeyTime;
        if((t.speed>0&&local<t.clip->lastKeyTime)||(t.speed<0&&local>0))return true;
    }
    p.settledDemand=std::pair(p.generation,time);return false;
}
void ButtonAnimation::setState(ButtonState state,std::string_view id,double value,bool reduce){
    slot(state);auto&p=*impl_;const auto time=p.advance(value);const auto i=p.indices.find(id);if(!time||i==p.indices.end())return;
    const auto index=i->second;auto&instance=p.instances[index];const auto&config=p.configurations[index];
    auto transition=std::find_if(config.transitions.begin(),config.transitions.end(),[&](const auto&t){return t.destination==state&&t.source==instance.playback.state;});
    if(transition==config.transitions.end())transition=std::find_if(config.transitions.begin(),config.transitions.end(),[&](const auto&t){return t.destination==state&&!t.source;});
    if(transition==config.transitions.end()||(state==instance.playback.state&&!transition->canRepeat))return;
    const auto previous=instance.playback;const auto current=p.sample(instance,index,*time);
    const auto&t=config.templates[slot(previous.state)];const auto duration=transition->fixed?transition->duration:t.speed==0?0:transition->duration*t.clip->lastKeyTime/std::abs(t.speed);
    Impl::Origin origin=instance.blend?Impl::Origin(current):Impl::Origin(previous);
    instance.playback={state,*time,reduce};instance.blend=duration>0&&!reduce?std::optional(Impl::Blend{std::move(origin),*time,duration}):std::nullopt;
    if(state==ButtonState::highlighted)instance.hovered=true;if(state==ButtonState::normal||state==ButtonState::disabled)instance.hovered=false;
    ++p.generation;
}
void ButtonAnimation::setHovered(bool hovered,std::string_view id,double time,bool reduce){
    auto&p=*impl_;const auto i=p.indices.find(id);if(i==p.indices.end())return;auto&instance=p.instances[i->second];
    if(instance.playback.state!=ButtonState::pressed&&instance.playback.state!=ButtonState::disabled)setState(hovered?ButtonState::highlighted:ButtonState::normal,id,time,reduce);
    const auto target=hovered&&instance.playback.state!=ButtonState::disabled;if(instance.hovered!=target)++p.generation;instance.hovered=target;
}
void ButtonAnimation::apply(Pose&pose,double value,bool reduce){
    auto&p=*impl_;const auto time=p.advance(value);if(!time)return;
    for(std::size_t index=0;index<p.instances.size();++index){auto&instance=p.instances[index];const auto&config=p.configurations[index];
        if(reduce){instance.playback.endpoint=true;instance.blend.reset();}
        if(instance.blend&&*time-instance.blend->started>=instance.blend->duration)instance.blend.reset();
        const auto sampled=p.sample(instance,index,*time);for(const auto&[channel,v]:sampled->channels)p.applyValue(channel,v,pose);
        pose.unboundPaths.insert(sampled->unbound.begin(),sampled->unbound.end());if(config.hover)pose.transforms[*config.hover].active=instance.hovered;
    }
}
void ButtonAnimation::reset(double time,bool reduce){
    auto&p=*impl_;if(!std::isfinite(time))return;p.clock=time;++p.generation;
    for(auto&instance:p.instances){instance.playback={ButtonState::normal,time,reduce};instance.blend.reset();instance.hovered=false;}
}
struct DomainAnimation::Impl {
    struct Binding {std::string root,level;int ease{};Clip selected,deselected;std::optional<Clip> hover;};
    const SceneDefinition*scene;
    std::set<std::string,std::less<>> loaded;
    SourceLayout layout;
    std::vector<Binding> bindings;
    std::vector<Clip> loops;
    Impl(const Json&source,const SceneDefinition&s,std::string domain,std::set<std::string,std::less<>> levels):scene(&s),loaded(std::move(levels)),layout(s){
        need(source["schema_version"].isNumber()&&source["schema_version"].number()==1,"Unsupported original Domain animation schema");
        std::set<std::string,std::less<>> roots;const auto&instances=source["instances"].array();need(instances.size()<=4096,"Too many Domain animation roots");
        for(const auto&i:instances){
            const auto root=text(i["root_node_id"]);if(!s.node(root))continue;need(roots.insert(root).second,"Repeated Domain animation root");
            const auto&wrapper=i["wrapper_data"];if(!flag(wrapper["m_Enabled"],true)||!flag(i["animation_data"]["m_Enabled"],true))continue;
            std::vector<Clip> clips;for(const auto&c:i["bound_clips"].array())clips.push_back(Clip::fromJson(c));
            for(const auto&c:clips)for(const auto&curve:c.curves)for(const auto&id:curve.nodeIDs())need(s.node(id),"Domain clip binding is outside assembled source scene");
            auto unique=[&](std::string_view binding)->const Clip*{const Clip*found{};for(const auto&c:clips)if(c.binding==binding){need(!found,"Repeated Domain clip binding");found=&c;}return found;};
            if(flag(i["is_level_model_root"])){
                const auto*level=first(i["levels"].array(),[&](const Json&v){return lowerASCII(text(v["domain"]))==lowerASCII(domain);});
                const auto levelID=level?text((*level)["level_id"]):std::string{};const auto ease=number(wrapper["_options"]["animEase"]);
                const auto*selected=unique("_animationIn"),*deselected=unique("_animationOut");
                need(!levelID.empty()&&loaded.contains(levelID)&&(ease==1||ease==6)&&selected&&deselected,"Unresolved original level-model wrapper");
                const auto*hover=unique("SourceAnimation");bindings.push_back({root,levelID,static_cast<int>(ease),*selected,*deselected,hover?std::optional(*hover):std::nullopt});
            }
            if(flag(wrapper["autoPlay"]))if(const auto*loop=unique("_animationLoop")){
                need(number(wrapper["_options"]["animEase"])==1&&loop->wrapMode==2,"Unsupported original Domain auto-loop clock");loops.push_back(*loop);
            }
        }
    }
};
DomainAnimation::DomainAnimation(const Json&j,const SceneDefinition&s,std::string d,std::set<std::string,std::less<>>ids):impl_(std::make_unique<Impl>(j,s,std::move(d),std::move(ids))){}
DomainAnimation::~DomainAnimation()=default;
DomainAnimation::DomainAnimation(DomainAnimation&&) noexcept=default;
DomainAnimation&DomainAnimation::operator=(DomainAnimation&&) noexcept=default;
std::size_t DomainAnimation::bindingCount() const noexcept{return impl_->bindings.size();}
std::size_t DomainAnimation::autoLoopCount() const noexcept{return impl_->loops.size();}
Pose DomainAnimation::pose(const DomainState&state,const Overrides&overrides) const {
    const auto&p=*impl_;need(std::isfinite(state.ambientTime)&&state.ambientTime>=0&&(!state.selectionElapsed||(std::isfinite(*state.selectionElapsed)&&*state.selectionElapsed>=0))&&(!state.currentLevelID||p.loaded.contains(*state.currentLevelID)),"Invalid explicit Domain animation state");
    for(const auto&[id,time]:state.hoverClipTimes)need(p.loaded.contains(id)&&std::isfinite(time)&&time>=0,"Invalid explicit Domain hover time");
    (void)p.layout.resolve({},overrides);Pose pose;pose.transforms=overrides;
    for(const auto&binding:p.bindings){
        const bool selected=state.currentLevelID==binding.level;const auto&clip=selected?binding.selected:binding.deselected;
        const auto time=selected&&state.selectionElapsed?(binding.ease==6?VisibilityClock::clipTime(*state.selectionElapsed,clip.lastKeyTime):std::min(*state.selectionElapsed,clip.lastKeyTime)):clip.lastKeyTime;
        applyClip(clip,time,pose,*p.scene);
        const auto hover=state.hoverClipTimes.find(binding.level);if(hover!=state.hoverClipTimes.end()&&binding.hover)applyClip(*binding.hover,hover->second,pose,*p.scene);
    }
    for(const auto&loop:p.loops)applyClip(loop,state.ambientTime,pose,*p.scene);
    return pose;
}
} // namespace endfield::core::source
