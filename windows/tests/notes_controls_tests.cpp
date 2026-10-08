#include "modules/notes_controls.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};unsigned checks{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace endfield::modules;using endfield::core::Rect;using ehud::data::Json;
namespace {
void check(bool okay,const char*what){++checks;if(!okay)throw std::runtime_error(what);}
void checkNear(double a,double b,const char*m){check(std::abs(a-b)<1e-6,m);}
template<class F>void rejects(F f,const char*m){bool yes=false;try{f();}catch(const std::invalid_argument&){yes=true;}check(yes,m);}
const Json*find(const Json&root,std::string_view key,std::string_view value){if(root[key].isString()&&root[key].string()==value)return &root;if(root["children"].isArray())for(const auto&child:root["children"].array())if(auto*p=find(child,key,value))return p;return nullptr;}
const Json&get(const Json&root,std::string_view id){const auto*p=find(root,"id",id);check(p!=nullptr,"Named source control artwork exists");return *p;}
Rect rect(const Json&j){const auto&a=j.array();return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
void tests(){
    { NotesControls confirm;NotesControlsInput input;input.kind=NotesControlsKind::deletion;confirm.update(input);
      check(confirm.bounds()==Rect{0,0,56,25}&&confirm.actions().size()==2&&confirm.images().empty(),"Source confirmation has two compact controls without external images");
      check(confirm.actionAt({1,1})=="cancelDelete"&&confirm.actionAt({32,1})=="confirmDelete"&&!confirm.actionAt({28,12}),"Confirmation gap cannot activate either action");
      check(get(confirm.artwork(),"cancelDelete/symbol")["shape"]["lineCap"].string()=="round"&&rect(get(confirm.artwork(),"cancelDelete/symbol")["frame"])==Rect{8,8,9,9},"Source cancellation icon preserves rounded one-point stroke and inset");
      confirm.setFeedback("confirmDelete",true,false);confirm.setFeedback({},false,false);check(confirm.feedback()[1].rimOpacity==0,"Confirmation uses source unframed highlight"); }

    NotesControls plan;NotesControlsInput i;check(plan.update(i),"First explicit center input constructs descriptors");
    check(plan.bounds()==Rect{0,0,400,334}&&plan.actions().size()==4&&plan.images().size()==2,"Original center contains four controls and two original icon dependencies");
    check(get(plan.artwork(),"notes.controls/heading")["text"]["string"].string()=="// NOTES","Source heading decoration retained");
    check(get(plan.artwork(),"notes.controls/status")["text"]["string"].string().empty(),"Original normal status stays empty rather than adding note counts");
    constexpr std::array<const char*,4>ids{"tool:text","tool:todo","tool:image","tool:drawing"};
    for(std::size_t n=0;n<4;++n){const auto&a=plan.actions()[n];check(a.id==ids[n]&&a.rect==Rect{7+98*double(n),294,92,31},"One source rectangle supplies hit and artwork geometry");check(plan.actionAt({a.rect.x+.5,a.rect.y+.5})==a.id,"Source add control hit maps to opaque owner action");}
    check(!plan.actionAt({400,300})&&!plan.actionAt({0,0}),"No invented center controls outside four original plates");
    const auto&drawing=get(plan.artwork(),"tool:drawing/icon");check(drawing["name"].string()=="hud.pencil"&&rect(drawing["frame"])==Rect{11,7,14,14},"Original retained pencil uses transparent-inset matching frame");
    check(drawing["shape"]["path"].array().size()==8&&drawing["shape"]["path"].array()[5]["op"].string()=="close","Pencil keeps its closed tip and separate underline");
    check(plan.images()[0].sourceResource=="AppIconSources/EndfieldWiki/Operational_Manual_icon.png"&&plan.images()[1].sourceResource=="AppIconSources/EndfieldWiki/Mission_Icon.png"&&plan.images()[0].requestedPixels==36&&plan.images()[0].sourceInTint,"Source game icons are explicit original asset requests with correct tint and raster scale");
    const auto revision=plan.contentRevision();const auto*root=&plan.artwork();const auto*actions=plan.actions().data();
    allocations=0;counting=true;for(unsigned n=0;n<1000;++n){check(!plan.update(i),"Unchanged state does not rebuild");plan.setFeedback(n%2?std::optional<std::string_view>{"tool:text"}:std::nullopt,false,false);}counting=false;
    check(allocations==0&&plan.contentRevision()==revision&&actions==plan.actions().data()&&root==&plan.artwork(),"Idle/content equality and pointer targets retain all artwork without allocations");
    plan.setFeedback("tool:text",true,false);checkNear(plan.feedback()[0].tintOpacity,1,"Pressed source tint");checkNear(plan.feedback()[0].duration,.06,"Pressed source duration");
    plan.setFeedback({},false,false);checkNear(plan.feedback()[0].duration,.14,"Source release duration");checkNear(plan.feedback()[0].rimOpacity,0,"Unframed center rim rests hidden");
    check(get(plan.artwork(),"tool:text/highlight/tint")["opacity"].number()==0,"Feedback does not rewrite/rasterize static local artwork");
    auto invalid=i;invalid.error="synthetic failure";rejects([&]{plan.update(invalid);},"Native dynamic error color is required, never guessed");check(plan.contentRevision()==revision,"Rejected complete update leaves all prior content intact");
    invalid.systemOrange=NotesColor{1,.6,.1,1};plan.update(invalid);check(get(plan.artwork(),"notes.controls/status")["text"]["string"].string()=="Could not save: synthetic failure","Explicit save error has source prefix");
    i.storageAvailable=false;plan.update(i);check(get(plan.artwork(),"notes.controls/status")["text"]["string"].string()=="Notes storage unavailable"&&plan.actions().size()==4,"Unavailable store displays status while retaining original enabled actions");
    i.notesSelected=false;plan.update(i);check(plan.actions().empty()&&!plan.actionAt({8,300}),"Outgoing Notes center has no accessible/active add actions");
    i.notesSelected=true;i.manualIconAvailable=false;i.missionIconAvailable=false;i.dark=false;i.strings.heading="// 便笺";plan.update(i);
    check(plan.images().empty()&&get(plan.artwork(),"tool:text/icon")["text"]["string"].string()=="T"&&get(plan.artwork(),"tool:todo/icon")["text"]["string"].string()=="☑","Only actual source fallback glyphs are used when original resources are explicitly absent");
    check(get(plan.artwork(),"notes.controls/heading")["text"]["string"].string()=="// 便笺","Already decorated localized heading is not doubled");
    checkNear(get(plan.artwork(),"tool:text")["shape"]["fillColor"]["sRGB"].array()[0].number(),.84,"Source light toolbar palette");
    i={};i.kind=NotesControlsKind::mediaSource;plan.update(i);check(plan.bounds()==Rect{0,0,208,75}&&plan.actions().size()==3&&plan.actions()[0].id=="finder"&&plan.actions()[1].id=="shelf","Actual source media chooser geometry/actions");
    check(NotesControls::mediaSourceModuleRect()==Rect{96,212,208,75}&&NotesControls::shelfModuleRect()==Rect{30,28,340,260},"Menus keep source module-to-workspace placement anchors");
    for(const auto&f:plan.feedback())checkNear(f.rimOpacity,.28,"Framed menu rests with source rim");plan.setFeedback("finder",false,false);plan.setFeedback({},false,true);checkNear(plan.feedback()[0].duration,0,"Reduced motion menu feedback has no clock demand");
    i.kind=NotesControlsKind::size;i.values=NotesControls::sizeValues(13.5);i.selectedValue="14";i.firstRow=NotesControls::initialFirstRow(i.values,i.selectedValue);plan.update(i);
    check(i.values.size()==17&&std::count(i.values.begin(),i.values.end(),"14")==1&&plan.actions().size()==8,"Source font sizes deduplicate rounded current value and show only seven rows");
    check(plan.actions()[1].label=="14"&&plan.actions()[1].selected,"Selected source size begins at clamped first row");
    i.firstRow=1000;const auto old=plan.contentRevision();rejects([&]{plan.update(i);},"Out-of-range menu scroll rejected before content publication");check(plan.contentRevision()==old,"Invalid menu scroll preserves content");
    i={};i.kind=NotesControlsKind::special;i.traits={true,false,true,false};plan.update(i);check(plan.bounds().height==48&&plan.actions().size()==5&&plan.actions()[1].label=="Bold"&&plan.actions()[1].selected&&plan.actions()[3].selected,"Original compact traits menu separates AX labels from B/I/U/S glyphs");
    i.kind=NotesControlsKind::color;plan.update(i);check(plan.images().size()==1&&plan.images()[0].requestedPixels==192&&!plan.images()[0].sourceInTint&&plan.images()[0].sourceResource=="source-generated:NotesColorWheelView.wheel","Original bounded wheel is explicit generated artwork dependency");
    check(plan.actions().size()==2&&!plan.actions()[1].enabled&&plan.feedback().size()==1,"Current color swatch has no activation/highlight");
    check(get(plan.artwork(),"swatch/plate")["opacity"].number()==1,"Disabled color swatch retains source full opacity");
    i={};i.kind=NotesControlsKind::shelfMedia;for(unsigned n=0;n<9;++n)i.choices.push_back({std::to_string(n),"Synthetic "+std::to_string(n),"PNG",n!=1,n!=2});
    plan.update(i);check(plan.actions().size()==7&&!plan.actions()[2].enabled&&plan.actions()[3].label=="Synthetic 0 · PNG"&&plan.actions()[4].label=="Synthetic 2 · PNG"&&!plan.actions()[4].enabled,"Shelf filters only unsupported records, retains unavailable rows disabled, and paints four rows");
    checkNear(rect(get(plan.artwork(),"notes.menu/scrollThumb")["frame"]).height,76,"Source four-of-eight shelf thumb");
    i.firstRow=4;i.selectedID="8";plan.update(i);check(plan.actions()[2].enabled&&plan.actions().back().selected,"Explicit owner selection reaches final source shelf row");
    checkNear(rect(get(plan.artwork(),"notes.menu/scrollThumb")["frame"]).y,138,"Source final shelf thumb position");
    i.mediaOnly=false;i.firstRow=0;i.selectedID.reset();plan.update(i);check(plan.actions()[4].label=="Synthetic 1 · PNG"&&!plan.actions()[4].enabled,"All-files view preserves unsupported records without enabling import");
    i.choices.push_back(i.choices[0]);rejects([&]{plan.update(i);},"Duplicate media identity rejected");
    rejects([&]{NotesControls::sizeValues(std::numeric_limits<double>::infinity());},"Nonfinite font-size conversion rejected");
}
void reference(const char*path){std::ifstream f(path,std::ios::binary|std::ios::ate);check(bool(f),"Explicit original Notes export opens");const auto size=f.tellg();check(size>0&&size<4*1024*1024,"Original Notes export is bounded");std::string bytes(std::size_t(size),'\0');f.seekg(0);f.read(bytes.data(),size);check(bool(f),"Original Notes export read complete");const auto j=Json::parse(bytes);const Json*source=nullptr;for(const auto&r:j["roots"].array())if(auto*p=find(r["layer"],"name","module.notes.canvas"))source=p;check(source!=nullptr,"Actual source has center canvas");NotesControls plan;NotesControlsInput i;plan.update(i);
    const auto&children=(*source)["children"].array();check(rect((*source)["bounds"])==plan.bounds(),"Actual source center dimensions match");
    for(unsigned n=0;n<2;++n){const auto id=n==0?"notes.controls/heading":"notes.controls/status";const auto&actual=children[n];const auto&ours=get(plan.artwork(),id);check(rect(actual["frame"])==rect(ours["frame"]),"Actual source header/status geometry matches");check(actual["text"]["string"]==ours["text"]["string"]&&actual["text"]["fontSize"]==ours["text"]["fontSize"],"Actual source header/status text and size match");}
    unsigned tools=0;for(const auto&a:j["actions"].array())if(a["space"].string()=="module-local"){check(tools<4,"No extra original center controls");const auto&ours=plan.actions()[tools++];check(a["id"].string()==ours.id&&a["label"].string()==ours.label&&rect(a["rect"])==ours.rect,"All original center action IDs/labels/rects match");}
    check(tools==4,"All four actual original tools checked");for(const auto&a:plan.actions()){const auto*actual=find(*source,"name",a.id);check(actual!=nullptr,"Actual source tool exists");const auto&ours=get(plan.artwork(),a.id);check(rect((*actual)["frame"])==rect(ours["frame"]),"Actual toolbar frame matches");checkNear((*actual)["shape"]["lineWidth"].number(),ours["shape"]["lineWidth"].number(),"Actual source plate stroke width");const auto&nodes=(*actual)["children"].array();const auto&label=nodes.back();const auto&ourLabel=get(plan.artwork(),a.id+"/label");check(rect(label["frame"])==rect(ourLabel["frame"])&&label["text"]["string"]==ourLabel["text"]["string"],"Actual source caption location/content");const auto&icon=nodes[nodes.size()-2];const auto&ourIcon=get(plan.artwork(),a.id+"/icon");check(rect(icon["frame"])==rect(ourIcon["frame"]),"Actual source icon frame/inset");if(a.id=="tool:drawing")check(icon["shape"]["path"]==ourIcon["shape"]["path"],"Actual retained pencil path is bit-identical source artwork");}
}
void sameColor(const Json&a,const Json&b){check(a.isNull()==b.isNull(),"Source optional color presence matches");if(!a.isNull()){check(a["sRGB"].array().size()==b["sRGB"].array().size(),"Source color component count");for(std::size_t n=0;n<a["sRGB"].array().size();++n)checkNear(a["sRGB"].array()[n].number(),b["sRGB"].array()[n].number(),"Original color-space conversion agrees within sub-byte precision");}}
void sameTree(const Json&a,const Json&b){
    check(rect(a["bounds"])==rect(b["bounds"]),"Original menu node bounds match");
    // Root CALayer has zero frame with explicit bounds; descendants have the
    // source local frame. Root projection is the owner's separate responsibility.
    if(b["id"].string()!="notes.menu")for(unsigned n=0;n<4;++n)check(std::abs(a["frame"].array()[n].number()-b["frame"].array()[n].number())<1e-10,"Original menu frame matches through CALayer anchor/position round-trip");
    check(a["kind"]==b["kind"],"Original menu node paint kind");
    checkNear(a["opacity"].number(),b["opacity"].number(),"Original menu opacity");
    sameColor(a["backgroundColor"],b["backgroundColor"]);
    if(!b["borderColor"].isNull()){sameColor(a["borderColor"],b["borderColor"]);checkNear(a["borderWidth"].number(),b["borderWidth"].number(),"Original menu border width");}
    if(b["kind"].string()=="text"){const auto&x=a["text"];const auto&y=b["text"];check(x["string"]==y["string"]&&x["truncation"]==y["truncation"]&&x["fontSize"]==y["fontSize"],"Actual menu text/content/truncation/size");sameColor(x["foregroundColor"],y["foregroundColor"]);}
    if(b["kind"].string()=="shape"){const auto&x=a["shape"];const auto&y=b["shape"];check(x["path"].array().size()==y["path"].array().size(),"Original menu path command count");for(std::size_t n=0;n<x["path"].array().size();++n){const auto&p=x["path"].array()[n];const auto&q=y["path"].array()[n];check(p["op"]==q["op"],"Original menu path operation");check(p["points"].array().size()==q["points"].array().size(),"Original menu path point count");for(std::size_t v=0;v<p["points"].array().size();++v)for(unsigned axis=0;axis<2;++axis)checkNear(p["points"].array()[v].array()[axis].number(),q["points"].array()[v].array()[axis].number(),"Original cut-corner highlight geometry");}sameColor(x["fillColor"],y["fillColor"]);sameColor(x["strokeColor"],y["strokeColor"]);checkNear(x["lineWidth"].number(),y["lineWidth"].number(),"Original menu stroke width");}
    check(a["children"].array().size()==b["children"].array().size(),"Original menu layer insertion count/order");for(std::size_t n=0;n<a["children"].array().size();++n)sameTree(a["children"].array()[n],b["children"].array()[n]);
}
void menuReference(const char*path){std::ifstream f(path,std::ios::binary|std::ios::ate);check(bool(f),"Explicit detached source menu reference opens");const auto length=f.tellg();check(length>0&&length<4*1024*1024,"Detached menu oracle stays bounded");std::string bytes(std::size_t(length),'\0');f.seekg(0);f.read(bytes.data(),length);check(bool(f),"Menu reference complete");const auto source=Json::parse(bytes);check(source["kind"].string()=="unchanged-source-detached-notes-menu-artwork"&&!source["windowCreated"].boolean()&&!source["screenCapture"].boolean(),"Menu reference calls actual original artwork with no window/capture");unsigned states=0;
    for(const auto&state:source["cases"].array()){NotesControlsInput i;i.dark=state["state"]["dark"].boolean();if(state["state"]["kind"].string()=="media")i.kind=NotesControlsKind::mediaSource;else{i.kind=NotesControlsKind::shelfMedia;i.firstRow=std::size_t(state["state"]["firstRow"].integer());i.mediaOnly=state["state"]["mediaOnly"].boolean();if(!state["state"]["selectedID"].isNull())i.selectedID=state["state"]["selectedID"].string();for(unsigned n=0;n<9;++n)i.choices.push_back({"00000000-0000-4000-8000-"+std::string(11,'0')+std::to_string(n),"Synthetic "+std::to_string(n),"PNG",n!=1,n!=2});}
        NotesControls plan;plan.update(i);check(plan.bounds()==rect(state["bounds"]),"Actual menu bounds match descriptor");check(plan.actions().size()==state["items"].array().size(),"Actual menu action count");for(std::size_t n=0;n<plan.actions().size();++n){const auto&a=plan.actions()[n];const auto&b=state["items"].array()[n];check(a.id==b["id"].string()&&a.rect==rect(b["rect"])&&a.enabled==b["enabled"].boolean()&&a.selected==b["selected"].boolean(),"Actual source ordered menu action states");}sameTree(state["root"],plan.artwork());++states;
    }check(states==10,"Both themes and shelf top/bottom/selection/filter variants verified");
}
}
int main(int argc,char**argv){try{tests();check(argc>=1&&argc<=3,"Pass optional original center and detached menu JSON fixtures");if(argc>=2)reference(argv[1]);if(argc==3)menuReference(argv[2]);std::cout<<"PASS "<<checks<<" Notes controls checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
