#pragma once
// The File Shelf's platform services around its design canvas
// (modules/file_shelf_canvas.hpp): the HUDFileShelfInteraction + FileShelfStore
// counterpart. The reference store with identity-based stale-path renewal
// (core/data/file_shelf_store.hpp over native/file_shelf_files.hpp), the
// native file picker, the HUD's one OLE drop target (and its external routes
// for the other modules), Ctrl+V paste, the copy-only drag-out with Shell drag
// images (the Shell icon worker runs only while the Shelf is shown), opening and
// revealing references, the properties reads on the shared utility executor,
// Mac-imported items awaiting relink (Migration/relink.json through the import
// ledger API) and the Event Log records SystemHUDView wires (shelfAdded per new
// item, shelfRemoved, shelfCleared with the count). The module itself is a
// tools::DesignModuleOwner over one FileShelfCanvas (makeOwner); this replaces
// tools/shelf_preview, its presentation, scene, automation and adapter.
// Cloud placeholders are referenced, never hydrated (no content is read; the
// properties read attributes with FILE_FLAG_OPEN_NO_RECALL and never list a
// placeholder or remote folder).
//
// Windows differences recorded in the port report:
//  - Open runs from the host's action message (ShellExecuteEx may pump
//    messages, never inside a canvas event); the toast follows its result,
//    and the HUD stays open as on the Mac.
//  - The drag-out (DoDragDrop) keeps the HUD shown where the Mac retracts and
//    hides it during the drag (OverlayController shelfDragPresentation).
#ifdef _WIN32
#include "modules/event_log.hpp"
#include "modules/file_shelf_canvas.hpp"
#include "native/file_shelf_transfer.hpp"
#include "native/shelf_file_picker.hpp"
#include "tools/hud_design_owner.hpp"

namespace endfield::app { class UtilityExecutor; }
namespace endfield::tools {
struct ShelfServiceOptions {
    // Development fixture: a NEW absolute directory, created by the service.
    // With persistentDataRoot (EndfieldHUD.exe, app/application_persistence.hpp)
    // it is the application's existing versioned root, opened as it is and
    // created only when absent on a fresh install.
    std::filesystem::path newDataRoot;
    // nativeIcons: the Shell icon worker and the drag images (outgoing drags
    // and incoming drags over the HUD).
    bool nativeIcons{true}, registerDropTarget{true};
    bool persistentDataRoot{};
    // Event Log records exactly as SystemHUDView wires FileShelfCanvas:
    // shelfAdded {filename} per new item (picker, paste and drops alike),
    // shelfRemoved {filename}, shelfCleared {count}. Null records nothing.
    std::function<void(modules::EventKind, modules::EventMetadata)> recordEvent;
    // Synthetic tests: the Shell handoffs of Open and Reveal (no application
    // or File Explorer starts). Null: the Shell's own.
    native::ShelfRevealOperation openOperation;
    // WHERE's "~": the user profile folder (default %USERPROFILE%).
    std::optional<std::string> homeFolder;
};
struct ShelfServiceAction {
    // choose: the file picker (Add, or Relink for an imported item); reveal:
    // the lease is handed to the host, which closes the HUD first; open: the
    // host runs the Shell's default verb now (the HUD stays); drag: the
    // copy-only OLE drag of the canvas's drag selection; paste: Ctrl+V.
    enum class Kind { choose, reveal, open, drag, paste };
    Kind kind;
    std::string itemID;
};
struct ShelfPanelState {
    bool picking{};
    std::uint64_t propertyReads{};     // utility reads started
    std::size_t pendingPropertyReads{};
};
// What the Shelf keeps resident (memory audit): the canvas scene (parts,
// surfaces, resident raster bytes, parks), the Shell icon worker (created by
// the first shown Shelf, its live threads, starts and parks) and the icon
// cache (at most 24 icons, for drag images).
struct ShelfResourceStats {
    std::size_t sceneParts{}, sceneSurfaces{}, scenePixelBytes{}; std::uint64_t sceneParks{};
    bool iconProvider{}; std::size_t iconWorkers{}; std::uint64_t iconWorkerStarts{}, iconParks{}, iconRequests{};
    std::size_t cachedIcons{}, cachedIconBytes{};
};
// Store / native failure text in the HUD language (Mac FileShelfStoreError
// errorDescription, with Windows' own situations from modules::shelfText).
std::string shelfErrorText(const std::exception&, modules::FileShelfServices::Failure, core::Language);
// Known English diagnostics of the Shelf layers mapped to localized text;
// nullopt: not a known Shelf diagnostic; empty: shown nowhere (Mac is silent).
std::optional<std::string> shelfKnownMessage(std::string_view, core::Language);

class ShelfService final {
public:
    static constexpr UINT noticeMessage = WM_APP + 185, actionMessage = WM_APP + 186;
    // The private utility executor's completion notice (without setUtility).
    static constexpr UINT workMessage = WM_APP + 192;
    // Area-service directory key under which the host publishes this service.
    static constexpr std::string_view serviceKey = "fileShelf";
    ShelfService(HWND, ShelfServiceOptions);
    ~ShelfService();
    ShelfService(const ShelfService&) = delete;
    ShelfService& operator=(const ShelfService&) = delete;
    // The module's owner (routing below). Destroy it before this service. Its
    // DesignOwnerOptions get this service's message, close and conceal hooks.
    std::unique_ptr<DesignModuleOwner> makeOwner(native::LayerRasterizer&, modules::design::Context&, DesignOwnerOptions);
    static app::ModuleRouting routing() noexcept;
    modules::FileShelfCanvas* canvas() const noexcept;     // while its owner lives
    DesignOwnerCore* core() const noexcept;

