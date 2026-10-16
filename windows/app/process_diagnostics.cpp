#include "app/process_diagnostics.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <tuple>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace endfield::app {
using ehud::data::Json;
namespace {
// PROCESS_MEMORY_COUNTERS_EX2 (Windows 10 21H2 / Server 2022 and later): the
// private working set is Task Manager's Memory column. Mirrored here so the
// census does not depend on the SDK's NTDDI gate; older systems reject the
// size and the census falls back to PROCESS_MEMORY_COUNTERS_EX.
struct MemoryCountersEx2 {
    DWORD cb; DWORD PageFaultCount;
    SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage,
        QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage,
        PrivateWorkingSetSize;
    ULONG64 SharedCommitUsage;
};
double seconds(FILETIME t) { return (double(t.dwHighDateTime) * 4294967296. + t.dwLowDateTime) * 1e-7; }
std::string narrow(const wchar_t* value, std::size_t length) {
    if (!value || !length) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length), out.data(), count, nullptr, nullptr);
    return out;
}
// Base file name of the module containing an address ("" outside any module).
std::pair<std::string, std::uint64_t> moduleOf(std::uintptr_t address) {
    HMODULE module{};
    if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        reinterpret_cast<LPCWSTR>(address), &module))
        return {"(no module)", 0};
    std::array<wchar_t, MAX_PATH> path{};
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {"(unnamed module)", address - reinterpret_cast<std::uintptr_t>(module)};
    std::size_t base = 0;
    for (std::size_t n = 0; n < length; ++n) if (path[n] == L'\\' || path[n] == L'/') base = n + 1;
    return {narrow(path.data() + base, length - base), address - reinterpret_cast<std::uintptr_t>(module)};
}
using QueryThread = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using ReadDescription = HRESULT(WINAPI*)(HANDLE, PWSTR*);
using WriteDescription = HRESULT(WINAPI*)(HANDLE, PCWSTR);
template <class F> F kernelProcedure(const wchar_t* module, const char* name) {
    const auto handle = GetModuleHandleW(module);
    return handle ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(handle, name))) : nullptr;
}
std::vector<ThreadGroup> threadGroups() {
    std::vector<ThreadGroup> groups;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return groups;
    const auto query = kernelProcedure<QueryThread>(L"ntdll.dll", "NtQueryInformationThread");
    auto describe = kernelProcedure<ReadDescription>(L"kernelbase.dll", "GetThreadDescription");
    if (!describe) describe = kernelProcedure<ReadDescription>(L"kernel32.dll", "GetThreadDescription");
    std::map<std::tuple<std::string, std::uint64_t, std::string>, ThreadGroup> grouped;
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    const auto self = GetCurrentProcessId();
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != self) continue;
        const auto thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        std::uintptr_t start{};
        std::string description;
        double cpu{};
        if (thread) {
            constexpr ULONG threadQuerySetWin32StartAddress = 9;
            if (query) (void)query(thread, threadQuerySetWin32StartAddress, &start, sizeof(start), nullptr);
            if (describe) {
                PWSTR text{};
                if (SUCCEEDED(describe(thread, &text)) && text) {
                    std::size_t length = 0;
                    while (text[length] && length < 128) ++length;
                    description = narrow(text, length);
                    LocalFree(text);
                }
            }
            FILETIME created, exited, kernel, user;
            if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) cpu = seconds(kernel) + seconds(user);
            CloseHandle(thread);
        }
        auto [module, offset] = moduleOf(start);
        auto& group = grouped[{module, offset, description}];
        if (!group.count) { group.module = std::move(module); group.offset = offset; group.description = description; }
        ++group.count; group.cpuSeconds += cpu;
    }
    CloseHandle(snapshot);
    groups.reserve(grouped.size());
    for (auto& [_, group] : grouped) groups.push_back(std::move(group));
    std::sort(groups.begin(), groups.end(), [](const ThreadGroup& a, const ThreadGroup& b) {
        return a.count != b.count ? a.count > b.count : std::tie(a.module, a.offset) < std::tie(b.module, b.offset); });
    return groups;
}
ProcessHeapCounters heapCounters() {
    ProcessHeapCounters result;
    std::vector<HANDLE> heaps(64);
    auto count = GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data());
    if (count > heaps.size()) { heaps.resize(count); count = GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data()); }
    result.heaps = static_cast<std::uint32_t>(std::min<std::size_t>(count, heaps.size()));
    for (std::uint32_t n = 0; n < result.heaps; ++n) {
        HEAP_SUMMARY summary{}; summary.cb = sizeof(summary);
        if (HeapSummary(heaps[n], 0, &summary)) {
            result.committed += summary.cbCommitted; result.allocated += summary.cbAllocated; result.reserved += summary.cbReserved;
        }
    }
    return result;
}
AddressSpaceCounters addressSpace() {
    AddressSpaceCounters result;
    result.privateAllocations = {{64ull << 10}, {1ull << 20}, {16ull << 20}, {64ull << 20}, {~0ull}};
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    auto address = reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress);
    const auto limit = reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
    std::uintptr_t allocation{}; std::uint64_t committed{};
    std::vector<std::uint64_t> largest;
    const auto finish = [&] {
        if (!allocation || !committed) return;
        for (auto& bucket : result.privateAllocations) if (committed < bucket.limit) { ++bucket.allocations; bucket.bytes += committed; break; }
        largest.push_back(committed);
    };
    MEMORY_BASIC_INFORMATION region{};
    while (address < limit && VirtualQuery(reinterpret_cast<LPCVOID>(address), &region, sizeof(region)) == sizeof(region)) {
        const auto base = reinterpret_cast<std::uintptr_t>(region.AllocationBase);
        if (region.Type == MEM_PRIVATE && base != allocation) { finish(); allocation = base; committed = 0; }
        if (region.State == MEM_COMMIT) {
            if (region.Type == MEM_PRIVATE) { result.privateCommitted += region.RegionSize; if (base == allocation) committed += region.RegionSize; }
            else if (region.Type == MEM_MAPPED) result.mappedCommitted += region.RegionSize;
            else if (region.Type == MEM_IMAGE) result.imageCommitted += region.RegionSize;
        } else if (region.State == MEM_RESERVE) {
            if (region.Type == MEM_PRIVATE) result.privateReserved += region.RegionSize;
            else if (region.Type == MEM_MAPPED) result.mappedReserved += region.RegionSize;
        }
        const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (next <= address) break;
        address = next;
    }
    finish();
    std::sort(largest.begin(), largest.end(), std::greater<>());
    if (largest.size() > 12) largest.resize(12);
    result.largestPrivateAllocations = std::move(largest);
    return result;
}
std::vector<AdapterMemory> adapterMemory() {
    using Microsoft::WRL::ComPtr;
    std::vector<AdapterMemory> result;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return result;
    for (UINT n = 0; n < 16; ++n) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(n, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description))) continue;
        AdapterMemory row;
        description.Description[127] = 0;
        std::size_t length = 0; while (description.Description[length]) ++length;
        row.name = narrow(description.Description, length); row.vendorID = description.VendorId;
        row.software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        ComPtr<IDXGIAdapter3> query;
        if (SUCCEEDED(adapter.As(&query))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonLocal{};
            if (SUCCEEDED(query->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local))) { row.localUsage = local.CurrentUsage; row.localBudget = local.Budget; }
            if (SUCCEEDED(query->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal))) { row.nonLocalUsage = nonLocal.CurrentUsage; row.nonLocalBudget = nonLocal.Budget; }
        }
        result.push_back(std::move(row));
    }
    return result;
}
Json integer(std::uint64_t value) { return Json(static_cast<std::int64_t>(value)); }
// SYSTEM_PROCESS_INFORMATION / SYSTEM_THREAD_INFORMATION (x64 layout, as
// packaging/perf_probe.cpp reads them for the section 9 gate).
using QuerySystem = LONG(WINAPI*)(ULONG, PVOID, ULONG, PULONG);
constexpr std::size_t processThreadCount = 0x04, processIdentifier = 0x50, processHeader = 0x100;
constexpr std::size_t threadEntrySize = 0x50, threadKernelTime = 0x00, threadUserTime = 0x08, threadIdentifier = 0x30, threadSwitchCount = 0x40;
template <class T> T field(const std::vector<std::uint8_t>& buffer, std::size_t at) {
    T value{}; std::memcpy(&value, buffer.data() + at, sizeof(T)); return value;
}
} // namespace

