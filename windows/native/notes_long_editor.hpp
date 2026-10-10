#pragma once
#include "native/projected_editor.hpp"
#ifdef _WIN32
#include <memory>
#include <span>
#include <stdexcept>
#include <variant>
namespace endfield::native {
// One paragraph (text between LF/CR breaks) is longer than one painted leaf can
// lay out. The stored record is valid and unchanged; only this field declines.
class NativeNotesLongParagraphError final:public std::length_error {
public:NativeNotesLongParagraphError():std::length_error("A Notes paragraph exceeds the long editor's layout capacity; stored text is unchanged"){}
};
// Bounds of the paragraph-chunked Notes editor. Chunks hold whole paragraphs
// (split only after LF, CR, CRLF or U+2029, which DirectWrite and bidi treat as
// paragraph ends); a single paragraph is never split, so one paragraph longer
// than the painted-layout limit (65,536 UTF-16 units) cannot be opened.
struct NativeNotesLongEditorOptions {
    std::uint32_t targetChunkUnits{2048};   // initial grouping and split size
    std::uint32_t maximumChunkUnits{8192};  // an edited region above this is re-split
    std::uint32_t minimumChunkUnits{256};   // an edited region below this joins a neighbor
    std::size_t retainedLayouts{32};        // painted chunk layouts kept besides visible ones
    std::size_t glyphSlots{12};             // simultaneously visible chunks (merged beyond)
    bool operator==(const NativeNotesLongEditorOptions&)const=default;
};
struct NativeNotesLongEditorChunk {std::uint32_t start{},length{};double top{},height{};bool laidOut{};int slot{-1};};
struct NativeNotesLongEditorStats {
    std::size_t chunks{},laidOutChunks{},visibleChunks{};
    std::uint64_t chunkLayouts{},offscreenLayouts{},slotPaints{},retainedRepaints{},evictions{},merges{},splits{};
    double documentHeight{};
};
// Long-document Notes editor with the same contract as NativeProjectedEditor
// (style, pose, TSF, commands, formatting, undo, pointer, scroll, reveal) for
// the attachment-free RichDocument. Instead of one DirectWrite layout of the
// whole document it keeps one painted layout per paragraph chunk:
//   * an edit re-lays out only the chunks it touches (text, runs or paragraph
//     spacing) and repaints only the visible viewport;
//   * glyphs, selection, composition, caret, hit-testing, cluster navigation
//     and TSF geometry all read the SAME chunk layouts that painted the pixels;
//   * paragraphs wrap independently, so line breaks, heights and positions
//     equal the single-layout editor; only the documented 65,536-unit painted
//     leaf limit moves from the document to one paragraph.
// The caller owns the RichDocument (its maximumUnits is the capacity), the
// dedicated EMPTY LayerScene, the rasterizer, HWND and the activated TSF
// manager, all outliving this UI-thread adapter. No window, renderer, clock,
// clipboard, timer or worker is created; work happens on content/scroll events.
class NativeNotesLongEditor final {
public:
    NativeNotesLongEditor(HWND,core::notes::RichDocument&,LayerScene&,LayerRasterizer&,ProjectedEditorStyle,
        LayerRasterOptions,UINT ownerMessage,UINT_PTR generation,
        ProjectedEditorTextMode=ProjectedEditorTextMode::rich,NativeNotesLongEditorOptions={});
    ~NativeNotesLongEditor();
    NativeNotesLongEditor(const NativeNotesLongEditor&)=delete;
    NativeNotesLongEditor&operator=(const NativeNotesLongEditor&)=delete;
    HRESULT connect(ITfThreadMgr& alreadyActivatedManager,TfClientId)noexcept;
    HRESULT focus(bool focused)noexcept;
    HRESULT stop()noexcept;
    HRESULT commitComposition()noexcept;
    bool filterKeyMessage(UINT,WPARAM,LPARAM)noexcept;
    unsigned takeChanges(UINT_PTR generation)noexcept;
    ITextStoreACP* textStore()const noexcept;
    bool syncContent();
    bool setStyle(const ProjectedEditorStyle&);
    bool setPose(const ProjectedEditorPose&);
    bool setScrollOffset(double);
    bool scrollBy(double);
    double scrollOffset()const noexcept;
    double maximumScrollOffset()const noexcept;
    bool revealCaret();
    ProjectedEditorResult applyFormat(const core::notes::FormatChange&);
    ProjectedEditorResult undo();
    ProjectedEditorResult redo();
    std::optional<core::notes::TextStyle> selectionStyle()const;
    bool richLayoutEnabled()const noexcept;
    ProjectedEditorResult command(ProjectedEditorCommand,bool extend=false);
    ProjectedEditorResult character(std::uint32_t value,bool unicodeScalar=false);
    ProjectedEditorResult replaceTextFromHost(core::text::Range,std::u16string_view);
    ProjectedEditorResult setSelectionFromHost(core::text::Selection);
    ProjectedEditorResult pointerDown(core::Point,bool extend=false);
    ProjectedEditorResult pointerDrag(core::Point);
    void pointerUp()noexcept;
    // Document-coordinate geometry over the chunk layouts (the Layout TSF reads).
    const core::text::Layout& geometry()const noexcept;
    std::span<const core::Rect> selectionRectangles(core::text::Range)const;
    std::uint32_t previousCluster(std::uint32_t)const;
    std::uint32_t nextCluster(std::uint32_t)const;
    double documentHeight()const noexcept;
    const core::text::Placement& placement()const noexcept;
    LayerScene& scene()const noexcept;
    std::uint64_t paintRevision()const noexcept;
    NativeNotesLongEditorStats stats()const noexcept;
    std::vector<NativeNotesLongEditorChunk> chunks()const; // current partition (diagnostics and tests)
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};

// The one Notes field editor: the single-layout projected editor for short
// documents and checklist rows, the chunked long editor for long documents.
// Both expose the same field contract; layout() exists only for the former.
class NativeNotesEditor final {
public:
    explicit NativeNotesEditor(std::unique_ptr<NativeProjectedEditor>);
    explicit NativeNotesEditor(std::unique_ptr<NativeNotesLongEditor>);
    ~NativeNotesEditor();
    NativeNotesEditor(const NativeNotesEditor&)=delete;
    NativeNotesEditor&operator=(const NativeNotesEditor&)=delete;
    bool chunked()const noexcept;
    NativeProjectedEditor* projected()noexcept;
    NativeNotesLongEditor* longEditor()noexcept;
    HRESULT connect(ITfThreadMgr&,TfClientId)noexcept;
    HRESULT focus(bool)noexcept;
    HRESULT stop()noexcept;
    HRESULT commitComposition()noexcept;
    bool filterKeyMessage(UINT,WPARAM,LPARAM)noexcept;
    unsigned takeChanges(UINT_PTR)noexcept;
    ITextStoreACP* textStore()const noexcept;
    bool syncContent();
    bool setStyle(const ProjectedEditorStyle&);
    bool setPose(const ProjectedEditorPose&);
    bool setScrollOffset(double);
    bool scrollBy(double);
    double scrollOffset()const noexcept;
    double maximumScrollOffset()const noexcept;
    bool revealCaret();
    ProjectedEditorResult applyFormat(const core::notes::FormatChange&);
    ProjectedEditorResult undo();
    ProjectedEditorResult redo();
    std::optional<core::notes::TextStyle> selectionStyle()const;
    bool richLayoutEnabled()const noexcept;
    ProjectedEditorResult command(ProjectedEditorCommand,bool extend=false);
    ProjectedEditorResult character(std::uint32_t,bool unicodeScalar=false);
    ProjectedEditorResult replaceTextFromHost(core::text::Range,std::u16string_view);
    ProjectedEditorResult setSelectionFromHost(core::text::Selection);
    ProjectedEditorResult pointerDown(core::Point,bool extend=false);
    ProjectedEditorResult pointerDrag(core::Point);
    void pointerUp()noexcept;
    const LayerTextLayout& layout()const; // single-layout editor only; throws for a chunked field
    const core::text::Layout& geometry()const noexcept;
    std::span<const core::Rect> selectionRectangles(core::text::Range)const;
    const core::text::Placement& placement()const noexcept;
    LayerScene& scene()const noexcept;
    std::uint64_t paintRevision()const noexcept;
private:
    std::variant<std::unique_ptr<NativeProjectedEditor>,std::unique_ptr<NativeNotesLongEditor>> editor_;
};
} // namespace endfield::native
#endif
