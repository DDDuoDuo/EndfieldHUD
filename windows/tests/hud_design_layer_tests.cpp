// Design library L1 layer-tree contracts (LIBRARY-DESIGN.md section 14 item 3):
// the CALayer model (frames about the anchor, actionless setters, revisions),
// the HUDMotion verbs (Sources/HUDDesignMotion.swift :42-183) with Core
// Animation's application order, emission into parts and surfaces (surface
// split, static compaction, fragments around boundaries, nested parts, reuse
// keys, node budget), samplePoses (transform order, clips, wipes, tint, the
// opacity product; no allocation), model-tree hit geometry
// (HUDDesignControl.swift isVisible :149-157, HUDDesignScreen.swift
// isUnclipped :663-672), arena lifetime and the "animate a static layer
// throws" rule. Portable; argv[1] is the optional L3 oracle fixture.
#include "modules/hud_design_layer.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace { std::atomic<bool> counting{}; std::atomic<std::size_t> allocations{}; }
void* operator new(std::size_t n) { if (counting) ++allocations; if (auto* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
#if defined(__cpp_sized_deallocation)
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

namespace {
namespace d = endfield::modules::design;
namespace m = endfield::modules::design::motion;
using J = ehud::data::Json;
using C = d::Channel;
unsigned checks{};
void check(bool value, const std::string& what) { ++checks; if (!value) throw std::runtime_error(what); }
template <class F> void rejects(F fn, const char* what) { bool rejected{}; try { fn(); } catch (const std::exception&) { rejected = true; } check(rejected, what); }
bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) <= e; }
bool near(d::Point a, d::Point b, double e = 1e-9) { return near(a.x, b.x, e) && near(a.y, b.y, e); }
const auto E = d::easing::standard;

// Text widths from character counts; only text emission needs it here.
struct Measurer final : d::TextMeasurer {
    double width(std::span<const d::TextRun> runs, d::TextRegion) override { double w{}; for (const auto& r : runs) w += double(r.text.size()) * r.style.size * .5; return w; }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion, double scale) override { return {s.size * s.lineHeight * scale, .928 * s.size * scale, .244 * s.size * scale}; }
};

void affine() {
    const auto t = d::Affine::translation(3, 4), s = d::Affine::scale(2, 5), r = d::Affine::rotation(std::numbers::pi / 2);
    check(near((t * s).apply({1, 1}), {5, 9}) && near((s * t).apply({1, 1}), {8, 25}), "A * B applies B first (CGAffineTransform concatenation order)");
    check(near(r.apply({1, 0}), {0, 1}) && near(d::Affine::skew(.5).apply({0, 2}), {1, 2}), "rotation.z and the NEW badge skew");
    const auto inv = (t * r * s).inverse();
    check(inv && near((*inv * t * r * s).apply({7, -3}), {7, -3}) && !d::Affine::scale(0, 1).inverse(), "Inverse; singular has none");
}

