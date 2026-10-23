// Windows readers of the facility alerts and the boot log (see the header).
#include "native/facility_sources.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "native/storage_files.hpp"
#include <cmath>

namespace endfield::native {
namespace {
// The system's low-memory signal: one notification object for the process
// (never closed; CreateMemoryResourceNotification's documented use).
HANDLE lowMemorySignal() noexcept {
    static const HANDLE handle = CreateMemoryResourceNotification(LowMemoryResourceNotification);
    return handle;
}
}
std::optional<modules::FacilityDiskReading> readWindowsFacilityDisk(std::string_view startupName) {
    const auto capacity = readWindowsStartupCapacity(0, startupName);
    if (!capacity) return std::nullopt;
    return modules::FacilityDiskReading::make(capacity->volumeName, capacity->totalBytes, capacity->availableBytes);
}
std::optional<WindowsFacilityMemory> readWindowsFacilityMemory() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    if (!GlobalMemoryStatusEx(&status) || !status.ullTotalPhys || status.ullAvailPhys > status.ullTotalPhys) return std::nullopt;
    BOOL low = FALSE;
    const HANDLE signal = lowMemorySignal();
    if (!signal || !QueryMemoryResourceNotification(signal, &low)) low = FALSE;
    return WindowsFacilityMemory{status.ullTotalPhys - status.ullAvailPhys, status.ullTotalPhys, low != FALSE};
}
double windowsUptimeSeconds() {
    ULONGLONG unbiased{};
    if (!QueryUnbiasedInterruptTime(&unbiased)) return double(GetTickCount64()) / 1000;
    return double(unbiased) / 10'000'000;
}
modules::boot::BootLogDate windowsLocalDate() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    return {t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute};
}
std::optional<std::int64_t> windowsLocalDay(double foundationSeconds) {
    // Foundation's reference date is 978307200 s after the Unix epoch; FILETIME
    // counts 100 ns from 1601 (11644473600 s before the Unix epoch).
    const double unix = foundationSeconds + 978'307'200;
    if (!std::isfinite(unix) || unix < 0 || unix > 32'503'680'000) return std::nullopt;
    const auto ticks = static_cast<ULONGLONG>((unix + 11'644'473'600.0) * 10'000'000);
    FILETIME utc{static_cast<DWORD>(ticks & 0xFFFFFFFFu), static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME universal{}, local{};
    if (!FileTimeToSystemTime(&utc, &universal) || !SystemTimeToTzSpecificLocalTimeEx(nullptr, &universal, &local)) return std::nullopt;
    return modules::boot::bootLogDayNumber(local.wYear, local.wMonth, local.wDay);
}
std::optional<modules::boot::BootLogDisk> readWindowsBootLogDisk() {
    const auto capacity = readWindowsStartupCapacity(0, {});
    if (!capacity || capacity->totalBytes <= 0) return std::nullopt;
    const double fraction = double(capacity->availableBytes) / double(capacity->totalBytes);
    return modules::boot::BootLogDisk{int(std::round(fraction * 100)), fraction < modules::FacilityAlertMonitor::diskRaiseFreeFraction};
}
} // namespace endfield::native
#endif
