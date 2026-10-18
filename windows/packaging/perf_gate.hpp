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
//                GPU busy %, private-byte drift over the window, twice:
//                within the graphics grace (the HUD keeps its device, so the
//                graphics driver's thread wakes the process: a higher
//                wake-up bound) and after the release (strict bound)
//   open idle    the same with ambient animation on and off
//   lifecycle    N open/close cycles: growth of handles, threads, GDI/USER
//                objects, private bytes, working set and GPU memory between
//                the first window after warm-up and the last window
//   footprint    private bytes, working set, their peaks, threads (grouped
//                by start module) and handles closed after launch, open,
//                closed again after the N cycles settled and closed after
//                the graphics release
//
// The default limits are provisional engineering gates, not Mac parity
// numbers (the Mac's closed CPU was 0.11-0.26 % of one core; docs/
// performance-1.2.md). Every measured value is reported whether or not a
// limit is set, with the machine and settings it was measured on.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::packaging {
// The closed HUD's graphics grace (app/application.hpp
// closedGraphicsGraceSeconds, since 3eecf89): EndfieldHUD keeps its graphics
// device, swap chain and composition target for ten minutes after closing
// (and after launch), so a reopening within them stays fast, and releases
// them afterwards. While the device lives, the graphics driver's thread wakes
// the process (about 70 times a second on the laptop's NVIDIA GPU; RC 128's
// desktop gate measured 73.0/s closed), so the gate measures the closed HUD
// on both sides of the grace.
inline constexpr double productionGraphicsGraceSeconds = 600;
// The environment variable through which a gate run shortens that grace.
// EndfieldHUD.exe honours it ONLY together with --data-root (an isolated
// temporary root); the gate sets it for its separate "released" launch only.
inline constexpr std::string_view graceOverrideVariable{"ENDFIELDHUD_CLOSED_GRAPHICS_GRACE"};
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
    std::uint64_t peakPrivateBytes{}, peakWorkingSet{}; // process lifetime maxima so far
    std::optional<double> gpuSeconds;   // GPU engine running time, cumulative
    std::optional<std::uint64_t> gpuDedicated, gpuShared;
};

// --- Footprint: memory, threads and handles at fixed points of the run ------
// Section 9 asks for private bytes, working set and allocation peaks; the
// points are the end of closed idle within the graphics grace (after
// launch), the end of open idle (ambient on, and ambient off), the closed HUD
// after the open/close cycles settled and the end of closed idle after the
// graphics release. Each point also lists the process's threads grouped by
// their start address (module+offset) and description, and its committed
// memory by region type with the largest private allocations.
struct ThreadGroup {
    std::string start;   // module+offset of the Win32 start address ("" when unknown)
    std::string module;  // module file name ("" when unknown)
    std::string role;    // what the module is (threadRole), "" when not known
    std::string name;    // thread description, when set
    std::uint32_t count{};
};
struct MemoryRegions {
    std::uint64_t privateCommitted{}, mappedCommitted{}, imageCommitted{};
    std::vector<std::uint64_t> largestPrivate; // committed bytes per private allocation, largest first
};
struct FootprintLimits {
    std::optional<double> privateBytes, workingSet, threads, handles;
};
struct FootprintPoint {
    std::string point; // "closed", "open", "after-cycles", "open-ambient-off", "after-release"
    ResourceSample sample;
    std::optional<MemoryRegions> regions;
    std::vector<ThreadGroup> threads;
    FootprintLimits limits;
};
// What a thread's start module is, for the report: the Windows thread pool
// (`threadPool`: the start address of this machine's pool workers), the
// measured program itself (`program`: its file name), graphics drivers and the
// system components EndfieldHUD uses. Case-insensitive; "" when unknown.
std::string threadRole(std::string_view module, bool threadPool, std::string_view program);
// Groups threads by (start, description), largest groups first, ties by start.
std::vector<ThreadGroup> groupThreads(std::vector<ThreadGroup> threads);
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

// Gates named "<point>.private-bytes|working-set|threads|handles".
std::vector<GateResult> gateFootprint(const FootprintPoint&);
bool allPassed(std::span<const GateResult>) noexcept;
double median(std::vector<double>);
} // namespace endfield::packaging
