#pragma once
#include "core/data/json.hpp"
#include "core/localization.hpp"
#include "core/scene.hpp"
#include "modules/battery_presentation.hpp"
#include "modules/charge_indicator.hpp"
#include "modules/event_log.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Portable Power policy: DisplayPolicy.swift, the AppDelegate battery
// presentation rules, the SystemEventRecorder battery section, the tray status
// line, DeviceBatteryProvider, AppConfiguration's alert settings and
// OverlayGeometry. Pure state and functions; the host supplies clocks, display
// work areas and the single shared battery provider. Nothing here reads power,
// starts a timer or touches a window.
namespace endfield::modules {
enum class ChargeDisplayAction : std::uint8_t { hide, showPersistent, showTransient, keepCurrent };
std::string_view chargeDisplayActionKey(ChargeDisplayAction) noexcept;
// DisplayPolicy.action: no battery hides; always mode is persistent; no or
// unavailable previous reading establishes a fresh baseline (hide); a change of
// external power presents transiently; everything else keeps the current
// presentation (percentage/charging-diagnostic updates never extend deadlines).
ChargeDisplayAction chargeDisplayAction(const BatteryReading& snapshot, const std::optional<BatteryReading>& previous,
                                        bool alwaysMode) noexcept;

enum class ChargeTheme : std::uint8_t { dark, light, system };
// AppConfiguration fields consumed by the alert, normalized exactly like
// AppConfiguration.normalized (duration 1...60 else 3, scale 0.65...1.6 else 1,
// custom position 0...1 else 0.5/0.9, six uppercase hex digits else FAD41F).
struct ChargeAlertSettings {
    bool alwaysMode{};
    double displayDuration{3};
    std::string accentHex{"FAD41F"};
    ChargeTheme theme{ChargeTheme::dark};
    double scale{1};
    bool customPlacement{};
    std::optional<std::uint32_t> customScreenID;
    double customX{0.5}, customY{0.9};
    ChargeMetric metric{ChargeMetric::battery};
    bool alertsEnabled{true}, devicePopupEnabled{true}, reduceMotion{}, lowPowerVisualMode{};
    ChargeAlertSettings normalized() const;
    ChargeColor accent() const; // of the normalized hex
    // Tolerant reader of the persisted settings object (data_store fields):
    // unknown/invalid values fall back like the Mac decoder.
    static ChargeAlertSettings fromSettings(const ehud::data::Json& fields);
    bool operator==(const ChargeAlertSettings&) const = default;
};

// OverlayGeometry in Windows virtual-desktop pixels with y increasing
// downwards. Mac point constants scale by the monitor's pixels per point.
// Saved positions keep the Mac convention: x from the left, y from the BOTTOM
// of the usable desktop, so imported custom positions land in the same place.
struct ChargeWorkArea {
    std::uint32_t id{};
    core::Rect bounds; // monitor work area in pixels
    double pixelsPerPoint{1};
    bool operator==(const ChargeWorkArea&) const = default;
};
struct ChargePosition {
    std::optional<std::uint32_t> screenID;
    double x{0.5}, y{0.9};
    bool operator==(const ChargePosition&) const = default;
};
core::Point chargeClampedAnchor(core::Point, const core::Rect& workArea, double scale, bool editing, double pixelsPerPoint) noexcept;
core::Point chargeAnchor(const ChargeAlertSettings&, const core::Rect& workArea, bool editing, double pixelsPerPoint) noexcept;
ChargePosition chargeNormalizedPosition(core::Point anchor, const ChargeWorkArea&) noexcept;
// OverlayController.layout: the indicator's center stays at the anchor while
// edit controls appear below it. Rectangles are virtual-desktop pixels.
struct ChargePanelLayout {
    core::Rect window, indicator, cancel, confirm;
};
ChargePanelLayout chargePanelLayout(core::Point anchor, double scale, bool editing, double pixelsPerPoint) noexcept;
// OverlayController.selectedScreen: requested (edit draft or saved custom
// screen) when connected, else the preferred internal/primary display, else first.
std::optional<std::size_t> chargeSelectedScreen(std::span<const ChargeWorkArea>, std::optional<std::uint32_t> requested,
                                                std::optional<std::size_t> preferred) noexcept;
// Stable 32-bit identity for a Windows display device path (FNV-1a over its
// UTF-16 code units), stored in the existing customScreenID number field.
std::uint32_t chargeScreenID(std::u16string_view persistentID) noexcept;

// SystemEventRecorder battery section: the first reading is a baseline;
// missing battery data never implies a cable removal.
struct PowerEvent {
    EventKind kind{};
    EventMetadata metadata;
    bool operator==(const PowerEvent&) const = default;
};
std::string_view powerEventState(const BatteryReading&) noexcept; // unavailable/charging/full/connected/battery
class PowerEventRecorder final {
public:
    // At most two events per reading; returned by value on a real transition only.
    std::vector<PowerEvent> receive(const BatteryReading&);
    void reset() noexcept { previous_.reset(); }
private:
    std::optional<BatteryReading> previous_;
};

// BatteryCapacityReading pair rules shared by every capacity provider: a
// matching hardware pair is integral, 0...1,000,000, maximum > 100 and
// current <= maximum; otherwise only an explicitly labelled 0...100 percent
// scale is used. A design capacity or estimate is never substituted.
std::optional<BatteryReading::Capacity> batteryCapacityPair(std::optional<double> current, std::optional<double> maximum,
                                                            std::string_view unit);
std::optional<BatteryReading::Capacity> batteryCapacityPercent(std::optional<unsigned> percentage);

// AppDelegate.updateMenu / preview menu item.
std::string powerTrayDescription(const BatteryReading&, core::Language);
std::string powerTrayTooltip(const BatteryReading&, core::Language);
std::string powerPreviewMenuTitle(core::Language);
// AppDelegate.demoSnapshot: 75 %, charging, 3,600 / 4,800 mAh.
BatteryReading chargeDemoReading();

// DeviceBatteryProvider: a pure adapter for the shared host reading. Accessory
// batteries are not provided (no Bluetooth/HID enumeration); unknown values stay
// unavailable rather than invented.
enum class DeviceBatteryAvailability : std::uint8_t { available, unavailable, noBattery };
enum class AccessoryBatteryCapability : std::uint8_t { notProvided };
struct DeviceBatteryReading {
    std::string id, name;
    bool host{true};
    std::optional<unsigned> percentage;
    DeviceBatteryAvailability availability{DeviceBatteryAvailability::noBattery};
    bool operator==(const DeviceBatteryReading&) const = default;
};
inline constexpr AccessoryBatteryCapability accessoryBatteryCapability = AccessoryBatteryCapability::notProvided;
// The stable nonidentifying host ID is preserved ("host-mac") so a future
// cross-platform accessory model keeps one key; its label is "This PC".
std::vector<DeviceBatteryReading> deviceBatteryReadings(const BatteryReading&, core::Language);
std::string deviceHostName(core::Language);

// What AppDelegate drives on OverlayController's indicator. The host's panel
// implements it (ChargeIndicatorPanel); tests use an inert recorder.
class ChargeAlertPresenter {
public:
    virtual ~ChargeAlertPresenter() = default;
    virtual bool visible() const = 0;
    virtual bool persistent() const = 0;
    virtual bool editingPosition() const = 0;
    virtual bool systemOverlayActive() const = 0;
    virtual bool projectionActive() const = 0;
    virtual void update(const BatteryReading&, bool preview, double time) = 0;
    virtual void show(bool persistent, double duration, bool replay, double time) = 0;
    virtual void hide(bool animated, double time) = 0;
};
// AppDelegate.SuspensionReason: willSleep, screensDidSleep and session resign
// are independent; presentation resumes only when every reason has cleared.
enum class PowerSuspension : std::uint8_t { system = 1, display = 2, session = 4 };
// AppDelegate battery rules: receive -> presentLatestSnapshot -> DisplayPolicy,
// manual preview ownership of its deadline, settings changes, sleep/wake.
class PowerAlertCoordinator final {
public:
    explicit PowerAlertCoordinator(ChargeAlertPresenter&, ChargeAlertSettings = {});
    // One deduplicated reading from the shared provider.
    void receive(const BatteryReading&, double time);
    void presentLatestSnapshot(double time);
    // Tray "Preview charging effect". Ignored while suspended, editing or HUD open.
    void preview(double time);
    // configurationChanged (alert-relevant fields only).
    void setSettings(const ChargeAlertSettings&, double time);
    // Sleep/display-off/session-switch: hide without animation, drop preview.
    void suspend(PowerSuspension, double time);
    // A duplicate or unmatched wake is ignored. When the last reason clears, a
    // reading taken while still suspended is applied exactly once against the
    // last awake snapshot (a plug change during sleep shows once). Returns true
    // when presentation resumed (the host also restarts its other services).
    bool resume(PowerSuspension, std::optional<BatteryReading> fresh, double time);
    void terminate(double time);
    // HUD toggles drop a manual preview; position editing uses previewValue.
    void systemOverlayToggled() noexcept { previewSnapshot_.reset(); }
    BatteryReading previewValue() const;
    const std::optional<BatteryReading>& snapshot() const noexcept { return snapshot_; }
    const std::optional<BatteryReading>& previewSnapshot() const noexcept { return previewSnapshot_; }
    const ChargeAlertSettings& settings() const noexcept { return settings_; }
    bool suspended() const noexcept { return suspensions_ != 0; }
    bool terminating() const noexcept { return terminating_; }
private:
    ChargeAlertPresenter* presenter_;
    ChargeAlertSettings settings_;
    std::optional<BatteryReading> snapshot_, presentedSnapshot_, previewSnapshot_;
    std::uint8_t suspensions_{};
    bool terminating_{};
    void apply(ChargeDisplayAction, double time);
};
} // namespace endfield::modules
