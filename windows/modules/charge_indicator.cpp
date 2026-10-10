#include "modules/charge_indicator.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace endfield::modules {
namespace {
using Json = ehud::data::Json;
std::string tr(const char* english, const char* chinese, core::Language language) { return core::localized(english, chinese, language); }
constexpr core::CubicTiming motionCurve{0.18, 0.78, 0.22, 1};
constexpr core::CubicTiming easeInOutCurve{0.42, 0, 0.58, 1};
constexpr core::CubicTiming easeOutCurve{0, 0, 0.58, 1};

// HUDChargeMetricReading.decimal: String(format:locale:) with en_US_POSIX,
// "%.1f", or "%.1e" from 10,000. Foundation's locale formatter emits an
// uppercase exponent ("1.2E+04"), as the source oracle records.
std::string decimal(double value, bool keepingFraction = false) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof buffer, value >= 10'000 ? "%.1E" : "%.1f", value);
    std::string result(buffer);
    if (!keepingFraction && result.size() >= 2 && result.ends_with(".0")) result.resize(result.size() - 2);
    return result;
}
std::string grouped(unsigned value) {
    auto digits = std::to_string(value);
    for (auto index = static_cast<std::ptrdiff_t>(digits.size()) - 3; index > 0; index -= 3)
        digits.insert(static_cast<std::size_t>(index), ",");
    return digits;
}
ChargeMetricReading rates(std::optional<double> first, std::optional<double> second, const char* trailing,
                          const std::string& firstName, const std::string& secondName, core::Language language) {
    if (first && !(std::isfinite(*first) && *first >= 0)) first.reset();
    if (second && !(std::isfinite(*second) && *second >= 0)) second.reset();
    const double largest = std::max(first.value_or(0), second.value_or(0));
    static constexpr const char* units[]{"B/s", "KB/s", "MB/s", "GB/s", "TB/s"};
    std::size_t index = 0;
    double divisor = 1;
    while (index < 4 && largest / divisor >= 1000) { ++index; divisor *= 1000; }
    const auto a = first ? decimal(*first / divisor, true) : std::string("—");
    const auto b = second ? decimal(*second / divisor, true) : std::string("—");
    const std::string unit = units[index], unavailable = tr("Unavailable", "不可用", language);
    return {a, "/" + b, unit, trailing, std::nullopt,
            firstName + (first ? a + " " + unit : unavailable) + ", " + secondName + (second ? b + " " + unit : unavailable)};
}
ChargeColor white(double value, double alpha = 1) { return {value, value, value, alpha}; }
ChargeColor alpha(ChargeColor color, double value) { color[3] = value; return color; }
Json color(const ChargeColor& c) { return Json::Object{{"sRGB", Json::Array{c[0], c[1], c[2], c[3]}}}; }
Json command(const char* op, std::initializer_list<core::Point> points = {}) {
    Json::Array values;
    for (const auto p : points) values.push_back(Json::Array{p.x, p.y});
    return Json::Object{{"op", op}, {"points", std::move(values)}};
}
Json layer(std::string id, const char* kind, const char* className, core::Rect bounds, core::Point position,
           core::Point anchor = {0.5, 0.5}) {
    return Json::Object{{"id", std::move(id)}, {"kind", kind}, {"class", className},
                        {"bounds", Json::Array{bounds.x, bounds.y, bounds.width, bounds.height}},
                        {"position", Json::Array{position.x, position.y}}, {"anchorPoint", Json::Array{anchor.x, anchor.y}},
                        {"opacity", 1}, {"children", Json::Array{}}};
}
Json shape(std::string id, core::Rect bounds, Json::Array path, std::optional<ChargeColor> fill,
           std::optional<ChargeColor> stroke, double lineWidth, const char* cap = "butt", const char* join = "miter") {
    auto node = layer(std::move(id), "shape", "CAShapeLayer", bounds, {bounds.width / 2, bounds.height / 2});
    node["shape"] = Json::Object{{"path", std::move(path)}, {"fillColor", fill ? color(*fill) : Json{}},
                                 {"strokeColor", stroke ? color(*stroke) : Json{}}, {"lineWidth", lineWidth},
                                 {"lineCap", cap}, {"lineJoin", join}, {"miterLimit", 10}, {"fillRule", "non-zero"},
                                 {"strokeStart", 0}, {"strokeEnd", 1}};
    return node;
}
// CGPath(ellipseIn:) as Core Graphics emits it: start at maxX/midY.
Json::Array ellipse(core::Rect r) {
    constexpr double k = 0.5522847498;
    const double cx = r.x + r.width / 2, cy = r.y + r.height / 2, rx = r.width / 2, ry = r.height / 2;
    return {command("move", {{r.x + r.width, cy}}),
            command("cubic", {{r.x + r.width, cy + ry * k}, {cx + rx * k, r.y + r.height}, {cx, r.y + r.height}}),
            command("cubic", {{cx - rx * k, r.y + r.height}, {r.x, cy + ry * k}, {r.x, cy}}),
            command("cubic", {{r.x, cy - ry * k}, {cx - rx * k, r.y}, {cx, r.y}}),
            command("cubic", {{cx + rx * k, r.y}, {r.x + r.width, cy - ry * k}, {r.x + r.width, cy}}), command("close")};
}
Json font(const char* postScript, double size, unsigned traits) {
    return Json::Object{{"postScriptName", postScript}, {"familyName", ".AppleSystemUIFont"}, {"pointSize", size},
                        {"symbolicTraits", static_cast<double>(traits)}};
}
struct Run { std::string text; Json font; ChargeColor color; std::optional<double> kern; };
// Source attributed CATextLayer: default Helvetica 36 layer font, white layer
// foreground, explicit runs carrying the real fonts/colors.
Json textLayer(std::string id, double width, double height, const std::vector<Run>& runs, const char* alignment) {
    auto node = layer(std::move(id), "text", "CATextLayer", {0, 0, width, height}, {0, 0}, {0, 0});
    std::string string;
    Json::Array attributed;
    std::size_t offset = 0;
    for (const auto& run : runs) {
        std::size_t units = 0;
        for (std::size_t i = 0; i < run.text.size();) {
            const auto byte = static_cast<unsigned char>(run.text[i]);
            const std::size_t length = byte < 0x80 ? 1 : byte < 0xE0 ? 2 : byte < 0xF0 ? 3 : 4;
            units += length == 4 ? 2 : 1;
            i += length;
        }
        Json::Object attributes{{"NSFont", run.font}, {"NSColor", color(run.color)}};
        if (run.kern) attributes.emplace("NSKern", *run.kern);
        if (units) attributed.push_back(Json::Object{{"utf16Range", Json::Array{static_cast<double>(offset), static_cast<double>(units)}},
                                                     {"attributes", std::move(attributes)}});
        offset += units;
        string += run.text;
    }
    node["text"] = Json::Object{{"string", string}, {"fontSize", 36},
                                // Every character carries an explicit run font; the
                                // layer default keeps only its family and size (the
                                // source's UI-optimized trait bit is not a style).
                                {"font", Json::Object{{"postScriptName", "Helvetica"}, {"familyName", "Helvetica"},
                                                      {"pointSize", 36}, {"symbolicTraits", 0}}},
                                {"foregroundColor", color({1, 1, 1, 1})}, {"alignment", alignment}, {"wrapped", false},
                                {"truncation", "none"}, {"runs", std::move(attributed)}};
    return node;
}
bool isOpacity(std::size_t property) noexcept;
} // namespace

