#include "packaging/perf_runner.hpp"
#ifdef _WIN32
#include "core/data/application_root.hpp"
#include "core/data/data_store.hpp"
#include "core/data/json.hpp"
#include "packaging/package_io.hpp"
#include "packaging/perf_probe.hpp"
#include "packaging/perf_report.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

namespace endfield::packaging {
namespace {
namespace fs = std::filesystem;
using ehud::data::Json;

std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const auto n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring randomId() {
    std::array<std::uint8_t, 4> bytes{};
    BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    const auto text = hex(bytes);
    return std::wstring(text.begin(), text.end());
}
struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("Cancelled by the user before the run finished") {}
};
// Waits, or throws Cancelled as soon as the cancel event is set.
void sleepSeconds(double seconds, void* cancel) {
    const auto ms = seconds > 0 ? static_cast<DWORD>(std::lround(seconds * 1000)) : 0;
    if (!cancel) { if (ms) Sleep(ms); return; }
    if (WaitForSingleObject(static_cast<HANDLE>(cancel), ms) == WAIT_OBJECT_0) throw Cancelled();
}
// The executable's image (both checks below look for names in it).
std::string imageOf(const fs::path& executable) {
    std::ifstream in(executable, std::ios::binary);
    if (!in) return {};
    std::string image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return image.size() > 2 && image[0] == 'M' && image[1] == 'Z' ? image : std::string();
}
// "10", "1.5": the grace as EndfieldHUD reads it (wcstod, C locale).
std::wstring graceText(double seconds) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.6g", seconds);
    return std::wstring(text, text + std::strlen(text));
}
std::string utcNow() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    char text[32]{};
    std::snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02uZ", static_cast<unsigned>(t.wYear), static_cast<unsigned>(t.wMonth), static_cast<unsigned>(t.wDay),
                  static_cast<unsigned>(t.wHour), static_cast<unsigned>(t.wMinute), static_cast<unsigned>(t.wSecond));
    return text;
}
std::string productVersion(const fs::path& exe) {
    DWORD ignored{};
    const auto size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
    if (!size) return {};
    std::vector<std::uint8_t> data(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, data.data())) return {};
    wchar_t* value{};
    UINT length{};
    if (!VerQueryValueW(data.data(), L"\\StringFileInfo\\040904B0\\ProductVersion", reinterpret_cast<void**>(&value), &length) || !value) return {};
    return utf8(std::wstring_view(value, length ? length - 1 : 0));
}
Json number(double value) { return Json(std::isfinite(value) ? value : 0.0); }
Json gatesJson(const std::vector<GateResult>& gates) {
    Json::Array out;
    for (const auto& g : gates) {
        Json::Object row;
        row["name"] = g.name;
        row["unit"] = g.unit;
        row["value"] = number(g.value);
        row["limit"] = number(g.limit);
        row["passed"] = g.passed;
        out.emplace_back(std::move(row));
    }
    return out;
}
Json threadsJson(const std::vector<ThreadActivity>& threads) {
    Json::Array out;
    for (const auto& t : threads) {
        Json::Object row;
        row["id"] = static_cast<std::int64_t>(t.id);
        row["name"] = t.name;
        row["start"] = t.start;
        row["wakeupsPerSecond"] = number(t.wakeupsPerSecond);
        out.emplace_back(std::move(row));
    }
    return out;
}
Json idleJson(const IdleSummary& s) {
    Json::Object o;
    o["seconds"] = number(s.seconds);
    o["cpuPercentOfOneCore"] = number(s.cpuPercent);
    o["wakeupsPerSecond"] = number(s.wakeupsPerSecond);
    o["gpuBusyPercent"] = s.gpuPercent ? number(*s.gpuPercent) : Json(nullptr);
    o["privateGrowthBytes"] = number(s.privateGrowthBytes);
    o["privateBytes"] = static_cast<std::int64_t>(s.privateBytes);
    o["workingSetBytes"] = static_cast<std::int64_t>(s.workingSet);
    o["threads"] = static_cast<std::int64_t>(s.threads);
    o["handles"] = static_cast<std::int64_t>(s.handles);
    o["gdiObjects"] = static_cast<std::int64_t>(s.gdiObjects);
    o["userObjects"] = static_cast<std::int64_t>(s.userObjects);
    return o;
}
Json sampleJson(const ResourceSample& s) {
    Json::Object o;
    o["handles"] = static_cast<std::int64_t>(s.handles);
    o["threads"] = static_cast<std::int64_t>(s.threads);
    o["gdiObjects"] = static_cast<std::int64_t>(s.gdiObjects);
    o["userObjects"] = static_cast<std::int64_t>(s.userObjects);
    o["privateBytes"] = static_cast<std::int64_t>(s.privateBytes);
    o["workingSetBytes"] = static_cast<std::int64_t>(s.workingSet);
    o["gpuDedicatedBytes"] = s.gpuDedicated ? Json(static_cast<std::int64_t>(*s.gpuDedicated)) : Json(nullptr);
    return o;
}
Json limitsJson(const IdleLimits& l) {
    Json::Object o;
    const auto put = [&](const char* name, const std::optional<double>& v) { o[name] = v ? number(*v) : Json(nullptr); };
    put("cpuPercentOfOneCore", l.cpuPercent);
    put("wakeupsPerSecond", l.wakeupsPerSecond);
    put("gpuBusyPercent", l.gpuPercent);
    put("privateGrowthBytes", l.privateGrowthBytes);
    return o;
}
// Returns the versioned data folder (<base>\v1).
fs::path prepareDataRoot(const fs::path& base, bool ambient) {
    const auto root = ehud::data::openApplicationRoot(base);
    ehud::data::SettingsStore store(root.data);
    auto value = store.value();
    value.set("launchAtLogin", false);       // never a startup entry from a gate run
    value.set("ambientAnimation", ambient);
    (void)store.update(value);
    (void)store.markLaunched();              // no first-run onboarding
    return root.data;
}
// The app's startup trace, when it was written (bounded, numbers only).
std::optional<Json> readStartupTrace(const fs::path& file) {
    constexpr std::uintmax_t bound = 4 * 1024 * 1024;
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error || size == 0 || size > bound) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && !in.eof()) return std::nullopt;
    try { return Json::parse(text, bound); } catch (const std::exception&) { return std::nullopt; }
}
// EndfieldHUD's fatal record (app/main.cpp: <data>\Diagnostics\last-fatal.txt,
// redacted, at most 4 KiB): the exception, message and stage lines, or
// nullopt when the run wrote none. A fatal error leaves a message box open,
// so the process still runs but does nothing worth measuring.
std::optional<std::string> fatalRecord(const fs::path& file) {
    std::error_code error;
    if (!fs::exists(file, error)) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.size() > 4096) text.resize(4096);
    std::string summary;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        auto line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("exception: ") || line.starts_with("message: ") || line.starts_with("stage: ") || line.starts_with("hresult: "))
            summary += (summary.empty() ? "" : "; ") + line;
        start = end + 1;
    }
    return summary.empty() ? std::string("unreadable fatal record") : summary;
}
} // namespace

