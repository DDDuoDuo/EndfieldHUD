// Portable contracts of the section 9 gate evaluation (synthetic samples),
// the opening timings from EndfieldHUD's startup trace and the readable report.
#include "core/data/json.hpp"
#include "packaging/perf_gate.hpp"
#include "packaging/perf_report.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
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

using ehud::data::Json;
bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }
// One opening in EndfieldHUD's trace format (app/application.cpp
// StartupTrace::report): the first interval is request -> first frame.
std::string openingText(int index, double first, const std::vector<double>& pacing, double cpu) {
    std::string intervals = std::to_string(first), costs = std::to_string(cpu);
    for (const auto v : pacing) { intervals += "," + std::to_string(v); costs += "," + std::to_string(cpu); }
    return "{\"index\":" + std::to_string(index) + ",\"cold\":" + (index == 0 ? "true" : "false") + ",\"requestedAtMilliseconds\":1000," +
           "\"requestToFirstFrameMilliseconds\":" + std::to_string(first) + ",\"requestToOpenedMilliseconds\":640,\"frames\":" +
           std::to_string(pacing.size() + 1) + ",\"frameIntervalsMilliseconds\":[" + intervals + "],\"frameCPUMilliseconds\":[" + costs + "]}";
}
Json traceOf(const std::string& openings) {
    return Json::parse("{\"schema\":1,\"numericOnly\":true,\"preparationMilliseconds\":40,\"launchToFirstFrameMilliseconds\":510.25,"
                       "\"processToFirstFrameMilliseconds\":560,\"startupStages\":[{\"stage\":\"shaders\",\"wallMilliseconds\":120,\"processCPUSeconds\":0.1}],"
                       "\"openings\":[" + openings + "]}");
}

