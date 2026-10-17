#include "core/source_button_motion.hpp"
#include "core/shell_packet.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace endfield::core;
using namespace endfield::core::source;
namespace {
unsigned checks{};
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
void near(double a,double b,const char*m,double tolerance=1e-11){check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance*std::max(1.,std::abs(b)),m);}
template<class F>void rejects(F&&f,const char*m){bool failed{};try{f();}catch(const std::invalid_argument&){failed=true;}check(failed,m);}
SceneDefinition scene(){Node root;root.id="root";root.children={"target","hover"};Node target;target.id="target";target.parent="root";target.position={8,9,3};target.rect=RectTransform{};Node hover;hover.id="hover";hover.parent="root";return SceneDefinition("root",{root,target,hover});}
Key scalarKey(double time,double value,double slope=0){return {time,Value::scalar(value),Value::scalar(slope),Value::scalar(slope),0,Value::scalar(1./3),Value::scalar(1./3)};}
Curve curve(std::string attr,std::vector<Key>keys,std::vector<std::string>nodes={"target"}){return Curve("m_FloatCurves","fixture/"+attr,attr,std::move(nodes),std::move(keys),224);}
Clip makeClip(std::string binding,std::string id,double from,double to,double length=1,int wrap=0){return {binding,id,id,60,length,wrap,{curve("m_Alpha",{scalarKey(0,from,(to-from)/length),scalarKey(length,to,(to-from)/length)})}};}
Library library(){auto normal=makeClip("button","normal",0,0);normal.curves.push_back(curve("m_LocalPosition.x",{scalarKey(0,7)}));normal.curves.push_back(curve("m_SizeDelta.x",{scalarKey(0,77)}));normal.curves.push_back(curve("missing",{scalarKey(0,1)},{}));normal.curves.push_back(curve("unknown-node",{scalarKey(0,1)},{"missing"}));return Library({std::move(normal),makeClip("button","highlighted",0,10,1,2),makeClip("button","pressed",20,20),makeClip("button","disabled",30,30)});}
Json transition(std::string destination,std::string bound,double duration,bool fixed,bool repeat=false,std::optional<int>source={}){
    Json j(Json::Object{});j["destination_name"]=destination;j["destination_bound_clip_id"]=bound;j["destination_source_clip_id"]=bound+"-source";j["duration"]=duration;j["has_fixed_duration"]=fixed;j["can_transition_to_self"]=repeat;j["state_machine_index"]=0;j["source_state_index"]=source?Json(*source):Json();return j;
}
Json metadata(){
    Json::Array states;for(unsigned i=0;i<4;++i){Json s(Json::Object{});const std::string names[]{"normal","highlighted","pressed","disabled"};s["index"]=int(i);s["name"]=std::string(buttonStateName(static_cast<ButtonState>(i)));s["source_clip_id"]=names[i]+"-source";s["speed"]=1.;s["cycle_offset"]=0.;states.push_back(s);}
    Json machine(Json::Object{});machine["index"]=0;machine["states"]=states;Json controller(Json::Object{});controller["id"]="controller";controller["state_machines"]=Json::Array{machine};
    Json instance(Json::Object{});instance["root_node_id"]="root";instance["controller_id"]="controller";instance["source_interactable"]=true;instance["hover_enable_node_id"]="hover";
    instance["transitions"]=Json::Array{transition("Normal","normal",.4,true),transition("Highlighted","highlighted",0,true),transition("Pressed","pressed",.2,false),transition("Disabled","disabled",0,true,true),transition("Highlighted","highlighted",.1,true,true,2)};
    Json result(Json::Object{});result["controllers"]=Json::Array{controller};result["instances"]=Json::Array{instance};return result;
}
double value(ButtonAnimation&b,double t){Pose p;b.apply(p,t);return p.value("m_Alpha","target",-999);}
void buttons(){
    auto s=scene();auto l=library();const auto data=metadata();const auto bindings=AnimatorBinding::fromTransitions(data);ButtonAnimation b(s,l,bindings,data);
    check(b.instanceIDs().size()==1&&b.state("root")==ButtonState::normal&&!b.state("missing"),"Bound source instance identity and initial normal state");
    Pose p;b.apply(p,0);check(p.transforms.at("target").positionComponents.at(0)==7,"Button controller XY channels remain graph setters before layout writers");
    check(!p.transforms.at("target").sizeDelta&&p.value("m_SizeDelta.x","target",0)==77,"Button property handler differs deliberately from wrapper rect-size handler");
    check(p.unboundPaths==std::set<std::string,std::less<>>{"fixture/missing"},"Empty matches are reported; absent nonempty node matches are skipped like source");
    check(p.transforms.at("hover").active==false,"Normal state disables independent hover transform");
    b.setHovered(true,"root",1);near(value(b,1.5),5,"Loop-flag highlighted samples finite linear clip time");
    check(b.requiresFrames(1.9)&&!b.requiresFrames(2),"Highlighted demand stops at actual endpoint");
    check(b.requiresFrames(1.6),"Future demand query does not advance monotonic sampler time");
    near(value(b,4),10,"Highlighted remains at endpoint after many loop periods");const auto gen=b.stateGeneration();b.setState(ButtonState::highlighted,"root",4.1);check(b.stateGeneration()==gen,"Disallowed repeated state neither restarts nor increments generation");
    b.setState(ButtonState::pressed,"root",5);near(value(b,5.1),15,"Normalized-duration transition uses previous clip length and speed");
    b.setState(ButtonState::normal,"root",5.1);near(value(b,5.1),15,"Interrupted blend starts from exact displayed snapshot");near(value(b,5.3),7.5,"Interrupted origin remains frozen while destination evolves");
    b.setHovered(true,"root",5.4);b.setState(ButtonState::pressed,"root",5.5);b.setHovered(false,"root",5.55);p={};b.apply(p,5.55);check(b.state("root")==ButtonState::pressed&&p.transforms.at("hover").active==false,"Pointer leave clears hover without releasing pressed state");
    // The specific Pressed->Highlighted transition takes precedence over the
    // earlier zero-duration AnyState rule, so the origin is still visible.
    const auto before=value(b,5.6);b.setState(ButtonState::highlighted,"root",5.6);near(value(b,5.6),before,"Specific source-state transition wins over AnyState");
    b.setState(ButtonState::disabled,"root",6);const auto disabledGen=b.stateGeneration();b.setState(ButtonState::disabled,"root",6.1);check(b.stateGeneration()==disabledGen+1,"Authored repeatable Disabled transition is retained");
    b.setHovered(true,"root",6.2);p={};b.apply(p,6.2);check(p.transforms.at("hover").active==false,"Disabled state cannot display hover");
    b.setState(ButtonState::highlighted,"root",7,true);near(value(b,7),10,"Reduced motion seeks exact state endpoint");check(!b.requiresFrames(7),"Reduced motion has no finite demand");
    const auto samples=b.sampledCurveCount();for(unsigned i=0;i<500;++i){check(!b.requiresFrames(8+i),"Settled demand remains false");p={};b.apply(p,8+i);}check(b.sampledCurveCount()==samples,"Steady endpoint cache never resamples curves");check(b.cachedStateCount()<=4,"State cache has four bounded slots per instance");
    b.reset(0);check(b.state("root")==ButtonState::normal&&b.requiresFrames(0),"Reset permits a new opening clock and restores source normal state");
    near(value(b,0),0,"Reset clears interrupted origins");b.setState(ButtonState::highlighted,"root",1);near(value(b,1.6),6,"New generation restarts finite clip");near(value(b,1.2),6,"Backward sampler calls hold monotonic source time");
    const auto finiteGen=b.stateGeneration();b.setState(ButtonState::pressed,"root",std::numeric_limits<double>::quiet_NaN());check(b.stateGeneration()==finiteGen,"Nonfinite state event ignored");
    rejects([&]{auto bad=bindings;bad[0].boundClipIDs[0]="missing";ButtonAnimation invalid(s,l,bad,data);},"Missing bound clip rejected");
    rejects([&]{auto bad=bindings;bad.push_back(bad.front());ButtonAnimation invalid(s,l,bad,data);},"Duplicate instance rejected before overwriting active state");
}
Json clipJSON(std::string binding,std::string id,std::string attribute,double from,double to,int wrap=0){
    auto key=[](double t,double v,double slope){Json j(Json::Object{});j["time"]=t;j["value"]=v;j["inSlope"]=slope;j["outSlope"]=slope;j["weightedMode"]=0;j["inWeight"]=1./3;j["outWeight"]=1./3;return j;};
    Json body(Json::Object{});body["m_Curve"]=Json::Array{key(0,from,to-from),key(1,to,to-from)};body["m_PreInfinity"]=2;body["m_PostInfinity"]=2;
    Json raw(Json::Object{});raw["curve"]=body;Json curve(Json::Object{});curve["group"]="m_FloatCurves";curve["path"]="fixture";curve["attribute"]=attribute;curve["node_matches"]=Json::Array{"target"};curve["raw"]=raw;
    Json c(Json::Object{});c["binding"]=binding;c["id"]=id;c["name"]=id;c["sample_rate"]=60;c["last_key_time"]=1;c["wrap_mode"]=wrap;c["curves"]=Json::Array{curve};return c;
}
Json domainData(){
    Json options(Json::Object{});options["animEase"]=6;Json wrapper(Json::Object{});wrapper["_options"]=options;wrapper["autoPlay"]=false;
    Json level(Json::Object{});level["domain"]="FixtureDomain";level["level_id"]="level1";
    Json instance(Json::Object{});instance["root_node_id"]="root";instance["is_level_model_root"]=true;instance["levels"]=Json::Array{level};instance["wrapper_data"]=wrapper;instance["animation_data"]=Json::Object{};
    instance["bound_clips"]=Json::Array{clipJSON("_animationIn","in","selection",0,1),clipJSON("_animationOut","out","selection",1,0),clipJSON("SourceAnimation","hover","hover",0,4)};
    Json loop(Json::Object{});loop["root_node_id"]="hover";loop["is_level_model_root"]=false;options["animEase"]=1;wrapper["_options"]=options;wrapper["autoPlay"]=true;loop["wrapper_data"]=wrapper;loop["animation_data"]=Json::Object{};loop["levels"]=Json::Array{};loop["bound_clips"]=Json::Array{clipJSON("_animationLoop","loop","ambient",0,8,2)};
    Json data(Json::Object{});data["schema_version"]=1;data["instances"]=Json::Array{instance,loop};return data;
}
void domain(){
    auto s=scene();const auto data=domainData();DomainAnimation animation(data,s,"fixturedomain",{"level1"});check(animation.bindingCount()==1&&animation.autoLoopCount()==1,"Domain joins original root and case-insensitive level domain");
    DomainState state;auto p=animation.pose(state);near(p.value("selection","target",-1),0,"Unset account level stays deselected endpoint");
    state.currentLevelID="level1";p=animation.pose(state);near(p.value("selection","target",-1),1,"Nil selection clock seeks selected endpoint");
    state.selectionElapsed=.5;state.hoverClipTimes["level1"]=.25;state.ambientTime=1.25;p=animation.pose(state);near(p.value("selection","target",-1),.75,"Domain OutQuad is applied exactly once");near(p.value("hover","target",-1),1,"Hover uses explicit clip time without wrapper easing");near(p.value("ambient","target",-1),2,"Linear auto loop retains source wrap duration");
    state.currentLevelID.reset();p=animation.pose(state);near(p.value("selection","target",-1),0,"Deselection seeks endpoint independent of supplied selection clock");
    state.currentLevelID="unknown";rejects([&]{animation.pose(state);},"Unloaded level rejected");state.currentLevelID.reset();state.selectionElapsed=-1;rejects([&]{animation.pose(state);},"Negative selection time rejected");state.selectionElapsed.reset();state.hoverClipTimes["missing"]=0;rejects([&]{animation.pose(state);},"Unloaded hover level rejected");
    Overrides invalid;invalid["target"].localScale=Vec3{std::numeric_limits<double>::infinity(),1,1};rejects([&]{animation.pose(DomainState{},invalid);},"Domain validates original scene before applying clips");
    rejects([&]{DomainAnimation missing(data,s,"other",{"level1"});},"Unresolved original domain wrapper rejected");
    auto rows=data["instances"].array();rows.push_back(rows.front());auto repeated=data;repeated["instances"]=rows;rejects([&]{DomainAnimation duplicate(repeated,s,"fixturedomain",{"level1"});},"Repeated source root rejected");
}
Json loadJSON(const std::filesystem::path&p){std::ifstream in(p,std::ios::binary);check(bool(in),"Fixture opens");std::string bytes((std::istreambuf_iterator<char>(in)),{});return Json::parse(bytes,64*1024*1024);}
template<std::size_t N>void compareVector(const std::optional<std::array<double,N>>& actual,const Json& expected){check(actual.has_value()==!expected.isNull(),"Oracle optional vector presence");if(actual){check(expected.array().size()==N,"Oracle vector width");for(unsigned i=0;i<N;++i)near((*actual)[i],expected.array()[i].number(),"Original Swift pose component");}}
void comparePose(const Pose& pose,const Json& expected){
    const auto& transforms=expected["transforms"].object();check(pose.transforms.size()==transforms.size(),"Oracle transform binding count");
    for(const auto& [id,t]:pose.transforms){const auto& e=transforms.at(id);
        compareVector(t.localPosition,e["position"]);compareVector(t.localScale,e["scale"]);compareVector(t.localRotation,e["rotation"]);
        compareVector(t.anchoredPosition3D,e["anchored"]);compareVector(t.anchorMin,e["anchorMin"]);compareVector(t.anchorMax,e["anchorMax"]);compareVector(t.sizeDelta,e["sizeDelta"]);compareVector(t.pivot,e["pivot"]);
        check(t.active.has_value()==!e["active"].isNull(),"Oracle active optional");if(t.active)check(*t.active==e["active"].boolean(),"Oracle active value");
        check(t.positionComponents.size()==e["components"].object().size(),"Oracle position component count");for(const auto& [axis,value]:t.positionComponents)near(value,e["components"][std::to_string(axis)].number(),"Oracle position component");
    }
    check(pose.properties.size()==expected["properties"].object().size(),"Oracle property node count");
    for(const auto& [id,props]:pose.properties){const auto& e=expected["properties"][id].object();check(props.size()==e.size(),"Oracle property count");for(const auto& [name,value]:props)near(value,e.at(name).number(),"Oracle scalar property");}
    auto strings=[](const Json& array){std::set<std::string,std::less<>> s;for(const auto& v:array.array())s.insert(v.string());return s;};
    check(pose.unboundPaths==strings(expected["unbound"]),"Oracle unbound source paths");check(pose.unregisteredBindings==strings(expected["unregistered"]),"Oracle unregistered class bindings");
}
void domainOracle(const std::filesystem::path&path){
    const auto oracle=loadJSON(path);const auto scene=SceneDefinition::fromJson(oracle["scene"]);
    DomainAnimation animation(oracle["source"],scene,"fixturedomain",{"level1"});
    for(const auto&row:oracle["rows"].array()){
        const auto&input=row["state"];DomainState state;
        if(input["level"].isString())state.currentLevelID=input["level"].string();
        if(input["elapsed"].isNumber())state.selectionElapsed=input["elapsed"].number();
        if(input["hover"].isNumber())state.hoverClipTimes["level1"]=input["hover"].number();
        if(input["ambient"].isNumber())state.ambientTime=input["ambient"].number();
        comparePose(animation.pose(state),row["pose"]);
    }
}
void actualPacket(const std::filesystem::path&root,const std::optional<std::filesystem::path>&oraclePath){
    packet::Package package(std::filesystem::absolute(root));const auto data=package.loadAnimation();
    const auto scene=SceneDefinition::fromJson(data["scene"]);const auto library=Library::fromJson(data["library"]);const auto bindings=AnimatorBinding::fromTransitions(data["controllerTransitions"]);
    ButtonAnimation b(scene,library,bindings,data["controllerTransitions"]);check(bindings.size()==32,"Actual source has 32 bound Animator instances");
    if(oraclePath){const auto oracle=loadJSON(*oraclePath);check(bindings==AnimatorBinding::fromJson(oracle["animators"]),"Transition reconstruction equals authoritative animator order and clip joins");
        for(const auto&row:oracle["rows"].array()){
            const auto&event=row["event"];const auto action=event["action"].string();const auto t=event["time"].number();const bool reduce=event["reduce"].boolean();const auto id=event["id"].string();
            if(action=="reset")b.reset(t,reduce);
            else if(action=="state")b.setState(*buttonState(event["state"].string()),id,t,reduce);
            else if(action=="hover")b.setHovered(event["hovered"].boolean(),id,t,reduce);
            if(action=="demand")check(b.requiresFrames(t)==row["demand"].boolean(),"Original Swift demand query parity");
            else{Pose pose;b.apply(pose,t,reduce);comparePose(pose,row["pose"]);}
            check(b.stateGeneration()==static_cast<std::uint64_t>(row["generation"].integer()),"Original Swift state/hover generation parity");
            for(std::size_t i=0;i<bindings.size();++i)check(buttonStateName(*b.state(bindings[i].rootID))==row["states"].array()[i].string(),"Original Swift state transition parity");
        }
    }else{
        for(const auto&binding:bindings){b.reset(0,true);b.setHovered(true,binding.rootID,.1);Pose p;b.apply(p,.15);b.setState(ButtonState::pressed,binding.rootID,.16);b.apply(p,.18);b.setHovered(false,binding.rootID,.19);b.apply(p,.2);check(!p.transforms.empty(),"Actual instance updates source-bound channels");}
    }
    check(b.cachedStateCount()<=bindings.size()*4,"Actual state cache remains bounded by authored instance count");
    std::cout<<bindings.size()<<" source button instances; Swift oracle "<<(oraclePath?"compared":"not supplied")<<'\n';
}
}
int main(int argc,char**argv){try{buttons();domain();if(argc>1)actualPacket(argv[1],argc>2?std::optional<std::filesystem::path>(argv[2]):std::nullopt);if(argc>3)domainOracle(argv[3]);std::cout<<checks<<" source button/domain checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
