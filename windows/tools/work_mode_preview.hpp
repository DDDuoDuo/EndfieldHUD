#pragma once
#include "core/localization.hpp"
#include "app/overlay_host.hpp"
#include "native/work_mode_scene.hpp"
#include "native/projected_editor.hpp"
#include "native/notes_clipboard.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#ifdef _WIN32
namespace endfield::tools {
struct WorkModePreviewOptions {
    native::LayerRasterOptions raster;
    modules::WorkModeAppearance appearance;
    modules::WorkModeStrings strings;
    double restoredWorkSeconds{};
    std::function<void(double)> saveWorkSeconds; // absolute checkpoint; owner schedules persistence
    std::function<void(bool)> focusDesired; // active includes pause; no implied platform support
    std::function<void()> requestFocusAccess;
    UINT textMessage{WM_APP+208};
    // AppDelegate Edit menu over the open duration field: Ctrl+X/C/V cut,
    // copy and paste plain text, read or written once per key (no listener,
    // no polling). The system clipboard by default; tests inject a synthetic
    // one; null declines those keys.
    std::shared_ptr<native::NotesClipboard> clipboard{native::systemNotesClipboard()};
};
// Modifier state of a key event; system is Alt or either Win key.
struct WorkModeKeyModifiers {bool shift{},control{},system{};};
// Shared module owner, driven by the shell's continuous QPC time and deadline.
// Borrows HWND, activated manager/client and rasterizer. It never creates a
// window, renderer, clock, timer, thread, Focus service, clipboard or store.
// Callbacks must not synchronously destroy/reenter this owner. All fields must
// be destroyed before the caller deactivates the one shared TSF manager.
// Custom duration uses the existing <=65536-unit plain editor leaf, source
// inset/finite horizontal growth and one painted layout for glyphs/input/IME.
// Platform font substitution remains explicit; no data truncation is performed.
// Only single-line values parse.
class WorkModePreview final {
public:
    WorkModePreview(HWND,native::LayerRasterizer&,ITfThreadMgr* borrowedManager,
        TfClientId,WorkModePreviewOptions={});
    ~WorkModePreview();
    void resize(const app::ClientMetrics&);
    void setLanguage(core::Language); // preference event; retains the model and current interaction
    void setAppearance(modules::WorkModeAppearance);void setReduceMotion(bool,double);
    void setFocusStatus(std::string,bool permissionAction,double);
    // HUD visibility is separate from OS suspension. False cancels the draft
    // and visual demand but leaves a running countdown completion deadline.
    // Last artwork remains available for the caller's finite closing fade.
    void setOverlayVisible(bool,double); // initially true
    void setSuspended(bool,double);void wake(double);void shutdown(double);
    std::optional<double>nextWakeTime()const noexcept;
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double continuousTime);
    bool requiresFrames(double)const;bool covers(core::Point logical)const;
    bool pointer(const app::PointerEvent&,double);
    // The first form is for the HUD window's key dispatch: it reads
    // GetKeyState, which Win32 synchronizes with the key message being
    // dispatched. Any other caller (tests, synthetic input) states the
    // modifiers explicitly: off that dispatch a thread's GetKeyState follows
    // the live keyboard of the interactive session.
    bool key(const app::KeyEvent&,double);
    bool key(const app::KeyEvent&,const WorkModeKeyModifiers&,double);
    bool filterKey(const app::NativeMessage&);bool message(const app::NativeMessage&,double);
    void focus(bool,double);void cancelInteraction(double);bool pointerLocked()const noexcept;
    bool editing()const noexcept;bool finishEditing(bool commit,double);
    // The open custom duration field's draft (the source HUDWorkDurationField
    // string, its accessibility value) as UTF-8; none without a field.
    std::optional<std::string>editingText()const;
    // UI Automation ValuePattern.SetValue on that field: replaces the whole
    // draft through the editor's TSF document like typed text and keeps the
    // field open (Return, Tab or an accessible press validates and commits it
    // as on the source). False while the page accepts no input, without an
    // open field, while marked text is composing, for invalid UTF-8 or past
    // the field's capacity.
    bool setEditingText(std::string_view utf8,double);
    // Accessibility-equivalent activation (HUDWorkModeInteraction.
    // activateAction): while the page accepts input, an open duration field
    // is committed first, then the listed source action runs exactly as its
    // click. False when inactive, when the field refuses to finish (an
    // invalid draft stays selected) or when the id is not a current action.
    bool perform(std::string_view actionID,double time);
    // Source accessibility projection: a 440 x 440 canvas rect -> the logical
    // client bounding rect of its projected corners while input is accepted.
    std::optional<core::Rect>clientRect(core::Rect canvas)const;
    // Pointer cursor (HUDProjectedTextView.projectedCursor): true when a
    // logical client point unprojects inside the open duration field
    // (durationEditorRect, the same test as its press) while the page accepts
    // input; the host then shows the system I-beam. Allocates nothing,
    // changes nothing, reads no clock.
    bool textCursorAt(core::Point logicalClient)const noexcept;
    const modules::WorkModeController&controller()const noexcept;
    const modules::WorkModePresentation&state()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
