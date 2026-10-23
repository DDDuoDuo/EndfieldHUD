// Contracts of the redesigned Clipboard, Event Log and Volume canvases
// (modules/clipboard_canvas.*, event_log_canvas.*, volume_canvas.*): the rules
// of Sources/ClipboardCanvas.swift, EventLogCanvas.swift and VolumeCanvas.swift
// (each check cites the Swift line it ports) over synthetic stores, and, when
// the Mac oracle is given (tests/fixtures/utility-canvases-source.json from
// tools/utility_canvases_reference.sh), every recorded scene node by node with
// the Mac's own text widths, its controls and accessibility items, plus the
// oracle's Event Log sentences in five languages, local times, metadata
// allowlist and the Clipboard meta-line formats. Synthetic data only.
#include "modules/clipboard_canvas.hpp"
#include "modules/event_log_canvas.hpp"
#include "modules/volume_canvas.hpp"
#include "tests/hud_design_oracle.hpp"
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

namespace d = endfield::modules::design;
namespace m = endfield::modules;
namespace n = endfield::native;
using endfield::core::Language;
using namespace endfield::tests::design_oracle;
namespace {
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
d::KeyInput keyOf(std::uint32_t vk, char32_t c = 0, bool shift = false, bool control = false) {
    d::KeyInput k; k.virtualKey = vk; k.character = c; k.shift = shift; k.control = control; return k;
}
d::Control* control(d::Canvas& canvas, std::string_view id) {
    for (d::Control* c : canvas.page().controls()) if (c->id() == id) return c;
    return nullptr;
}
bool hasText(const d::Layer& layer, std::string_view text) {
    if (const auto* block = layer.text(); block && block->text() == text) return true;
    for (const auto* child : layer.children()) if (hasText(*child, text)) return true;
    return false;
}
d::Point centre(const d::Control& c, const d::Layer& root) { const auto r = c.rect(root); return {r.x + r.width / 2, r.y + r.height / 2}; }

// A Context with a measurer (the deterministic .55 em one unless the oracle's).
struct World {
    d::TextMeasurer& measurer;
    d::Context context;
    double now{100};
    explicit World(d::TextMeasurer& m) : measurer(m), context(m, d::asciiUppercase) { context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); }
    void appearance(Language language, bool light = false, bool reduce = true, bool recording = false) {
        d::Context::Appearance a; a.language = language; a.light = light; a.reduceMotion = reduce; a.recordingMode = recording;
        context.apply(a, now);
    }
    void advance(double seconds) { now += seconds; context.advance(now); }
    void show(d::Canvas& c) { c.show(now); c.setActive(true, now); }
    std::vector<std::string> toasts() const {
        std::vector<std::string> out;
        if (const auto& t = const_cast<d::Context&>(context).overlay().toastState().current()) out.push_back(t->title + "|" + t->caption);
        return out;
    }
};