void model() {
    d::LayerArena arena("page");
    auto& l = arena.make("row");
    check(l.id() == "page.row" && l.anchorPoint() == d::Point{.5, .5} && l.value(C::scaleX) == 1 && l.value(C::opacity) == 1 && l.value(C::wipe) == 1, "CALayer defaults: anchor (.5, .5), scale 1, opacity 1, no wipe");
    l.setFrame({10, 20, 100, 50});
    check(l.position() == d::Point{60, 45} && l.bounds() == d::Size{100, 50} && l.frame() == d::Rect{10, 20, 100, 50}, "frame keeps the anchor (position = origin + anchor x size)");
    l.setAnchorPoint({0, 0});
    check(l.frame() == d::Rect{60, 45, 100, 50}, "Changing the anchor moves the frame, not the position");
    l.setOrigin({0, 0});
    check(l.position() == d::Point{0, 0}, "setOrigin");
    rejects([&] { arena.make("row"); }, "Ids are unique in an arena");
    rejects([&] { l.setFrame({0, 0, -1, 2}); }, "Negative bounds are rejected");
    rejects([&] { l.setValue(C::opacity, NAN); }, "Channel values must be finite");
    d::LayerArena bare("");
    check(bare.make("x").id() == "x", "No prefix, no dot");

    // Revisions: content and structure always; static placement; never the pose of animatable layers.
    auto& parent = arena.make("parent");
    auto& notch = arena.make("notch");
    notch.setAnimatable();
    auto r = arena.revision();
    parent.add(notch);
    check(arena.revision() > r && notch.parent() == &parent && parent.children().size() == 1, "Structure bumps the revision");
    r = arena.revision(); notch.setPosition({4, 5}); notch.setValue(C::translateX, 3); notch.setHidden(true); notch.setTint(d::white(1)); notch.setValue(C::opacity, .5);
    check(arena.revision() == r, "An animatable layer's pose never re-emits");
    notch.setBounds({4, 20});
    check(arena.revision() > r, "Bounds are content (masks, clips, the surface extent)");
    r = arena.revision(); l.setPosition({1, 1});
    check(arena.revision() > r, "A static layer's placement is baked: content");
    r = arena.revision(); l.setValue(C::opacity, .4); check(arena.revision() > r, "Static opacity is content");
    const auto content = l.contentRevision();
    l.setContent(d::solid("plate", {0, 0, 100, 50}, d::white(1)));
    check(l.contentRevision() == content + 1 && l.content() && !l.text(), "setContent bumps the content revision");
    l.setText(d::TextBlock("Row", d::type::rowLabel()));
    check(!l.content() && l.text() && l.contentRevision() == content + 3, "setText replaces content; the mutable accessor counts as a change");
    rejects([&] { l.setTint(d::white(1)); }, "A tint needs an animatable layer");
    rejects([&] { l.setContent(J::Object{}); }, "Content is one framed node");
    // setText: the frame follows the padded line box (HUDTextLayer.lineFrame :124-130).
    auto& label = arena.make("label");
    label.setAnchorPoint({0, 0});
    d::TextBlock block("Label", d::type::rowLabel()); block.setLineFrame({20, 10, 120, 18.6});
    label.setText(block);
    check(label.frame() == d::Rect{16, 3, 128, 32.6}, "The layer frame is the line box padded by ceil(.25 s) x ceil(.45 s)");

    // Tree: insert, insertBelow, cycles, removal.
    auto& a = arena.make("a"); auto& b = arena.make("b"); auto& c = arena.make("c");
    parent.add(a); parent.insert(b, 0); parent.insertBelow(c, a);
    check(parent.children() == std::vector<d::Layer*>{&b, &notch, &c, &a}, "insert(at:) and insertBelow");
    rejects([&] { a.add(parent); }, "A layer cannot contain its ancestor");
    rejects([&] { a.add(a); }, "A layer cannot contain itself");
    rejects([&] { parent.insertBelow(c, label); }, "insertBelow needs a sibling");
    a.add(c);
    check(c.parent() == &a && parent.children().size() == 3 && c.isDescendant(parent), "Adding moves a layer from its old parent");
    c.removeFromParent();
    check(!c.parent() && a.children().empty(), "removeFromParent");
    // HitTarget geometry: path in the target's own coordinates (controlPath).
    auto& target = a.addTarget({10, 10, 100, 40}, d::chamfer({0, 0, 100, 40}, 11), false, {});
    check(target.contains({50, 30}) && !target.contains({109, 11}) && target.contains({98, 30}) && !target.contains({5, 5}), "Chamfered target in host coordinates");
    check(a.targets().size() == 1 && a.addTarget({0, 0, 4, 4}, std::nullopt, true, {}).contains({4, 4}), "Rect targets");
}

