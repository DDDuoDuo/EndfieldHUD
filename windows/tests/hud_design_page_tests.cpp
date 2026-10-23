// Design library L3 contracts (LIBRARY-DESIGN.md section 14 item 5): the
// shared overlay (Sources/HUDDesignOverlay.swift) and the canvas / page /
// context (Sources/HUDDesignScreen.swift) against the Mac oracle
// (tests/fixtures/hud-design-source.json, tools/hud_design_reference.*):
//  - the overlay's decision traces (Esc / Return / keypad Enter, the first
//    decision counts, Cancel at once and Confirm after 120 ms, keys swallowed
//    during the .18 s collapse, dismiss without callback, a toast during a
//    confirm dropped, a new toast replacing the current one, a confirm
//    clearing the toast, the dim while either shows, twin-pill outside press,
//    countdown, reset), step by step;
//  - the confirm bar's geometry for every recorded request, theme and
//    language (column width, the title rule <= 3 lines at 17 px else 14 px
//    and 4 lines past (column - 22) x 2.7, line boxes, pill frames), with the
//    Mac's recorded text widths;
//  - the result toast helper's English-only caption rule in five languages;
//  - a fake module canvas driven through the oracle's script: lifecycle,
//    hover routing, pointer / wheel / key precedence, menus and pop-overs,
//    sub-navigation with retarget and retained offsets, settle concealed,
//    Reduce Motion, stale rebuilds on palette / language, Recording mode and
//    render scale.
// The Mac oracle drives its swap with a real run loop; here time is the
// explicit clock (Context::advance). Q4 builds the next page when a swap
// starts, so build COUNTS differ from the Mac's; rebuild DECISIONS are compared.
#include "modules/hud_design_page.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace {
namespace d = endfield::modules::design;
using J = ehud::data::Json;
using d::Point;
using d::Rect;
using endfield::core::Language;
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) <= e; }
std::string str(Rect r) { std::ostringstream s; s << "(" << r.x << ", " << r.y << ", " << r.width << ", " << r.height << ")"; return s.str(); }
Rect rect(const J& a) { return {a.array()[0].number(), a.array()[1].number(), a.array()[2].number(), a.array()[3].number()}; }
bool near(Rect a, Rect b, double e = 1e-6) { return near(a.x, b.x, e) && near(a.y, b.y, e) && near(a.width, b.width, e) && near(a.height, b.height, e); }
std::string fixed1(double v) { std::ostringstream s; s.setf(std::ios::fixed); s.precision(1); s << v; return s.str(); }
std::string languageTag(Language l) {
    switch (l) {
    case Language::simplifiedChinese: return "zh-Hans";
    case Language::traditionalChinese: return "zh-Hant";
    case Language::japanese: return "ja";
    case Language::korean: return "ko";
    default: return "en";
    }
}
Language languageNamed(std::string_view tag) {
    if (tag == "zh-Hans") return Language::simplifiedChinese;
    if (tag == "zh-Hant") return Language::traditionalChinese;
    if (tag == "ja") return Language::japanese;
    if (tag == "ko") return Language::korean;
    return Language::english;
}

// ---------------------------------------------------------------------------
// Fixture: the document is larger than one parse allows; take its top-level sections.
// ---------------------------------------------------------------------------
std::size_t skipValue(std::string_view t, std::size_t i) {
    int depth = 0;
    for (; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '"') { for (++i; i < t.size() && t[i] != '"'; ++i) if (t[i] == '\\') ++i; if (depth == 0) return i + 1; continue; }
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') { if (--depth == 0) return i + 1; }
        else if (depth == 0 && (c == ',' || c == '}')) return i;
    }
    return i;
}
std::map<std::string, std::string> sections(const std::string& text) {
    std::map<std::string, std::string> out;
    std::size_t i = text.find('{') + 1;
    while (i < text.size()) {
        const auto keyStart = text.find('"', i);
        if (keyStart == std::string::npos) break;
        const auto keyEnd = text.find('"', keyStart + 1);
        const auto valueStart = text.find(':', keyEnd) + 1;
        const auto valueEnd = skipValue(text, valueStart);
        out[text.substr(keyStart + 1, keyEnd - keyStart - 1)] = text.substr(valueStart, valueEnd - valueStart);
        i = text.find_first_of(",}", valueEnd);
        if (i == std::string::npos || text[i] == '}') break;
        ++i;
    }
    return out;
}

// The Mac's recorded HUDText.width / metrics (text.widths, text.metrics); a
// width the oracle never measured falls back to an estimate and is reported.
struct RecordedMeasurer final : d::TextMeasurer {
    std::map<std::string, double> widths;
    std::map<std::string, d::LineMetrics> metric;
    std::vector<std::string> misses;
    static std::string number(double v) { std::ostringstream s; s.precision(10); s << (std::round(v * 1e6) / 1e6); return s.str(); }
    static std::string key(std::span<const d::TextRun> runs, d::TextRegion region) {
        std::string s(d::regionTag(region));
        for (const auto& r : runs) if (!r.text.empty())
            s += '|' + r.text + '|' + number(r.style.size) + '|' + std::to_string(int(d::resolved(r.style.weight))) + '|' + number(r.style.tracking) + '|' + (r.style.tabular ? "t" : "f");
        return s;
    }
    static std::string metricKey(double size, int weight, double lineHeight, bool tabular, std::string_view language) {
        return number(size) + '|' + std::to_string(weight) + '|' + number(lineHeight) + '|' + (tabular ? "t" : "f") + '|' + std::string(language);
    }
    double width(std::span<const d::TextRun> runs, d::TextRegion region) override {
        // Runs may arrive authored or displayed: try both spellings.
        const auto k = key(runs, region);
        if (const auto it = widths.find(k); it != widths.end()) return it->second;
        std::vector<d::TextRun> upper(runs.begin(), runs.end());
        for (auto& r : upper) if (r.style.uppercase) r.text = d::asciiUppercase(r.text);
        if (const auto it = widths.find(key(upper, region)); it != widths.end()) return it->second;
        misses.push_back(k);
        double w = 0;
        for (const auto& r : runs) w += double(d::portableCharacterRules().count(r.text)) * r.style.size * (.55 + r.style.tracking);
        return std::ceil(w * 2) / 2;
    }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion region, double) override {
        const auto k = metricKey(s.size, int(d::resolved(s.weight)), s.lineHeight, s.tabular, d::regionTag(region));
        if (const auto it = metric.find(k); it != metric.end()) return it->second;
        misses.push_back("metrics " + k);
        return {s.size * s.lineHeight, s.size * .8, s.size * .2};
    }
    void load(const J& text) {
        for (const auto& entry : text["widths"].array()) {
            std::vector<d::TextRun> runs;
            for (const auto& r : entry["runs"].array()) {
                d::TextStyle s; s.size = r["size"].number(); s.weight = d::FontWeight(int(r["weight"].number()));
                s.tracking = r["tracking"].number(); s.tabular = r["tabular"].boolean();
                runs.push_back({r["displayed"].string(), s});
            }
            if (const auto region = d::regionFromTag(entry["language"].string())) widths[key(runs, *region)] = entry["width"].number();
        }
        for (const auto& m : text["metrics"].array())
            metric[metricKey(m["size"].number(), int(m["weight"].number()), m["lineHeight"].number(), m["tabular"].boolean(), m["language"].string())]
                = {m["height"].number(), m["ascent"].number(), m["descent"].number()};
    }
};

