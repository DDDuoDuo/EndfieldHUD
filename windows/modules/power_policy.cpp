#include "modules/power_policy.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace endfield::modules {
namespace {
using Json = ehud::data::Json;
std::string tr(const char* english, const char* chinese, core::Language language) { return core::localized(english, chinese, language); }
double clamp(double value, double low, double high, double fallback) { return std::isfinite(value) ? std::min(high, std::max(low, value)) : fallback; }
bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
} // namespace

std::string_view chargeDisplayActionKey(ChargeDisplayAction action) noexcept {
    switch (action) {
    case ChargeDisplayAction::hide: return "hide";
    case ChargeDisplayAction::showPersistent: return "showPersistent";
    case ChargeDisplayAction::showTransient: return "showTransient";
    case ChargeDisplayAction::keepCurrent: return "keepCurrent";
    }
    return "hide";
}
ChargeDisplayAction chargeDisplayAction(const BatteryReading& snapshot, const std::optional<BatteryReading>& previous,
                                        bool alwaysMode) noexcept {
    if (!snapshot.present) return ChargeDisplayAction::hide;
    if (alwaysMode) return ChargeDisplayAction::showPersistent;
    // Starting the app, changing modes, or recovering after an unavailable
    // reading establishes a baseline instead of a spurious connection popup.
    if (!previous || !previous->present) return ChargeDisplayAction::hide;
    return previous->pluggedIn != snapshot.pluggedIn ? ChargeDisplayAction::showTransient : ChargeDisplayAction::keepCurrent;
}

ChargeAlertSettings ChargeAlertSettings::normalized() const {
    auto result = *this;
    std::string raw;
    std::size_t begin = 0, end = accentHex.size();
    while (begin < end && whitespace(accentHex[begin])) ++begin;
    while (end > begin && whitespace(accentHex[end - 1])) --end;
    for (std::size_t i = begin; i < end; ++i)
        if (accentHex[i] != '#') raw.push_back(accentHex[i] >= 'a' && accentHex[i] <= 'z' ? static_cast<char>(accentHex[i] - 32) : accentHex[i]);
    const bool valid = raw.size() == 6 && std::all_of(raw.begin(), raw.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); });
    result.displayDuration = clamp(displayDuration, 1, 60, 3);
    result.accentHex = valid ? raw : "FAD41F";
    result.scale = clamp(scale, 0.65, 1.6, 1);
    result.customX = clamp(customX, 0, 1, 0.5);
    result.customY = clamp(customY, 0, 1, 0.9);
    return result;
}
ChargeColor ChargeAlertSettings::accent() const {
    const auto hex = normalized().accentHex;
    const auto value = std::stoul(hex, nullptr, 16);
    return {((value >> 16) & 255) / 255.0, ((value >> 8) & 255) / 255.0, (value & 255) / 255.0, 1};
}
ChargeAlertSettings ChargeAlertSettings::fromSettings(const Json& f) {
    ChargeAlertSettings s;
    if (!f.isObject()) return s;
    auto string = [&](const char* key, std::string fallback) { return f[key].isString() ? f[key].string() : fallback; };
    auto real = [&](const char* key, double fallback) { return f[key].isNumber() ? f[key].number() : fallback; };
    auto flag = [&](const char* key, bool fallback) { return f[key].isBool() ? f[key].boolean() : fallback; };
    s.alwaysMode = string("displayMode", "whenChargingStarts") == "always";
    s.displayDuration = real("displayDuration", 3);
    s.accentHex = string("accentHex", "FAD41F");
    const auto theme = string("theme", "dark");
    s.theme = theme == "light" ? ChargeTheme::light : theme == "system" ? ChargeTheme::system : ChargeTheme::dark;
    s.scale = real("scale", 1);
    s.customPlacement = string("placement", "topCenter") == "custom";
    if (f["customScreenID"].isNumber()) {
        const double id = f["customScreenID"].number();
        if (std::isfinite(id) && id >= 0 && id <= std::numeric_limits<std::uint32_t>::max() && std::floor(id) == id)
            s.customScreenID = static_cast<std::uint32_t>(id);
    }
    s.customX = real("customPositionX", 0.5);
    s.customY = real("customPositionY", 0.9);
    s.metric = chargeMetricFromKey(string("alertMetric", "battery")).value_or(ChargeMetric::battery);
    s.alertsEnabled = flag("batteryAlertsEnabled", true);
    s.devicePopupEnabled = flag("devicePopupEnabled", true);
    s.reduceMotion = flag("reduceMotion", false);
    s.lowPowerVisualMode = flag("lowPowerVisualMode", false);
    return s.normalized();
}

