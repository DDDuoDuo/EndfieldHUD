#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/reader_scene.hpp"
#include "modules/reader_accessibility.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
namespace endfield::tools {
struct ReaderScrollSnapshot {native::NativeReaderScrollSnapshot scene;double time{},targetOffset{},zoom{},panY{},deferred{};int pendingDirection{};std::uint64_t stateRevision{};bool providerBusy{};};
struct ReaderImportAction {
    enum class Kind {chooseLocal,useShelf};Kind kind{};std::string shelfID;std::uint64_t generation{};
    // Library "Relink…" of an unavailable (e.g. Mac-imported) book: the same
    // single-book picker and validated import as chooseLocal; the preview then
    // relinks this book (ReaderState::relink) instead of adding a new one.
    std::string relinkBookID;
};
struct ReaderPreviewOptions {
    native::LayerRasterOptions raster;
    modules::ReaderAppearance appearance;
    modules::ReaderStrings strings=modules::ReaderStrings::simplifiedChinese();
    std::function<std::string()>newID;
    // Configuration-event catalogs only; neither filesystem/provider/worker is
    // queried from a pointer/pose frame. Shelf choices are already available,
    // supported original references, not file contents or copied books.
    std::function<std::vector<modules::ReaderMenuChoice>()>shelfChoices;
    std::function<std::vector<std::string>()>fontFamilies;
    bool reduceMotion{};
};
// Borrows ReaderState (its one existing FIFO UtilityExecutor/provider), shared
// rasterizer and module transition. No service, TSF, HWND, clock, renderer,
// private worker or timer. The root owns the one picker and validated-file
// import job, returns generation-tagged completion, then publishes entries.
class ReaderPreview final {
public:
    ReaderPreview(modules::ReaderState&,native::LayerRasterizer&,ReaderPreviewOptions);
    ~ReaderPreview();
    void resize(const app::ClientMetrics&);
    void setAppearance(modules::ReaderAppearance);void setStrings(modules::ReaderStrings);
    void setReduceMotion(bool)noexcept;void setOverlayVisible(bool,double);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;std::optional<double>nextWakeTime()const noexcept;
    bool deadline(double);bool covers(core::Point)const;
    ReaderScrollSnapshot scrollSnapshot()const noexcept; // source geometry only; no IDs/text/paths or extra work
    // Memory trace (ModuleOwner::residency): the page pixels the canvas holds
    // (the current page, its neighbours, a departing page and a zoomed crop,
    // shared with ReaderState) beyond the shared raster cache and renderer.
    // The document, its page cache and its PDF device live on the shared
    // executor only while the Reader is shown and are released on hiding. It
    // starts no thread. Counters only.
    struct Residency {std::uint64_t cpuBytes{},gpuBytes{};std::uint32_t threads{};};
    Residency residency()const noexcept;
    bool pointer(const app::PointerEvent&,double);bool wheel(const app::WheelEvent&,double);
    bool key(const app::KeyEvent&,double);bool key(const app::KeyEvent&,bool modified,double);
    // Local event routing only: source Reader menus/pans do not lock HUD tilt.
    bool capturesPointer()const noexcept;bool pointerLocked()const noexcept;void cancelInteraction(double);
    std::optional<ReaderImportAction>takeImportAction();
    bool importRequestCurrent(std::uint64_t)const noexcept;
    // Native dialog cancellation leaves the existing book untouched. Call begin
    // only after an actual reference is selected, before validation starts.
    bool beginImport(std::uint64_t,double);
    // A validated book for a relink generation replaces the locator of the
    // relinked book (identity, title, progress and bookmarks kept) instead of
    // adding it; if that book was removed meanwhile it is added like an import.
    bool receiveImport(std::uint64_t,modules::ReaderBook,double);
    bool receiveImportError(std::uint64_t,std::optional<std::string>,double);
    // Source documentDropURLs(.reader)/ReaderInteraction.importFiles: a file
    // drop is accepted over the interactive Reader canvas while no menu, pan
    // or picker owns it (a running validation is replaced, as on the Mac).
    // beginDrop closes any menu and returns the generation the root passes to
    // beginImport and its validated-file import of modules::readerDroppedReference.
    bool acceptsDrop(core::Point client)const;
    std::optional<std::uint64_t>beginDrop(double);
    void queueCapacityAvailable();
    // Source accessibility (ReaderInteraction.layoutAccessibility over
    // ReaderCanvas.accessibleActions, and the open NotesRetainedMenu's items)
    // for the HUD's UI Automation peers (tools/reader_automation.hpp): empty
    // unless the canvas accepts input. Menu rects are offset into the 400 x
    // 440 canvas; help is ReaderCanvas.accessibilityText (the visible error,
    // else the page summary, else the empty caption) in the caption language.
    // Content-event query: it brings the canvas content up to date first,
    // as a pointer event does; never per frame.
    struct Accessibility {std::vector<modules::ReaderAccessible>canvas,menu;std::string help;};
    Accessibility accessibility();
    // Accessible presses: ReaderAXButton.onPress -> ReaderCanvas.perform(id)
    // for a canvas action, NotesRetainedMenu.accessibilityActivate for an item
    // of the open menu. A listed, enabled id runs exactly as its click; false
    // otherwise or while input is not accepted.
    bool perform(std::string_view canvasActionID,double time);
    bool performMenu(std::string_view menuItemID,double time);
    // Source accessibility projection: a canvas rect -> the logical client
    // bounding rect of its projected corners while input is accepted.
    std::optional<core::Rect>clientRect(core::Rect canvas)const;
    const app::ClientMetrics&clientMetrics()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