d::KeyInput keyOf(std::uint32_t vk, char32_t c = 0, bool shift = false, bool control = false) {
    d::KeyInput k; k.virtualKey = vk; k.character = c; k.shift = shift; k.control = control; return k;
}
// Mac key codes used by the oracle: 0 'a', 49 space, 53 Esc, 36 Return, 76 keypad Enter.
d::KeyInput macKey(int code) {
    switch (code) {
    case 53: return keyOf(d::key::escape);
    case 36: return keyOf(d::key::enter);
    case 76: { auto k = keyOf(d::key::enter); k.extended = true; return k; }
    case 49: return keyOf(d::key::space, U' ');
    default: return keyOf('A', U'a');
    }
}
const d::Layer* findLayer(const d::Layer& root, std::string_view id) {
    if (root.id() == id) return &root;
    for (const d::Layer* child : root.children()) if (const auto* hit = findLayer(*child, id)) return hit;
    return nullptr;
}

struct World {
    RecordedMeasurer& measurer;
    d::Context context;
    double now{100};
    explicit World(RecordedMeasurer& m) : measurer(m), context(m, d::asciiUppercase) { context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); }
    void appearance(Language language, std::string accent = "FAD41F", bool light = false, bool reduce = false, bool recording = false) {
        d::Context::Appearance a; a.language = language; a.accentHex = std::move(accent); a.light = light; a.reduceMotion = reduce; a.recordingMode = recording;
        context.apply(a, now);
    }
    void advance(double seconds) { now += seconds; context.advance(now); }
};

// ---------------------------------------------------------------------------
// The overlay's decision traces (overlay.trace.*)
// ---------------------------------------------------------------------------
struct Trace {
    World& w;
    d::CenterOverlay& o;
    std::vector<std::string> events;
    std::vector<J> rows;
    std::size_t index{};
    std::string name;
    Trace(World& world, const J& expected, std::string traceName) : w(world), o(world.context.overlay()), rows(expected.array()), name(std::move(traceName)) {
        o.onChange = [this] { events.push_back("onChange"); };
    }
    ~Trace() { o.onChange = nullptr; o.reset(); }
    // State fields that do not depend on how the clock interleaves the 120 ms callback and the .18 s collapse.
    void step(const std::string& label) {
        check(index < rows.size(), name + ": more steps than the Mac trace");
        const J& m = rows[index++];
        const std::string where = name + "/" + label;
        check(m["step"].string() == label, where + ": step order differs from the Mac trace (" + m["step"].string() + ")");
        const auto& state = o.confirmState();
        check(o.isConfirming() == m["isConfirming"].boolean(), where + ": isConfirming");
        check(o.capturesInput() == m["capturesInput"].boolean(), where + ": capturesInput");
        check(o.dimmed() == m["dimmed"].boolean(), where + ": dimmed");
        check((state.request() ? J(state.request()->id) : J()) == m["request"], where + ": request");
        const J decision = state.decision() ? J(*state.decision() == d::ConfirmState::Decision::confirm ? "confirm" : "cancel") : J();
        check(decision == m["decision"], where + ": decision");
        check((o.toastState().current() ? J(o.toastState().current()->title) : J()) == m["toast"], where + ": toast");
        check(o.confirmBar().layer().hidden() == m["barHidden"].boolean(), where + ": bar hidden");
        check(o.toast().layer().hidden() == m["toastHidden"].boolean(), where + ": toast hidden");
        std::vector<std::string> router;
        for (const auto* c : o.router().controls()) router.push_back(c->id());
        std::vector<std::string> expectedRouter;
        for (const auto& id : m["router"].array()) expectedRouter.push_back(id.string());
        check(router == expectedRouter, where + ": routed controls");
        const auto dim = findLayer(o.layer(), "overlay.center.dim");
        check(dim && near(dim->value(d::Channel::opacity), m["dimOpacity"].number()), where + ": dim model opacity");
    }
    // Every callback and onChange the Mac saw, in any order (the clock interleaves them differently).
    void finish() {
        check(index == rows.size(), name + ": fewer steps than the Mac trace");
        std::vector<std::string> mac;
        for (const auto& e : rows.back()["events"].array()) mac.push_back(e.string());
        auto ours = events;
        std::sort(mac.begin(), mac.end()); std::sort(ours.begin(), ours.end());
        std::string list; for (const auto& e : events) list += e + "; ";
        check(ours == mac, name + ": callbacks and onChange calls differ from the Mac: " + list);
    }
    std::string key(int code) {
        const bool used = o.handleKey(macKey(code));
        const std::string label = code == 0 ? "a" : code == 49 ? "space" : code == 53 ? "esc" : "return";
        return "key " + label + " -> " + (used ? "true" : "false");
    }
};

d::ConfirmRequest plainRequest() { d::ConfirmRequest r; r.id = "q"; r.title = "Question?"; r.message = "Detail"; r.confirmTitle = "Do it"; return r; }
d::ConfirmRequest twinRequest() {
    d::ConfirmRequest r; r.id = "twin"; r.title = "Disable?"; r.confirmTitle = "Disable"; r.cancelTitle = "Disable"; r.icon = d::Icon::power;
    r.style = d::ConfirmRequest::Style::alert; r.cancelPillConfirms = true; return r;
}
d::ConfirmRequest countdownRequest(World& w) {
    d::ConfirmRequest r; r.id = "layout"; r.title = w.context.text("Keep this UI scale?", "保留此缩放？"); r.countdown = 12;
    r.confirmTitle = w.context.text("Keep", "保留"); r.cancelTitle = w.context.text("Revert", "恢复"); r.icon = d::Icon::check; return r;
}
d::ToastRequest toastOf(std::string title, d::Icon icon = d::Icon::check) { d::ToastRequest t; t.icon = icon; t.title = std::move(title); return t; }
Point pillCentre(d::CenterOverlay& o, std::size_t index) {
    const Rect bar = o.confirmBar().layer().frame(), pill = o.confirmBar().pills()[index]->layer().frame();
    return {bar.x + pill.x + pill.width / 2, bar.y + pill.y + pill.height / 2};
}

