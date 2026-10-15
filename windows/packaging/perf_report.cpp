#include "packaging/perf_report.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace endfield::packaging {
namespace {
using ehud::data::Json;

double finite(const Json& value, std::string_view what) {
    if (!value.isNumber()) throw std::invalid_argument("Startup trace field is not a number: " + std::string(what));
    const auto number = value.number();
    if (!std::isfinite(number)) throw std::invalid_argument("Startup trace field is not finite: " + std::string(what));
    return number;
}
std::vector<double> numbers(const Json& value, std::string_view what) {
    if (!value.isArray()) throw std::invalid_argument("Startup trace field is not an array: " + std::string(what));
    std::vector<double> out;
    out.reserve(value.array().size());
    for (const auto& n : value.array()) {
        const auto v = finite(n, what);
        if (v < 0) throw std::invalid_argument("Startup trace field is negative: " + std::string(what));
        out.push_back(v);
    }
    return out;
}

// --- Report rendering -------------------------------------------------------
std::string fixed(double value, int decimals) {
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}
std::optional<double> optionalNumber(const Json& value) {
    if (!value.isNumber()) return std::nullopt;
    const auto n = value.number();
    return std::isfinite(n) ? std::optional<double>(n) : std::nullopt;
}
std::string text(const Json& value, std::string fallback = "unknown") {
    return value.isString() && !value.string().empty() ? value.string() : fallback;
}
// Table cells: one line, no column separators.
std::string cell(std::string value) {
    std::string out;
    for (const char c : value) {
        if (c == '|') out += "\\|";
        else if (c == '\r' || c == '\n') out += ' ';
        else out += c;
    }
    return out;
}
std::string mebibytes(double bytes) { return fixed(bytes / (1024.0 * 1024.0), 2) + " MiB"; }
std::string signedMebibytes(double bytes) { return (bytes > 0 ? "+" : "") + mebibytes(bytes); }
std::string signedCount(double value) {
    const auto rounded = fixed(value, std::abs(value - std::round(value)) < 1e-9 ? 0 : 1);
    return (value > 0 ? "+" : "") + rounded;
}
std::string withUnit(double value, std::string_view unit) {
    if (unit == "bytes") return mebibytes(value);
    if (unit == "% of one core") return fixed(value, 2) + " % of one core";
    if (unit == "% busy") return fixed(value, 2) + " %";
    if (unit == "context switches/s") return fixed(value, 1) + " /s";
    if (unit == "ms") return fixed(value, 1) + " ms";
    if (unit == "count" || unit == "frames") return fixed(value, std::abs(value - std::round(value)) < 1e-9 ? 0 : 1);
    return fixed(value, 2) + (unit.empty() ? "" : " " + std::string(unit));
}
// A footprint point; `cycles` 0 when the count is not known.
std::string pointTitle(std::string_view point, std::size_t cycles) {
    if (point == "closed") return "closed after launch";
    if (point == "open") return "open";
    if (point == "open-ambient-off") return "open, ambient off";
    if (point == "after-cycles") return cycles ? "closed after " + std::to_string(cycles) + " cycles" : std::string("closed after the cycles");
    return std::string(point);
}
std::string capitalized(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') text[0] = static_cast<char>(text[0] - 'a' + 'A');
    return text;
}
std::string footprintMetricTitle(std::string_view metric) {
    if (metric == "private-bytes") return "Private bytes";
    if (metric == "working-set") return "Working set";
    if (metric == "threads") return "Threads";
    if (metric == "handles") return "Handles";
    return std::string(metric);
}
std::string gateTitle(std::string_view name) {
    // Footprint gates: "<point>.<metric>".
    if (const auto dot = name.find('.'); dot != std::string_view::npos)
        return footprintMetricTitle(name.substr(dot + 1)) + ", " + pointTitle(name.substr(0, dot), 0);
    if (name == "cpu") return "CPU";
    if (name == "wakeups") return "Wake-ups";
    if (name == "gpu") return "GPU busy";
    if (name == "private-growth") return "Private bytes growth";
    if (name == "first-frame") return "Request to first frame";
    if (name == "p95-frame-interval") return "p95 frame interval";
    if (name == "late-frames") return "Frames later than 25 ms";
    if (name.ends_with("-growth")) {
        const auto metric = name.substr(0, name.size() - 7);
        if (metric == "handles") return "Handles growth";
        if (metric == "threads") return "Threads growth";
        if (metric == "gdiObjects") return "GDI objects growth";
        if (metric == "userObjects") return "USER objects growth";
        if (metric == "privateBytes") return "Private bytes growth";
        if (metric == "workingSet") return "Working set growth";
        if (metric == "gpuDedicated") return "Dedicated GPU memory growth";
    }
    return std::string(name);
}
std::string metricTitle(std::string_view metric) {
    if (metric == "handles") return "Handles";
    if (metric == "threads") return "Threads";
    if (metric == "gdiObjects") return "GDI objects";
    if (metric == "userObjects") return "USER objects";
    if (metric == "privateBytes") return "Private bytes";
    if (metric == "workingSet") return "Working set";
    if (metric == "gpuDedicated") return "Dedicated GPU memory";
    return std::string(metric);
}
bool bytesMetric(std::string_view metric) { return metric == "privateBytes" || metric == "workingSet" || metric == "gpuDedicated"; }
std::string kindOf(const Json& scenario) {
    if (scenario["kind"].isString()) return scenario["kind"].string();
    const auto name = text(scenario["name"], "");
    if (name.starts_with("lifecycle")) return "lifecycle";
    if (name == "openings") return "openings";
    if (name == "footprint") return "footprint";
    return "idle";
}
std::string scenarioTitle(const Json& scenario) {
    const auto name = text(scenario["name"], "");
    if (name == "footprint") return "Memory, threads and handles";
    if (name == "closed-idle") return "Closed idle";
    if (name == "open-idle-ambient-on") return "Open idle, ambient animation on";
    if (name == "open-idle-ambient-off") return "Open idle, ambient animation off";
    if (name == "open-idle") return "Open idle";
    if (name == "openings") return "Opening the HUD (cold and warm)";
    if (name.starts_with("lifecycle-")) return "Open/close lifecycle (" + name.substr(10) + " cycles)";
    return name.empty() ? "Scenario" : name;
}
std::string verdict(const Json& value) { return value.isBool() ? (value.boolean() ? "PASS" : "FAIL") : "not evaluated"; }
const Json* gateNamed(const Json& gates, std::string_view name) {
    if (!gates.isArray()) return nullptr;
    for (const auto& g : gates.array()) if (g["name"].isString() && g["name"].string() == name) return &g;
    return nullptr;
}
// "value | limit | result" for a measured value and its (optional) gate.
std::string gateCells(const std::optional<double>& value, std::string_view unit, const Json* gate) {
    std::string out = (value ? withUnit(*value, unit) : std::string("not available")) + " | ";
    if (gate && optionalNumber((*gate)["limit"])) out += withUnit(*optionalNumber((*gate)["limit"]), unit);
    else out += "none";
    out += " | ";
    if (!value) out += gate ? "not measured" : "-";
    else out += gate ? verdict((*gate)["passed"]) : "reported";
    return out;
}

std::string idleHighlights(const Json& s) {
    const auto& summary = s["summary"];
    std::string out;
    if (const auto cpu = optionalNumber(summary["cpuPercentOfOneCore"])) out += "CPU " + fixed(*cpu, 2) + " % of one core";
    if (const auto w = optionalNumber(summary["wakeupsPerSecond"])) out += (out.empty() ? "" : ", ") + fixed(*w, 1) + " wake-ups/s";
    if (const auto gpu = optionalNumber(summary["gpuBusyPercent"])) out += (out.empty() ? "" : ", ") + std::string("GPU ") + fixed(*gpu, 2) + " %";
    else out += (out.empty() ? "" : ", ") + std::string("GPU not available");
    return out;
}
std::string lifecycleHighlights(const Json& s) {
    std::string out;
    if (!s["growth"].isArray()) return out;
    for (const auto& g : s["growth"].array()) {
        const auto metric = text(g["metric"], "");
        const auto growth = optionalNumber(g["growth"]);
        if (!growth || (metric != "handles" && metric != "threads" && metric != "privateBytes" && metric != "workingSet")) continue;
        out += (out.empty() ? "" : ", ") + metricTitle(metric) + " " + (bytesMetric(metric) ? signedMebibytes(*growth) : signedCount(*growth));
    }
    return out;
}
std::string openingHighlights(const Json& s) {
    std::string out;
    if (!s["openings"].isArray()) return "no opening was recorded";
    for (const auto& o : s["openings"].array()) {
        const bool cold = o["cold"].isBool() && o["cold"].boolean();
        const auto first = optionalNumber(o["firstFrameMilliseconds"]);
        out += (out.empty() ? "" : "; ") + std::string(cold ? "cold" : "warm") + ": first frame " +
               (first && *first >= 0 ? fixed(*first, 0) + " ms" : std::string("never"));
        if (const auto p95 = optionalNumber(o["p95IntervalMilliseconds"])) out += ", p95 " + fixed(*p95, 1) + " ms";
        if (const auto late = optionalNumber(o["lateFrames"])) out += ", " + fixed(*late, 0) + " late";
    }
    return out.empty() ? "no opening was recorded" : out;
}
std::size_t cyclesOf(const Json& s) {
    const auto n = optionalNumber(s["cycles"]);
    return n && *n >= 0 ? static_cast<std::size_t>(*n) : 0;
}
std::string footprintHighlights(const Json& s) {
    std::string out;
    if (!s["points"].isArray()) return "no point was measured";
    for (const auto& p : s["points"].array()) {
        const auto bytes = optionalNumber(p["privateBytes"]);
        const auto threads = optionalNumber(p["threads"]);
        out += (out.empty() ? "" : "; ") + pointTitle(text(p["point"], "?"), cyclesOf(s)) + " " + (bytes ? fixed(*bytes / (1024.0 * 1024.0), 1) : std::string("?")) +
               " MiB private, " + (threads ? fixed(*threads, 0) : std::string("?")) + " threads";
    }
    return out.empty() ? "no point was measured" : out;
}
std::string highlights(const Json& s) {
    const auto kind = kindOf(s);
    if (kind == "lifecycle") return lifecycleHighlights(s);
    if (kind == "openings") return openingHighlights(s);
    if (kind == "footprint") return footprintHighlights(s);
    return idleHighlights(s);
}

void renderIdle(std::string& md, const Json& s) {
    const auto& summary = s["summary"];
    const auto& gates = s["gates"];
    if (const auto seconds = optionalNumber(summary["seconds"])) md += "Sampled for " + fixed(*seconds, 1) + " s.\n\n";
    md += "| Measure | Value | Limit | Result |\n| --- | ---: | ---: | --- |\n";
    md += "| CPU | " + gateCells(optionalNumber(summary["cpuPercentOfOneCore"]), "% of one core", gateNamed(gates, "cpu")) + " |\n";
    md += "| Wake-ups (context switches) | " + gateCells(optionalNumber(summary["wakeupsPerSecond"]), "context switches/s", gateNamed(gates, "wakeups")) + " |\n";
    // A GPU limit without a GPU reading is shown as not measured.
    const auto* gpuGate = gateNamed(gates, "gpu");
    const auto gpuLimit = optionalNumber(s["limits"]["gpuBusyPercent"]);
    std::string gpuRow = "| GPU busy | ";
    if (const auto gpu = optionalNumber(summary["gpuBusyPercent"])) gpuRow += gateCells(gpu, "% busy", gpuGate);
    else gpuRow += "not available | " + (gpuLimit ? withUnit(*gpuLimit, "% busy") : std::string("none")) + " | not measured";
    md += gpuRow + " |\n";
    md += "| Private bytes growth | " + gateCells(optionalNumber(summary["privateGrowthBytes"]), "bytes", gateNamed(gates, "private-growth")) + " |\n";
    if (const auto v = optionalNumber(summary["privateBytes"])) md += "| Private bytes | " + mebibytes(*v) + " | | |\n";
    if (const auto v = optionalNumber(summary["workingSetBytes"])) md += "| Working set | " + mebibytes(*v) + " | | |\n";
    const auto count = [&](const char* key) { const auto v = optionalNumber(summary[key]); return v ? fixed(*v, 0) : std::string("?"); };
    md += "| Threads / handles / GDI / USER objects | " + count("threads") + " / " + count("handles") + " / " + count("gdiObjects") + " / " + count("userObjects") + " | | |\n\n";
    if (s["busiestThreads"].isArray() && !s["busiestThreads"].array().empty()) {
        md += "Busiest threads during the window:\n\n| Thread | Name | Start | Wake-ups/s |\n| ---: | --- | --- | ---: |\n";
        for (const auto& t : s["busiestThreads"].array()) {
            const auto id = optionalNumber(t["id"]);
            const auto w = optionalNumber(t["wakeupsPerSecond"]);
            md += "| " + (id ? fixed(*id, 0) : std::string("?")) + " | " + cell(text(t["name"], "-")) + " | " + cell(text(t["start"], "-")) + " | " +
                  (w ? fixed(*w, 1) : std::string("?")) + " |\n";
        }
        md += "\n";
    }
}
void renderLifecycle(std::string& md, const Json& s) {
    const auto warmup = optionalNumber(s["warmupCycles"]);
    const auto window = optionalNumber(s["windowCycles"]);
    if (warmup && window)
        md += "Growth is the median of the last " + fixed(*window, 0) + " cycles minus the median of the first " + fixed(*window, 0) +
              " cycles after " + fixed(*warmup, 0) + " warm-up cycles, sampled after each close settled.\n\n";
    md += "| Metric | First window | Last window | Growth | Limit | Result |\n| --- | ---: | ---: | ---: | ---: | --- |\n";
    if (s["growth"].isArray()) {
        for (const auto& g : s["growth"].array()) {
            const auto metric = text(g["metric"], "?");
            const bool bytes = bytesMetric(metric);
            const auto show = [&](const Json& v) {
                const auto n = optionalNumber(v);
                return n ? (bytes ? mebibytes(*n) : fixed(*n, 1)) : std::string("?");
            };
            const auto growth = optionalNumber(g["growth"]);
            const auto* gate = gateNamed(s["gates"], metric + "-growth");
            const auto limit = gate ? optionalNumber((*gate)["limit"]) : std::nullopt;
            md += "| " + metricTitle(metric) + " | " + show(g["baseline"]) + " | " + show(g["latest"]) + " | " +
                  (growth ? (bytes ? signedMebibytes(*growth) : signedCount(*growth)) : std::string("?")) + " | " +
                  (limit ? (bytes ? mebibytes(*limit) : fixed(*limit, 0)) : std::string("none")) + " | " +
                  (gate ? verdict((*gate)["passed"]) : std::string("reported")) + " |\n";
        }
    }
    md += "\n";
}
void renderFootprint(std::string& md, const Json& s) {
    std::vector<const Json*> points;
    if (s["points"].isArray()) for (const auto& p : s["points"].array()) points.push_back(&p);
    if (points.empty()) {
        md += "No point was measured.\n\n";
        return;
    }
    const auto cycles = cyclesOf(s);
    md += "Process-wide values of the measured program at the end of each state. Private bytes are its commit charge (Task Manager's "
          "\"Commit size\"), the working set its resident memory; the peaks are the process's maxima up to that moment.\n\n| Measure |";
    std::string rule = "| --- |";
    for (const auto* p : points) {
        md += " " + capitalized(pointTitle(text((*p)["point"], "?"), cycles)) + " |";
        rule += " ---: |";
    }
    md += "\n" + rule + "\n";
    const auto row = [&](const char* title, auto value) {
        md += std::string("| ") + title + " |";
        for (const auto* p : points) md += " " + value(*p) + " |";
        md += "\n";
    };
    const auto bytes = [](const Json& v) { const auto n = optionalNumber(v); return n ? mebibytes(*n) : std::string("not available"); };
    const auto count = [](const Json& v) { const auto n = optionalNumber(v); return n ? fixed(*n, 0) : std::string("?"); };
    row("Private bytes", [&](const Json& p) { return bytes(p["privateBytes"]); });
    row("Working set", [&](const Json& p) { return bytes(p["workingSetBytes"]); });
    row("Peak private bytes", [&](const Json& p) { return bytes(p["peakPrivateBytes"]); });
    row("Peak working set", [&](const Json& p) { return bytes(p["peakWorkingSetBytes"]); });
    row("Threads", [&](const Json& p) { return count(p["threads"]); });
    row("Handles", [&](const Json& p) { return count(p["handles"]); });
    row("GDI / USER objects", [&](const Json& p) { return count(p["gdiObjects"]) + " / " + count(p["userObjects"]); });
    row("Dedicated / shared GPU memory", [&](const Json& p) {
        const auto d = optionalNumber(p["gpuDedicatedBytes"]), sh = optionalNumber(p["gpuSharedBytes"]);
        return d && sh ? fixed(*d / (1024.0 * 1024.0), 2) + " / " + fixed(*sh / (1024.0 * 1024.0), 2) + " MiB" : std::string("not available");
    });
    row("Committed private / mapped / image", [&](const Json& p) {
        const auto& m = p["memory"];
        const auto a = optionalNumber(m["privateCommittedBytes"]), b = optionalNumber(m["mappedCommittedBytes"]), c = optionalNumber(m["imageCommittedBytes"]);
        return a && b && c ? fixed(*a / (1024.0 * 1024.0), 2) + " / " + fixed(*b / (1024.0 * 1024.0), 2) + " / " + fixed(*c / (1024.0 * 1024.0), 2) + " MiB"
                           : std::string("not available");
    });
    row("Largest private allocations", [&](const Json& p) {
        const auto& list = p["memory"]["largestPrivateAllocations"];
        if (!list.isArray()) return std::string("not available");
        std::string out;
        std::size_t shown{};
        for (const auto& v : list.array()) {
            const auto n = optionalNumber(v);
            if (!n || shown++ == 5) break;
            out += (out.empty() ? "" : ", ") + fixed(*n / (1024.0 * 1024.0), 2);
        }
        return out.empty() ? std::string("none") : out + " MiB";
    });
    md += "\n";

    // What the closed HUD keeps after use: the settled point after the
    // cycles minus the point after launch.
    const Json* closed{};
    const Json* after{};
    for (const auto* p : points) {
        if (text((*p)["point"], "") == "closed") closed = p;
        if (text((*p)["point"], "") == "after-cycles") after = p;
    }
    if (closed && after) {
        const auto pb = optionalNumber((*closed)["privateBytes"]), pa = optionalNumber((*after)["privateBytes"]);
        const auto tb = optionalNumber((*closed)["threads"]), ta = optionalNumber((*after)["threads"]);
        const auto hb = optionalNumber((*closed)["handles"]), ha = optionalNumber((*after)["handles"]);
        if (pb && pa && tb && ta && hb && ha)
            md += "Kept after use (" + pointTitle("after-cycles", cycles) + " minus closed after launch): " + signedMebibytes(*pa - *pb) + " private bytes, " +
                  signedCount(*ta - *tb) + " threads, " + signedCount(*ha - *hb) + " handles.\n\n";
    }

    md += "| Gate | Value | Limit | Result |\n| --- | ---: | ---: | --- |\n";
    bool any{};
    for (const auto* p : points) {
        if (!(*p)["gates"].isArray()) continue;
        for (const auto& g : (*p)["gates"].array()) {
            const auto name = text(g["name"], "?");
            const auto dot = name.find('.');
            const auto title = dot == std::string::npos ? gateTitle(name)
                                                        : footprintMetricTitle(std::string_view(name).substr(dot + 1)) + ", " + pointTitle(name.substr(0, dot), cycles);
            const auto unit = text(g["unit"], "");
            const auto value = optionalNumber(g["value"]);
            const auto limit = optionalNumber(g["limit"]);
            md += "| " + title + " | " + (value ? withUnit(*value, unit) : std::string("?")) + " | " + (limit ? withUnit(*limit, unit) : std::string("none")) + " | " +
                  verdict(g["passed"]) + " |\n";
            any = true;
        }
    }
    if (!any) md += "| (no footprint limits set) | | | |\n";
    md += "\n";

    // Threads by start address, one column per point.
    struct Row { std::string start, role, name; std::vector<double> counts; double most{}; };
    std::vector<Row> rows;
    for (std::size_t n = 0; n < points.size(); ++n) {
        const auto& groups = (*points[n])["threadGroups"];
        if (!groups.isArray()) continue;
        for (const auto& g : groups.array()) {
            const auto start = text(g["start"], "unknown"), name = text(g["name"], "");
            auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.start == start && r.name == name; });
            if (it == rows.end()) {
                rows.push_back({start, text(g["role"], ""), name, std::vector<double>(points.size(), 0.0), 0});
                it = rows.end() - 1;
            }
            const auto c = optionalNumber(g["count"]).value_or(0);
            it->counts[n] += c;
            it->most = std::max(it->most, it->counts[n]);
        }
    }
    if (!rows.empty()) {
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.most != b.most) return a.most > b.most;
            return a.start < b.start;
        });
        md += "Threads by start address (module+offset of each thread's start; threads with the same start and description are counted together; "
              "the threads the program starts itself share its C runtime's start addresses):\n\n| Start | Component | Description |";
        std::string threadRule = "| --- | --- | --- |";
        for (const auto* p : points) {
            md += " " + capitalized(pointTitle(text((*p)["point"], "?"), cycles)) + " |";
            threadRule += " ---: |";
        }
        md += "\n" + threadRule + "\n";
        constexpr std::size_t shownRows = 40;
        std::vector<double> rest(points.size(), 0.0);
        std::size_t hidden{};
        for (std::size_t r = 0; r < rows.size(); ++r) {
            if (r >= shownRows) {
                ++hidden;
                for (std::size_t n = 0; n < points.size(); ++n) rest[n] += rows[r].counts[n];
                continue;
            }
            md += "| " + cell(rows[r].start) + " | " + cell(rows[r].role.empty() ? "-" : rows[r].role) + " | " + cell(rows[r].name.empty() ? "-" : rows[r].name) + " |";
            for (const auto c : rows[r].counts) md += " " + fixed(c, 0) + " |";
            md += "\n";
        }
        if (hidden) {
            md += "| " + std::to_string(hidden) + " other start addresses | | |";
            for (const auto c : rest) md += " " + fixed(c, 0) + " |";
            md += "\n";
        }
        md += "\n";
    }
    md += "For context only (different platform and measure): the macOS 1.2 measurement on an Apple M2 recorded a physical footprint of "
          "96.5-98.2 MiB with the HUD closed and 186-190 MiB during continuous tilt (docs/performance-1.2.md).\n\n";
}
void renderOpenings(std::string& md, const Json& s, const Json& trace) {
    const auto launch = optionalNumber(s["launchToFirstFrameMilliseconds"]);
    const auto process = optionalNumber(s["processToFirstFrameMilliseconds"]);
    const auto prepared = optionalNumber(s["preparationMilliseconds"]);
    md += "Source: EndfieldHUD's own startup trace (numbers only). The cold opening is the first opening after launch; the warm one is the next.\n\n";
    // When the HUD started closed and was opened later (the gate measures
    // closed idle first), launch to the first frame is mostly that wait.
    std::optional<double> coldRequest;
    if (s["openings"].isArray())
        for (const auto& o : s["openings"].array())
            if (o["cold"].isBool() && o["cold"].boolean()) coldRequest = optionalNumber(o["requestedAtMilliseconds"]);
    if (launch && *launch >= 0 && coldRequest && *coldRequest > 0 && *launch >= *coldRequest) {
        md += "The HUD started closed, and the cold opening was requested " + fixed(*coldRequest / 1000.0, 1) +
              " s after launch (after the closed idle window), so launch to the first presented frame (" + fixed(*launch, 0) +
              " ms) is mostly that wait";
        if (prepared) md += ". Startup until the app was ready, before the window: **" + fixed(*prepared, 0) + " ms**";
        md += ".\n\n";
    } else if (launch && *launch >= 0) {
        md += "Launch to the first presented frame: **" + fixed(*launch, 0) + " ms**";
        if (process && *process >= 0) md += " (process creation to first frame: " + fixed(*process, 0) + " ms)";
        if (prepared) md += "; preparation before the window: " + fixed(*prepared, 0) + " ms";
        md += ".\n\n";
    }
    std::vector<const Json*> openings;
    if (s["openings"].isArray()) for (const auto& o : s["openings"].array()) openings.push_back(&o);
    if (openings.empty()) {
        md += "No opening was recorded.\n\n";
        return;
    }
    md += "| Measure |";
    std::string rule = "| --- |";
    for (const auto* o : openings) {
        const bool cold = (*o)["cold"].isBool() && (*o)["cold"].boolean();
        md += cold ? " Cold (first opening) |" : " Warm (next opening) |";
        rule += " ---: |";
    }
    md += "\n" + rule + "\n";
    const auto row = [&](const char* title, auto value) {
        md += std::string("| ") + title + " |";
        for (const auto* o : openings) md += " " + value(*o) + " |";
        md += "\n";
    };
    const auto ms = [](const Json& v) { const auto n = optionalNumber(v); return n && *n >= 0 ? fixed(*n, 1) + " ms" : std::string("not reached"); };
    const auto count = [](const Json& v) { const auto n = optionalNumber(v); return n ? fixed(*n, 0) : std::string("?"); };
    row("Request to first frame", [&](const Json& o) { return ms(o["firstFrameMilliseconds"]); });
    row("Request to opened (entrance complete)", [&](const Json& o) { return ms(o["openedMilliseconds"]); });
    row("Frames presented", [&](const Json& o) { return count(o["frames"]); });
    row("Frame interval p50 / p95 / max", [&](const Json& o) {
        const auto a = optionalNumber(o["p50IntervalMilliseconds"]), b = optionalNumber(o["p95IntervalMilliseconds"]), c = optionalNumber(o["maxIntervalMilliseconds"]);
        return a && b && c ? fixed(*a, 1) + " / " + fixed(*b, 1) + " / " + fixed(*c, 1) + " ms" : std::string("?");
    });
    row("Frames later than 25 ms", [&](const Json& o) { return count(o["lateFrames"]); });
    row("present() CPU mean / max", [&](const Json& o) {
        const auto a = optionalNumber(o["meanFrameCPUMilliseconds"]), b = optionalNumber(o["maxFrameCPUMilliseconds"]);
        return a && b ? fixed(*a, 1) + " / " + fixed(*b, 1) + " ms" : std::string("?");
    });
    row("Frames over 16.67 ms of CPU", [&](const Json& o) { return count(o["framesOverBudget"]); });
    md += "\n| Gate | Value | Limit | Result |\n| --- | ---: | ---: | --- |\n";
    bool any{};
    for (const auto* o : openings) {
        const bool cold = (*o)["cold"].isBool() && (*o)["cold"].boolean();
        if (!(*o)["gates"].isArray()) continue;
        for (const auto& g : (*o)["gates"].array()) {
            const auto unit = text(g["unit"], "");
            const auto value = optionalNumber(g["value"]);
            const auto limit = optionalNumber(g["limit"]);
            md += std::string("| ") + (cold ? "Cold: " : "Warm: ") + gateTitle(text(g["name"], "?")) + " | " +
                  (value && *value >= 0 ? withUnit(*value, unit) : std::string("not reached")) + " | " + (limit ? withUnit(*limit, unit) : std::string("none")) +
                  " | " + verdict(g["passed"]) + " |\n";
            any = true;
        }
    }
    if (!any) md += "| (no opening limits set) | | | |\n";
    md += "\n";

    // Where the time went: the slowest startup stages and the owners whose
    // frame work was longest during the cold opening.
    if (trace["startupStages"].isArray() && !trace["startupStages"].array().empty()) {
        std::vector<std::pair<double, const Json*>> stages;
        for (const auto& st : trace["startupStages"].array())
            if (const auto wall = optionalNumber(st["wallMilliseconds"])) stages.emplace_back(*wall, &st);
        std::stable_sort(stages.begin(), stages.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (!stages.empty()) {
            md += "Slowest startup stages:\n\n| Stage | Wall time | Process CPU |\n| --- | ---: | ---: |\n";
            for (std::size_t n = 0; n < stages.size() && n < 8; ++n) {
                const auto cpu = optionalNumber((*stages[n].second)["processCPUSeconds"]);
                md += "| " + cell(text((*stages[n].second)["stage"], "?")) + " | " + fixed(stages[n].first, 1) + " ms | " +
                      (cpu ? fixed(*cpu * 1000.0, 1) + " ms" : std::string("?")) + " |\n";
            }
            md += "\n";
        }
    }
    if (trace["openingStages"].isArray() && !trace["openingStages"].array().empty()) {
        const auto& cold = trace["openingStages"].array().front();
        if (cold["slowestOwners"].isArray() && !cold["slowestOwners"].array().empty()) {
            md += "Module owners with the longest frame work during the cold opening:\n\n"
                  "| Owner | Update total / max | Upload total / max |\n| --- | ---: | ---: |\n";
            std::size_t shown{};
            for (const auto& o : cold["slowestOwners"].array()) {
                if (shown++ == 6) break;
                const auto ut = optionalNumber(o["updateTotalMS"]), um = optionalNumber(o["updateMaxMS"]);
                const auto pt = optionalNumber(o["uploadTotalMS"]), pm = optionalNumber(o["uploadMaxMS"]);
                md += "| " + cell(text(o["owner"], "?")) + " | " + (ut && um ? fixed(*ut, 1) + " / " + fixed(*um, 1) + " ms" : std::string("?")) + " | " +
                      (pt && pm ? fixed(*pt, 1) + " / " + fixed(*pm, 1) + " ms" : std::string("?")) + " |\n";
            }
            md += "\n";
        }
    }
    md += "For context only (different machine and definitions): the macOS 1.2 measurement on an Apple M2 recorded a first synchronous open of "
          "225 ms, a warm reopen of 258 ms and the first presented source frame 773 ms after launch (docs/performance-1.2.md).\n\n";
}
} // namespace

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) throw std::invalid_argument("No values");
    if (!(fraction >= 0 && fraction <= 1)) throw std::invalid_argument("Percentile fraction outside 0...1");
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())));
    return values[rank == 0 ? 0 : rank - 1];
}

