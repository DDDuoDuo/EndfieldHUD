// ModuleRegistry contracts: routing order, capture chains, failure isolation
// and its bounded record (app/module_failure_log.hpp, in a new temporary
// folder), and release order. Synthetic owners only; no window, renderer or
// GPU, never the user's data root.
#include "app/module_owner.hpp"
#include "core/data/file_io.hpp"
#include <chrono>
#include <filesystem>
#include "native/renderer.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>

namespace app = endfield::app;
namespace core = endfield::core;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }

struct Probe final : app::ModuleOwner {
    std::string id;
    app::ModuleRouting route;
    std::vector<std::string>* log{};
    bool consumes{}, captures{}, throwsOnUpdate{}, essentialOwner{}, covering{};
    core::Module module{core::Module::notes};
    unsigned updates{}, releases{}, appearance{};
    std::vector<app::HostEvent> events;
    bool artworkOnEvent{}, throwsOnEvent{};
    Probe(std::string name, std::vector<std::string>& journal) : id(std::move(name)), log(&journal) {}
    std::string_view name() const noexcept override { return id; }
    app::ModuleRouting routing() const noexcept override { return route; }
    bool essential() const noexcept override { return essentialOwner; }
    bool presents(core::Module m) const noexcept override { return m == module; }
    void setAppearance(const app::ModuleAppearance&, double) override { ++appearance; }
    void update(const app::ModuleFrame&) override { if (throwsOnUpdate) throw std::out_of_range("synthetic failure with /private/path"); ++updates; }
    bool covers(core::Point) const override { return covering; }
    bool capturesPointer() const override { return captures; }
    bool pointer(const app::PointerEvent&, double) override { log->push_back(id); return consumes; }
    bool key(const app::KeyEvent&, double) override { log->push_back(id); return consumes; }
    void release(endfield::native::Renderer&) override { ++releases; log->push_back("release:" + id); }
    bool hostEvent(const app::HostEvent& e, double) override {
        if (throwsOnEvent) throw std::invalid_argument("synthetic broadcast failure");
        events.push_back(e); log->push_back("event:" + id); return artworkOnEvent;
    }
};

// Host broadcasts (app/module_owner.hpp HostEvent): every enabled owner in
// registration order, the artwork result is the union, a failing owner is
// isolated like any event, and every kind has a diagnostic name.
void broadcasts() {
    std::vector<std::string> journal;
    Probe notes("notes", journal), area("area", journal), late("late", journal);
    notes.route.pointer = 9; area.route.pointer = 1; // routing priority does not order broadcasts
    app::ModuleRegistry registry(true);
    registry.add(notes); registry.add(area); registry.add(late);
    using K = app::HostEvent::Kind;
    check(!registry.broadcast({K::utilityCompleted, 0}, 1) &&
          journal == std::vector<std::string>{"event:notes", "event:area", "event:late"}, "Broadcasts follow registration order");
    area.artworkOnEvent = true;
    check(registry.broadcast({K::audioChanged, app::HostEvent::audioVolume | app::HostEvent::audioDevices}, 2), "Any owner's artwork change is reported");
    check(late.events.size() == 2 && late.events[1].kind == K::audioChanged && late.events[1].flags == (app::HostEvent::audioVolume | app::HostEvent::audioDevices),
          "Every owner receives the kind and its flags, even after an artwork change");
    std::vector<app::ModuleFailure> failures;
    registry.setFailureObserver([&](const app::ModuleFailure& f) { failures.push_back(f); });
    area.throwsOnEvent = true;
    journal.clear();
    check(!registry.broadcast({K::suspended, app::HostEvent::suspendSystem}, 3) && !registry.enabled(area) &&
          journal == std::vector<std::string>{"event:notes", "event:late"}, "A failing owner is isolated and the others still receive the broadcast");
    check(failures.size() == 1 && failures[0].owner == "area" && failures[0].operation == "suspend", "The failure names the broadcast, not the message");
    std::set<std::string_view> names;
    for (unsigned n = 0; n < static_cast<unsigned>(K::count); ++n) {
        const auto name = app::hostEventName(static_cast<K>(n));
        check(!name.empty() && name != "host event", "Every broadcast kind has a diagnostic name");
        names.insert(name);
    }
    check(names.size() == static_cast<std::size_t>(K::count), "Broadcast names are distinct");
    app::ModuleOwner& plain = late;
    app::ModuleCoverage step;
    plain.app::ModuleOwner::coverage(step); // the default coverage hook is inert
    check(!plain.app::ModuleOwner::hostEvent({K::quitting, 0}, 4), "The default broadcast handler reports no artwork change");
}

