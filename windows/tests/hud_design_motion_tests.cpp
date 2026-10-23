// Design library L1 motion contracts (LIBRARY-DESIGN.md section 14 item 2):
// Track sampling with Core Animation's timing semantics (group composition,
// keyframes, delays with backwards fill, fill forwards, repeat), the stagger
// and shell offsets (Sources/HUDDesignMotion.swift), Deadlines, the idle-hiding
// scroll bar (HUDDesignBadges.swift HUDScrollBarVisibility :305-368) and the
// caret blink (HUDProjectedTextEditor.swift:251-271). Vectors derive from the
// Swift formulas (lines cited); the Mac oracle's animation dumps are compared
// when the L3 fixture is present (argv[1]). Caller clock only, portable.
#include "modules/hud_design_motion.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace d = endfield::modules::design;
namespace c = endfield::core;
using J = ehud::data::Json;
unsigned checks{};
void check(bool value, const std::string& what) { ++checks; if (!value) throw std::runtime_error(what); }
template <class F> void rejects(F fn, const char* what) { bool rejected{}; try { fn(); } catch (const std::exception&) { rejected = true; } check(rejected, what); }
bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) <= e; }
const auto E = d::easing::standard;

void tracks() {
    // CABasicAnimation (HUDMotion.animate :42-55): from -> to on its timing.
    auto t = d::Track::basic(2, 10, 1, .22, E);
    check(t.present() && t.keys == 2 && t.end() == 1.22 && !t.additive && !t.fillForwards && !t.fillBackwards, "A basic track");
    check(!t.active(.99) && t.active(1) && t.active(1.2199) && !t.active(1.22) && t.finished(1.22) && !t.finished(1.1), "Active over [start, start + duration), no fills");
    check(t.sample(1) == 2 && near(t.sample(1.11), 2 + 8 * E.value(.5)) && near(t.sample(1.2199), 2 + 8 * E.value(.2199 / .22)), "Values on the curve E");
    check(t.running(.5) && t.running(1.1) && !t.running(1.22), "Running from creation until its end (delays included)");
    // A delay fills backwards (:53): the from value shows before the start.
    t.fillBackwards = true;
    check(t.active(.5) && t.sample(.5) == 2, "Backwards fill shows the from value");
    // Fill forwards holds the end (leave, hold, bandOut).
    auto held = d::Track::basic(1, 0, 0, .13, d::easing::ease); held.fillForwards = true;
    check(held.active(5) && held.sample(5) == 0 && !held.finished(5) && !held.running(5), "Fill forwards holds the end without asking for frames");
    // Keyframes (calculationMode linear) with a group curve over a linear child.
    d::Track k; k.keys = 3; k.values = {0, 1, 1}; k.keyTimes = {0, .6, 1}; k.duration = .38; k.outer = E;
    for (const double at : {0.0, .05, .1, .19, .3, .37}) {
        const double x = E.value(at / .38);
        check(near(k.sample(at), std::min(1.0, x / .6)), "Keyframes are linear between key times on the group's eased clock");
    }
    // Group composition: the child's own curve applies after the group's (E(E(t))).
    auto composed = d::Track::basic(20, 0, 0, .38, E); composed.inner = E;
    check(near(composed.sample(.1), 20 * (1 - E.value(E.value(.1 / .38)))), "inner(outer(t))");
    // Repeats: a 1.06 s loop forever, never finished, always running.
    d::Track loop; loop.keys = 5; loop.values = {1, 1, 0, 0, 1}; loop.keyTimes = {0, .5, .58, .92, 1}; loop.duration = 1.06; loop.repeat = std::numeric_limits<double>::infinity();
    check(loop.active(1000) && !loop.finished(1000) && loop.running(1000) && near(loop.sample(1.06 * 3 + .54 * 1.06), .5) && loop.sample(1.06 * 7 + .75 * 1.06) == 0, "Infinite repeat cycles its keyframes");
    d::Track twice = loop; twice.repeat = 2; twice.fillForwards = true;
    check(twice.end() == 2.12 && twice.sample(3) == 1 && twice.active(3), "A finite repeat holds the end of its last cycle");
    d::Track half = twice; half.repeat = 1.5;
    check(near(half.sample(5), 1 - (.5 - .5) * 0) && half.sample(5) == 1, "A fractional repeat stops part-way through its cycle");
    // A key time pair with no span takes the later value.
    d::Track step; step.keys = 3; step.values = {0, 0, 1}; step.keyTimes = {0, .5, .5}; step.duration = 1;
    check(step.sample(.25) == 0 && step.sample(.75) == 1, "Zero-length key spans jump");
    // enter / leave kinds go through core::DesignMotion (the one implementation).
    d::Track enter; enter.kind = d::Track::Kind::enterOpacity; enter.fromOpacity = .8; enter.duration = .38; enter.outer = E;
    d::Track move; move.kind = d::Track::Kind::enterTranslation; move.offset = 44; move.additive = true; move.duration = .38; move.outer = E;
    for (const double at : {-.05, 0.0, .1, .2, .3}) {
        const auto expected = c::DesignMotion::enterAt(at, {44, 0}, .8);
        check(near(enter.sample(at), expected.opacity) && near(move.sample(at), expected.translation.x), "enter tracks sample DesignMotion::enterAt");
        // HUDMotion.enter (:58-75) from the formula itself.
        const double x = E.value(std::clamp(at / .38, 0.0, 1.0));
        check(near(enter.sample(at), .8 * std::min(1.0, x / .6)) && near(move.sample(at), 44 * (1 - E.value(x))), "stIn: opacity [0,o,o] at [0,.6,1] on E(t); translation 1 - E(E(t))");
    }
    d::Track out; out.kind = d::Track::Kind::leaveOpacity; out.fromOpacity = .7; out.duration = .13; out.fillForwards = true;
    d::Track drift; drift.kind = d::Track::Kind::leaveTranslation; drift.offset = -44; drift.additive = true; drift.duration = .13; drift.fillForwards = true;
    const double q = d::easing::ease.value(.5);
    check(near(out.sample(.065), .7 * (1 - q)) && near(drift.sample(.065), -.6 * -44 * q) && out.sample(1) == 0 && near(drift.sample(1), 26.4), "leave (:88-102): .13 s ease to 0 and -0.6 x offset, held");
    // Colour tracks lerp sRGB components (CA's CGColor interpolation).
    d::ColorTrack ct; ct.keys = 2; ct.values = {d::rgb(0x2A2A2A), d::rgb(0xE9CF12)}; ct.keyTimes = {0, 1}; ct.duration = .22; ct.timing = d::easing::ease;
    const auto mid = ct.sample(.11);
    check(near(mid.r, 42 / 255. + (233 - 42) / 255. * q) && near(mid.b, 42 / 255. + (18 - 42) / 255. * q) && ct.active(.2) && !ct.active(.22) && ct.running(.2), "Tint lerps in sRGB");
}

