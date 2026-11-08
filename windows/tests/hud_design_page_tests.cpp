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
#include <functional>
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
// The place-and-confirm strip (overlay.placement: HUDPlacementEditor)
// ---------------------------------------------------------------------------
bool sameColors(const d::Layer& layer, const J& mac) {
    if (!layer.content() || !mac.isArray()) return false;
    const auto& mine = (*layer.content())["gradient"]["colors"].array();
    if (mine.size() != mac.array().size()) return false;
    for (std::size_t i = 0; i < mine.size(); ++i)
        for (std::size_t c = 0; c < 4; ++c)
            if (!near(mine[i]["sRGB"].array()[c].number(), mac.array()[i].array()[c].number(), 1e-4)) return false;
    return true;
}
void placementStrip(RecordedMeasurer& measurer, const J& overlay) {
    const auto& placement = overlay["placement"];
    check(d::PlacementEditor::bubbleClearance == placement["bubbleClearance"].number() && d::PlacementEditor::rowPitch == placement["rowPitch"].number() &&
          d::PlacementEditor::sliderWidth == placement["sliderWidth"].number(), "Strip constants: bubble clearance 8, row pitch 49, slider width 236");
    unsigned strips{};
    for (const auto& row : placement["strips"].array()) {
        const auto language = languageNamed(row["language"].string());
        World w(measurer);
        w.appearance(language, "FAD41F", row["light"].boolean());
        std::vector<d::PlacementEditor::SliderSpec> specs{
            {"chargeBar.size", endfield::core::localized("Size", "大小", language), 100, {65, 160, 1, {}}, d::ValueFormat::percent},
            {"chargeBar.length", endfield::core::localized("Length", "长度", language), 37, {14, 100, 1, {}}, d::ValueFormat::percent},
            {"extra", "A considerably longer title", 1, {.5, 2, .01, {}}, d::ValueFormat::scale}};
        specs.resize(std::size_t(row["rows"].number()));
        d::PlacementEditor editor(w.context.env(), "chargeBar", specs);
        const std::string where = "Strip " + row["language"].string() + (row["light"].boolean() ? " light " : " dark ") + std::to_string(specs.size()) + " rows";
        const Rect size{0, 0, row["size"].array()[0].number(), row["size"].array()[1].number()};
        check(near(Rect{0, 0, editor.size().width, editor.size().height}, size), where + ": size " + str({0, 0, editor.size().width, editor.size().height}));
        const auto* band = findLayer(editor.layer(), "placement.band");
        const auto* marker = findLayer(editor.layer(), "placement.marker");
        check(band && marker && near(band->frame(), rect(row["band"])) && near(marker->frame(), rect(row["marker"])), where + ": band and marker frames");
        check(sameColors(*band, row["bandColors"]) && sameColors(*marker, row["markerColors"]), where + ": band and marker colours");
        for (std::size_t i = 0; i < 2; ++i) {
            const auto* hairline = findLayer(editor.layer(), "placement.hairline." + std::to_string(i));
            check(hairline && near(hairline->frame(), rect(row["hairlines"].array()[i])) && sameColors(*hairline, row["hairlineColors"].array()[i]),
                  where + ": hairline " + std::to_string(i));
        }
        const auto& titles = row["titles"].array();
        check(titles.size() == specs.size(), where + ": one title per row");
        for (std::size_t i = 0; i < titles.size(); ++i) {
            const auto* title = findLayer(editor.layer(), "placement.title." + std::to_string(i));
            check(title && d::textOf(*title).text() == titles[i]["text"].string() && near(d::textOf(*title).lineFrame, rect(titles[i]["lineFrame"])),
                  where + ": title " + titles[i]["text"].string() + " line box " + (title ? str(d::textOf(*title).lineFrame) : std::string()));
        }
        const auto& sliders = row["sliders"].array();
        check(sliders.size() == editor.sliders().size(), where + ": one slider per row");
        for (std::size_t i = 0; i < sliders.size(); ++i) {
            const auto* slider = editor.sliders()[i];
            check(slider->id() == "placement." + sliders[i]["id"].string() || slider->id() == sliders[i]["id"].string(), where + ": slider id");
            check(slider->label == sliders[i]["label"].string() && near(slider->value(), sliders[i]["value"].number()) &&
                  near(slider->layer().frame(), rect(sliders[i]["frame"])), where + ": slider " + sliders[i]["id"].string() + " " + str(slider->layer().frame()));
        }
        const auto& pills = row["pills"].array();
        for (std::size_t i = 0; i < 2; ++i) {
            const d::Pill& pill = i == 0 ? editor.cancel() : editor.confirm();
            check(pill.id() == pills[i]["id"].string() && pill.label == pills[i]["label"].string() && near(pill.layer().frame(), rect(pills[i]["frame"])),
                  where + ": pill " + pills[i]["id"].string() + " " + str(pill.layer().frame()) + " vs " + str(rect(pills[i]["frame"])));
        }
        std::vector<std::string> controls, expectedControls;
        for (const auto* c : editor.controls()) controls.push_back(c->id());
        for (const auto& id : row["controls"].array()) expectedControls.push_back(id.string());
        check(controls == expectedControls, where + ": routed controls (sliders, Cancel, Confirm)");
        std::vector<d::AccessibilityItem> items;
        editor.accessibilityItems(editor.layer(), items);
        const auto& mac = row["accessibility"].array();
        check(items.size() == mac.size(), where + ": accessibility items");
        for (std::size_t i = 0; i < items.size(); ++i)
            check(items[i].id == mac[i]["id"].string() && items[i].label == mac[i]["label"].string() && near(items[i].rect, rect(mac[i]["rect"])) &&
                  (items[i].role == d::AccessibilityRole::slider) == (mac[i]["role"].string() == "slider"), where + ": accessibility item " + items[i].id);
        ++strips;
    }
    check(strips == 8, "Every Mac strip compared (five languages, light, no rows and three rows)");
    // Keys: Esc and Return / keypad Enter are taken (only the first decides), other keys are left to the host.
    {
        World w(measurer); w.appearance(Language::english);
        d::PlacementEditor editor(w.context.env(), "keys", {});
        std::vector<std::string> decisions;
        editor.onDecision = [&](d::ConfirmState::Decision decision) { decisions.push_back(decision == d::ConfirmState::Decision::confirm ? "confirm" : "cancel"); };
        const auto& keys = overlay["placementKeys"];
        for (std::size_t i = 0; i < keys["keys"].array().size(); ++i)
            check(editor.key(macKey(int(keys["keys"].array()[i].number()))) == keys["handled"].array()[i].boolean(), "Strip key " + std::to_string(i));
        std::vector<std::string> expected;
        for (const auto& d : keys["decisions"].array()) expected.push_back(d.string());
        check(decisions == expected && !editor.isOpen(), "Esc decides once; Return afterwards decides nothing");
    }
    // Where the strip goes beside its target (y down).
    for (const auto& row : placement["frames"].array()) {
        const auto frame = d::PlacementEditor::frame({row["size"].array()[0].number(), row["size"].array()[1].number()}, rect(row["target"]),
                                                     rect(row["bounds"]), row["gap"].number());
        check(near(frame, rect(row["frame"])), "Strip frame beside " + row["target"].encode() + " in " + row["bounds"].encode() + ": " + str(frame));
    }
    // Pointer: a press on the strip goes to its controls, any other press drags from the anchor.
    {
        World w(measurer); w.appearance(Language::english);
        d::PlacementEditor editor(w.context.env(), "chargeBar", {{"chargeBar.size", "Size", 100, {65, 160, 1, {}}, d::ValueFormat::percent}});
        std::vector<std::pair<std::string, bool>> changes;
        editor.onChange = [&](const std::string& id, double, bool final) { changes.emplace_back(id, final); };
        const Rect track = d::convert(Rect{0, 0, 10, 30}, editor.sliders()[0]->layer(), editor.layer());
        editor.pointerDown({track.x + 60, track.y + 15}, editor.layer(), {500, 500}, {300, 40});
        check(editor.adjusting() && !editor.dragging() && changes.size() == 1 && !changes[0].second, "A press on a slider adjusts it (not final)");
        check(!editor.pointerDragged({track.x + 80, track.y + 15}, editor.layer(), {520, 500}), "Dragging a slider moves no anchor");
        editor.pointerUp({track.x + 80, track.y + 15}, editor.layer());
        check(!editor.adjusting() && !changes.empty() && changes.back().second, "Release commits the slider");
        editor.pointerDown({-50, -50}, editor.layer(), {500, 500}, {300, 40});
        const auto anchor = editor.pointerDragged({-40, -30}, editor.layer(), {510, 520});
        check(editor.dragging() && anchor && near(anchor->x, 310) && near(anchor->y, 60), "A press outside the strip drags the anchor with the pointer");
        editor.pointerUp({-40, -30}, editor.layer());
        check(!editor.dragging() && !editor.pointerDragged({0, 0}, editor.layer(), {0, 0}), "Release ends the drag");
        editor.adjust("placement.chargeBar.size", 120);
        editor.adjust("chargeBar.size", 120);
        check(near(*editor.value("chargeBar.size"), 120) && changes.back() == std::pair<std::string, bool>{"chargeBar.size", true},
              "An accessibility adjustment sets the slider and reports it as final");
        editor.setRange({100, 160, 1, {}}, 90, "chargeBar.size");
        check(near(*editor.value("chargeBar.size"), 100) && editor.sliders()[0]->range().minimum == 100, "setRange clamps the value into the new range");
        bool decided{};
        editor.onDecision = [&](d::ConfirmState::Decision decision) { decided = decision == d::ConfirmState::Decision::confirm; };
        editor.perform("chargeBar:confirm");
        check(decided && !editor.isOpen(), "An accessibility press on Confirm confirms");
        editor.pointerDown({-50, -50}, editor.layer(), {0, 0}, {0, 0});
        check(!editor.dragging(), "A decided strip starts no drag");
    }
    // show(): the confirm bar's entrance. The band wipes from its centre over
    // bandIn (snap); the marker and each row (title and slider) enter from
    // (-12, 0) after confirmTextDelay + row x pillStep; Cancel and Confirm
    // enter from (+12, 0) after pillDelay + index x pillStep.
    {
        World w(measurer); w.appearance(Language::english);
        d::PlacementEditor editor(w.context.env(), "chargeBar", {
            {"chargeBar.size", "Size", 100, {65, 160, 1, {}}, d::ValueFormat::percent},
            {"chargeBar.length", "Length", 37, {14, 100, 1, {}}, d::ValueFormat::percent}});
        const double t0 = w.now;
        editor.show(w.context.env().clock());
        namespace t = d::duration;
        const auto* band = findLayer(editor.layer(), "placement.band");
        check(band && near(band->presented(d::Channel::wipe, t0), 0) && near(band->presented(d::Channel::wipe, t0 + t::bandIn), 1) &&
              near(band->presented(d::Channel::wipe, t0 + t::bandIn / 2), d::easing::snap.value(.5)), "Strip entrance: the band wipes in over bandIn (snap)");
        // A layer entering from `dx` after `delay`: still at its offset and
        // transparent just before, moving just after, settled at the end.
        const auto enters = [&](const d::Layer* layer, double dx, double delay, const std::string& what) {
            const double start = t0 + delay, eps = 1e-4;
            const double moving = layer ? layer->presented(d::Channel::translateX, start + t::confirmText / 2) : 0;
            check(layer && near(layer->presented(d::Channel::translateX, start - eps), dx) && near(layer->presented(d::Channel::opacity, start - eps), 0) &&
                  std::abs(moving) > 0 && std::abs(moving) < std::abs(dx) && (moving > 0) == (dx > 0) &&
                  near(layer->presented(d::Channel::translateX, start + t::confirmText + eps), 0) &&
                  near(layer->presented(d::Channel::opacity, start + t::confirmText + eps), 1), "Strip entrance: " + what);
        };
        enters(findLayer(editor.layer(), "placement.marker"), -12, t::confirmTextDelay, "the marker from the left after confirmTextDelay");
        for (std::size_t i = 0; i < 2; ++i) {
            const double delay = t::confirmTextDelay + double(i) * t::pillStep;
            enters(findLayer(editor.layer(), "placement.title." + std::to_string(i)), -12, delay, "title " + std::to_string(i) + " from the left, staggered by pillStep");
            enters(&editor.sliders()[i]->layer(), -12, delay, "slider " + std::to_string(i) + " from the left with its title");
        }
        enters(&editor.cancel().layer(), 12, t::pillDelay, "Cancel from the right after pillDelay");
        enters(&editor.confirm().layer(), 12, t::pillDelay + t::pillStep, "Confirm from the right one pillStep later");
    }
}

