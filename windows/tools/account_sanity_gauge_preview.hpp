#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "modules/hypergryph_account_controller.hpp"
#include "native/hypergryph_account_sanity_gauge_scene.hpp"
#include <filesystem>
namespace endfield::tools {
struct AccountSanityGaugeOptions {
    native::LayerRasterOptions raster;
    core::Language language{core::Language::english};
    bool reduceMotion{};
    // Current Work Mode timer (Mac WorkModeSnapshot): shown when unlinked or in
    // Work Mode header mode. `running` lets the gauge wake at minute changes.
    std::function<modules::hypergryph::WorkModeGaugeInput()> workMode;
    std::function<bool()> workModeRunning;
    // HUDAccountGauge.onOpenWorkMode (Mac 1.3.0): a press on the bar while it
    // has no sanity data opens the Work Mode module through the host.
    std::function<void()> openWorkMode;
};
// Header wallet owner (HUDAccountGauge, authored at header position 292,0,
// i.e. design point 562,2 on the core plane; Display > HUD elements moves and
// sizes it through setPlacement). Borrows the app-owned account controller and
// the shared rasterizer/renderer; owns no timer, window or service. update()
// receives the same projected centre homography and chrome opacity that the
// header uses. Hidden header mode removes the gauge completely.
class AccountSanityGaugePreview final {
public:
    AccountSanityGaugePreview(modules::hypergryph::HypergryphAccountController&,native::LayerRasterizer&,
        const std::filesystem::path& accountResources,AccountSanityGaugeOptions);
    ~AccountSanityGaugePreview();
    void resize(const app::ClientMetrics&);
    // SystemHUDView.designScale (DesktopChromeLayout::designScale of the
    // frame's chrome): with the client's DPI scale it is the gauge's base
    // scale (hudRasterScale). A change redraws the value, icon and recovery
    // text at once when their render scale changes.
    void setDesignScale(double,double time);
    // SystemHUDView.applyOverlayLayout for the gauge: its drawn top-left on
    // the core plane (the Display > HUD elements placement, clamped), its
    // size (HUDAccountGauge.sizeFactor: the text and icon redraw at it) and
    // the recovery menu's direction (popoverDirection). The wallet and the
    // menu are posed at center x T(origin) x S(size).
    void setPlacement(core::Point origin,double size,modules::hypergryph::GaugePopoverDirection,double time);
    void setLanguage(core::Language,double time);
    void setReduceMotion(bool) noexcept;
    void setOverlayVisible(bool,double time);
    // Controller presentation/sanity or Work Mode changed: re-evaluate the value.
    void contentChanged(double time);
    void update(const core::Matrix4& projectedCenter,float chromeOpacity,double time);
    bool requiresFrames(double time) const;
    // The shown wallet (whatever it shows) and an open popover: a left press
    // there is always consumed by pointer(), so it never falls through to the
    // source shell or the host's outside-the-circle close (a bar without
    // sanity data opens Work Mode, never closes the HUD).
    bool covers(core::Point logicalClientPoint) const;
    bool capturesPointer() const noexcept;   // open recovery popover consumes clicks
    bool pointer(const app::PointerEvent&,double time);
    bool key(const app::KeyEvent&,bool modified,double time); // Esc closes; Return/Enter/Space refresh
    bool dismiss(double time);
    std::optional<double> nextWakeTime(double hostTime) const;
    bool deadline(double hostTime);
    const modules::hypergryph::SanityGaugeModel& model() const noexcept;
    std::vector<modules::hypergryph::GaugeAction> accessibleActions() const;
    // The toggle's accessibility value (open popover countdowns), in the UI language.
    std::optional<std::string> accessibilityValue() const;
    // A gauge-local point (accessibleActions() rects) in logical client
    // coordinates through the current projected header pose (UIA, coverage);
    // empty before the first posed frame or when it does not project.
    std::optional<core::Point> clientPoint(core::Point local) const;
    // Projected bounding box of a gauge-local rect (logical client
    // coordinates) and the accessibility press of a gauge action.
    std::optional<core::Rect> clientRect(core::Rect local) const;
    bool perform(std::string_view id,double time);
    native::NativeSanityGaugeStats sceneStats() const noexcept;
    // The HUD finished closing: a shut recovery popover releases its card
    // (loaded again when it next opens); the wallet stays. True when parked.
    bool conceal();
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();        // the wallet
    // The recovery menu while it is open or fading out: the HUD's top layer
    // (SystemHUDView.accountPopoverHost), on the gauge's position and size.
    std::span<const native::LayerCompositionEntry> popoverEntries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
#endif
