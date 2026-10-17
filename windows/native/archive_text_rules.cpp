#include "native/archive_text_rules.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <icu.h>
#else
#include <unicode/ubrk.h>
#include <unicode/ustring.h>
#include <unicode/unorm2.h>
#include <unicode/uchar.h>
#endif
namespace endfield::native {
namespace {
void need(bool value,const char*m){if(!value)throw std::invalid_argument(m);}
bool space(char16_t c){return (c>=9&&c<=13)||c==0x20||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200b)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;}
std::u16string utf16(std::string_view s){need(s.size()<=modules::archiveMaximumPayloadBytes,"Archive text exceeds payload safety bound");auto value=modules::archiveUTF16(s);need(value.size()<=std::size_t(INT32_MAX),"Archive text exceeds installed ICU length");return value;}
std::u16string trimmed(std::u16string value){std::size_t first{},last=value.size();while(first<last&&space(value[first]))++first;while(last>first&&space(value[last-1]))--last;return value.substr(first,last-first);}
std::size_t characters(std::string_view s){const auto text=utf16(s);UErrorCode error=U_ZERO_ERROR;UBreakIterator*iterator=ubrk_open(UBRK_CHARACTER,"",reinterpret_cast<const UChar*>(text.data()),static_cast<int32_t>(text.size()),&error);if(U_FAILURE(error)||!iterator){if(iterator)ubrk_close(iterator);throw std::runtime_error("Cannot segment Archive characters");}
    struct Close{UBreakIterator*value;~Close(){ubrk_close(value);}}close{iterator};std::size_t count{};ubrk_first(iterator);while(ubrk_next(iterator)!=UBRK_DONE)++count;return count;
}
std::string nameKey(std::string_view s){auto text=trimmed(utf16(s));UErrorCode error=U_ZERO_ERROR;const auto foldedLength=u_strFoldCase(nullptr,0,reinterpret_cast<const UChar*>(text.data()),static_cast<int32_t>(text.size()),U_FOLD_CASE_DEFAULT,&error);need(error==U_BUFFER_OVERFLOW_ERROR||U_SUCCESS(error),"Cannot size Archive case folding");need(foldedLength>=0&&std::size_t(foldedLength)<=modules::archiveMaximumPayloadBytes,"Folded Archive category exceeds bound");std::u16string folded(static_cast<std::size_t>(foldedLength),u'\0');error=U_ZERO_ERROR;const auto actual=u_strFoldCase(reinterpret_cast<UChar*>(folded.data()),foldedLength,reinterpret_cast<const UChar*>(text.data()),static_cast<int32_t>(text.size()),U_FOLD_CASE_DEFAULT,&error);need(U_SUCCESS(error)&&actual==foldedLength,"Cannot fold Archive category");
    error=U_ZERO_ERROR;const auto*normalizer=unorm2_getNFCInstance(&error);need(U_SUCCESS(error)&&normalizer,"Cannot initialize Archive canonical equality");error=U_ZERO_ERROR;const auto length=unorm2_normalize(normalizer,reinterpret_cast<const UChar*>(folded.data()),foldedLength,nullptr,0,&error);need(error==U_BUFFER_OVERFLOW_ERROR||U_SUCCESS(error),"Cannot size normalized Archive category");need(length>=0&&std::size_t(length)<=modules::archiveMaximumPayloadBytes,"Normalized Archive category exceeds bound");std::u16string normalized(static_cast<std::size_t>(length),u'\0');error=U_ZERO_ERROR;const auto result=unorm2_normalize(normalizer,reinterpret_cast<const UChar*>(folded.data()),foldedLength,reinterpret_cast<UChar*>(normalized.data()),length,&error);need(U_SUCCESS(error)&&result==length,"Cannot normalize Archive category");return modules::archiveUTF8(normalized);
}
}
modules::ArchiveTextRules nativeArchiveTextRules(){return {characters,[](std::string_view text){return modules::archiveUTF8(trimmed(utf16(text)));},nameKey};}
std::array<std::uint8_t,4>archiveUnicodeVersion()noexcept{UVersionInfo v{};u_getUnicodeVersion(v);return {v[0],v[1],v[2],v[3]};}
}
