// Reports the Windows text and time-zone data EndfieldHUD compares against its
// Mac oracles, and checks it against windows/packaging/os-requirements.json.
//
// EndfieldHUD links the operating system's ICU (icu.dll), so grapheme
// segmentation (Event Log name compaction) and time-zone rules (Archive dates)
// follow the Windows build. The oracles were generated on macOS with Unicode
// 15.1 grapheme rules (Indic conjunct breaks) and post-2021 Samoa rules; an
// older Windows image (e.g. the windows-2022 runner) fails those suites for
// that reason alone. This tool names the cause before the suites run:
//
//   package_os_capabilities [--requirements <os-requirements.json>]
//
// Exit 0 when every requirement holds, 1 when one does not, 2 for usage.
// Read only: it queries icu.dll and RtlGetVersion and changes nothing.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <icu.h>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct Requirements {
    std::vector<int> unicode;   // minimum Unicode version, e.g. {15, 1}
    std::string timeZoneData;   // minimum tzdata release, e.g. "2021b"
    bool samoaWithoutDaylightSaving{};
};
// Minimal reader for the three fields of os-requirements.json (no JSON
// library is linked into this standalone tool).
std::optional<std::string> field(const std::string& json, const std::string& name) {
    const auto key = "\"" + name + "\"";
    auto at = json.find(key);
    if (at == std::string::npos) return std::nullopt;
    at = json.find(':', at + key.size());
    if (at == std::string::npos) return std::nullopt;
    ++at;
    while (at < json.size() && (json[at] == ' ' || json[at] == '\n' || json[at] == '\r' || json[at] == '\t')) ++at;
    if (at < json.size() && json[at] == '"') {
        const auto end = json.find('"', at + 1);
        if (end == std::string::npos) return std::nullopt;
        return json.substr(at + 1, end - at - 1);
    }
    const auto end = json.find_first_of(",}\r\n", at);
    return json.substr(at, end == std::string::npos ? std::string::npos : end - at);
}
std::vector<int> versionParts(const std::string& text) {
    std::vector<int> parts;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, '.')) parts.push_back(std::stoi(item));
    return parts;
}
bool atLeast(const std::vector<int>& actual, const std::vector<int>& minimum) {
    for (std::size_t n = 0; n < minimum.size(); ++n) {
        const int a = n < actual.size() ? actual[n] : 0;
        if (a != minimum[n]) return a > minimum[n];
    }
    return true;
}
std::string text(const UVersionInfo v, int parts) {
    std::string out;
    for (int n = 0; n < parts; ++n) out += (n ? "." : "") + std::to_string(v[n]);
    return out;
}
// Daylight-saving offset of Pacific/Apia on 2026-10-08 12:00 UTC (Samoa
// abolished daylight saving in 2021; older data still applies it).
std::optional<int> samoaDaylightOffset() {
    UErrorCode status = U_ZERO_ERROR;
    const UChar zone[] = u"Pacific/Apia";
    UCalendar* calendar = ucal_open(zone, -1, "en_US", UCAL_GREGORIAN, &status);
    if (U_FAILURE(status) || !calendar) return std::nullopt;
    ucal_setMillis(calendar, 1791460800000.0, &status);
    const int offset = ucal_get(calendar, UCAL_DST_OFFSET, &status);
    ucal_close(calendar);
    if (U_FAILURE(status)) return std::nullopt;
    return offset;
}
} // namespace

int main(int argc, char** argv) {
    std::optional<std::string> path;
    for (int n = 1; n < argc; ++n) {
        const std::string a = argv[n];
        if (a == "--requirements" && n + 1 < argc) path = argv[++n];
        else { std::cerr << "Usage: package_os_capabilities [--requirements <os-requirements.json>]\n"; return 2; }
    }
    using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW os{sizeof os};
    if (const auto get = reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) get(&os);
    UVersionInfo icu{}, unicode{};
    u_getVersion(icu);
    u_getUnicodeVersion(unicode);
    UErrorCode status = U_ZERO_ERROR;
    const char* tz = ucal_getTZDataVersion(&status);
    const std::string tzdata = U_SUCCESS(status) && tz ? tz : "";
    const auto samoa = samoaDaylightOffset();
    std::cout << "Windows " << os.dwMajorVersion << "." << os.dwMinorVersion << "." << os.dwBuildNumber << "; ICU " << text(icu, 2) << "; Unicode "
              << text(unicode, 3) << "; time-zone data " << (tzdata.empty() ? "unknown" : tzdata) << "; Pacific/Apia daylight offset on 2026-10-08: "
              << (samoa ? std::to_string(*samoa / 60000) + " min" : std::string("unknown")) << '\n';
    if (!path) return 0;
    std::ifstream input(*path, std::ios::binary);
    if (!input) { std::cerr << "Cannot read " << *path << '\n'; return 2; }
    const std::string json((std::istreambuf_iterator<char>(input)), {});
    Requirements r;
    if (const auto v = field(json, "minimumUnicode")) r.unicode = versionParts(*v);
    if (const auto v = field(json, "minimumTimeZoneData")) r.timeZoneData = *v;
    if (const auto v = field(json, "samoaWithoutDaylightSaving")) r.samoaWithoutDaylightSaving = *v == "true";
    if (r.unicode.empty() || r.timeZoneData.size() != 5) { std::cerr << "Invalid requirements in " << *path << '\n'; return 2; }
    std::vector<std::string> failures;
    std::vector<int> actualUnicode{unicode[0], unicode[1], unicode[2]};
    if (!atLeast(actualUnicode, r.unicode))
        failures.push_back("Unicode " + text(unicode, 3) + " is older than the oracle's grapheme rules (event_log_names_contracts needs Unicode " +
                           field(json, "minimumUnicode").value_or("") + ")");
    // tzdata releases sort as text: "2021b" < "2024a".
    if (tzdata.size() != 5 || tzdata < r.timeZoneData)
        failures.push_back("Time-zone data " + (tzdata.empty() ? std::string("unknown") : tzdata) + " is older than " + r.timeZoneData +
                           " (archive_date_contracts needs current Samoa rules)");
    if (r.samoaWithoutDaylightSaving && samoa != 0)
        failures.push_back("Pacific/Apia still observes daylight saving in this ICU (archive_date_contracts expects the 2021 rules)");
    for (const auto& f : failures) std::cerr << "Unsupported OS text data: " << f << '\n';
    if (!failures.empty()) {
        std::cerr << "Run the Windows suites on a Windows build with this ICU data (CI: windows-2025); the assertions are unchanged.\n";
        return 1;
    }
    std::cout << "OS text and time-zone data meet " << *path << '\n';
    return 0;
}
