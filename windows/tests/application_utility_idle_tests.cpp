// UtilityExecutor idle worker exit (memory trace: join idle workers). With an
// idle exit the worker thread ends after that long without work and the next
// submit starts a fresh, named one; without it the worker stays until
// shutdown. Synthetic work only; no files, windows or user data.
#include "app/utility_executor.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace {
unsigned checks{};
void check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL after %u: %s\n", checks, what); std::exit(1); }
}
bool threadAlive(DWORD id) {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true;
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL more = Thread32First(snapshot, &entry); more && !found; more = Thread32Next(snapshot, &entry))
        found = entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID == id;
    CloseHandle(snapshot);
    return found;
}
std::wstring description(DWORD id) {
    using Read = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    Read read{};
    for (const auto* module : {L"kernelbase.dll", L"kernel32.dll"})
        if (const auto handle = GetModuleHandleW(module))
            if ((read = reinterpret_cast<Read>(reinterpret_cast<void*>(GetProcAddress(handle, "GetThreadDescription"))))) break;
    if (!read) return L"(unsupported)";
    const auto thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!thread) return {};
    PWSTR text{}; std::wstring out;
    if (SUCCEEDED(read(thread, &text)) && text) { out = text; LocalFree(text); }
    CloseHandle(thread);
    return out;
}
// Runs one task and returns the worker's thread id after its completion drained.
DWORD runOnce(endfield::app::UtilityExecutor& executor, endfield::app::UtilityExecutor::Route route) {
    std::atomic<DWORD> worker{};
    bool completed = false;
    check(executor.submit(route, [&] { worker = GetCurrentThreadId(); }, [&](std::exception_ptr error) { completed = !error; }), "The executor accepts work");
    executor.waitIdle();
    check(executor.drain() == 1 && completed, "The completion drains on the owner thread");
    check(worker.load() != 0 && worker.load() != GetCurrentThreadId(), "The work ran on the executor's worker");
    return worker.load();
}
template <class F> bool eventually(F&& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}
}

int main() {
    using endfield::app::UtilityExecutor;
    using namespace std::chrono_literals;
    {
        // Idle exit: the worker ends, the thread is gone, the next task starts a new named worker.
        UtilityExecutor executor([] {}, 8, 60ms, "EndfieldHUD test worker");
        const auto route = executor.makeRoute();
        check(!executor.stats().started && !executor.stats().workerAlive && executor.stats().workersStarted == 0, "No worker before the first task");
        const auto first = runOnce(executor, route);
        check(executor.stats().workersStarted == 1, "The first task starts one worker");
        const auto named = description(first);
        check(named == L"EndfieldHUD test worker" || named == L"(unsupported)", "The worker carries its description");
        check(eventually([&] { return !executor.stats().workerAlive; }), "An idle worker ends after its idle exit");
        check(eventually([&] { return !threadAlive(first); }), "The idle worker's thread exits");
        const auto second = runOnce(executor, route);
        check(second != first || executor.stats().workersStarted == 2, "The next task starts a fresh worker");
        check(executor.stats().workersStarted == 2 && executor.stats().started, "Exactly one new worker started for the next task");
        // Work queued back to back keeps one worker (the exit waits for idleness only).
        std::atomic<unsigned> ran{};
        for (unsigned n = 0; n < 6; ++n) check(executor.submit(route, [&] { ++ran; std::this_thread::sleep_for(5ms); }, [](std::exception_ptr) {}), "Queued tasks are accepted");
        executor.waitIdle(); (void)executor.drain();
        check(ran == 6 && executor.stats().workersStarted <= 3, "Busy work does not restart the worker per task");
        executor.shutdown();
        check(!executor.stats().workerAlive, "Shutdown leaves no worker");
    }
    {
        // Without an idle exit the worker stays until shutdown (the previous behaviour).
        UtilityExecutor executor([] {}, 8);
        const auto route = executor.makeRoute();
        const auto worker = runOnce(executor, route);
        std::this_thread::sleep_for(200ms);
        check(executor.stats().workerAlive && threadAlive(worker) && executor.stats().workersStarted == 1, "A worker without idle exit stays alive while idle");
        executor.shutdown();
        check(!threadAlive(worker), "Shutdown joins the worker");
    }
    {
        // A worker exiting while the owner submits: no lost task and no deadlock.
        UtilityExecutor executor([] {}, 32, 1ms, "EndfieldHUD race worker");
        const auto route = executor.makeRoute();
        unsigned completed{};
        for (unsigned n = 0; n < 200; ++n) {
            check(executor.submit(route, [] {}, [&](std::exception_ptr error) { completed += !error; }), "Racing submissions are accepted");
            if (n % 7 == 0) std::this_thread::sleep_for(2ms);
            executor.waitIdle(); (void)executor.drain();
        }
        check(completed == 200, "Every task submitted around idle exits completes");
        executor.shutdown();
    }
    // Invalid configuration is rejected.
    bool rejected = false;
    try { UtilityExecutor invalid([] {}, 8, std::chrono::milliseconds(-1)); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "A negative idle exit is rejected");
    std::printf("application_utility_idle_contracts: %u checks\n", checks);
    return 0;
}
