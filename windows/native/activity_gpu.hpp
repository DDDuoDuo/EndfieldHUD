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
// out-of-range numbers).
std::optional<ActivityGPUInstance>activityGPUInstance(std::string_view instance);
struct ActivityGPUCounter {std::string instance;std::uint64_t runningTime{};bool valid{};};
// Pure: one collection from raw (instance, running time) pairs at the
// collection's 100 ns time base. Instances that do not parse or carry invalid
// data are skipped; a repeated (engine, pid) keeps its first reading; the list
// is sorted by (engine, pid), engines by (adapter, type, index). With `pids`
// (sorted) only those processes are kept. Unavailable above
// ActivityGPUSample::maximumUsage readings.
std::optional<modules::ActivityGPUSample>activityGPUSample(std::span<const ActivityGPUCounter>,std::uint64_t time,std::span<const std::uint32_t>pids={});

// Demand gate (UI thread, lock-free): the GPU counters are collected only
// while Activity Monitor is visible (tools::ActivityPreview sets it from its
// sampling demand). Turning it off releases the collector at once when the
// sampling worker is not inside a collection, else on that worker's next call.
void setWindowsActivityGPUDemand(bool visible)noexcept;
bool windowsActivityGPUDemand()noexcept;
struct WindowsActivityGPUStats {
    std::uint64_t collections{},reuses{},opens{},closes{},failures{};
    bool open{};std::size_t instances{};double lastSeconds{},maximumSeconds{};
};
WindowsActivityGPUStats windowsActivityGPUStats()noexcept;
// Sampling worker only (the shared UtilityExecutor). While demanded: one PDH
// query over "\GPU Engine(*)\Running Time", opened on first use, collected at
// most once per 250 ms (the system and the Apps readers of one sampling job
// share a collection) and filtered to `pids` (sorted) when given. Without
// demand it closes the query, drops its buffers and returns empty. Empty too
// where the counter set is unavailable (no WDDM 2.x GPU, counters disabled).
std::optional<modules::ActivityGPUSample>readWindowsActivityGPU(std::span<const std::uint32_t>pids={});
}
