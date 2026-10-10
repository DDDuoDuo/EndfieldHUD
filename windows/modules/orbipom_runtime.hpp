#pragma once
#include "modules/orbipom_state.hpp"
#include <filesystem>
#include <string_view>

namespace endfield::modules {
struct OrbiPomRuntimeOptions {
    // Explicit bundled Resources directory, containing OrbiPom/*.js. Files are
    // read once when the first command needs the VM, and verified against the
    // original source SHA256 pins. No search, fallback path or script copies.
    std::filesystem::path resourceRoot;
    // Heap range64KiB...256MiB; snapshot body buffer <= heap; stack16...256KiB.
    // Budgets are positive and <=60s. Defaults are failure guards, not cadence.
    std::size_t memoryBytes{32*1024*1024},stackBytes{256*1024},snapshotBytes{4*1024*1024};
    double commandSeconds{.1},initializationSeconds{2};
};
struct OrbiPomRuntimeStats {
    bool initialized{},faulted{};std::uint64_t boots{},calls{},advances{},interrupts{};
    std::size_t heapBytes{},snapshotCapacityBytes{};double totalCommandSeconds{},maximumCommandSeconds{};
};
// Exact offline Matter0.20.0 + bundled original-controller bridge, executed by
// pinned QuickJS-NG0.17.0. One lazy, creating-thread VM; no libc/OS/network
// bindings, native timers, event loop, worker, image service or shader/device.
// Commands publish typed snapshots without JSON encoding or per-body handles.
// A limit/exception freezes the last complete snapshot and exposes an error;
// a fresh Start recreates a faulted VM, preserving the requested local best.
class OrbiPomRuntime final:public OrbiPomRuntimePort {
public:
    explicit OrbiPomRuntime(OrbiPomRuntimeOptions);
    ~OrbiPomRuntime()override;
    OrbiPomRuntime(const OrbiPomRuntime&)=delete;OrbiPomRuntime&operator=(const OrbiPomRuntime&)=delete;
    const OrbiPomSnapshot&snapshot()const noexcept override;
    const std::optional<std::string>&error()const noexcept override;
    void start(std::optional<std::uint32_t> seed={})override;void advance(double seconds)override;
    void move(core::Point)override;void pointerUp(core::Point)override;
    bool drop()override;bool activate(OrbiPomSkill)override;
    void cancelSkill()override;void pause(bool)override;void highScore(std::int64_t)override;
    OrbiPomRuntimeStats stats()const;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
    // Isolated unit tests exercise original private reducers/physics directly,
    // like macOS evaluateForTesting. No evaluation entry point is public.
    friend struct OrbiPomRuntimeTestAccess;
    std::string evaluateForTesting(std::string_view);
    static std::size_t liveVMsForTesting()noexcept;
};
}