// ---------------------------------------------------------------------------
// The fake module canvas (the oracle's FakeCanvas, line for line)
// ---------------------------------------------------------------------------
class FakeCanvas final : public d::Canvas {
public:
    using Canvas::Canvas;
    int depth{}, tab{}, rows{12}, builds{};
    bool deferredBuild{};                   // recordingDeferred() during the last build
    std::vector<std::string> events, keys;
    d::Dropdown* dropdown{};
    d::Slider* slider{};
    void buildPage(d::Page& page) override {
        ++builds; deferredBuild = recordingDeferred();
        auto& action = page.make<d::Button>("fake.add", text("Add", "添加"), d::Icon::plus, d::Button::Kind::secondary, d::Button::Height::compact);
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

// A Recording mode switch (rc139: the HUD froze on every switch once every
// module was built): only the shown canvas builds a page, and that page keeps
// its layer ids, so the scene finds every unchanged part byte-equal under the
// same id and rasterizes nothing again for it; a built canvas that is not
// shown builds once, when next shown, in the new mode (as a palette change).
void recordingModeWork(RecordedMeasurer& measurer) {
    World w(measurer); w.appearance(Language::english);
    FakeCanvas shown(endfield::core::Module::clipboard, w.context), hidden(endfield::core::Module::fileShelf, w.context), unbuilt(endfield::core::Module::notes, w.context);
    hidden.show(w.now); hidden.setActive(true, w.now); hidden.setActive(false, w.now); hidden.prepareToLeave(w.now);
    shown.show(w.now); shown.setActive(true, w.now);
    w.advance(1);
    const d::TextServices& text = w.context.env().text;
    const auto artwork = [&](const FakeCanvas& c) {
        std::vector<std::pair<std::string, J>> parts;
        for (auto& part : d::emit(c.root(), {&text, "test."}).parts) parts.emplace_back(part.id, std::move(part.layers));
        return parts;
    };
    for (const bool recording : {true, false}) {
        const std::string mode = recording ? "on" : "off";
        const auto before = artwork(shown);
        const auto pages = w.context.pagesBuilt();
        const int shownBuilds = shown.builds, hiddenBuilds = hidden.builds;
        w.appearance(Language::english, "FAD41F", false, false, recording);
        check(w.context.pagesBuilt() == pages + 1 && shown.builds == shownBuilds + 1, "Recording mode " + mode + ": the shown canvas re-renders in place, one page");
        check(hidden.builds == hiddenBuilds && unbuilt.builds == 0 && !unbuilt.renderedLanguage(), "Recording mode " + mode + ": canvases not shown build nothing on the switch");
        check(artwork(shown) == before, "Recording mode " + mode + ": unchanged artwork keeps its part ids and bytes (nothing rasterizes again)");
        check(!shown.deferredBuild, "Recording mode " + mode + ": the shown canvas's rebuild is the switch's own (build-time effects play)");
        w.advance(1);
        hidden.show(w.now); hidden.setActive(true, w.now);
        check(hidden.builds == hiddenBuilds + 1 && w.context.pagesBuilt() == pages + 2, "Recording mode " + mode + ": a built canvas re-renders once, when next shown");
        check(hidden.deferredBuild && !hidden.recordingDeferred(), "Recording mode " + mode + ": that build knows it applies the switch late (the Mac's ran unseen), only while it builds");
        hidden.setActive(false, w.now); hidden.prepareToLeave(w.now);
        hidden.show(w.now);
        check(hidden.builds == hiddenBuilds + 1, "Recording mode " + mode + ": showing it again builds nothing");
        hidden.prepareToLeave(w.now);
        w.advance(1);
    }
    // A language change while a Recording mode change waits rebuilds (new ids) once.
    w.appearance(Language::english, "FAD41F", false, false, true);
    w.appearance(Language::japanese, "FAD41F", false, false, true);
    const int builds = hidden.builds;
    hidden.show(w.now);
    check(hidden.builds == builds + 1 && hidden.renderedLanguage() == Language::japanese, "A waiting Recording mode change and a language change rebuild once when shown");
    check(!hidden.deferredBuild, "A language rebuild is not a late Recording mode build (the Mac rebuilds it when shown too)");
}

// Lists start at the top on return and on Back (HUDDesignScreen showsFromTop):
// a Back starts at the top like a forward sub-page; a module that was left
// builds its page from the top when next shown, once and only when a list was
// left scrolled; in-place re-renders while it is shown keep the scroll.
void listsFromTop(RecordedMeasurer& measurer) {
    World w(measurer); w.appearance(Language::english);
    FakeCanvas c(endfield::core::Module::clipboard, w.context);
    const auto offset = [&] { return c.page().scrollAreas()[0]->offset(); };
    const auto scroll = [&] { c.page().scrollAreas()[0]->setOffset(120); check(near(offset(), 120), "The fake list scrolls to 120"); };
    const auto leave = [&] { c.prepareToLeave(w.now); c.setActive(false, w.now); };   // the owner's leave
    const auto enter = [&] { c.show(w.now); c.setActive(true, w.now); };
    enter();
    check(!c.showsFromTop(), "A module shown for the first time has nothing to reset");
    scroll();
    c.rerender();
    check(near(offset(), 120), "An in-place re-render while active keeps the list's scroll");
    c.depth = 1; c.navigate(1); c.settleSwap();
    check(near(offset(), 0), "A forward sub-page starts at the top");
    scroll();
    c.depth = 0; c.navigate(-1); c.settleSwap();
    check(near(offset(), 0), "Back starts the list at the top too (no offsets kept for going back)");
    scroll();
    leave();
    check(c.showsFromTop(), "Leaving the module marks its next showing to start at the top");
    c.rerender();
    check(near(offset(), 0) && c.showsFromTop(), "A re-render between prepareToLeave and show builds the list at the top");
    auto pages = w.context.pagesBuilt();
    enter();
    check(w.context.pagesBuilt() == pages && near(offset(), 0) && !c.showsFromTop(), "Shown again with its list already at the top, the module builds nothing");
    scroll();
    leave();
    pages = w.context.pagesBuilt();
    enter();
    check(w.context.pagesBuilt() == pages + 1 && near(offset(), 0) && !c.showsFromTop(), "Shown again with its list left scrolled, the module builds its page once, at the top");
    leave();
    pages = w.context.pagesBuilt();
    enter();
    check(w.context.pagesBuilt() == pages, "Shown again unscrolled, the module builds nothing");
    scroll();
    leave();
    c.keepsScrollOnNextShow();
    pages = w.context.pagesBuilt();
    enter();
    check(w.context.pagesBuilt() == pages && near(offset(), 120), "keepsScrollOnNextShow keeps the scroll it set for the next showing");
    leave();
    c.setActive(true, w.now);                                // a cancelled module swap: active again without show
    check(!c.showsFromTop() && near(offset(), 120), "setActive(true) without show clears the flag and keeps the page");
    pages = w.context.pagesBuilt();
    c.show(w.now);
    check(w.context.pagesBuilt() == pages && near(offset(), 120), "A later show of that module keeps its scroll");
}

// The entrance as the HUD opens into the module (Mac a5d58f1,
// HUDDirectionalContent.playEntrance(from:delay:)): every staggered row and
// the header wipe and rule grow start `delay` later; the two-argument form
// (module swaps, sub-pages) is the same as a zero delay.
void delayedEntrance(RecordedMeasurer& measurer) {
    World w(measurer); w.appearance(Language::english);
    const auto offset = d::entranceOffset(std::nullopt, endfield::core::Module::clipboard);
    const auto enterSlot = [](const d::Layer& l, std::size_t i) -> const auto& { return l.tracks().slots[std::size_t(d::MotionSlot::enter)][i]; };
    const auto starts = [&](FakeCanvas& c) {
        std::vector<double> out;
        for (const auto* layer : c.page().staggered()) out.push_back(enterSlot(*layer, 0).start - w.now);
        return out;
    };
    const auto must = [](const d::Layer& root, std::string_view suffix) -> const d::Layer& {
        const std::function<const d::Layer*(const d::Layer&)> find = [&](const d::Layer& l) -> const d::Layer* {
            if (std::string_view(l.id()).ends_with(suffix)) return &l;
            for (const auto* child : l.children()) if (const auto* hit = find(*child)) return hit;
            return nullptr;
        };
        const auto* hit = find(root);
        if (!hit) throw std::runtime_error("No layer ending in " + std::string(suffix) + " under " + root.id());
        return *hit;
    };
    FakeCanvas plain(endfield::core::Module::clipboard, w.context), opening(endfield::core::Module::clipboard, w.context);
    for (auto* c : {&plain, &opening}) { c->show(w.now); c->setActive(true, w.now); }
    plain.playEntrance(offset, w.now);
    opening.playEntrance(offset, w.now, .2);
    const auto at = starts(plain), delayed = starts(opening);
    check(!at.empty() && at.size() == delayed.size(), "Both canvases stagger the same rows");
    for (std::size_t i = 0; i < at.size(); ++i) {
        check(near(at[i], d::staggerDelay(int(i))) && near(delayed[i], .2 + d::staggerDelay(int(i))), "Row " + std::to_string(i) + " starts .2 s later with the delay");
        const auto& row = *opening.page().staggered()[i];
        check(row.animating(w.now + .19) && near(row.presented(d::Channel::opacity, w.now + .2 + d::staggerDelay(int(i)) - .001), 0)
            && near(row.presented(d::Channel::translateY, w.now + .2 + d::staggerDelay(int(i)) - .001), row.value(d::Channel::translateY) + offset.y),
            "Row " + std::to_string(i) + " holds its start pose until its delayed start");
        check(!row.animating(w.now + .2 + d::staggerDelay(int(i)) + d::duration::entrance + .001) && near(row.presented(d::Channel::opacity, w.now + .2 + d::staggerDelay(int(i)) + d::duration::entrance + .001), row.value(d::Channel::opacity)),
            "Row " + std::to_string(i) + " settles .2 s later");
    }
    const auto slot = [](const d::Layer& l, d::MotionSlot s) -> const auto& { return l.tracks().slots[std::size_t(s)][0]; };
    const auto& plainTitle = must(plain.header().layer(), "header.title"), &openingTitle = must(opening.header().layer(), "header.title");
    const auto& plainRule = must(plain.header().layer(), "header.rule"), &openingRule = must(opening.header().layer(), "header.rule");
    check(near(slot(plainTitle, d::MotionSlot::wipe).start - w.now, 0) && near(slot(openingTitle, d::MotionSlot::wipe).start - w.now, .2)
        && near(slot(openingTitle, d::MotionSlot::fade).start - w.now, .2), "The header's title wipe and fade start .2 s later");
    check(near(slot(plainRule, d::MotionSlot::grow).start - w.now, d::duration::ruleDelay) && near(slot(openingRule, d::MotionSlot::grow).start - w.now, d::duration::ruleDelay + .2),
        "The rule grows after .05 + .2 s");
    // Reduce Motion: no entrance with or without a delay.
    w.appearance(Language::english, "FAD41F", false, true);
    FakeCanvas reduced(endfield::core::Module::clipboard, w.context);
    reduced.show(w.now); reduced.setActive(true, w.now); reduced.playEntrance(offset, w.now, .2);
    check(!reduced.page().staggered().empty(), "The reduced canvas staggers rows too");
    for (const auto* layer : reduced.page().staggered()) check(!layer->animating(w.now), "Reduce Motion plays no delayed entrance");
}

// A page re-rendered in place while the module entrance plays carries it on
// in phase (Mac d03d5b9, HUDDesignCanvas.rerender with the recorded
// entrance): rows already shown stay shown, the rest still enter; past the
// entrance, after a leave, under Reduce Motion and during a sub-page swap
// nothing is carried.
void carriedEntrance(RecordedMeasurer& measurer) {
    World w(measurer); w.appearance(Language::english);
    const d::Point offset{0, 24};
    const auto enterSlot = [](const d::Layer& l) -> const auto& { return l.tracks().slots[std::size_t(d::MotionSlot::enter)][0]; };
    const auto noEnter = [&](FakeCanvas& c) {
        for (const auto* layer : c.page().staggered()) if (layer->animating(w.now)) return false;
        return true;
    };
    const auto rebuild = [&](FakeCanvas& c) {
        const auto before = w.context.pagesBuilt();
        c.rows += 1;                                     // a structural change: one more row
        c.rerender();
        check(w.context.pagesBuilt() == before + 1, "One page is built per re-render");
    };
    {
        FakeCanvas c(endfield::core::Module::clipboard, w.context);
        c.show(w.now); c.setActive(true, w.now);
        const double t0 = w.now;
        c.playEntrance(offset, t0);
        w.advance(.2);
        const auto& oldFirst = *c.page().staggered()[0];
        const double opacity = oldFirst.presented(d::Channel::opacity, w.now), moved = oldFirst.presented(d::Channel::translateY, w.now);
        rebuild(c);
        const auto rows = c.page().staggered();
        check(rows.size() > 3, "The rebuilt page staggers its rows");
        for (std::size_t i = 0; i < rows.size(); ++i)
            check(std::abs(enterSlot(*rows[i]).start - (t0 + d::staggerDelay(int(i)))) < 1e-9 && enterSlot(*rows[i]).fillBackwards == (enterSlot(*rows[i]).start > w.now),
                "Row " + std::to_string(i) + " of the rebuilt page enters at the entrance's start + its stagger (a row not yet started holds its start pose)");
        check(near(rows[0]->presented(d::Channel::opacity, w.now), opacity) && near(rows[0]->presented(d::Channel::translateY, w.now), moved),
            "The first row shows what the replaced page showed");
        check(near(rows.back()->presented(d::Channel::opacity, w.now), 0), "The last row is still at its start pose");
        // Past the entrance (its last row's start plus the title wipe): nothing is carried.
        w.advance(1.02 - .2);
        rebuild(c);
        check(noEnter(c), "A re-render after the entrance carries nothing");
        w.advance(.01);
        rebuild(c);
        check(noEnter(c), "Nor does a later one");
    }
    {
        // The HUD's open delays the entrance .2 s: a rebuild at +.1 still waits for it.
        FakeCanvas c(endfield::core::Module::clipboard, w.context);
        c.show(w.now); c.setActive(true, w.now);
        const double t0 = w.now;
        c.playEntrance(offset, t0, .2);
        w.advance(.1);
        rebuild(c);
        const auto rows = c.page().staggered();
        for (std::size_t i = 0; i < rows.size(); ++i)
            check(std::abs(enterSlot(*rows[i]).start - (t0 + .2 + d::staggerDelay(int(i)))) < 1e-9 && enterSlot(*rows[i]).fillBackwards,
                "Row " + std::to_string(i) + " waits for the delayed entrance");
        check(near(rows[0]->presented(d::Channel::opacity, w.now), 0), "The first row is still hidden before the delayed start");
    }
    {
        // Left (prepareToLeave, then shown again) or deactivated: no carry.
        FakeCanvas c(endfield::core::Module::clipboard, w.context);
        c.show(w.now); c.setActive(true, w.now);
        c.playEntrance(offset, w.now);
        w.advance(.1);
        c.setActive(false, w.now); c.prepareToLeave(w.now); c.show(w.now);
        rebuild(c);
        check(noEnter(c), "A module that left carries no entrance");
        c.setActive(true, w.now);
        c.playEntrance(offset, w.now);
        w.advance(.1);
        c.setActive(false, w.now);
        rebuild(c);
        check(noEnter(c), "A deactivated module carries no entrance");
    }
    {
        // A sub-page swap under way: the rebuilt page waits for the landing, which plays its own entrance.
        FakeCanvas c(endfield::core::Module::clipboard, w.context);
        c.show(w.now); c.setActive(true, w.now);
        c.playEntrance(offset, w.now);
        w.advance(.05);
        c.depth = 1; c.navigate(1);
        const double landing = w.now + d::duration::leave;
        w.advance(.05);
        rebuild(c);
        w.advance(landing - w.now);
        check(!c.transitioning(), "The swap landed");
        const auto rows = c.page().staggered();
        const auto swap = d::subpageOffset(1);
        for (std::size_t i = 0; i < rows.size(); ++i)
            check(std::abs(enterSlot(*rows[i]).start - (landing + d::staggerDelay(int(i)))) < 1e-9, "Row " + std::to_string(i) + " enters with the landing");
        check(near(rows.back()->presented(d::Channel::translateX, landing), rows.back()->value(d::Channel::translateX) + swap.x), "From the swap's offset");
    }
    {
        // Reduce Motion: no entrance is recorded, and one recorded before is dropped.
        FakeCanvas c(endfield::core::Module::clipboard, w.context);
        c.show(w.now); c.setActive(true, w.now);
        c.playEntrance(offset, w.now);
        w.advance(.1);
        w.appearance(Language::english, "FAD41F", false, true);
        rebuild(c);
        check(noEnter(c), "Reduce Motion carries nothing");
        w.appearance(Language::english);
        rebuild(c);
        check(noEnter(c), "The entrance dropped under Reduce Motion stays dropped");
    }
}

// Kept layer ids (the owner's "slight freeze" on Work Mode start and Shelf
// selection): every page that replaces another while the two are never in
// the tree together takes its layer ids, so the scene finds unchanged parts
// under the same ids. In place: any re-render. A sub-page swap: its pending
// page and staging header, which enter the tree only when the leaving ones
// left it. A render-scale change re-renders the shown canvas now and a built
// one that is not shown once, when next shown (as Recording mode).
void keptIds(RecordedMeasurer& measurer) {
    World w(measurer); w.appearance(Language::english);
    FakeCanvas shown(endfield::core::Module::clipboard, w.context), hidden(endfield::core::Module::fileShelf, w.context);
    hidden.show(w.now); hidden.setActive(true, w.now); hidden.setActive(false, w.now); hidden.prepareToLeave(w.now);
    shown.show(w.now); shown.setActive(true, w.now);
    w.advance(1);
    const d::TextServices& text = w.context.env().text;
    const auto artwork = [&](const FakeCanvas& c) {
        std::vector<std::pair<std::string, J>> parts;
        for (auto& part : d::emit(c.root(), {&text, "test."}).parts) parts.emplace_back(part.id, std::move(part.layers));
        return parts;
    };
    const auto ids = [&](const FakeCanvas& c) { std::vector<std::string> out; for (const auto& [id, layers] : artwork(c)) out.push_back(id); return out; };
    const auto unique = [](std::vector<std::string> v) { std::sort(v.begin(), v.end()); return std::adjacent_find(v.begin(), v.end()) == v.end(); };
    // In place: the same page again is byte-equal under the same ids; one more row adds its part, the others stay.
    auto before = artwork(shown);
    shown.rerender();
    check(artwork(shown) == before, "An in-place re-render keeps every part id and byte");
    shown.rows = 13; shown.rerender();
    {
        const auto after = artwork(shown);
        std::size_t rows{}, kept{};
        for (const auto& part : before) if (part.first.find(".fake.row.") != std::string::npos) { ++rows; kept += std::count(after.begin(), after.end(), part); }
        check(after.size() > before.size() && rows >= 2 && kept == rows, "A changed page keeps the ids and bytes of every row it did not change (" + std::to_string(kept) + " of " + std::to_string(rows) + ")");
    }
    shown.rows = 12; shown.rerender(); w.advance(1);
    before = artwork(shown);
    // A sub-page swap: during it only the leaving page and header are in the tree, with no id twice.
    const auto beforeIds = ids(shown);
    const auto pages = w.context.pagesBuilt();
    shown.navigate(1);
    check(shown.transitioning() && ids(shown) == beforeIds && unique(ids(shown)), "During a swap the tree holds only the leaving page and header");
    shown.rerender();
    check(w.context.pagesBuilt() == pages + 2 && ids(shown) == beforeIds, "A re-render during a swap rebuilds only the pending page, outside the tree");
    w.advance(.2);
    check(!shown.transitioning() && ids(shown) == beforeIds && unique(ids(shown)), "The landed page and header take the leaving ones' layer ids");
    w.advance(1);
    check(artwork(shown) == before, "Once its entrance ended, the same page after a swap is byte-equal under the same ids");
    // Render scale: the shown canvas now (its ids kept), the hidden one when next shown.
    const int shownBuilds = shown.builds, hiddenBuilds = hidden.builds;
    const auto scaled = w.context.pagesBuilt();
    w.context.update(d::geometry::baseUnit, 3 / d::geometry::baseUnit, w.now);
    check(shown.builds == shownBuilds + 1 && hidden.builds == hiddenBuilds && w.context.pagesBuilt() == scaled + 1, "A render-scale change builds only the shown page");
    check(!shown.deferredBuild && unique(ids(shown)) && ids(shown) == beforeIds, "The shown page re-renders at the new scale under its ids");
    hidden.show(w.now);
    check(hidden.builds == hiddenBuilds + 1 && hidden.deferredBuild, "A built canvas re-renders once at the new scale when next shown (the Mac's ran unseen)");
    hidden.prepareToLeave(w.now);
    hidden.show(w.now);
    check(hidden.builds == hiddenBuilds + 1, "Showing it again builds nothing");
    hidden.prepareToLeave(w.now);
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

// HUDDesignTextField.textDidEndEditing (:281-286, LIB-85): once the first
// responder leaves an editing field's text view the field commits.
// Context::editorFocusMoved reports that move (a press another owner or the
// shell took); the canvas that got the press keeps its own fields to
// Canvas::pointerDown. Context::confirm (HUDDesignContext.confirm) commits
// nothing itself; Canvas::confirm commits its own fields.
struct FieldHost final : d::TextEditorHost {
    int begins{}, ends{};
    std::string typed{"typed"};
    bool begin(const d::TextEditRequest&) override { ++begins; return true; }
    void end(d::TextField&) override { ++ends; }
    std::string text(const d::TextField&) const override { return typed; }
    std::optional<std::pair<std::uint32_t, std::uint32_t>> selection(const d::TextField&) const override { return std::nullopt; }
    bool hasMarkedText(const d::TextField&) const override { return false; }
    void setText(d::TextField&, std::string_view) override {}
    void reproject(d::TextField&, Rect) override {}
};
class FieldCanvas final : public d::Canvas {
public:
    using Canvas::Canvas;
    d::TextField* field{};
    std::vector<std::string> commits;
    // A body editor (Archive body, Notes TEXT note): focused, and how often it ended.
    bool bodyFocused{}; int bodyEnds{};
    bool editingOtherText() const override { return bodyFocused; }
    void endOtherEditing(bool commit) override { if (commit) ++bodyEnds; bodyFocused = false; }
    void buildPage(d::Page& page) override {
        auto& f = page.make<d::TextField>("field.name", "Name", "Endmin", "Name", d::Size{260, 34});
        f.layer().setOrigin({20, 120}); page.layer().add(f.layer()); page.add(f);
        registerField(f);
        f.onCommit = [this](const std::string& t) { commits.push_back(t); };
        field = &f;
    }
};
void focusMoves(RecordedMeasurer& measurer) {
    World w(measurer); w.appearance(Language::english);
    FieldHost host; w.context.setEditorHost(&host);
    FieldCanvas a(endfield::core::Module::system, w.context), b(endfield::core::Module::profile, w.context);
    for (auto* c : {&a, &b}) { c->show(w.now); c->setActive(true, w.now); }
    // HUDDesignCanvas.refreshRouter: a registered field that the page also
    // lists keeps its page place and is routed (and spoken) once.
    {
        std::vector<d::AccessibilityItem> items; a.accessibilityItems(items);
        check(std::count_if(items.begin(), items.end(), [](const d::AccessibilityItem& i) { return i.id == "field.name"; }) == 1,
            "A field in the page controls is listed once");
    }
    w.context.editorFocusMoved();
    check(host.ends == 0 && a.commits.empty(), "Nothing editing: a focus move changes nothing");
    a.field->beginEditing();
    check(a.field->editing() && host.begins == 1, "The field edits");
    w.context.editorFocusMoved(&a);
    check(a.field->editing() && host.ends == 0, "The canvas that got the press keeps its own field (Canvas::pointerDown decides)");
    host.typed = "Endministrator"; b.field->beginEditing();
    check(a.field->editing() && b.field->editing(), "Two canvases may each hold an editing field");
    w.context.editorFocusMoved(&b);
    check(!a.field->editing() && b.field->editing() && a.commits == std::vector<std::string>{"Endministrator"} && a.field->text() == "Endministrator",
        "A press another canvas took commits this canvas's field with the editor's text");
    w.context.editorFocusMoved();
    check(!b.field->editing() && host.ends == 2 && b.commits.size() == 1, "A press no canvas took commits every editing field");
    a.field->beginEditing();
    w.context.confirm(plainRequest(), [] {});
    check(a.field->editing(), "HUDDesignContext.confirm itself commits nothing (only Canvas::confirm and the shell press do)");
    w.context.overlay().reset();
    a.confirm(plainRequest(), [] {});
    check(!a.field->editing() && host.ends == 3, "Canvas::confirm commits its own field first");
    w.context.overlay().reset();
    // The body editors are text views too (ArchiveCanvas / NotesCanvas
    // textDidEndEditing): a press elsewhere ends them with their save.
    a.bodyFocused = b.bodyFocused = true; a.bodyEnds = b.bodyEnds = 0;
    w.context.editorFocusMoved(&a);
    check(a.bodyFocused && !b.bodyFocused && b.bodyEnds == 1 && a.bodyEnds == 0, "A press another canvas took ends this canvas's body editor, saving");
    w.context.editorFocusMoved();
    check(!a.bodyFocused && a.bodyEnds == 1 && b.bodyEnds == 1, "A press no canvas took ends every focused body editor once");
    // SystemHUDView.beginOverlayPlacement: an open design menu closes; a
    // pop-over is left alone.
    d::Menu::Item item; item.id = "one"; item.title = "One";
    a.openMenu("field.menu", {item}, Rect{20, 120, 260, 34}, nullptr, std::nullopt, [](const d::Menu::Item&) {});
    b.openPopover(std::make_unique<d::Popover>(b.env(), "Pen", std::nullopt, 270, 60), nullptr, Rect{20, 120, 260, 34}, {});
    check(a.menuOpen() && b.popoverOpen(), "A menu and a pop-over are open");
    w.context.closeMenus();
    check(!a.menuOpen() && b.popoverOpen(), "Context::closeMenus closes every canvas's menu and leaves pop-overs");
    b.closePopover();
    w.context.setEditorHost(nullptr);
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
        placementStrip(measurer, overlay);
        canvasScript(measurer, canvas);
        lifetimes(measurer);
        recordingModeWork(measurer);
        keptIds(measurer);
        listsFromTop(measurer);
        delayedEntrance(measurer);
        carriedEntrance(measurer);
        focusMoves(measurer);
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
