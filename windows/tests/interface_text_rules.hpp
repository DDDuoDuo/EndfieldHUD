#pragma once
// The Mac's interface text rules (Tests/LocalizationTests.swift endsWithFullStop
// and hasDotSeparator at 79bc665), shared by every test that checks shown text:
// no final full stop, and no middle dot or bullet between phrases. Tests only.
#include <cstddef>
#include <string_view>

namespace endfield::tests {

// The next UTF-8 code point at `at` (advanced past it); U+FFFD for a broken sequence.
inline char32_t nextCodePoint(std::string_view text, std::size_t& at) noexcept {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(at);
    const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
    if (length == 0 || at + length > text.size()) { ++at; return U'�'; }
    char32_t value = length == 1 ? lead : lead & (0x7F >> length);
    for (std::size_t i = 1; i < length; ++i) {
        if ((byte(at + i) & 0xC0) != 0x80) { ++at; return U'�'; }
        value = (value << 6) | (byte(at + i) & 0x3F);
    }
    at += length;
    return value;
}

// Text that ends in ".", "。" or "．"; an ellipsis written as "..." and a lone stop are not sentences.
inline bool endsWithFullStop(std::string_view text) noexcept {
    for (const std::string_view stop : {std::string_view{"."}, std::string_view{"。"}, std::string_view{"．"}})
        if (text.size() > stop.size() && text.ends_with(stop)) return !text.ends_with("...");
    return false;
}

// A middle dot or bullet between phrases (U+00B7, U+2022, U+2027, U+FF65 anywhere).
// The Japanese name dot U+30FB stays only inside a katakana name (Rhodes Island in katakana):
// between two katakana (U+30A0 to U+30FF), the next one not another U+30FB.
inline bool hasDotSeparator(std::string_view text) noexcept {
    constexpr char32_t nameDot = U'・';
    const auto katakana = [](char32_t c) { return c >= 0x30A0 && c <= 0x30FF; };
    char32_t previous = 0;
    for (std::size_t at = 0; at < text.size();) {
        const char32_t c = nextCodePoint(text, at);
        if (c == U'·' || c == U'•' || c == U'‧' || c == U'･') return true;
        if (c == nameDot) {
            std::size_t peek = at;
            const char32_t next = at < text.size() ? nextCodePoint(text, peek) : 0;
            if (!(katakana(previous) && katakana(next) && next != nameDot)) return true;
        }
        previous = c;
    }
    return false;
}

// Both rules at once, for the per-table loops.
inline bool followsInterfaceTextRules(std::string_view text) noexcept { return !endsWithFullStop(text) && !hasDotSeparator(text); }

} // namespace endfield::tests
