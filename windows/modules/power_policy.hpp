#pragma once
#include "core/data/json.hpp"
#include "core/localization.hpp"
#include "core/scene.hpp"
#include "modules/battery_reading.hpp"
#include "modules/charge_indicator.hpp"
#include "modules/event_log.hpp"
#include "modules/hud_design_tokens.hpp"
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

// Saved "displayMode" values: the Mac's DisplayMode raw values
// ("whenChargingStarts" = Power changes, "always") and the Windows-only
// "never" (Don't display). Older builds and the Mac read "never" as an
// unknown value, i.e. Power changes; an unknown value read here is too.
inline constexpr std::string_view chargeDisplayModePowerChanges = "whenChargingStarts";
inline constexpr std::string_view chargeDisplayModeAlways = "always";
inline constexpr std::string_view chargeDisplayModeNever = "never";

enum class ChargeTheme : std::uint8_t { dark, light, system };
// AppConfiguration fields consumed by the alert, normalized exactly like
// AppConfiguration.normalized (duration 1...60 else 3, scale 0.65...1.6 else 1,
// custom position 0...1 else 0.5/0.9, six uppercase hex digits else FAD41F,
// length finite and above 0 to min(1, length), anything else none).
// length (saved "chargeBarLength", absent by default): the compact bar's
// length as a fraction of its display's usable width (1 = edge to edge; the
// 200-point minimum applies in chargeBarWidth). None keeps the classic
// 264-point bar, so saved 1.3.0 settings look the same.
// neverMode (Windows "Don't display"): no automatic presentation at all, on
// a power change, in always mode, after the HUD or Projection closes or on
// wake; the explicit tray preview and Edit position still present. It never
// combines with alwaysMode (normalized() keeps alwaysMode).
struct ChargeAlertSettings {
    bool alwaysMode{}, neverMode{};
    double displayDuration{3};
    std::string accentHex{"FAD41F"};
    ChargeTheme theme{ChargeTheme::dark};
    double scale{1};
    std::optional<double> length;
    bool customPlacement{};
    std::optional<std::uint32_t> customScreenID;
    double customX{0.5}, customY{0.9};
    ChargeMetric metric{ChargeMetric::battery};
    bool alertsEnabled{true}, devicePopupEnabled{true}, reduceMotion{}, lowPowerVisualMode{};
    ChargeAlertSettings normalized() const;
    ChargeColor accent() const; // of the normalized hex
    // Tolerant reader of the persisted settings object (data_store fields):
    // unknown/invalid values fall back like the Mac decoder (an unknown
    // displayMode is Power changes, an unknown alertMetric is battery).
    static ChargeAlertSettings fromSettings(const ehud::data::Json& fields);
    bool operator==(const ChargeAlertSettings&) const = default;
};

// OverlayGeometry in Windows virtual-desktop pixels with y increasing
// downwards. Mac point constants scale by the monitor's pixels per point.
// Saved positions keep the Mac convention: x from the left, y from the BOTTOM
// of the usable desktop, so custom positions saved on either system land in
// the same place. Widths of the bar and its canvas are in the bar's own
// points (before its Size), like ChargeIndicatorMetrics.
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
// OverlayGeometry.factor: the Size every geometry helper uses (0.65...1.6,
// non-finite 1; the bar's Size range is ChargeIndicatorMetrics' scale range).
double chargeScaleFactor(double scale) noexcept;
// OverlayGeometry.barWidth: `length` of the usable width at this Size, never
// under the 200-point minimum nor past the screen edges; none (or not finite,
// or not above 0) keeps the classic 264-point bar.
double chargeBarWidth(std::optional<double> length, const core::Rect& workArea, double scale, double pixelsPerPoint) noexcept;
// OverlayGeometry.panelFrame: the panel cut at the display's sides, so a
// full-length bar's shadow margins never hang onto (or take clicks from) the
// next display. Shorter bars are already kept inside by the anchor clamp.
core::Rect chargePanelFrame(const core::Rect& bar, const core::Rect& workArea) noexcept;
// OverlayGeometry.lengthRange: the Length slider's fractions on this display
// at this Size, from the 200-point minimum up to edge to edge.
struct ChargeLengthRange { double minimum{1}, maximum{1}; bool operator==(const ChargeLengthRange&) const = default; };
ChargeLengthRange chargeLengthRange(const core::Rect& workArea, double scale, double pixelsPerPoint) noexcept;
// OverlayGeometry.length(of:): the fraction the Length slider shows for a bar
// (the classic bar too).
double chargeLength(const ChargeAlertSettings&, const core::Rect& workArea, double pixelsPerPoint) noexcept;
// OverlayGeometry.clampedAnchor. canvasWidth is the bar's canvas in its own
// points (300 for the classic bar). `editing` keeps the 1.3.0 two-button
// editor's room below the bar; the bar editor's strip flips above the bar
// instead and passes false.
core::Point chargeClampedAnchor(core::Point, const core::Rect& workArea, double scale, bool editing, double pixelsPerPoint,
                                double canvasWidth = ChargeIndicatorMetrics::canvasWidth) noexcept;
