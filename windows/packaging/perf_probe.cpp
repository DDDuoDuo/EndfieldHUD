#include "packaging/perf_probe.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <restartmanager.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace endfield::packaging {
namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

std::string narrowText(std::wstring_view value) {
    if (value.empty()) return {};
    const auto n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
using QuerySystemInformation = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using QueryThreadInformation = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using GetDescription = HRESULT(WINAPI*)(HANDLE, PWSTR*);
template <class T> T ntdll(const char* name) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), name)));
}
// SYSTEM_PROCESS_INFORMATION / SYSTEM_THREAD_INFORMATION (x64 layout).
constexpr std::size_t processThreads = 0x04, processId = 0x50, processHeader = 0x100;
constexpr std::size_t threadEntry = 0x50, threadId = 0x30, threadSwitches = 0x40;
constexpr ULONG systemProcessInformation = 5, threadQuerySetWin32StartAddress = 9;
constexpr LONG statusInfoLengthMismatch = static_cast<LONG>(0xC0000004L);
template <class T> T read(const std::vector<std::uint8_t>& buffer, std::size_t at) {
    T value{};
    std::memcpy(&value, buffer.data() + at, sizeof(T));
    return value;
}
std::uint64_t fileTime(const FILETIME& t) noexcept { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
double pdhSum(PDH_HCOUNTER counter, DWORD format) {
    DWORD bytes{}, count{};
    auto status = PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, nullptr);
    if (status != PDH_MORE_DATA || !bytes) return 0;
    std::vector<std::uint8_t> buffer(bytes);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, items);
    if (status != ERROR_SUCCESS) return 0;
    double total = 0;
    for (DWORD n = 0; n < count; ++n) {
        if (items[n].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[n].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        total += format == PDH_FMT_DOUBLE ? items[n].FmtValue.doubleValue : static_cast<double>(items[n].FmtValue.largeValue);
    }
    return total;
}
} // namespace

double gateClock() noexcept {
    static const auto frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return static_cast<double>(f.QuadPart); }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / frequency;
}

struct ProcessProbe::Impl {
    DWORD id{};
    HANDLE process{};
    QuerySystemInformation query = ntdll<QuerySystemInformation>("NtQuerySystemInformation");
    std::vector<std::uint8_t> buffer;
    std::unordered_map<DWORD, ULONG> current, mark;
    double currentTime{}, markTime{};
    PDH_HQUERY pdh{};
    PDH_HCOUNTER engine{}, dedicated{}, shared{};
    bool gpu{};
    double gpuSeconds{}, lastGpuTime{};

    // Per-thread context switches of the process; false when it is gone.
    bool threads(std::uint64_t& total, std::uint32_t& count) {
        if (!query) return false;
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (buffer.empty()) buffer.resize(1u << 20);
            ULONG needed{};
            const auto status = query(systemProcessInformation, buffer.data(), static_cast<ULONG>(buffer.size()), &needed);
            if (status == statusInfoLengthMismatch) { buffer.resize(std::max<std::size_t>(needed + 65536, buffer.size() * 2)); continue; }
            if (status < 0) return false;
            current.clear();
            for (std::size_t at = 0;;) {
                if (at + processHeader > buffer.size()) return false;
                const auto pid = static_cast<DWORD>(read<std::uintptr_t>(buffer, at + processId));
                if (pid == id) {
                    count = read<ULONG>(buffer, at + processThreads);
                    total = 0;
                    for (std::uint32_t n = 0; n < count; ++n) {
                        const auto entry = at + processHeader + n * threadEntry;
                        if (entry + threadEntry > buffer.size()) return false;
                        const auto tid = static_cast<DWORD>(read<std::uintptr_t>(buffer, entry + threadId));
                        const auto switches = read<ULONG>(buffer, entry + threadSwitches);
                        current[tid] = switches;
                        total += switches;
                    }
                    return true;
                }
                const auto next = read<ULONG>(buffer, at);
                if (!next) return false;
                at += next;
            }
        }
        return false;
    }
    std::string startModule(DWORD tid) const {
        static const auto queryThread = ntdll<QueryThreadInformation>("NtQueryInformationThread");
        HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
        if (!thread || !queryThread) { if (thread) CloseHandle(thread); return {}; }
        void* address{};
        const auto status = queryThread(thread, threadQuerySetWin32StartAddress, &address, sizeof(address), nullptr);
        CloseHandle(thread);
        if (status < 0 || !address) return {};
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQueryEx(process, address, &memory, sizeof(memory)) || !memory.AllocationBase) return {};
        wchar_t file[MAX_PATH]{};
        if (!GetMappedFileNameW(process, memory.AllocationBase, file, MAX_PATH)) return {};
        const std::wstring_view path(file);
        const auto slash = path.find_last_of(L"\\/");
        char offset[32]{};
        std::snprintf(offset, sizeof(offset), "+0x%llx",
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(memory.AllocationBase)));
        return narrowText(slash == std::wstring_view::npos ? path : path.substr(slash + 1)) + offset;
    }
    static std::string description(DWORD tid) {
        static const auto get = reinterpret_cast<GetDescription>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
        if (!get) return {};
        HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
        if (!thread) return {};
        PWSTR text{};
        std::string out;
        if (SUCCEEDED(get(thread, &text)) && text) { out = narrowText(text); LocalFree(text); }
        CloseHandle(thread);
        return out;
    }
};

