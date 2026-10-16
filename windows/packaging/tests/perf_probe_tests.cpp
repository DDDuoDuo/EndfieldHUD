// The section 9 gate runner against the packaging stand-in app (never the
// real EndfieldHUD): a quiet process passes, a process that leaks one handle
// and one GDI object per open/close cycle and a process that wakes every
// 10 ms while open are caught, the measured process is stopped through the
// session-end protocol, and an executable without --data-root support is
// never started. The startup trace reaches the app through the environment
// and its cold/warm openings are gated (a slow cold opening is caught), a
// cancelled run stops at once with a partial report, a fatal error record
// ends the run instead of being measured as idle, the readable report is
// rendered, the measured process never outlives a killed gate tool, and the
// footprint (memory held while open, threads grouped by start and the
// Windows thread pool recognized) is reported first and gated. The closed
// HUD is measured on both sides of the graphics grace: a stand-in device
// that wakes the process passes the higher bound within the grace, the
// release launch shortens the grace through ENDFIELDHUD_CLOSED_GRAPHICS_GRACE
// and passes the strict bound after the release, and a device kept past the
// grace fails it.
//
//   packaging_perf_probe_tests <fixture app> <package_perf_gate.exe>
#include "core/data/json.hpp"
#include "packaging/perf_probe.hpp"
#include "packaging/perf_report.hpp"
#include "packaging/perf_runner.hpp"
#include <cmath>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace p = endfield::packaging;
namespace fs = std::filesystem;
using ehud::data::Json;

