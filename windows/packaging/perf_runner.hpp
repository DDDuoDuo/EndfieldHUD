#pragma once
// Section 9 gate runner (windows/tools/package_perf_gate.cpp and its tests).
//
// Target "endfieldhud" drives a real EndfieldHUD.exe from outside, ONLY when
// that executable supports a temporary data root ("--data-root <folder>")
// and the summon toggle ("--toggle"); otherwise nothing is started, so a run
// can never touch the user's data root, startup entry or running copy:
//
//   1. temporary data root %TEMP%\EndfieldHUD-perf-<id>\EndfieldHUD (long
//      path form) with launchAtLogin off, onboarding done and ambient
//      animation on
//   2. EndfieldHUD.exe --data-root <root> --login  (starts closed), with
//      ENDFIELDHUD_STARTUP_TRACE=1 so the app times its own first two
//      openings (<root>\v1\Diagnostics\startup-trace.json, numbers only)
//      closed idle: <seconds> sampled after <settle>
//   3. relaunch with --data-root <root>           (reopen: the cold opening)
//      open idle (ambient on)
//   4. --toggle closes; <cycles> x (--toggle open, --toggle close), one
//      sample per cycle after the close settled -> lifecycle growth; the
//      first of these openings is the warm traced one
//   5. graceful stop through the session-end protocol (Restart Manager);
//      the startup trace is read -> the "openings" scenario
//   6. ambient animation off: launch, reopen, open idle, stop
//
// The measured process runs in a kill-on-close job object, so it never
// outlives the gate. A cancel event (Ctrl+C in the tool) stops the run at the
// next wait: the app is stopped through the session-end protocol, the
// temporary root is removed and the partial report says "cancelled".
//
// Target "fixture" drives packaging/tests/package_fixture_app.cpp the same
// way (its tests only); it writes a synthetic startup trace through the same
// environment variable.
#ifdef _WIN32
#include "packaging/perf_gate.hpp"
#include "packaging/perf_report.hpp"
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
    bool startupTrace{true}; // the "openings" scenario (needs open or lifecycle)
    double seconds{60}, settle{10}, openSettle{1.5}, closeSettle{1.5}, interval{1.0};
    std::size_t cycles{100};
    IdleLimits closedLimits;
    IdleLimits openLimits{10.0, 400.0, 25.0, 8.0 * 1024 * 1024};
    IdleLimits ambientOffLimits{5.0, 200.0, 15.0, 8.0 * 1024 * 1024};
    LifecycleLimits lifecycleLimits;
    // Provisional: the Mac 1.2 first synchronous open was 225 ms (Apple M2);
    // 25 ms is 1.5 x the 60 Hz frame budget (section 9: stable 60 fps).
    OpeningLimits coldOpenLimits{250.0, 25.0, 3.0};
    OpeningLimits warmOpenLimits{100.0, 25.0, 1.0};
    std::filesystem::path workFolder; // default: %TEMP% (long path form)
    bool keepData{};
    void* cancelEvent{}; // optional manual-reset event: set to cancel the run
};
struct GateRun {
    std::string report; // JSON (EndfieldHUD.Windows.PerformanceGate)
    bool passed{};
    bool cancelled{};
    std::vector<std::string> failures;
};
// True when the executable's image contains both control arguments.
bool supportsGateControl(const std::filesystem::path& executable);
// Seconds the run will take with these options (an estimate for the prompt).
double estimatedGateSeconds(const GateOptions&);
// Throws std::runtime_error when the target cannot be driven safely.
GateRun runPerformanceGate(const GateOptions&);
} // namespace endfield::packaging
#endif
