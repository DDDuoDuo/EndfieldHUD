#include "modules/hypergryph_account_canvas.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules::hypergryph {
namespace {
using Json=ehud::data::Json;using core::Rect;using core::Point;using core::Matrix4;
bool inside(Rect r,Point p) {return r.width>0&&r.height>0&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Rect intersection(Rect a,Rect b) {
    const double l=std::max(a.x,b.x),t=std::max(a.y,b.y),r=std::min(a.x+a.width,b.x+b.width),d=std::min(a.y+a.height,b.y+b.height);
    if(r<=l||d<=t) return {};return {l,t,r-l,d-t};
}
Json rect(Rect r) {return Json::Array{r.x,r.y,r.width,r.height};}
Json color(CanvasColor c) {return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
CanvasColor white(double w,double a=1) {return {w,w,w,a};}
CanvasColor alpha(CanvasColor c,double a) {c[3]=a;return c;}
constexpr CanvasColor orange{1,159./255,10./255,1}; // NSColor.systemOrange (dark)
Json command(const char* op,std::initializer_list<Point> points) {Json::Array v;for(const auto& p:points) v.emplace_back(Json::Array{p.x,p.y});return Json::Object{{"op",op},{"points",std::move(v)}};}
Json cut(Rect r) {
    const double c=std::min(4.0,std::min(r.width,r.height)/3);
    return Json::Array{command("move",{{r.x+c,r.y}}),command("line",{{r.x+r.width,r.y}}),command("line",{{r.x+r.width,r.y+r.height-c}}),
        command("line",{{r.x+r.width-c,r.y+r.height}}),command("line",{{r.x,r.y+r.height}}),command("line",{{r.x,r.y+c}}),command("close",{})};
}
// CGPath.copy(strokingWithWidth: 1.6) of the arc (center 9,9, radius 6,
// pi/3...65pi/36) plus the arrow head: exact CoreGraphics outline exported
// from the unchanged HUDAccountCanvas.drawRefresh (static geometry).
Json refreshPath() {
    using P=Point;
    return Json::Array{command("move",{P{11.6,13.503332099679081}}),
        command("cubic",{P{9.112878358083766,14.939272449146763},P{5.9326082497886015,14.087121641916237},P{4.496667900320919,11.6}}),
        command("cubic",{P{3.060727550853236,9.112878358083766},P{3.912878358083764,5.9326082497886015},P{6.3999999999999995,4.496667900320919}}),
        command("cubic",{P{8.735000101381416,3.148554963230554},P{11.71309900179269,3.808783594274541},P{13.259590630302759,6.017402530974559}}),
        command("cubic",{P{13.513012645229939,6.379326676457382},P{14.011849269331966,6.467285032252096},P{14.373773414814789,6.213863017324915}}),
        command("cubic",{P{14.735697560297611,5.960441002397734},P{14.823655916092324,5.461604378295708},P{14.570233901165144,5.099680232812885}}),
        command("cubic",{P{12.547898694651979,2.211486238666709},P{8.653461671037235,1.3481103365322638},P{5.599999999999999,3.1110272542658173}}),
        command("cubic",{P{2.3476101605218433,4.988795403598134},P{1.2332591049334996,9.147610160521843},P{3.1110272542658173,12.4}}),
        command("cubic",{P{4.988795403598134,15.652389839478158},P{9.147610160521843,16.7667408950665},P{12.4,14.888972745734183}}),
        command("cubic",{P{12.78263409878096,14.668058845801864},P{12.913734222959869,14.178786521487591},P{12.69282032302755,13.796152422706632}}),
        command("cubic",{P{12.471906423095232,13.413518323925672},P{11.98263409878096,13.282418199746763},P{11.6,13.503332099679081}}),
        command("close",{}),command("move",{P{15.9,8.2}}),command("line",{P{11.4,8.2}}),command("line",{P{15.9,3.7}}),command("close",{})};
}
struct Painter {
    CanvasPlan plan;Json::Array layers;CanvasStyle style;CanvasColor ink;bool menu{},rows{};std::optional<Rect> clip;
    explicit Painter(CanvasStyle s):style(s),ink(white(s.dark?.95:.12)) {}
    void emit(std::string id,Json node,Rect frame,float opacity=1,std::string feedback={},bool rim=false) {
        node["id"]=id;node["position"]=Json::Array{frame.x,frame.y};node["anchorPoint"]=Json::Array{0,0};node["opacity"]=1;node["children"]=Json::Array{};
        plan.surfaces.push_back({std::move(id),Matrix4::translation(frame.x,frame.y),opacity,clip,std::move(feedback),rim,menu,rows});layers.push_back(std::move(node));
    }
    void text(std::string id,std::string value,Rect r,double size,std::optional<CanvasColor> c={},bool centered=false) {
        emit(std::move(id),Json::Object{{"kind","text"},{"bounds",rect({0,0,r.width,r.height})},{"text",Json::Object{{"string",std::move(value)},{"fontSize",size},
            {"font",Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".AppleSystemUIFontDemi"},{"pointSize",size}}},{"foregroundColor",color(c.value_or(ink))},
            {"alignment",centered?"center":"left"},{"wrapped",false},{"truncation","end"},{"runs",Json::Array{}}}}},r);
    }
    void fill(std::string id,Rect r,CanvasColor c,float opacity=1,std::optional<std::pair<double,CanvasColor>> border={}) {
        Json node=Json::Object{{"kind","layer"},{"bounds",rect({0,0,r.width,r.height})},{"backgroundColor",color(c)}};
        if(border) {node["borderWidth"]=border->first;node["borderColor"]=color(border->second);}
        emit(std::move(id),std::move(node),r,opacity);
    }
    void shape(std::string id,Json path,Rect frame,std::optional<CanvasColor> fillColor,std::optional<CanvasColor> stroke,double width,float opacity,std::string feedback={},bool rim=false) {
        emit(std::move(id),Json::Object{{"kind","shape"},{"bounds",rect({0,0,frame.width,frame.height})},{"shape",Json::Object{{"path",std::move(path)},
            {"fillColor",fillColor?color(*fillColor):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"},{"fillRule","non-zero"}}}},
            frame,opacity,std::move(feedback),rim);
    }
    // HUDControlHighlightLayer.add(..., shape: .cutCorner, framed: true)
    void highlight(const std::string& prefix,const std::string& control,Rect r,bool enabled) {
        const auto accent=style.feedbackAccent.value_or(style.accent);
        shape(prefix+"/tint",cut({0,0,r.width,r.height}),r,alpha(accent,.30),{},1,0,enabled?control:"",false);
        shape(prefix+"/rim",cut({-2,-2,r.width+4,r.height+4}),r,{},accent,.9,enabled?.28f:0.f,enabled?control:"",true);
    }
    void button(const std::string& prefix,const std::string& control,const std::string& label,Rect r,bool enabled=true,bool strong=false,bool selected=false,bool centered=true) {
        fill(prefix+"/plate",r,strong?style.accent:selected?alpha(style.accent,.22):alpha(ink,.07),enabled?1.f:.4f);
        highlight(prefix+"/feedback",control,r,enabled);
        const auto textColor=alpha(strong?white(.12):ink,enabled?1:.4);
        text(prefix+"/label",label,{r.x+7,r.y+r.height/2-7,r.width-14,16},10,textColor,centered);
    }
};
}

AccountCanvasModel::AccountCanvasModel()=default;
std::string AccountCanvasModel::t(std::string_view e,std::string_view z) const {return core::localized(e,z,language_);}
std::vector<CanvasControl> AccountCanvasModel::mainControls() const {
    const auto& p=presentation_;const bool busy=p.busy();std::vector<CanvasControl> c;
    c.push_back({"connect",busy?t("Connecting…","正在连接…"):p.isLinked?t("Reconnect","重新连接"):t("Connect","绑定账户"),{276,7,112,28},!busy});
    c.push_back({"refresh",t("Refresh","刷新"),{321,47,28,28},p.isLinked&&!busy});
    c.push_back({"disconnect",t("Disconnect","解除绑定"),{360,47,28,28},p.isLinked&&!busy});
    c.push_back({"region",regionChoiceTitle(p.region,language_)+"  ▾",{174,81,214,28},!busy});
    const auto role=std::find_if(p.roles.begin(),p.roles.end(),[&](const auto& r){return p.selectedRoleID&&r.id==*p.selectedRoleID;});
    c.push_back({"role",(role!=p.roles.end()?role->title:t("Choose account","选择账户"))+"  ▾",{174,120,214,28},!p.roles.empty()&&!busy});
    c.push_back({"header",headerModeTitle(p.headerMode,language_)+"  ▾",{174,159,214,28},true});
    c.push_back({"syncProfile",t("Sync personal profile","同步个人名片"),{360,203,28,28},!busy});
    c.push_back({"syncAvatar",t("Sync game avatar","同步游戏头像"),{360,244,28,28},!busy});
    return c;
}
std::vector<CanvasControl> AccountCanvasModel::menuControls() const {
    std::vector<CanvasControl> c;if(!menu_) return c;
    if(*menu_==Menu::disconnect) {
        c.push_back({"menu:cancel",t("Cancel","取消"),{menuRect_.x+menuRect_.width-80,menuRect_.y+menuRect_.height-40,28,28},true});
        c.push_back({"menu:disconnect",t("Disconnect","解除绑定"),{menuRect_.x+menuRect_.width-40,menuRect_.y+menuRect_.height-40,28,28},true});
        return c;
    }
    for(std::size_t i=0;i<choices_.size();++i) {
        const auto r=intersection({list_.x+2,list_.y+double(i)*rowPitch+2-offset_,list_.width-6,26},list_);
        if(r.width>0&&r.height>0) c.push_back({choices_[i].id,choices_[i].title,r,true});
    }
    return c;
}
std::vector<CanvasControl> AccountCanvasModel::accessibleActions() const {return menu_?menuControls():mainControls();}
std::optional<core::Rect> AccountCanvasModel::popoverBounds() const {if(!menu_) return std::nullopt;return menuRect_;}
bool AccountCanvasModel::update(const AccountPresentation& value) {
    if(presentation_==value) return false;
    const auto old=presentation_;presentation_=value;++revision_;
    if(old.region!=value.region||old.roles!=value.roles||value.busy()||(!value.isLinked&&menu_==Menu::disconnect)) dismissPopover(false);
    return true;
}
bool AccountCanvasModel::setLanguage(core::Language language) {if(language_==language) return false;language_=language;++revision_;return true;}
void AccountCanvasModel::setVisible(bool value) {active_=value;if(!value) dismissPopover(false);}
bool AccountCanvasModel::dismissPopover(bool animated) {
    if(!menu_) return false;
    retireAnimated_=animated&&active_;menu_.reset();choices_.clear();offset_=0;++menuRevision_;return true;
}
void AccountCanvasModel::show(Menu value) {
    dismissPopover(false);retireAnimated_=false;menu_=value;offset_=0;choices_.clear();
    const auto& p=presentation_;
    switch(value) {
        case Menu::region:
            for(const auto r:{AccountPresentation::RegionChoice::china,AccountPresentation::RegionChoice::global})
                choices_.push_back({"region:"+std::string(regionChoiceName(r)),regionChoiceTitle(r,language_),r==p.region});
            menuRect_={174,112,214,76};break;
        case Menu::header:
            for(const auto m:{AccountPresentation::HeaderMode::workMode,AccountPresentation::HeaderMode::endfield,AccountPresentation::HeaderMode::arknights,AccountPresentation::HeaderMode::hidden})
                choices_.push_back({"header:"+std::string(headerModeName(m)),headerModeTitle(m,language_),m==p.headerMode});
            menuRect_={148,190,240,138};break;
        case Menu::role:
            for(const auto& r:p.roles) choices_.push_back({"role:"+r.id,r.subtitle.empty()?r.title:r.title+" · "+r.subtitle,p.selectedRoleID&&r.id==*p.selectedRoleID});
            menuRect_={112,151,276,std::min(174.0,double(choices_.size())*rowPitch+14)};break;
        case Menu::disconnect: menuRect_={144,80,244,88};break;
    }
    list_={menuRect_.x+8,menuRect_.y+7,menuRect_.width-16,menuRect_.height-14};
    selected_=0;for(std::size_t i=0;i<choices_.size();++i) if(choices_[i].selected) {selected_=i;break;}
    ++menuRevision_;
}
void AccountCanvasModel::perform(std::string_view id) {
    const auto actions=accessibleActions();
    if(std::none_of(actions.begin(),actions.end(),[&](const auto& c){return c.id==id&&c.enabled;})) return;
    const auto emit=[&](AccountAction a){if(onAction) {auto callback=onAction;callback(a);}};
    using K=AccountAction::Kind;const auto& p=presentation_;const std::string key(id);
    if(key=="connect") {AccountAction a;a.kind=K::connect;a.region=p.region;emit(a);}
    else if(key=="refresh") {AccountAction a;a.kind=K::refresh;emit(a);}
    else if(key=="disconnect") show(Menu::disconnect);
    else if(key=="region") show(Menu::region);
    else if(key=="role") show(Menu::role);
    else if(key=="header") show(Menu::header);
    else if(key=="syncProfile") {AccountAction a;a.kind=K::setSyncProfile;a.enabled=!p.syncProfile;emit(a);}
    else if(key=="syncAvatar") {AccountAction a;a.kind=K::setSyncAvatar;a.enabled=!p.syncAvatar;emit(a);}
    else if(key=="menu:cancel") dismissPopover();
    else if(key=="menu:disconnect") {dismissPopover();AccountAction a;a.kind=K::disconnect;emit(a);}
    else if(key.rfind("region:",0)==0) {
        const auto value=key.substr(7)=="china"?AccountPresentation::RegionChoice::china:AccountPresentation::RegionChoice::global;
        dismissPopover();if(value!=p.region) {AccountAction a;a.kind=K::selectRegion;a.region=value;emit(a);}
    } else if(key.rfind("header:",0)==0) {
        const auto value=headerModeNamed(key.substr(7));
        dismissPopover();if(value&&*value!=p.headerMode) {AccountAction a;a.kind=K::selectHeaderMode;a.headerMode=*value;emit(a);}
    } else if(key.rfind("role:",0)==0) {
        const auto value=key.substr(5);dismissPopover();
        if(!p.selectedRoleID||value!=*p.selectedRoleID) {AccountAction a;a.kind=K::selectRole;a.roleID=value;emit(a);}
    }
}
bool AccountCanvasModel::mouseDown(Point point) {
    if(menu_) {
        const auto controls=menuControls();
        const auto hit=std::find_if(controls.rbegin(),controls.rend(),[&](const auto& c){return c.enabled&&inside(c.rect,point);});
        if(hit!=controls.rend()) perform(hit->id);
        else if(!inside(menuRect_,point)) dismissPopover();
        return true;
    }
    if(!inside(bounds,point)) return false;
    const auto controls=mainControls();
    const auto hit=std::find_if(controls.rbegin(),controls.rend(),[&](const auto& c){return c.enabled&&inside(c.rect,point);});
    if(hit!=controls.rend()) perform(hit->id);
    return true;
}
bool AccountCanvasModel::scroll(Point point,double delta) {
    if(!menu_) return false;
    if(!inside(menuRect_,point)||*menu_==Menu::disconnect||!std::isfinite(delta)) return true;
    const double next=std::min(std::max(0.0,double(choices_.size())*rowPitch-list_.height),std::max(0.0,offset_+std::min(250.0,std::max(-250.0,delta))));
    if(next==offset_) return true;
    offset_=next;return true;
}
void AccountCanvasModel::moveMenuSelection(int direction) {
    if(!menu_||choices_.empty()) return;
    selected_=static_cast<std::size_t>(std::min<long long>(static_cast<long long>(choices_.size())-1,std::max<long long>(0,static_cast<long long>(selected_)+direction)));
    const double top=double(selected_)*rowPitch;
    const double desired=std::min(top,std::max(offset_,top+28-list_.height));
    scroll({list_.x+list_.width/2,list_.y+list_.height/2},desired-offset_);
}
void AccountCanvasModel::activateMenuSelection() {
    if(!menu_) return;
    if(*menu_==Menu::disconnect) perform("menu:cancel");
    else if(selected_<choices_.size()) perform(choices_[selected_].id);
}
bool AccountCanvasModel::key(Key key) {
    if(!menu_) return false;
    switch(key) {
        case Key::escape: dismissPopover();break;
        case Key::down: moveMenuSelection(1);break;
        case Key::up: moveMenuSelection(-1);break;
        case Key::activate: activateMenuSelection();break;
    }
    return true;
}
CanvasPlan AccountCanvasModel::plan(const CanvasStyle& style) const {
    Painter a(style);const auto& p=presentation_;const bool busy=p.busy();const auto controls=mainControls();
    a.text("account/title","// "+t("Account","账户绑定"),{12,8,244,24},17);
    a.button("account/connect","connect",busy?t("Connecting…","正在连接…"):p.isLinked?t("Reconnect","重新连接"):t("Connect","绑定账户"),{276,7,112,28},!busy,true);
    a.fill("account/rule",{12,40,376,1},alpha(a.ink,.2));
    using S=AccountPresentation::Status;std::string status;
    switch(p.status) {
        case S::disconnected: status=t("Not connected","未绑定");break;
        case S::connecting: status=t("Connecting…","正在连接…");break;
        case S::connected: status=p.accountName.empty()?t("Connected","已绑定"):p.accountName;break;
        case S::refreshing: status=t("Syncing…","正在同步…");break;
        case S::failed: status=t("Connection unavailable","连接不可用");break;
    }
    a.text("account/status",status,{12,53,295,18},12);
    const bool refreshEnabled=p.isLinked&&!busy;
    a.button("account/refresh","refresh","",{321,47,28,28},refreshEnabled);
    a.shape("account/refresh/icon",refreshPath(),{326,52,18,18},alpha(a.ink,refreshEnabled?1:.35),{},1,1);
    a.button("account/disconnect","disconnect","×",{360,47,28,28},p.isLinked&&!busy);
    a.text("account/region/row",t("Region","地区"),{12,88,150,17},11);
    a.button("account/region","region",controls[3].label,{174,81,214,28},!busy);
    a.text("account/role/row",t("Game account","游戏账户"),{12,127,150,17},11);
    a.button("account/role","role",controls[4].label,{174,120,214,28},!p.roles.empty()&&!busy);
    a.text("account/header/row",t("Header display","顶部显示"),{12,166,150,17},11);
    a.button("account/header","header",controls[5].label,{174,159,214,28});
    a.text("account/syncProfile/row",t("Sync personal profile","同步个人名片"),{12,208,150,17},11);
    a.button("account/syncProfile","syncProfile",p.syncProfile?"✓":"",{360,203,28,28},!busy,p.syncProfile);
    a.text("account/syncAvatar/row",t("Sync game avatar","同步游戏头像"),{12,249,150,17},11);
    a.button("account/syncAvatar","syncAvatar",p.syncAvatar?"✓":"",{360,244,28,28},!busy,p.syncAvatar);
    if(!p.lastSync.empty()) a.text("account/lastSync",p.lastSync,{12,287,376,15},9,alpha(a.ink,.55));
    if(!p.statusMessage.empty()) a.text("account/message",p.statusMessage,{12,309,376,17},10,p.status==S::failed?orange:alpha(a.ink,.6));
    if(menu_) {
        a.menu=true;
        a.fill("account/menu/shadow",{menuRect_.x-3,menuRect_.y+4,menuRect_.width,menuRect_.height},{0,0,0,.30});
        a.fill("account/menu/plate",menuRect_,white(style.dark?.08:.92,.98),1,std::pair{.7,alpha(style.accent,.7)});
        if(*menu_==Menu::disconnect) {
            a.text("account/menu/question",t("Disconnect this account?","解除此账户绑定？"),{menuRect_.x+12,menuRect_.y+13,220,20},12);
            a.button("account/menu/cancel","menu:cancel","×",{menuRect_.x+menuRect_.width-80,menuRect_.y+menuRect_.height-40,28,28});
            a.button("account/menu/disconnect","menu:disconnect","✓",{menuRect_.x+menuRect_.width-40,menuRect_.y+menuRect_.height-40,28,28},true,true);
        } else {
            a.clip=list_;a.rows=true;
            for(std::size_t i=0;i<choices_.size();++i)
                a.button("account/menu/row/"+std::to_string(i),choices_[i].id,choices_[i].title,{list_.x+2,list_.y+double(i)*rowPitch+2,list_.width-6,26},true,false,choices_[i].selected,false);
            a.clip.reset();a.rows=false;
        }
    }
    a.plan.layers=Json::Object{{"bounds",rect(bounds)},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"allowsGroupOpacity",false},{"children",std::move(a.layers)}};
    return std::move(a.plan);
}
}