void overlayTraces(RecordedMeasurer& measurer, const J& overlay) {
    const auto completions = [](World& w) { w.advance(.25); };   // the .18 s collapse / .24 s fade (CA completion blocks)
    const auto scheduled = [](World& w) { w.advance(1.6); };     // the 120 ms callback and the 1.5 s toast timer
    {
        World w(measurer); w.appearance(Language::english);
        Trace t(w, overlay["trace.returnConfirms"], "returnConfirms");
        t.o.confirm(plainRequest(), [&] { t.events.push_back("confirm"); }, [&] { t.events.push_back("cancel"); }); t.step("confirm");
        t.o.handleKey(macKey(36)); t.step("return");
        t.events.push_back(t.key(0)); t.step("swallowedDuringCollapse");
        t.events.push_back(t.key(53)); t.step("escAfterDecision");
        completions(w); t.step("collapseDone");
        t.events.push_back(t.key(0)); t.step("keysPassAfterCollapse");
        scheduled(w); t.step("after120ms");
        check(std::count(t.events.begin(), t.events.end(), "confirm") == 1, "returnConfirms: the confirm callback ran once");
        t.finish();
    }
    {
        World w(measurer); w.appearance(Language::english);
        Trace t(w, overlay["trace.keypadEnterConfirms"], "keypadEnterConfirms");
        t.o.confirm(plainRequest(), [&] { t.events.push_back("confirm"); }); t.step("confirm");
        t.o.handleKey(macKey(76));
        check(std::count(t.events.begin(), t.events.end(), "confirm") == 0, "Confirm calls back after 120 ms, not at once");
        w.advance(.119);
        check(std::count(t.events.begin(), t.events.end(), "confirm") == 0, "Not before 120 ms");
        w.advance(.002); t.step("keypadEnter");
        t.finish();
    }
    {
        World w(measurer); w.appearance(Language::english);
        Trace t(w, overlay["trace.escCancels"], "escCancels");
        t.o.confirm(plainRequest(), [&] { t.events.push_back("confirm"); }, [&] { t.events.push_back("cancel"); }); t.step("confirm");
        t.events.push_back(t.key(49)); t.step("otherKeySwallowed");
        t.o.handleKey(macKey(53));
        check(t.events.back() == "cancel", "Cancel calls back at once");
        t.step("esc");
        t.events.push_back(std::string("decide again -> ") + (t.o.decide(d::ConfirmState::Decision::confirm) ? "true" : "false")); t.step("secondDecisionIgnored");
        completions(w); t.step("collapseDone");
        t.finish();
    }
    {
        World w(measurer); w.appearance(Language::english, "FAD41F", false, true);
        Trace t(w, overlay["trace.reduceMotionConfirmsAtOnce"], "reduceMotionConfirmsAtOnce");
        t.o.confirm(plainRequest(), [&] { t.events.push_back("confirm"); }); t.step("confirm");
        t.o.handleKey(macKey(36)); t.step("return");
        t.finish();
    }
    {
        World w(measurer); w.appearance(Language::english);
        Trace t(w, overlay["trace.dismissNoCallback"], "dismissNoCallback");
        t.o.confirm(plainRequest(), [&] { t.events.push_back("confirm"); }, [&] { t.events.push_back("cancel"); }); t.step("confirm");
        t.o.dismissConfirm(); t.step("dismiss");
        t.o.dismissConfirm(); t.step("dismissAgain");
        w.advance(2);
        t.finish();
    }
    {
        World w(measurer); w.appearance(Language::english);
        Trace t(w, overlay["trace.toastReplaceAndExpire"], "toastReplaceAndExpire");
        t.o.showToast(toastOf("One"), [&] { t.events.push_back("toast one done"); }); t.step("toast1");
        w.advance(1);
        auto two = toastOf("Two", d::Icon::copy); two.caption = "two";
        t.o.showToast(two, [&] { t.events.push_back("toast two done"); }); t.step("toast2Replaces");
        w.advance(.6); t.step("firstTimerFiresIgnored");                 // toast one's 1.5 s passed: its timer was replaced
        w.advance(.95); t.step("secondTimerFires");
        completions(w); t.step("hideCompleted");
        t.finish();
    }
    {
        World w(measurer); w.appearance(Language::english);
        Trace t(w, overlay["trace.toastDroppedDuringConfirm"], "toastDroppedDuringConfirm");
        t.o.confirm(plainRequest(), [&] { t.events.push_back("confirm"); }); t.step("confirm");
        t.o.showToast(toastOf("Dropped"), [&] { t.events.push_back("dropped done"); }); t.step("toastDropped");
        t.o.handleKey(macKey(53)); completions(w); t.step("cancelled");
        scheduled(w);
        t.finish();
    }
    {
        World w(measurer); w.appearance(Language::english);
        Trace t(w, overlay["trace.confirmClearsToast"], "confirmClearsToast");
        t.o.showToast(toastOf("Shown"), [&] { t.events.push_back("toast done"); }); t.step("toast");
        t.o.confirm(plainRequest(), [&] { t.events.push_back("confirm"); }); t.step("confirmClears");
        w.advance(1.6); t.step("staleToastTimer");
        t.o.handleKey(macKey(36)); t.step("return");
        t.o.showToast(toastOf("During collapse"), [&] { t.events.push_back("late toast done"); }); t.step("toastDuringCollapse");
        completions(w); t.step("collapseDone");
        w.advance(1.4); t.step("timersFire");
        completions(w); t.step("toastHidden");
        t.finish();
    }
    {
        World w(measurer); w.appearance(Language::english);
        Trace t(w, overlay["trace.twinPills"], "twinPills");
        auto& o = t.o;
        o.confirm(twinRequest(), [&] { t.events.push_back("confirm"); }, [&] { t.events.push_back("cancel"); }); t.step("confirm");
        const Point outside{295, 100};
        t.events.push_back(std::string("pointerDown outside -> ") + (o.router().pointerDown(outside, o.layer()) ? "true" : "false"));
        o.router().pointerUp(outside, o.layer()); t.step("outsidePressCancels");
        completions(w);
        o.confirm(twinRequest(), [&] { t.events.push_back("confirm"); }, [&] { t.events.push_back("cancel"); }); t.step("confirmAgain");
        const Point upper = pillCentre(o, 0);
        t.events.push_back(std::string("pointerDown upper pill -> ") + (o.router().pointerDown(upper, o.layer()) ? "true" : "false"));
        o.router().pointerUp(upper, o.layer()); w.advance(.13); t.step("upperPillConfirms");
        completions(w);
        o.confirm(plainRequest(), [&] { t.events.push_back("confirm"); }, [&] { t.events.push_back("cancel"); });
        t.events.push_back(std::string("pointerDown outside standard -> ") + (o.router().pointerDown(outside, o.layer()) ? "true" : "false")); t.step("standardHasNoOutside");
        const Point cancel = pillCentre(o, 0);
        o.router().pointerDown(cancel, o.layer()); o.router().pointerUp(cancel, o.layer()); t.step("cancelPill");
        t.finish();
    }
    {
        World w(measurer); w.appearance(Language::english);
        Trace t(w, overlay["trace.countdownAndReset"], "countdownAndReset");
        t.o.confirm(countdownRequest(w), [&] { t.events.push_back("confirm"); }); t.step("confirm");
        t.o.setCountdown(3); t.step("countdown3");
        check(t.o.confirmBar().messageText() == "3" && t.o.confirmState().request()->countdown == 3, "setCountdown updates the state and the bar");
        t.o.setCountdown(2); t.step("countdown2");
        t.o.showToast(toastOf("x")); t.step("toastDropped");
        t.o.reset(); t.step("reset");
        check(!t.o.filterNeeded(w.now) && t.o.dimFilter(w.now) == 0, "reset removes the content filter at once");
        t.o.setCountdown(1); t.step("countdownAfterReset");
        t.finish();
    }
    // HUDConfirmState as pure state.
    {
        d::ConfirmState state;
        World w(measurer); w.appearance(Language::english);
        const std::vector<std::pair<std::string, std::function<void()>>> steps{
            {"key before present", [&] { state.key(macKey(36)); }}, {"present", [&] { state.present(countdownRequest(w)); }},
            {"countdown 5", [&] { state.setCountdown(5); }}, {"key a", [&] { state.key(macKey(0)); }}, {"key esc", [&] { state.key(macKey(53)); }},
            {"key return", [&] { state.key(macKey(36)); }}, {"countdown 4", [&] { state.setCountdown(4); }}, {"close", [&] { state.close(); }},
            {"present plain", [&] { state.present(plainRequest()); }}, {"countdown 3 (none)", [&] { state.setCountdown(3); }}};
        const auto& rows = overlay["confirmState"].array();
        for (std::size_t i = 0; i < steps.size(); ++i) {
            steps[i].second();
            const J& m = rows[i];
            check(m["step"].string() == steps[i].first && state.isOpen() == m["isOpen"].boolean(), "ConfirmState " + steps[i].first + ": isOpen");
            const J decision = state.decision() ? J(*state.decision() == d::ConfirmState::Decision::confirm ? "confirm" : "cancel") : J();
            check(decision == m["decision"], "ConfirmState " + steps[i].first + ": decision");
            const J countdown = state.request() && state.request()->countdown ? J(*state.request()->countdown) : J();
            check(countdown.isNull() == m["countdown"].isNull() && (countdown.isNull() || countdown.number() == m["countdown"].number()), "ConfirmState " + steps[i].first + ": countdown");
        }
    }
    // HUDToastState.
    {
        d::ToastState state;
        const auto& m = overlay["toastState"];
        const auto a = state.show(toastOf("A")), b = state.show(toastOf("B"));
        check(J(std::int64_t(a)) == m["tokens"].array()[0] && J(std::int64_t(b)) == m["tokens"].array()[1], "ToastState tokens");
        check(state.expire(a) == m["expireA"].boolean() && state.current()->title == m["current"].string(), "Only the current token expires");
        check(state.expire(b) == m["expireB"].boolean() && state.expire(b) == m["expireBAgain"].boolean(), "A token expires once");
        check(double(state.generation()) == m["generation"].number(), "ToastState generation");
        state.clear();
        check(double(state.generation()) == m["afterClear"].number(), "clear advances the generation");
    }
}

