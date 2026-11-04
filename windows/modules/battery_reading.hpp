#pragma once
// The battery reading every Power piece shares: the port of the Mac
// BatterySnapshot (Sources/BatteryMonitor.swift:7-46) as Windows reads it,
// plus the BatteryDiagnosticsParsing rules of Sources/BatteryCapacity.swift
// (:53-90, MODS-468) that keep an unsupported or implausible reading at "—"
// instead of an invented number. Pure data; nothing here reads a device.
#include <cstdint>
#include <optional>
#include <string>

namespace endfield::modules {
// Optional provider values stay optional. A percentage pair is an explicit
// source fallback, never a fabricated mAh capacity or battery-health estimate.
// The Battery page's diagnostics (BatterySnapshot :15-23) are nil without a
// battery; adapterWatts only while plugged in (init gating :39-46). Windows
// has no OS adapter rating and no time-to-full estimate, so those two stay
// empty there (recorded platform gaps); cycles and temperature come from the
// battery class driver, minutesToEmpty from GetSystemPowerStatus.
struct BatteryReading {
    std::optional<unsigned> percentage;
    bool present{},pluggedIn{},charging{},fullyCharged{};
    struct Capacity {unsigned current{},maximum{};std::string unit;bool operator==(const Capacity&)const=default;};
    std::optional<Capacity>capacity;
    std::optional<std::string>health;
    std::optional<int>cycleCount;
    std::optional<double>temperatureCelsius;
    std::optional<int>adapterWatts,minutesToFull,minutesToEmpty;
    bool operator==(const BatteryReading&)const=default;
};
// BatteryDiagnosticsParsing, value for value: integers must be finite, >= 0,
// <= 1,000,000 and whole (booleans never reach here); cycleCount <= 100,000;
// temperature from hundredths of a degree Celsius within -40...150 °C;
// adapterWatts 1...1000; minutes <= 6000 (the OS's -1 "calculating" is
// rejected by the >= 0 rule).
std::optional<int>batteryCycleCount(double value)noexcept;
std::optional<double>batteryTemperatureCelsius(double hundredthsCelsius)noexcept;
std::optional<int>batteryAdapterWatts(double value)noexcept;
std::optional<int>batteryMinutes(double value)noexcept;
// BatterySnapshot.init's gating: without a battery every diagnostic is nil,
// adapterWatts survives only while plugged in.
BatteryReading gatedBatteryReading(BatteryReading)noexcept;
}
