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
};
// Header wallet owner (HUDAccountGauge at header position 292,0, i.e. design
// point 562,2 on the core plane). Borrows the app-owned account controller and
// the shared rasterizer/renderer; owns no timer, window or service. update()
// receives the same projected centre homography and chrome opacity that the
// header uses. Hidden header mode removes the gauge completely.
class AccountSanityGaugePreview final {
public:
    AccountSanityGaugePreview(modules::hypergryph::HypergryphAccountController&,native::LayerRasterizer&,
        const std::filesystem::path& accountResources,AccountSanityGaugeOptions);
    ~AccountSanityGaugePreview();
    void resize(const app::ClientMetrics&);
    void setLanguage(core::Language,double time);
    void setReduceMotion(bool) noexcept;
    void setOverlayVisible(bool,double time);
    // Controller presentation/sanity or Work Mode changed: re-evaluate the value.
    void contentChanged(double time);
    void update(const core::Matrix4& projectedCenter,float chromeOpacity,double time);
    bool requiresFrames(double time) const;
    bool covers(core::Point logicalClientPoint) const;
    bool capturesPointer() const noexcept;   // open recovery popover consumes clicks
    bool pointer(const app::PointerEvent&,double time);
    bool key(const app::KeyEvent&,bool modified,double time); // Esc closes; Return/Enter/Space refresh
    bool dismiss(double time);
    std::optional<double> nextWakeTime(double hostTime) const;
    bool deadline(double hostTime);
    const modules::hypergryph::SanityGaugeModel& model() const noexcept;
    std::vector<modules::hypergryph::GaugeAction> accessibleActions() const;
    native::NativeSanityGaugeStats sceneStats() const noexcept;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
#endif
