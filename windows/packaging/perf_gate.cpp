#include "packaging/perf_gate.hpp"
#include <algorithm>
#include <stdexcept>

namespace endfield::packaging {
namespace {
void gate(std::vector<GateResult>& out, std::string name, std::string unit, double value, const std::optional<double>& limit) {
    if (!limit) return;
    out.push_back({std::move(name), std::move(unit), value, *limit, value <= *limit});
}
} // namespace

double median(std::vector<double> values) {
    if (values.empty()) throw std::invalid_argument("No values");
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}

IdleSummary summarizeIdle(std::span<const ResourceSample> samples) {
    if (samples.size() < 2) throw std::invalid_argument("An idle window needs at least two samples");
    for (std::size_t n = 1; n < samples.size(); ++n)
        if (!(samples[n].time > samples[n - 1].time)) throw std::invalid_argument("Idle samples must advance in time");
    const auto& first = samples.front();
    const auto& last = samples.back();
    IdleSummary s;
    s.seconds = last.time - first.time;
    s.cpuPercent = std::max(0.0, last.cpuSeconds - first.cpuSeconds) / s.seconds * 100.0;
    s.wakeupsPerSecond = last.contextSwitches >= first.contextSwitches ? static_cast<double>(last.contextSwitches - first.contextSwitches) / s.seconds : 0.0;
    if (first.gpuSeconds && last.gpuSeconds) s.gpuPercent = std::max(0.0, *last.gpuSeconds - *first.gpuSeconds) / s.seconds * 100.0;
    s.privateGrowthBytes = static_cast<double>(last.privateBytes) - static_cast<double>(first.privateBytes);
    s.privateBytes = last.privateBytes;
    s.workingSet = last.workingSet;
    s.threads = last.threads;
    s.handles = last.handles;
    s.gdiObjects = last.gdiObjects;
    s.userObjects = last.userObjects;
    return s;
}

std::vector<GateResult> gateIdle(const IdleSummary& s, const IdleLimits& l) {
    std::vector<GateResult> out;
    gate(out, "cpu", "% of one core", s.cpuPercent, l.cpuPercent);
    gate(out, "wakeups", "context switches/s", s.wakeupsPerSecond, l.wakeupsPerSecond);
    if (s.gpuPercent) gate(out, "gpu", "% busy", *s.gpuPercent, l.gpuPercent);
    gate(out, "private-growth", "bytes", s.privateGrowthBytes, l.privateGrowthBytes);
    return out;
}

std::vector<Growth> lifecycleGrowth(std::span<const ResourceSample> cycles, const LifecycleLimits& l) {
    if (l.window == 0 || cycles.size() < l.warmup + 2 * l.window)
        throw std::invalid_argument("A lifecycle run needs warm-up plus two comparison windows of cycles");
    const auto measured = cycles.subspan(l.warmup);
    const auto baseline = measured.first(l.window);
    const auto latest = measured.last(l.window);
    std::vector<Growth> out;
    const auto add = [&](std::string metric, auto value) {
        std::vector<double> a, b;
        for (const auto& s : baseline) a.push_back(value(s));
        for (const auto& s : latest) b.push_back(value(s));
        Growth g{std::move(metric), median(a), median(b), 0};
        g.growth = g.latest - g.baseline;
        out.push_back(std::move(g));
    };
    add("handles", [](const ResourceSample& s) { return static_cast<double>(s.handles); });
    add("threads", [](const ResourceSample& s) { return static_cast<double>(s.threads); });
    add("gdiObjects", [](const ResourceSample& s) { return static_cast<double>(s.gdiObjects); });
    add("userObjects", [](const ResourceSample& s) { return static_cast<double>(s.userObjects); });
    add("privateBytes", [](const ResourceSample& s) { return static_cast<double>(s.privateBytes); });
    add("workingSet", [](const ResourceSample& s) { return static_cast<double>(s.workingSet); });
    const bool gpu = std::all_of(cycles.begin(), cycles.end(), [](const ResourceSample& s) { return s.gpuDedicated.has_value(); });
    if (gpu) add("gpuDedicated", [](const ResourceSample& s) { return static_cast<double>(*s.gpuDedicated); });
    return out;
}

std::vector<GateResult> gateLifecycle(std::span<const Growth> growth, const LifecycleLimits& l) {
    std::vector<GateResult> out;
    for (const auto& g : growth) {
        const std::optional<double>* limit = g.metric == "handles" ? &l.handles : g.metric == "threads" ? &l.threads
            : g.metric == "gdiObjects" ? &l.gdiObjects : g.metric == "userObjects" ? &l.userObjects : g.metric == "privateBytes" ? &l.privateBytes
            : g.metric == "workingSet" ? &l.workingSet : g.metric == "gpuDedicated" ? &l.gpuDedicated : nullptr;
        if (limit) gate(out, g.metric + "-growth", g.metric.find("Bytes") != std::string::npos || g.metric == "workingSet" || g.metric == "gpuDedicated" ? "bytes" : "count",
                        g.growth, *limit);
    }
    return out;
}

bool allPassed(std::span<const GateResult> results) noexcept {
    return std::all_of(results.begin(), results.end(), [](const GateResult& r) { return r.passed; });
}
} // namespace endfield::packaging