double estimatedGateSeconds(const GateOptions& o) {
    constexpr double launch = 3.0, forward = 0.35, stop = 3.0;
    double total{};
    const bool first = o.closed || o.open || o.lifecycle;
    if (first) total += launch + o.settle + stop;
    if (o.closed) total += o.seconds;
    if (o.open || o.lifecycle) total += forward + o.openSettle + o.settle;
    if (o.open) total += o.seconds;
    if (o.lifecycle) total += forward + o.closeSettle + static_cast<double>(o.cycles) * (2 * forward + o.openSettle + o.closeSettle) + o.settle;
    if (o.target == GateOptions::Target::endfieldhud && o.ambientOff) total += launch + o.settle + forward + o.openSettle + o.settle + o.seconds + stop;
    if (o.released) total += launch + o.settle + 2 * forward + o.openSettle + o.settle + releaseWaitGrace(o) + o.settle + o.seconds + stop;
    return total;
}

bool supportsGateControl(const fs::path& executable) {
    const auto image = imageOf(executable);
    return image.find("--data-root") != std::string::npos && image.find("--toggle") != std::string::npos;
}

bool supportsGraceOverride(const fs::path& executable) {
    const auto image = imageOf(executable);
    if (image.empty()) return false;
    const std::string name(graceOverrideVariable);
    std::string wide; // GetEnvironmentVariableW's UTF-16LE literal
    for (const char c : name) {
        wide.push_back(c);
        wide.push_back('\0');
    }
    return image.find(wide) != std::string::npos || image.find(name) != std::string::npos;
}

