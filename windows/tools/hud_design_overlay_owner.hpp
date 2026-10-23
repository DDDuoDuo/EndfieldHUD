#pragma once
// The shared centre overlay owner: one per HUD, created by the host BEFORE
// every design owner. It owns the modules::design::Context (palette, text
// services, deadlines, the CenterOverlay model) and the native pieces every
// design owner shares: the DirectWrite measurer and the overlay's
// NativeDesignScene. Mac: HUDDesignContext + HUDCenterOverlay as SystemHUDView
// hosts them (Sources/HUDDesignScreen.swift:25-97, Sources/HUDDesignOverlay.swift).
//
// It replaces the separate modal cards: the quit confirmation
// (app/quit_confirmation.*) becomes a confirm request (Phase B2) and the
// "Keep this UI scale?" card (modules/settings_safety.*) a countdown request
// (Phase B1), both through this owner's confirm bar.
//
// ModuleOwner role: the overlay is placed like the content box on the centre
// plane (core::source::DesktopChromeLayout::boxWorld of the frame, without the
// switch motion) but composed ABOVE every module
// (modalEntries); while CenterOverlay::capturesInput() it captures pointer,
// wheel and keys (routing pointerCapture / wheelCapture / keyCapture 0, the
// quit card's slot) and suppresses key filters; every event is consumed
// (Esc / Return decide; presses go to the pills, or the outside press of a
// twin-pill request). Appearance events are applied to the Context here once
// (Context::apply); deadlines (120 ms confirm callback, 1.5 s toast, hides,
// scroll bars, module timers) are its nextWakeTime / deadline. The context is
// published for areas (ApplicationHost::publish(contextServiceKey, context)).
// No timer, thread or window of its own.
#include "modules/hud_design_page.hpp"
#ifdef _WIN32
#include "app/module_owner.hpp"
#include "native/hud_design_automation.hpp"
#include "native/hud_design_scene.hpp"
#include "native/hud_design_text_metrics.hpp"

namespace endfield::tools {
struct DesignOverlayOptions {
    native::LayerRasterOptions raster;
    std::function<double()> now;
    std::function<void()> changed;   // present a frame (+ UIA refresh)
    void* window{};                  // the HUD HWND (UIA screen rects); null: no rects
    // The applied preference record's recordingMode (Mac AppConfiguration
    // recordingMode), read on setAppearance and settingsChanged; absent: off.
    std::function<bool()> recordingMode;
};
class DesignOverlayOwner final : public app::ModuleOwner {
public:
    DesignOverlayOwner(native::LayerRasterizer&, DesignOverlayOptions);
    ~DesignOverlayOwner() override;
    modules::design::Context& context() noexcept;
    native::NativeDesignTextMeasurer& measurer() noexcept;
    // CenterOverlay::dimFilter (core::ContentDimTrack amount): the host passes
    // it to Renderer::setContentFilter over the module entries each frame.
    double contentDim(double now) const noexcept;
    // The filter the host passes to Renderer::setContentFilter over its module
    // entries' draws [begin, begin + count) (LayerComposition::drawRange):
    // CIGaussianBlur 1.5 centre-plane design units (core::source::planeScale
    // of the last frame's centre at the disc, times the DPI scale) and
    // CIExposureAdjust -2.2 EV (core::ContentDimTrack), eased by contentDim;
    // nullopt while no dim is needed.
    std::optional<native::ContentFilter> contentFilter(double now, std::size_t begin, std::size_t count) const;
    // The overlay draws something (bar, toast, dim or their hide motion).
    bool presented() const noexcept;

    std::string_view name() const noexcept override { return "designOverlay"; }
    app::ModuleRouting routing() const noexcept override;
    void resize(const app::ClientMetrics&) override;
    void setAppearance(const app::ModuleAppearance&, double) override;
    void setOverlayVisible(bool, double) override;
    void overlayClosing(double) override;                     // CenterOverlay::reset
    void update(const app::ModuleFrame&) override;            // places the overlay on the current boxWorld
    bool requiresFrames(double) const override;
    std::optional<double> nextWakeTime(double) const override;
    bool deadline(double) override;                           // Context::advance
    void upload(native::Renderer&) override;
    std::span<const native::LayerCompositionEntry> modalEntries() override;
    void collected(native::Renderer&) override;
    void release(native::Renderer&) override;
    bool covers(core::Point) const override;
    bool capturesPointer() const override;
    bool capturesWheel() const override;
    bool capturesKeys() const override;
    bool suppressesKeyFilters() const override;
    bool pointer(const app::PointerEvent&, double) override;
    bool wheel(const app::WheelEvent&, double) override;
    bool key(const app::KeyEvent&, double) override;
    bool hostEvent(const app::HostEvent&, double) override;   // systemSettingsChanged "intl" -> region
    bool automationRefresh(IRawElementProviderFragmentRoot*) override;
    std::size_t automationCount() const override;
    long automationChild(std::size_t, IRawElementProviderFragment**) const override;
    long automationFromPoint(double, double, IRawElementProviderFragment**) const override;
    void automationDisconnect() noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