// ---------------------------------------------------------------------------
// Confirm bar geometry (overlay.bars) and the toast's texts
// ---------------------------------------------------------------------------
void confirmBars(RecordedMeasurer& measurer, const J& overlay) {
    for (const auto& row : overlay["bars"].array()) {
        const std::string name = row["request"].string();
        const auto& s = row["summary"];
        const auto language = languageNamed(row["language"].string());
        World w(measurer);
        w.appearance(language, row["accent"].string(), row["light"].boolean());
        d::ConfirmRequest r;
        r.id = s["pills"].array()[0]["id"].string().substr(0, s["pills"].array()[0]["id"].string().rfind(':'));
        r.title = s["title"].string();
        r.cancelTitle = s["pills"].array()[0]["label"].string();
        r.confirmTitle = s["pills"].array()[1]["label"].string();
        if (name == "countdown") { r.countdown = 12; r.icon = d::Icon::check; }
        else r.message = s["message"].string();
        if (name == "standard") r.icon = d::Icon::reset;
        if (name == "danger" || name == "noMessage3Lines") r.danger = true;
        if (name == "danger") r.icon = d::Icon::trash;
        if (name == "alertTwin") { r.icon = d::Icon::power; r.style = d::ConfirmRequest::Style::alert; r.cancelPillConfirms = true; }
        auto& o = w.context.overlay();
        o.confirm(r, [] {});
        const std::string where = "Confirm bar " + name + " " + row["language"].string() + (row["light"].boolean() ? " light " : " dark ") + row["accent"].string();
        const auto& bar = o.confirmBar();
        const auto* column = findLayer(bar.layer(), "confirm.text");
        const auto* title = findLayer(bar.layer(), "confirm.title");
        const auto* message = findLayer(bar.layer(), "confirm.message");
        check(column && title && message, where + ": layers");
        check(near(column->frame().width, s["columnWidth"].number()), where + ": column width " + std::to_string(column->frame().width) + " vs " + std::to_string(s["columnWidth"].number()));
        check(near(bar.titleSize(), s["titleSize"].number()) && bar.titleLines() == int(s["titleLines"].number()),
            where + ": the title rule (" + std::to_string(bar.titleSize()) + " px, " + std::to_string(bar.titleLines()) + " lines)");
        check(near(d::textOf(*title).lineFrame, rect(s["titleLineFrame"])), where + ": title line box " + str(d::textOf(*title).lineFrame));
        check(near(d::textOf(*message).lineFrame, rect(s["messageLineFrame"])) && d::textOf(*message).maximumLines == int(s["messageLines"].number()), where + ": message line box");
        check(bar.titleText() == s["title"].string() && bar.messageText() == s["message"].string(), where + ": texts");
        const auto pills = bar.pills();
        for (std::size_t i = 0; i < 2; ++i) {
            const auto& p = s["pills"].array()[i];
            check(pills[i]->id() == p["id"].string() && pills[i]->label == p["label"].string(), where + ": pill identity");
            check(near(pills[i]->layer().frame(), rect(p["frame"])), where + ": pill frame " + str(pills[i]->layer().frame()) + " vs " + str(rect(p["frame"])));
        }
        std::vector<std::string> router;
        for (const auto* c : o.router().controls()) router.push_back(c->id());
        std::vector<std::string> expectedRouter;
        for (const auto& id : row["router"].array()) expectedRouter.push_back(id.string());
        check(router == expectedRouter, where + ": routed controls (twin requests add the outside press)");
        if (row.contains("afterCountdown9")) {
            o.setCountdown(9);
            check(bar.messageText() == row["afterCountdown9"]["message"].string(), where + ": countdown text");
        }
        if (row.contains("overlay")) {
            const auto& tree = row["overlay"];
            const auto& children = tree["children"].array();
            check(children.size() == 3 && children[0]["name"].string() == "center.dim", "Overlay children: dim, bar, toast");
            const auto* dim = findLayer(o.layer(), "overlay.center.dim");
            check(dim && near(dim->frame(), rect(children[0]["frame"])) && near(dim->zPosition(), children[0]["zPosition"].number()), "The 850 px dim circle on the disc, z 15");
            check(near(bar.layer().frame(), rect(children[1]["frame"])) && near(bar.layer().zPosition(), children[1]["zPosition"].number()), "The bar at box (-15, 370), z 20");
            check(near(o.toast().layer().frame(), rect(children[2]["frame"])) && near(o.toast().layer().zPosition(), children[2]["zPosition"].number()), "The toast at box (-5, 190), z 25");
            check(near(column->frame().y, 18) && near(column->frame().x, 46), "The text column at (46, 18)");
        }
        o.reset();
    }
}

