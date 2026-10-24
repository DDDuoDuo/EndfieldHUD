// Contracts of the redesigned minigame canvas (modules/orbipom_canvas.*): the
// rules of Sources/OrbiPomCanvas.swift and the smoke test
// Sources/OrbiPomHUDVerification.swift (each check cites the Swift line or
// verification step it ports) over a scripted runtime port, and, when the Mac
// oracle is given (tests/fixtures/play-canvases-source.json from
// tools/play_canvases_reference.sh), every recorded minigame scene node by
// node with the Mac's own text widths, its controls and accessibility items.
// Synthetic state only: no QuickJS VM, no Settings, no Event Log.
#include "modules/orbipom_canvas.hpp"
#include "tests/hud_design_oracle.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <map>
#include <stdexcept>

namespace d = endfield::modules::design;
namespace m = endfield::modules;
using endfield::core::Language;
using namespace endfield::tests::design_oracle;
using C = m::OrbiPomCanvas;
namespace {
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
d::KeyInput keyOf(std::uint32_t vk, char32_t c = 0, bool repeat = false, bool control = false) {
    d::KeyInput k; k.virtualKey = vk; k.character = c; k.repeat = repeat; k.control = control; return k;
}
d::Control* control(d::Canvas& canvas, std::string_view id) {
    for (d::Control* c : canvas.page().controls()) if (c->id() == id) return c;
    if (canvas.showsHeader()) for (d::Control* c : canvas.header().controls()) if (c->id() == id) return c;
    return nullptr;
}
const d::Layer* find(const d::Layer& layer, std::string_view id) {
    if (layer.id().ends_with(id)) return &layer;
    for (const auto* child : layer.children()) if (const auto* f = find(*child, id)) return f;
    return nullptr;
}
std::string textOf(const d::Layer& root, std::string_view id) {
    const auto* l = find(root, id);
    return l && l->text() ? l->text()->text() : std::string("<none>");
}
d::Point centre(const d::Control& c, const d::Layer& root) { const auto r = c.rect(root); return {r.x + r.width / 2, r.y + r.height / 2}; }

struct World {
    d::TextMeasurer& measurer;
    d::Context context;
    double now{100};
    explicit World(d::TextMeasurer& m) : measurer(m), context(m, d::asciiUppercase) { context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); }
    void appearance(Language language, bool light = false, bool reduce = true) {
        d::Context::Appearance a; a.language = language; a.light = light; a.reduceMotion = reduce;
        context.apply(a, now);
    }
    void advance(double seconds) { now += seconds; context.advance(now); }
    void show(d::Canvas& c) { c.show(now); c.setActive(true, now); }
    std::string toast() const {
        const auto& t = const_cast<d::Context&>(context).overlay().toastState().current();
        return t ? t->title + "|" + t->caption : std::string();
    }
};

// The original runtime's published fields, scripted (orbipom_state_tests' Port).
struct Port final : m::OrbiPomRuntimePort {
    m::OrbiPomSnapshot value; std::optional<std::string> failure;
    unsigned starts{}, advances{}, moves{}, ups{}, drops{}, activations{}, cancels{}, pauses{};
    d::Point last{}; double advanced{};
    const m::OrbiPomSnapshot& snapshot() const noexcept override { return value; }
    const std::optional<std::string>& error() const noexcept override { return failure; }
    void start(std::optional<std::uint32_t>) override { ++starts; const auto high = value.highScore; value = {}; value.state = "playing"; value.highScore = high; value.previewVisible = true; }
    void advance(double s) override { ++advances; advanced += s; value.simulationTime += s; }
    void move(d::Point p) override { ++moves; last = p; value.previewX = p.x; }
    void pointerUp(d::Point p) override { ++ups; last = p; }
    bool drop() override { ++drops; return value.canAdvance(); }
    bool activate(m::OrbiPomSkill s) override { ++activations; value.skill = s; value.skillPhase = "selecting"; return true; }
    void cancelSkill() override { ++cancels; value.skill.reset(); value.skillPhase.reset(); }
    void pause(bool v) override { ++pauses; value.paused = v; }
    void highScore(std::int64_t h) override { value.highScore = h; }
};
struct Game {
    Port* port{}; std::vector<m::OrbiPomEvent> events; std::vector<std::int64_t> saves;
    m::OrbiPomSession session;
    explicit Game(std::int64_t best = 0) : session(best, {[this] { auto p = std::make_unique<Port>(); port = p.get(); return p; },
        [this](std::int64_t s) { saves.push_back(s); }, [this](m::OrbiPomEvent e) { events.push_back(e); }}) {}
};

