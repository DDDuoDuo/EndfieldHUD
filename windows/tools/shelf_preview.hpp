#pragma once
#include "core/localization.hpp"
#include "app/overlay_host.hpp"
#include "native/shelf_assets.hpp"
#include "native/file_shelf_transfer.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/module_presentation.hpp"
#include "core/subsection_mask.hpp"
#include "modules/event_log.hpp"
#include "modules/file_shelf_state.hpp"
#include <exception>
#include <functional>

namespace endfield::tools {
struct ShelfPreviewOptions {
    // Development fixture: a NEW absolute directory, created by the owner.
    // With persistentDataRoot (EndfieldHUD.exe, app/application_persistence.hpp)
    // it is the application's existing versioned root, opened as it is and
    // created only when absent on a fresh install.
    std::filesystem::path newDataRoot;
    // nativeIcons also enables the other Shell presentation services: the
    // drag image of outgoing drags and of incoming drags over the HUD.
    bool nativeIcons{true},registerDropTarget{true};
    std::shared_ptr<const core::SubsectionMaskSampler> revealSamples;
    bool persistentDataRoot{}; // existing four-member aggregate initialisers stay valid
    // Event Log records exactly as SystemHUDView wires FileShelfCanvas:
    // shelfAdded {filename} per new item (picker, paste and drops alike),
    // shelfRemoved {filename}, shelfCleared {count}. Null records nothing.
    std::function<void(modules::EventKind,modules::EventMetadata)> recordEvent;
    // The native preview popup is shown (false: hidden coverage keeps it hidden).
    bool visiblePanels{true};
};
// Native panel state (picker and preview popup), for owners and coverage.
struct ShelfPanelState {
    bool picking{},previewActive{},previewVisible{},previewFallback{};
    std::uint64_t previewOpens{},previewFallbacks{},previewFailures{};
};
struct ShelfPreviewAction {
    enum class Kind {choose,preview,reveal,drag,paste};Kind kind;
    std::string itemID;
};
// Connected reference-only shelf. Borrows the shell's sampled module transition,
// renderer, HWND and frame clock; never owns another module-selection state.
// Test construction requires a fresh data root and may disable native services;
// production opens the persistent root (ShelfPreviewOptions::persistentDataRoot).
// Windows-only user-facing Shelf text in the five source languages.
enum class ShelfText {revealPrefix,previewPrefix,pickerFailed,previewFailed,virtualFiles,chooseAvailable,
    unavailableReason,tooManyReferences,changedOnDisk,newerVersion,modified,revealFailed};
std::string shelfText(ShelfText,core::Language);
// Mac FileShelfStoreError.errorDescription for a native/store failure.
std::string shelfErrorText(const std::exception&,modules::FileShelfState::Failure,core::Language);
// Known English diagnostics of the Shelf layers mapped to localized text;
// nullopt: not a known Shelf diagnostic; empty: shown nowhere (Mac is silent).
std::optional<std::string> shelfKnownMessage(std::string_view,core::Language);

class ShelfPreview final {
public:
    static constexpr UINT noticeMessage=WM_APP+185,actionMessage=WM_APP+186;
    // Area-service directory key under which a host may publish this owner.
    static constexpr std::string_view serviceKey="fileShelf";
    ShelfPreview(HWND,native::LayerRasterizer&,const native::NativeShelfAssets&,ShelfPreviewOptions);
    ~ShelfPreview();
    void resize(const app::ClientMetrics&);
    void setLanguage(core::Language); // preference event; retains the model and current interaction
    void setAppearance(bool dark,modules::ShelfColor accent); // derives source light drop color
    void setAppearance(modules::ShelfPresentationStyle); // preserves selected language; source 2x packaged artwork
    const modules::ShelfPresentationStyle& appearance() const;
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double time)const;
    bool covers(core::Point logicalClientPoint)const;
    bool pointer(const app::PointerEvent&,double time);
    bool wheel(const app::WheelEvent&,double time);
    bool key(const app::KeyEvent&,double time);
    bool message(const app::NativeMessage&,double time);
    bool pointerLocked()const;
    bool preservesFocusOnLoss()const;
    ShelfPanelState panels()const;
    std::optional<ShelfPreviewAction> takeAction();
    bool importFiles(std::span<const std::string>,double time);
    // Ctrl+V (HUDFileShelfInteraction.importPasteboard): file paths from the
    // OLE clipboard; a virtual-file-only clipboard explains itself, anything
    // else is ignored as on the Mac. Only for an explicit user paste.
    bool paste(double time);
    // Localizes known Shelf diagnostics (shelfKnownMessage); others as given.
    void showError(std::string,double time);
    void showError(const std::exception&,double time);
    void requestFiles();
    // Opens the Shell preview, or Quick Look's generic preview for folders and
    // types without a stream preview handler. Failures become Shelf errors.
    void requestPreview(std::string_view itemID);
    // Re-resolves every reference now (the activation refresh): moved/renamed
    // originals are found by identity and their paths saved, missing ones are
    // marked unavailable. Explicit only; there is no watcher or polling.
    void refresh(double time);
    // Performs a FileShelfState action id (accessibility buttons, coverage).
    bool perform(std::string_view action,double time);
    void setEventRecorder(std::function<void(modules::EventKind,modules::EventMetadata)>);
    bool filterKey(const app::NativeMessage&);
    void cancelPanels();
    void cancelInteraction();
    void setNativeDragActive(bool);
    // Selects the existing HWND's file-drop route. Present callbacks enable an
    // external owner; null restores Shelf's sampled input gate. Call only on a
    // route transition. Callbacks own their dependencies; commit only enqueues
    // bounded work, which the external owner drains after the COM callback.
    void setExternalDropCallbacks(std::optional<native::ShelfDropTarget::Callbacks>,double time);
    // Borrowed existing COM target; retaining callers must AddRef. No second
    // registration is created. Null when file-drop registration was disabled.
    IDropTarget* fileDropTarget()const noexcept;
    ehud::data::ShelfFileAccess access(std::string_view);
    std::unique_ptr<native::ShelfDragTransfer> prepareDrag(std::string_view);
    const modules::FileShelfState& state()const;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
}
