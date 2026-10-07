#include "core/source_animation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace endfield::core::source {
namespace {
void need(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
constexpr std::size_t maximumNodes = 16384, maximumCurves = 65536, maximumKeys = 1000000;
bool finite(double value) { return std::isfinite(value); }
double numeric(const Json& value) {
    if (value.isNumber()) return value.number();
    if (value.isString()) {
        const auto token = value.string();
        if (token == "Infinity") return std::numeric_limits<double>::infinity();
        if (token == "-Infinity") return -std::numeric_limits<double>::infinity();
        if (token == "NaN") return std::numeric_limits<double>::quiet_NaN();
    }
    throw std::invalid_argument("Expected source numeric component");
}
int integer(const Json& value) {
    const auto n = value.integer();
    need(n >= std::numeric_limits<int>::min() && n <= std::numeric_limits<int>::max(), "Source integer overflow");
    return static_cast<int>(n);
}
std::string identity(const Json& value) {
    auto result = value.string(); need(!result.empty() && result.size() <= 4096, "Invalid source identity"); return result;
}
Vec2 vec2(const Json& v) { return {numeric(v["x"]),numeric(v["y"])}; }
Vec3 vec3(const Json& v) { return {numeric(v["x"]),numeric(v["y"]),numeric(v["z"])}; }
Quaternion quat(const Json& v) { return {numeric(v["x"]),numeric(v["y"]),numeric(v["z"]),numeric(v["w"])}; }
template<std::size_t N> bool allFinite(const std::array<double,N>& v) { return std::all_of(v.begin(),v.end(),finite); }
std::optional<Quaternion> normalized(Quaternion value) noexcept {
    // Match SIMD component length and normalization, not a shortest-arc slerp.
    const auto magnitude = std::sqrt(value[0]*value[0]+value[1]*value[1]+value[2]*value[2]+value[3]*value[3]);
    if (!finite(magnitude) || magnitude <= 0) return {};
    for (auto& component : value) component /= magnitude;
    return value;
}
double bezier(double u,double a,double b,double c,double d) noexcept {
    const auto v=1-u; return v*v*v*a + 3*v*v*u*b + 3*v*u*u*c + u*u*u*d;
}
double bezierTime(double time,double outgoing,double incoming) noexcept {
    double lower=0,upper=1,u=time;
    for (unsigned i=0;i<8;++i) {
        const auto difference=bezier(u,0,outgoing,1-incoming,1)-time;
        if (std::abs(difference)<=1e-14) return u;
        if (difference<0) lower=u; else upper=u;
        const auto v=1-u, derivative=3*v*v*outgoing+6*v*u*(1-incoming-outgoing)+3*u*u*incoming;
        const auto candidate=derivative>1e-14 ? u-difference/derivative : std::numeric_limits<double>::quiet_NaN();
        u=finite(candidate)&&candidate>lower&&candidate<upper ? candidate : (lower+upper)/2;
    }
    for (unsigned i=0;i<40;++i) {
        u=(lower+upper)/2;
        const auto difference=bezier(u,0,outgoing,1-incoming,1)-time;
        if (std::abs(difference)<=1e-14) break;
        if (difference<0) lower=u; else upper=u;
    }
    return u;
}
unsigned axis(std::string_view attribute) { return attribute.ends_with(".x") ? 0u : attribute.ends_with(".y") ? 1u : 2u; }
bool rectAttribute(std::string_view name) {
    constexpr std::string_view names[]{"m_LocalPosition.x","m_LocalPosition.y","m_LocalPosition.z",
        "m_LocalScale.x","m_LocalScale.y","m_LocalScale.z","m_AnchoredPosition.x","m_AnchoredPosition.y",
        "m_AnchorMin.x","m_AnchorMin.y","m_AnchorMax.x","m_AnchorMax.y","m_SizeDelta.x","m_SizeDelta.y","m_Pivot.x","m_Pivot.y"};
    return std::find(std::begin(names),std::end(names),name)!=std::end(names);
}
void validateOverrides(const Overrides& values) {
    for (const auto& [id,v] : values) {
        need(!id.empty(),"Empty source override identity");
        for (const auto* p : {&v.localPosition,&v.localScale,&v.anchoredPosition3D}) if (*p) need(allFinite(**p),"Nonfinite source vector override");
        for (const auto* p : {&v.sizeDelta,&v.anchorMin,&v.anchorMax,&v.pivot}) if (*p) need(allFinite(**p),"Nonfinite source rect override");
        if (v.localRotation) need(normalized(*v.localRotation).has_value(),"Invalid source rotation override");
        for (const auto& [component,value] : v.positionComponents) need(component<3 && finite(value),"Invalid source position component override");
    }
}
}
std::optional<Quaternion> normalizedQuaternion(Quaternion value) noexcept { return normalized(value); }
Quaternion multiplyQuaternion(const Quaternion& a,const Quaternion& b) noexcept {
    return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
            a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
            a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
            a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
}
std::optional<Quaternion> slerpQuaternion(Quaternion a,Quaternion b,double fraction) noexcept {
    const auto first=normalized(a),second=normalized(b);
    if(!first||!second||!finite(fraction))return {};
    a=*first;b=*second;
    auto dot=a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];
    if(dot<0){for(auto& c:b)c=-c;dot=-dot;}
    dot=std::clamp(dot,0.0,1.0);
    const auto angle=std::acos(dot),sine=std::sin(angle);
    if(sine<1e-12) {for(unsigned i=0;i<4;++i)a[i]+=fraction*(b[i]-a[i]);return normalized(a);}
    const auto left=std::sin((1-fraction)*angle)/sine,right=std::sin(fraction*angle)/sine;
    for(unsigned i=0;i<4;++i)a[i]=a[i]*left+b[i]*right;
    return normalized(a);
}
std::optional<Matrix4> quaternionMatrix(Quaternion value) noexcept {
    const auto q=normalized(value);if(!q)return {};
    const auto x=(*q)[0],y=(*q)[1],z=(*q)[2],w=(*q)[3];
    Matrix4 result;
    result.values={1-2*y*y-2*z*z,2*x*y+2*z*w,2*x*z-2*y*w,0,
                   2*x*y-2*z*w,1-2*x*x-2*z*z,2*y*z+2*x*w,0,
                   2*x*z+2*y*w,2*y*z-2*x*w,1-2*x*x-2*y*y,0,
                   0,0,0,1};
    return result;
}
ScalarCurve::ScalarCurve(std::vector<ScalarKey> keys) : keys_(std::move(keys)) {
    need(!keys_.empty() && keys_.size()<=maximumKeys,"Empty or oversized source curve");
    for (std::size_t i=0;i<keys_.size();++i) {
        const auto& key=keys_[i];
        need(finite(key.time)&&finite(key.value)&&!std::isnan(key.inSlope)&&!std::isnan(key.outSlope)&&key.weightedMode<=3&&
            finite(key.inWeight)&&finite(key.outWeight)&&key.inWeight>=0&&key.inWeight<=1&&key.outWeight>=0&&key.outWeight<=1&&
            (i==0||key.time>keys_[i-1].time),"Invalid or unsorted source curve");
    }
}
ScalarCurve ScalarCurve::fromJson(const Json& body) {
    if(body.contains("m_PreInfinity"))need(integer(body["m_PreInfinity"])==2,"Unsupported scalar pre-infinity mode");
    if(body.contains("m_PostInfinity"))need(integer(body["m_PostInfinity"])==2,"Unsupported scalar post-infinity mode");
    const auto& values=body["m_Curve"].array();need(values.size()<=maximumKeys,"Source key count exceeds limit");
    std::vector<ScalarKey> keys;keys.reserve(values.size());
    for(const auto& v:values) {
        const auto mode=integer(v["weightedMode"]);need(mode>=0&&mode<=3,"Invalid scalar weighted mode");
        keys.push_back({numeric(v["time"]),numeric(v["value"]),numeric(v["inSlope"]),numeric(v["outSlope"]),
                        static_cast<unsigned>(mode),numeric(v["inWeight"]),numeric(v["outWeight"])});
    }
    return ScalarCurve(std::move(keys));
}
std::optional<double> ScalarCurve::sample(double time) const noexcept {
    if (!finite(time)) return {};
    if (time<=keys_.front().time) return keys_.front().value;
    if (time>=keys_.back().time) return keys_.back().value;
    std::size_t lower=0,upper=keys_.size()-1;
    while (upper-lower>1) { const auto middle=(lower+upper)/2; if (keys_[middle].time<=time) lower=middle; else upper=middle; }
    const auto& a=keys_[lower]; const auto& b=keys_[upper];
    if (time==a.time || std::isinf(a.outSlope) || std::isinf(b.inSlope)) return a.value;
    const auto duration=b.time-a.time,u=(time-a.time)/duration;
    double result;
    if (!(a.weightedMode&2) && !(b.weightedMode&1)) {
        const auto u2=u*u,u3=u2*u;
        result=(2*u3-3*u2+1)*a.value+(u3-2*u2+u)*duration*a.outSlope+(-2*u3+3*u2)*b.value+(u3-u2)*duration*b.inSlope;
    } else {
        const auto outgoing=(a.weightedMode&2)?a.outWeight:1.0/3, incoming=(b.weightedMode&1)?b.inWeight:1.0/3;
        result=bezier(bezierTime(u,outgoing,incoming),a.value,a.value+duration*outgoing*a.outSlope,b.value-duration*incoming*b.inSlope,b.value);
    }
    return finite(result)?std::optional<double>(result):std::nullopt;
}
unsigned Value::count() const noexcept { return kind==ValueKind::scalar?1:kind==ValueKind::vector3?3:4; }
Value Value::scalar(double value) { return {ValueKind::scalar,{value,0,0,0}}; }
Value Value::vector(Vec3 value) { return {ValueKind::vector3,{value[0],value[1],value[2],0}}; }
Value Value::quaternion(Quaternion value) { return {ValueKind::quaternion,value}; }
Value Value::fromJson(const Json& value) {
    if (value.isNumber()||value.isString()) return scalar(numeric(value));
    need(value.isObject(),"Invalid source channel value");
    return value.contains("w")?quaternion(quat(value)):vector(vec3(value));
}
Curve::Curve(std::string group,std::string path,std::string attribute,std::vector<std::string> nodes,std::vector<Key> keys,
             std::optional<int> classID,int preInfinity,int postInfinity)
    : group_(std::move(group)),path_(std::move(path)),attribute_(std::move(attribute)),nodes_(std::move(nodes)),classID_(classID) {
    need(!keys.empty()&&keys.size()<=maximumKeys&&nodes_.size()<=maximumNodes&&preInfinity==2&&postInfinity==2,"Empty curve or unsupported infinity mode");
    need(group_=="m_FloatCurves"||group_=="m_PositionCurves"||group_=="m_ScaleCurves"||group_=="m_RotationCurves","Unsupported source curve group");
    kind_=group_=="m_FloatCurves"?ValueKind::scalar:group_=="m_RotationCurves"?ValueKind::quaternion:ValueKind::vector3;
    const unsigned count=kind_==ValueKind::scalar?1:kind_==ValueKind::quaternion?4:3;
    std::vector<std::vector<ScalarKey>> components(count);
    for (const auto& key:keys) {
        need(key.value.kind==kind_&&key.inSlope.kind==kind_&&key.outSlope.kind==kind_&&key.inWeight.kind==kind_&&key.outWeight.kind==kind_,"Mismatched source curve components");
        for (unsigned i=0;i<count;++i) components[i].push_back({key.time,key.value.components[i],key.inSlope.components[i],key.outSlope.components[i],key.weightedMode,key.inWeight.components[i],key.outWeight.components[i]});
    }
    for (auto& keysForComponent:components) channels_.emplace_back(std::move(keysForComponent));
}
Curve Curve::fromJson(const Json& value) {
    const auto& body=value["raw"]["curve"];
    const auto& rawKeys=body["m_Curve"].array(); need(rawKeys.size()<=maximumKeys,"Source key count exceeds limit");
    std::vector<Key> keys; keys.reserve(rawKeys.size());
    for (const auto& key:rawKeys) {
        const auto mode=integer(key["weightedMode"]); need(mode>=0&&mode<=3,"Invalid weighted mode");
        keys.push_back({numeric(key["time"]),Value::fromJson(key["value"]),Value::fromJson(key["inSlope"]),Value::fromJson(key["outSlope"]),
            static_cast<unsigned>(mode),Value::fromJson(key["inWeight"]),Value::fromJson(key["outWeight"])});
    }
    const auto& rawNodes=value["node_matches"].array(); need(rawNodes.size()<=maximumNodes,"Source binding count exceeds limit");
    std::vector<std::string> nodes; for (const auto& id:rawNodes) nodes.push_back(identity(id));
    std::optional<int> classID;
    if (!value["class_id"].isNull()) classID=integer(value["class_id"]);
    else if (!value["raw"]["classID"].isNull()) classID=integer(value["raw"]["classID"]);
    return Curve(value["group"].string(),value["path"].string(),value["attribute"].string(),std::move(nodes),std::move(keys),
                 classID,integer(body["m_PreInfinity"]),integer(body["m_PostInfinity"]));
}
std::optional<Value> Curve::sample(double time) const noexcept {
    Value value{kind_,{}};
    for (unsigned i=0;i<channels_.size();++i) { const auto v=channels_[i].sample(time); if (!v) return {}; value.components[i]=*v; }
    if (kind_==ValueKind::quaternion) { auto q=normalized(value.components); if (!q) return {}; value.components=*q; }
    return value;
}
Clip Clip::fromJson(const Json& value) {
    Clip result; result.binding=value["binding"].string(); result.id=identity(value["id"]); result.name=value["name"].string();
    result.sampleRate=numeric(value["sample_rate"]); result.lastKeyTime=numeric(value["last_key_time"]); result.wrapMode=integer(value["wrap_mode"]);
    need(finite(result.sampleRate)&&result.sampleRate>0&&finite(result.lastKeyTime)&&result.lastKeyTime>=0,"Invalid source clip timing");
    const auto& curves=value["curves"].array(); need(curves.size()<=maximumCurves,"Source curve count exceeds limit");
    for (const auto& curve:curves) result.curves.push_back(Curve::fromJson(curve));
    return result;
}
std::optional<double> Clip::localTime(double time) const noexcept {
    if (!finite(time)||!finite(lastKeyTime)||lastKeyTime<0) return {};
    if (wrapMode==2&&lastKeyTime>0) { const auto r=std::fmod(time,lastKeyTime); return r<0?r+lastKeyTime:r; }
    return std::clamp(time,0.0,lastKeyTime);
}
Library::Library(std::vector<Clip> clips) : clips_(std::move(clips)) {
    need(clips_.size()<=4096,"Source clip count exceeds limit");
    std::size_t curves{},keys{};
    for (std::size_t i=0;i<clips_.size();++i) {
        need(!clips_[i].id.empty()&&indices_.emplace(clips_[i].id,i).second,"Duplicate or empty source clip identity");
        curves+=clips_[i].curves.size(); need(curves<=maximumCurves,"Source curve count exceeds limit");
        for (const auto& curve:clips_[i].curves) { keys+=curve.channels()[0].keys().size(); need(keys<=maximumKeys,"Source key count exceeds limit"); }
    }
}
Library Library::fromJson(const Json& value) {
    const auto& source=value["clips"].array(); need(source.size()<=4096,"Source clip count exceeds limit");
    std::vector<Clip> clips; clips.reserve(source.size());
    for (const auto& clip:source) clips.push_back(Clip::fromJson(clip));
    return Library(std::move(clips));
}
const Clip* Library::clip(std::string_view id) const noexcept { auto i=indices_.find(id);return i==indices_.end()?nullptr:&clips_[i->second]; }
const Clip& Library::uniqueBinding(std::string_view binding) const {
    const Clip* result=nullptr;
    for (const auto& clip:clips_) if (clip.binding==binding) { need(!result,"Duplicate Watch binding"); result=&clip; }
    need(result,"Missing Watch binding"); return *result;
}
SceneDefinition::SceneDefinition(std::string root,std::vector<Node> nodes) : root_(std::move(root)),nodes_(std::move(nodes)) {
    need(!nodes_.empty()&&nodes_.size()<=maximumNodes,"Invalid source node count");
    for (std::size_t i=0;i<nodes_.size();++i) {
        const auto& n=nodes_[i]; need(!n.id.empty()&&indices_.emplace(n.id,i).second,"Duplicate source node");
        need(allFinite(n.position)&&allFinite(n.scale)&&normalized(n.rotation).has_value(),"Invalid source node transform");
        if (n.rect) need(allFinite(n.rect->anchorMin)&&allFinite(n.rect->anchorMax)&&allFinite(n.rect->anchoredPosition)&&allFinite(n.rect->sizeDelta)&&allFinite(n.rect->pivot),"Invalid source rect transform");
    }
    const auto* rootNode=node(root_); need(rootNode&&!rootNode->parent,"Missing or non-root source root");
    for (const auto& n:nodes_) {
        if (n.parent) { const auto* p=node(*n.parent); need(p&&std::find(p->children.begin(),p->children.end(),n.id)!=p->children.end(),"Missing source parent edge"); }
        std::set<std::string,std::less<>> unique;
        for (const auto& id:n.children) { const auto* child=node(id); need(unique.insert(id).second&&child&&child->parent==n.id,"Missing or duplicate source child edge"); }
    }
    std::vector<const Node*> pending{rootNode}; std::set<std::string,std::less<>> visited;
    while (!pending.empty()) { const auto* n=pending.back(); pending.pop_back(); need(visited.insert(n->id).second,"Cyclic source hierarchy"); for (const auto& id:n->children) pending.push_back(node(id)); }
    need(visited.size()==nodes_.size(),"Disconnected source hierarchy");
}
SceneDefinition SceneDefinition::fromJson(const Json& value) {
    const auto& source=value["nodes"].array(); need(source.size()<=maximumNodes,"Source node count exceeds limit");
    std::vector<Node> nodes; nodes.reserve(source.size());
    for (const auto& item:source) {
        Node n; n.id=identity(item["id"]); n.name=item["name"].string();n.path=item["path"].string();
        if (!item["parent_id"].isNull()) n.parent=identity(item["parent_id"]);
        for (const auto& id:item["child_ids"].array()) n.children.push_back(identity(id));
        n.active=item["game_object"]["data"]["m_IsActive"].boolean();
        const auto& t=item["transform"]; const auto& raw=t["raw"];
        n.position=vec3(raw["m_LocalPosition"]);n.scale=vec3(raw["m_LocalScale"]);n.rotation=quat(raw["m_LocalRotation"]);
        const auto type=t["type"].string(); need(type=="Transform"||type=="RectTransform","Unknown source transform kind");
        if(type=="RectTransform") n.rect=RectTransform{vec2(raw["m_AnchorMin"]),vec2(raw["m_AnchorMax"]),vec2(raw["m_AnchoredPosition"]),vec2(raw["m_SizeDelta"]),vec2(raw["m_Pivot"])};
        nodes.push_back(std::move(n));
    }
    return SceneDefinition(identity(value["root_node_id"]),std::move(nodes));
}
const Node* SceneDefinition::node(std::string_view id) const noexcept {const auto i=indices_.find(id);return i==indices_.end()?nullptr:&nodes_[i->second];}
double Pose::value(std::string_view attribute,std::string_view node,double fallback) const {
    const auto n=properties.find(node);if(n==properties.end())return fallback;const auto p=n->second.find(attribute);return p==n->second.end()?fallback:p->second;
}
void applyClip(const Clip& clip,double time,Pose& pose,const SceneDefinition& scene,const std::set<std::string,std::less<>>* membership) {
    const auto local=clip.localTime(time);if(!local)return;
    for(const auto& curve:clip.curves) {
        const auto& attribute=curve.attribute();
        if(curve.group()=="m_FloatCurves"&&curve.classID()==224&&!rectAttribute(attribute)){pose.unregisteredBindings.insert("224:"+attribute);continue;}
        if(curve.nodeIDs().empty()){pose.unboundPaths.insert(curve.path());continue;}
        const auto value=curve.sample(*local);if(!value)continue;
        for(const auto& id:curve.nodeIDs()) {
            const auto* node=scene.node(id);
            if(!node||(membership&&!membership->contains(id))){pose.unboundPaths.insert(curve.path());continue;}
            auto& t=pose.transforms[id];const auto& c=value->components;const auto a=axis(attribute);
            if(value->kind==ValueKind::vector3) {
                if(curve.group()=="m_PositionCurves")t.localPosition=Vec3{c[0],c[1],c[2]};
                if(curve.group()=="m_ScaleCurves")t.localScale=Vec3{c[0],c[1],c[2]};
            } else if(value->kind==ValueKind::quaternion)t.localRotation=c;
            else if(attribute=="m_IsActive")t.active=c[0]>=.5;
            else if(attribute.starts_with("m_LocalPosition.")&&rectAttribute(attribute)) {
                if(curve.classID()==224&&node->rect&&a<2)pose.properties[id][attribute]=c[0];else t.positionComponents[a]=c[0];
            } else if(attribute.starts_with("m_LocalScale.")&&rectAttribute(attribute)) {auto v=t.localScale.value_or(node->scale);v[a]=c[0];t.localScale=v;}
            else if(attribute=="m_AnchoredPosition.x"||attribute=="m_AnchoredPosition.y") {
                auto v=t.anchoredPosition3D.value_or(Vec3{node->rect?node->rect->anchoredPosition[0]:node->position[0],node->rect?node->rect->anchoredPosition[1]:node->position[1],node->position[2]});
                v[a]=c[0];t.anchoredPosition3D=v;
            } else if(attribute=="m_AnchorMin.x"||attribute=="m_AnchorMin.y") {auto v=t.anchorMin.value_or(node->rect?node->rect->anchorMin:Vec2{});v[a]=c[0];t.anchorMin=v;}
            else if(attribute=="m_AnchorMax.x"||attribute=="m_AnchorMax.y") {auto v=t.anchorMax.value_or(node->rect?node->rect->anchorMax:Vec2{});v[a]=c[0];t.anchorMax=v;}
            else if(attribute=="m_SizeDelta.x"||attribute=="m_SizeDelta.y") {auto v=t.sizeDelta.value_or(node->rect?node->rect->sizeDelta:Vec2{});v[a]=c[0];t.sizeDelta=v;}
            else if(attribute=="m_Pivot.x"||attribute=="m_Pivot.y") {auto v=t.pivot.value_or(node->rect?node->rect->pivot:Vec2{.5,.5});v[a]=c[0];t.pivot=v;}
            else pose.properties[id][attribute]=c[0];
        }
    }
}
WatchAnimation::WatchAnimation(const SceneDefinition& scene,const Library& library) : scene_(&scene),
    entrance_(&library.uniqueBinding("_animationIn")),ambient_(&library.uniqueBinding("_animationLoop")),exit_(&library.uniqueBinding("_animationOut")) {}
