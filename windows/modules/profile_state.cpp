#include "modules/profile_state.hpp"
#include <algorithm>
#include <charconv>
#include <locale.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace endfield::modules {
namespace {
constexpr std::array<std::string_view,21>ids{"name","tag","playerID","introduction","awakeningDate","birthday","permissionLevel","explorationLevel","operatorsCount","weaponsCount","archivesCount",
    "backgroundWidth","backgroundZoom","backgroundOffsetX","backgroundOffsetY","thumbnailZoom","thumbnailOffsetX","thumbnailOffsetY","avatarZoom","avatarOffsetX","avatarOffsetY"};
std::string L(const char*english,const char*chinese,core::Language language){return core::localized(english,chinese,language);}
double bound(double v,double lo,double hi,double fallback)noexcept{return std::isfinite(v)?std::clamp(v,lo,hi):fallback;}
// UserProfile.maximumBirthdayDay (Feb 29 allowed).
int maximumBirthdayDay(int month)noexcept{constexpr int days[]{31,29,31,30,31,30,31,31,30,31,30,31};return days[std::clamp(month,1,12)-1];}
bool hexUUID(std::string_view s)noexcept{
    if(s.size()!=36)return false;
    for(std::size_t i=0;i<36;++i){const char c=s[i];if(i==8||i==13||i==18||i==23){if(c!='-')return false;continue;}
        if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;}
    return true;
}
// UserProfileStore.validImageName
bool validImageName(std::string_view name)noexcept{
    const std::string_view suffix=name.ends_with(".png")?".png":".image";
    return name.ends_with(suffix)&&name.size()==36+suffix.size()&&hexUUID(name.substr(0,36));
}
// UserProfile.normalizedThemeHex
std::optional<std::string>themeHex(const std::string&value,const ProfileTextRules&r){
    auto s=r.trim(value,true);s.erase(std::remove(s.begin(),s.end(),'#'),s.end());s=r.uppercase(s);
    if(s.size()!=6||!std::all_of(s.begin(),s.end(),[](char c){return(c>='0'&&c<='9')||(c>='A'&&c<='F');}))return std::nullopt;
    return s;
}
// String.split(separator:"/",omittingEmptySubsequences:false) after "-" -> "/".
template<std::size_t N>std::optional<std::array<std::int64_t,N>>dateParts(std::string_view input){
    std::array<std::int64_t,N>parts{};std::size_t index{},start{};
    for(;;){
        const auto end=input.find_first_of("/-",start);
        if(index==N)return std::nullopt;
        const auto value=swiftInteger(input.substr(start,end==std::string_view::npos?std::string_view::npos:end-start));
        if(!value)return std::nullopt;
        parts[index++]=*value;
        if(end==std::string_view::npos)break;
        start=end+1;
    }
    if(index!=N)return std::nullopt;
    return parts;
}
}

std::string profileFailureMessage(ProfileFailure failure,core::Language language,std::string_view detail){
    switch(failure){
    case ProfileFailure::name:return L("Enter a name of 1–20 characters without line breaks.","请输入 1–20 个字符的名称，不含换行。",language);
    case ProfileFailure::tag:return L("Enter a tag of 1–10 characters without spaces or #.","请输入 1–10 个字符的编号，不含空格或 #。",language);
    case ProfileFailure::value:return L("Enter a number within the allowed range.","请输入允许范围内的数字。",language);
    case ProfileFailure::image:return L("Choose a readable image file.","请选择可读取的图片文件。",language);
    case ProfileFailure::imageTooLarge:return L("Choose an image smaller than 128 MB.","请选择小于 128 MB 的图片。",language);
    case ProfileFailure::imageDimensions:return L("Choose a portrait no larger than 128 megapixels or 32,768 pixels on either side.","请选择不超过 1.28 亿像素、且单边不超过 32,768 像素的头像。",language);
    case ProfileFailure::record:return L("The personal card could not be read. The saved data has been preserved.","无法读取个人名片，已保留原始数据。",language);
    case ProfileFailure::newerVersion:return L("This personal card was saved by a newer version of EndfieldHUD.","此个人名片由较新版本的 EndfieldHUD 保存。",language);
    case ProfileFailure::changedOnDisk:return L("The personal card changed in another app instance. Restart before editing.","个人名片已由另一个应用实例更改，请重启后编辑。",language);
    case ProfileFailure::persistence:return L("The personal card could not be saved: ","无法保存个人名片：",language)+std::string(detail.substr(0,1024));
    case ProfileFailure::unavailable:return L("Profile storage is unavailable.","个人名片存储不可用。",language);
    }
    return {};
}

