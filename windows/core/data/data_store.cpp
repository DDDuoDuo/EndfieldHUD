#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <limits>
#include <random>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace ehud::data {
namespace {
constexpr std::size_t jsonLimit=4*1024*1024;
void require(bool condition,const char* message) {if(!condition) throw StoreError(StoreErrorCode::invalid,message);}
Json parseRecord(std::string_view bytes) {
    try {return Json::parse(bytes,jsonLimit);} catch(const std::exception&) {throw StoreError(StoreErrorCode::invalid,"Saved JSON record is invalid; the original was preserved");}
}
std::string encodeRecord(const Json& value) {
    try {return value.encode(jsonLimit);} catch(const std::exception&) {throw StoreError(StoreErrorCode::invalid,"Data cannot be encoded safely");}
}
void version(const Json& value) {
    require(value.isObject() && value["version"].isNumber(),"Missing saved record version");
    const auto number=value["version"].integer();
    if(number>1) throw StoreError(StoreErrorCode::newerVersion,"Saved data requires a newer app version");
    require(number==1,"Unsupported saved record version");
}
bool between(double value,double low,double high) {return std::isfinite(value)&&value>=low&&value<=high;}
bool clean(std::string_view value,std::size_t maximum,bool newline=false) {
    if(value.size()>maximum || !Json::validUtf8(value)) return false;
    return std::none_of(value.begin(),value.end(),[newline](unsigned char c){return c==0 || (!newline&&(c<32||c==127));});
}
bool hex(std::string_view value) {return value.size()==6 && std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');});}
bool imageName(std::string_view name) {
    return (name.size()==40 && name.substr(36)==".png" && validUUID(name.substr(0,36))) ||
        (name.size()==42 && name.substr(36)==".image" && validUUID(name.substr(0,36)));
}
// UTF-8 scalars for the Mac text normalization. Invalid bytes become U+FFFD
// of one byte (never whitespace); validateProfile rejects them afterwards.
struct Scalar {char32_t value;std::size_t offset,size;};
std::vector<Scalar> scalars(std::string_view text) {
    std::vector<Scalar> out;out.reserve(text.size());
    for(std::size_t i=0;i<text.size();) {
        const auto lead=static_cast<unsigned char>(text[i]);
        const std::size_t size=lead<0x80?1:lead>=0xF0&&lead<0xF8?4:lead>=0xE0?3:lead>=0xC0?2:0;
        char32_t value=size==1?lead:size?static_cast<char32_t>(lead&(0x7F>>size)):0xFFFD;bool valid=size!=0&&i+size<=text.size();
        for(std::size_t k=1;valid&&k<size;++k) {const auto next=static_cast<unsigned char>(text[i+k]);valid=(next&0xC0)==0x80;value=(value<<6)|(next&0x3F);}
        if(!valid) {out.push_back({0xFFFD,i,1});++i;continue;}
        out.push_back({value,i,size});i+=size;
    }
    return out;
}
// Measured on macOS 15.5 (Foundation): CharacterSet.whitespaces is tab, U+0020,
// U+00A0, U+1680, U+2000...U+200B, U+202F, U+205F and U+3000; Character.isNewline
// is U+000A...U+000D, U+0085, U+2028 and U+2029; whitespacesAndNewlines is
// their union.
bool swiftWhitespace(char32_t c) noexcept {return c==0x09||c==0x20||c==0xA0||c==0x1680||(c>=0x2000&&c<=0x200B)||c==0x202F||c==0x205F||c==0x3000;}
bool swiftNewline(char32_t c) noexcept {return (c>=0x0A&&c<=0x0D)||c==0x85||c==0x2028||c==0x2029;}
template <class Predicate> std::string trimmed(std::string_view text,Predicate trim) {
    const auto all=scalars(text);std::size_t first=0,last=all.size();
    while(first<last&&trim(all[first].value)) ++first;
    while(last>first&&trim(all[last-1].value)) --last;
    if(first==last) return {};
    return std::string(text.substr(all[first].offset,all[last-1].offset+all[last-1].size-all[first].offset));
}
std::string withoutNewlines(std::string_view text) {
    std::string out;out.reserve(text.size());
    for(const auto& scalar:scalars(text)) if(!swiftNewline(scalar.value)) out.append(text.substr(scalar.offset,scalar.size));
    return out;
}
// UserProfile.normalizedThemeHex: trim, drop every '#', uppercase, then six
// hex digits or nil. U+FB00 (LATIN SMALL LIGATURE FF) is the only non-ASCII
// scalar whose Unicode uppercase consists of hex digits ("FF").
std::optional<std::string> normalizedThemeHex(std::string_view input) {
    const auto value=trimmed(input,[](char32_t c){return swiftWhitespace(c)||swiftNewline(c);});std::string out;
    for(const auto& scalar:scalars(value)) {
        const auto c=scalar.value;
        if(c=='#') continue;
        if(c==0xFB00) {out+="FF";continue;}
        if(c>0x7F) return std::nullopt;
        out.push_back(c>='a'&&c<='z'?static_cast<char>(c-'a'+'A'):static_cast<char>(c));
        if(out.size()>6) return std::nullopt;
    }
    if(!hex(out)) return std::nullopt;
    return out;
}
double bound(double value,double low,double high,double fallback) noexcept {return std::isfinite(value)?std::min(high,std::max(low,value)):fallback;}
std::tm localDate(double epoch) {
    std::tm date{};
    const double unixSeconds=epoch+978307200.0;
    if(!std::isfinite(unixSeconds)||unixSeconds<-62135596800.0||unixSeconds>253402300799.0) {date.tm_mon=0;date.tm_mday=1;return date;}
    const auto time=static_cast<std::time_t>(unixSeconds);
#ifdef _WIN32
    if(localtime_s(&date,&time)!=0) {date.tm_mon=0;date.tm_mday=1;}
#else
    if(!localtime_r(&time,&date)) {date.tm_mon=0;date.tm_mday=1;}
#endif
    return date;
}
void optional(Json& object,const char* key,const std::optional<std::string>& value) {
    if(value) object[key]=*value;else object.erase(key);
}
void number(Json& object,const char* key,double value) {
    // Preserve unchanged imported doubles' original token as well as its value.
    if(!object[key].isNumber() || object[key].number()!=value) object[key]=value;
}
void integer(Json& object,const char* key,std::int64_t value) {
    if(!object[key].isNumber() || object[key].integer()!=value) object[key]=value;
}
Profile decodeProfile(const Json& object) {
    require(object.isObject(),"Missing personal profile");Profile profile;profile.originalFields=object;
    // Swift decodeIfPresent: an absent key and an explicit null both mean "use the default".
    auto present=[&](const char* key) {return object.contains(key)&&!object[key].isNull();};
    auto text=[&](const char* key) {require(object[key].isString(),"Missing profile text field");return object[key].string();};
    auto real=[&](const char* key) {require(object[key].isNumber(),"Missing profile numeric field");return object[key].number();};
    auto whole=[&](const char* key) {require(object[key].isNumber(),"Missing profile integer field");return object[key].integer();};
    auto opt=[&](const char* key)->std::optional<std::string> {if(object[key].isNull()) return {};return text(key);};
    auto flag=[&](const char* key,bool fallback=false) {if(!present(key)) return fallback;require(object[key].isBool(),"Invalid profile Boolean");return object[key].boolean();};
    auto fallbackReal=[&](const char* key,double fallback) {return present(key)?real(key):fallback;};
    // Any Swift Int; normalizedEditableProfile clamps it like the Mac store
    // instead of rejecting an out-of-range level or annual birthday.
    auto clampedInt=[&](const char* key) {return static_cast<int>(std::clamp<std::int64_t>(whole(key),std::numeric_limits<int>::min(),std::numeric_limits<int>::max()));};
    profile.name=text("name");profile.tag=text("tag");profile.uid=text("uid");profile.awakeningDate=real("awakeningDate");
    profile.gamePlayerID=opt("gamePlayerID");profile.playerIDOverride=opt("playerIDOverride");
    profile.hasManualAwakeningDate=flag("hasManualAwakeningDate");profile.showsBirthday=flag("showsBirthday");
    profile.permissionLevel=clampedInt("permissionLevel");profile.explorationLevel=clampedInt("explorationLevel");
    profile.operatorsCount=whole("operatorsCount");profile.weaponsCount=whole("weaponsCount");profile.archivesCount=whole("archivesCount");
    profile.avatarFilename=opt("avatarFilename");profile.backgroundFilename=opt("backgroundFilename");profile.themeColorHex=opt("themeColorHex");
    profile.accumulatedWorkSeconds=real("accumulatedWorkSeconds");
    profile.introduction=present("introduction")?text("introduction"):"";
    const auto birthday=localDate(profile.awakeningDate);
    profile.birthdayMonth=present("birthdayMonth")?clampedInt("birthdayMonth"):birthday.tm_mon+1;
    profile.birthdayDay=present("birthdayDay")?clampedInt("birthdayDay"):birthday.tm_mday;
    profile.avatarZoom=fallbackReal("avatarZoom",1);profile.avatarOffsetX=fallbackReal("avatarOffsetX",0);profile.avatarOffsetY=fallbackReal("avatarOffsetY",0);
    profile.backgroundWidth=fallbackReal("backgroundWidth",600);profile.backgroundZoom=fallbackReal("backgroundZoom",1);
    profile.backgroundOffsetX=fallbackReal("backgroundOffsetX",0);profile.backgroundOffsetY=fallbackReal("backgroundOffsetY",0);
    profile.thumbnailZoom=fallbackReal("thumbnailZoom",1);profile.thumbnailOffsetX=fallbackReal("thumbnailOffsetX",0);profile.thumbnailOffsetY=fallbackReal("thumbnailOffsetY",0);
    return profile;
}
void validateProfile(const Profile& profile) {
    // Editing controls enforce grapheme limits; byte guards deliberately do not
    // reject valid imported names made of multi-scalar emoji/combining clusters.
    require(!profile.name.empty() && clean(profile.name,4096),"Invalid profile name");
    require(!profile.tag.empty() && clean(profile.tag,4096) && profile.tag.find_first_of(" #\t\r\n")==std::string::npos,"Invalid profile tag");
    require(clean(profile.introduction,32768,true),"Invalid profile introduction");
    require(profile.uid.size()==10 && std::all_of(profile.uid.begin(),profile.uid.end(),[](char c){return c>='0'&&c<='9';}),"Invalid original profile UID");
    for(const auto& id:{profile.gamePlayerID,profile.playerIDOverride}) if(id) require(!id->empty()&&clean(*id,1024),"Invalid game profile identity");
    static constexpr int days[]{31,29,31,30,31,30,31,31,30,31,30,31};
    require(profile.birthdayMonth>=1&&profile.birthdayMonth<=12&&profile.birthdayDay>=1&&profile.birthdayDay<=days[profile.birthdayMonth-1],"Invalid annual birthday");
    require(profile.permissionLevel>=1&&profile.permissionLevel<=60&&profile.explorationLevel>=1&&profile.explorationLevel<=7,"Invalid profile levels");
    require(profile.operatorsCount>=0&&profile.weaponsCount>=0&&profile.archivesCount>=0,"Invalid profile counts");
    require(std::isfinite(profile.awakeningDate)&&std::isfinite(profile.accumulatedWorkSeconds)&&profile.accumulatedWorkSeconds>=0,"Invalid profile time");
    require(between(profile.avatarZoom,1,20)&&between(profile.avatarOffsetX,-1,1)&&between(profile.avatarOffsetY,-1,1)&&
        between(profile.backgroundWidth,400,900)&&between(profile.backgroundZoom,1,20)&&between(profile.backgroundOffsetX,-400,400)&&between(profile.backgroundOffsetY,-250,250)&&
        between(profile.thumbnailZoom,1,20)&&between(profile.thumbnailOffsetX,-1,1)&&between(profile.thumbnailOffsetY,-1,1),"Invalid profile image positioning");
    for(const auto& filename:{profile.avatarFilename,profile.backgroundFilename}) if(filename) require(imageName(*filename),"Invalid managed profile image name");
    if(profile.themeColorHex) require(hex(*profile.themeColorHex),"Invalid profile theme color");
    require(profile.originalFields.isObject(),"Invalid profile retained fields");
}
Json encodeProfile(const Profile& profile) {
    validateProfile(profile);auto object=profile.originalFields;
    object["name"]=profile.name;object["tag"]=profile.tag;object["introduction"]=profile.introduction;object["uid"]=profile.uid;
    number(object,"awakeningDate",profile.awakeningDate);optional(object,"gamePlayerID",profile.gamePlayerID);optional(object,"playerIDOverride",profile.playerIDOverride);
    object["hasManualAwakeningDate"]=profile.hasManualAwakeningDate;object["showsBirthday"]=profile.showsBirthday;
    integer(object,"birthdayMonth",profile.birthdayMonth);integer(object,"birthdayDay",profile.birthdayDay);
    integer(object,"permissionLevel",profile.permissionLevel);integer(object,"explorationLevel",profile.explorationLevel);
    integer(object,"operatorsCount",profile.operatorsCount);integer(object,"weaponsCount",profile.weaponsCount);integer(object,"archivesCount",profile.archivesCount);
    optional(object,"avatarFilename",profile.avatarFilename);optional(object,"backgroundFilename",profile.backgroundFilename);optional(object,"themeColorHex",profile.themeColorHex);
    number(object,"avatarZoom",profile.avatarZoom);number(object,"avatarOffsetX",profile.avatarOffsetX);number(object,"avatarOffsetY",profile.avatarOffsetY);
    number(object,"backgroundWidth",profile.backgroundWidth);number(object,"backgroundZoom",profile.backgroundZoom);number(object,"backgroundOffsetX",profile.backgroundOffsetX);number(object,"backgroundOffsetY",profile.backgroundOffsetY);
    number(object,"thumbnailZoom",profile.thumbnailZoom);number(object,"thumbnailOffsetX",profile.thumbnailOffsetX);number(object,"thumbnailOffsetY",profile.thumbnailOffsetY);
    number(object,"accumulatedWorkSeconds",profile.accumulatedWorkSeconds);return object;
}
void validateSettings(const Settings& value) {
    require(value.fields.isObject(),"Invalid settings object");const auto defaults=Settings::defaults();
    for(const auto& [key,expected]:defaults.fields.object()) {
        const auto& actual=value.fields[key];
        require(expected.isNull() || (expected.isString()&&actual.isString()) || (expected.isNumber()&&actual.isNumber()) ||
            (expected.isBool()&&actual.isBool()) || (expected.isObject()&&actual.isObject()),"Invalid setting value type");
    }
    const auto& f=value.fields;
    require(hex(f["accentHex"].string()),"Invalid HUD theme color");
    for(const auto key:{"backgroundDarkness","blurAmount","customPositionX","customPositionY"}) require(between(f[key].number(),0,1),"Invalid normalized setting");
    for(const auto key:{"hudScale","parallaxIntensity","perspectiveIntensity"}) require(between(f[key].number(),key==std::string_view("hudScale")?.2:0,2),"Invalid HUD transform setting");
    for(const auto key:{"hudOffsetX","hudOffsetY"}) require(between(f[key].number(),-.5,.5),"Invalid HUD offset");
    require(between(f["scale"].number(),.65,1.6)&&between(f["displayDuration"].number(),1,60),"Invalid battery display setting");
    auto choice=[&](const char* key,std::initializer_list<const char*> choices) {const auto current=f[key].string();require(std::any_of(choices.begin(),choices.end(),[&](const char* choice){return current==choice;}),"Invalid setting choice");};
    choice("theme",{"dark","light","system"});choice("language",{"system","english","simplifiedChinese","traditionalChinese","japanese","korean"});
    choice("clockFormat",{"twentyFourHour","twelveHour"});choice("placement",{"topCenter","custom"});
    for(const auto key:{"hudDisplayUUID","hudDisplayName","centerLogoRevision"}) require(f[key].isNull() || (f[key].isString()&&clean(f[key].string(),1024)),"Invalid optional setting");
    if(!f["customScreenID"].isNull()) require(f["customScreenID"].isNumber()&&f["customScreenID"].integer()>=0&&f["customScreenID"].integer()<=4294967295LL,"Invalid legacy display identity");
    // D3: absent = default; present = an array of module names (unknown and
    // repeated names stay valid and are dropped on read, as on the Mac).
    for(const auto key:{"hudModuleOrder","hudHiddenModules"}) if(f.contains(key)) {
        require(f[key].isArray()&&f[key].array().size()<=64,"Invalid module layout setting");
        for(const auto& name:f[key].array()) require(name.isString()&&clean(name.string(),256),"Invalid module layout setting");
    }
}
std::vector<std::string> strings(const Json& value) {
    std::vector<std::string> out;if(!value.isArray()) return out;
    for(const auto& item:value.array()) if(item.isString()) out.push_back(item.string());
    return out;
}
bool configurableModule(std::string_view name) {return std::find(ModuleLayout::configurable.begin(),ModuleLayout::configurable.end(),name)!=ModuleLayout::configurable.end();}
bool contains(const std::vector<std::string>& list,std::string_view name) {return std::find(list.begin(),list.end(),name)!=list.end();}
// D2 downgrade mirror: while the summon hotkey is off the record also carries
// windowsSummonHotkeyDisabled: true, so Windows 1.2.0 keeps the same intent;
// while it is on the legacy key is removed (absent = on for 1.2.0).
Json persisted(const Settings& value) {
    auto fields=value.fields;
    if(value.summonHotkeyEnabled()) fields.erase("windowsSummonHotkeyDisabled");else fields["windowsSummonHotkeyDisabled"]=true;
    return fields;
}
}
std::string makeUUID() {
    std::random_device random;std::array<unsigned char,16> bytes{};
    for(auto& byte:bytes) byte=static_cast<unsigned char>(random());bytes[6]=(bytes[6]&15)|64;bytes[8]=(bytes[8]&63)|128;
    static constexpr char digits[]="0123456789ABCDEF";std::string output;output.reserve(36);
    for(std::size_t i=0;i<bytes.size();++i) {if(i==4||i==6||i==8||i==10) output.push_back('-');output.push_back(digits[bytes[i]>>4]);output.push_back(digits[bytes[i]&15]);}return output;
}
bool validUUID(std::string_view value) noexcept {
    if(value.size()!=36) return false;for(std::size_t i=0;i<value.size();++i) {
        const char c=value[i];if(i==8||i==13||i==18||i==23) {if(c!='-') return false;}
        else if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f'))) return false;
    }return true;
}
double foundationNow() {return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()-978307200.0;}
// "displayMode" and "alertMetric" are open string choices (validateSettings
// checks only their type): the Mac values, the Windows-only "never" (Don't
// display) and "gpu", and another build's unknown value all load and
// round-trip verbatim; their readers fall back to Power changes / Battery.
Settings Settings::defaults() {
    Settings result;result.fields=Json::Object{
        {"displayMode","whenChargingStarts"},{"displayDuration",3},{"accentHex","FAD41F"},{"theme","dark"},{"scale",1},
        {"placement","topCenter"},{"customScreenID",nullptr},{"customPositionX",.5},{"customPositionY",.9},{"language","system"},
        {"hudScale",1},{"hudOffsetX",0},{"hudOffsetY",0},{"parallaxIntensity",1},{"perspectiveIntensity",1},
        {"backgroundDarkness",.63},{"blurAmount",.75},{"reduceMotion",false},{"ambientAnimation",true},{"closeOnFocusLost",true},
        {"openOnActiveDisplay",true},{"hudDisplayUUID",nullptr},{"hudDisplayName",nullptr},{"launchAtLogin",true},
        {"batteryAlertsEnabled",true},{"devicePopupEnabled",true},{"lowPowerVisualMode",false},{"applicationIcon","endfield"},
        {"clockFormat","twentyFourHour"},{"clockStyle","digital"},{"centerLogo","endfield"},{"centerLogoRevision",nullptr},{"alertMetric","battery"},
        {"summonShortcut",Json::Object{{"keyCode",50},{"modifiers",1}}},
        {"summonHotkeyEnabled",true},{"recordingMode",false},{"dailyBootLog",true},{"facilityAlerts",true}
    };return result;
}
Settings Settings::restoredDefaults(const Settings& current) {
    auto result=defaults();
    for(const auto key:{"orbipom.bestScore.v1","projection.settings.v1","dailyBootLogLastDay","storageVolume"}) if(current.fields.contains(key)) result.fields[key]=current.fields[key];
    return result;
}
namespace {
bool flag(const Json& fields,std::string_view key,bool fallback) {const auto& value=fields[key];return value.isBool()?value.boolean():fallback;}
}
bool Settings::summonHotkeyEnabled() const {return flag(fields,"summonHotkeyEnabled",true);}
bool Settings::recordingMode() const {return flag(fields,"recordingMode",false);}
bool Settings::dailyBootLog() const {return flag(fields,"dailyBootLog",true);}
bool Settings::facilityAlerts() const {return flag(fields,"facilityAlerts",true);}
std::optional<std::string> Settings::dailyBootLogLastDay() const {const auto& value=fields["dailyBootLogLastDay"];if(!value.isString()) return {};return value.string();}
bool Settings::consumeDailyBootLog(std::string_view today) {
    if(dailyBootLogLastDay()==today) return false;
    set("dailyBootLogLastDay",std::string(today));return true;
}
ModuleLayout ModuleLayout::normalized(std::vector<std::string> order,std::vector<std::string> hidden) {
    ModuleLayout value;value.order.clear();
    for(auto& name:order) if(configurableModule(name)&&!contains(value.order,name)) value.order.push_back(std::move(name));
    for(const auto name:configurable) if(!contains(value.order,name)) value.order.emplace_back(name);
    for(const auto& name:value.order) if(contains(hidden,name)) value.hidden.push_back(name);
    if(value.hidden.size()==configurable.size()) value.hidden.erase(value.hidden.begin());
    return value;
}
std::vector<std::string> ModuleLayout::visible() const {std::vector<std::string> out;for(const auto& name:order) if(!contains(hidden,name)) out.push_back(name);return out;}
std::string ModuleLayout::restorable(std::string_view module) const {return isHidden(module)?visible().front():std::string(module);}
bool ModuleLayout::isDefault() const {return hidden.empty()&&std::equal(order.begin(),order.end(),configurable.begin(),configurable.end());}
bool ModuleLayout::isHidden(std::string_view module) const {return contains(hidden,module);}
bool ModuleLayout::canSetHidden(std::string_view module,bool value) const {return !value||!configurableModule(module)||isHidden(module)||visible().size()>1;}
bool ModuleLayout::setHidden(std::string_view module,bool value) {
    if(!configurableModule(module)||!canSetHidden(module,value)) return false;
    if(value&&!isHidden(module)) {std::vector<std::string> next;for(const auto& name:order) if(contains(hidden,name)||name==module) next.push_back(name);hidden=std::move(next);}
    else if(!value) hidden.erase(std::remove(hidden.begin(),hidden.end(),module),hidden.end());
    return true;
}
bool ModuleLayout::move(std::string_view module,std::ptrdiff_t index) {
    const auto found=std::find(order.begin(),order.end(),module);if(found==order.end()) return false;
    const auto from=found-order.begin(),to=std::clamp<std::ptrdiff_t>(index,0,static_cast<std::ptrdiff_t>(order.size())-1);if(to==from) return false;
    auto moved=std::move(*found);order.erase(found);order.insert(order.begin()+to,std::move(moved));
    std::vector<std::string> next;for(const auto& name:order) if(contains(hidden,name)) next.push_back(name);hidden=std::move(next);return true;
}
// HUDModuleLayout(defaults:): an absent order is the default order; a present
// one is read as given (a non-array reads as empty, so the default order).
ModuleLayout Settings::moduleLayout() const {
    return ModuleLayout::normalized(fields.contains("hudModuleOrder")?strings(fields["hudModuleOrder"]):std::vector<std::string>(ModuleLayout::configurable.begin(),ModuleLayout::configurable.end()),strings(fields["hudHiddenModules"]));
}
void Settings::setModuleLayout(const ModuleLayout& layout) {
    const auto value=ModuleLayout::normalized(layout.order,layout.hidden);
    if(std::equal(value.order.begin(),value.order.end(),ModuleLayout::configurable.begin(),ModuleLayout::configurable.end())) fields.erase("hudModuleOrder");
    else {Json::Array names;for(const auto& name:value.order) names.emplace_back(name);fields["hudModuleOrder"]=std::move(names);}
    if(value.hidden.empty()) fields.erase("hudHiddenModules");
    else {Json::Array names;for(const auto& name:value.hidden) names.emplace_back(name);fields["hudHiddenModules"]=std::move(names);}
}
std::string Settings::batteryAlertMethod() const {
    if(!flag(fields,"batteryAlertsEnabled",true)) return "never";
    const auto& mode=fields["displayMode"];return mode.isString()?mode.string():std::string("whenChargingStarts");
}
void Settings::setBatteryAlertMethod(std::string method) {set("displayMode",std::move(method));set("batteryAlertsEnabled",true);}
ProjectionSettings decodeProjectionSettings(const Json& stored) {
    ProjectionSettings value;if(!stored.isObject()) return value;
    // NSNumber.uint32Value: integers wrap to their low 32 bits, reals truncate.
    if(const auto& color=stored["color"];color.isNumber()) {
        const auto real=color.number();std::int64_t whole{};
        if(std::isfinite(real)&&std::trunc(real)==real&&std::abs(real)<9.2e18) whole=color.integer();
        else if(std::isfinite(real)&&std::abs(real)<9.2e18) whole=static_cast<std::int64_t>(std::trunc(real));
        value.color=static_cast<std::uint32_t>(static_cast<std::uint64_t>(whole))&0xFFFFFFu;
    } else if(color.isBool()) value.color=color.boolean()?1u:0u;
    if(const auto& width=stored["width"];width.isNumber()&&std::isfinite(width.number())) value.width=std::clamp(width.number(),1.,80.);
    value.erasing=stored["erasing"].isBool()&&stored["erasing"].boolean();
    value.background=!stored["background"].isBool()||stored["background"].boolean();
    return value;
}
Json encodeProjectionSettings(const ProjectionSettings& value) {
    Json::Object out{{"width",std::isfinite(value.width)?std::clamp(value.width,1.,80.):12.},{"erasing",value.erasing},{"background",value.background}};
    if(value.color) out["color"]=static_cast<std::int64_t>(*value.color&0xFFFFFFu);
    return out;
}
ProjectionSettings Settings::projection() const {return decodeProjectionSettings(fields["projection.settings.v1"]);}
void Settings::setProjection(const ProjectionSettings& value) {set("projection.settings.v1",encodeProjectionSettings(value));}
std::int64_t Settings::orbipomBestScore() const {const auto& value=fields["orbipom.bestScore.v1"];return value.isNumber()?std::max<std::int64_t>(0,value.integer()):0;}
std::string Settings::string(std::string_view key) const {return fields[key].string();}
double Settings::number(std::string_view key) const {return fields[key].number();}
bool Settings::boolean(std::string_view key) const {return fields[key].boolean();}
void Settings::set(std::string key,Json value) {fields[std::move(key)]=std::move(value);}
SettingsStore::SettingsStore(const std::filesystem::path& root) {
    detail::validateRoot(root);path_=root/"settings.json";value_=Settings::defaults();persisted_=detail::readFile(path_,jsonLimit);
    if(persisted_) {
        try {envelope_=parseRecord(*persisted_);version(envelope_);require(envelope_["settings"].isObject(),"Missing saved settings");
            for(const auto& [key,value]:envelope_["settings"].object()) value_.fields[key]=value;
            // D2: the Mac summonHotkeyEnabled is the switch. A record without it
            // (Windows 1.2.0) takes it from windowsSummonHotkeyDisabled (opposite
            // polarity); when both exist the Mac key wins. The legacy key never
            // lives in memory: persisted() writes it back as a mirror.
            if(const auto& saved=std::as_const(envelope_)["settings"];saved.contains("windowsSummonHotkeyDisabled")) {
                if(const auto& legacy=saved["windowsSummonHotkeyDisabled"];!saved.contains("summonHotkeyEnabled")&&legacy.isBool()) value_.fields["summonHotkeyEnabled"]=!legacy.boolean();
                value_.fields.erase("windowsSummonHotkeyDisabled");
            }
            validateSettings(value_);
            if(envelope_.contains("hasLaunched")){require(envelope_["hasLaunched"].isBool(),"Invalid first-launch marker");launched_=envelope_["hasLaunched"].boolean();}
        } catch(const StoreError&) {throw;} catch(const std::exception&) {throw StoreError(StoreErrorCode::invalid,"Saved settings are invalid; the original was preserved");}
    }
}
bool SettingsStore::markLaunched() {
    if(launched_) return false;
    auto envelope=envelope_;envelope["version"]=1;envelope["settings"]=persisted(value_);envelope["hasLaunched"]=true;const auto bytes=encodeRecord(envelope);
    detail::replaceFile(path_,persisted_,bytes,jsonLimit);envelope_=std::move(envelope);persisted_=bytes;launched_=true;return true;
}
bool SettingsStore::update(const Settings& value) {
    validateSettings(value);if(value==value_ && persisted_) return false;
    auto envelope=envelope_;envelope["version"]=1;envelope["settings"]=persisted(value);const auto bytes=encodeRecord(envelope);
    detail::replaceFile(path_,persisted_,bytes,jsonLimit);value_=value;envelope_=std::move(envelope);persisted_=bytes;return true;
}
Profile Profile::defaults() {
    Profile value;value.awakeningDate=foundationNow();std::random_device random;
    const auto entropy=(static_cast<std::uint64_t>(random())<<32)|random();value.uid=std::to_string(1'000'000'000ULL+entropy%9'000'000'000ULL);
    const auto time=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&time);
#else
    localtime_r(&time,&local);
#endif
    value.birthdayMonth=local.tm_mon+1;value.birthdayDay=local.tm_mday;return value;
}
Profile normalizedEditableProfile(Profile value) {
    static constexpr int days[]{31,29,31,30,31,30,31,31,30,31,30,31};
    const auto whitespace=[](char32_t c){return swiftWhitespace(c);};
    const auto whitespaceOrNewline=[](char32_t c){return swiftWhitespace(c)||swiftNewline(c);};
    if(value.playerIDOverride) {auto clean=trimmed(withoutNewlines(*value.playerIDOverride),whitespace);value.playerIDOverride=clean.empty()?std::nullopt:std::optional(std::move(clean));}
    value.name=trimmed(value.name,whitespaceOrNewline);value.tag=trimmed(value.tag,whitespaceOrNewline);
    value.permissionLevel=std::clamp(value.permissionLevel,1,60);value.explorationLevel=std::clamp(value.explorationLevel,1,7);
    value.birthdayMonth=std::clamp(value.birthdayMonth,1,12);value.birthdayDay=std::clamp(value.birthdayDay,1,days[value.birthdayMonth-1]);
    if(value.themeColorHex) value.themeColorHex=normalizedThemeHex(*value.themeColorHex);
    value.avatarZoom=bound(value.avatarZoom,1,20,1);value.avatarOffsetX=bound(value.avatarOffsetX,-1,1,0);value.avatarOffsetY=bound(value.avatarOffsetY,-1,1,0);
    value.backgroundWidth=bound(value.backgroundWidth,400,900,600);value.backgroundZoom=bound(value.backgroundZoom,1,20,1);
    value.backgroundOffsetX=bound(value.backgroundOffsetX,-400,400,0);value.backgroundOffsetY=bound(value.backgroundOffsetY,-250,250,0);
    value.thumbnailZoom=bound(value.thumbnailZoom,1,20,1);value.thumbnailOffsetX=bound(value.thumbnailOffsetX,-1,1,0);value.thumbnailOffsetY=bound(value.thumbnailOffsetY,-1,1,0);
    return value;
}
std::string Profile::displayedUID() const {return playerIDOverride.value_or(gamePlayerID.value_or(uid));}
ProfileStore::ProfileStore(const std::filesystem::path& root) {
    detail::validateRoot(root);path_=root/"Profile"/"profile.json";persisted_=detail::readFile(path_,jsonLimit);
    if(persisted_) {
        try {envelope_=parseRecord(*persisted_);version(envelope_);value_=normalizedEditableProfile(decodeProfile(envelope_["profile"]));validateProfile(value_);}
        catch(const StoreError&) {throw;} catch(const std::exception&) {throw StoreError(StoreErrorCode::invalid,"Saved profile is invalid; the original was preserved");}
    } else {value_=Profile::defaults();update(value_);}
}
bool ProfileStore::update(const Profile& value) {
    return commit(value,false);
}
bool ProfileStore::updateFromGame(const Profile& value) {
    return commit(value,true);
}
bool ProfileStore::commit(const Profile& value,bool fromGame) {
    require(value.uid==value_.uid,"The original local UID cannot change");validateProfile(value);
    // Only the fields the account sync owns are locked (Mac gameSyncedFields).
    using F=ProfileSyncField;const auto locked=[&](F f){return !fromGame&&syncLocked_.contains(f);};
    require((!locked(F::name)||value.name==value_.name)&&(!locked(F::tag)||value.tag==value_.tag)&&
        (!locked(F::uid)||(value.gamePlayerID==value_.gamePlayerID&&value.playerIDOverride==value_.playerIDOverride))&&
        (!locked(F::awakening)||(value.awakeningDate==value_.awakeningDate&&value.hasManualAwakeningDate==value_.hasManualAwakeningDate))&&
        (!locked(F::levels)||(value.permissionLevel==value_.permissionLevel&&value.explorationLevel==value_.explorationLevel))&&
        (!locked(F::counts)||(value.operatorsCount==value_.operatorsCount&&value.weaponsCount==value_.weaponsCount&&value.archivesCount==value_.archivesCount)),
        "Synced game profile fields cannot be edited locally");
    if(value==value_ && persisted_) return false;
    const auto fields=encodeProfile(value);auto envelope=envelope_;envelope["version"]=1;envelope["profile"]=fields;
    const auto bytes=encodeRecord(envelope);detail::replaceFile(path_,persisted_,bytes,jsonLimit);
    value_=value;value_.originalFields=fields;envelope_=std::move(envelope);persisted_=bytes;return true;
}
std::filesystem::path ProfileStore::imagePath(std::string_view filename) const {
    require(imageName(filename),"Invalid managed profile image name");return path_.parent_path()/"Images"/std::string(filename);
}
Note Note::textNote(std::string text) {Note value;value.text=std::move(text);return value;}
Note Note::todoNote() {Note value;value.kind=NoteKind::todo;value.width=180;value.height=110;return value;}
}