void geometry() {
    // :12-27: unit 1.2, drop zone 32.5, board x 157 ... 433, 276 x 375 from y 81; the column 145 wide from 445.
    check(C::board.x == 157 && C::board.y == 81 && std::abs(C::board.width - 276) < 1e-9 && C::board.height == 375, "Board (157, 81, 276 x 375) (:16-17)");
    check(std::abs(C::column.x - 445) < 1e-9 && std::abs(C::column.width - 145) < 1e-9 && C::column.y == 81, "Column x 445, width 145 (:19)");
    const auto w = C::world({157 + 60, 81 + 100});
    check(std::abs(w.x - 50) < 1e-9 && std::abs(w.y - (100 / 1.2 - 32.5)) < 1e-9, "Box px -> game units (:21-23)");
    const auto b = C::boardPoint(0, 8);
    check(std::abs(b.y - 48.6) < 1e-9 && b.x == 0, "Game units -> board px: the red line at 48.6 (:25-27, :336)");
    check(std::abs(C::pieceSide(1) - 24 * .8 * 1.2) < 1e-9 && std::abs(C::pieceSide(11) - 192 * .8 * 1.2) < 1e-9 && C::pieceSide(0) == C::pieceSide(1)
        && C::pieceSide(99) == C::pieceSide(11), "Sprite boxes 24 ... 192 x .8 x 1.2, levels clamped (:768-777)");
    check(m::orbiPomAssets().size() == 15 && m::orbiPomLevelImage(0).key == "OrbiPom/level-1.png" && m::orbiPomLevelImage(12).key == "OrbiPom/level-11.png"
        && m::orbiPomSkillImage(m::OrbiPomSkill::swap).key == "OrbiPom/skill-swap.png", "The original artwork, level clamped to 1...11 (:728-754)");
}