// --- Clipboard ---------------------------------------------------------------------------
struct ClipboardModel {
    m::ClipboardSnapshot state;
    std::vector<std::uint64_t> copies;
    bool failCopy{};
    m::ClipboardActions actions() {
        m::ClipboardActions a;
        a.snapshot = [this] { return state; };
        a.copy = [this](std::uint64_t id) { copies.push_back(id); return !failCopy; };
        a.togglePin = [this](std::uint64_t id) { for (auto& r : state.items) if (r.id == id) { r.pinned = !r.pinned; return true; } return false; };
        a.remove = [this](std::uint64_t id) { return std::erase_if(state.items, [&](const auto& r) { return r.id == id; }) > 0; };
        a.clearUnpinned = [this] { return std::erase_if(state.items, [](const auto& r) { return !r.pinned; }) > 0; };
        return a;
    }
    static m::ClipboardRecord text(std::uint64_t id, std::string preview, bool pinned = false, double created = 1000) {
        m::ClipboardRecord r; r.id = id; r.preview = std::move(preview); r.pinned = pinned; r.createdAt = created; return r;
    }
};
void clipboardFormats() {
    using C = m::ClipboardCanvas;
    // age (:339-347).
    check(C::age(1000, 1059.9, Language::english) == "NOW" && C::age(1000, 1060, Language::english) == "1 MIN" && C::age(1000, 1000 + 3599, Language::english) == "59 MIN"
        && C::age(1000, 1000 + 3600, Language::english) == "1 H" && C::age(1000, 1000 + 86400 * 3, Language::english) == "3 D" && C::age(1000, 900, Language::english) == "NOW",
        "Ages: NOW, n MIN, n H, n D; a future capture is NOW (:339-347)");
    check(C::age(1000, 1120, Language::simplifiedChinese) == "2 分钟", "Ages are localised");
    // meta (:319-336): parts joined by three spaces, empty parts dropped.
    m::ClipboardRecord r = ClipboardModel::text(1, "Some text"); r.details.source = "Notes"; r.details.characters = 28;
    check(C::meta(r, Language::english) == "Notes   28 chars", "Text meta: source + characters");
    r.details.source.reset(); check(C::meta(r, Language::english) == "28 chars", "A missing source leaves no gap");
    r.kind = m::ClipboardRecordKind::url; r.preview = " https://user@example.com:8080/a?b "; r.details = {};
    check(C::meta(r, Language::english) == "example.com", "A link without source shows the URL's host (:324)");
    r.details.source = "Safari"; check(C::meta(r, Language::english) == "Safari", "A link with source shows the source");
    r.preview = "mailto:someone@example.com"; r.details = {}; check(C::meta(r, Language::english).empty(), "No host: no meta");
    r.kind = m::ClipboardRecordKind::image; r.details.format = "PNG"; r.details.pixelWidth = 2880; r.details.pixelHeight = 1872;
    check(C::meta(r, Language::english) == "PNG   2880 × 1872" && C::recordTitle(r, Language::english) == "Image" && C::recordTitle(r, Language::simplifiedChinese) == "图片",
        "Image meta and title (:315, :325-327)");
    r.kind = m::ClipboardRecordKind::files; r.details = {}; r.details.source = "Finder"; r.details.fileCount = 1; r.details.byteCount = 48'200'000;
    check(C::meta(r, Language::english) == "Finder   48.2 MB", "One file: its size");
    r.details.fileCount = 3; check(C::meta(r, Language::english) == "Finder   3 items", "Several files: the count");
    r.details.fileCount = 1; r.details.byteCount.reset(); check(C::meta(r, Language::english) == "Finder   Folder", "No size: Folder");
    for (const auto& [bytes, text] : std::vector<std::pair<std::int64_t, std::string>>{{0, "Zero KB"}, {1, "1 byte"}, {999, "999 bytes"}, {1000, "1 KB"},
            {1500, "2 KB"}, {2500, "3 KB"}, {999'500, "1 MB"}, {1'050'000, "1.1 MB"}, {999'950'000, "1 GB"}, {1'234'567'890, "1.23 GB"}, {5'555'555'555'555, "5.56 TB"}})
        check(C::fileSize(bytes) == text, "ByteCountFormatter .file: " + text);
}
void clipboardCanvas() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    ClipboardModel model;
    for (std::uint64_t id = 1; id <= 8; ++id) model.state.items.push_back(ClipboardModel::text(id, "Item " + std::to_string(id), id == 3, 1000));
    model.state.capacity = 10;
    double wall = 1000 + 125;
    m::ClipboardCanvas canvas(w.context, model.actions(), [&] { return wall; });
    w.show(canvas);
    // Header (:126-131), order (:91), list geometry (:145-185).
    check(canvas.header().counterText() == "08/10" && canvas.header().crumbTexts().back() == "CLIPBOARD", "Counter pad2(records)/pad2(capacity)");
    const auto titles = canvas.cardTitlesForTesting();
    check(titles.size() == 8 && titles[0] == "Copy: Item 3" && titles[1] == "Copy: Item 1" && titles[2] == "Copy: Item 2", "Pinned records lead, then capture order (:91)");
    const auto* card0 = control(canvas, "clip:0");
    const auto* card1 = control(canvas, "clip:1");
    check(card0 && card1 && card1->layer().frame().y - card0->layer().frame().y == 64 && card0->layer().frame().width == 590 - 6 - 4,
        "Cards every 64 px, 590 - 6 - 4 wide while the list scrolls");
    auto* pin0 = dynamic_cast<d::IconButton*>(control(canvas, "clip:0:pin"));
    auto* delete0 = control(canvas, "clip:0:delete");
    check(pin0 && pin0->isOn() && pin0->label == "Unpin: Item 3" && delete0 && !delete0->enabled() && control(canvas, "clip:1:delete")->enabled(),
        "Pin lit and delete disabled on a pinned record (:162-170)");
    auto* clear = dynamic_cast<d::Button*>(canvas.header().controls().back());
    check(clear && clear->enabled(), "Clear is enabled while an unpinned record exists (:128)");
    // Keys (:247-283).
    check(canvas.keyDown(keyOf(d::key::down), w.now) && canvas.focusedID() == 3u && canvas.focusedCardIndexForTesting() == 0u, "Down focuses the first record");
    check(canvas.keyDown(keyOf(d::key::down), w.now) && canvas.focusedID() == 1u, "Down moves through the list");
    check(!canvas.keyDown(keyOf(d::key::down, 0, true), w.now) && canvas.focusedID() == 1u, "Shift-Down is not the canvas's");
    check(canvas.keyDown(keyOf(d::key::enter), w.now) && model.copies == std::vector<std::uint64_t>{1}, "Return copies the focused record");
    check(!w.toasts().empty() && w.toasts()[0] == "Copied|", "A copy toasts Copied, English caption empty (:206)");
    // Delete (:214-223): the focus moves to the record now at that index.
    check(canvas.keyDown(keyOf(d::key::back), w.now) && canvas.focusedID() == 2u && model.state.items.size() == 7, "Delete removes it and focuses the next");
    // A card built while focused (:176-178): frame.origin is set after the
    // 3 px lift, so it shows at x 0 and sits 3 px left once the focus leaves.
    const auto* rebuilt = control(canvas, "clip:" + std::to_string(canvas.focusedCardIndexForTesting().value_or(99)));
    check(rebuilt && rebuilt->layer().frame().x == -3 && rebuilt->layer().value(d::Channel::translateX) == 3, "A card built focused shows unlifted");
    canvas.moveFocus(-10);
    check(rebuilt->layer().frame().x == -3 && rebuilt->layer().value(d::Channel::translateX) == 0, "Losing the focus leaves it 3 px left");
    check(canvas.focusedID() == 3u && canvas.keyDown(keyOf(d::key::deleteForward), w.now) && model.state.items.size() == 7, "A pinned record is never deleted one by one (:216)");
    canvas.remove(3);
    check(model.state.items.size() == 7, "remove() refuses a pinned record");
    // Digits copy the cards in view (:271-278).
    model.copies.clear();
    check(canvas.keyDown(keyOf('2', U'2'), w.now) && model.copies == std::vector<std::uint64_t>{2}, "2 copies the second card in view");
    check(!canvas.keyDown(keyOf('7', U'7'), w.now), "7 is not a copy key");
    // A copy failure shows the store status or the fallback (:207-208).
    model.failCopy = true; canvas.copy(4); model.failCopy = false;
    check(w.toasts()[0] == "Notice|Could not restore this item", "A failed copy toasts the fallback notice");
    // A new status while active toasts once (:114-117), not during a copy.
    model.state.status = "The cache is full of pinned items."; canvas.storeChanged();
    check(w.toasts()[0] == "Notice|The cache is full of pinned items.", "A new store status toasts while active");
    w.context.overlay().reset(); canvas.storeChanged();
    check(w.toasts().empty(), "The same status does not toast again");
    // Clear asks first (:225-235).
    clear->activate();
    const auto& request = w.context.overlay().confirmState().request();
    check(request && request->title == "Clear clipboard history?" && request->message == "6 records / 1 pinned kept" && request->danger && request->confirmTitle == "Clear",
        "Clear asks with the record counts");
    (void)w.context.overlay().handleKey(keyOf(d::key::enter)); w.advance(.2);
    check(model.state.items.size() == 1 && model.state.items[0].pinned && w.toasts()[0] == "History cleared|", "Confirming clears the unpinned records");
    check(!dynamic_cast<d::Button*>(canvas.header().controls().back())->enabled(), "Only pinned records left: Clear is disabled");
    // Empty filter (:153-157).
    canvas.selectFilter(m::ClipboardCanvas::Filter::image);
    check(canvas.cardTitlesForTesting().empty() && canvas.filter() == m::ClipboardCanvas::Filter::image, "A filter with no record shows the empty state");
    canvas.selectFilter(m::ClipboardCanvas::Filter::all);
    // Right-click menu (:285-302).
    const auto* only = control(canvas, "clip:0");
    check(canvas.rightPointerDown(centre(*only, canvas.root()), w.now) && canvas.menuOpen(), "Right-click on a card opens its menu");
    canvas.closeMenu();
    // Ages refresh every 30 s while active (:65-76).
    wall += 3600; w.advance(30);
    check(hasText(canvas.root(), "1 H"), "The age timer refreshes ages in place");
    canvas.setActive(false, w.now);
    check(!w.context.nextWakeTime(), "Deactivation cancels the age timer (no wake-up while hidden)");
    // A pin while shown slides the moved cards (:107-112).
    model.state.items.push_back(ClipboardModel::text(20, "Twenty")); model.state.items.push_back(ClipboardModel::text(21, "Twenty one"));
    canvas.storeChanged(); w.show(canvas);
    w.appearance(Language::english, false, false);
    canvas.togglePin(21);
    const auto* moved = control(canvas, "clip:1");
    check(moved && moved->layer().animating(w.now), "Pinning slides the moved cards to their rows (0.3 s)");
}