core::Point chargeClampedAnchor(core::Point p, const core::Rect& screen, double scale, bool editing, double ppp) noexcept {
    const double factor = std::isfinite(scale) ? std::min(1.6, std::max(0.65, scale)) : 1;
    const double unit = std::isfinite(ppp) && ppp > 0 ? ppp : 1;
    const double halfWidth = std::min(150 * factor * unit, screen.width / 2);
    const double topMargin = std::min(30 * factor * unit, screen.height / 2);
    const double bottomMargin = std::min(editing ? (24 * factor + 34) * unit : 30 * factor * unit, screen.height / 2);
    const double x = std::isfinite(p.x) ? p.x : screen.x + screen.width / 2;
    const double y = std::isfinite(p.y) ? p.y : screen.y + screen.height / 2;
    // Mac: min(maxY - top, max(minY + bottom, y)) in bottom-left space.
    return {std::min(screen.x + screen.width - halfWidth, std::max(screen.x + halfWidth, x)),
            std::max(screen.y + topMargin, std::min(screen.y + screen.height - bottomMargin, y))};
}
core::Point chargeAnchor(const ChargeAlertSettings& settings, const core::Rect& screen, bool editing, double ppp) noexcept {
    const auto s = settings.normalized();
    const double unit = std::isfinite(ppp) && ppp > 0 ? ppp : 1;
    const core::Point p = !s.customPlacement
        ? core::Point{screen.x + screen.width / 2, screen.y + 30 * s.scale * unit}
        : core::Point{screen.x + s.customX * screen.width, screen.y + screen.height - s.customY * screen.height};
    return chargeClampedAnchor(p, screen, s.scale, editing, unit);
}
ChargePosition chargeNormalizedPosition(core::Point anchor, const ChargeWorkArea& screen) noexcept {
    const auto& r = screen.bounds;
    if (!(r.width > 0 && r.height > 0)) return {};
    return {screen.id, clamp((anchor.x - r.x) / r.width, 0, 1, 0.5), clamp((r.y + r.height - anchor.y) / r.height, 0, 1, 0.9)};
}
ChargePanelLayout chargePanelLayout(core::Point anchor, double scale, bool editing, double ppp) noexcept {
    const double s = std::isfinite(scale) ? std::min(1.6, std::max(0.65, scale)) : 1;
    const double unit = std::isfinite(ppp) && ppp > 0 ? ppp : 1;
    const double width = ChargeIndicatorMetrics::canvasWidth * s * unit, canvasHeight = ChargeIndicatorMetrics::canvasHeight * s * unit;
    const double height = editing ? std::max(canvasHeight, (66 * s + 34) * unit) : canvasHeight;
    ChargePanelLayout layout;
    layout.window = {anchor.x - width / 2, anchor.y - canvasHeight / 2, width, height};
    layout.indicator = {layout.window.x, layout.window.y, width, canvasHeight};
    layout.cancel = {layout.window.x + width / 2 - 34 * unit, layout.window.y + 66 * s * unit, 28 * unit, 28 * unit};
    layout.confirm = {layout.window.x + width / 2 + 6 * unit, layout.window.y + 66 * s * unit, 28 * unit, 28 * unit};
    return layout;
}
std::optional<std::size_t> chargeSelectedScreen(std::span<const ChargeWorkArea> screens, std::optional<std::uint32_t> requested,
                                                std::optional<std::size_t> preferred) noexcept {
    if (requested)
        for (std::size_t i = 0; i < screens.size(); ++i) if (screens[i].id == *requested) return i;
    if (preferred && *preferred < screens.size()) return preferred;
    return screens.empty() ? std::nullopt : std::optional<std::size_t>(0);
}
std::uint32_t chargeScreenID(std::u16string_view id) noexcept {
    std::uint32_t hash = 2166136261u;
    for (const auto unit : id) {
        hash ^= static_cast<std::uint32_t>(unit & 0xff); hash *= 16777619u;
        hash ^= static_cast<std::uint32_t>(unit >> 8); hash *= 16777619u;
    }
    return hash;
}

