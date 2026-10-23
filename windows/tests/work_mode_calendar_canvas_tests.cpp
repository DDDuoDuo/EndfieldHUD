// Work Mode and Calendar on the redesign (modules/work_mode_canvas.*,
// modules/calendar_canvas.*) against the Mac oracle
// (tests/fixtures/work-mode-calendar-source.json, produced by
// tools/work_mode_calendar_reference.sh from the unchanged Mac 1.3.0 Sources):
// every scripted Mac state is replayed through the Windows canvases with the
// same controller steps and compared node by node (what each tree draws, the
// controls, the accessibility items) with the Mac's recorded CoreText widths,
// line counts and date formats; then the values the Mac records directly
// (timer text, ring fraction and lit ticks, laps, toasts, the delete request)
// and the motion the Mac starts (ring .6 s E, marker, blink) are checked.
// The ring's tick chunks and half-ring arc are an adaptation (see
// work_mode_canvas.hpp): they are checked against the Mac's tick path,
// strokeEnd and marker transform instead of as primitives.
// Portable; synthetic in-memory Calendar repository; no file, OS or clock.
#include "modules/calendar_canvas.hpp"
#include "modules/calendar_localization.hpp"
#include "modules/work_mode_canvas.hpp"
#include "tests/hud_design_oracle.hpp"
#include <deque>
#include <iostream>
#include <numbers>
#include <set>
#include <utility>

namespace {
namespace d = endfield::modules::design;
namespace m = endfield::modules;
namespace core = endfield::core;
using namespace endfield::tests::design_oracle;
using L = core::Language;
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
constexpr double pi = std::numbers::pi;
bool same(const core::CubicTiming& a, const core::CubicTiming& b) { return a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2; }

// Recorded widths and metrics (RecordedMeasurer) plus the Mac's line counts
// of the two-line plate texts (designText "lines" of the recorded trees).
struct OracleMeasurer final : d::TextMeasurer {
    RecordedMeasurer recorded;
    std::map<std::string, std::size_t> lines;
    std::vector<std::string> lineMisses;
    static std::string lineKey(std::span<const d::TextRun> runs, d::TextRegion region, double width, int maximum) {
        return RecordedMeasurer::key(runs, region) + "@" + RecordedMeasurer::number(width) + "/" + std::to_string(maximum);
    }
    double width(std::span<const d::TextRun> runs, d::TextRegion region) override { return recorded.width(runs, region); }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion region, double scale) override { return recorded.metrics(s, region, scale); }
    std::size_t lineCount(std::span<const d::TextRun> runs, d::TextRegion region, double width, int maximum) override {
        const auto key = lineKey(runs, region, width, maximum);
        if (const auto it = lines.find(key); it != lines.end()) return it->second;
        lineMisses.push_back(key);
        return d::TextMeasurer::lineCount(runs, region, width, maximum);
    }
};
d::TextStyle styleOf(const Json& s) {
    d::TextStyle style;
    style.size = s["size"].number(); style.weight = d::FontWeight(int(s["weight"].number()));
    style.tracking = s.contains("tracking") ? s["tracking"].number() : 0; style.tabular = s.contains("tabular") && s["tabular"].boolean();
    style.lineHeight = s.contains("lineHeight") ? s["lineHeight"].number() : 1;
    return style;
}
void collectLines(const Json& n, OracleMeasurer& measurer) {
    if (n["designText"].isObject()) {
        const auto& t = n["designText"];
        if (int(t["maximumLines"].number()) != 1 && t["lines"].isArray() && !t["lines"].array().empty()) {
            std::vector<d::TextRun> runs;
            for (const auto& r : t["runs"].array()) runs.push_back({r.contains("displayed") ? r["displayed"].string() : r["text"].string(), styleOf(r["style"])});
            if (const auto region = d::regionFromTag(t["resolvedLanguage"].string()))
                measurer.lines[OracleMeasurer::lineKey(runs, *region, rectOf(t["lineFrame"]).width, int(t["maximumLines"].number()))] = t["lines"].array().size();
        }
    }
    if (n["children"].isArray()) for (const auto& c : n["children"].array()) collectLines(c, measurer);
}
void load(const Json& data, OracleMeasurer& measurer) {
    for (const auto& entry : data["text"]["widths"].array()) {
        std::vector<d::TextRun> runs;
        for (const auto& r : entry["runs"].array()) {
            d::TextStyle s; s.size = r["size"].number(); s.weight = d::FontWeight(int(r["weight"].number())); s.tracking = r["tracking"].number(); s.tabular = r["tabular"].boolean();
            runs.push_back({r.contains("displayed") ? r["displayed"].string() : r["text"].string(), s});
        }
        if (const auto region = d::regionFromTag(entry["language"].string())) measurer.recorded.widths[RecordedMeasurer::key(runs, *region)] = entry["width"].number();
    }
    for (const auto& x : data["text"]["metrics"].array())
        measurer.recorded.metric[RecordedMeasurer::metricKey(x["size"].number(), int(x["weight"].number()), x["lineHeight"].number(), x["tabular"].boolean(), x["language"].string())]
            = {x["height"].number(), x["ascent"].number(), x["descent"].number()};
    for (const auto& row : data["rows"].array()) collectLines(row["layer"], measurer);
}
std::string tag(L l) {
    switch (l) { case L::simplifiedChinese: return "zh-Hans"; case L::traditionalChinese: return "zh-Hant"; case L::japanese: return "ja"; case L::korean: return "ko"; default: return "en"; }
}
constexpr std::array<L, 5> languages{L::english, L::simplifiedChinese, L::traditionalChinese, L::japanese, L::korean};

