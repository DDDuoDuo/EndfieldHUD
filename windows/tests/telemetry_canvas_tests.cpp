// Storage, Activity Monitor and Battery on the design library (Mac 1.3.0
// Sources/TelemetryCanvases.swift, ActivityMonitorCanvas.swift,
// BatteryCanvas.swift): the shared number formats, the Overview graph paths,
// the app-icon cache rules and the three canvases in their Mac states
// (geometry, strings, values, tones, controls, accessibility, lifecycle,
// reading demand), driven through the canvas host entries with an explicit
// clock. The numbers are the Swift's (cited per check); text widths come from
// a deterministic estimating measurer, so only layout that does not depend on
// a measured width is compared exactly.
#include "modules/activity_canvas.hpp"
#include "modules/battery_canvas.hpp"
#include "modules/storage_canvas.hpp"
#include "modules/telemetry_format.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
namespace d = endfield::modules::design;
namespace m = endfield::modules;
using endfield::core::Language;
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) <= e; }
bool near(d::Rect a, d::Rect b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.width, b.width) && near(a.height, b.height); }

// Text widths: the Mac's recorded CoreText widths when the oracle measured
// the runs (tools/telemetry_reference.swift), else a deterministic estimate.
struct Measurer final : d::TextMeasurer {
    std::map<std::string, double> recorded;
    std::map<std::string, d::LineMetrics> lineMetrics;
    static inline std::vector<std::string> misses;
    static std::string metricKey(double size, int weight, double lineHeight, bool tabular, std::string_view language) {
        return number(size) + '|' + std::to_string(weight) + '|' + number(lineHeight) + '|' + (tabular ? "t" : "f") + '|' + std::string(language);
    }
    static std::string number(double v) { std::ostringstream s; s.precision(10); s << (std::round(v * 1e6) / 1e6); return s.str(); }
    static std::string key(std::span<const d::TextRun> runs, d::TextRegion region) {
        std::string k(d::regionTag(region));
        for (const auto& r : runs) if (!r.text.empty())
            k += '|' + r.text + '|' + number(r.style.size) + '|' + std::to_string(int(d::resolved(r.style.weight))) + '|' + number(r.style.tracking) + '|' + (r.style.tabular ? "t" : "f");
        return k;
    }
    double width(std::span<const d::TextRun> runs, d::TextRegion region) override {
        if (const auto it = recorded.find(key(runs, region)); it != recorded.end()) return it->second;
        if (!recorded.empty()) misses.push_back(key(runs, region));
        double w = 0;
        for (const auto& r : runs) w += double(d::portableCharacterRules().count(r.text)) * r.style.size * (.55 + r.style.tracking);
        return std::ceil(w * 2) / 2;
    }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion region, double) override {
        if (const auto it = lineMetrics.find(metricKey(s.size, int(d::resolved(s.weight)), s.lineHeight, s.tabular, d::regionTag(region))); it != lineMetrics.end()) return it->second;
        return {s.size * s.lineHeight, s.size * .8, s.size * .2};
    }
};
Measurer* sharedMeasurer{};
struct World {
    Measurer measurer;
    d::Context context;
    double now{100};
    World() : context(measurer, d::asciiUppercase) {
        if (sharedMeasurer) { measurer.recorded = sharedMeasurer->recorded; measurer.lineMetrics = sharedMeasurer->lineMetrics; }
        context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); appearance(Language::english);
    }
    void appearance(Language language, bool light = false) {
        d::Context::Appearance a; a.language = language; a.light = light;
        context.apply(a, now);
    }
    void advance(double seconds) { now += seconds; context.advance(now); }
};
void open(d::Canvas& canvas, World& w) { canvas.show(w.now); canvas.setActive(true, w.now); }
const d::Layer* find(const d::Layer& root, std::string_view suffix) {
    if (root.id().ends_with(suffix)) return &root;
    for (const auto* child : root.children()) if (const auto* hit = find(*child, suffix)) return hit;
    return nullptr;
}
const d::Layer& layer(const d::Canvas& canvas, std::string_view suffix) {
    const auto* hit = find(canvas.root(), suffix);
    if (!hit) throw std::runtime_error("No layer " + std::string(suffix));
    return *hit;
}
std::string text(const d::Canvas& canvas, std::string_view suffix) { return d::textOf(layer(canvas, suffix)).text(); }
std::vector<d::AccessibilityItem> items(const d::Canvas& canvas) { std::vector<d::AccessibilityItem> out; canvas.accessibilityItems(out); return out; }
std::optional<d::AccessibilityItem> item(const d::Canvas& canvas, std::string_view id) {
    for (auto& i : items(canvas)) if (i.id == id) return i;
    return std::nullopt;
}
d::KeyInput key(std::uint32_t vk) { d::KeyInput k; k.virtualKey = vk; return k; }

// ---------------------------------------------------------------------------
// TelemetryFormat / TelemetryCaption (TelemetryCanvases.swift:7-62)
// ---------------------------------------------------------------------------
void formats() {
    using namespace m::telemetry;
    check(bytes(std::nullopt) == "—" && bytes(-1.) == "—" && bytes(std::nan("")) == "—", "A missing or invalid byte count reads —");
    check(bytes(0.) == "0 B" && bytes(999.) == "999 B" && bytes(480000.) == "480 KB" && bytes(82400000000.) == "82.4 GB" && bytes(1200000.) == "1.2 MB"
        && bytes(99940.) == "99.9 KB" && bytes(1e18) == "1000 PB", "Decimal units, one decimal below 100, none from 100");
    check(rate(12000.) == "12.0 KB/s" && rate(std::nullopt) == "—", "Rates add /s; missing stays —");
    check(percent(30.44) == "30.4%" && percent(130.) == "100.0%" && percent(130., std::nullopt) == "130.0%" && percent(std::nullopt) == "—", "Percent: one decimal, clamped to 100 unless unlimited");
    const auto gb = storage(994700000000), tb = storage(1864600000000);
    check(gb.number == "994.7" && gb.unit == "GB" && tb.number == "1.86" && tb.unit == "TB" && storage(-5).number == "0.0", "Disk sizes split into numeral and unit (MODS-447)");
    check(memory(720000000) == "720 MB" && memory(1200000000) == "1.2 GB" && memory(std::nullopt) == "—", "App memory: MB below 1 GB, one-decimal GB from it (MODS-415)");
    check(duration(11) == "0:11" && duration(377) == "6:17" && duration(-1) == "—" && duration(std::nullopt) == "—", "Battery times with an unpadded hour");
    check(caption("Charge good", "电量充足", Language::english).empty() && caption("Charge good", "电量充足", Language::simplifiedChinese) == "电量充足",
        "TelemetryCaption: nothing in English, the pair elsewhere");
}

// ---------------------------------------------------------------------------
// ActivityGraph.paths (ActivityMonitorCanvas.swift:240-260)
// ---------------------------------------------------------------------------
void graphPaths() {
    const std::vector<std::optional<double>> values{std::nullopt, 10., 20., std::nullopt, 30., 40., 150.};
    const auto paths = m::activityGraphPaths(values);
    const auto line = paths.line.json().array(), area = paths.area.json().array();
    const auto x = [](std::size_t i) { return 296. * double(44 - 7 + i) / 43; };
    const auto y = [](double v) { return 44 - std::min(100., std::max(0., v)) * .42; };
    check(line.size() == 5 && line[0]["op"].string() == "move" && line[2]["op"].string() == "move", "Two runs: a gap ends the first, a missing value is no zero");
    check(near(line[0]["points"].array()[0].array()[0].number(), x(1)) && near(line[0]["points"].array()[0].array()[1].number(), y(10)), "The newest sample sits at the right edge; short histories leave the left empty");
    check(near(line[4]["points"].array()[0].array()[1].number(), y(100)), "Values clamp to 0-100");
    check(area.size() == 2 * 2 + 3 + 2 + 2 && area[0]["op"].string() == "move" && near(area[0]["points"].array()[0].array()[1].number(), 44), "Each run's area closes on the floor");
    const std::vector<std::optional<double>> lone{std::nullopt, 5., std::nullopt};
    check(m::activityGraphPaths(lone).line.empty(), "A one-point run is not drawn");
    std::vector<std::optional<double>> many(60, 50.);
    const auto full = m::activityGraphPaths(many).line.json().array();
    check(full.size() == 44 && near(full[0]["points"].array()[0].array()[0].number(), 0) && near(full[43]["points"].array()[0].array()[0].number(), 296), "The last 44 samples across 296 px");
}