std::string_view powerEventState(const BatteryReading& value) noexcept {
    if (!value.present) return "unavailable";
    if (value.charging) return "charging";
    if (value.fullyCharged) return "full";
    return value.pluggedIn ? "connected" : "battery";
}
std::vector<PowerEvent> PowerEventRecorder::receive(const BatteryReading& value) {
    const auto previous = previous_;
    previous_ = value;
    if (!previous) return {};
    EventMetadata metadata{{"state", std::string(powerEventState(value))}};
    if (value.percentage) metadata.emplace("percentage", std::to_string(*value.percentage));
    std::vector<PowerEvent> events;
    if (previous->present && value.present && previous->pluggedIn != value.pluggedIn)
        events.push_back({value.pluggedIn ? EventKind::powerConnected : EventKind::powerDisconnected, metadata});
    if (powerEventState(*previous) != powerEventState(value)) events.push_back({EventKind::batteryStateChanged, metadata});
    return events;
}

std::optional<BatteryReading::Capacity> batteryCapacityPair(std::optional<double> current, std::optional<double> maximum,
                                                            std::string_view unit) {
    auto integral = [](std::optional<double> value) -> std::optional<unsigned> {
        // A generous physical bound also rejects signed/unsigned sentinels.
        if (!value || !std::isfinite(*value) || *value < 0 || *value > 1'000'000 || std::trunc(*value) != *value) return std::nullopt;
        return static_cast<unsigned>(*value);
    };
    const auto c = integral(current), m = integral(maximum);
    if (!c || !m || *m <= 100 || *c > *m || unit.empty()) return std::nullopt;
    return BatteryReading::Capacity{*c, *m, std::string(unit)};
}
std::optional<BatteryReading::Capacity> batteryCapacityPercent(std::optional<unsigned> percentage) {
    if (!percentage || *percentage > 100) return std::nullopt;
    return BatteryReading::Capacity{*percentage, 100, "%"};
}

std::string powerTrayDescription(const BatteryReading& value, core::Language language) {
    if (!value.present) return tr("No internal battery · Preview available", "无内置电池 · 可预览效果", language);
    const auto percent = value.percentage ? std::to_string(*value.percentage) + "%" : tr("Unknown charge", "电量未知", language);
    const auto state = value.charging ? tr("Charging", "正在充电", language)
                     : value.pluggedIn ? tr("Power connected", "已连接电源", language) : tr("On battery", "使用电池", language);
    return percent + " · " + state;
}
std::string powerTrayTooltip(const BatteryReading& value, core::Language language) {
    return "EndfieldHUD — " + powerTrayDescription(value, language);
}
std::string powerPreviewMenuTitle(core::Language language) { return tr("Preview charging effect", "预览充电效果", language); }
BatteryReading chargeDemoReading() {
    BatteryReading demo;
    demo.percentage = 75; demo.present = demo.pluggedIn = demo.charging = true;
    demo.capacity = BatteryReading::Capacity{3600, 4800, "mAh"};
    return demo;
}

std::string deviceHostName(core::Language language) {
    switch (language) {
    case core::Language::simplifiedChinese: return "此电脑";
    case core::Language::traditionalChinese: return "本機";
    case core::Language::japanese: return "PC";
    case core::Language::korean: return "내 PC";
    default: return "This PC";
    }
}
std::vector<DeviceBatteryReading> deviceBatteryReadings(const BatteryReading& value, core::Language language) {
    std::optional<unsigned> percentage;
    if (value.present && value.percentage && *value.percentage <= 100) percentage = value.percentage;
    const auto availability = value.present ? (percentage ? DeviceBatteryAvailability::available : DeviceBatteryAvailability::unavailable)
                                            : DeviceBatteryAvailability::noBattery;
    return {DeviceBatteryReading{"host-mac", deviceHostName(language), true, percentage, availability}};
}

PowerAlertCoordinator::PowerAlertCoordinator(ChargeAlertPresenter& presenter, ChargeAlertSettings settings)
    : presenter_(&presenter), settings_(settings.normalized()) {}