ProfileError::ProfileError(ProfileFailure failure,std::string detail)
    :std::runtime_error(profileFailureMessage(failure,core::Language::english,detail)),code_(failure),detail_(std::move(detail)){}
std::string profileErrorText(const std::exception&e,core::Language language){
    if(const auto*profile=dynamic_cast<const ProfileError*>(&e))return profileFailureMessage(profile->code(),language,profile->detail());
    if(const auto*store=dynamic_cast<const ehud::data::StoreError*>(&e)){
        switch(store->code()){
        case ehud::data::StoreErrorCode::changedOnDisk:return profileFailureMessage(ProfileFailure::changedOnDisk,language);
        case ehud::data::StoreErrorCode::newerVersion:return profileFailureMessage(ProfileFailure::newerVersion,language);
        default:return profileFailureMessage(ProfileFailure::persistence,language,e.what());
        }
    }
    return profileFailureMessage(ProfileFailure::persistence,language,e.what());
}

std::string_view profileFieldID(ProfileField f)noexcept{const auto n=static_cast<std::size_t>(f);return n<ids.size()?ids[n]:std::string_view{};}
std::optional<ProfileField>profileField(std::string_view s)noexcept{const auto i=std::find(ids.begin(),ids.end(),s);return i==ids.end()?std::nullopt:std::optional(static_cast<ProfileField>(i-ids.begin()));}
std::string profileFieldTitle(ProfileField f,core::Language l){switch(f){
    case ProfileField::name:return L("Name","名称",l);case ProfileField::playerID:return L("Player ID","玩家 ID",l);case ProfileField::tag:return "#";
    case ProfileField::introduction:return L("Introduction","个人介绍",l);case ProfileField::awakeningDate:return L("Awakening day","苏醒日",l);case ProfileField::birthday:return L("Birthday","生日",l);
    case ProfileField::permissionLevel:return L("Authority level","权限等级",l);case ProfileField::explorationLevel:return L("Exploration level","探索等级",l);
    case ProfileField::operatorsCount:return L("Operators","干员",l);case ProfileField::weaponsCount:return L("Weapons","武器",l);case ProfileField::archivesCount:return L("Archives","档案",l);
    case ProfileField::backgroundWidth:return L("Background width","背景宽度",l);case ProfileField::backgroundZoom:return L("Background zoom","背景缩放",l);
    case ProfileField::backgroundOffsetX:return L("Background X","背景 X",l);case ProfileField::backgroundOffsetY:return L("Background Y","背景 Y",l);
    case ProfileField::thumbnailZoom:return L("Thumbnail zoom","缩略图缩放",l);case ProfileField::thumbnailOffsetX:return L("Thumbnail X","缩略图 X",l);case ProfileField::thumbnailOffsetY:return L("Thumbnail Y","缩略图 Y",l);
    case ProfileField::avatarZoom:return L("Zoom","缩放",l);case ProfileField::avatarOffsetX:return L("Portrait X","头像 X",l);case ProfileField::avatarOffsetY:return L("Portrait Y","头像 Y",l);
    }return {};}
