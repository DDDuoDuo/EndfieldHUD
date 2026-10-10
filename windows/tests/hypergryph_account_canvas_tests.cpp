// Account module face and shaped menus against the unchanged Mac
// HUDAccountCanvas: controls, menu geometry/scrolling/keyboard, emitted actions
// and the flattened CALayer paint tree (text, fonts, colours, paths, clips).
// Detached synthetic presentations only.
#include "modules/hypergryph_account_canvas.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <iostream>
#include <limits>

namespace h=endfield::modules::hypergryph;
using ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool value,const std::string& why) {++checks;if(!value) throw std::runtime_error(why);}
bool equivalent(const Json& a,const Json& b) {
    if(a.isNumber()&&b.isNumber()) return std::abs(a.number()-b.number())<1e-6;
    if(a.isArray()!=b.isArray()||a.isObject()!=b.isObject()) return false;
    if(a.isArray()) {if(a.array().size()!=b.array().size()) return false;for(std::size_t i=0;i<a.array().size();++i) if(!equivalent(a.array()[i],b.array()[i])) return false;return true;}
    if(a.isObject()) {if(a.object().size()!=b.object().size()) return false;for(const auto& [k,v]:a.object()) if(!b.contains(k)||!equivalent(v,b[k])) return false;return true;}
    return a==b;
}
double number(const Json& j,double fallback=0) {return j.isNull()?fallback:j.number();}
Json rgba(const Json& c) {return c.isNull()?Json{}:c["sRGB"];}
Json canonical(const Json& n,endfield::core::Point origin,double opacity,const Json& clips) {
    const auto kind=n["kind"].isNull()?std::string("layer"):n["kind"].string();
    Json r=Json::Object{{"kind",kind},{"origin",Json::Array{origin.x,origin.y}},{"opacity",opacity},{"clips",clips}};
    if(kind=="text") {const auto& t=n["text"];r["bounds"]=n["bounds"];
        r["text"]=Json::Object{{"string",t["string"]},{"fontSize",t["fontSize"]},{"family",t["font"]["familyName"]},{"face",t["font"]["postScriptName"]},{"foreground",rgba(t["foregroundColor"])},
            {"alignment",t["alignment"]},{"wrapped",t["wrapped"]},{"truncation",t["truncation"]}};}
    else if(kind=="shape") {const auto& s=n["shape"];r["shape"]=Json::Object{{"path",s["path"]},{"fill",rgba(s["fillColor"])},{"stroke",rgba(s["strokeColor"])},
        {"width",s["strokeColor"].isNull()?Json{0}:s["lineWidth"]}};}
    else {r["bounds"]=n["bounds"];r["background"]=rgba(n["backgroundColor"]);r["border"]=number(n["borderWidth"])>0?Json::Array{n["borderWidth"],rgba(n["borderColor"])}:Json{};}
    return r;
}
void flatten(const Json& n,endfield::core::Point parent,double parentOpacity,Json::Array clips,std::vector<Json>& out,bool root=false) {
    if(!n["hidden"].isNull()&&n["hidden"].boolean()) return;
    const auto& b=n["bounds"].array();const auto& p=n["position"].array();const auto& a=n["anchorPoint"].array();
    // The detached canvas root is the module's own coordinate space (its host placement is separate).
    const endfield::core::Point origin=root?endfield::core::Point{-b[0].number(),-b[1].number()}:
        endfield::core::Point{parent.x+p[0].number()-b[0].number()-a[0].number()*b[2].number(),parent.y+p[1].number()-b[1].number()-a[1].number()*b[3].number()};
    const double opacity=parentOpacity*number(n["opacity"],1);const auto kind=n["kind"].string();
    if(kind=="text"||kind=="shape"||!rgba(n["backgroundColor"]).isNull()) out.push_back(canonical(n,origin,opacity,clips));
    if(!n["masksToBounds"].isNull()&&n["masksToBounds"].boolean()) clips.push_back(Json::Array{origin.x,origin.y,b[2].number(),b[3].number()});
    for(const auto& c:n["children"].array()) flatten(c,origin,opacity,clips,out);
}
std::vector<h::AccountPresentation::RoleRow> roles(int count) {
    std::vector<h::AccountPresentation::RoleRow> out;
    for(int i=0;i<count;++i) out.push_back({"role-"+std::to_string(i),std::string(i%2==0?"Endfield":"Arknights")+" · Fixture "+std::to_string(i),i%3==0?"":"Server "+std::to_string(i)});
    return out;
}
std::string describe(const h::AccountAction& a) {
    using K=h::AccountAction::Kind;
    switch(a.kind) {
        case K::connect: return "connect:"+std::string(h::regionChoiceName(a.region));
        case K::refresh: return "refresh";case K::disconnect: return "disconnect";
        case K::selectRegion: return "selectRegion:"+std::string(h::regionChoiceName(a.region));
        case K::selectRole: return "selectRole:"+a.roleID;
        case K::selectHeaderMode: return "selectHeaderMode:"+std::string(h::headerModeName(a.headerMode));
        case K::setSyncProfile: return std::string("setSyncProfile:")+(a.enabled?"true":"false");
        case K::setSyncAvatar: return std::string("setSyncAvatar:")+(a.enabled?"true":"false");
    }
    return {};
}
void replay(const Json& canvas) {
    h::AccountCanvasModel model;model.setVisible(true);std::vector<std::string> emitted;
    model.onAction=[&](const h::AccountAction& a){emitted.push_back(describe(a));};
    h::CanvasStyle style;const auto& accent=canvas["accent"].array();style.accent={accent[0].number(),accent[1].number(),accent[2].number(),accent[3].number()};
    h::AccountPresentation fixture;std::size_t layerStates{};
    for(const auto& state:canvas["states"].array()) {
        const auto name=state["name"].string();
        for(const auto& op:state["ops"].array()) {
            if(op.contains("perform")) model.perform(op["perform"].string());
            else if(op.contains("move")) model.moveMenuSelection(static_cast<int>(op["move"].integer()));
            else if(op.contains("activate")) model.activateMenuSelection();
            else if(op.contains("scroll")) {const auto& v=op["scroll"].array();model.scroll({v[0].number(),v[1].number()},v[2].number());}
            else if(op.contains("mouseDown")) {const auto& v=op["mouseDown"].array();model.mouseDown({v[0].number(),v[1].number()});}
            else if(op.contains("style")) {style.dark=false;style.feedbackAccent=style.accent;style.accent={.2,.55,.9,1};}
            else if(op.contains("language")) {model.setLanguage(endfield::core::Language::simplifiedChinese);style=h::CanvasStyle{};style.accent={accent[0].number(),accent[1].number(),accent[2].number(),accent[3].number()};}
            else if(op.contains("update")) {
                const auto which=op["update"].string();using S=h::AccountPresentation::Status;
                if(which=="linked") {fixture.isLinked=true;fixture.region=h::AccountPresentation::RegionChoice::global;fixture.status=S::connected;fixture.accountName="Fixture Endministrator";
                    fixture.roles=roles(2);fixture.selectedRoleID="role-0";fixture.headerMode=h::AccountPresentation::HeaderMode::endfield;fixture.lastSync="Updated 10/05 12:00:00";}
                else if(which=="roles9") {fixture.roles=roles(9);fixture.selectedRoleID="role-4";fixture.syncProfile=true;}
                else if(which=="roles3") fixture.roles=roles(3);
                else if(which=="busy") fixture.status=S::refreshing;
                else if(which=="failed") {fixture.status=S::failed;fixture.statusMessage="Sync unavailable. Saved data is preserved.";}
                else if(which=="connecting") {fixture.status=S::connecting;fixture.statusMessage="";fixture.isLinked=false;}
                else if(which=="reconnect") {fixture=h::AccountPresentation{};fixture.status=S::failed;fixture.statusMessage="Sign in again to continue syncing.";fixture.isLinked=true;
                    fixture.syncAvatar=true;fixture.headerMode=h::AccountPresentation::HeaderMode::workMode;}
                model.update(fixture);
            }
        }
        const auto controls=model.accessibleActions();const auto& expected=state["controls"].array();
        check(controls.size()==expected.size(),name+": control count");
        for(std::size_t i=0;i<controls.size();++i) {
            const auto& e=expected[i];const auto& r=e["rect"].array();const auto& c=controls[i];
            check(c.id==e["id"].string()&&c.label==e["label"].string()&&c.enabled==e["enabled"].boolean(),name+": control "+c.id+" '"+c.label+"' vs "+e.encode());
            check(std::abs(c.rect.x-r[0].number())<1e-9&&std::abs(c.rect.y-r[1].number())<1e-9&&std::abs(c.rect.width-r[2].number())<1e-9&&std::abs(c.rect.height-r[3].number())<1e-9,name+": control geometry "+c.id);
        }
        check(model.isPopoverOpen()==state["open"].boolean()&&std::abs(model.menuScrollOffset()-state["offset"].number())<1e-9,name+": popover state");
        if(const auto b=model.popoverBounds()) {const auto& r=state["bounds"].array();check(b->x==r[0].number()&&b->y==r[1].number()&&b->width==r[2].number()&&b->height==r[3].number(),name+": popover bounds");}
        else check(state["bounds"].isNull(),name+": no popover bounds");
        Json::Array actions;for(const auto& e:emitted) actions.emplace_back(e);
        check(Json(actions)==state["actions"],name+": emitted actions "+Json(actions).encode()+" vs "+state["actions"].encode());emitted.clear();
        if(state.contains("layers")) {
            ++layerStates;std::vector<Json> mac;flatten(state["layers"],{},1,{},mac,true);
            const auto plan=model.plan(style);const auto& leaves=plan.layers["children"].array();
            check(leaves.size()==plan.surfaces.size(),name+": plan surfaces");
            if(mac.size()!=leaves.size()) std::cerr<<name<<": Mac leaves "<<mac.size()<<" vs port "<<leaves.size()<<'\n';
            check(mac.size()==leaves.size(),name+": CALayer paint leaves match prepared surfaces");
            for(std::size_t i=0;i<leaves.size();++i) {
                const auto& s=plan.surfaces[i];Json::Array clips;if(s.clip) clips.push_back(Json::Array{s.clip->x,s.clip->y,s.clip->width,s.clip->height});
                const double y=s.local.values[13]-(s.rows?model.menuScrollOffset():0);
                const auto mine=canonical(leaves[i],{s.local.values[12],y},s.opacity,clips);
                if(!equivalent(mac[i],mine)) {std::cerr<<name<<" leaf "<<i<<" ("<<s.id<<")\nMac:  "<<mac[i].encode()<<"\nPort: "<<mine.encode()<<'\n';}
                check(equivalent(mac[i],mine),name+": leaf "+s.id+" matches the Mac layer (order, text, font, colour, path, placement, clip)");
            }
        }
    }
    check(layerStates>=12,"Many visual states compared");
}
void keyboard() {
    h::AccountCanvasModel model;model.setVisible(true);std::vector<std::string> emitted;
    model.onAction=[&](const h::AccountAction& a){emitted.push_back(describe(a));};
    h::AccountPresentation p;p.isLinked=true;p.status=h::AccountPresentation::Status::connected;p.roles=roles(9);p.selectedRoleID="role-0";model.update(p);
    check(!model.key(h::AccountCanvasModel::Key::down),"Keys pass through without a menu");
    model.perform("role");check(model.key(h::AccountCanvasModel::Key::down)&&model.selectedChoice()==1,"Down selects the next role");
    for(int i=0;i<20;++i) model.key(h::AccountCanvasModel::Key::down);
    check(model.selectedChoice()==8&&model.menuScrollOffset()==8*31+28-160,"Selection clamps and scrolls the last row into view");
    check(model.key(h::AccountCanvasModel::Key::activate)&&emitted==std::vector<std::string>{"selectRole:role-8"}&&!model.isPopoverOpen(),"Return chooses and closes");
    model.perform("disconnect");model.key(h::AccountCanvasModel::Key::activate);
    check(!model.isPopoverOpen()&&emitted.size()==1,"Return on the disconnect confirmation cancels (Mac safety)");
    model.perform("header");check(model.key(h::AccountCanvasModel::Key::escape)&&!model.isPopoverOpen(),"Escape dismisses only the menu");
    model.perform("header");p.status=h::AccountPresentation::Status::refreshing;model.update(p);check(!model.isPopoverOpen(),"Busy presentation dismisses menus");
    model.setVisible(false);check(!model.isPopoverOpen(),"Hiding dismisses menus");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Pass tests/fixtures/hypergryph-account-source.json");
        const auto bytes=ehud::data::detail::readFile(argv[1],4*1024*1024);check(bytes.has_value(),"Read bounded account oracle");
        const auto fixture=Json::parse(*bytes,4*1024*1024);
        check(fixture["canvas"]["unsupported"].array().size()>0,"Oracle lists highlight layers as custom (ported as tint/rim surfaces)");
        replay(fixture["canvas"]);keyboard();
        std::cout<<"PASS "<<checks<<" Account canvas checks\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