void verbs() {
    d::Deadlines deadlines;
    d::LayerArena arena("page", &deadlines);
    auto& root = arena.make("root");
    auto& row = arena.make("row");
    auto& plain = arena.make("plain");
    root.add(row); root.add(plain);
    row.setAnimatable();
    const m::Clock at0{0, false, &deadlines};
    rejects([&] { m::animate(plain, C::translateX, 3, .22, at0); }, "Animating a static layer throws");
    rejects([&] { m::enter(plain, {44, 0}, at0); }, "enter on a static layer throws");
    rejects([&] { m::hold(plain); }, "hold on a static layer throws");
    // HUDMotion.animate: the model holds the end, the track runs from what shows.
    m::animate(row, C::translateX, 3, .22, at0);
    check(row.value(C::translateX) == 3 && row.presented(C::translateX, 0) == 0 && near(row.presented(C::translateX, .11), 3 * E.value(.5)) && row.presented(C::translateX, .3) == 3, "Model end state, presented from the start value");
    check(row.animating(.1) && !row.animating(.22), "Animating while the track runs");
    m::animate(row, C::translateX, 0, .22, {.11, false, &deadlines});
    const double from = 3 * E.value(.5);
    check(row.value(C::translateX) == 0 && near(row.presented(C::translateX, .11), from) && near(row.presented(C::translateX, .22), from * (1 - E.value(.5))), "An interruption restarts from the presented value");
    m::animate(row, C::translateX, 5, .22, {1, true, &deadlines});
    check(row.presented(C::translateX, 1) == 5 && !row.animating(1), "Reduce Motion: no track, the model jumps");
    m::animate(row, C::translateX, 5, .22, {2, false, &deadlines});
    check(!row.property[std::size_t(C::translateX)].present(), "from == to makes no track");
    m::animate(row, C::scaleY, .5, .1, {3, false, &deadlines}, d::easing::ease, .2);
    check(row.presented(C::scaleY, 3.1) == 1 && near(row.presented(C::scaleY, 3.25), 1 - .5 * d::easing::ease.value(.5)), "A delay fills backwards with the from value");
    row.setValue(C::scaleY, 1);
    check(!row.property[std::size_t(C::scaleY)].present() && row.presented(C::scaleY, 3.25) == 1, "setValue is immediate and cancels the channel's track");
    // enter + a later absolute opacity animation: later-added wins (CA order).
    m::enter(row, {44, 0}, {10, false, &deadlines});
    check(row.presented(C::opacity, 10) == 0 && row.presented(C::translateX, 10) == 5 + 44, "stIn starts transparent and offset (additive to the model)");
    const auto sample = endfield::core::DesignMotion::enterAt(.1, {44, 0}, 1);
    check(near(row.presented(C::opacity, 10.1), sample.opacity) && near(row.presented(C::translateX, 10.1), 5 + sample.translation.x), "enter samples DesignMotion::enterAt");
    m::animate(row, C::opacity, .5, .22, {10.1, false, &deadlines});
    check(near(row.presented(C::opacity, 10.1), sample.opacity) && near(row.presented(C::opacity, 10.21), sample.opacity + (.5 - sample.opacity) * E.value(.5)), "A later absolute track overrides enter's opacity");
    check(near(row.presented(C::translateX, 10.2), 5 + endfield::core::DesignMotion::enterAt(.2, {44, 0}, 1).translation.x), "enter's additive translation still adds");
    const double moved = 5 + endfield::core::DesignMotion::enterAt(.2, {44, 0}, 1).translation.x;
    m::animate(row, C::translateX, 9, .22, {10.2, false, &deadlines});
    check(near(row.presented(C::translateX, 10.2), moved) && near(row.presented(C::translateX, 10.3), moved + (9 - moved) * E.value(.1 / .22)), "An absolute track added after an additive one replaces it");
    row.removeAllMotion(); row.setValue(C::opacity, 1); row.setValue(C::translateX, 0);
    // stagger delays, Reduce Motion skips enter.
    auto& second = arena.make("second"); second.setAnimatable(); root.add(second);
    d::Layer* list[]{&row, &second};
    m::stagger(list, {0, 44}, {20, false, &deadlines});
    check(row.presented(C::opacity, 20.05) == 0 && second.presented(C::translateY, 20.08) == 44 && second.presented(C::opacity, 20.06 + .036) == 0 && second.presented(C::opacity, 20.5) == 1, "Stagger: 60 ms + 36 ms x index, backwards fill");
    m::enter(row, {44, 0}, {30, true, &deadlines});
    check(row.presented(C::opacity, 30) == 1, "Reduce Motion skips enter");
    // leave: from the presented opacity, holds its end.
    m::leave(row, {44, 0}, {40, false, &deadlines});
    check(row.presented(C::opacity, 40.065) < 1 && row.presented(C::opacity, 41) == 0 && near(row.presented(C::translateX, 41), -26.4) && !row.animating(41), "leave holds transparent at -0.6 x offset");
    m::release(row, d::MotionSlot::leave);
    check(row.presented(C::opacity, 41) == 1, "release removes the leave");
    m::leave(row, {44, 0}, {42, true, &deadlines});
    check(row.presented(C::opacity, 42.1) == 1, "Reduce Motion skips leave");
    // hold applies under Reduce Motion too; a later enter overrides it.
    m::hold(row);
    check(row.presented(C::opacity, -100) == 0 && row.presented(C::opacity, 1e9) == 0 && !row.animating(50), "hold keeps a layer transparent, no frames");
    m::enter(row, {0, 0}, {50, false, &deadlines});
    check(row.presented(C::opacity, 51) == 0 && near(row.presented(C::opacity, 50.1), endfield::core::DesignMotion::enterAt(.1, {}, 1).opacity), "An entrance added after the hold shows, the hold returns after it");
    m::release(row, d::MotionSlot::hold);
    check(row.presented(C::opacity, 51) == 1, "Released");
    // wipe: the mask grows and is gone at the end; the fade starts from fadeFrom x model opacity.
    row.setFrame({0, 0, 200, 50});
    m::wipe(row, d::WipeOrigin::left, .18, {60, false, &deadlines}, E, 0, .4);
    check(row.presented(C::wipe, 60) == 0 && near(row.presented(C::wipe, 60.09), E.value(.5)) && row.presented(C::wipe, 60.18) == 1, "Wipe 0 -> 1");
    check(near(row.presented(C::opacity, 60), .4) && row.presented(C::opacity, 61) == 1, "Wipe fade from .4 x opacity");
    m::wipe(row, d::WipeOrigin::top, .18, {70, true, &deadlines});
    check(row.presented(C::wipe, 70) == 1, "Reduce Motion: no mask");
    m::wipe(row, d::WipeOrigin::top, .18, {80, false, &deadlines}); m::removeWipe(row);
    check(row.presented(C::wipe, 80.05) == 1, "removeWipe");
    // grow and pop.
    m::grow(row, .42, {90, false, &deadlines}, .05);
    check(row.presented(C::scaleX, 90.02) == 0 && near(row.presented(C::scaleX, 90.26), E.value(.5)), "ruleIn: scaleX 0 -> 1 after its delay");
    row.setValue(C::opacity, .8);
    m::pop(row, {100, false, &deadlines});
    const double s = d::easing::snap.value(.5);
    check(row.presented(C::scaleX, 100) == .4 && row.presented(C::scaleY, 100) == .4 && near(row.presented(C::rotation, 100), -12 * std::numbers::pi / 180) && row.presented(C::opacity, 100) == 0, "pop starts at scale .4, -12 deg, transparent");
    check(near(row.presented(C::scaleX, 100.16), .4 + .6 * s) && near(row.presented(C::opacity, 100.16), .8 * s) && row.presented(C::scaleY, 101) == 1, "pop: one SNAP group over linear children");
    row.setValue(C::opacity, 1);
    // Effects: keyframes per channel, infinite loops, stopped by Reduce Motion.
    const double pulse[]{1, .4, 1};
    m::keyframes(row, C::opacity, pulse, {}, 2.4, {110, false, &deadlines}, d::easing::linear, std::numeric_limits<double>::infinity());
    check(near(row.presented(C::opacity, 110.6), .7) && row.animating(1e6), "An endless pulse animates forever (even key times)");
    const double shake[]{0, -6, 6, 0};
    const double shakeTimes[]{0, .3, .6, 1};
    m::keyframes(row, C::translateX, shake, shakeTimes, .3, {110, false, &deadlines}, d::easing::linear, 1, true);
    check(near(row.presented(C::translateX, 110.045), -3) && near(row.presented(C::opacity, 110.6), .7), "Two effects coexist on different channels");
    m::keyframes(row, C::opacity, pulse, {}, 1, {111, true, &deadlines});
    check(row.presented(C::opacity, 111.5) == 1, "Reduce Motion replaces and stops the effect");
    m::release(row, d::MotionSlot::effect);
    rejects([&] { const double one[]{1}; m::keyframes(row, C::opacity, one, {}, 1, at0); }, "Keyframes need two values");
    // Tint: an animatable colour lerped in sRGB.
    m::animateTint(row, d::rgb(0xE9CF12), .22, {120, false, &deadlines}, d::easing::ease);
    const auto tint = *row.presentedTint(120.11);
    check(row.tint() == d::rgb(0xE9CF12) && near(tint.r, 1 + (233 / 255. - 1) * d::easing::ease.value(.5)) && *row.presentedTint(121) == d::rgb(0xE9CF12), "Tint from white to yellowLow");
    const d::Color glow[]{d::white(1), d::rgb(0x8CE03C)};
    m::colorKeyframes(row, glow, {}, 2.4, {130, false, &deadlines}, d::easing::easeInOut, std::numeric_limits<double>::infinity());
    check(*row.presentedTint(130) == d::white(1) && row.animating(500), "A colour effect overrides the model tint while it runs");
    row.removeAllMotion();
    check(*row.presentedTint(130) == d::rgb(0xE9CF12) && !row.animating(130), "removeAllMotion");
    // Completions: deadlines in the arena's group.
    int done{};
    m::after(row, 140.12, {140, false, &deadlines}, [&] { ++done; });
    check(deadlines.next() == 140.12 && deadlines.advance(140.12) && done == 1, "after() runs at its time");
    d::LayerArena loose("loose");
    auto& orphan = loose.make("x"); orphan.setAnimatable();
    rejects([&] { m::after(orphan, 1, at0, [] {}); }, "Completions need an arena with deadlines");
}