// One design context per scenario (the oracle's fresh HUDDesignContext).
struct Harness {
    d::Context context;
    Harness(OracleMeasurer& measurer, L language = L::english, std::string accent = "FAD41F", bool light = false) : context(measurer, d::asciiUppercase) {
        d::Context::Appearance a; a.language = language; a.accentHex = std::move(accent); a.light = light;
        context.apply(a, 0);
    }
    void reduce(bool v) { auto a = context.appearance(); a.reduceMotion = v; context.apply(a, context.env().now); }
};
const Json* findNamed(const Json& n, std::string_view name) {
    if (n["name"].isString() && n["name"].string() == name) return &n;
    if (n["children"].isArray()) for (const auto& c : n["children"].array()) if (const auto* f = findNamed(c, name)) return f;
    return nullptr;
}
const d::Layer* findLayer(const d::Layer& l, std::string_view suffix) {
    if (std::string_view(l.id()).ends_with(suffix)) return &l;
    for (const auto* c : l.children()) if (const auto* f = findLayer(*c, suffix)) return f;
    return nullptr;
}
std::string roleName(d::AccessibilityRole r) {
    switch (r) {
    case d::AccessibilityRole::toggle: return "toggle"; case d::AccessibilityRole::popUp: return "popUp"; case d::AccessibilityRole::menuItem: return "menuItem";
    case d::AccessibilityRole::tab: return "tab"; case d::AccessibilityRole::slider: return "slider"; case d::AccessibilityRole::text: return "text";
    case d::AccessibilityRole::edit: return "button";   // the Mac's idle field stand-in is a button (an edit here, LIBRARY-DESIGN section 9)
    default: return "button";
    }
}

std::string canvasText(const d::Canvas& canvas, std::string_view english, std::string_view chinese) { return canvas.text(english, chinese); }
d::Layer& findField(d::Canvas& canvas, std::string_view id) {
    const std::function<d::Layer*(d::Layer&)> find = [&](d::Layer& l) -> d::Layer* {
        if (l.id() == "calendar.canvas." + std::string(id)) return &l;
        for (auto* c : l.children()) if (auto* f = find(*c)) return f;
        return nullptr;
    };
    auto* l = find(canvas.root());
    if (!l) throw std::runtime_error("No field " + std::string(id));
    return *l;
}
struct Run {
    const Json& data;
    OracleMeasurer& measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    const Json* row(std::string_view module, std::string_view state, L language = L::english, bool light = false) const {
        for (const auto& r : data["rows"].array())
            if (r["module"].string() == module && r["state"].string() == state && r["language"].string() == tag(language) && r["light"].boolean() == light) return &r;
        return nullptr;
    }
    // The tree, the controls and the canvas's accessibility list against one oracle row.
    const Json* compareCanvas(const std::string& label, std::string_view module, std::string_view state, d::Canvas& canvas,
        std::vector<d::Control*> controls, L language = L::english, bool light = false) {
        const Json* expected = row(module, state, language, light);
        if (!expected) { failures.push_back(label + ": not in the oracle"); return nullptr; }
        ++compared;
        Json mac = *expected;
        // The Mac root sits at the disc centre of its module host (frame -295, -255); both trees compare in root coordinates.
        mac["layer"]["frame"] = Json::Array{0, 0, d::metric::boxWidth, d::metric::boxHeight};
        std::vector<d::Layer*> hidden;
        // Not copied (a Mac bug): the Mac's lazy Calendar fields keep the label
        // language and frame theme of their first use (the oracle builds them in
        // English dark first). Their labels follow the UI language here and
        // their frames the palette: those two entries are checked by fields().
        std::vector<d::Control*> compared;
        for (auto* c : controls) compared.push_back(c);
        if (module == "calendar") {
            const auto patch = [&](Json& list, bool items) {
                Json::Array out;
                for (const auto& c : std::as_const(list).array()) {
                    Json copy = c;
                    const auto id = c["id"].string();
                    if (id == "calendar.title" || id == "calendar.details") {
                        const std::string label = id == "calendar.title" ? canvasText(canvas, "Title", "标题") : canvasText(canvas, "Details", "详情");
                        copy["label"] = label;
                        if (!items && c["accessibility"].isArray() && !c["accessibility"].array().empty()) {
                            Json::Array a = c["accessibility"].array(); a[0]["label"] = label; copy["accessibility"] = Json(std::move(a));
                        }
                    }
                    out.push_back(std::move(copy));
                }
                list = Json(std::move(out));
            };
            patch(mac["controls"], false);
            if (light) {
                const std::function<void(Json&)> strip = [&](Json& n) {
                    if (!std::as_const(n)["children"].isArray()) return;
                    Json::Array kept;
                    for (const auto& c : std::as_const(n)["children"].array()) {
                        if (c["name"].isString() && c["name"].string() == "field") continue;
                        Json copy = c; strip(copy); kept.push_back(std::move(copy));
                    }
                    n["children"] = Json(std::move(kept));
                };
                strip(mac["layer"]);
                for (auto* l : {&findField(canvas, "calendar.title"), &findField(canvas, "calendar.details")}) if (!l->hidden()) hidden.push_back(l);
            }
        }
        if (module == "workMode") {
            // The ring's tick paths are chunked here: checked by ring() instead.
            const std::function<void(Json&)> strip = [&](Json& n) {
                if (!std::as_const(n)["children"].isArray()) return;
                Json::Array kept;
                for (const auto& c : std::as_const(n)["children"].array()) {
                    if (c["name"].isString() && (c["name"].string() == "workMode.ticks" || c["name"].string() == "workMode.ticks.lit")) continue;
                    Json copy = c; strip(copy); kept.push_back(std::move(copy));
                }
                n["children"] = Json(std::move(kept));
            };
            strip(mac["layer"]);
            const std::function<void(const d::Layer&)> chunks = [&](const d::Layer& l) {
                const std::string_view id = l.id();
                if (id.find("workMode.ticks") != std::string_view::npos && !l.hidden()) hidden.push_back(const_cast<d::Layer*>(&l));
                for (const auto* c : l.children()) chunks(*c);
            };
            chunks(canvas.root());
            }
        for (auto* l : hidden) l->setHidden(true);
        endfield::tests::design_oracle::compare(label, mac, canvas.root(), canvas.root(), compared, failures);
        for (auto* l : hidden) l->setHidden(false);
        std::vector<d::AccessibilityItem> items;
        canvas.accessibilityItems(items);
        const auto& theirs = (*expected)["accessibility"].array();
        if (theirs.size() != items.size()) failures.push_back(label + ": accessibility list " + std::to_string(items.size()) + " vs " + std::to_string(theirs.size()));
        else for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& t = theirs[i]; const auto& o = items[i];
            const bool field = o.id == "calendar.title" || o.id == "calendar.details";      // see above
            const bool created = o.id.starts_with("calendar.event:C0000000-0000-4000-8000-0000000001"); // a new event's generated UUID
            if ((t["id"].string() != o.id && !(created && t["id"].string().starts_with("calendar.event:"))) || (!field && t["label"].string() != o.label) || t["role"].string() != roleName(o.role) || !near(rectOf(t["rect"]), o.rect, 2e-3)
                || t["enabled"].boolean() != o.enabled || (t.contains("on") && t["on"].boolean() != o.on) || (t.contains("value") && t["value"].string() != o.value))
                failures.push_back(label + ": accessibility item " + o.id + " '" + o.label + "' vs " + t["id"].string() + " '" + t["label"].string() + "'");
        }
        return expected;
    }
};