bool profileGeometryField(ProfileField f)noexcept{return f>=ProfileField::backgroundWidth&&f<=ProfileField::avatarOffsetY;}
bool profileNumericField(ProfileField f)noexcept{return f>=ProfileField::permissionLevel&&f<=ProfileField::archivesCount;}
std::optional<std::size_t>profileTextLimit(ProfileField f)noexcept{
    switch(f){
    case ProfileField::name:return 20;case ProfileField::tag:return 11;case ProfileField::playerID:return 64;case ProfileField::introduction:return 150;
    case ProfileField::awakeningDate:case ProfileField::birthday:return 10;case ProfileField::permissionLevel:case ProfileField::explorationLevel:return 2;
    case ProfileField::operatorsCount:case ProfileField::weaponsCount:case ProfileField::archivesCount:return 5;
    default:return std::nullopt;
    }
}
std::optional<ehud::data::ProfileSyncField>profileSyncField(ProfileField f)noexcept{
    using S=ehud::data::ProfileSyncField;using F=ProfileField;
    switch(f){case F::name:return S::name;case F::tag:return S::tag;case F::playerID:return S::uid;case F::awakeningDate:return S::awakening;
        case F::permissionLevel:case F::explorationLevel:return S::levels;case F::operatorsCount:case F::weaponsCount:case F::archivesCount:return S::counts;default:return std::nullopt;}
}
std::array<double,3>profileAccent(const PersonalProfile&p,std::array<double,3>fallback)noexcept{
    if(!p.themeColorHex||p.themeColorHex->size()!=6)return fallback;
    unsigned value{};const auto*first=p.themeColorHex->data();const auto r=std::from_chars(first,first+6,value,16);
    if(r.ec!=std::errc{}||r.ptr!=first+6)return fallback;
    return{static_cast<double>((value>>16)&255)/255,static_cast<double>((value>>8)&255)/255,static_cast<double>(value&255)/255};
}
std::optional<std::int64_t>swiftInteger(std::string_view s)noexcept{
    if(s.empty())return std::nullopt;
    bool negative{};std::size_t i{};
    if(s[0]=='+'||s[0]=='-'){negative=s[0]=='-';i=1;}
    if(i==s.size())return std::nullopt;
    std::uint64_t magnitude{};constexpr auto limit=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())+1;
    for(;i<s.size();++i){
        if(s[i]<'0'||s[i]>'9')return std::nullopt;
        const auto digit=static_cast<std::uint64_t>(s[i]-'0');
        if(magnitude>(limit-digit)/10)return std::nullopt;
        magnitude=magnitude*10+digit;
    }
    if(negative)return magnitude==limit?std::numeric_limits<std::int64_t>::min():-static_cast<std::int64_t>(magnitude);
    if(magnitude==limit)return std::nullopt;
    return static_cast<std::int64_t>(magnitude);
}
std::optional<double>swiftDouble(std::string_view s)noexcept{
    if(s.empty())return std::nullopt;
    if(const auto c=static_cast<unsigned char>(s[0]);c==32||(c>=9&&c<=13))return std::nullopt; // Swift rejects leading whitespace
    std::size_t i{};bool negative{};
    if(s[0]=='+'||s[0]=='-'){negative=s[0]=='-';i=1;}
    const auto rest=s.substr(i);
    const auto same=[](std::string_view a,std::string_view b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](char x,char y){return (x>='A'&&x<='Z'?x-'A'+'a':x)==y;});};
    constexpr double infinity=std::numeric_limits<double>::infinity(),nan=std::numeric_limits<double>::quiet_NaN();
    if(same(rest,"snan")||same(rest,"nan"))return nan;
    if(same(rest,"inf")||same(rest,"infinity"))return negative?-infinity:infinity;
    if(rest.size()>=4&&same(rest.substr(0,4),"nan(")&&rest.back()==')'){
        const auto inner=rest.substr(4,rest.size()-5);
        if(std::all_of(inner.begin(),inner.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';}))return nan;
        return std::nullopt;
    }
    const auto digits=[](std::string_view t,std::size_t&k,bool hex){std::size_t n{};while(k<t.size()){const char c=t[k];const bool ok=(c>='0'&&c<='9')||(hex&&((c>='a'&&c<='f')||(c>='A'&&c<='F')));if(!ok)break;++k;++n;}return n;};
    double value{};
    if(rest.size()>=2&&rest[0]=='0'&&(rest[1]=='x'||rest[1]=='X')){
        const auto body=rest.substr(2);std::size_t k{};auto count=digits(body,k,true);
        if(k<body.size()&&body[k]=='.'){++k;count+=digits(body,k,true);}
        if(!count)return std::nullopt; // strtod consumes only "0"
        if(k<body.size()&&(body[k]=='p'||body[k]=='P')){std::size_t e=k+1;if(e<body.size()&&(body[e]=='+'||body[e]=='-'))++e;if(!digits(body,e,false))return std::nullopt;k=e;}
        if(k!=body.size())return std::nullopt;
    }else{
        std::size_t k{};auto count=digits(rest,k,false);
        if(k<rest.size()&&rest[k]=='.'){++k;count+=digits(rest,k,false);}
        if(!count)return std::nullopt;
        if(k<rest.size()&&(rest[k]=='e'||rest[k]=='E')){std::size_t e=k+1;if(e<rest.size()&&(rest[e]=='+'||rest[e]=='-'))++e;if(!digits(rest,e,false))return std::nullopt;k=e;}
        if(k!=rest.size())return std::nullopt;
    }
    // strtod_l in an immutable C numeric locale, never the process locale.
    struct Locale {
#ifdef _WIN32
        _locale_t value{_create_locale(LC_NUMERIC,"C")};~Locale(){if(value)_free_locale(value);}
#else
        locale_t value{newlocale(LC_NUMERIC_MASK,"C",nullptr)};~Locale(){if(value)freelocale(value);}
#endif
    };
    static const Locale locale;if(!locale.value)return std::nullopt;
    const std::string buffer(rest);char*end{};
#ifdef _WIN32
    value=_strtod_l(buffer.c_str(),&end,locale.value);
#else
    value=strtod_l(buffer.c_str(),&end,locale.value);
#endif
    if(end!=buffer.c_str()+buffer.size())return std::nullopt;
    // Measured source behavior: an underflow is the parsed zero/subnormal and
    // an overflow is infinite (rejected later as non-finite), not nil.
    return negative?-value:value;
}
bool sameProfileValues(const PersonalProfile&a,const PersonalProfile&b)noexcept{
    return a.name==b.name&&a.tag==b.tag&&a.introduction==b.introduction&&a.uid==b.uid&&a.awakeningDate==b.awakeningDate&&a.gamePlayerID==b.gamePlayerID&&
        a.playerIDOverride==b.playerIDOverride&&a.hasManualAwakeningDate==b.hasManualAwakeningDate&&a.showsBirthday==b.showsBirthday&&a.birthdayMonth==b.birthdayMonth&&
        a.birthdayDay==b.birthdayDay&&a.permissionLevel==b.permissionLevel&&a.explorationLevel==b.explorationLevel&&a.operatorsCount==b.operatorsCount&&
        a.weaponsCount==b.weaponsCount&&a.archivesCount==b.archivesCount&&a.avatarFilename==b.avatarFilename&&a.backgroundFilename==b.backgroundFilename&&
        a.themeColorHex==b.themeColorHex&&a.avatarZoom==b.avatarZoom&&a.avatarOffsetX==b.avatarOffsetX&&a.avatarOffsetY==b.avatarOffsetY&&
        a.backgroundWidth==b.backgroundWidth&&a.backgroundZoom==b.backgroundZoom&&a.backgroundOffsetX==b.backgroundOffsetX&&a.backgroundOffsetY==b.backgroundOffsetY&&
        a.thumbnailZoom==b.thumbnailZoom&&a.thumbnailOffsetX==b.thumbnailOffsetX&&a.thumbnailOffsetY==b.thumbnailOffsetY&&a.accumulatedWorkSeconds==b.accumulatedWorkSeconds;
}
PersonalProfile normalizedProfile(PersonalProfile p,const ProfileTextRules&r){
    if(!r.characters||!r.prefix||!r.trim||!r.removeNewlines||!r.hasControl||!r.hasWhitespace||!r.uppercase)throw std::invalid_argument("Profile Unicode rules are required");
    if(p.playerIDOverride){auto v=r.prefix(r.trim(r.removeNewlines(*p.playerIDOverride),false),64);p.playerIDOverride=v.empty()?std::nullopt:std::optional(std::move(v));}
    p.name=r.prefix(r.trim(p.name,true),20);p.tag=r.prefix(r.trim(p.tag,true),10);p.introduction=r.prefix(p.introduction,150);
    p.permissionLevel=std::clamp(p.permissionLevel,1,60);p.explorationLevel=std::clamp(p.explorationLevel,1,7);
    p.birthdayMonth=std::clamp(p.birthdayMonth,1,12);p.birthdayDay=std::clamp(p.birthdayDay,1,maximumBirthdayDay(p.birthdayMonth));
    p.themeColorHex=p.themeColorHex?themeHex(*p.themeColorHex,r):std::nullopt;
    p.avatarZoom=bound(p.avatarZoom,1,20,1);p.avatarOffsetX=bound(p.avatarOffsetX,-1,1,0);p.avatarOffsetY=bound(p.avatarOffsetY,-1,1,0);
    p.backgroundWidth=bound(p.backgroundWidth,400,900,600);p.backgroundZoom=bound(p.backgroundZoom,1,20,1);
    p.backgroundOffsetX=bound(p.backgroundOffsetX,-400,400,0);p.backgroundOffsetY=bound(p.backgroundOffsetY,-250,250,0);
    p.thumbnailZoom=bound(p.thumbnailZoom,1,20,1);p.thumbnailOffsetX=bound(p.thumbnailOffsetX,-1,1,0);p.thumbnailOffsetY=bound(p.thumbnailOffsetY,-1,1,0);
    return p;
}
void validatePersonalProfile(const PersonalProfile&p,const ProfileTextRules&r){
    if(p.name.empty()||r.characters(p.name)>20||r.hasControl(p.name))throw ProfileError(ProfileFailure::name);
    if(p.tag.empty()||r.characters(p.tag)>10||r.hasControl(p.tag)||r.hasWhitespace(p.tag)||p.tag.find('#')!=std::string::npos)throw ProfileError(ProfileFailure::tag);
    const auto within=[](double v,double lo,double hi){return v>=lo&&v<=hi;};
    if(r.characters(p.introduction)>150||p.permissionLevel<1||p.permissionLevel>60||p.explorationLevel<1||p.explorationLevel>7||
       p.birthdayMonth<1||p.birthdayMonth>12||p.birthdayDay<1||p.birthdayDay>maximumBirthdayDay(p.birthdayMonth)||
       !within(p.avatarZoom,1,20)||!within(p.avatarOffsetX,-1,1)||!within(p.avatarOffsetY,-1,1)||!within(p.backgroundWidth,400,900)||
       !within(p.backgroundZoom,1,20)||!within(p.thumbnailZoom,1,20)||!within(p.backgroundOffsetX,-400,400)||!within(p.backgroundOffsetY,-250,250)||
       !within(p.thumbnailOffsetX,-1,1)||!within(p.thumbnailOffsetY,-1,1)||p.operatorsCount<0||p.weaponsCount<0||p.archivesCount<0||
       !std::isfinite(p.accumulatedWorkSeconds)||p.accumulatedWorkSeconds<0)throw ProfileError(ProfileFailure::value);
    if(p.uid.size()!=10||!std::all_of(p.uid.begin(),p.uid.end(),[](char c){return c>='0'&&c<='9';})||!std::isfinite(p.awakeningDate)||
       (p.avatarFilename&&!validImageName(*p.avatarFilename))||(p.backgroundFilename&&!validImageName(*p.backgroundFilename)))throw ProfileError(ProfileFailure::record);
}
std::optional<double>profileParseAwakeningDate(std::string_view input,const ProfileDateRules&dates){
    const auto parts=dateParts<3>(input);
    if(!parts||(*parts)[0]<1||(*parts)[0]>9999||(*parts)[1]<1||(*parts)[1]>12||(*parts)[2]<1||(*parts)[2]>31||!dates.noon)return std::nullopt;
    // calendar.date(from:) then the round trip of year/month/day (noon rejects a changed day).
    return dates.noon(static_cast<int>((*parts)[0]),static_cast<int>((*parts)[1]),static_cast<int>((*parts)[2]));
}
std::optional<std::pair<int,int>>profileParseBirthday(std::string_view input){
    const auto parts=dateParts<2>(input);
    if(!parts||(*parts)[0]<1||(*parts)[0]>12||(*parts)[1]<1||(*parts)[1]>maximumBirthdayDay(static_cast<int>((*parts)[0])))return std::nullopt;
    return std::pair{static_cast<int>((*parts)[0]),static_cast<int>((*parts)[1])};
}
double profileGeometryValue(ProfileField f,const PersonalProfile&p)noexcept{switch(f){
    case ProfileField::backgroundWidth:return p.backgroundWidth;case ProfileField::backgroundZoom:return p.backgroundZoom;
    case ProfileField::backgroundOffsetX:return p.backgroundOffsetX;case ProfileField::backgroundOffsetY:return p.backgroundOffsetY;
    case ProfileField::thumbnailZoom:return p.thumbnailZoom;case ProfileField::thumbnailOffsetX:return p.thumbnailOffsetX*100;case ProfileField::thumbnailOffsetY:return p.thumbnailOffsetY*100;
    case ProfileField::avatarZoom:return p.avatarZoom;case ProfileField::avatarOffsetX:return p.avatarOffsetX*100;case ProfileField::avatarOffsetY:return p.avatarOffsetY*100;
    default:return 0;}}
void profileApplyGeometry(ProfileField f,double v,PersonalProfile&p)noexcept{switch(f){
    case ProfileField::backgroundWidth:p.backgroundWidth=v;break;case ProfileField::backgroundZoom:p.backgroundZoom=v;break;
    case ProfileField::backgroundOffsetX:p.backgroundOffsetX=v;break;case ProfileField::backgroundOffsetY:p.backgroundOffsetY=v;break;
    case ProfileField::thumbnailZoom:p.thumbnailZoom=v;break;case ProfileField::thumbnailOffsetX:p.thumbnailOffsetX=v/100;break;case ProfileField::thumbnailOffsetY:p.thumbnailOffsetY=v/100;break;
    case ProfileField::avatarZoom:p.avatarZoom=v;break;case ProfileField::avatarOffsetX:p.avatarOffsetX=v/100;break;case ProfileField::avatarOffsetY:p.avatarOffsetY=v/100;break;
    default:break;}}
std::string profileValue(ProfileField f,const PersonalProfile&p,const ProfileDateRules&dates){
    switch(f){
    case ProfileField::name:return p.name;case ProfileField::tag:return p.tag;case ProfileField::playerID:return p.displayedUID();
    case ProfileField::introduction:return p.introduction;case ProfileField::awakeningDate:return dates.format(p.awakeningDate);
    case ProfileField::birthday:{char s[32]{};std::snprintf(s,sizeof s,"%02d/%02d",p.birthdayMonth,p.birthdayDay);return s;}
    case ProfileField::permissionLevel:return std::to_string(p.permissionLevel);case ProfileField::explorationLevel:return std::to_string(p.explorationLevel);
    case ProfileField::operatorsCount:return std::to_string(p.operatorsCount);case ProfileField::weaponsCount:return std::to_string(p.weaponsCount);
    case ProfileField::archivesCount:return std::to_string(p.archivesCount);
    default:{char s[384]{};std::snprintf(s,sizeof s,"%.2f",profileGeometryValue(f,p));return s;}
    }
}

ProfileState::ProfileState(PersonalProfile p,ProfileTextRules rules,ProfileDateRules dates,ProfilePersistence io)
    :profile_(normalizedProfile(std::move(p),rules)),text_(std::move(rules)),dates_(std::move(dates)),persistence_(std::move(io)){
    if(!dates_.noon||!dates_.format)throw std::invalid_argument("Profile Gregorian date rules are required");
    validatePersonalProfile(profile_,text_);
}
void ProfileState::notify(){
    ++revision_;
    // A copy: an observer may add or remove observers.
    const auto list=observers_;for(const auto&[id,fn]:list)if(fn)fn();
}
void ProfileState::adopt(PersonalProfile next){profile_=std::move(next);notify();}
void ProfileState::setSyncLocked(ehud::data::ProfileSyncFields v){if(v==locked_)return;locked_=v;notify();}
void ProfileState::refresh(PersonalProfile p){
    p=normalizedProfile(std::move(p),text_);validatePersonalProfile(p,text_);
    if(p.uid!=profile_.uid)throw ProfileError(ProfileFailure::record);
    if(!sameProfileValues(p,profile_)||p.originalFields!=profile_.originalFields)adopt(std::move(p));
}
void ProfileState::setDateRules(ProfileDateRules dates){
    if(!dates.noon||!dates.format)throw std::invalid_argument("Profile Gregorian date rules are required");
    dates_=std::move(dates);notify();
}
bool ProfileState::commit(PersonalProfile next){
    if(!persistence_.commit)throw ProfileError(ProfileFailure::unavailable);
    next=normalizedProfile(std::move(next),text_);validatePersonalProfile(next,text_);
    if(next.uid!=profile_.uid)throw ProfileError(ProfileFailure::record);
    // ProfileStore's rule, synchronously: the fields the account sync owns
    // (Mac gameSyncedFields) are not edited locally (the canvas locks them).
    using S=ehud::data::ProfileSyncField;const auto locked=[&](S f){return locked_.contains(f);};const auto&p=profile_;
    if((locked(S::name)&&next.name!=p.name)||(locked(S::tag)&&next.tag!=p.tag)||
       (locked(S::uid)&&(next.gamePlayerID!=p.gamePlayerID||next.playerIDOverride!=p.playerIDOverride))||
       (locked(S::awakening)&&(next.awakeningDate!=p.awakeningDate||next.hasManualAwakeningDate!=p.hasManualAwakeningDate))||
       (locked(S::levels)&&(next.permissionLevel!=p.permissionLevel||next.explorationLevel!=p.explorationLevel))||
       (locked(S::counts)&&(next.operatorsCount!=p.operatorsCount||next.weaponsCount!=p.weaponsCount||next.archivesCount!=p.archivesCount)))
        throw ProfileError(ProfileFailure::value);
    if(sameProfileValues(next,profile_))return false; // UserProfileStore skips an identical record
    auto saved=persistence_.commit(next);
    saved=normalizedProfile(std::move(saved),text_);validatePersonalProfile(saved,text_);
    if(saved.uid!=profile_.uid)throw ProfileError(ProfileFailure::record);
    adopt(std::move(saved));return true;
}
bool ProfileState::removeImage(ProfileImageKind kind){
    return update([&](PersonalProfile&p){
        if(kind==ProfileImageKind::avatar){p.avatarFilename.reset();p.avatarZoom=1;p.avatarOffsetX=p.avatarOffsetY=0;}
        else{p.backgroundFilename.reset();p.backgroundWidth=600;p.backgroundZoom=p.thumbnailZoom=1;p.backgroundOffsetX=p.backgroundOffsetY=p.thumbnailOffsetX=p.thumbnailOffsetY=0;}});
}
bool ProfileState::imageImported(ProfileImageKind kind,std::string filename,bool preserveAvatarCrop){
    return update([&](PersonalProfile&p){
        if(kind==ProfileImageKind::avatar){p.avatarFilename=std::move(filename);if(!preserveAvatarCrop){p.avatarZoom=1;p.avatarOffsetX=p.avatarOffsetY=0;}}
        else p.backgroundFilename=std::move(filename);});
}
bool ProfileState::setWorkSeconds(double seconds){
    if(!std::isfinite(seconds)||seconds<profile_.accumulatedWorkSeconds||!persistence_.commit)return false;
    try{return update([&](PersonalProfile&p){p.accumulatedWorkSeconds=seconds;});}catch(const std::exception&){return false;}
}
std::uint64_t ProfileState::observe(std::function<void()>fn){const auto id=nextObserver_++;observers_.emplace_back(id,std::move(fn));return id;}
void ProfileState::removeObserver(std::uint64_t id)noexcept{std::erase_if(observers_,[id](const auto&o){return o.first==id;});}
}