    // The shared UtilityExecutor for the properties reads and the import-
    // ledger writes; until one is given a private one-worker executor is used,
    // notified through workMessage.
    void setUtility(app::UtilityExecutor*);
    // Private window messages (picker, icons, drops, utility); true when handled.
    bool message(const app::NativeMessage&, double time);
    std::optional<ShelfServiceAction> takeAction();
    // Host actions (application_events.cpp, actionMessage).
    void requestFiles();                                  // choose: shows the picker
    bool paste(double time);                              // HUDFileShelfInteraction.importPasteboard
    void open(std::string_view id, double time);          // the Shell's default verb, then the toast
    ehud::data::ShelfFileAccess access(std::string_view); // throws when unavailable
    std::unique_ptr<native::ShelfDragTransfer> prepareDrag(std::string_view primary);
    void setNativeDragActive(bool);
    // Reveal's Explorer handoff failed after the HUD closed: shown when the
    // Shelf is next active.
    void revealFailed();
    bool importFiles(std::span<const std::string>, double time);
    // Localizes known Shelf diagnostics (shelfKnownMessage); others as given.
    void showError(std::string, double time);
    void showError(const std::exception&, double time);
    // Re-resolves every reference now (moved / renamed originals are found by
    // identity and saved; missing ones are marked unavailable) and reads the
    // import ledger on the utility worker. Explicit only: no watcher.
    void refresh(double time);
    bool perform(std::string_view action, double time);   // FileShelfCanvas::perform
    // The picker's completion for an imported item (also for tests): the
    // chosen object becomes a reference that keeps the imported identifier and
    // date; the ledger row is resolved after the record write.
    bool relink(std::string_view id, std::span<const std::string> paths, double time);
    std::size_t pendingRelinks() const;
    std::vector<modules::FileShelfItem> items() const;    // references, then imported items
    bool preservesFocusOnLoss() const;
    // Selects the existing HWND's file-drop route: present callbacks enable an
    // external owner; null restores the Shelf's own gate. No second
    // registration is created.
    void setExternalDropCallbacks(std::optional<native::ShelfDropTarget::Callbacks>, double time);
    IDropTarget* fileDropTarget() const noexcept;
    void setEventRecorder(std::function<void(modules::EventKind, modules::EventMetadata)>);
    ShelfPanelState panels() const;
    ShelfResourceStats resources() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
