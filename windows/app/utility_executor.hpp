#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>

namespace endfield::app {
// One app-owned queue for short, bounded file work. No module owns a polling
// thread. Work receives immutable/value-owned inputs; it must not touch UI or
// wait for UI callbacks. Completions run only inside owner-thread drain().
class UtilityExecutor final {
public:
    using Route=std::uint64_t;
    using Work=std::function<void()>;
    using Completion=std::function<void(std::exception_ptr)>;
    // started: a worker ran at least once; workerAlive: a worker thread exists
    // now; workersStarted: threads started so far (an idle exit and the next
    // submission start a new one).
    struct Stats { std::size_t pending{},running{},completed{};std::uint64_t accepted{},executed{},discarded{};bool started{};bool workerAlive{};std::uint64_t workersStarted{}; };
    // Called from the worker after a result becomes available. Post one owner
    // message only; do not execute UI code or retain a raw window/view here.
    // idleExit > 0: a worker that found no work for that long ends (one
    // bounded wait per busy-to-idle transition, never a periodic wake); the
    // next submit() joins it and starts a fresh worker. Zero keeps the worker
    // until shutdown(). threadName names the worker (SetThreadDescription) so
    // the memory trace and debuggers can tell it apart.
    explicit UtilityExecutor(std::function<void()> notify,std::size_t capacity=32,
        std::chrono::milliseconds idleExit=std::chrono::milliseconds::zero(),std::string threadName={});
    ~UtilityExecutor();
    UtilityExecutor(const UtilityExecutor&)=delete;
    UtilityExecutor&operator=(const UtilityExecutor&)=delete;
    Route makeRoute();
    // Prevents callbacks into a destroyed module. Running work finishes.
    // discardPending=false also finishes every already accepted queued save;
    // true cancels queued disposable work (e.g. an obsolete thumbnail).
    void invalidate(Route,bool discardPending=true);
    // false means full; work was never started. The caller retains its latest
    // immutable save and retries on a completion rather than starting a timer.
    bool submit(Route,Work,Completion);
    // Owner attribution for the host's failure isolation (ModuleRegistry):
    // `current` names the module owner whose call is running when work is
    // submitted (null: the host's own work); drain() hands each attributed
    // completion to `run`, which calls it under that owner's isolation, so a
    // failing completion disables only its module. Owner thread only.
    using Attribution=const void*;
    void attribute(std::function<Attribution()> current,std::function<void(Attribution,const std::function<void()>&)> run);
    std::size_t drain();
    Stats stats()const;
    // Explicit shutdown/flush barrier only; never call during pointer/paint.
    // Runs no callbacks. A following drain() can free completion capacity for
    // one final retained save before shutdown().
    void waitIdle();
    // Stops accepting, finishes accepted work, joins, then discards callbacks.
    // Caller explicitly drains needed results before shutdown. No detached
    // thread or callback survives this function. Long network work belongs in
    // its cancellable service, not this local-file queue.
    void shutdown();
private:
    struct Impl;std::shared_ptr<Impl>impl_;
};
}