// ---------------------------------------------------------------------------
// The fake module canvas (the oracle's FakeCanvas, line for line)
// ---------------------------------------------------------------------------
class FakeCanvas final : public d::Canvas {
public:
    using Canvas::Canvas;
    int depth{}, tab{}, rows{12}, builds{};
    std::vector<std::string> events, keys;
    d::Dropdown* dropdown{};
    d::Slider* slider{};
    void buildPage(d::Page& page) override {
        ++builds;
        auto& action = page.make<d::Button>("fake.add", text("Add", "添加"), d::Icon::plus, d::Button::Kind::secondary, d::Button::Height::small);
        action.onActivate = [this] { events.push_back("add"); };
        header().configure(depth == 0 ? std::vector<std::string>{"CLIPBOARD"} : std::vector<std::string>{"CLIPBOARD", "DETAIL"}, "MOD-03",
            title("Clipboard", "剪贴板缓存"), d::format::pad2(rows), depth > 0, {&action});
        auto& tabs = page.makeTabs("fake.tabs", {{text("All", "全部"), std::nullopt}, {text("Text", "文本"), d::Icon::text}, {text("Links", "链接"), d::Icon::link}},
            tab, d::metric::boxWidth);
        tabs.onSelect = [this](int index, double direction) { tab = index; events.push_back("tab " + std::to_string(index) + " " + fixed1(direction)); navigate(direction); };
        tabs.layer().setOrigin({0, d::metric::headerHeight}); page.layer().add(tabs.layer());
        page.tabs = &tabs; page.add(tabs.controls()); page.stagger(tabs.layer());
        auto& menu = page.make<d::Dropdown>("fake.menu", text("Language", "语言"), "Item 3");
        menu.layer().setOrigin({0, 130}); page.layer().add(menu.layer());
        menu.onActivate = [this, &menu] {
            std::vector<d::Menu::Item> items;
            for (int i = 0; i < 12; ++i) { d::Menu::Item item; item.id = "i" + std::to_string(i); item.title = "Item " + std::to_string(i); item.selected = i == 3; items.push_back(item); }
            openMenu("fake.menu.list", std::move(items), menu, std::nullopt, [this](const d::Menu::Item& item) { events.push_back("pick " + item.id); });
        };
        dropdown = &menu; page.add(menu); page.stagger(menu.layer());
        auto& volume = page.make<d::Slider>("fake.slider", "Volume", 40, d::Slider::Range{0, 100, 1, std::nullopt}, d::ValueFormat::percent, 360);
        volume.onChange = [this](double value, bool final) { events.push_back("slider " + fixed1(value) + (final ? " true" : " false")); };
        volume.layer().setOrigin({200, 130}); page.layer().add(volume.layer());
        hostBubble(volume); slider = &volume; page.add(volume); page.stagger(volume.layer());
        auto& scroll = page.scrollArea("fake.rows", {0, 170, d::metric::boxWidth, 340});
        double y = 0;
        for (int index = 0; index < rows; ++index) {
            const bool nav = index % 4 == 3;
            auto& row = page.make<d::RowPlate>("fake.row." + std::to_string(index), "Row " + std::to_string(index),
                index == 0 ? std::optional<std::string>("Detail") : std::nullopt, nav ? d::RowPlate::Variant::nav : d::RowPlate::Variant::standard, "›", scroll.contentWidth());
            if (index % 4 == 0) {
                auto& toggle = page.make<d::Toggle>("fake.toggle." + std::to_string(index), "Row " + std::to_string(index), index == 0);
                toggle.onChange = [this, index](bool on) { events.push_back("toggle " + std::to_string(index) + (on ? " true" : " false")); };
                row.setCluster({&toggle.layer()}); page.add(toggle);
            }
            row.onActivate = [this, index] { events.push_back("row " + std::to_string(index)); navigate(1); };
            row.layer().setOrigin({0, y}); scroll.content().add(row.layer());
            page.add(row); page.stagger(row.layer());
            y += row.layer().bounds().height + d::metric::rowGap;
        }
        scroll.setContentHeight(y - d::metric::rowGap);
        page.restoreOffset(scroll);
        if (depth > 0) wireBack(page, [this] { depth -= 1; events.push_back("back"); navigate(-1); });
    }
    bool handleKey(const d::KeyInput& k) override { if (k.character == U'k') { keys.push_back("handleKey k"); return true; } return false; }
    bool interceptKey(const d::KeyInput& k) override { if (k.character == U'i') { keys.push_back("interceptKey i"); return true; } return false; }
    bool handleExtraKey(const d::KeyInput& k) override { if (k.character == U'x') { keys.push_back("handleExtraKey x"); return true; } return false; }
    bool handleScroll(Point point, Point) override { if (point.y >= 20) return false; events.push_back("handleScroll"); return true; }
    void didActivate() override { events.push_back("didActivate"); }
    void didDeactivate() override { events.push_back("didDeactivate"); }
    void willLeave() override { events.push_back("willLeave"); }
};
Point centre(const d::Control& c, const d::Layer& root) { const Rect r = c.rect(root); return {r.x + r.width / 2, r.y + r.height / 2}; }
const char* roleName(d::AccessibilityRole r) {
    switch (r) {
    case d::AccessibilityRole::toggle: return "toggle";
    case d::AccessibilityRole::popUp: return "popUp";
    case d::AccessibilityRole::menuItem: return "menuItem";
    case d::AccessibilityRole::tab: return "tab";
    case d::AccessibilityRole::slider: return "slider";
    case d::AccessibilityRole::text: return "text";
    default: return "button";
    }
}

