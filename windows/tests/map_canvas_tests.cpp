// The redesigned Map (Mac 1.3.0 Sources/WorldMapCanvas.swift,
// WorldMapSurface.swift) against its oracle (tools/map_canvas_reference.*):
// the surface's camera maths in map px, the canvas geometry (the map square,
// the input circle's square, v1.2.0's control places), and the canvas
// driven through its host entries in the Mac's scripted session (English and
// Simplified Chinese). After every step: the camera and the saved camera,
// the pins (index order) and the selection, the header counter, the readout
// and coordinates plates (frames from the Mac's recorded text widths), the
// controls' rects and enabled states, the accessibility items, the cursor at
// probe points, the toast, the style and recenter events, the press and
// gesture state, and what each key returned. Reduce Motion as in the
// oracle; the clock stands still, so a held wheel gesture stays held.
#include "modules/map_canvas.hpp"
#include "core/data/json.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
namespace d = endfield::modules::design;
namespace m = endfield::modules;
using endfield::core::Language;
using J = ehud::data::Json;
using G = m::MapSurfaceGeometry;
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) <= e * std::max(1., std::max(std::abs(a), std::abs(b))); }
bool near(d::Point p, const J& j) { return near(p.x, j.array()[0].number()) && near(p.y, j.array()[1].number()); }
bool near(d::Rect r, const J& j) { const auto& a = j.array(); return near(r.x, a[0].number()) && near(r.y, a[1].number()) && near(r.width, a[2].number()) && near(r.height, a[3].number()); }
bool near(const m::MapViewport& v, const J& j) { const auto& a = j.array(); return near(v.centerX, a[0].number(), 1e-8) && near(v.centerY, a[1].number(), 1e-8) && near(v.zoom, a[2].number(), 1e-8); }
double number(const J& j) { return j.isNumber() ? j.number() : j.string() == "inf" ? INFINITY : j.string() == "-inf" ? -INFINITY : NAN; }
d::Point point(const J& j) { return {number(j.array()[0]), number(j.array()[1])}; }
m::MapViewport viewport(const J& j) { return {number(j.array()[0]), number(j.array()[1]), number(j.array()[2])}; }

// Text widths: the Mac's recorded CoreText widths.
struct Measurer final : d::TextMeasurer {
    std::map<std::string, double> recorded;
    std::vector<std::string> misses;
    static std::string number(double v) { std::ostringstream s; s.precision(10); s << (std::round(v * 1e6) / 1e6); return s.str(); }
    static std::string key(std::span<const d::TextRun> runs, d::TextRegion region) {
        std::string k(d::regionTag(region));
        for (const auto& r : runs) if (!r.text.empty())
            k += '|' + r.text + '|' + number(r.style.size) + '|' + std::to_string(int(d::resolved(r.style.weight))) + '|' + number(r.style.tracking) + '|' + (r.style.tabular ? "t" : "f");
        return k;
    }
    double width(std::span<const d::TextRun> runs, d::TextRegion region) override {
        if (const auto it = recorded.find(key(runs, region)); it != recorded.end()) return it->second;
        misses.push_back(key(runs, region));
        double w = 0;
        for (const auto& r : runs) w += double(d::portableCharacterRules().count(r.text)) * r.style.size * .55;
        return std::ceil(w * 2) / 2;
    }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion, double) override { return {s.size * s.lineHeight, s.size * .8, s.size * .2}; }
};
struct Store {
    m::MapSnapshot data; unsigned serial{1}, writes{};
    m::MapPersistence callbacks() {
        return {[this](const m::MapSnapshot& v) { data = v; ++writes; },
                [this] { char id[37]; std::snprintf(id, sizeof id, "00000000-0000-4000-8000-%012u", serial++); return std::string(id); },
                [this] { return 800000000. + serial; }};
    }
};
const d::Layer* find(const d::Layer& root, std::string_view suffix) {
    if (root.id().ends_with(suffix)) return &root;
    for (const auto* child : root.children()) if (const auto* hit = find(*child, suffix)) return hit;
    return nullptr;
}
bool visible(const d::Layer& layer, const d::Layer& root) {
    for (const d::Layer* l = &layer; l && l != &root; l = l->parent()) if (l->hidden()) return false;
    return true;
}
std::string pinStyle(m::MapPinStyle s) { return s == m::MapPinStyle::green ? "green" : s == m::MapPinStyle::player ? "player" : "yellow"; }

