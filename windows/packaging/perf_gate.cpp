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

std::vector<GateResult> gateFootprint(const FootprintPoint& p) {
    std::vector<GateResult> out;
    gate(out, p.point + ".private-bytes", "bytes", static_cast<double>(p.sample.privateBytes), p.limits.privateBytes);
    gate(out, p.point + ".working-set", "bytes", static_cast<double>(p.sample.workingSet), p.limits.workingSet);
    gate(out, p.point + ".threads", "count", static_cast<double>(p.sample.threads), p.limits.threads);
    gate(out, p.point + ".handles", "count", static_cast<double>(p.sample.handles), p.limits.handles);
    return out;
}

namespace {
std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
} // namespace

std::string threadRole(std::string_view module, bool threadPool, std::string_view program) {
    if (threadPool) return "Windows thread pool";
    const auto m = lower(module);
    if (m.empty()) return {};
    if (!program.empty() && m == lower(program)) return "the program's own threads";
    struct Known { std::string_view prefix, role; bool exact; };
    static constexpr Known known[] = {
        {"nvwgf2um", "NVIDIA graphics driver", false}, {"nvd3dum", "NVIDIA graphics driver", false}, {"nvldumd", "NVIDIA graphics driver", false},
        {"nvgpucomp", "NVIDIA graphics driver", false}, {"nvoglv", "NVIDIA graphics driver", false},
        {"igd", "Intel graphics driver", false}, {"igc", "Intel graphics driver", false}, {"igx", "Intel graphics driver", false},
        {"intelocl", "Intel graphics driver", false}, {"amdxx", "AMD graphics driver", false}, {"amdxc", "AMD graphics driver", false},
        {"atidxx", "AMD graphics driver", false}, {"aticfx", "AMD graphics driver", false},
        {"d3d10warp.dll", "WARP software renderer", true}, {"d3d11.dll", "Direct3D 11", true}, {"d3d12.dll", "Direct3D 12", true},
        {"d3d12core.dll", "Direct3D 12", true}, {"dxgi.dll", "DXGI", true}, {"dcomp.dll", "DirectComposition", true},
        {"dwrite.dll", "DirectWrite", true}, {"d2d1.dll", "Direct2D", true}, {"windowscodecs.dll", "WIC", true},
        {"rtworkq.dll", "Media Foundation work queues", true}, {"mfplat.dll", "Media Foundation", true}, {"mf.dll", "Media Foundation", true},
        {"mfreadwrite.dll", "Media Foundation", true}, {"msmpeg2vdec.dll", "Media Foundation decoder", true},
        {"combase.dll", "COM", true}, {"ole32.dll", "COM", true}, {"rpcrt4.dll", "RPC", true},
        {"mmdevapi.dll", "Windows audio", true}, {"audioses.dll", "Windows audio", true}, {"audioeng.dll", "Windows audio", true},
        {"winhttp.dll", "WinHTTP", true}, {"webio.dll", "WinHTTP", true}, {"wininet.dll", "WinINet", true},
        {"msctf.dll", "Text input (TSF)", true}, {"textinputframework.dll", "Text input (TSF)", true},
        {"uiautomationcore.dll", "UI Automation", true}, {"windows.media.dll", "Windows media (WinRT)", true},
        {"windows.media.", "Windows media (WinRT)", false}, {"twinapi.appcore.dll", "WinRT app core", true}, {"twinapi.dll", "WinRT", true},
        {"embeddedbrowserwebview.dll", "WebView2", true}, {"msedgewebview2.exe", "WebView2", true}, {"webview2loader.dll", "WebView2", true},
        {"windows.storage.dll", "Shell", true}, {"shell32.dll", "Shell", true}, {"shcore.dll", "Shell", true},
        {"user32.dll", "Windows UI", true}, {"win32u.dll", "Windows UI", true}, {"gdi32full.dll", "Windows UI", true},
        {"ucrtbase.dll", "C runtime", true}, {"kernel32.dll", "Windows", true}, {"kernelbase.dll", "Windows", true},
        {"ntdll.dll", "Windows (ntdll)", true}, {"pdh.dll", "Performance counters", true}, {"sechost.dll", "Windows services", true},
    };
    for (const auto& k : known)
        if (k.exact ? m == k.prefix : m.starts_with(k.prefix)) return std::string(k.role);
    return {};
}

std::vector<ThreadGroup> groupThreads(std::vector<ThreadGroup> threads) {
    std::vector<ThreadGroup> out;
    for (auto& t : threads) {
        const auto same = std::find_if(out.begin(), out.end(), [&](const ThreadGroup& g) { return g.start == t.start && g.name == t.name; });
        if (same != out.end()) same->count += std::max<std::uint32_t>(1, t.count);
        else {
            t.count = std::max<std::uint32_t>(1, t.count);
            out.push_back(std::move(t));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const ThreadGroup& a, const ThreadGroup& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.start != b.start) return a.start < b.start;
        return a.name < b.name;
    });
    return out;
}

bool allPassed(std::span<const GateResult> results) noexcept {
    return std::all_of(results.begin(), results.end(), [](const GateResult& r) { return r.passed; });
}
} // namespace endfield::packaging