struct CanvasRun {
    World& w;
    const std::vector<J> rows;
    std::size_t index{};
    std::map<std::string, int> builds;      // the Mac's build count per canvas at the previous step
    std::map<std::string, int> ours;
    CanvasRun(World& world, const J& canvas) : w(world), rows(canvas.array()) {}
    const J& step(const FakeCanvas& c, const std::string& label) {
        check(index < rows.size(), "Canvas: more steps than the Mac script");
        const J& m = rows[index++];
        const std::string where = "Canvas " + label;
        check(m["step"].string() == label, where + ": step order differs (" + m["step"].string() + ")");
        check(c.isActive() == m["active"].boolean(), where + ": active");
        check(c.transitioning() == m["transitioning"].boolean(), where + ": transitioning");
        check(c.menuOpen() == m["menuOpen"].boolean() && c.popoverOpen() == m["popoverOpen"].boolean() && c.capturesPointer() == m["capturesPointer"].boolean(), where + ": popups");
        check(c.dragging() == m["dragging"].boolean() && c.inputLocked() == m["inputLocked"].boolean(), where + ": drag / input lock");
        check(c.depth == int(m["depth"].number()) && c.tab == int(m["tab"].number()), where + ": module state");
        std::vector<std::string> events, keys;
        for (const auto& e : m["events"].array()) events.push_back(e.string());
        for (const auto& e : m["keys"].array()) keys.push_back(e.string());
        std::string list; for (const auto& e : c.events) list += e + "; ";
        check(c.events == events, where + ": module events differ: " + list);
        check(c.keys == keys, where + ": key hooks");
        check((c.renderedLanguage() ? J(languageTag(*c.renderedLanguage())) : J()) == m["renderedLanguage"], where + ": rendered language");
        // Rebuild decisions (counts differ by Q4: a swap builds when it starts).
        const std::string module(endfield::core::moduleIdentifier(c.module()));
        const int mac = int(m["builds"].number());
        static const std::set<std::string> swapping{"keyPrecedence", "afterTabSwaps", "navigateForward", "retarget", "swapLanded"};
        if (builds.contains(module) && !swapping.contains(label))
            check((mac > builds[module]) == (c.builds > ours[module]), where + ": rebuilt (Mac " + std::to_string(builds[module]) + " -> " + std::to_string(mac) + ")");
        builds[module] = mac; ours[module] = c.builds;
        if (!c.transitioning()) {
            std::vector<std::string> ids;
            for (const auto* control : c.page().controls()) ids.push_back(control->id());
            std::vector<std::string> expected;
            for (const auto& id : m["pageControls"].array()) expected.push_back(id.string());
            check(ids == expected, where + ": page controls in registration order");
            check(c.page().staggered().size() == std::size_t(m["staggered"].number()), where + ": stagger list");
            const auto& offsets = m["scrollOffsets"].array();
            check(c.page().scrollAreas().size() == offsets.size(), where + ": scroll areas");
            for (std::size_t i = 0; i < offsets.size(); ++i)
                check(c.page().scrollAreas()[i]->id() == offsets[i]["id"].string() && near(c.page().scrollAreas()[i]->offset(), offsets[i]["offset"].number()),
                    where + ": scroll offset " + std::to_string(c.page().scrollAreas()[i]->offset()) + " vs " + std::to_string(offsets[i]["offset"].number()));
        }
        std::vector<d::AccessibilityItem> items;
        c.accessibilityItems(items);
        const auto& expectedItems = m["accessibility"].array();
        std::string got; for (const auto& item : items) got += item.id + " ";
        check(items.size() == expectedItems.size(), where + ": accessibility items " + got);
        for (std::size_t i = 0; i < items.size(); ++i)
            check(items[i].id == expectedItems[i]["id"].string() && roleName(items[i].role) == expectedItems[i]["role"].string(), where + ": accessibility item " + items[i].id);
        return m;
    }
};