std::string_view chargeStageKey(ChargeStage stage) noexcept {
    switch (stage) {
    case ChargeStage::hidden: return "hidden";
    case ChargeStage::circle: return "circle";
    case ChargeStage::supercharge: return "supercharge";
    case ChargeStage::compact: return "compact";
    }
    return "hidden";
}
std::string_view chargeMetricKey(ChargeMetric metric) noexcept {
    switch (metric) {
    case ChargeMetric::battery: return "battery";
    case ChargeMetric::ram: return "ram";
    case ChargeMetric::cpu: return "cpu";
    case ChargeMetric::network: return "network";
    case ChargeMetric::disk: return "disk";
    }
    return "battery";
}
std::optional<ChargeMetric> chargeMetricFromKey(std::string_view key) noexcept {
    for (auto metric : {ChargeMetric::battery, ChargeMetric::ram, ChargeMetric::cpu, ChargeMetric::network, ChargeMetric::disk})
        if (chargeMetricKey(metric) == key) return metric;
    return std::nullopt;
}
bool chargeMetricRequiresTelemetry(ChargeMetric metric) noexcept { return metric != ChargeMetric::battery; }
std::string chargeMetricTitle(ChargeMetric metric, core::Language language) {
    switch (metric) {
    case ChargeMetric::battery: return tr("Battery", "电池", language);
    case ChargeMetric::ram: return tr("RAM", "RAM", language);
    case ChargeMetric::cpu: return "CPU";
    case ChargeMetric::network: return tr("Network", "网络", language);
    case ChargeMetric::disk: return tr("Disk", "磁盘", language);
    }
    return {};
}
std::optional<BatteryLevelTone> batteryLevelTone(std::optional<unsigned> percentage) noexcept {
    if (!percentage || *percentage > 100) return std::nullopt;
    if (*percentage > 50) return BatteryLevelTone::green;
    if (*percentage >= 20) return BatteryLevelTone::yellow;
    return BatteryLevelTone::red;
}
bool batteryChargeMode(const BatteryReading& reading) noexcept { return reading.present && reading.pluggedIn; }