// ---------------------------------------------------------------------------
// ActivityAppIcons (:566-620)
// ---------------------------------------------------------------------------
void appIcons() {
    std::vector<std::pair<std::vector<m::ActivityAppIcons::Request>, m::ActivityAppIcons::Done>> batches;
    std::vector<std::string> released;
    m::ActivityAppIcons::Loader loader{[&](std::vector<m::ActivityAppIcons::Request> r, m::ActivityAppIcons::Done done) { batches.emplace_back(std::move(r), std::move(done)); return true; },
        [&](const d::ImageSource& image) { released.push_back(image.key); }};
    m::ActivityAppIcons icons(loader);
    icons.setScale(2);
    const auto app = [](std::string id, std::string key) { m::ActivityApp a; a.identity = {id, id, key, {1}}; return a; };
    using Kind = m::ActivityAppIcons::Icon::Kind;
    check(icons.icon(app("none", "")).kind == Kind::generic, "An app without an icon key shows the generic tile");
    std::vector<std::string> loaded;
    icons.onLoad = [&](const std::vector<std::string>& ids) { loaded.insert(loaded.end(), ids.begin(), ids.end()); };
    for (int n = 0; n < 6; ++n) check(icons.icon(app("a" + std::to_string(n), "key" + std::to_string(n))).kind == Kind::pending, "A miss is pending (an empty slot)");
    check(batches.empty(), "Misses load on a later pass");
    icons.schedule();
    check(batches.size() == 1 && batches[0].first.size() == m::ActivityAppIcons::batch && batches[0].first[0].pixels == 40 && icons.queued() == 2,
        "Four per pass at 20 px x the render scale; one pass at a time");
    auto image = [](std::string key) { return std::optional(d::ImageSource{d::ImageSource::Kind::memory, std::move(key), {}, 1}); };
    check(batches[0].second({image("i0"), std::nullopt, image("i2"), image("i3")}), "The cache takes a finished pass");
    check(loaded.size() == 4 && batches.size() == 2 && batches[1].first.size() == 2, "Loaded ids are reported; the next pass follows");
    check(icons.icon(app("a0", "key0")).kind == Kind::image && icons.icon(app("a0", "key0")).image.key == "i0" && icons.icon(app("a1", "key1")).kind == Kind::generic,
        "A loaded icon is shown; an unavailable one becomes the generic tile");
    icons.setScale(3);
    check(released.size() == 3 && icons.cached() == 0 && icons.queued() == 0, "A render-scale change empties the cache (images released)");
    check(!batches[1].second({image("late"), std::nullopt}), "A pass for the old scale is not taken (the loader releases it)");
    // Overflow clears everything (:599).
    m::ActivityAppIcons small(loader);
    batches.clear();
    for (std::size_t n = 0; n < m::ActivityAppIcons::capacity; ++n) (void)small.icon(app("c" + std::to_string(n), "k"));
    small.schedule();
    std::size_t served{};
    while (served < batches.size()) { auto b = batches[served++]; std::vector<std::optional<d::ImageSource>> out(b.first.size(), image("x")); (void)b.second(out); }
    check(small.cached() == m::ActivityAppIcons::capacity, "256 entries fill the cache");
    (void)small.icon(app("over", "k"));
    small.schedule();
    (void)batches.back().second({image("y")});
    check(small.cached() == 1, "On overflow the whole cache is cleared");
}