void canvasScript(RecordedMeasurer& measurer, const J& canvasRows) {
    World w(measurer);
    w.appearance(Language::english);
    int lockCount = 0;
    w.context.onLockParallax = [&] { ++lockCount; };
    auto& overlay = w.context.overlay();
    FakeCanvas canvas(endfield::core::Module::clipboard, w.context);
    FakeCanvas other(endfield::core::Module::eventLog, w.context);
    CanvasRun run(w, canvasRows);
    const auto& root = canvas.root();
    canvas.show(w.now);
    run.step(canvas, "madeContentInactive");
    canvas.setActive(true, w.now);
    run.step(canvas, "activated");
    canvas.playEntrance(d::entranceOffset(endfield::core::Module::notes, endfield::core::Module::clipboard), w.now);
    run.step(canvas, "entrance");
    {   // The stagger: 60 ms + 36 ms x index (the tabs, the dropdown, the slider, then the rows).
        const auto staggered = canvas.page().staggered();
        for (std::size_t i = 0; i < staggered.size(); ++i)
            check(staggered[i]->animating(w.now + d::staggerDelay(int(i)) + .01) && !staggered[i]->animating(w.now + d::staggerDelay(int(i)) + d::duration::entrance + .01),
                "Entrance stagger " + std::to_string(i));
    }
    auto findControl = [&](std::string_view id) -> d::Control* {
        for (auto* c : canvas.page().controls()) if (c->id() == id) return c;
        throw std::runtime_error("missing control " + std::string(id));
    };
    auto* toggle = findControl("fake.toggle.0");
    auto* row0 = findControl("fake.row.0");
    canvas.pointerMoved(centre(*toggle, root), false, w.now);
    {
        const J& m = run.step(canvas, "hoverToggleInRow");
        check(toggle->hovered() == m["toggle"]["hovered"].boolean() && row0->hovered() == m["row"]["hovered"].boolean(), "The toggle wins hover; its row container stays hovered");
        check(near(toggle->rect(root), rect(m["toggle"]["rect"])) && near(row0->rect(root), rect(m["row"]["rect"])),
            "Toggle and row rects in the canvas " + str(toggle->rect(root)) + " vs " + str(rect(m["toggle"]["rect"])));
    }
    canvas.pointerMoved(Point{100, 195}, true, w.now);
    {
        const J& m = run.step(canvas, "pressRowBody");
        check(row0->pressed() == m["row"]["pressed"].boolean() && toggle->hovered() == m["toggle"]["hovered"].boolean(), "A container is pressed only when no control is hit");
    }
    canvas.pointerMoved(std::nullopt, false, w.now);
    const Point pt = centre(*toggle, root);
    {
        const J& m = run.step(canvas, "containsControl");
        check(canvas.containsControl(pt) == m["toggle"].boolean() && canvas.containsControl({580, 100}) == m["empty"].boolean()
            && canvas.containsControl({300, 10}) == m["header"].boolean(), "containsControl");
    }
    d::PointerInput press;
    canvas.pointerDown(pt, press, w.now); canvas.pointerUp(pt, w.now);
    run.step(canvas, "toggleClicked");
    const Rect sr = canvas.slider->rect(root);
    canvas.pointerDown({sr.x + 20, sr.y + sr.height / 2}, press, w.now);
    canvas.pointerDragged({sr.x + 200, sr.y + sr.height / 2}, w.now);
    run.step(canvas, "sliderDragging");
    canvas.pointerUp({sr.x + 200, sr.y + sr.height / 2}, w.now);
    run.step(canvas, "sliderReleased");
    const Point wheel{300, 300};
    const std::vector<bool> wheels{canvas.scroll(wheel, {0, 120}, w.now), canvas.scroll(wheel, {0, 10000}, w.now), canvas.scroll(wheel, {0, 50}, w.now),
        canvas.scroll({300, 10}, {0, 50}, w.now), canvas.scroll({300, 100}, {0, 50}, w.now)};
    {
        const J& m = run.step(canvas, "wheel");
        for (std::size_t i = 0; i < wheels.size(); ++i) check(wheels[i] == m["results"].array()[i].boolean(), "Wheel result " + std::to_string(i) + " (areas consume at their limit)");
    }
    canvas.scroll(wheel, {0, -10000}, w.now); canvas.scroll(wheel, {0, 120}, w.now);
    const Point drop = centre(*canvas.dropdown, root);
    canvas.pointerDown(drop, press, w.now); canvas.pointerUp(drop, w.now);
    {
        const J& m = run.step(canvas, "menuOpened");
        check(canvas.dropdown->isOpen() == m["dropdownOpen"].boolean(), "The anchor dropdown shows its open state");
    }
    const bool wheelInside = canvas.scroll({drop.x, drop.y + 60}, {0, 40}, w.now);
    const bool wheelOutside = canvas.scroll({560, 480}, {0, 40}, w.now);
    const bool rightClick = canvas.rightPointerDown({560, 480}, w.now);
    {
        const J& m = run.step(canvas, "menuWheelAndRightClick");
        check(wheelInside == m["wheelInside"].boolean() && wheelOutside == m["wheelOutside"].boolean() && rightClick == m["rightClick"].boolean(),
            "An open menu consumes every wheel; a right click closes popups");
    }
    canvas.pointerDown(drop, press, w.now); canvas.pointerUp(drop, w.now);
    const std::vector<bool> menuKeys{canvas.keyDown(keyOf(d::key::down), w.now), canvas.keyDown(keyOf(d::key::down), w.now),
        canvas.keyDown(keyOf('E', U'e'), w.now), canvas.keyDown(keyOf(d::key::escape), w.now)};
    {
        const J& m = run.step(canvas, "menuKeys");
        for (std::size_t i = 0; i < menuKeys.size(); ++i) check(menuKeys[i] == m["consumed"].array()[i].boolean(), "An open menu swallows every key");
    }
    canvas.pointerDown(drop, press, w.now); canvas.pointerUp(drop, w.now);
    const bool outside = canvas.pointerDown({560, 480}, press, w.now);
    check(outside == run.rows[run.index]["consumed"].boolean(), "The first press outside a menu only closes it");
    run.step(canvas, "pressOutsideClosesOnly");
    canvas.pointerDown(drop, press, w.now); canvas.pointerUp(drop, w.now);
    canvas.keyDown(keyOf(d::key::down), w.now); canvas.keyDown(keyOf(d::key::enter), w.now);
    {
        const J& m = run.step(canvas, "menuPickByKeyboard");
        check(canvas.dropdown->isOpen() == m["dropdownOpen"].boolean(), "Picking closes the menu and clears the anchor's open state");
    }
    canvas.openPopover(std::make_unique<d::Popover>(canvas.env(), "Pen", std::nullopt, 270, 60), canvas.dropdown, std::nullopt, {});
    const std::vector<bool> popKeys{canvas.keyDown(keyOf('A', U'a'), w.now), canvas.keyDown(keyOf('E', U'e'), w.now), canvas.keyDown(keyOf(d::key::escape), w.now)};
    {
        const J& m = run.step(canvas, "popoverKeys");
        for (std::size_t i = 0; i < popKeys.size(); ++i) check(popKeys[i] == m["consumed"].array()[i].boolean(), "A pop-over takes only Esc");
    }
    const std::vector<d::KeyInput> plainKeys{keyOf('I', U'i'), keyOf('K', U'k'), keyOf('E', U'e', false, true), keyOf(d::key::escape),
        keyOf('E', U'E', true), keyOf('Q', U'q'), keyOf('X', U'x'), keyOf('Z', U'z')};
    std::vector<bool> keyResults;
    for (const auto& k : plainKeys) keyResults.push_back(canvas.keyDown(k, w.now));
    {
        const J& m = run.step(canvas, "keyPrecedence");
        for (std::size_t i = 0; i < keyResults.size(); ++i) check(keyResults[i] == m["results"].array()[i]["consumed"].boolean(), "Key precedence: " + m["results"].array()[i]["key"].string());
    }
    w.advance(.25);
    run.step(canvas, "afterTabSwaps");
    canvas.page().scrollAreas()[0]->setOffset(120);
    canvas.depth = 1;
    canvas.navigate(1);
    const bool pressDuringSwap = canvas.pointerDown(pt, press, w.now), keyDuringSwap = canvas.keyDown(keyOf('X', U'x'), w.now);
    {
        const J& m = run.step(canvas, "navigateForward");
        check(pressDuringSwap == m["pressDuringSwap"].boolean() && keyDuringSwap == m["keyDuringSwap"].boolean(), "During a swap presses are ignored, keys are not");
        check(canvas.page().layer().animating(w.now + .05), "The old page leaves");
    }
    const double deadline = *canvas.nextDeadline(w.now);
    w.advance(.06);
    canvas.navigate(1);
    check(*canvas.nextDeadline(w.now) == deadline, "A retarget lands on the original deadline");
    run.step(canvas, "retarget");
    w.advance(.08);
    run.step(canvas, "swapLanded");
    check(canvas.header().back() && canvas.header().crumbTexts().size() == 5, "The sub-page header (back button, crumbs) came with the swap");
    canvas.depth = 0;
    canvas.navigate(-1);
    canvas.settleSwap();
    run.step(canvas, "backSettled");
    canvas.depth = 1; canvas.navigate(1);
    canvas.prepareToLeave(w.now);
    {
        run.step(canvas, "prepareToLeaveConcealed");
        check(!d::isVisible(canvas.page().layer(), root) || canvas.page().layer().presented(d::Channel::opacity, w.now) == 0, "A settled page stays concealed while the module leaves");
    }
    canvas.show(w.now);
    run.step(canvas, "revealedByMakeContent");
    check(canvas.page().layer().presented(d::Channel::opacity, w.now) == 1, "Showing the module reveals the settled page");
    canvas.depth = 0;
    w.appearance(Language::english, "FAD41F", false, true);
    canvas.navigate(-1);
    w.appearance(Language::english);
    run.step(canvas, "navigateReduceMotion");
    other.show(w.now);
    run.step(other, "otherBuiltInactive");
    w.appearance(Language::english, "22A6F2");
    run.step(canvas, "paletteChangedActive");
    run.step(other, "paletteChangedInactive");
    other.setActive(true, w.now);
    run.step(other, "inactiveRebuildsOnActivate");
    other.setActive(false, w.now);
    w.appearance(Language::japanese, "22A6F2");
    other.show(w.now);
    run.step(other, "languageStaleRebuildOnShow");
    check(other.header().titleTexts().first == other.text("Clipboard", "剪贴板缓存"), "The rebuilt header speaks the new language");
    run.step(canvas, "activeNotRebuiltUntilShown");
    w.appearance(Language::english, "FAD41F");
    w.appearance(Language::english, "FAD41F", false, false, true);
    run.step(canvas, "recordingModeOn");
    w.context.update(d::geometry::baseUnit * 1.5, 2, w.now);
    {
        const J& m = run.step(canvas, "unitChanged");
        check(near(w.context.boxScale(), m["renderScale"].number()) && near(w.context.unit(), m["unit"].number()), "Context::update: unit and box scale");
    }
    w.context.update(d::geometry::baseUnit, 2, w.now);
    canvas.pointerDown(drop, press, w.now); canvas.pointerUp(drop, w.now);
    canvas.confirm(plainRequest(), [] {});
    {
        const J& m = run.step(canvas, "confirmClosesMenus");
        check(overlay.capturesInput() == m["capturesInput"].boolean(), "Canvas::confirm presents the bar after closing menus");
    }
    overlay.dismissConfirm();
    {
        const J& m = canvasRows.array()[run.index];
        std::size_t i = 0;
        for (const Language language : {Language::english, Language::simplifiedChinese, Language::traditionalChinese, Language::japanese, Language::korean}) {
            w.appearance(language, "FAD41F", false, false, true);   // only the language changes (Recording mode stays on)
            canvas.toast(d::Icon::trash, "History cleared", "剪贴板记录已清空");
            const auto& a = m["toasts"].array()[i++];
            check(overlay.toast().texts() == std::pair{a["title"].string(), a["caption"].string()}, "Result toast in " + languageTag(language) + ": English only in English, the English caption elsewhere");
            canvas.toast(d::Icon::ext, "Opening", "正在打开", std::string("Safari"));
            const auto& b = m["toasts"].array()[i++];
            check(overlay.toast().texts() == std::pair{b["title"].string(), b["caption"].string()}, "Result toast with a detail in " + languageTag(language));
        }
        w.appearance(Language::english, "FAD41F", false, false, true);
        overlay.reset();
    }
    run.step(canvas, "toastHelper");
    canvas.setActive(false, w.now);
    run.step(canvas, "deactivated");
    canvas.setActive(true, w.now);
    canvas.navigate(1); canvas.settleSwap(); canvas.navigate(-1); canvas.settleSwap();
    run.step(canvas, "offsetsForgottenAfterDeactivate");
    check(run.index == run.rows.size(), "Every Mac canvas step was replayed");
    check(lockCount > 0, "A tracked drag locks the parallax");
}