Pose WatchAnimation::pose(double entranceTime,std::optional<double> ambientTime,std::optional<double> exitTime,Vec2 canvas,const Overrides& overrides) const {
    need(allFinite(canvas)&&canvas[0]>0&&canvas[1]>0,"Invalid Watch canvas resolution");validateOverrides(overrides);
    Pose result;result.transforms=overrides;auto& root=result.transforms[scene_->rootID()];
    root.localScale=Vec3{1,1,1};root.anchoredPosition3D=Vec3{};root.anchorMin=Vec2{};root.anchorMax=Vec2{1,1};root.pivot=Vec2{.5,.5};root.sizeDelta=canvas;
    applyClip(*entrance_,entranceTime,result,*scene_);
    if(ambientTime)applyClip(*ambient_,*ambientTime,result,*scene_);
    if(exitTime)applyClip(*exit_,*exitTime,result,*scene_);
    return result;
}
Pose WatchAnimation::pose(const VisibilitySample& sample,Vec2 canvas,const Overrides& overrides,const DesktopAmbientMotion* desktopAmbient) const {
    need(sample.phase!=VisibilityPhase::concealed,"Concealed lifecycle has no renderable source pose");
    auto result=pose(sample.entranceTime,std::nullopt,std::nullopt,canvas,overrides);
    if(sample.ambientTime) {if(desktopAmbient)desktopAmbient->apply(*sample.ambientTime,result);else applyClip(*ambient_,*sample.ambientTime,result,*scene_);}
    if(sample.exitTime)applyClip(*exit_,*sample.exitTime,result,*scene_);
    return result;
}
DesktopAmbientMotion::DesktopAmbientMotion(const WatchAnimation& animation,std::uint64_t seed) : duration_(animation.ambient().lastKeyTime) {
    auto random=[state=seed]() mutable {state+=0x9e3779b97f4a7c15ull;auto v=state;v=(v^(v>>30))*0xbf58476d1ce4e5b9ull;v=(v^(v>>27))*0x94d049bb133111ebull;return static_cast<double>((v^(v>>31))>>11)/9007199254740992.0;};
    auto rate=[&](double lower,double upper){const auto magnitude=lower+(upper-lower)*random();return magnitude*(random()<.5?-1:1);};
    for(const auto& curve:animation.ambient().curves) if(curve.group()=="m_RotationCurves") for(const auto& id:curve.nodeIDs()) if(const auto* n=animation.scene().node(id))
        channels_.push_back({id,&curve,n->rotation,curve.path().find("/MiddleDecoNode/")!=std::string::npos?rate(.6,1.25):1});
    for(const auto& n:animation.scene().nodes()) if(n.name=="triangle_fx1"&&n.path.find("/MiddleDecoNode/TriagleNode/")!=std::string::npos)
        channels_.push_back({n.id,nullptr,n.rotation,rate(10,24)*std::numbers::pi/180});
}
void DesktopAmbientMotion::apply(double elapsed,Pose& pose) const {
    if(!finite(elapsed)||duration_<=0)return;
    for(const auto& channel:channels_) {
        Quaternion rotation;
        if(channel.curve) {
            const auto raw=std::max(0.0,elapsed)*channel.rate,wrapped=std::fmod(raw,duration_);
            const auto value=channel.curve->sample(wrapped<0?wrapped+duration_:wrapped);if(!value||value->kind!=ValueKind::quaternion)continue;
            rotation=value->components;
        } else {
            const auto angle=std::fmod(std::max(0.0,elapsed)*channel.rate,2*std::numbers::pi),s=std::sin(angle/2),c=std::cos(angle/2);
            const auto& a=channel.base;
            rotation={a[0]*c+a[1]*s,a[1]*c-a[0]*s,a[2]*c+a[3]*s,a[3]*c-a[2]*s};
        }
        pose.transforms[channel.node].localRotation=rotation;
    }
}
std::optional<double> buttonClipTime(double time,double started,double speed,double offset,double length,bool endpoint) noexcept {
    if(!finite(time)||!finite(started)||!finite(speed)||!finite(offset)||!finite(length)||length<0)return {};
    return endpoint?length:std::clamp(std::max(0.0,time-started)*speed+offset*length,0.0,length);
}
Value blendButtonValue(const Value& from,const Value& to,double fraction) {
    need(finite(fraction)&&fraction>=0&&fraction<=1,"Invalid button blend fraction");
    if(from.kind!=to.kind)return fraction<1?from:to;
    auto result=from;for(unsigned i=0;i<from.count();++i)result.components[i]=from.components[i]+(to.components[i]-from.components[i])*fraction;
    return result; // Source button transition does not normalize this quaternion.
}
} // namespace endfield::core::source
