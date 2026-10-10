#pragma once
// Word and paragraph navigation for the Notes and Archive fields: the Windows
// keyboard conventions for the source NSTextView word movements (Ctrl+Left and
// Ctrl+Right, with Shift extending; Ctrl+Backspace and Ctrl+Delete) and the
// source double-click word and triple-click paragraph selections. Home/End
// keep the document start/end mapping every HUD field uses.
//
// Word segments come from the operating system's ICU word-break rules
// (icu.dll, the same Unicode rules and CJK dictionaries the source text system
// uses), loaded on first use; without it a Unicode character-class fallback
// keeps navigation working. Every search is bounded by the caret's paragraph.
#include "core/text_input.hpp"
#include <cstdint>
#include <string_view>
#ifdef _WIN32
#include "native/notes_long_editor.hpp"
#endif

namespace endfield::native {
enum class NotesWordEngine {icu,fallback};
// Which segmentation is in use (ICU from the OS, or the fallback).
NotesWordEngine notesWordEngine()noexcept;
// Testing: force the fallback even when ICU is available (process-wide).
void notesUseFallbackWords(bool)noexcept;

// Windows Ctrl+Right: the start of the next word after `at` (punctuation is a
// word, whitespace is skipped); at a paragraph end, the next paragraph start.
std::uint32_t notesNextWordStart(std::u16string_view,std::uint32_t at);
// Windows Ctrl+Left: the start of the word before `at`; at a paragraph start,
// the end of the previous paragraph.
std::uint32_t notesPreviousWordStart(std::u16string_view,std::uint32_t at);
// Source double-click: the word, whitespace run or punctuation at `at`.
core::text::Range notesWordAt(std::u16string_view,std::uint32_t at);
// Source triple-click: the paragraph at `at` including its line break.
core::text::Range notesParagraphAt(std::u16string_view,std::uint32_t at);
// One user-perceived character before/after `at` (grapheme cluster).
std::uint32_t notesPreviousGrapheme(std::u16string_view,std::uint32_t at);
std::uint32_t notesNextGrapheme(std::u16string_view,std::uint32_t at);

#ifdef _WIN32
enum class NotesNavigation {previousWord,nextWord};
// Moves the caret (or extends the selection from its anchor) through the
// editor's host selection path. Declines unchanged while composing.
ProjectedEditorResult notesNavigate(NativeNotesEditor&,const core::text::Document&,NotesNavigation,bool extend);
// Ctrl+Backspace / Ctrl+Delete: deletes the selection, or to the previous /
// next word start, as one undo group through the host edit path.
ProjectedEditorResult notesDeleteWord(NativeNotesEditor&,core::text::Document&,bool forward);
// Selects `range` (double/triple click), the active end at its end.
ProjectedEditorResult notesSelectRange(NativeNotesEditor&,const core::text::Document&,core::text::Range);
// Shared field key mapping: Ctrl+Left/Right (Shift extends), Ctrl+Backspace
// and Ctrl+Delete. Returns handled=false for every other key.
ProjectedEditorResult notesNavigationKey(NativeNotesEditor&,core::text::Document&,unsigned virtualKey,bool control,bool shift);
// Press count of one field: 1, 2 (the platform double-click) or 3 for a press
// inside the system double-click time and rectangle after a double-click.
class NotesClickCount {
public:
    unsigned press(bool doubleClick,core::Point logical,double seconds)noexcept;
    void reset()noexcept{armed_=false;}
private:
    double time_{};core::Point point_{};bool armed_{};
};
#endif
} // namespace endfield::native