void openings() {
    check(p::percentile({5, 1, 4, 2, 3}, 0.5) == 3 && p::percentile({5, 1, 4, 2, 3}, 0.95) == 5 && p::percentile({7}, 0.95) == 7 &&
          p::percentile({1, 2, 3, 4}, 0) == 1, "Nearest-rank percentiles");
    rejects([] { (void)p::percentile({}, 0.5); }, "No percentile of nothing");
    // Cold: 300 ms to the first frame, then 20 frames at 60 Hz with one
    // 40 ms and one 30 ms hitch; warm: 30 ms, then 20 smooth frames.
    std::vector<double> cold(20, 16.7), warm(20, 16.7);
    cold[3] = 40;
    cold[11] = 30;
    const auto trace = traceOf(openingText(0, 300, cold, 20.5) + "," + openingText(1, 30, warm, 3));
    const auto summaries = p::summarizeOpenings(trace);
    check(summaries.size() == 2 && summaries[0].cold && !summaries[1].cold && summaries[0].frames == 21 && near(summaries[0].firstFrameMS, 300) &&
          near(summaries[0].openedMS, 640), "Two openings with their first-frame and opened times");
    check(near(summaries[0].p50IntervalMS, 16.7) && near(summaries[0].p95IntervalMS, 30) && near(summaries[0].maxIntervalMS, 40) && summaries[0].lateFrames == 2,
          "Cold pacing excludes the request step: p95 is the second hitch, two frames later than 25 ms");
    check(near(summaries[0].meanFrameCPUMS, 20.5) && summaries[0].framesOverBudget == 21 && summaries[1].framesOverBudget == 0 &&
          summaries[1].lateFrames == 0 && near(summaries[1].p95IntervalMS, 16.7), "Frame CPU against the 16.67 ms budget");
    const p::OpeningLimits coldLimits{250.0, 25.0, 3.0}, warmLimits{100.0, 25.0, 1.0};
    auto gates = p::gateOpening(summaries[0], coldLimits);
    check(gates.size() == 3 && !find(gates, "first-frame")->passed && !find(gates, "p95-frame-interval")->passed && find(gates, "late-frames")->passed,
          "A 300 ms cold first frame and a 30 ms p95 fail the provisional cold limits");
    check(p::allPassed(p::gateOpening(summaries[1], warmLimits)), "A smooth warm opening passes");
    check(p::gateOpening(summaries[0], {}).empty(), "No limits, no gates");
    check(p::describeFailure("closed-idle", {"cpu", "% of one core", 0.84, 0.5, false}) == "closed-idle: CPU 0.84 % of one core (limit 0.50 % of one core)" &&
          p::describeFailure("lifecycle-100", {"privateBytes-growth", "bytes", 9437184, 8388608, false}) ==
              "lifecycle-100: Private bytes growth 9.00 MiB (limit 8.00 MiB)" &&
          p::describeFailure("cold opening", {"first-frame", "ms", -1, 250, false}) == "cold opening: Request to first frame never reached (limit 250.0 ms)",
          "Failure lines");
    p::OpeningSummary never;
    never.firstFrameMS = -1;
    gates = p::gateOpening(never, coldLimits);
    check(!find(gates, "first-frame")->passed && !find(gates, "p95-frame-interval")->passed && !find(gates, "late-frames")->passed,
          "An opening that never presented a frame fails every gate");
    // Single-frame opening: no pacing, no late frames.
    const auto single = p::summarizeOpenings(traceOf(openingText(0, 12, {}, 2)));
    check(single.size() == 1 && single[0].frames == 1 && single[0].p95IntervalMS == 0 && single[0].lateFrames == 0, "One frame has no pacing");

    // The report scenario: summaries, gates and failures.
    std::vector<std::string> failures;
    auto scenario = p::openingsScenario(trace, coldLimits, warmLimits, 2, failures);
    check(scenario["kind"].string() == "openings" && !scenario["passed"].boolean() && scenario["openings"].array().size() == 2 &&
          scenario["launchToFirstFrameMilliseconds"].number() == 510.25 && scenario["gates"].array().size() == 6, "Openings scenario");
    check(failures.size() == 2 && failures[0] == "cold opening: Request to first frame 300.0 ms (limit 250.0 ms)" &&
          failures[1] == "cold opening: p95 frame interval 30.0 ms (limit 25.0 ms)",
          "Failures name the opening and the limit: " + (failures.empty() ? std::string() : failures[0]));
    failures.clear();
    scenario = p::openingsScenario(traceOf(openingText(0, 30, warm, 3)), coldLimits, warmLimits, 2, failures);
    check(!scenario["passed"].boolean() && failures.size() == 1 && failures[0] == "openings: 1 of 2 openings were recorded in the startup trace",
          "A missing warm opening fails the scenario");

    // Malformed traces are rejected, never half-read.
    rejects([] { (void)p::summarizeOpenings(Json::parse("{\"schema\":2,\"openings\":[]}")); }, "Another trace schema");
    rejects([] { (void)p::summarizeOpenings(Json::parse("[]")); }, "Not an object");
    rejects([] { (void)p::summarizeOpenings(traceOf("{\"index\":0,\"requestToFirstFrameMilliseconds\":1,\"requestToOpenedMilliseconds\":2,"
                                                    "\"frameIntervalsMilliseconds\":[1,2],\"frameCPUMilliseconds\":[1]}")); }, "Frame lists of different lengths");
    rejects([] { (void)p::summarizeOpenings(traceOf("{\"index\":0,\"requestToFirstFrameMilliseconds\":1,\"requestToOpenedMilliseconds\":2,"
                                                    "\"frameIntervalsMilliseconds\":[1,-2],\"frameCPUMilliseconds\":[1,1]}")); }, "A negative interval");
    rejects([] { (void)p::summarizeOpenings(traceOf("{\"index\":0,\"requestToFirstFrameMilliseconds\":\"x\",\"requestToOpenedMilliseconds\":2,"
                                                    "\"frameIntervalsMilliseconds\":[],\"frameCPUMilliseconds\":[]}")); }, "A text where a number belongs");
}