std::vector<OpeningSummary> summarizeOpenings(const Json& trace) {
    if (!trace.isObject() || !trace["schema"].isNumber() || trace["schema"].integer() != 1 || !trace["openings"].isArray())
        throw std::invalid_argument("Not an EndfieldHUD startup trace (schema 1)");
    std::vector<OpeningSummary> out;
    for (const auto& o : trace["openings"].array()) {
        if (!o.isObject()) throw std::invalid_argument("Startup trace opening is not an object");
        OpeningSummary s;
        const auto index = finite(o["index"], "index");
        if (index < 0 || index != std::floor(index)) throw std::invalid_argument("Startup trace opening index is invalid");
        s.index = static_cast<std::size_t>(index);
        s.cold = o["cold"].isBool() ? o["cold"].boolean() : s.index == 0;
        s.firstFrameMS = finite(o["requestToFirstFrameMilliseconds"], "requestToFirstFrameMilliseconds");
        if (o["requestedAtMilliseconds"].isNumber()) {
            const auto at = finite(o["requestedAtMilliseconds"], "requestedAtMilliseconds");
            if (at < 0) throw std::invalid_argument("Startup trace field is negative: requestedAtMilliseconds");
            s.requestedAtMS = at;
        }
        s.openedMS = finite(o["requestToOpenedMilliseconds"], "requestToOpenedMilliseconds");
        const auto intervals = numbers(o["frameIntervalsMilliseconds"], "frameIntervalsMilliseconds");
        const auto cpu = numbers(o["frameCPUMilliseconds"], "frameCPUMilliseconds");
        if (intervals.size() != cpu.size()) throw std::invalid_argument("Startup trace frame lists differ in length");
        s.frames = intervals.size();
        // The first interval is the request -> first frame step (reported as
        // the first-frame time); pacing starts with the second frame.
        if (intervals.size() > 1) {
            const std::vector<double> pacing(intervals.begin() + 1, intervals.end());
            s.p50IntervalMS = percentile(pacing, 0.5);
            s.p95IntervalMS = percentile(pacing, 0.95);
            s.maxIntervalMS = *std::max_element(pacing.begin(), pacing.end());
            s.lateFrames = static_cast<std::size_t>(std::count_if(pacing.begin(), pacing.end(), [](double v) { return v > lateIntervalMilliseconds; }));
        }
        if (!cpu.empty()) {
            double total{};
            for (const auto v : cpu) total += v;
            s.meanFrameCPUMS = total / static_cast<double>(cpu.size());
            s.maxFrameCPUMS = *std::max_element(cpu.begin(), cpu.end());
            s.framesOverBudget = static_cast<std::size_t>(std::count_if(cpu.begin(), cpu.end(), [](double v) { return v > frameBudgetMilliseconds; }));
        }
        out.push_back(s);
    }
    return out;
}