// The clamp for the bar `settings` draws (its Size and Length).
core::Point chargeClampedAnchor(core::Point, const ChargeAlertSettings&, const core::Rect& workArea, bool editing,
                                double pixelsPerPoint) noexcept;
core::Point chargeAnchor(const ChargeAlertSettings&, const core::Rect& workArea, bool editing, double pixelsPerPoint) noexcept;
ChargePosition chargeNormalizedPosition(core::Point anchor, const ChargeWorkArea&) noexcept;
// The bar editor (OverlayGeometry.editorStripScale, editorStripSize,
// editorReach, editorAnchor, draftLength).
// The editor strip beside the bar is drawn at 70 % of the shared strip
// (design::PlacementEditor), text and controls alike.
inline constexpr double chargeEditorStripScale = 0.7;
// The room the scaled strip of `size` (strip points) takes, with the value
// bubble's clearance (design::PlacementEditor::bubbleClearance) above it, in
// pixels.
design::Size chargeEditorStripSize(design::Size stripPoints, double pixelsPerPoint) noexcept;
// The heights (y down, pixels) the visible bar (38 points tall before Size)
// can cover while the editor's Size slider moves from `anchor`, up to the
// largest Size, including the shift when that Size's margins clamp the anchor
// by an edge. The editor strip keeps clear of it.
struct ChargeSpan { double minimum{}, maximum{}; bool operator==(const ChargeSpan&) const = default; };
ChargeSpan chargeEditorReach(core::Point anchor, const core::Rect& workArea, double pixelsPerPoint) noexcept;
// The bar editor's preview at the drafts in `settings`: a dragged bar where
// it was dropped, otherwise where Confirm keeps it (Top centre or the saved
// custom point, placed at the drafts' Size and Length).
core::Point chargeEditorAnchor(core::Point draft, bool moved, const ChargeAlertSettings&, const core::Rect& workArea,
                               double pixelsPerPoint) noexcept;
// The Length draft a Length slider report (in percent) sets, or none when it
// reports the value the slider already showed (a press or release without
// movement), so a classic (none) bar stays classic.
std::optional<double> chargeDraftLength(double reported, std::optional<double> shown) noexcept;
// OverlayController.layout: the panel holds the bar's canvas centred on the
// anchor (indicator, canvasWidth(barWidth) x 84 at the Size), cut at the
// display's sides; while editing it also holds the strip (strip), and the
// window is their union. Rectangles are virtual-desktop pixels.
struct ChargePanelLayout {
    core::Rect window, indicator;
    std::optional<core::Rect> strip;
    double barWidth{ChargeIndicatorMetrics::classicBarWidth};   // the bar's own points
    bool operator==(const ChargePanelLayout&) const = default;
};
ChargePanelLayout chargePanelLayout(core::Point anchor, double scale, double barWidth, const core::Rect& workArea, double pixelsPerPoint,
                                    std::optional<core::Rect> strip = {}) noexcept;
// Where the editor's strip goes beside the bar at `anchor` (pixels): spaced
// for the largest Size (chargeEditorReach), so a Size change never moves it
// vertically, 10 points below the bar or above it near the bottom edge
// (design::PlacementEditor::frame); `stripSize` from chargeEditorStripSize.
core::Rect chargeEditorStripFrame(core::Point anchor, double scale, double barWidth, design::Size stripSize, const core::Rect& workArea,
                                  double pixelsPerPoint) noexcept;
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
// Every automatic presentation goes through apply(), which hides instead
// while alerts are disabled or the display method is Don't display.
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
