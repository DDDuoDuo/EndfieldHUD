#include "native/event_log_time.hpp"
#include <cmath>
#include <cstdio>

namespace endfield::native {
namespace {
// FILETIME/SYSTEMTIME cover 1601-01-01 ... 30827-12-31; keep two days of
// margin so a local offset can never leave that range.
constexpr std::int64_t earliestSeconds = -11644473600LL + 2 * 86400;
constexpr std::int64_t latestSeconds = 910692730085LL - 2 * 86400;
}
std::optional<std::int64_t> eventLogTimestampSeconds(double reference) noexcept {
    if (!std::isfinite(reference)) return {};
    // CFDateFormatter: UDate = (at + kCFAbsoluteTimeIntervalSince1970) * 1000 + 0.5,
    // then ICU floors the millisecond and the second.
    const double milliseconds = std::floor((reference + 978307200.0) * 1000.0 + 0.5);
    if (!(milliseconds >= double(earliestSeconds) * 1000.0 && milliseconds <= double(latestSeconds) * 1000.0)) return {};
    const auto value = static_cast<std::int64_t>(milliseconds);
    auto seconds = value / 1000;
    if (value % 1000 < 0) --seconds;
    return seconds;
}
std::string formatEventLogCivil(unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second) {
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) return {};
    char text[16]{};
    std::snprintf(text, sizeof(text), "%02u-%02u %02u:%02u:%02u", month, day, hour, minute, second);
    return text;
}
} // namespace endfield::native

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timezoneapi.h>

namespace endfield::native {
namespace {
std::optional<std::string> convert(double reference, const DYNAMIC_TIME_ZONE_INFORMATION* zone) {
    const auto seconds = eventLogTimestampSeconds(reference);
    if (!seconds) return {};
    ULARGE_INTEGER ticks{};
    ticks.QuadPart = static_cast<std::uint64_t>(*seconds + 11644473600LL) * 10000000ULL;
    const FILETIME time{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&time, &utc)) return {};
    // The dynamic zone applies the daylight-saving rules of the event's own
    // year (and honors "Adjust for daylight saving time automatically").
    const BOOL converted = zone ? SystemTimeToTzSpecificLocalTimeEx(zone, &utc, &local) : SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
    if (!converted) return {};
    auto text = formatEventLogCivil(local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
    if (text.empty()) return {};
    return text;
}
}
std::string localEventLogTimestamp(double reference) {
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    const bool known = GetDynamicTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID;
    auto text = convert(reference, known ? &zone : nullptr);
    return text ? std::move(*text) : std::string("--");
}
std::optional<std::string> eventLogTimestampInZone(double reference, std::wstring_view key) {
    if (key.empty() || key.size() >= 128) return {};
    // One remembered zone per thread: consecutive lookups of the same key do
    // not enumerate the registry's zone list again.
    thread_local DYNAMIC_TIME_ZONE_INFORMATION remembered{};
    if (std::wstring_view(remembered.TimeZoneKeyName) == key) return convert(reference, &remembered);
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    for (DWORD index = 0; index < 4096; ++index) {
        if (EnumDynamicTimeZoneInformation(index, &zone) != ERROR_SUCCESS) return {};
        if (std::wstring_view(zone.TimeZoneKeyName) == key) { remembered = zone; return convert(reference, &zone); }
    }
    return {};
}
} // namespace endfield::native
#endif