void emission() {
    Measurer measurer;
    d::TextServices text{&measurer, {}, d::TextRegion::simplifiedChinese, 2};
    d::LayerArena arena("clip");
    auto& root = arena.make("root");
    root.setAnchorPoint({0, 0}); root.setFrame({0, 0, 590, 510});
    // row (part root, static): plate content, notch (animatable), label after it.
    auto& row = arena.make("row");
    row.setAnchorPoint({0, 0}); row.setFrame({20, 100, 550, 50}); row.setPartRoot(true);
    row.setContent(d::plate("plate", {0, 0, 550, 50}, d::fixed::rowPlate, {}, 11));
    auto& notch = arena.make("notch");
    notch.setAnchorPoint({0, 0}); notch.setFrame({0, 18, 4, 14}); notch.setAnimatable();
    notch.setContent(d::solid("fill", {0, 0, 4, 14}, d::white(1)));
    auto& label = arena.make("label");
    label.setAnchorPoint({0, 0});
    d::TextBlock block("Launch at login", d::type::rowLabel()); block.setLineFrame({16, 15.7, 300, 18.6});
    label.setText(block);
    row.add(notch); row.add(label);
    root.add(row);
    // A header subtree (not a part root): a static group whose children
    // straddle an animatable rule; a clip; a raised layer (zPosition).
    auto& header = arena.make("header");
    header.setAnchorPoint({0, 0}); header.setFrame({0, 0, 590, 81});
    auto& group = arena.make("group");
    group.setAnchorPoint({0, 0}); group.setFrame({10, 10, 200, 40}); group.setValue(C::opacity, .5); group.setMasksToBounds(true, 3);
    auto& before = arena.make("before"); before.setAnchorPoint({0, 0}); before.setContent(d::solid("a", {0, 0, 10, 10}, d::white(1))); before.setFrame({0, 0, 10, 10});
    auto& rule = arena.make("rule"); rule.setAnchorPoint({0, .5}); rule.setFrame({0, 30, 200, 2}); rule.setAnimatable(); rule.setContent(d::solid("r", {0, 0, 200, 2}, d::white(1)));
    auto& after = arena.make("after"); after.setAnchorPoint({0, 0}); after.setContent(d::solid("b", {0, 0, 10, 10}, d::white(1))); after.setFrame({50, 0, 10, 10});
    auto& raised = arena.make("raised"); raised.setAnchorPoint({0, 0}); raised.setContent(d::solid("z", {0, 0, 5, 5}, d::white(1))); raised.setFrame({60, 0, 5, 5}); raised.setZPosition(1);
    group.add(raised); group.add(before); group.add(rule); group.add(after);
    header.add(group);
    root.add(header);

    const auto art = d::emit(root, {&text, "w:"});
    check(art.root == &root && art.parts.size() == 2, "Parts: the row, then the root continued after it (the empty first root part is dropped)");
    check(art.parts[0].id == "w:clip.row" && art.parts[1].id == "w:clip.root~1", "Nested part roots split the outer part into a continuation");
    const auto& rowPart = art.parts[0];
    check(rowPart.surfaces.size() == 3 && rowPart.surfaces[0].id == "w:clip.row.static.0" && rowPart.surfaces[1].id == "clip.notch" && rowPart.surfaces[2].id == "w:clip.row.static.1", "Static run, the boundary, the next run");
    check(rowPart.surfaces[1].role == d::SurfaceRole::animated && rowPart.surfaces[0].role == d::SurfaceRole::staticContent, "Roles");
    const auto& surfaces = rowPart.layers["children"].array();
    check(rowPart.layers["bounds"] == J(J::Array{0., 0., 0., 0.}) && surfaces.size() == 3, "A zero-bounds part root, one child per surface");
    check(surfaces[0]["children"].array()[0]["id"].string() == "clip.row:plate" && surfaces[0]["children"].array()[0]["children"].array()[0]["id"].string() == "clip.row:plate.fill", "Content ids are prefixed by their layer");
    check(surfaces[0]["bounds"] == J(J::Array{0., 0., 550., 50.}) && surfaces[0]["position"] == J(J::Array{0., 0.}) && surfaces[0]["allowsGroupOpacity"] == J(false), "A run is authored in its placing layer's space");
    const auto& textShell = surfaces[2]["children"].array()[0];
    check(textShell["id"].string() == "clip.label" && textShell["position"] == J(J::Array{12., 8.7}) && textShell["children"].array()[0]["kind"].string() == "text", "The label's shell keeps its padded frame");
    check(textShell["children"].array()[0]["position"] == J(J::Array{4., 7.}) && textShell["children"].array()[0]["bounds"] == J(J::Array{0., 0., 300., 18.6}), "Its text node is the line box at the padding offset");
    const auto& runBounds = surfaces[2]["bounds"].array();
    check(near(runBounds[0].number(), 12) && near(runBounds[1].number(), 8.7) && near(runBounds[2].number(), 308) && near(runBounds[3].number(), 32.6), "Run bounds = the union of its frames (padded text)");
    check(surfaces[1]["id"].string() == "clip.notch" && surfaces[1]["bounds"] == J(J::Array{0., 0., 4., 14.}) && !surfaces[1].contains("transform"), "A boundary's own surface starts in its own space");

    // The root continuation: header shell fragments around the rule.
    const auto& rest = art.parts[1];
    check(rest.surfaces.size() == 3 && rest.surfaces[0].id == "w:clip.root~1.static.0" && rest.surfaces[1].id == "clip.rule" && rest.surfaces[2].id == "w:clip.root~1.static.1", "Header: run, rule, run");
    const auto& runs = rest.layers["children"].array();
    const auto& firstGroup = runs[0]["children"].array()[0]["children"].array()[0];
    check(firstGroup["id"].string() == "clip.group" && firstGroup["opacity"].number() == .5 && firstGroup["masksToBounds"].boolean() && firstGroup["cornerRadius"].number() == 3, "Static opacity and clip are baked into the shell");
    check(firstGroup["children"].array().size() == 1 && firstGroup["children"].array()[0]["id"].string() == "clip.before", "Content before the boundary (zPosition sorts the raised layer last)");
    const auto& secondGroup = runs[2]["children"].array()[0]["children"].array()[0];
    check(secondGroup["id"].string() == "clip.group~1" && secondGroup["children"].array().size() == 2 && secondGroup["children"].array()[1]["id"].string() == "clip.raised", "The fragment after the boundary repeats the shell with a new id");
    check(runs[0]["children"].array()[0]["id"].string() == "clip.header" && runs[2]["children"].array()[0]["id"].string() == "clip.header~1", "Every open static ancestor is reopened");

    // Bindings and the frame scan list.
    check(art.bindings.size() == 6 && art.bindings[1].placement == &notch && art.bindings[0].placement == &row && art.bindings[4].placement == &rule && art.bindings[5].placement == &root, "One binding per surface, placed by its boundary or part root");
    check(art.bindings[4].part == 1 && art.bindings[4].surface == 1 && art.animatable == std::vector<const d::Layer*>{&notch, &rule}, "Part/surface indices, the boundary list");
    // Re-emission is byte-stable (the native scene's reuse key).
    const auto again = d::emit(root, {&text, "w:"});
    check(again.parts[0].layers == art.parts[0].layers && again.parts[1].layers == art.parts[1].layers, "Unchanged trees emit equal JSON");
    notch.setValue(C::translateX, 3); m::animate(notch, C::opacity, .3, .2, {0, false, nullptr});
    check(d::emit(root, {&text, "w:"}).parts[0].layers == art.parts[0].layers, "Pose changes leave the JSON alone");
    label.text()->setColor(d::white(1));
    check(d::emit(root, {&text, "w:"}).parts[0].layers != art.parts[0].layers, "A content change differs");

    // Lazy surfaces and a static transform.
    d::LayerArena other("o");
    auto& top = other.make("top"); top.setAnchorPoint({0, 0}); top.setFrame({0, 0, 100, 100});
    auto& sheen = other.make("sheen"); sheen.setAnchorPoint({0, 0}); sheen.setFrame({0, 0, 30, 50}); sheen.setAnimatable(d::SurfaceRole::lazy); sheen.setContent(d::solid("band", {0, 0, 30, 50}, d::white(.2)));
    sheen.setAlphaClip(&notch);
    auto& turned = other.make("turned"); turned.setFrame({10, 10, 20, 10}); turned.setBaseTransform(d::Affine::rotation(std::numbers::pi / 2)); turned.setContent(d::solid("t", {0, 0, 20, 10}, d::white(1)));
    top.add(sheen); top.add(turned);
    const auto lazy = d::emit(top, {&text, ""});
    check(lazy.parts.size() == 1 && lazy.parts[0].surfaces[0].role == d::SurfaceRole::lazy && lazy.bindings[0].alphaClip == &notch, "Lazy role and alpha clip carried to the binding");
    const auto& shell = lazy.parts[0].layers["children"].array()[1]["children"].array()[0];
    check(shell["anchorPoint"] == J(J::Array{.5, .5}) && shell["position"] == J(J::Array{20., 15.}) && near(shell["transform"].array()[0].array()[1].number(), 1) && near(shell["transform"].array()[1].array()[0].number(), -1), "Static CA transform in the encoder's column form");
    check(near(lazy.parts[0].surfaces[1].localBounds.x, 15) && near(lazy.parts[0].surfaces[1].localBounds.width, 10) && near(lazy.parts[0].surfaces[1].localBounds.height, 20), "The run's extent follows the transform");
    // Text without services, and the node budget.
    rejects([&] { d::emit(root, {}); }, "Text emission needs TextServices");
    d::LayerArena big("big");
    auto& huge = big.make("huge"); huge.setFrame({0, 0, 10, 10});
    J::Array many; for (int i = 0; i < 4100; ++i) many.push_back(d::solid("n" + std::to_string(i), {0, 0, 1, 1}, d::white(1)));
    auto node = d::frameNode("c", {0, 0, 10, 10}); node["children"] = std::move(many); huge.setContent(std::move(node));
    rejects([&] { d::emit(huge, {}); }, "A part over the rasterizer's 4096 nodes is refused");
    d::LayerArena dupe("");
    auto& host = dupe.make("host"); auto& b1 = dupe.make("x"); auto& b2 = dupe.make("host.static.0");
    host.setPartRoot(true); b1.setContent(d::solid("q", {0, 0, 1, 1}, d::white(1))); b2.setAnimatable(); b2.setContent(d::solid("q", {0, 0, 1, 1}, d::white(1)));
    host.add(b1); host.add(b2);
    rejects([&] { d::emit(host, {}); }, "Surface ids must be unique in a part");
}

