#include "native/archive_dates.hpp"
#include <array>
#include <cmath>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <icu.h>
#else
#include <unicode/udat.h>
#include <unicode/ucal.h>
#endif

namespace endfield::native {
namespace {
constexpr double foundationEpoch=978307200.;
void need(bool value,const char*reason){if(!value)throw std::invalid_argument(reason);}
std::u16string systemZone(){
    std::array<UChar,256>buffer{};UErrorCode error=U_ZERO_ERROR;int32_t length{};
#ifdef _WIN32
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    need(GetDynamicTimeZoneInformation(&zone)!=TIME_ZONE_ID_INVALID,"Cannot read Archive system time zone");
    // Mapping the current OS identifier avoids ICU's process-cached default
    // zone after Windows broadcasts a time-zone change. No global ICU mutation.
    length=ucal_getTimeZoneIDForWindowsID(reinterpret_cast<const UChar*>(zone.TimeZoneKeyName),-1,nullptr,buffer.data(),static_cast<int32_t>(buffer.size()),&error);
#else
    length=ucal_getDefaultTimeZone(buffer.data(),static_cast<int32_t>(buffer.size()),&error);
#endif
    need(U_SUCCESS(error)&&length>0&&length<static_cast<int32_t>(buffer.size()),"Cannot resolve Archive system time zone");
    return {reinterpret_cast<const char16_t*>(buffer.data()),static_cast<std::size_t>(length)};
}
struct DateFormat {UDateFormat*value{};~DateFormat(){if(value)udat_close(value);}};
std::unique_ptr<DateFormat>makeFormat(std::u16string_view zone){
    need(!zone.empty()&&zone.size()<=255&&zone.find(u'\0')==zone.npos,"Invalid Archive time zone");
    auto result=std::make_unique<DateFormat>();UErrorCode error=U_ZERO_ERROR;
    static constexpr char16_t pattern[]=u"yyyy-MM-dd";
    result->value=udat_open(UDAT_PATTERN,UDAT_PATTERN,"en_US_POSIX@calendar=gregorian",reinterpret_cast<const UChar*>(zone.data()),static_cast<int32_t>(zone.size()),reinterpret_cast<const UChar*>(pattern),10,&error);
    need(U_SUCCESS(error)&&result->value,"Cannot create Archive date formatter");
    udat_setLenient(result->value,false);return result;
}
}
struct ArchiveDateFormatter::Impl {
    std::thread::id thread{std::this_thread::get_id()};bool system{};
    std::u16string zone;std::unique_ptr<DateFormat>formatter;
    explicit Impl(std::u16string z):system(z.empty()),zone(system?systemZone():std::move(z)),formatter(makeFormat(zone)){}
    void check()const{need(thread==std::this_thread::get_id(),"Archive date formatter belongs to its creating thread");}
};
ArchiveDateFormatter::ArchiveDateFormatter(std::u16string zone):impl_(std::make_unique<Impl>(std::move(zone))){}
ArchiveDateFormatter::~ArchiveDateFormatter()=default;
std::string ArchiveDateFormatter::format(double seconds)const{
    const auto&i=*impl_;i.check();need(std::isfinite(seconds),"Nonfinite Archive date");
    const auto milliseconds=(seconds+foundationEpoch)*1000.;
    need(std::isfinite(milliseconds),"Archive date exceeds formatter range");
    std::array<UChar,64>buffer{};UErrorCode error=U_ZERO_ERROR;
    const auto length=udat_format(i.formatter->value,milliseconds,buffer.data(),static_cast<int32_t>(buffer.size()),nullptr,&error);
    need(U_SUCCESS(error)&&length>=0&&length<static_cast<int32_t>(buffer.size()),"Cannot format Archive date");
    std::string result;result.reserve(length);for(int32_t n=0;n<length;++n){need(buffer[n]<128,"Unexpected Archive date alphabet");result.push_back(static_cast<char>(buffer[n]));}return result;
}
std::optional<double>ArchiveDateFormatter::parse(std::string_view text)const{
    const auto&i=*impl_;i.check();if(text.empty()||text.size()>=64)return {};
    std::array<UChar,64>buffer{};for(std::size_t n=0;n<text.size();++n){const auto c=static_cast<unsigned char>(text[n]);if(c>=128||c==0)return {};buffer[n]=c;}
    UErrorCode error=U_ZERO_ERROR;int32_t consumed{};
    const auto value=udat_parse(i.formatter->value,buffer.data(),static_cast<int32_t>(text.size()),&consumed,&error);
    if(U_FAILURE(error)||consumed!=static_cast<int32_t>(text.size())||!std::isfinite(value))return {};
    const auto seconds=value/1000.-foundationEpoch;
    if(format(seconds)!=text)return {};return seconds;
}
bool ArchiveDateFormatter::refreshSystemTimeZone(){
    auto&i=*impl_;i.check();if(!i.system)return false;auto zone=systemZone();if(zone==i.zone)return false;
    auto replacement=makeFormat(zone);i.zone=std::move(zone);i.formatter=std::move(replacement);return true;
}
}
