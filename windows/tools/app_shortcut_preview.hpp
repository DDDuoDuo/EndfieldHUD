#pragma once
#include "app/overlay_host.hpp"
#include "native/app_shortcut_scene.hpp"
#include "native/app_shortcut_service.hpp"
#include "native/notes_clipboard.hpp"
#include "native/projected_editor.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#ifdef _WIN32
namespace endfield::tools {
enum class ShortcutRequestKind {choose,inspect,edit,save,remove,launch};
struct ShortcutRequest {
    std::uint64_t generation{};ShortcutRequestKind kind{};
    std::optional<native::AppShortcutSelection>selection;
    std::optional<modules::ShortcutRecord>record;
    std::optional<modules::ShortcutCandidate>candidate;
    std::optional<std::string>editingID;std::string name,icon;
};
struct ShortcutKeyModifiers {bool modified{},shift{},control{},system{};};
// The modifiers of a key routed by the HUD's own key-message dispatch, where
// GetKeyState is synchronized with that message (Shift, Ctrl, Alt or the
// event's Alt, Windows keys). Synthetic keys state theirs instead.
ShortcutKeyModifiers shortcutKeyboardModifiers(const app::KeyEvent&);
struct AppShortcutPreviewOptions {
    modules::ShortcutFile initial;
    std::optional<std::string>initialError;
    modules::ShortcutAppearance appearance;
    native::LayerRasterOptions raster;
    UINT textMessage{WM_APP+219};
    // Edit-menu Cut/Copy/Paste of the name field (AppDelegate Edit menu over
    // HUDShortcutNameField): read once per explicit Ctrl+V, written once per
    // Ctrl+C/Ctrl+X. Null declines those keys (no clipboard access at all).
    std::shared_ptr<native::NotesClipboard> clipboard;
};
// What the + Add App canvas keeps resident (memory audit): the retained
// canvas (its parts, rasterized leaves and their CPU pixel bytes behind the
// composed GPU textures, builds, replacements and parks) and the open or
// retired name fields with their pixel bytes.
struct AppShortcutResourceStats {
    native::AppShortcutSceneStats scene;
    std::size_t fields{},fieldPixelBytes{};
};
// Retained source App Launcher with one borrowed-TSF plain-history name field.
// All OS/repository work is explicit takeRequest/complete*. One request is in
// flight: no service, worker, clipboard read, window, timer or store IO here.
// Root performs launch only after closing the HUD; save/remove completion must
// contain the file actually committed by its one repository worker. Conceal
// invalidates selection/dialog work, but never discards an accepted write or
// an already-dispatched close-first launch. The current shared editor supports
// at most 65,536 UTF16 units; oversized source names reject, never truncate.
// Caller keeps assets/raster/TSF alive, detaches shared composition entries,
// calls release(), then destroys this owner before deactivating TSF.
class AppShortcutPreview final {
public:
    AppShortcutPreview(HWND,native::LayerRasterizer&,const native::NativeAppShortcutAssets&,
        ITfThreadMgr*,TfClientId,AppShortcutPreviewOptions={});
    ~AppShortcutPreview();
    void resize(const app::ClientMetrics&);
    void setAppearance(modules::ShortcutAppearance,double);
    void setLanguage(core::Language,double);void setReduceMotion(bool,double);
    void setOriginalImages(modules::ShortcutOriginalImages);
    void showError(std::optional<std::string>,double);
    // The repository's committed file after the owner's initial load (or a
    // later committed reload). Never tied to a request; scroll is clamped.
    void replaceItems(modules::ShortcutFile,double);
    void setOverlayVisible(bool,double);
    // The owner opens a new HUD session (AppShortcutState::resetSession):
    // only while the canvas is inactive; an accepted write still completes.
    bool resetSession(double);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;bool covers(core::Point logicalClientPoint)const;
    bool pointer(const app::PointerEvent&,double);bool wheel(const app::WheelEvent&,double);
    bool scroll(core::Point logicalClientPoint,double logicalDelta,double);
    // key(event) reads the keyboard state (the HUD's key-message dispatch
    // only). Synthetic input in tests and hidden coverage never reads it:
    // key(event, modified) states that an unnamed modifier is down or that
    // none is (the event's own Alt still counts as System), and
    // ShortcutKeyModifiers states every modifier explicitly.
    bool key(const app::KeyEvent&,double);bool key(const app::KeyEvent&,bool modified,double);
    bool key(const app::KeyEvent&,const ShortcutKeyModifiers&,double);
    bool filterKey(const app::NativeMessage&);bool message(const app::NativeMessage&,double);
    void focus(bool,double);void cancelInteraction(double);
    bool pointerLocked()const noexcept;bool presentingPanel()const noexcept;
    bool editingName()const noexcept;bool finishEditing(bool commit,double);
    // Accessibility projection (HUDAppShortcutInteraction.layoutAccessibility):
    // whether the canvas accepts input, a canvas rect -> logical client rect
    // through the current module plane (none without input or when it does
    // not project), and the open name field's text, replaced through the
    // editor's host-edit path with the typing normalization (UIA SetValue).
    bool acceptsInput()const noexcept;
    std::optional<core::Rect>clientRect(core::Rect canvas)const;
    // HUDProjectedTextView.projectedCursor: true while the name field is open
    // and the logical client point falls inside its projected rect (the test
    // the pointer routing uses to reach the editor). No allocation, state
    // change, OS or clock call (ModuleOwner::cursorAt).
    bool textCursorAt(core::Point logicalClient)const noexcept;
    std::optional<std::string>fieldText()const;
    bool setFieldText(std::string_view,double);
    bool perform(const modules::ShortcutAction&,double);
    // Chosen/dropped application. While an imported (non-Windows) record is
    // being edited the inspection relinks that record: its id, order, name
    // and icon are kept and only the Windows target is replaced on save.
    bool inspectSelection(native::AppShortcutSelection,double);
    void setExternalDrag(bool,double);
    std::optional<ShortcutRequest>takeRequest();
    // Root consumes cancellation even when this module is hidden.
    std::optional<std::uint64_t>takeCancelledRequest()noexcept;
    bool completeCandidate(std::uint64_t,modules::ShortcutCandidate,double);
    bool completeFile(std::uint64_t,modules::ShortcutFile,double);
    bool completeRequest(std::uint64_t,std::optional<std::string>error,double);
    bool busy()const noexcept;
    const modules::AppShortcutState&state()const noexcept;
    AppShortcutResourceStats resources()const noexcept;
    // The HUD finished closing (HostEvent::overlayConcealed). Mac
    // OverlayController releases SystemHUDView on every close (systemView =
    // nil) and makes a fresh canvas on the next opening: a canvas that is not
    // shown (another module is selected; the composition already released
    // its GPU textures when it stopped composing it) releases its raster
    // artwork now, and showing it again rebuilds the same artwork. The shown
    // + Add App keeps it, so reopening does not stall on a rebuild, and a
    // module switch inside the open HUD never releases it. Needs the renderer
    // of an earlier upload(); true when it released the canvas.
    bool conceal();
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
