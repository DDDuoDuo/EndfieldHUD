#pragma once
#include "app/overlay_host.hpp"
#include "native/notes_workspace.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/module_presentation.hpp"
#include "core/localization.hpp"
#include "core/data/file_shelf_store.hpp"
#include "modules/notes_shelf_choice.hpp"
#include <functional>

namespace endfield::app { class UtilityExecutor; }
namespace endfield::modules { class NotesCanvas; }
namespace endfield::modules::design { class Context; }
namespace endfield::tools {
// What the Media sources ask the host to do (HUDNotesInteraction
// chooseImages / onChooseShelfMedia / the shelf pick), posted as
// mediaActionMessage: the native file chooser, the File Shelf list, one
// File Shelf item; at the workspace insertion point.
struct NotesMediaAction {
    enum class Kind {chooseLocal,chooseShelf,useShelf};Kind kind;
    core::Point workspacePoint;std::string itemID;
};
struct NotesKeyModifiers {bool control{},shift{},alt{},system{};};
struct NotesPreviewMediaOptions {
    native::NativeMediaRequestBroker* broker{};
    native::NativeMediaRequestBroker::Client client{};
};
struct NotesPreviewPreferences {
    native::NativeNotesWorkspaceStyle workspace;
    core::Language language{core::Language::english};
    // The Windows file browser's menu title (the Mac's "Choose in Finder").
    std::string chooseFile{core::chooseInFileExplorer.english};
    bool dark{true},reduceMotion{};
    bool operator==(const NotesPreviewPreferences&)const=default;
};
// Notes module owner: the desktop notes (native/notes_workspace.hpp) with
// their redesign controls, deletion confirmation and formatting pop-ups
// (tools/notes_desktop_controls.hpp, tools/notes_desktop_popup.hpp), the
// module selection (core::ModulePresentation) and the one activated TSF
// manager every editor borrows. The redesigned centre (modules/
// notes_canvas.hpp) is a design canvas its own owner presents; this owner
// creates the desktop notes it asks for (tool), runs its Media sources and
// reports creations and status to it (setCentre). Its scenes enter the
// caller's one composition and existing frame clock; it never opens an
// account or service.
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
    // formatAssets: resources/notes-format (the colour wheel raster of the
    // formatting pop-ups).
    NotesPreview(HWND,native::LayerRasterizer&,const std::filesystem::path& dataRoot,bool activateTextServices,
        const std::filesystem::path& formatAssets={},std::span<const ehud::data::Note> initialNotes={},
        NotesPreviewMediaOptions media={},bool persistentDataRoot=false);
    ~NotesPreview();
    // The shared design Context (the desktop controls and pop-ups are drawn
    // on its environment), handed over once the design overlay exists; null
    // draws no controls. Before any card is shown.
    void setDesignContext(modules::design::Context*);
    // The centre canvas (its owner outlives this call; clear it first).
    void setCentre(modules::NotesCanvas*);
    // NotesWorkspace.perform(actionID: "tool:text|todo|image|drawing"): the
    // centre's add bar; "tool:image" finishes desktop editing and asks the
    // centre for a source (NotesCanvas.presentMediaSources).
    bool tool(std::string_view action,double time);
    // The centre Media menu's sources (posted as NotesMediaAction).
    void chooseFiles(core::Point workspace);
    void chooseShelf(core::Point workspace);
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
    // The File Shelf's items: the centre's Media menu (NotesCanvas.
    // presentShelfMedia), or under the card of a media relink (Windows).
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
    // A workspace problem (an import, a limit, a busy picker): the centre's
    // "Notice" toast with "Could not save: " + the message, once per distinct
    // message (NotesWorkspace.statusMessage; no dialog/throw-close).
    bool showMediaError(std::string,double time);
    void select(core::Module,double time);
    // The module presentation this owner keeps for the HUD (HUDModuleContent):
    // a redesigned module (a design-library canvas) is entered directionally
    // (ModulePresentation::setDesignScreen; the host marks it before the HUD
    // first shows), and the selected one enters from the disc centre as the
    // HUD deploys (SystemHUDView.animateEntrance -> beginEntrance; nothing
    // for other modules or under Reduce Motion).
    void setDesignScreen(core::Module,bool design);
    // `offset` (content px) replaces the disc-centre offset: the boot log's
    // end enters from below (SystemHUDView.finishBootLog).
    void beginEntrance(double time,std::optional<core::MotionPoint> offset={});
    // SystemHUDView.playsBootLogOnOpen: the module host stays transparent
    // while the daily boot log plays (ModulePresentation::setConcealed).
    void setCentreConcealed(bool);
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
    // as HUDProjectedTextView.projectedCursor; none while a formatting pop-up
    // is open (HUDNotesInteraction.capturesPointer hit-tests only the pop-up)
    // or over the deletion confirmation. No allocation, state change, OS or
    // clock call.
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
    // accessibility views over the projected HUD - every shown card's
    // NotesWorkspace.accessibleActions ("note:<id>:<verb>"), the deletion
    // confirmation, the video seek sliders, the open field
    // ("notes:editor:...") and the open formatting pop-up's controls
    // ("notes.desk:<control id>": buttons, toggles, the brightness slider).
    // Physical client rectangles; nothing without a pose or while the owner
    // accepts no input. Content events only (allocates).
    void automationElements(std::vector<native::NotesAutomationElement>&)const;
    // HUDNotesInteraction.activateAction -> NotesWorkspace.perform(actionID:)
    // and the pop-up's stand-ins, through the same owner paths as pointer
    // input. False for an unknown, hidden or disabled id.
    bool perform(std::string_view id,double time);
    // Seek slider (note id, seconds) or the colour pop-over's brightness.
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