void lifecycle() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    Game g(71);
    bool foreground = true;
    C canvas(w.context, g.session, [&] { return foreground; });
    w.show(canvas);
    // Verification: showing the game starts the original session on its clock (:97-103).
    check(g.port && g.port->starts == 1 && g.session.snapshot().isPlaying() && canvas.hasFrameTimer() && !g.session.manuallyPaused()
        && g.events == std::vector{m::OrbiPomEvent::started} && w.toast().empty(), "Showing starts a game without a toast; the clock runs (:99)");
    check(canvas.nextWakeTime(w.now) && std::abs(*canvas.nextWakeTime(w.now) - (w.now + 1. / 60)) < 1e-9, "A 60 Hz tick (:121)");
    for (const auto id : {"rules", "pause", "restart", "clear", "wind", "shake", "swap"}) check(control(canvas, id) != nullptr, std::string("Live control ") + id);
    std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
    const auto listed = [&](std::string_view id) { return std::any_of(items.begin(), items.end(), [&](const auto& i) { return i.id == id; }); };
    check(listed("pause") && !listed("resume") && listed("rules") && listed("restart"), "Resume is not listed while running (verification)");
    for (std::size_t i = 0; i < 4; ++i) check(control(canvas, C::skillIDs[i])->enabled() == g.session.snapshot().canUse(m::OrbiPomSkill(i)), "Each skill's enabled state is canUse (:555)");
    // The tick advances by the measured elapsed time (:126-131).
    const double t0 = w.now;
    w.now += 1. / 60; canvas.wake(w.now);
    check(g.port->advances == 1 && std::abs(g.port->advanced - 1. / 60) < 1e-9, "One tick advances the elapsed time");
    // A projected press at (board.minX + 60, board.minY + 100) aims and drops there (verification).
    const d::Point press{C::board.x + 60, C::board.y + 100};
    check(canvas.pointerDown(press, {press}, w.now), "A press inside the board is the module's (:248-253)");
    canvas.pointerUp(press, w.now);
    check(g.port->ups == 1 && std::abs(g.port->last.x - 50) < 1e-9 && std::abs(g.port->last.y - (100 / 1.2 - 32.5)) < 1e-9, "Release drops at the aimed game point (:257-263)");
    // In the drop zone the point is the container's top edge unless a skill aims (:229-233).
    const d::Point zone{C::board.x + 30, C::board.y + 10};
    canvas.pointerDown(zone, {zone}, w.now); canvas.pointerUp(zone, w.now);
    check(g.port->ups == 2 && g.port->last.y == 0, "The drop zone counts as the container's top edge (:232)");
    canvas.pointerDown(press, {press}, w.now); canvas.pointerUp({C::board.x - 10, press.y}, w.now);
    check(g.port->ups == 2, "A release outside the board drops nothing (:259)");
    check(!canvas.pointerDown({100, 200}, {}, w.now), "Left of the board is not the game's (reserved)");
    // Keys (:265-301): Space pauses (no drop), Return drops, P pauses, arrows aim within -32.5 ... 280.
    const auto drops = g.port->drops;
    check(canvas.keyDown(keyOf(d::key::space), w.now) && g.session.manuallyPaused() && !canvas.hasFrameTimer() && g.port->drops == drops, "Space pauses, never drops (MODS-219)");
    check(textOf(canvas.root(), "orbipom.overlay.text") == "[ PAUSED ]", "The board says [ PAUSED ] (:460)");
    items.clear(); canvas.accessibilityItems(items);
    check(listed("resume") && !listed("pause"), "Pause swaps to the Resume control (verification)");
    check(canvas.keyDown(keyOf(d::key::space, 0, true), w.now) && g.session.manuallyPaused(), "A repeated Space is consumed and ignored");
    check(canvas.keyDown(keyOf(d::key::enter), w.now) && g.port->drops == drops, "Return is consumed but cannot drop while paused (:276)");
    canvas.keyDown(keyOf('P', U'p'), w.now);
    check(!g.session.manuallyPaused() && canvas.hasFrameTimer(), "P resumes");
    check(canvas.keyDown(keyOf(d::key::enter), w.now) && g.port->drops == drops + 1, "Return drops (:278)");
    for (int i = 0; i < 20; ++i) canvas.keyDown(keyOf(d::key::up), w.now);
    check(g.port->last.y == -32.5, "Keyboard aim can enter the drop zone, clamped at -32.5 (:289)");
    for (int i = 0; i < 80; ++i) canvas.keyDown(keyOf(d::key::down), w.now);
    check(g.port->last.y == 280, "... and stops at 280");
    check(!canvas.keyDown(keyOf(d::key::escape), w.now), "Esc is not consumed without a selected skill (:273)");
    check(!canvas.keyDown(keyOf(d::key::space, 0, false, true), w.now), "Control+Space passes through (:268)");
    // Skills (:173-180, :215): 1-4 select, pressing again cancels, Esc cancels.
    g.port->value.energy = 3;
    canvas.keyDown(keyOf('1', U'1'), w.now);
    items.clear(); canvas.accessibilityItems(items);
    check(g.session.snapshot().skill == m::OrbiPomSkill::clear && std::any_of(items.begin(), items.end(), [](const auto& i) { return i.id == "clear" && i.on && i.role == d::AccessibilityRole::toggle; }),
        "1 selects Eliminate; its key is an on toggle (:859)");
    canvas.keyDown(keyOf('1', U'1'), w.now);
    check(!g.session.snapshot().skill && g.port->cancels == 1, "Pressing the selected skill again cancels it (MODS-215)");
    control(canvas, "wind")->activate();
    check(g.session.snapshot().skill == m::OrbiPomSkill::wind && control(canvas, "wind")->enabled(), "Wind is chosen and stays enabled while chosen");
    check(canvas.keyDown(keyOf(d::key::escape), w.now) && !g.session.snapshot().skill, "Esc cancels the selected skill");
    // Skill cast toast (:489-492).
    g.port->value.skill = m::OrbiPomSkill::shake; canvas.wake(w.now += 1. / 60);
    g.port->value.skill.reset(); g.port->value.skillUseCount = 1; canvas.wake(w.now += 1. / 60);
    check(w.toast() == "Shake|Skill activated", "A cast skill toasts its name and Skill activated");
    // BEST includes the current score (MODS-210, MODS-228).
    g.port->value.score = 3262; canvas.wake(w.now += 1. / 60);
    check(textOf(canvas.root(), "orbipom.best") == "BEST 3,262" && g.session.bestSoFar() == 3262 && g.session.bestScore() == 71, "BEST climbs live; bestSoFar never saves");
    // The "?" holds play and resumes it (verification, :189-205).
    control(canvas, "rules")->activate();
    check(canvas.rulesPresented() && canvas.popoverOpen() && !canvas.hasFrameTimer() && g.session.snapshot().paused
        && static_cast<d::IconButton*>(control(canvas, "rules"))->isOn(), "How to play opens and holds play");
    check(canvas.keyDown(keyOf(d::key::escape), w.now) && !canvas.rulesPresented() && canvas.hasFrameTimer(), "Esc closes it and play resumes");
    control(canvas, "rules")->activate();
    check(canvas.pointerDown({10, 300}, {}, w.now) && !canvas.rulesPresented() && canvas.hasFrameTimer(), "A press outside closes it (shared pop-over)");
    // Restart while playing asks; cancel keeps the stack, confirm restarts in place (:207-223).
    control(canvas, "restart")->activate();
    const auto& confirm = w.context.overlay().confirmState();
    check(confirm.isOpen() && confirm.request()->id == "minigameRestart" && confirm.request()->title == "Restart the game?" && confirm.request()->message == "The current score is lost."
        && confirm.request()->confirmTitle == "Restart" && !canvas.hasFrameTimer() && canvas.restartConfirmation(), "Restart asks while playing and holds play");
    check(textOf(canvas.root(), "orbipom.overlay.text") == "[ PAUSED ]" && !control(canvas, "pause")->enabled() && !control(canvas, "clear")->enabled(), "The board says PAUSED; pause and skills are off");
    w.context.overlay().decide(d::ConfirmState::Decision::cancel);
    check(!canvas.restartConfirmation() && canvas.hasFrameTimer() && g.port->starts == 1 && g.session.snapshot().score == 3262, "Cancel resumes with the stack kept");
    control(canvas, "restart")->activate();
    w.context.overlay().decide(d::ConfirmState::Decision::confirm);
    check(g.port->starts == 2 && g.session.snapshot().isPlaying() && canvas.hasFrameTimer() && w.toast() == "New game|" && g.events.back() == m::OrbiPomEvent::restarted,
        "Confirm restarts the same runtime and toasts New game (:217)");
    // Game over: the toast and the board; Restart then starts at once (:494-496, :208).
    g.port->value.score = 94; g.port->value.state = "over"; canvas.wake(w.now += 1. / 60);
    check(w.toast() == "Game over|94" && !canvas.hasFrameTimer() && textOf(canvas.root(), "orbipom.overlay.text") == "[ GAME OVER ]", "Game over toasts the score and stops the clock");
    control(canvas, "restart")->activate();
    check(!w.context.overlay().confirmState().isOpen() && g.port->starts == 3 && w.toast() == "New game|", "After game over Restart starts at once, no confirm");
    // Foreground, low power and sleep (:75-86, :121).
    canvas.setForeground(false, w.now);
    check(!canvas.hasFrameTimer() && g.session.snapshot().paused, "Losing the foreground pauses");
    canvas.setForeground(true, w.now);
    canvas.setLowPowerVisualMode(true, w.now);
    check(canvas.hasFrameTimer() && std::abs(*canvas.nextWakeTime(w.now) - (w.now + 1. / 30)) < 1e-9, "Low-power visual mode ticks at 30 Hz");
    canvas.setLowPowerVisualMode(false, w.now);
    canvas.systemSuspended(true, w.now);
    check(!canvas.hasFrameTimer(), "Sleep stops the clock");
    foreground = false; canvas.systemSuspended(false, w.now);
    check(!canvas.hasFrameTimer(), "Wake re-reads the foreground");
    foreground = true; canvas.setForeground(true, w.now);
    // Manual pause survives leaving; only Resume restarts it (verification).
    canvas.keyDown(keyOf(d::key::space), w.now);
    canvas.prepareToLeave(w.now); canvas.setActive(false, w.now);
    check(g.session.manuallyPaused() && g.session.snapshot().paused && !canvas.hasFrameTimer(), "Leaving keeps the manual pause and stops the clock (:112)");
    w.show(canvas);
    check(g.session.manuallyPaused() && !canvas.hasFrameTimer() && g.port->starts == 3, "Returning shows the paused game, no new start");
    control(canvas, "resume")->activate();
    check(!g.session.manuallyPaused() && canvas.hasFrameTimer(), "Resume restarts it");
    // A restart confirm is dismissed when the module leaves (:108).
    control(canvas, "restart")->activate();
    canvas.prepareToLeave(w.now); canvas.setActive(false, w.now);
    check(!w.context.overlay().confirmState().isOpen() && !canvas.restartConfirmation(), "Leaving dismisses the restart confirm");
    (void)t0;
}

