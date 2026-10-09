#pragma once
#include "app/overlay_host.hpp"
#include "native/notes_controls_assets.hpp"
#include "native/notes_workspace.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/module_presentation.hpp"
#include "tools/notes_media_menu.hpp"
#include "core/data/file_shelf_store.hpp"

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
// Build-only integration owner. Requires an explicit NEW temporary data root;
// never opens the installed application's data or any account/service.
// Its scenes enter the caller's one composition and existing frame clock.
// Optional initialNotes seed ONLY that new injected fixture store; no installed
// records are read, replaced, or truncated by this build-only entry point.
class NotesPreview final {
public:
    NotesPreview(HWND,native::LayerRasterizer&,const std::filesystem::path& newDataRoot,
        const native::NativeNotesControlsAssets&,bool activateTextServices,
        const std::filesystem::path& formatAssets={},std::span<const ehud::data::Note> initialNotes={},
        NotesPreviewMediaOptions media={});
    ~NotesPreview();
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
    bool pointer(const app::PointerEvent&,double time);
    bool wheel(const app::WheelEvent&,double time);
    // Defaults sample the current thread's delivered keyboard modifiers. Tests
    // may supply an explicit snapshot without changing global keyboard state.
    bool key(const app::KeyEvent&,double time,std::optional<NotesKeyModifiers> modifiers={});
    bool filterKey(const app::NativeMessage&);
    bool message(const app::NativeMessage&,std::optional<double> time={});
    void focus(bool);
    // Build-only automatic live profiling of the first synthetic note.
    bool diagnosticEditing(bool enabled,double time);
    bool finish(); // false: active TSF lock; owner retries after its queued message
    std::uint64_t compositionRevision()const noexcept;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();
    void collected(native::Renderer&);
    void release(native::Renderer&); // only after shared composition is detached
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