ChargeMetricReading resolveChargeMetric(ChargeMetric metric, const BatteryReading& battery,
                                        const std::optional<ChargeTelemetry>& telemetry, core::Language language) {
    const auto unavailable = tr("Unavailable", "不可用", language);
    switch (metric) {
    case ChargeMetric::battery: {
        const auto& capacity = battery.present ? battery.capacity : std::nullopt;
        const auto current = capacity ? grouped(capacity->current) : std::string("—");
        const auto maximum = capacity ? grouped(capacity->maximum) : std::string("—");
        std::optional<double> percent;
        if (battery.present && battery.percentage && *battery.percentage <= 100) percent = *battery.percentage;
        const auto value = percent ? decimal(*percent) + "%" : std::string("—");
        return {current, "/" + maximum, capacity ? capacity->unit : std::string(), value,
                percent ? std::optional<double>(*percent / 100) : std::nullopt,
                chargeMetricTitle(metric, language) + ": " + (percent ? value : unavailable)};
    }
    case ChargeMetric::ram: {
        std::optional<std::uint64_t> total, used;
        if (telemetry && telemetry->memoryTotal && *telemetry->memoryTotal > 0) total = telemetry->memoryTotal;
        if (telemetry && telemetry->memoryUsed && total && *telemetry->memoryUsed <= *total) used = telemetry->memoryUsed;
        constexpr double gib = 1'073'741'824.0;
        const auto current = used ? decimal(static_cast<double>(*used) / gib) : std::string("—");
        const auto maximum = total ? decimal(static_cast<double>(*total) / gib) : std::string("—");
        std::optional<double> fraction;
        if (used && total) fraction = static_cast<double>(*used) / static_cast<double>(*total);
        const auto percentage = fraction ? decimal(*fraction * 100) + "%" : std::string("—");
        return {current, "/" + maximum, "GB", percentage, fraction,
                chargeMetricTitle(metric, language) + ": " + current + "/" + maximum + " GB, " + (fraction ? percentage : unavailable)};
    }
    case ChargeMetric::cpu: {
        std::optional<double> percent;
        if (telemetry && telemetry->cpuPercent && std::isfinite(*telemetry->cpuPercent) && *telemetry->cpuPercent >= 0 &&
            *telemetry->cpuPercent <= 100) percent = telemetry->cpuPercent;
        const auto value = percent ? decimal(*percent) + "%" : std::string("—");
        return {"CPU", "", "", value, percent ? std::optional<double>(*percent / 100) : std::nullopt,
                "CPU: " + (percent ? value : unavailable)};
    }
    case ChargeMetric::network:
        return rates(telemetry ? telemetry->upload : std::nullopt, telemetry ? telemetry->download : std::nullopt, "↑/↓",
                     tr("Upload ", "上传 ", language), tr("Download ", "下载 ", language), language);
    case ChargeMetric::disk:
        return rates(telemetry ? telemetry->diskRead : std::nullopt, telemetry ? telemetry->diskWrite : std::nullopt, "R/W",
                     tr("Read ", "读取 ", language), tr("Write ", "写入 ", language), language);
    }
    return {};
}

ChargeStageGeometry chargeStageGeometry(ChargeStage stage) noexcept {
    switch (stage) {
    case ChargeStage::hidden:
    case ChargeStage::circle: return {{131, 23, 38, 38}, 19, {150, 42}, 23, 23, 11.5, 0.69};
    case ChargeStage::supercharge: return {{30, 18, 240, 48}, 14, {107, 42}, 23, 23, 11.5, 0.70};
    case ChargeStage::compact: return {{18, 23, 264, 38}, 19, {37, 42}, 17, 20, 3, 0.60};
    }
    return {};
}

core::Rect ChargeIndicatorPose::bodyRect() const noexcept {
    const double width = bodyWidth * std::abs(bodyScale), height = bodyHeight * std::abs(bodyScale);
    return {ChargeIndicatorMetrics::center.x - width / 2, ChargeIndicatorMetrics::center.y - height / 2, width, height};
}

double ChargeTrack::value(double time) const noexcept {
    if (duration <= 0 || !(time < start + duration)) return to;
    const double u = std::clamp((time - start) / duration, 0.0, 1.0);
    switch (curve) {
    case ChargeCurve::motion: return from + (to - from) * motionCurve.value(u);
    case ChargeCurve::easeInOut: return from + (to - from) * easeInOutCurve.value(u);
    case ChargeCurve::hiddenFade:
        // Let the circle visibly shrink before fading its final few pixels.
        return u < 0.68 ? from : from + (to - from) * easeInOutCurve.value((u - 0.68) / 0.32);
    }
    return to;
}

