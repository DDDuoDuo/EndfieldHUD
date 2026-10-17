#pragma once
// Mac font names stored in imported Notes and Archive runs (NotesRichText
// keeps NSFont.fontName, a PostScript name such as "HelveticaNeue-Bold",
// "TimesNewRomanPSMT" or "STSongti-SC-Regular") mapped to the Windows family
// that plays the same typographic role. The stored name never changes; the
// weight still follows the PostScript name and the italic flag, and a name
// without a counterpart here keeps the caller's language fallback.
#include <optional>
#include <string_view>

namespace endfield::core::notes {
std::optional<std::string_view> notesWindowsFontFamily(std::string_view macFontName)noexcept;
}