// MARK: Geometry (WorldMapSurface.swift:17-160, WorldMapCanvas.swift:34-51)
void geometry(const J& root) {
    const auto& c = root["constants"];
    check(near(G::unitScale, c["unitScale"].number()) && near(G::radius, c["radius"].number()) && G::visibleRadius == c["visibleRadius"].number()
        && near(G::rasterScale, c["rasterScale"].number()) && G::side == c["size"].array()[0].number(), "WorldMapSurface scale, input circle and visible radius");
    check(near(m::MapCanvas::mapCenter, c["mapCenter"]) && near(m::MapCanvas::mapFrame, c["mapFrame"]) && near(m::MapCanvas::circleFrame, c["circleFrame"]),
        "The map square is the backdrop disc's; the input circle's square overflows the box");
    const std::array<d::Point, 5> v1{d::Point{37, 223}, {403, 218}, {0, 301}, {0, 0}, {440, 440}};
    for (std::size_t n = 0; n < v1.size(); ++n) check(near(m::MapCanvas::v1(v1[n].x, v1[n].y), c["v1"].array()[n]), "v1.2.0's viewport points in box px");
    for (std::size_t n = 0; n < 3; ++n) check(near(G::pinHitRadius(std::array{m::MapPinStyle::yellow, m::MapPinStyle::green, m::MapPinStyle::player}[n]), c["pinHitRadius"].array()[n].number()), "Circular pin hit radii");
    check(c["zoomRange"].array()[0].number() == m::MapViewport::minimumZoom && c["zoomRange"].array()[1].number() == m::MapViewport::maximumZoom
        && c["legacyMinZoom"].number() == m::MapViewport::legacyMinimumZoom && near(m::MapViewport{}, c["defaultCamera"]), "Zoom 1...128, v1.2.0's 2.1 floor, the default camera");
    std::size_t n{};
    for (const auto& row : root["cameras"].array()) {
        const auto start = G::constrained(viewport(row["camera"]));
        check(near(start, row["constrained"]), "WorldMapSurface.constrained");
        check(near(m::mapConstrained(viewport(row["camera"])), row["geometryConstrained"]), "WorldMapGeometry.constrained (the raster's space)");
        const auto at = point(row["point"]), delta = point(row["delta"]);
        check(G::contains(at) == row["contains"].boolean(), "The input circle");
        const auto world = G::world(at, start);
        check(near(world, row["world"]) && near(G::screen(world.x, world.y, start), row["screen"]), "world / screen in map px");
        check(near(G::panned(start, delta), row["panned"]) && near(G::zoomed(start, at, number(row["factor"])), row["zoomed"]), "panned / zoomed");
        const auto unit = G::unit(start.zoom);
        check(near(unit.width, row["unit"].array()[0].number()) && near(unit.height, row["unit"].array()[1].number()), "Map px per world unit");
        check(m::mapCoordinateDescription(start.centerX, start.centerY) == row["description"].string(), "WorldMapGeometry.coordinateDescription");
        ++n;
    }
    check(n == 90, "Every camera case compared");
}

