#pragma once
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Exact Foundation/ICU Unicode sets that the unchanged Mac account sources rely
// on (CharacterSet.alphanumerics for identifiers, whitespace trimming, ICU \d
// and \s in diagnostics, and grapheme Prepend before "#"). The tables are
// generated from the Mac runtime by windows/tools/hypergryph_account_reference.sh.
namespace endfield::modules::hypergryph {
struct UnicodeRange {char32_t first,last;};
enum class UnicodeSet {alphanumerics,whitespaces,whitespacesAndNewlines,controlCharacters,format,icuDigit,icuSpace,prependBeforeHash,joinsAfterBase,
    digitEquivalent0,digitEquivalent1,digitEquivalent2,digitEquivalent3,digitEquivalent4,
    digitEquivalent5,digitEquivalent6,digitEquivalent7,digitEquivalent8,digitEquivalent9};
std::span<const UnicodeRange> unicodeRanges(UnicodeSet) noexcept;
bool unicodeContains(UnicodeSet,char32_t) noexcept;
struct DecodedScalar {char32_t value{};std::size_t length{};};
// Strict UTF-8 decoding (no overlongs/surrogates); nullopt on malformed input.
std::optional<DecodedScalar> decodeUTF8(std::string_view,std::size_t offset) noexcept;
bool validUTF8(std::string_view) noexcept;
void appendUTF8(std::string&,char32_t);
// NSString.trimmingCharacters(in:) for the given set (scalar granularity).
std::string trimScalars(std::string_view,UnicodeSet);
}
