#include "modules/profile_presentation.hpp"
#include "native/profile_text.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace m=endfield::modules;namespace n=endfield::native;using Json=ehud::data::Json;using endfield::core::Language;using endfield::core::Rect;
namespace {
std::size_t checks{};std::string context;
void check(bool v,const std::string&why){++checks;if(!v)throw std::runtime_error(context+": "+why);}
void close(double a,double b,const std::string&why){if(std::abs(a-b)>2e-6){std::ostringstream s;s.precision(12);s<<why<<" actual "<<a<<" expected "<<b;check(false,s.str());}++checks;}
struct Leaf{const Json*node{};double x{},y{},opacity{1};bool highlight{};};
// Absolute CALayer leaf origins: position - anchor * size - bounds origin.
void flatten(const Json&node,std::vector<Leaf>&out,double x=0,double y=0,double opacity=1,bool highlight=false){
    const auto&b=node["bounds"].array();const auto&p=node["position"].array();const auto&a=node["anchorPoint"].array();
    x+=p[0].number()-a[0].number()*b[2].number()-b[0].number();y+=p[1].number()-a[1].number()*b[3].number()-b[1].number();
    const double own=node.contains("opacity")?node["opacity"].number():1;
    const auto kind=node["kind"].string();highlight=highlight||(node["class"].isString()&&node["class"].string()=="HUDControlHighlightLayer");
    if(kind=="text"||kind=="shape"||kind=="gradient"||(node.contains("backgroundColor")&&!node["backgroundColor"].isNull())||(node.contains("contents")&&!node["contents"].isNull()))
        out.push_back({&node,x,y,opacity*own,highlight});
    for(const auto&c:node["children"].array())flatten(c,out,x,y,opacity*own,highlight);
}
std::vector<double>components(const Json&c){std::vector<double>v;if(c.isNull())return v;const auto&a=c.isArray()?c.array():c["sRGB"].array();for(const auto&x:a)v.push_back(x.number());return v;}
void color(const Json&actual,const Json&expected,const std::string&why){
    const auto a=components(actual),e=components(expected);check(a.size()==e.size(),why+" presence");
    for(std::size_t k=0;k<a.size();++k)close(a[k],e[k],why);
}
void compareLeaf(const Leaf&a,const Leaf&e){
    const auto&x=*a.node;const auto&y=*e.node;const auto kind=y["kind"].string();
    check(x["kind"].string()==kind,"leaf kind "+x["kind"].string()+" vs "+kind+" ("+x["id"].string()+")");
    if(kind=="shape"){
        const auto&s=x["shape"];const auto&t=y["shape"];
        color(s["fillColor"],t["fillColor"],"shape fill "+x["id"].string());color(s["strokeColor"],t["strokeColor"],"shape stroke "+x["id"].string());
        close(s["lineWidth"].number(),t["lineWidth"].number(),"shape line width");
        check(s["fillRule"].string()==t["fillRule"].string(),"shape fill rule "+x["id"].string());
        const auto&u=s["path"].array();const auto&v=t["path"].array();check(u.size()==v.size(),"path element count "+x["id"].string());
        for(std::size_t i=0;i<u.size();++i){
            check(u[i]["op"].string()==v[i]["op"].string(),"path element "+std::to_string(i)+" "+x["id"].string());
            const auto&pu=u[i]["points"].array();const auto&pv=v[i]["points"].array();check(pu.size()==pv.size(),"path arity");
            for(std::size_t k=0;k<pu.size();++k){close(a.x+pu[k].array()[0].number(),e.x+pv[k].array()[0].number(),"path x "+x["id"].string());close(a.y+pu[k].array()[1].number(),e.y+pv[k].array()[1].number(),"path y "+x["id"].string());}
        }
        return;
    }
    close(a.x,e.x,"leaf x "+x["id"].string());close(a.y,e.y,"leaf y "+x["id"].string());
    close(x["bounds"].array()[2].number(),y["bounds"].array()[2].number(),"leaf width "+x["id"].string());close(x["bounds"].array()[3].number(),y["bounds"].array()[3].number(),"leaf height "+x["id"].string());
    if(kind=="text"){
        const auto&s=x["text"];const auto&t=y["text"];
        check(s["string"].string()==t["string"].string(),"text '"+s["string"].string()+"' vs '"+t["string"].string()+"'");
        close(s["fontSize"].number(),t["fontSize"].number(),"font size");
        check(s["font"]["postScriptName"].string()==t["font"]["postScriptName"].string()&&s["font"]["symbolicTraits"].number()==t["font"]["symbolicTraits"].number(),"font weight "+s["string"].string());
        check(s["alignment"].string()==t["alignment"].string()&&s["wrapped"].boolean()==t["wrapped"].boolean()&&s["truncation"].string()==t["truncation"].string(),"text layout "+s["string"].string());
        color(s["foregroundColor"],t["foregroundColor"],"text color "+s["string"].string());
        close(x["contentsScale"].number(),y["contentsScale"].number(),"text oversampling");
        return;
    }
    color(x.contains("backgroundColor")?x["backgroundColor"]:Json{},y["backgroundColor"],"background color "+x["id"].string());
    close(x.contains("cornerRadius")?x["cornerRadius"].number():0,y["cornerRadius"].number(),"corner radius");
    close(x.contains("borderWidth")?x["borderWidth"].number():0,y["borderWidth"].number(),"border width");
    if(y["borderWidth"].number()>0)color(x["borderColor"],y["borderColor"],"border color");
    check((x.contains("contents")&&!x["contents"].isNull())==!y["contents"].isNull(),"intrinsic contents presence "+x["id"].string());
    check((x.contains("masksToBounds")&&x["masksToBounds"].boolean())==y["masksToBounds"].boolean(),"clip "+x["id"].string());
}
void gradient(const m::ProfileGradient&g,const Json&e,const std::string&why){
    check(g.colors.size()==e["colors"].array().size()&&g.locations.size()==e["locations"].array().size(),why+" stops");
    for(std::size_t k=0;k<g.colors.size();++k){for(std::size_t c=0;c<4;++c)close(g.colors[k][c],e["colors"].array()[k].array()[c].number(),why+" color");close(g.locations[k],e["locations"].array()[k].number(),why+" location");}
    close(g.start.x,e["startPoint"].array()[0].number(),why+" start");close(g.start.y,e["startPoint"].array()[1].number(),why+" start");
    close(g.end.x,e["endPoint"].array()[0].number(),why+" end");close(g.end.y,e["endPoint"].array()[1].number(),why+" end");
}
std::optional<std::string>text(const Json&j,std::string_view k){return j.contains(k)&&!j[k].isNull()?std::optional(j[k].string()):std::nullopt;}
m::PersonalProfile profile(const Json&j){
    m::PersonalProfile p;p.name=j["name"].string();p.tag=j["tag"].string();p.uid=j["uid"].string();p.introduction=j["introduction"].string();p.awakeningDate=j["awakeningDate"].number();
    p.gamePlayerID=text(j,"gamePlayerID");p.playerIDOverride=text(j,"playerIDOverride");p.hasManualAwakeningDate=j["hasManualAwakeningDate"].boolean();p.showsBirthday=j["showsBirthday"].boolean();
    p.birthdayMonth=static_cast<int>(j["birthdayMonth"].integer());p.birthdayDay=static_cast<int>(j["birthdayDay"].integer());p.permissionLevel=static_cast<int>(j["permissionLevel"].integer());p.explorationLevel=static_cast<int>(j["explorationLevel"].integer());
    p.operatorsCount=j["operatorsCount"].integer();p.weaponsCount=j["weaponsCount"].integer();p.archivesCount=j["archivesCount"].integer();
    p.avatarFilename=text(j,"avatarFilename");p.backgroundFilename=text(j,"backgroundFilename");p.themeColorHex=text(j,"themeColorHex");
    p.avatarZoom=j["avatarZoom"].number();p.avatarOffsetX=j["avatarOffsetX"].number();p.avatarOffsetY=j["avatarOffsetY"].number();
    p.backgroundWidth=j["backgroundWidth"].number();p.backgroundZoom=j["backgroundZoom"].number();p.backgroundOffsetX=j["backgroundOffsetX"].number();p.backgroundOffsetY=j["backgroundOffsetY"].number();
    p.thumbnailZoom=j["thumbnailZoom"].number();p.thumbnailOffsetX=j["thumbnailOffsetX"].number();p.thumbnailOffsetY=j["thumbnailOffsetY"].number();p.accumulatedWorkSeconds=j["accumulatedWorkSeconds"].number();
    return p;
}
const Json*firstContents(const Json&node,std::string_view name){
    if(node["name"].isString()&&node["name"].string()==name)return &node;
    for(const auto&c:node["children"].array())if(const auto*found=firstContents(c,name))return found;
    return nullptr;
}
void state(const Json&row){
    const auto name=row["name"].string();context="state "+name;
    const auto language=row["language"].string()=="simplifiedChinese"?Language::simplifiedChinese:Language::english;
    m::ProfilePersistence io;double live=3600;io.commit=[](const m::PersonalProfile&p){return p;};io.workSeconds=[&]{return live;};
    m::ProfileState s(profile(row["profile"]),n::nativeProfileTextRules(),n::nativeProfileDateRules(u"Asia/Shanghai"),io,language);
    const auto perform=[&](std::initializer_list<const char*>ids){for(const auto*id:ids)s.perform(id);};
    if(name=="identity")perform({"profile:menu"});
    else if(name=="lockedIdentity"){s.setSyncLocked(true);perform({"profile:menu"});}
    else if(name=="locked")s.setSyncLocked(true);
    else if(name=="portrait")perform({"profile:menu","profile:portraitMenu"});
    else if(name=="background"||name=="chineseBackground")perform({"profile:backgroundMenu"});
    else if(name=="theme")perform({"profile:backgroundMenu","profile:themeMenu"});
    else if(name=="hidden"||name=="imagesHidden")perform({"profile:visibility"});
    else if(name=="error")s.commit(m::ProfileField::tag,"a b");
    else if(name=="imagesBackground")perform({"profile:backgroundMenu"});
    check(s.popover().has_value()==!row["popover"].isNull()&&s.textHidden()==row["hidden"].boolean()&&s.accessibilityStatus()==row["status"].string()&&s.shownHours()==row["hours"].string(),"source state reconstruction");
    if(!row["popover"].isNull()){const auto&r=row["popover"].array();check(*s.popoverBounds()==Rect{r[0].number(),r[1].number(),r[2].number(),r[3].number()},"popover bounds");}
    m::ProfileContents contents;m::ProfileAppearance appearance;appearance.dark=row["dark"].boolean();
    contents.frame=Json::Object{{"asset","synthetic-frame"},{"sha256",std::string(64,'0')}};
    if(const auto*photo=firstContents(row["layer"],"portrait.image");photo&&!(*photo)["contents"].isNull())contents.avatar=Json::Object{{"asset","synthetic-avatar"},{"sha256",std::string(64,'1')}};
    const auto&background=row["background"]["children"].array()[0];
    if(!background["contents"].isNull()){contents.background=true;const auto&size=background["contents"]["size"].array();contents.backgroundPixels={size[0].number(),size[1].number()};}
    const auto art=m::prepareProfileArtwork(s,appearance,contents);
    // Source canvas tree: fields (with optional contrast first), toolbar (+ error), popover.
    std::vector<Leaf>expected;flatten(row["layer"],expected);
    const auto&fieldsRoot=row["layer"]["children"].array()[0];
    close(art.fieldsOpacity,fieldsRoot["opacity"].number(),"text visibility");
    std::size_t index{};
    if(art.contrast){
        const auto&g=*expected.at(index++).node;check(g["kind"].string()=="gradient"&&g["name"].string()=="profile.textContrast","contrast leaf order");
        gradient(art.contrast->fill,g["gradient"],"contrast");gradient(art.contrast->mask,g["mask"]["gradient"],"contrast mask");
        check(art.contrast->frame==Rect{0,0,g["bounds"].array()[2].number(),g["bounds"].array()[3].number()},"contrast frame");
    }else check(expected.empty()||(*expected.front().node)["kind"].string()!="gradient","no contrast without a backdrop");
    std::vector<Leaf>actual;
    for(const auto*part:{&art.fields,&art.toolbar,&art.popover})flatten(part->layers,actual);
    check(actual.size()+index==expected.size(),"leaf count "+std::to_string(actual.size()+index)+" vs "+std::to_string(expected.size()));
    std::size_t highlightLeaves{};
    for(std::size_t i=0;i<actual.size();++i){
        const auto&e=expected[i+index];compareLeaf(actual[i],e);
        if(e.highlight){
            // Rest opacity of HUDControlHighlightLayer tint/rim pieces.
            const auto&id=(*actual[i].node)["id"].string();const auto h=std::find_if(art.highlights.begin(),art.highlights.end(),[&](const auto&v){return v.tint==id||v.rim==id;});
            check(h!=art.highlights.end(),"highlight surface "+id);const bool rim=h->rim==id;
            close(rim&&h->framed&&h->enabled?.28:0,e.opacity,"highlight rest opacity "+id);++highlightLeaves;
        }
    }
    check(highlightLeaves==2*art.highlights.size(),"every highlight has a tint and a rim");
    // profile.background
    const auto&b=art.backdrop;const auto&bs=background["bounds"].array();const auto&bp=background["position"].array();
    check(b.frame==Rect{bp[0].number()-bs[2].number()/2,bp[1].number()-bs[3].number()/2,bs[2].number(),bs[3].number()},"background frame");
    const auto&cr=background["contentsRect"].array();
    close(b.contentsRect.x,cr[0].number(),"background crop x");close(b.contentsRect.y,cr[1].number(),"background crop y");close(b.contentsRect.width,cr[2].number(),"background crop w");close(b.contentsRect.height,cr[3].number(),"background crop h");
    check(b.photo==!background["contents"].isNull(),"background photo presence");
    gradient(b.horizontal,background["mask"]["gradient"],"horizontal fade");gradient(b.vertical,background["mask"]["mask"]["gradient"],"vertical fade");
    const auto&shade=background["children"].array()[0];gradient(b.shade,shade["gradient"],"shade");close(b.shadeOpacity,shade["opacity"].number(),"shade visibility");
}
void hits(){
    context="hits";m::ProfilePersistence io;io.commit=[](const m::PersonalProfile&p){return p;};
    m::PersonalProfile p;p.uid="1000000000";m::ProfileState s(p,n::nativeProfileTextRules(),n::nativeProfileDateRules(u"UTC"),io);
    auto art=m::prepareProfileArtwork(s,{});
    const auto at=[&](double x,double y){const auto h=m::profileHighlightAt(art,{x,y});return h?art.highlights[*h].rect:Rect{};};
    check(at(30,125)==Rect{20,116,19,19},"menu ellipse");check(at(20.5,116.5)==Rect{},"ellipse corner is outside");
    check(at(300,200)==Rect{257,157,132,101},"introduction");check(at(200,150)==Rect{180,145,57,25},"smallest control wins inside its band");
    check(at(243.5,294.5)==Rect{},"cut corner excluded");check(at(300,300)==Rect{243,294,109,24},"card theme");
    s.perform("profile:visibility");art=m::prepareProfileArtwork(s,{});check(at(300,200)==Rect{},"hidden text has no highlight");check(at(376,306)==Rect{364,294,25,24},"eye stays live");
    s.perform("profile:visibility");s.perform("profile:backgroundMenu");art=m::prepareProfileArtwork(s,{});
    check(at(300,300)==Rect{},"popover suppresses underlying feedback");check(at(150,92)==Rect{138,84,240,17},"slider highlight");
    // refreshFromStore update roles.
    context="update roles";const auto base=s.profile();auto next=base;
    const auto roles=[&](const m::PersonalProfile&after){return m::profileUpdateAnimation(s,base,after);};
    next.name="Other";check(roles(next).roles==std::vector<std::string>{"name"},"name+tag share one layer");
    next=base;next.permissionLevel=3;next.weaponsCount=1;check((roles(next).roles==std::vector<std::string>{"value:permissionLevel","value:weaponsCount"}),"level and counter layers");
    next=base;next.showsBirthday=true;check((roles(next).roles==std::vector<std::string>{"dateCaption","value:birthday"}),"date caption toggle");
    next=base;next.awakeningDate+=86400*3;check(roles(next).roles==std::vector<std::string>{"value:awakeningDate"},"visible date");
    next.showsBirthday=true;next.awakeningDate=base.awakeningDate;next.birthdayMonth=base.birthdayMonth==1?2:1;check((roles(next).roles==std::vector<std::string>{"value:birthday","dateCaption"}),"hidden date has no layer");
    next=base;next.accumulatedWorkSeconds=99;next.backgroundWidth=700;next.playerIDOverride="X";check(roles(next).empty(),"work hours, crop geometry and UID have no update layer");
    next=base;next.avatarZoom=2;check(roles(next).roles==std::vector<std::string>{"portrait"},"portrait crop");
    next=base;next.themeColorHex="FFFFFF";check(roles(next).fields&&roles(next).toolbar&&!roles(next).backdrop,"theme animates artwork and toolbar");
    next=base;next.backgroundFilename="00000000-0000-4000-8000-000000000002.png";check(roles(next).fields&&roles(next).backdrop&&!roles(next).toolbar,"image animates backdrop and artwork");
    context="bitmaps";const auto art2=m::prepareProfileArtwork(s,{});
    const auto fade=m::profileFadeBitmap(art2.backdrop.horizontal,&art2.backdrop.vertical,100,100);
    check(fade.rgba[(50*100+50)*4+3]==255&&fade.rgba[3]==1&&fade.rgba[(50*100+4)*4+3]==143&&fade.rgba[(4*100+50)*4+3]==143,"fade product of CA locations");
    const auto shade=m::profileGradientBitmap(m::ProfileGradient{{{0,0,0,.7},{0,0,0,.38},{0,0,0,.03}},{0,.6,1},{0,.5},{1,.5}},10,1);
    check(shade.rgba[3]==std::lround((.7-(.7-.38)*(.05/.6))*255)&&shade.rgba[9*4+3]==std::lround((.38-(.38-.03)*(.35/.4))*255),"axial shade samples pixel centers");
}
}
int main(int argc,char**argv){
    try{
        check(argc==2,"Pass profile-presentation-source.json");
        std::ifstream in(argv[1],std::ios::binary);check(bool(in),"Open presentation oracle");
        const std::string bytes((std::istreambuf_iterator<char>(in)),{});const auto fixture=Json::parse(bytes,8*1024*1024);
        check(fixture["provenance"]["modifications"].array().empty(),"Unchanged Mac sources");
        std::size_t states{};for(const auto&row:fixture["states"].array()){state(row);++states;}
        check(states>=19,"Every exported canvas state");hits();
        std::cout<<"Personal Profile presentation: "<<checks<<" source checks passed\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Personal Profile presentation failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
