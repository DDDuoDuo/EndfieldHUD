// WINDOWS-MIGRATION.md section 9 gate tool: closed idle, open idle (ambient
// animation on and off) and repeated open/close lifecycle of a real
// EndfieldHUD.exe, measured from outside with a temporary data root.
//
//   package_perf_gate --exe <EndfieldHUD.exe> --report <report.json>
//       [--scenarios closed,open,ambient-off,lifecycle] [--seconds 60] [--settle 10]
//       [--cycles 100] [--open-settle 1.5] [--close-settle 1.5] [--interval 1]
//       [--limit <scenario>.<metric>=<value>]... [--keep-data]
//
// Limits: closed|open|ambient-off . cpu|wakeups|gpu|private-growth and
// lifecycle . handles|threads|gdi|user|private|working-set|gpu-dedicated|warmup|window
// ("none" removes a limit). Exit code: 0 every gate passed, 1 a gate failed,
// 2 the run could not be performed (for example an EndfieldHUD.exe without
// --data-root support, which is never started).
//
// Run it in an interactive desktop session on the target laptop with nothing
// else of EndfieldHUD's running; the report records the machine, display,
// session and settings with every result.
#include "packaging/perf_runner.hpp"
#include <charconv>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace p = endfield::packaging;
namespace fs = std::filesystem;
namespace {
std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const auto n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
double number(const std::string& text) {
    double value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 0) throw std::invalid_argument("Not a non-negative number: " + text);
    return value;
}
void setLimit(p::GateOptions& o, const std::string& assignment) {
    const auto equals = assignment.find('='), dot = assignment.find('.');
    if (equals == std::string::npos || dot == std::string::npos || dot > equals) throw std::invalid_argument("--limit needs <scenario>.<metric>=<value>");
    const auto scenario = assignment.substr(0, dot), metric = assignment.substr(dot + 1, equals - dot - 1), text = assignment.substr(equals + 1);
    const std::optional<double> value = text == "none" ? std::nullopt : std::optional<double>(number(text));
    if (scenario == "closed" || scenario == "open" || scenario == "ambient-off") {
        auto& l = scenario == "closed" ? o.closedLimits : scenario == "open" ? o.openLimits : o.ambientOffLimits;
        if (metric == "cpu") l.cpuPercent = value;
        else if (metric == "wakeups") l.wakeupsPerSecond = value;
        else if (metric == "gpu") l.gpuPercent = value;
        else if (metric == "private-growth") l.privateGrowthBytes = value;
        else throw std::invalid_argument("Unknown idle metric: " + metric);
    } else if (scenario == "lifecycle") {
        auto& l = o.lifecycleLimits;
        if (metric == "warmup" || metric == "window") {
            if (!value || *value < 1 || *value > 1000) throw std::invalid_argument("lifecycle warm-up/window must be 1...1000 cycles");
            (metric == "warmup" ? l.warmup : l.window) = static_cast<std::size_t>(*value);
        } else if (metric == "handles") l.handles = value;
        else if (metric == "threads") l.threads = value;
        else if (metric == "gdi") l.gdiObjects = value;
        else if (metric == "user") l.userObjects = value;
        else if (metric == "private") l.privateBytes = value;
        else if (metric == "working-set") l.workingSet = value;
        else if (metric == "gpu-dedicated") l.gpuDedicated = value;
        else throw std::invalid_argument("Unknown lifecycle metric: " + metric);
    } else {
        throw std::invalid_argument("Unknown scenario: " + scenario);
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); // report the real display DPI
    p::GateOptions o;
    fs::path report;
    try {
        for (int n = 1; n < argc; ++n) {
            const std::wstring name = argv[n];
            const auto wideValue = [&]() -> std::wstring {
                if (n + 1 >= argc) throw std::invalid_argument(narrow(name) + " needs a value");
                return argv[++n];
            };
            const auto value = [&]() { return narrow(wideValue()); };
            if (name == L"--exe") o.executable = fs::path(wideValue()).make_preferred();
            else if (name == L"--report") report = fs::path(wideValue());
            else if (name == L"--target") {
                const auto t = value();
                if (t == "endfieldhud") o.target = p::GateOptions::Target::endfieldhud;
                else if (t == "fixture") o.target = p::GateOptions::Target::fixture;
                else throw std::invalid_argument("--target is endfieldhud or fixture");
            } else if (name == L"--fixture-argument") o.fixtureArguments.push_back(wideValue());
            else if (name == L"--scenarios") {
                const auto list = "," + value() + ",";
                o.closed = list.find(",closed,") != std::string::npos;
                o.open = list.find(",open,") != std::string::npos;
                o.ambientOff = list.find(",ambient-off,") != std::string::npos;
                o.lifecycle = list.find(",lifecycle,") != std::string::npos;
                if (!o.closed && !o.open && !o.ambientOff && !o.lifecycle) throw std::invalid_argument("--scenarios names none of closed,open,ambient-off,lifecycle");
            } else if (name == L"--seconds") o.seconds = number(value());
            else if (name == L"--settle") o.settle = number(value());
            else if (name == L"--open-settle") o.openSettle = number(value());
            else if (name == L"--close-settle") o.closeSettle = number(value());
            else if (name == L"--interval") o.interval = number(value());
            else if (name == L"--cycles") o.cycles = static_cast<std::size_t>(number(value()));
            else if (name == L"--limit") setLimit(o, value());
            else if (name == L"--keep-data") o.keepData = true;
            else throw std::invalid_argument("Unknown argument: " + narrow(name));
        }
        if (o.executable.empty() || report.empty()) throw std::invalid_argument("--exe and --report are required");
        if (o.lifecycle && o.cycles < o.lifecycleLimits.warmup + 2 * o.lifecycleLimits.window)
            throw std::invalid_argument("--cycles must cover the warm-up plus two comparison windows");
    } catch (const std::exception& e) {
        std::cerr << "package_perf_gate: " << e.what() << '\n';
        return 2;
    }
    p::GateRun run;
    try {
        run = p::runPerformanceGate(o);
    } catch (const std::exception& e) {
        std::cerr << "package_perf_gate: " << e.what() << '\n';
        return 2;
    }
    std::ofstream out(report, std::ios::binary | std::ios::trunc);
    out << run.report;
    if (!out) {
        std::cerr << "package_perf_gate: the report could not be written\n";
        return 2;
    }
    std::cout << "EndfieldHUD performance gate: " << (run.passed ? "passed" : "FAILED") << " (" << narrow(report.native()) << ")\n";
    for (const auto& f : run.failures) std::cout << "  " << f << '\n';
    return run.passed ? 0 : 1;
}