ProcessProbe::ProcessProbe(std::uint32_t pid) : impl_(std::make_unique<Impl>()) {
    impl_->id = pid;
    impl_->process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
    if (!impl_->process) throw std::runtime_error("The measured process cannot be opened");
    // GPU counters (Windows 10 1709+): engine utilization and process GPU memory.
    if (PdhOpenQueryW(nullptr, 0, &impl_->pdh) == ERROR_SUCCESS) {
        const auto prefix = L"pid_" + std::to_wstring(pid) + L"_*";
        impl_->gpu = PdhAddEnglishCounterW(impl_->pdh, (L"\\GPU Engine(" + prefix + L")\\Utilization Percentage").c_str(), 0, &impl_->engine) == ERROR_SUCCESS &&
                     PdhAddEnglishCounterW(impl_->pdh, (L"\\GPU Process Memory(" + prefix + L")\\Dedicated Usage").c_str(), 0, &impl_->dedicated) == ERROR_SUCCESS &&
                     PdhAddEnglishCounterW(impl_->pdh, (L"\\GPU Process Memory(" + prefix + L")\\Shared Usage").c_str(), 0, &impl_->shared) == ERROR_SUCCESS &&
                     PdhCollectQueryData(impl_->pdh) == ERROR_SUCCESS;
    }
}
ProcessProbe::~ProcessProbe() {
    if (impl_->pdh) PdhCloseQuery(impl_->pdh);
    if (impl_->process) CloseHandle(impl_->process);
}
bool ProcessProbe::exited() const noexcept { return WaitForSingleObject(impl_->process, 0) == WAIT_OBJECT_0; }
bool ProcessProbe::gpuCounters() const noexcept { return impl_->gpu; }

ResourceSample ProcessProbe::sample() {
    ResourceSample s;
    s.time = gateClock();
    FILETIME created{}, ended{}, kernel{}, user{};
    if (!GetProcessTimes(impl_->process, &created, &ended, &kernel, &user)) throw std::runtime_error("Process times are unavailable");
    s.cpuSeconds = static_cast<double>(fileTime(kernel) + fileTime(user)) / 1e7;
    DWORD handles{};
    GetProcessHandleCount(impl_->process, &handles);
    s.handles = handles;
    s.gdiObjects = GetGuiResources(impl_->process, GR_GDIOBJECTS);
    s.userObjects = GetGuiResources(impl_->process, GR_USEROBJECTS);
    PROCESS_MEMORY_COUNTERS_EX memory{};
    if (GetProcessMemoryInfo(impl_->process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        s.privateBytes = memory.PrivateUsage;
        s.workingSet = memory.WorkingSetSize;
    }
    if (!impl_->threads(s.contextSwitches, s.threads) && !exited()) throw std::runtime_error("Thread activity is unavailable");
    impl_->currentTime = s.time;
    if (impl_->gpu && PdhCollectQueryData(impl_->pdh) == ERROR_SUCCESS) {
        const auto busy = pdhSum(impl_->engine, PDH_FMT_DOUBLE);
        if (impl_->lastGpuTime > 0) impl_->gpuSeconds += busy / 100.0 * (s.time - impl_->lastGpuTime);
        impl_->lastGpuTime = s.time;
        s.gpuSeconds = impl_->gpuSeconds;
        s.gpuDedicated = static_cast<std::uint64_t>(pdhSum(impl_->dedicated, PDH_FMT_LARGE));
        s.gpuShared = static_cast<std::uint64_t>(pdhSum(impl_->shared, PDH_FMT_LARGE));
    }
    return s;
}
void ProcessProbe::markThreads() {
    impl_->mark = impl_->current;
    impl_->markTime = impl_->currentTime;
}
std::vector<ThreadActivity> ProcessProbe::threadsSinceMark(std::size_t count) {
    const auto seconds = impl_->currentTime - impl_->markTime;
    std::vector<ThreadActivity> out;
    if (seconds <= 0) return out;
    for (const auto& [tid, switches] : impl_->current) {
        const auto before = impl_->mark.find(tid);
        const auto delta = switches - (before == impl_->mark.end() ? 0u : std::min(before->second, switches));
        out.push_back({tid, {}, {}, static_cast<double>(delta) / seconds});
    }
    std::sort(out.begin(), out.end(), [](const ThreadActivity& a, const ThreadActivity& b) { return a.wakeupsPerSecond > b.wakeupsPerSecond; });
    if (out.size() > count) out.resize(count);
    for (auto& t : out) {
        t.name = Impl::description(t.id);
        t.start = impl_->startModule(t.id);
    }
    return out;
}

MachineInfo describeMachine() {
    MachineInfo m;
    using GetVersion = LONG(NTAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW version{sizeof(version)};
    if (const auto get = ntdll<GetVersion>("RtlGetVersion"); get && get(&version) >= 0) {
        DWORD ubr{}, bytes = sizeof(ubr);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &bytes);
        wchar_t display[64]{};
        DWORD displayBytes = sizeof(display);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion", RRF_RT_REG_SZ, nullptr, display, &displayBytes);
        m.os = "Windows " + std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) + "." +
               std::to_string(version.dwBuildNumber) + "." + std::to_string(ubr) + (display[0] ? " (" + narrowText(display) + ")" : std::string());
    }
    wchar_t cpu[256]{};
    DWORD cpuBytes = sizeof(cpu);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu, &cpuBytes) == ERROR_SUCCESS)
        m.cpu = narrowText(cpu);
    m.logicalProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) m.memoryBytes = memory.ullTotalPhys;
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT n = 0; factory->EnumAdapters1(n, &adapter) != DXGI_ERROR_NOT_FOUND; ++n, adapter.Reset()) {
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc))) continue;
            m.gpus.push_back(narrowText(desc.Description) + ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " (software)" : "") + ", " +
                             std::to_string(desc.DedicatedVideoMemory / (1024 * 1024)) + " MiB dedicated");
        }
    }
    m.dpi = GetDpiForSystem();
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
        m.refreshHz = mode.dmDisplayFrequency;
        m.displayWidth = mode.dmPelsWidth;
        m.displayHeight = mode.dmPelsHeight;
    }
    DWORD session{};
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    m.session = (session == WTSGetActiveConsoleSessionId() ? "interactive console session " : "non-console session ") + std::to_string(session);
    return m;
}