void offsets() {
    // HUDMotion.staggerDelay (:78-80).
    check(near(d::staggerDelay(0), .06) && near(d::staggerDelay(3), .06 + 3 * .036) && near(d::staggerDelay(14), .06 + 14 * .036) && d::staggerDelay(40) == d::staggerDelay(14) && d::staggerDelay(-2) == .06, "Stagger .06 + min(max(0, i), 14) x .036");
    // HUDShellPosition (:188-235).
    const auto first = d::entranceOffset(std::nullopt, c::Module::system);
    const double dx = 417 - 975, dy = 396 - 575, n = std::hypot(dx, dy);
    check(near(first.x, dx / n * 44) && near(first.y, dy / n * 44), "The first module enters from the disc centre (975, 575)");
    const auto swap = d::entranceOffset(c::Module::clipboard, c::Module::archive);
    check(near(swap.x, 107 / std::hypot(107., 5.) * 44) && near(swap.y, -5 / std::hypot(107., 5.) * 44), "normalize(B - A) x 44 between shell positions");
    check(d::entranceOffset(c::Module::map, c::Module::map) == d::Point{}, "A zero vector stays (0, 0)");
    check(d::subpageOffset(1) == d::Point{44, 0} && d::subpageOffset(-1) == d::Point{-44, 0} && d::subpageOffset(0, 1) == d::Point{0, 44} && d::subpageOffset(0) == d::Point{}, "Sub-pages: forward from the right, back from the left, +dy from below");
}

