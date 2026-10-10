#pragma once
#include "modules/battery_presentation.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Windows battery capacity: the matching mWh pair from the battery class
// driver (IOCTL_BATTERY_QUERY_INFORMATION FullChargedCapacity with
// IOCTL_BATTERY_QUERY_STATUS Capacity), the Windows counterpart of the Mac
// AppleSmartBattery raw pair. The decoder is portable and synthetic-testable;
// the device reader is Win32 only and blocking, so it runs on the shared
// utility worker after a power event, never on a timer or the UI thread.
namespace endfield::native {
struct BatteryDeviceReport {
    // batclass.h constants, kept here so the decoder needs no Windows headers.
    static constexpr std::uint32_t systemBattery = 0x80000000u, relativeCapacity = 0x40000000u, shortTerm = 0x20000000u;
    static constexpr std::uint32_t unknownCapacity = 0xFFFFFFFFu;
    static constexpr std::uint32_t powerOnLine = 1, discharging = 2, charging = 4, critical = 8;
    std::uint32_t capabilities{};
    std::uint32_t designedCapacity{unknownCapacity}, fullChargedCapacity{unknownCapacity};
    std::optional<std::uint32_t> remainingCapacity; // BATTERY_STATUS.Capacity when the status query succeeded
    std::uint32_t powerState{};
    bool operator==(const BatteryDeviceReport&) const = default;
};
// Aggregate system batteries (UPS/short-term devices excluded). Every battery
// must report a known absolute pair; relative-scale hardware or a partial set
// falls back to the explicitly labelled percent pair, never invented energy.
// The design capacity is not a health estimate and is never substituted.
std::optional<modules::BatteryReading::Capacity> decodeBatteryCapacity(
    std::span<const BatteryDeviceReport>, std::optional<unsigned> percentage);
// Windows exposes no OS battery-condition category (kIOPSBatteryHealth has no
// equivalent), so health stays unavailable rather than computed from design.
inline constexpr bool batteryHealthCategoryAvailable = false;

struct BatteryDeviceRead {
    std::vector<BatteryDeviceReport> reports;
    std::int32_t error{}; // HRESULT-shaped first enumeration failure (0 = ok)
    bool operator==(const BatteryDeviceRead&) const = default;
};
// Bounded (maximumDevices) enumeration of present battery class devices.
// Blocking IOCTLs with zero wait; utility-worker only. Non-Windows builds
// return an empty, successful result.
BatteryDeviceRead readBatteryDevices(std::size_t maximumDevices = 8);
} // namespace endfield::native
