// WINDOWS-MIGRATION.md section 9 gate tool: closed idle on both sides of the
// graphics grace (within it the HUD keeps its graphics device for fast
// reopening; after it the device is released), open idle (ambient animation
// on and off), the cold and warm openings, repeated open/close lifecycle and
// the memory/thread/handle footprint (closed after launch, open, closed after
// the cycles, closed after the release) of a real EndfieldHUD.exe, measured
// from outside with a temporary data root. Writes a JSON report and a
// readable Markdown report.
//
// On the laptop desktop, one command (from the build's Release folder):
//
//   package_perf_gate --desktop
//
// measures the EndfieldHUD.exe next to the tool with the default settings
// and writes performance-reports\<local time>\performance-gate.json/.md next
// to it. It opens and closes the HUD about 100 times over ~12 minutes (an
// EndfieldHUD.exe that cannot shorten its graphics grace for the release
// launch adds the ten-minute grace), so it asks first and never starts on
// its own.
//
// Full form:
//   package_perf_gate --exe <EndfieldHUD.exe> --report <report.json> [--markdown <report.md>]
//       [--scenarios closed,released,open,ambient-off,lifecycle] [--seconds 60] [--settle 10]
//       [--cycles 100] [--open-settle 1.5] [--close-settle 1.5] [--interval 1] [--release-grace 10]
//       [--limit <scenario>.<metric>=<value>]... [--no-startup-trace] [--work-folder <dir>] [--keep-data] [--yes]
//   package_perf_gate --render <report.json> [--markdown <report.md>]
//
// Scenarios: closed = closed within the graphics grace (after launch);
// released = closed after the graphics release (a separate launch, opened
// once and closed; --release-grace seconds through ENDFIELDHUD_CLOSED_GRAPHICS_
// GRACE when the executable honours it, otherwise the production ten minutes).
//
// Limits: closed|released|open|ambient-off . cpu|wakeups|gpu|private-growth (idle) and
// private|working-set|threads|handles (footprint at the end of that idle),
// after-cycles . private|working-set|threads|handles (closed, after the cycles),
// lifecycle . handles|threads|gdi|user|private|working-set|gpu-dedicated|warmup|window,
// cold-open|warm-open . first-frame|p95-interval|late-frames
// ("none" removes a limit). Exit code: 0 every gate passed, 1 a gate failed,
// 2 the run could not be performed or was refused (for example an
// EndfieldHUD.exe without --data-root support, which is never started, or a
// run on a visible desktop without confirmation), 3 cancelled with Ctrl+C.
//
// Whenever the measured EndfieldHUD.exe would appear on a visible desktop the
// tool describes the run and waits for Enter (or --yes). In a session without
// a visible desktop (SSH, CI) nothing is shown, so no confirmation is asked.
#include "core/data/json.hpp"
#include "packaging/perf_probe.hpp"
#include "packaging/perf_report.hpp"
#include "packaging/perf_runner.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace p = endfield::packaging;
namespace fs = std::filesystem;
using ehud::data::Json;
namespace {
HANDLE cancelEvent{};
BOOL WINAPI consoleControl(DWORD type) {
    // Ctrl+C / Ctrl+Break: stop at the next wait, stop the app gracefully
    // and remove the temporary data root. Closing the console ends the
    // process; the kill-on-close job then ends the measured app too.
    if ((type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) && cancelEvent) {
        SetEvent(cancelEvent);
        return TRUE;
    }
    return FALSE;
}
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
    // Footprint points: closed, released, open, ambient-off and after-cycles.
    const auto footprint = [&](p::FootprintLimits& l) {
        if (metric == "private") l.privateBytes = value;
        else if (metric == "working-set") l.workingSet = value;
        else if (metric == "threads") l.threads = value;
        else if (metric == "handles") l.handles = value;
        else return false;
        return true;
    };
    if (scenario == "after-cycles") {
        if (!footprint(o.cyclesFootprint)) throw std::invalid_argument("Unknown footprint metric: " + metric);
    } else if (scenario == "closed" || scenario == "released" || scenario == "open" || scenario == "ambient-off") {
        auto& l = scenario == "closed" ? o.graceLimits : scenario == "released" ? o.releasedLimits : scenario == "open" ? o.openLimits : o.ambientOffLimits;
        if (footprint(scenario == "closed" ? o.closedFootprint : scenario == "released" ? o.releasedFootprint
                      : scenario == "open" ? o.openFootprint : o.ambientOffFootprint)) return;
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
    } else if (scenario == "cold-open" || scenario == "warm-open") {
        auto& l = scenario == "cold-open" ? o.coldOpenLimits : o.warmOpenLimits;
        if (metric == "first-frame") l.firstFrameMS = value;
        else if (metric == "p95-interval") l.p95IntervalMS = value;
        else if (metric == "late-frames") l.lateFrames = value;
        else throw std::invalid_argument("Unknown opening metric: " + metric);
    } else {
        throw std::invalid_argument("Unknown scenario: " + scenario);
    }
}
fs::path moduleFolder() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) throw std::runtime_error("The tool's own folder is unknown");
    buffer.resize(length);
    return p::longPath(fs::path(buffer).parent_path());
}
std::wstring localStamp() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t text[32]{};
    swprintf_s(text, L"%04u%02u%02u-%02u%02u%02u", static_cast<unsigned>(t.wYear), static_cast<unsigned>(t.wMonth), static_cast<unsigned>(t.wDay),
               static_cast<unsigned>(t.wHour), static_cast<unsigned>(t.wMinute), static_cast<unsigned>(t.wSecond));
    return text;
}
std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("The report cannot be read: " + narrow(file.native()));
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.size() > 64 * 1024 * 1024) throw std::runtime_error("The report is too large");
    return text;
}
void writeText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) throw std::runtime_error("The file could not be written: " + narrow(file.native()));
}
// The run is described and the user confirms it on the console; refused
// when there is no console to answer on.
bool confirmed(const p::GateOptions& o, const fs::path& report) {
    const auto minutes = std::max(1.0, std::round(p::estimatedGateSeconds(o) / 60.0));
    const auto openings = (o.open || o.lifecycle ? 1 : 0) + (o.lifecycle ? static_cast<double>(o.cycles) : 0.0) + (o.ambientOff ? 1 : 0) + (o.released ? 1 : 0);
    std::cout << "\nEndfieldHUD performance gate (WINDOWS-MIGRATION.md section 9)\n\n"
              << "  Program:  " << narrow(o.executable.native()) << "\n"
              << "  Data:     a new temporary data root (your EndfieldHUD data, startup entry and running copy are not used)\n"
              << "  Duration: about " << minutes << " minute(s)\n"
              << "  Report:   " << narrow(report.native()) << "\n";
    if (o.released) {
        if (p::supportsGraceOverride(o.executable))
            std::cout << "  Release:  the closed HUD after its graphics release is measured with the grace shortened to " << o.releaseGrace
                      << " s for that launch\n            (" << p::graceOverrideVariable << ", honoured only with a temporary data root)\n";
        else
            std::cout << "  Release:  this EndfieldHUD.exe cannot shorten its ten-minute graphics grace, so the gate waits the full ten\n"
                      << "            minutes once (leave out \"released\" with --scenarios to skip it)\n";
    }
    std::cout << "\n"
              << "The HUD opens and closes on this desktop about " << static_cast<long long>(openings) << " times. Do not use the mouse or\n"
              << "keyboard until the gate reports its result. To stop early, switch to this window and press\n"
              << "Ctrl+C, or close this window (the measured EndfieldHUD ends with it).\n\n";
    if (const auto others = p::countProcesses(L"EndfieldHUD.exe", 0))
        std::cout << others << " EndfieldHUD process(es) are already running. They are not touched, but for clean numbers quit\n"
                  << "them first (tray icon > Quit); the report notes them either way.\n\n";
    DWORD mode{};
    const auto input = GetStdHandle(STD_INPUT_HANDLE);
    if (!input || input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode)) {
        std::cerr << "package_perf_gate: there is no console to confirm on; add --yes to start it from a script.\n";
        return false;
    }
    std::cout << "Press Enter to start, or Ctrl+C to cancel: " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line) || WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) return false;
    return true;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); // report the real display DPI
    cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(consoleControl, TRUE);
    p::GateOptions o;
    o.cancelEvent = cancelEvent;
    fs::path report, markdown, render;
    bool desktop{}, yes{};
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
            else if (name == L"--markdown") markdown = fs::path(wideValue());
            else if (name == L"--render") render = fs::path(wideValue());
            else if (name == L"--desktop") desktop = true;
            else if (name == L"--yes") yes = true;
            else if (name == L"--target") {
                const auto t = value();
                if (t == "endfieldhud") o.target = p::GateOptions::Target::endfieldhud;
                else if (t == "fixture") o.target = p::GateOptions::Target::fixture;
                else throw std::invalid_argument("--target is endfieldhud or fixture");
            } else if (name == L"--fixture-argument") o.fixtureArguments.push_back(wideValue());
            else if (name == L"--scenarios") {
                const auto list = "," + value() + ",";
                o.closed = list.find(",closed,") != std::string::npos;
                o.released = list.find(",released,") != std::string::npos;
                o.open = list.find(",open,") != std::string::npos;
                o.ambientOff = list.find(",ambient-off,") != std::string::npos;
                o.lifecycle = list.find(",lifecycle,") != std::string::npos;
                if (!o.closed && !o.released && !o.open && !o.ambientOff && !o.lifecycle)
                    throw std::invalid_argument("--scenarios names none of closed,released,open,ambient-off,lifecycle");
            } else if (name == L"--seconds") o.seconds = number(value());
            else if (name == L"--release-grace") {
                o.releaseGrace = number(value());
                if (o.releaseGrace < 1 || o.releaseGrace > 3600) throw std::invalid_argument("--release-grace is 1...3600 seconds");
            }
            else if (name == L"--settle") o.settle = number(value());
            else if (name == L"--open-settle") o.openSettle = number(value());
            else if (name == L"--close-settle") o.closeSettle = number(value());
            else if (name == L"--interval") o.interval = number(value());
            else if (name == L"--cycles") o.cycles = static_cast<std::size_t>(number(value()));
            else if (name == L"--limit") setLimit(o, value());
            else if (name == L"--no-startup-trace") o.startupTrace = false;
            else if (name == L"--keep-data") o.keepData = true;
            else if (name == L"--work-folder") o.workFolder = fs::path(wideValue()).make_preferred();
            else throw std::invalid_argument("Unknown argument: " + narrow(name));
        }
        if (!render.empty()) {
            // Offline: a saved JSON report rendered again.
            if (markdown.empty()) markdown = fs::path(render).replace_extension(L".md");
            writeText(markdown, p::renderGateMarkdown(Json::parse(readText(render), 64 * 1024 * 1024)));
            std::cout << "Readable report: " << narrow(markdown.native()) << '\n';
            return 0;
        }
        if (desktop) {
            if (o.target != p::GateOptions::Target::endfieldhud) throw std::invalid_argument("--desktop measures EndfieldHUD.exe only");
            if (!p::interactiveDesktop())
                throw std::invalid_argument("--desktop runs on the laptop's visible desktop only: start it from a terminal there (this session has no visible desktop)");
            const auto folder = moduleFolder();
            if (o.executable.empty()) o.executable = folder / L"EndfieldHUD.exe";
            if (report.empty()) report = folder / L"performance-reports" / localStamp() / L"performance-gate.json";
        }
        if (o.executable.empty() || report.empty()) throw std::invalid_argument("--exe and --report are required (or --desktop)");
        if (o.lifecycle && o.cycles < o.lifecycleLimits.warmup + 2 * o.lifecycleLimits.window)
            throw std::invalid_argument("--cycles must cover the warm-up plus two comparison windows");
        if (markdown.empty()) markdown = fs::path(report).replace_extension(L".md");
    } catch (const std::exception& e) {
        std::cerr << "package_perf_gate: " << e.what() << '\n';
        return 2;
    }
    try {
        // The executable is checked before anything is shown or asked.
        if (o.target == p::GateOptions::Target::endfieldhud && !p::supportsGateControl(o.executable))
            throw std::runtime_error("This EndfieldHUD.exe has no --data-root/--toggle support, so the gate does not run it (it would use the real data root)");
        if (o.target == p::GateOptions::Target::endfieldhud && p::interactiveDesktop() && !yes && !confirmed(o, report)) {
            std::cerr << "package_perf_gate: not started.\n";
            return 2;
        }
        if (!report.parent_path().empty()) fs::create_directories(report.parent_path());
        if (!markdown.parent_path().empty()) fs::create_directories(markdown.parent_path());
    } catch (const std::exception& e) {
        std::cerr << "package_perf_gate: " << e.what() << '\n';
        return 2;
    }
    if (o.target == p::GateOptions::Target::endfieldhud) std::cout << "Measuring; the result follows in about " << std::lround(p::estimatedGateSeconds(o) / 60.0) << " minute(s).\n" << std::flush;
    p::GateRun run;
    try {
        run = p::runPerformanceGate(o);
    } catch (const std::exception& e) {
        std::cerr << "package_perf_gate: " << e.what() << '\n';
        return 2;
    }
    try {
        writeText(report, run.report);
        const auto parsed = Json::parse(run.report, 64 * 1024 * 1024);
        writeText(markdown, p::renderGateMarkdown(parsed));
        std::cout << "EndfieldHUD performance gate: " << (run.cancelled ? "cancelled" : run.passed ? "passed" : "FAILED") << '\n'
                  << p::summarizeGateReport(parsed);
        for (const auto& f : run.failures) std::cout << "  - " << f << '\n';
        std::cout << "Readable report: " << narrow(markdown.native()) << "\nJSON report:     " << narrow(report.native()) << '\n';
    } catch (const std::exception& e) {
        std::cerr << "package_perf_gate: the report could not be written (" << e.what() << ")\n";
        return 2;
    }
    return run.cancelled ? 3 : run.passed ? 0 : 1;
}