namespace {
unsigned checks{};
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
p::GateOptions quick(const fs::path& fixture) {
    p::GateOptions o;
    o.executable = fixture;
    o.target = p::GateOptions::Target::fixture;
    o.seconds = 2;
    o.interval = 0.25;
    o.settle = 0.3;
    o.openSettle = 0.05;
    o.closeSettle = 0.05;
    o.cycles = 25;
    o.ambientOff = false;
    o.graceLimits = {2.0, 10.0, std::nullopt, 4.0 * 1024 * 1024};
    o.releasedLimits = {2.0, 10.0, std::nullopt, 4.0 * 1024 * 1024};
    o.releaseGrace = 1;
    o.openLimits = {2.0, 20.0, std::nullopt, 4.0 * 1024 * 1024};
    o.lifecycleLimits.warmup = 5;
    o.lifecycleLimits.window = 5;
    o.lifecycleLimits.handles = 8;
    o.lifecycleLimits.gdiObjects = 8;
    return o;
}
const Json* scenario(const Json& report, std::string_view name) {
    for (const auto& s : report["scenarios"].array()) if (s["name"].string() == name) return &s;
    return nullptr;
}
bool gatePassed(const Json& s, std::string_view gate) {
    for (const auto& g : s["gates"].array()) if (g["name"].string() == gate) return g["passed"].boolean();
    throw std::runtime_error("Missing gate " + std::string(gate));
}

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }
const Json* footprintPoint(const Json& report, std::string_view name) {
    const auto* footprint = scenario(report, "footprint");
    if (!footprint) return nullptr;
    for (const auto& q : (*footprint)["points"].array()) if (q["point"].string() == name) return &q;
    return nullptr;
}
std::int64_t threadGroup(const Json& point, std::string_view name) {
    std::int64_t count{};
    for (const auto& g : point["threadGroups"].array()) if (g["name"].string() == name) count += g["count"].integer();
    return count;
}
// A file that starts like an executable and holds `contents`.
bool imageSupportsGrace(const fs::path& file, const std::string& contents) {
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << "MZ" << contents;
    }
    const bool supported = p::supportsGraceOverride(file);
    std::error_code ignored;
    fs::remove(file, ignored);
    return supported;
}
const Json* opening(const Json& openings, bool cold) {
    for (const auto& o : openings["openings"].array()) if (o["cold"].boolean() == cold) return &o;
    return nullptr;
}
bool openingGate(const Json& o, std::string_view gate) {
    for (const auto& g : o["gates"].array()) if (g["name"].string() == gate) return g["passed"].boolean();
    throw std::runtime_error("Missing opening gate " + std::string(gate));
}
// The gate tool killed while it measures: its kill-on-close job ends the
// measured stand-in too (it would otherwise run on for five minutes).
void killedTool(const fs::path& fixture, const fs::path& tool) {
    const auto work = fs::temp_directory_path() / ("ehud-perf-killed-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(work);
    const auto command = p::commandLine(tool, {L"--target", L"fixture", L"--exe", fixture.native(), L"--report", (work / L"killed.json").native(),
                                               L"--scenarios", L"closed", L"--seconds", L"60", L"--interval", L"0.5", L"--settle", L"0.2",
                                               L"--work-folder", work.native()});
    auto mutableCommand = command;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    check(CreateProcessW(tool.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info) != FALSE,
          "The gate tool starts");
    CloseHandle(info.hThread);
    HANDLE child{};
    for (int attempt = 0; attempt < 100 && !child; ++attempt) {
        Sleep(100);
        const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W entry{sizeof(entry)};
        for (auto more = Process32FirstW(snapshot, &entry); more && !child; more = Process32NextW(snapshot, &entry))
            if (entry.th32ParentProcessID == info.dwProcessId && _wcsicmp(entry.szExeFile, fixture.filename().c_str()) == 0)
                child = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        CloseHandle(snapshot);
    }
    check(child != nullptr, "The gate tool started the measured stand-in");
    Sleep(500);
    TerminateProcess(info.hProcess, 9);
    WaitForSingleObject(info.hProcess, 10000);
    CloseHandle(info.hProcess);
    const bool ended = WaitForSingleObject(child, 10000) == WAIT_OBJECT_0;
    if (!ended) TerminateProcess(child, 1);
    CloseHandle(child);
    check(ended, "The measured process ends with a killed gate tool (kill-on-close job)");
    std::error_code ignored;
    for (int attempt = 0; attempt < 20 && fs::exists(work); ++attempt) {
        fs::remove_all(work, ignored);
        if (fs::exists(work)) Sleep(250);
    }
}

void contracts(const fs::path& fixture, const fs::path& tool) {
    // Unsupported executables are never started.
    check(!p::supportsGateControl(fixture), "The stand-in app has no --data-root support");
    auto refused = quick(fixture);
    refused.target = p::GateOptions::Target::endfieldhud;
    bool thrown{};
    try { (void)p::runPerformanceGate(refused); } catch (const std::runtime_error&) { thrown = true; }
    check(thrown, "An EndfieldHUD.exe without a temporary data root is refused");
    // The grace override is recognized by its name in the image (UTF-16 as
    // GetEnvironmentVariableW reads it, or 8-bit); without it the release
    // launch waits the production ten minutes.
    const auto image = fs::temp_directory_path() / ("ehud-perf-image-" + std::to_string(GetCurrentProcessId()) + ".exe");
    std::string wideName;
    for (const char c : std::string(p::graceOverrideVariable)) { wideName.push_back(c); wideName.push_back('\0'); }
    check(p::supportsGraceOverride(fixture) && !imageSupportsGrace(image, "--data-root --toggle") && imageSupportsGrace(image, "--data-root" + wideName) &&
          imageSupportsGrace(image, std::string(p::graceOverrideVariable)) && !p::supportsGraceOverride(image),
          "The grace override is recognized in the image");
    auto noOverride = quick(fixture);
    noOverride.executable = image;
    check(p::releaseWaitGrace(noOverride) == p::productionGraphicsGraceSeconds && p::releaseWaitGrace(quick(fixture)) == 1 &&
          std::abs(p::estimatedGateSeconds(noOverride) - p::estimatedGateSeconds(quick(fixture)) - (p::productionGraphicsGraceSeconds - 1)) < 1e-6,
          "Without the override the release launch waits the production grace");

    // Quiet: every gate passes and the report describes the run.
    auto run = p::runPerformanceGate(quick(fixture));
    auto report = Json::parse(run.report, 64 * 1024 * 1024);
    check(run.passed && report["passed"].boolean() && run.failures.empty(), "A quiet process passes: " + run.report.substr(0, 4000));
    check(report["format"].string() == "EndfieldHUD.Windows.PerformanceGate" && !report["machine"]["os"].string().empty() &&
          report["machine"]["logicalProcessors"].integer() > 0 && report["target"]["kind"].string() == "fixture" &&
          report["target"]["sha256"].string().size() == 64, "The report records the machine and the target");
    const auto* closed = scenario(report, "closed-within-grace");
    const auto* released = scenario(report, "closed-after-release");
    const auto* open = scenario(report, "open-idle");
    const auto* cycles = scenario(report, "lifecycle-25");
    check(closed && released && open && cycles, "Closed (within the grace and after the release), open and lifecycle scenarios");
    check(report["scenarios"].array().size() >= 3 && report["scenarios"].array()[1]["name"].string() == "closed-within-grace" &&
          report["scenarios"].array()[2]["name"].string() == "closed-after-release", "Both closed windows follow the footprint, side by side");
    check((*closed)["summary"]["wakeupsPerSecond"].number() < 10 && (*closed)["summary"]["seconds"].number() >= 2 &&
          (*released)["summary"]["wakeupsPerSecond"].number() < 10, "A quiet closed process barely wakes");
    const auto& withinState = (*closed)["closed"];
    const auto& releasedState = (*released)["closed"];
    check(withinState["state"].string() == "within-grace" && withinState["since"].string() == "launch" && withinState["graceSeconds"].number() == 600 &&
          !withinState["graceOverride"].boolean() && withinState["closedSeconds"].number() >= 0.3 && withinState["closedSeconds"].number() < 30 &&
          releasedState["state"].string() == "after-release" && releasedState["since"].string() == "close" && releasedState["graceSeconds"].number() == 1 &&
          releasedState["graceOverride"].boolean() && releasedState["closedSeconds"].number() >= 1.2 && releasedState["closedSeconds"].number() < 30 &&
          releasedState["productionGraceSeconds"].number() == 600,
          "Each closed window records its side of the graphics grace: " + Json(withinState).encode() + " " + Json(releasedState).encode());
    check(report["settings"]["releaseGraceSeconds"].number() == 1 && report["settings"]["graceOverride"].boolean() &&
          report["settings"]["productionGraphicsGraceSeconds"].number() == 600, "The settings record the release grace");
    check((*cycles)["cycles"].array().size() == 25 && (*cycles)["growth"].array().size() >= 6, "One sample per cycle and every growth metric");
    check(report["schema"].integer() == 3 && report["cancelled"].isBool() && !report["cancelled"].boolean() && report["machine"]["interactiveDesktop"].isBool() &&
          report["machine"]["otherEndfieldHUDProcesses"].isNumber(), "The report records the session kind and other EndfieldHUD processes");
    // ENDFIELDHUD_STARTUP_TRACE=1 reached the stand-in through the gate's
    // environment block: its trace was read back and both openings gated.
    const auto* openings = scenario(report, "openings");
    check(openings && (*openings)["passed"].boolean() && (*openings)["openings"].array().size() == 2 && report["startupTrace"].isObject(),
          "The startup trace's cold and warm openings are reported: " + run.report.substr(0, 4000));
    const auto* cold = opening(*openings, true);
    const auto* warm = opening(*openings, false);
    check(cold && warm && (*cold)["firstFrameMilliseconds"].number() == 12.5 && (*cold)["frames"].integer() == 36 &&
          std::abs((*cold)["p95IntervalMilliseconds"].number() - 16.7) < 1e-9 && (*cold)["lateFrames"].integer() == 0 &&
          openingGate(*cold, "first-frame") && openingGate(*warm, "late-frames") && (*openings)["launchToFirstFrameMilliseconds"].number() == 180.25,
          "Opening summaries come from the trace");
    const auto markdown = p::renderGateMarkdown(report);
    check(contains(markdown, "# EndfieldHUD performance gate: PASSED") && contains(markdown, "stand-in test program") &&
          contains(markdown, "## Opening the HUD (cold and warm)") && contains(markdown, "| Request to first frame | 12.5 ms | 12.5 ms |") &&
          contains(markdown, "## Open/close lifecycle (25 cycles)") && contains(markdown, "| renderer | 61.5 ms | 50.0 ms |") &&
          contains(markdown, "| fixture | 4.5 / 1.5 ms | 2.0 / 1.0 ms |") && !contains(markdown, "nan") && !contains(markdown, "inf "),
          "The readable report covers every scenario:\n" + markdown);
    check(contains(p::summarizeGateReport(report), "openings") && contains(p::summarizeGateReport(report), "cold: first frame ") &&
          contains(p::summarizeGateReport(report), "warm: first frame "),
          "The console summary lists the openings: " + p::summarizeGateReport(report));
    // The footprint comes first: closed after launch, open, closed after the
    // cycles and closed after the release, within the default limits for a
    // small process.
    const auto* quietFootprint = scenario(report, "footprint");
    check(quietFootprint && report["scenarios"].array().front()["name"].string() == "footprint" && (*quietFootprint)["passed"].boolean() &&
          (*quietFootprint)["points"].array().size() == 4 && (*quietFootprint)["gates"].array().size() == 8 && contains(markdown, "## Memory, threads and handles") &&
          contains(markdown, "| Measure | Closed after launch | Open | Closed after 25 cycles | Closed after the release |") &&
          contains(markdown, "| Closed, within the graphics grace | PASS |") && contains(markdown, "| Closed, after the graphics release | PASS |") &&
          contains(markdown, "| Graphics grace of the release launch | 1 s (ENDFIELDHUD_CLOSED_GRAPHICS_GRACE) |"),
          "The quiet process's footprint is reported first and passes: " + run.report.substr(0, 4000));

    // Footprint: three idle "fixture worker" threads and one waiting pool
    // worker, and 48 MiB held while open (released on close).
    auto held = quick(fixture);
    held.fixtureArguments = {L"--threads", L"3", L"--hold-mib", L"48"};
    held.startupTrace = false;
    held.openFootprint.privateBytes = 32.0 * 1024 * 1024;
    run = p::runPerformanceGate(held);
    report = Json::parse(run.report, 64 * 1024 * 1024);
    const auto* footprint = scenario(report, "footprint");
    const auto point = [&](std::string_view name) -> const Json* {
        if (!footprint) return nullptr;
        for (const auto& q : (*footprint)["points"].array()) if (q["point"].string() == name) return &q;
        return nullptr;
    };
    const auto* closedPoint = point("closed");
    const auto* openPoint = point("open");
    const auto* afterPoint = point("after-cycles");
    check(closedPoint && openPoint && afterPoint, "Every footprint point: " + run.report.substr(0, 4000));
    constexpr double mib = 1024.0 * 1024.0;
    const auto bytes = [](const Json& q, const char* key) { return q[key].number(); };
    check(bytes(*openPoint, "privateBytes") - bytes(*closedPoint, "privateBytes") >= 44 * mib &&
          bytes(*openPoint, "workingSetBytes") - bytes(*closedPoint, "workingSetBytes") >= 44 * mib &&
          (*openPoint)["memory"]["largestPrivateAllocations"].array().front().number() >= 48 * mib &&
          (*openPoint)["memory"]["privateCommittedBytes"].number() >= 48 * mib && bytes(*openPoint, "peakPrivateBytes") >= bytes(*openPoint, "privateBytes") &&
          bytes(*afterPoint, "privateBytes") <= bytes(*openPoint, "privateBytes") - 44 * mib && bytes(*afterPoint, "peakPrivateBytes") >= 48 * mib,
          "Memory held while open is measured, its allocation listed, and it is gone after the cycles: " + Json(*footprint).encode(1 << 20).substr(0, 4000));
    bool workers{}, pool{};
    for (const auto& g : (*closedPoint)["threadGroups"].array()) {
        if (g["name"].string() == "fixture worker")
            workers = g["count"].integer() == 3 && g["module"].string() == fixture.filename().string() && g["role"].string() == "the program's own threads" &&
                      contains(g["start"].string(), fixture.filename().string() + "+0x");
        if (g["role"].string() == "Windows thread pool") pool = g["count"].integer() >= 1 && _stricmp(g["module"].string().c_str(), "ntdll.dll") == 0;
    }
    check(workers && pool && (*closedPoint)["threads"].integer() >= 5,
          "Threads are grouped by start and description, the pool worker recognized: " + Json((*closedPoint)["threadGroups"]).encode(1 << 20).substr(0, 4000));
    bool openFailed{}, closedPassed{};
    for (const auto& g : (*footprint)["gates"].array()) {
        if (g["name"].string() == "open.private-bytes") openFailed = !g["passed"].boolean();
        if (g["name"].string() == "closed.private-bytes") closedPassed = g["passed"].boolean();
    }
    bool listed{};
    for (const auto& f : run.failures) listed = listed || f.starts_with("footprint: Private bytes, open ");
    check(!run.passed && openFailed && closedPassed && listed && !(*footprint)["passed"].boolean(), "An open footprint over its limit fails the gate");
    const auto heldMarkdown = p::renderGateMarkdown(report);
    check(contains(heldMarkdown, "| fixture worker | 3 | 3 | 3 | 3 |") && contains(heldMarkdown, "| Windows thread pool | - |") &&
          contains(heldMarkdown, "| Private bytes, open | ") && contains(heldMarkdown, "Kept after use (closed after 25 cycles minus closed after launch): "),
          "The readable report lists the footprint and the threads:\n" + heldMarkdown);

    // The graphics grace: a stand-in device that wakes about 100 times a
    // second exists from the start and goes only after the grace. The first
    // launch keeps the production grace, so its closed window sees the
    // device and passes the higher bound; the release launch shortens the
    // grace to 1 s and its window after the release passes the strict one.
    auto device = quick(fixture);
    device.fixtureArguments = {L"--device", L"100"};
    device.open = device.lifecycle = false;
    device.startupTrace = false;
    device.graceLimits = {2.0, 200.0, std::nullopt, 4.0 * 1024 * 1024};
    run = p::runPerformanceGate(device);
    report = Json::parse(run.report, 64 * 1024 * 1024);
    closed = scenario(report, "closed-within-grace");
    released = scenario(report, "closed-after-release");
    check(run.passed && closed && released && (*closed)["summary"]["wakeupsPerSecond"].number() >= 30 && gatePassed(*closed, "wakeups") &&
          (*released)["summary"]["wakeupsPerSecond"].number() < 10 && gatePassed(*released, "wakeups") && !(*closed)["busiestThreads"].array().empty() &&
          (*closed)["busiestThreads"].array().front()["name"].string() == "fixture graphics device",
          "The device wakes the HUD within the grace and is gone after the release: " + run.report.substr(0, 4000));
    const auto* withinPoint = footprintPoint(report, "closed");
    const auto* releasedPoint = footprintPoint(report, "after-release");
    check(withinPoint && releasedPoint && threadGroup(*withinPoint, "fixture graphics device") == 1 && threadGroup(*releasedPoint, "fixture graphics device") == 0,
          "The device's thread shows in the census within the grace only");
    const auto deviceMarkdown = p::renderGateMarkdown(report);
    check(contains(deviceMarkdown, "| Closed, within the graphics grace | PASS |") && contains(deviceMarkdown, "| Closed, after the graphics release | PASS |") &&
          contains(deviceMarkdown, "within its graphics grace of 600 s.") &&
          contains(deviceMarkdown, "past its graphics grace of 1 s (shortened for this run through `ENDFIELDHUD_CLOSED_GRAPHICS_GRACE`") &&
          contains(deviceMarkdown, "Given back by the graphics release (closed after the release minus closed after launch, within the grace; two launches of "
                                   "the same program): ") &&
          contains(deviceMarkdown, "| fixture graphics device | 1 | 0 |"),
          "The readable report explains both closed windows:\n" + deviceMarkdown);
    // A device kept past the grace fails the strict bound only.
    device.fixtureArguments = {L"--device", L"100", L"--keep-device"};
    run = p::runPerformanceGate(device);
    report = Json::parse(run.report, 64 * 1024 * 1024);
    closed = scenario(report, "closed-within-grace");
    released = scenario(report, "closed-after-release");
    bool keptListed{};
    for (const auto& f : run.failures) keptListed = keptListed || f.starts_with("closed-after-release: Wake-ups ");
    const auto* keptPoint = footprintPoint(report, "after-release");
    check(!run.passed && closed && released && gatePassed(*closed, "wakeups") && !gatePassed(*released, "wakeups") && keptListed && keptPoint &&
          threadGroup(*keptPoint, "fixture graphics device") == 1,
          "A device kept past the grace fails the window after the release: " + run.report.substr(0, 4000));

    // A slow cold opening: slow first frame and five late frames.
    auto slow = quick(fixture);
    slow.fixtureArguments = {L"--slow-open"};
    slow.closed = slow.open = slow.released = false;
    run = p::runPerformanceGate(slow);
    report = Json::parse(run.report, 64 * 1024 * 1024);
    openings = scenario(report, "openings");
    cold = openings ? opening(*openings, true) : nullptr;
    warm = openings ? opening(*openings, false) : nullptr;
    check(!run.passed && openings && !(*openings)["passed"].boolean() && cold && warm && !openingGate(*cold, "first-frame") &&
          !openingGate(*cold, "late-frames") && !openingGate(*cold, "p95-frame-interval") && openingGate(*warm, "first-frame") &&
          (*cold)["lateFrames"].integer() == 5, "A slow cold opening is caught: " + run.report.substr(0, 4000));
    bool named{};
    for (const auto& f : run.failures) named = named || f == "cold opening: Request to first frame 412.5 ms (limit 250.0 ms)";
    check(named, "The failure names the cold first frame");
    check(contains(p::renderGateMarkdown(report), "| Cold: Request to first frame | 412.5 ms | 250.0 ms | FAIL |"), "The readable report shows the failed opening gate");

    // A program that hit a fatal error (EndfieldHUD's message box keeps it
    // alive and idle) is not measured: its record ends the run.
    auto fatal = quick(fixture);
    fatal.fixtureArguments = {L"--fail-start"};
    run = p::runPerformanceGate(fatal);
    report = Json::parse(run.report, 64 * 1024 * 1024);
    bool reported{};
    for (const auto& f : run.failures)
        reported = reported || f == "The measured program stopped with a fatal error during startup (exception: class std::runtime_error; "
                                    "message: Cannot locate the current display; stage: launch > application start)";
    check(!run.passed && reported && report["scenarios"].array().empty(), "A fatal error ends the run with its record: " + run.report.substr(0, 2000));

    // Cancelled before the first measurement: stops at once, partial report.
    const auto cancel = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    auto cancelled = quick(fixture);
    cancelled.cancelEvent = cancel;
    cancelled.settle = 30;
    const auto started = p::gateClock();
    run = p::runPerformanceGate(cancelled);
    CloseHandle(cancel);
    report = Json::parse(run.report, 64 * 1024 * 1024);
    check(run.cancelled && !run.passed && report["cancelled"].boolean() && report["scenarios"].array().empty() && p::gateClock() - started < 20,
          "A cancelled run stops at once with a partial report: " + run.report.substr(0, 2000));
    check(contains(p::renderGateMarkdown(report), "# EndfieldHUD performance gate: CANCELLED"), "The readable report says the run was cancelled");

    // One handle and one GDI object leaked per cycle.
    auto leaking = quick(fixture);
    leaking.fixtureArguments = {L"--leak"};
    leaking.closed = leaking.open = leaking.released = false;
    run = p::runPerformanceGate(leaking);
    report = Json::parse(run.report, 64 * 1024 * 1024);
    cycles = scenario(report, "lifecycle-25");
    check(!run.passed && cycles && !gatePassed(*cycles, "handles-growth") && !gatePassed(*cycles, "gdiObjects-growth") && gatePassed(*cycles, "threads-growth"),
          "Per-cycle growth is caught: " + run.report.substr(0, 4000));

    // A 10 ms wake-up while open.
    auto busy = quick(fixture);
    busy.fixtureArguments = {L"--busy"};
    busy.lifecycle = false;
    busy.startupTrace = false;
    run = p::runPerformanceGate(busy);
    report = Json::parse(run.report, 64 * 1024 * 1024);
    closed = scenario(report, "closed-within-grace");
    released = scenario(report, "closed-after-release");
    open = scenario(report, "open-idle");
    check(!run.passed && closed && released && open && gatePassed(*closed, "wakeups") && gatePassed(*released, "wakeups") && !gatePassed(*open, "wakeups"),
          "Open-state wake-ups are caught");
    check(!scenario(report, "openings") && report["startupTrace"].isNull(), "Without the startup trace no opening scenario is reported");
    const auto& busiest = (*open)["busiestThreads"].array();
    check(!busiest.empty() && busiest.front()["wakeupsPerSecond"].number() >= 30 && busiest.front()["start"].string().find("packaging_fixture_app") != std::string::npos,
          "The waking thread is attributed to its module: " + Json((*open)["busiestThreads"]).encode());
    killedTool(fixture, tool);
    std::cout << "Performance gate runner: " << checks << " checks passed\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) { std::cerr << "usage: packaging_perf_probe_tests <fixture app> <package_perf_gate.exe>\n"; return 2; }
        contracts(fs::path(argv[1]).make_preferred(), fs::path(argv[2]).make_preferred());
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Performance gate runner failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