// ---------------------------------------------------------------------------
// Work Mode
// ---------------------------------------------------------------------------
struct WorkWorld {
    Harness harness;
    double now{1000};
    m::WorkModeCanvas* canvasPointer{};
    m::WorkModeController controller{{[this] { if (canvasPointer) canvasPointer->controllerChanged(); }, {}}};
    bool hudVisible{};
    std::unique_ptr<m::WorkModeCanvas> canvas;
    explicit WorkWorld(OracleMeasurer& measurer, L language = L::english, std::string accent = "FAD41F", bool light = false) : harness(measurer, language, std::move(accent), light) {
        m::WorkModeCanvasOptions options;
        options.clock = [this] { return now; };
        options.hudVisible = [this] { return hudVisible; };
        canvas = std::make_unique<m::WorkModeCanvas>(harness.context, controller, options);
        canvasPointer = canvas.get();
        harness.context.env().now = now;
        canvas->show(now);
        canvas->setActive(true, now);
    }
    ~WorkWorld() { canvasPointer = nullptr; }
    void advance(double seconds) { now += seconds; harness.context.env().now = now; controller.wake(now); }
    d::Control* control(std::string_view id) const {
        for (auto* c : canvas->page().controls()) if (c->id() == id) return c;
        return nullptr;
    }
    d::Point centre(const d::Control& c) const { const auto r = c.rect(canvas->root()); return {r.x + r.width / 2, r.y + r.height / 2}; }
    void press(std::string_view id) { const auto p = centre(*control(id)); canvas->pointerDown(p, {p}, now); canvas->pointerUp(p, now); }
    bool key(std::uint32_t vk, char32_t ch = 0) { d::KeyInput k; k.virtualKey = vk; k.character = ch; return canvas->keyDown(k, now); }
    std::vector<d::Control*> controls() {
        std::vector<d::Control*> list(canvas->page().controls().begin(), canvas->page().controls().end());
        if (canvas->editingCustom()) list.push_back(&canvas->customField());
        return list;
    }
};
// The ring against the Mac's: lit ticks (path), the arc's strokeEnd, the marker.
void ring(Run& run, const std::string& label, const Json& expected, WorkWorld& w) {
    const auto snapshot = w.controller.snapshot(w.now);
    const auto lit = m::workModeDial::litTicks(m::workModeDial::fraction(snapshot));
    const auto* litPath = findNamed(expected["layer"], "workMode.ticks.lit");
    const auto* dimPath = findNamed(expected["layer"], "workMode.ticks");
    std::size_t macLit{}, macDim{};
    for (const auto& op : litPath->operator[]("shape")["path"].array()) macLit += op["op"].string() == "move";
    for (const auto& op : dimPath->operator[]("shape")["path"].array()) macDim += op["op"].string() == "move";
    if (int(macLit) != lit || int(macDim) != 120 - lit) run.failures.push_back(label + ": lit ticks " + std::to_string(lit) + " vs " + std::to_string(macLit));
    // The painted chunks hold exactly the lit / unlit quads (accentInk / tone 3D3D3D).
    std::size_t litQuads{}, dimQuads{};
    const std::function<void(const d::Layer&)> visit = [&](const d::Layer& l) {
        if (std::string_view(l.id()).find("workMode.ticks") != std::string_view::npos && l.content() && (*l.content())["children"].isArray())
            for (const auto& part : (*l.content())["children"].array()) {
                std::size_t quads{};
                for (const auto& op : part["shape"]["path"].array()) quads += op["op"].string() == "move";
                (std::string_view(part["id"].string()).ends_with(".lit") ? litQuads : dimQuads) += quads;
            }
        for (const auto* c : l.children()) visit(*c);
    };
    visit(w.canvas->root());
    if (int(litQuads) != lit || int(dimQuads) != 120 - lit) run.failures.push_back(label + ": painted tick quads " + std::to_string(litQuads) + "/" + std::to_string(dimQuads));
    const auto* arc = findNamed(expected["layer"], "workMode.arc");
    const double strokeEnd = (*arc)["shape"]["strokeEnd"].number();
    const auto* right = findLayer(w.canvas->root(), "workMode.arc.right.half");
    const auto* left = findLayer(w.canvas->root(), "workMode.arc.left.half");
    const double shown = (right->value(Channel::rotation) + left->value(Channel::rotation)) / (2 * pi);
    if (!near(shown, strokeEnd, 1e-9)) run.failures.push_back(label + ": arc strokeEnd " + std::to_string(shown) + " vs " + std::to_string(strokeEnd));
    const auto* marker = findNamed(expected["layer"], "workMode.marker");
    const auto* orbit = findLayer(w.canvas->root(), "workMode.marker");
    const auto& t = (*marker)["transform"];
    const double macAngle = t.isArray() ? std::atan2(t.array()[0].array()[1].number(), t.array()[0].array()[0].number()) : 0;
    const double ours = std::remainder(orbit->value(Channel::rotation), 2 * pi);
    if (!near(std::remainder(macAngle - ours, 2 * pi), 0, 1e-6)) run.failures.push_back(label + ": marker angle");
    // Motion: the Mac's .6 s E arc and marker transitions (or none).
    const auto animations = [](const Json* n) { return n && (*n)["animations"].isArray() ? (*n)["animations"].array().size() : std::size_t(0); };
    const bool macMoving = animations(arc) > 0, ourMoving = right->animating(w.now) || left->animating(w.now);
    if (macMoving != ourMoving) run.failures.push_back(label + ": arc transition " + std::to_string(ourMoving));
    if (macMoving) {
        const auto& a = (*arc)["animations"].array()[0];
        check(near(a["duration"].number(), .6) && near(a["from"].number(), (right->presented(Channel::rotation, w.now) + left->presented(Channel::rotation, w.now)) / (2 * pi), 1e-9),
            label + ": the arc eases .6 s from the shown strokeEnd (:131)");
        const auto& turn = (*marker)["animations"].array()[0];
        check(near(turn["from"].number(), orbit->presented(Channel::rotation, w.now), 1e-9) && near(turn["to"].number(), orbit->value(Channel::rotation), 1e-9)
            && orbit->property[std::size_t(Channel::rotation)].duration == .6, label + ": the marker turns .6 s E from the shown fraction (:132-135)");
    }
}
void workExtras(Run& run, const std::string& label, const Json& e, WorkWorld& w) {
    const auto s = w.controller.snapshot(w.now);
    const auto text = w.canvas->timerText();
    if (!text || *text != e["timerText"].string()) run.failures.push_back(label + ": timer " + text.value_or("-") + " vs " + e["timerText"].string());
    if (!near(w.canvas->ringFraction().value_or(-1), e["ringFraction"].number(), 1e-9)) run.failures.push_back(label + ": ring fraction");
    std::vector<std::string> laps;
    for (const auto& lap : e["laps"].array()) laps.push_back(lap.string());
    if (w.canvas->lapTexts() != laps) run.failures.push_back(label + ": laps");
    if (w.canvas->editingCustom() != e["editingCustom"].boolean() || m::workModePhaseKey(s.phase) != e["phase"].string()
        || m::workModeKindKey(s.kind) != e["kind"].string() || s.displayedSeconds() != std::int64_t(e["displayedSeconds"].number()) || s.duration != e["duration"].number())
        run.failures.push_back(label + ": controller state");
    if (w.canvas->customField().text() != e["customField"]["text"].string()) run.failures.push_back(label + ": custom field text " + w.canvas->customField().text());
    const auto& toast = e["toast"];
    const auto& ours = w.harness.context.overlay().toastState().current();
    if (toast.isNull() != !ours || (ours && (ours->title != toast["title"].string() || ours->caption != toast["caption"].string() || d::iconName(ours->icon) != toast["icon"].string())))
        run.failures.push_back(label + ": toast " + (ours ? ours->title + " / " + ours->caption : std::string("none")));
}
void workMode(Run& run) {
    const auto snap = [&](const std::string& state, WorkWorld& w, L language = L::english, bool light = false) {
        const auto label = "workMode/" + state + "/" + tag(language) + (light ? "/light" : "");
        if (const Json* e = run.compareCanvas(label, "workMode", state, *w.canvas, w.controls(), language, light)) {
            if (e->contains("timerText")) workExtras(run, label, *e, w);
            if (e->contains("ringFraction")) ring(run, label, *e, w);
        }
    };
    for (L language : languages) { WorkWorld w(run.measurer, language); snap("countdownIdle", w, language); }
    { WorkWorld w(run.measurer, L::english, "22A6F2", true); snap("countdownIdle", w, L::english, true); }
    {
        WorkWorld w(run.measurer);
        w.canvas->primaryAction(); snap("countdownStarted", w);
        w.advance(61.3); snap("countdownRunning", w);
        // The blink: opacity 1, .2, 1 at 0, .5, 1 over 1.6 s easeInOut, forever, only while running (:283-291).
        const auto* dot = findLayer(w.canvas->root(), "workMode.state.dot");
        const auto& blink = dot->slots[std::size_t(d::MotionSlot::effect)][0];
        check(blink.present() && blink.keys == 3 && blink.values[1] == .2 && blink.keyTimes[1] == .5 && blink.duration == 1.6 && std::isinf(blink.repeat)
            && same(blink.outer, d::easing::easeInOut), "The FOCUS ON square blinks 1 -> .2 -> 1 over 1.6 s (:287-290)");
        w.advance(1); snap("countdownTick", w);
        check(w.key(d::key::space, U' '), "Space runs the primary action (:556-560)");
        snap("countdownPaused", w);
        check(!dot || !findLayer(w.canvas->root(), "workMode.state.dot")->animating(w.now), "A paused session shows a steady square");
        {
            const auto label = std::string("workMode/countdownPausedTabs");
            const Json* e = run.row("workMode", "countdownPausedTabs");
            std::vector<d::Control*> tabs;
            for (auto* c : w.canvas->page().controls()) if (c->id().starts_with("workMode.kind")) tabs.push_back(c);
            if (!e) run.failures.push_back(label + ": not in the oracle");
            else {
                const auto& theirs = (*e)["tabs"].array();
                check(theirs.size() == tabs.size(), "Two mode tabs");
                for (std::size_t i = 0; i < tabs.size(); ++i)
                    if (theirs[i]["enabled"].boolean() != tabs[i]->enabled()) run.failures.push_back(label + ": tab enabled " + tabs[i]->id());
                check(w.key(0x51, U'q') == (*e)["keyQ"].boolean(), "A locked mode bar is not the page's tabs: Q passes (:363)");
            }
        }
        check(w.key(d::key::enter, U'\r'), "Return resumes");
        w.advance(29 * 60); snap("countdownCompleted", w);
        check(!w.harness.context.overlay().toastState().current(), "Without a visible HUD window a finished countdown shows no toast (:261)");
    }
    {
        // The oracle has no window; with the HUD on screen (any module) the completion toasts.
        WorkWorld w(run.measurer);
        w.hudVisible = true;
        w.canvas->primaryAction(); w.advance(1801);
        const auto& toast = w.harness.context.overlay().toastState().current();
        check(toast && toast->title == "Session complete" && toast->icon == d::Icon::timer && toast->caption.empty(),
            "A countdown finishing while the HUD shows toasts Session complete (:255-263)");
    }
    {
        WorkWorld w(run.measurer);
        w.canvas->pointerMoved(w.centre(*w.control("workMode.preset:0")), false, w.now); snap("presetHovered", w);
        w.canvas->pointerMoved(std::nullopt, false, w.now);
        w.press("workMode.preset:0"); snap("preset5", w);
        w.press("workMode.preset:3"); snap("customOpen", w);
        check(w.canvas->page().back != nullptr, "While the custom field shows, Esc / back closes it (:326)");
        w.canvas->commitCustom("abc"); snap("customInvalid", w);
        const auto& shake = w.canvas->customField().layer().slots[std::size_t(d::MotionSlot::effect)][0];
        check(shake.present() && shake.additive && shake.duration == .32, "An invalid duration shakes the field (MODS-734)");
        w.canvas->commitCustom("0:04"); snap("customCommitted", w);
        w.canvas->openCustom(); w.canvas->commitCustom("4:00"); w.canvas->openCustom(); snap("customOpenMinutes", w);
        check(w.key(d::key::escape), "Esc closes the open field"); snap("customClosedByEscape", w);
        // Leaving commits the field (MODS-734): a typed duration lands, a running session ends.
        w.canvas->primaryAction();
        w.canvas->openCustom();
        w.canvas->customField().setText("12");
        check(w.controller.snapshot(w.now).phase == m::WorkModePhase::running, "Opening the field keeps the session running");
        w.canvas->commitCustom(w.canvas->customField().text());
        check(w.controller.snapshot(w.now).duration == 720 && w.controller.snapshot(w.now).phase == m::WorkModePhase::idle,
            "A new duration ends a running session (chooseCountdown, MODS-734)");
        w.canvas->openCustom(); w.canvas->customField().setText("12");
        w.canvas->setActive(false, w.now);
        check(!w.canvas->editingCustom() && w.controller.snapshot(w.now).duration == 720, "Leaving closes the field; the same duration does not restart");
    }
    {
        WorkWorld w(run.measurer);
        w.press("workMode.kind:1"); w.canvas->settleSwap(); snap("stopwatchIdle", w);
        w.canvas->primaryAction(); w.advance(3907.2);
        for (int i = 0; i < 4; ++i) { w.canvas->recordLap(); w.advance(1); }
        snap("stopwatchLaps", w);
        w.canvas->primaryAction(); snap("stopwatchPaused", w);
        w.canvas->reset(); snap("stopwatchReset", w);
    }
    {
        WorkWorld w(run.measurer);
        unsigned requests{};
        w.canvas->onRequestFocusAccess = [&] { ++requests; };
        w.canvas->setFocusStatusMessage("  Focus could not turn on  ", true); snap("focusPermission", w);
        w.press("workMode.focus");
        check(requests == 1, "The warning button asks for the accessibility settings while that permission is missing (:550-553)");
        w.canvas->setFocusStatusMessage("Focus could not turn on"); snap("focusMessage", w);
        w.canvas->setFocusStatusMessage("   "); snap("focusCleared", w);
    }
    {
        WorkWorld w(run.measurer);
        w.harness.reduce(true);
        w.canvas->primaryAction(); w.advance(2); snap("reduceMotionRunning", w);
        check(!w.canvas->requiresFrames(w.now), "Reduce Motion: no blink, no ring transition");
    }
    // The Mac's tables.
    for (const auto& r : run.data["workMode"]["durationTitles"].array()) {
        L language = L::english;
        for (L l : languages) if (tag(l) == r["language"].string()) language = l;
        check(m::WorkModeCanvas::durationTitle(r["seconds"].number(), language) == r["title"].string(), "durationTitle " + r["title"].string() + " (:300-304)");
    }
    for (const auto& r : run.data["workMode"]["timeTexts"].array()) {
        const m::WorkModeSnapshot s{r["kind"].string() == "countdown" ? m::WorkModeKind::countdown : m::WorkModeKind::stopwatch, m::WorkModePhase::idle,
            r["duration"].number(), r["elapsed"].number()};
        check(m::WorkModeCanvas::timeText(s) == r["text"].string() && near(m::workModeDial::fraction(s), r["fraction"].number(), 1e-9)
            && m::workModeDial::litTicks(m::workModeDial::fraction(s)) == int(r["lit"].number()), "MM:SS with unbounded minutes and the ring fraction: " + r["text"].string() + " (MODS-724, MODS-732)");
    }
    {
        WorkWorld w(run.measurer);
        check(near(w.canvas->hitBounds(), rectOf(run.data["workMode"]["hitBounds"])), "Input reaches the whole 620 px ring (MODS-722)");
        // WorkModeHUDVerification: the page's workMode.ring layer is 620 px wide, drawn 1:1 around the disc (MODS-744).
        const auto* ring = findLayer(w.canvas->root(), "workMode.ring");
        const auto* macRing = findNamed((*run.row("workMode", "countdownIdle"))["layer"], "workMode.ring");
        check(ring && macRing && near(ring->frame(), rectOf((*macRing)["frame"])) && ring->frame().width == 2 * m::workModeDial::radius,
            "The full 620 px ring around the disc (MODS-744)");
        // Every tick quad equals the Mac's.
        const auto& mac = run.data["workMode"]["ticks"].array();
        const auto ours = m::workModeDial::ticks(0, 120).json().array();
        check(mac.size() == ours.size(), "120 tick quads");
        for (std::size_t i = 0; i < mac.size(); ++i) {
            const auto& a = mac[i]["points"].array(); const auto& b = ours[i]["points"].array();
            check(mac[i]["op"].string() == ours[i]["op"].string() && a.size() == b.size(), "Tick path ops");
            for (std::size_t k = 0; k < a.size(); ++k) check(near(a[k].array()[0].number(), b[k].array()[0].number(), 1e-6) && near(a[k].array()[1].number(), b[k].array()[1].number(), 1e-6), "Tick quad corners (:31-44)");
        }
    }
}

