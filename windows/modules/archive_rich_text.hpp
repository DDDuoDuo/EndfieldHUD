#pragma once
// ArchiveRichText (Mac Sources/ArchiveCanvas.swift:1262-1429): the Archive
// body's NotesRichText runs drawn with the redesign's faces. Shared with the
// Notes centre TEXT note, which reuses it on the Mac (NotesCanvas.swift:534-591).
// Portable rules only; the native editor (native/archive_body_editor.hpp)
// lays out and paints the runs.
//
// The model (core::notes::RichTextDefaults::archive()):
//  - no fontName is the default face, HarmonyOS Sans with the UI region's Noto
//    cascade; stored names starting with "." (old system-font runs) show in it
//    too. A font menu pick of the default face stores no name (FormatChange::
//    clears); any other pick stores the family name. Windows adaptation: the
//    Mac stores the PostScript name NSFontManager resolves for the family and
//    the B/I traits; Windows keeps its v1.2.0 rule of storing the family (the
//    rasterizer resolves Mac PostScript names and families alike), so a Mac
//    reading a Windows-formatted run finds the family by name.
//  - no colour is the #EEEEEE ink (its Light tone in Light); a run set to
//    either theme's ink stores no colour (isInk, :1293-1300).
//  - untouched new documents type at 14 pt (typingStyle, :1361-1365).
//  - load: gaps between runs, and the whole text when the runs do not match
//    it, take the document's base style (attributed, :1342-1347), not
//    TextStyle(); capture is RichDocument::coveringRichText (:1349-1358).
#include "core/notes_rich_text.hpp"
#include "modules/archive_model.hpp"
#include <functional>

namespace endfield::modules {
// Font services of the menus (HUDFontRegistry + NSFontManager on the Mac).
struct ArchiveFontServices {
    // Every installed family, sorted (LayerRasterizer::installedFontFamilies).
    std::function<std::vector<std::string>()> installedFamilies;
    // The bundled CJK families in manifest order (Noto Sans SC, TC, JP, KR).
    std::vector<std::string> bundledCJKFamilies;
    // The family of a stored font name (a family or a Mac PostScript name);
    // nullopt when unknown (the bar then shows the default face).
    std::function<std::optional<std::string>(std::string_view)> familyOf;
};
namespace archive_rich_text {
using core::notes::RGBA;
using core::notes::TextStyle;
inline constexpr std::string_view defaultFamily{"HarmonyOS Sans"};
inline constexpr double newDocumentSize = 14;
inline constexpr std::uint32_t inkHex = 0xEEEEEE;
inline constexpr double lineHeightMultiple = 1.3;            // the paragraph style (:1273-1275)
// The font menu (:1277-1284): the default face, the bundled CJK families,
// then every installed family not starting with "." (sorted, no repeats).
std::vector<std::string> families(const ArchiveFontServices&);
// 0xRRGGBB of a stored colour (nil: the #EEEEEE ink).
std::uint32_t hex(const std::optional<RGBA>&) noexcept;
// Either theme's default ink, within .004 per channel at alpha > .996.
bool isInk(const RGBA&) noexcept;
// The family the bar shows for a run style (:1302-1305).
std::string family(const TextStyle&, const ArchiveFontServices&);
// The covering runs a stored body loads with: `rich` when it is valid for
// `text`, its gaps filled with `base`; else one run of `base`. Empty text: none.
core::notes::RichText loaded(std::u16string_view text, const std::optional<core::notes::RichText>&, const TextStyle& base);
// New, untouched documents type at 14 (:1361-1365).
TextStyle typingStyle(const ArchiveEntry&);
// What the format bar shows (:1369-1384): the typing style at a focused caret
// (and for an empty text); otherwise the style of the first character of the
// focused selection, or of the whole text when unfocused, with B / I / U / S
// on only when the whole range has them.
TextStyle displayedStyle(const core::notes::RichDocument&, bool focused);
// The stored form of a bar change (:1396-1428): the default family and
// either theme's ink store nothing (FormatChange::clears); sizes clamp to
// 6...144. Toggles stay as they are (RichDocument::applyFormat switches them
// on unless the whole range already has them).
core::notes::FormatChange stored(core::notes::FormatChange);
// The selection a format change applies to (:699-706): the selection while
// the body is focused (the typing style when collapsed), the whole text otherwise.
core::text::Range formatRange(const core::notes::RichDocument&, bool focused) noexcept;
} // namespace archive_rich_text
} // namespace endfield::modules
