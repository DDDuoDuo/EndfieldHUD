#pragma once
// Windows readers of the facility alerts and the boot log: the Windows
// counterparts of Sources/FacilityAlertMonitor.swift FacilitySystemReader
// (:447-527) and of the system values SystemHUDView.bootLogLines reads
// (:3060-3097). Documented Win32 calls only; no thread, timer, observer or
// window. Which thread may call each is stated per function.
//
//  - Disk: the Storage page's startup-volume figure
//    (native/storage_files.hpp readWindowsStartupCapacity: the system volume's
//    GetDiskFreeSpaceExW free-to-caller over total, with its label or
//    Explorer's name), so the alert and the page always agree.
//  - Memory: GlobalMemoryStatusEx (in use = total - available physical
//    memory, the Activity page's RAM figure) and the system's
//    LowMemoryResourceNotification, mapped by modules::facilityMemoryPressure.
//  - Thermal: none. Windows documents no user-mode thermal state, and the
//    battery temperature (IOCTL_BATTERY_QUERY_INFORMATION BatteryTemperature)
//    is optional and unreported on most systems; WMI thermal zones need
//    administrator rights. Recorded platform gap D4: no overheating alert.
//  - Uptime: QueryUnbiasedInterruptTime (time awake since boot, as the Mac's
//    ProcessInfo.systemUptime excludes sleep).
//  - Dates: the local calendar of the current time zone (GetLocalTime,
//    SystemTimeToTzSpecificLocalTimeEx for Event Log records).
#include "modules/boot_log.hpp"
#include "modules/facility_alerts.hpp"
#include <cstdint>
#include <optional>
#include <string_view>

namespace endfield::native {
// Worker thread (volume label and Explorer name: shell COM inside).
// `startupName` names an unlabelled startup disk Explorer cannot name either
// (StorageController.readStartupCapacity's "Startup disk" / 启动磁盘).
std::optional<modules::FacilityDiskReading> readWindowsFacilityDisk(std::string_view startupName);
// Any thread (two cheap kernel queries).
struct WindowsFacilityMemory { std::uint64_t used{}, total{}; bool lowMemory{}; };
std::optional<WindowsFacilityMemory> readWindowsFacilityMemory();
// Any thread.
double windowsUptimeSeconds();
modules::boot::BootLogDate windowsLocalDate();
// The local day number (days since 1970-01-01 in the local calendar) of a
// Foundation timestamp (seconds since 2001-01-01 UTC); nullopt when it does
// not convert.
std::optional<std::int64_t> windowsLocalDay(double foundationSeconds);
// SystemHUDView.bootLogLines' direct read when the monitor has no disk
// reading (facility alerts off): the system volume's free share, low below
// 10 %. GetDiskFreeSpaceExW only (UI thread allowed: one call, once a day).
std::optional<modules::boot::BootLogDisk> readWindowsBootLogDisk();
} // namespace endfield::native
