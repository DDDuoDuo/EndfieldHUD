#include "core/source_selectable_color.hpp"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {bool countAllocations=false;std::size_t allocations=0;}
void* operator new(std::size_t size){if(countAllocations)++allocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

using namespace endfield::core::source;
namespace {
std::size_t checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
Json color(double value){return Json::Object{{"r",value},{"g",value},{"b",value},{"a",value}};}
Json block(double normal=0,double disabled=.125){return Json::Object{{"m_NormalColor",color(normal)},{"m_HighlightedColor",color(1)},
    {"m_PressedColor",color(.25)},{"m_SelectedColor",color(.75)},{"m_DisabledColor",color(disabled)},{"m_ColorMultiplier",1},{"m_FadeDuration",.2}};}
WatchComponent button(std::string id,std::string target,Json colors,bool interactable=true){return {id,"MonoBehaviour","UIButton",Json::Object{
    {"m_Transition",1},{"m_TargetGraphic",Json::Object{{"target_id",target}}},{"m_Colors",std::move(colors)},{"m_Interactable",interactable}}};}
WatchComponent image(std::string id,double alpha=1){return {id,"MonoBehaviour","UIImage",Json::Object{{"m_Color",color(alpha)}}};}
Node node(std::string id,std::string name,std::string path){Node n;n.id=std::move(id);n.name=std::move(name);n.path=std::move(path);n.parent="root";return n;}
struct Fixture {
    SceneDefinition scene;
    MountedLayoutDocument document;
    DesktopHoverProfile profile;
};
Fixture fixture(){
    std::vector<Node> nodes{node("root","Root","Root"),node("profile","PlayerInfo","Root/PlayerInfo"),node("peer","Peer","Root/PlayerInfo/Peer"),
        node("target","Image","Root/PlayerInfo/Image"),node("quit","QuitBtn","Root/QuitBtn"),node("quitTarget","Image","Root/QuitBtn/Image"),
        node("background","Bg","Root/QuitBtn/Bg"),node("line","LeftLineImage","Root/PlayerInfo/DecoNode/LeftLineImage"),
        node("zero","LineImage","Root/PlayerInfo/DecoNode/LineImage"),node("edge","Img","Root/QuitBtn/HoverHint/NaviHint/Img"),
        node("otherEdge","Img","Other/QuitBtn/HoverHint/NaviHint/Img")};
    nodes[0].parent.reset();for(std::size_t i=1;i<nodes.size();++i)nodes[0].children.push_back(nodes[i].id);
    MountedLayoutDocument doc;doc.components["profile"]={button("profile.button","target.image",block())};
    doc.components["peer"]={button("peer.button","target.image",block(.5),false)};
    doc.components["quit"]={button("quit.button","quit.image",block())};
    doc.components["target"]={image("target.image")};doc.components["quitTarget"]={image("quit.image")};
    doc.components["line"]={image("line.image",.2)};doc.components["zero"]={image("zero.image",0)};
    doc.components["root"]={{"null.button","Button",{},Json::Object{{"m_Transition",1},{"m_TargetGraphic",Json()}}}};
    doc.buttons.push_back({"quit","Root/QuitBtn",{}});
    return {SceneDefinition("root",std::move(nodes)),std::move(doc),{"profile",{"profile","peer"},{"profile","peer","line","zero"}}};
}
void synthetic(){
    auto f=fixture();SelectableColor sampler(f.scene,f.document);DesktopHoverFeedback hover(f.scene,f.document,sampler,f.profile);
    check(sampler.bindings().size()==3&&sampler.ignoredNullTargetButtonIDs().size()==1,"Null targets remain ignored and cannot silently bind another Graphic");
    check(sampler.state("peer")==SelectableState::disabled&&!sampler.state("missing"),"Source non-interactable state and missing button are distinct");
    check(sampler.colors(0).at("target")==SelectableTint{.125f,.125f,.125f,.125f},"Last source binding initializes a shared target renderer");
    sampler.setState(SelectableState::highlighted,"profile",1);check(sampler.requiresFrames(1)&&!sampler.requiresFrames(100),"Demand query samples the finite fade");
    check(sampler.colors(1.1).at("target")[3]==.5625f,"Demand query does not advance the clock before a halfway sample");
    sampler.setState(SelectableState::normal,"profile",1.1);check(sampler.colors(1.2).at("target")[3]==.28125f,"An interrupted fade starts from the current shared renderer color");
    sampler.setEnabled(false,"peer",1.2);check(sampler.colors(1.2).at("target")==SelectableTint{1,1,1,1},"OnDisable clears shared renderer tint to white rather than the Disabled color");
    sampler.setState(SelectableState::pressed,"peer",1.3);check(sampler.colors(1.3).at("target")[3]==1,"A disabled component cannot overwrite its renderer");
    sampler.setEnabled(true,"peer",1.3);check(sampler.colors(1.3).at("target")[3]==.125f,"OnEnable restores the source non-interactable Disabled tint instantly");
    sampler.setState(SelectableState::highlighted,"profile",1.4);sampler.colors(1.45,true);check(!sampler.requiresFrames(1.45)&&sampler.colors(1.45).at("target")[3]==1,"Reduced motion snaps all active target tweens to their endpoints");
    sampler.setEnabled(false,"profile",1.5);sampler.reset(1.6);sampler.setState(SelectableState::highlighted,"profile",1.6,true);
    check(sampler.colors(1.6).at("target")[3]==1,"Reset re-enables components and reduced transition writes the endpoint");
    sampler.setState(SelectableState::normal,"profile",2);sampler.setState(SelectableState::selected,"unknown",2.1);
    const auto clamped=sampler.colors(1).at("target")[3];check(clamped==.5f,"An unknown event still advances the source monotonic clock");
    const auto nan=std::numeric_limits<double>::quiet_NaN();sampler.reset(nan);sampler.setState(SelectableState::pressed,"profile",nan);sampler.setEnabled(false,"profile",nan);
    check(sampler.colors(nan).at("target")[3]==clamped&&!sampler.requiresFrames(nan),"Invalid times preserve sample state and report no demand");
    sampler.reset(3);check(!sampler.requiresFrames(3)&&sampler.colors(3).at("target")[3]==.125f,"Reset applies shared bindings in original traversal order");
    check(hover.sideEdgeIDs()==std::set<std::string,std::less<>>{"edge"}&&DesktopHoverFeedback::sideEdgeOpacity==.18f,"Side edge requires both exact source suffix and a mounted button prefix");
    check(hover.groupedButton("peer")=="profile"&&hover.groupedButton("quit")=="quit"&&!hover.groupedButton({}),"Only exported profile regions group to the actual mounted root");
    check(hover.profileHighlightNodeID()=="target","Profile highlight uses the root selectable target identity");
    SelectableTints tints{{"target",{1,1,1,1}},{"quitTarget",{1,1,1,1}}};const auto& opacity=hover.opacities(tints);
    check(opacity.size()==2&&!opacity.contains("zero")&&opacity.at("line")==1.f+(.62f-.2f)/.2f&&opacity.at("background")==1.25f,"Profile and quit decoration gains match authored alpha rules");
    tints["target"][3]=nan;tints.erase("quitTarget");check(hover.opacities(tints).at("line")==1&&hover.opacities(tints).at("background")==1,"Missing or nonfinite sampled tint produces neutral decoration opacity");
    const auto* output=&sampler.colors(4);const auto* targetValue=&output->at("target");const auto* opacities=&hover.opacities(*output);
    allocations=0;countAllocations=true;
    for(unsigned i=0;i<1000;++i){const auto time=5+i*.013;sampler.setState(i%2?SelectableState::normal:SelectableState::highlighted,"profile",time);
        sampler.setState(SelectableState::highlighted,"quit",time);const auto& values=sampler.colors(time+.007);hover.opacities(values);sampler.requiresFrames(time+.008);hover.groupedButton("peer");}
    countAllocations=false;check(allocations==0,"Changing fades, output tints and decoration opacities allocate nothing after construction");
    check(&sampler.colors(30)==output&&&sampler.colors(30).at("target")==targetValue&&&hover.opacities(*output)==opacities,"All returned maps and existing slots retain their identities");
    auto invalid=f.document;invalid.components["root"].push_back(image("target.image"));rejects([&]{SelectableColor bad(f.scene,invalid);},"Duplicate component IDs are rejected even when the selectable is ignored");
    invalid=f.document;invalid.components["profile"][0].data["m_TargetGraphic"]=Json::Object{{"target_id","absent"}};rejects([&]{SelectableColor bad(f.scene,invalid);},"Missing non-null targets reject before sampling");
    invalid=f.document;invalid.components["profile"].push_back(button("other.button","target.image",block()));rejects([&]{SelectableColor bad(f.scene,invalid);},"Multiple ColorTint selectables on one node reject ambiguous state");
    invalid=f.document;invalid.components["profile"][0].data["m_Colors"]["m_FadeDuration"]=-1;rejects([&]{SelectableColor bad(f.scene,invalid);},"Negative source duration rejects");
    invalid=f.document;invalid.components["profile"][0].data["m_Colors"]["m_NormalColor"]["r"]=1e100;rejects([&]{SelectableColor bad(f.scene,invalid);},"A finite Double outside Float range rejects before GPU sampling");
    auto profile=f.profile;profile.rootID="missing";rejects([&]{DesktopHoverFeedback bad(f.scene,f.document,sampler,profile);},"Profile binding cannot invent absent source nodes");
}
double sampleTime(const Json& v){if(v.isNumber())return v.number();const auto s=v.string();if(s=="nan")return std::numeric_limits<double>::quiet_NaN();return s=="inf"?std::numeric_limits<double>::infinity():-std::numeric_limits<double>::infinity();}
void bits(float actual,const Json& expected,const std::string& context){++checks;const auto value=std::bit_cast<std::uint32_t>(actual),want=static_cast<std::uint32_t>(expected.integer());if(value!=want)throw std::runtime_error(context+": Float bits "+std::to_string(value)+" expected "+std::to_string(want));}
void optionalID(const std::optional<std::string>& actual,const Json& expected){check(actual?(expected.isString()&&*actual==expected.string()):expected.isNull(),"Optional source binding identity matches Swift");}
void oracle(std::istream& stream){
    std::string line;check(bool(std::getline(stream,line)),"Original source oracle has provenance");const auto provenance=Json::parse(line);
    check(provenance["sourceSHA256"].string()=="8d1729e5480f7f82d214c62cb61b48ed13c306662f57bbd12a4a03e9622be7b4"&&
        provenance["hoverDeclarationSHA256"].string()=="77a2e3ffec61b1e4c59fc81d8fedec4c6e022285ba9095983faaa258a52924b7","Oracle used unchanged current Swift selectable and hover source");
    std::size_t cases{},errors{},rows{};
    while(std::getline(stream,line)){
        const auto record=Json::parse(line,32*1024*1024);const auto& input=record["input"];const auto name=input["name"].string();++cases;
        const auto scene=SceneDefinition::fromJson(input["scene"]);const auto doc=MountedLayoutDocument::fromJson(input["document"]);
        std::unique_ptr<SelectableColor> sampler;
        try{sampler=std::make_unique<SelectableColor>(scene,doc);}catch(const std::invalid_argument& error){if(record["error"].isNull())throw std::runtime_error(name+": unexpected constructor rejection "+error.what());++errors;++checks;continue;}
        check(record["error"].isNull(),"C++ does not accept an original Swift error case");DesktopHoverFeedback hover(scene,doc,*sampler,DesktopHoverProfile::fromJson(input["profile"]));
        const auto& expected=record["expected"];check(sampler->bindings().size()==expected["bindings"].array().size(),"Source binding count matches");
        for(std::size_t i=0;i<sampler->bindings().size();++i){const auto& a=sampler->bindings()[i];const auto& b=expected["bindings"].array()[i];
            check(a.buttonNodeID==b["button"].string()&&a.buttonComponentID==b["component"].string()&&a.targetGraphicID==b["targetGraphic"].string()&&a.targetNodeID==b["targetNode"].string(),"Binding source traversal/order/target identities match");
            check(a.sourceInteractable==b["interactable"].boolean()&&a.colors.fadeDuration==b["duration"].number(),"Source interactability and Float-to-Double duration match");
            for(unsigned state=0;state<5;++state){const auto value=a.colors.color(static_cast<SelectableState>(state));for(unsigned channel=0;channel<4;++channel)bits(value[channel],b["colors"].array()[state].array()[channel],name+" binding");}}
        check(sampler->ignoredNullTargetButtonIDs().size()==expected["ignored"].array().size(),"Null targets ignored count matches");for(std::size_t i=0;i<sampler->ignoredNullTargetButtonIDs().size();++i)check(sampler->ignoredNullTargetButtonIDs()[i]==expected["ignored"].array()[i].string(),"Null target source order matches");
        check(hover.sideEdgeIDs().size()==expected["sideEdges"].array().size(),"Side edge identity count matches");for(const auto& id:expected["sideEdges"].array())check(hover.sideEdgeIDs().contains(id.string()),"Side edge source identity matches");
        optionalID(hover.profileRootID(),expected["profileRoot"]);optionalID(hover.profileHighlightNodeID(),expected["profileHighlight"]);
        for(std::size_t i=0;i<scene.nodes().size();++i){const auto group=hover.groupedButton(scene.nodes()[i].id);const auto& wanted=expected["groups"].array()[i];check(group?(wanted.isString()&&*group==wanted.string()):wanted.isNull(),"Exact exported profile button grouping matches");}
        const auto& commands=input["commands"].array();check(commands.size()==expected["rows"].array().size(),"Every event has an oracle row");
        for(std::size_t i=0;i<commands.size();++i){++rows;const auto& command=commands[i];const auto& row=expected["rows"].array()[i];const auto op=command["op"].string();const auto time=sampleTime(command["time"]);
            const auto button=command["button"].isString()?command["button"].string():std::string{};const bool reduce=command["reduce"].isBool()&&command["reduce"].boolean();
            if(op=="reset")sampler->reset(time);else if(op=="state")sampler->setState(static_cast<SelectableState>(command["state"].integer()),button,time,reduce);else if(op=="enabled")sampler->setEnabled(command["enabled"].boolean(),button,time);
            for(std::size_t j=0;j<sampler->instanceIDs().size();++j)check(static_cast<unsigned>(*sampler->state(sampler->instanceIDs()[j]))==row["states"].array()[j].integer(),"Event state matches original Swift");
            if(op=="sample"){
                const auto& values=sampler->colors(time,reduce);check(values.size()==row["colors"].object().size(),"Sample renderer target count matches");
                for(const auto& [id,value]:values)for(unsigned channel=0;channel<4;++channel)bits(value[channel],row["colors"][id].array()[channel],name+" row "+std::to_string(i)+" target "+id+" channel "+std::to_string(channel));
                const auto& opacity=hover.opacities(values);check(opacity.size()==row["opacities"].object().size(),"Hover target count matches");for(const auto& [id,value]:opacity)bits(value,row["opacities"][id],name+" hover row "+std::to_string(i)+" node "+id);
            }
            if(op=="query"||op=="sample")check(sampler->requiresFrames(time)==row["demand"].boolean(),"Finite frame demand matches Swift without mutating the clock");
        }
    }
    check(cases>=50&&errors>=9&&rows>=15000,"Oracle covers mounted controls, randomized shared-target fades and malformed inputs");
    std::cout<<"Compared "<<cases<<" original Swift fixtures, "<<rows<<" events, "<<errors<<" matching errors\n";
}
}
int main(int argc,char**argv){try{synthetic();if(argc==2){std::ifstream stream(argv[1],std::ios::binary);check(bool(stream),"Explicit owned oracle opens");oracle(stream);}else check(argc==1,"Pass at most one explicit original Swift oracle JSONL");
    std::cout<<"PASS "<<checks<<" source selectable color checks\n";return 0;
}catch(const std::exception& error){countAllocations=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
