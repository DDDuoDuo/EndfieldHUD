#pragma once
// EventLogCanvas.timestamp (Sources/EventLogCanvas.swift): en_US_POSIX
// "MM-dd HH:mm:ss" of an event's Foundation-epoch createdAt in the current
// local time zone (Gregorian calendar, two digits per field, 24-hour clock).
// CFDateFormatter hands ICU (reference + 978307200) * 1000 + 0.5 milliseconds
// and ICU takes the floor, so sub-millisecond values round half up to whole
// milliseconds before the second is floored (Mac oracle:
// tools/event_log_timestamp_reference.sh -> tests/fixtures/event_log_timestamps.json).
// Formatting happens only when the Event Log refreshes its visible rows,
// never per frame; no clock, timer or cache is involved.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::native {
// Unix seconds shown for a Foundation reference date; empty when not finite or
// outside the Gregorian range Windows can convert (years 1601...30827).
std::optional<std::int64_t> eventLogTimestampSeconds(double foundationReference) noexcept;
// "MM-dd HH:mm:ss" from civil fields (validated; empty when out of range).
std::string formatEventLogCivil(unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second);
#ifdef _WIN32
// Production formatter (EventLogOwner::callbacks): the current Windows time
// zone, read on each call so a time zone change applies on the next refresh,
// with that zone's dynamic daylight-saving rules for the event's year. An
// unrepresentable date yields "--" rather than an invented time.
std::string localEventLogTimestamp(double foundationReference);
// Same conversion in an explicit Windows time zone (registry key such as
// "Pacific Standard Time"); empty when the zone is not installed. Tests only.
std::optional<std::string> eventLogTimestampInZone(double foundationReference, std::wstring_view windowsZoneKey);
#endif
} // namespace endfield::native