std::wstring commandLine(const fs::path& executable, const std::vector<std::wstring>& arguments) {
    const auto quote = [](std::wstring_view value) {
        std::wstring out = L"\"";
        std::size_t backslashes = 0;
        for (const auto c : value) {
            if (c == L'\\') { ++backslashes; out += c; continue; }
            if (c == L'"') out.append(backslashes + 1, L'\\');
            backslashes = 0;
            out += c;
        }
        out.append(backslashes, L'\\');
        return out + L"\"";
    };
    std::wstring command = quote(executable.native());
    for (const auto& a : arguments) command += L" " + quote(a);
    return command;
}

GateProcess::GateProcess(const fs::path& executable, const std::vector<std::wstring>& arguments, const fs::path& folder) {
    auto command = commandLine(executable, arguments);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup, &info))
        throw std::runtime_error("The measured program could not be started");
    CloseHandle(info.hThread);
    process_ = info.hProcess;
    id_ = info.dwProcessId;
    FILETIME created{}, ended{}, kernel{}, user{};
    GetProcessTimes(info.hProcess, &created, &ended, &kernel, &user);
    startTime_ = fileTime(created);
}
GateProcess::~GateProcess() {
    if (running()) (void)stopGracefully(30000);
    if (process_) CloseHandle(static_cast<HANDLE>(process_));
}
std::uint32_t GateProcess::id() const noexcept { return id_; }
bool GateProcess::waitIdle(std::uint32_t milliseconds) const { return WaitForInputIdle(static_cast<HANDLE>(process_), milliseconds) == 0; }
bool GateProcess::running() const noexcept { return process_ && WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_TIMEOUT; }
bool GateProcess::stopGracefully(std::uint32_t milliseconds) {
    if (!running()) return true;
    DWORD session{};
    WCHAR key[CCH_RM_SESSION_KEY + 1]{};
    bool graceful = false;
    if (RmStartSession(&session, 0, key) == ERROR_SUCCESS) {
        RM_UNIQUE_PROCESS unique{};
        unique.dwProcessId = id_;
        unique.ProcessStartTime.dwLowDateTime = static_cast<DWORD>(startTime_);
        unique.ProcessStartTime.dwHighDateTime = static_cast<DWORD>(startTime_ >> 32);
        if (RmRegisterResources(session, 0, nullptr, 1, &unique, 0, nullptr) == ERROR_SUCCESS && RmShutdown(session, 0, nullptr) == ERROR_SUCCESS)
            graceful = WaitForSingleObject(static_cast<HANDLE>(process_), milliseconds) == WAIT_OBJECT_0;
        RmEndSession(session);
    }
    if (!graceful && running()) {
        TerminateProcess(static_cast<HANDLE>(process_), 1);
        WaitForSingleObject(static_cast<HANDLE>(process_), 5000);
    }
    return graceful;
}

std::optional<std::uint32_t> runToCompletion(const fs::path& executable, const std::vector<std::wstring>& arguments, const fs::path& folder,
                                             std::uint32_t milliseconds) {
    auto command = commandLine(executable, arguments);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup, &info)) return std::nullopt;
    CloseHandle(info.hThread);
    std::optional<std::uint32_t> result;
    if (WaitForSingleObject(info.hProcess, milliseconds) == WAIT_OBJECT_0) {
        DWORD code{};
        GetExitCodeProcess(info.hProcess, &code);
        result = code;
    } else {
        TerminateProcess(info.hProcess, 1);
    }
    CloseHandle(info.hProcess);
    return result;
}
} // namespace endfield::packaging
#endif
