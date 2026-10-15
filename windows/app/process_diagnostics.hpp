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
struct ProcessDiagnostics {
    ProcessMemoryCounters memory;
    ProcessHeapCounters heaps;
    AddressSpaceCounters space;
    std::vector<AdapterMemory> adapters;
    std::vector<ThreadGroup> threads; // only with threadOrigins
};
// Cheap counters only (GetProcessMemoryInfo, handle and GUI object counts).
ProcessMemoryCounters captureProcessMemory();
// The full census; threadOrigins adds the per-thread start address walk.
ProcessDiagnostics captureProcessDiagnostics(bool threadOrigins);
ehud::data::Json processMemoryJSON(const ProcessMemoryCounters&);
ehud::data::Json processDiagnosticsJSON(const ProcessDiagnostics&);
// Names the calling thread for the census and debuggers (best effort).
void describeCurrentThread(const wchar_t* description) noexcept;
} // namespace endfield::app
