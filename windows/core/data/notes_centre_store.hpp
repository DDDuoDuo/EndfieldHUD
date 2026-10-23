#pragma once
#include "core/data/data_store.hpp"
#include "core/notes_rich_text.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Notes/centre.json (new in Mac 1.3.0): the centre's own TODO list and TEXT
// note with its tab, Hide/Show state and orange dots (NotesCentreDocument and
// NotesCentreStore, Sources/NotesCanvas.swift:6-78; NotesNoteStyle,
// Sources/NotesFormattingControls.swift:33-71). Desktop cards stay in
// notes.sqlite3. Decoding follows the Mac's Codable exactly: every document
// key is decodeIfPresent with its default (an explicit null too); a present
// value of the wrong type makes the file undecodable; task items need all
// three keys; UUIDs parse in any case and are written uppercase. Encoding is
// JSONEncoder(.sortedKeys), compact: every field is written except a nil
// richText and a nil style family. UNKNOWN KEYS ARE DROPPED on the next write,
// at every level (the Mac encoder writes only known keys), so this file keeps
// no pass-through fields. v1.2.0 never reads the file.
namespace ehud::data {
struct NotesCentreTask {
    std::string id{makeUUID()},text; // id: canonical uppercase UUID
    bool isChecked{};
    bool operator==(const NotesCentreTask&)const=default;
};
// The centre note's whole-text style of the first redesign build: the base of
// text that has no runs. Every key optional on decode; no strikethrough key.
struct NotesNoteStyle {
    static constexpr std::string_view defaultFamily="HarmonyOS Sans";
    std::optional<std::string> family; // nil = defaultFamily
    double size{14};
    bool bold{},italic{},underline{};
    std::string color{"EEEEEE"}; // RRGGBB
    // Swift UInt32(color, radix: 16) on a 6-Character string; like Swift a
    // leading "+" (or "-" before zeros) parses.
    std::optional<std::uint32_t> rgb()const noexcept;
    bool valid()const noexcept; // size 6...144, rgb, family non-empty <= 256 UTF-8 bytes
    // NotesNoteStyle.textStyle: size clamped 6...144, traits, a family other
    // than the default becomes fontName, a colour other than EEEEEE an sRGB
    // colour (channel/255). Mac gap: the Mac stores the regular face's
    // PostScript name (NSFontManager); Windows stores the family name, which
    // its font lookup resolves the same way.
    endfield::core::notes::TextStyle textStyle()const;
    bool operator==(const NotesNoteStyle&)const=default;
};
// NotesRichText as the Mac decodes it: structurally only. Runs are NOT
// validated on load (any Int location/length, any fontSize or channel); when
// they do not fit the text, the whole text is shown in `style`.
struct NotesCentreRichText {
    struct Run {std::int64_t location{},length{};endfield::core::notes::TextStyle style;bool operator==(const Run&)const=default;};
    std::int64_t version{1};std::vector<Run> runs;
    bool operator==(const NotesCentreRichText&)const=default;
};
struct NotesCentreDocument {
    enum class Tab {text,todo};
    static constexpr std::size_t maximumTaskLength=60; // input cap (Characters); not checked on load
    static constexpr endfield::core::notes::RichTextDefaults richTextDefaults=endfield::core::notes::RichTextDefaults::centre();
    std::int64_t version{1};
    std::vector<NotesCentreTask> tasks; // list order
    std::string text;                   // canonical plain text of the TEXT note
    std::optional<NotesCentreRichText> richText;
    NotesNoteStyle style;
    Tab tab{Tab::todo};
    bool isFolded{};
    std::set<std::string> flagged;      // canonical uppercase; may name absent or done tasks
    // isValid: version 1, unique task ids, style.valid(). Task length, run
    // validity and flagged membership are deliberately not checked.
    bool valid()const;
    std::size_t doneCount()const noexcept;
    bool showsDot(const NotesCentreTask&)const; // flagged and not checked
    bool hasDot()const;
    // The runs when they fit `utf16` (the text in UTF-16), else nullopt: the
    // owner then shows the whole text in style.textStyle().
    std::optional<endfield::core::notes::RichText> richRuns(std::u16string_view utf16)const;
    // NotesCanvas.swift:592: richText becomes nil for empty text. Takes a
    // RichDocument::coveringRichText() capture.
    void setText(std::string text,const endfield::core::notes::RichText& captured);
    bool operator==(const NotesCentreDocument&)const=default;
};
inline constexpr std::size_t notesCentreMaximumBytes=16*1024*1024; // Windows bound; the Mac has none
// Throws StoreError(invalid) for undecodable bytes. A decodable but invalid
// document (valid() false) is returned; the store sets both kinds aside.
NotesCentreDocument decodeNotesCentre(std::string_view bytes);
std::string encodeNotesCentre(const NotesCentreDocument&);
// Mac importer check: decodable AND valid, else StoreError(invalid).
NotesCentreDocument validatedNotesCentre(std::string_view bytes);

class NotesCentreStore final {
public:
    enum class LoadIssue {none,setAside,unavailable};
    // <appRoot>/Notes/centre.json. nullopt = storage unavailable: the document
    // lives in memory only (Mac init(directory: nil)). A missing file is the
    // default document. An undecodable, invalid or oversized file is renamed
    // to centre-unreadable-<ISO8601 UTC, ':' -> '-'>.json beside it and never
    // overwritten (LoadIssue::setAside; the owner shows the Mac notice "The
    // centre notes could not be read and were set aside."). A file that cannot
    // be read or renamed leaves the store memory-only (LoadIssue::unavailable
    // or setAside with no rename): unlike the Mac, a later edit never replaces
    // it. unixNow stamps the set-aside name (default: system clock).
    explicit NotesCentreStore(std::optional<std::filesystem::path> appRoot,std::function<double()> unixNow={});
    const NotesCentreDocument& document()const noexcept{return document_;}
    LoadIssue loadIssue()const noexcept{return issue_;}
    const std::optional<std::filesystem::path>& setAsidePath()const noexcept{return setAside_;}
    const std::optional<std::filesystem::path>& path()const noexcept{return path_;}
    // NotesCentreStore.update: no-op (false) when the result equals the
    // document or is invalid; otherwise the document changes FIRST, then the
    // whole file is written atomically (compare-and-replace, directory created).
    // A write failure throws StoreError but keeps the change, so the next edit
    // writes everything again. Call after each edit, never on a timer.
    bool update(const std::function<void(NotesCentreDocument&)>& change);
private:
    std::optional<std::filesystem::path> path_,setAside_;
    NotesCentreDocument document_;
    std::optional<std::string> persisted_;
    LoadIssue issue_{LoadIssue::none};
    bool writable_{};
};
}