void deadlines() {
    d::Deadlines list;
    std::vector<std::string> ran;
    const auto a = list.token(), b = list.token();
    check(a != b && a && b && list.empty() && !list.next(), "Tokens are distinct and non-zero");
    list.after(2, a, [&] { ran.push_back("a2"); });
    list.after(1, b, [&] { ran.push_back("b1"); });
    list.after(1, a, [&] { ran.push_back("a1"); list.after(1.5, b, [&] { ran.push_back("nested"); }); });
    list.after(5, b, [&] { ran.push_back("b5"); });
    check(list.next() == 1, "next() is the earliest deadline");
    check(!list.advance(.5) && ran.empty(), "Nothing is due early");
    check(list.advance(2) && ran == std::vector<std::string>{"b1", "a1", "nested", "a2"}, "Due work runs by time, then scheduling order; work scheduled meanwhile for <= now runs too");
    list.cancel(b);
    check(list.empty() && !list.next() && !list.advance(10), "cancel drops a token's pending work");
    int count{};
    std::function<void()> again = [&] { ++count; list.after(0, a, again); };
    list.after(0, a, again);
    check(list.advance(0, 10) && count == 10 && !list.empty(), "A runaway chain stops at the limit");
    list.cancel(a);
    rejects([&] { list.after(NAN, a, [] {}); }, "A deadline needs a finite time");
    rejects([&] { list.after(1, a, {}); }, "A deadline needs work");
    list.after(1, a, [&] { list.cancel(a); ran.push_back("self"); });
    list.after(1, a, [&] { ran.push_back("never"); });
    list.advance(1);
    check(ran.back() == "self" && list.empty(), "Work may cancel its own group");
}

void scrollBar() {
    using V = d::ScrollBarVisibility;
    V bar;
    check(!bar.shown() && bar.opacity(0) == 0 && !bar.nextDeadline() && !bar.animating(0), "Hidden at rest, nothing armed");
    bar.noteScroll(10, false);
    check(bar.shown() && bar.nextDeadline() == 11 && near(bar.opacity(10.075), d::easing::easeOut.value(.5)) && bar.animating(10.1) && bar.opacity(10.15) == 1, "Scroll: fade in .15 easeOut, one hide 1 s later");
    bar.noteScroll(10.5, false);
    check(bar.nextDeadline() == 11, "A burst keeps the one armed hide");
    check(!bar.advance(10.9, false) && bar.advance(11, false) && bar.shown() && near(*bar.nextDeadline(), 11.5), "Due while the burst went on: re-armed for the rest (max(.05, last + 1 - now))");
    check(bar.advance(11.5, false) && !bar.shown() && !bar.nextDeadline(), "Then hides");
    check(near(bar.opacity(11.65), 1 - d::easing::easeIn.value(.5)) && bar.opacity(11.8) == 0 && !bar.animating(11.8), "Fade out .30 easeIn");
    // Hover never hides; leaving arms the hide from that moment.
    bar.setHovered(true, 20, false);
    check(bar.shown() && !bar.nextDeadline(), "Hovering the strip shows the bar, arms nothing");
    bar.noteScroll(20.2, false);
    check(bar.advance(21.2, false) && bar.shown() && !bar.nextDeadline(), "A due hide while hovered does nothing and is not re-armed");
    bar.setHovered(false, 22, false);
    check(bar.nextDeadline() == 23 && bar.advance(23, false) && !bar.shown(), "Leaving arms the hide 1 s later");
    // Dragging shows; its end counts as a scroll.
    bar.setDragging(true, 30, false);
    check(bar.shown() && !bar.nextDeadline(), "A drag shows the bar");
    bar.setDragging(false, 31, false);
    check(bar.nextDeadline() == 32, "The drag's end counts as a scroll");
    // reset(): hidden at once; a fade-out under way keeps running.
    bar.reset();
    check(!bar.shown() && bar.opacity(31) == 0 && !bar.nextDeadline(), "reset hides at once");
    bar.noteScroll(40, false); bar.advance(41, false);
    bar.reset();
    check(near(bar.opacity(41.15), 1 - d::easing::easeIn.value(.5)), "reset leaves a fade-out alone (guard isShown)");
    // Reduce Motion shows and hides at once; the minimum delay.
    V reduced;
    reduced.noteScroll(0, true);
    check(reduced.opacity(0) == 1 && !reduced.animating(0), "Reduce Motion shows at once");
    reduced.advance(1, true);
    check(reduced.opacity(1) == 0 && !reduced.shown(), "and hides at once");
    V late;
    late.setHovered(true, 0, false); late.setHovered(false, 0, false);
    late.noteScroll(5, false);
    check(late.nextDeadline() == 1, "The armed hide is not moved by later activity");
    check(late.advance(1, false) && near(*late.nextDeadline(), 6), "It re-arms for the rest of the burst");
    V tiny;
    tiny.noteScroll(0, false); tiny.advance(1, false);
    check(!tiny.shown(), "Exactly 1 s after the last activity it hides");
    V edge;
    edge.noteScroll(0, false); edge.noteScroll(.97, false); edge.advance(1, false);
    check(edge.shown() && near(*edge.nextDeadline(), 1.97), "A remaining linger above .05 s re-arms");
    V last;
    last.noteScroll(0, false); last.noteScroll(.96, false);
    last.advance(1.92, false);
    check(!last.shown(), "A remaining linger of .04 s hides now");
}