// ---------------------------------------------------------------------------
// Calendar
// ---------------------------------------------------------------------------
struct Repository final : m::CalendarRepository {
    m::CalendarFile value;
    bool exists() override { return !value.events.empty(); }
    m::CalendarFile load() override { return value; }
    void save(const m::CalendarFile& v) override { value = v; }
};
struct Queue {
    struct Job { std::function<void()> work; std::function<void(std::exception_ptr)> done; };
    std::deque<Job> jobs;
    m::CalendarExecutor executor() { return {[this](auto work, auto done) { jobs.push_back({std::move(work), std::move(done)}); return true; }}; }
    void drain() {
        for (unsigned n = 0; !jobs.empty(); ++n) {
            check(n < 256, "Synthetic Calendar FIFO settles");
            auto j = std::move(jobs.front()); jobs.pop_front();
            std::exception_ptr e;
            try { j.work(); } catch (...) { e = std::current_exception(); }
            j.done(e);
        }
    }
};
m::CalendarTextRules rules() {
    const auto characters = d::portableCharacterRules();
    m::CalendarTextRules r;
    r.characters = characters.count;
    r.trimmed = [](std::string_view s) { const auto b = s.find_first_not_of(" \t\r\n"); return b == s.npos ? std::string() : std::string(s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1)); };
    r.prefix = characters.prefix;
    return r;
}
std::string uuid(int n) { char b[48]; std::snprintf(b, sizeof b, "C0000000-0000-4000-8000-%012d", n); return b; }
struct CalendarWorld {
    Harness harness;
    Queue queue;
    std::shared_ptr<Repository> repository = std::make_shared<Repository>();
    m::CalendarPermission permission{m::CalendarPermission::authorized};
    double now = *m::calendarUTC().timestamp({2026, 10, 4}, 10, 0);
    m::CalendarCanvas* canvasPointer{};
    unsigned ids{};
    std::unique_ptr<m::CalendarState> state;
    std::unique_ptr<m::CalendarCanvas> canvas;
    CalendarWorld(OracleMeasurer& measurer, const Json& data, bool events, m::CalendarPermission p = m::CalendarPermission::authorized,
        L language = L::english, std::string accent = "FAD41F", bool light = false)
        : harness(measurer, language, std::move(accent), light), permission(p) {
        if (events) {
            const auto add = [&](int n, m::CalendarDay day, std::string title, std::string details) {
                m::CalendarEvent e; e.id = uuid(n); e.title = std::move(title); e.details = std::move(details); e.day = day; e.created = now + n; e.modified = now + n;
                repository->value.events.push_back(std::move(e));
            };
            add(1, {2026, 10, 4}, "Planning review", "");
            add(2, {2026, 10, 4}, "Design sync with the Endfield operations team about the new centre layout", "Room 4\nBring the printed plans\rand the notes");
            add(3, {2026, 10, 4}, "Gym", "Leg day");
            add(4, {2026, 10, 15}, "Dentist", "09:30");
            add(5, {2026, 11, 2}, "Trip", "");
            add(6, {2026, 9, 30}, "Report", "Quarterly");
        }
        m::CalendarStateOptions o;
        o.text = rules();
        o.now = [this] { return now; };
        o.zone = [] { return m::calendarUTC(); };
        o.newID = [this] { return uuid(100 + int(++ids)); };
        // Delivered later, as the Mac's RunLoop.main.perform and the native notification adapter do.
        o.scheduling.authorization = [this](bool, auto done) { queue.jobs.push_back({[] {}, [this, done](std::exception_ptr) { done(permission); }}); };
        o.scheduling.reconcile = [](auto, auto, auto done) { done({}); };
        o.scheduling.cancelObsolete = [](auto, auto done) { done(); };
        o.changed = [this] { if (canvasPointer) canvasPointer->controllerChanged(); };
        state = std::make_unique<m::CalendarState>(repository, queue.executor(), std::move(o));
        m::CalendarCanvasOptions options;
        const auto& formats = data["calendar"]["formats"].array();
        const auto table = [&formats](L l) -> const Json& { for (const auto& f : formats) if (f["language"].string() == tag(l)) return f; return formats[0]; };
        options.formats.format = [table](m::CalendarDay day, std::string_view skeleton, L l) {
            const auto& days = table(l)["days"];
            return days.contains(day.string()) ? days[day.string()][skeleton].string() : day.string();
        };
        options.formats.shortWeekdays = [table](L l) { std::array<std::string, 7> out; for (std::size_t i = 0; i < 7; ++i) out[i] = table(l)["weekdays"].array()[i].string(); return out; };
        options.formats.shortMonths = [table](L l) { std::array<std::string, 12> out; for (std::size_t i = 0; i < 12; ++i) out[i] = table(l)["months"].array()[i].string(); return out; };
        const unsigned first = unsigned(data["calendar"]["firstWeekday"].number());
        options.formats.firstWeekday = [first] { return first; };
        options.text = rules();
        canvas = std::make_unique<m::CalendarCanvas>(harness.context, *state, std::move(options));
        canvasPointer = canvas.get();
        harness.context.env().now = 10;
        canvas->show(10);
        canvas->setActive(true, 10);
        queue.drain();
    }
    ~CalendarWorld() { canvasPointer = nullptr; }
    std::vector<d::Control*> controls() {
        std::vector<d::Control*> list = canvas->header().controls();
        list.insert(list.end(), canvas->page().controls().begin(), canvas->page().controls().end());
        list.push_back(&canvas->titleField()); list.push_back(&canvas->detailsField());
        return list;
    }
    d::Control* control(std::string_view id) const {
        for (auto* c : canvas->page().controls()) if (c->id() == id) return c;
        return nullptr;
    }
    d::Point centre(const d::Control& c) const { const auto r = c.rect(canvas->root()); return {r.x + r.width / 2, r.y + r.height / 2}; }
    bool key(std::uint32_t vk) { d::KeyInput k; k.virtualKey = vk; return canvas->keyDown(k, 10); }
};
void calendarExtras(Run& run, const std::string& label, const Json& e, CalendarWorld& w) {
    if (w.canvas->selectedDay().string() != e["selected"].string() || w.canvas->month().string() != e["month"].string())
        run.failures.push_back(label + ": selected " + w.canvas->selectedDay().string() + " month " + w.canvas->month().string());
    if ((e["editing"].isNull() != !w.canvas->editingID()) || (w.canvas->editingID() && *w.canvas->editingID() != e["editing"].string())) run.failures.push_back(label + ": editing");
    if (w.canvas->titleField().text() != e["titleField"]["text"].string() || w.canvas->detailsField().text() != e["detailsField"]["text"].string())
        run.failures.push_back(label + ": field text '" + w.canvas->detailsField().text() + "'");
    if (w.state->busy() != e["busy"].boolean() || std::int64_t(w.state->events().size()) != std::int64_t(e["events"].number())) run.failures.push_back(label + ": state");
    const auto& toast = e["toast"];
    const auto& ours = w.harness.context.overlay().toastState().current();
    if (toast.isNull() != !ours || (ours && (ours->title != toast["title"].string() || ours->caption != toast["caption"].string() || d::iconName(ours->icon) != toast["icon"].string())))
        run.failures.push_back(label + ": toast " + (ours ? ours->title + " / " + ours->caption : std::string("none")));
}
void calendar(Run& run) {
    const auto snap = [&](const std::string& state, CalendarWorld& w, L language = L::english, bool light = false) {
        const auto label = "calendar/" + state + "/" + tag(language) + (light ? "/light" : "");
        if (const Json* e = run.compareCanvas(label, "calendar", state, *w.canvas, w.controls(), language, light)) calendarExtras(run, label, *e, w);
    };
    for (L language : languages) { CalendarWorld w(run.measurer, run.data, false, m::CalendarPermission::authorized, language); snap("empty", w, language); }
    {
        CalendarWorld w(run.measurer, run.data, true);
        snap("events", w);
        {
            CalendarWorld light(run.measurer, run.data, true, m::CalendarPermission::authorized, L::english, "E5383B", true);
            snap("events", light, L::english, true);
            const auto* node = findField(*light.canvas, "calendar.title").content() ? &*findField(*light.canvas, "calendar.title").content() : nullptr;
            check(node && near(*rgbaOf((*node)["backgroundColor"]["sRGB"]), light.harness.context.env().palette.token(d::ThemeToken::field), 1e-6),
                "The Calendar fields follow the Light palette (the Mac's lazy fields keep their first theme: not copied)");
        }
        w.canvas->pointerMoved(w.centre(*w.control("calendar.day:2026-10-15")), false, 10); snap("dayHovered", w);
        w.canvas->pointerMoved(std::nullopt, false, 10);
        w.canvas->perform("day:2026-10-15"); snap("otherDay", w);
        check(w.key(d::key::right) && w.key(d::key::right), "Right moves the selected day (MODS-657)"); snap("arrowRight", w);
        w.canvas->perform("today");
        w.canvas->perform("event:" + uuid(2)); snap("editing", w);
        w.canvas->perform("delete");
        {
            const Json* e = run.row("calendar", "deleteConfirm");
            const auto& request = w.harness.context.overlay().confirmState().request();
            check(e && request && request->id == (*e)["confirm"]["id"].string() && request->title == (*e)["confirm"]["title"].string()
                && request->message == (*e)["confirm"]["message"].string() && request->confirmTitle == (*e)["confirm"]["confirmTitle"].string()
                && request->danger && request->glyph() == d::Icon::trash, "Delete asks on the confirm bar (MODS-659)");
        }
        w.harness.context.overlay().dismissConfirm();
        w.canvas->perform("event:" + uuid(2)); snap("editingClosed", w);
        w.canvas->perform("add"); snap("addEmptyTitle", w);
        w.canvas->perform("reminders"); snap("remindersScheduled", w);
        w.queue.drain();
        w.canvas->perform("nextMonth"); w.canvas->settleSwap(); snap("nextMonth", w);
        w.canvas->perform("monthPicker");
        const auto popover = [&](const std::string& state) {
            const Json* e = run.row("calendar", state);
            if (!e) { run.failures.push_back("calendar/" + state + ": not in the oracle"); return; }
            const Json& top = (*e)["top"];
            // The pop-over's own tree: the top layer of both.
            Json row = Json::Object{{"layer", top}};
            endfield::tests::design_oracle::compare("calendar/" + state + "/top", row, w.canvas->top(), w.canvas->root(), {}, run.failures);
        };
        snap("monthPicker", w); popover("monthPicker");
        {
            std::vector<d::AccessibilityItem> items; w.canvas->accessibilityItems(items);
            const auto it = std::find_if(items.begin(), items.end(), [](const auto& i) { return i.id == "calendar.nextYear"; });
            check(it != items.end(), "The picker's year stepper is reachable");
            const d::Point p{it->rect.x + it->rect.width / 2, it->rect.y + it->rect.height / 2};
            w.canvas->pointerDown(p, {p}, 10); w.canvas->pointerUp(p, 10);
        }
        snap("pickerNextYear", w); popover("pickerNextYear");
        w.canvas->perform("month:2027-02"); w.canvas->settleSwap(); snap("pickedFebruary2027", w);
        w.canvas->perform("month:2026-10"); w.canvas->settleSwap(); snap("pickedToday", w);
    }
    {
        CalendarWorld w(run.measurer, run.data, false, m::CalendarPermission::denied);
        w.canvas->perform("reminders"); w.queue.drain(); snap("remindersDenied", w);
        for (const auto& [month, name] : {std::pair{"1900-01", "firstMonth"}, std::pair{"9999-12", "lastMonth"}, std::pair{"2024-02", "leapFebruary"}}) {
            w.canvas->perform(std::string("month:") + month); w.canvas->settleSwap(); snap(name, w);
        }
        w.canvas->perform("reminders");
        const auto& toast = w.harness.context.overlay().toastState().current();
        check(toast && toast->title == "Reminders off" && toast->caption == m::calendarReminderStatus(m::CalendarPermission::denied, L::english)
            && toast->caption == "Notifications are off in Windows Settings", "Reminders off shows the Windows notification wording (MODS-660)");
    }
    {
        CalendarWorld w(run.measurer, run.data, true);
        w.canvas->titleField().setText("Synthetic new event");
        w.canvas->perform("add"); snap("busy", w);
        w.queue.drain(); snap("added", w);
    }
    {
        // MODS-661 (Mac authority, data-visible): saving an opened multi-line event stores its details joined with spaces.
        CalendarWorld w(run.measurer, run.data, true);
        w.canvas->perform("event:" + uuid(2));
        w.canvas->perform("save"); w.queue.drain();
        const auto events = w.state->events();
        const auto it = std::find_if(events.begin(), events.end(), [](const auto& e) { return e.id == uuid(2); });
        check(it != events.end() && it->details == "Room 4 Bring the printed plans and the notes" && !w.canvas->editingID(),
            "Saving keeps the event's day and flattens its details' line breaks (MODS-658, MODS-661)");
        const auto& toast = w.harness.context.overlay().toastState().current();
        check(toast && toast->title == "Event saved" && toast->caption == it->title, "Event saved toast (MODS-659)");
        // An emptied title keeps the previous one.
        w.canvas->perform("event:" + uuid(1)); w.canvas->titleField().setText("   "); w.canvas->perform("save"); w.queue.drain();
        check(std::any_of(w.state->events().begin(), w.state->events().end(), [](const auto& e) { return e.id == uuid(1) && e.title == "Planning review"; }),
            "An emptied title keeps the previous title (:446-447)");
        // A controller error toasts once while active (:87-92).
        w.state->report(m::CalendarErrorCode::capacity);
        const auto& error = w.harness.context.overlay().toastState().current();
        check(error && error->title == "Calendar" && error->caption == m::calendarErrorMessage(m::CalendarErrorCode::capacity, L::english), "Controller errors toast while active");
        // Deleting through the bar: the toast pair "Event deleted" / 提醒已取消 (spec 3.17).
        w.canvas->perform("event:" + uuid(3)); w.canvas->perform("delete");
        w.harness.context.overlay().decide(d::ConfirmState::Decision::confirm);
        w.harness.context.advance(11);
        w.queue.drain();
        check(std::none_of(w.state->events().begin(), w.state->events().end(), [](const auto& e) { return e.id == uuid(3); }), "Confirmed delete removes the event");
        const auto& deleted = w.harness.context.overlay().toastState().current();
        check(deleted && deleted->title == "Event deleted" && deleted->icon == d::Icon::trash, "Event deleted toast");
    }
    {
        CalendarWorld zh(run.measurer, run.data, true, m::CalendarPermission::authorized, L::simplifiedChinese);
        zh.canvas->perform("event:" + uuid(3)); zh.canvas->perform("delete");
        zh.harness.context.overlay().decide(d::ConfirmState::Decision::confirm);
        zh.harness.context.advance(11); zh.queue.drain();
        const auto& deleted = zh.harness.context.overlay().toastState().current();
        check(deleted && deleted->title == "提醒已取消" && deleted->caption == "Event deleted", "The Chinese delete toast reads 提醒已取消 (MODS-659, catalog pair)");
    }
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Usage: work_mode_calendar_canvas_tests <work-mode-calendar-source.json>");
        const auto data = loadOracle(argv[1], {"rows"}, {"text", "layerDefaults", "workMode", "calendar"});
        check(data.has_value(), "The Mac oracle is present");
        OracleMeasurer measurer;
        load(*data, measurer);
        Run run{*data, measurer, {}};
        workMode(run);
        calendar(run);
        for (const auto& f : run.failures) std::cerr << "MISMATCH " << f << '\n';
        for (const auto& miss : std::set<std::string>(measurer.recorded.misses.begin(), measurer.recorded.misses.end())) std::cerr << "UNRECORDED " << miss << '\n';
        if (!measurer.lineMisses.empty()) std::cerr << measurer.lineMisses.size() << " unrecorded line counts, first: " << measurer.lineMisses.front() << '\n';
        check(run.failures.empty(), std::to_string(run.failures.size()) + " oracle mismatches");
        std::cout << "PASS " << checks << " Work Mode / Calendar checks, " << run.compared << " Mac states compared\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