void routing() {
    std::vector<std::string> journal;
    Probe notes("notes", journal), settings("settings", journal), archive("archive", journal), shelf("shelf", journal);
    notes.route.pointer = 1; settings.route.pointer = 2; settings.route.pointerCapture = 1; archive.route.pointer = 3; shelf.route.pointer = 8;
    notes.route.key = 1; settings.route.key = 2;
    app::ModuleRegistry registry;
    // Registration order differs from routing order on purpose.
    registry.add(shelf); registry.add(archive); registry.add(notes); registry.add(settings);
    bool threw{};
    try { registry.add(notes); } catch (const std::logic_error&) { threw = true; }
    check(threw, "Duplicate registration is rejected");
    using C = app::ModuleRegistry::Chain;
    const app::PointerEvent e{app::PointerKind::move, app::PointerButton::none, 1, 1};
    auto* consumed = registry.first(C::pointer, [&](app::ModuleOwner& o, const app::ModuleRouting&) { return o.pointer(e, 0); });
    check(!consumed && journal == std::vector<std::string>{"notes", "settings", "archive", "shelf"}, "Pointer chain follows routing priority, not registration order");
    journal.clear();
    settings.captures = true; settings.consumes = true;
    consumed = registry.first(C::pointerCapture, [&](app::ModuleOwner& o, const app::ModuleRouting&) { return o.capturesPointer() && o.pointer(e, 0); });
    check(consumed == &settings && journal == std::vector<std::string>{"settings"}, "Capture chain runs only capturing owners and stops at the consumer");
    journal.clear();
    notes.consumes = true;
    consumed = registry.first(C::key, [&](app::ModuleOwner& o, const app::ModuleRouting&) { return o.key({}, 0); });
    check(consumed == &notes && journal == std::vector<std::string>{"notes"}, "Key chain stops at the first consumer");
    check(registry.first(C::wheel, [](app::ModuleOwner&, const app::ModuleRouting&) { return true; }) == nullptr, "Owners without a wheel route are excluded");
    archive.module = core::Module::archive;
    check(registry.presenter(core::Module::archive) == &archive && registry.presenter(core::Module::map) == nullptr, "Presenter lookup");
    // Mutation during routing is rejected.
    threw = false;
    Probe late("late", journal);
    registry.forEach([&](app::ModuleOwner&) { if (!threw) { try { registry.add(late); } catch (const std::logic_error&) { threw = true; } } });
    check(threw && !registry.contains(late), "Registry cannot change while routing");
    // Release in reverse registration order.
    journal.clear();
    endfield::native::Renderer renderer; // never initialized: probes own no GPU resources
    registry.releaseAll(renderer);
    check((journal == std::vector<std::string>{"release:settings", "release:notes", "release:archive", "release:shelf"}), "GPU release is reverse registration order");
    registry.remove(archive);
    check(!registry.contains(archive) && registry.size() == 3, "Removal");
}

