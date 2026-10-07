#include "core/source_animation.hpp"
#include "core/shell_packet.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
using namespace endfield::core;
using namespace endfield::core::source;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void near(double value,double expected,const char* message,double tolerance=1e-11){++checks;if(!std::isfinite(value)||std::abs(value-expected)>tolerance*std::max(1.0,std::abs(expected)))throw std::runtime_error(std::string(message)+": "+std::to_string(value)+" != "+std::to_string(expected));}
template<class F>void rejects(F&& f,const char* message){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,message);}
Key key(double t,Value value,Value slope,Value weight){return {t,value,slope,slope,0,weight,weight};}
Curve scalar(std::string attribute,double value,std::vector<std::string> nodes={"rect"},std::optional<int> classID={}) {
    return Curve("m_FloatCurves","/fixture/"+attribute,attribute,std::move(nodes),
        {key(0,Value::scalar(value),Value::scalar(0),Value::scalar(1.0/3))},classID);
}
Clip clip(std::string binding,std::vector<Curve> curves,double length=1,int wrap=0){return {binding,binding,binding,60,length,wrap,std::move(curves)};}
SceneDefinition scene(){
    Node root;root.id="root";root.name="root";root.path="Root";root.rect=RectTransform{};root.scale={0,0,0};root.children={"rect","transform","triangle"};
    Node rect;rect.id="rect";rect.parent="root";rect.name="rect";rect.path="Root/MiddleDecoNode/Ring";
    rect.position={11,12,13};rect.scale={2,3,4};rect.rect=RectTransform{{.1,.2},{.8,.9},{21,22},{90,100},{.3,.4}};
    Node transform;transform.id="transform";transform.parent="root";transform.position={31,32,33};
    Node triangle;triangle.id="triangle";triangle.parent="root";triangle.name="triangle_fx1";triangle.path="Root/MiddleDecoNode/TriagleNode/triangle_fx1";
    return SceneDefinition("root",{root,rect,transform,triangle});
}
void interpolation(){
    ScalarCurve smooth({{0,0},{2,1}});
    near(*smooth.sample(.5),.15625,"Hermite uses segment duration");near(*smooth.sample(1),.5,"Hermite midpoint");
    near(*smooth.sample(-5),0,"Pre-infinity clamp");near(*smooth.sample(9),1,"Post-infinity clamp");
    check(!smooth.sample(std::numeric_limits<double>::infinity()),"Nonfinite sample time has no value");
    ScalarCurve tangents({{0,1,0,4},{2,3,-2,0}});near(*tangents.sample(1),3.5,"Outgoing/incoming slopes use seconds, not normalized time");
    const auto inf=std::numeric_limits<double>::infinity();
    ScalarCurve step({{0,2,0,inf},{1,9,-inf,0},{2,13}});
    near(*step.sample(.999),2,"Infinite tangent holds outgoing key");near(*step.sample(1),9,"Step changes at the exact key");
    ScalarCurve weighted({{0,0,0,2,2,1.0/3,.1},{1,1,-1,0,1,.6,1.0/3}});
    // At Bezier parameter .5, X=.3125 and Y=.8. Sampling .5 seconds
    // directly as the parameter would miss this independent analytic identity.
    near(*weighted.sample(.3125),.8,"Weighted tangents solve Bezier time");
    ScalarCurve vertical({{0,0,0,0,2,1.0/3,1},{1,1,0,0,1,1,1.0/3}});
    near(*vertical.sample(.5),.5,"Crossed time handles with zero derivative remain bracketed");
    ScalarCurve outgoing({{0,0,0,2,2,1.0/3,.1},{1,1,0,0}});
    near(*outgoing.sample(.4125),.575,"Unset incoming weighted bit uses one-third, not serialized unused weight");
    rejects([]{ScalarCurve c({});},"Empty keys rejected");
    rejects([]{ScalarCurve c({{1,0},{1,1}});},"Duplicate times rejected");
    rejects([]{ScalarCurve c({{1,0},{0,1}});},"Unsorted keys rejected");
    rejects([]{ScalarCurve c({{0,0,0,0,4}});},"Invalid weighted mask rejected");
    rejects([]{ScalarCurve c({{0,0,0,0,1,1.1}});},"Out-of-range handle rejected");
    rejects([]{ScalarCurve c({{0,0,std::numeric_limits<double>::quiet_NaN()}});},"NaN slope rejected");
    const auto body=Json::parse(R"({"m_PreInfinity":2,"m_PostInfinity":2,"m_Curve":[{"time":0,"value":1,"inSlope":0,"outSlope":"Infinity","weightedMode":0,"inWeight":0.3333333333333333,"outWeight":0.3333333333333333},{"time":1,"value":8,"inSlope":0,"outSlope":0,"weightedMode":0,"inWeight":0.3333333333333333,"outWeight":0.3333333333333333}]})");
    near(*ScalarCurve::fromJson(body).sample(.75),1,"Scalar parser preserves source Infinity sentinel");
    auto invalid=body;invalid["m_PreInfinity"]=0;rejects([&]{ScalarCurve::fromJson(invalid);},"Unsupported infinity mode is explicit");
    const Value zero=Value::quaternion({0,0,0,0}),oneThird=Value::quaternion({1.0/3,1.0/3,1.0/3,1.0/3});
    Curve rotation("m_RotationCurves","rotation","",{"rect"},
        {key(0,Value::quaternion({0,0,0,1}),zero,oneThird),key(1,Value::quaternion({0,0,1,0}),zero,oneThird)});
    auto q=rotation.sample(.5);check(q&&q->kind==ValueKind::quaternion,"Quaternion sample type");
    near(q->components[2],std::sqrt(.5),"Quaternion normalizes component curves");near(q->components[3],std::sqrt(.5),"Quaternion W normalization");
    Curve opposite("m_RotationCurves","rotation","",{"rect"},
        {key(0,Value::quaternion({0,0,0,1}),zero,oneThird),key(1,Value::quaternion({0,0,0,-1}),zero,oneThird)});
    check(!opposite.sample(.5),"Zero quaternion crossing is invalid; no invented slerp shortest arc");
    auto loop=clip("loop",{},13.683333396911621,2);
    near(*loop.localTime(loop.lastKeyTime),0,"Loop includes actual lastKeyTime duration");
    near(*loop.localTime(-.25),loop.lastKeyTime-.25,"Negative loop clocks wrap to tail");
    auto finite=clip("finite",{},.75);near(*finite.localTime(9),.75,"Finite clip clamps endpoint");
    near(VisibilityClock::clipTime(.375,.75),.5625,"Existing visibility clock provides original OutQuad");
}
void poses(){
    auto s=scene();Pose pose;
    auto c=clip("bind",{scalar("m_LocalPosition.x",99,{"rect"},224),scalar("m_LocalPosition.z",8,{"rect"},224),
        scalar("m_LocalScale.y",9),scalar("m_AnchoredPosition.x",44),scalar("m_SizeDelta.y",55),scalar("m_Pivot.x",.6),
        scalar("m_Alpha",.7),scalar("m_Color.a",.8),scalar("material._Alpha",.9),scalar("m_IsActive",.49),
        scalar("unsupported",2,{"rect"},224),scalar("m_Alpha",1,{}),scalar("m_Alpha",1,{"missing"})});
    applyClip(c,0,pose,s);
    const auto& t=pose.transforms.at("rect");
    check(!t.positionComponents.contains(0),"RectTransform inherited XY is not a graph setter");
    near(pose.value("m_LocalPosition.x","rect",0),99,"Inherited serialized XY remains a distinct property");
    near(t.positionComponents.at(2),8,"RectTransform Z stays a graph override");
    check(t.localScale==Vec3{2,9,4},"Scalar scale keeps other base components");
    check(t.anchoredPosition3D==Vec3{44,22,13},"Anchored position retains authored Y and Z");
    check(t.sizeDelta==Vec2{90,55}&&t.pivot==Vec2{.6,.4},"Rect size/pivot channel rules");
    check(t.active==false,"Active channel threshold is .5");
    near(pose.value("m_Alpha","rect",0),.7,"CanvasGroup alpha separate");
    near(pose.value("m_Color.a","rect",0),.8,"Graphic alpha separate");
    near(pose.value("material._Alpha","rect",0),.9,"Material alpha separate");
    check(pose.unregisteredBindings.contains("224:unsupported"),"Unknown RectTransform binding diagnosed");
    check(pose.unboundPaths.size()==1&&pose.unboundPaths.contains("/fixture/m_Alpha"),"Missing exact source identities diagnosed, never rebound by name");
    Pose filtered;std::set<std::string,std::less<>> membership{"transform"};applyClip(c,0,filtered,s,&membership);
    check(filtered.transforms.empty(),"Explicit resolved membership filters bindings");
    Library library({clip("_animationIn",{scalar("m_Alpha",.1)}),clip("_animationLoop",{scalar("m_Alpha",.2)},2,2),clip("_animationOut",{scalar("m_Alpha",.3)})});
    WatchAnimation watch(s,library);auto output=watch.pose(0,.5,.5,{1000,640});
    near(output.value("m_Alpha","rect",0),.3,"Application order is entrance, ambient, exit");
    const auto& root=output.transforms.at("root");
    check(root.localScale==Vec3{1,1,1}&&root.anchoredPosition3D==Vec3{0,0,0}&&root.sizeDelta==Vec2{1000,640},"Runtime initialization repairs serialized zero root scale");
    check(root.anchorMin==Vec2{0,0}&&root.anchorMax==Vec2{1,1}&&root.pivot==Vec2{.5,.5},"Runtime root anchors/pivot match Mac");
    rejects([&]{watch.pose(0,{},{},{0,640});},"Invalid canvas rejected");
    Overrides overrides;overrides["rect"].localScale=Vec3{2,std::numeric_limits<double>::infinity(),4};
    rejects([&]{watch.pose(0,{},{},{1000,640},overrides);},"Invalid caller override rejected");
    VisibilityClock clock(1,1);clock.open(0);auto sample=clock.sample(.5);check(sample.has_value(),"Existing lifecycle drives evaluator");
    output=watch.pose(*sample,{1000,640});near(output.value("m_Alpha","rect",0),.1,"Opening suppresses ambient until existing clock enables it");
    clock.close(1);sample=clock.sample(2);check(sample&&sample->phase==VisibilityPhase::concealed,"Closing lifecycle reports completion without a render frame");
    rejects([&]{watch.pose(*sample,{1000,640});},"A completion-only concealed sample cannot revive source geometry");
    auto malformed=s.nodes();std::vector<Node> nodes(malformed.begin(),malformed.end());nodes[1].parent="missing";
    rejects([&]{SceneDefinition invalid("root",nodes);},"Disconnected source edge rejected");
}
void quaternionHelpers(){
    check(!normalizedQuaternion({0,0,0,0}),"Zero quaternion rejected");
    const auto q=normalizedQuaternion({0,0,2,2});near((*q)[2],std::sqrt(.5),"Shared quaternion normalization");
    const auto square=multiplyQuaternion(*q,*q);near(square[2],1,"Hamilton product Z");near(square[3],0,"Hamilton product W");
    const auto matrix=quaternionMatrix(*q);near(matrix->values[0],0,"Column-major quaternion rotation X");near(matrix->values[1],1,"Column-major quaternion maps +X to +Y");
    const auto halfway=slerpQuaternion({0,0,0,1},{0,0,1,0},.5);near((*halfway)[2],std::sqrt(.5),"Camera shortest-arc SLERP");
    const auto same=slerpQuaternion({0,0,0,1},{0,0,0,-1},.5);near((*same)[3],1,"Camera antipodal representation does not collapse");
    near(*buttonClipTime(30,0,1,0,1,false),1,"Highlighted clip stops at endpoint even if source wrap flag loops");
    near(*buttonClipTime(.25,0,-1,1,1,false),.75,"Button negative speed/cycle offset");
    near(*buttonClipTime(0,0,-1,0,1,true),1,"Reduced motion seeks source last key regardless speed");
    auto mixed=blendButtonValue(Value::quaternion({0,0,0,1}),Value::quaternion({0,0,1,0}),.5);
    check(mixed.components==Quaternion{0,0,.5,.5},"Button transitions linearly mix components without normalization");
    check(blendButtonValue(Value::scalar(2),Value::vector({1,2,3}),.5)==Value::scalar(2),"Mismatched button channel retains origin until completion");
}
Json loadJSON(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);check(bool(input),"Oracle file opened");std::string bytes((std::istreambuf_iterator<char>(input)),{});return Json::parse(bytes,32*1024*1024);}
void compareValue(const Value& actual,const Json& expected){const auto value=Value::fromJson(expected);check(actual.kind==value.kind,"Oracle channel kind");for(unsigned i=0;i<actual.count();++i)near(actual.components[i],value.components[i],"Original Swift curve parity");}
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
void actualPacket(const std::filesystem::path& root,const std::optional<std::filesystem::path>& oraclePath){
    packet::Package package(std::filesystem::absolute(root));const auto data=package.loadAnimation();
    auto library=Library::fromJson(data["library"]);auto definition=SceneDefinition::fromJson(data["scene"]);WatchAnimation watch(definition,library);
    std::size_t curves{},keys{};
    for(const auto& c:library.clips())for(const auto& curve:c.curves){
        ++curves;keys+=curve.channels()[0].keys().size();
        for(const auto& channel:curve.channels())for(const auto& key:channel.keys())near(*channel.sample(key.time),key.value,"Actual source keys reproduce their authored values");
        for(double t:{0.0,c.lastKeyTime*.137,c.lastKeyTime*.5,c.lastKeyTime*.923,c.lastKeyTime})check(curve.sample(t).has_value(),"Actual curve finite samples are evaluable");
    }
    check(library.clips().size()==142&&curves==2637&&keys==12502,"Current Mac packet fixture inventory");
    check(watch.entrance().lastKeyTime==.75,"Actual source opening duration");
    near(watch.ambient().lastKeyTime,13.683333396911621,"Actual ambient last-frame hold");
    auto pose=watch.pose(watch.entrance().lastKeyTime,{},{},{1000,640});check(!pose.transforms.empty()&&!pose.unboundPaths.empty(),"Actual bound pose retains explicit missing game bindings");
    DesktopAmbientMotion first(watch,123),second(watch,123),different(watch,124);
    check(!first.channels().empty()&&first.channels().size()==second.channels().size(),"Actual seeded ambient channels exist");
    bool differs=false;for(std::size_t i=0;i<first.channels().size();++i){near(first.channels()[i].rate,second.channels()[i].rate,"Seed stable channel rate",0);differs|=first.channels()[i].rate!=different.channels()[i].rate;}
    check(differs,"Opening seed changes decoration rates/directions");Pose a,b;first.apply(123.4,a);second.apply(123.4,b);check(a==b,"Seeded ambient pose is deterministic");
    if(oraclePath){
        const auto oracle=loadJSON(*oraclePath);
        for(const auto& row:oracle["curveSamples"].array()){
            const auto& curve=library.clips()[static_cast<std::size_t>(row["clip"].integer())].curves[static_cast<std::size_t>(row["curve"].integer())];
            const auto value=curve.sample(row["time"].number());check(value.has_value()==!row["value"].isNull(),"Oracle optional sample parity");if(value)compareValue(*value,row["value"]);
        }
        for(const auto& row:oracle["poseSamples"].array()){
            auto output=watch.pose(row["entrance"].number(),{},{},{1000,640});
            if(!row["ambient"].isNull())first.apply(row["ambient"].number(),output);
            if(!row["exit"].isNull())applyClip(watch.exit(),row["exit"].number(),output,definition);
            comparePose(output,row["pose"]);
        }
        const auto& channels=oracle["ambientChannels"].array();check(channels.size()==first.channels().size(),"Oracle ambient channel count");
        for(std::size_t i=0;i<channels.size();++i){check(first.channels()[i].node==channels[i]["node"].string(),"Oracle ambient source binding order");near(first.channels()[i].rate,channels[i]["rate"].number(),"Original Swift SplitMix64 and rate parity",1e-15);}
    }
    std::cout<<library.clips().size()<<" actual clips, "<<curves<<" curves, "<<keys<<" keys; Swift oracle "<<(oraclePath?"compared":"not supplied")<<'\n';
}
}
int main(int argc,char** argv){try{interpolation();poses();quaternionHelpers();if(argc>1)actualPacket(argv[1],argc>2?std::optional<std::filesystem::path>(argv[2]):std::nullopt);std::cout<<checks<<" source animation checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