void poses() {
    d::LayerArena arena("p");
    auto& root = arena.make("root"); root.setAnchorPoint({0, 0}); root.setFrame({0, 0, 590, 510});
    auto& clip = arena.make("clip"); clip.setAnchorPoint({0, 0}); clip.setFrame({0, 100, 590, 300}); clip.setMasksToBounds(true, 4); clip.setValue(C::opacity, .5);
    auto& content = arena.make("content"); content.setAnchorPoint({0, 0}); content.setFrame({0, 0, 590, 900}); content.setAnimatable();
    auto& box = arena.make("box"); box.setFrame({100, 50, 40, 20}); box.setAnimatable(); box.setBaseTransform(d::Affine::skew(std::tan(-8 * std::numbers::pi / 180)));
    box.setContent(d::solid("fill", {0, 0, 40, 20}, d::white(1)));
    content.add(box); clip.add(content); root.add(clip);
    const auto art = d::emit(root, {});
    check(art.bindings.size() == 1 && art.bindings[0].placement == &box, "Only the box draws");
    std::vector<d::SurfacePose> out(art.bindings.size());
    m::animate(content, C::translateY, -200, .2, {0, false, nullptr});
    m::animate(box, C::scaleX, 2, .2, {0, false, nullptr});
    m::animate(box, C::rotation, .3, .2, {0, false, nullptr});
    m::animate(box, C::opacity, .4, .2, {0, false, nullptr});
    m::wipe(box, d::WipeOrigin::centerX, .2, {0, false, nullptr});
    counting = true; allocations = 0;
    d::samplePoses(art, .1, out);
    const bool frames = d::requiresFrames(art, .1);
    counting = false;
    check(allocations == 0, "samplePoses and requiresFrames allocate nothing");
    check(frames && !d::requiresFrames(art, .3), "Frames only while a track runs");
    const double e = E.value(.5), ty = -200 * e, sx = 1 + e, rot = .3 * e, op = 1 - .6 * e, w = e;
    // T(position) * T(translate) * base * R * S * T(-anchor x bounds), composed up to the root.
    const auto expected = d::Affine::translation(0, 100) * d::Affine::translation(0, ty) * d::Affine::translation(120, 60) * d::Affine::skew(std::tan(-8 * std::numbers::pi / 180))
        * d::Affine::rotation(rot) * d::Affine::scale(sx, 1) * d::Affine::translation(-20, -10);
    const auto& pose = out[0];
    for (const d::Point p : {d::Point{0, 0}, d::Point{40, 20}, d::Point{13, 7}}) check(near(pose.world.apply(p), expected.apply(p), 1e-9), "Pose world = presented transforms in CA order");
    check(near(pose.opacity, .5 * op, 1e-6) && pose.visible && !pose.tint, "Opacity is the product of the chain (static .5 x animated)");
    check(pose.maskCount == 2, "Masks: the static clip ancestor and the box's own wipe");
    check(pose.masks[0].bounds == d::Rect{0, 0, 590, 300} && pose.masks[0].cornerRadius == 4 && near(pose.masks[0].world.apply({0, 0}), {0, 100}), "The clip in its layer's space");
    check(near(pose.masks[1].bounds.x, 20 - 20 * w) && near(pose.masks[1].bounds.width, 40 * w) && pose.masks[1].bounds.height == 20, "centerX wipe rect");
    check(near(pose.masks[1].world.apply({0, 0}), pose.world.apply({0, 0})), "The wipe is in the wiping layer's space");
    d::samplePoses(art, 1, out);
    check(out[0].maskCount == 1 && near(out[0].world.apply({0, 0}), (d::Affine::translation(0, -100) * d::Affine::translation(120, 60) * d::Affine::skew(std::tan(-8 * std::numbers::pi / 180)) * d::Affine::rotation(.3) * d::Affine::scale(2, 1) * d::Affine::translation(-20, -10)).apply({0, 0})), "At rest: the model, the wipe gone");
    m::animateTint(box, d::rgb(0xFFFA00), .2, {2, false, nullptr});
    d::samplePoses(art, 2.1, out);
    check(out[0].tint && near(out[0].tint->b, 1 - e), "Tint sampled in sRGB");
    m::wipe(box, d::WipeOrigin::left, .2, {3, false, nullptr});
    d::samplePoses(art, 3, out);
    check(!out[0].visible, "A wipe at 0 hides the surface");
    clip.setHidden(true);
    d::samplePoses(art, 5, out);
    check(!out[0].visible, "A hidden ancestor hides");
    clip.setHidden(false);
    content.removeFromParent();
    d::samplePoses(art, 5, out);
    check(!out[0].visible, "A placement detached since emission draws nothing");
    rejects([&] { std::vector<d::SurfacePose> wrong(2); d::samplePoses(art, 5, wrong); }, "One pose per binding");
}

