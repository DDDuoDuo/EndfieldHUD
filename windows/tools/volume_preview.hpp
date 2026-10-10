#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/volume_scene.hpp"
#include "native/volume_icon_provider.hpp"
#include "native/layer_scene.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/module_presentation.hpp"
namespace endfield::tools {
// Borrowed app-row icon source, normally a NativeVolumeIconProvider over the
// ONE shared Shelf Shell-icon worker. The owner asks only for the visible app
// rows (<= 8) after content events, never per frame, and hides the request set
// when Volume leaves, the Headphones page or a chooser is shown. When the
// shell routes the shared icon worker's completion to the provider's drain(),
// call VolumePreview::iconsChanged() so successful bindings reach the artwork.
struct VolumeIconHooks {
    std::function<bool(std::span<const native::VolumeIconApplication>)>setVisible;
    std::function<const native::VolumeIconPlan*()>plan;
    std::function<void()>hide;
};
struct VolumePreviewOptions {
    native::VolumeSnapshot initial;
    native::VolumeCallbacks actions;
    native::VolumeStrings strings=native::VolumeStrings::simplifiedChinese();
    native::VolumeStyle style;
    double rasterDensity{2};bool reduceMotion{};
    native::LayerImageSource*memoryImages{}; // Borrowed shared Shell icon cache; outlives this preview.
    VolumeIconHooks icons; // Optional; without it rows keep the source nil icon.
};
// No audio provider/window/timer ownership: the existing shell supplies fresh
// event snapshots, supported write callbacks, its sampled transition and one
// LayerComposition. A service owner aggregates audio activation across users.
class VolumePreview final {
public:
    VolumePreview(native::LayerRasterizer&,VolumePreviewOptions);
    ~VolumePreview();
    void resize(const app::ClientMetrics&);
    bool receiveSnapshot(native::VolumeSnapshot);bool receiveAudio(const native::AudioSnapshot&);
    // After the borrowed icon provider drained new completions. True when the
    // app-row artwork changed (redraw); false while hidden or unchanged.
    bool iconsChanged();
    std::span<const native::VolumeIconApplication>visibleIcons()const noexcept;
    void setStyle(native::VolumeStyle);void setStrings(native::VolumeStrings);void setReduceMotion(bool)noexcept;
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double time)const;bool covers(core::Point logicalClientPoint)const;
    bool pointer(const app::PointerEvent&,double time);bool wheel(const app::WheelEvent&,double time);
    bool key(const app::KeyEvent&,double time);bool key(const app::KeyEvent&,bool modified,double time);
    bool pointerLocked()const noexcept;void cancelInteraction(double time);
    // Source HUDVolumeInteraction accessibility projection. clientRect maps a
    // 400x334 canvas rect through the current module plane to the bounding
    // box in logical client points; none while Volume is not presented and
    // accepting input (the source hides its projected controls). perform and
    // setAccessibleValue are the projected button press and NSSlider value
    // (clamped to the slider range); both refuse disabled controls.
    bool acceptsInput()const noexcept;
    std::optional<core::Rect>clientRect(core::Rect canvas)const;
    bool perform(std::string_view action,double time);
    bool setAccessibleValue(std::string_view slider,double value,double time);
    const native::VolumeController&state()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
// Creates supported master/balance and asynchronous relative app writers.
// Construction performs no API calls;
// each explicit user write rechecks the current controlled endpoint. Optional
// activation belongs to the shared service owner, rather than an observer here.
native::VolumeCallbacks volumeCallbacksFromSystemServices(native::SystemServices&,
    std::function<void(bool)> sharedAudioActivation={});
}
#endif
