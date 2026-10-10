#pragma once
// Section 9 gate runner (windows/tools/package_perf_gate.cpp and its tests).
//
// Target "endfieldhud" drives a real EndfieldHUD.exe from outside, ONLY when
// that executable supports a temporary data root ("--data-root <folder>")
// and the summon toggle ("--toggle"); otherwise nothing is started, so a run
// can never touch the user's data root, startup entry or running copy:
//
//   1. temporary data root %TEMP%\EndfieldHUD-perf-<id>\EndfieldHUD with
//      launchAtLogin off, onboarding done and ambient animation on
//   2. EndfieldHUD.exe --data-root <root> --login  (starts closed)
//      closed idle: <seconds> sampled after <settle>
//   3. relaunch with --data-root <root>           (reopen: the HUD opens)
//      open idle (ambient on)
//   4. --toggle closes; <cycles> x (--toggle open, --toggle close), one
//      sample per cycle after the close settled -> lifecycle growth
//   5. graceful stop through the session-end protocol (Restart Manager)
//   6. ambient animation off: launch, reopen, open idle, stop
//
// Target "fixture" drives packaging/tests/package_fixture_app.cpp the same
// way (its tests only).
#ifdef _WIN32
#include "packaging/perf_gate.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace endfield::packaging {
struct GateOptions {
    enum class Target { endfieldhud, fixture };
    std::filesystem::path executable;
    Target target{Target::endfieldhud};
    std::vector<std::wstring> fixtureArguments; // extra primary arguments (fixture)
    bool closed{true}, open{true}, ambientOff{true}, lifecycle{true};
    double seconds{60}, settle{10}, openSettle{1.5}, closeSettle{1.5}, interval{1.0};
    std::size_t cycles{100};
    IdleLimits closedLimits;
    IdleLimits openLimits{10.0, 400.0, 25.0, 8.0 * 1024 * 1024};
    IdleLimits ambientOffLimits{5.0, 200.0, 15.0, 8.0 * 1024 * 1024};
    LifecycleLimits lifecycleLimits;
    std::filesystem::path workFolder; // default: %TEMP%
    bool keepData{};
};
struct GateRun {
    std::string report; // JSON (EndfieldHUD.Windows.PerformanceGate)
    bool passed{};
    std::vector<std::string> failures;
};
// True when the executable's image contains both control arguments.
bool supportsGateControl(const std::filesystem::path& executable);
// Throws std::runtime_error when the target cannot be driven safely.
GateRun runPerformanceGate(const GateOptions&);
} // namespace endfield::packaging
#endif