// --- Event Log ---------------------------------------------------------------------------
std::optional<m::EventLogCivilTime> utc(double reference) {
    if (!std::isfinite(reference)) return std::nullopt;
    const auto seconds = std::int64_t(std::floor(reference + 978307200.0));
    std::int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
    const auto rest = seconds - days * 86400;
    // civil_from_days (Howard Hinnant).
    days += 719468; const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097; const auto doe = days - era * 146097;
    const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const auto mp = (5 * doy + 2) / 153; const auto day = doy - (153 * mp + 2) / 5 + 1; const auto month = mp < 10 ? mp + 3 : mp - 9;
    return m::EventLogCivilTime{int(yoe + era * 400 + (month <= 2)), int(month), int(day), int(rest / 3600), int(rest % 3600 / 60)};
}
m::SystemEvent event(int index, m::EventKind kind, double created, m::EventMetadata metadata = {}) {
    char id[40]; std::snprintf(id, sizeof(id), "00000000-0000-0000-0000-%012d", index);
    return {id, kind, created, std::move(metadata)};
}
void eventLogText() {
    using K = m::EventKind;
    check(m::eventLogFilterOf(K::calendarAction) == m::EventLogFilter::other && m::eventLogFilterOf(K::minigameAction) == m::EventLogFilter::other
        && m::eventLogFilterOf(K::playbackAction) == m::EventLogFilter::other && m::eventLogFilterOf(K::displaySettingsChanged) == m::EventLogFilter::other
        && m::eventLogFilterOf(K::workReset) == m::EventLogFilter::timer && m::eventLogFilterOf(K::displayConnected) == m::EventLogFilter::devices
        && m::eventLogFilterOf(K::shelfCleared) == m::EventLogFilter::shelf, "Kinds map to Timer / Shelf / Devices / Other (:10-18)");
    check(m::eventLogFilterChip(m::EventLogFilter::timer) == d::Chip::Variant::yellow && m::eventLogFilterChip(m::EventLogFilter::devices) == d::Chip::Variant::cyan
        && m::eventLogFilterChip(m::EventLogFilter::other) == d::Chip::Variant::dark, "Chip colours (:33)");
    check(m::eventLogLine(event(1, K::workCompleted, 0, {{"kind", "countdown"}, {"seconds", "1800"}}), Language::english) == "Countdown finished, 30 min"
        && m::eventLogLine(event(1, K::workPaused, 0, {{"kind", "countdown"}, {"seconds", "95"}}), Language::simplifiedChinese) == "倒计时暂停，01:35"
        && m::eventLogLine(event(1, K::workStarted, 0, {{"kind", "stopwatch"}, {"seconds", "60"}}), Language::english) == "Stopwatch started",
        "Work sentences with the duration title (:45-79)");
    check(m::eventLogLine(event(1, K::displaySettingsChanged, 0, {{"field", "clockStyle"}, {"value", "rail"}}), Language::english) == "Clock style 04"
        && m::eventLogLine(event(1, K::displaySettingsChanged, 0, {{"field", "alertMetric"}, {"value", "gpu"}}), Language::english) == "Battery alert reading: GPU"
        && m::eventLogLine(event(1, K::playbackAction, 0, {{"action", "next"}, {"source", "spotify"}}), Language::english) == "Next track, Spotify",
        "Settings and playback sentences (:101-147)");
    const auto day = 812000000.0;
    check(m::eventLogTime(day, day + 60, utc) == m::EventLogTime{"03:33", std::nullopt} && m::eventLogTime(day - 86400, day, utc) == m::EventLogTime{"03:33", "09.24"},
        "Today HH:mm; another day adds MM.dd (:367-372)");
}
struct EventStore {
    m::EventLogSnapshot state; unsigned clears{};
    m::EventLogCallbacks callbacks() { return {[this] { return state; }, [this] { ++clears; state.events.clear(); }}; }
};
void eventLogCanvas() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    EventStore store;
    const double now = 812000000;
    for (int i = 0; i < 500; ++i) store.state.events.push_back(event(i + 1, i % 3 ? m::EventKind::shelfAdded : m::EventKind::workStarted, now - i * 60, {{"filename", "F.pdf"}}));
    store.state.events.insert(store.state.events.begin(), event(900, m::EventKind::overlayOpened, now));
    store.state.status = "Saved log could not be read; the original file is preserved.";
    m::EventLogCanvas canvas(w.context, store.callbacks(), utc, [&] { return now; });
    w.show(canvas);
    check(canvas.header().counterText() == "500" && canvas.events().size() == 500, "The counter lists every record but navigation (:239, :288)");
    check(w.toasts().empty(), "A failure present at activation is not shown (owner decision: copy the Mac, MODS-716)");
    check(canvas.rowCountForTesting() == 9, "Only the rows in view plus one either side exist (:322-339)");
    canvas.scrollListForTesting(46 * 100);
    check(canvas.listOffsetForTesting() == 4600 && canvas.rowCountForTesting() == 10, "Scrolling rebuilds the window of rows");
    check(canvas.keyDown(keyOf(d::key::pageDown), w.now) && canvas.listOffsetForTesting() == 4920, "Page Down scrolls 320 (:417)");
    check(canvas.keyDown(keyOf(d::key::end), w.now) && canvas.listOffsetForTesting() == 500 * 46 - 6 - 350, "End jumps to the bottom");
    check(canvas.keyDown(keyOf(d::key::home), w.now) && canvas.listOffsetForTesting() == 0, "Home to the top");
    check(!canvas.keyDown(keyOf(d::key::down, 0, true), w.now), "Shift keys are not the list's");
    check(canvas.scroll({300, 300}, {0, 46}, w.now) && canvas.listOffsetForTesting() == 46 && !canvas.scroll({300, 100}, {0, 46}, w.now), "The wheel scrolls only over the list (:397-401)");
    // The top visible row stays in place when records arrive (:269-319).
    store.state.events.insert(store.state.events.begin() + 1, event(901, m::EventKind::shelfRemoved, now + 1));
    store.state.status = "The log could not be saved. New events are kept in this session.";
    canvas.storeChanged();
    check(canvas.listOffsetForTesting() == 92 && canvas.header().counterText() == "501", "A new record keeps the top row in view");
    check(w.toasts()[0] == "Event log|The log could not be saved. New events are kept in this session.", "A failure that appears while open toasts (:262-267)");
    // Rows: label and readout (:341-364).
    const auto* row = control(canvas, "eventLog.row:00000000-0000-0000-0000-000000000003");
    check(row && row->label == "03:31, Shelf" && dynamic_cast<const d::RowPlate*>(row)->readout == "Added F.pdf" && row->layer().frame().height == 40,
        "A row reads its time, filter and sentence");
    // Filters (:376-380): offset resets.
    static_cast<d::Tabs*>(canvas.page().tabs)->select(1, true);
    check(canvas.filter() == m::EventLogFilter::timer && canvas.listOffsetForTesting() == 0 && canvas.shown().size() == 167, "Timer lists the work records");
    // Clear (:382-394).
    auto* clear = canvas.header().controls().back();
    clear->activate();
    check(w.context.overlay().confirmState().request()->message == "501 entries", "Clear asks with the listed count");
    (void)w.context.overlay().handleKey(keyOf(d::key::enter)); w.advance(.2);
    check(store.clears == 1 && canvas.header().counterText() == "00" && w.toasts()[0] == "Log cleared|" && !canvas.header().controls().back()->enabled(),
        "Confirming clears the store; Clear is disabled on an empty log");
    canvas.setActive(false, w.now);
    store.state.status = "This log uses a newer format; the original file is preserved.";
    canvas.storeChanged(); w.context.overlay().reset();
    check(w.toasts().empty(), "The store is observed only while active");
}

