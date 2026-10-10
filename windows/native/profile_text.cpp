#include "native/profile_text.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#include <icu.h>
#else
#include <unicode/ubrk.h>
#include <unicode/ucal.h>
#include <unicode/uchar.h>
#include <unicode/udat.h>
#include <unicode/ustring.h>
#include <unicode/uversion.h>
#endif
namespace endfield::native {
namespace {
constexpr double referenceEpoch=978307200.;
void need(bool b,const char*m){if(!b)throw std::invalid_argument(m);}
std::u16string utf16(std::string_view s){
    need(s.size()<=32768&&ehud::data::Json::validUtf8(s),"Profile text exceeds retained payload safety bound or is not UTF8");
    if(s.empty())return {};
    UErrorCode e=U_ZERO_ERROR;int32_t n{};u_strFromUTF8(nullptr,0,&n,s.data(),static_cast<int32_t>(s.size()),&e);
    need(e==U_BUFFER_OVERFLOW_ERROR||U_SUCCESS(e),"Cannot size Profile UTF16");
    std::u16string out(static_cast<std::size_t>(n),u'\0');e=U_ZERO_ERROR;
    u_strFromUTF8(reinterpret_cast<UChar*>(out.data()),n,nullptr,s.data(),static_cast<int32_t>(s.size()),&e);need(U_SUCCESS(e),"Cannot decode Profile UTF16");return out;
}
std::string utf8(std::u16string_view s){
    if(s.empty())return {};
    UErrorCode e=U_ZERO_ERROR;int32_t n{};u_strToUTF8(nullptr,0,&n,reinterpret_cast<const UChar*>(s.data()),static_cast<int32_t>(s.size()),&e);
    need(e==U_BUFFER_OVERFLOW_ERROR||U_SUCCESS(e),"Cannot size Profile UTF8");
    std::string out(static_cast<std::size_t>(n),'\0');e=U_ZERO_ERROR;
    u_strToUTF8(out.data(),n,nullptr,reinterpret_cast<const UChar*>(s.data()),static_cast<int32_t>(s.size()),&e);need(U_SUCCESS(e),"Cannot encode Profile UTF8");return out;
}
// CoreFoundation's predefined CharacterSet.newlines and .whitespaces tables.
// Its whitespace list is fixed (it includes U+200B and excludes U+180E/U+FEFF)
// rather than following the current Unicode Zs category.
bool newline(char16_t c){return (c>=10&&c<=13)||c==0x85||c==0x2028||c==0x2029;}
bool whitespace(char16_t c){return c==9||c==0x20||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200b)||c==0x202f||c==0x205f||c==0x3000;}
struct Breaks{UBreakIterator*p{};~Breaks(){if(p)ubrk_close(p);}};
// Characters counted and the UTF16 end of the first maximum extended grapheme clusters.
std::pair<std::size_t,std::size_t>boundary(std::u16string_view s,std::size_t maximum){
    if(s.empty())return {0,0};
    UErrorCode e=U_ZERO_ERROR;Breaks b{ubrk_open(UBRK_CHARACTER,"",reinterpret_cast<const UChar*>(s.data()),static_cast<int32_t>(s.size()),&e)};
    need(U_SUCCESS(e)&&b.p,"Cannot segment Profile Characters");
    std::size_t count{};int32_t end=ubrk_first(b.p);
    while(count<maximum){const auto next=ubrk_next(b.p);if(next==UBRK_DONE)return {count,s.size()};end=next;++count;}
    return {count,static_cast<std::size_t>(end)};
}
bool anyScalar(std::u16string_view v,bool(*test)(UChar32)){
    for(int32_t n=0;n<static_cast<int32_t>(v.size());){UChar32 c{};U16_NEXT(reinterpret_cast<const UChar*>(v.data()),n,static_cast<int32_t>(v.size()),c);if(test(c))return true;}
    return false;
}
struct Calendar{UCalendar*p{};~Calendar(){if(p)ucal_close(p);}};
struct Formatter{UDateFormat*p{};~Formatter(){if(p)udat_close(p);}};
}
modules::ProfileTextRules nativeProfileTextRules(){
    modules::ProfileTextRules rules;
    rules.characters=[](std::string_view s){return boundary(utf16(s),std::numeric_limits<std::size_t>::max()).first;};
    rules.prefix=[](std::string_view s,std::size_t n){const auto v=utf16(s);return utf8(std::u16string_view(v).substr(0,boundary(v,n).second));};
    rules.trim=[](std::string_view s,bool lines){
        const auto v=utf16(s);std::size_t begin{},end=v.size();
        while(begin<end&&(whitespace(v[begin])||(lines&&newline(v[begin]))))++begin;
        while(end>begin&&(whitespace(v[end-1])||(lines&&newline(v[end-1]))))--end;
        return utf8(std::u16string_view(v).substr(begin,end-begin));
    };
    rules.removeNewlines=[](std::string_view s){auto v=utf16(s);v.erase(std::remove_if(v.begin(),v.end(),newline),v.end());return utf8(v);};
    // CharacterSet.controlCharacters: General Categories Cc and Cf.
    rules.hasControl=[](std::string_view s){return anyScalar(utf16(s),[](UChar32 c){const auto t=u_charType(c);return t==U_CONTROL_CHAR||t==U_FORMAT_CHAR;});};
    rules.hasWhitespace=[](std::string_view s){const auto v=utf16(s);return std::any_of(v.begin(),v.end(),[](char16_t c){return whitespace(c)||newline(c);});};
    // String.uppercased(): locale-independent full case mapping.
    rules.uppercase=[](std::string_view s){
        const auto v=utf16(s);if(v.empty())return std::string{};
        UErrorCode e=U_ZERO_ERROR;const auto n=u_strToUpper(nullptr,0,reinterpret_cast<const UChar*>(v.data()),static_cast<int32_t>(v.size()),"",&e);
        need(e==U_BUFFER_OVERFLOW_ERROR||U_SUCCESS(e),"Cannot size Profile uppercase");
        std::u16string out(static_cast<std::size_t>(n),u'\0');e=U_ZERO_ERROR;
        u_strToUpper(reinterpret_cast<UChar*>(out.data()),n,reinterpret_cast<const UChar*>(v.data()),static_cast<int32_t>(v.size()),"",&e);
        need(U_SUCCESS(e),"Cannot map Profile uppercase");return utf8(out);
    };
    return rules;
}
modules::ProfileDateRules nativeProfileDateRules(std::u16string zone){
    if(zone.empty()){
        std::array<UChar,256>out{};UErrorCode e=U_ZERO_ERROR;const auto n=ucal_getDefaultTimeZone(out.data(),static_cast<int32_t>(out.size()),&e);
        need(U_SUCCESS(e)&&n>0&&n<static_cast<int32_t>(out.size()),"Cannot resolve Profile system time zone");
        zone.assign(reinterpret_cast<const char16_t*>(out.data()),static_cast<std::size_t>(n));
    }
    need(zone.size()<=255&&zone.find(u'\0')==std::u16string::npos,"Invalid Profile time zone");
    UErrorCode e=U_ZERO_ERROR;auto calendar=std::make_shared<Calendar>();
    calendar->p=ucal_open(reinterpret_cast<const UChar*>(zone.data()),static_cast<int32_t>(zone.size()),"en_US_POSIX",UCAL_GREGORIAN,&e);
    need(U_SUCCESS(e)&&calendar->p,"Cannot create Profile Gregorian calendar");
    // Foundation computes a lenient date, then rejects a changed day; the
    // non-lenient ICU calendar rejects the same nonexistent local dates.
    ucal_setAttribute(calendar->p,UCAL_LENIENT,0);
    auto formatter=std::make_shared<Formatter>();constexpr char16_t pattern[]=u"yyyy/MM/dd";e=U_ZERO_ERROR;
    formatter->p=udat_open(UDAT_PATTERN,UDAT_PATTERN,"en_US_POSIX",reinterpret_cast<const UChar*>(zone.data()),static_cast<int32_t>(zone.size()),
        reinterpret_cast<const UChar*>(pattern),10,&e);
    need(U_SUCCESS(e)&&formatter->p,"Cannot create Profile date formatter");
    modules::ProfileDateRules rules;
    rules.noon=[calendar](int year,int month,int day)->std::optional<double>{
        if(year<1||year>9999||month<1||month>12||day<1||day>31)return std::nullopt;
        UErrorCode e=U_ZERO_ERROR;ucal_clear(calendar->p);ucal_setDateTime(calendar->p,year,month-1,day,12,0,0,&e);
        const auto t=ucal_getMillis(calendar->p,&e);
        if(U_FAILURE(e)||!std::isfinite(t))return std::nullopt;
        const auto y=ucal_get(calendar->p,UCAL_EXTENDED_YEAR,&e),m=ucal_get(calendar->p,UCAL_MONTH,&e),d=ucal_get(calendar->p,UCAL_DATE,&e);
        if(U_FAILURE(e)||y!=year||m!=month-1||d!=day)return std::nullopt;
        return t/1000-referenceEpoch;
    };
    rules.format=[formatter](double t){
        need(std::isfinite(t)&&std::isfinite((t+referenceEpoch)*1000),"Invalid Profile date");
        std::array<UChar,128>out{};UErrorCode e=U_ZERO_ERROR;
        const auto n=udat_format(formatter->p,(t+referenceEpoch)*1000,out.data(),static_cast<int32_t>(out.size()),nullptr,&e);
        need(U_SUCCESS(e)&&n>=0&&n<static_cast<int32_t>(out.size()),"Cannot format Profile date");
        return utf8({reinterpret_cast<const char16_t*>(out.data()),static_cast<std::size_t>(n)});
    };
    return rules;
}
std::array<std::uint8_t,4>profileUnicodeVersion()noexcept{UVersionInfo v{};u_getUnicodeVersion(v);return{v[0],v[1],v[2],v[3]};}
}
