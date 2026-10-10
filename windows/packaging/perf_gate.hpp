#pragma once
// WINDOWS-MIGRATION.md section 9 performance gates (portable evaluation).
//
// windows/tools/package_perf_gate.cpp samples a running EndfieldHUD.exe from
// outside (packaging/perf_probe.hpp: process CPU time, per-thread context
// switches, handles, threads, GDI/USER objects, private bytes, working set
// and, where Windows exposes them, GPU engine time and GPU memory) and this
// module turns the samples into gate results:
//
//   closed idle  CPU % of one core, wakeups (context switches) per second,
//                GPU busy %, private-byte drift over the window
//   open idle    the same with ambient animation on and off
//   lifecycle    N open/close cycles: growth of handles, threads, GDI/USER
//                objects, private bytes, working set and GPU memory between
//                the first window after warm-up and the last window
//
// The default limits are provisional engineering gates, not Mac parity
// numbers (the Mac's closed CPU was 0.11-0.26 % of one core; docs/
// performance-1.2.md). Every measured value is reported whether or not a
// limit is set, with the machine and settings it was measured on.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace endfield::packaging {
struct ThreadActivity {
    std::uint32_t id{};
    std::string name;        // thread description, when set
    std::string start;       // module+offset of the thread's start address
    double wakeupsPerSecond{};
};
struct ResourceSample {
    double time{};                      // seconds on the gate's clock
    double cpuSeconds{};                // user + kernel, cumulative
    std::uint64_t contextSwitches{};    // all threads, cumulative (threads that exited are not counted)
    std::uint32_t threads{}, handles{}, gdiObjects{}, userObjects{};
    std::uint64_t privateBytes{}, workingSet{};
    std::optional<double> gpuSeconds;   // GPU engine running time, cumulative
    std::optional<std::uint64_t> gpuDedicated, gpuShared;
};
struct GateResult {
    std::string name, unit;
    double value{}, limit{};
    bool passed{};
};

struct IdleLimits {
    std::optional<double> cpuPercent{0.5};      // of one core
    std::optional<double> wakeupsPerSecond{20};
    std::optional<double> gpuPercent{0.5};
    std::optional<double> privateGrowthBytes{4.0 * 1024 * 1024};
};
struct IdleSummary {
    double seconds{}, cpuPercent{}, wakeupsPerSecond{};
    std::optional<double> gpuPercent;
    double privateGrowthBytes{};
    std::uint64_t privateBytes{}, workingSet{};
    std::uint32_t threads{}, handles{}, gdiObjects{}, userObjects{};
};
// Needs at least two samples with increasing time; throws std::invalid_argument.
IdleSummary summarizeIdle(std::span<const ResourceSample>);
std::vector<GateResult> gateIdle(const IdleSummary&, const IdleLimits&);

struct LifecycleLimits {
    std::size_t warmup{10}, window{10};          // cycles
    std::optional<double> handles{16}, threads{2}, gdiObjects{8}, userObjects{8};
    std::optional<double> privateBytes{8.0 * 1024 * 1024}, workingSet{24.0 * 1024 * 1024}, gpuDedicated{16.0 * 1024 * 1024};
};
struct Growth {
    std::string metric;
    double baseline{}, latest{}, growth{};
};
// One sample per completed cycle (taken after the close settled). Needs
// warmup + 2 * window samples; throws std::invalid_argument otherwise.
std::vector<Growth> lifecycleGrowth(std::span<const ResourceSample>, const LifecycleLimits&);
std::vector<GateResult> gateLifecycle(std::span<const Growth>, const LifecycleLimits&);

bool allPassed(std::span<const GateResult>) noexcept;
double median(std::vector<double>);
} // namespace endfield::packaging
