// Portable contracts of the section 9 gate evaluation (synthetic samples).
#include "packaging/perf_gate.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace p = endfield::packaging;
namespace {
unsigned checks{};
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
bool near(double a, double b) { return std::abs(a - b) < 1e-9 * std::max(1.0, std::abs(b)); }
template <class F> void rejects(F&& f, const std::string& what) {
    bool thrown{};
    try { f(); } catch (const std::invalid_argument&) { thrown = true; }
    check(thrown, what);
}
const p::GateResult* find(const std::vector<p::GateResult>& gates, const std::string& name) {
    for (const auto& g : gates) if (g.name == name) return &g;
    return nullptr;
}

void idle() {
    std::vector<p::ResourceSample> samples;
    for (int n = 0; n <= 60; ++n) {
        p::ResourceSample s;
        s.time = 100.0 + n;
        s.cpuSeconds = 2.0 + 0.0012 * n;          // 0.12 % of one core
        s.contextSwitches = 5000 + 3u * static_cast<unsigned>(n); // 3 wakeups/s
        s.privateBytes = 50'000'000 + 1000u * static_cast<unsigned>(n);
        s.workingSet = 80'000'000;
        s.threads = 12; s.handles = 400; s.gdiObjects = 30; s.userObjects = 20;
        s.gpuSeconds = 0.001 * n;                   // 0.1 % busy
        samples.push_back(s);
    }
    const auto summary = p::summarizeIdle(samples);
    check(near(summary.seconds, 60) && near(summary.cpuPercent, 0.12) && near(summary.wakeupsPerSecond, 3) && near(*summary.gpuPercent, 0.1) &&
          near(summary.privateGrowthBytes, 60000) && summary.handles == 400 && summary.threads == 12, "Idle summary arithmetic");
    auto gates = p::gateIdle(summary, {});
    check(gates.size() == 4 && p::allPassed(gates), "A quiet closed HUD passes the default closed gates");
    check(find(gates, "cpu")->limit == 0.5 && find(gates, "wakeups")->limit == 20, "Default closed limits");
    gates = p::gateIdle(summary, {0.1, 2.0, std::nullopt, std::nullopt});
    check(gates.size() == 2 && !find(gates, "cpu")->passed && !find(gates, "wakeups")->passed && !p::allPassed(gates), "Exceeded limits fail");
    // Without GPU counters the GPU gate is absent, not failed.
    for (auto& s : samples) s.gpuSeconds.reset();
    check(!p::summarizeIdle(samples).gpuPercent && !find(p::gateIdle(p::summarizeIdle(samples), {}), "gpu"), "No GPU counters, no GPU gate");
    // A thread that exits can make the cumulative switch count drop: never negative.
    samples.back().contextSwitches = 10;
    check(p::summarizeIdle(samples).wakeupsPerSecond == 0, "Wakeups never go negative");
    rejects([&] { (void)p::summarizeIdle(std::span(samples).first(1)); }, "One sample is not a window");
    auto stalled = samples;
    stalled[5].time = stalled[4].time;
    rejects([&] { (void)p::summarizeIdle(stalled); }, "Samples must advance in time");
}

void lifecycle() {
    const auto run = [](double leakPerCycle, double noise) {
        std::vector<p::ResourceSample> cycles;
        for (int n = 0; n < 100; ++n) {
            p::ResourceSample s;
            s.time = n;
            const double wobble = (n % 3 - 1) * noise;
            // Caches fill during the first cycles, then the process must plateau.
            const double warm = n < 8 ? n * 20.0 : 160.0;
            s.handles = static_cast<std::uint32_t>(500 + warm + wobble + leakPerCycle * n);
            s.threads = 14;
            s.gdiObjects = static_cast<std::uint32_t>(40 + leakPerCycle * n);
            s.userObjects = 25;
            s.privateBytes = static_cast<std::uint64_t>(60e6 + warm * 1e5 + wobble * 1e5);
            s.workingSet = static_cast<std::uint64_t>(90e6 + warm * 1e5);
            s.gpuDedicated = static_cast<std::uint64_t>(30e6 + leakPerCycle * n * 1e6);
            cycles.push_back(s);
        }
        return cycles;
    };
    p::LifecycleLimits limits;
    auto growth = p::lifecycleGrowth(run(0, 3), limits);
    check(growth.size() == 7, "Every metric, GPU memory included");
    auto gates = p::gateLifecycle(growth, limits);
    check(p::allPassed(gates), "Warm-up growth followed by a plateau passes");
    for (const auto& g : growth) check(std::abs(g.growth) <= 3e5, "A plateau shows no growth: " + g.metric);
    growth = p::lifecycleGrowth(run(1, 3), limits);
    gates = p::gateLifecycle(growth, limits);
    check(!p::allPassed(gates) && !find(gates, "handles-growth")->passed && !find(gates, "gdiObjects-growth")->passed &&
          !find(gates, "gpuDedicated-growth")->passed && find(gates, "threads-growth")->passed, "One leaked handle per cycle is caught");
    check(near(find(gates, "handles-growth")->value, 80), "Growth is the median difference of the first and last windows");
    limits.handles.reset();
    check(!find(p::gateLifecycle(growth, limits), "handles-growth"), "A removed limit is not gated");
    auto noGpu = run(0, 0);
    noGpu[50].gpuDedicated.reset();
    check(p::lifecycleGrowth(noGpu, {}).size() == 6, "GPU memory is gated only when every cycle reported it");
    rejects([&] { (void)p::lifecycleGrowth(std::span(noGpu).first(29), {}); }, "Too few cycles");
    check(p::median({3, 1, 2}) == 2 && p::median({4, 1, 2, 3}) == 2.5, "Median");
}
} // namespace

int main() {
    try {
        idle();
        lifecycle();
        std::cout << "Performance gate evaluation: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Performance gate evaluation failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