BatteryReading PowerAlertCoordinator::previewValue() const {
    return snapshot_ && snapshot_->present ? *snapshot_ : chargeDemoReading();
}
void PowerAlertCoordinator::receive(const BatteryReading& next, double time) {
    snapshot_ = next;
    if (suspended()) return;
    presentLatestSnapshot(time);
}
void PowerAlertCoordinator::presentLatestSnapshot(double time) {
    if (!snapshot_ || terminating_ || suspended() || presenter_->editingPosition()) return;
    const auto next = *snapshot_;
    const auto previous = presentedSnapshot_;
    presentedSnapshot_ = next;
    if (presenter_->systemOverlayActive() || presenter_->projectionActive()) {
        previewSnapshot_.reset();
        presenter_->update(next, false, time);
        return;
    }
    // A manual preview owns its deadline; capacity-only updates must not dismiss it.
    const bool powerChanged = previous && (previous->present != next.present || previous->pluggedIn != next.pluggedIn ||
                                           previous->charging != next.charging);
    if (presenter_->visible() && previewSnapshot_ && !powerChanged) {
        const auto value = next.present ? next : chargeDemoReading();
        previewSnapshot_ = value;
        presenter_->update(value, true, time);
        return;
    }
    previewSnapshot_.reset();
    presenter_->update(next, false, time);
    apply(chargeDisplayAction(next, previous, settings_.alwaysMode), time);
}
void PowerAlertCoordinator::apply(ChargeDisplayAction action, double time) {
    if (terminating_) return;
    if (!settings_.alertsEnabled) { presenter_->hide(true, time); return; }
    switch (action) {
    case ChargeDisplayAction::hide: presenter_->hide(true, time); break;
    case ChargeDisplayAction::showPersistent:
        if (!presenter_->visible() || !presenter_->persistent()) presenter_->show(true, 0, false, time);
        break;
    case ChargeDisplayAction::showTransient: presenter_->show(false, settings_.displayDuration, true, time); break;
    case ChargeDisplayAction::keepCurrent: break;
    }
}
void PowerAlertCoordinator::preview(double time) {
    if (terminating_ || suspended() || presenter_->editingPosition() || presenter_->systemOverlayActive()) return;
    const auto value = previewValue();
    const bool persistent = settings_.alwaysMode && snapshot_ && snapshot_->present;
    if (persistent) previewSnapshot_.reset(); else previewSnapshot_ = value;
    presenter_->update(value, !persistent, time);
    presenter_->show(persistent, settings_.displayDuration, true, time);
}
void PowerAlertCoordinator::setSettings(const ChargeAlertSettings& requested, double time) {
    const auto previous = settings_;
    settings_ = requested.normalized();
    if (suspended() || terminating_) return;
    const auto value = snapshot_.value_or(BatteryReading{});
    if (presenter_->systemOverlayActive()) { presenter_->update(value, false, time); return; }
    if (presenter_->editingPosition()) { presenter_->update(previewValue(), true, time); return; }
    if (settings_.alwaysMode != previous.alwaysMode || settings_.alertsEnabled != previous.alertsEnabled) {
        previewSnapshot_.reset();
        presentedSnapshot_ = value;
        presenter_->update(value, false, time);
        presenter_->hide(false, time);
        apply(chargeDisplayAction(value, std::nullopt, settings_.alwaysMode), time);
    } else {
        const auto preview = presenter_->visible() ? previewSnapshot_ : std::nullopt;
        presenter_->update(preview.value_or(value), preview.has_value(), time);
        if (presenter_->visible() && settings_.displayDuration != previous.displayDuration)
            presenter_->show(presenter_->persistent(), settings_.displayDuration, false, time);
    }
}
void PowerAlertCoordinator::suspend(PowerSuspension reason, double time) {
    suspensions_ |= static_cast<std::uint8_t>(reason);
    presenter_->hide(false, time);
    previewSnapshot_.reset();
}
bool PowerAlertCoordinator::resume(PowerSuspension reason, std::optional<BatteryReading> fresh, double time) {
    const auto bit = static_cast<std::uint8_t>(reason);
    // Duplicate or unmatched wake notifications must not dismiss a manual preview.
    if (!(suspensions_ & bit)) return false;
    // Read while still suspended, so presentation is applied exactly once.
    if (fresh) snapshot_ = std::move(fresh);
    if (suspensions_ != bit) { suspensions_ &= static_cast<std::uint8_t>(~bit); return false; }
    suspensions_ = 0;
    presentLatestSnapshot(time);
    return true;
}
void PowerAlertCoordinator::terminate(double time) {
    terminating_ = true;
    presenter_->hide(false, time);
}
} // namespace endfield::modules