// ---------------------------------------------------------------------------
// Activity Monitor
// ---------------------------------------------------------------------------
m::ActivitySnapshot sample(double t, double cpu) {
    m::ActivitySnapshot s; s.timestamp = t; s.uptime = t; s.cpuPercent = cpu; s.gpuPercent = 37.5;
    s.memory = m::ActivityMemory{4000000000ULL, 16000000000ULL, 1000000000ULL};
    s.upload = 12000; s.download = 400000; s.diskRead = 150000; s.diskWrite = 34000;
    return s;
}
m::ActivityAppsSnapshot apps(double t, unsigned count) {
    m::ActivityAppsSnapshot out; out.timestamp = t; out.uptime = t;
    for (unsigned n = 0; n < count; ++n) {
        m::ActivityApp a; a.identity = {"app" + std::to_string(n), "App " + std::string(1, char('A' + n)), "", {100 + n}};
        a.cpuPercent = double(n * 7 % 11); a.gpuPercent = double(n); a.memoryBytes = 100000000ULL * (n + 1); a.threads = 10 + n;
        out.items.push_back(std::move(a));
    }
    return out;
}
void activity() {
    World w;
    m::ActivityState state([](std::string_view a, std::string_view b) { return a.compare(b); });
    state.receive(sample(1, 12.25));
    std::vector<std::pair<bool, bool>> demand;
    m::ActivityCanvas canvas(w.context, state, [&](bool system, bool apps, double) { demand.push_back({system, apps}); });
    open(canvas, w);
    check(demand.size() == 1 && demand.back() == std::pair{true, false}, "Activation asks for 1 Hz system samples only (MODS-420)");
    check(canvas.header().counterText() == "LIVE", "Overview counter LIVE (MODS-400)");
    for (int n = 0; n < 5; ++n) check(near(layer(canvas, "activity.row." + std::to_string(n)).frame(), {0, 125 + 70. * n, 590, 64}), "Overview rows 590 x 64 every 70 px from y 125 (MODS-403)");
    check(text(canvas, "activity.row.0.name") == "GPU" && text(canvas, "activity.row.0.caption") == "TOTAL LOAD" && text(canvas, "activity.row.1.name") == "CPU"
        && text(canvas, "activity.row.3.name") == "Network" && text(canvas, "activity.row.4.caption") == "READ / WRITE", "GPU first, then CPU, RAM, Network, Disk I/O");
    check(text(canvas, "activity.row.0.primary") == "37.5%" && text(canvas, "activity.row.0.secondary") == "0\xE2\x80\x93" "100%", "GPU total load and its 0-100% range");
    check(text(canvas, "activity.row.2.primary") == "4.0 GB" && text(canvas, "activity.row.2.secondary") == "/ 16.0 GB" && !find(canvas.root(), "tertiary"),
        "RAM shows used / total only: the Compressed line is gone (MODS-406)");
    check(text(canvas, "activity.row.3.primary") == "\xE2\x86\x91 12.0 KB/s" && text(canvas, "activity.row.3.secondary") == "\xE2\x86\x93 400 KB/s"
        && text(canvas, "activity.row.4.primary") == "R 150 KB/s" && text(canvas, "activity.row.4.secondary") == "W 34.0 KB/s", "Network and Disk two-line readouts");
    check(near(layer(canvas, "activity.row.0.graph").frame(), {148, 10, 296, 44}) && near(layer(canvas, "activity.row.0.primary").frame().x, 456 - d::textOf(layer(canvas, "activity.row.0.primary")).padding().width),
        "Graph 296 x 44 at (148, 10); readouts at x 456");
    const auto& graph = *layer(canvas, "activity.row.3.graph").content();
    check(graph["children"].array().size() == 5 && near(graph["children"].array()[1]["opacity"].number(), .25) && near(graph["children"].array()[3]["opacity"].number(), .35),
        "Network draws upload (.25) behind download (.35) over the grid");
    const auto row0 = item(canvas, "activity.row.0");
    check(row0 && row0->role == d::AccessibilityRole::text && row0->label == "GPU" && row0->value == "37.5%, 0\xE2\x80\x93" "100%", "A row reads its readout lines (MODS-410)");
    check(item(canvas, "activityTabs:0") && item(canvas, "activityTabs:1"), "The Overview and Apps tabs (MODS-401)");
    state.receive(sample(2, 40));
    check(text(canvas, "activity.row.1.primary") == "40.0%", "A sample updates the values in place");
    const auto rowsColour = d::textOf(layer(canvas, "activity.row.2.primary")).runs[0].style.color;
    check(rowsColour == w.context.env().palette.legible(d::fixed::cyan), "RAM reads in cyan, GPU / CPU / Disk in the accent ink (MODS-404)");

    // Apps.
    state.receiveApps(apps(3, 12));
    check(canvas.perform("activity:apps", w.now) && canvas.isShowingApps() && demand.back() == std::pair{true, true}, "The Apps tab asks for app readings");
    w.advance(.2); canvas.show(w.now);
    check(canvas.header().counterText() == "12", "The Apps counter is the app count");
    const auto cpu = item(canvas, "activity:sort:cpu"), name = item(canvas, "activity:sort:name");
    check(cpu && cpu->label == "Sort by CPU, descending" && name && name->label == "Sort by App" && item(canvas, "activity:sort:threads") && !item(canvas, "activity:sort:network"),
        "Columns App, GPU, CPU, RAM, Threads; CPU descending by default (MODS-412, 413)");
    check(near(layer(canvas, "activity.sort.threads").frame(), {510, 125, 62, 26}), "Header hit areas grow 8 px above and below the 10 px label");
    check(d::textOf(layer(canvas, "activity.sort.cpu.text")).text() == "CPU \xE2\x96\xBC", "The active column shows ▼");
    for (int n = 0; n < 9; ++n) check(find(canvas.root(), "activity.app." + std::to_string(n)) != nullptr, "A pool of nine rows (MODS-416)");
    check(!find(canvas.root(), "activity.app.9"), "No row beyond the pool");
    const auto first = item(canvas, "activity.app.0.readout");
    check(first && first->label == "App D" && first->value == "GPU 3.0%, CPU 10.0%, RAM 400 MB, Threads 13", "Rows sorted by CPU read their four columns (MODS-414, 415)");
    check(near(layer(canvas, "activity.app.1").frame(), {0, 46, 584, 40}), "Rows 584 x 40 at a 46 px pitch");
    check(canvas.perform("activity:sort:name", w.now) && !canvas.sortDescending() && state.sortKey() == m::ActivitySort::name, "App sorts A-Z first (MODS-417)");
    check(item(canvas, "activity.app.0.readout")->label == "App A" && item(canvas, "activity:sort:name")->label == "Sort by App, ascending", "Sorted by name");
    check(canvas.perform("activity:sort:name", w.now) && canvas.sortDescending(), "The same column flips");
    check(canvas.perform("activity:sort:threads", w.now) && canvas.sortDescending() && item(canvas, "activity.app.0.readout")->value.ends_with("Threads 21"), "Threads sort descending");
    check(canvas.scroll({100, 300}, {0, 92}, w.now) && item(canvas, "activity.app.0.readout")->value.ends_with("Threads 19"), "Scrolling rebinds the pool");
    check(!canvas.keyDown(key(d::key::escape), w.now), "Esc on Apps is left to the host, which closes the HUD (MODS-402)");
    state.receiveApps(apps(4, 0));
    check(canvas.header().counterText() == "00" && find(canvas.root(), "activity.apps.empty.1"), "No app readings: the bilingual empty state (MODS-418)");
    canvas.setActive(false, w.now);
    check(demand.back() == std::pair{false, false} && !state.onSystem && !state.onApps, "Leaving stops the readers and observers");
}
void activityState() {
    // Threads (AppActivityMonitor.swift:99-104) and the GPU gating (:124-130).
    m::ActivityAppsRaw before, after;
    before.uptime = 1; after.uptime = 2; before.secondsPerCPUTick = after.secondsPerCPUTick = 1e-7;
    const m::ActivityAppIdentity app{"a", "A", "", {10, 11}};
    before.apps = after.apps = {app};
    before.members["a"] = after.members["a"] = {10, 11};
    for (auto* raw : {&before, &after}) for (std::uint32_t pid : {10u, 11u}) { m::ActivityProcess p; p.pid = pid; p.startID = pid; p.memoryBytes = 1000; p.threads = pid; raw->processes[pid] = p; }
    const auto items = m::deriveActivityApps(after, &before).items;
    check(items.size() == 1 && items[0].threads == 21u, "Threads sum over the app's processes");
    after.processes[11].threads.reset();
    check(!m::deriveActivityApps(after, &before).items[0].threads, "An unreadable count leaves the app's threads unknown");
    after.inventoryComplete = false;
    check(!m::deriveActivityApps(after, &before).items[0].threads, "An incomplete app has no threads");
    m::ActivityState state([](std::string_view a, std::string_view b) { return a.compare(b); });
    auto snapshot = apps(1, 3);
    snapshot.items[0].threads.reset();
    state.receiveApps(snapshot);
    state.setSort(m::ActivitySort::threads, true);
    check(state.appSnapshot().items.front().identity.id == "app2" && state.appSnapshot().items.back().identity.id == "app0", "A missing value sorts last");
    check(m::activitySortKey(m::ActivitySort::threads) == "threads" && m::activitySortKey(m::ActivitySort::gpu) == "gpu", "Sort keys gpu and threads (MODS-422)");
}

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
m::StorageVolume volume(std::string id, std::string name, std::int64_t total, std::int64_t available, bool startup, bool removable = false) {
    return {std::move(id), "C:\\", removable, startup, {std::move(name), total, available, 1}};
}
void storage() {
    World w;
    m::StorageController controller;
    std::vector<std::string> saved;
    controller.selectionChanged = [&](const std::string& id) { saved.push_back(id); };
    unsigned requests{};
    controller.requested = [&] { ++requests; };
    m::StorageCanvas canvas(w.context, controller);
    open(canvas, w);
    check(requests == 1 && controller.snapshot().isLoading && item(canvas, "storage.readings")->value == "Refreshing", "Activation reads at once (Refreshing while loading)");
    check(item(canvas, "volume") && item(canvas, "volume")->role == d::AccessibilityRole::popUp && item(canvas, "volume")->value == "Startup disk",
        "The disk dropdown is always shown and names the disk (MODS-451, 452)");
    auto read = *controller.takeCapacityRequest();
    const std::vector<m::StorageVolume> disks{volume("sys", "System", 1000000000000, 50000000000, true), volume("data", "Data", 2000000000000, 1500000000000, false, true)};
    check(controller.completeCapacity(read.token, {disks, std::nullopt}, w.now), "A read completes");
    canvas.controllerChanged(w.now);
    check(canvas.header().counterText() == "95.0%" && text(canvas, "storage.available.numeral.value") == "50.0" && text(canvas, "storage.available.numeral.unit") == "GB"
        && text(canvas, "storage.used.numeral.value") == "95.0" && text(canvas, "storage.used.numeral.unit") == "%", "Counter, AVAILABLE and USED readouts (MODS-443, 444)");
    check(canvas.cellFill() == std::pair{38, true}, "38 of 40 cells, red under 10 % free (MODS-437, 445)");
    check(item(canvas, "storage.readings")->label == "System" && item(canvas, "storage.readings")->value == "Total 1.00 TB, Used 950.0 GB, Available 50.0 GB, 95.0%",
        "The readout reads the disk and its numbers (MODS-446, 447, 454)");
    check(item(canvas, "volume")->value == "System" && item(canvas, "settings") && item(canvas, "refresh") && item(canvas, "refresh")->label == "Refresh storage", "Dropdown, refresh and Storage Settings");
    check(near(layer(canvas, "storage.cells").frame().y, 189) && near(layer(canvas, "storage.plate.0").frame().y, 233) && near(layer(canvas, "storage.plate.1").frame().x, (590 - 16) / 3. + 8),
        "Cells at y 189, plates from y 233");
    // Refresh: the toast once the read completes (MODS-449).
    check(canvas.perform("storage:refresh", w.now) && requests == 2, "Refresh re-reads (and re-lists) the disks");
    read = *controller.takeCapacityRequest();
    controller.completeCapacity(read.token, {disks, std::nullopt}, w.now); canvas.controllerChanged(w.now);
    const auto& toast = w.context.overlay().toastState().current();
    check(toast && toast->title == "Readings updated" && toast->caption == "System" && toast->icon == d::Icon::refresh, "Readings updated, with the disk name");
    w.advance(2);
    // A failed read: the error toast once.
    controller.refresh(w.now); read = *controller.takeCapacityRequest();
    controller.completeCapacity(read.token, {}, w.now); canvas.controllerChanged(w.now);
    check(w.context.overlay().toastState().current() && w.context.overlay().toastState().current()->title == "Storage unavailable"
        && w.context.overlay().toastState().current()->caption == "Storage capacity is unavailable.", "A new error toasts Storage unavailable");
    w.advance(2); canvas.controllerChanged(w.now);
    check(!w.context.overlay().toastState().current(), "The same error does not toast again");
    // The disk menu: names only, the current one selected; choosing re-enters from below.
    controller.refresh(w.now); read = *controller.takeCapacityRequest(); controller.completeCapacity(read.token, {disks, std::nullopt}, w.now); canvas.controllerChanged(w.now);
    check(canvas.performAccessibilityAction("volume", w.now) && canvas.menuOpen(), "The dropdown opens the disk menu");
    const auto data = item(canvas, "volume:data"), sys = item(canvas, "volume:sys");
    check(data && data->label == "Data" && sys && sys->on && !data->on, "Menu rows show the disk names; the current one is selected");
    check(canvas.performAccessibilityAction("volume:data", w.now) && !canvas.menuOpen() && saved == std::vector<std::string>{"data"} && requests == 5, "Choosing a disk remembers it and reads afresh (MODS-435)");
    w.advance(.2); canvas.show(w.now);
    check(item(canvas, "volume")->value == "Data" && canvas.header().counterText() == "25.0%", "The chosen disk shows at once with its listed numbers");
    // Mount events re-list (after a read in flight).
    read = *controller.takeCapacityRequest();
    controller.volumesChanged(w.now);
    check(!controller.takeCapacityRequest(), "A mount event during a read waits for it");
    controller.completeCapacity(read.token, {{disks[0]}, std::nullopt}, w.now);
    check(controller.takeCapacityRequest().has_value() && controller.currentVolume()->id == "sys" && controller.selectedVolume() == "data",
        "Then re-reads; an unmounted choice falls back to the startup disk and is kept (MODS-431, 434)");
    canvas.setActive(false, w.now);
    check(!controller.active() && !canvas.nextWakeTime(w.now), "Leaving stops reading and its 60 s timer");
    // StorageVolumeCatalog.limited (:116-123).
    std::vector<m::StorageVolume> many;
    for (int n = 0; n < 9; ++n) many.push_back(volume("v" + std::to_string(n), "V", 10, 5, n == 0));
    const auto limited = m::limitedStorageVolumes(many, std::string("v8"));
    check(limited.size() == 7 && limited.back().id == "v8" && m::limitedStorageVolumes(many, std::string("v2")).back().id == "v6", "A chosen disk beyond seven takes the last place (MODS-432)");
    check(m::StorageCanvas::localizedError(std::nullopt, Language::simplifiedChinese) == "无法读取存储容量。"
        && m::StorageCanvas::localizedError(std::string("The startup filesystem is unavailable."), Language::english) == "The startup filesystem is unavailable.", "Error strings (MODS-453)");
}
// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------
std::vector<std::string> chips(const d::Canvas& canvas) {
    std::vector<std::string> out;
    for (const auto* chip : layer(canvas, "battery.status").children()) out.push_back(d::textOf(*chip->children().front()).text());
    return out;
}
void battery() {
    using T = m::BatteryCanvas::Tone;
    check(m::BatteryCanvas::tone(19) == T::low && m::BatteryCanvas::tone(20) == T::moderate && m::BatteryCanvas::tone(70) == T::moderate && m::BatteryCanvas::tone(71) == T::good
        && !m::BatteryCanvas::tone(std::nullopt), "Tones follow BatteryLevelTone 70 / 20 (MODS-457, 467)");
    check(m::BatteryCanvas::color(T::good) == d::rgb(0x8CE03C) && m::BatteryCanvas::cellTone(T::low).high == d::rgb(0xFF6B6D) && m::BatteryCanvas::cellTone(T::moderate).low == d::rgb(0xE9CF12),
        "Tone colours and cell gradients");
    const auto pulse = m::BatteryCanvas::pulse(T::moderate);
    check(near(pulse.r, 1 * .35 + 42. / 255 * .65) && near(pulse.b, 42. / 255 * .65), "The pulse is the tone mixed 35 % into #2A2A2A");
    check(m::BatteryCanvas::condition(std::string("Service Battery"), Language::english) == "Service Recommended" && m::BatteryCanvas::condition(std::nullopt, Language::english) == "—"
        && m::BatteryCanvas::condition(std::string("Good"), Language::simplifiedChinese) == "正常", "Condition words (MODS-463)");
    World w;
    m::BatteryCanvas canvas(w.context);
    unsigned alerts{};
    canvas.onOpenBatteryAlert = [&] { ++alerts; };
    m::BatteryReading r; r.present = true; r.percentage = 63; r.pluggedIn = true; r.charging = true;
    r.capacity = m::BatteryReading::Capacity{4200, 5000, "mWh"}; r.cycleCount = 1234; r.temperatureCelsius = 85.26;
    canvas.update(r, m::ChargeMetric::gpu, w.now);
    open(canvas, w);
    check(canvas.header().counterText() == "LIVE" && text(canvas, "battery.source") == "// Power source", "Header and the power source label");
    check(text(canvas, "battery.numeral.numeral.value") == "63" && text(canvas, "battery.numeral.numeral.unit") == "%", "The level and its unit (MODS-458)");
    check(chips(canvas) == std::vector<std::string>{"MODERATE", "CHARGE MODE"} && text(canvas, "battery.caption").empty(), "Tone and mode chips; no CJK caption in English (MODS-459)");
    check(text(canvas, "battery.timeLabel") == "Until full" && text(canvas, "battery.time") == "\xE2\x80\x94", "Until full: — without an OS estimate while charging (MODS-460)");
    check(canvas.liveAnimationCount() == 3, "The next three empty cells pulse while charging and active (MODS-461)");
    const auto value = [&](int n) { return d::textOf(layer(canvas, "battery.plate." + std::to_string(n) + ".stat.value")); };
    check(value(0).text() == "External" && value(1).text() == "4,200 / 5,000 mWh" && value(2).text() == "1,234" && value(3).text() == "\xE2\x80\x94"
        && value(4).text() == "85.3 °C" && value(5).text() == "GPU", "Source, Capacity (mWh), Cycles, Condition, Temperature, Bar reading (MODS-462)");
    check(value(4).runs[0].style.color == w.context.env().palette.legible(d::fixed::orange) && value(5).runs[0].style.color == w.context.env().palette.accentInk(),
        "A hot battery reads orange; the bar reading is highlighted");
    const auto reading = item(canvas, "battery.reading");
    check(reading && reading->label == "Battery alert, Bar reading GPU" && canvas.performAccessibilityAction("battery.reading", w.now) && alerts == 1, "BAR READING links to the Battery alert (MODS-464)");
    check(item(canvas, "battery.level")->value == "63%, MODERATE, CHARGE MODE, Until full \xE2\x80\x94" && item(canvas, "battery.plate.1")->value == "4,200 / 5,000 mWh", "Readouts (MODS-465)");
    // On battery, Chinese UI.
    w.appearance(Language::simplifiedChinese);
    r.pluggedIn = false; r.charging = false; r.minutesToEmpty = 377; r.capacity = m::BatteryReading::Capacity{63, 100, "%"}; r.temperatureCelsius.reset();
    canvas.update(r, m::ChargeMetric::battery, w.now);
    check(chips(canvas) == std::vector<std::string>{"MODERATE", "POWER MODE"} && text(canvas, "battery.caption") == "电量中等\xE3\x80\x80电源模式", "Chips stay English; the caption in the UI language");
    check(text(canvas, "battery.timeLabel") == "剩余时间" && text(canvas, "battery.time") == "6:17" && value(1).text() == "63%" && value(0).text() == "电池" && value(4).text() == "\xE2\x80\x94",
        "Remaining time, the percent pair, Battery source");
    check(canvas.liveAnimationCount() == 0, "No pulse on battery");
    w.appearance(Language::english);
    r.pluggedIn = true; r.fullyCharged = true; r.percentage = 100; r.adapterWatts = 96;
    canvas.update(r, m::ChargeMetric::battery, w.now);
    check(text(canvas, "battery.time") == "0:00" && chips(canvas).front() == "GOOD" && value(0).text() == "External 96 W", "Full: 0:00, GOOD, the adapter's watts");
    m::BatteryReading none;
    canvas.update(none, m::ChargeMetric::battery, w.now);
    check(text(canvas, "battery.numeral.numeral.value") == "\xE2\x80\x94" && chips(canvas) == std::vector<std::string>{"NO BATTERY", "POWER MODE"} && text(canvas, "battery.time") == "\xE2\x80\x94",
        "No battery: —, NO BATTERY, POWER MODE");
    r = {}; r.present = true; r.percentage = 40; r.pluggedIn = true; r.charging = true;
    canvas.update(r, m::ChargeMetric::battery, w.now);
    check(canvas.liveAnimationCount() == 3, "Charging pulses again");
    canvas.setActive(false, w.now);
    check(canvas.liveAnimationCount() == 0, "The pulse stops when Battery is not the active module");
}