namespace {
bool isOpacity(std::size_t property) noexcept {
    switch (property) {
    case 4: case 11: case 15: case 20: case 21: case 22: case 23: case 25: case 32: case 33: case 34: return true;
    default: return false;
    }
}
double rippleGroupOpacity(double eased) noexcept {
    static constexpr double times[]{0, 0.12, 0.52, 1}, values[]{0, 0.20, 0.14, 0};
    for (int i = 0; i < 3; ++i)
        if (eased <= times[i + 1]) return values[i] + (values[i + 1] - values[i]) * (eased - times[i]) / (times[i + 1] - times[i]);
    return 0;
}
} // namespace

ChargeIndicatorTimeline::ChargeIndicatorTimeline(ChargeStage initial) {
    static_assert(propertyCount == 40);
    static_assert(bodyOpacity == 4 && emblemOpacity == 11 && boltOpacity == 15 && text0Opacity == 20 && ringOpacity == 25 &&
                  ripple0Opacity == 32 && ripple2Opacity == 34);
    // Constructor state equals ChargeIndicatorView.init: model values, then
    // apply(.hidden, 0); a caller may request a different settled stage.
    for (auto& track : tracks_) track = {};
    tracks_[borderWidth].from = tracks_[borderWidth].to = 0.5;
    apply(ChargeStage::hidden, 0, 0);
    if (initial != ChargeStage::hidden) apply(initial, 0, 0);
}
void ChargeIndicatorTimeline::setReduceMotion(bool value) noexcept { reduced_ = value; }
double ChargeIndicatorTimeline::value(Property property, double time) const noexcept {
    const auto& track = tracks_[property];
    if (property >= ripple0Opacity && property <= ripple2Opacity) {
        const auto index = static_cast<std::size_t>(property - ripple0Opacity);
        const bool groupActive = ripplesStarted_ && time < rippleBegin_[index] + ChargeIndicatorMetrics::rippleDuration;
        const bool trackActive = track.active(time);
        if (groupActive && (!trackActive || rippleOrder_[index] > track.order)) {
            const double local = std::clamp((time - rippleBegin_[index]) / ChargeIndicatorMetrics::rippleDuration, 0.0, 1.0);
            return rippleGroupOpacity(easeOutCurve.value(local));
        }
    }
    return track.value(time);
}
void ChargeIndicatorTimeline::begin(double time) noexcept {
    for (std::size_t property = 0; property < propertyCount; ++property) transaction_[property] = value(static_cast<Property>(property), time);
}
void ChargeIndicatorTimeline::change(Property property, double target, double duration, double time) noexcept {
    const double previous = transaction_[property];
    auto& track = tracks_[property];
    if (!(duration > 0)) { track = {target, target, time, 0, ChargeCurve::motion, track.order}; return; }
    const auto curve = stage_ == ChargeStage::hidden ? (isOpacity(property) ? ChargeCurve::hiddenFade : ChargeCurve::easeInOut)
                                                     : ChargeCurve::motion;
    track = {previous, target, time, duration, curve, ++order_};
}
void ChargeIndicatorTimeline::apply(ChargeStage next, double duration, double time) noexcept {
    stage_ = next;
    const auto g = chargeStageGeometry(next);
    const bool hidden = next == ChargeStage::hidden, banner = next == ChargeStage::supercharge, compact = next == ChargeStage::compact;
    const double scale = hidden ? 0.001 : 1, opacity = hidden ? 0 : 1;
    change(bodyWidth, g.body.width, duration, time);
    change(bodyHeight, g.body.height, duration, time);
    change(bodyRadius, g.radius, duration, time);
    change(bodyScale, scale, duration, time);
    change(bodyOpacity, opacity, duration, time);
    change(emblemWidth, g.emblemWidth, duration, time);
    change(emblemHeight, g.emblemHeight, duration, time);
    change(emblemX, g.emblemCenter.x, duration, time);
    change(emblemY, g.emblemCenter.y, duration, time);
    change(emblemRadius, g.emblemRadius, duration, time);
    change(emblemScale, scale, duration, time);
    change(emblemOpacity, opacity, duration, time);
    change(boltX, g.emblemCenter.x, duration, time);
    change(boltY, g.emblemCenter.y, duration, time);
    change(boltScale, g.boltScale * scale, duration, time);
    change(boltOpacity, opacity, duration, time);
    // The text translates with the morph; it is never squashed with the body.
    for (std::size_t i = 0; i < 4; ++i) {
        const bool visible = i < 2 ? banner : compact;
        const auto& frame = ChargeIndicatorMetrics::textFrames[i];
        change(static_cast<Property>(text0X + i), visible ? frame[0] : frame[1], duration, time);
        change(static_cast<Property>(text0Opacity + i), visible ? 1 : 0, std::min(duration, 0.20), time);
    }
    change(ringX, compact ? 263 : 239, duration, time);
    change(ringOpacity, compact ? 1 : 0, duration, time);
    for (std::size_t i = 0; i < 3; ++i) {
        change(static_cast<Property>(ripple0X + i), g.emblemCenter.x - g.body.x, duration, time);
        change(static_cast<Property>(ripple0Y + i), g.emblemCenter.y - g.body.y, duration, time);
        if (!banner) change(static_cast<Property>(ripple0Opacity + i), 0, std::min(duration, 0.12), time);
    }
}
void ChargeIndicatorTimeline::startRipples(double time) noexcept {
    for (std::size_t i = 0; i < 3; ++i) {
        // Model opacity 0 / scale 1.10; the group fills backwards until its turn.
        rippleAdded_[i] = time;
        rippleBegin_[i] = time + static_cast<double>(i) * ChargeIndicatorMetrics::rippleStagger;
        rippleOrder_[i] = ++order_;
    }
    ripplesStarted_ = true;
}
void ChargeIndicatorTimeline::cancel() noexcept {
    ++generation_;
    step_ = Step::none;
    for (auto& track : tracks_) { track.from = track.to; track.duration = 0; }
    ripplesStarted_ = false;
}
void ChargeIndicatorTimeline::setBorderModel(double width, const ChargeColor& color, double time) noexcept {
    const double values[]{width, color[0], color[1], color[2], color[3]};
    for (std::size_t i = 0; i < 5; ++i) {
        auto& track = tracks_[borderWidth + i];
        if (track.active(time)) continue; // an explicit animation keeps its own destination
        track = {values[i], values[i], time, 0, ChargeCurve::motion, track.order};
    }
}
void ChargeIndicatorTimeline::animateBorder(double width, const ChargeColor& color, bool animated, double time) noexcept {
    begin(time);
    const double duration = animated && !reduced_ ? ChargeIndicatorMetrics::hoverDuration : 0;
    const double values[]{width, color[0], color[1], color[2], color[3]};
    for (std::size_t i = 0; i < 5; ++i) change(static_cast<Property>(borderWidth + i), values[i], duration, time);
}
void ChargeIndicatorTimeline::setStage(ChargeStage stage, bool animated, double time) noexcept {
    begin(time);
    cancel();
    apply(stage, animated && !reduced_ ? ChargeIndicatorMetrics::morphDuration : 0, time);
}
void ChargeIndicatorTimeline::morphEmbedded(ChargeStage stage, bool animated, double time) noexcept {
    begin(time);
    ++generation_;
    step_ = Step::none;
    apply(stage, animated && !reduced_ ? ChargeIndicatorMetrics::morphDuration : 0, time);
}
void ChargeIndicatorTimeline::animateEntrance(double time) noexcept {
    begin(time);
    cancel();
    sequenceStart_ = time;
    if (reduced_) { apply(ChargeStage::compact, 0, time); step_ = Step::entranceDone; stepAt_ = time; return; }
    apply(ChargeStage::hidden, 0, time);
    apply(ChargeStage::circle, ChargeIndicatorMetrics::entranceCircle, time);
    step_ = Step::supercharge;
    stepAt_ = time + ChargeIndicatorMetrics::superchargeAt;
}
void ChargeIndicatorTimeline::animateExit(double time) noexcept {
    begin(time);
    cancel();
    sequenceStart_ = time;
    if (reduced_) { apply(ChargeStage::hidden, 0, time); step_ = Step::exitDone; stepAt_ = time; return; }
    apply(ChargeStage::circle, ChargeIndicatorMetrics::exitCircle, time);
    step_ = Step::exitHidden;
    stepAt_ = time + ChargeIndicatorMetrics::exitHiddenAt;
}
ChargeIndicatorTimeline::Event ChargeIndicatorTimeline::advance(double time) noexcept {
    // A nanosecond allowance absorbs decimal deadline composition
    // (0.55 + 0.30 > 0.85 in binary) without reordering any source step.
    while (step_ != Step::none && stepAt_ <= time + 1e-9) {
        const double at = stepAt_;
        begin(at); // each scheduled source block commits its own transaction
        switch (step_) {
        case Step::supercharge:
            apply(ChargeStage::supercharge, ChargeIndicatorMetrics::superchargeDuration, at);
            startRipples(at);
            step_ = Step::compact; stepAt_ = sequenceStart_ + ChargeIndicatorMetrics::compactAt;
            break;
        case Step::compact:
            apply(ChargeStage::compact, ChargeIndicatorMetrics::compactDuration, at);
            step_ = Step::entranceDone; stepAt_ = sequenceStart_ + ChargeIndicatorMetrics::entranceDuration;
            break;
        case Step::entranceDone: step_ = Step::none; return Event::entranceCompleted;
        case Step::exitHidden:
            apply(ChargeStage::hidden, ChargeIndicatorMetrics::exitHidden, at);
            step_ = Step::exitDone; stepAt_ = at + ChargeIndicatorMetrics::exitHidden;
            break;
        case Step::exitDone: step_ = Step::none; return Event::exitCompleted;
        case Step::none: break;
        }
    }
    return Event::none;
}
std::optional<double> ChargeIndicatorTimeline::nextDeadline() const noexcept {
    return step_ == Step::none ? std::nullopt : std::optional<double>(stepAt_);
}
bool ChargeIndicatorTimeline::animating(double time) const noexcept {
    if (std::any_of(tracks_.begin(), tracks_.end(), [&](const auto& track) { return track.active(time) && track.from != track.to; }))
        return true;
    if (ripplesStarted_)
        for (std::size_t i = 0; i < 3; ++i)
            if (time < rippleBegin_[i] + ChargeIndicatorMetrics::rippleDuration) return true;
    return false;
}
ChargeIndicatorPose ChargeIndicatorTimeline::sample(double time) const noexcept {
    ChargeIndicatorPose pose;
    pose.bodyWidth = value(bodyWidth, time); pose.bodyHeight = value(bodyHeight, time);
    pose.bodyRadius = value(bodyRadius, time); pose.bodyScale = value(bodyScale, time); pose.bodyOpacity = value(bodyOpacity, time);
    pose.emblemWidth = value(emblemWidth, time); pose.emblemHeight = value(emblemHeight, time);
    pose.emblemRadius = value(emblemRadius, time); pose.emblemCenter = {value(emblemX, time), value(emblemY, time)};
    pose.emblemScale = value(emblemScale, time); pose.emblemOpacity = value(emblemOpacity, time);
    pose.boltPosition = {value(boltX, time), value(boltY, time)};
    pose.boltScale = value(boltScale, time); pose.boltOpacity = value(boltOpacity, time);
    for (std::size_t i = 0; i < 4; ++i) {
        pose.textPosition[i] = {value(static_cast<Property>(text0X + i), time), ChargeIndicatorMetrics::textFrames[i][2]};
        pose.textOpacity[i] = value(static_cast<Property>(text0Opacity + i), time);
    }
    pose.ringPosition = {value(ringX, time), 42};
    pose.ringOpacity = value(ringOpacity, time);
    for (std::size_t i = 0; i < 3; ++i) {
        pose.ripplePosition[i] = {value(static_cast<Property>(ripple0X + i), time), value(static_cast<Property>(ripple0Y + i), time)};
        pose.rippleOpacity[i] = value(static_cast<Property>(ripple0Opacity + i), time);
        const bool group = ripplesStarted_ && time < rippleBegin_[i] + ChargeIndicatorMetrics::rippleDuration;
        if (group) {
            const double local = std::clamp((time - rippleBegin_[i]) / ChargeIndicatorMetrics::rippleDuration, 0.0, 1.0);
            pose.rippleScale[i] = ChargeIndicatorMetrics::rippleFrom +
                                  (ChargeIndicatorMetrics::rippleTo - ChargeIndicatorMetrics::rippleFrom) * easeOutCurve.value(local);
        } else pose.rippleScale[i] = rippleOrder_[i] > 0 ? ChargeIndicatorMetrics::rippleTo : 1;
    }
    pose.borderWidth = value(borderWidth, time);
    pose.borderColor = {value(borderRed, time), value(borderGreen, time), value(borderBlue, time), value(borderAlpha, time)};
    return pose;
}

