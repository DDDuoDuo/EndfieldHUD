#pragma once
// Media Assembly's module owner on the design library: the redesigned
// MediaAssemblyCanvas (modules/media_assembly_canvas.hpp) in a DesignOwnerCore
// (tools/hud_design_owner.hpp), plus the platform services the canvas draws
// and runs on. It replaces the v1.2.0 tools/media_assembly_preview owner (its
// retained scene, menus and projected peers): menus, the confirm bar, toasts,
// input, UI Automation, prewarm and parking are the core's.
//
// What stays here (unchanged rules of the v1.2.0 owner):
//  - the session controller over the production engine (WIC / Media
//    Foundation decode, export) on the app's ONE utility executor, explicit
//    exports on the optional long-work executor;
//  - GPU previews: with a Renderer created with the mediaVideo option the
//    deferred previews are edited on that device (NativeMediaAssemblyGpuProcessor,
//    device leases through Renderer::mediaDevice), CPU previews otherwise;
//  - playback through the app's shared media broker, the played-frame colour
//    filter, the end-of-trim deadline and the per-player identity re-check;
//  - the modal open / save dialogs posted to the HUD HWND;
//  - session-only state (nothing is persisted).
// New with the redesign: the rendered previews (the shown one and, during the
// .12 s cross-fade, the previous one) and the playing video are draw records
// inserted into the canvas scene right after the stage's picture slot
// (DesignOwnerCore::setInsertion), placed every frame from the picture
// layer's presented pose (the Mac's rotator / picture CALayers), clipped by
// the stage. Two preview textures alternate (the cross-fade keeps the
// previous one); the drawer's sticker thumbnails are 96 px memory images
// decoded on first use (MediaAssemblyAssetCatalog.stickerThumbnail).
//
// No thread, timer or window of its own; UI thread only.
#ifdef _WIN32
#include "app/utility_executor.hpp"
#include "modules/media_assembly_canvas.hpp"
#include "native/media_assembly_codec.hpp"
#include "native/media_assembly_dialog.hpp"
#include "native/media_request_broker.hpp"
#include "tools/hud_design_owner.hpp"
#include <functional>

namespace endfield::tools {
// Played-frame filter hook of the shared frame server
// (NotesVideoRequest::frameFilter, a shared_ptr<const
// std::function<void(void*)>> called on the UI thread with the borrowed
// straight BGRA8 IDXGISurface* between IMFMediaEngine::TransferVideoFrame and
// Renderer::commitMediaTexture). Detected at compile time so this owner builds
// with or without it; true when the filter was attached.
template<class Request>constexpr bool mediaAssemblyFrameFilterHook=requires(Request&q){q.frameFilter=std::shared_ptr<const std::function<void(void*)>>{};};
// The shared media broker re-creates each movie request from its scoped
// catalog entry (core::ScopedMediaRequest); a request's frameFilter reaches
// the frame server only when that entry carries it.
template<class Scoped>constexpr bool mediaAssemblyBrokerFilterRoute=requires(Scoped&q){q.frameFilter;};
template<class Request>bool attachMediaAssemblyFrameFilter(Request&request,std::function<void(void*)>filter){
    if constexpr(mediaAssemblyFrameFilterHook<Request>){request.frameFilter=std::make_shared<const std::function<void(void*)>>(std::move(filter));return true;}
    else{(void)request;(void)filter;return false;}
}
struct MediaAssemblyOwnerOptions {
    // The core's options (ApplicationHost::designOwnerOptions: name, routing,
    // host raster options, clock, change notice). raster.assetRoot is set to
    // `assetRoot`, raster.memoryImages to the owner's thumbnails.
    DesignOwnerOptions design;
    std::filesystem::path assetRoot;                    // Resources/MediaAssembly (pinned provenance)
    // Owner window for the posted dialog notice; dialogs are modal to it.
    HWND window{};UINT dialogMessage{WM_APP+243};
    std::function<void(std::string_view action)>event;  // EventLog mediaAssemblyAction {action}
    std::function<std::string()>uuid;                   // ehud::data::makeUUID
    // Temporary File Shelf (source shelfChoices / shelfAccess). Optional:
    // without choices the Shelf menu reads "Shelf is empty"; without access a
    // row does nothing; an access that fails throws MediaAssemblyFailure.
    std::function<std::vector<modules::MediaAssemblyShelfChoice>()>shelfChoices;
    std::function<std::optional<modules::MediaAssemblyShelfFile>(std::string_view id)>shelfAccess;
    // The app's ONE shared media broker/client (IMFMediaEngine playback with
    // audio); the owner detaches the client when it is destroyed. Optional:
    // without it the play control keeps its intent only.
    native::NativeMediaRequestBroker*broker{};core::MediaClient client{};
    // Optional app-owned executor for explicit exports (see controller).
    app::UtilityExecutor*exportExecutor{};
    // Edit bounded previews on the renderer's own media device (requires the
    // Renderer mediaVideo option; otherwise, or after any GPU failure, the
    // engine edits them on the utility worker).
    bool gpuPreview{true};
    // Modal native dialog boundary (production: native::showMediaAssemblyDialog).
    std::function<std::optional<std::string>(HWND,const native::MediaAssemblyDialogRequest&)>dialog{native::showMediaAssemblyDialog};
};
struct MediaAssemblyOwnerStats {
    std::uint64_t previewUploads{},dialogs{},playbackStarts{},playbackReleases{},thumbnailDecodes{};std::size_t retainedTextures{};
    // GPU-edited previews and their media-texture (re)allocations.
    std::uint64_t gpuPreviews{},gpuTargets{};bool gpuActive{};
    std::uint64_t filteredPlayers{}; // player requests carrying the colour-edit filter
    std::uint64_t filteredFrames{};  // played frames edited in place by that filter
    bool frameFilterHook{};          // the shared frame server offers the hook
    bool frameFilterRoute{};         // the shared broker carries it to the frame server
    bool videoShown{};
    std::size_t pictureDraws{};      // owner records composed this frame (previews, video)
    // Residency (memory audit): canvas artwork (design scene) parts and their
    // raster bytes, CPU preview pixels this owner holds (the shown previews
    // and the GPU source), bytes the preview and played-frame GPU processors
    // hold, the thumbnails' bytes, and parks of the canvas after the HUD closed.
    std::size_t sceneParts{},scenePixelBytes{},previewPixelBytes{},gpuResidentBytes{},playbackResidentBytes{},thumbnailBytes{};std::uint64_t parks{};
};
// Not final: the module area adds its hidden coverage and the shared frame
// server refresh (app/areas/media_assembly_module.cpp).
class MediaAssemblyOwner:public app::ModuleOwner {
public:
    MediaAssemblyOwner(native::LayerRasterizer&,app::UtilityExecutor&,modules::design::Context&,
        std::shared_ptr<modules::MediaAssemblyEngine>,MediaAssemblyOwnerOptions);
    ~MediaAssemblyOwner()override;
    MediaAssemblyOwner(const MediaAssemblyOwner&)=delete;
    MediaAssemblyOwner&operator=(const MediaAssemblyOwner&)=delete;

