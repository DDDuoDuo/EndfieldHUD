#pragma once
// Source NSTextView cut/copy/paste (AppDelegate Edit menu: Cut, Copy, Paste)
// for the Notes and Archive projected editors, and the Notes Command-V media
// paste (HUDNotesInteraction.keyDown -> importPasteboard).
//
// The clipboard is read once per explicit paste key and written once per cut
// or copy key: no listener, no retained data object, no polling. Text keeps the
// document's own line breaks; CF_UNICODETEXT carries CRLF for other Windows
// applications. A copy from a rich field also writes a private registered
// format holding the source NotesRichText v1 runs, so formatting round-trips
// between EndfieldHUD fields as the Mac RTF pasteboard does. Foreign rich
// formats (RTF/HTML) paste as plain text in the current typing style.
#include "core/notes_rich_text.hpp"
#include "core/notes_paste_import.hpp"
#include "native/notes_long_editor.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>
#ifdef _WIN32
#include <objidl.h>
#include <wrl/client.h>
#endif

namespace endfield::native {
struct NotesClipboardText {
    std::u16string text;
    std::optional<core::notes::RichText> rich; // valid for text when present
    // Another application's rich content (core/notes_paste_import.hpp): the
    // source rich text view reads RTF, then HTML, before plain text. Rich
    // fields paste it; plain fields (checklist rows, dates) paste `text`, the
    // CF_UNICODETEXT string a plain NSTextView reads first. Never set with
    // EndfieldHUD's own runs.
    std::optional<core::notes::ImportedRichText> foreign;
    bool operator==(const NotesClipboardText&)const=default;
};
// The text a paste inserts into a rich or plain field.
inline const std::u16string& notesClipboardPastedText(const NotesClipboardText& value,bool richField){
    return richField&&value.foreign&&!value.rich?value.foreign->text:value.text;
}
// The largest clipboard text a field reads (the text service's safety maximum).
inline constexpr std::uint32_t notesClipboardMaximumUnits=core::text::Buffer::safetyMaximumUnits;
// Line breaks: LF (and a lone CR) become CRLF for CF_UNICODETEXT; CRLF and a
// lone CR become LF when platform text is pasted, as typed Return does.
std::u16string notesClipboardPlatformText(std::u16string_view document);
std::u16string notesClipboardDocumentText(std::u16string_view platform);
// Private format payload: UTF-8 JSON {"version":1,"text":...,"rich":{...}}.
// Decoding returns nullopt for anything that is not exactly such a payload.
std::string encodeNotesClipboardRich(const NotesClipboardText&);
std::optional<NotesClipboardText> decodeNotesClipboardRich(std::string_view);
// The selected text of a document and, when `formatted` (a RichDocument whose
// editor lays out rich runs), its runs; unformatted notes, checklist rows and
// dates copy text alone. Nullopt for a collapsed selection or while composing.
std::optional<NotesClipboardText> notesClipboardSelection(const core::text::Document&,bool formatted);

#ifdef _WIN32
// Replaces the editor's selection through its host-edit path (one undo group,
// text service notified). Declines unchanged ({handled,!changed}) while
// composing or read only, when the document would exceed its capacity, or
// when a chunked field's paragraph would exceed its layout limit. Runs reach
// only a rich field (Notes text, Archive title/body; checklist rows and dates
// take the text alone); formatted runs pasted into a still-unformatted note
// switch its editor to rich layout as the first formatting command does.
ProjectedEditorResult notesClipboardPaste(NativeNotesEditor&,core::text::Document&,const NotesClipboardText&,bool richField);
// Removes a non-empty selection the same way (after its copy was written).
ProjectedEditorResult notesClipboardDeleteSelection(NativeNotesEditor&,core::text::Document&);

class NotesClipboard {
public:
    virtual ~NotesClipboard()=default;
    virtual std::optional<NotesClipboardText> readText(std::uint32_t maximumUnits)=0;
    // CF_HDROP file paths (Unicode, existing paths only) for media paste.
    virtual std::vector<std::string> readFiles()=0;
    virtual bool writeText(const NotesClipboardText&)=0;
    // Image data for the source PNG/TIFF pasteboard import (see below).
    virtual std::optional<std::vector<std::uint8_t>> readImage(std::size_t){return std::nullopt;}
};
// Data-object codec shared by the system clipboard and synthetic tests. The
// returned object owns copies of its formats and never touches the clipboard.
Microsoft::WRL::ComPtr<IDataObject> makeNotesClipboardData(const NotesClipboardText&);
// Plain text (unpaired surrogates become U+FFFD), with the private runs only
// when their text is exactly the CF_UNICODETEXT text; without them, foreign
// "Rich Text Format" or else "HTML Format" content (DBCS code pages through
// MultiByteToWideChar, HTML font lists through the installed DirectWrite
// families). Malformed or oversized foreign content leaves the plain text.
// Nullopt without text or above maximumUnits.
std::optional<NotesClipboardText> readNotesClipboardData(IDataObject&,std::uint32_t maximumUnits);
// Pasted image data as one container the WIC decoder reads: the registered
// "PNG" format first, then CF_DIBV5 or CF_DIB given a bitmap file header, then
// CF_TIFF. Nullopt without image data or above maximumBytes.
std::optional<std::vector<std::uint8_t>> readNotesClipboardImage(IDataObject&,std::size_t maximumBytes);
// OleGetClipboard / OleSetClipboard + OleFlushClipboard on the caller's
// OLE-initialized UI thread. Failures (another application holding the
// clipboard) return nullopt/empty/false; nothing is retried or polled.
std::shared_ptr<NotesClipboard> systemNotesClipboard();
#endif
} // namespace endfield::native