void footprint() {
    constexpr double mib = 1024.0 * 1024.0;
    // Thread roles: the pool by address, the program by name, drivers and
    // system components by module (case-insensitive), unknown modules empty.
    check(p::threadRole("ntdll.dll", true, "EndfieldHUD.exe") == "Windows thread pool" && p::threadRole("ntdll.dll", false, "EndfieldHUD.exe") == "Windows (ntdll)" &&
          p::threadRole("EndfieldHUD.EXE", false, "EndfieldHUD.exe") == "the program's own threads" &&
          p::threadRole("nvwgf2umx.dll", false, "EndfieldHUD.exe") == "NVIDIA graphics driver" && p::threadRole("igd10iumd64.dll", false, "") == "Intel graphics driver" &&
          p::threadRole("RTWorkQ.DLL", false, "") == "Media Foundation work queues" && p::threadRole("Windows.Media.Control.dll", false, "") == "Windows media (WinRT)" &&
          p::threadRole("d3d10warp.dll", false, "") == "WARP software renderer" && p::threadRole("winhttp.dll", false, "") == "WinHTTP" &&
          p::threadRole("unknown-helper.dll", false, "EndfieldHUD.exe").empty() && p::threadRole("", false, "EndfieldHUD.exe").empty(),
          "Thread roles");
    // Grouping by start and description, largest groups first.
    const auto groups = p::groupThreads({{"ntdll.dll+0x1a2e0", "ntdll.dll", "Windows thread pool", "", 1},
                                         {"EndfieldHUD.exe+0x10", "EndfieldHUD.exe", "the program's own threads", "", 1},
                                         {"ntdll.dll+0x1a2e0", "ntdll.dll", "Windows thread pool", "", 1},
                                         {"EndfieldHUD.exe+0x10", "EndfieldHUD.exe", "the program's own threads", "summon hotkey", 1},
                                         {"ntdll.dll+0x1a2e0", "ntdll.dll", "Windows thread pool", "", 1}});
    check(groups.size() == 3 && groups[0].start == "ntdll.dll+0x1a2e0" && groups[0].count == 3 && groups[1].count == 1 && groups[1].name.empty() &&
          groups[2].name == "summon hotkey", "Threads are grouped by start and description");

    // Gates: only the limits that are set, named by point and metric.
    p::FootprintPoint closed;
    closed.point = "closed";
    closed.sample.privateBytes = static_cast<std::uint64_t>(352.5 * mib);
    closed.sample.workingSet = static_cast<std::uint64_t>(192 * mib);
    closed.sample.peakPrivateBytes = static_cast<std::uint64_t>(360 * mib);
    closed.sample.peakWorkingSet = static_cast<std::uint64_t>(250 * mib);
    closed.sample.threads = 58;
    closed.sample.handles = 826;
    closed.sample.gdiObjects = 46;
    closed.sample.userObjects = 22;
    closed.regions = p::MemoryRegions{static_cast<std::uint64_t>(300 * mib), static_cast<std::uint64_t>(40 * mib), static_cast<std::uint64_t>(80 * mib),
                                      {static_cast<std::uint64_t>(64 * mib), static_cast<std::uint64_t>(15.6 * mib), 1048576}};
    closed.threads = groups;
    closed.limits = {160 * mib, std::nullopt, 40, std::nullopt};
    auto gates = p::gateFootprint(closed);
    check(gates.size() == 2 && gates[0].name == "closed.private-bytes" && gates[0].unit == "bytes" && !gates[0].passed && gates[1].name == "closed.threads" &&
          gates[1].unit == "count" && !gates[1].passed, "Footprint gates over their limits fail");
    auto open = closed;
    open.point = "open";
    open.sample.privateBytes = static_cast<std::uint64_t>(200 * mib);
    open.sample.threads = 60;
    open.sample.gpuDedicated = static_cast<std::uint64_t>(174.5 * mib);
    open.sample.gpuShared = static_cast<std::uint64_t>(20 * mib);
    open.regions.reset();
    open.limits = {256 * mib, 512 * mib, 64, 2000};
    check(p::allPassed(p::gateFootprint(open)) && p::gateFootprint(open).size() == 4, "Footprint gates within their limits pass");
    auto after = closed;
    after.point = "after-cycles";
    after.sample.privateBytes = static_cast<std::uint64_t>(425.5 * mib);
    after.sample.threads = 77;
    after.sample.handles = 942;
    after.threads = {{"nvwgf2umx.dll+0xeb9eb0", "nvwgf2umx.dll", "NVIDIA graphics driver", "", 12}};
    after.limits = {};
    check(p::gateFootprint(after).empty(), "No footprint limits, no gates");

    std::vector<std::string> failures;
    const std::vector<p::FootprintPoint> points{closed, open, after};
    const auto scenario = p::footprintScenario(points, 100, failures);
    check(scenario["kind"].string() == "footprint" && scenario["cycles"].integer() == 100 && scenario["points"].array().size() == 3 && !scenario["passed"].boolean() &&
          scenario["gates"].array().size() == 6, "Footprint scenario");
    const auto& first = scenario["points"].array()[0];
    check(first["point"].string() == "closed" && first["privateBytes"].integer() == static_cast<std::int64_t>(352.5 * mib) && first["threads"].integer() == 58 &&
          first["peakWorkingSetBytes"].integer() == static_cast<std::int64_t>(250 * mib) && first["gpuDedicatedBytes"].isNull() &&
          first["memory"]["largestPrivateAllocations"].array().size() == 3 && first["threadGroups"].array().size() == 3 &&
          first["threadGroups"].array()[0]["role"].string() == "Windows thread pool" && first["limits"]["workingSetBytes"].isNull() &&
          scenario["points"].array()[1]["memory"].isNull() && scenario["points"].array()[1]["gpuSharedBytes"].integer() == static_cast<std::int64_t>(20 * mib),
          "Footprint point values");
    check(failures.size() == 2 && failures[0] == "footprint: Private bytes, closed after launch 352.50 MiB (limit 160.00 MiB)" &&
          failures[1] == "footprint: Threads, closed after launch 58 (limit 40)",
          "Footprint failures name the point and the limit: " + (failures.empty() ? std::string() : failures[0]));

    // Rendered: the summary line, the table per point, what the closed HUD
    // kept after use, the gates and the threads by start address.
    auto parsed = Json::parse(R"json({"format":"EndfieldHUD.Windows.PerformanceGate","schema":2,"measuredAt":"2026-10-15T12:00:00Z",
      "machine":{},"target":{"kind":"endfieldhud"},"settings":{},"scenarios":[],"cancelled":false,"notes":[],"failures":[],"passed":false})json");
    auto object = parsed.object();
    object["scenarios"] = Json(Json::Array{scenario});
    const Json full(object);
    const auto md = p::renderGateMarkdown(full);
    check(contains(md, "| Memory, threads and handles | FAIL | closed after launch 352.5 MiB private, 58 threads; open 200.0 MiB private, 60 threads; "
                       "closed after 100 cycles 425.5 MiB private, 77 threads |"), "Footprint summary row:\n" + md);
    check(contains(md, "## Memory, threads and handles\n") &&
          contains(md, "| Measure | Closed after launch | Open | Closed after 100 cycles |\n| --- | ---: | ---: | ---: |\n") &&
          contains(md, "| Private bytes | 352.50 MiB | 200.00 MiB | 425.50 MiB |") && contains(md, "| Peak working set | 250.00 MiB | 250.00 MiB | 250.00 MiB |") &&
          contains(md, "| Threads | 58 | 60 | 77 |") && contains(md, "| Handles | 826 | 826 | 942 |") && contains(md, "| GDI / USER objects | 46 / 22 | 46 / 22 | 46 / 22 |") &&
          contains(md, "| Dedicated / shared GPU memory | not available | 174.50 / 20.00 MiB | not available |") &&
          contains(md, "| Committed private / mapped / image | 300.00 / 40.00 / 80.00 MiB | not available | 300.00 / 40.00 / 80.00 MiB |") &&
          contains(md, "| Largest private allocations | 64.00, 15.60, 1.00 MiB | not available | 64.00, 15.60, 1.00 MiB |"), "Footprint table:\n" + md);
    check(contains(md, "Kept after use (closed after 100 cycles minus closed after launch): +73.00 MiB private bytes, +19 threads, +116 handles."),
          "What the closed HUD keeps after use");
    check(contains(md, "| Private bytes, closed after launch | 352.50 MiB | 160.00 MiB | FAIL |") && contains(md, "| Threads, closed after launch | 58 | 40 | FAIL |") &&
          contains(md, "| Working set, open | 192.00 MiB | 512.00 MiB | PASS |") && contains(md, "| Handles, open | 826 | 2000 | PASS |"), "Footprint gates");
    check(contains(md, "| Start | Component | Description | Closed after launch | Open | Closed after 100 cycles |") &&
          contains(md, "| ntdll.dll+0x1a2e0 | Windows thread pool | - | 3 | 3 | 0 |") &&
          contains(md, "| nvwgf2umx.dll+0xeb9eb0 | NVIDIA graphics driver | - | 0 | 0 | 12 |") &&
          contains(md, "| EndfieldHUD.exe+0x10 | the program's own threads | summon hotkey | 1 | 1 | 0 |") &&
          md.find("nvwgf2umx.dll+0xeb9eb0") < md.find("| ntdll.dll+0x1a2e0"), "Threads by start address, largest first:\n" + md);
    check(contains(md, "96.5-98.2 MiB with the HUD closed"), "Mac context");
    check(contains(p::summarizeGateReport(full), "  footprint                FAIL  closed after launch 352.5 MiB private, 58 threads"), "Console line");
    // Only the points measured (a cancelled run): no "kept after use" line.
    failures.clear();
    object["scenarios"] = Json(Json::Array{p::footprintScenario(std::span(points).first(1), 0, failures)});
    const auto partial = p::renderGateMarkdown(Json(object));
    check(contains(partial, "| Measure | Closed after launch |\n") && !contains(partial, "Kept after use"), "One point only");
}