std::vector<GateResult> gateOpening(const OpeningSummary& s, const OpeningLimits& l) {
    std::vector<GateResult> out;
    // An opening that never presented a frame fails its first-frame gate.
    if (l.firstFrameMS) out.push_back({"first-frame", "ms", s.firstFrameMS, *l.firstFrameMS, s.firstFrameMS >= 0 && s.firstFrameMS <= *l.firstFrameMS});
    if (l.p95IntervalMS) out.push_back({"p95-frame-interval", "ms", s.p95IntervalMS, *l.p95IntervalMS, s.frames > 0 && s.p95IntervalMS <= *l.p95IntervalMS});
    if (l.lateFrames) out.push_back({"late-frames", "frames", static_cast<double>(s.lateFrames), *l.lateFrames, s.frames > 0 && static_cast<double>(s.lateFrames) <= *l.lateFrames});
    return out;
}

Json openingsScenario(const Json& trace, const OpeningLimits& cold, const OpeningLimits& warm, std::size_t expectedOpenings, std::vector<std::string>& failures) {
    const auto summaries = summarizeOpenings(trace);
    const auto limitsJson = [](const OpeningLimits& l) {
        Json::Object o;
        o["firstFrameMilliseconds"] = l.firstFrameMS ? Json(*l.firstFrameMS) : Json(nullptr);
        o["p95IntervalMilliseconds"] = l.p95IntervalMS ? Json(*l.p95IntervalMS) : Json(nullptr);
        o["lateFrames"] = l.lateFrames ? Json(*l.lateFrames) : Json(nullptr);
        return o;
    };
    Json::Object scenario;
    scenario["name"] = "openings";
    scenario["kind"] = "openings";
    scenario["source"] = "EndfieldHUD startup trace (ENDFIELDHUD_STARTUP_TRACE=1)";
    for (const auto* key : {"launchToFirstFrameMilliseconds", "processToFirstFrameMilliseconds", "preparationMilliseconds"})
        scenario[key] = optionalNumber(trace[key]) ? Json(*optionalNumber(trace[key])) : Json(nullptr);
    Json::Array openings, allGates;
    bool passed = summaries.size() >= expectedOpenings;
    if (!passed)
        failures.push_back("openings: " + std::to_string(summaries.size()) + " of " + std::to_string(expectedOpenings) + " openings were recorded in the startup trace");
    for (const auto& s : summaries) {
        const auto& limits = s.cold ? cold : warm;
        const auto gates = gateOpening(s, limits);
        Json::Object o;
        o["index"] = static_cast<std::int64_t>(s.index);
        o["cold"] = s.cold;
        o["requestedAtMilliseconds"] = s.requestedAtMS >= 0 ? Json(s.requestedAtMS) : Json(nullptr);
        o["firstFrameMilliseconds"] = s.firstFrameMS;
        o["openedMilliseconds"] = s.openedMS;
        o["frames"] = static_cast<std::int64_t>(s.frames);
        o["p50IntervalMilliseconds"] = s.p50IntervalMS;
        o["p95IntervalMilliseconds"] = s.p95IntervalMS;
        o["maxIntervalMilliseconds"] = s.maxIntervalMS;
        o["lateFrames"] = static_cast<std::int64_t>(s.lateFrames);
        o["meanFrameCPUMilliseconds"] = s.meanFrameCPUMS;
        o["maxFrameCPUMilliseconds"] = s.maxFrameCPUMS;
        o["framesOverBudget"] = static_cast<std::int64_t>(s.framesOverBudget);
        o["limits"] = limitsJson(limits);
        Json::Array rows;
        for (const auto& g : gates) {
            Json::Object row;
            row["name"] = g.name;
            row["unit"] = g.unit;
            row["value"] = g.value;
            row["limit"] = g.limit;
            row["passed"] = g.passed;
            rows.push_back(row);
            allGates.push_back(Json(row));
            if (!g.passed) {
                passed = false;
                failures.push_back(describeFailure(s.cold ? "cold opening" : "warm opening", g));
            }
        }
        o["gates"] = std::move(rows);
        o["passed"] = allPassed(gates);
        openings.emplace_back(std::move(o));
    }
    scenario["openings"] = std::move(openings);
    scenario["gates"] = std::move(allGates);
    scenario["passed"] = passed;
    return scenario;
}