std::vector<ThreadActivity> captureThreadActivity() {
    std::vector<ThreadActivity> result;
    static const auto query = kernelProcedure<QuerySystem>(L"ntdll.dll", "NtQuerySystemInformation");
    if (!query) return result;
    constexpr ULONG systemProcessInformation = 5;
    constexpr LONG lengthMismatch = static_cast<LONG>(0xC0000004L);
    std::vector<std::uint8_t> buffer(1u << 20);
    for (int attempt = 0; attempt < 8; ++attempt) {
        ULONG needed{};
        const auto status = query(systemProcessInformation, buffer.data(), static_cast<ULONG>(buffer.size()), &needed);
        if (status == lengthMismatch) { buffer.resize(std::max<std::size_t>(needed + 65536, buffer.size() * 2)); continue; }
        if (status < 0) return result;
        const auto self = GetCurrentProcessId();
        for (std::size_t at = 0;;) {
            if (at + processHeader > buffer.size()) return result;
            if (static_cast<DWORD>(field<std::uintptr_t>(buffer, at + processIdentifier)) == self) {
                const auto count = field<ULONG>(buffer, at + processThreadCount);
                for (ULONG n = 0; n < count; ++n) {
                    const auto entry = at + processHeader + std::size_t(n) * threadEntrySize;
                    if (entry + threadEntrySize > buffer.size()) break;
                    ThreadActivity row;
                    row.id = static_cast<std::uint32_t>(field<std::uintptr_t>(buffer, entry + threadIdentifier));
                    row.contextSwitches = field<ULONG>(buffer, entry + threadSwitchCount);
                    row.cpuSeconds = double(field<std::int64_t>(buffer, entry + threadKernelTime) + field<std::int64_t>(buffer, entry + threadUserTime)) * 1e-7;
                    result.push_back(std::move(row));
                }
                break;
            }
            const auto next = field<ULONG>(buffer, at);
            if (!next) return result;
            at += next;
        }
        break;
    }
    // Start address and description per thread (as threadGroups does).
    const auto queryThread = kernelProcedure<QueryThread>(L"ntdll.dll", "NtQueryInformationThread");
    auto describe = kernelProcedure<ReadDescription>(L"kernelbase.dll", "GetThreadDescription");
    if (!describe) describe = kernelProcedure<ReadDescription>(L"kernel32.dll", "GetThreadDescription");
    for (auto& row : result) {
        const auto thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION, FALSE, row.id);
        if (!thread) continue;
        std::uintptr_t start{};
        constexpr ULONG threadQuerySetWin32StartAddress = 9;
        if (queryThread) (void)queryThread(thread, threadQuerySetWin32StartAddress, &start, sizeof(start), nullptr);
        if (describe) {
            PWSTR text{};
            if (SUCCEEDED(describe(thread, &text)) && text) {
                std::size_t length = 0; while (text[length] && length < 128) ++length;
                row.description = narrow(text, length); LocalFree(text);
            }
        }
        CloseHandle(thread);
        auto [module, offset] = moduleOf(start);
        row.module = std::move(module); row.offset = offset;
    }
    return result;
}
Json threadActivityJSON(const std::vector<ThreadActivity>& before, const std::vector<ThreadActivity>& after, double seconds, std::size_t top) {
    seconds = std::max(seconds, 1e-6);
    struct Row { const ThreadActivity* thread; std::uint64_t switches; double cpu; };
    std::vector<Row> rows;
    std::uint64_t total{};
    double cpu{};
    for (const auto& thread : after) {
        const auto previous = std::find_if(before.begin(), before.end(), [&](const ThreadActivity& t) { return t.id == thread.id; });
        const auto switches = previous == before.end() ? thread.contextSwitches : thread.contextSwitches - std::min(thread.contextSwitches, previous->contextSwitches);
        const auto used = previous == before.end() ? thread.cpuSeconds : std::max(0., thread.cpuSeconds - previous->cpuSeconds);
        total += switches; cpu += used;
        if (switches || used > 0) rows.push_back({&thread, switches, used});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.switches != b.switches ? a.switches > b.switches : a.cpu > b.cpu; });
    std::map<std::tuple<std::string, std::uint64_t, std::string>, std::pair<std::uint64_t, std::pair<double, std::uint32_t>>> grouped;
    for (const auto& r : rows) {
        auto& g = grouped[{r.thread->module, r.thread->offset, r.thread->description}];
        g.first += r.switches; g.second.first += r.cpu; ++g.second.second;
    }
    Json::Array threads, groups;
    for (std::size_t n = 0; n < rows.size() && n < top; ++n) {
        const auto& r = rows[n];
        threads.push_back(Json::Object{{"id", integer(r.thread->id)}, {"module", r.thread->module}, {"offset", integer(r.thread->offset)},
            {"description", r.thread->description}, {"wakeupsPerSecond", double(r.switches) / seconds}, {"cpuPercentOfOneCore", r.cpu * 100 / seconds}});
    }
    std::vector<std::pair<const std::tuple<std::string, std::uint64_t, std::string>*, const std::pair<std::uint64_t, std::pair<double, std::uint32_t>>*>> order;
    for (const auto& [key, value] : grouped) order.emplace_back(&key, &value);
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second->first > b.second->first; });
    for (const auto& [key, value] : order)
        groups.push_back(Json::Object{{"module", std::get<0>(*key)}, {"offset", integer(std::get<1>(*key))}, {"description", std::get<2>(*key)},
            {"threads", integer(value->second.second)}, {"wakeupsPerSecond", double(value->first) / seconds}, {"cpuPercentOfOneCore", value->second.first * 100 / seconds}});
    return Json::Object{{"seconds", seconds}, {"threads", integer(after.size())}, {"wakeupsPerSecond", double(total) / seconds},
        {"threadCpuPercentOfOneCore", cpu * 100 / seconds}, {"busiestThreads", std::move(threads)}, {"byStart", std::move(groups)}};
}