void caret() {
    d::CaretBlink blink;
    blink.restart(10);
    const double P = d::CaretBlink::period;
    check(blink.opacity(10, false) == 1 && blink.opacity(10 + .25 * P, false) == 1 && near(blink.opacity(10 + .54 * P, false), .5) && blink.opacity(10 + .75 * P, false) == 0, "1, 1, 0, 0, 1 at 0, .5, .58, .92, 1");
    check(near(blink.opacity(10 + .96 * P, false), .5) && near(blink.opacity(10 + 3 * P + .54 * P, false), .5), "The ramps repeat every 1.06 s");
    check(!blink.ramping(10 + .25 * P, false) && blink.ramping(10 + .54 * P, false) && !blink.ramping(10 + .75 * P, false) && blink.ramping(10 + .95 * P, false), "Frames only during the two 80 ms ramps");
    check(near(*blink.nextChange(10, false), 10 + .5 * P) && near(*blink.nextChange(10 + .54 * P, false), 10 + .58 * P) && near(*blink.nextChange(10 + .6 * P, false), 10 + .92 * P)
        && near(*blink.nextChange(10 + .95 * P, false), 11.06), "nextChange: the next ramp start or end");
    check(near(*blink.nextChange(9, false), 10 + .5 * P) && blink.opacity(9, false) == 1, "Before the origin it is solid");
    blink.restart(10 + .54 * P);
    check(blink.opacity(10 + .54 * P, false) == 1, "A caret move restarts it solid");
    check(blink.opacity(10.6, true) == 1 && !blink.ramping(10.6, true) && !blink.nextChange(10.6, true), "Reduce Motion keeps it solid, no wakes");
}

// TODO(lead): the L3 oracle fixture dumps the Mac's animations (key, keyPath,
// values/keyTimes, duration, begin offset, timing control points, fill mode,
// additive, repeat; groups with children). When a "motion" section exists,
// each dumped animation is rebuilt as Tracks and compared at its recorded
// sample times. Expected shape:
//   motion.samples[]: {verb: "enter"|"leave"|"animate", offset:[x,y], opacity, delay,
//                      duration, timing:[x1,y1,x2,y2], times:[...], opacities:[...], translation:[[x,y]...]}
void oracle(const char* path) {
    if (!std::filesystem::exists(path)) { std::cout << "hud_design_motion: oracle fixture not present yet (" << path << "); Swift-derived vectors only (TODO lead)\n"; return; }
    std::ifstream file(path, std::ios::binary);
    const auto json = J::parse(std::string((std::istreambuf_iterator<char>(file)), {}), 64 * 1024 * 1024);
    const auto& samples = json["motion"]["samples"];
    if (!samples.isArray()) { std::cout << "hud_design_motion: oracle has no motion.samples yet (TODO lead: map its schema here)\n"; return; }
    for (const auto& s : samples.array()) {
        const auto verb = s["verb"].string();
        const auto& offset = s["offset"].array();
        const auto& times = s["times"].array();
        for (std::size_t i = 0; i < times.size(); ++i) {
            const double at = times[i].number();
            if (verb == "enter") {
                const auto& tm = s["timing"].array();
                const c::CubicTiming timing{tm[0].number(), tm[1].number(), tm[2].number(), tm[3].number()};
                d::Track o; o.kind = d::Track::Kind::enterOpacity; o.fromOpacity = s["opacity"].number(); o.duration = s["duration"].number(); o.outer = timing; o.start = s["delay"].number(); o.fillBackwards = o.start > 0;
                d::Track x = o; x.kind = d::Track::Kind::enterTranslation; x.offset = offset[0].number(); x.additive = true;
                check(near(o.sample(at), s["opacities"].array()[i].number(), 1e-4) && near(x.sample(at), s["translation"].array()[i].array()[0].number(), 1e-3), "Oracle enter sample");
            } else if (verb == "leave") {
                d::Track o; o.kind = d::Track::Kind::leaveOpacity; o.fromOpacity = s["opacity"].number(); o.duration = .13; o.fillForwards = true;
                d::Track x = o; x.kind = d::Track::Kind::leaveTranslation; x.offset = offset[0].number(); x.additive = true;
                check(near(o.sample(at), s["opacities"].array()[i].number(), 1e-4) && near(x.sample(at), s["translation"].array()[i].array()[0].number(), 1e-3), "Oracle leave sample");
            }
        }
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: hud_design_motion_tests <hud-design-source.json>");
        tracks(); offsets(); deadlines(); scrollBar(); caret(); oracle(argv[1]);
        std::cout << "hud_design_motion contracts: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "hud_design_motion contracts failed after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