Json footprintScenario(std::span<const FootprintPoint> points, std::size_t cycles, std::vector<std::string>& failures) {
    const auto integer = [](std::uint64_t v) { return Json(static_cast<std::int64_t>(v)); };
    const auto optionalLimit = [](const std::optional<double>& v) { return v ? Json(*v) : Json(nullptr); };
    Json::Object scenario;
    scenario["name"] = "footprint";
    scenario["kind"] = "footprint";
    scenario["cycles"] = static_cast<std::int64_t>(cycles);
    Json::Array list, allGates;
    bool passed = true;
    for (const auto& p : points) {
        const auto& s = p.sample;
        Json::Object o;
        o["point"] = p.point;
        o["privateBytes"] = integer(s.privateBytes);
        o["workingSetBytes"] = integer(s.workingSet);
        o["peakPrivateBytes"] = integer(s.peakPrivateBytes);
        o["peakWorkingSetBytes"] = integer(s.peakWorkingSet);
        o["threads"] = static_cast<std::int64_t>(s.threads);
        o["handles"] = static_cast<std::int64_t>(s.handles);
        o["gdiObjects"] = static_cast<std::int64_t>(s.gdiObjects);
        o["userObjects"] = static_cast<std::int64_t>(s.userObjects);
        o["gpuDedicatedBytes"] = s.gpuDedicated ? integer(*s.gpuDedicated) : Json(nullptr);
        o["gpuSharedBytes"] = s.gpuShared ? integer(*s.gpuShared) : Json(nullptr);
        if (p.regions) {
            Json::Object m;
            m["privateCommittedBytes"] = integer(p.regions->privateCommitted);
            m["mappedCommittedBytes"] = integer(p.regions->mappedCommitted);
            m["imageCommittedBytes"] = integer(p.regions->imageCommitted);
            Json::Array largest;
            for (const auto v : p.regions->largestPrivate) largest.push_back(integer(v));
            m["largestPrivateAllocations"] = std::move(largest);
            o["memory"] = std::move(m);
        } else {
            o["memory"] = nullptr;
        }
        Json::Array groups;
        for (const auto& g : p.threads) {
            Json::Object row;
            row["start"] = g.start;
            row["module"] = g.module;
            row["role"] = g.role;
            row["name"] = g.name;
            row["count"] = static_cast<std::int64_t>(g.count);
            groups.emplace_back(std::move(row));
        }
        o["threadGroups"] = std::move(groups);
        Json::Object limits;
        limits["privateBytes"] = optionalLimit(p.limits.privateBytes);
        limits["workingSetBytes"] = optionalLimit(p.limits.workingSet);
        limits["threads"] = optionalLimit(p.limits.threads);
        limits["handles"] = optionalLimit(p.limits.handles);
        o["limits"] = std::move(limits);
        const auto gates = gateFootprint(p);
        Json::Array rows;
        for (const auto& g : gates) {
            Json::Object row;
            row["name"] = g.name;
            row["unit"] = g.unit;
            row["value"] = g.value;
            row["limit"] = g.limit;
            row["passed"] = g.passed;
            rows.push_back(row);
            allGates.push_back(Json(row));
            if (!g.passed) {
                passed = false;
                failures.push_back(describeFailure("footprint", g));
            }
        }
        o["gates"] = std::move(rows);
        o["passed"] = allPassed(gates);
        list.emplace_back(std::move(o));
    }
    scenario["points"] = std::move(list);
    scenario["gates"] = std::move(allGates);
    scenario["passed"] = passed;
    return scenario;
}

