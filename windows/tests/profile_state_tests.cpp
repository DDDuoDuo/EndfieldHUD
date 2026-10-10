#include "modules/profile_state.hpp"
#include "modules/profile_events.hpp"
#include "native/profile_text.hpp"
#include "core/data/json.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace m=endfield::modules;namespace n=endfield::native;using Json=ehud::data::Json;using endfield::core::Language;using endfield::core::Rect;
namespace {
std::size_t checks{};
void check(bool value,const std::string&why){++checks;if(!value)throw std::runtime_error(why);}
std::string show(const Json&j){return j.encode();}
double number(const Json&j){
    if(j.isString()){const auto s=j.string();if(s=="nan")return std::numeric_limits<double>::quiet_NaN();if(s=="inf")return std::numeric_limits<double>::infinity();if(s=="-inf")return -std::numeric_limits<double>::infinity();}
    return j.number();
}
Rect rect(const Json&j){const auto&a=j.array();return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
std::optional<std::string>optionalText(const Json&j,std::string_view key){if(!j.contains(key)||j[key].isNull())return std::nullopt;return j[key].string();}
Language language(std::string_view name){
    if(name=="english")return Language::english;if(name=="simplifiedChinese")return Language::simplifiedChinese;if(name=="traditionalChinese")return Language::traditionalChinese;
    if(name=="japanese")return Language::japanese;if(name=="korean")return Language::korean;throw std::runtime_error("Unknown oracle language");
}
// Raw Mac record, before UserProfile.normalizedEditableValues.
m::PersonalProfile macProfile(const Json&j,const m::ProfileDateRules&dates){
    m::PersonalProfile p;
    p.name=j["name"].string();p.tag=j["tag"].string();p.uid=j["uid"].string();p.awakeningDate=j["awakeningDate"].number();
    p.introduction=j.contains("introduction")?j["introduction"].string():"";
    p.gamePlayerID=optionalText(j,"gamePlayerID");p.playerIDOverride=optionalText(j,"playerIDOverride");
    p.hasManualAwakeningDate=j.contains("hasManualAwakeningDate")&&j["hasManualAwakeningDate"].boolean();
    p.showsBirthday=j.contains("showsBirthday")&&j["showsBirthday"].boolean();
    const auto day=dates.format(p.awakeningDate); // UserProfile.defaultBirthday in the same Gregorian zone
    p.birthdayMonth=j.contains("birthdayMonth")?static_cast<int>(j["birthdayMonth"].integer()):std::stoi(day.substr(day.size()-5,2));
    p.birthdayDay=j.contains("birthdayDay")?static_cast<int>(j["birthdayDay"].integer()):std::stoi(day.substr(day.size()-2));
    p.permissionLevel=static_cast<int>(j["permissionLevel"].integer());p.explorationLevel=static_cast<int>(j["explorationLevel"].integer());
    p.operatorsCount=j["operatorsCount"].integer();p.weaponsCount=j["weaponsCount"].integer();p.archivesCount=j["archivesCount"].integer();
    p.avatarFilename=optionalText(j,"avatarFilename");p.backgroundFilename=optionalText(j,"backgroundFilename");p.themeColorHex=optionalText(j,"themeColorHex");
    const auto real=[&](const char*key,double fallback){return j.contains(key)?j[key].number():fallback;};
    p.avatarZoom=real("avatarZoom",1);p.avatarOffsetX=real("avatarOffsetX",0);p.avatarOffsetY=real("avatarOffsetY",0);
    p.backgroundWidth=real("backgroundWidth",600);p.backgroundZoom=real("backgroundZoom",1);p.backgroundOffsetX=real("backgroundOffsetX",0);p.backgroundOffsetY=real("backgroundOffsetY",0);
    p.thumbnailZoom=real("thumbnailZoom",1);p.thumbnailOffsetX=real("thumbnailOffsetX",0);p.thumbnailOffsetY=real("thumbnailOffsetY",0);
    p.accumulatedWorkSeconds=j["accumulatedWorkSeconds"].number();
    return p;
}
std::string profileDifference(const m::PersonalProfile&p,const Json&j){
    std::ostringstream out;
    const auto text=[&](const char*key,const std::string&value){if(!j.contains(key)||j[key].string()!=value)out<<key<<": '"<<value<<"' != "<<show(j[key])<<"; ";};
    const auto optional=[&](const char*key,const std::optional<std::string>&value){if(optionalText(j,key)!=value)out<<key<<": '"<<value.value_or("<nil>")<<"' != "<<show(j[key])<<"; ";};
    const auto real=[&](const char*key,double value){if(!j.contains(key)||j[key].number()!=value){out.precision(17);out<<key<<": "<<value<<" != "<<show(j[key])<<"; ";}};
    const auto whole=[&](const char*key,std::int64_t value){if(!j.contains(key)||j[key].integer()!=value)out<<key<<": "<<value<<" != "<<show(j[key])<<"; ";};
    const auto flag=[&](const char*key,bool value){if(!j.contains(key)||j[key].boolean()!=value)out<<key<<": "<<value<<" != "<<show(j[key])<<"; ";};
    text("name",p.name);text("tag",p.tag);text("introduction",p.introduction);text("uid",p.uid);real("awakeningDate",p.awakeningDate);
    optional("gamePlayerID",p.gamePlayerID);optional("playerIDOverride",p.playerIDOverride);flag("hasManualAwakeningDate",p.hasManualAwakeningDate);flag("showsBirthday",p.showsBirthday);
    whole("birthdayMonth",p.birthdayMonth);whole("birthdayDay",p.birthdayDay);whole("permissionLevel",p.permissionLevel);whole("explorationLevel",p.explorationLevel);
    whole("operatorsCount",p.operatorsCount);whole("weaponsCount",p.weaponsCount);whole("archivesCount",p.archivesCount);
    optional("avatarFilename",p.avatarFilename);optional("backgroundFilename",p.backgroundFilename);optional("themeColorHex",p.themeColorHex);
    real("avatarZoom",p.avatarZoom);real("avatarOffsetX",p.avatarOffsetX);real("avatarOffsetY",p.avatarOffsetY);real("backgroundWidth",p.backgroundWidth);
    real("backgroundZoom",p.backgroundZoom);real("backgroundOffsetX",p.backgroundOffsetX);real("backgroundOffsetY",p.backgroundOffsetY);real("thumbnailZoom",p.thumbnailZoom);
    real("thumbnailOffsetX",p.thumbnailOffsetX);real("thumbnailOffsetY",p.thumbnailOffsetY);real("accumulatedWorkSeconds",p.accumulatedWorkSeconds);
    return out.str();
}
struct Fixture {
    m::ProfileTextRules text=n::nativeProfileTextRules();
    m::ProfileDateRules dates;
    double live{3600};
    std::size_t commits{};
    m::ProfileState state;
    Fixture(const m::PersonalProfile&baseline,std::u16string zone,Language l=Language::english,double seconds=3600)
        :dates(n::nativeProfileDateRules(std::move(zone))),live(seconds),state(baseline,text,dates,persistence(),l){}
    m::ProfilePersistence persistence(){
        m::ProfilePersistence io;
        io.commit=[this](const m::PersonalProfile&p){++commits;return p;};
        io.workSeconds=[this]{return live;};
        return io;
    }
};
std::string requestKind(m::ProfileRequest::Kind k){switch(k){case m::ProfileRequest::Kind::edit:return "edit";case m::ProfileRequest::Kind::chooseAvatar:return "chooseAvatar";case m::ProfileRequest::Kind::chooseBackground:return "chooseBackground";case m::ProfileRequest::Kind::chooseColor:return "chooseColor";}return {};}
void compareState(const m::ProfileState&s,std::vector<m::ProfileRequest>&requests,const Json&state,const std::string&where){
    const auto&actions=state["actions"].array();
    check(s.actions().size()==actions.size(),where+": action count "+std::to_string(s.actions().size())+" != "+std::to_string(actions.size()));
    for(std::size_t i=0;i<actions.size();++i){
        const auto&a=s.actions()[i];
        check(a.id==actions[i]["id"].string(),where+": action id "+a.id+" != "+actions[i]["id"].string());
        check(a.label==actions[i]["label"].string(),where+": action label '"+a.label+"' != '"+actions[i]["label"].string()+"'");
        check(a.rect==rect(actions[i]["rect"]),where+": action rect "+a.id);
    }
    const auto&sliders=state["sliders"].array();
    check(s.sliders().size()==sliders.size(),where+": slider count");
    for(std::size_t i=0;i<sliders.size();++i){
        const auto&v=s.sliders()[i];const auto&j=sliders[i];
        check(m::profileFieldID(v.field)==j["field"].string()&&v.label==j["label"].string()&&v.rect==rect(j["rect"]),where+": slider identity "+j["field"].string());
        check(v.value==j["value"].number()&&v.minimum==j["minimum"].number()&&v.maximum==j["maximum"].number()&&v.step==j["step"].number(),where+": slider value "+j["field"].string());
        check(v.valueDescription==j["valueDescription"].string(),where+": slider description '"+v.valueDescription+"' != '"+j["valueDescription"].string()+"'");
    }
    const auto bounds=s.popoverBounds();
    check(bounds.has_value()!=state["popover"].isNull(),where+": popover presence");
    if(bounds)check(*bounds==rect(state["popover"]),where+": popover bounds");
    check(s.popover().has_value()==state["open"].boolean()&&s.textHidden()==state["hidden"].boolean()&&s.dragging()==state["dragging"].boolean(),where+": open/hidden/dragging");
    check(s.accessibilityStatus()==state["status"].string(),where+": status '"+s.accessibilityStatus()+"' != '"+state["status"].string()+"'");
    const auto stored=profileDifference(s.profile(),state["profile"]);check(stored.empty(),where+": stored profile "+stored);
    const auto preview=profileDifference(s.preview(),state["preview"]);check(preview.empty(),where+": preview profile "+preview);
    check(s.shownHours()==state["hours"].string(),where+": hours '"+s.shownHours()+"' != '"+state["hours"].string()+"'");
    const auto&expected=state["requests"].array();
    check(requests.size()==expected.size(),where+": request count "+std::to_string(requests.size())+" != "+std::to_string(expected.size()));
    for(std::size_t i=0;i<expected.size();++i){
        const auto&r=requests[i];const auto&j=expected[i];
        check(requestKind(r.kind)==j["kind"].string(),where+": request kind");
        if(r.kind==m::ProfileRequest::Kind::edit)check(r.field&&m::profileFieldID(*r.field)==j["field"].string()&&r.rect==rect(j["rect"])&&r.text==j["text"].string(),where+": edit request '"+r.text+"' "+show(j));
        if(r.kind==m::ProfileRequest::Kind::chooseColor)for(std::size_t c=0;c<3;++c)check(std::abs(r.rgb[c]-j["rgb"].array()[c].number())<1e-6,where+": initial custom color");
    }
    requests.clear();
    std::string editable;for(std::size_t f=0;f<21;++f)if(s.canEdit(static_cast<m::ProfileField>(f)))editable+=std::string(m::profileFieldID(static_cast<m::ProfileField>(f)))+",";
    std::string expectedEditable;for(const auto&f:state["canEdit"].array())expectedEditable+=f.string()+",";
    check(editable==expectedEditable,where+": editable fields "+editable);
}
void drain(m::ProfileState&s,std::vector<m::ProfileRequest>&requests){while(auto r=s.takeRequest())requests.push_back(std::move(*r));}
m::PersonalProfile external(m::PersonalProfile p,const Json&fields){
    if(fields.contains("name"))p.name=fields["name"].string();
    if(fields.contains("tag"))p.tag=fields["tag"].string();
    if(fields.contains("gamePlayerID")){p.gamePlayerID=fields["gamePlayerID"].string();p.playerIDOverride.reset();}
    if(fields.contains("backgroundWidth"))p.backgroundWidth=fields["backgroundWidth"].number();
    if(fields.contains("permissionLevel"))p.permissionLevel=static_cast<int>(fields["permissionLevel"].integer());
    return p;
}
void scenario(const Json&fixture){
    Fixture f(macProfile(fixture["baseline"],n::nativeProfileDateRules(u"Asia/Shanghai")),u"Asia/Shanghai");
    std::vector<m::ProfileRequest>requests;std::size_t index{};
    compareState(f.state,requests,fixture["scenario"].array().front()["state"],"initial");
    for(const auto&row:fixture["scenario"].array()){
        const auto op=row["op"].string();const auto where="step "+std::to_string(index++)+" "+op+" "+show(row["field"].isNull()?row["id"]:row["field"])+" "+(row.contains("text")?show(row["text"]):"");
        std::optional<bool>result;auto&s=f.state;
        const auto point=[&]{return endfield::core::Point{number(row["x"]),number(row["y"])};};
        if(op=="sample"){}
        else if(op=="perform")s.perform(row["id"].string());
        else if(op=="mouseDown")result=s.mouseDown(point());
        else if(op=="mouseDragged")s.mouseDragged(point());
        else if(op=="mouseUp")s.mouseUp();
        else if(op=="setSlider")result=s.setSlider(*m::profileField(row["field"].string()),number(row["value"]));
        else if(op=="nudge")result=s.nudgeSlider(number(row["direction"]));
        else if(op=="dismiss")s.dismissPopover();
        else if(op=="commit")result=s.commit(*m::profileField(row["field"].string()),row["text"].string());
        else if(op=="lock")s.setSyncLocked(row["value"].boolean());
        else if(op=="live"){f.live=number(row["value"]);s.refreshWork();}
        else if(op=="setWork")result=s.setWorkSeconds(number(row["value"]));
        else if(op=="customColor")s.setCustomColor(number(row["r"]),number(row["g"]),number(row["b"]));
        else if(op=="external")s.refresh(external(s.profile(),row["fields"]),s.syncLocked());
        else throw std::runtime_error("Unknown oracle op "+op);
        if(result)check(row["result"].isBool()&&*result==row["result"].boolean(),where+": result");
        else check(row["result"].isNull(),where+": void source operation");
        drain(s,requests);compareState(s,requests,row["state"],where);
    }
    check(index>=200,"Original canvas scenario covers editing, sliders, popovers, locks and hours");
}
void labels(const Json&fixture){
    const auto baseline=macProfile(fixture["baseline"],n::nativeProfileDateRules(u"Asia/Shanghai"));
    for(const auto&entry:fixture["labels"].array()){
        const auto l=language(entry["language"].string());const auto&states=entry["states"];const auto name=entry["language"].string();
        for(const auto&[id,title]:entry["titles"].object())check(m::profileFieldTitle(*m::profileField(id),l)==title.string(),name+": field title "+id);
        Fixture f(baseline,u"Asia/Shanghai",l);auto&s=f.state;std::vector<m::ProfileRequest>requests;
        compareState(s,requests,states["default"],name+" default");
        const std::vector<std::pair<std::string,std::vector<std::string>>>sequence{{"identity",{"profile:menu"}},{"portrait",{"profile:menu","profile:portraitMenu"}},
            {"background",{"profile:backgroundMenu"}},{"theme",{"profile:backgroundMenu","profile:themeMenu"}},{"hidden",{"profile:visibility"}},
            {"birthday",{"profile:visibility","profile:toggleDateLabel"}}};
        for(const auto&[state,steps]:sequence){for(const auto&id:steps)s.perform(id);drain(s,requests);compareState(s,requests,states[state],name+" "+state);s.dismissPopover();}
        s.setSyncLocked(true);s.perform("profile:menu");compareState(s,requests,states["lockedIdentity"],name+" locked identity");
        s.commit(m::ProfileField::tag,"a b");compareState(s,requests,states["tagError"],name+" tag error");
        s.commit(m::ProfileField::birthday,"x");compareState(s,requests,states["birthdayError"],name+" birthday error");
        s.setSyncLocked(false);
        s.commit(m::ProfileField::awakeningDate,"x");compareState(s,requests,states["awakeningError"],name+" awakening error");
        s.commit(m::ProfileField::name,"");compareState(s,requests,states["nameError"],name+" name error");
        s.commit(m::ProfileField::permissionLevel,"4");compareState(s,requests,states["cleared"],name+" cleared error");
    }
}
void decoding(const Json&fixture){
    const auto text=n::nativeProfileTextRules();const auto dates=n::nativeProfileDateRules(u"Asia/Shanghai");
    for(const auto&row:fixture["decode"].array()){
        const auto name=row["name"].string();auto raw=macProfile(row["input"],dates);
        if(row.contains("output")){
            const auto normalized=m::normalizedProfile(raw,text);m::validatePersonalProfile(normalized,text);
            const auto difference=profileDifference(normalized,row["output"]);check(difference.empty(),"Decode "+name+": "+difference);
        }else{
            bool rejected{};try{m::validatePersonalProfile(m::normalizedProfile(raw,text),text);}catch(const m::ProfileError&){rejected=true;}
            check(rejected,"Decode "+name+" is rejected like invalidRecord");
        }
    }
}
void dates(const Json&fixture){
    const auto baseline=macProfile(fixture["baseline"],n::nativeProfileDateRules(u"Asia/Shanghai"));
    for(const auto&entry:fixture["dates"].array()){
        const auto zoneName=entry["zone"].string();const std::u16string zone(zoneName.begin(),zoneName.end());
        Fixture f(baseline,zone);std::vector<m::ProfileRequest>requests;
        for(const auto&row:entry["rows"].array()){
            if(row.contains("input")){
                const auto input=row["input"].string();const bool ok=f.state.commit(m::ProfileField::awakeningDate,input);
                check(ok==row["ok"].boolean(),zoneName+" "+input+": Gregorian acceptance");
                if(ok){
                    std::ostringstream detail;detail.precision(17);detail<<f.state.profile().awakeningDate<<" vs "<<row["seconds"].number();
                    check(f.state.profile().awakeningDate==row["seconds"].number(),zoneName+" "+input+": local noon "+detail.str());
                    check(f.state.value(m::ProfileField::awakeningDate)==row["formatted"].string(),zoneName+" "+input+": yyyy/MM/dd");
                }
            }else check(f.dates.format(row["seconds"].number())==row["formattedOnly"].string(),zoneName+": format "+show(row["seconds"])+" = "+f.dates.format(row["seconds"].number()));
        }
    }
}
void hours(const Json&fixture){
    const auto baseline=macProfile(fixture["baseline"],n::nativeProfileDateRules(u"Asia/Shanghai"));
    for(const auto&row:fixture["hours"].array()){
        auto p=baseline;p.accumulatedWorkSeconds=row["stored"].number();
        Fixture f(p,u"Asia/Shanghai",Language::english,number(row["live"]));
        check(f.state.shownHours()==row["text"].string(),"Work hours stored "+std::to_string(p.accumulatedWorkSeconds)+" -> "+f.state.shownHours()+" expected "+row["text"].string());
    }
}
void staticGeometry(const Json&fixture){
    std::size_t i{};
    for(const auto&field:fixture["fields"].array()){
        const auto f=static_cast<m::ProfileField>(i++);
        check(m::profileFieldID(f)==field["id"].string()&&m::profileFieldRect(f)==rect(field["rect"]),"Field identity/rect "+field["id"].string());
        check(m::profileGeometryField(f)==field["geometry"].boolean()&&m::profileZoomField(f)==field["zoom"].boolean()&&m::profileNumericField(f)==field["numeric"].boolean()&&m::profileDateField(f)==field["date"].boolean(),"Field kind "+field["id"].string());
        const auto limit=m::profileTextLimit(f);check(limit.has_value()!=field["textLimit"].isNull()&&(!limit||static_cast<std::int64_t>(*limit)==field["textLimit"].integer()),"Grapheme limit "+field["id"].string());
    }
    check(i==21,"Every source field");
    const auto presets=m::profileThemePresets();const auto&expected=fixture["presets"].array();check(presets.size()==expected.size(),"Preset count");
    for(std::size_t k=0;k<presets.size();++k)check(presets[k]==expected[k].string(),"Preset accent");
}
void cropEvents(const Json&fixture){
    m::ProfileCropRecorder recorder;std::size_t rows{};
    for(const auto&row:fixture["cropEvents"].array()){
        const auto target=recorder.receive(number(row["background"]),number(row["thumbnail"]));
        check(target.has_value()!=row["event"].isNull(),"profileCropChanged presence "+std::to_string(rows));
        if(target)check(*target==row["event"].string()&&row["kind"].string()=="profileCropChanged","profileCropChanged target "+std::to_string(rows));
        ++rows;
    }
    check(rows>=10,"Source crop recorder cases");
}
void native(){
    const auto text=n::nativeProfileTextRules();
    check(text.characters("é̂")==1&&text.characters("👍🏽🇯🇵")==2&&text.characters("가각")==2&&text.characters("각")==1,"Extended grapheme clusters, not UTF16 or bytes");
    check(text.prefix("😀😀😀",2)=="😀😀"&&m::profileEditorText(m::ProfileField::tag,"#😀12345678901",text)=="😀123456789","Editor keeps Character limits and drops one # Character");
    check(m::profileEditorText(m::ProfileField::tag,"#́x",text)=="#́x","A # with a combining mark is not the source # prefix");
    check(m::profileEditorText(m::ProfileField::introduction,std::string(151,'x'),text).size()==150,"Biography limit");
    check(m::swiftInteger("+7")==7&&!m::swiftInteger("+-7")&&!m::swiftInteger("9223372036854775808")&&m::swiftInteger("-9223372036854775808")==std::numeric_limits<std::int64_t>::min(),"Swift Int grammar");
    check(m::swiftDouble("0x1p3")==8.&&std::isinf(*m::swiftDouble("1e400"))&&m::swiftDouble("1e-400")==0.&&!m::swiftDouble("0x")&&!m::swiftDouble(" 1")&&std::isnan(*m::swiftDouble("nan(abc)")),"Swift Double grammar");
    const auto version=n::profileUnicodeVersion();std::cout<<"ICU Unicode "<<int(version[0])<<"."<<int(version[1])<<"\n";
    // No owner: persistence absent shows the source storage message.
    m::PersonalProfile p;p.uid="1000000000";p.awakeningDate=0;
    m::ProfileState unavailable(p,text,n::nativeProfileDateRules(u"UTC"));
    check(!unavailable.commit(m::ProfileField::name,"x")&&unavailable.error()==m::profileFailureMessage(m::ProfileFailure::unavailable,Language::english),"Unavailable profile storage");
    check(!unavailable.setThemeColor("FFFFFF")&&!unavailable.restoreImage(m::ProfileImageKind::avatar),"Theme/reset silently need storage");
    // A rejected owner commit keeps the committed profile and shows the localized persistence message.
    m::ProfilePersistence failing;failing.commit=[](const m::PersonalProfile&)->m::PersonalProfile{throw ehud::data::StoreError(ehud::data::StoreErrorCode::changedOnDisk,"changed");};
    m::ProfileState rejected(p,text,n::nativeProfileDateRules(u"UTC"),failing,Language::simplifiedChinese);
    check(!rejected.commit(m::ProfileField::name,"Changed")&&rejected.profile().name=="Endministrator"&&rejected.error()==m::profileFailureMessage(m::ProfileFailure::changedOnDisk,Language::simplifiedChinese),"Changed-on-disk record is preserved");
    // Work refresh follows a 30 s repeating source timer on the caller's clock.
    double live{};m::ProfilePersistence io;io.commit=[](const m::PersonalProfile&v){return v;};io.workSeconds=[&]{return live;};
    m::ProfileState work(p,text,n::nativeProfileDateRules(u"UTC"),io);
    check(!work.nextWorkRefresh(),"Inactive page has no work deadline");work.activate(100);check(work.nextWorkRefresh()==130.,"First refresh 30 s after activation");
    live=36;check(!work.wake(129.9)&&work.shownHours()=="0.00","No early refresh");check(work.wake(130)&&work.shownHours()=="0.01"&&work.nextWorkRefresh()==160.,"Refresh at the deadline");
    live=72;check(work.wake(225)&&work.nextWorkRefresh()==250.,"Late wake keeps the 30 s phase");
    const auto before=work.workRevision();check(!work.wake(250)&&work.workRevision()==before,"Unchanged caption keeps its revision");
    work.deactivate();check(!work.nextWorkRefresh(),"Deactivated page releases its deadline");
    check(work.setWorkSeconds(500)&&!work.setWorkSeconds(499)&&!work.setWorkSeconds(std::numeric_limits<double>::infinity())&&work.profile().accumulatedWorkSeconds==500&&!work.error(),"Monotonic lifetime hours never become a canvas error");
    // A time-zone change replaces the date snapshot: the same instant is shown
    // in the new zone and later input parses as that zone's local noon.
    auto zoned=p;zoned.awakeningDate=(1767225600.-978307200.)-3600; // 2025-12-31 23:00 UTC
    m::ProfileState zone(zoned,text,n::nativeProfileDateRules(u"UTC"),io);const auto revision=zone.revision();
    check(zone.value(m::ProfileField::awakeningDate)=="2025/12/31","Awakening day in UTC");
    zone.setDateRules(n::nativeProfileDateRules(u"Asia/Tokyo"));
    check(zone.value(m::ProfileField::awakeningDate)=="2026/01/01"&&zone.revision()>revision&&zone.profile().awakeningDate==zoned.awakeningDate,"Zone change re-renders the stored instant without rewriting it");
    check(zone.commit(m::ProfileField::awakeningDate,"2026/01/02")&&zone.profile().awakeningDate==(1767312000.-978307200.)+3*3600,"Input parses as local noon in the new zone");
    bool rejected2{};try{zone.setDateRules({});}catch(const std::invalid_argument&){rejected2=true;}check(rejected2,"Date rules are required");
}
}
int main(int argc,char**argv){
    try{
        check(argc==2,"Pass profile-state-source.json");
        std::ifstream in(argv[1],std::ios::binary);check(bool(in),"Open original profile oracle");
        const std::string bytes((std::istreambuf_iterator<char>(in)),{});check(bytes.size()<4*1024*1024,"Bounded oracle");
        const auto fixture=Json::parse(bytes,4*1024*1024);
        check(fixture["provenance"]["modifications"].array().empty(),"Oracle compiled the unchanged Mac sources");
        staticGeometry(fixture);scenario(fixture);labels(fixture);decoding(fixture);dates(fixture);hours(fixture);cropEvents(fixture);native();
        std::cout<<"Personal Profile state: "<<checks<<" source checks passed\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Personal Profile state failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
