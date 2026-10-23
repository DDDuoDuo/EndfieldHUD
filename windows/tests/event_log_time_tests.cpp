// EventLogCanvas.time parity (Mac 1.3.0 oracle: tools/utility_canvases_reference.sh
// -> tests/fixtures/utility-canvases-source.json "eventLogTimes": Calendar
// components in nine zones, every daylight-saving switch of 2025...2027 and
// the seconds around it, sub-millisecond instants at a minute boundary).
// Synthetic instants only; the Windows time zone database is read, never changed.
#include "native/event_log_time.hpp"
#include "modules/event_log_canvas.hpp"
#include "core/data/json.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace n = endfield::native;
namespace m = endfield::modules;
using Json = ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
Json load(const char* path) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "The utility canvases oracle is readable");
    std::stringstream bytes; bytes << in.rdbuf();
    return Json::parse(bytes.str(), 8 * 1024 * 1024);
}
std::optional<std::string> day(const Json& item) { return item["day"].isString() ? std::optional(item["day"].string()) : std::nullopt; }
void portable(const Json& times) {
    // Calendar floors: no millisecond rounding (the fixture's 39.9995 s case).
    check(n::eventLogTimestampSeconds(0) == 978307200, "Reference date 0 is 2001-01-01T00:00:00Z");
    check(n::eventLogTimestampSeconds(0.9994) == 978307200 && n::eventLogTimestampSeconds(0.9995) == 978307200 && n::eventLogTimestampSeconds(1) == 978307201, "A 999.5 ms remainder stays in its second");
    check(n::eventLogTimestampSeconds(-0.0005) == 978307199 && n::eventLogTimestampSeconds(-0.0006) == 978307199, "Negative sub-millisecond values take the earlier second");
    check(n::eventLogTimestampSeconds(-1.5) == 978307198, "Pre-2001 fractions take the floor second");
    check(!n::eventLogTimestampSeconds(std::numeric_limits<double>::quiet_NaN()) && !n::eventLogTimestampSeconds(std::numeric_limits<double>::infinity()), "Non-finite dates have no timestamp");
    check(!n::eventLogTimestampSeconds(1e18) && !n::eventLogTimestampSeconds(-1e18), "Dates outside the Windows calendar range have no timestamp");
    // UTC and the fixed-offset zones need no daylight rules: every case,
    // including the 1970 and pre-2001 instants, follows from the seconds.
    const std::map<std::string, std::int64_t> fixed{{"UTC", 0}, {"Asia/Shanghai", 8 * 3600}, {"Asia/Kolkata", 5 * 3600 + 1800}};
    std::size_t compared{};
    for (const auto& item : times.array()) {
        const auto zone = fixed.find(item["zone"].string());
        if (zone == fixed.end()) continue;
        const auto offset = zone->second;
        const m::EventLogLocalTime local = [offset](double reference) -> std::optional<m::EventLogCivilTime> {
            const auto seconds = n::eventLogTimestampSeconds(reference);
            if (!seconds) return std::nullopt;
            // Civil date from days since 1970-01-01 (proleptic Gregorian).
            const auto at = *seconds + offset;
            auto days = at / 86400; auto rest = at % 86400; if (rest < 0) { rest += 86400; --days; }
            days += 719468; const auto era = (days >= 0 ? days : days - 146096) / 146097; const auto doe = days - era * 146097;
            const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const auto mp = (5 * doy + 2) / 153; const auto d = doy - (153 * mp + 2) / 5 + 1; const auto month = mp < 10 ? mp + 3 : mp - 9;
            return m::EventLogCivilTime{int(yoe + era * 400 + (month <= 2)), int(month), int(d), int(rest / 3600), int(rest / 60 % 60)};
        };
        const auto value = m::eventLogTime(item["reference"].number(), item["now"].number(), local);
        check(value.clock == item["clock"].string() && value.day == day(item),
            "Fixed-offset time matches EventLogCanvas.time: " + item["zone"].string() + " @" + std::to_string(item["reference"].number()) + " expected " + item["clock"].string() + " got " + value.clock + " " + value.day.value_or("-"));
        ++compared;
    }
    check(compared == 51, "Fixed-offset fixture cases are present");
}
#ifdef _WIN32
// Foundation reference seconds of 2025-01-01T00:00:00Z and 2028-03-02T00:00:00Z.
constexpr double modernStart = 757382400, modernEnd = 857260800;
std::wstring wide(const std::string& value) {
    std::wstring out(value.size(), L'\0');
    for (std::size_t n = 0; n < value.size(); ++n) out[n] = static_cast<unsigned char>(value[n]);
    return out;
}
void windows(const Json& times) {
    std::size_t modern{}, historical{}, historicalDifferent{};
    std::map<std::string, std::size_t> zones;
    for (const auto& item : times.array()) {
        const auto reference = item["reference"].number();
        const auto key = wide(item["windowsZone"].string());
        check(bool(n::eventLogTimeInZone(reference, key)), "Windows time zone is installed: " + item["windowsZone"].string());
        const auto value = m::eventLogTime(reference, item["now"].number(), [&](double r) { return n::eventLogTimeInZone(r, key); });
        const auto& name = item["windowsZone"].string();
        const bool fixedOffset = name == "UTC" || name == "China Standard Time" || name == "India Standard Time";
        if (fixedOffset || (reference >= modernStart && reference < modernEnd)) {
            // Every DST switch of 2025...2027 (and the seconds around it) in
            // the nine zones, including 30- and 45-minute offsets.
            check(value.clock == item["clock"].string() && value.day == day(item),
                "Windows dynamic time zone matches EventLogCanvas.time: " + name + " @" + std::to_string(reference) + " expected " + item["clock"].string() + " got " + value.clock);
            ++modern; ++zones[name];
        } else {
            // Older instants depend on per-zone history that the Windows
            // registry does not carry (for example the 1968-71 British
            // Standard Time). They stay well-formed and are reported only.
            ++historical;
            if (value.clock != item["clock"].string() || value.day != day(item)) ++historicalDifferent;
        }
    }
    check(zones.size() == 9 && modern >= 300, "All nine zones are compared over the modern range");
    std::cout << "historical instants differing from tzdb: " << historicalDifferent << " of " << historical << '\n';
    check(!n::eventLogTimeInZone(0, L"Not A Real Zone"), "Unknown zone keys are rejected");
    // Production converter: the current zone, identical to the explicit one.
    DYNAMIC_TIME_ZONE_INFORMATION current{};
    check(GetDynamicTimeZoneInformation(&current) != TIME_ZONE_ID_INVALID, "Current dynamic zone is readable");
    for (const double reference : {0., 812000000.25, 794656800., 805453200.5}) {
        const auto local = n::localEventLogTime(reference);
        check(local && local->month >= 1 && local->month <= 12, "The local converter yields civil fields");
        if (!current.DynamicDaylightTimeDisabled) check(n::eventLogTimeInZone(reference, current.TimeZoneKeyName) == local, "The local converter uses the current Windows zone");
    }
    check(!n::localEventLogTime(std::numeric_limits<double>::quiet_NaN()), "Unrepresentable dates have no invented time");
}
#endif
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Usage: event_log_time_tests utility-canvases-source.json");
        const auto fixture = load(argv[1]);
        const auto& times = fixture["eventLogTimes"];
        check(times.isArray() && times.array().size() >= 400, "The Mac time cases are complete");
        portable(times);
#ifdef _WIN32
        windows(times);
#endif
        std::cout << "Event Log time contracts passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Event Log time contracts failed: " << e.what() << '\n';
        return 1;
    }
}
