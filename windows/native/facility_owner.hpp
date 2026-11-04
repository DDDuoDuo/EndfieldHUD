#pragma once
// The facility alerts, the daily boot log and their host glue as ONE module
// area (app/module_owner.hpp "Adding a module area"; registered by
// cmake/modules/hud_facility.cmake). Mac authority: SystemHUDView's facility
// and boot-log code (installFacilityLayers, layoutFacilityLayers,
// facilityChanged, openFacilityModule, hudDidBecomeInteractive,
// finishBootLog, bootLogLines, the boot-log / banner / tag input branches;
// SHELL-3..6, SHELL-20..25) and OverlayController (the app-wide
// FacilityAlertMonitor, consumeDailyBootLog; SHELL-76, SHELL-78).
//
// Planes (SHELL-20):
//  - banner: flat in shell space at the top of the screen (the Mac's
//    `canvas`, zPosition 1000): x 500 (design centre), top 14 points below
//    the client top (Windows has no camera housing: the Mac's safe-area
//    term is 0), scaled by the design unit; composed in modalEntries, which
//    the host composes after the shared overlay (app/application.cpp
//    publishNative): the canvas's z 1000 is above the core plane;
//  - tags: on the CORE plane under the header (they tilt with it): anchored
//    at their top-right corner at (header.minX - 8, header.minY + 50) =
//    (262, 52) design units, scaled by the unit; composed in planeEntries;
//  - boot log: on the content box at (10, 30) px (a sublayer of the Mac's
//    overlay, HUDLayerOrder.bootLog 30 above the toast 25 and the confirm
//    bar 20), composed in modalEntries below the banner, above the overlay.
// All three are design-library trees in their own NativeDesignScene, posed
// numerically each frame; they rasterize on content events only and keep no
// artwork while nothing shows (park).
//
// Monitor cadence (D4): the Mac polls every 60 s on a timer and reacts to
// memory-pressure, thermal, mount and wake events. Windows has none of those
// events for memory; this owner adds no timer or thread: it re-reads on the
// host's existing wake-ups - every shared-executor drain (the Activity
// system samples' cadence, 5 s while the HUD is closed, 1 s while Activity
// shows) evaluates the memory pressure (two cheap kernel queries) and, from
// 60 s after the last full read, runs a full read (disk + memory) on the
// shared UtilityExecutor; resume (Mac didWake), enabling and the end of the
// boot log read at once; without any drain for 65 s its own deadline reads.
// Volume mount/unmount (Mac didMount/didUnmount -> volumesChanged): the
// HUD window's WM_DEVICECHANGE volume arrival / removal reaches message()
// (never consumed: Storage and other owners see it too) and reads at once.
// Thermal: none (gap D4).
//
// Daily boot log (SHELL-24, SHELL-78): when the HUD opens, today's first
// opening (local yyyy-MM-dd, Settings::consumeDailyBootLog through
// ApplicationHost::updateSettings) with Daily boot log on, Reduce Motion off
// and outside diagnostic runs (hidden coverage, development previews and
// isolated --data-root gate runs) defers the deploy entrance and conceals
// the centre (modules::boot::BootLogStage, filled by the host); once the
// opening finished the log plays, any click or key skips it, and at its end
// the centre shows again and enters from below, then the alerts are checked.
// Lines come from modules::boot::BootLogSources (host / areas), the Event Log
// (Work Mode streak), the Work Mode controller (hours), the monitor's disk
// reading (else a direct read) and the profile card's name.
//
// Recording mode: nothing here (the tag no longer exists, SHELL-16/72); the
// design Context's env().recordingMode is applied by the host.
#include "app/application.hpp"
#include "modules/boot_log.hpp"
#include "modules/facility_tags.hpp"
#include "native/facility_sources.hpp"
#include "native/layer_raster.hpp"
#include <memory>

namespace endfield::app { class UtilityExecutor; }
namespace endfield::facility {
// The readers (production: the Windows readers of native/facility_sources;
// hidden runs and tests: synthetic values).
struct FacilitySources {
    // Worker thread: the startup disk; `startupName` names an unnamed one.
    std::function<std::optional<modules::FacilityDiskReading>(std::string_view startupName)> disk;
    // Any thread: physical memory in use and the low-memory signal.
    std::function<std::optional<native::WindowsFacilityMemory>()> memory;
    // The boot log's system values.
    std::function<modules::boot::BootLogDate()> localDate;
    std::function<double()> uptime;
    std::function<std::optional<std::int64_t>(double foundationSeconds)> localDay;
    std::function<std::optional<modules::boot::BootLogDisk>()> bootDisk;
    static FacilitySources windows();
};
struct FacilityOwnerOptions {
    native::LayerRasterOptions raster;
    void* window{};                     // the HUD HWND (UIA screen rects); null: no rects
    std::function<void()> changed;      // present a frame + UIA refresh
    FacilitySources sources;
    app::UtilityExecutor* executor{};   // full reads run here; null: synchronously (tests)
    std::string version{"1.3.0"};       // the terminal line's version (CFBundleShortVersionString)
    bool diagnostic{};                  // OverlayController.isDiagnosticRun: no live alerts, no boot-log day
};
class FacilityOwner final : public app::ModuleOwner {
public:
    static constexpr std::string_view areaName = "facility";
    FacilityOwner(app::ApplicationHost&, modules::design::Context&, native::LayerRasterizer&, FacilityOwnerOptions);
    ~FacilityOwner() override;
    // The host fills the stage after the areas are built (published under
    // modules::boot::bootLogStageKey while this owner lives).
    modules::boot::BootLogStage& stage() noexcept;
    // Verification.
    const modules::FacilityAlertMonitor& monitor() const noexcept;
    const modules::facility::FacilityBanner& banner() const noexcept;
    const modules::facility::FacilityTagStack& tags() const noexcept;
    const modules::boot::BootLog& bootLog() const noexcept;
    bool playsBootLogOnOpen() const noexcept;

    std::string_view name() const noexcept override { return areaName; }
    app::ModuleRouting routing() const noexcept override;
    void resize(const app::ClientMetrics&) override;
    void setOverlayVisible(bool, double) override;
    void overlayClosing(double) override;
    void update(const app::ModuleFrame&) override;
    bool requiresFrames(double) const override;
    std::optional<double> nextWakeTime(double) const override;
    bool deadline(double) override;
    void upload(native::Renderer&) override;
    std::span<const native::LayerCompositionEntry> planeEntries() override;
    std::span<const native::LayerCompositionEntry> modalEntries() override;
    void collected(native::Renderer&) override;
    void release(native::Renderer&) override;
    bool covers(core::Point) const override;
    bool capturesPointer() const override;
    bool capturesKeys() const override;
    bool suppressesKeyFilters() const override;
    bool pointer(const app::PointerEvent&, double) override;
    bool key(const app::KeyEvent&, double) override;
    bool message(const app::NativeMessage&, double) override;
    void cancelInteraction(double) override;
    bool hostEvent(const app::HostEvent&, double) override;
    bool automationRefresh(IRawElementProviderFragmentRoot*) override;
    std::size_t automationCount() const override;
    long automationChild(std::size_t, IRawElementProviderFragment**) const override;
    long automationFromPoint(double, double, IRawElementProviderFragment**) const override;
    void automationDisconnect() noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Area factory: production readers in EndfieldHUD.exe; synthetic, nominal
// readings (no alert, no boot log) in hidden coverage and development
// previews. Null without the shared design Context.
std::unique_ptr<app::ModuleOwner> facilityModule(const app::ApplicationModuleContext&);
} // namespace endfield::facility
