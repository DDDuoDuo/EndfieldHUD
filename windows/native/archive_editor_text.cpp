#include "native/archive_editor_text.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <icu.h>
#else
#include <unicode/ubrk.h>
#endif

namespace endfield::native {
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
struct Breaks {UBreakIterator*value{};~Breaks(){if(value)ubrk_close(value);}};
}
ArchiveEditorTextClamp clampArchiveEditorText(std::u16string_view text,ArchiveEditorTextKind kind){
 need(core::text::Buffer::validUTF16(text)&&text.size()<=modules::archiveMaximumPayloadBytes,"Invalid or oversized Archive edit text");
 ArchiveEditorTextClamp result;result.text.reserve(text.size());std::vector<std::uint32_t>original;original.reserve(text.size());
 for(std::size_t n=0;n<text.size();++n)if(kind==ArchiveEditorTextKind::body||text[n]!=0){result.text.push_back(text[n]);original.push_back(static_cast<std::uint32_t>(n));}
 std::size_t end=result.text.size();
 if(kind==ArchiveEditorTextKind::body){std::size_t bytes{};end=0;while(end<result.text.size()){const auto c=result.text[end];const std::size_t units=c>=0xd800&&c<=0xdbff?2:1,cost=units==2?4:c<0x80?1:c<0x800?2:3;if(cost>modules::archiveMaximumBodyBytes-bytes)break;bytes+=cost;end+=units;}}
 else {need(result.text.size()<=static_cast<std::size_t>(std::numeric_limits<int32_t>::max()),"Archive edit exceeds ICU length");UErrorCode error=U_ZERO_ERROR;Breaks breaks{ubrk_open(UBRK_CHARACTER,"",reinterpret_cast<const UChar*>(result.text.data()),static_cast<int32_t>(result.text.size()),&error)};need(U_SUCCESS(error)&&breaks.value,"Cannot establish Archive grapheme boundaries");const auto limit=kind==ArchiveEditorTextKind::title?200u:40u;std::size_t count{};int32_t boundary=ubrk_first(breaks.value);while(count<limit){const auto next=ubrk_next(breaks.value);if(next==UBRK_DONE){boundary=static_cast<int32_t>(result.text.size());break;}boundary=next;++count;}end=static_cast<std::size_t>(boundary);}
 // Record removed units as coalesced original ranges, then reverse so every
 // caller replacement uses the original indices even when NULs were stripped.
 std::size_t kept{};std::optional<std::uint32_t>begin;for(std::uint32_t n=0;n<text.size();++n){const bool retain=kept<end&&original[kept]==n;if(retain){if(begin){result.removed.push_back({*begin,n});begin.reset();}++kept;}else if(!begin)begin=n;}
 if(begin)result.removed.push_back({*begin,static_cast<std::uint32_t>(text.size())});std::reverse(result.removed.begin(),result.removed.end());result.text.resize(end);return result;
}
}