// --- Volume --------------------------------------------------------------------------------
struct VolumeModel {
    n::VolumeSnapshot state;
    std::vector<std::string> writes;
    m::VolumeCanvas* canvas{};
    n::VolumeCallbacks callbacks() {
        n::VolumeCallbacks c;
        c.setActive = [this](bool v) { writes.push_back(v ? "start" : "stop"); };
        c.setVolume = [this](std::string_view id, double v) { writes.push_back("volume " + std::string(id) + " " + std::to_string(int(std::round(v * 100)))); state.volume = v; return true; };
        c.setBalance = [this](std::string_view id, double v) { writes.push_back("balance " + std::string(id) + " " + std::to_string(int(std::round(v * 100)))); return true; };
        c.setMute = [this](std::string_view id, bool v) { writes.push_back("mute " + std::string(id) + (v ? " on" : " off")); state.muted = v; if (canvas) canvas->receive(state); return true; };
        c.setAppGain = [this](std::string_view id, double v) { writes.push_back("gain " + std::string(id) + " " + std::to_string(int(std::round(v * 100)))); return true; };
        c.stopApp = [this](std::string_view id) { writes.push_back("stop " + std::string(id)); return true; };
        c.stopAllApps = [this] { writes.push_back("stopAll"); };
        c.setDefaultOutput = [this](std::string_view id) { writes.push_back("output " + std::string(id)); return true; };
        return c;
    }
    static n::VolumeSnapshot initial() {
        n::VolumeSnapshot s;
        s.outputs = {{"spk", "Speakers"}, {"hp", "Headphones", true, false, false}}; s.inputs = {{"mic", "Microphone"}};
        s.outputID = "spk"; s.inputID = "mic"; s.volume = .55; s.balance = -.2; s.muted = false;
        s.canSetVolume = s.canSetMute = s.canSetBalance = s.canSetDefaultOutput = s.canSetDefaultInput = s.applicationActivitySupported = true;
        for (int i = 1; i <= 5; ++i) { n::VolumeApplication a; a.id = std::to_string(900 + i); a.name = "Player " + std::to_string(i); a.pid = 4100u + unsigned(i); a.available = i != 5; s.applications.push_back(a); }
        return s;
    }
};
void volumeCanvas() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    VolumeModel model; model.state = VolumeModel::initial();
    std::vector<std::vector<std::string>> iconRequests;
    m::VolumeIconHooks icons;
    icons.show = [&](std::span<const std::string> ids) { iconRequests.emplace_back(ids.begin(), ids.end()); };
    icons.icon = [](std::string_view id) -> std::optional<d::ImageSource> {
        if (id != "901") return std::nullopt;
        return d::ImageSource{d::ImageSource::Kind::memory, "icon-901", {}, 1};
    };
    m::VolumeCanvas canvas(w.context, model.state, model.callbacks(), icons);
    model.canvas = &canvas;
    w.show(canvas);
    check(model.writes == std::vector<std::string>{"start"}, "Activation starts the device controller (:61-65)");
    check(canvas.header().counterText() == "55" && canvas.hasAdjustableApplication(), "Counter pad2(volume); the dot rule (:151-158)");
    // Devices rows (:195-205): y 125, 175, 225, 275.
    for (const auto& [id, y] : std::vector<std::pair<std::string, double>>{{"output", 125}, {"input", 175}, {"volume", 225}, {"balance", 275}}) {
        const auto* c = control(canvas, id);
        check(c && std::abs(c->rect(canvas.root()).y - y) < 30, "Device row " + id + " at its slot");
    }
    // The row plates are released once built (:188-206, :251): hovering a
    // row records its target's highlight but the row neither lifts nor brightens.
    {
        auto* volumeSlider = control(canvas, "volume");
        const d::Layer* plate = volumeSlider->layer().parent();
        canvas.pointerMoved(centre(*volumeSlider, canvas.root()), false, w.now);
        bool recorded = false;
        for (const auto& target : plate->targets()) recorded = recorded || target->highlighted;
        check(volumeSlider->hovered() && recorded && plate->value(d::Channel::translateX) == 0, "A Volume row records its hover but does not show it");
        canvas.pointerMoved(std::nullopt, false, w.now);
    }
    // Mute (:163-173).
    control(canvas, "mute")->activate();
    check(model.writes.back() == "mute spk on" && canvas.header().counterText() == "MUTED" && control(canvas, "mute")->label == "Unmute", "Mute toggles and the counter reads MUTED");
    // Keys (:422-431): the last slider touched, else Volume, 2 steps.
    check(canvas.keyDown(keyOf(d::key::right), w.now) && model.writes.back() == "volume spk 57", "Right nudges the volume by 2 steps");
    check(!canvas.keyDown(keyOf(d::key::right, 0, true), w.now), "Shift-Right is not the canvas's");
    // A drag keeps its device (:103-121): an output change cancels it.
    auto* slider = dynamic_cast<d::Slider*>(control(canvas, "balance"));
    const auto at = centre(*slider, canvas.root());
    check(canvas.pointerDown(at, {}, w.now) && slider->dragging(), "A press drags the balance");
    model.state.outputID = "hp"; canvas.receive(model.state);
    check(!slider->dragging(), "A new default output cancels the drag");
    canvas.pointerUp(at, w.now);
    model.state.outputID = "spk"; canvas.receive(model.state);
    // The Output menu (:230-244): every device, the ones that cannot become
    // the default disabled; a different pick stops the app routes first.
    model.state.outputs.push_back({"usb", "USB DAC"}); canvas.receive(model.state);
    control(canvas, "output")->activate();
    check(canvas.menuOpen(), "The Output dropdown opens its menu");
    canvas.keyDown(keyOf(d::key::down), w.now); canvas.keyDown(keyOf(d::key::enter), w.now);
    check(!canvas.menuOpen() && model.writes.size() >= 2 && model.writes[model.writes.size() - 2] == "stopAll" && model.writes.back() == "output usb",
        "Down skips the disabled Headphones; picking USB DAC stops every app route, then sets the default output");
    const auto before = model.writes.size();
    control(canvas, "output")->activate(); canvas.keyDown(keyOf(d::key::enter), w.now);
    check(model.writes.size() == before + 1 && model.writes.back() == "output spk", "Picking the current output keeps the routes");
    // Windows cannot switch the default device: the dropdowns are disabled.
    model.state.outputs.pop_back(); model.state.canSetDefaultOutput = model.state.canSetDefaultInput = false; canvas.receive(model.state);
    check(!control(canvas, "output")->enabled() && !control(canvas, "input")->enabled(), "Without default-device switching both dropdowns are disabled");
    model.state.canSetDefaultOutput = model.state.canSetDefaultInput = true; canvas.receive(model.state);
    // App volume (:283-370).
    static_cast<d::Tabs*>(canvas.page().tabs)->select(1, true);
    check(canvas.tab() == 1 && canvas.listedApplications().size() == 4 && canvas.visibleApplicationRows() == 3, "Listed apps: a route or an available one");
    const auto* list = control(canvas, "app:901");
    check(list && dynamic_cast<const d::Slider*>(list)->value() == 100, "An app without a route reads 100%");
    check(!iconRequests.empty() && iconRequests.back() == std::vector<std::string>{"901", "902", "903", "904"}, "Icons are asked for the rows in or next to the view");
    check(canvas.setGain(1, *canvas.listedApplications()[0]) && model.writes.back() != "gain 901 100", "100% on direct playback stays untouched (:397)");
    check(canvas.setGain(.4, *canvas.listedApplications()[0]) && model.writes.back() == "gain 901 40", "Another gain starts the route");
    model.state.applications[0].state = n::VolumeAppState::failed; model.state.applications[0].gain = .4; canvas.receive(model.state);
    check(canvas.setGain(1, *canvas.listedApplications()[0]) && model.writes.back() == "stop 901", "Back at 100% a failed route is retired (:394)");
    {
        // The app row's state line stays as built (its binder holds the released plate weakly, :365-368).
        const auto* row = control(canvas, "app:901")->layer().parent();
        const auto sublabel = [&] {
            for (const auto* child : row->children()) if (child->id().ends_with("row.sublabel")) return d::textOf(*child).text();
            return std::string();
        };
        check(sublabel() == "PID 4101", "The failed route did not touch the row built for direct playback");
        model.state.applications[0].state = n::VolumeAppState::preparing; canvas.receive(model.state);
        check(sublabel() == "PID 4101", "A route state change leaves the built row's state line as it was");
    }
    model.state.perAppStatus = "Route status"; canvas.receive(model.state);
    check(canvas.visibleApplicationRows() == 2, "A status line takes the third row");
    // App sliders scrolled out are not offered (:433-440).
    std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
    const auto offered = std::count_if(items.begin(), items.end(), [](const auto& i) { return i.id.rfind("app:", 0) == 0; });
    check(offered == 2, "Only app sliders inside the list are offered");
    // Unsupported (:286-295).
    model.state.applicationActivitySupported = false; model.state.applicationMessage.reset(); canvas.receive(model.state);
    check(canvas.listedApplications().empty() && !canvas.hasAdjustableApplication(), "Unsupported: no app is listed");
    canvas.setActive(false, w.now);
    check(model.writes.back() == "stop", "Deactivation stops the controller; routes survive (:67-70)");
}