ChargeIndicatorColors chargeIndicatorColors(const ChargeIndicatorAppearance& appearance, const BatteryReading& battery,
                                            ChargeMetric metric, std::optional<double> metricProgress, ChargeStage stage,
                                            bool embeddedHovered) noexcept {
    const bool dark = appearance.dark;
    ChargeIndicatorColors c;
    c.foreground = dark ? white(0.95) : white(0.10);
    c.background = dark ? white(0.105, 0.99) : white(0.975, 0.99);
    c.border = embeddedHovered ? alpha(appearance.accent, 0.98) : alpha(c.foreground, dark ? 0.045 : 0.08);
    c.borderWidth = embeddedHovered ? 1.5 : 0.5;
    if (metric != ChargeMetric::battery) {
        c.level = !metricProgress && (metric == ChargeMetric::ram || metric == ChargeMetric::cpu) ? alpha(c.foreground, 0.45)
                                                                                                  : alpha(appearance.accent, 1);
    } else if (const auto tone = batteryLevelTone(battery.percentage)) {
        switch (*tone) {
        case BatteryLevelTone::green: c.level = dark ? ChargeColor{0.25, 0.87, 0.43, 1} : ChargeColor{0.08, 0.62, 0.27, 1}; break;
        case BatteryLevelTone::yellow: c.level = dark ? ChargeColor{0.96, 0.80, 0.25, 1} : ChargeColor{0.73, 0.49, 0.02, 1}; break;
        case BatteryLevelTone::red: c.level = dark ? ChargeColor{0.98, 0.35, 0.34, 1} : ChargeColor{0.80, 0.16, 0.16, 1}; break;
        }
    } else c.level = alpha(c.foreground, 0.45);
    const bool compact = stage == ChargeStage::compact;
    c.emblem = compact ? alpha(c.foreground, 0.19) : c.foreground;
    c.bolt = compact ? c.foreground : c.background;
    c.ringTrack = alpha(c.level, 0.19);
    c.ringProgress = c.level;
    c.laptop = alpha(c.foreground, 0.78);
    c.ripple = c.foreground;
    return c;
}
std::string chargeModeEnglish(const BatteryReading& battery) { return batteryChargeMode(battery) ? "CHARGE MODE" : "POWER MODE"; }
std::string chargeModeTitle(const BatteryReading& battery, core::Language language) {
    return batteryChargeMode(battery) ? tr("CHARGE MODE", "超充模式", language) : tr("POWER MODE", "电源模式", language);
}
std::string chargePowerState(const BatteryReading& battery, core::Language language) {
    return battery.charging ? tr("Charging", "正在充电", language)
         : battery.pluggedIn ? tr("Power connected", "已连接电源", language)
                             : tr("On battery", "使用电池", language);
}
std::string chargeIndicatorAccessibility(const ChargeMetricReading& reading, const BatteryReading& battery, bool preview,
                                         core::Language language) {
    return "EndfieldHUD, " + reading.accessibilityValue + ", " + chargePowerState(battery, language) +
           (preview ? tr(", Preview", "，预览", language) : std::string());
}