// MARK: The scripted session (tools/map_canvas_reference.swift)
struct Session {
    Measurer& measurer;
    d::Context context;
    Store store;
    m::MapState state;
    m::MapCanvas canvas;
    std::vector<std::string> styles;
    int recentered{};
    double now{100};
    Session(Measurer& w, Language language) : measurer(w), context(w, d::asciiUppercase), state({}, store.callbacks()), canvas(context, state) {
        context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now);
        d::Context::Appearance a; a.language = language; a.reduceMotion = true;
        context.apply(a, now);
        state.setReducedMotion(true);
        canvas.onPinStyleChanged = [this](m::MapPinStyle s) { styles.push_back(pinStyle(s)); };
        canvas.onRecenter = [this] { ++recentered; };
        canvas.show(now);
        canvas.setActive(true, now);
    }
    d::Point centre(std::string_view id) const {
        for (const auto* control : canvas.page().controls()) if (control->id().ends_with(id)) { const auto r = d::convert(d::localBounds(control->layer()), control->layer(), canvas.root()); return {r.x + r.width / 2, r.y + r.height / 2}; }
        throw std::runtime_error("No control " + std::string(id));
    }
    d::Point box(double x, double y) const { const auto s = G::screen(x, y, state.camera()); return {s.x + m::MapCanvas::mapFrame.x, s.y + m::MapCanvas::mapFrame.y}; }
    void click(d::Point p) { (void)canvas.pointerDown(p, {p}, now); canvas.pointerUp(p, now); }
    bool key(std::uint32_t vk, bool shift = false, bool control = false) { d::KeyInput k; k.virtualKey = vk; k.shift = shift; k.control = control; return canvas.keyDown(k, now); }
    void ax(const std::string& id) { (void)canvas.performAccessibilityAction(id, now); }
};
std::string label(const J& row) { return row["name"].string(); }
void compare(Session& s, const J& row, std::span<const d::Point> probes) {
    const auto name = "[" + label(row) + "] ";
    check(near(s.state.camera(), row["camera"]), name + "camera");
    check(near(s.store.data.viewport, row["stored"]) || (s.store.writes == 0 && near(m::MapViewport{}, row["stored"])), name + "saved camera");
    check(s.store.data.pins.size() == std::size_t(row["storedPins"].number()), name + "saved pins");
    const auto pins = s.state.pins();
    check(pins.size() == row["pins"].array().size(), name + "pin count");
    for (std::size_t n = 0; n < pins.size(); ++n) {
        const auto& p = row["pins"].array()[n].array();
        check(near(pins[n].x, p[0].number(), 1e-8) && near(pins[n].y, p[1].number(), 1e-8) && pinStyle(pins[n].style) == p[2].string(), name + "pin " + std::to_string(n));
    }
    std::optional<std::size_t> selected;
    if (s.state.selection()) for (std::size_t n = 0; n < pins.size(); ++n) if (pins[n].id == *s.state.selection()) selected = n;
    check(row["selected"].isNull() ? !selected : selected && *selected == std::size_t(row["selected"].number()), name + "selection");
    check(s.canvas.header().counterText() == row["counter"].string(), name + "counter " + s.canvas.header().counterText());
    for (const auto& c : row["controls"].array()) {
        bool found{};
        for (const auto* control : s.canvas.page().controls()) if (control->id().ends_with(c["id"].string())) {
            found = true;
            const auto r = d::convert(d::localBounds(control->layer()), control->layer(), s.canvas.root());
            check(near(r, c["rect"]) && control->enabled() == c["enabled"].boolean(), name + "control " + c["id"].string() + " " + std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.width) + " enabled " + std::to_string(control->enabled()));
        }
        check(found, name + "control exists " + c["id"].string());
    }
    std::vector<d::AccessibilityItem> items;
    s.canvas.accessibilityItems(items);
    const auto& expected = row["accessibility"].array();
    check(items.size() == expected.size(), name + "accessibility count " + std::to_string(items.size()));
    for (std::size_t n = 0; n < items.size(); ++n) {
        auto id = items[n].id;
        constexpr std::string_view prefix = "map.pin:";
        if (id.starts_with(prefix)) for (std::size_t k = 0; k < pins.size(); ++k) if (id == std::string(prefix) + pins[k].id) id = "map.pin:#" + std::to_string(k);
        const auto& e = expected[n];
        check(id.ends_with(e["id"].string()) && items[n].label == e["label"].string() && near(items[n].rect, e["rect"]) && items[n].enabled == e["enabled"].boolean(),
            name + "accessibility " + e["id"].string() + " / " + items[n].label);
    }
    for (const auto& [id, key] : {std::pair{"map.zoom.plate", "map.zoom.plate"}, std::pair{"map.coordinates", "map.coordinates"}}) {
        const auto* layer = find(s.canvas.root(), id);
        check(layer != nullptr, name + "plate layer");
        if (row[key].isNull()) check(layer->hidden(), name + key + " hidden");
        else check(!layer->hidden() && near(d::convert(d::localBounds(*layer), *layer, s.canvas.root()), row[key]), name + key + " frame");
    }
    // The map's own texts (readout, coordinates): string and line box.
    for (const auto& t : row["texts"].array()) {
        const auto& text = t["text"].string();
        const bool mine = text.find("×") != std::string::npos || text.starts_with("PIN ") || text.starts_with("标记 ") || text.starts_with("N ") || text.starts_with("S ") || text.starts_with("E ") || text.starts_with("W ");
        if (!mine) continue;
        bool found{};
        std::vector<const d::Layer*> stack{&s.canvas.root()};
        while (!stack.empty() && !found) {
            const auto* l = stack.back(); stack.pop_back();
            for (const auto* child : l->children()) stack.push_back(child);
            if (!l->text() || l->text()->text() != text || !visible(*l, s.canvas.root())) continue;
            found = l->parent() && near(d::convert(l->text()->lineFrame, *l->parent(), s.canvas.root()), t["frame"]);
        }
        check(found, name + "text " + text);
    }
    const auto& toast = s.context.overlay().toastState().current();
    if (row["toast"].isNull()) check(!toast, name + "no toast");
    else check(toast && toast->title == row["toast"].array()[0].string() && toast->caption == row["toast"].array()[1].string(), name + "toast " + (toast ? toast->title : ""));
    s.context.overlay().clearToast();
    J::Array styles; for (const auto& st : s.styles) styles.push_back(st);
    check(J(styles) == row["styles"] && s.recentered == int(row["recentered"].number()), name + "style and recenter events");
    check(s.state.dragging() == row["dragging"].boolean() && s.canvas.inputLocked() == row["inputLocked"].boolean(), name + "press and gesture state");
    if (row.contains("cursors")) {
        const auto& cursors = row["cursors"].array();
        for (std::size_t n = 0; n < probes.size(); ++n) {
            const auto c = s.canvas.cursor(probes[n]);
            const std::string got = !c ? "none" : *c == d::Cursor::openHand ? "open" : *c == d::Cursor::closedHand ? "closed" : "other";
            check(got == (cursors[n].isNull() ? "none" : cursors[n].string()), name + "cursor " + std::to_string(n));
        }
    }
}
void session(Measurer& measurer, Language language, const J& expected) {
    Session s(measurer, language);
    const auto& steps = expected["steps"].array();
    std::size_t at{};
    std::vector<d::Point> probes;
    for (const auto& p : steps[0]["probes"].array()) probes.push_back(point(p));
    const auto step = [&](std::string_view name, std::optional<bool> handled = {}) {
        check(at < steps.size() && label(steps[at]) == name, "Step order: " + std::string(name));
        const auto& row = steps[at++];
        if (handled) check(row["handled"].boolean() == *handled, "[" + std::string(name) + "] handled");
        compare(s, row, probes);
    };
    check(near(s.canvas.hitBounds(), steps[0]["hitBounds"]), "hitBounds = box U the input circle's square");
    for (std::size_t n = 0; n < probes.size(); ++n) check(m::MapCanvas::onMap(probes[n]) == steps[0]["onMap"].array()[n].boolean(), "onMap probe " + std::to_string(n));
    step("initial");
    const auto corner = probes[3];
    s.click(corner); (void)s.canvas.rightPointerDown(corner, s.now); step("corner");
    s.click(s.box((139.6917 + 180) / 360, (90 - 35.6895) / 180)); step("recenter.tokyo");
    s.ax("map.zoomIn"); step("zoomIn");
    s.ax("map.zoomOut"); step("zoomOut");
    for (int n = 0; n < 5; ++n) s.ax("map.zoomOut");
    step("zoomOut.floor");
    for (int n = 0; n < 13; ++n) s.ax("map.zoomIn");
    step("zoomIn.cap");
    s.click(s.centre("map.reset")); step("reset.click");
    (void)s.canvas.scroll({400, 250}, {0, -30}, s.now); step("wheel.in");
    (void)s.canvas.scroll({400, 250}, {0, 30}, s.now); step("wheel.out");
    (void)s.canvas.scroll({400, 250}, {0, -200}, s.now); step("wheel.clamped");
    (void)s.canvas.scroll(corner, {0, -30}, s.now); step("wheel.outside");
    (void)s.canvas.magnify({250, 330}, .25, s.now); step("magnify");
    (void)s.canvas.magnify({250, 330}, -3, s.now); step("magnify.clamped");
    s.ax("map.reset"); step("reset");
    (void)s.canvas.pointerDown({300, 300}, {{300, 300}}, s.now);
    s.canvas.pointerDragged({301, 302}, s.now); step("drag.threshold");
    s.canvas.pointerDragged({320, 315}, s.now); s.canvas.pointerDragged({340, 330}, s.now); step("drag.moved");
    s.canvas.pointerDragged({900, 900}, s.now); step("drag.outside");
    s.canvas.pointerUp({900, 900}, s.now); step("drag.released");
    s.ax("map.reset");
    // Mac key codes 123-126, 24, 27, 69, 78 as Windows virtual keys.
    for (const auto& [name, vk] : {std::pair{"key.left", d::key::left}, {"key.right", d::key::right}, {"key.down", d::key::down}, {"key.up", d::key::up},
                                   {"key.equal", 0xBBu}, {"key.minus", 0xBDu}, {"key.keypadPlus", 0x6Bu}, {"key.keypadMinus", 0x6Du}}) {
        const bool handled = s.key(vk); step(name, handled);
    }
    { const bool h = s.key(d::key::left, true); step("key.shiftLeft", h); }
    { const bool h = s.key(d::key::left, false, true); step("key.commandLeft", h); }
    { const bool h = s.key(d::key::escape); step("key.escape.none", h); }
    { const bool h = s.key(d::key::back); step("key.delete.none", h); }
    s.ax("map.reset");
    (void)s.canvas.rightPointerDown({250, 260}, s.now); step("pin.add");
    (void)s.canvas.rightPointerDown(s.centre("map.zoomOut"), s.now); step("pin.column");
    const auto first = [&] { const auto& p = s.state.pins().front(); return s.box(p.x, p.y); };
    s.click(first()); step("pin.cycle.green");
    s.click(first()); step("pin.cycle.player");
    s.click(first()); step("pin.cycle.yellow");
    s.click({200, 250}); step("pin.deselect");
    s.click(first()); step("pin.select");
    { const bool h = s.key(d::key::escape); step("pin.escape", h); }
    s.click(first());
    { const bool h = s.key(d::key::back); step("pin.delete", h); }
    (void)s.canvas.rightPointerDown({360, 280}, s.now); (void)s.canvas.rightPointerDown({360, 280}, s.now); step("pin.rightRemove");
    s.click(s.centre("map.addPin")); step("pin.centre");
    (void)s.canvas.rightPointerDown({330, 220}, s.now); step("pin.second");
    s.ax("map.pin:" + s.state.pins()[0].id); step("ax.pin.select");
    s.ax("map.pin:" + s.state.pins()[0].id); step("ax.pin.cycle");
    s.ax("map.removePin"); step("ax.removePin");
    (void)s.canvas.rightPointerDown({340, 222}, s.now); step("pin.near");
    for (int n = 0; n < 4; ++n) s.ax("map.zoomOut");
    step("pins.wide");
    s.ax("map.reset");
    (void)s.canvas.rightPointerDown({120, 420}, s.now); step("pin.southWest");
    for (int n = 0; n < 9; ++n) s.ax("map.zoomIn");
    step("readout.long");
    s.canvas.setActive(false, s.now); step("inactive");
    s.canvas.setActive(true, s.now); step("reactivated");
    check(at == steps.size(), "Every oracle step replayed");
}
void oracle(const char* path) {
    std::ifstream f(path, std::ios::binary);
    check(bool(f), "Pass map-canvas-source.json");
    const auto root = J::parse(std::string((std::istreambuf_iterator<char>(f)), {}), 8 * 1024 * 1024);
    geometry(root);
    Measurer measurer;
    for (const auto& w : root["widths"].array()) {
        std::vector<d::TextRun> runs;
        for (const auto& r : w["runs"].array()) { d::TextStyle style; style.size = r["size"].number(); style.weight = d::FontWeight(int(r["weight"].number())); style.tracking = r["tracking"].number(); style.tabular = r["tabular"].boolean(); runs.push_back({r["displayed"].string(), style}); }
        if (const auto region = d::regionFromTag(w["language"].string())) measurer.recorded[Measurer::key(runs, *region)] = w["width"].number();
    }
    for (const auto& s : root["sessions"].array()) session(measurer, s["language"].string() == "en" ? Language::english : Language::simplifiedChinese, s);
    // The oracle records only the texts its snapshots show: a readout length
    // or a pin's coordinates the session passes between snapshots is
    // estimated here (a later snapshot showing it would fail the plate frames
    // above). Every other text (header, labels) must have its Mac width.
    std::erase_if(measurer.misses, [](const std::string& k) {
        const auto text = k.substr(k.find('|') + 1);
        return text.find("×") != std::string::npos || text.starts_with("N ") || text.starts_with("S ") || text.starts_with("E ") || text.starts_with("W ")
            || text.starts_with("PIN ") || text.starts_with("标记 ");
    });
    check(measurer.misses.empty(), "Every measured text had a recorded Mac width" + (measurer.misses.empty() ? std::string() : ": " + measurer.misses.front()));
}