void isolation() {
    std::vector<std::string> journal;
    Probe notes("notes", journal), map("map", journal), clipboard("clipboard", journal);
    notes.essentialOwner = true; map.throwsOnUpdate = true; map.covering = true; map.module = core::Module::map;
    map.route.pointer = 1; clipboard.route.pointer = 2;
    std::vector<app::ModuleFailure> observed;
    app::ModuleRegistry registry(true);
    registry.setFailureObserver([&](const app::ModuleFailure& f) { observed.push_back(f); });
    registry.add(notes); registry.add(map); registry.add(clipboard);
    const core::Matrix4 center;
    const core::source::DesktopChromeSettings chrome{};
    const core::ModulePresentationSample presentation{};
    const app::ModuleFrame frame{center, chrome, presentation, 1, 0, true};
    registry.forEach([&](app::ModuleOwner& o) { o.update(frame); }, "frame");
    check(notes.updates == 1 && clipboard.updates == 1 && !registry.enabled(map), "A throwing owner is disabled; the others still update");
    check(observed.size() == 1 && observed[0].owner == "map" && observed[0].operation == "frame" &&
          observed[0].what.find("/private/path") == std::string::npos, "Failure report names the owner; the type carries no message");
    check(observed[0].message == "synthetic failure with <path>" && observed[0].stage == "frame [map]",
          "The report carries the message with its path redacted, and the call when no inner stage was recorded");
    registry.forEach([&](app::ModuleOwner& o) { o.update(frame); }, "frame");
    check(observed.size() == 1 && clipboard.updates == 2, "A disabled owner is skipped afterwards");
    check(!registry.covers({0, 0}) && registry.presenter(core::Module::map) == nullptr, "A disabled owner neither covers input nor presents content");
    journal.clear();
    const app::PointerEvent e{app::PointerKind::down, app::PointerButton::left, 1, 1};
    (void)registry.first(app::ModuleRegistry::Chain::pointer, [&](app::ModuleOwner& o, const app::ModuleRouting&) { return o.pointer(e, 0); });
    check(journal == std::vector<std::string>{"clipboard"}, "Input skips the disabled owner");
    endfield::native::Renderer renderer;
    registry.releaseAll(renderer);
    check(map.releases == 1, "A disabled owner still releases its GPU resources");
    // Essential owners are never isolated: their failure still ends the run.
    notes.throwsOnUpdate = true;
    bool threw{};
    try { registry.forEach([&](app::ModuleOwner& o) { o.update(frame); }, "frame"); } catch (const std::out_of_range&) { threw = true; }
    check(threw && registry.enabled(notes), "Essential owner failures propagate");
    // Without isolation (development previews) every failure propagates.
    app::ModuleRegistry strict;
    Probe failing("failing", journal);
    failing.throwsOnUpdate = true;
    strict.add(failing);
    threw = false;
    try { strict.forEach([&](app::ModuleOwner& o) { o.update(frame); }); } catch (const std::out_of_range&) { threw = true; }
    check(threw && strict.enabled(failing), "Development registries keep failures fatal");
}

