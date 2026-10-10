#pragma once
// Out-of-process sampling and control for the section 9 performance gates
// (packaging/perf_gate.hpp). Nothing is injected into the measured process,
// no debugging privilege or ETW session is needed and no administrator
// rights are used: process times and memory (GetProcessTimes,
// GetProcessMemoryInfo), handle/GDI/USER counts, per-thread context switches
// (NtQuerySystemInformation, the counter Process Explorer shows), thread
// descriptions and start modules for attribution, and GPU engine/memory
// counters through PDH when Windows provides them for the session.
#ifdef _WIN32
#include "packaging/perf_gate.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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
    bool exited() const noexcept;
    bool gpuCounters() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct MachineInfo {
    std::string os, cpu, session;
    std::vector<std::string> gpus;
    std::uint32_t logicalProcessors{}, dpi{}, refreshHz{}, displayWidth{}, displayHeight{};
    std::uint64_t memoryBytes{};
};
MachineInfo describeMachine();

// A process started (and, at the end, stopped) by the gate. Only processes
// the gate itself started are ever stopped.
class GateProcess final {
public:
    GateProcess(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments, const std::filesystem::path& folder);
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
