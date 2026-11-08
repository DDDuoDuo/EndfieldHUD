#include "modules/profile_state.hpp"
#include "modules/profile_events.hpp"
#include "native/profile_text.hpp"
#include "core/data/data_store.hpp"
#include "core/data/json.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

namespace m=endfield::modules;namespace n=endfield::native;using Json=ehud::data::Json;using endfield::core::Language;
namespace {
std::size_t checks{};
void check(bool value,const std::string&why){++checks;if(!value)throw std::runtime_error(why);}
std::string show(const Json&j){return j.encode();}
double number(const Json&j){
    if(j.isString()){const auto s=j.string();if(s=="nan")return std::numeric_limits<double>::quiet_NaN();if(s=="inf")return std::numeric_limits<double>::infinity();if(s=="-inf")return -std::numeric_limits<double>::infinity();}
    return j.number();
}
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
        :dates(n::nativeProfileDateRules(std::move(zone))),live(seconds),state(baseline,text,dates,persistence()){(void)l;}
    m::ProfilePersistence persistence(){
        m::ProfilePersistence io;
        io.commit=[this](const m::PersonalProfile&p){++commits;return p;};
        return io;
    }
};
// PersonalProfileField.title in every language (unchanged in 1.3.0).
void labels(const Json&fixture){
    for(const auto&entry:fixture["labels"].array()){
        const auto l=language(entry["language"].string());const auto name=entry["language"].string();
        for(const auto&[id,title]:entry["titles"].object())check(m::profileFieldTitle(*m::profileField(id),l)==title.string(),name+": field title "+id);
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
        Fixture f(baseline,zone);
        for(const auto&row:entry["rows"].array()){
            if(row.contains("input")){
                // parseAwakeningDate (unchanged in 1.3.0) and the save mapping.
                const auto input=row["input"].string();const auto parsed=m::profileParseAwakeningDate(input,f.dates);const bool ok=parsed.has_value();
                check(ok==row["ok"].boolean(),zoneName+" "+input+": Gregorian acceptance");
                if(ok){
                    f.state.update([&](m::PersonalProfile&p){p.awakeningDate=*parsed;p.hasManualAwakeningDate=true;});
                    std::ostringstream detail;detail.precision(17);detail<<f.state.profile().awakeningDate<<" vs "<<row["seconds"].number();
                    check(f.state.profile().awakeningDate==row["seconds"].number(),zoneName+" "+input+": local noon "+detail.str());
                    check(m::profileValue(m::ProfileField::awakeningDate,f.state.profile(),f.dates)==row["formatted"].string(),zoneName+" "+input+": yyyy/MM/dd");
                }
            }else check(f.dates.format(row["seconds"].number())==row["formattedOnly"].string(),zoneName+": format "+show(row["seconds"])+" = "+f.dates.format(row["seconds"].number()));
        }
    }
}
void staticGeometry(const Json&fixture){
    std::size_t i{};
    for(const auto&field:fixture["fields"].array()){
        const auto f=static_cast<m::ProfileField>(i++);
        // PersonalProfileField.isNumeric (1.3.0) covers the levels and counters only;
        // v1.2.0's fixture also called the geometry values numeric.
        check(m::profileFieldID(f)==field["id"].string()&&m::profileGeometryField(f)==field["geometry"].boolean()&&(m::profileGeometryField(f)?!m::profileNumericField(f):m::profileNumericField(f)==field["numeric"].boolean()),"Field identity/kind "+field["id"].string());
    }
    check(i==21,"Every source field");
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
    check(text.prefix("😀😀😀",2)=="😀😀","Character prefix");
    // Display > HUD elements text keeps 24 Characters through this same rule.
    std::string flags;for(unsigned k=0;k<30;++k)flags+="🇯🇵";
    check(ehud::data::OverlayLayout::cleanText(" é̂ \n"+flags,text.prefix)=="é̂ "+flags.substr(0,22*8),"HUD elements text is cut at 24 extended grapheme clusters");
    // The same segmentation without Profile's payload bound (the design
    // library's CharacterRules): 100,001 clusters in 300,004 bytes.
    std::string longText;for(unsigned k=0;k<100000;++k)longText+="e\xcc\x81";longText+="👍🏽";
    bool bounded{};try{(void)text.characters(longText);}catch(const std::invalid_argument&){bounded=true;}
    check(bounded&&text.characters(std::string(32768,'x'))==32768,"Profile keeps its 32 KiB payload bound");
    check(n::graphemeCount(longText)==100001&&n::graphemePrefix(longText,99999).size()==99999*3&&n::graphemePrefix(longText,100000)==std::string_view(longText).substr(0,300000)&&n::graphemePrefix(longText,200000)==longText&&n::graphemeCount("")==0,
        "Grapheme count and prefix work on text of any length");
    check(m::swiftInteger("+7")==7&&!m::swiftInteger("+-7")&&!m::swiftInteger("9223372036854775808")&&m::swiftInteger("-9223372036854775808")==std::numeric_limits<std::int64_t>::min(),"Swift Int grammar");
    check(m::swiftDouble("0x1p3")==8.&&std::isinf(*m::swiftDouble("1e400"))&&m::swiftDouble("1e-400")==0.&&!m::swiftDouble("0x")&&!m::swiftDouble(" 1")&&std::isnan(*m::swiftDouble("nan(abc)")),"Swift Double grammar");
    const auto version=n::profileUnicodeVersion();std::cout<<"ICU Unicode "<<int(version[0])<<"."<<int(version[1])<<"\n";
    // No owner (the Mac canvas's nil store): every update is refused as unavailable.
    m::PersonalProfile p;p.uid="1000000000";p.awakeningDate=0;
    m::ProfileState unavailable(p,text,n::nativeProfileDateRules(u"UTC"));
    const auto refuses=[](auto&&f){try{f();}catch(const m::ProfileError&e){return e.code()==m::ProfileFailure::unavailable;}return false;};
    check(!unavailable.persistent()&&refuses([&]{unavailable.update([](m::PersonalProfile&v){v.name="x";});})&&refuses([&]{unavailable.removeImage(m::ProfileImageKind::avatar);}),
          "Unavailable profile storage");
    check(m::profileErrorText(m::ProfileError(m::ProfileFailure::unavailable),Language::simplifiedChinese)=="个人名片存储不可用","The store's localized message");
    // A rejected owner commit keeps the committed profile; its text is the localized store message.
    m::ProfilePersistence failing;failing.commit=[](const m::PersonalProfile&)->m::PersonalProfile{throw ehud::data::StoreError(ehud::data::StoreErrorCode::changedOnDisk,"changed");};
    m::ProfileState rejected(p,text,n::nativeProfileDateRules(u"UTC"),failing);
    std::string message;
    try{rejected.update([](m::PersonalProfile&v){v.name="Changed";});}catch(const std::exception&e){message=m::profileErrorText(e,Language::simplifiedChinese);}
    check(rejected.profile().name=="Endministrator"&&message==m::profileFailureMessage(m::ProfileFailure::changedOnDisk,Language::simplifiedChinese),"Changed-on-disk record is preserved");
    // UserProfileStore: an identical record is not written; observers run per adopted change.
    std::size_t commits{},notices{};m::ProfilePersistence io;io.commit=[&](const m::PersonalProfile&v){++commits;return v;};
    m::ProfileState store(p,text,n::nativeProfileDateRules(u"UTC"),io);const auto id=store.observe([&]{++notices;});
    check(!store.update([](m::PersonalProfile&){})&&commits==0&&notices==0,"An unchanged record is not saved");
    check(store.update([](m::PersonalProfile&v){v.backgroundWidth=777;v.backgroundZoom=3;v.thumbnailOffsetX=.5;})&&commits==1&&notices==1,"A change saves once and notifies");
    store.removeImage(m::ProfileImageKind::background);
    check(store.profile().backgroundWidth==600&&store.profile().backgroundZoom==1&&store.profile().thumbnailOffsetX==0&&notices==2,"removeImage(background) resets the art and thumbnail values");
    check(store.setWorkSeconds(500)&&!store.setWorkSeconds(499)&&!store.setWorkSeconds(std::numeric_limits<double>::infinity())&&store.profile().accumulatedWorkSeconds==500,"Monotonic lifetime hours");
    store.setSyncLocked(ehud::data::ProfileSyncFields::all());check(notices==4&&store.syncLocked()==ehud::data::ProfileSyncFields::all(),"A lock change notifies");
    store.removeObserver(id);store.setSyncLocked({});check(notices==4,"A removed observer hears nothing");
    // A time-zone change replaces the date snapshot: the same instant is shown
    // in the new zone and later input parses as that zone's local noon.
    auto zoned=p;zoned.awakeningDate=(1767225600.-978307200.)-3600; // 2025-12-31 23:00 UTC
    m::ProfileState zone(zoned,text,n::nativeProfileDateRules(u"UTC"),io);const auto revision=zone.revision();
    check(m::profileValue(m::ProfileField::awakeningDate,zone.profile(),zone.dates())=="2025/12/31","Awakening day in UTC");
    zone.setDateRules(n::nativeProfileDateRules(u"Asia/Tokyo"));
    check(m::profileValue(m::ProfileField::awakeningDate,zone.profile(),zone.dates())=="2026/01/01"&&zone.revision()>revision&&zone.profile().awakeningDate==zoned.awakeningDate,"Zone change re-renders the stored instant without rewriting it");
    check(m::profileParseAwakeningDate("2026/01/02",zone.dates())==(1767312000.-978307200.)+3*3600,"Input parses as local noon in the new zone");
    bool rejected2{};try{zone.setDateRules({});}catch(const std::invalid_argument&){rejected2=true;}check(rejected2,"Date rules are required");
}
// Issue #17 (Mac UserProfileStore a2777d8..60a61ae): the account through an
// injected source only (never the real account), shown values, Recording
// mode, the saved flags, Use account info, observers and the 60 s throttle.
void systemAccount(){
    const auto text=n::nativeProfileTextRules();const auto dates=n::nativeProfileDateRules(u"UTC");
    // SystemAccountIdentity's name and UID rules (UserProfile.cappedName).
    const auto clean=[&](std::string name,std::string uid){return m::normalizedSystemAccount({std::move(name),std::move(uid),std::nullopt},text);};
    check(clean("  Ada\u0007 Love\r\nlace\u200d ","1001")==ehud::data::SystemAccount{"Ada Lovelace","1001",std::nullopt},"Control characters and line breaks are dropped, the name trimmed");
    check(clean("abcdefghijklmnopqrstuvwxyz","").name==std::optional<std::string>("abcdefghijklmnopqrst")&&!clean("x","").uid,"The name is capped at 20 characters; an empty UID is none");
    check(!clean(" \t\u3000 ","12a").name&&!clean("x","12a").uid&&!clean("x","\xef\xbc\x91").uid,"A blank name is none; a UID must be ASCII digits");
    check(m::profileCappedName(" \n😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀 ",text)==[]{std::string s;for(int k=0;k<20;++k)s+="😀";return s;}(),"One cap for typed and account names");
    check(m::profileTextLimit(m::ProfileField::name)==ehud::data::Profile::nameLimit,"The editor's limit is the same cap");

    m::PersonalProfile p;p.uid="1000000000";p.awakeningDate=0;
    std::size_t commits{},notices{},reads{};std::vector<m::ProfileAccountSource::Done>pending;
    m::ProfilePersistence io;io.commit=[&](const m::PersonalProfile&v){++commits;check(!v.systemAccount.value,"The persistence never sees the account");return v;};
    m::ProfileAccountSource source;source.read=[&](m::ProfileAccountSource::Done done){++reads;pending.push_back(std::move(done));};
    const auto answer=[&](ehud::data::SystemAccount a){check(!pending.empty(),"A read is pending");auto done=std::move(pending.front());pending.erase(pending.begin());done(std::move(a));};
    // No source (tests, previews, hidden coverage): nothing is read.
    m::ProfileState none(p,text,dates,io);none.refreshSystemAccount(0);
    check(!none.systemAccount()&&none.profile().displayedName()=="Endministrator"&&none.profile().displayedUID()=="1000000000"&&!none.profile().avatarKey(),"Without a source the stored defaults show");
    m::ProfileState store(p,text,dates,io,source);store.observe([&]{++notices;});
    store.refreshSystemAccount(100);store.refreshSystemAccount(101);
    check(reads==1&&pending.size()==1&&notices==0,"One read at a time; nothing shows before it answers");
    answer({" Ada Lovelace ","1001","system-1"});
    check(notices==1&&commits==0&&store.profile().displayedName()=="Ada Lovelace"&&store.profile().displayedUID()=="1001"&&store.profile().avatarKey()==std::optional<std::string>("system-1"),
          "The account's name, UID and picture show; nothing is saved");
    check(m::profileValue(m::ProfileField::name,store.profile(),dates)=="Ada Lovelace","Personal ID's name value is the shown name");
    // The Mac re-reads the name and UID at the open at 101 although its picture
    // read is pending: here that open reads once the first read answers.
    check(reads==2&&pending.size()==1,"An open during a read reads again after it answers");
    answer({"Ada Lovelace","1001","system-1"});
    check(reads==2&&pending.empty()&&notices==1,"An unchanged account notifies no one, and no further read follows");
    // The throttle: HUD opens within 60 s of the last read do not read.
    store.refreshSystemAccount(150,60);check(reads==2,"A read 49 s after the last is skipped");
    store.refreshSystemAccount(162,60);check(reads==3,"A read 61 s after the last runs");
    store.refreshSystemAccount(230,60);store.refreshSystemAccount(240);
    check(reads==3&&pending.size()==1,"Opens during a read wait for it");
    answer({"Ada Lovelace","1001","system-1"});
    check(reads==4&&pending.size()==1&&notices==1,"Several opens during a read read once more");
    answer({"Ada Lovelace","1001","system-1"});
    check(reads==4&&pending.empty()&&notices==1,"An unchanged account notifies no one");
    store.refreshSystemAccount(300);answer({"Ada Lovelace","1001","system-2"});
    check(notices==2&&store.profile().avatarKey()==std::optional<std::string>("system-2"),"A new picture is a new key and a notice");
    // Recording mode hides only the account's values.
    store.setHidesSystemAccount(true);
    check(notices==3&&store.profile().displayedName()=="Endministrator"&&store.profile().displayedUID()=="1000000000"&&!store.profile().avatarKey()&&store.systemAccount().has_value(),
          "Recording mode shows the stored defaults in place of the account");
    store.setHidesSystemAccount(true);check(notices==3,"Setting the same switch again notifies no one");
    store.update([](m::PersonalProfile&v){v.playerIDOverride="42";});
    check(store.profile().displayedUID()=="42"&&!store.profile().systemAccount.value,"A chosen value still shows in Recording mode");
    store.setHidesSystemAccount(false);
    check(store.profile().displayedName()=="Ada Lovelace"&&store.profile().displayedUID()=="42","Leaving Recording mode shows the account again under the chosen UID");
    // A typed name stops following (the canvas sets autoName off with it).
    store.update([](m::PersonalProfile&v){v.name="Kal'tsit";v.autoName=false;});
    check(store.profile().displayedName()=="Kal'tsit"&&!store.profile().autoName,"A typed name wins and clears autoName");
    // Restore default picture: the drawn portrait, not the account's.
    store.imageImported(m::ProfileImageKind::avatar,"00000000-0000-4000-8000-000000000001.image");
    check(store.profile().avatarKey()==std::optional<std::string>("00000000-0000-4000-8000-000000000001.image"),"A chosen picture wins");
    store.removeImage(m::ProfileImageKind::avatar);
    check(!store.profile().autoAvatar&&!store.profile().avatarKey(),"Restore default picture sets autoAvatar off: the drawn portrait");
    // Use Windows account info: name, Player ID and picture back to the account.
    const auto before=commits;
    check(store.useSystemAccount(),"Use account info saves");
    const auto&v=store.profile();
    check(commits==before+1&&v.name=="Endministrator"&&v.autoName&&!v.playerIDOverride&&!v.avatarFilename&&v.autoAvatar&&v.avatarZoom==1&&v.avatarOffsetX==0&&v.avatarOffsetY==0,
          "The default name follows again, no override, no chosen picture, autoAvatar on");
    check(v.displayedName()=="Ada Lovelace"&&v.displayedUID()=="1001"&&v.avatarKey()==std::optional<std::string>("system-2"),"The card shows the account again");
    check(!store.useSystemAccount()&&commits==before+1,"Nothing left to change: nothing is saved");
    // Fields a game sync owns are left to it.
    store.update([](m::PersonalProfile&q){q.name="Typed";q.autoName=false;q.playerIDOverride="7";});
    store.setSyncLocked(ehud::data::ProfileSyncFields{}.with(ehud::data::ProfileSyncField::name).with(ehud::data::ProfileSyncField::uid));
    store.useSystemAccount(false,false);
    check(store.profile().name=="Typed"&&store.profile().playerIDOverride==std::optional<std::string>("7")&&store.profile().autoAvatar,"A synced name and Player ID stay");
    store.setSyncLocked({});
    // A failed or empty read falls back to the stored defaults.
    store.update([](m::PersonalProfile&q){q.name="Endministrator";q.autoName=true;q.playerIDOverride.reset();});
    store.refreshSystemAccount(1000);answer({});
    check(!store.systemAccount()&&store.profile().displayedName()=="Endministrator"&&store.profile().displayedUID()=="1000000000"&&!store.profile().avatarKey(),"An empty reading shows the stored defaults");
    store.refreshSystemAccount(2000);answer({"\xff","1001",std::nullopt});
    check(store.systemAccount()==std::nullopt,"An unreadable name is no account");
    // A late answer after the store is gone is dropped.
    {
        std::vector<m::ProfileAccountSource::Done>late;m::ProfileAccountSource lateSource;lateSource.read=[&](m::ProfileAccountSource::Done d){late.push_back(std::move(d));};
        {m::ProfileState gone(p,text,dates,io,lateSource);gone.refreshSystemAccount(0);}
        check(late.size()==1,"The read started");late.front()({"Ada","1",std::nullopt});
    }
}
}
int main(int argc,char**argv){
    try{
        check(argc==2,"Pass profile-state-source.json");
        std::ifstream in(argv[1],std::ios::binary);check(bool(in),"Open original profile oracle");
        const std::string bytes((std::istreambuf_iterator<char>(in)),{});check(bytes.size()<4*1024*1024,"Bounded oracle");
        const auto fixture=Json::parse(bytes,4*1024*1024);
        check(fixture["provenance"]["modifications"].array().empty(),"Oracle compiled the unchanged Mac sources");
        // The v1.2.0 canvas scenario (actions, pop-overs, inline errors) is gone with the Mac's
        // (tests/profile_canvas_tests.cpp compares the 1.3.0 page); the record rules are unchanged.
        staticGeometry(fixture);labels(fixture);decoding(fixture);dates(fixture);cropEvents(fixture);native();systemAccount();
        std::cout<<"Personal Profile state: "<<checks<<" source checks passed\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Personal Profile state failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
