#include "native/battery_capacity.hpp"
#include "modules/power_policy.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <batclass.h>
#endif

namespace endfield::native {
std::optional<modules::BatteryReading::Capacity> decodeBatteryCapacity(std::span<const BatteryDeviceReport> reports,
                                                                       std::optional<unsigned> percentage) {
    std::uint64_t current{}, full{};
    std::size_t used{};
    bool absolute = true;
    for (const auto& report : reports) {
        // Only the machine's own batteries; a UPS never stands in for them.
        if (!(report.capabilities & BatteryDeviceReport::systemBattery) || (report.capabilities & BatteryDeviceReport::shortTerm)) continue;
        ++used;
        if ((report.capabilities & BatteryDeviceReport::relativeCapacity) || !report.remainingCapacity ||
            *report.remainingCapacity == BatteryDeviceReport::unknownCapacity ||
            report.fullChargedCapacity == BatteryDeviceReport::unknownCapacity) {
            absolute = false;
            continue;
        }
        current += *report.remainingCapacity;
        full += report.fullChargedCapacity;
    }
    if (used && absolute)
        if (auto pair = modules::batteryCapacityPair(static_cast<double>(current), static_cast<double>(full), "mWh")) return pair;
    return modules::batteryCapacityPercent(percentage);
}

#ifdef _WIN32
namespace {
// GUID_DEVCLASS_BATTERY / GUID_DEVICE_BATTERY (devguid.h, batclass.h).
constexpr GUID batteryClass{0x72631e54, 0x78a4, 0x11d0, {0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a}};
struct DeviceList {
    HDEVINFO value;
    ~DeviceList() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); }
};
struct Handle {
    HANDLE value;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::int32_t lastError() { return static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())); }
} // namespace

BatteryDeviceRead readBatteryDevices(std::size_t maximumDevices) {
    BatteryDeviceRead result;
    DeviceList devices{SetupDiGetClassDevsW(&batteryClass, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
    if (devices.value == INVALID_HANDLE_VALUE) { result.error = lastError(); return result; }
    for (DWORD index = 0; index < 64 && result.reports.size() < maximumDevices; ++index) {
        SP_DEVICE_INTERFACE_DATA data{};
        data.cbSize = sizeof data;
        if (!SetupDiEnumDeviceInterfaces(devices.value, nullptr, &batteryClass, index, &data)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) result.error = lastError();
            break;
        }
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devices.value, &data, nullptr, 0, &required, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || required > 4096) continue;
        std::vector<std::uint64_t> storage((required + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devices.value, &data, detail, required, &required, nullptr)) continue;
        // Read-only queries: the battery class IOCTLs require FILE_READ_ACCESS.
        Handle battery{CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (battery.value == INVALID_HANDLE_VALUE) continue;
        BATTERY_QUERY_INFORMATION query{};
        ULONG wait = 0;
        DWORD bytes = 0;
        if (!DeviceIoControl(battery.value, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof wait, &query.BatteryTag, sizeof query.BatteryTag,
                             &bytes, nullptr) || query.BatteryTag == BATTERY_TAG_INVALID) continue;
        BATTERY_INFORMATION information{};
        query.InformationLevel = BatteryInformation;
        if (!DeviceIoControl(battery.value, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof query, &information, sizeof information,
                             &bytes, nullptr) || bytes < sizeof information) continue;
        BatteryDeviceReport report;
        report.capabilities = information.Capabilities;
        report.designedCapacity = information.DesignedCapacity;
        report.fullChargedCapacity = information.FullChargedCapacity;
        BATTERY_WAIT_STATUS request{};
        request.BatteryTag = query.BatteryTag; // Timeout 0: the current status, no wait
        BATTERY_STATUS status{};
        if (DeviceIoControl(battery.value, IOCTL_BATTERY_QUERY_STATUS, &request, sizeof request, &status, sizeof status, &bytes, nullptr) &&
            bytes >= sizeof status) {
            report.remainingCapacity = status.Capacity;
            report.powerState = status.PowerState;
        }
        result.reports.push_back(report);
    }
    return result;
}
#else
BatteryDeviceRead readBatteryDevices(std::size_t) { return {}; }
#endif
} // namespace endfield::native
