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
using core::Rect;using core::Point;
constexpr std::array<std::string_view,21>ids{"name","tag","playerID","introduction","awakeningDate","birthday","permissionLevel","explorationLevel","operatorsCount","weaponsCount","archivesCount",
    "backgroundWidth","backgroundZoom","backgroundOffsetX","backgroundOffsetY","thumbnailZoom","thumbnailOffsetX","thumbnailOffsetY","avatarZoom","avatarOffsetX","avatarOffsetY"};
constexpr std::array<ProfileField,7>backgroundFields{ProfileField::backgroundWidth,ProfileField::backgroundZoom,ProfileField::backgroundOffsetX,ProfileField::backgroundOffsetY,
    ProfileField::thumbnailZoom,ProfileField::thumbnailOffsetX,ProfileField::thumbnailOffsetY};
constexpr std::array<ProfileField,3>portraitFields{ProfileField::avatarZoom,ProfileField::avatarOffsetX,ProfileField::avatarOffsetY};
constexpr std::array<ProfileField,5>levelsAndCounters{ProfileField::permissionLevel,ProfileField::explorationLevel,ProfileField::operatorsCount,ProfileField::weaponsCount,ProfileField::archivesCount};
constexpr std::array<std::string_view,5>themes{"FAD41F","6EDFE8","A8E58B","C9A2FF","FF8F78"};
constexpr Rect menuRect{20,116,19,19},backgroundMenuRect{243,294,109,24},visibilityRect{364,294,25,24},dateLabelRect{98,77,67,16},introductionActionRect{257,157,132,101};
std::string L(const char*english,const char*chinese,core::Language language){return core::localized(english,chinese,language);}
bool contains(Rect r,Point p)noexcept{return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
double bound(double v,double lo,double hi,double fallback)noexcept{return std::isfinite(v)?std::clamp(v,lo,hi):fallback;}
int maximumBirthdayDay(int month)noexcept{constexpr int days[]{31,29,31,30,31,30,31,31,30,31,30,31};return days[std::clamp(month,1,12)-1];}
std::string printf2(double v,bool zoom){char s[384]{};std::snprintf(s,sizeof s,zoom?"%.2f":"%.0f",v);return s;}
struct Range{double minimum,maximum;};
Range range(ProfileField f)noexcept{
    if(profileZoomField(f))return{1,20};
    switch(f){case ProfileField::backgroundWidth:return{400,900};case ProfileField::backgroundOffsetX:return{-400,400};case ProfileField::backgroundOffsetY:return{-250,250};default:return{-100,100};}
}
bool percentField(ProfileField f)noexcept{return f==ProfileField::avatarOffsetX||f==ProfileField::avatarOffsetY||f==ProfileField::thumbnailOffsetX||f==ProfileField::thumbnailOffsetY;}
double geometry(ProfileField f,const PersonalProfile&p)noexcept{switch(f){
    case ProfileField::backgroundWidth:return p.backgroundWidth;case ProfileField::backgroundZoom:return p.backgroundZoom;
    case ProfileField::backgroundOffsetX:return p.backgroundOffsetX;case ProfileField::backgroundOffsetY:return p.backgroundOffsetY;
    case ProfileField::thumbnailZoom:return p.thumbnailZoom;case ProfileField::thumbnailOffsetX:return p.thumbnailOffsetX*100;case ProfileField::thumbnailOffsetY:return p.thumbnailOffsetY*100;
    case ProfileField::avatarZoom:return p.avatarZoom;case ProfileField::avatarOffsetX:return p.avatarOffsetX*100;case ProfileField::avatarOffsetY:return p.avatarOffsetY*100;
    default:return 0;}}
void applyGeometry(ProfileField f,double v,PersonalProfile&p)noexcept{switch(f){
    case ProfileField::backgroundWidth:p.backgroundWidth=v;break;case ProfileField::backgroundZoom:p.backgroundZoom=v;break;
    case ProfileField::backgroundOffsetX:p.backgroundOffsetX=v;break;case ProfileField::backgroundOffsetY:p.backgroundOffsetY=v;break;
    case ProfileField::thumbnailZoom:p.thumbnailZoom=v;break;case ProfileField::thumbnailOffsetX:p.thumbnailOffsetX=v/100;break;case ProfileField::thumbnailOffsetY:p.thumbnailOffsetY=v/100;break;
    case ProfileField::avatarZoom:p.avatarZoom=v;break;case ProfileField::avatarOffsetX:p.avatarOffsetX=v/100;break;case ProfileField::avatarOffsetY:p.avatarOffsetY=v/100;break;
    default:break;}}
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
std::string failureText(const std::exception&e,core::Language language){
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
    case ProfileFailure::awakeningDate:return L("Use YYYY/MM/DD for awakening day.","苏醒日格式为 YYYY/MM/DD。",language);
    case ProfileFailure::birthday:return L("Use MM/DD for birthday.","生日格式为 MM/DD。",language);
    }
    return {};
}
ProfileError::ProfileError(ProfileFailure failure,std::string detail)
    :std::runtime_error(profileFailureMessage(failure,core::Language::english,detail)),code_(failure),detail_(std::move(detail)){}

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
bool profileZoomField(ProfileField f)noexcept{return f==ProfileField::avatarZoom||f==ProfileField::backgroundZoom||f==ProfileField::thumbnailZoom;}
bool profileDateField(ProfileField f)noexcept{return f==ProfileField::awakeningDate||f==ProfileField::birthday;}
bool profileNumericField(ProfileField f)noexcept{return f!=ProfileField::name&&f!=ProfileField::tag&&f!=ProfileField::playerID&&f!=ProfileField::introduction&&!profileDateField(f);}
std::optional<std::size_t>profileTextLimit(ProfileField f)noexcept{switch(f){case ProfileField::name:return 20;case ProfileField::tag:return 10;case ProfileField::playerID:return 64;case ProfileField::introduction:return 150;default:return std::nullopt;}}
std::span<const std::string_view>profileThemePresets()noexcept{return themes;}
Rect profileFieldRect(ProfileField f){
    switch(f){
    case ProfileField::playerID:return{98,99,260,18};case ProfileField::name:case ProfileField::tag:return{97,37,286,26};
    case ProfileField::introduction:return{264,181,118,59};case ProfileField::awakeningDate:case ProfileField::birthday:return{165,77,90,16};
    case ProfileField::permissionLevel:return{180,145,57,25};case ProfileField::explorationLevel:return{180,177,57,25};
    case ProfileField::operatorsCount:return{18,219,74,42};case ProfileField::weaponsCount:return{96,219,74,42};case ProfileField::archivesCount:return{174,219,74,42};
    default:break;
    }
    // Background popover minY 4: 294 - 6 - (74 + 7 * 30).
    if(const auto i=std::find(backgroundFields.begin(),backgroundFields.end(),f);i!=backgroundFields.end())return{260,4+68+30.*static_cast<double>(i-backgroundFields.begin()),55,23};
    const auto j=std::find(portraitFields.begin(),portraitFields.end(),f);
    return{260,151+30.*static_cast<double>(j-portraitFields.begin()),55,23};
}
Rect profilePortraitCrop(Point image,Point target,double zoom,Point offset)noexcept{
    if(!std::isfinite(image.x)||!std::isfinite(image.y)||!std::isfinite(target.x)||!std::isfinite(target.y)||image.x<=0||image.y<=0||target.x<=0||target.y<=0)return{0,0,1,1};
    const double magnification=bound(zoom,1,20,1),fit=std::max(target.x/image.x,target.y/image.y);
    const double w=std::min(1.,target.x/(image.x*fit))/magnification,h=std::min(1.,target.y/(image.y*fit))/magnification;
    const double x=std::isfinite(offset.x)?std::clamp(offset.x,-1.,1.):0,y=std::isfinite(offset.y)?std::clamp(offset.y,-1.,1.):0;
    return{(1-w)*(x+1)/2,(1-h)*(y+1)/2,w,h};
}
Rect profileBackgroundRect(const PersonalProfile&p)noexcept{return{(400-p.backgroundWidth)/2+p.backgroundOffsetX,p.backgroundOffsetY,p.backgroundWidth,334};}
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
std::string profileEditorText(ProfileField f,std::string_view text,const ProfileTextRules&r){
    const auto limit=profileTextLimit(f);
    if(!limit)return std::string(text);
    if(f==ProfileField::tag&&r.prefix(text,1)=="#")text.remove_prefix(1);
    return r.prefix(text,*limit);
}