void hits() {
    d::LayerArena arena("h");
    auto& root = arena.make("root"); root.setAnchorPoint({0, 0}); root.setFrame({0, 0, 590, 510});
    auto& area = arena.make("area"); area.setAnchorPoint({0, 0}); area.setFrame({0, 81, 590, 300}); area.setMasksToBounds(true);
    auto& rows = arena.make("rows"); rows.setAnchorPoint({0, 0}); rows.setFrame({0, 0, 590, 1000}); rows.setAnimatable();
    auto& row = arena.make("row"); row.setFrame({20, 400, 550, 50}); row.setAnimatable();
    rows.add(row); area.add(rows); root.add(area);
    rows.setValue(C::translateY, -380);
    m::enter(row, {44, 0}, {0, false, nullptr});
    // Model geometry (CALayer.convert on the model tree): the entrance offset never moves hits.
    check(near(*d::convert(d::Point{0, 0}, row, root), {20, 101}) && near(d::modelWorld(row, root).apply({0, 0}), {20, 101}), "convert / modelWorld use model values, not the entrance's presented offset");
    check(near(*d::convert(d::Point{20, 101}, root, row), {0, 0}) && d::convert(d::Rect{0, 0, 10, 10}, row, area) == d::Rect{20, 20, 10, 10}, "Inverse conversion and rect bounding");
    d::LayerArena elsewhere("e");
    auto& stranger = elsewhere.make("s");
    check(!d::convert(d::Point{}, row, stranger) && d::convert(d::Rect{0, 0, 1, 1}, row, stranger) == d::Rect{}, "Layers of different trees do not convert");
    // isVisible (:149-157): hidden or opacity <= .01 anywhere up to the root.
    check(d::isVisible(row, root), "Visible");
    rows.setValue(C::opacity, .01);
    check(!d::isVisible(row, root), "Opacity .01 counts as invisible");
    rows.setValue(C::opacity, 1); area.setHidden(true);
    check(!d::isVisible(row, root) && !d::isVisible(row, stranger), "A hidden ancestor, or a root that is not an ancestor");
    area.setHidden(false);
    // isUnclipped (:663-672): every clipping ancestor below the root contains the point.
    check(d::isUnclipped(row, {30, 110}, root) && !d::isUnclipped(row, {30, 390}, root) && d::isUnclipped(row, {30, 380.9}, root), "The scroll area clips (half-open bounds)");
    check(d::isUnclipped(area, {30, 600}, root), "A layer's own clip does not count");
}

