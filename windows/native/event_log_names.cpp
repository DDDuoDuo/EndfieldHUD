#include "native/event_log_names.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <icu.h>
#else
// Only the isolated portable oracle test uses an installed non-Windows ICU.
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#endif
namespace endfield::native {
namespace {
#include "native/event_log_name_sets.inc"
template<std::size_t N>bool contains(const std::array<ScalarRange,N>&ranges,std::uint32_t value){const auto it=std::lower_bound(ranges.begin(),ranges.end(),value,[](const auto&r,std::uint32_t v){return r.last<v;});return it!=ranges.end()&&it->first<=value;}
void need(bool value,const char*reason){if(!value)throw std::invalid_argument(reason);}
std::uint32_t scalar(std::u16string_view value,std::size_t&at){const auto first=value[at++];if(first<0xd800||first>0xdbff)return first;const auto second=value[at++];return 0x10000+(std::uint32_t(first)-0xd800)*1024+second-0xdc00;}
void append(std::string&out,std::uint32_t value){
    if(value<0x80)out.push_back(static_cast<char>(value));
    else if(value<0x800){out.push_back(char(0xc0|(value>>6)));out.push_back(char(0x80|(value&63)));}
    else if(value<0x10000){out.push_back(char(0xe0|(value>>12)));out.push_back(char(0x80|((value>>6)&63)));out.push_back(char(0x80|(value&63)));}
    else{out.push_back(char(0xf0|(value>>18)));out.push_back(char(0x80|((value>>12)&63)));out.push_back(char(0x80|((value>>6)&63)));out.push_back(char(0x80|(value&63)));}
}
}
struct NativeEventNameCompactor::Impl {
    std::thread::id thread=std::this_thread::get_id();
    std::array<char16_t,4096>text{};
    UBreakIterator*iterator{};
    Impl(){UErrorCode error=U_ZERO_ERROR;iterator=ubrk_open(UBRK_CHARACTER,"",nullptr,0,&error);if(U_FAILURE(error)||!iterator){if(iterator)ubrk_close(iterator);throw std::runtime_error("Cannot initialize system event grapheme iterator");}}
    ~Impl(){if(iterator)ubrk_close(iterator);}
};
NativeEventNameCompactor::NativeEventNameCompactor():impl_(std::make_unique<Impl>()){}
NativeEventNameCompactor::~NativeEventNameCompactor()=default;
std::array<std::uint8_t,4>NativeEventNameCompactor::unicodeVersion()noexcept{UVersionInfo version{};u_getUnicodeVersion(version);return {version[0],version[1],version[2],version[3]};}
std::string NativeEventNameCompactor::compact(std::string_view value){
    auto&i=*impl_;need(std::this_thread::get_id()==i.thread,"Event name compactor belongs to its creating thread");
    need(value.size()<=4096&&ehud::data::Json::validUtf8(value),"Invalid bounded event name UTF-8");
    std::size_t units{};
    for(std::size_t at=0;at<value.size();){const auto first=static_cast<unsigned char>(value[at++]);std::uint32_t code=first;
        if(first>=128){const unsigned count=first<0xe0?2u:first<0xf0?3u:4u;code=first&((1u<<(7-count))-1);for(unsigned n=1;n<count;++n)code=(code<<6)|(static_cast<unsigned char>(value[at++])&63);}
        if(code<0x10000)i.text[units++]=static_cast<char16_t>(code);else{code-=0x10000;i.text[units++]=static_cast<char16_t>(0xd800+(code>>10));i.text[units++]=static_cast<char16_t>(0xdc00+(code&1023));}}
    UErrorCode error=U_ZERO_ERROR;ubrk_setText(i.iterator,reinterpret_cast<const UChar*>(i.text.data()),static_cast<int32_t>(units),&error);
    if(U_FAILURE(error))throw std::runtime_error("Cannot segment bounded event name");
    std::string output,part;output.reserve(160);part.reserve(160);bool lastWasSpace=true;
    auto start=ubrk_first(i.iterator);
    for(unsigned inspected=0;inspected<512;++inspected){const auto end=ubrk_next(i.iterator);if(end==UBRK_DONE)break;
        need(start>=0&&end>start&&static_cast<std::size_t>(end)<=units,"Invalid ICU event character range");
        const auto cluster=std::u16string_view(i.text.data()+start,static_cast<std::size_t>(end-start));bool isSpace=true;part.clear();
        for(std::size_t at=0;at<cluster.size();){const auto code=scalar(cluster,at);isSpace&=contains(sourceSpaceOrControl,code);if(!contains(sourceControls,code))append(part,code);}
        if(isSpace)part=" ";start=end;
        if(part.empty()||(isSpace&&lastWasSpace))continue;if(output.size()+part.size()>160)break;
        output+=part;lastWasSpace=isSpace;
    }
    // Swift trims CharacterSet.whitespaces at both edges after cluster-level
    // filtering. Work in code points here so leading combining marks survive.
    const auto trimLeft=[&](std::string_view s){std::size_t at{};while(at<s.size()){const auto begin=at;const auto first=static_cast<unsigned char>(s[at++]);std::uint32_t code=first;if(first>=128){const unsigned n=first<0xe0?2u:first<0xf0?3u:4u;code=first&((1u<<(7-n))-1);for(unsigned k=1;k<n;++k)code=(code<<6)|(static_cast<unsigned char>(s[at++])&63);}if(!contains(sourceWhitespace,code))return begin;}return at;};
    const auto leading=trimLeft(output);if(leading)output.erase(0,leading);
    std::size_t at{},lastNonspace{};while(at<output.size()){const auto first=static_cast<unsigned char>(output[at++]);std::uint32_t code=first;if(first>=128){const unsigned n=first<0xe0?2u:first<0xf0?3u:4u;code=first&((1u<<(7-n))-1);for(unsigned k=1;k<n;++k)code=(code<<6)|(static_cast<unsigned char>(output[at++])&63);}if(!contains(sourceWhitespace,code))lastNonspace=at;}output.resize(lastNonspace);return output;
}
}
