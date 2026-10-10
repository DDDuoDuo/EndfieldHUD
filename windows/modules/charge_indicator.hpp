#pragma once
#include "core/data/json.hpp"
#include "core/localization.hpp"
#include "core/scene.hpp"
#include "modules/battery_presentation.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// Portable port of Sources/ChargeIndicatorView.swift and HUDChargeMetric.swift.
// The Mac view is a finite Core Animation sequence. This model reproduces its
// model-layer geometry and every presentation track (from the presentation
// value at change time, Core Animation timing curves, cancellation and
// interruption semantics) as a pure function of an explicit clock. It owns no
// timer, window, renderer, provider or allocation on the sampling path.
namespace endfield::modules {
enum class ChargeStage : std::uint8_t { hidden, circle, supercharge, compact };
enum class ChargeMetric : std::uint8_t { battery, ram, cpu, network, disk };
enum class BatteryLevelTone : std::uint8_t { green, yellow, red };
using ChargeColor = std::array<double, 4>; // straight sRGB-encoded RGBA

std::string_view chargeStageKey(ChargeStage) noexcept;
std::string_view chargeMetricKey(ChargeMetric) noexcept; // saved preference identifiers
std::optional<ChargeMetric> chargeMetricFromKey(std::string_view) noexcept;
bool chargeMetricRequiresTelemetry(ChargeMetric) noexcept;
std::string chargeMetricTitle(ChargeMetric, core::Language);
// BatteryLevelTone.forPercentage: >50 green, >=20 yellow, otherwise red.
std::optional<BatteryLevelTone> batteryLevelTone(std::optional<unsigned> percentage) noexcept;
// BatterySnapshot.isChargeMode: external power on a battery machine, including
// charger negotiation, optimized-charging pauses and full charge.
bool batteryChargeMode(const BatteryReading&) noexcept;

// One existing activity sample, never read by this model. Bytes are exact
// physical counts; rates are bytes per second.
struct ChargeTelemetry {
    std::optional<double> cpuPercent;
    std::optional<std::uint64_t> memoryUsed, memoryTotal;
    std::optional<double> upload, download, diskRead, diskWrite;
    bool operator==(const ChargeTelemetry&) const = default;
};
struct ChargeMetricReading {
    std::string primary, secondary, unit, trailing;
    std::optional<double> progress; // a real bounded fraction only
    std::string accessibilityValue;
    bool operator==(const ChargeMetricReading&) const = default;
};
// HUDChargeMetricReading.resolve. Never substitutes battery percentage for
// another metric; unknown values stay "—"/Unavailable.
ChargeMetricReading resolveChargeMetric(ChargeMetric, const BatteryReading&,
    const std::optional<ChargeTelemetry>&, core::Language);

struct ChargeStageGeometry {
    core::Rect body;
    double radius{};
    core::Point emblemCenter;
    double emblemWidth{}, emblemHeight{}, emblemRadius{}, boltScale{};
};
// ChargeIndicatorView.apply(_:duration:) stage table (canvas points, top-left).
ChargeStageGeometry chargeStageGeometry(ChargeStage) noexcept;

struct ChargeIndicatorMetrics {
    static constexpr double canvasWidth = 300, canvasHeight = 84;
    static constexpr core::Point center{150, 42};
    static constexpr double entranceDuration = 1.50, exitDuration = 0.64;
    static constexpr double bannerTitleWidth = 121, morphDuration = 0.26, hoverDuration = 0.16;
    // animateEntrance: circle 0.18; +0.20 supercharge 0.30 with ripples;
    // +1.22 compact 0.28; completion +1.50. animateExit: circle 0.30; +0.30
    // hidden 0.34 whose fade completes at +0.64.
    static constexpr double entranceCircle = 0.18, superchargeAt = 0.20, superchargeDuration = 0.30;
    static constexpr double compactAt = 1.22, compactDuration = 0.28;
    static constexpr double exitCircle = 0.30, exitHiddenAt = 0.30, exitHidden = 0.34;
    static constexpr double rippleDuration = 0.76, rippleStagger = 0.15, rippleFrom = 0.06, rippleTo = 1.10;
    // Fixed text frames {x while visible, x otherwise, y, width, height}.
    // The banner pair is visible in .supercharge; the readings in .compact.
    static constexpr std::array<std::array<double, 5>, 4> textFrames{{
        {126, 150, 27, 116, 12}, {124, 149, 36, 121, 28}, {55, 83, 33, 143, 19}, {205, 192, 32.5, 40, 20}}};
};

// One sampled presentation tree. Body and its source shadow layer share size,
// radius, transform and opacity. Points are canvas coordinates.
struct ChargeIndicatorPose {
    double bodyWidth{38}, bodyHeight{38}, bodyRadius{19}, bodyScale{0.001}, bodyOpacity{};
    double emblemWidth{23}, emblemHeight{23}, emblemRadius{11.5};
    core::Point emblemCenter{150, 42};
    double emblemScale{0.001}, emblemOpacity{};
    core::Point boltPosition{150, 42};
    double boltScale{0.69 * 0.001}, boltOpacity{};
    // bannerEnglish, bannerTitle, capacityLabel, percentageLabel (anchor 0,0).
    std::array<core::Point, 4> textPosition{};
    std::array<double, 4> textOpacity{};
    core::Point ringPosition{239, 42};
    double ringOpacity{};
    // Ripple centers in the body's top-left local space; masked by the body.
    std::array<core::Point, 3> ripplePosition{};
    std::array<double, 3> rippleScale{1, 1, 1}, rippleOpacity{};
    double borderWidth{0.5};
    ChargeColor borderColor{};
    double canvasOpacity{1}; // includes the badge's additive deployment flicker
    // ChargeIndicatorView.embeddedBodyRect from presentation geometry.
    core::Rect bodyRect() const noexcept;
};

// Core Animation curves used by ChargeIndicatorView.change(): the motion curve
// (0.18,0.78,0.22,1), easeInEaseOut for geometry entering .hidden, and the
// hidden-opacity keyframe [prev,prev,value] at [0,0.68,1] (linear, easeInOut).
enum class ChargeCurve : std::uint8_t { motion, easeInOut, hiddenFade };
struct ChargeTrack {
    double from{}, to{}, start{}, duration{};
    ChargeCurve curve{ChargeCurve::motion};
    std::uint64_t order{}; // Core Animation addition order (later wins)
    double value(double time) const noexcept;
    bool active(double time) const noexcept { return duration > 0 && time < start + duration; }
};

// Finite sequence owner. All deadlines are explicit; the caller advances the
// same frame clock (or a one-shot host deadline) and samples. Methods never
// allocate. Times must be finite and nondecreasing per caller.
class ChargeIndicatorTimeline final {
public:
    enum class Event : std::uint8_t { none, entranceCompleted, exitCompleted };
    explicit ChargeIndicatorTimeline(ChargeStage initial = ChargeStage::hidden);
    ChargeStage stage() const noexcept { return stage_; }
    bool reduceMotion() const noexcept { return reduced_; }
    void setReduceMotion(bool) noexcept;
    // Model border color/width supplied by the owner's appearance (updateColors).
    void setBorderModel(double width, const ChargeColor&, double time) noexcept;
    // setStage: cancel, then apply 0.26 s when animated. As in the source, the
    // new tracks start from the transaction-start presentation, not the model.
    void setStage(ChargeStage, bool animated, double time) noexcept;
    // morphEmbeddedStage: cancel pending deadlines only; reversals start from
    // the rendered intermediate geometry.
    void morphEmbedded(ChargeStage, bool animated, double time) noexcept;
    void animateEntrance(double time) noexcept;
    void animateExit(double time) noexcept;
    // cancelAnimations: generation change, deadlines dropped, all tracks removed.
    void cancel() noexcept;
    // setEmbeddedHovered border feedback (0.16 s unless reduced/not animated).
    void animateBorder(double width, const ChargeColor&, bool animated, double time) noexcept;
    // Applies due sequence steps at their exact scheduled times. Returns the
    // first completion reached (at most one finite sequence is pending).
    Event advance(double time) noexcept;
    std::optional<double> nextDeadline() const noexcept;
    bool animating(double time) const noexcept;
    ChargeIndicatorPose sample(double time) const noexcept;
    std::uint64_t generation() const noexcept { return generation_; }
private:
    enum Property : std::size_t {
        bodyWidth, bodyHeight, bodyRadius, bodyScale, bodyOpacity,
        emblemWidth, emblemHeight, emblemRadius, emblemX, emblemY, emblemScale, emblemOpacity,
        boltX, boltY, boltScale, boltOpacity,
        text0X, text1X, text2X, text3X, text0Opacity, text1Opacity, text2Opacity, text3Opacity,
        ringX, ringOpacity,
        ripple0X, ripple1X, ripple2X, ripple0Y, ripple1Y, ripple2Y, ripple0Opacity, ripple1Opacity, ripple2Opacity,
        borderWidth, borderRed, borderGreen, borderBlue, borderAlpha,
        propertyCount
    };
    enum class Step : std::uint8_t { none, supercharge, compact, entranceDone, exitHidden, exitDone };
    std::array<ChargeTrack, propertyCount> tracks_{};
    // CALayer.presentation() reports the state at the start of the current
    // transaction: animations removed or replaced inside one source call
    // still supply their rendered values as the next animation's origin.
    std::array<double, propertyCount> transaction_{};
    std::array<double, 3> rippleBegin_{}, rippleAdded_{};
    std::array<std::uint64_t, 3> rippleOrder_{};
    bool ripplesStarted_{}, reduced_{};
    ChargeStage stage_{ChargeStage::hidden};
    std::uint64_t generation_{}, order_{};
    Step step_{Step::none};
    double stepAt_{}, sequenceStart_{};
    double value(Property, double time) const noexcept;
    void begin(double time) noexcept;
    void change(Property, double target, double duration, double time) noexcept;
    void apply(ChargeStage, double duration, double time) noexcept;
    void startRipples(double time) noexcept;
};

// Content: labels and fixed vector artwork in the source layer encoding the
// shared native rasterizer consumes. A measure callback returns the caller's
// actual DirectWrite width of one source text descriptor (content events only).
struct ChargeIndicatorAppearance {
    bool dark{true};
    ChargeColor accent{250. / 255, 212. / 255, 31. / 255, 1};
    core::Language language{core::Language::english};
    bool operator==(const ChargeIndicatorAppearance&) const = default;
};
struct ChargeIndicatorColors {
    ChargeColor foreground, background, level, emblem, bolt, ringTrack, ringProgress, laptop, ripple, border;
    double borderWidth{0.5};
};
// ChargeIndicatorView foreground/background/bodyBorder/levelColor/updateColors.
ChargeIndicatorColors chargeIndicatorColors(const ChargeIndicatorAppearance&, const BatteryReading&,
    ChargeMetric, std::optional<double> metricProgress, ChargeStage, bool embeddedHovered) noexcept;
// "CHARGE MODE"/"超充模式" for a plugged-in battery machine; otherwise POWER MODE.
std::string chargeModeTitle(const BatteryReading&, core::Language);
std::string chargeModeEnglish(const BatteryReading&);
std::string chargePowerState(const BatteryReading&, core::Language); // Charging/Power connected/On battery
std::string chargeIndicatorAccessibility(const ChargeMetricReading&, const BatteryReading&, bool preview, core::Language);
// Exact BatteryCapacity-unit preservation: a percent scale is never labelled mAh/mWh.

using ChargeTextMeasure = std::function<double(std::string_view sourceID, const ehud::data::Json& textDescriptor)>;
struct ChargeIndicatorContent {
    ChargeMetricReading reading;
    std::string accessibility;
    double titleFontSize{20}, lineScale{1}, trailingFontSize{13};
    // Text layer bodies (bannerEnglish, bannerTitle, capacityLabel, percentageLabel),
    // in local layer coordinates at the origin, ready for LayerScene.
    std::array<ehud::data::Json, 4> texts;
    bool operator==(const ChargeIndicatorContent&) const = default;
};
// Font fitting runs the source loops with the caller's measurement: title
// 20pt -> >=10pt in 0.5pt steps while wider than 119; metric line scale 1 -> 0.6
// in 0.05 steps while wider than 143; trailing 13 -> >=9 while wider than 40.
ChargeIndicatorContent prepareChargeIndicatorContent(const ChargeIndicatorAppearance&, const BatteryReading&,
    ChargeMetric, const std::optional<ChargeTelemetry>&, bool preview, const ChargeTextMeasure&);
ehud::data::Json chargeTextDescriptor(const ehud::data::Json& textLayer); // layer["text"] convenience

// Static vector leaves (white, tinted by the GPU): bolt, laptop, ring track,
// full progress ring (angularly clipped), round cap, ripple ring.
ehud::data::Json chargeBoltLayer();
ehud::data::Json chargeLaptopLayer();
ehud::data::Json chargeRingLayer(bool progress);
ehud::data::Json chargeRippleLayer();
ehud::data::Json chargeCapLayer();

// OverlayController.PositionActionButton (28 x 28, flipped): a circle inset by
// one point and a 1.7-point round-capped symbol, as white leaves the GPU tints.
ehud::data::Json chargePositionButtonCircleLayer();
ehud::data::Json chargePositionButtonSymbolLayer(bool confirm);
struct ChargePositionButtonColors { ChargeColor fill, symbol; };
ChargePositionButtonColors chargePositionButtonColors(bool dark, bool highlighted) noexcept;
std::string chargePositionButtonLabel(bool confirm, core::Language);   // accessibility label
std::string chargePositionButtonTooltip(bool confirm, core::Language); // "Discard (Esc)" / "Confirm (Return)"
} // namespace endfield::modules
