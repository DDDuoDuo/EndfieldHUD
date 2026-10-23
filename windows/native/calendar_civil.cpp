#include "native/calendar_civil.hpp"
#include <cmath>
#include <limits>
#include <map>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <icu.h>
#else
#include <unicode/ubrk.h>
#include <unicode/ucal.h>
#include <unicode/udat.h>
#include <unicode/udatpg.h>
#include <unicode/ustring.h>
#include <unicode/uloc.h>
#endif
namespace endfield::native {
namespace {using D=modules::CalendarDay;constexpr double epoch=modules::calendarFoundationToUnix;
void need(bool b,const char*m){if(!b)throw std::runtime_error(m);}
std::u16string utf16(std::string_view s){need(ehud::data::Json::validUtf8(s)&&s.size()<=modules::calendarMaximumBytes,"Invalid Calendar UTF8");UErrorCode e=U_ZERO_ERROR;int32_t n{};u_strFromUTF8(nullptr,0,&n,s.data(),static_cast<int32_t>(s.size()),&e);need(e==U_BUFFER_OVERFLOW_ERROR||U_SUCCESS(e),"Cannot size Calendar UTF16");std::u16string out(n,u'\0');e=U_ZERO_ERROR;u_strFromUTF8(reinterpret_cast<UChar*>(out.data()),n,nullptr,s.data(),static_cast<int32_t>(s.size()),&e);need(U_SUCCESS(e),"Cannot convert Calendar UTF16");return out;}
std::string utf8(std::u16string_view s){need(s.size()<=modules::calendarMaximumBytes,"Calendar UTF16 exceeds file bound");UErrorCode e=U_ZERO_ERROR;int32_t n{};u_strToUTF8(nullptr,0,&n,reinterpret_cast<const UChar*>(s.data()),static_cast<int32_t>(s.size()),&e);need(e==U_BUFFER_OVERFLOW_ERROR||U_SUCCESS(e),"Cannot size Calendar UTF8");std::string out(n,'\0');e=U_ZERO_ERROR;u_strToUTF8(out.data(),n,nullptr,reinterpret_cast<const UChar*>(s.data()),static_cast<int32_t>(s.size()),&e);need(U_SUCCESS(e),"Cannot convert Calendar UTF8");return out;}
bool space(char16_t c){return(c>=9&&c<=13)||c==0x20||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200b)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;}
struct Break {UBreakIterator*p{};~Break(){if(p)ubrk_close(p);}};
std::pair<std::size_t,std::size_t>boundary(std::u16string_view s,std::size_t limit){UErrorCode e=U_ZERO_ERROR;Break b{ubrk_open(UBRK_CHARACTER,"",reinterpret_cast<const UChar*>(s.data()),static_cast<int32_t>(s.size()),&e)};need(U_SUCCESS(e)&&b.p,"Cannot segment Calendar Characters");std::size_t count{};auto end=ubrk_first(b.p);while(count<limit){const auto n=ubrk_next(b.p);if(n==UBRK_DONE)return{count,s.size()};end=n;++count;}return{count,static_cast<std::size_t>(end)};}
struct Cal {UCalendar*p{};~Cal(){if(p)ucal_close(p);}};
std::unique_ptr<Cal>calendar(std::u16string_view z,std::string_view locale){need(!z.empty()&&z.size()<=255&&z.find(u'\0')==z.npos&&!locale.empty()&&locale.size()<=128&&locale.find('\0')==locale.npos,"Invalid Calendar zone/locale");UErrorCode e=U_ZERO_ERROR;auto c=std::make_unique<Cal>();c->p=ucal_open(reinterpret_cast<const UChar*>(z.data()),static_cast<int32_t>(z.size()),std::string(locale).c_str(),UCAL_GREGORIAN,&e);need(U_SUCCESS(e)&&c->p,"Cannot create Gregorian Calendar");ucal_setAttribute(c->p,UCAL_LENIENT,0);return c;}
std::u16string systemZone(){std::array<UChar,256>out{};UErrorCode e=U_ZERO_ERROR;int32_t n{};
#ifdef _WIN32
    DYNAMIC_TIME_ZONE_INFORMATION z{};need(GetDynamicTimeZoneInformation(&z)!=TIME_ZONE_ID_INVALID,"Cannot read Calendar time zone");n=ucal_getTimeZoneIDForWindowsID(reinterpret_cast<const UChar*>(z.TimeZoneKeyName),-1,nullptr,out.data(),static_cast<int32_t>(out.size()),&e);
#else
    n=ucal_getDefaultTimeZone(out.data(),static_cast<int32_t>(out.size()),&e);
#endif
    need(U_SUCCESS(e)&&n>0&&n<static_cast<int32_t>(out.size()),"Cannot resolve Calendar time zone");return{reinterpret_cast<const char16_t*>(out.data()),static_cast<std::size_t>(n)};}
std::string systemLocale(){
#ifdef _WIN32
    std::array<wchar_t,LOCALE_NAME_MAX_LENGTH>name{};need(GetUserDefaultLocaleName(name.data(),static_cast<int>(name.size()))>0,"Cannot read Calendar locale");std::string out;for(auto c:name){if(!c)break;need(c<128,"Unexpected Calendar locale alphabet");out.push_back(c=='-'?'_':static_cast<char>(c));}return out;
#else
    return uloc_getDefault();
#endif
}
struct Format {UDateFormat*p{};~Format(){if(p)udat_close(p);}};
// HUDCalendarCanvas.locale: the UI language's locale, Gregorian.
std::string displayLocale(core::Language l){switch(l){case core::Language::simplifiedChinese:return "zh_CN@calendar=gregorian";case core::Language::traditionalChinese:return "zh_TW@calendar=gregorian";case core::Language::japanese:return "ja_JP@calendar=gregorian";case core::Language::korean:return "ko_KR@calendar=gregorian";default:return "en_US@calendar=gregorian";}}
// DateFormatter.setLocalizedDateFormatFromTemplate: the generator's best pattern for the skeleton.
std::unique_ptr<Format>dateFormat(std::u16string_view z,const std::string&locale,std::u16string_view skeleton){UErrorCode e=U_ZERO_ERROR;auto*generator=udatpg_open(locale.c_str(),&e);need(U_SUCCESS(e)&&generator,"Cannot create Calendar date pattern");struct End{UDateTimePatternGenerator*p;~End(){udatpg_close(p);}}end{generator};std::array<UChar,128>pattern{};const auto n=udatpg_getBestPattern(generator,reinterpret_cast<const UChar*>(skeleton.data()),static_cast<int32_t>(skeleton.size()),pattern.data(),static_cast<int32_t>(pattern.size()),&e);need(U_SUCCESS(e)&&n>0&&n<static_cast<int32_t>(pattern.size()),"Cannot resolve Calendar date pattern");auto f=std::make_unique<Format>();f->p=udat_open(UDAT_PATTERN,UDAT_PATTERN,locale.c_str(),reinterpret_cast<const UChar*>(z.data()),static_cast<int32_t>(z.size()),pattern.data(),n,&e);need(U_SUCCESS(e)&&f->p,"Cannot create Calendar date format");return f;}
modules::CalendarTimeZone zone(std::u16string id,std::string locale){const auto identity=utf8(id);return{identity,[id,locale](D d,unsigned hour,unsigned minute)->std::optional<double>{if(!d.valid()||hour>23||minute>59)return{};auto c=calendar(id,locale);UErrorCode e=U_ZERO_ERROR;ucal_clear(c->p);ucal_setDateTime(c->p,d.year,d.month-1,d.day,static_cast<int32_t>(hour),static_cast<int32_t>(minute),0,&e);const auto value=ucal_getMillis(c->p,&e);if(U_FAILURE(e)||!std::isfinite(value))return {};return value/1000-epoch;},[id,locale](double seconds){need(std::isfinite(seconds)&&std::isfinite((seconds+epoch)*1000),"Nonfinite Calendar timestamp");auto c=calendar(id,locale);UErrorCode e=U_ZERO_ERROR;ucal_setMillis(c->p,(seconds+epoch)*1000,&e);D d{ucal_get(c->p,UCAL_YEAR,&e),ucal_get(c->p,UCAL_MONTH,&e)+1,ucal_get(c->p,UCAL_DATE,&e)};need(U_SUCCESS(e)&&d.valid(),"Calendar timestamp exceeds supported years");return d;}};}
}
modules::CalendarTextRules nativeCalendarTextRules(){return{[](std::string_view s){return boundary(utf16(s),std::numeric_limits<std::size_t>::max()).first;},[](std::string_view s){auto v=utf16(s);std::size_t begin{},end=v.size();while(begin<end&&space(v[begin]))++begin;while(end>begin&&space(v[end-1]))--end;return utf8(std::u16string_view(v).substr(begin,end-begin));},[](std::string_view s,std::size_t limit){const auto v=utf16(s);return utf8(std::u16string_view(v).substr(0,boundary(v,limit).second));}};}
std::array<std::uint8_t,4>calendarUnicodeVersion()noexcept{UVersionInfo v{};u_getUnicodeVersion(v);return{v[0],v[1],v[2],v[3]};}
struct CalendarCivilContext::Impl {std::thread::id owner{std::this_thread::get_id()};bool currentZone{},currentLocale{};std::u16string id;std::string locale;unsigned first{};
    std::map<std::pair<core::Language,std::u16string>,std::unique_ptr<Format>>formats;
    Impl(std::u16string z,std::string l):currentZone(z.empty()),currentLocale(l.empty()),id(currentZone?systemZone():std::move(z)),locale(currentLocale?systemLocale():std::move(l)){auto c=calendar(id,locale);first=static_cast<unsigned>(ucal_getAttribute(c->p,UCAL_FIRST_DAY_OF_WEEK));}
    void check()const{need(owner==std::this_thread::get_id(),"Calendar labels belong to their creating owner");}
    Format&formatter(core::Language l,std::u16string_view skeleton){auto&f=formats[{l,std::u16string(skeleton)}];if(!f)f=dateFormat(id,displayLocale(l),skeleton);return *f;}
    // HUDCalendarDay.date(in:): the day at 12:00 in the zone, also past the stored range.
    UDate noon(D d)const{auto c=calendar(id,"en_US@calendar=gregorian");ucal_setAttribute(c->p,UCAL_LENIENT,1);UErrorCode e=U_ZERO_ERROR;ucal_clear(c->p);ucal_setDateTime(c->p,d.year,d.month-1,d.day,12,0,0,&e);const auto v=ucal_getMillis(c->p,&e);need(U_SUCCESS(e)&&std::isfinite(v),"Invalid Calendar label date");return v;}
};
CalendarCivilContext::CalendarCivilContext(std::u16string z,std::string l):impl_(std::make_unique<Impl>(std::move(z),std::move(l))){}CalendarCivilContext::~CalendarCivilContext()=default;
modules::CalendarTimeZone CalendarCivilContext::timeZone()const{impl_->check();return zone(impl_->id,impl_->locale);}unsigned CalendarCivilContext::firstWeekday()const noexcept{return impl_->first;}
std::string CalendarCivilContext::format(D d,std::string_view skeleton,core::Language l)const{auto&i=*impl_;i.check();need(!skeleton.empty()&&skeleton.size()<32,"Invalid Calendar skeleton");const std::u16string pattern(skeleton.begin(),skeleton.end());auto&f=i.formatter(l,pattern);std::array<UChar,128>out{};UErrorCode e=U_ZERO_ERROR;const auto n=udat_format(f.p,i.noon(d),out.data(),static_cast<int32_t>(out.size()),nullptr,&e);need(U_SUCCESS(e)&&n>=0&&n<static_cast<int32_t>(out.size()),"Cannot format Calendar date");return utf8({reinterpret_cast<const char16_t*>(out.data()),static_cast<std::size_t>(n)});}
namespace {template<std::size_t N>std::array<std::string,N>symbols(UDateFormat*f,UDateFormatSymbolType type,int32_t first){std::array<std::string,N>result;for(std::size_t index=0;index<N;++index){std::array<UChar,64>out{};UErrorCode e=U_ZERO_ERROR;const auto n=udat_getSymbols(f,type,first+static_cast<int32_t>(index),out.data(),static_cast<int32_t>(out.size()),&e);need(U_SUCCESS(e)&&n>=0&&n<static_cast<int32_t>(out.size()),"Cannot read Calendar symbols");result[index]=utf8({reinterpret_cast<const char16_t*>(out.data()),static_cast<std::size_t>(n)});}return result;}}
std::array<std::string,7>CalendarCivilContext::shortWeekdays(core::Language l)const{auto&i=*impl_;i.check();return symbols<7>(i.formatter(l,u"EEE").p,UDAT_STANDALONE_SHORT_WEEKDAYS,1);}
std::array<std::string,12>CalendarCivilContext::shortMonths(core::Language l)const{auto&i=*impl_;i.check();return symbols<12>(i.formatter(l,u"MMM").p,UDAT_STANDALONE_SHORT_MONTHS,0);}
modules::CalendarFormats CalendarCivilContext::formats()const{return{[this](D d,std::string_view skeleton,core::Language l){return format(d,skeleton,l);},[this](core::Language l){return shortWeekdays(l);},[this](core::Language l){return shortMonths(l);},[this]{return firstWeekday();}};}
bool CalendarCivilContext::refreshSystemContext(){auto&i=*impl_;i.check();auto z=i.currentZone?systemZone():i.id;auto l=i.currentLocale?systemLocale():i.locale;if(z==i.id&&l==i.locale)return false;auto c=calendar(z,l);i.id=std::move(z);i.locale=std::move(l);i.first=static_cast<unsigned>(ucal_getAttribute(c->p,UCAL_FIRST_DAY_OF_WEEK));i.formats.clear();return true;}
}
