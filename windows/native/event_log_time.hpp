#pragma once
// The Event Log's local time (EventLogCanvas.time, Sources/EventLogCanvas.swift
// :367-372): Calendar.current's month, day, hour and minute of an event's
// Foundation-epoch createdAt in the current time zone (Gregorian calendar),
// the fields modules::eventLogTime (modules/event_log_canvas.hpp) turns into
// `06:45` and `10.19`. Converted
// when the Event Log builds rows, never per frame; no clock, timer or cache.
// Mac oracle: tools/utility_canvases_reference.sh (eventLogTimes) ->
// tests/fixtures/utility-canvases-source.json.
#include "modules/event_log.hpp"
#include <cstdint>
#include <optional>
#include <string_view>

namespace endfield::native {
// Unix seconds of a Foundation reference date, floored to whole milliseconds
// and then seconds like Calendar; empty when not finite or outside the
// Gregorian range Windows can convert (years 1601...30827).
std::optional<std::int64_t> eventLogTimestampSeconds(double foundationReference) noexcept;
#ifdef _WIN32
// The current Windows time zone, read on each call so a time zone change
// applies on the next build, with that zone's dynamic daylight-saving rules
// for the event's year; empty for an unrepresentable date.
std::optional<modules::EventLogCivilTime> localEventLogTime(double foundationReference);
// Same conversion in an explicit Windows time zone (registry key such as
// "Pacific Standard Time"); empty when the zone is not installed. Tests only.
std::optional<modules::EventLogCivilTime> eventLogTimeInZone(double foundationReference, std::wstring_view windowsZoneKey);
#endif
} // namespace endfield::native
