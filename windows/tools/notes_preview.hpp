#pragma once
#include "app/overlay_host.hpp"
#include "native/notes_controls_assets.hpp"
#include "native/notes_workspace.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/module_presentation.hpp"
#include "tools/notes_media_menu.hpp"
#include "core/data/file_shelf_store.hpp"
#include <functional>

namespace endfield::app { class UtilityExecutor; }
namespace endfield::tools {
struct NotesKeyModifiers {bool control{},shift{},alt{},system{};};
struct NotesPreviewMediaOptions {
    native::NativeMediaRequestBroker* broker{};
    native::NativeMediaRequestBroker::Client client{};
};
struct NotesPreviewPreferences {
    native::NativeNotesWorkspaceStyle workspace;
    modules::NotesControlsStrings controls;
    bool dark{true},reduceMotion{};
};
// Notes module owner. Its scenes enter the caller's one composition and
// existing frame clock; it never opens an account or service.
//
// Development fixture (persistentDataRoot=false, the default): dataRoot must
// be a NEW absolute directory; it is created here and seeded with one sample
// note, or with initialNotes instead when given.
// Production (persistentDataRoot=true, app/application_persistence.hpp):
// dataRoot is the application's existing versioned root (created when absent
// on a fresh install) and is opened as it is: no sample note is ever written,
// and initialNotes, when given, are upserted into it.
class NotesPreview final {
public:
    NotesPreview(HWND,native::LayerRasterizer&,const std::filesystem::path& dataRoot,
        const native::NativeNotesControlsAssets&,bool activateTextServices,
        const std::filesystem::path& formatAssets={},std::span<const ehud::data::Note> initialNotes={},
        NotesPreviewMediaOptions media={},bool persistentDataRoot=false);
    ~NotesPreview();
    // The shared UtilityExecutor, handed over once by the host after
    // construction: the Mac import relink ledger writes that follow Notes'
    // own record writes (a relinked media note resolves its row, a deleted
    // note dismisses it) then run on it in order instead of on the owner
    // thread. Null (the default) keeps them here. The load's reconcile runs
    // during construction. relinkLedgerJobs counts the ledger writes asked.
    void setUtilityExecutor(app::UtilityExecutor*);
    std::uint64_t relinkLedgerJobs()const noexcept;
    // Shared preview-session TSF service. Other module editors borrow this
    // already activated manager and MUST be destroyed before NotesPreview.
    ITfThreadMgr*activatedTextManager()const noexcept;
    TfClientId textClient()const noexcept;
    static constexpr UINT mediaActionMessage=WM_APP+190,mediaNoticeMessage=WM_APP+191;
    std::optional<NotesMediaAction>takeMediaAction(UINT_PTR generation);
    bool importMedia(std::span<const std::string> explicitPaths,core::Point,double time);
    bool importMedia(ehud::data::ShelfFileAccess,core::Point,double time);
    void presentShelfMedia(std::vector<modules::NotesShelfChoice>,core::Point,double time);
    bool setMediaActive(bool,double time,bool preserveArtwork=false);
    std::optional<double>nextWakeTime()const;
    bool deadline(double time);
    // App accepts/samples its shared broker once, then refreshes each module.
    // Shared mode starts no private worker/video owner and owns no deadline.
    bool refreshSharedMedia(double time);
    // Event only, caller-localized strings. False means a TSF lock deferred the
    // appearance change; retry after the queued editor message. Dirty text is
    // saved before replacing artwork; stored formatting is never flattened.
    bool applyPreferences(NotesPreviewPreferences,double time);
    const std::optional<std::string>&mediaError()const noexcept;
    // Source NotesCanvas.onAction names (createdText ... mediaPlayback), raised
    // after each accepted mutation; the root records Event Log noteAction with
    // the name only. Empty by default. UI thread; must not reenter this owner.
    void setActionSink(std::function<void(std::string_view action)>);
    // Edit-menu clipboard (Ctrl+X/C/V in a field, Ctrl+V media files with no
    // field open). Defaults to the system clipboard, touched only on those
    // keys; tests inject a synthetic one. Null disables clipboard keys.
    void setClipboard(std::shared_ptr<native::NotesClipboard>);
    // Source SystemHUDView drop on the Notes canvas (notesDropPoint, then
    // importPasteboard at the unprojected drop point): image/movie files import
    // as media notes, otherwise image data becomes a managed image note.
    // `logical` is the client point in logical units; nullopt/false declines.
    std::optional<core::Point> dropPoint(core::Point logical)const;
    bool dropMedia(std::span<const std::string> paths,core::Point logical,double time);
    bool dropImage(std::span<const std::uint8_t> data,core::Point logical,double time);
    bool showMediaError(std::string,double time); // existing source status row; no dialog/throw-close
    void select(core::Module,double time);
    core::Module selected()const noexcept;
    const core::ModulePresentationSample& modulePresentation()const noexcept;
    void resize(const app::ClientMetrics&);
    void update(const core::Matrix4& sourceCenter,const core::source::DesktopChromeSettings&,
        float opacity,double time,bool focused);
    bool requiresFrames(double time)const;
    bool pointerLocked()const;
    bool covers(core::Point logicalClientPoint)const;
    // The pointer cursor (ModuleOwner::cursorAt): the I-beam over the open
    // field where a press places the caret (NativeNotesWorkspace::editorAt),
    // as HUDProjectedTextView.projectedCursor; none while a formatting,
    // media-source or Shelf menu is open (HUDNotesInteraction.capturesPointer
    // hit-tests only the menus) or over the deletion confirmation. No
    // allocation, state change, OS or clock call.
    bool textCursorAt(core::Point logicalClient)const noexcept;
    bool pointer(const app::PointerEvent&,double time);
    bool wheel(const app::WheelEvent&,double time);
    // Defaults sample the current thread's delivered keyboard modifiers. Tests
    // may supply an explicit snapshot without changing global keyboard state.
    bool key(const app::KeyEvent&,double time,std::optional<NotesKeyModifiers> modifiers={});
    bool filterKey(const app::NativeMessage&);
    bool message(const app::NativeMessage&,std::optional<double> time={});
    void focus(bool);
    // UI Automation (native/notes_automation.hpp): the source HUDNotesInteraction
    // accessibility views over the projected HUD - the toolbar while Notes is
    // selected ("tool:text"...), every shown card's NotesCanvas.accessibleActions
    // ("note:<id>:<verb>"), the deletion confirmation, the video seek sliders,
    // the open field ("notes:editor:...") and the open retained menu
    // ("notes.format:<item>", "notes.format:wheel", "notes.media:<item>").
    // Physical client rectangles; nothing without a pose or while the owner
    // accepts no input. Content events only (allocates).
    void automationElements(std::vector<native::NotesAutomationElement>&)const;
    // HUDNotesInteraction.activateAction -> NotesCanvas.perform(actionID:)
    // and NotesRetainedMenu.perform(id), through the same owner paths as
    // pointer input. False for an unknown, hidden or disabled id.
    bool perform(std::string_view id,double time);
    // Seek slider (note id, seconds), the color wheel (0...166) or the font/size
    // menu scroll bar ("notes.format:scroll", first shown row).
    bool setAutomationValue(std::string_view id,double value,double time);
    // Replaces the open field's draft (one undo group; a long value promotes
    // a short field like a paste). False for another id or while composing.
    bool setAutomationText(std::string_view id,std::string_view utf8,double time);
    // The provider for this owner's group under the HUD root: elements and
    // actions above, ClientToScreen of the owner HWND; `changed` runs after an
    // accepted invoke/value/text so the host presents and refreshes.
    std::unique_ptr<native::NativeNotesAutomation> automation(IRawElementProviderFragmentRoot* group,
        std::function<double()> clock,std::function<void()> changed={});
    // Build-only automatic live profiling of the first synthetic note.
    bool diagnosticEditing(bool enabled,double time);
    bool finish(); // false: active TSF lock; owner retries after its queued message
    std::uint64_t compositionRevision()const noexcept;
    // Diagnostics: a drawing card's ink painting (samples planned, regions painted).
    native::NativeNotesDrawingStats drawingStats(std::string_view noteID)const;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();
    void collected(native::Renderer&);
    void release(native::Renderer&); // only after shared composition is detached
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
