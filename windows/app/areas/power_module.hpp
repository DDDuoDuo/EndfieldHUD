#pragma once
// Power area (app/module_owner.hpp, "Adding a module area"; registered from
// cmake/modules/power.cmake). One ModuleOwner joins EndfieldHUD.exe and the
// hidden coverage harness with:
//   - the HUD charge badge (Sources/HUDChargeBadge.swift on SystemHUDView's
//     charge plane: the core-plane pose plus the source shell's 38-point
//     offset, entrance after 0.58 s, 3 s compact hold, hover expansion, a
//     click selects Power, the exit follows the HUD close);
//   - the floating desktop charge alert (OverlayController's panel in its
//     charging role) and the AppDelegate battery rules through
//     tools::PowerOwner: the single observer of the shared SystemServices
//     battery provider, the battery-class mWh pair on the shared
//     UtilityExecutor, DisplayPolicy, sleep/wake and position editing.
// Overlay lifecycle: setOverlayVisible(true) is OverlayController's
// toggleSystemOverlay (the alert yields to the HUD), the overlayConcealed
// broadcast is onSystemClosed (presentLatestSnapshot, a pending position edit
// begins), quitting is onQuitAccepted (alerts stop until a cancelled quit
// reopens the HUD). The alert animates on the shared waitable timer while the
// HUD (and its frame clock) is closed; no timer, thread or poll is created.
// Residency: the launch prewarm (ModuleOwner::prewarm) places the hidden
// badge once and installs its artwork, so its reveal 0.58 s into the first
// opening uploads nothing; installed artwork keeps no CPU pixel copy. The
// alert's window, D3D11 device and textures exist only while the alert or
// the position editor shows (created on demand from the build-time hud.hlsl
// programs, released 10 s after it is ordered out with the HUD closed); each
// presentation starts after its first frame, so that creation never skips
// the entrance's first frames.
//
// Host hooks that only the Application can call (tray preview, Settings
// "Edit position", projection, the Power page's capacity) reach this area
// through PowerAreaService, published in the area service directory under
// PowerAreaService::key. Every hook is optional: without it the area still
// observes the provider, alerts, draws the badge and follows the HUD.
#include "app/application.hpp"
#include "core/localization.hpp"
#include "modules/battery_presentation.hpp"
#include "modules/charge_indicator.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::power {
// Host-facing service of the Power area. UI thread only; valid while the
// area's owner is registered (look it up when used).
class PowerAreaService {
public:
    static constexpr std::string_view key = "power.charge";
    // Tray "Preview charging effect" (AppDelegate.preview): the current
    // reading, or the 75 % charging demo without a battery.
    virtual void preview(double time) = 0;
    // Settings > Display > Battery alert > Edit position
    // (onEditBatteryPosition): closes the HUD, then edits after concealment.
    // Deferred through a private WM_APP notice, so it may be called from a
    // Settings controller callback. False while suspended or quitting.
    virtual bool editPosition(double time) = 0;
    virtual bool editingPosition() const = 0;
    // Mac isProjectionActive: no alert over Projection; readings stay silent.
    virtual void setProjectionActive(bool active, double time) = 0;
    // Latest provider reading merged with the battery-class capacity pair
    // (Power page and tray text). Empty reading: no internal battery/unknown.
    virtual const modules::BatteryReading& reading() const = 0;
    virtual std::string trayStatus(core::Language) const = 0;
    virtual std::string trayTooltip(core::Language) const = 0;
    virtual std::string previewMenuTitle(core::Language) const = 0;
    // Power navigation entry help (HUDChargeBadge.accessibilityLabel).
    virtual std::string powerAccessibilityLabel() const = 0;
    // Shared Activity samples for a non-battery alert metric, attached only
    // while telemetryDemand() is true (alert or badge visible).
    virtual bool telemetryDemand() const = 0;
    virtual void setTelemetry(const std::optional<modules::ChargeTelemetry>&, double time) = 0;
    // Optional host observer of the editor (SystemOverlayLifecycle::
    // setEditingPosition: no summon over the editor, as on the Mac).
    virtual void setEditingObserver(std::function<void(bool)>) = 0;
protected:
    ~PowerAreaService() = default;
};

// Optional Power page sink the host may publish under this key (exact type):
// receives every merged reading (with the mWh pair); returns true when the
// page artwork changed. The area requests a frame for it.
inline constexpr std::string_view powerPageSinkKey = "power.batteryPage";
using PowerPageSink = std::function<bool(const modules::BatteryReading&)>;

// Area factory: the production owner in EndfieldHUD.exe (shared SystemServices
// provider, verified package shader for the alert window), synthetic battery
// readings with a hidden WARP alert in hidden coverage, and the shared
// provider (else a synthetic reading) in development previews.
std::unique_ptr<app::ModuleOwner> powerModule(const app::ApplicationModuleContext&);
} // namespace endfield::power
