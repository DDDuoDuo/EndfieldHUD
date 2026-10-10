// The section 9 gate runner against the packaging stand-in app (never the
// real EndfieldHUD): a quiet process passes, a process that leaks one handle
// and one GDI object per open/close cycle and a process that wakes every
// 10 ms while open are caught, the measured process is stopped through the
// session-end protocol, and an executable without --data-root support is
// never started.
//
//   packaging_perf_probe_tests <fixture app>
#include "core/data/json.hpp"
#include "packaging/perf_probe.hpp"
#include "packaging/perf_runner.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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
    o.closedLimits = {2.0, 10.0, std::nullopt, 4.0 * 1024 * 1024};
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

void contracts(const fs::path& fixture) {
    // Unsupported executables are never started.
    check(!p::supportsGateControl(fixture), "The stand-in app has no --data-root support");
    auto refused = quick(fixture);
    refused.target = p::GateOptions::Target::endfieldhud;
    bool thrown{};
    try { (void)p::runPerformanceGate(refused); } catch (const std::runtime_error&) { thrown = true; }
    check(thrown, "An EndfieldHUD.exe without a temporary data root is refused");

    // Quiet: every gate passes and the report describes the run.
    auto run = p::runPerformanceGate(quick(fixture));
    auto report = Json::parse(run.report, 64 * 1024 * 1024);
    check(run.passed && report["passed"].boolean() && run.failures.empty(), "A quiet process passes: " + run.report.substr(0, 4000));
    check(report["format"].string() == "EndfieldHUD.Windows.PerformanceGate" && !report["machine"]["os"].string().empty() &&
          report["machine"]["logicalProcessors"].integer() > 0 && report["target"]["kind"].string() == "fixture" &&
          report["target"]["sha256"].string().size() == 64, "The report records the machine and the target");
    const auto* closed = scenario(report, "closed-idle");
    const auto* open = scenario(report, "open-idle");
    const auto* cycles = scenario(report, "lifecycle-25");
    check(closed && open && cycles, "Closed, open and lifecycle scenarios");
    check((*closed)["summary"]["wakeupsPerSecond"].number() < 10 && (*closed)["summary"]["seconds"].number() >= 2, "A quiet closed process barely wakes");
    check((*cycles)["cycles"].array().size() == 25 && (*cycles)["growth"].array().size() >= 6, "One sample per cycle and every growth metric");

    // One handle and one GDI object leaked per cycle.
    auto leaking = quick(fixture);
    leaking.fixtureArguments = {L"--leak"};
    leaking.closed = leaking.open = false;
    run = p::runPerformanceGate(leaking);
    report = Json::parse(run.report, 64 * 1024 * 1024);
    cycles = scenario(report, "lifecycle-25");
    check(!run.passed && cycles && !gatePassed(*cycles, "handles-growth") && !gatePassed(*cycles, "gdiObjects-growth") && gatePassed(*cycles, "threads-growth"),
          "Per-cycle growth is caught: " + run.report.substr(0, 4000));

    // A 10 ms wake-up while open.
    auto busy = quick(fixture);
    busy.fixtureArguments = {L"--busy"};
    busy.lifecycle = false;
    run = p::runPerformanceGate(busy);
    report = Json::parse(run.report, 64 * 1024 * 1024);
    closed = scenario(report, "closed-idle");
    open = scenario(report, "open-idle");
    check(!run.passed && closed && open && gatePassed(*closed, "wakeups") && !gatePassed(*open, "wakeups"), "Open-state wake-ups are caught");
    const auto& busiest = (*open)["busiestThreads"].array();
    check(!busiest.empty() && busiest.front()["wakeupsPerSecond"].number() >= 30 && busiest.front()["start"].string().find("packaging_fixture_app") != std::string::npos,
          "The waking thread is attributed to its module: " + Json((*open)["busiestThreads"]).encode());
    std::cout << "Performance gate runner: " << checks << " checks passed\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) { std::cerr << "usage: packaging_perf_probe_tests <fixture app>\n"; return 2; }
        contracts(fs::path(argv[1]).make_preferred());
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Performance gate runner failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
