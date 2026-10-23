#pragma once
// Storage on the design library: the port of StorageCanvas
// (Sources/TelemetryCanvases.swift:67-266, MODS-443..455) over the portable
// StorageController (modules/storage_state.hpp). It replaces the v1.2.0
// Windows page (modules/storage_presentation, native/storage_scene,
// tools/storage_preview: three 376 x 40 rows, the 32-segment meter, the
// rotating refresh arrow, the Windows-only "C: ▾" chooser shown from two
// disks): the disk dropdown is now always visible (it names the disk), its
// menu lists every listed disk by name, Refresh re-lists them and toasts.
//
// The controller reads, remembers the disk and refreshes; this canvas
// presents it and updates the readings in place. Controller changes reach
// the canvas through controllerChanged(now): the canvas's own actions call
// it at once; the owner calls it after a completion it fed (the Mac's
// controller.observe). No per-category breakdown, no telemetry loop.
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_page.hpp"
#include "modules/storage_state.hpp"

namespace endfield::modules {
class StorageCanvas final : public design::Canvas {
public:
    StorageCanvas(design::Context&, StorageController&);
    ~StorageCanvas() override;
    // Closes the HUD and opens the system storage settings (the host wires
    // it: OverlayController.openStorageAfterClose; Windows opens Settings >
    // System > Storage once the HUD is hidden).
    std::function<void()> onOpenSystemStorage;
    // A controller publication the owner fed (a completed read, a mount
    // event, the 60 s deadline): readings in place and the result toasts.
    void controllerChanged(double now);
    // `storage:settings` and `storage:refresh`, as the visible controls do.
    bool perform(std::string_view actionID, double now);
    // Filled cells and the low-space (red) state, as drawn.
    std::pair<int, bool> cellFill() const noexcept { return cellFill_; }
    static constexpr int animationCount = 0; // no telemetry-owned loops
    static std::string localizedError(const std::optional<std::string>&, core::Language);

    void buildPage(design::Page&) override;
    void didActivate() override;
    void didDeactivate() override { stopReading(); }
    void willLeave() override { stopReading(); }
    // The controller's 60 s capacity timer while the page reads (the Mac's
    // scheduled Timer): a module timer of the owner, nothing while hidden.
    std::optional<double> nextWakeTime(double) const override { return active_ ? controller_->nextWakeTime() : std::nullopt; }
    void wake(double now) override;
private:
    struct Live {
        design::Dropdown* volume{};
        design::BigNumeral *available{}, *used{};
        design::Cells* cells{};
        std::array<design::StatPlate*, 3> plates{};
        design::Readout* readout{};
    };
    StorageController* controller_;
    std::optional<Live> live_;
    bool active_{}, pendingRefreshToast_{}, switchingVolume_{};
    std::optional<std::string> lastError_;
    std::pair<int, bool> cellFill_{0, false};
    void stopReading();
    void changed();
    void openSettings();
    void refresh();
    void openVolumeMenu(const design::Dropdown&);
    std::string volumeTitle() const;
    void applyReadings(bool animated = true);
};
} // namespace endfield::modules
