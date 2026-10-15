#pragma once
// Section 9 performance gate: opening timings and the readable report
// (portable; windows/tools/package_perf_gate.cpp, packaging/perf_runner.hpp).
//
// Openings come from EndfieldHUD.exe's own opt-in startup trace
// (ENDFIELDHUD_STARTUP_TRACE=1 writes <data root>\v1\Diagnostics\
// startup-trace.json once the first two openings completed, or at exit with
// what was recorded; numbers only, app/application_impl.hpp StartupTrace).
// Opening 0 is the cold first opening after launch, opening 1 a warm one.
// Per presented frame the trace holds the wall time since the opening
// request and the CPU time of present(). WINDOWS-MIGRATION.md section 9 asks
// for the first and the warmed opening separately and for p50/p95 frame
// times, so each opening is summarized as:
//
//   first frame   request -> first presented frame (ms)
//   opened        request -> the source entrance completed (ms)
//   p50/p95/max   intervals between consecutive presented frames (ms),
//                 excluding the request -> first frame step
//   late frames   those intervals over 25 ms (1.5 x the 60 Hz budget)
//   frame CPU     mean/max present() CPU, frames over 16.67 ms
//
// The limits are provisional engineering gates (GateOptions in
// perf_runner.hpp); every value is reported whether or not a limit is set.
#include "core/data/json.hpp"
#include "packaging/perf_gate.hpp"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::packaging {
inline constexpr double frameBudgetMilliseconds = 1000.0 / 60.0;
inline constexpr double lateIntervalMilliseconds = 25.0;

struct OpeningSummary {
    std::size_t index{};
    bool cold{};
    double requestedAtMS{-1};              // launch -> the opening request; -1: not in the trace
    double firstFrameMS{-1}, openedMS{-1}; // -1: not reached
    std::size_t frames{};
    double p50IntervalMS{}, p95IntervalMS{}, maxIntervalMS{};
    std::size_t lateFrames{};
    double meanFrameCPUMS{}, maxFrameCPUMS{};
    std::size_t framesOverBudget{};
};
struct OpeningLimits {
    std::optional<double> firstFrameMS, p95IntervalMS, lateFrames;
};

// Nearest-rank percentile (fraction 0...1) of a non-empty list.
double percentile(std::vector<double> values, double fraction);
// The openings of an EndfieldHUD startup trace (schema 1). Throws
// std::invalid_argument when it is not one or its numbers are malformed.
std::vector<OpeningSummary> summarizeOpenings(const ehud::data::Json& trace);
std::vector<GateResult> gateOpening(const OpeningSummary&, const OpeningLimits&);
// The report's "openings" scenario from a startup trace: every opening's
// summary and gates (`cold` limits for the cold opening, `warm` for the
// others), the launch timings, and a failure when fewer than
// `expectedOpenings` were recorded. Failed gates are appended to `failures`.
// Throws std::invalid_argument for a malformed trace.
ehud::data::Json openingsScenario(const ehud::data::Json& trace, const OpeningLimits& cold, const OpeningLimits& warm, std::size_t expectedOpenings,
                                  std::vector<std::string>& failures);
// The report's "footprint" scenario (memory, threads and handles): every
// point's values, peaks, GPU memory, committed memory by region type, thread
// groups and gates (FootprintPoint::limits). `cycles` names the lifecycle
// point. Failed gates are appended to `failures`, e.g. "footprint: Private
// bytes, closed after launch 352.54 MiB (limit 160.00 MiB)".
ehud::data::Json footprintScenario(std::span<const FootprintPoint> points, std::size_t cycles, std::vector<std::string>& failures);

// Markdown rendering of an EndfieldHUD.Windows.PerformanceGate report (the
// JSON perf_runner writes): verdict, machine, every scenario with its values,
// limits and results, the opening timings, lifecycle growth, the busiest
// threads and the notes. Missing or unavailable values are written as such,
// never as zero. Throws std::invalid_argument for another document format.
std::string renderGateMarkdown(const ehud::data::Json& report);
// A failed gate for the report's failure list, e.g.
// "closed-idle: CPU 0.84 % of one core (limit 0.50 % of one core)".
std::string describeFailure(std::string_view scenario, const GateResult&);
// One line per scenario for the console ("closed-idle  PASS  CPU 0.12 % ...").
std::string summarizeGateReport(const ehud::data::Json& report);
} // namespace endfield::packaging
