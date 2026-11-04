// Facility alerts: the port of Sources/FacilityAlertMonitor.swift (:7-443;
// the system readers :447-527 are native/facility_sources.cpp). Strings are
// the Mac catalog pairs through core::localized.
#include "modules/facility_alerts.hpp"
#include "modules/hud_design_format.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace endfield::modules {
namespace {
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
std::string text(std::string_view english, std::string_view chinese, core::Language l, std::span<const std::string_view> arguments = {}) {
    return core::localized(english, chinese, l, arguments);
}
std::string format(const char* pattern, double value) {
    char out[64]{};
    std::snprintf(out, sizeof out, pattern, value);
    return out;
}
// Decimal units as the Storage and Activity modules print them (spec §1.8):
// FacilityAlert.storage is HUDFormat.storage's rule (design::format::storage);
// gigabytes "%.1f".
std::string storage(std::int64_t bytes) { return design::format::storage(double(bytes) / 1'000'000'000); }
std::string gigabytes(std::uint64_t bytes) { return format("%.1f", double(bytes) / 1'000'000'000); }
// Swift's Double.rounded() (schoolbook: half away from zero), then Int().
int roundedPercent(double fraction) noexcept { return int(std::round(fraction * 100)); }
}

std::string_view facilityAlertID(FacilityAlertKind k) noexcept {
    switch (k) {
    case FacilityAlertKind::disk: return "disk";
    case FacilityAlertKind::memory: return "mem";
    case FacilityAlertKind::heat: break;
    }
    return "heat";
}
core::Module facilityAlertModule(FacilityAlertKind k) noexcept {
    switch (k) {
    case FacilityAlertKind::disk: return core::Module::storage;
    case FacilityAlertKind::memory: return core::Module::activityMonitor;
    case FacilityAlertKind::heat: break;
    }
    return core::Module::power;
}
std::string facilityAlertTitle(FacilityAlertKind k, core::Language l) {
    switch (k) {
    case FacilityAlertKind::disk: return text("LOW DISK SPACE", "存储空间不足", l);
    case FacilityAlertKind::memory: return text("HIGH MEMORY PRESSURE", "内存压力过高", l);
    case FacilityAlertKind::heat: break;
    }
    return text("OVERHEATING", "设备过热", l);
}

std::optional<FacilityDiskReading> FacilityDiskReading::make(std::string volumeName, std::int64_t total, std::int64_t available) {
    if (!(total > 0 && available >= 0 && available <= total)) return std::nullopt;
    return FacilityDiskReading{std::move(volumeName), total, available};
}
int FacilityDiskReading::freePercent() const noexcept { return roundedPercent(freeFraction()); }
std::optional<FacilityMemoryPressure> facilityMemoryPressure(std::uint64_t used, std::uint64_t total, bool lowMemory) noexcept {
    if (!total) return std::nullopt;
    const double load = double(std::min(used, total)) / double(total);
    if (lowMemory || load >= facilityMemoryCriticalLoad) return FacilityMemoryPressure::critical;
    return load >= facilityMemoryWarningLoad ? FacilityMemoryPressure::warning : FacilityMemoryPressure::normal;
}
std::optional<FacilityMemoryUsage> FacilityMemoryUsage::make(std::uint64_t used, std::uint64_t total) {
    if (!total) return std::nullopt;
    return FacilityMemoryUsage{std::min(used, total), total};
}
int FacilityMemoryUsage::usedPercent() const noexcept { return roundedPercent(double(usedBytes) / double(totalBytes)); }

// FacilityAlert (:142-200).
std::string FacilityAlert::value(core::Language l) const {
    switch (kind()) {
    case FacilityAlertKind::disk: return std::to_string(measurement.disk->freePercent()) + "%";
    case FacilityAlertKind::memory:
        if (measurement.memory->usage) return std::to_string(measurement.memory->usage->usedPercent()) + "%";
        return measurement.memory->pressure == FacilityMemoryPressure::critical ? text("CRITICAL", "危急", l) : text("WARNING", "警告", l);
    case FacilityAlertKind::heat: break;
    }
    if (const auto celsius = measurement.heat->temperatureCelsius) return std::to_string(int(std::round(*celsius))) + "°C";
    return measurement.heat->state == FacilityThermalState::critical ? text("CRITICAL", "危急", l) : text("SERIOUS", "严重", l);
}
std::string FacilityAlert::unit(core::Language l) const {
    switch (kind()) {
    case FacilityAlertKind::disk: return text("FREE", "可用", l);
    case FacilityAlertKind::memory: return text("PRESSURE", "压力", l);
    case FacilityAlertKind::heat: break;
    }
    // Public APIs expose no CPU die temperature; the Mac's reading is the battery's.
    return measurement.heat->temperatureCelsius ? text("BATTERY", "电池", l) : text("THERMAL STATE", "热状态", l);
}
std::string FacilityAlert::detail(core::Language l) const {
    switch (kind()) {
    case FacilityAlertKind::disk: {
        const auto& disk = *measurement.disk;
        const auto left = storage(disk.availableBytes);
        const std::array<std::string_view, 2> arguments{disk.volumeName, left};
        return text("{0}  {1} left", "{0}  剩余 {1}", l, arguments);
    }
    case FacilityAlertKind::memory: {
        const auto& usage = measurement.memory->usage;
        if (!usage) return {};
        const auto used = gigabytes(usage->usedBytes), total = gigabytes(usage->totalBytes);
        const std::array<std::string_view, 2> arguments{used, total};
        return text("{0} / {1} GB in use", "已用 {0} / {1} GB", l, arguments);
    }
    case FacilityAlertKind::heat: break;
    }
    // The thermal state documentation: at serious, fans run at maximum speed.
    return text("Fans at maximum", "风扇全速运转", l);
}
std::string FacilityAlert::tagLabel(core::Language l) const {
    switch (kind()) {
    case FacilityAlertKind::disk: return text("DISK", "磁盘", l);
    case FacilityAlertKind::memory: return text("RAM", "RAM", l);
    case FacilityAlertKind::heat: break;
    }
    return text("HEAT", "温度", l);
}
std::optional<std::string> FacilityAlert::tagSuffix(core::Language l) const {
    if (kind() == FacilityAlertKind::heat) return std::nullopt;
    return unit(l);
}
std::string FacilityAlert::tagText(core::Language l) const {
    auto out = tagLabel(l) + " " + value(l);
    if (const auto suffix = tagSuffix(l)) out += " " + *suffix;
    return out;
}

// ---------------------------------------------------------------------------
// FacilityAlertMonitor (:249-443)
// ---------------------------------------------------------------------------
FacilityAlertMonitor::FacilityAlertMonitor(Sources sources) : sources_(std::move(sources)) {
    need(bool(sources_.runWorker) && bool(sources_.clock), "Facility alerts need a worker and a clock");
}
void FacilityAlertMonitor::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    if (on) start(); else stop();
}
std::vector<FacilityAlert> FacilityAlertMonitor::newAlerts() const {
    std::vector<FacilityAlert> out;
    for (const auto& a : alerts_) if (a.isNew) out.push_back(a);
    return out;
}
const FacilityAlert* FacilityAlertMonitor::alert(FacilityAlertKind k) const noexcept {
    for (const auto& a : alerts_) if (a.kind() == k) return &a;
    return nullptr;
}
std::optional<double> FacilityAlertMonitor::temperatureCelsius() const noexcept {
    return isTemperatureHigh() && readings_.thermal ? readings_.thermal->temperatureCelsius : std::nullopt;
}
void FacilityAlertMonitor::checkNow() { requestSample(); }
void FacilityAlertMonitor::dismiss(FacilityAlertKind k) {
    mutate([&] { std::erase_if(alerts_, [k](const FacilityAlert& a) { return a.kind() == k; }); });
}
void FacilityAlertMonitor::markPresented(FacilityAlertKind k) {
    mutate([&] { for (auto& a : alerts_) if (a.kind() == k) a.isNew = false; });
}
// start (:310-325): the Mac also installs its observers and the 60 s timer
// here; the owner's cadence and wake events call checkNow instead.
void FacilityAlertMonitor::start() {
    ++generation_;
    requestSample();
}
void FacilityAlertMonitor::stop() {
    ++generation_;
    needsSample_ = false;
    mutate([&] { alerts_.clear(); conditions_.clear(); readings_ = {}; });
}
// requestSample (:334-349): one read in flight, a request meanwhile coalesces.
void FacilityAlertMonitor::requestSample() {
    if (!enabled_) return;
    if (sampling_) { needsSample_ = true; return; }
    sampling_ = true; needsSample_ = false;
    const auto token = generation_;
    const bool usage = wantsMemoryUsage(), temperature = wantsTemperature();
    auto sources = sources_;
    sources_.runWorker([sources, usage, temperature] { return read(sources, usage, temperature); },
        [this, token](FacilityReadings result) {
            sampling_ = false;
            if (enabled_ && generation_ == token) applySample(result, sources_.clock());
            if (needsSample_) requestSample();
        });
}
// read (:351-362).
FacilityReadings FacilityAlertMonitor::read(const Sources& s, bool wantsMemoryUsage, bool wantsTemperature) {
    FacilityReadings result;
    if (s.readDisk) result.disk = s.readDisk();
    if (const auto pressure = s.readMemoryPressure ? s.readMemoryPressure() : std::nullopt) {
        std::optional<FacilityMemoryUsage> usage;
        if ((*pressure != FacilityMemoryPressure::normal || wantsMemoryUsage) && s.readMemoryUsage) usage = s.readMemoryUsage();
        result.memory = FacilityMemoryReading{*pressure, usage};
    }
    if (const auto state = s.readThermalState ? s.readThermalState() : std::nullopt) {
        std::optional<double> celsius;
        if ((*state >= FacilityThermalState::serious || wantsTemperature) && s.readTemperature) celsius = s.readTemperature();
        result.thermal = FacilityThermalReading{*state, celsius};
    }
    return result;
}
void FacilityAlertMonitor::apply(const FacilityReadings& sample) {
    if (enabled_) applySample(sample, sources_.clock());
}
// apply (:364-376): an unavailable source keeps its last reading and state.
void FacilityAlertMonitor::applySample(const FacilityReadings& sample, double now) {
    need(std::isfinite(now), "Facility alerts need a finite clock");
    mutate([&] {
        if (sample.disk) readings_.disk = sample.disk;
        if (sample.memory) readings_.memory = sample.memory;
        if (sample.thermal) readings_.thermal = sample.thermal;
        if (sample.disk) step({FacilityAlertKind::disk, sample.disk, {}, {}}, now);
        if (sample.memory) step({FacilityAlertKind::memory, {}, sample.memory, {}}, now);
        if (sample.thermal) step({FacilityAlertKind::heat, {}, {}, sample.thermal}, now);
    });
}
// step (:378-405).
void FacilityAlertMonitor::step(const FacilityAlert::Measurement& measurement, double now) {
    const auto kind = measurement.kind;
    auto condition = conditions_.contains(kind) ? conditions_[kind] : Condition{};
    const auto removeKind = [&] { std::erase_if(alerts_, [kind](const FacilityAlert& a) { return a.kind() == kind; }); };
    switch (signal(measurement)) {
    case Signal::raise:
        condition.recoverySince.reset();
        if (!condition.isActive) {
            condition.isActive = true;
            removeKind();
            alerts_.push_back({measurement, true});
        }
        break;
    case Signal::hold:
        condition.recoverySince.reset();
        break;
    case Signal::recover: {
        if (!condition.isActive) break;
        const double since = condition.recoverySince.value_or(now);
        condition.recoverySince = since;
        if (now - since >= recoveryHold) { condition = {}; removeKind(); }
        break;
    }
    }
    conditions_[kind] = condition;
    for (auto& a : alerts_) if (a.kind() == kind) a.measurement = measurement;
}
// signal (:407-415).
FacilityAlertMonitor::Signal FacilityAlertMonitor::signal(const FacilityAlert::Measurement& m) noexcept {
    switch (m.kind) {
    case FacilityAlertKind::disk:
        if (m.disk->freeFraction() < diskRaiseFreeFraction) return Signal::raise;
        return m.disk->freeFraction() >= diskClearFreeFraction ? Signal::recover : Signal::hold;
    case FacilityAlertKind::memory: return m.memory->pressure == FacilityMemoryPressure::normal ? Signal::recover : Signal::raise;
    case FacilityAlertKind::heat: break;
    }
    return m.heat->state >= FacilityThermalState::serious ? Signal::raise : Signal::recover;
}
FacilityAlertMonitor::Published FacilityAlertMonitor::published() const {
    return {alerts_, isStorageLow(), isTemperatureHigh(), temperatureCelsius()};
}
} // namespace endfield::modules