    // --- app::ModuleOwner ---
    std::string_view name()const noexcept override;
    app::ModuleRouting routing()const noexcept override;
    bool presents(core::Module)const noexcept override;
    void resize(const app::ClientMetrics&)override;           // a hidden window keeps the last real size
    void setAppearance(const app::ModuleAppearance&,double)override;
    void setOverlayVisible(bool,double)override;
    void overlayClosing(double)override;
    void update(const app::ModuleFrame&)override;
    void prewarm(const app::ModuleFrame&)override;
    bool requiresFrames(double)const override;
    std::optional<double>nextWakeTime(double)const override;
    bool deadline(double)override;
    void upload(native::Renderer&)override;
    std::span<const native::LayerCompositionEntry>entries(core::Module)override;
    void collected(native::Renderer&)override;
    void release(native::Renderer&)override;
    bool covers(core::Point)const override;
    bool pointerLocked()const override;
    bool capturesPointer()const override;
    bool capturesWheel()const override;
    bool capturesKeys()const override;
    bool suppressesKeyFilters()const override;
    bool pointer(const app::PointerEvent&,double)override;
    // Fractional detents are a precision touchpad (pans), whole ones a mouse
    // wheel (zooms); Alt stands in for the Mac's ⌘ / ⌥ (zoom with a touchpad).
    bool wheel(const app::WheelEvent&,double)override;
    bool wheel(const app::WheelEvent&,bool alt,double); // explicit modifier (tests)
    bool key(const app::KeyEvent&,double)override;
    bool key(const app::KeyEvent&,std::uint32_t nativeModifiers,double); // 1 Alt, 2 Ctrl, 4 Shift, 8 Win (tests)
    bool filterKey(const app::NativeMessage&)override;
    // The owner's posted dialog notice (the modal dialog runs here), then the core's.
    bool message(const app::NativeMessage&,double)override;
    void focus(bool,double)override;
    void cancelInteraction(double)override;
    bool finishEditing(double)override;
    bool flush(double)override;
    // overlayConcealed: the closed HUD keeps no preview, texture or GPU processor.
    bool hostEvent(const app::HostEvent&,double)override;
    // Source importFiles (first file; ignored while exporting), routed only
    // while the canvas takes input, captures no pointer and shows no panel.
    bool drop(std::span<const std::string>,core::Point,double)override;
    std::optional<app::ModuleCursor>cursorAt(core::Point)const override;
    bool automationRefresh(IRawElementProviderFragmentRoot*)override;
    std::size_t automationCount()const override;
    long automationChild(std::size_t,IRawElementProviderFragment**)const override;
    long automationFromPoint(double,double,IRawElementProviderFragment**)const override;
    void automationDisconnect()noexcept override;
    std::optional<core::Module>automationModule()const noexcept override;
    app::ModuleResidency residency()const override;

    // --- Owner services (hidden coverage, tests) ---
    // The shared frame server: the root calls this once after the broker's
    // accept()/sample() (the retained video record: position, trim end, texture).
    bool refreshSharedMedia(double time);
    modules::MediaAssemblyCanvas&canvas()const noexcept;
    DesignOwnerCore&core()const noexcept;
    const modules::MediaAssemblyController&controller()const noexcept;
    MediaAssemblyOwnerStats stats()const noexcept;
    std::optional<native::MediaAssemblyDialogRequest>pendingDialog()const;
    // The played-frame filter attached to the current player request: null
    // without a player, for geometry-only edits or without GPU shaders.
    std::shared_ptr<const std::function<void(void*)>>playbackFrameFilter()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
