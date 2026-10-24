#pragma once
// The E-Reader's module owner on the design library: the Mac 1.3.0
// ReaderCanvas (modules/reader_canvas.hpp) in a DesignOwnerCore, its page
// rasters composed natively inside the canvas (native/reader_pages.hpp,
// right after the paper's part), and the host routes SystemHUDView gives the
// canvas: shelf choices, the file chooser (a generation-tagged import
// action the root turns into its native picker / shelf lease), the
// validated-file import and the relink, the off-thread missing-file check,
// the installed font families and the Event Log observer (Mac 1.3.0 logs no
// page turns: "progress" is dropped here, older entries still read back).
// It wraps the core in its own ModuleOwner (the page draws are not part of
// DesignModuleOwner), so the root marks Reader a design screen itself.
//
// Replaces tools/reader_preview.*, tools/reader_automation.* and the v1.2.0
// ReaderModule adapter (app/application_modules.cpp): the UI Automation tree
// is the design canvas's (the page item turns forward).
#ifdef _WIN32
#include "app/utility_executor.hpp"
#include "native/reader_owner.hpp"
#include "native/reader_pages.hpp"
#include "tools/hud_design_owner.hpp"

namespace endfield::tools {
struct ReaderImportAction {
    enum class Kind { chooseLocal, useShelf };
    Kind kind{};
    std::string shelfID;
    std::uint64_t generation{};
    // Relink... of this book (Mac 1.3.0: any book, from its ⋯ menu or a
    // MISSING tile): the same single-book picker and validated import; the
    // owner then relinks the book (ReaderState::relink) instead of adding one.
    std::string relinkBookID;
    // The picker's caption: "Choose file" or the relink message "Locate {0}".
    std::string caption;
};
// Diagnostics (--reader-scroll-trace): numbers only.
struct ReaderScrollSnapshot {
    double time{}, offset{}, zoom{1}, panY{};
    std::uint64_t stateRevision{}, turnSequence{};
    bool providerBusy{}, turning{};
    std::array<core::Rect, modules::ReaderCanvas::maximumPageLayers> pages{};  // laid-out page layers (column px)
    std::size_t pageCount{};
};
struct ReaderCanvasOwnerOptions {
    DesignOwnerOptions design;                 // the host's designOwnerOptions("reader", routing)
    std::function<std::vector<modules::ReaderShelfChoice>()> shelfChoices;
    std::function<std::vector<std::string>()> fontFamilies;
    std::function<std::string()> newID;
    // An import action waits (the root takes it with takeImportAction on its
    // next refresh; input events and UIA presses refresh anyway).
    std::function<void()> actionQueued;
    std::function<void(std::string_view)> event; // readerAction for the Event Log (never "progress")
    std::function<void(const app::ModuleAppearance&, double)> appearance; // the root's font refresh
    std::function<void(const app::WheelEvent&, double)> wheelObserved; // diagnostics: a wheel the Reader took
};
class ReaderCanvasOwner final : public app::ModuleOwner {
public:
    ReaderCanvasOwner(native::LayerRasterizer&, modules::design::Context&, native::ReaderOwner&, app::UtilityExecutor&, ReaderCanvasOwnerOptions);
    ~ReaderCanvasOwner() override;
    modules::ReaderCanvas& canvas() noexcept;
    DesignOwnerCore& core() noexcept;

    // --- The root's import route (application_events.cpp) -------------------
    std::optional<ReaderImportAction> takeImportAction();
    bool importRequestCurrent(std::uint64_t) const noexcept;
    // The chosen / dropped / shelf file starts validating (call only after an
    // actual reference was selected).
    bool beginImport(std::uint64_t, std::string_view path, double);
    bool receiveImport(std::uint64_t, modules::ReaderBook, double);
    // nullopt: the chooser was cancelled (nothing shows); otherwise the Notice toast.
    bool receiveImportError(std::uint64_t, std::optional<std::string>, double);
    // documentDropURLs(.reader) / ReaderCanvas.importFiles: a drop over the
    // active Reader's centre box while nothing captures the pointer.
    bool acceptsDrop(core::Point client) const;
    std::optional<std::uint64_t> beginDrop(double);
    void queueCapacityAvailable();
    ReaderScrollSnapshot scrollSnapshot() const;

    // --- app::ModuleOwner ----------------------------------------------------
    std::string_view name() const noexcept override { return "reader"; }
    app::ModuleRouting routing() const noexcept override;
    bool presents(core::Module m) const noexcept override { return m == core::Module::reader; }
    void resize(const app::ClientMetrics&) override;
    void setAppearance(const app::ModuleAppearance&, double) override;
    void setOverlayVisible(bool, double) override;
    void overlayClosing(double) override;
    void update(const app::ModuleFrame&) override;
    void prewarm(const app::ModuleFrame&) override;
    bool requiresFrames(double) const override;
    std::optional<double> nextWakeTime(double) const override;
    bool deadline(double) override;
    void upload(native::Renderer&) override;
    std::span<const native::LayerCompositionEntry> entries(core::Module) override;
    void collected(native::Renderer&) override;
    void release(native::Renderer&) override;
    bool covers(core::Point) const override;
    bool pointerLocked() const override;
    bool capturesPointer() const override;
    bool capturesWheel() const override;
    bool capturesKeys() const override;
    bool pointer(const app::PointerEvent&, double) override;
    bool wheel(const app::WheelEvent&, double) override;
    bool key(const app::KeyEvent&, double) override;
    bool message(const app::NativeMessage&, double) override;
    void focus(bool, double) override;
    void cancelInteraction(double) override;
    bool flush(double) override;
    bool hostEvent(const app::HostEvent&, double) override;
    std::optional<app::ModuleCursor> cursorAt(core::Point) const override;
    bool automationRefresh(IRawElementProviderFragmentRoot*) override;
    std::size_t automationCount() const override;
    long automationChild(std::size_t, IRawElementProviderFragment**) const override;
    long automationFromPoint(double, double, IRawElementProviderFragment**) const override;
    void automationDisconnect() noexcept override;
    std::optional<core::Module> automationModule() const noexcept override;
    app::ModuleResidency residency() const override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