Json chargeTextDescriptor(const Json& textLayer) { return textLayer["text"]; }

ChargeIndicatorContent prepareChargeIndicatorContent(const ChargeIndicatorAppearance& appearance, const BatteryReading& battery,
                                                     ChargeMetric metric, const std::optional<ChargeTelemetry>& telemetry,
                                                     bool preview, const ChargeTextMeasure& measure) {
    if (!measure) throw std::invalid_argument("Charge indicator fitting needs the native text measurement");
    if (appearance.language == core::Language::system) throw std::invalid_argument("Resolve charge language in the owner");
    ChargeIndicatorContent content;
    content.reading = resolveChargeMetric(metric, battery, telemetry, appearance.language);
    content.accessibility = chargeIndicatorAccessibility(content.reading, battery, preview, appearance.language);
    const auto foreground = appearance.dark ? white(0.95) : white(0.10);
    const auto& frames = ChargeIndicatorMetrics::textFrames;
    content.texts[0] = textLayer("charge/4", frames[0][3], frames[0][4],
                                 {{"// " + chargeModeEnglish(battery), font(".AppleSystemUIFontMedium", 7.5, 0), alpha(foreground, 0.55), 0.2}}, "left");
    const auto title = chargeModeTitle(battery, appearance.language);
    auto titleLayer = [&](double size) {
        return textLayer("charge/5", frames[1][3], frames[1][4], {{title, font(".AppleSystemUIFontDemi", size, 2), foreground, {}}}, "left");
    };
    // Keep the bilingual styling and capsule geometry while fitting longer
    // English and Japanese titles without clipping.
    double titleSize = 20;
    content.texts[1] = titleLayer(titleSize);
    while (titleSize > 10 && measure("charge/5", chargeTextDescriptor(content.texts[1])) > ChargeIndicatorMetrics::bannerTitleWidth - 2) {
        titleSize -= 0.5;
        content.texts[1] = titleLayer(titleSize);
    }
    content.titleFontSize = titleSize;
    const auto muted = alpha(foreground, 0.47);
    auto metricLine = [&](double scale) {
        std::vector<Run> runs{{content.reading.primary, font(".SFNS-Semibold", 12.5 * scale, 2), foreground, {}},
                              {content.reading.secondary, font(".SFNS-Medium", 9.5 * scale, 0), muted, {}}};
        if (!content.reading.unit.empty()) runs.push_back({" " + content.reading.unit, font(".AppleSystemUIFontMedium", 7.5 * scale, 0), muted, {}});
        return textLayer("charge/6", frames[2][3], frames[2][4], runs, "left");
    };
    double scale = 1;
    content.texts[2] = metricLine(scale);
    while (measure("charge/6", chargeTextDescriptor(content.texts[2])) > 143 && scale > 0.6) {
        scale -= 0.05;
        content.texts[2] = metricLine(scale);
    }
    content.lineScale = scale;
    auto trailingLayer = [&](double size) {
        return textLayer("charge/7", frames[3][3], frames[3][4], {{content.reading.trailing, font(".SFNS-Semibold", size, 2), foreground, {}}}, "right");
    };
    double trailingSize = 13;
    content.texts[3] = trailingLayer(trailingSize);
    while (trailingSize > 9 && measure("charge/7", chargeTextDescriptor(content.texts[3])) > 40) {
        trailingSize -= 0.5;
        content.texts[3] = trailingLayer(trailingSize);
    }
    content.trailingFontSize = trailingSize;
    return content;
}