std::string renderGateMarkdown(const Json& report) {
    if (!report.isObject() || !report["format"].isString() || report["format"].string() != "EndfieldHUD.Windows.PerformanceGate")
        throw std::invalid_argument("Not an EndfieldHUD performance gate report");
    const bool cancelled = report["cancelled"].isBool() && report["cancelled"].boolean();
    const bool passed = report["passed"].isBool() && report["passed"].boolean();
    std::string md = "# EndfieldHUD performance gate: " + std::string(cancelled ? "CANCELLED" : passed ? "PASSED" : "FAILED") + "\n\n";
    const auto& machine = report["machine"];
    const auto& target = report["target"];
    md += "- **Measured:** " + text(report["measuredAt"]) + " (UTC)\n";
    md += "- **Program:** " + cell(text(target["executable"])) + ", version " + text(target["productVersion"]) + ", SHA-256 `" + text(target["sha256"]) + "`";
    if (target["kind"].isString() && target["kind"].string() != "endfieldhud") md += " (stand-in test program, not EndfieldHUD)";
    md += "\n";
    md += "- **Windows:** " + text(machine["os"]) + "\n";
    std::string cpu = "- **Processor:** " + text(machine["cpu"]);
    if (const auto n = optionalNumber(machine["logicalProcessors"])) cpu += ", " + fixed(*n, 0) + " logical processors";
    if (const auto m = optionalNumber(machine["memoryBytes"])) cpu += ", " + fixed(*m / (1024.0 * 1024.0 * 1024.0), 1) + " GiB RAM";
    md += cpu + "\n";
    if (machine["gpus"].isArray() && !machine["gpus"].array().empty()) {
        std::string gpus;
        for (const auto& g : machine["gpus"].array()) gpus += (gpus.empty() ? "" : "; ") + text(g);
        md += "- **GPUs:** " + gpus + "\n";
    }
    std::string display = "- **Display:** " + text(machine["display"]);
    if (const auto hz = optionalNumber(machine["refreshHz"])) display += " at " + fixed(*hz, 0) + " Hz";
    if (const auto dpi = optionalNumber(machine["dpi"])) display += ", " + fixed(*dpi, 0) + " DPI (" + fixed(*dpi / 96.0 * 100.0, 0) + " % scale)";
    md += display + "\n";
    md += "- **Session:** " + text(machine["session"]);
    if (machine["interactiveDesktop"].isBool()) md += machine["interactiveDesktop"].boolean() ? ", interactive desktop" : ", no visible desktop (hidden run)";
    md += "\n";
    if (const auto others = optionalNumber(machine["otherEndfieldHUDProcesses"]); others && *others > 0)
        md += "- **Note:** " + fixed(*others, 0) + " other EndfieldHUD process(es) were running during the measurement.\n";
    md += "- **GPU counters:** " + text(report["gpuCounters"]) + "\n\n";

    const auto& scenarios = report["scenarios"];
    md += "## Summary\n\n";
    if (scenarios.isArray() && !scenarios.array().empty()) {
        md += "| Scenario | Result | Highlights |\n| --- | --- | --- |\n";
        for (const auto& s : scenarios.array()) md += "| " + scenarioTitle(s) + " | " + verdict(s["passed"]) + " | " + cell(highlights(s)) + " |\n";
    } else {
        md += "No scenario was measured.\n";
    }
    md += "\n";
    if (report["failures"].isArray() && !report["failures"].array().empty()) {
        md += "## Failures\n\n";
        for (const auto& f : report["failures"].array()) md += "- " + cell(text(f, "?")) + "\n";
        md += "\n";
    }
    if (cancelled) md += "The run was cancelled before every scenario finished; the results above cover only what was measured.\n\n";

    if (scenarios.isArray()) {
        for (const auto& s : scenarios.array()) {
            md += "## " + scenarioTitle(s) + "\n\n";
            const auto kind = kindOf(s);
            if (kind == "lifecycle") renderLifecycle(md, s);
            else if (kind == "openings") renderOpenings(md, s, report["startupTrace"]);
            else if (kind == "footprint") renderFootprint(md, s);
            else renderIdle(md, s);
        }
    }

    const auto& settings = report["settings"];
    md += "## Settings\n\n| Setting | Value |\n| --- | --- |\n";
    const auto setting = [&](const char* title, const char* key, const char* unit) {
        if (const auto v = optionalNumber(settings[key])) md += std::string("| ") + title + " | " + fixed(*v, std::abs(*v - std::round(*v)) < 1e-9 ? 0 : 2) + unit + " |\n";
    };
    setting("Idle sampling window", "seconds", " s");
    setting("Settle time after launch or opening", "settleSeconds", " s");
    setting("Open settle (per cycle)", "openSettleSeconds", " s");
    setting("Close settle (per cycle)", "closeSettleSeconds", " s");
    setting("Sample interval", "sampleIntervalSeconds", " s");
    setting("Open/close cycles", "cycles", "");
    if (settings["dataRoot"].isString()) md += "| Data root | " + cell(settings["dataRoot"].string()) + " |\n";
    md += "\n## Notes\n\n";
    if (report["notes"].isArray())
        for (const auto& n : report["notes"].array()) md += "- " + cell(text(n, "")) + "\n";
    md += "- Frame pacing comes from the presented-frame times EndfieldHUD records itself; compositor latency, GPU duration and input latency "
          "need an ETW recording (Windows Performance Recorder or PresentMon) and are not part of this gate.\n";
    return md;
}

std::string describeFailure(std::string_view scenario, const GateResult& g) {
    const bool never = g.name == "first-frame" && g.value < 0;
    return std::string(scenario) + ": " + gateTitle(g.name) + " " + (never ? std::string("never reached") : withUnit(g.value, g.unit)) + " (limit " +
           withUnit(g.limit, g.unit) + ")";
}

std::string summarizeGateReport(const Json& report) {
    if (!report.isObject() || !report["scenarios"].isArray()) return {};
    std::string out;
    for (const auto& s : report["scenarios"].array()) {
        auto name = text(s["name"], "?");
        if (name.size() < 24) name.append(24 - name.size(), ' ');
        auto result = verdict(s["passed"]);
        if (result.size() < 5) result.append(5 - result.size(), ' ');
        out += "  " + name + " " + result + " " + highlights(s) + "\n";
    }
    return out;
}
} // namespace endfield::packaging
