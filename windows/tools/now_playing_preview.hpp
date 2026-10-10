#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/now_playing_scene.hpp"
#include "native/now_playing_service.hpp"
#include "native/now_playing_image.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
namespace endfield::tools {
struct NowPlayingPreviewOptions {
    native::LayerRasterOptions raster;modules::NowPlayingAppearance appearance;
    native::NativeNowPlayingArtwork::Decoder decode{native::decodeNowPlayingImage};
    std::function<void()>changed,onLock;
};
// Sole activation owner of the borrowed service. Service, executor and raster
// outlive this owner. No provider/worker/timer/window is created here. Host
// drains its ONE utility queue, calls utilityCompleted, folds nextWakeTime
// into its existing scheduler, and presents retained entries normally.
class NowPlayingPreview final {
public:
    NowPlayingPreview(native::NativeNowPlayingService&,native::LayerRasterizer&,
        app::UtilityExecutor&,NowPlayingPreviewOptions);
    ~NowPlayingPreview();
    void resize(const app::ClientMetrics&);
    bool setAppearance(modules::NowPlayingAppearance,double time);
    void setLanguage(core::Language,double time);void setOverlayVisible(bool,double time);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool utilityCompleted(double time);bool deadline(double time);
    std::optional<double>nextWakeTime()const;
    bool requiresFrames(double time)const;bool covers(core::Point)const;
    bool containsControl(core::Point,double time)const;bool capturesPointer()const noexcept;
    bool pointer(const app::PointerEvent&,double time);
    // Host supplies modifiers explicitly; avoids OS keyboard reads in tests.
    bool key(const app::KeyEvent&,bool modified,double time);
    bool perform(modules::NowPlayingAction,double time);
    bool setSlider(modules::NowPlayingSliderKind,double value,double time);
    void cancelInteraction(double time);bool pointerLocked()const noexcept;
    const modules::NowPlayingPresentation&presentation()const noexcept;
    native::NowPlayingSceneStats sceneStats()const noexcept;
    native::NativeNowPlayingArtwork::Stats artworkStats()const;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void release(native::Renderer&);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
