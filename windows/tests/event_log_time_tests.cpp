// EventLogCanvas.timestamp parity (Mac oracle: tools/event_log_timestamp_reference.sh
// -> tests/fixtures/event_log_timestamps.json). Synthetic instants only; the
// Windows time zone database is read, never changed.
#include "native/event_log_time.hpp"
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
using Json = ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
Json load(const char* path) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "Event Log timestamp fixture is readable");
    std::stringstream bytes; bytes << in.rdbuf();
    return Json::parse(bytes.str(), 4 * 1024 * 1024);
}
// Foundation reference seconds of 2025-01-01T00:00:00Z and 2028-03-02T00:00:00Z.
constexpr double modernStart = 757382400, modernEnd = 857260800;
bool pattern(const std::string& text) {
    if (text.size() != 14) return false;
    for (std::size_t n = 0; n < text.size(); ++n) {
        const char c = text[n];
        if (n == 2 ? c != '-' : n == 5 ? c != ' ' : n == 8 || n == 11 ? c != ':' : (c < '0' || c > '9')) return false;
    }
    return true;
}
void portable(const Json& fixture) {
    // Rounding contract: half up to whole milliseconds, then the floor second.
    check(n::eventLogTimestampSeconds(0) == 978307200, "Reference date 0 is 2001-01-01T00:00:00Z");
    check(n::eventLogTimestampSeconds(0.9994) == 978307200 && n::eventLogTimestampSeconds(0.9995) == 978307201, "A 999.5 ms remainder rounds up to the next second");
    check(n::eventLogTimestampSeconds(-0.0005) == 978307200 && n::eventLogTimestampSeconds(-0.0006) == 978307199, "Negative sub-millisecond values round before the floor");
    check(n::eventLogTimestampSeconds(-1.5) == 978307198, "Pre-2001 fractions take the floor second");
    check(!n::eventLogTimestampSeconds(std::numeric_limits<double>::quiet_NaN()) && !n::eventLogTimestampSeconds(std::numeric_limits<double>::infinity()), "Non-finite dates have no timestamp");
    check(!n::eventLogTimestampSeconds(1e18) && !n::eventLogTimestampSeconds(-1e18), "Dates outside the Windows calendar range have no timestamp");
    check(n::formatEventLogCivil(3, 8, 3, 0, 1) == "03-08 03:00:01" && n::formatEventLogCivil(12, 31, 23, 59, 59) == "12-31 23:59:59", "Civil fields format as en_US_POSIX MM-dd HH:mm:ss");
    check(n::formatEventLogCivil(13, 1, 0, 0, 0).empty() && n::formatEventLogCivil(1, 0, 0, 0, 0).empty() && n::formatEventLogCivil(1, 1, 24, 0, 0).empty(), "Invalid civil fields are rejected");
    // UTC and the fixed-offset zones need no daylight rules: every case,
    // including the 1970 and pre-2001 instants, follows from the seconds.
    const std::map<std::string, std::int64_t> fixed{{"UTC", 0}, {"Asia/Shanghai", 8 * 3600}, {"Asia/Kolkata", 5 * 3600 + 1800}};
    std::size_t compared{};
    for (const auto& item : fixture["cases"].array()) {
        const auto zone = fixed.find(item["zone"].string());
        if (zone == fixed.end()) continue;
        const auto seconds = n::eventLogTimestampSeconds(item["reference"].number());
        check(bool(seconds), "Fixture instants are representable");
        // Civil date from days since 1970-01-01 (proleptic Gregorian).
        const auto local = *seconds + zone->second;
        auto days = local / 86400; auto rest = local % 86400; if (rest < 0) { rest += 86400; --days; }
        days += 719468; const auto era = (days >= 0 ? days : days - 146096) / 146097; const auto doe = days - era * 146097;
        const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const auto mp = (5 * doy + 2) / 153; const auto day = doy - (153 * mp + 2) / 5 + 1; const auto month = mp < 10 ? mp + 3 : mp - 9;
        const auto text = n::formatEventLogCivil(unsigned(month), unsigned(day), unsigned(rest / 3600), unsigned(rest / 60 % 60), unsigned(rest % 60));
        check(text == item["text"].string(), "Fixed-offset timestamp matches the Mac formatter: " + item["zone"].string() + " " + item["text"].string());
        ++compared;
    }
    check(compared >= 60, "Fixed-offset fixture cases are present");
}
#ifdef _WIN32
std::wstring wide(const std::string& value) {
    std::wstring out(value.size(), L'\0');
    for (std::size_t n = 0; n < value.size(); ++n) out[n] = static_cast<unsigned char>(value[n]);
    return out;
}
void windows(const Json& fixture) {
    std::size_t modern{}, historical{}, historicalDifferent{};
    std::map<std::string, std::size_t> zones;
    for (const auto& item : fixture["cases"].array()) {
        const auto reference = item["reference"].number();
        const auto key = item["windowsZone"].string();
        const auto text = n::eventLogTimestampInZone(reference, wide(key));
        check(bool(text), "Windows time zone is installed: " + key);
        check(pattern(*text), "Windows timestamp has the MM-dd HH:mm:ss shape");
        const bool fixedOffset = key == "UTC" || key == "China Standard Time" || key == "India Standard Time";
        if (fixedOffset || (reference >= modernStart && reference < modernEnd)) {
            // Every DST switch of 2025...2027 (and the seconds around it) in
            // the nine zones, including 30- and 45-minute offsets.
            check(*text == item["text"].string(), "Windows dynamic time zone matches the Mac formatter: " + key + " @" + std::to_string(reference) + " expected " + item["text"].string() + " got " + *text);
            ++modern; ++zones[key];
        } else {
            // Older instants depend on per-zone history that the Windows
            // registry does not carry (for example the 1968-71 British
            // Standard Time). They stay well-formed and are reported only.
            ++historical;
            if (*text != item["text"].string()) ++historicalDifferent;
        }
    }
    check(zones.size() == 9 && modern >= 300, "All nine zones are compared over the modern range");
    std::cout << "historical instants differing from tzdb: " << historicalDifferent << " of " << historical << '\n';
    check(!n::eventLogTimestampInZone(0, L"Not A Real Zone"), "Unknown zone keys are rejected");
    // Production formatter: the current zone, identical to the explicit one.
    DYNAMIC_TIME_ZONE_INFORMATION current{};
    check(GetDynamicTimeZoneInformation(&current) != TIME_ZONE_ID_INVALID, "Current dynamic zone is readable");
    for (const double reference : {0., 812000000.25, 794656800., 805453200.5}) {
        const auto local = n::localEventLogTimestamp(reference);
        check(pattern(local), "Local formatter yields MM-dd HH:mm:ss");
        if (!current.DynamicDaylightTimeDisabled) {
            const auto explicitZone = n::eventLogTimestampInZone(reference, current.TimeZoneKeyName);
            check(explicitZone && *explicitZone == local, "Local formatter uses the current Windows zone");
        }
    }
    check(n::localEventLogTimestamp(std::numeric_limits<double>::quiet_NaN()) == "--", "Unrepresentable dates show a placeholder, not an invented time");
}
#endif
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Usage: event_log_time_tests event_log_timestamps.json");
        const auto fixture = load(argv[1]);
        check(fixture["format"].string() == "en_US_POSIX MM-dd HH:mm:ss" && fixture["cases"].array().size() >= 400, "Mac timestamp fixture is complete");
        portable(fixture);
#ifdef _WIN32
        windows(fixture);
#endif
        std::cout << "Event Log timestamp contracts passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Event Log timestamp contracts failed: " << e.what() << '\n';
        return 1;
    }
}
