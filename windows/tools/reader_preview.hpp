#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/reader_scene.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
namespace endfield::tools {
struct ReaderScrollSnapshot {native::NativeReaderScrollSnapshot scene;double time{},targetOffset{},zoom{},panY{},deferred{};int pendingDirection{};std::uint64_t stateRevision{};bool providerBusy{};};
struct ReaderImportAction {
    enum class Kind {chooseLocal,useShelf};Kind kind{};std::string shelfID;std::uint64_t generation{};
};
struct ReaderPreviewOptions {
    native::LayerRasterOptions raster;
    modules::ReaderAppearance appearance;
    modules::ReaderStrings strings=modules::ReaderStrings::simplifiedChinese();
    std::function<std::string()>newID;
    // Configuration-event catalogs only; neither filesystem/provider/worker is
    // queried from a pointer/pose frame. Shelf choices are already available,
    // supported original references, not file contents or copied books.
    std::function<std::vector<modules::ReaderMenuChoice>()>shelfChoices;
    std::function<std::vector<std::string>()>fontFamilies;
    bool reduceMotion{};
};
// Borrows ReaderState (its one existing FIFO UtilityExecutor/provider), shared
// rasterizer and module transition. No service, TSF, HWND, clock, renderer,
// private worker or timer. The root owns the one picker and validated-file
// import job, returns generation-tagged completion, then publishes entries.
class ReaderPreview final {
public:
    ReaderPreview(modules::ReaderState&,native::LayerRasterizer&,ReaderPreviewOptions);
    ~ReaderPreview();
    void resize(const app::ClientMetrics&);
    void setAppearance(modules::ReaderAppearance);void setStrings(modules::ReaderStrings);
    void setReduceMotion(bool)noexcept;void setOverlayVisible(bool,double);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;std::optional<double>nextWakeTime()const noexcept;
    bool deadline(double);bool covers(core::Point)const;
    ReaderScrollSnapshot scrollSnapshot()const noexcept; // source geometry only; no IDs/text/paths or extra work
    bool pointer(const app::PointerEvent&,double);bool wheel(const app::WheelEvent&,double);
    bool key(const app::KeyEvent&,double);bool key(const app::KeyEvent&,bool modified,double);
    // Local event routing only: source Reader menus/pans do not lock HUD tilt.
    bool capturesPointer()const noexcept;bool pointerLocked()const noexcept;void cancelInteraction(double);
    std::optional<ReaderImportAction>takeImportAction();
    bool importRequestCurrent(std::uint64_t)const noexcept;
    // Native dialog cancellation leaves the existing book untouched. Call begin
    // only after an actual reference is selected, before validation starts.
    bool beginImport(std::uint64_t,double);
    bool receiveImport(std::uint64_t,modules::ReaderBook,double);
    bool receiveImportError(std::uint64_t,std::optional<std::string>,double);
    void queueCapacityAvailable();
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
