#pragma once
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>

namespace endfield::app {
// One app-owned queue for short, bounded file work. No module owns a polling
// thread. Work receives immutable/value-owned inputs; it must not touch UI or
// wait for UI callbacks. Completions run only inside owner-thread drain().
class UtilityExecutor final {
public:
    using Route=std::uint64_t;
    using Work=std::function<void()>;
    using Completion=std::function<void(std::exception_ptr)>;
    struct Stats { std::size_t pending{},running{},completed{};std::uint64_t accepted{},executed{},discarded{};bool started{}; };
    // Called from the worker after a result becomes available. Post one owner
    // message only; do not execute UI code or retain a raw window/view here.
    explicit UtilityExecutor(std::function<void()> notify,std::size_t capacity=32);
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
