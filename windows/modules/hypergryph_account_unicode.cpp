#include "modules/hypergryph_account_unicode.hpp"
#include <algorithm>
#include <stdexcept>

namespace endfield::modules::hypergryph {
namespace {
#include "modules/hypergryph_account_unicode.inc"
}
std::span<const UnicodeRange> unicodeRanges(UnicodeSet set) noexcept {
    switch(set) {
        case UnicodeSet::alphanumerics: return alphanumericsRanges;
        case UnicodeSet::whitespaces: return whitespacesRanges;
        case UnicodeSet::whitespacesAndNewlines: return whitespacesAndNewlinesRanges;
        case UnicodeSet::controlCharacters: return controlCharactersRanges;
        case UnicodeSet::format: return formatRanges;
        case UnicodeSet::icuDigit: return icuDigitRanges;
        case UnicodeSet::icuSpace: return icuSpaceRanges;
        case UnicodeSet::prependBeforeHash: return prependBeforeHashRanges;
        case UnicodeSet::joinsAfterBase: return joinsAfterBaseRanges;
        case UnicodeSet::digitEquivalent0: return digitEquivalent0Ranges;
        case UnicodeSet::digitEquivalent1: return digitEquivalent1Ranges;
        case UnicodeSet::digitEquivalent2: return digitEquivalent2Ranges;
        case UnicodeSet::digitEquivalent3: return digitEquivalent3Ranges;
        case UnicodeSet::digitEquivalent4: return digitEquivalent4Ranges;
        case UnicodeSet::digitEquivalent5: return digitEquivalent5Ranges;
        case UnicodeSet::digitEquivalent6: return digitEquivalent6Ranges;
        case UnicodeSet::digitEquivalent7: return digitEquivalent7Ranges;
        case UnicodeSet::digitEquivalent8: return digitEquivalent8Ranges;
        case UnicodeSet::digitEquivalent9: return digitEquivalent9Ranges;
    }
    return {};
}
bool unicodeContains(UnicodeSet set,char32_t value) noexcept {
    const auto ranges=unicodeRanges(set);
    const auto found=std::upper_bound(ranges.begin(),ranges.end(),value,[](char32_t v,const UnicodeRange& r){return v<r.first;});
    return found!=ranges.begin()&&value<=std::prev(found)->last;
}
std::optional<DecodedScalar> decodeUTF8(std::string_view text,std::size_t at) noexcept {
    if(at>=text.size()) return std::nullopt;
    const auto b=static_cast<unsigned char>(text[at]);
    if(b<0x80) return DecodedScalar{b,1};
    std::size_t length;char32_t value,minimum;
    if(b>=0xc2&&b<=0xdf) {length=2;value=b&31;minimum=0x80;}
    else if(b>=0xe0&&b<=0xef) {length=3;value=b&15;minimum=0x800;}
    else if(b>=0xf0&&b<=0xf4) {length=4;value=b&7;minimum=0x10000;}
    else return std::nullopt;
    if(length>text.size()-at) return std::nullopt;
    for(std::size_t i=1;i<length;++i) {
        const auto c=static_cast<unsigned char>(text[at+i]);if((c&0xc0)!=0x80) return std::nullopt;value=(value<<6)|(c&63);
    }
    if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff)) return std::nullopt;
    return DecodedScalar{value,length};
}
bool validUTF8(std::string_view text) noexcept {
    for(std::size_t at=0;at<text.size();) {const auto s=decodeUTF8(text,at);if(!s) return false;at+=s->length;}
    return true;
}
void appendUTF8(std::string& out,char32_t v) {
    if(v>0x10ffff||(v>=0xd800&&v<=0xdfff)) throw std::invalid_argument("Invalid Unicode scalar");
    if(v<0x80) out.push_back(static_cast<char>(v));
    else if(v<0x800) {out.push_back(static_cast<char>(0xc0|(v>>6)));out.push_back(static_cast<char>(0x80|(v&63)));}
    else if(v<0x10000) {out.push_back(static_cast<char>(0xe0|(v>>12)));out.push_back(static_cast<char>(0x80|((v>>6)&63)));out.push_back(static_cast<char>(0x80|(v&63)));}
    else {out.push_back(static_cast<char>(0xf0|(v>>18)));out.push_back(static_cast<char>(0x80|((v>>12)&63)));out.push_back(static_cast<char>(0x80|((v>>6)&63)));out.push_back(static_cast<char>(0x80|(v&63)));}
}
std::string trimScalars(std::string_view text,UnicodeSet set) {
    std::size_t begin=0,end=text.size();
    while(begin<end) {const auto s=decodeUTF8(text,begin);if(!s||!unicodeContains(set,s->value)) break;begin+=s->length;}
    while(end>begin) {
        std::size_t start=end-1;while(start>begin&&(static_cast<unsigned char>(text[start])&0xc0)==0x80) --start;
        const auto s=decodeUTF8(text,start);if(!s||start+s->length!=end||!unicodeContains(set,s->value)) break;end=start;
    }
    return std::string(text.substr(begin,end-begin));
}
}