ProfileState::ProfileState(PersonalProfile p,ProfileTextRules rules,ProfileDateRules dates,ProfilePersistence io,core::Language language)
    :profile_(normalizedProfile(std::move(p),rules)),preview_(profile_),text_(std::move(rules)),dates_(std::move(dates)),persistence_(std::move(io)),language_(language){
    if(!dates_.noon||!dates_.format)throw std::invalid_argument("Profile Gregorian date rules are required");
    if(language_==core::Language::system)throw std::invalid_argument("Resolve the profile language once in the application owner");
    validatePersonalProfile(profile_,text_);actions_.reserve(16);sliders_.reserve(7);shownHours_=formattedHours();rebuild();
}
bool ProfileState::canEdit(ProfileField f)const noexcept{return !locked_||f==ProfileField::introduction||f==ProfileField::birthday||profileGeometryField(f);}
std::array<double,3>ProfileState::accent()const noexcept{return profileAccent(preview_,hudAccent_);}
void ProfileState::adopt(PersonalProfile next){
    auto preview=next;
    if(dragged_)applyGeometry(*dragged_,geometry(*dragged_,preview_),preview);
    profile_=std::move(next);preview_=std::move(preview);++revision_;++geometryRevision_;rebuild();
}
void ProfileState::refresh(PersonalProfile p,bool locked){
    p=normalizedProfile(std::move(p),text_);validatePersonalProfile(p,text_);
    if(p.uid!=profile_.uid)throw ProfileError(ProfileFailure::record);
    const bool lockChanged=locked!=locked_;locked_=locked;
    if(!sameProfileValues(p,profile_)||p.originalFields!=profile_.originalFields)adopt(std::move(p));
    else if(lockChanged){++revision_;rebuild();}
}
void ProfileState::setSyncLocked(bool v){if(v==locked_)return;locked_=v;++revision_;rebuild();}
void ProfileState::setLanguage(core::Language language){
    if(language==core::Language::system)throw std::invalid_argument("Resolve the profile language once in the application owner");
    if(language==language_)return;language_=language;++revision_;rebuild();
}
void ProfileState::setDateRules(ProfileDateRules dates){
    if(!dates.noon||!dates.format)throw std::invalid_argument("Profile Gregorian date rules are required");
    dates_=std::move(dates);++revision_;rebuild();
}
void ProfileState::setHudAccent(std::array<double,3>value){
    for(const auto c:value)if(!std::isfinite(c)||c<0||c>1)throw std::invalid_argument("Invalid HUD accent");
    if(value==hudAccent_)return;hudAccent_=value;++revision_;
}
void ProfileState::activate(double now){
    if(!std::isfinite(now))throw std::invalid_argument("Invalid profile activation time");
    if(active_)return;active_=true;workDeadline_=now+30;refreshWork();
}
void ProfileState::deactivate(){
    mouseUp();active_=false;workDeadline_.reset();
    if(popover_||request_){popover_.reset();request_.reset();++revision_;rebuild();}
}
std::optional<double>ProfileState::nextWorkRefresh()const noexcept{return active_?workDeadline_:std::nullopt;}
bool ProfileState::wake(double now){
    if(!std::isfinite(now))throw std::invalid_argument("Invalid profile wake time");
    if(!active_||!workDeadline_||now<*workDeadline_)return false;
    // A late repeating timer skips missed fires and keeps its phase.
    *workDeadline_+=30*(std::floor((now-*workDeadline_)/30)+1);
    return refreshWork();
}
bool ProfileState::refreshWork(){
    auto next=formattedHours();if(next==shownHours_)return false;
    shownHours_=std::move(next);++workRevision_;return true;
}
void ProfileState::showError(std::string message){error_=std::move(message);++revision_;}
template<class F>bool ProfileState::update(F&&mutate){
    if(!persistence_.commit)throw ProfileError(ProfileFailure::unavailable);
    auto next=profile_;mutate(next);
    next=normalizedProfile(std::move(next),text_);validatePersonalProfile(next,text_);
    if(next.uid!=profile_.uid)throw ProfileError(ProfileFailure::record);
    if(sameProfileValues(next,profile_))return false; // UserProfileStore skips an identical record
    auto saved=persistence_.commit(next);
    saved=normalizedProfile(std::move(saved),text_);validatePersonalProfile(saved,text_);
    if(saved.uid!=profile_.uid)throw ProfileError(ProfileFailure::record);
    adopt(std::move(saved));return true;
}
bool ProfileState::commit(ProfileField f,std::string_view raw){
    if(!canEdit(f))return false;
    if(!persistence_.commit){showError(profileFailureMessage(ProfileFailure::unavailable,language_));return false;}
    const auto input=text_.trim(raw,true);
    if(profileGeometryField(f))return commitNumber(f,swiftDouble(input).value_or(std::numeric_limits<double>::quiet_NaN()));
    std::optional<double>date;std::optional<std::array<std::int64_t,2>>birthday;
    if(f==ProfileField::awakeningDate){
        if(const auto parts=dateParts<3>(input);parts&&(*parts)[0]>=1&&(*parts)[0]<=9999&&(*parts)[1]>=1&&(*parts)[1]<=12&&(*parts)[2]>=1&&(*parts)[2]<=31)
            date=dates_.noon(static_cast<int>((*parts)[0]),static_cast<int>((*parts)[1]),static_cast<int>((*parts)[2]));
        if(!date){showError(profileFailureMessage(ProfileFailure::awakeningDate,language_));return false;}
    }
    if(f==ProfileField::birthday){
        birthday=dateParts<2>(input);
        if(!birthday||(*birthday)[0]<1||(*birthday)[0]>12||(*birthday)[1]<1||(*birthday)[1]>maximumBirthdayDay(static_cast<int>((*birthday)[0]))){
            showError(profileFailureMessage(ProfileFailure::birthday,language_));return false;}
    }
    const bool hadError=error_.has_value();error_.reset();
    try{
        const bool changed=update([&](PersonalProfile&p){switch(f){
            case ProfileField::playerID:p.playerIDOverride=input;break;
            case ProfileField::name:p.name=text_.prefix(input,20);break;
            case ProfileField::tag:p.tag=text_.prefix(text_.prefix(input,1)=="#"?std::string_view(input).substr(1):std::string_view(input),10);break;
            case ProfileField::introduction:p.introduction=text_.prefix(input,150);break;
            case ProfileField::awakeningDate:p.awakeningDate=*date;p.hasManualAwakeningDate=true;break;
            case ProfileField::birthday:p.birthdayMonth=static_cast<int>((*birthday)[0]);p.birthdayDay=static_cast<int>((*birthday)[1]);break;
            case ProfileField::permissionLevel:p.permissionLevel=static_cast<int>(std::clamp(swiftInteger(input).value_or(60),std::int64_t{1},std::int64_t{60}));break;
            case ProfileField::explorationLevel:p.explorationLevel=static_cast<int>(std::clamp(swiftInteger(input).value_or(7),std::int64_t{1},std::int64_t{7}));break;
            case ProfileField::operatorsCount:p.operatorsCount=std::max(std::int64_t{},swiftInteger(input).value_or(p.operatorsCount));break;
            case ProfileField::weaponsCount:p.weaponsCount=std::max(std::int64_t{},swiftInteger(input).value_or(p.weaponsCount));break;
            case ProfileField::archivesCount:p.archivesCount=std::max(std::int64_t{},swiftInteger(input).value_or(p.archivesCount));break;
            default:break;}});
        if(!changed&&hadError)++revision_;
        return true;
    }catch(const std::exception&e){showError(failureText(e,language_));return false;}
}
// commit(field:text:String(value)) for a finite slider value: Double(String(v))
// round-trips exactly, so the value is applied without a text detour.
bool ProfileState::commitNumber(ProfileField f,double number){
    if(!canEdit(f))return false;
    if(!persistence_.commit){showError(profileFailureMessage(ProfileFailure::unavailable,language_));return false;}
    const bool hadError=error_.has_value();error_.reset();
    try{
        const auto limits=range(f);
        const bool changed=update([&](PersonalProfile&p){applyGeometry(f,std::isfinite(number)?std::clamp(number,limits.minimum,limits.maximum):geometry(f,p),p);});
        if(!changed&&hadError)++revision_;
        return true;
    }catch(const std::exception&e){showError(failureText(e,language_));return false;}
}
bool ProfileState::setThemeColor(std::optional<std::string>hex){
    if(!persistence_.commit)return false;
    try{update([&](PersonalProfile&p){p.themeColorHex=std::move(hex);});return true;}
    catch(const std::exception&e){showError(failureText(e,language_));return false;}
}
bool ProfileState::setCustomColor(double red,double green,double blue){
    if(!std::isfinite(red)||!std::isfinite(green)||!std::isfinite(blue))return false;
    std::array<long long,3>c{std::llround(red*255),std::llround(green*255),std::llround(blue*255)};
    // String(format:"%02X") of an out-of-range Int is not six digits, so the
    // store's normalization follows the HUD theme.
    if(std::any_of(c.begin(),c.end(),[](long long v){return v<0||v>255;}))return setThemeColor(std::string("invalid"));
    char s[8]{};std::snprintf(s,sizeof s,"%02llX%02llX%02llX",c[0],c[1],c[2]);return setThemeColor(std::string(s));
}
bool ProfileState::restoreImage(ProfileImageKind kind){
    if(!persistence_.commit)return false;
    const bool hadError=error_.has_value();error_.reset();
    try{
        const bool changed=update([&](PersonalProfile&p){
            if(kind==ProfileImageKind::avatar){p.avatarFilename.reset();p.avatarZoom=1;p.avatarOffsetX=p.avatarOffsetY=0;}
            else{p.backgroundFilename.reset();p.backgroundWidth=600;p.backgroundZoom=p.thumbnailZoom=1;p.backgroundOffsetX=p.backgroundOffsetY=p.thumbnailOffsetX=p.thumbnailOffsetY=0;}});
        if(!changed&&hadError)++revision_;
        return true;
    }catch(const std::exception&e){showError(failureText(e,language_));return false;}
}
void ProfileState::beginImageImport(){if(error_){error_.reset();++revision_;}}
bool ProfileState::imageImported(ProfileImageKind kind,std::string filename,bool preserveAvatarCrop){
    if(!persistence_.commit){showError(profileFailureMessage(ProfileFailure::unavailable,language_));return false;}
    try{
        update([&](PersonalProfile&p){
            if(kind==ProfileImageKind::avatar){p.avatarFilename=std::move(filename);if(!preserveAvatarCrop){p.avatarZoom=1;p.avatarOffsetX=p.avatarOffsetY=0;}}
            else p.backgroundFilename=std::move(filename);});
        return true;
    }catch(const std::exception&e){showError(failureText(e,language_));return false;}
}
void ProfileState::imageImportFailed(ProfileFailure failure,std::string_view detail){showError(profileFailureMessage(failure,language_,detail));}
void ProfileState::persistenceFailed(ProfileFailure failure,std::string_view detail){showError(profileFailureMessage(failure,language_,detail));}
bool ProfileState::setWorkSeconds(double seconds){
    if(!std::isfinite(seconds)||seconds<profile_.accumulatedWorkSeconds)return false;
    try{update([&](PersonalProfile&p){p.accumulatedWorkSeconds=seconds;});return true;}catch(const std::exception&){return false;}
}
std::optional<Rect>ProfileState::popoverBounds()const noexcept{
    if(!popover_)return std::nullopt;
    switch(*popover_){
    case ProfilePopover::identity:return Rect{18,136,188,locked_?87.:139.};
    case ProfilePopover::background:return Rect{126,4,264,284};
    case ProfilePopover::themeColor:return Rect{126,158,264,130};
    case ProfilePopover::portrait:return Rect{100,111,288,146};
    }
    return std::nullopt;
}
std::string ProfileState::accessibilityStatus()const{return error_?*error_:L("Personal profile","个人名片",language_);}
void ProfileState::rebuild(){
    actions_.clear();sliders_.clear();const auto l=language_;
    const auto action=[&](std::string id,std::string label,Rect r,std::optional<ProfileField>f={}){actions_.push_back({std::move(id),std::move(label),r,f});};
    const auto labeled=[&](ProfileField f,std::string value){return profileFieldTitle(f,l)+": "+value;};
    if(!popover_){
        action("profile:backgroundMenu",L("Change card theme","更换名片主题",l),backgroundMenuRect);
        action("profile:visibility",hidden_?L("Show profile text","显示名片文字",l):L("Hide profile text","隐藏名片文字",l),visibilityRect);
        if(!hidden_){
            const auto date=preview_.showsBirthday?ProfileField::birthday:ProfileField::awakeningDate;
            action("profile:toggleDateLabel",preview_.showsBirthday?L("Show awakening day","切换为苏醒日",l):L("Show birthday","切换为生日",l),dateLabelRect);
            if(canEdit(date))action("profile:"+std::string(profileFieldID(date)),labeled(date,value(date)),profileFieldRect(date),date);
            action("profile:menu",L("Edit personal profile","编辑个人名片",l),menuRect);
            action("profile:introduction",L("Edit introduction","编辑个人介绍",l),introductionActionRect,ProfileField::introduction);
            if(canEdit(ProfileField::playerID))action("profile:playerID",labeled(ProfileField::playerID,preview_.displayedUID()),profileFieldRect(ProfileField::playerID),ProfileField::playerID);
            for(const auto f:levelsAndCounters)if(canEdit(f))action("profile:"+std::string(profileFieldID(f)),labeled(f,value(f)),profileFieldRect(f),f);
        }
    }else if(*popover_==ProfilePopover::identity){
        const std::array<std::pair<std::string_view,std::string>,5>rows{{{"name",L("Edit name","修改名称",l)},{"tag",L("Edit #","修改 #",l)},
            {"avatar",L("Change profile picture","更换头像",l)},{"portraitMenu",L("Adjust portrait","调整头像",l)},{"restoreAvatar",L("Restore default picture","恢复默认头像",l)}}};
        int row{};
        for(const auto&[id,label]:rows){const auto f=profileField(id);if(f&&!canEdit(*f))continue;action("profile:"+std::string(id),label,{23,141+26.*row++,178,24},f);}
        action("profile:popoverClose",L("Close","关闭",l),menuRect);
    }else if(*popover_==ProfilePopover::themeColor){
        action("profile:backgroundMenu",L("Back to card theme","返回名片主题",l),{136,164,23,23});
        action("profile:popoverClose",L("Close","关闭",l),{361,164,23,23});
        action("profile:themeDefault",L("Follow HUD theme","跟随浮层主题",l),{136,250,242,25});
        for(std::size_t n=0;n<themes.size();++n)action("profile:theme:"+std::string(themes[n]),L("Card color ","名片颜色 ",l)+std::string(themes[n]),{138+40.*static_cast<double>(n),199,32,32});
        action("profile:themeCustom",L("Custom card color","自定名片颜色",l),{338,199,32,32});
    }else{
        const bool portrait=*popover_==ProfilePopover::portrait;
        if(portrait){action("profile:menu",L("Back to profile menu","返回名片菜单",l),{110,117,23,23});action("profile:popoverClose",L("Close","关闭",l),{360,117,23,23});}
        else{
            action("profile:popoverClose",L("Close","关闭",l),{361,9,23,23});
            action("profile:background",L("Choose background","选择背景",l),{136,37,116,23});
            action("profile:resetBackground",L("Restore default","恢复默认",l),{259,37,119,23});
            action("profile:themeMenu",L("Card color","名片颜色",l),{331,9,23,23});
        }
        const auto fields=portrait?std::span<const ProfileField>(portraitFields):std::span<const ProfileField>(backgroundFields);
        for(const auto f:fields){
            const auto row=profileFieldRect(f);const auto limits=range(f);
            sliders_.push_back({f,profileFieldTitle(f,l),{portrait?112.:138.,row.y+12,portrait?262.:240.,17},geometry(f,preview_),limits.minimum,limits.maximum,
                profileZoomField(f)?.1:1.,value(f)+(profileZoomField(f)?"×":percentField(f)?"%":"")});
        }
    }
    refreshWork();
}
void ProfileState::open(ProfilePopover p){mouseUp();selected_.reset();popover_=p;++opens_;++revision_;rebuild();}
void ProfileState::dismissPopover(){if(!popover_)return;mouseUp();popover_.reset();++dismissals_;++revision_;rebuild();}
bool ProfileState::containsPopoverPoint(Point p)const noexcept{
    const auto b=popoverBounds();if(!b)return false;
    return contains(*b,p)||std::any_of(actions_.begin(),actions_.end(),[&](const auto&a){return contains(a.rect,p);});
}
bool ProfileState::perform(std::string_view id){
    if(id=="profile:menu")open(ProfilePopover::identity);
    else if(id=="profile:backgroundMenu")open(ProfilePopover::background);
    else if(id=="profile:themeMenu")open(ProfilePopover::themeColor);
    else if(id=="profile:portraitMenu")open(ProfilePopover::portrait);
    else if(id=="profile:popoverClose")dismissPopover();
    else if(id=="profile:avatar"){dismissPopover();request_=ProfileRequest{ProfileRequest::Kind::chooseAvatar,{},{},{},{}};++revision_;}
    else if(id=="profile:background"){request_=ProfileRequest{ProfileRequest::Kind::chooseBackground,{},{},{},{}};++revision_;}
    else if(id=="profile:restoreAvatar"){dismissPopover();restoreImage(ProfileImageKind::avatar);}
    else if(id=="profile:resetBackground")restoreImage(ProfileImageKind::background);
    else if(id=="profile:themeDefault")setThemeColor(std::nullopt);
    else if(id=="profile:themeCustom"){request_=ProfileRequest{ProfileRequest::Kind::chooseColor,{},{},{},accent()};++revision_;}
    else if(id=="profile:toggleDateLabel"){
        if(!persistence_.commit)return true;
        try{update([](PersonalProfile&p){p.showsBirthday=!p.showsBirthday;});}catch(const std::exception&e){showError(failureText(e,language_));}
    }else if(id=="profile:visibility"){hidden_=!hidden_;popover_.reset();++visibilityToggles_;++revision_;rebuild();}
    else if(id.starts_with("profile:theme:"))setThemeColor(std::string(id.substr(14)));
    else{
        if(!id.starts_with("profile:"))return false;
        const auto f=profileField(id.substr(8));
        if(!f)return false;
        if(profileGeometryField(*f)||!canEdit(*f))return true;
        dismissPopover();request_=ProfileRequest{ProfileRequest::Kind::edit,*f,profileFieldRect(*f),value(*f),{}};++revision_;
    }
    return true;
}
bool ProfileState::mouseDown(Point p){
    if(!contains({0,0,400,334},p)){if(popover_){dismissPopover();return true;}return false;}
    for(const auto&s:sliders_)if(contains(s.rect,p)){dragged_=selected_=s.field;moveSlider(p);return true;}
    for(const auto&a:actions_)if(contains(a.rect,p)){const auto id=a.id;perform(id);return true;}
    if(popover_)dismissPopover();
    return true;
}
void ProfileState::moveSlider(Point p){
    if(!dragged_||!std::isfinite(p.x))return;
    for(const auto&s:sliders_)if(s.field==*dragged_){
        const auto fraction=std::min(1.,std::max(0.,(p.x-s.rect.x-4)/(s.rect.width-8)));
        setSlider(s.field,s.minimum+fraction*(s.maximum-s.minimum));return;
    }
}
void ProfileState::mouseDragged(Point p){if(dragged_)moveSlider(p);}
void ProfileState::restoreFromCommitted(){
    if(sameProfileValues(preview_,profile_))return;
    preview_=profile_;++revision_;++geometryRevision_;rebuild();
}
void ProfileState::mouseUp(){
    if(!dragged_)return;
    const auto f=*dragged_;const auto v=geometry(f,preview_);
    // A drag previews in memory and performs one durable save; ownership is
    // cleared before the store notifies observers.
    dragged_.reset();
    commitNumber(f,v);
    restoreFromCommitted();
}
bool ProfileState::setSlider(ProfileField f,double v){
    if(!std::isfinite(v))return false;
    const auto it=std::find_if(sliders_.begin(),sliders_.end(),[&](const auto&s){return s.field==f;});
    if(it==sliders_.end())return false;
    const double precision=profileZoomField(f)?100:1;
    const double bounded=std::round(std::min(it->maximum,std::max(it->minimum,v))*precision)/precision;
    selected_=f;
    if(dragged_==f){
        if(geometry(f,preview_)==bounded)return true;
        applyGeometry(f,bounded,preview_);++geometryRevision_;++revision_;
        it->value=geometry(f,preview_);it->valueDescription=value(f)+(profileZoomField(f)?"×":percentField(f)?"%":"");
        return true;
    }
    return commitNumber(f,bounded);
}
bool ProfileState::nudgeSlider(double direction){
    if(!selected_)return false;
    for(const auto&s:sliders_)if(s.field==*selected_)return setSlider(s.field,s.value+direction*s.step);
    return false;
}
std::optional<ProfileRequest>ProfileState::takeRequest(){auto r=std::move(request_);request_.reset();return r;}
std::string ProfileState::value(ProfileField f)const{return value(f,preview_);}
std::string ProfileState::value(ProfileField f,const PersonalProfile&p)const{
    switch(f){
    case ProfileField::name:return p.name;case ProfileField::tag:return p.tag;case ProfileField::playerID:return p.displayedUID();
    case ProfileField::introduction:return p.introduction;case ProfileField::awakeningDate:return dates_.format(p.awakeningDate);
    case ProfileField::birthday:{char s[32]{};std::snprintf(s,sizeof s,"%02d/%02d",p.birthdayMonth,p.birthdayDay);return s;}
    case ProfileField::permissionLevel:return std::to_string(p.permissionLevel);case ProfileField::explorationLevel:return std::to_string(p.explorationLevel);
    case ProfileField::operatorsCount:return std::to_string(p.operatorsCount);case ProfileField::weaponsCount:return std::to_string(p.weaponsCount);
    case ProfileField::archivesCount:return std::to_string(p.archivesCount);
    default:return printf2(geometry(f,p),profileZoomField(f));
    }
}
std::string ProfileState::formattedHours()const{
    const double live=persistence_.workSeconds?persistence_.workSeconds():0;
    const double seconds=live>=preview_.accumulatedWorkSeconds?live:preview_.accumulatedWorkSeconds; // Swift max(x, y)
    char s[400]{};std::snprintf(s,sizeof s,"%.2f",std::isfinite(seconds)?std::max(0.,seconds)/3600:0.);return s;
}
}