Json chargeBoltLayer() {
    return shape("charge/3", {0, 0, 24, 24},
                 {command("move", {{14, 2.8}}), command("line", {{5.5, 13.3}}), command("line", {{11.0, 13.3}}),
                  command("line", {{9.5, 21.2}}), command("line", {{18.5, 10.2}}), command("line", {{13.0, 10.2}}), command("close")},
                 ChargeColor{1, 1, 1, 1}, std::nullopt, 1);
}
Json chargeLaptopLayer() {
    // CGPath(roundedRect: (8.5,8,11,9), corner 0.7) followed by the stand.
    constexpr double k = 0.5522847498 * 0.7;
    Json::Array path{command("move", {{19.5, 12.5}}), command("line", {{19.5, 16.3}}),
                     command("cubic", {{19.5, 16.3 + k}, {18.8 + k, 17}, {18.8, 17}}), command("line", {{9.2, 17}}),
                     command("cubic", {{9.2 - k, 17}, {8.5, 16.3 + k}, {8.5, 16.3}}), command("line", {{8.5, 8.7}}),
                     command("cubic", {{8.5, 8.7 - k}, {9.2 - k, 8}, {9.2, 8}}), command("line", {{18.8, 8}}),
                     command("cubic", {{18.8 + k, 8}, {19.5, 8.7 - k}, {19.5, 8.7}}), command("close"),
                     command("move", {{7, 19}}), command("line", {{21, 19}}), command("move", {{8.5, 17}}),
                     command("line", {{7, 19}}), command("move", {{19.5, 17}}), command("line", {{21, 19}})};
    return shape("charge/8/2", {0, 0, 28, 28}, std::move(path), std::nullopt, ChargeColor{1, 1, 1, 1}, 1.15, "butt", "round");
}
Json chargeRingLayer(bool progress) {
    if (!progress) return shape("charge/8/0", {0, 0, 28, 28}, ellipse({3, 3, 22, 22}), std::nullopt, ChargeColor{1, 1, 1, 1}, 2, "round");
    // The progress arc starts at the top and runs clockwise in the flipped
    // canvas; the GPU applies strokeEnd as an angular clip plus round caps.
    constexpr double k = 0.5522847498 * 11;
    Json::Array path{command("move", {{14, 3}}), command("cubic", {{14 + k, 3}, {25, 14 - k}, {25, 14}}),
                     command("cubic", {{25, 14 + k}, {14 + k, 25}, {14, 25}}), command("cubic", {{14 - k, 25}, {3, 14 + k}, {3, 14}}),
                     command("cubic", {{3, 14 - k}, {14 - k, 3}, {14, 3}})};
    return shape("charge/8/1", {0, 0, 28, 28}, std::move(path), std::nullopt, ChargeColor{1, 1, 1, 1}, 2, "butt");
}
Json chargeCapLayer() { return shape("charge/8/cap", {0, 0, 2, 2}, ellipse({0, 0, 2, 2}), ChargeColor{1, 1, 1, 1}, std::nullopt, 1); }
Json chargePositionButtonCircleLayer() {
    return shape("charge/button/circle", {0, 0, 28, 28}, ellipse({1, 1, 26, 26}), ChargeColor{1, 1, 1, 1}, std::nullopt, 1);
}
Json chargePositionButtonSymbolLayer(bool confirm) {
    Json::Array path = confirm
        ? Json::Array{command("move", {{8, 14}}), command("line", {{12, 18}}), command("line", {{20, 10}})}
        : Json::Array{command("move", {{9, 9}}), command("line", {{19, 19}}), command("move", {{9, 19}}), command("line", {{19, 9}})};
    return shape(confirm ? "charge/button/confirm" : "charge/button/cancel", {0, 0, 28, 28}, std::move(path), std::nullopt,
                 ChargeColor{1, 1, 1, 1}, 1.7, "round", "round");
}
ChargePositionButtonColors chargePositionButtonColors(bool dark, bool highlighted) noexcept {
    return {dark ? white(0.12, 0.97) : white(0.96, 0.98), alpha(dark ? white(1) : white(0), highlighted ? 0.5 : 0.85)};
}
std::string chargePositionButtonLabel(bool confirm, core::Language language) {
    return confirm ? tr("Confirm position", "确认位置", language) : tr("Discard position", "取消位置更改", language);
}
std::string chargePositionButtonTooltip(bool confirm, core::Language language) {
    return confirm ? tr("Confirm (Return)", "确认（回车）", language) : tr("Discard (Esc)", "取消（Esc）", language);
}
Json chargeRippleLayer() { return shape("charge/1/ripple", {0, 0, 320, 320}, ellipse({3, 3, 314, 314}), std::nullopt, ChargeColor{1, 1, 1, 1}, 5); }
} // namespace endfield::modules
