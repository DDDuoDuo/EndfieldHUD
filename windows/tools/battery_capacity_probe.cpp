// User-run, read-only live acceptance probe for the Windows battery capacity
// path. The default invocation is inert (the registered test checks only
// that). `--read` performs one GetSystemPowerStatus decode and one bounded
// battery-class enumeration; it changes no setting and writes nothing.
#include "native/power_service.hpp"
#include <iostream>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace endfield;

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--read") {
        std::cout << "battery_capacity_probe: inert. Pass --read for one read-only power/battery-class query.\n";
        return 0;
    }
#ifdef _WIN32
    native::BatterySnapshot snapshot;
    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status))
        snapshot = native::decode_power_status({status.ACLineStatus, status.BatteryFlag, status.BatteryLifePercent,
                                                status.BatteryLifeTime, status.BatteryFullLifeTime});
    auto reading = native::batteryReadingFromSnapshot(snapshot);
    const auto devices = native::readBatteryDevices();
    if (reading.present) reading.capacity = native::decodeBatteryCapacity(devices.reports, reading.percentage);
    std::cout << "present=" << reading.present << " percent=" << (reading.percentage ? std::to_string(*reading.percentage) : "unknown")
              << " pluggedIn=" << reading.pluggedIn << " charging=" << reading.charging << " full=" << reading.fullyCharged << '\n';
    std::cout << "classDevices=" << devices.reports.size() << " error=0x" << std::hex << static_cast<std::uint32_t>(devices.error) << std::dec << '\n';
    for (const auto& r : devices.reports)
        std::cout << "  capabilities=0x" << std::hex << r.capabilities << std::dec << " full=" << r.fullChargedCapacity
                  << " remaining=" << (r.remainingCapacity ? std::to_string(*r.remainingCapacity) : "n/a") << " powerState=" << r.powerState << '\n';
    if (reading.capacity) std::cout << "capacity=" << reading.capacity->current << "/" << reading.capacity->maximum << " " << reading.capacity->unit << '\n';
    else std::cout << "capacity=unavailable\n";
    std::cout << "health=unavailable (no Windows battery condition category)\n";
    return 0;
#else
    std::cout << "battery_capacity_probe: Windows only\n";
    return 0;
#endif
}