double releaseWaitGrace(const GateOptions& o) { return supportsGraceOverride(o.executable) ? o.releaseGrace : productionGraphicsGraceSeconds; }

GateRun runPerformanceGate(const GateOptions& o) {
    const bool app = o.target == GateOptions::Target::endfieldhud;
    if (app && !supportsGateControl(o.executable))
        throw std::runtime_error("This EndfieldHUD.exe has no --data-root/--toggle support, so the gate does not run it (it would use the real data root)");
    if (o.interval <= 0 || o.seconds < 2 * o.interval) throw std::runtime_error("The sampling window needs at least two intervals");
    if (o.released && !(o.releaseGrace >= 1 && o.releaseGrace <= 3600)) throw std::runtime_error("The release grace is 1...3600 seconds");
    // The "released" launch shortens the graphics grace when the executable
    // can (EndfieldHUD only with --data-root); otherwise it waits the
    // production ten minutes. Every other launch keeps the production grace.
    const bool graceOverride = o.released && supportsGraceOverride(o.executable);
    const double releaseGrace = graceOverride ? o.releaseGrace : productionGraphicsGraceSeconds;
    auto work = o.workFolder;
    if (work.empty()) {
        std::wstring temp(MAX_PATH + 1, L'\0');
        temp.resize(GetTempPathW(static_cast<DWORD>(temp.size()), temp.data()));
        // %TEMP% may be an 8.3 alias (C:\Users\RUNNER~1\...); the data root
        // and its instance names use the long form.
        work = longPath(fs::path(temp).lexically_normal().parent_path());
    }
    const auto others = countProcesses(L"EndfieldHUD.exe", 0);
    const auto id = randomId();
    const auto container = work / (L"EndfieldHUD-perf-" + id);
    const auto base = container / L"EndfieldHUD";
    fs::create_directories(container);
    const auto folder = o.executable.parent_path();
    const auto fixture = L"gate-" + id;
    // The app writes its trace into its data root; the fixture where told.
    auto traceFile = app ? base / std::string(ehud::data::applicationRootDataDirectory) / "Diagnostics" / "startup-trace.json"
                         : container / "fixture-startup-trace.json";
    const auto fatalFile = app ? base / std::string(ehud::data::applicationRootDataDirectory) / "Diagnostics" / "last-fatal.txt"
                               : container / "fixture-last-fatal.txt";
    const bool tracing = o.startupTrace && (o.open || o.lifecycle);
    const std::size_t expectedOpenings = (o.open || o.lifecycle ? 1u : 0u) + (o.lifecycle ? 1u : 0u);
    std::vector<std::wstring> primary, reopen, toggle;
    if (app) {
        primary = {L"--data-root", base.native(), L"--login"};
        reopen = {L"--data-root", base.native()};
        toggle = {L"--data-root", base.native(), L"--toggle"};
    } else {
        primary = {L"--perf", fixture, L"--fatal-record", fatalFile.native()};
        if (tracing) { primary.push_back(L"--trace"); primary.push_back(traceFile.native()); }
        for (const auto& a : o.fixtureArguments) primary.push_back(a);
        reopen = toggle = {L"--perf", fixture, L"--toggle"};
    }
    const auto wait = [&](double seconds) { sleepSeconds(seconds, o.cancelEvent); };
    std::optional<Json> trace;

    GateRun run;
    Json::Array scenarios;
    std::vector<FootprintPoint> points;
    const auto failed = [&](const std::string& what) { run.failures.push_back(what); };
    // A footprint point: the sample, then committed memory and the thread
    // census taken right after it (outside any measured window).
    const auto footprint = [&](ProcessProbe& probe, const ResourceSample& sample, const char* point, const FootprintLimits& limits) {
        FootprintPoint p;
        p.point = point;
        p.sample = sample;
        p.regions = probe.memoryRegions();
        p.threads = probe.threadCensus();
        p.limits = limits;
        points.push_back(std::move(p));
    };
    // A fatal error ends the measurement with its record (a message box
    // keeps the process alive, so it would otherwise measure as idle).
    const auto checkFatal = [&](const char* when) {
        if (const auto record = fatalRecord(fatalFile)) throw std::runtime_error(std::string("The measured program stopped with a fatal error ") + when + " (" + *record + ")");
    };
    const auto forward = [&](const std::vector<std::wstring>& args) {
        const auto code = runToCompletion(o.executable, args, folder, 15000);
        if (code != 0u) {
            checkFatal("while it was being opened or closed");
            throw std::runtime_error("A forwarded launch did not complete (exit " + (code ? std::to_string(*code) : std::string("timeout")) + ")");
        }
    };
    // What a closed window measures: "within-grace" (the device kept) or
    // "after-release", closed since the "launch" or a "close".
    const auto closedState = [&](const char* state, const char* since, double grace, bool overridden) {
        Json::Object c;
        c["state"] = state;
        c["since"] = since;
        c["graceSeconds"] = number(grace);
        c["productionGraceSeconds"] = number(productionGraphicsGraceSeconds);
        c["graceOverride"] = overridden;
        return c;
    };
    // `closed`: the closed state and since when (gate clock) it was closed.
    const auto idle = [&](ProcessProbe& probe, const std::string& name, const IdleLimits& limits, const char* point, const FootprintLimits& footprintLimits,
                          const std::optional<std::pair<Json::Object, double>>& closed = std::nullopt) {
        std::vector<ResourceSample> samples{probe.sample()};
        probe.markThreads();
        const auto start = samples.front().time;
        while (samples.back().time - start < o.seconds) {
            const auto next = start + o.interval * static_cast<double>(samples.size());
            wait(next - gateClock());
            samples.push_back(probe.sample());
            if (probe.exited()) throw std::runtime_error("The measured process exited during " + name);
        }
        const auto summary = summarizeIdle(samples);
        const auto gates = gateIdle(summary, limits);
        Json::Object scenario;
        scenario["name"] = name;
        scenario["kind"] = "idle";
        if (closed) {
            auto state = closed->first;
            state["closedSeconds"] = number(start - closed->second);
            scenario["closed"] = std::move(state);
        }
        scenario["summary"] = idleJson(summary);
        scenario["limits"] = limitsJson(limits);
        scenario["busiestThreads"] = threadsJson(probe.threadsSinceMark(8));
        scenario["gates"] = gatesJson(gates);
        scenario["passed"] = allPassed(gates);
        for (const auto& g : gates) if (!g.passed) failed(describeFailure(name, g));
        scenarios.emplace_back(std::move(scenario));
        footprint(probe, samples.back(), point, footprintLimits);
    };
    const auto session = [&](bool ambient, bool closed, bool open, bool lifecycle, const std::string& openName, const IdleLimits& openLimits,
                             const char* openPoint, const FootprintLimits& openFootprint, bool traced) {
        if (app) (void)prepareDataRoot(base, ambient);
        {
            std::error_code ignored;
            fs::remove(fatalFile, ignored);
        }
        GateProcess::Environment environment;
        if (traced) {
            std::error_code ignored;
            fs::remove(traceFile, ignored);
            environment.emplace_back(L"ENDFIELDHUD_STARTUP_TRACE", L"1");
        }
        const auto launched = gateClock();
        GateProcess process(o.executable, primary, folder, environment);
        if (!process.waitIdle(60000)) failed("The measured program did not reach its message loop within 60 s");
        wait(o.settle);
        checkFatal("during startup");
        if (!process.running()) throw std::runtime_error("The measured program exited during startup");
        ProcessProbe probe(process.id());
        // Started closed: the production grace runs from the launch prewarm,
        // so the HUD still keeps its graphics device here.
        if (closed)
            idle(probe, "closed-within-grace", o.graceLimits, "closed", o.closedFootprint,
                 std::pair{closedState("within-grace", "launch", productionGraphicsGraceSeconds, false), launched});
        if (open || lifecycle) {
            forward(reopen);
            wait(o.openSettle + o.settle);
            checkFatal("while the HUD opened");
        }
        if (open) idle(probe, openName, openLimits, openPoint, openFootprint);
        if (lifecycle) {
            forward(toggle); // close
            wait(o.closeSettle);
            std::vector<ResourceSample> cycles;
            Json::Array perCycle;
            for (std::size_t n = 0; n < o.cycles; ++n) {
                forward(toggle);
                wait(o.openSettle);
                forward(toggle);
                wait(o.closeSettle);
                cycles.push_back(probe.sample());
                perCycle.push_back(sampleJson(cycles.back()));
                if (probe.exited()) throw std::runtime_error("The measured process exited during the lifecycle cycles");
            }
            const auto growth = lifecycleGrowth(cycles, o.lifecycleLimits);
            const auto gates = gateLifecycle(growth, o.lifecycleLimits);
            Json::Array growthRows;
            for (const auto& g : growth) {
                Json::Object row;
                row["metric"] = g.metric;
                row["baseline"] = number(g.baseline);
                row["latest"] = number(g.latest);
                row["growth"] = number(g.growth);
                growthRows.emplace_back(std::move(row));
            }
            Json::Object scenario;
            scenario["name"] = "lifecycle-" + std::to_string(o.cycles);
            scenario["kind"] = "lifecycle";
            scenario["warmupCycles"] = static_cast<std::int64_t>(o.lifecycleLimits.warmup);
            scenario["windowCycles"] = static_cast<std::int64_t>(o.lifecycleLimits.window);
            scenario["growth"] = std::move(growthRows);
            scenario["cycles"] = std::move(perCycle);
            scenario["gates"] = gatesJson(gates);
            scenario["passed"] = allPassed(gates);
            for (const auto& g : gates) if (!g.passed) failed(describeFailure("lifecycle-" + std::to_string(o.cycles), g));
            scenarios.emplace_back(std::move(scenario));
            // What the closed HUD keeps after use, once deferred releases ran.
            wait(o.settle);
            const auto settled = probe.sample();
            if (probe.exited()) throw std::runtime_error("The measured process exited after the lifecycle cycles");
            footprint(probe, settled, "after-cycles", o.cyclesFootprint);
        }
        checkFatal("during the measurement");
        if (!process.stopGracefully(30000)) failed("The measured program did not exit through the session-end protocol");
        if (traced) {
            // Written once both traced openings completed, or at exit.
            trace = readStartupTrace(traceFile);
            if (!trace) failed("openings: the program wrote no readable startup trace");
            else {
                try {
                    scenarios.emplace_back(openingsScenario(*trace, o.coldOpenLimits, o.warmOpenLimits, expectedOpenings, run.failures));
                } catch (const std::invalid_argument& e) {
                    failed(std::string("openings: the startup trace is malformed (") + e.what() + ")");
                }
            }
        }
        return probe.gpuCounters();
    };
    // Closed after the graphics release: a separate launch (ambient on) with
    // the shortened grace when supported, opened once and closed, then left
    // closed for the grace plus <settle> so the release (and a retry after a
    // media owner let go) has happened before the window.
    const auto releasedSession = [&] {
        if (app) (void)prepareDataRoot(base, true);
        {
            std::error_code ignored;
            fs::remove(fatalFile, ignored);
        }
        GateProcess::Environment environment;
        const std::string variable(graceOverrideVariable);
        if (graceOverride) environment.emplace_back(std::wstring(variable.begin(), variable.end()), graceText(o.releaseGrace));
        GateProcess process(o.executable, primary, folder, environment);
        if (!process.waitIdle(60000)) failed("The measured program did not reach its message loop within 60 s");
        wait(o.settle);
        checkFatal("during startup");
        if (!process.running()) throw std::runtime_error("The measured program exited during startup");
        ProcessProbe probe(process.id());
        forward(reopen);
        wait(o.openSettle + o.settle);
        checkFatal("while the HUD opened");
        forward(toggle); // close
        const auto closedAt = gateClock();
        wait(releaseGrace + o.settle);
        checkFatal("while the HUD stayed closed");
        if (probe.exited()) throw std::runtime_error("The measured process exited while the HUD stayed closed");
        idle(probe, "closed-after-release", o.releasedLimits, "after-release", o.releasedFootprint,
             std::pair{closedState("after-release", "close", releaseGrace, graceOverride), closedAt});
        checkFatal("during the measurement");
        if (!process.stopGracefully(30000)) failed("The measured program did not exit through the session-end protocol");
        return probe.gpuCounters();
    };

    std::optional<bool> gpu;
    const auto counters = [&](bool available) { gpu = gpu.value_or(true) && available; };
    try {
        if (o.closed || o.open || o.lifecycle)
            counters(session(true, o.closed, o.open, o.lifecycle, app ? "open-idle-ambient-on" : "open-idle", o.openLimits, "open", o.openFootprint, tracing));
        if (app && o.ambientOff)
            counters(session(false, false, true, false, "open-idle-ambient-off", o.ambientOffLimits, "open-ambient-off", o.ambientOffFootprint, false));
        if (o.released) counters(releasedSession());
    } catch (const Cancelled& e) {
        run.cancelled = true;
        failed(e.what());
    } catch (const std::exception& e) {
        failed(e.what());
    }
    // The two closed windows side by side (the release launch runs last).
    {
        const auto named = [&](std::string_view name) {
            return std::find_if(scenarios.begin(), scenarios.end(), [&](const Json& s) { return s["name"].isString() && s["name"].string() == name; });
        };
        const auto released = named("closed-after-release");
        if (released != scenarios.end() && named("closed-within-grace") != scenarios.end()) {
            auto moved = std::move(*released);
            scenarios.erase(released);
            scenarios.insert(named("closed-within-grace") + 1, std::move(moved));
        }
    }
    // Memory, threads and handles first: every point measured, also of a
    // cancelled or failed run.
    if (!points.empty()) {
        Json::Array ordered{footprintScenario(points, o.lifecycle ? o.cycles : 0, run.failures)};
        for (auto& s : scenarios) ordered.push_back(std::move(s));
        scenarios = std::move(ordered);
    }
    if (!o.keepData) {
        std::error_code ignored;
        for (int attempt = 0; attempt < 20 && fs::exists(container); ++attempt) {
            fs::remove_all(container, ignored);
            if (fs::exists(container)) Sleep(250);
        }
    }

    const auto machine = describeMachine();
    Json::Object m;
    m["os"] = machine.os;
    m["cpu"] = machine.cpu;
    m["logicalProcessors"] = static_cast<std::int64_t>(machine.logicalProcessors);
    m["memoryBytes"] = static_cast<std::int64_t>(machine.memoryBytes);
    Json::Array gpus;
    for (const auto& g : machine.gpus) gpus.emplace_back(g);
    m["gpus"] = std::move(gpus);
    m["dpi"] = static_cast<std::int64_t>(machine.dpi);
    m["refreshHz"] = static_cast<std::int64_t>(machine.refreshHz);
    m["display"] = std::to_string(machine.displayWidth) + "x" + std::to_string(machine.displayHeight);
    m["session"] = machine.session;
    m["interactiveDesktop"] = machine.interactiveDesktop;
    m["otherEndfieldHUDProcesses"] = static_cast<std::int64_t>(others);
    Json::Object target;
    target["executable"] = utf8(o.executable.native());
    target["kind"] = app ? "endfieldhud" : "fixture";
    target["productVersion"] = productVersion(o.executable);
    try { target["sha256"] = fileSha256Hex(o.executable); } catch (...) { target["sha256"] = nullptr; }
    Json::Object settings;
    settings["seconds"] = number(o.seconds);
    settings["settleSeconds"] = number(o.settle);
    settings["openSettleSeconds"] = number(o.openSettle);
    settings["closeSettleSeconds"] = number(o.closeSettle);
    settings["sampleIntervalSeconds"] = number(o.interval);
    settings["cycles"] = static_cast<std::int64_t>(o.cycles);
    settings["productionGraphicsGraceSeconds"] = number(productionGraphicsGraceSeconds);
    if (o.released) {
        settings["releaseGraceSeconds"] = number(releaseGrace);
        settings["graceOverride"] = graceOverride;
    }
    settings["dataRoot"] = app ? "temporary (deleted after the run)" : "none";
    Json::Object report;
    report["format"] = "EndfieldHUD.Windows.PerformanceGate";
    report["schema"] = 3;
    report["measuredAt"] = utcNow();
    report["machine"] = std::move(m);
    report["target"] = std::move(target);
    report["settings"] = std::move(settings);
    report["scenarios"] = std::move(scenarios);
    report["gpuCounters"] = gpu.value_or(false) ? "available" : "unavailable in this session";
    report["startupTrace"] = trace ? *trace : Json(nullptr);
    report["cancelled"] = run.cancelled;
    report["notes"] = Json::Array{
        Json("Wakeups are thread context switches of the measured process (all threads), sampled with NtQuerySystemInformation."),
        Json("CPU is user+kernel time of the measured process as a percentage of one core; compositor and GPU work in other processes are not included."),
        Json("Limits are provisional engineering gates, not Mac parity figures; values are reported regardless."),
        Json("Closed: EndfieldHUD keeps its graphics device for ten minutes after closing and after launch, so that reopening stays fast, and releases it "
             "afterwards (app/application.hpp closedGraphicsGraceSeconds). Within that grace the live device keeps the graphics driver's thread waking the "
             "process, so that window has a higher wake-up bound; after the release the strict bound applies. The release is measured in a separate launch, "
             "with the grace shortened through ENDFIELDHUD_CLOSED_GRAPHICS_GRACE when the program honours it (EndfieldHUD only with --data-root)."),
        Json("Frame submission: the gate cannot count presents from outside; a closed HUD submits no GPU work, so GPU busy is its measure where Windows "
             "exposes the GPU counters."),
        Json("Opening timings come from the app's own startup trace (ENDFIELDHUD_STARTUP_TRACE=1): the wall time of each presented frame since the opening request and the CPU time of present()."),
        Json("Footprint: private bytes are the process's commit charge (PrivateUsage), peaks PeakPagefileUsage and PeakWorkingSetSize; committed memory by region type and the largest private allocations come from VirtualQueryEx; threads are grouped by their Win32 start address, and Windows thread-pool workers are recognized by the gate's own pool worker.")};
    Json::Array failures;
    for (const auto& f : run.failures) failures.emplace_back(f);
    report["failures"] = std::move(failures);
    run.passed = run.failures.empty();
    report["passed"] = run.passed;
    run.report = Json(std::move(report)).encode(64 * 1024 * 1024) + "\n";
    return run;
}
} // namespace endfield::packaging
#endif