// A disabled owner is never silent (Windows 1.3.0: the whole Settings group
// went blank with no trace): the report carries the exception's static
// message and the stage it left, once per owner; production appends it to a
// bounded Diagnostics/module-failures.txt that a missing folder or a write
// error never turns into a second failure.
struct Thrower final : app::ModuleOwner {
    std::string id, message; bool inner{}, foreign{};
    unsigned calls{};
    Thrower(std::string name, std::string text, bool stage) : id(std::move(name)), message(std::move(text)), inner(stage) {}
    std::string_view name() const noexcept override { return id; }
    void update(const app::ModuleFrame&) override {
        ++calls;
        if (foreign) throw 7;
        if (!inner) throw std::invalid_argument(message);
        const app::FatalStage scene("scene sync");
        throw std::invalid_argument(message);
    }
    bool hostEvent(const app::HostEvent&, double) override { ++calls; throw std::runtime_error("second failure"); }
};
std::string text(const std::filesystem::path& path) { return ehud::data::detail::readFile(path, 64 * 1024).value_or(std::string()); }
// A new temporary folder, removed however the test leaves.
struct TemporaryFolder {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("ehud-module-failures-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryFolder() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    ~TemporaryFolder() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;
};
void failureRecords() {
    check(app::redacted("Cannot open C:\\Users\\someone\\notes.txt now") == "Cannot open <path> now" && app::redacted("bad\x01\xe6\x96\x87 /x") == "bad???? <path>",
          "redacted() removes path-like words and non-ASCII bytes");
    check(app::failureMessage("unsupported text language: zh-Hans-x-private") == "unsupported text language" &&
          app::failureMessage(std::string(400, 'a')).size() == 160 && app::failureMessage("Cannot read /Users/someone/file.txt") == "Cannot read <path>",
          "Only the static prefix of a message is kept, bounded");
    Thrower settings("settings", "Retained layer resource budget exceeded", true), area("area", "unsupported text language: zh-Hans-x /Users/someone", false), odd("odd", "", false);
    odd.foreign = true;
    std::vector<app::ModuleFailure> observed;
    app::ModuleRegistry registry(true);
    registry.setFailureObserver([&](const app::ModuleFailure& f) { observed.push_back(f); });
    registry.add(settings); registry.add(area); registry.add(odd);
    const core::Matrix4 center;
    const core::source::DesktopChromeSettings chrome{};
    const core::ModulePresentationSample presentation{};
    const app::ModuleFrame frame{center, chrome, presentation, 1, 0, true};
    registry.forEach([&](app::ModuleOwner& o) { o.update(frame); }, "frame");
    check(observed.size() == 3 && !registry.enabled(settings) && !registry.enabled(area) && !registry.enabled(odd), "Every throwing owner is disabled and reported");
    check(observed[0].owner == "settings" && observed[0].what.find("invalid_argument") != std::string::npos && observed[0].operation == "frame" &&
          observed[0].message == "Retained layer resource budget exceeded" && observed[0].stage == "frame [settings] > scene sync",
          "The report carries the message and the stage the exception left");
    check(observed[1].message == "unsupported text language" && observed[1].stage == "frame [area]", "A dynamic suffix (tags, paths) never reaches the report");
    check(observed[2].what == "unknown" && observed[2].message.empty() && observed[2].stage == "frame [odd]", "A non-standard exception is reported without a message");
    check(app::FatalStage::last().empty(), "The handled failure leaves no fatal stage behind");
    const auto calls = settings.calls;
    check(!registry.guard(settings, [&] { (void)settings.hostEvent({}, 1); }, "message") && settings.calls == calls, "A disabled owner is not called again");
    registry.forEach([&](app::ModuleOwner& o) { (void)o.hostEvent({}, 1); }, "event");
    check(observed.size() == 3 && registry.failures().size() == 3, "One report per owner and run");

    const TemporaryFolder folder;
    const auto& root = folder.root;
    // The host's budget lines: the raster cache's held bytes (its budget's
    // measure, released pixels excluded) and released bytes, the renderer's
    // resident and parked bytes, each "N of M" (a slash would be redacted).
    const auto budgets = app::budgetLines({900, 4096, 200000000, 268435456, 68000000, 300000000, 536870912, 12000000, 268435456});
    check(budgets == "raster: entries 900 of 4096, held bytes 200000000 of 268435456, released bytes 68000000\nrenderer: bytes 300000000 of 536870912, parked bytes 12000000 of 268435456\n",
          "The budget lines name held, released and parked bytes against their bounds");
    // The Diagnostics folder does not exist yet: it is created.
    const auto file = root / "v1" / "Diagnostics" / "module-failures.txt";
    app::ModuleFailureLog log(file);
    check(log.record(observed[0], "density: 2.5\n" + budgets + "about: main, update current\n"), "A record is written into a missing folder");
    check(!log.record(observed[0], "density: 2.5\n"), "One record per owner and run");
    const auto first = text(file);
    check(first.starts_with(app::ModuleFailureLog::header) && first.find("utc: ") != std::string::npos && first.find("owner: settings\n") != std::string::npos &&
          first.find("message: Retained layer resource budget exceeded\n") != std::string::npos && first.find("operation: frame\n") != std::string::npos &&
          first.find("stage: frame [settings] > scene sync\n") != std::string::npos && first.find("raster: entries 900 of 4096, held bytes 200000000 of 268435456, released bytes 68000000\n") != std::string::npos &&
          first.find("renderer: bytes 300000000 of 536870912, parked bytes 12000000 of 268435456\n") != std::string::npos &&
          first.find("about: main, update current\n") != std::string::npos, "The record names the owner, exception, message, operation, stage and the host's numbers");
    // Paths in a message or in the context lines are redacted in the file.
    app::ModuleFailure leaked{"leaky", "class std::runtime_error", "frame", "Cannot open C:\\Users\\someone\\notes.txt", "frame [leaky]"};
    check(log.record(leaked, "folder: /Users/someone/Documents\n") && text(file).find("someone") == std::string::npos && text(file).find("message: Cannot open <path>\n") != std::string::npos,
          "Paths never reach the file");
    // A message redaction lengthened (each "a/b" becomes "<path>") is kept whole, never cut inside a marker.
    std::string paths;
    for (unsigned n = 0; n < 40; ++n) paths += "a/b ";
    app::ModuleFailure lengthened{"lengthened", "class std::runtime_error", "frame", app::failureMessage(paths), "frame [lengthened]"};
    check(lengthened.message.size() > 160 && log.record(lengthened) && text(file).find("message: " + lengthened.message + "\n") != std::string::npos,
          "A redacted message is recorded whole");
    // A module whose isolated startup threw (Application's production start
    // sites): reported and recorded like a disabled owner, operation "start".
    const auto started = [](std::string_view owner, auto&& build) {
        const app::FatalStage starting("start", owner);
        try { build(); } catch (const std::exception& e) { return app::startFailure(owner, e); }
        return app::ModuleFailure{};
    };
    const auto settingsStart = started("settings", [] { const app::FatalStage inner("scene sync"); throw std::invalid_argument("Retained layer resource budget exceeded: 268435457"); });
    check(settingsStart.owner == "settings" && settingsStart.operation == "start" && settingsStart.what.find("invalid_argument") != std::string::npos &&
          settingsStart.message == "Retained layer resource budget exceeded" && settingsStart.stage == "start [settings] > scene sync" && app::FatalStage::last().empty(),
          "A startup failure carries its message and the stage it left");
    check(app::failureLine(settingsStart) == "EndfieldHUD module settings could not start (" + settingsStart.what + "): Retained layer resource budget exceeded (start [settings] > scene sync)\n" &&
          app::failureLine(observed[0]) == "EndfieldHUD module settings was disabled after a " + observed[0].what + " during frame: Retained layer resource budget exceeded (frame [settings] > scene sync)\n",
          "The stderr line names a start failure and a disable");
    const auto areaStart = started("battery", [] { throw std::runtime_error("Cannot open C:\\Users\\someone\\area.dll"); });
    check(areaStart.stage == "start [battery]" && areaStart.message == "Cannot open <path>" && app::failureLine(areaStart).find("someone") == std::string::npos,
          "A startup failure without an inner stage names its start");
    check(log.record(areaStart, "density: 1.8\n") && text(file).find("owner: battery\nexception: " + app::redacted(areaStart.what) + "\nmessage: Cannot open <path>\noperation: start\nstage: start [battery]\n") != std::string::npos,
          "A module that never started leaves a record");
    // Bounded to 16 KiB: the oldest records go first, whole.
    for (unsigned n = 0; n < 60; ++n) {
        app::ModuleFailure f{"owner" + std::to_string(n), "class std::invalid_argument", "upload", "Design artwork still published", "upload [owner" + std::to_string(n) + "]"};
        (void)log.record(f, "padding: " + std::string(200, 'x') + "\n");
    }
    const auto bounded = text(file);
    check(bounded.size() <= app::ModuleFailureLog::maximumBytes && bounded.size() > app::ModuleFailureLog::maximumBytes / 2 && bounded.starts_with(app::ModuleFailureLog::header) &&
          bounded.find("owner: owner59\n") != std::string::npos && bounded.find("owner: settings\n") == std::string::npos, "The file keeps the newest records within 16 KiB");
    check(!app::appendBoundedRecord(file, std::string(app::ModuleFailureLog::maximumBytes, 'y'), app::ModuleFailureLog::header, 64), "A file over the bound is never rewritten");
    // A folder that cannot exist (its parent is a file) fails quietly.
    app::ModuleFailureLog blocked(file / "module-failures.txt");
    bool threw{};
    try { check(!blocked.record(observed[1]), "An impossible folder reports no record"); } catch (const std::exception&) { threw = true; }
    check(!threw, "A failed write never throws");
}

// The ONE host time (ModuleRegistry::hostTime / HostClock). Owners need()
// non-decreasing times, like the scenes that ended the live RCs. Reproduced
// orders: (1) Projection - the host samples t1, the owner's pointer call
// releases its capture and the nested WM_CAPTURECHANGED is handed a later
// t2, then the host's refresh asks frame demand with its earlier sample;
// (2) the deadline/Event Log order - an earlier owner's deadline dispatches
// a later event, then later owners and the host's own calls follow with the
// wake sample.
struct Monotonic final : app::ModuleOwner {
    std::string id; app::ModuleRouting route; mutable double latest{-1}; mutable unsigned calls{};
    std::function<void(double)> nested;
    explicit Monotonic(std::string name) : id(std::move(name)) {}
    void see(double t) const { ++calls; if (!std::isfinite(t) || t < latest) throw std::logic_error(id + " requires a finite monotonic owner clock"); latest = t; }
    std::string_view name() const noexcept override { return id; }
    app::ModuleRouting routing() const noexcept override { return route; }
    bool pointer(const app::PointerEvent&, double t) override { see(t); if (auto f = std::exchange(nested, {})) f(t); return true; }
    bool requiresFrames(double t) const override { see(t); return false; }
    std::optional<double> nextWakeTime(double t) const override { see(t); return std::nullopt; }
    bool deadline(double t) override { see(t); if (auto f = std::exchange(nested, {})) f(t); return false; }
    bool hostEvent(const app::HostEvent&, double t) override { see(t); return false; }
};
void hostClock() {
    app::HostClock clock;
    check(!clock.latest() && clock(std::numeric_limits<double>::quiet_NaN()) == 0, "No hand-off yet: a non-finite sample yields 0");
    check(clock(5) == 5 && clock(4) == 5 && clock(7) == 7 && clock(std::numeric_limits<double>::infinity()) == 7 && clock.latest() == 7.,
          "An earlier or non-finite sample is clamped to the latest hand-off");
    using C = app::ModuleRegistry::Chain;
    using K = app::HostEvent::Kind;
    // Development registry: an owner's need() failure propagates, as in hidden coverage.
    app::ModuleRegistry registry;
    Monotonic projection("projection"), later("later");
    projection.route.pointer = 1;
    registry.add(projection); registry.add(later);
    // (1) The Projection order.
    const double sampled = registry.hostTime(10);
    projection.nested = [&](double) {
        (void)registry.first(C::pointer, [&](app::ModuleOwner& o, const app::ModuleRouting&) {
            return o.pointer({app::PointerKind::captureLost}, registry.hostTime(10.004)); }, "pointer"); };
    (void)registry.first(C::pointer, [&](app::ModuleOwner& o, const app::ModuleRouting&) {
        return o.pointer({app::PointerKind::up, app::PointerButton::left}, registry.hostTime(sampled)); }, "pointer");
    bool threw{};
    try { (void)registry.requiresFrames(sampled); (void)registry.nextWakeTime(sampled); } catch (const std::logic_error&) { threw = true; }
    check(!threw && projection.latest == 10.004 && later.latest == 10.004, "Frame demand after a nested capture-lost receives its later time, not the earlier sample");
    threw = false;
    try { (void)projection.requiresFrames(sampled); } catch (const std::logic_error&) { threw = true; }
    check(threw, "The earlier sample handed directly is the live failure the clamp prevents");
    // (2) The deadline order.
    later.nested = [&](double t) { (void)registry.broadcast({K::utilityCompleted, 0}, t + .5); };
    threw = false;
    try {
        registry.forEach([&](app::ModuleOwner& o) { (void)o.deadline(registry.hostTime(11)); }, "deadline");
        (void)registry.broadcast({K::settingsChanged, 0}, 11);
        (void)registry.requiresFrames(11);
    } catch (const std::logic_error&) { threw = true; }
    check(!threw && projection.latest == 11.5 && later.latest == 11.5 && registry.hostTime(11) == 11.5,
          "Calls after an owner's nested later event take the later host time");
    // A finite later sample moves the host time on.
    check(registry.hostTime(12) == 12 && registry.latestHostTime() == 12., "A later sample advances the host time");
    registry.remove(projection); registry.remove(later);
}
} // namespace

int main() {
    try {
        routing();
        isolation();
        broadcasts();
        failureRecords();
        hostClock();
        std::cout << "Module registry: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Module registry failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
