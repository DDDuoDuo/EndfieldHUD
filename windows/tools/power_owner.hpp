#pragma once
#include "app/utility_executor.hpp"
#include "core/data/json.hpp"
#include "modules/power_policy.hpp"
#include "native/charge_indicator_panel.hpp"
#include "native/power_service.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#ifdef _WIN32
namespace endfield::tools {
// What the production app owner receives from the Power area. All callbacks
// run on the owner thread; none may destroy this owner reentrantly.
struct PowerOwnerCallbacks {
    // SystemEventRecorder battery section (allowlisted metadata); the host
    // stamps the event ID/time and appends it to its Event Log store.
    std::function<void(modules::EventKind, const modules::EventMetadata&)> recordEvent;
    // Battery reading changed: refresh the tray status line/tooltip, the
    // Power page (BatteryPreview::receive) and the HUD badge content.
    std::function<void(const modules::BatteryReading&)> readingChanged;
    // Position editor confirmed: persist placement=custom, customScreenID,
    // customPositionX/Y through the settings queue.
    std::function<void(const modules::ChargePosition&)> savePosition;
    // Editor finished either way: the source reopens the HUD on Display.
    std::function<void()> positionEditFinished;
};
// One production owner for the Power area's desktop behaviour: the single
// observer of the shared SystemServices battery provider (PowerService with
// the battery-class capacity pair), the AppDelegate battery rules
// (PowerAlertCoordinator), the event recorder, the tray status text and the
// floating charge alert panel. Event driven: battery notifications, settings
// events, tray actions, HUD/projection transitions, sleep/wake, plus the
// host's frame clock while the alert animates (requiresFrames/nextWakeTime).
class PowerOwner final {
public:
    // reader: battery-class capacity reader for the utility worker (the
    // default reads the system batteries; tests inject synthetic reports).
    PowerOwner(app::UtilityExecutor&, native::LayerRasterizer&, native::ChargePanelOptions, PowerOwnerCallbacks,
               native::PowerService::Reader reader = {});
    ~PowerOwner();
    PowerOwner(const PowerOwner&) = delete;
    PowerOwner& operator=(const PowerOwner&) = delete;
    // ServiceChange::battery from the app-lifetime SystemServices.
    void receive(const native::BatterySnapshot&, double time);
    // Saved settings object (data_store fields), resolved theme/language and
    // the effective reduce-motion (setting || OS animation preference).
    void setSettings(const ehud::data::Json& settings, bool systemDark, core::Language, bool reduceMotion, double time);
    void preview(double time);               // tray "Preview charging effect"
    bool editPosition(double time);          // after the HUD finished closing
    void systemOverlayOpening(double time);  // before the HUD opens (hides the alert)
    void systemOverlayClosed(double time);
    void setProjectionActive(bool, double time);
    void suspend(modules::PowerSuspension, double time);
    // fresh: SystemServices::refresh_battery() read while still suspended.
    void resume(modules::PowerSuspension, const std::optional<native::BatterySnapshot>& fresh, double time);
    void setTelemetry(const std::optional<modules::ChargeTelemetry>&);
    bool telemetryDemand() const noexcept; // alert visible with a non-battery metric
    // Shared frame clock / one-shot wake.
    void advance(double time);
    bool requiresFrames(double time) const;
    std::optional<double> nextWakeTime() const;
    bool hasReading() const noexcept;
    const modules::BatteryReading& reading() const noexcept;
    bool devicePopupEnabled() const noexcept;
    std::string trayStatus(core::Language) const;
    std::string trayTooltip(core::Language) const;
    std::vector<modules::DeviceBatteryReading> deviceReadings(core::Language) const;
    native::ChargeIndicatorPanel& panel() noexcept;
    const modules::PowerAlertCoordinator& coordinator() const noexcept;
    // Quit: hide without animation and stop provider work.
    void terminate(double time);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