ProcessMemoryCounters captureProcessMemory() {
    ProcessMemoryCounters result;
    const auto process = GetCurrentProcess();
    MemoryCountersEx2 memory{}; memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        result.privateWorkingSet = memory.PrivateWorkingSetSize; result.privateWorkingSetAvailable = memory.PrivateWorkingSetSize > 0;
    } else {
        memory = {}; memory.cb = sizeof(PROCESS_MEMORY_COUNTERS_EX);
        (void)GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(PROCESS_MEMORY_COUNTERS_EX));
    }
    result.privateBytes = memory.PrivateUsage; result.peakPrivateBytes = memory.PeakPagefileUsage;
    result.workingSet = memory.WorkingSetSize; result.peakWorkingSet = memory.PeakWorkingSetSize;
    DWORD handles{}; if (GetProcessHandleCount(process, &handles)) result.handles = handles;
    result.gdiObjects = GetGuiResources(process, GR_GDIOBJECTS); result.userObjects = GetGuiResources(process, GR_USEROBJECTS);
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) result.cpuSeconds = seconds(kernel) + seconds(user);
    // Thread count without the per-thread walk.
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
            if (entry.th32ProcessID == GetCurrentProcessId()) { result.threads = entry.cntThreads; break; }
        CloseHandle(snapshot);
    }
    return result;
}
ProcessDiagnostics captureProcessDiagnostics(bool threadOrigins) {
    ProcessDiagnostics result;
    result.memory = captureProcessMemory();
    result.heaps = heapCounters();
    result.space = addressSpace();
    result.adapters = adapterMemory();
    if (threadOrigins) result.threads = threadGroups();
    return result;
}
Json processMemoryJSON(const ProcessMemoryCounters& m) {
    Json::Object out{{"privateBytes", integer(m.privateBytes)}, {"peakPrivateBytes", integer(m.peakPrivateBytes)},
        {"workingSetBytes", integer(m.workingSet)}, {"peakWorkingSetBytes", integer(m.peakWorkingSet)},
        {"handles", integer(m.handles)}, {"gdiObjects", integer(m.gdiObjects)}, {"userObjects", integer(m.userObjects)},
        {"threads", integer(m.threads)}, {"cpuSeconds", m.cpuSeconds}};
    if (m.privateWorkingSetAvailable) out["privateWorkingSetBytes"] = integer(m.privateWorkingSet);
    return out;
}
Json processDiagnosticsJSON(const ProcessDiagnostics& d) {
    Json::Array buckets, largest, adapters, threads;
    for (const auto& b : d.space.privateAllocations)
        buckets.push_back(Json::Object{{"belowBytes", b.limit == ~0ull ? Json() : integer(b.limit)}, {"allocations", integer(b.allocations)}, {"committedBytes", integer(b.bytes)}});
    for (const auto bytes : d.space.largestPrivateAllocations) largest.push_back(integer(bytes));
    for (const auto& a : d.adapters)
        adapters.push_back(Json::Object{{"name", a.name}, {"vendorID", integer(a.vendorID)}, {"software", a.software},
            {"localUsageBytes", integer(a.localUsage)}, {"localBudgetBytes", integer(a.localBudget)},
            {"nonLocalUsageBytes", integer(a.nonLocalUsage)}, {"nonLocalBudgetBytes", integer(a.nonLocalBudget)}});
    for (const auto& t : d.threads)
        threads.push_back(Json::Object{{"module", t.module}, {"offset", integer(t.offset)}, {"description", t.description},
            {"count", integer(t.count)}, {"cpuSeconds", t.cpuSeconds}});
    Json::Object out{{"process", processMemoryJSON(d.memory)},
        {"heaps", Json::Object{{"count", integer(d.heaps.heaps)}, {"committedBytes", integer(d.heaps.committed)},
            {"allocatedBytes", integer(d.heaps.allocated)}, {"reservedBytes", integer(d.heaps.reserved)}}},
        {"addressSpace", Json::Object{{"privateCommittedBytes", integer(d.space.privateCommitted)}, {"mappedCommittedBytes", integer(d.space.mappedCommitted)},
            {"imageCommittedBytes", integer(d.space.imageCommitted)}, {"privateReservedBytes", integer(d.space.privateReserved)},
            {"mappedReservedBytes", integer(d.space.mappedReserved)}, {"privateAllocationsBySize", std::move(buckets)},
            {"largestPrivateAllocationsBytes", std::move(largest)}}},
        {"adapters", std::move(adapters)}};
    if (!d.threads.empty()) out["threadsByStart"] = std::move(threads);
    return out;
}
void describeCurrentThread(const wchar_t* description) noexcept {
    auto write = kernelProcedure<WriteDescription>(L"kernelbase.dll", "SetThreadDescription");
    if (!write) write = kernelProcedure<WriteDescription>(L"kernel32.dll", "SetThreadDescription");
    if (write && description) (void)write(GetCurrentThread(), description);
}
} // namespace endfield::app
