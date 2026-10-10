#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "app/utility_executor.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "modules/media_assembly_controller.hpp"
#include "modules/media_assembly_menu.hpp"
#include "modules/notes_controls.hpp"
#include "native/media_assembly_dialog.hpp"
#include "native/media_assembly_scene.hpp"
#include "native/media_request_broker.hpp"
#include <functional>
namespace endfield::tools {
struct MediaAssemblyShelfAccess {std::string path;std::shared_ptr<void>lease;};
// Played-frame filter hook of the shared frame server (requested shared
// change: NotesVideoRequest::frameFilter, a shared_ptr<const
// std::function<void(void*)>> called on the UI thread with the borrowed
// straight BGRA8 IDXGISurface* between IMFMediaEngine::TransferVideoFrame and
// Renderer::commitMediaTexture). Detected at compile time so this owner builds
// with or without it; true when the filter was attached.
template<class Request>constexpr bool mediaAssemblyFrameFilterHook=requires(Request&q){q.frameFilter=std::shared_ptr<const std::function<void(void*)>>{};};
template<class Request>bool attachMediaAssemblyFrameFilter(Request&request,std::function<void(void*)>filter){
    if constexpr(mediaAssemblyFrameFilterHook<Request>){request.frameFilter=std::make_shared<const std::function<void(void*)>>(std::move(filter));return true;}
    else{(void)request;(void)filter;return false;}
}
struct MediaAssemblyPreviewOptions {
    // assetRoot must be windows/resources/media-assembly (pinned catalog).
    native::LayerRasterOptions raster;
    modules::MediaAssemblyAppearance appearance;
    bool reduceMotion{};
    // Owner window for the posted dialog notice; dialogs are modal to it.
    HWND window{};UINT dialogMessage{WM_APP+243};
    std::function<void()>changed;                       // request a frame / re-read nextWakeTime
    std::function<void(std::string_view action)>event;  // EventLog mediaAssemblyAction {action}
    std::function<std::string()>uuid;                   // ehud::data::makeUUID
    // Temporary File Shelf (source shelfChoices/shelfAccess). Optional.
    std::function<std::vector<modules::NotesShelfChoice>()>shelfChoices;
    std::function<std::optional<MediaAssemblyShelfAccess>(std::string_view id)>shelfAccess;
    // The app's ONE shared media broker/client (IMFMediaEngine playback with
    // audio). Optional: without it the play control keeps its intent only.
    native::NativeMediaRequestBroker*broker{};core::MediaClient client{};
    // Optional app-owned executor for explicit exports (see controller).
    app::UtilityExecutor*exportExecutor{};
    // Edit bounded previews on the renderer's own media device (requires the
    // Renderer mediaVideo option; otherwise, or after any GPU failure, the
    // engine edits them on the utility worker exactly as before).
    bool gpuPreview{true};
    // Modal native dialog boundary (production: native::showMediaAssemblyDialog).
    std::function<std::optional<std::string>(HWND,const native::MediaAssemblyDialogRequest&)>dialog{native::showMediaAssemblyDialog};
};
struct MediaAssemblyPreviewStats {
    std::uint64_t previewUploads{},menuBuilds{},dialogs{},playbackStarts{},playbackReleases{};std::size_t retainedTextures{};
    // GPU-edited previews and their media-texture (re)allocations.
    std::uint64_t gpuPreviews{},gpuTargets{};bool gpuActive{};
    std::uint64_t filteredPlayers{}; // players whose frames get colour edits in place
    bool frameFilterHook{};          // the shared frame server offers the hook
    bool videoShown{};
};
// GPU: with a Renderer created with the mediaVideo option, bounded previews
// (stills and paused movie frames) are edited on that same device into one
// retained media texture (NativeMediaAssemblyGpuProcessor); played frames get
// colour edits in place once the frame server offers frameFilter. Otherwise,
// or after a device failure, previews are edited on the utility worker.
// UI Automation: native::NativeMediaAssemblyAccessibility wraps
// accessibility()/perform()/setAccessibleValue() as fragments for the host's
// HUD root provider.
// Source HUDMediaAssemblyInteraction + MediaAssemblyCanvas host owner. The
// rasterizer, ONE utility executor and the media engine are borrowed app
// services that outlive this owner. It creates no thread, timer, window or
// device: the controller's 0.4 s edited event and 0.25 s export progress are
// folded into nextWakeTime/deadline; decode/export run on the executor and
// complete during the host's drain. Pointer, wheel and key input are routed
// to the retained menus first (they consume input before the controls).
class MediaAssemblyPreview final {
public:
    MediaAssemblyPreview(native::LayerRasterizer&,app::UtilityExecutor&,
        std::shared_ptr<modules::MediaAssemblyEngine>,MediaAssemblyPreviewOptions);
    ~MediaAssemblyPreview();
    MediaAssemblyPreview(const MediaAssemblyPreview&)=delete;
    MediaAssemblyPreview&operator=(const MediaAssemblyPreview&)=delete;
    void resize(const app::ClientMetrics&);
    void setLanguage(core::Language,double time);
    void setAppearance(modules::MediaAssemblyAppearance,double time);
    void setReduceMotion(bool,double time);
    void setOverlayVisible(bool,double time);
    void update(const core::Matrix4& sourceCenter,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    std::optional<double>nextWakeTime()const;
    bool deadline(double time);
    bool requiresFrames(double time)const;
    bool covers(core::Point logicalClientPoint)const;
    bool capturesPointer()const noexcept;   // a menu or a drag is active
    bool pointerLocked()const noexcept;     // drag in progress
    bool pointer(const app::PointerEvent&,double time);
    bool wheel(const app::WheelEvent&,double time);
    // Host supplies modifiers explicitly (no OS keyboard reads in tests).
    bool key(const app::KeyEvent&,modules::MediaAssemblyModifiers,double time);
    void focus(bool,double time);
    void cancelInteraction(double time);
    // Source importFiles: first dropped file, ignored while exporting.
    bool importFiles(std::span<const std::string>paths,double time);
    // Root calls this once after broker accept()/sample(): re-reads the
    // retained video record (position, end of trim range, live texture).
    bool refreshSharedMedia(double time);
    // Handles the owner's posted dialog notice (modal dialog runs here).
    bool message(const app::NativeMessage&,double time);
    // Explicit test/automation hooks for the source actions.
    bool perform(std::string_view action,double time);
    bool menuAction(std::string_view action,double time);
    std::optional<modules::MediaAssemblyMenuKind>menu()const noexcept;
    // UI Automation boundary (host provider): the source AX elements with
    // their rects projected through the current plane into logical client
    // coordinates; values/presses route exactly like the source AX controls.
    std::vector<modules::MediaAssemblyAccessible>accessibility()const;
    bool setAccessibleValue(std::string_view id,double value,double time);
    const modules::MediaAssemblyController&controller()const noexcept;
    const modules::MediaAssemblyPresentation&presentation()const noexcept;
    native::MediaAssemblySceneStats sceneStats()const noexcept;
    bool sceneClosing()const noexcept;
    MediaAssemblyPreviewStats stats()const noexcept;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);
    void release(native::Renderer&); // app detaches the combined list first
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
