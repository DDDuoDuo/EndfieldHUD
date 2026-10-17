#pragma once
// Opt-in process memory and thread census for the memory trace
// (watch_session_preview --memory-trace and EndfieldHUD.exe with
// ENDFIELDHUD_MEMORY_TRACE=1). Numbers and system module file names only:
// never paths, window titles, text or document content. Each capture walks
// the address space, the process heaps, the thread list and the DXGI
// adapters once; it is a diagnostic stage call, never on the frame path.
#include "core/data/json.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace endfield::app {
struct ProcessMemoryCounters {
    std::uint64_t privateBytes{}, peakPrivateBytes{}, workingSet{}, peakWorkingSet{}, privateWorkingSet{};
    bool privateWorkingSetAvailable{};
    std::uint32_t handles{}, gdiObjects{}, userObjects{}, threads{};
    double cpuSeconds{};
};
// HeapSummary over every process heap (the CRT allocates from the process heap).
struct ProcessHeapCounters { std::uint32_t heaps{}; std::uint64_t committed{}, allocated{}, reserved{}; };
// VirtualQuery census of the user address space by page type. Private
// committed allocations are also bucketed by their committed size, which
// separates small heap segments and thread stacks from large buffers
// (decoded images, retained rasters, driver allocations).
struct AddressSpaceCounters {
    std::uint64_t privateCommitted{}, mappedCommitted{}, imageCommitted{}, privateReserved{}, mappedReserved{};
    struct Bucket { std::uint64_t limit{}, allocations{}, bytes{}; };
    std::vector<Bucket> privateAllocations; // committed bytes per allocation: <64 KiB, <1 MiB, <16 MiB, <64 MiB, larger
    std::vector<std::uint64_t> largestPrivateAllocations; // committed bytes, the 12 largest
};
// Threads grouped by their Win32 start address: the module file name, the
// offset in it and the thread description (SetThreadDescription), with
// their CPU time. A CRT thread start (std::thread) shows ucrtbase.dll; the
// application names its own workers so they stay distinguishable.
struct ThreadGroup {
    std::string module, description;
    std::uint64_t offset{};
    std::uint32_t count{};
    double cpuSeconds{};
};
// This process's usage on each DXGI adapter (QueryVideoMemoryInfo; any
// device the process created on that adapter counts).
struct AdapterMemory {
    std::string name;
    std::uint32_t vendorID{};
    bool software{};
    std::uint64_t localUsage{}, localBudget{}, nonLocalUsage{}, nonLocalBudget{};
};
// The OS thread-pool workers (start ntdll!TppWorkerThread) all share one
// start address, so the thread groups cannot tell their users apart. Each
// worker's committed stack is read (never suspended, so no lock it holds can
// stall the census) and every 8-byte value that is a return address into a
// loaded module's executable section (the bytes before it are a call) counts
// for that module: the live frames and the stale frames of the callbacks the
// worker ran before. ntdll, kernel32 and kernelbase (on every worker) are
// left out. A heuristic for attribution, numbers and module names only.
struct PoolWorker {
    std::uint32_t id{};
    double cpuSeconds{}, ageSeconds{};
    std::vector<std::pair<std::string, std::uint32_t>> modules; // module, return addresses seen (most first, at most 10)
};
struct PoolWorkers {
    std::uint32_t workers{};
    std::vector<std::pair<std::string, std::uint32_t>> modules; // module, workers referencing it (most first)
    std::vector<PoolWorker> threads;
};
struct ProcessDiagnostics {
    ProcessMemoryCounters memory;
    ProcessHeapCounters heaps;
    AddressSpaceCounters space;
    std::vector<AdapterMemory> adapters;
    std::vector<ThreadGroup> threads; // only with threadOrigins
    PoolWorkers pool;                 // only with threadOrigins
};
// The pool workers of this process now (PoolWorkers above), and only their
// number (no stack is read).
PoolWorkers capturePoolWorkers();
std::uint32_t countPoolWorkers();
// One thread of this process at a capture: its context switches and CPU time
// so far (NtQuerySystemInformation, the counters Process Explorer and the
// section 9 gate read), with its start module, offset and description.
struct ThreadActivity {
    std::uint32_t id{};
    std::string module, description;
    std::uint64_t offset{}, contextSwitches{};
    double cpuSeconds{};
};
// Every thread of this process now (empty when the system refuses the query).
std::vector<ThreadActivity> captureThreadActivity();
// What happened between two captures `seconds` apart: the wake-ups (context
// switches) per second of the whole process and of each thread that ran,
// busiest first (at most `top`; a thread created between the captures counts
// from zero, one that exited is dropped), and the same grouped by start and
// description.
ehud::data::Json threadActivityJSON(const std::vector<ThreadActivity>& before, const std::vector<ThreadActivity>& after,
                                    double seconds, std::size_t top);
// Cheap counters only (GetProcessMemoryInfo, handle and GUI object counts).
ProcessMemoryCounters captureProcessMemory();
// The full census; threadOrigins adds the per-thread start address walk.
ProcessDiagnostics captureProcessDiagnostics(bool threadOrigins);
ehud::data::Json processMemoryJSON(const ProcessMemoryCounters&);
ehud::data::Json processDiagnosticsJSON(const ProcessDiagnostics&);
// Names the calling thread for the census and debuggers (best effort).
void describeCurrentThread(const wchar_t* description) noexcept;
} // namespace endfield::app
