#pragma once
// Foreign rich clipboard text as Notes rich text: what the source rich
// NSTextView pastes (its readablePasteboardTypes read RTF first, then HTML,
// then plain text) and NotesRichText.capture then stores. Windows clipboard
// equivalents: "Rich Text Format" (Word, WordPad, Outlook, LibreOffice) and
// "HTML Format" (CF_HTML: browsers, Office, Teams). The pinned Mac oracle is
// windows/tools/notes_paste_reference.sh (tests/fixtures/notes-paste-reference.json).
//
// Character formatting maps to the closed Notes model: font family, size in
// points (clamped like capture to 6...144), sRGB color (null = theme ink),
// bold, italic, underline and strikethrough. Paragraph formatting, links,
// pictures and objects are not part of the model and are dropped exactly as
// capture drops them. Font names are Windows family names: the Mac's own
// defaults (RTF without a font table: Helvetica; HTML: Times, sans-serif
// Helvetica, monospace Courier 13 pt) become their Windows counterparts
// (Arial, Times New Roman, Courier New). Runs are dense from 0.
//
// RTF color tables are Generic RGB on the Mac and convert to sRGB through the
// shared portable profile transform (<1/255 per channel). Two Mac fonts with
// one Windows counterpart (Times and Times New Roman) become one family.
// Office HTML class style sheets are not applied (inline styles and
// presentational tags are); Office also supplies RTF, which is read first.
#include "core/notes_rich_text.hpp"
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::core::notes {
struct ImportedRichText {
    std::u16string text;RichText rich;
    bool operator==(const ImportedRichText&)const=default;
};
struct PasteImportOptions {
    // Bytes of a Windows code page other than 1252 (RTF \'hh runs in a DBCS
    // or other ANSI font charset). The native layer supplies the OS decoder;
    // absent or failing, each byte becomes U+FFFD (text is never dropped).
    std::function<std::optional<std::u16string>(std::uint32_t codePage,std::string_view bytes)> decode;
    // HTML font-family lists: the first installed family wins, as in WebKit
    // (the native layer asks DirectWrite). Absent: the first named family.
    std::function<bool(std::string_view family)> fontAvailable;
    // The largest imported text (UTF-16 units); larger content returns
    // nullopt so the caller pastes the plain text instead.
    std::size_t maximumUnits{4*1024*1024};
};
// RTF bytes (any RTF 1.x writer). Nullopt when the data does not start an
// RTF document or exceeds the limits.
std::optional<ImportedRichText> importNotesRTF(std::string_view rtf,const PasteImportOptions& = {});
// CF_HTML ("Version:0.9 StartHTML:..." header, UTF-8) or plain HTML. Only the
// fragment between StartFragment/EndFragment (offsets or comment markers) is
// pasted; its ancestors in the context still style it.
std::optional<ImportedRichText> importNotesHTML(std::string_view html,const PasteImportOptions& = {});

// The source text view's copy also offers other applications RTF. A Notes
// selection (text and its sparse runs) as RTF for Word, WordPad or Outlook:
// font families (Mac names as their Windows counterparts, no name: the
// default family), point sizes, bold, italic, underline, strikethrough and
// sRGB colors; theme ink stays the reader's automatic color rather than a
// fixed white or black. Line breaks: "\n" is \par, U+2028 \line.
struct NotesRTFExportOptions {std::string defaultFamily{"Segoe UI"};};
std::string exportNotesRTF(std::u16string_view text,const RichText&,const NotesRTFExportOptions& = {});
}