void board() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::simplifiedChinese, false, false);
    Game g;
    C canvas(w.context, g.session);
    w.show(canvas);
    auto& s = g.port->value;
    // English-only labels in every language (MODS-213).
    check(textOf(canvas.root(), "orbipom.sp.label") == "SP" && canvas.header().counterText() == "SCORE", "SP and the rule counter SCORE stay English");
    // Pieces are retained layers placed numerically (:559-580).
    s.bodies = {{1, 3, 100, 200, .5, 32, 1, 1}, {2, 5, 150, 250, 0, 51.2, .8, 1.2}};
    s.selectedBodyIDs = {2};
    canvas.wake(w.now += 1. / 60);
    check(canvas.bodyLayerCount() == 2, "One layer per body (verification)");
    const auto* first = find(canvas.root(), "orbipom.piece.1");   // body 2 (pieces are made in body order)
    check(first && first->position() == C::boardPoint(150, 250) && first->value(d::Channel::scaleX) == 1.2 && first->value(d::Channel::opacity) == .8
        && first->bounds().width == 51.2 * 1.2, "A body's layer is placed at its point, scale and opacity in its sprite box");
    const auto revision = canvas.contentRevision();
    s.bodies[0].x = 101; s.bodies[0].angle = .7;
    canvas.wake(w.now += 1. / 60);
    check(canvas.contentRevision() == revision, "Moving bodies is no content event");
    s.bodies.erase(s.bodies.begin()); s.bodies.push_back({3, 1, 20, 20, 0, 19.2, 1, 1});
    canvas.wake(w.now += 1. / 60);
    check(canvas.bodyLayerCount() == 2 && find(canvas.root(), "orbipom.piece.0") != nullptr && !find(canvas.root(), "orbipom.piece.2"), "A merged body's layer is reused by the next one");
    // Ghost and guide (:581-591): only while it can advance and no skill aims.
    s.previewX = 100; s.currentLevel = 2; s.previewSize = 25.6;
    canvas.wake(w.now += 1. / 60);
    const auto* ghost = find(canvas.root(), "orbipom.ghost");
    const auto* guide = find(canvas.root(), "orbipom.guide");
    check(ghost && !ghost->hidden() && ghost->value(d::Channel::opacity) == .5 && guide && !guide->hidden() && guide->position().x == 120, "Ghost at 50 % and the guide at its x");
    s.skill = m::OrbiPomSkill::clear;
    canvas.wake(w.now += 1. / 60);
    check(ghost->hidden() && guide->hidden(), "No ghost while a skill aims");
    s.skill.reset();
    // Danger (:526-543): one decimal inside the board, the line pulses while it can advance.
    s.dangerSeconds = 4.26;
    canvas.wake(w.now += 1. / 60);
    check(textOf(canvas.root(), "orbipom.countdown") == "4.3", "Countdown %.1f");
    const auto* danger = find(canvas.root(), "orbipom.danger");
    const auto tintA = danger->tint();
    canvas.wake(w.now += .1);
    check(danger->tint() != tintA && danger->tint()->r < d::fixed::red.r, "The red line pulses between #E5383B and #661B1D (:538-542)");
    canvas.keyDown(keyOf(d::key::space), w.now);
    check(danger->tint() == d::fixed::red && textOf(canvas.root(), "orbipom.countdown") == "4.3", "Paused: the solid red line without the pulse (:536)");
    canvas.keyDown(keyOf(d::key::space), w.now);
    w.appearance(Language::simplifiedChinese, false, true);
    s.dangerSeconds = 3.0;
    canvas.wake(w.now += .1);
    check(danger->tint() == d::fixed::red, "Reduce Motion: no pulse once the line renders again (:536)");
    w.appearance(Language::simplifiedChinese, false, false);
    s.dangerSeconds = -0.2;
    canvas.wake(w.now += 1. / 60);
    check(textOf(canvas.root(), "orbipom.countdown") == "0.0", "max(0, seconds)");
    s.dangerSeconds.reset();
    canvas.wake(w.now += 1. / 60);
    check(find(canvas.root(), "orbipom.countdown")->hidden() && danger->tint() == d::rgb(0xE5383B, .45), "Idle: the dashed line at 45 %");
    // SP (:465, :470-473): pad2/03 and 12 cells.
    s.energy = 2; s.energyProgress = 7.9;
    canvas.wake(w.now += 1. / 60);
    check(textOf(canvas.root(), "orbipom.sp.energy") == "02/03", "Energy pad2(energy)/03");
    s.swapCharge = 9;
    canvas.wake(w.now += 1. / 60);
    check(textOf(canvas.root(), "swap.cost") == "6/6" && textOf(canvas.root(), "wind.cost") == "2", "Swap shows min(6, charge)/6; others their cost (:553)");
    // A session error (:458, :155).
    g.port->failure = "Synthetic runtime error";
    canvas.wake(w.now += 1. / 60);
    check(textOf(canvas.root(), "orbipom.overlay.text") == "[ UNAVAILABLE ]" && !canvas.hasFrameTimer(), "An error stops the clock; the board says UNAVAILABLE");
}

