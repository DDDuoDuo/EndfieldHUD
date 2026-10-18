#pragma once
#include "modules/activity_state.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace endfield::native {
// GPU usage for Activity Monitor (a Windows addition the Mac does not show).
// Source: the documented "GPU Engine" performance counter set, one instance
// per process and engine, named
//   pid_<pid>_luid_0x<8 hex>_0x<8 hex>_phys_<n>_eng_<n>_engtype_<type>
// whose "Running Time" is the cumulative time that process kept the engine
// busy, in 100 ns units. Activity shows the total load derived from them
// (engine types summed per physical adapter, the busier adapter;
// modules::activityGPUPercent).
struct ActivityGPUInstance {std::uint32_t pid{};modules::ActivityGPUEngine engine;bool operator==(const ActivityGPUInstance&)const=default;};
// Pure parse of one instance name (the engine's type from its engtype_
// label, modules::activityGPUEngineType); empty for any other shape
// (including the "#n" duplicate suffix PDH appends, a zero PID or
// out-of-range numbers). The UTF-16 form reads PDH's own names in place,
// without a converted copy; a name with a non-ASCII character is not an
// engine there. An ASCII name parses identically in both forms.
std::optional<ActivityGPUInstance>activityGPUInstance(std::string_view instance);
std::optional<ActivityGPUInstance>activityGPUInstance(std::wstring_view instance);
struct ActivityGPUCounter {std::string instance;std::uint64_t runningTime{};bool valid{};};
// Pure: one collection from raw (instance, running time) pairs at the
// collection's 100 ns time base. Instances that do not parse or carry invalid
// data are skipped; a repeated (engine, pid) keeps its first reading; the list
// is sorted by (engine, pid), engines by (adapter, type, index). With `pids`
// (sorted) only those processes are kept. Unavailable above
// ActivityGPUSample::maximumUsage readings. The live collector below orders
// its readings with the same code.
std::optional<modules::ActivityGPUSample>activityGPUSample(std::span<const ActivityGPUCounter>,std::uint64_t time,std::span<const std::uint32_t>pids={});

// Recording policy: GPU is recorded like CPU, RAM, network and disk. Every
// system sample (native::readWindowsActivity) carries the GPU reading, so
// the history fills on the host's one ActivitySamplingPlan deadline: from
// launch every 5 s, every 1 s while Activity or a charge alert/badge with a
// non-battery metric is shown, the same whether Activity is shown, hidden or
// the HUD is closed, paused only while the session is suspended. There is no
// visibility gate and no timer, thread or wake-up of its own: the reading
// runs inside the existing system sample on the shared UtilityExecutor. The
// Apps GPU column is read with the per-app CPU, only while the Apps page is
// shown (its own sampling demand, native::ActivityCatalogSampler).
struct WindowsActivityGPUStats {
    std::uint64_t collections{},reuses{},opens{},closes{},failures{},recycled{};
    bool open{};std::size_t instances{};
    // Capacity kept between samples: the raw counter array, the parse buffer,
    // the latest collection and the recycled system reading.
    std::size_t retainedBytes{};
    double lastSeconds{},maximumSeconds{};
};
WindowsActivityGPUStats windowsActivityGPUStats()noexcept;
// Sampling worker only (the shared UtilityExecutor). One PDH query over
// "\GPU Engine(*)\Running Time", opened by the first reading and then kept,
// like the disk query (a failed collection closes it and the next sample
// opens it again; where the counter set cannot be opened, it is tried again
// at most once a minute), collected at most once per 250 ms (the system and
// the Apps readers of one sampling job share a collection).
// Without `pids` (the system reading) the steady state allocates nothing:
// the raw counter array, the parse buffer and the latest collection keep
// their capacity, instance names are parsed in place in PDH's buffer, and
// the reading is copied into the buffer recycleWindowsActivityGPU returned.
// With `pids` (sorted; the Apps column) it is a new list filtered to them.
// Empty where the counter set is unavailable (no WDDM 2.x GPU, counters
// disabled).
std::optional<modules::ActivityGPUSample>readWindowsActivityGPU(std::span<const std::uint32_t>pids={});
// Gives back a system reading the sampling worker no longer needs (the
// previous sample ActivityProbe derived from), so the next system reading
// reuses its buffer instead of allocating one. Keeps at most one buffer.
void recycleWindowsActivityGPU(modules::ActivityGPUSample&&)noexcept;
// Closes the query and drops every buffer (tests; the next reading reopens).
void releaseWindowsActivityGPU()noexcept;
}
