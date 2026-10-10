#include "packaging/perf_runner.hpp"
#ifdef _WIN32
#include "core/data/application_root.hpp"
#include "core/data/data_store.hpp"
#include "core/data/json.hpp"
#include "packaging/package_io.hpp"
#include "packaging/perf_probe.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
void sleepSeconds(double seconds) {
    if (seconds > 0) Sleep(static_cast<DWORD>(std::lround(seconds * 1000)));
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
void prepareDataRoot(const fs::path& base, bool ambient) {
    const auto root = ehud::data::openApplicationRoot(base);
    ehud::data::SettingsStore store(root.data);
    auto value = store.value();
    value.set("launchAtLogin", false);       // never a startup entry from a gate run
    value.set("ambientAnimation", ambient);
    (void)store.update(value);
    (void)store.markLaunched();              // no first-run onboarding
}
} // namespace

bool supportsGateControl(const fs::path& executable) {
    std::ifstream in(executable, std::ios::binary);
    if (!in) return false;
    const std::string image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return image.size() > 2 && image[0] == 'M' && image[1] == 'Z' && image.find("--data-root") != std::string::npos && image.find("--toggle") != std::string::npos;
}

GateRun runPerformanceGate(const GateOptions& o) {
    const bool app = o.target == GateOptions::Target::endfieldhud;
    if (app && !supportsGateControl(o.executable))
        throw std::runtime_error("This EndfieldHUD.exe has no --data-root/--toggle support, so the gate does not run it (it would use the real data root)");
    if (o.interval <= 0 || o.seconds < 2 * o.interval) throw std::runtime_error("The sampling window needs at least two intervals");
    auto work = o.workFolder;
    if (work.empty()) {
        std::wstring temp(MAX_PATH + 1, L'\0');
        temp.resize(GetTempPathW(static_cast<DWORD>(temp.size()), temp.data()));
        work = fs::path(temp).lexically_normal().parent_path();
    }
    const auto id = randomId();
    const auto container = work / (L"EndfieldHUD-perf-" + id);
    const auto base = container / L"EndfieldHUD";
    fs::create_directories(container);
    const auto folder = o.executable.parent_path();
    const auto fixture = L"gate-" + id;
    std::vector<std::wstring> primary, reopen, toggle;
    if (app) {
        primary = {L"--data-root", base.native(), L"--login"};
        reopen = {L"--data-root", base.native()};
        toggle = {L"--data-root", base.native(), L"--toggle"};
    } else {
        primary = {L"--perf", fixture};
        for (const auto& a : o.fixtureArguments) primary.push_back(a);
        reopen = toggle = {L"--perf", fixture, L"--toggle"};
    }

    GateRun run;
    Json::Array scenarios;
    const auto failed = [&](const std::string& what) { run.failures.push_back(what); };
    const auto forward = [&](const std::vector<std::wstring>& args) {
        const auto code = runToCompletion(o.executable, args, folder, 15000);
        if (code != 0u) throw std::runtime_error("A forwarded launch did not complete (exit " + (code ? std::to_string(*code) : std::string("timeout")) + ")");
    };
    const auto idle = [&](ProcessProbe& probe, const std::string& name, const IdleLimits& limits) {
        std::vector<ResourceSample> samples{probe.sample()};
        probe.markThreads();
        const auto start = samples.front().time;
        while (samples.back().time - start < o.seconds) {
            const auto next = start + o.interval * static_cast<double>(samples.size());
            sleepSeconds(next - gateClock());
            samples.push_back(probe.sample());
            if (probe.exited()) throw std::runtime_error("The measured process exited during " + name);
        }
        const auto summary = summarizeIdle(samples);
        const auto gates = gateIdle(summary, limits);
        Json::Object scenario;
        scenario["name"] = name;
        scenario["summary"] = idleJson(summary);
        scenario["limits"] = limitsJson(limits);
        scenario["busiestThreads"] = threadsJson(probe.threadsSinceMark(8));
        scenario["gates"] = gatesJson(gates);
        scenario["passed"] = allPassed(gates);
        for (const auto& g : gates) if (!g.passed) failed(name + ": " + g.name + " " + std::to_string(g.value) + " > " + std::to_string(g.limit) + " " + g.unit);
        scenarios.emplace_back(std::move(scenario));
    };
    const auto session = [&](bool ambient, bool closed, bool open, bool lifecycle, const std::string& openName, const IdleLimits& openLimits) {
        if (app) prepareDataRoot(base, ambient);
        GateProcess process(o.executable, primary, folder);
        if (!process.waitIdle(60000)) failed("The measured program did not reach its message loop within 60 s");
        sleepSeconds(o.settle);
        if (!process.running()) throw std::runtime_error("The measured program exited during startup");
        ProcessProbe probe(process.id());
        if (closed) idle(probe, "closed-idle", o.closedLimits);
        if (open || lifecycle) {
            forward(reopen);
            sleepSeconds(o.openSettle + o.settle);
        }
        if (open) idle(probe, openName, openLimits);
        if (lifecycle) {
            forward(toggle); // close
            sleepSeconds(o.closeSettle);
            std::vector<ResourceSample> cycles;
            Json::Array perCycle;
            for (std::size_t n = 0; n < o.cycles; ++n) {
                forward(toggle);
                sleepSeconds(o.openSettle);
                forward(toggle);
                sleepSeconds(o.closeSettle);
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
            scenario["warmupCycles"] = static_cast<std::int64_t>(o.lifecycleLimits.warmup);
            scenario["windowCycles"] = static_cast<std::int64_t>(o.lifecycleLimits.window);
            scenario["growth"] = std::move(growthRows);
            scenario["cycles"] = std::move(perCycle);
            scenario["gates"] = gatesJson(gates);
            scenario["passed"] = allPassed(gates);
            for (const auto& g : gates) if (!g.passed) failed("lifecycle: " + g.name + " " + std::to_string(g.value) + " > " + std::to_string(g.limit) + " " + g.unit);
            scenarios.emplace_back(std::move(scenario));
        }
        if (!process.stopGracefully(30000)) failed("The measured program did not exit through the session-end protocol");
        return probe.gpuCounters();
    };

    bool gpu{};
    std::string error;
    try {
        gpu = session(true, o.closed, o.open, o.lifecycle, app ? "open-idle-ambient-on" : "open-idle", o.openLimits);
        if (app && o.ambientOff) gpu = session(false, false, true, false, "open-idle-ambient-off", o.ambientOffLimits) && gpu;
    } catch (const std::exception& e) {
        error = e.what();
        failed(error);
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
    settings["dataRoot"] = app ? "temporary (deleted after the run)" : "none";
    Json::Object report;
    report["format"] = "EndfieldHUD.Windows.PerformanceGate";
    report["schema"] = 1;
    report["measuredAt"] = utcNow();
    report["machine"] = std::move(m);
    report["target"] = std::move(target);
    report["settings"] = std::move(settings);
    report["scenarios"] = std::move(scenarios);
    report["gpuCounters"] = gpu ? "available" : "unavailable in this session";
    report["notes"] = Json::Array{
        Json("Wakeups are thread context switches of the measured process (all threads), sampled with NtQuerySystemInformation."),
        Json("CPU is user+kernel time of the measured process as a percentage of one core; compositor and GPU work in other processes are not included."),
        Json("Limits are provisional engineering gates, not Mac parity figures; values are reported regardless.")};
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
