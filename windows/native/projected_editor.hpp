#pragma once
#include "core/notes_rich_text.hpp"
#include "native/layer_scene.hpp"
#include "native/layer_text_layout.hpp"
#include "native/projected_text_input.hpp"
#ifdef _WIN32
namespace endfield::native {
enum class ProjectedEditorAlignment {natural,left,center,right};
struct ProjectedEditorStyle {
    double width{},height{},fontSize{12};
    // Both zero uses native field spacing. Notes supplies measured source-style
    // uniform line height/ascent so entering edit mode does not move its lines.
    double lineHeight{},baseline{};
    std::string fontFamily{"Noto Sans SC"},fontFace{"NotoSansSC-Regular"};
    std::array<double,4> textColor{1,1,1,1},caretColor{1,1,1,1};
    std::array<double,4> selectionColor{.2,.4,.7,.5},compositionColor{1,1,1,1};
    double cornerRadius{}; // caller source viewport radius; shared GPU/hit/TSF clip
    // Defaults retain Notes artwork/layout. No-wrap prevents soft wrapping; it
    // neither filters newlines nor truncates the caller's document to one line.
    ProjectedEditorAlignment alignment{ProjectedEditorAlignment::natural};
    bool wrapped{true};
    // Archive uses natural font metrics plus one point between every line.
    // This is distinct from Notes' explicit uniform lineHeight/baseline mode.
    bool naturalParagraphSpacingOne{};
    // Source HUDProjectedTextEditor.configureSingleLine: three-point horizontal
    // inset, selected-font vertical centering, finite growing document width.
    // The caller still routes Return/Tab and owns single-line input semantics.
    bool sourceSingleLineField{};
    bool operator==(const ProjectedEditorStyle&)const=default;
};
struct PlainEditorFixtureCapacity {std::uint32_t maximumUnits;};
enum class ProjectedEditorTextMode {rich,plainHistory};
enum class ProjectedEditorCommand {left,right,up,down,documentStart,documentEnd,selectAll,backspace,deleteForward,finish};
struct ProjectedEditorResult {bool handled{},changed{},finishRequested{};};
struct ProjectedEditorPose {
    core::Matrix4 localToScreen,screenToClip; // exactly the matrices used by caller Renderer
    unsigned pixelWidth{},pixelHeight{};
    float opacity{1};bool visible{true},ownerFocused{},caretVisible{true};
};
// Reusable bounded plain/rich document with a fixed visible raster viewport.
// Caller-owned Document, dedicated EMPTY LayerScene/rasterizer, HWND and shared
// activated TSF manager outlive this UI-thread adapter. Use the typed rich
// overload for Notes runs; formatting is never inferred from a plain Document.
// Capacity is a fixture/leaf limitation (<=65536 UTF16), not a Notes storage limit.
// The explicit RichDocument overload preserves source runs/undo/composition.
// plainHistory uses that SAME undo/composition model with plain glyph metrics;
// imported rich runs and formatting commands are rejected, never flattened.
// Visual-bidi/word navigation and exact cross-platform font parity remain open.
// No window, renderer, publisher, clock, focus stealing, clipboard or timers.
class NativeProjectedEditor final {
public:
    NativeProjectedEditor(HWND,core::text::Document&,LayerScene&,ProjectedEditorStyle,
        LayerRasterOptions,PlainEditorFixtureCapacity,UINT ownerMessage,UINT_PTR generation);
    NativeProjectedEditor(HWND,core::notes::RichDocument&,LayerScene&,ProjectedEditorStyle,
        LayerRasterOptions,PlainEditorFixtureCapacity,UINT ownerMessage,UINT_PTR generation,
        ProjectedEditorTextMode=ProjectedEditorTextMode::rich);
    ~NativeProjectedEditor();
    NativeProjectedEditor(const NativeProjectedEditor&)=delete;
    NativeProjectedEditor&operator=(const NativeProjectedEditor&)=delete;
    HRESULT connect(ITfThreadMgr& alreadyActivatedManager,TfClientId)noexcept;
    HRESULT focus(bool focused)noexcept; // TS_E_NOLOCK: caller retries after queued change
    HRESULT stop()noexcept; // rolls back marked text only; caller saves/discards whole draft
    // Caller routes queued keys here BEFORE TranslateMessage/command handling,
    // including Escape and Return. A consumed IME key must not reach command().
    bool filterKeyMessage(UINT,WPARAM,LPARAM)noexcept;
    unsigned takeChanges(UINT_PTR generation)noexcept;
    ITextStoreACP* textStore()const noexcept; // borrowed; fake-sink tests/integration
    // Call on document/style/selection events, then composition.upload() before
    // present. Only glyph changes create a DWrite layout; caret/selection paths
    // reuse that exact handle. No publication occurs here.
    bool syncContent();
    bool setStyle(const ProjectedEditorStyle&); // syncContent then setPose; radius alone never rerasterizes
    // Same world/camera drive glyphs, adornments, pointer and TSF geometry.
    // No raster/JSON/text measurement/allocation on unchanged-document frames.
    bool setPose(const ProjectedEditorPose&);
    // Logical document points, positive downward. Requires synchronized text.
    // Repaints only viewport pixels from the same DWrite layout and refreshes
    // the current pose/TSF placement; caller uploads/presents changed resources.
    // A locked TSF transaction declines scrolling before any paint mutation.
    bool setScrollOffset(double);
    bool scrollBy(double);
    double scrollOffset()const noexcept;
    double maximumScrollOffset()const noexcept;
    bool setHorizontalScrollOffset(double);
    double horizontalScrollOffset()const noexcept;
    double maximumHorizontalScrollOffset()const noexcept;
    // Explicit reveal is also available; ordinary command/character/TSF text
    // edits coalesce reveal into their next sync. Manual scroll cancels pending
    // reveal (including an unchanged restore). Pointer-down never reveals.
    bool revealCaret();
    // Caller invokes syncContent/upload after changed=true. Collapsed formatting
    // changes only future typing unless source paragraph normalization is due.
    // Marked composition/TSF locks decline host formatting/history unchanged.
    ProjectedEditorResult applyFormat(const core::notes::FormatChange&);
    ProjectedEditorResult undo();
    ProjectedEditorResult redo();
    std::optional<core::notes::TextStyle> selectionStyle()const;
    bool richLayoutEnabled()const noexcept;
    ProjectedEditorResult command(ProjectedEditorCommand,bool extend=false);
    ProjectedEditorResult character(std::uint32_t value,bool unicodeScalar=false);
    // Client PHYSICAL pixels. Host owns actual focus/capture. Call syncContent
    // before hit/cluster navigation after a Document text revision.
    ProjectedEditorResult pointerDown(core::Point,bool extend=false);
    ProjectedEditorResult pointerDrag(core::Point);
    void pointerUp()noexcept;
    const LayerTextLayout& layout()const;
    const core::text::Placement& placement()const noexcept;
    LayerScene& scene()const noexcept;
    std::uint64_t paintRevision()const noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
} // namespace endfield::native
#endif
