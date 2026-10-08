#pragma once
#include "core/data/json.hpp"
#include "core/text_input.hpp"
#include <memory>
#include <optional>
#include <span>

namespace endfield::core::notes {
using Json=ehud::data::Json;
// Sources/NotesRichText.swift: straight sRGB, theme-relative null color, public
// font name or null/system, logical points. No font lookup occurs in this model.
struct RGBA {
    double red{},green{},blue{},alpha{1};
    bool valid()const noexcept;
    bool operator==(const RGBA&)const=default;
};
struct TextStyle {
    std::optional<std::string> fontName;
    double fontSize{12};
    std::optional<RGBA> color;
    bool bold{},italic{},underline{},strikethrough{};
    Json::Object extra; // additive imported fields survive unrelated edits
    bool valid()const noexcept;
    bool operator==(const TextStyle&)const=default;
};
struct TextRun {
    std::uint32_t location{},length{}; // UTF-16, never UTF-8 bytes/grapheme count
    TextStyle style;
    Json::Object extra;
    bool operator==(const TextRun&)const=default;
};
struct RichText {
    static constexpr std::size_t maximumRuns=50'000;
    std::uint32_t version{1};std::vector<TextRun> runs;Json::Object extra;
    bool validFor(std::u16string_view)const noexcept;
    bool operator==(const RichText&)const=default;
};
// Exact v1 known fields; unknown additive fields are retained. Sparse sorted
// runs are valid on macOS: omitted ranges use TextStyle(). No truncation, font
// substitution, attachment import or JSON/file I/O is hidden here.
RichText decodeRichText(const Json&,std::u16string_view);
Json encodeRichText(const RichText&,std::u16string_view);

enum class FormatKind {font,size,color,bold,italic,underline,strikethrough};
struct FormatChange {
    FormatKind kind{FormatKind::bold};
    std::string fontName;double fontSize{12};RGBA color;
};
struct HistoryBudget {
    std::size_t maximumGroups{256},maximumBytes{16*1024*1024};
    // Soft retained payload budget. One current oversized group is preserved
    // so an accepted edit is always undoable; this is NOT a text-storage limit
    // or a bound on allocator/transient IME snapshot memory.
};
struct RichDocumentStats {
    std::size_t undoGroups{},redoGroups{},historyBytes{},pendingChanges{};
    bool compositionSnapshot{};
};
// Attachment-free source Notes/Archive document. Reuses the one existing TSF
// Document interface; no HWND/editor/layout/service/clipboard/clock/store.
// Owner supplies its explicit native text capacity (up to signed ACP maximum).
// It is not the short painted-editor leaf's 65536-unit integration boundary.
// Format commands preserve selection/unrelated runs. Collapsed commands affect
// future typing only; a mixed selected trait becomes enabled across selection.
// Style-only document edits advance revision(), so the painted layout must be
// refreshed. Selection/typing-only edits do not cause glyph layout work.
class RichDocument final:public text::Document {
public:
    RichDocument(std::u16string,std::optional<RichText>,std::uint32_t maximumUnits,
        HistoryBudget={});
    ~RichDocument();
    RichDocument(const RichDocument&)=delete;
    RichDocument&operator=(const RichDocument&)=delete;
    std::u16string_view text()const noexcept override;
    text::Selection selection()const noexcept override;
    std::uint64_t revision()const noexcept override;
    std::uint32_t maximumUnits()const noexcept override;
    bool readOnly()const noexcept override;
    void setReadOnly(bool)noexcept;
    void beginInputTransaction()override;
    void endInputTransaction()noexcept override;
    void setSelection(text::Selection)override;
    text::Change replace(text::Range,std::u16string_view)override;
    void beginComposition(text::Range)override;
    void updateComposition(text::Range)override;
    std::optional<text::Range> composition()const noexcept override;
    std::optional<text::Change> endComposition(bool cancel)override;
    std::span<const TextRun> runs()const noexcept;
    const TextStyle& typingStyle()const noexcept;
    // Mac menu style: first selected character for font/size/color, AND of each
    // trait over the whole range. No expensive native font enumeration here.
    TextStyle selectionStyle()const;
    bool applyFormat(const FormatChange&);
    bool canUndo()const noexcept;
    bool canRedo()const noexcept;
    bool undo();bool redo();
    void clearHistory(); // prohibited during an input lock/composition
    // Explicit commit operation, not a per-frame query. A formerly plain note
    // remains null if still default-styled; originally-rich notes remain rich.
    // Compatible sparse ranges are preserved; equal neighboring known/extra
    // fields may merge. Original imported root/run/style metadata survives.
    // Explicit sRGB colors stay explicit even when their channels equal the
    // theme ink: AppKit distinguishes an sRGB white from NSColor.white. Only
    // null is theme-relative; native capture/font resolution belongs to owner.
    std::optional<RichText> richText()const;
    RichDocumentStats stats()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::core::notes