void lifetime() {
    d::Deadlines deadlines;
    d::LayerArena canvas("canvas", &deadlines);
    auto& top = canvas.make("top");
    auto& page = canvas.make("page");
    int ran{};
    d::Layer* kept{};
    {
        d::LayerArena slider("slider", &deadlines);
        auto& bubble = slider.make("bubble"); bubble.setAnimatable();
        auto& hosted = canvas.make("hosted");
        kept = &hosted;
        auto& knob = slider.make("knob");
        top.add(bubble);           // a slider's bubble hosted in the canvas's top layer
        knob.add(hosted);          // a canvas layer hosted under the slider
        page.add(knob);
        m::after(bubble, 1, {0, false, &deadlines}, [&] { ++ran; });
        check(top.children().size() == 1 && hosted.parent() == &knob && !deadlines.empty(), "Cross-arena links and a pending completion");
    }
    check(top.children().empty() && page.children().empty(), "The dying arena leaves no pointer in foreign parents");
    check(!kept->parent(), "Its foreign children are detached");
    check(!canvas.make("probe").parent(), "Arena still usable");
    check(deadlines.empty() && !deadlines.advance(5) && ran == 0, "Its completions are cancelled with it");
}

// TODO(lead): when the L3 oracle fixture lands, compare emitted trees of the
// fixture's component scenes node by node here (frames, colours, z order)
// and its recorded allowsGroupOpacity flags (Q3). Until then the vectors
// above derive from the Swift and Core Animation semantics.
void oracle(const char* path) {
    if (!std::filesystem::exists(path)) std::cout << "hud_design_layer: oracle fixture not present yet (" << path << "); Swift-derived contracts only (TODO lead)\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: hud_design_layer_tests <hud-design-source.json>");
        affine(); model(); verbs(); emission(); poses(); hits(); lifetime(); oracle(argv[1]);
        std::cout << "hud_design_layer contracts: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "hud_design_layer contracts failed after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