void report() {
    // A complete report as perf_runner writes it (schema 2), with one failed
    // gate, an idle window without GPU counters and the openings above.
    std::vector<double> cold(20, 16.7), warm(20, 16.7);
    cold[3] = 40;
    std::vector<std::string> failures;
    const auto trace = traceOf(openingText(0, 180, cold, 4) + "," + openingText(1, 30, warm, 3));
    const auto openingsJson = p::openingsScenario(trace, {250.0, 25.0, 3.0}, {100.0, 25.0, 1.0}, 2, failures);
    check(failures.empty() && openingsJson["passed"].boolean(), "A 180 ms cold opening with one hitch passes");
    auto parsed = Json::parse(R"json({"format":"EndfieldHUD.Windows.PerformanceGate","schema":2,"measuredAt":"2026-10-11T12:00:00Z",
      "machine":{"os":"Windows 10.0.26200.6584 (25H2)","cpu":"Test CPU | 8 cores","logicalProcessors":16,"memoryBytes":17179869184,
        "gpus":["Test GPU, 4096 MiB dedicated"],"dpi":144,"refreshHz":120,"display":"2880x1800","session":"interactive console session 1",
        "interactiveDesktop":true,"otherEndfieldHUDProcesses":1},
      "target":{"executable":"C:\\b\\EndfieldHUD.exe","kind":"endfieldhud","productVersion":"1.2.0.18","sha256":"ab"},
      "settings":{"seconds":60,"settleSeconds":10,"openSettleSeconds":1.5,"closeSettleSeconds":1.5,"sampleIntervalSeconds":1,"cycles":100,
        "dataRoot":"temporary (deleted after the run)"},
      "scenarios":[
        {"name":"closed-idle","kind":"idle","summary":{"seconds":60,"cpuPercentOfOneCore":0.84,"wakeupsPerSecond":12.4,"gpuBusyPercent":null,
          "privateGrowthBytes":1048576,"privateBytes":104857600,"workingSetBytes":157286400,"threads":24,"handles":800,"gdiObjects":40,"userObjects":30},
         "limits":{"cpuPercentOfOneCore":0.5,"wakeupsPerSecond":20,"gpuBusyPercent":0.5,"privateGrowthBytes":4194304},
         "busiestThreads":[{"id":4242,"name":"HUD","start":"EndfieldHUD.exe+0x1234","wakeupsPerSecond":10.5}],
         "gates":[{"name":"cpu","unit":"% of one core","value":0.84,"limit":0.5,"passed":false},
                  {"name":"wakeups","unit":"context switches/s","value":12.4,"limit":20,"passed":true},
                  {"name":"private-growth","unit":"bytes","value":1048576,"limit":4194304,"passed":true}],"passed":false},
        {"name":"lifecycle-100","kind":"lifecycle","warmupCycles":10,"windowCycles":10,
         "growth":[{"metric":"handles","baseline":800,"latest":802,"growth":2},{"metric":"privateBytes","baseline":104857600,"latest":105906176,"growth":1048576}],
         "gates":[{"name":"handles-growth","unit":"count","value":2,"limit":16,"passed":true},
                  {"name":"privateBytes-growth","unit":"bytes","value":1048576,"limit":8388608,"passed":true}],"passed":true}],
      "gpuCounters":"unavailable in this session","cancelled":false,
      "notes":["CPU is user+kernel time."],"failures":["closed-idle: CPU 0.84 % of one core (limit 0.50 % of one core)"],"passed":false})json");
    auto object = parsed.object();
    auto list = object["scenarios"].array();
    list.push_back(openingsJson);
    object["scenarios"] = Json(list);
    object["startupTrace"] = trace;
    const Json full(object);
    const auto md = p::renderGateMarkdown(full);
    check(contains(md, "# EndfieldHUD performance gate: FAILED\n") && contains(md, "- **Program:** C:\\b\\EndfieldHUD.exe, version 1.2.0.18") &&
          !contains(md, "stand-in test program"), "Verdict and program");
    check(contains(md, "Test CPU | 8 cores, 16 logical processors, 16.0 GiB RAM") && contains(md, "2880x1800 at 120 Hz, 144 DPI (150 % scale)") &&
          contains(md, "interactive console session 1, interactive desktop") && contains(md, "1 other EndfieldHUD process(es)"), "Machine, display and session");
    check(contains(md, "| Closed idle | FAIL | CPU 0.84 % of one core, 12.4 wake-ups/s, GPU not available |") &&
          contains(md, "| Open/close lifecycle (100 cycles) | PASS | Handles +2, Private bytes +1.00 MiB |") &&
          contains(md, "| Opening the HUD (cold and warm) | PASS | cold: first frame 180 ms, p95 16.7 ms, 1 late; warm: first frame 30 ms, p95 16.7 ms, 0 late |"),
          "Summary table:\n" + md);
    check(contains(md, "## Failures\n\n- closed-idle: CPU 0.84 % of one core (limit 0.50 % of one core)\n"), "Failures are listed");
    check(contains(md, "| CPU | 0.84 % of one core | 0.50 % of one core | FAIL |") && contains(md, "| GPU busy | not available | 0.50 % | not measured |") &&
          contains(md, "| Private bytes growth | 1.00 MiB | 4.00 MiB | PASS |") && contains(md, "| Threads / handles / GDI / USER objects | 24 / 800 / 40 / 30 | | |") &&
          contains(md, "| 4242 | HUD | EndfieldHUD.exe+0x1234 | 10.5 |"), "Idle details with an unavailable GPU reading");
    check(contains(md, "| Handles | 800.0 | 802.0 | +2 | 16 | PASS |") && contains(md, "| Private bytes | 100.00 MiB | 101.00 MiB | +1.00 MiB | 8.00 MiB | PASS |"),
          "Lifecycle growth");
    check(contains(md, "Launch to the first presented frame: **510 ms** (process creation to first frame: 560 ms); preparation before the window: 40 ms."),
          "Launch timings");
    // The gate's own order: the HUD starts closed and the cold opening comes
    // after the closed idle window, so launch to first frame is mostly waiting.
    {
        auto late = Json::parse("{\"schema\":1,\"preparationMilliseconds\":1098,\"launchToFirstFrameMilliseconds\":70975,"
                                "\"processToFirstFrameMilliseconds\":72217,\"openings\":[" + openingText(0, 89, cold, 4) + "]}").object();
        auto first = late["openings"].array()[0].object();
        first["requestedAtMilliseconds"] = 70886.0;
        late["openings"] = Json(Json::Array{Json(first)});
        std::vector<std::string> ignored;
        auto waited = object;
        waited["scenarios"] = Json(Json::Array{p::openingsScenario(Json(late), {}, {}, 1, ignored)});
        waited["startupTrace"] = Json(late);
        const auto text = p::renderGateMarkdown(Json(waited));
        check(contains(text, "The HUD started closed, and the cold opening was requested 70.9 s after launch (after the closed idle window), so launch to "
                             "the first presented frame (70975 ms) is mostly that wait. Startup until the app was ready, before the window: **1098 ms**.") &&
              !contains(text, "Launch to the first presented frame: **"), "Launch timings of a HUD opened after the closed idle:\n" + text);
        rejects([&] {
            first["requestedAtMilliseconds"] = -1.0;
            late["openings"] = Json(Json::Array{Json(first)});
            (void)p::summarizeOpenings(Json(late));
        }, "A negative request time");
    }
    check(contains(md, "| Request to first frame | 180.0 ms | 30.0 ms |") && contains(md, "| Frame interval p50 / p95 / max | 16.7 / 16.7 / 40.0 ms | 16.7 / 16.7 / 16.7 ms |") &&
          contains(md, "| Frames later than 25 ms | 1 | 0 |") && contains(md, "| Cold: Request to first frame | 180.0 ms | 250.0 ms | PASS |") &&
          contains(md, "| Warm: Frames later than 25 ms | 0 | 1 | PASS |") && contains(md, "| shaders | 120.0 ms | 100.0 ms |"), "Opening tables");
    check(contains(md, "| Open/close cycles | 100 |") && contains(md, "| Open settle (per cycle) | 1.50 s |") && contains(md, "- CPU is user+kernel time.\n") &&
          contains(md, "need an ETW recording"), "Settings and notes");
    const auto summary = p::summarizeGateReport(full);
    check(contains(summary, "  closed-idle              FAIL  CPU 0.84 % of one core") && contains(summary, "  openings                 PASS  cold: first frame 180 ms"),
          "Console summary:\n" + summary);
    // A cancelled run and a foreign document.
    object["cancelled"] = true;
    check(contains(p::renderGateMarkdown(Json(object)), "# EndfieldHUD performance gate: CANCELLED"), "A cancelled run");
    object["scenarios"] = Json(Json::Array{});
    const auto empty = p::renderGateMarkdown(Json(object));
    check(contains(empty, "## Summary\n\nNo scenario was measured.\n") && !contains(empty, "| Scenario | Result |"), "A run that measured nothing says so");
    rejects([] { (void)p::renderGateMarkdown(Json::parse("{\"format\":\"other\"}")); }, "Another document is not rendered");
}
} // namespace

int main() {
    try {
        idle();
        lifecycle();
        openings();
        footprint();
        report();
        std::cout << "Performance gate evaluation: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Performance gate evaluation failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
