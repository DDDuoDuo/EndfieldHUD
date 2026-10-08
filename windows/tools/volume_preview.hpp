#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "native/volume_scene.hpp"
#include "native/layer_scene.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/module_presentation.hpp"
namespace endfield::tools {
struct VolumePreviewOptions {
    native::VolumeSnapshot initial;
    native::VolumeCallbacks actions;
    native::VolumeStrings strings=native::VolumeStrings::simplifiedChinese();
    native::VolumeStyle style;
    double rasterDensity{2};bool reduceMotion{};
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
    void setStyle(native::VolumeStyle);void setStrings(native::VolumeStrings);void setReduceMotion(bool)noexcept;
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double time)const;bool covers(core::Point logicalClientPoint)const;
    bool pointer(const app::PointerEvent&,double time);bool wheel(const app::WheelEvent&,double time);
    bool key(const app::KeyEvent&,double time);bool key(const app::KeyEvent&,bool modified,double time);
    bool pointerLocked()const noexcept;void cancelInteraction(double time);
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