// --- The Mac oracle -------------------------------------------------------------------------------
struct Oracle {
    const Json& data;
    RecordedMeasurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const Json& d) : data(d) {
        recordText(measurer, d["text"]);
        for (const auto& row : d["eventLog"].array()) if (recordMicroTag(measurer, row["layer"])) break;
    }
    const Json* row(std::string_view module, std::string_view state, std::string_view language = "en", bool light = false) const {
        for (const auto& r : data[module].array())
            if (r["state"].string() == state && r["language"].string() == language && r["light"].boolean() == light) return &r;
        return nullptr;
    }
    // The canvas root's tree in its own coordinates (the Mac root sits centred
    // on its host's origin; the Windows root is the box at (0, 0)).
    static Json rooted(const Json& layer) {
        Json copy = layer;
        Point anchor{.5, .5};
        if (copy["anchorPoint"].isArray()) anchor = {copy["anchorPoint"].array()[0].number(), copy["anchorPoint"].array()[1].number()};
        const Rect b = rectOf(copy["bounds"]);
        copy["position"] = Json::Array{anchor.x * b.width + b.x, anchor.y * b.height + b.y};
        copy["transform"] = Json();
        return copy;
    }
    void scene(std::string_view module, std::string_view state, d::Canvas& canvas, std::string_view language = "en", bool light = false) {
        const Json* r = row(module, state, language, light);
        const std::string label = std::string(module) + "/" + std::string(state) + "/" + std::string(language) + (light ? "/light" : "");
        if (!r) { failures.push_back(label + ": not in the oracle"); return; }
        ++compared;
        Json expected = *r;
        expected["layer"] = rooted((*r)["layer"]);
        endfield::tests::design_oracle::compare(label, expected, canvas.root(), canvas.root(), canvas.page().controls(), failures);
        std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
        compareAccessibility(label, static_cast<const Json&>(expected)["accessibility"], items, failures);
        if ((*r)["top"].isObject()) endfield::tests::design_oracle::compare(label + "/top", Json::Object{{"layer", (*r)["top"]}}, canvas.top(), canvas.root(), {}, failures);
        if (canvas.header().counterText() != (*r)["header"]["counter"].string()) failures.push_back(label + ": counter " + canvas.header().counterText());
    }
};
std::uint64_t idOf(std::map<std::string, std::uint64_t>& ids, const std::string& uuid) { return ids.emplace(uuid, ids.size() + 1).first->second; }
void clipboardOracle(Oracle& o) {
    World w(o.measurer); w.appearance(Language::english);
    std::map<std::string, std::uint64_t> ids;
    ClipboardModel model;
    const auto& input = (*o.row("clipboard", "all"))["input"];
    model.state.capacity = std::size_t(input["capacity"].number());
    for (const auto& item : input["items"].array()) {
        m::ClipboardRecord r; r.id = idOf(ids, item["id"].string()); r.pinned = item["pinned"].boolean(); r.preview = item["preview"].string();
        const auto& kind = item["kind"].string();
        r.kind = kind == "url" ? m::ClipboardRecordKind::url : kind == "image" ? m::ClipboardRecordKind::image : kind == "files" ? m::ClipboardRecordKind::files : m::ClipboardRecordKind::text;
        r.createdAt = item["createdAt"].number();
        if (item["thumbnail"].boolean()) r.thumbnail = d::ImageSource{d::ImageSource::Kind::memory, "thumb", {}, 1};
        auto& dd = r.details;
        if (item.contains("source")) dd.source = item["source"].string();
        if (item.contains("characters")) dd.characters = std::int64_t(item["characters"].number());
        if (item.contains("format")) dd.format = item["format"].string();
        if (item.contains("pixelWidth")) dd.pixelWidth = std::int64_t(item["pixelWidth"].number());
        if (item.contains("pixelHeight")) dd.pixelHeight = std::int64_t(item["pixelHeight"].number());
        if (item.contains("fileCount")) dd.fileCount = std::int64_t(item["fileCount"].number());
        if (item.contains("byteCount")) dd.byteCount = std::int64_t(item["byteCount"].number());
        model.state.items.push_back(std::move(r));
    }
    const double wall = input["now"].number();
    m::ClipboardCanvas canvas(w.context, model.actions(), [&] { return wall; });
    w.show(canvas);
    o.scene("clipboard", "all", canvas);
    canvas.pointerMoved(centre(*control(canvas, "clip:0"), canvas.root()), false, w.now);
    o.scene("clipboard", "hoverCard", canvas);
    canvas.pointerMoved(std::nullopt, false, w.now);
    canvas.keyDown(keyOf(d::key::down), w.now); canvas.keyDown(keyOf(d::key::down), w.now);
    o.scene("clipboard", "focusSecond", canvas);
    canvas.rightPointerDown({300, 125 + 64 + 20}, w.now);
    o.scene("clipboard", "contextMenu", canvas);
    canvas.closeMenu();
    canvas.scroll({300, 300}, {0, 80}, w.now);
    o.scene("clipboard", "scrolled", canvas);
    canvas.scroll({300, 300}, {0, -1000}, w.now);
    for (const auto& [filter, name] : std::vector<std::pair<m::ClipboardCanvas::Filter, std::string>>{{m::ClipboardCanvas::Filter::text, "filterText"},
            {m::ClipboardCanvas::Filter::link, "filterLink"}, {m::ClipboardCanvas::Filter::image, "filterImage"}, {m::ClipboardCanvas::Filter::file, "filterFile"}}) {
        canvas.selectFilter(filter);
        o.scene("clipboard", name, canvas);
    }
    canvas.selectFilter(m::ClipboardCanvas::Filter::all);
    w.appearance(Language::english, false, true, true);
    o.scene("clipboard", "recording", canvas);
    w.appearance(Language::english);
    w.appearance(Language::simplifiedChinese); canvas.setActive(false, w.now); w.show(canvas);
    o.scene("clipboard", "all", canvas, "zh-Hans");
    w.appearance(Language::english); canvas.setActive(false, w.now); w.show(canvas);
    w.appearance(Language::english, true);
    o.scene("clipboard", "all", canvas, "en", true);
    w.appearance(Language::english);
    std::erase_if(model.state.items, [](const auto& r) { return !r.pinned; }); canvas.storeChanged();
    o.scene("clipboard", "pinnedOnly", canvas);
    canvas.selectFilter(m::ClipboardCanvas::Filter::image);
    o.scene("clipboard", "emptyFilter", canvas);
    // Formats recorded from Foundation and the canvas.
    for (const auto& s : o.data["clipboardFormat"]["fileSizes"].array())
        if (m::ClipboardCanvas::fileSize(std::int64_t(s["bytes"].number())) != s["text"].string()) o.failures.push_back("fileSize " + s["text"].string());
    for (const auto& s : o.data["clipboardFormat"]["ages"].array()) {
        const double now = 800000000 + 978307200.0;
        if (m::ClipboardCanvas::age(now - s["seconds"].number(), now, Language::english) != s["en"].string()
            || m::ClipboardCanvas::age(now - s["seconds"].number(), now, Language::simplifiedChinese) != s["zh"].string()) o.failures.push_back("age " + s["en"].string());
    }
}
std::optional<m::EventKind> kindOf(std::string_view key) {
    for (unsigned k = 0; k < 32; ++k) if (m::eventKindKey(m::EventKind(k)) == key) return m::EventKind(k);
    return std::nullopt;
}
Language languageOf(std::string_view tag) {
    return tag == "zh-Hans" ? Language::simplifiedChinese : tag == "zh-Hant" ? Language::traditionalChinese : tag == "ja" ? Language::japanese : tag == "ko" ? Language::korean : Language::english;
}
void eventLogOracle(Oracle& o) {
    World w(o.measurer); w.appearance(Language::english);
    EventStore store;
    const auto& input = (*o.row("eventLog", "all"))["input"];
    for (const auto& e : input["events"].array()) {
        m::EventMetadata metadata; for (const auto& [k, v] : e["metadata"].object()) metadata.emplace(k, v.string());
        store.state.events.push_back({e["id"].string(), *kindOf(e["kind"].string()), e["createdAt"].number() - 978307200.0, std::move(metadata)});
    }
    const double now = input["now"].number() - 978307200.0;
    m::EventLogCanvas canvas(w.context, store.callbacks(), utc, [&] { return now; });
    w.show(canvas);
    o.scene("eventLog", "all", canvas);
    canvas.pointerMoved(centre(*canvas.page().controls()[5], canvas.root()), false, w.now);
    o.scene("eventLog", "hoverRow", canvas);
    canvas.pointerMoved(std::nullopt, false, w.now);
    canvas.scrollListForTesting(100);
    o.scene("eventLog", "scrolled", canvas);
    canvas.keyDown(keyOf(d::key::end), w.now);
    o.scene("eventLog", "end", canvas);
    canvas.keyDown(keyOf(d::key::home), w.now);
    for (const auto& [tab, name] : std::vector<std::pair<int, std::string>>{{1, "filterTimer"}, {3, "filterDevices"}, {2, "filterShelf"}}) {
        canvas.page().tabs->controls()[std::size_t(tab)]->activate();
        o.scene("eventLog", name, canvas);
    }
    canvas.page().tabs->controls()[0]->activate();
    w.appearance(Language::simplifiedChinese); canvas.setActive(false, w.now); w.show(canvas);
    o.scene("eventLog", "all", canvas, "zh-Hans");
    w.appearance(Language::english); canvas.setActive(false, w.now); w.show(canvas);
    w.appearance(Language::english, true);
    o.scene("eventLog", "all", canvas, "en", true);
    w.appearance(Language::english);
    store.state.events.clear(); canvas.setActive(false, w.now); w.show(canvas);
    o.scene("eventLog", "empty", canvas);
    w.appearance(Language::simplifiedChinese); canvas.setActive(false, w.now); w.show(canvas);
    o.scene("eventLog", "empty", canvas, "zh-Hans");
    // Sentences in five languages, filters, local times, the metadata allowlist.
    for (const auto& s : o.data["eventLogLines"].array()) {
        m::EventMetadata metadata; for (const auto& [k, v] : s["metadata"].object()) metadata.emplace(k, v.string());
        const auto kind = *kindOf(s["kind"].string());
        const m::SystemEvent e{"00000000-0000-0000-0000-000000009000", kind, 0, metadata};
        if (int(m::eventLogFilterOf(kind)) != int(s["filter"].number())) o.failures.push_back("filter of " + s["kind"].string());
        for (const char* tag : {"en", "zh-Hans", "zh-Hant", "ja", "ko"})
            if (m::eventLogLine(e, languageOf(tag)) != s[tag].string()) o.failures.push_back(std::string("line ") + tag + " " + s[tag].string() + " vs " + m::eventLogLine(e, languageOf(tag)));
    }
    for (const auto& t : o.data["eventLogTimes"].array()) {
        if (t["zone"].string() != "UTC") continue;   // other zones: native/event_log_time.hpp on Windows
        const auto value = m::eventLogTime(t["reference"].number(), t["now"].number(), utc);
        const std::optional<std::string> day = t["day"].isString() ? std::optional(t["day"].string()) : std::nullopt;
        if (value.clock != t["clock"].string() || value.day != day) o.failures.push_back("time " + t["clock"].string());
    }
    for (const auto& s : o.data["eventLogMetadata"].array()) {
        m::EventMetadata raw, expected;
        for (const auto& [k, v] : s["raw"].object()) raw.emplace(k, v.string());
        for (const auto& [k, v] : s["metadata"].object()) expected.emplace(k, v.string());
        const auto kind = *kindOf(s["kind"].string());
        const auto actual = m::sanitizedEventMetadata(kind, raw);
        if (actual != expected) o.failures.push_back("metadata of " + s["kind"].string());
        if (m::eventDetail({"00000000-0000-0000-0000-000000000001", kind, 0, actual}) != s["detail"].string()) o.failures.push_back("detail of " + s["kind"].string());
    }
}
n::VolumeSnapshot volumeOf(const Json& input) {
    n::VolumeSnapshot s;
    const auto device = [](const Json& j) { n::VolumeDevice v; v.id = j["id"].string(); v.name = j["name"].string(); v.canBeDefaultOutput = j["canBeDefaultOutput"].boolean(); v.canBeDefaultInput = j["canBeDefaultInput"].boolean(); return v; };
    for (const auto& j : input["outputs"].array()) s.outputs.push_back(device(j));
    for (const auto& j : input["inputs"].array()) s.inputs.push_back(device(j));
    s.outputID = input["outputID"].string(); s.inputID = input["inputID"].string();
    if (input["volume"].isNumber()) s.volume = input["volume"].number();
    if (input["muted"].isBool()) s.muted = input["muted"].boolean();
    if (input["balance"].isNumber()) s.balance = input["balance"].number();
    s.canSetVolume = input["canSetVolume"].boolean(); s.canSetMute = input["canSetMute"].boolean(); s.canSetBalance = input["canSetBalance"].boolean();
    s.canSetDefaultOutput = input["canSetDefaultOutput"].boolean(); s.canSetDefaultInput = input["canSetDefaultInput"].boolean();
    s.applicationActivitySupported = input["supported"].boolean();
    if (input["applicationMessage"].isString()) s.applicationMessage = input["applicationMessage"].string();
    if (input["perAppStatus"].isString()) s.perAppStatus = input["perAppStatus"].string();
    if (input["status"].isString()) s.status = input["status"].string();
    for (const auto& j : input["applications"].array()) {
        n::VolumeApplication a; a.id = j["id"].string(); a.name = j["name"].string(); a.pid = std::uint32_t(j["pid"].number()); a.available = j["available"].boolean();
        const auto& state = j["state"].string();
        a.state = state == "active" ? n::VolumeAppState::active : state == "preparing" ? n::VolumeAppState::preparing : state == "stopping" ? n::VolumeAppState::stopping
            : state == "failed" ? n::VolumeAppState::failed : n::VolumeAppState::direct;
        if (j["gain"].isNumber()) a.gain = j["gain"].number();
        s.applications.push_back(std::move(a));
    }
    return s;
}
void volumeOracle(Oracle& o) {
    World w(o.measurer); w.appearance(Language::english);
    VolumeModel model; model.state = volumeOf((*o.row("volume", "devices"))["input"]);
    auto canvas = std::make_unique<m::VolumeCanvas>(w.context, model.state, model.callbacks());
    w.show(*canvas);
    o.scene("volume", "devices", *canvas);
    const auto field = centre(*control(*canvas, "output"), canvas->root());
    canvas->pointerDown(field, {}, w.now); canvas->pointerUp(field, w.now);
    o.scene("volume", "outputMenu", *canvas);
    canvas->closeMenu();
    canvas->pointerMoved(centre(*control(*canvas, "volume"), canvas->root()), false, w.now);
    // VolumeCanvas keeps no reference to its row plates: hovering the Volume
    // slider leaves its row unhighlighted (copied).
    o.scene("volume", "hoverSlider", *canvas);
    canvas->pointerMoved(std::nullopt, false, w.now);
    canvas->setActive(false, w.now);
    model.state = volumeOf((*o.row("volume", "mutedReadOnly"))["input"]);
    canvas = std::make_unique<m::VolumeCanvas>(w.context, model.state, model.callbacks());
    w.show(*canvas);
    o.scene("volume", "mutedReadOnly", *canvas);
    canvas->page().tabs->controls()[1]->activate();
    o.scene("volume", "apps", *canvas);
    canvas->scroll({300, 200}, {0, 70}, w.now);
    o.scene("volume", "appsScrolled", *canvas);
    // The Mac's route start sets the per-app status (a rebuild) and its
    // activation clears it (another): the list is rebuilt, its bar hidden.
    auto starting = volumeOf((*o.row("volume", "appsRouted"))["input"]);
    starting.perAppStatus = "Allow System Audio Recording if asked, then play audio in the selected app to activate.";
    canvas->receive(starting);
    canvas->receive(volumeOf((*o.row("volume", "appsRouted"))["input"]));
    o.scene("volume", "appsRouted", *canvas);
    w.appearance(Language::simplifiedChinese); canvas->setActive(false, w.now); w.show(*canvas);
    o.scene("volume", "appsRouted", *canvas, "zh-Hans");
    w.appearance(Language::english); canvas->setActive(false, w.now); w.show(*canvas);
    w.appearance(Language::english, true);
    o.scene("volume", "appsRouted", *canvas, "en", true);
    w.appearance(Language::english);
    canvas->setActive(false, w.now);
    model.state = volumeOf((*o.row("volume", "appsUnsupported"))["input"]);
    canvas = std::make_unique<m::VolumeCanvas>(w.context, model.state, model.callbacks());
    w.show(*canvas);
    canvas->page().tabs->controls()[1]->activate();
    o.scene("volume", "appsUnsupported", *canvas);
}
}

int main(int argc, char** argv) {
    try {
        clipboardFormats(); clipboardCanvas(); eventLogText(); eventLogCanvas(); volumeCanvas();
        if (const auto data = loadOracle(argc > 1 ? argv[1] : nullptr, {"clipboard", "eventLog", "volume"},
                {"text", "layerDefaults", "clipboardFormat", "eventLogLines", "eventLogTimes", "eventLogMetadata"})) {
            Oracle oracle(*data);
            clipboardOracle(oracle); eventLogOracle(oracle); volumeOracle(oracle);
            std::cout << "oracle: " << oracle.compared << " scenes compared, " << oracle.measurer.misses.size() << " unrecorded measurements\n";
            for (const auto& miss : oracle.measurer.misses) std::cout << "  unrecorded: " << miss << '\n';
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
        } else std::cout << "utility-canvases-source.json not present: Swift-derived contracts only\n";
        std::cout << "utility_canvases_tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "utility_canvases_tests failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