// MARK: Behaviour the session does not reach
void lifecycle() {
    Measurer measurer;
    // A store that could not be opened: one notice on the first activation;
    // placing a pin says why; nothing is saved.
    d::Context context(measurer, d::asciiUppercase);
    context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, 1);
    m::MapState state;
    m::MapCanvas canvas(context, state, std::string("The saved map could not be read. The original data has been preserved."));
    int prepared{}, released{};
    canvas.onPrepare = [&] { ++prepared; };
    canvas.onRelease = [&] { ++released; };
    canvas.show(1);
    check(prepared == 1 && !state.active(), "Showing the canvas starts the painter (prepareForPresentation) before input");
    canvas.setActive(true, 1);
    check(prepared == 2 && state.active(), "Activation prepares again and activates the surface (pins may pulse)");
    const auto& toast = context.overlay().toastState().current();
    check(toast && toast->title == "Notice" && toast->caption == "The saved map could not be read. The original data has been preserved.", "The store error is a notice once");
    context.overlay().clearToast();
    canvas.setActive(false, 2); canvas.setActive(true, 2);
    check(!context.overlay().toastState().current() && released == 1, "Not again on a later activation; deactivation releases the working set");
    (void)canvas.rightPointerDown({250, 260}, 3);
    check(state.pins().empty() && context.overlay().toastState().current()->caption.starts_with("The saved map"), "Placing a pin without a store says why");
    context.overlay().clearToast();
    // The 0.18 s gesture hold: the wheel commits once input pauses.
    Store store;
    m::MapState saved({}, store.callbacks());
    m::MapCanvas held(context, saved);
    held.show(10); held.setActive(true, 10);
    (void)held.scroll({400, 250}, {0, -12}, 10);
    check(held.gestureHeld() && store.writes == 0 && held.nextWakeTime(10) && std::abs(*held.nextWakeTime(10) - 10.18) < 1e-9, "A wheel notch holds the gesture for .18 s");
    (void)held.scroll({400, 250}, {0, -12}, 10.1);
    check(std::abs(*held.nextWakeTime(10.1) - 10.28) < 1e-9 && store.writes == 0, "Each event re-arms the one deadline");
    held.wake(10.2);
    check(held.gestureHeld() && store.writes == 0, "Not before it");
    held.wake(10.28);
    check(!held.gestureHeld() && store.writes == 1 && store.data.viewport == saved.camera() && !held.nextWakeTime(10.3), "Then one archive write of the final camera");
    // Interrupted pan: committed, never a click recenter.
    (void)held.pointerDown({300, 300}, {{300, 300}}, 11);
    held.pointerDragged({340, 330}, 11);
    const auto panned = saved.camera();
    held.setActive(false, 11);
    check(!saved.dragging() && store.data.viewport == panned && store.writes == 2, "Leaving the Map cancels the pan and saves it without a click recenter");
    // Reset view: default zoom AND centre (v1.2.0 kept the centre).
    held.setActive(true, 12);
    (void)held.performAccessibilityAction("map.zoomIn", 12);
    (void)held.performAccessibilityAction("map.reset", 12);
    check(saved.camera() == G::constrained({}), "Reset view restores the default camera");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass tests/fixtures/map-canvas-source.json");
        oracle(argv[1]);
        lifecycle();
        std::cout << "PASS " << checks << " map canvas checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