// --- The Mac oracle (play-canvases-source.json, minigame rows) ------------------------------------
// Widths and metrics from the oracle's CoreText recordings; line counts of the
// How to play paragraphs from the Mac's own HUDText.lines.
struct Measurer final : d::TextMeasurer {
    RecordedMeasurer recorded;
    std::map<std::string, std::size_t> lines;
    static std::string key(std::string_view text, double size, int weight, double width) {
        return std::string(text) + "|" + RecordedMeasurer::number(size) + "|" + std::to_string(weight) + "|" + RecordedMeasurer::number(width);
    }
    double width(std::span<const d::TextRun> runs, d::TextRegion region) override { return recorded.width(runs, region); }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion region, double scale) override { return recorded.metrics(s, region, scale); }
    std::size_t lineCount(std::span<const d::TextRun> runs, d::TextRegion region, double width, int maximumLines) override {
        if (runs.size() == 1) if (const auto it = lines.find(key(runs[0].text, runs[0].style.size, int(d::resolved(runs[0].style.weight)), width)); it != lines.end()) return it->second;
        return d::TextMeasurer::lineCount(runs, region, width, maximumLines);
    }
};
struct Oracle {
    const Json& data;
    Measurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const Json& j) : data(j) {
        recordText(measurer.recorded, j["text"]);
        for (const auto& row : j["minigame"].array()) if (row["guideLines"].isArray())
            for (const auto& g : row["guideLines"].array()) measurer.lines[Measurer::key(g["text"].string(), g["size"].number(), int(g["weight"].number()), g["width"].number())] = std::size_t(g["lines"].number());
    }
    const Json* row(std::string_view state, std::string_view language = "en", bool light = false) const {
        for (const auto& r : data["minigame"].array()) if (r["state"].string() == state && r["language"].string() == language && r["light"].boolean() == light) return &r;
        return nullptr;
    }
    static Json rooted(const Json& layer) {
        Json copy = layer;
        Point anchor{.5, .5};
        if (copy["anchorPoint"].isArray()) anchor = {copy["anchorPoint"].array()[0].number(), copy["anchorPoint"].array()[1].number()};
        const Rect b = rectOf(copy["bounds"]);
        copy["position"] = Json::Array{anchor.x * b.width + b.x, anchor.y * b.height + b.y};
        copy["transform"] = Json();
        return copy;
    }
    void scene(const Json& r, d::Canvas& canvas) {
        const std::string label = "minigame/" + r["state"].string() + "/" + r["language"].string() + (r["light"].boolean() ? "/light" : "");
        ++compared;
        Json expected = r;
        expected["layer"] = rooted(r["layer"]);
        endfield::tests::design_oracle::compare(label, expected, canvas.root(), canvas.root(), canvas.page().controls(), failures);
        std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
        compareAccessibility(label, r["accessibility"], items, failures);
        if (r["top"].isObject()) endfield::tests::design_oracle::compare(label + "/top", Json::Object{{"layer", r["top"]}}, canvas.top(), canvas.root(), {}, failures);
        if (canvas.header().counterText() != r["header"]["counter"].string()) failures.push_back(label + ": counter " + canvas.header().counterText());
    }
};
std::optional<m::OrbiPomSkill> skillOf(const Json& j) {
    if (!j.isString()) return std::nullopt;
    for (std::size_t i = 0; i < C::skillIDs.size(); ++i) if (C::skillIDs[i] == j.string()) return m::OrbiPomSkill(i);
    return std::nullopt;
}
std::optional<double> numberOr(const Json& j) { return j.isNumber() ? std::optional(j.number()) : std::nullopt; }
m::OrbiPomSnapshot snapshotOf(const Json& s) {
    m::OrbiPomSnapshot v;
    v.state = s["state"].string(); v.paused = s["paused"].boolean();
    v.score = s["score"].integer(); v.highScore = s["highScore"].integer(); v.mergeCount = s["mergeCount"].integer(); v.skillUseCount = s["skillUseCount"].integer();
    v.energy = int(s["energy"].integer()); v.swapCharge = int(s["swapCharge"].integer()); v.energyProgress = s["energyProgress"].number();
    v.currentLevel = int(s["currentLevel"].integer()); v.nextLevel = int(s["nextLevel"].integer());
    v.previewX = s["previewX"].number(); v.previewY = s["previewY"].number(); v.previewSize = s["previewSize"].number();
    v.previewVisible = s["previewVisible"].boolean(); v.previewScale = s["previewScale"].number();
    v.dangerSeconds = numberOr(s["dangerSeconds"]); v.skill = skillOf(s["skill"]);
    if (s["skillPhase"].isString()) v.skillPhase = s["skillPhase"].string();
    for (const auto& id : s["selectedBodyIDs"].array()) v.selectedBodyIDs.push_back(id.integer());
    if (s["hoverBodyID"].isNumber()) v.hoverBodyID = s["hoverBodyID"].integer();
    v.windSurfaceY = numberOr(s["windSurfaceY"]);
    for (const auto& b : s["bodies"].array())
        v.bodies.push_back({b["id"].integer(), int(b["level"].integer()), b["x"].number(), b["y"].number(), b["angle"].number(), b["size"].number(), b["opacity"].number(), b["scale"].number()});
    v.simulationTime = s["simulationTime"].isNumber() ? s["simulationTime"].number() : 0;
    return v;
}
// Each recorded state rebuilt through the canvas's own entry points: the
// runtime publishes the recorded snapshot, the canvas starts the game when it
// shows, and pause / rules / restart are pressed as the Mac oracle did.
void oracleScenes(Oracle& o) {
    for (const auto& r : o.data["minigame"].array()) {
        const auto& input = r["input"];
        World w(o.measurer);
        w.appearance(r["language"].string() == "zh-Hans" ? Language::simplifiedChinese : Language::english, r["light"].boolean());
        m::OrbiPomSnapshot recorded = snapshotOf(input["snapshot"]);
        struct Scripted final : m::OrbiPomRuntimePort {
            m::OrbiPomSnapshot value, recorded; std::optional<std::string> failure;
            const m::OrbiPomSnapshot& snapshot() const noexcept override { return value; }
            const std::optional<std::string>& error() const noexcept override { return failure; }
            void start(std::optional<std::uint32_t>) override { value = recorded; }
            void advance(double) override {}
            void move(d::Point) override {} void pointerUp(d::Point) override {}
            bool drop() override { return false; } bool activate(m::OrbiPomSkill s) override { value.skill = s; return true; }
            void cancelSkill() override { value.skill.reset(); }
            void pause(bool v) override { value.paused = v; }
            void highScore(std::int64_t) override {}
        };
        const bool paused = recorded.paused;
        m::OrbiPomSession session(input["bestScore"].integer(), {[&] { auto p = std::make_unique<Scripted>(); p->recorded = recorded; return p; }});
        C canvas(w.context, session);
        canvas.automaticallySchedulesFrames = false;
        w.show(canvas);
        if (input["manuallyPaused"].boolean()) canvas.perform("pause");
        if (input["rulesPresented"].boolean()) canvas.perform("rules");
        if (input["restartConfirmation"].boolean()) canvas.perform("restart");
        (void)paused;
        o.scene(r, canvas);
        if (canvas.bodyLayerCount() != std::size_t(input["bodyLayers"].integer())) o.failures.push_back(r["state"].string() + ": body layers");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        geometry();
        lifecycle();
        board();
        if (const auto data = loadOracle(argc > 1 ? argv[1] : nullptr, {"minigame"}, {"text", "layerDefaults"})) {
            Oracle oracle(*data);
            oracleScenes(oracle);
            std::cout << "oracle: " << oracle.compared << " minigame scenes compared, " << oracle.measurer.recorded.misses.size() << " unrecorded measurements\n";
            for (const auto& miss : oracle.measurer.recorded.misses) std::cout << "  unrecorded: " << miss << '\n';
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
        } else std::cout << "play-canvases-source.json not present: Swift-derived contracts only\n";
        std::cout << "PASS " << checks << " OrbiPom canvas checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
