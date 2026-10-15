#pragma once
// Out-of-process sampling and control for the section 9 performance gates
// (packaging/perf_gate.hpp). Nothing is injected into the measured process,
// no debugging privilege or ETW session is needed and no administrator
// rights are used: process times and memory with their peaks
// (GetProcessTimes, GetProcessMemoryInfo), handle/GDI/USER counts, per-thread
// context switches (NtQuerySystemInformation, the counter Process Explorer
// shows), thread descriptions and start modules for attribution, committed
// memory by region type (VirtualQueryEx), and GPU engine/memory counters
// through PDH when Windows provides them for the session.
#ifdef _WIN32
#include "packaging/perf_gate.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace endfield::packaging {
double gateClock() noexcept; // seconds, QueryPerformanceCounter

class ProcessProbe final {
public:
    explicit ProcessProbe(std::uint32_t processId);
    ~ProcessProbe();
    ProcessProbe(const ProcessProbe&) = delete;
    ProcessProbe& operator=(const ProcessProbe&) = delete;
    ResourceSample sample();
    // Starts a per-thread attribution window at the current sample.
    void markThreads();
    // The busiest threads (context switches per second) since markThreads().
    std::vector<ThreadActivity> threadsSinceMark(std::size_t count);
    // Every thread of the process now, grouped by start address and
    // description (groupThreads), each with its role (threadRole): the
    // Windows thread pool is recognized by this process's own pool worker.
    std::vector<ThreadGroup> threadCensus();
    // The process's committed memory by region type (VirtualQueryEx) and its
    // largest private allocations (committed bytes per allocation base).
    std::optional<MemoryRegions> memoryRegions();
    bool exited() const noexcept;
    bool gpuCounters() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct MachineInfo {
    std::string os, cpu, session;
    bool interactiveDesktop{}; // the gate's window station is visible (a desktop the user sees)
    std::vector<std::string> gpus;
    std::uint32_t logicalProcessors{}, dpi{}, refreshHz{}, displayWidth{}, displayHeight{};
    std::uint64_t memoryBytes{};
};
MachineInfo describeMachine();
// True when this process runs on a visible (interactive) window station, i.e.
// windows it starts appear on a desktop the user sees; false in SSH, service
// and CI sessions.
bool interactiveDesktop() noexcept;
// Running processes with this image name (case-insensitive), except one id.
std::size_t countProcesses(std::wstring_view imageName, std::uint32_t excluding) noexcept;
// The path with every existing component in its long form (8.3 aliases such
// as C:\Users\RUNNER~1 expanded); unchanged when it cannot be resolved.
std::filesystem::path longPath(const std::filesystem::path&);

// A process started (and, at the end, stopped) by the gate. Only processes
// the gate itself started are ever stopped. It runs in a kill-on-close job
// object, so it ends with the gate even when the gate is closed or killed
// (its own child processes break away and are never affected). `environment`
// adds or replaces variables of the inherited environment.
class GateProcess final {
public:
    using Environment = std::vector<std::pair<std::wstring, std::wstring>>;
    GateProcess(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments, const std::filesystem::path& folder,
                const Environment& environment = {});
    ~GateProcess();
    GateProcess(const GateProcess&) = delete;
    GateProcess& operator=(const GateProcess&) = delete;
    std::uint32_t id() const noexcept;
    bool waitIdle(std::uint32_t milliseconds) const; // WaitForInputIdle
    bool running() const noexcept;
    // The session-end protocol through the Restart Manager for exactly this
    // process (the app saves first); false when it did not exit in time, in
    // which case it is terminated.
    bool stopGracefully(std::uint32_t milliseconds);
private:
    void* process_{};
    void* job_{};
    std::uint32_t id_{};
    std::uint64_t startTime_{};
};
// Runs a short-lived command (e.g. a relaunch that forwards to the primary
// instance) and returns its exit code; nullopt when it did not finish.
std::optional<std::uint32_t> runToCompletion(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
                                             const std::filesystem::path& folder, std::uint32_t milliseconds);
std::wstring commandLine(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments);
} // namespace endfield::packaging
#endif