// ---------------------------------------------------------------------------
// The Mac 1.3.0 oracle (tests/fixtures/telemetry-source.json)
// ---------------------------------------------------------------------------
using J = ehud::data::Json;
std::optional<double> opt(const J& v) { return v.isNull() ? std::nullopt : std::optional(v.number()); }
std::string hex(d::Color c) {
    char out[32];
    const auto byte = [](double v) { return int(std::round(std::min(1., std::max(0., v)) * 255)); };
    std::snprintf(out, sizeof out, "%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
    return out;
}
std::string roleName(d::AccessibilityRole r) {
    switch (r) {
    case d::AccessibilityRole::button: return "button";
    case d::AccessibilityRole::toggle: return "toggle";
    case d::AccessibilityRole::popUp: return "popUp";
    case d::AccessibilityRole::menuItem: return "menuItem";
    case d::AccessibilityRole::tab: return "tab";
    case d::AccessibilityRole::slider: return "slider";
    case d::AccessibilityRole::text: return "text";
    case d::AccessibilityRole::edit: return "edit";
    case d::AccessibilityRole::group: return "group";
    }
    return "?";
}
std::string str(d::Rect r) { std::ostringstream s; s << "(" << r.x << ", " << r.y << ", " << r.width << ", " << r.height << ")"; return s.str(); }
d::Rect rectOf(const J& a) { return {a.array()[0].number(), a.array()[1].number(), a.array()[2].number(), a.array()[3].number()}; }
bool close(d::Rect a, d::Rect b, double e = .01) { return std::abs(a.x - b.x) <= e && std::abs(a.y - b.y) <= e && std::abs(a.width - b.width) <= e && std::abs(a.height - b.height) <= e; }
// The colour shown: a tinted text is authored in white and coloured by its
// layer's model tint (the library's substitute for colour copies).
struct TextRow { std::string text; d::Rect frame; const d::TextBlock* block; std::optional<d::Color> tint; };
void collect(const d::Layer& layer, const d::Layer& root, std::vector<TextRow>& out) {
    if (layer.hidden() || layer.value(d::Channel::opacity) < .01) return;
    if (const auto* block = layer.text(); block && !block->text().empty() && layer.parent())
        out.push_back({block->text(), d::convert(block->lineFrame, *layer.parent(), root), block, layer.tint()});
    for (const auto* child : layer.children()) collect(*child, root, out);
}
std::string alignmentName(d::TextAlignment a) { return a == d::TextAlignment::left ? "left" : a == d::TextAlignment::center ? "center" : "right"; }
// One canvas state against the Mac's: accessibility items by id (role, label,
// value, selection, rect) and the visible text layers (text, line box in the
// content box, runs with size, weight, colour, line height, tracking, case,
// figures; alignment, lines, shrink).
void compare(const d::Canvas& canvas, const J& mac, const std::string& name) {
    std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
    std::map<std::string, d::AccessibilityItem> byID;
    for (auto& i : items) byID[i.id] = i;
    for (const auto& m : mac["accessibility"].array()) {
        const auto id = m["id"].string();
        const auto found = byID.find(id);
        check(found != byID.end(), name + ": accessibility item " + id);
        const auto& w = found->second;
        check(roleName(w.role) == m["role"].string(), name + ": " + id + " role " + roleName(w.role) + " vs " + m["role"].string());
        check(w.label == m["label"].string(), name + ": " + id + " label '" + w.label + "' vs '" + m["label"].string() + "'");
        if (m["value"].isString()) check(w.value == m["value"].string(), name + ": " + id + " value '" + w.value + "' vs '" + m["value"].string() + "'");
        if (m["on"].isBool()) check(w.on == m["on"].boolean(), name + ": " + id + " selection");
        check(close(w.rect, rectOf(m["rect"])), name + ": " + id + " rect " + str(w.rect) + " vs " + str(rectOf(m["rect"])));
    }
    check(items.size() == mac["accessibility"].array().size(), name + ": " + std::to_string(items.size()) + " accessibility items vs " + std::to_string(mac["accessibility"].array().size()));
    std::vector<TextRow> texts; collect(canvas.root(), canvas.root(), texts);
    std::vector<bool> used(texts.size());
    for (const auto& m : mac["texts"].array()) {
        const auto text = m["text"].string();
        const auto frame = rectOf(m["frame"]);
        std::size_t hit = texts.size(), loose = texts.size();
        for (std::size_t n = 0; n < texts.size(); ++n) {
            if (used[n] || texts[n].text != text) continue;
            if (close(texts[n].frame, frame)) { hit = n; break; }
            if (loose == texts.size()) loose = n;
        }
        check(hit < texts.size() || loose < texts.size(), name + ": text '" + text + "'");
        check(hit < texts.size(), name + ": text '" + text + "' line box " + str(texts[loose == texts.size() ? 0 : loose].frame) + " vs " + str(frame));
        used[hit] = true;
        const auto& block = *texts[hit].block;
        const auto& runs = m["runs"].array();
        check(block.runs.size() == runs.size(), name + ": '" + text + "' run count");
        for (std::size_t r = 0; r < runs.size(); ++r) {
            const auto& w = block.runs[r]; const auto& o = runs[r];
            const auto where = name + ": '" + text + "' run " + std::to_string(r);
            check(w.text == o["text"].string() && near(w.style.size, o["size"].number()) && int(d::resolved(w.style.weight)) == int(o["weight"].number()), where + " text/size/weight");
            const auto shown = texts[hit].tint && w.style.color == d::white(1) ? *texts[hit].tint : w.style.color;
            check(hex(shown) == o["color"].string(), where + " colour " + hex(shown) + " vs " + o["color"].string());
            check(near(w.style.lineHeight, o["lineHeight"].number()) && near(w.style.tracking, o["tracking"].number()) && w.style.uppercase == o["uppercase"].boolean()
                && w.style.tabular == o["tabular"].boolean(), where + " line height/tracking/case/figures");
        }
        check(alignmentName(block.alignment) == m["alignment"].string(), name + ": '" + text + "' alignment");
        check(block.maximumLines == int(m["maximumLines"].number()) && near(block.minimumScale, m["minimumScale"].number()), name + ": '" + text + "' lines/shrink");
        check(m["language"].isNull() == !block.language && (!block.language || std::string(d::regionTag(*block.language)) == m["language"].string()), name + ": '" + text + "' language");
    }
    std::string extra;
    for (std::size_t n = 0; n < texts.size(); ++n) if (!used[n]) extra += " '" + texts[n].text + "' " + str(texts[n].frame);
    check(extra.empty(), name + ": text layers the Mac does not show:" + extra);
}
const J& macState(const J& root, std::string_view name, std::string_view language) {
    for (const auto& c : root["canvases"].array()) if (c["name"].string() == name && c["language"].string() == language) return c;
    throw std::runtime_error("No oracle state " + std::string(name));
}
m::ChargeMetric metricNamed(std::string_view name) {
    return name == "gpu" ? m::ChargeMetric::gpu : name == "cpu" ? m::ChargeMetric::cpu : name == "ram" ? m::ChargeMetric::ram
        : name == "network" ? m::ChargeMetric::network : name == "disk" ? m::ChargeMetric::disk : m::ChargeMetric::battery;
}
std::vector<m::ActivitySnapshot> fixtureHistory() {
    // SystemActivityMonitor.fixture (SystemActivityMonitor.swift:285-304).
    std::vector<m::ActivitySnapshot> out;
    for (int index = 0; index < 60; ++index) {
        const double phase = double(index) / 6;
        m::ActivitySnapshot s; s.timestamp = 1'700'000'000 + index; s.uptime = 1000 + index;
        s.cpuPercent = 24 + std::sin(phase) * 9;
        s.memory = m::ActivityMemory{10'200'000'000ULL + std::uint64_t(index) * 100'000ULL, 17'179'869'184ULL, 760'000'000ULL};
        s.upload = 80'000 + std::cos(phase) * 20'000; s.download = 1'400'000 + std::sin(phase / 2) * 700'000;
        s.diskRead = 2'000'000 + std::cos(phase) * 1'300'000; s.diskWrite = 300'000 + std::sin(phase) * 140'000;
        s.gpuPercent = 30 + std::sin(phase * .7 + 1) * 8;
        out.push_back(s);
    }
    return out;
}
m::ActivityAppsSnapshot fixtureApps(bool many, bool empty) {
    // AppActivityMonitor.fixture (AppActivityMonitor.swift:303-316) and the oracle's twelve apps.
    m::ActivityAppsSnapshot out; out.timestamp = 1'700'000'000; out.uptime = 1000;
    if (empty) return out;
    if (!many) {
        const std::array<std::tuple<const char*, const char*, double, std::uint64_t, double, std::uint64_t>, 3> apps{{
            {"Finder", "com.apple.finder", 2.4, 82'000'000, 0, 11}, {"Safari", "com.apple.Safari", 18.6, 720'000'000, .8, 60}, {"Music", "com.apple.Music", 4.1, 164'000'000, 0, 16}}};
        for (std::size_t n = 0; n < apps.size(); ++n) {
            const auto& [name, id, cpu, memory, gpu, threads] = apps[n];
            m::ActivityApp a; a.identity = {id, name, "", {std::uint32_t(n + 100)}};
            a.cpuPercent = cpu; a.memoryBytes = memory; a.upload = double(n * 900); a.download = double(n * 82'000); a.diskRead = double(n * 200'000); a.diskWrite = double(n * 15'000);
            a.gpuPercent = gpu; a.threads = threads;
            out.items.push_back(std::move(a));
        }
        return out;
    }
    for (int n = 0; n < 12; ++n) {
        m::ActivityApp a; a.identity = {"app" + std::to_string(n), "App " + std::to_string(n), "", {std::uint32_t(100 + n)}};
        a.cpuPercent = double(n * 7 % 11); a.memoryBytes = std::uint64_t(n + 1) * 100'000'000ULL; a.gpuPercent = double(n % 4); a.threads = std::uint64_t(6 + n);
        out.items.push_back(std::move(a));
    }
    return out;
}
void oracle(const char* path) {
    std::ifstream file(path, std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(file), {}};
    const auto root = J::parse(bytes, 32 * 1024 * 1024);
    // Number formats (TelemetryFormat).
    for (const auto& c : root["formats"].array()) {
        const auto value = c["value"].isString() ? std::optional(std::nan("")) : opt(c["value"]);
        check(m::telemetry::bytes(value) == c["bytes"].string() && m::telemetry::rate(value) == c["rate"].string() && m::telemetry::percent(value) == c["percent"].string()
            && m::telemetry::percent(value, std::nullopt) == c["percentUnlimited"].string(), "TelemetryFormat bytes/rate/percent " + c["bytes"].string());
    }
    for (const auto& c : root["storageFormats"].array()) { const auto v = m::telemetry::storage(c["bytes"].integer()); check(v.number == c["number"].string() && v.unit == c["unit"].string(), "TelemetryFormat.storage"); }
    for (const auto& c : root["memoryFormats"].array()) check(m::telemetry::memory(c["bytes"].isNull() ? std::nullopt : std::optional(std::uint64_t(c["bytes"].integer()))) == c["memory"].string(), "TelemetryFormat.memory");
    for (const auto& c : root["durations"].array()) check(m::telemetry::duration(c["minutes"].isNull() ? std::nullopt : std::optional(int(c["minutes"].integer()))) == c["text"].string(), "TelemetryFormat.duration");
    // ActivityGraph.paths.
    for (const auto& c : root["graphs"].array()) {
        std::vector<std::optional<double>> values;
        for (const auto& v : c["values"].array()) values.push_back(v.isString() ? std::optional<double>(INFINITY) : opt(v));
        const auto paths = m::activityGraphPaths(values);
        for (const auto& [mine, theirs] : {std::pair{paths.line.json(), c["line"]}, std::pair{paths.area.json(), c["area"]}}) {
            check(mine.array().size() == theirs.array().size(), "Graph path element count");
            for (std::size_t n = 0; n < mine.array().size(); ++n) {
                check(mine.array()[n]["op"].string() == theirs.array()[n]["op"].string(), "Graph path op");
                const auto& a = mine.array()[n]["points"].array(); const auto& b = theirs.array()[n]["points"].array();
                check(a.size() == b.size(), "Graph path points");
                for (std::size_t k = 0; k < a.size(); ++k) check(near(a[k].array()[0].number(), b[k].array()[0].number(), 1e-9) && near(a[k].array()[1].number(), b[k].array()[1].number(), 1e-9), "Graph point");
            }
        }
    }
    // BatteryLevelTone and BatteryDiagnosticsParsing.
    for (const auto& c : root["tones"].array()) {
        const auto level = opt(c["level"]);
        const auto tone = level && *level >= 0 ? m::batteryLevelTone(unsigned(*level)) : std::nullopt;
        const std::string name = !tone ? "null" : *tone == m::BatteryLevelTone::green ? "green" : *tone == m::BatteryLevelTone::yellow ? "yellow" : "red";
        check(name == (c["tone"].isNull() ? "null" : c["tone"].string()), "BatteryLevelTone at " + c["level"].encode());
    }
    for (const auto& c : root["diagnostics"].array()) {
        if (!c["input"].isNumber()) continue; // booleans, strings and absent values never reach the Windows readers
        const double v = c["input"].number();
        check(m::batteryCycleCount(v) == (c["cycleCount"].isNull() ? std::nullopt : std::optional(int(c["cycleCount"].integer()))), "cycleCount " + c["input"].encode());
        const auto t = m::batteryTemperatureCelsius(v);
        check(bool(t) == !c["temperature"].isNull() && (!t || near(*t, c["temperature"].number(), 1e-12)), "temperature " + c["input"].encode());
        check(m::batteryAdapterWatts(v) == (c["watts"].isNull() ? std::nullopt : std::optional(int(c["watts"].integer()))), "watts " + c["input"].encode());
        check(m::batteryMinutes(v) == (c["minutes"].isNull() ? std::nullopt : std::optional(int(c["minutes"].integer()))), "minutes " + c["input"].encode());
    }
    // App threads.
    for (const auto& c : root["threads"].array()) {
        m::ActivityAppsRaw raw; raw.uptime = 11; raw.secondsPerCPUTick = .01; raw.inventoryComplete = c["complete"].boolean();
        raw.apps = {{"app", "Example", "", {7, 8}}}; raw.members["app"] = {7, 8};
        for (const auto& [pid, key] : {std::pair{7u, "a"}, std::pair{8u, "b"}}) { m::ActivityProcess p; p.pid = pid; p.startID = pid; p.cpuTicks = 100; p.memoryBytes = 1000; p.startUptime = 1;
            if (!c[key].isNull()) p.threads = std::uint32_t(c[key].integer()); raw.processes[pid] = p; }
        const auto threads = m::deriveActivityApps(raw).items[0].threads;
        check(threads == (c["threads"].isNull() ? std::nullopt : std::optional(std::uint64_t(c["threads"].integer()))), "App threads");
    }
    // StorageController: the disk choice script.
    {
        m::StorageController c; std::optional<std::string> saved; c.selectionChanged = [&](const std::string& id) { saved = id; };
        std::vector<m::StorageCapacityRequest> workers;
        const auto collect = [&] { if (auto r = c.takeCapacityRequest()) workers.push_back(*r); };
        c.requested = collect;
        std::vector<m::StorageVolume> listed;
        const auto volume = [](std::string id, std::string name, std::int64_t total, std::int64_t available, bool startup) {
            return m::StorageVolume{std::move(id), "C:\\", false, startup, {std::move(name), total, available, 0}}; };
        std::size_t index{};
        const auto& rows = root["storageController"].array();
        const auto step = [&](std::string_view action, double now, auto&& body) {
            body(now);
            const auto& r = rows[index++];
            const auto where = "Storage controller step " + std::to_string(index) + " " + std::string(action);
            check(r["action"].string() == action, where + ": script order");
            const auto& s = c.snapshot();
            check(s.isLoading == r["loading"].boolean() && (s.error ? J(*s.error) : J()) == r["error"], where + ": loading/error");
            check(bool(s.capacity) == !r["capacity"].isNull() && (!s.capacity || (s.capacity->volumeName == r["capacity"]["name"].string()
                && s.capacity->totalBytes == r["capacity"]["total"].integer() && s.capacity->availableBytes == r["capacity"]["available"].integer())), where + ": capacity");
            J::Array ids; for (const auto& v : c.volumes()) ids.push_back(v.id);
            check(J(ids) == r["volumes"] && (c.currentVolume() ? J(c.currentVolume()->id) : J()) == r["current"] && (c.selectedVolume() ? J(*c.selectedVolume()) : J()) == r["selected"]
                && (saved ? J(*saved) : J()) == r["saved"], where + ": disks, current, choice");
            check(c.revision() == std::uint64_t(r["revision"].integer()) && workers.size() == std::size_t(r["workers"].integer()), where + ": publications and queued reads");
        };
        const auto work = [&](double now) { if (workers.empty()) return; const auto r = workers.front(); workers.erase(workers.begin()); c.completeCapacity(r.token, {listed, std::nullopt}, now); };
        listed = {volume("sys", "System", 1000, 350, true), volume("data", "Data", 2000, 1500, false), volume("card", "EF-CARD", 60, 47, false)};
        step("activate", 0, [&](double t) { c.activate(t); });
        step("work", 1, work);
        step("select data", 2, [&](double t) { (void)c.selectVolume("data", t); });
        step("select data again", 2.5, [&](double t) { (void)c.selectVolume("data", t); });
        step("select missing", 2.6, [&](double t) { (void)c.selectVolume("missing", t); });
        step("work", 3, work);
        listed = {volume("sys", "System", 1000, 340, true)};
        step("refresh", 4, [&](double t) { c.refresh(t); });
        step("unmount during read", 4.5, [&](double t) { c.volumesChanged(t); });
        step("work", 5, work);
        listed = {volume("sys", "System", 1000, 330, true), volume("data", "Data", 2000, 1400, false)};
        step("work", 6, work);
        step("deactivate", 7, [&](double) { c.deactivate(); });
        step("mount hidden", 8, [&](double t) { c.volumesChanged(t); });
        step("activate", 9, [&](double t) { c.activate(t); });
        step("work", 10, work);
        listed = {};
        step("refresh", 11, [&](double t) { c.refresh(t); });
        step("work", 12, work);
        check(index == rows.size(), "Every Storage controller step compared");
    }
    for (const auto& c : root["limited"].array()) {
        std::vector<m::StorageVolume> many; for (int n = 0; n < 9; ++n) many.push_back({"v" + std::to_string(n), "C:\\", false, n == 0, {"V", 10, 5, 0}});
        J::Array ids; for (const auto& v : m::limitedStorageVolumes(many, c["keeping"].isNull() ? std::nullopt : std::optional(c["keeping"].string()))) ids.push_back(v.id);
        check(J(ids) == c["ids"], "StorageVolumeCatalog.limited");
    }
    // The canvases.
    Measurer recorded;
    for (const auto& m : root["text"]["metrics"].array())
        recorded.lineMetrics[Measurer::metricKey(m["size"].number(), int(m["weight"].number()), m["lineHeight"].number(), m["tabular"].boolean(), m["language"].string())]
            = {m["height"].number(), m["ascent"].number(), m["descent"].number()};
    for (const auto& w : root["text"]["widths"].array()) {
        std::vector<d::TextRun> runs;
        for (const auto& r : w["runs"].array()) { d::TextStyle style; style.size = r["size"].number(); style.weight = d::FontWeight(int(r["weight"].number())); style.tracking = r["tracking"].number(); style.tabular = r["tabular"].boolean(); runs.push_back({r["displayed"].string(), style}); }
        if (const auto region = d::regionFromTag(w["language"].string())) recorded.recorded[Measurer::key(runs, *region)] = w["width"].number();
    }
    sharedMeasurer = &recorded;
    for (const auto& [language, tag] : {std::pair{Language::english, "english"}, std::pair{Language::simplifiedChinese, "simplifiedChinese"}}) {
        // Battery.
        {
            World w; w.appearance(language);
            m::BatteryCanvas canvas(w.context);
            canvas.update({}, m::ChargeMetric::battery, w.now);
            open(canvas, w);
            const auto show = [&](std::string name, m::BatteryReading r, std::string_view metric) {
                canvas.update(std::move(r), metricNamed(metric), w.now);
                const auto& mac = macState(root, "battery." + name, tag);
                compare(canvas, mac, "battery." + name + " " + tag);
                check(canvas.liveAnimationCount() == int(mac["pulsing"].integer()), "battery." + name + ": pulsing cells");
            };
            m::BatteryReading r; r.present = true; r.percentage = 63; r.pluggedIn = true; r.charging = true; r.capacity = m::BatteryReading::Capacity{3262, 4380, "mAh"};
            r.health = "Good"; r.cycleCount = 1234; r.temperatureCelsius = 31.25; r.adapterWatts = 96; r.minutesToFull = 42;
            show("charging", r, "gpu");
            r = {}; r.present = true; r.percentage = 15; r.capacity = m::BatteryReading::Capacity{15, 100, "%"}; r.health = "Service Battery"; r.temperatureCelsius = 85.26; r.minutesToEmpty = 377;
            show("low", r, "battery");
            r = {}; r.present = true; r.percentage = 71; r.pluggedIn = true; r.charging = true; r.health = "Fair";
            show("estimating", r, "cpu");
            r = {}; r.present = true; r.percentage = 80; r.pluggedIn = true;
            show("paused", r, "ram");
            r = {}; r.present = true; r.percentage = 100; r.pluggedIn = true; r.fullyCharged = true; r.health = "Odd report";
            show("full", r, "network");
            show("none", {}, "disk");
            canvas.setActive(false, w.now);
            compare(canvas, macState(root, "battery.inactive", tag), std::string("battery.inactive ") + tag);
        }
        // Storage.
        {
            World w; w.appearance(language);
            m::StorageController c;
            std::vector<m::StorageCapacityRequest> workers;
            c.requested = [&] { if (auto r = c.takeCapacityRequest()) workers.push_back(*r); };
            const auto disk = [](std::string id, std::string name, std::int64_t total, std::int64_t available, bool startup) {
                return m::StorageVolume{std::move(id), "C:\\", false, startup, {std::move(name), total, available, 1}}; };
            std::vector<m::StorageVolume> disks{disk("fixture-startup", "Macintosh HD", 1'000'000'000'000, 420'000'000'000, true),
                disk("fixture-backup", "Backup SSD", 1'864'600'000'000, 1'160'000'000'000, false), disk("fixture-card", "EF-CARD", 59'500'000'000, 46'700'000'000, false)};
            bool readNil{};
            m::StorageCanvas canvas(w.context, c);
            const auto run = [&] { while (!workers.empty()) { const auto r = workers.front(); workers.erase(workers.begin()); c.completeCapacity(r.token, readNil ? m::StorageCapacityResult{} : m::StorageCapacityResult{disks, std::nullopt}, w.now); canvas.controllerChanged(w.now); } };
            const auto toast = [&]() -> J { const auto& t = w.context.overlay().toastState().current(); return t ? J(J::Array{t->title, t->caption}) : J(); };
            open(canvas, w);
            compare(canvas, macState(root, "storage.loading", tag), std::string("storage.loading ") + tag);
            run();
            const auto cells = [&](const J& mac) { check(canvas.cellFill() == std::pair{int(mac["cells"].array()[0].integer()), mac["cells"].array()[1].integer() == 1}, "Storage cells"); };
            compare(canvas, macState(root, "storage.startup", tag), std::string("storage.startup ") + tag); cells(macState(root, "storage.startup", tag));
            check(canvas.perform("storage:refresh", w.now), "Storage refresh"); run();
            compare(canvas, macState(root, "storage.refreshed", tag), std::string("storage.refreshed ") + tag);
            check(toast() == macState(root, "storage.refreshed", tag)["toast"], "Storage refresh toast");
            w.context.overlay().clearToast();
            (void)c.selectVolume("fixture-backup", w.now); canvas.controllerChanged(w.now); canvas.navigate(0, 1); w.advance(.2); canvas.show(w.now);
            compare(canvas, macState(root, "storage.backup", tag), std::string("storage.backup ") + tag); cells(macState(root, "storage.backup", tag));
            disks[1] = disk("fixture-backup", "Backup SSD", 1'864'600'000'000, 100'000'000'000, false); run();
            compare(canvas, macState(root, "storage.low", tag), std::string("storage.low ") + tag); cells(macState(root, "storage.low", tag));
            readNil = true; check(canvas.perform("storage:refresh", w.now), "Storage refresh"); run();
            compare(canvas, macState(root, "storage.error", tag), std::string("storage.error ") + tag);
            check(toast() == macState(root, "storage.error", tag)["toast"], "Storage error toast");
            w.context.overlay().clearToast();
            canvas.setActive(false, w.now);
        }
        // Activity Monitor.
        {
            World w; w.appearance(language);
            const auto make = [&](bool many, bool empty) {
                auto state = std::make_unique<m::ActivityState>([](std::string_view a, std::string_view b) { return a.compare(b); });
                for (const auto& s : fixtureHistory()) state->receive(s);
                state->receiveApps(fixtureApps(many, empty));
                return state;
            };
            auto state = make(false, false);
            m::ActivityCanvas canvas(w.context, *state);
            open(canvas, w);
            compare(canvas, macState(root, "activity.overview", tag), std::string("activity.overview ") + tag);
            const auto apps = [&](m::ActivityCanvas& c, std::string_view action) { check(c.perform(action, w.now), "Activity action"); w.advance(.2); c.show(w.now); };
            apps(canvas, "activity:apps");
            compare(canvas, macState(root, "activity.apps", tag), std::string("activity.apps ") + tag);
            apps(canvas, "activity:sort:name");
            compare(canvas, macState(root, "activity.byName", tag), std::string("activity.byName ") + tag);
            apps(canvas, "activity:sort:threads");
            compare(canvas, macState(root, "activity.byThreads", tag), std::string("activity.byThreads ") + tag);
            canvas.setActive(false, w.now);
            auto longState = make(true, false);
            m::ActivityCanvas pool(w.context, *longState);
            open(pool, w); apps(pool, "activity:apps");
            compare(pool, macState(root, "activity.pool", tag), std::string("activity.pool ") + tag);
            check(pool.scroll({100, 300}, {0, 92}, w.now), "Activity scroll");
            compare(pool, macState(root, "activity.scrolled", tag), std::string("activity.scrolled ") + tag);
            pool.setActive(false, w.now);
            auto emptyState = make(false, true);
            m::ActivityCanvas empty(w.context, *emptyState);
            open(empty, w); apps(empty, "activity:apps");
            compare(empty, macState(root, "activity.empty", tag), std::string("activity.empty ") + tag);
            empty.setActive(false, w.now);
        }
    }
    sharedMeasurer = nullptr;
    std::cout << "Oracle: " << Measurer::misses.size() << " text widths the Mac did not record\n";
    for (const auto& miss : Measurer::misses) std::cout << "  " << miss << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        formats();
        graphPaths();
        appIcons();
        activity();
        activityState();
        storage();
        battery();
        if (argc == 2) oracle(argv[1]); else check(argc == 1, "Pass the optional telemetry-source.json");
        std::cout << "PASS " << checks << " telemetry canvas checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
