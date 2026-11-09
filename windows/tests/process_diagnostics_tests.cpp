// The thread census the hidden summon-cycle check reads
// (app/process_diagnostics.hpp): the kernel's thread state and wait reason
// are read from the capture, an OS thread-pool worker is recognised by its
// ntdll start (as countPoolWorkers and capturePoolWorkers count them), it is
// busy while it runs a callback and idle (waiting on its worker factory) once
// the callback is done, and an application thread is never a pool worker.
// Synthetic work only; no files, windows or user data.
#include "app/process_diagnostics.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
using endfield::app::ThreadActivity;
unsigned checks{};
void check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL after %u: %s\n", checks, what); std::exit(1); }
}
constexpr std::uint32_t waiting = 5; // KTHREAD_STATE Waiting
// A fresh capture's row for the thread, until `holds` is true of it (or 10 s).
template <class P> bool eventually(DWORD id, P holds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        const auto list = endfield::app::captureThreadActivity();
        const auto row = std::find_if(list.begin(), list.end(), [&](const ThreadActivity& t) { return t.id == id; });
        if (row != list.end() && holds(*row)) return true;
        Sleep(2);
    } while (std::chrono::steady_clock::now() < until);
    return false;
}
struct Work { HANDLE started{}, release{}; std::atomic<DWORD> id{}; };
} // namespace

int main() {
    using namespace endfield::app;
    const auto first = captureThreadActivity();
    check(!first.empty(), "The census reads this process's threads");
    const auto self = std::find_if(first.begin(), first.end(), [](const ThreadActivity& t) { return t.id == GetCurrentThreadId(); });
    check(self != first.end() && !self->exited && !self->poolWorker && !idlePoolWorker(*self), "The main thread is running and no pool worker");

    // An application thread blocked in a wait: Waiting, but never a pool worker.
    const auto release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    check(release != nullptr, "Create the release event");
    std::atomic<DWORD> plainId{};
    std::thread plain([&] { plainId = GetCurrentThreadId(); WaitForSingleObject(release, INFINITE); });
    while (!plainId) Sleep(1);
    check(eventually(plainId, [](const ThreadActivity& t) { return t.state == waiting; }), "An application thread's wait is read from the capture");
    check(eventually(plainId, [](const ThreadActivity& t) { return !t.poolWorker && !t.exited && !idlePoolWorker(t); }), "A waiting application thread is no idle pool worker");

    // A pool callback blocked in its work: a pool worker, but busy.
    Work work; work.started = CreateEventW(nullptr, FALSE, FALSE, nullptr); work.release = release;
    check(work.started != nullptr, "Create the started event");
    check(TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, void* context) {
        auto& w = *static_cast<Work*>(context);
        w.id = GetCurrentThreadId(); SetEvent(w.started); WaitForSingleObject(w.release, INFINITE);
    }, &work, nullptr) != FALSE, "Submit work to the process's default pool");
    check(WaitForSingleObject(work.started, 10000) == WAIT_OBJECT_0, "The pool runs the work");
    const DWORD worker = work.id;
    check(eventually(worker, [](const ThreadActivity& t) { return t.poolWorker && t.state == waiting; }), "The worker running the callback starts in ntdll and waits in it");
    check(eventually(worker, [](const ThreadActivity& t) { return t.poolWorker && t.state == waiting && !idlePoolWorker(t); }), "A worker waiting inside its callback is busy, not idle");
    check(countPoolWorkers() >= 1, "countPoolWorkers counts the worker");
    {
        const auto pool = capturePoolWorkers();
        check(std::any_of(pool.threads.begin(), pool.threads.end(), [&](const PoolWorker& p) { return p.id == worker; }), "capturePoolWorkers lists the same worker");
    }

    // The callback done: the same worker goes back to waiting on its worker factory.
    SetEvent(release);
    plain.join();
    check(eventually(worker, [](const ThreadActivity& t) { return idlePoolWorker(t); }), "The worker is idle (Waiting, WrQueue) once its callback is done");

    CloseHandle(work.started); CloseHandle(release);
    std::printf("process_diagnostics_contracts: %u checks passed\n", checks);
    return 0;
}