// Contracts that need no oracle: lifetimes, revisions, deadlines.
void lifetimes(RecordedMeasurer& measurer) {
    World w(measurer); w.appearance(Language::english);
    FakeCanvas canvas(endfield::core::Module::clipboard, w.context);
    canvas.show(w.now); canvas.setActive(true, w.now);
    const auto r0 = canvas.contentRevision();
    check(canvas.contentRevision() == r0, "The content revision is stable without changes");
    // A tab whose onSelect re-renders under Reduce Motion destroys its own page: the page is buried, not freed under it.
    w.appearance(Language::english, "FAD41F", false, true);
    canvas.page().tabs->controls()[1]->activate();
    check(canvas.tab == 1 && !canvas.transitioning(), "Reduce Motion installs at once");
    check(canvas.contentRevision() != r0, "A new page moves the content revision");
    check(w.context.nextWakeTime() && *w.context.nextWakeTime() <= w.now, "The replaced page is collected by a deadline at once");
    w.advance(0);
    w.appearance(Language::english);
    // Menu pick closes then calls back; the menu dies after the callback.
    const Point drop = centre(*canvas.dropdown, canvas.root());
    canvas.pointerDown(drop, {}, w.now); canvas.pointerUp(drop, w.now);
    check(canvas.menuOpen(), "Menu open");
    canvas.keyDown(keyOf(d::key::enter), w.now);
    check(!canvas.menuOpen() && canvas.events.back() == "pick i3", "Return picks the keyboard item (the selected one)");
    w.advance(0);
    // Timers die with their group; module timers with the canvas.
    int fired = 0;
    const auto group = canvas.timerGroup();
    canvas.after(.5, group, [&] { ++fired; });
    canvas.after(.6, group, [&] { ++fired; });
    canvas.cancel(group);
    w.advance(1);
    check(fired == 0, "A cancelled timer group never runs");
    {
        FakeCanvas temporary(endfield::core::Module::eventLog, w.context);
        temporary.after(.5, temporary.timerGroup(), [&] { ++fired; });
        temporary.show(w.now);
        check(w.context.canvases().size() == 2, "Canvases register with the context");
    }
    w.advance(1);
    check(fired == 0 && w.context.canvases().size() == 1, "A destroyed canvas cancels its timers and unregisters");
    // The content filter: present while dimmed, eased .25 s out, then gone.
    auto& o = w.context.overlay();
    o.showToast(toastOf("x"));
    check(o.dimmed() && o.filterNeeded(w.now), "A toast dims the content too");
    w.advance(.25);
    check(near(o.dimFilter(w.now), 1), "The filter reaches full strength in .25 s");
    w.advance(1.3);
    check(!o.dimmed() && o.filterNeeded(w.now), "Undimming eases out");
    w.advance(.3);
    check(!o.filterNeeded(w.now), "The filter is removed once eased out");
    const auto before = o.contentRevision();
    o.confirm(plainRequest(), [] {});
    check(o.contentRevision() != before, "A new request changes the overlay's content");
    o.reset();
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: hud_design_page_tests <hud-design-source.json>");
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) throw std::runtime_error("The Mac oracle fixture is missing (tools/hud_design_reference.sh)");
        const std::string text((std::istreambuf_iterator<char>(file)), {});
        const auto parts = sections(text);
        for (const char* key : {"overlay", "canvas", "text"}) if (!parts.contains(key)) throw std::runtime_error(std::string("Oracle section missing: ") + key);
        RecordedMeasurer measurer;
        measurer.load(J::parse(parts.at("text"), 64 * 1024 * 1024));
        const J overlay = J::parse(parts.at("overlay"), 64 * 1024 * 1024);
        const J canvas = J::parse(parts.at("canvas"), 64 * 1024 * 1024);
        overlayTraces(measurer, overlay);
        confirmBars(measurer, overlay);
        canvasScript(measurer, canvas);
        lifetimes(measurer);
        std::sort(measurer.misses.begin(), measurer.misses.end());
        measurer.misses.erase(std::unique(measurer.misses.begin(), measurer.misses.end()), measurer.misses.end());
        if (!measurer.misses.empty()) {
            std::cout << "hud_design_page: " << measurer.misses.size() << " widths/metrics not recorded by the oracle (estimated):\n";
            for (const auto& miss : measurer.misses) std::cout << "  " << miss << '\n';
        }
        std::cout << "hud_design_page: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "hud_design_page_tests failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
