// L2 component contracts (LIBRARY-DESIGN.md section 14 item 4): every
// component of the redesign library in its Mac states (rest, hovered, pressed,
// disabled, selected, open, recording, redacted, Light), its callbacks and its
// accessibility items, against values derived from the Swift sources (each
// check cites the line it ports) with a deterministic measurer; and, when the
// Mac oracle (tests/fixtures/hud-design-source.json, tools/hud_design_reference.*)
// is given, node by node against the Mac's own trees with its recorded widths.
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_cards.hpp"
#include "modules/hud_design_header.hpp"
#include "modules/hud_design_menu.hpp"
#include "modules/hud_design_redaction.hpp"
#include "modules/hud_design_scroll.hpp"
#include "modules/hud_design_text_field.hpp"
#include "tests/hud_design_oracle.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace d = endfield::modules::design;
using Json = ehud::data::Json;
using d::Channel; using d::Color; using d::Point; using d::Rect;
namespace {
using namespace endfield::tests::design_oracle;
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
double fakeWidth(std::string_view text, double size, double tracking = 0) { return std::ceil(double(characters().count(text)) * size * (.55 + tracking) * 2) / 2; }

struct World {
    FakeMeasurer measurer;
    d::Environment env;
    d::LayerArena arena{"t", &env.deadlines};
    d::ControlSet set;
    d::Builder b{env, arena, set};
    d::Layer& root = arena.make("root");
    explicit World(bool light = false, std::string_view accent = "FAD41F") {
        env.text.measurer = &measurer; env.text.uppercase = d::asciiUppercase; env.now = 10;
        env.palette.apply(accent, light);
        root.setFrame(d::metric::box);
    }
    d::Layer& host(d::Layer& l) { root.add(l); return l; }
    void move(std::optional<Point> p, bool pressed = false) { d::updateHighlights(root, p, pressed); }
    Point centre(const d::Control& c) const { const auto r = c.rect(root); return {r.x + r.width / 2, r.y + r.height / 2}; }
    void at(double t) { env.now = t; env.deadlines.advance(t); }
};
const Json* node(const d::Layer& l) { return l.content() ? &*l.content() : nullptr; }
std::optional<Color> colorKey(const d::Layer& l, std::string_view key) {
    const Json* n = node(l);
    if (!n || !(*n)[key].isObject()) return std::nullopt;
    const auto& a = (*n)[key]["sRGB"].array();
    return Color{a[0].number(), a[1].number(), a[2].number(), a[3].number()};
}
// What the layer shows: its content colour times its model tint (a white-authored tinted surface shows the tint).
std::optional<Color> fill(const d::Layer& l) { const auto c = colorKey(l, "backgroundColor"); return c ? std::optional(multiply(*c, l.tint())) : std::nullopt; }
std::optional<Color> border(const d::Layer& l) { const auto c = colorKey(l, "borderColor"); return c ? std::optional(multiply(*c, l.tint())) : std::nullopt; }
std::optional<Color> shapeFill(const d::Layer& l) {
    const Json* n = node(l);
    if (!n || !(*n)["shape"].isObject() || !(*n)["shape"]["fillColor"].isObject()) return std::nullopt;
    const auto& a = (*n)["shape"]["fillColor"]["sRGB"].array();
    return multiply(Color{a[0].number(), a[1].number(), a[2].number(), a[3].number()}, l.tint());
}
Color textColor(const d::Layer& l) { return multiply(d::textOf(l).style().color, l.tint()); }
const d::Layer* find(const d::Layer& from, std::string_view suffix) {
    if (std::string_view(from.id()).ends_with(suffix)) return &from;
    for (const auto* c : from.children()) if (const auto* f = find(*c, suffix)) return f;
    return nullptr;
}
const d::Layer& must(const d::Layer& from, std::string_view suffix) {
    const auto* l = find(from, suffix);
    if (!l) throw std::runtime_error("No layer ending in " + std::string(suffix) + " under " + from.id());
    return *l;
}
// The value a channel settles at: the model (motion runs from the presentation to it).
double model(const d::Layer& l, Channel c) { return l.value(c); }
bool animating(const d::Layer& l, double now) { return l.animating(now); }
std::vector<d::AccessibilityItem> items(const d::Control& c, const d::Layer& root) { std::vector<d::AccessibilityItem> out; c.accessibilityItems(root, out); return out; }

// ---------------------------------------------------------------------------
// HUDControl, HUDInteractionRouter, HUDControlHighlightLayer.update
// ---------------------------------------------------------------------------
struct Probe final : d::Control {
    Probe(d::Builder& b, std::string id, Rect frame) : Control(b.env, id, id, b.layer(id)) { layer().setFrame(frame); setTarget(d::localBounds(layer())); }
    int hovers{}, presses{}, activations{};
    void hoverChanged() override { ++hovers; }
    void pressChanged() override { ++presses; }
    void activate() override { if (enabled()) ++activations; }
};
void controlBase() {
    World w;
    auto& card = w.b.make<Probe>("card", Rect{10, 10, 200, 60});
    auto& small = w.b.make<Probe>("small", Rect{20, 20, 20, 20});
    auto& smaller = w.b.make<Probe>("smaller", Rect{25, 25, 10, 10});
    w.host(card.layer()); card.layer().add(small.layer()); card.layer().add(smaller.layer());
    // The card is a container: the smallest-area non-container target wins the hover (:149-151), the container stays hovered (:143-145, :159).
    const_cast<d::HitTarget*>(card.target())->container = true;
    w.move(Point{40, 40});
    check(card.hovered() && !small.hovered() && smaller.hovered(), "Hover goes to the smallest target, the container stays hovered (:149-159)");
    w.move(Point{40, 40}, true);
    check(smaller.pressed() && !card.pressed(), "A container is pressed only when no non-container target is hit (:159)");
    w.move(Point{100, 50}, true);
    check(card.pressed() && card.hovered() && !smaller.hovered(), "Outside the small targets the container takes the press");
    w.move(std::nullopt);
    check(!card.hovered() && !card.pressed() && !smaller.hovered(), "Leaving clears every target");
    // Ties: the later target in paint order (<=, :150).
    auto& twin = w.b.make<Probe>("twin", Rect{25, 25, 10, 10});
    card.layer().add(twin.layer());
    w.move(Point{40, 40});
    check(twin.hovered() && !smaller.hovered(), "Equal areas: the later target wins");
    // Disabled targets never light up; disabling clears the state (:54-58).
    twin.setEnabled(false);
    check(!twin.hovered(), "Disabling clears hover");
    w.move(Point{40, 40});
    check(smaller.hovered() && !twin.hovered(), "A disabled target is skipped");
    // Hidden or transparent hosts and clips (:125-142).
    smaller.layer().setHidden(true); w.move(Point{40, 40});
    check(!smaller.hovered() && small.hovered(), "A hidden target cannot be hovered");
    smaller.layer().setHidden(false);
    card.layer().setMasksToBounds(true); smaller.layer().setFrame({190, 20, 30, 30});
    w.move(Point{215, 35});
    check(!smaller.hovered(), "A target outside a clipping ancestor cannot light up");
    w.move(Point{205, 35});
    check(smaller.hovered(), "Inside the clip it does");
    card.layer().setMasksToBounds(false);
    w.move(std::nullopt); smaller.layer().setDecorative(true); w.move(Point{205, 35});
    check(!smaller.hovered(), "Decorative subtrees are skipped (never hold targets)");
    smaller.layer().setDecorative(false);
    // Router (:112-145): the TOPMOST registered control takes the press, tracks it, and pointerUp activates only inside.
    d::Router router;
    router.set({&card, &small, &smaller});
    check(router.control({30, 30}, w.root) == &small, "The last registered control containing the point takes the press");
    check(router.pointerDown({30, 30}, w.root) && router.tracked() == &small, "pointerDown tracks the control");
    router.pointerUp({100, 100}, w.root);
    check(small.activations == 0 && !router.tracking(), "A release outside does not activate");
    router.pointerDown({30, 30}, w.root); router.pointerUp({31, 31}, w.root);
    check(small.activations == 1, "A release inside activates");
    small.setEnabled(false);
    check(router.control({30, 30}, w.root) == &card, "Disabled controls are skipped by the router");
    router.pointerDown({100, 50}, w.root);
    router.set({&small});
    check(!router.tracking(), "set() drops a tracked control that left");
    card.layer().setValue(Channel::opacity, .005);
    std::vector<d::AccessibilityItem> list; router.set({&card, &smaller}); router.accessibilityItems(w.root, list);
    check(list.empty(), "Invisible controls (opacity <= .01) have no accessibility items");
    card.layer().setValue(Channel::opacity, 1);
    list.clear(); router.accessibilityItems(w.root, list);
    check(list.size() == 2 && list[0].id == "card" && near(list[0].rect, {10, 10, 200, 60}) && list[0].role == d::AccessibilityRole::button,
        "Items carry the id, the label and the rect in root coordinates");
    // Readout (:27-39).
    d::Readout readout(w.env, "battery.level", "Level", "72%", w.root);
    check(!readout.contains({1, 1}) && !readout.pointerDown({1, 1}), "A readout never takes the pointer");
    const auto readoutItems = items(readout, w.root);
    check(readoutItems.size() == 1 && readoutItems[0].role == d::AccessibilityRole::text && readoutItems[0].value == "72%", "A readout reads as text with its value");
    // applyHoverMotion (HUDDesignButtons.swift:6-18).
    auto& lifted = w.b.make<Probe>("lifted", Rect{300, 300, 40, 40});
    lifted.layer().setAnimatable(); w.host(lifted.layer());
    w.move(w.centre(lifted)); d::applyHoverMotion(lifted, 1, 1.06, .94, nullptr);
    check(model(lifted.layer(), Channel::translateY) == -1 && model(lifted.layer(), Channel::scaleX) == 1.06, "Hover lifts and scales");
    w.move(w.centre(lifted), true); d::applyHoverMotion(lifted, 1, 1.06, .94, nullptr);
    check(model(lifted.layer(), Channel::translateY) == 0 && model(lifted.layer(), Channel::scaleY) == .94, "Press drops the lift and pushes in");
    check(near(lifted.layer().property[std::size_t(Channel::scaleY)].duration, .10), "The press scale runs .10 s");
    // Swift Character counts (String.count) used by redaction and fields.
    check(characters().count("Endministrator") == 14 && characters().count("管理员") == 3 && characters().count("é") == 1
        && characters().count("\U0001F1EF\U0001F1F5") == 1 && characters().count("\U0001F469‍\U0001F4BB") == 1 && characters().count("\r\n") == 1
        && characters().count("한가") == 2, "Grapheme clusters: marks, flags, ZWJ, CRLF, Hangul jamo");
    check(characters().prefix("aéb", 2) == "aé" && characters().dropFirst("aéb", 2) == "b", "prefix / dropFirst on clusters");
}

// ---------------------------------------------------------------------------
// HUDRowPlate, HUDSheen (HUDDesignRows.swift:8-134, :288-328)
// ---------------------------------------------------------------------------
void rows() {
    World w;
    auto& row = w.b.make<d::RowPlate>("row", "Launch at login", std::optional<std::string>("Starts with macOS"));
    w.host(row.layer());
    check(near(d::localBounds(row.layer()), {0, 0, 590, 50}), "Standard rows are 590 x 50 (:11)");
    check(near(must(row.layer(), "row.notch").frame(), {0, 9, 3, 32}), "The notch is 3 x (h - 18) at y 9 (:44-45)");
    // layoutContents (:83-107): column 3 + 18.6 + 1 + 13.2, centred in (10, h - 18) + 3.
    const double top = 10 + (32 - (3 + 18.6 + 1 + 13.2)) / 2 + 3;
    check(near(row.titleFrame(), {18, top, 590 - 12 - 14 - 18, 18.6}), "Title line box " + str(row.titleFrame()));
    check(near(d::textOf(must(row.layer(), "row.sublabel")).lineFrame, {18, top + 18.6 + 1, 590 - 12 - 14 - 18, 13.2}), "Sublabel 1 px below");
    check(near(must(row.layer(), "row.tag").frame().x, 18) && near(must(row.layer(), "row.tag").frame().y, 4), "Micro tag at (18, 4) (:46)");
    check(row.target()->container && !row.pointerDown({5, 5}), "Standard rows are containers that never take the press (:54, :129)");
    check(near(*fill(must(row.layer(), "row.notch")), d::fixed::notch), "Notch #2A2A2A at rest");
    // Hover (:115-124).
    w.move(w.centre(row));
    check(row.hovered() && model(row.layer(), Channel::translateX) == 3 && model(must(row.layer(), "row.brighten"), Channel::opacity) == 1, "Hover: +3 px and the brighten");
    const auto& notch = must(row.layer(), "row.notch");
    check(near(*notch.tint(), w.env.palette.yellowLow()) && near(model(notch, Channel::scaleY), 40.0 / 32), "Notch yellowLow, scaleY (h-10)/(h-18)");
    check(near(notch.tintTrack.duration, .22) && notch.tintTrack.timing.x1 == d::easing::ease.x1, "The notch colour eases .22 s");
    const auto& band = must(row.layer(), "sheen.band");
    check(model(band, Channel::opacity) == 1 && animating(band, w.env.now) && band.alphaClip() == &must(row.layer(), "row.plate"), "One sheen pass clipped to the plate");
    w.at(w.env.now + .71);
    check(model(band, Channel::opacity) == 0, "The band hides after its .70 s pass");
    w.move(std::nullopt);
    check(model(row.layer(), Channel::translateX) == 0 && near(*notch.tint(), d::fixed::notch) && model(band, Channel::opacity) == 0, "Hover-out returns at once (sheen hidden)");
    w.env.reduceMotion = true; w.move(w.centre(row));
    check(model(band, Channel::opacity) == 0 && !animating(row.layer(), w.env.now), "Reduce Motion: no sheen, no tracks");
    w.env.reduceMotion = false; w.move(std::nullopt);
    // Variants, cluster and control area.
    auto& nav = w.b.make<d::RowPlate>("nav", "Navigation row", std::nullopt, d::RowPlate::Variant::nav, "↗", 400.0);
    check(!nav.target()->container && nav.pointerDown({5, 5}) && nav.accessibilityRole() == d::AccessibilityRole::button, "Nav rows are clickable buttons");
    check(near(d::textOf(must(nav.layer(), "row.chevron")).lineFrame, {400 - 32, 10 + 16 - 8, 20, 16}) && d::textOf(must(nav.layer(), "row.chevron")).text() == "↗", "Chevron box (:95)");
    check(near(nav.controlArea(), {18, 10 + 16 - 15, 400 - 12 - 22 - 18, 30}), "Nav control area ends 22 px earlier");
    auto& compact = w.b.make<d::RowPlate>("compact", "Compact", std::nullopt, d::RowPlate::Variant::compact);
    check(compact.layer().bounds().height == 44 && compact.accessibilityRole() == std::nullopt, "Compact 44, not in the tree");
    compact.readout = "3 events";
    const auto readout = items(compact, w.root);
    check(readout.size() == 1 && readout[0].role == d::AccessibilityRole::text && readout[0].value == "3 events", "A readout row reads as text (:127-133)");
    auto& slider = w.b.make<d::RowPlate>("slider", "Opacity", std::nullopt, d::RowPlate::Variant::slider);
    check(near(slider.controlArea(), {202, 10 + 13 - 15, 590 - 12 - 202, 30}), "Slider rows: control area from 202 (:76-81)");
    check(near(slider.titleFrame().width, 170), "Slider rows: the label column is 170");
    auto& custom = w.b.make<d::RowPlate>("custom", "Custom", std::nullopt, d::RowPlate::Variant::standard, "›", 590.0, 56.0);
    check(custom.layer().bounds().height == 56 && near(must(custom.layer(), "row.notch").frame(), {0, 9, 3, 38}), "Custom height 56");
    auto& toggle = w.b.make<d::Toggle>("row.toggle", "Toggle", true);
    auto& button = w.b.make<d::Button>("row.button", "Open", std::nullopt, d::Button::Kind::secondary, d::Button::Height::compact);
    auto& cluster = w.b.make<d::RowPlate>("cluster", "Slider row");
    cluster.setCluster({&button.layer(), &toggle.layer()});
    cluster.setTitleInset(56);
    check(near(toggle.layer().frame(), {590 - 12 - 88, 10 + 16 - 13, 88, 26}), "Cluster right-aligned at w - 12, centred (:97-104): " + str(toggle.layer().frame()));
    check(near(button.layer().frame().x, 590 - 12 - 88 - 8 - button.layer().bounds().width), "8 px between cluster items");
    check(near(cluster.titleFrame().x, 18 + 56) && near(cluster.titleFrame().width, button.layer().frame().x - 14 - 74), "The title inset and the cluster bound the label");
    // Container + nested control: the row stays hovered while its toggle is (LIB-93).
    w.host(cluster.layer()); cluster.layer().setOrigin({0, 200});
    w.move(w.centre(toggle));
    check(cluster.hovered() && toggle.hovered(), "The row stays hovered while its toggle is");
    w.move(w.centre(toggle), true);
    check(toggle.pressed() && !cluster.pressed(), "The toggle takes the press, not the row");
    w.move(std::nullopt);
    row.setTitle("Renamed"); row.setSublabel(std::nullopt);
    check(row.label == "Renamed" && must(row.layer(), "row.sublabel").hidden(), "setTitle / setSublabel in place");
    check(near(row.titleFrame().y, 10 + (32 - (3 + 18.6)) / 2 + 3), "Without a sublabel the column recentres");
}

// ---------------------------------------------------------------------------
// HUDToggle, HUDSegmentedControl (HUDDesignRows.swift:138-283)
// ---------------------------------------------------------------------------
void toggles() {
    World w;
    auto& toggle = w.b.make<d::Toggle>("toggle", "Ambient", false);
    w.host(toggle.layer());
    const auto& knob = must(toggle.layer(), "toggle.knob");
    check(near(d::localBounds(toggle.layer()), {0, 0, 88, 26}) && near(knob.frame(), {2, 2, 42, 22}), "88 x 26 with a 42 x 22 knob at (2, 2) (:139, :161)");
    check(near(*fill(toggle.layer()), w.env.palette.token(d::ThemeToken::toggleTrack)) && near(*border(toggle.layer()), w.env.palette.token(d::ThemeToken::ringToggle)), "Track and ring");
    check(model(knob, Channel::translateX) == 0 && near(*must(toggle.layer(), "toggle.knob.fill").tint(), d::fixed::knobOff), "Off: knob left in knobOff");
    check(near(textColor(must(toggle.layer(), "toggle.off")), d::fixed::nearBlack) && near(textColor(must(toggle.layer(), "toggle.on")), w.env.palette.token(d::ThemeToken::muted)),
        "The word under the knob is dark, the other grey");
    check(!animating(knob, w.env.now), "The initial state is not animated");
    std::vector<std::string> order;
    toggle.onChange = [&](bool on) { order.push_back(on ? "change:on" : "change:off"); };
    toggle.onActivate = [&] { order.push_back("activate"); };
    toggle.activate();
    check(toggle.isOn() && order == std::vector<std::string>{"change:on", "activate"}, "activate toggles, then onChange, then onActivate (:194-199)");
    check(model(knob, Channel::translateX) == 42 && near(knob.property[std::size_t(Channel::translateX)].duration, .18)
        && knob.property[std::size_t(Channel::translateX)].outer.y1 == d::easing::snap.y1, "Knob slides 42 px in .18 s SNAP");
    check(near(*must(toggle.layer(), "toggle.knob.fill").tint(), w.env.palette.yellow()), "Knob turns yellow");
    check(near(must(toggle.layer(), "toggle.on").tintTrack.duration, .14), "Labels change over .14 s");
    toggle.setOn(false, false);
    check(model(knob, Channel::translateX) == 0, "setOn(animated: false) jumps");
    const auto item = items(toggle, w.root);
    check(item.size() == 1 && item[0].role == d::AccessibilityRole::toggle && !item[0].on, "A switch with its state");
    toggle.setEnabled(false);
    check(model(toggle.layer(), Channel::opacity) == .35, "Disabled .35");
    toggle.activate(); check(!toggle.isOn(), "Disabled toggles ignore activation");

    auto& seg = w.b.make<d::SegmentedControl>("seg", "Mode", std::vector<std::string>{"Dark", "Light", "System"}, 1, 300.0);
    w.host(seg.layer()); seg.layer().setOrigin({0, 100});
    const double sw = (300 - 8) / 3.0;
    check(seg.segments().size() == 3 && near(seg.segments()[2]->layer().frame(), {2 * (sw + 4), 0, sw, 30}), "Equal columns, gap 4 (:241-253)");
    const auto& s1 = seg.segments()[1]->layer();
    check(near(*fill(must(s1, "segment.fill")), d::fixed::plate) && must(s1, "segment.ring").tint()->a == 0 && !must(s1, "segment.bar").hidden()
        && near(textColor(must(s1, "segment.text")), d::fixed::nearBlack), "Selected: plate, no ring, #111 bar and text (:265-276)");
    const auto& s0 = seg.segments()[0]->layer();
    check(near(*border(must(s0, "segment.ring")), w.env.palette.token(d::ThemeToken::ringField)) && must(s0, "segment.bar").hidden(), "Rest: ringField");
    w.move(w.centre(*seg.segments()[0]));
    check(near(*must(s0, "segment.ring").tint(), w.env.palette.token(d::ThemeToken::muted)) && near(textColor(must(s0, "segment.text")), w.env.palette.foreground(1))
        && near(must(s0, "segment.ring").tintTrack.duration, .22), "Hover: muted ring (.22 ease), white text");
    std::vector<int> changes, reselects;
    seg.onChange = [&](int i) { changes.push_back(i); }; seg.onReselect = [&](int i) { reselects.push_back(i); };
    seg.segments()[2]->activate(); seg.segments()[2]->activate();
    check(seg.selectedIndex() == 2 && changes == std::vector<int>{2} && reselects == std::vector<int>{2}, "Select notifies; the selected segment reselects (:222-225)");
    check(must(seg.segments()[2]->layer(), "segment.fill").tintTrack.keys == 0, "A selection change is not animated");
    check(!seg.contains({1, 1}) && seg.accessibilityRole() == std::nullopt, "The group is not a target");
    const auto tab = items(*seg.segments()[2], w.root);
    check(tab.size() == 1 && tab[0].role == d::AccessibilityRole::tab && tab[0].on, "Segments are tabs");
    auto& yellow = w.b.make<d::SegmentedControl>("seg.y", "Mode", std::vector<std::string>{"25", "50", "Custom"}, 0, 240.0, true);
    check(near(*fill(must(yellow.segments()[0]->layer(), "segment.fill")), w.env.palette.yellow()) && must(yellow.segments()[0]->layer(), "segment.bar").hidden(),
        "The yellow variant: accent fill, no bar");
}

// ---------------------------------------------------------------------------
// HUDButton, HUDIconButton, HUDPill, HUDSwatch, HUDDragGrip (HUDDesignButtons.swift)
// ---------------------------------------------------------------------------
void buttons() {
    World w;
    using K = d::Button::Kind; using H = d::Button::Height;
    auto& primary = w.b.make<d::Button>("primary", "Export", d::Icon::export_, K::primary);
    primary.place({12, 30});
    const double text = fakeWidth("EXPORT", 12, .08);
    check(near(primary.preferredWidth(), std::ceil(16 + 15 + 8 + text + 16)) && near(primary.layer().frame(), {12, 30, primary.preferredWidth(), 32}),
        "Width ceil(padding + 15 + 8 + text + padding), regular 32 (:55-59)");
    check(near(must(primary.layer(), "button.icon").frame(), {16, 8.5, 15, 15}) && near(d::textOf(must(primary.layer(), "button.text")).lineFrame, {39, 10, primary.preferredWidth() - 39 - 16 + 2, 12}),
        "Icon then text, text box y (h - font) / 2 (:90-93)");
    check(near(must(primary.layer(), "button.pixel").frame(), {primary.preferredWidth() - 10, 32 - 9, 4, 4}), "The #111 corner pixel (:78)");
    check(primary.target()->path.has_value(), "Primary hit path is the chamfer");
    w.host(primary.layer());
    w.move(w.centre(primary));
    check(model(primary.layer(), Channel::translateY) == -1 && model(must(primary.layer(), "button.brighten"), Channel::opacity) == 1, "Hover lifts 1 and brightens");
    w.move(w.centre(primary), true);
    check(model(primary.layer(), Channel::scaleX) == .965 && model(primary.layer(), Channel::translateY) == 0, "Press .965");
    w.move(std::nullopt);
    primary.setEnabled(false);
    check(model(primary.layer(), Channel::opacity) == .35, "Disabled .35");
    auto& secondary = w.b.make<d::Button>("secondary", "Open", std::nullopt, K::secondary, H::compact);
    secondary.place({0, 100});
    check(near(secondary.preferredWidth(), std::ceil(20 + fakeWidth("OPEN", 10.5, .08) + 11)) && secondary.layer().bounds().height == 26, "Secondary small: leading 20, padding 11, h 26");
    check(!secondary.target()->path && near(*fill(must(secondary.layer(), "button.plate")), w.env.palette.token(d::ThemeToken::secondaryButton)), "Secondary: flat plate, rect target");
    const auto& bar = must(secondary.layer(), "button.bar"); const auto& arrow = must(secondary.layer(), "button.bar.arrow");
    check(near(bar.frame(), {8, 7, 7, 12}) && model(bar, Channel::opacity) == 1 && model(arrow, Channel::opacity) == 0, "The 7 x 12 bar at (8, centred)");
    w.host(secondary.layer()); w.move(w.centre(secondary));
    check(model(bar, Channel::opacity) == 0 && model(arrow, Channel::opacity) == 1 && near(arrow.property[std::size_t(Channel::opacity)].duration, .2)
        && near(*must(secondary.layer(), "button.plate").tint(), w.env.palette.token(d::ThemeToken::secondaryButtonHover)), "Hover: bar -> ▶ (.2 ease), hover plate");
    auto& flex = w.b.make<d::Button>("flex", "Flex", d::Icon::check, K::light);
    flex.place({12, 30}, 220.0);
    check(near(flex.layer().bounds().width, 220) && near(must(flex.layer(), "button.icon").frame().x, 16 + std::round((220 - flex.preferredWidth()) / 2)), "Flex centres icon and title");
    auto& iconOnly = w.b.make<d::Button>("plus", "", d::Icon::plus, K::primary, H::compact);
    check(near(iconOnly.preferredWidth(), std::ceil(11 + 15 + 11)), "Icon-only: no gap after the icon");

    using S = d::IconButton::Style;
    auto& dark = w.b.make<d::IconButton>("icon", "Pin", d::Icon::pin);
    check(must(dark.layer(), "icon.plate.ring").tintTrack.keys == 2 && near(must(dark.layer(), "icon.plate.ring").tintTrack.values[0], d::black(1))
        && near(must(dark.layer(), "icon.plate").tintTrack.values[0], d::clear), "Creation fade: border from black, fill from no colour (HUDIconButton.init)");
    w.host(dark.layer()); dark.layer().setOrigin({300, 300});
    check(dark.layer().bounds().width == 30 && near(must(dark.layer(), "icon.glyph").bounds().width, 15) && !dark.target()->path, "Side 30, glyph 15, square");
    check(near(*fill(must(dark.layer(), "icon.plate")), w.env.palette.token(d::ThemeToken::iconButton)) && near(*border(must(dark.layer(), "icon.plate.ring")), w.env.palette.token(d::ThemeToken::ringIcon)),
        "Dark: iconButton fill, ringIcon");
    w.move(w.centre(dark));
    check(near(*must(dark.layer(), "icon.plate.ring").tint(), w.env.palette.tone(0x888888)) && model(dark.layer(), Channel::scaleX) == 1.06, "Hover: ring 888888, 1.06");
    w.move(w.centre(dark), true);
    check(model(dark.layer(), Channel::scaleX) == .94, "Press .94");
    w.move(std::nullopt);
    dark.setOn(true); dark.setIcon(d::Icon::mute);
    check(near(*must(dark.layer(), "icon.plate").tint(), w.env.palette.yellow()) && must(dark.layer(), "icon.plate.ring").tint()->a == 0
        && near(*must(dark.layer(), "icon.glyph").tint(), d::fixed::nearBlack), "isOn: yellow, no ring, #111 glyph");
    auto& back = w.b.make<d::IconButton>("back", "Back", d::Icon::back, S::back);
    check(near(must(back.layer(), "icon.plate.ring").tintTrack.duration, .12), "The back style fades in .12 s");
    w.host(back.layer()); back.layer().setOrigin({100, 300});
    check(back.target()->path.has_value() && near(must(back.layer(), "icon.glyph").bounds().width, 14) && near(*border(must(back.layer(), "icon.plate.ring")), w.env.palette.tone(0xDDDDDD)),
        "Back: round, glyph 14, ring DDDDDD");
    w.move(w.centre(back));
    check(near(*must(back.layer(), "icon.plate").tint(), w.env.palette.foreground(1)) && near(*must(back.layer(), "icon.glyph").tint(), w.env.palette.tone(0x111111))
        && near(must(back.layer(), "icon.plate").tintTrack.duration, .12) && model(back.layer(), Channel::scaleX) == 1, "Back hover: filled, .12 s, no scale");
    auto& big = w.b.make<d::IconButton>("big", "Play", d::Icon::play, S::dark, true, true);
    check(big.layer().bounds().width == 44 && near(must(big.layer(), "icon.glyph").bounds().width, 20) && big.target()->path.has_value(), "Big 44 / 20, round");
    auto& tiny = w.b.make<d::IconButton>("tiny", "X", d::Icon::x, S::back, false, false, 24.0, 10.0);
    check(near(tiny.layer().bounds().width, 24), "Custom side");

    auto& cancel = w.b.make<d::Pill>("cancel", "Cancel", d::Icon::x, d::Pill::Kind::cancel);
    check(near(cancel.layer().bounds().width, std::max(150.0, 16 + 22 + 8 + fakeWidth("Cancel", 14, .06) + 10 + 28 + 5)), "Pill width max(150, ...) (:198)");
    const double longWidth = 16 + 22 + 8 + fakeWidth("Delete everything now", 14, .06) + 10 + 28 + 5;
    auto& danger = w.b.make<d::Pill>("danger", "Delete everything now", d::Icon::trash, d::Pill::Kind::danger);
    check(near(danger.layer().bounds().width, longWidth) && near(must(danger.layer(), "pill.glyph").frame(), {longWidth - 33, 5, 28, 28}), "Glyph circle at (w - 33, 5)");
    check(near(*border(must(danger.layer(), "pill.ring")), d::rgb(0xE5383B, .9)) && near(*fill(must(danger.layer(), "pill.glyph")), d::fixed::red), "Danger ring and circle");
    const double labelX = longWidth - 33 - 10 - fakeWidth("Delete everything now", 14, .06);
    check(near(must(danger.layer(), "pill.line").frame(), {16, 18.5, std::max(22.0, labelX - 24), 1}) && model(must(danger.layer(), "pill.line"), Channel::opacity) == .4, "Line .4");
    check(find(cancel.layer(), "pill.ring") == nullptr, "Cancel has no outer ring");
    w.host(danger.layer()); danger.layer().setOrigin({10, 400});
    w.move(w.centre(danger));
    const auto& circle = must(danger.layer(), "pill.glyph");
    check(near(model(circle, Channel::rotation), -8 * 3.14159265358979323846 / 180) && model(circle, Channel::scaleX) == 1.1 && model(must(danger.layer(), "pill.line"), Channel::opacity) == .75
        && near(*must(danger.layer(), "pill.plate").tint(), w.env.palette.foreground(1)), "Hover: 1.1, -8 deg, line .75, plate white");
    w.move(w.centre(danger), true);
    check(model(danger.layer(), Channel::scaleX) == .97, "Press .97");

    auto& swatch = w.b.make<d::Swatch>("swatch", "Cyan", d::fixed::cyan);
    check(must(swatch.layer(), "swatch.ring").hidden() && near(must(swatch.layer(), "swatch.ring").frame(), {-5, -5, 34, 34}), "Selection ring at inset -5, hidden");
    swatch.setSelected(true);
    check(!must(swatch.layer(), "swatch.ring").hidden() && items(swatch, w.root)[0].on, "Selected: ring shown; a selected tab");
    w.host(swatch.layer()); swatch.layer().setOrigin({500, 10}); w.move(w.centre(swatch));
    check(model(swatch.layer(), Channel::scaleX) == 1.08, "Hover 1.08");
    auto& custom = w.b.make<d::Swatch>("custom", "Custom", std::nullopt, 28.0);
    check(must(custom.layer(), "swatch.wheel").masksToBounds() && near(must(custom.layer(), "swatch.wheel").maskCornerRadius(), 14), "The custom wheel is a clipped circle");

    auto& grip = w.b.make<d::DragGrip>("grip", "", d::Size{30, 40}, Point{12, 15});
    w.host(grip.layer()); grip.layer().setOrigin({200, 200});
    check(near(must(grip.layer(), "grip.dots").frame(), {12, 15, 6, 10}) && grip.accessibilityRole() == std::nullopt, "Dots 6 x 10; no label, no item");
    std::vector<std::pair<d::DragGrip::Phase, Point>> phases;
    grip.onDrag = [&](d::DragGrip::Phase p, Point at) { phases.emplace_back(p, at); };
    int activations = 0; grip.onActivate = [&] { ++activations; };
    grip.pointerDown({5, 5}); grip.pointerDragged({7, 6});
    check(phases.empty(), "No drag within 3 px");
    grip.pointerDragged({9, 5});
    check(phases.size() == 2 && phases[0].first == d::DragGrip::Phase::began && phases[0].second == Point{5, 5} && phases[1].second == Point{9, 5}, "began reports the press point");
    check(near(*must(grip.layer(), "grip.dots").tint(), d::fixed::bracket), "Not hovered yet");
    grip.clearInteractionState();
    check(phases.size() == 3 && phases[2].first == d::DragGrip::Phase::ended && phases[2].second == Point{5, 5}, "Clearing mid-drag ends at the start point");
    grip.pointerDown({5, 5}); grip.pointerUp({6, 5});
    check(activations == 1, "A press without a drag activates");
    w.move(Point{215, 220});
    check(near(*must(grip.layer(), "grip.dots").tint(), d::fixed::nearBlack), "Hover darkens the dots");
}

// ---------------------------------------------------------------------------
// HUDSlider, HUDSliderBubble (HUDDesignSlider.swift)
// ---------------------------------------------------------------------------
void sliders() {
    World w;
    auto& top = w.arena.make("top");
    top.setFrame(d::metric::box); top.setZPosition(d::order::menu); w.root.add(top);
    auto& slider = w.b.make<d::Slider>("slider", "Opacity", 62.0, d::Slider::Range{0, 100, 1}, d::ValueFormat::percent, 300.0);
    w.host(slider.layer()); slider.layer().setOrigin({100, 200});
    slider.setBubbleHost(&top);
    const double track = 300 - 66;
    check(near(must(slider.layer(), "slider.track").frame(), {0, 11, track, 5}) && near(*fill(must(slider.layer(), "slider.track")), d::fixed::sliderTrack), "Track max(40, w - 66) at y 11");
    const auto& fillLayer = must(slider.layer(), "slider.fill");
    check(near(model(fillLayer, Channel::scaleX) * track, .62 * track) && model(fillLayer, Channel::translateX) == 0, "Fill from 0 to the thumb");
    check(near(must(slider.layer(), "slider.thumb").position().x, .62 * track) && near(must(slider.layer(), "slider.thumb").position().y, 13.5), "Thumb centred at (x, 13.5)");
    const auto& readout = d::textOf(must(slider.layer(), "slider.readout"));
    check(readout.text() == "[ 62% ]" && near(readout.lineFrame, {300 - 58, 9, 58, 12}) && near(readout.runs[0].style.color, d::fixed::bracket), "Readout `[ value ]` (:103-105)");
    check(slider.contains({-4, 15}) && slider.contains({track + 4, 15}) && !slider.contains({track + 5, 15}), "Hit area x -4.5 ... track + 4.5");
    std::vector<std::pair<double, bool>> changes;
    slider.onChange = [&](double v, bool final) { changes.emplace_back(v, final); };
    w.move(Point{130, 215}, true);
    check(slider.pointerDown({30, 15}) && slider.dragging() && changes.back() == std::pair{std::round(30 / track * 100), false}, "A press jumps to the pointer (:114-122)");
    check(near(model(must(slider.layer(), "slider.thumb"), Channel::scaleY), 21.0 / 17), "The thumb grows while dragging");
    const auto& bubble = must(top, "slider.bubble");
    check(model(bubble, Channel::opacity) == 1 && model(bubble, Channel::scaleX) == 1 && bubble.zPosition() == d::order::bubble, "The bubble shows in the top layer (z 40)");
    slider.pointerDragged({150, 15});
    const double dragged = std::round(150 / track * 100);
    check(changes.back() == std::pair{dragged, false} && d::textOf(must(bubble, "text")).text() == std::to_string(int(dragged)) + "%", "Drag follows; the bubble shows the value");
    const auto anchor = 100 + dragged / 100 * track;
    check(near(bubble.position().x, anchor) && near(bubble.position().y, 200 + 2 - 6 + 4), "Bubble bottom-centred 6 px above (thumb x, track y 2)");
    slider.pointerUp({150, 15});
    check(!slider.dragging() && changes.back() == std::pair{dragged, true} && model(bubble, Channel::opacity) == 0, "Release commits and hides the bubble");
    slider.nudge(1);
    check(changes.back() == std::pair{dragged + 1, true}, "nudge commits one step");
    const auto count = changes.size();
    slider.pointerDown({10, 15}); slider.cancelDrag();
    check(changes.size() == count + 1 && !changes.back().second && !slider.dragging(), "cancelDrag ends without a commit (:136-139)");
    const auto item = items(slider, w.root);
    check(item.size() == 1 && item[0].role == d::AccessibilityRole::slider && item[0].number == slider.value() && item[0].maximum == 100 && item[0].step == 1
        && item[0].value == d::formatValue(d::ValueFormat::percent, slider.value()), "Slider item: value, range, step, description");
    auto& zero = w.b.make<d::Slider>("zero", "Balance", -20.0, d::Slider::Range{-100, 100, 5, 0.0}, d::ValueFormat::balance, 260.0, false, true, false);
    const double zt = 260 - 66;
    check(near(must(zero.layer(), "slider.zero").frame(), {zt / 2 - .5, 8, 1, 11}) && !must(zero.layer(), "slider.zero").hidden() && must(zero.layer(), "slider.ticks").hidden(),
        "Centre-zero marker; ticks hidden");
    check(near(model(must(zero.layer(), "slider.fill"), Channel::translateX), .4 * zt) && near(model(must(zero.layer(), "slider.fill"), Channel::scaleX) * zt, .1 * zt), "Fill between zero and the thumb");
    check(d::textOf(must(zero.layer(), "slider.readout")).text() == "[ L 20 ]", "Balance readout");
    auto& bare = w.b.make<d::Slider>("bare", "Seek", .25, d::Slider::Range{0, 1, 0}, d::ValueFormat::decimal, 200.0, false, false);
    check(near(must(bare.layer(), "slider.track").frame().width, 200) && must(bare.layer(), "slider.readout").hidden(), "No readout: the track takes the width");
    check(d::Slider::Range{0, 10, 2}.clamp(4.9) == 4 && d::Slider::Range{1, 10, 2}.clamp(4.1) == 5, "Values snap to minimum + k x step");
    World light(true);
    auto& lightDark = light.b.make<d::Slider>("dark", "Volume", 50.0, d::Slider::Range{0, 100, 1}, d::ValueFormat::percent, 300.0, true);
    check(near(*fill(must(lightDark.layer(), "slider.thumb")), d::fixed::counterBackground), "Light disables the dark variant (:44-46)");
    World dark;
    auto& darkSlider = dark.b.make<d::Slider>("dark", "Volume", 50.0, d::Slider::Range{0, 100, 1}, d::ValueFormat::percent, 300.0, true);
    check(near(*fill(must(darkSlider.layer(), "slider.thumb")), d::rgb(0xEDEDED)) && near(d::textOf(must(darkSlider.layer(), "slider.readout")).runs[1].style.color, dark.env.palette.token(d::ThemeToken::textOnDark)),
        "Dark variant: EDEDED thumb, textOnDark value");
}

// ---------------------------------------------------------------------------
// HUDDropdown, HUDMenu, HUDPopover (HUDDesignMenu.swift)
// ---------------------------------------------------------------------------
void menus() {
    World w;
    auto& dropdown = w.b.make<d::Dropdown>("dropdown", "Language", "简体中文", d::TextRegion::simplifiedChinese);
    w.host(dropdown.layer()); dropdown.layer().setOrigin({300, 100});
    check(near(d::localBounds(dropdown.layer()), {0, 0, 178, 30}) && near(d::textOf(must(dropdown.layer(), "dropdown.value")).lineFrame, {12, (30 - 12.5) / 2, 178 - 45, 12.5}),
        "Regular 178 x 30; value at x 12, width w - 45");
    check(d::textOf(must(dropdown.layer(), "dropdown.value")).language == d::TextRegion::simplifiedChinese, "The value carries its language");
    const auto& caret = must(dropdown.layer(), "dropdown.caret");
    check(near(caret.position().x, 178 - 14.5) && model(must(dropdown.layer(), "dropdown.bar"), Channel::scaleY) == 0, "Caret at w - 14.5; bar collapsed");
    w.move(w.centre(dropdown));
    check(model(must(dropdown.layer(), "dropdown.bar"), Channel::scaleY) == 1 && model(caret, Channel::translateY) == 2 && near(caret.property[std::size_t(Channel::translateY)].duration, .16), "Hover: bar, caret 2 px down");
    w.move(std::nullopt); dropdown.setOpen(true);
    check(near(model(caret, Channel::rotation), 3.14159265358979323846) && model(must(dropdown.layer(), "dropdown.bar"), Channel::scaleY) == 1, "Open: caret turned, bar kept");
    const auto popUp = items(dropdown, w.root);
    check(popUp.size() == 1 && popUp[0].role == d::AccessibilityRole::popUp && popUp[0].value == "简体中文", "popUp with the value");
    auto& small = w.b.make<d::Dropdown>("small", "Sort", "Newest first", std::nullopt, d::Dropdown::Height::compact);
    check(near(small.layer().bounds().width, std::max(96.0, 12 + fakeWidth("Newest first", 12) + 33)) && small.layer().bounds().height == 26, "Small: natural width, 26");
    small.setArtwork(d::ImageSource{d::ImageSource::Kind::memory, "logo", {}, 1}, d::Size{80, 40}, 18);
    check(must(small.layer(), "dropdown.value").hidden() && near(must(small.layer(), "dropdown.artwork").frame().height, 18) && small.valueText() == "Newest first", "Artwork replaces the text, the value stays spoken");
    small.setArtwork(std::nullopt, {}, 18);
    check(!must(small.layer(), "dropdown.value").hidden(), "nil restores the text");

    using Item = d::Menu::Item;
    std::vector<Item> list{{"a", "English"}, {"b", "简体中文", d::TextRegion::simplifiedChinese, true}, {"c", "繁體中文", d::TextRegion::traditionalChinese},
        {"d", "日本語", d::TextRegion::japanese}, {"e", "한국어", d::TextRegion::korean, false, false, true}, {"f", "Delete", {}, false, true}, {"g", "Disabled", {}, false, false, false, false}};
    d::Menu menu(w.env, "menu.short", list, 178);
    w.root.add(menu.layer());
    check(!menu.isLong() && near(menu.layer().bounds().width, std::ceil(std::max(178.0, 150.0))) && near(menu.layer().bounds().height, 7 * 30 + 9 + 8), "7 rows + one separator + 8 (:292-324)");
    check(menu.keyboardIndex() == 1u && menu.layer().zPosition() == d::order::menu, "Opens on the selected item, z 30");
    menu.place({380, 300, 178, 30}, {590, 510});
    check(near(menu.layer().frame().x, 380) && near(menu.layer().frame().y, 300 - 4 - menu.layer().bounds().height), "Flipped above past the box bottom + 20 (:353-359)");
    menu.place({500, 100, 178, 30}, {590, 510});
    check(near(menu.layer().frame().x, 590 - 178) && near(menu.layer().frame().y, 134), "Shifted left into the box, below + 4");
    const auto rows = menu.controls();
    check(rows.size() == 7 && rows[0]->id() == "menu.short:a", "Row ids are menu:item");
    check(near(rows[4]->layer().frame().y, 4 * 30 + 9) && !rows[6]->enabled() && near(model(rows[6]->layer(), Channel::opacity), .35), "Separator slot 9; disabled rows .35");
    check(near(*colorKey(must(rows[1]->layer(), "menu.item.mark"), "backgroundColor"), w.env.palette.accentInk()), "The selected mark in accentInk");
    check(model(must(rows[1]->layer(), "menu.item.plate"), Channel::opacity) == 0,
        "Opening sets the keyboard row without highlighting it (assigned in init: no didSet, :341-343)");
    check(near(textColor(must(rows[5]->layer(), "menu.item.text")), w.env.palette.token(d::ThemeToken::danger)), "Danger rows in the danger colour");
    std::vector<std::string> picks;
    menu.onPick = [&](const Item& i) { picks.push_back(i.id); }; menu.onClose = [&] { picks.push_back("close"); };
    d::KeyInput key; key.virtualKey = d::key::down;
    check(menu.handleKey(key) && menu.keyboardIndex() == 2u, "Down moves");
    check(model(must(rows[2]->layer(), "menu.item.plate"), Channel::opacity) == 1 && near(textColor(must(rows[2]->layer(), "menu.item.text")), d::fixed::nearBlack),
        "The keyboard row shows the plate and #111 text");
    for (int i = 0; i < 4; ++i) menu.handleKey(key);
    check(menu.keyboardIndex() == 0u, "Down wraps over enabled items (the disabled row is skipped)");
    key.virtualKey = d::key::up; menu.handleKey(key);
    check(menu.keyboardIndex() == 5u, "Up wraps backwards");
    key.virtualKey = 'Q'; check(menu.handleKey(key), "Every other key is swallowed");
    key.virtualKey = d::key::enter; menu.handleKey(key);
    key.virtualKey = d::key::escape; menu.handleKey(key);
    check(picks == std::vector<std::string>{"f", "close"}, "Return picks, Esc closes");
    w.move(w.centre(*rows[3]));
    check(!menu.keyboardIndex() && model(must(rows[3]->layer(), "menu.item.plate"), Channel::opacity) == 1 && model(must(rows[5]->layer(), "menu.item.plate"), Channel::opacity) == 0,
        "Pointer hover clears the keyboard row");
    const auto item = items(*rows[1], w.root);
    check(item[0].role == d::AccessibilityRole::menuItem && item[0].on, "Menu items with their selection");
    menu.playEntrance(w.env.clock());
    check(menu.layer().slots[std::size_t(d::MotionSlot::wipe)][0].present() && near(menu.layer().slots[std::size_t(d::MotionSlot::fade)][0].values[0], .4), "Top-down wipe with a fade from .4");

    std::vector<Item> many;
    for (int i = 0; i < 12; ++i) many.push_back({"i" + std::to_string(i), "Display " + std::to_string(i), {}, i == 9});
    d::Menu longMenu(w.env, "menu.long", many, 178);
    w.root.add(longMenu.layer()); longMenu.place({10, 50, 178, 30}, {590, 510});
    check(longMenu.isLong() && near(longMenu.layer().bounds().height, 218) && longMenu.controls().size() == 13, "More than 7: a 210 list and a track control");
    check(near(longMenu.scrollOffset(), std::min(9.0 * 30 - 60, 12.0 * 30 - 210)), "Opens scrolled to rowTop(selected) - 60, clamped");
    const auto& rail = must(longMenu.layer(), "menu.rail");
    check(model(rail, Channel::opacity) == 0, "The rail is hidden at rest");
    longMenu.scroll(1000);
    check(model(rail, Channel::opacity) == 0, "A wheel that does not move shows nothing");
    longMenu.setScroll(0); longMenu.scroll(1000);
    check(near(longMenu.scrollOffset(), 12 * 30 - 210) && model(rail, Channel::opacity) == 1, "Wheel scrolls to the end and shows the bar");
    {
        const auto rows = longMenu.controls();
        const auto first = items(*rows[0], w.root), last = items(*rows[11], w.root);
        check(first.size() == 1 && first[0].offscreen && last.size() == 1 && !last[0].offscreen,
            "Rows scrolled out of a long menu stay listed, offscreen (UIA IsOffscreen, like a scrolled NSMenu's items)");
    }
    w.at(w.env.now + 1.1);
    check(model(rail, Channel::opacity) == 0 && near(rail.property[std::size_t(Channel::opacity)].duration, .3), "The bar hides 1 s later (.30 easeIn)");
    auto* track = longMenu.controls().back();
    track->pointerDown({2, 0});
    check(near(longMenu.scrollOffset(), 0) && model(rail, Channel::opacity) == 1, "A press at the top of the track scrolls to the start");
    track->pointerUp({2, 0});
    check(near(must(longMenu.layer(), "menu.track").frame().x, longMenu.layer().bounds().width - 9), "Track 5 px wide at w - 9");

    std::vector<Item> grid;
    for (int i = 0; i < 9; ++i) grid.push_back({"t" + std::to_string(i), "Icon " + std::to_string(i), {}, i == 4, false, false, true, d::Menu::IconTile{{d::ImageSource::Kind::memory, "icon"}}});
    grid.push_back({"custom", "Custom…"});
    d::Menu gridMenu(w.env, "menu.grid", grid, 120);
    // fit = (300 - 16 + 6) / 46 = 6; lines = 2; columns = 5 (:285-287).
    const double gridWidth = 5 * 46 - 6 + 16;
    check(near(gridMenu.layer().bounds().width, std::ceil(std::max({120.0, 150.0, 14 + fakeWidth("Custom…", 12.5) + 36, gridWidth}))), "Grid width");
    const auto tiles = gridMenu.controls();
    check(near(tiles[5]->layer().frame().y, 4 + 46) && near(tiles[1]->layer().frame().x - tiles[0]->layer().frame().x, 46), "Tiles 40 + 6 apart, two rows");
    check(model(must(tiles[4]->layer(), "menu.tile.ring"), Channel::opacity) == 1, "The selected tile's ring");
    key.virtualKey = d::key::down; gridMenu.handleKey(key);
    check(gridMenu.keyboardIndex() == 9u, "Down from the second row leaves the grid for the text rows");
    key.virtualKey = d::key::up; gridMenu.handleKey(key);
    check(gridMenu.keyboardIndex() == 8u, "Up from the text row goes back to the last enabled item");

    d::Popover popover(w.env, "Pen", std::string("画笔"), 270, 120);
    popover.place({400, 300, 30, 30}, {590, 510});
    check(near(popover.layer().bounds().height, 34 + 120 + 12) && near(popover.layer().frame(), {430 - 270, 300 - 8 - 166, 270, 166}), "Right-aligned above the anchor (:490-496)");
    check(popover.closeButton() && popover.closeButton()->label == "Close" && near(popover.closeButton()->layer().frame(), {270 - 36, 6, 24, 24}), "Close button");
    check(near(popover.content().frame(), {12, 34, 246, 120}), "Content at (12, 34)");
    bool closed = false; popover.onClose = [&] { closed = true; }; popover.closeButton()->activate();
    check(closed, "The close button calls onClose");
    d::Popover below(w.env, "Pen", std::nullopt, 270, 120);
    below.place({40, 90, 30, 30}, {590, 510});
    check(near(below.layer().frame().y, 128) && near(below.layer().frame().x, 0), "Below when there is no room, clamped to the box");
    d::Popover headerless(w.env, "", std::nullopt, 270, 80, false);
    check(!headerless.closeButton() && near(headerless.layer().bounds().height, 12 + 80 + 12) && near(headerless.content().frame().y, 12), "Headerless: 12 + content + 12");
    World zh; zh.env.language = endfield::core::Language::simplifiedChinese;
    d::Popover chinese(zh.env, "Pen", std::nullopt, 270, 10);
    check(chinese.closeButton()->label == "关闭", "Close is localized");
}

// ---------------------------------------------------------------------------
// HUDTabs (HUDDesignTabs.swift:7-152)
// ---------------------------------------------------------------------------
void tabs() {
    World w;
    d::Tabs tabs(w.b, "tabs", {{"All", std::nullopt}, {"Text", d::Icon::text}, {"Links", d::Icon::link}, {"Images", d::Icon::image}}, 0, 590);
    w.host(tabs.layer());
    const double width = (590 - 56) / 4.0;
    const auto& first = tabs.controls()[0]->layer();
    check(near(first.bounds().height, 37) && near(first.position().y, 15.5) && first.zPosition() == 1, "Active tab 37 tall, raised 3 px, z 1 (:118-119)");
    check(near(tabs.controls()[1]->layer().frame(), {28 + width, 0, width, 34}), "Inactive tabs 34, equal widths after the 28 px keycap inset");
    check(near(must(first, "tab.underline").frame(), {width * .32, 40, width * .36, 3}) && !must(first, "tab.underline").hidden(), "Underline 36 % x 3 at h + 3");
    check(near(textColor(must(first, "tab.text")), d::fixed::nearBlack) && d::textOf(must(first, "tab.text")).style().weight == d::FontWeight::bold, "Active text bold #111");
    check(must(tabs.layer(), "tabs.separator.1").hidden() && !must(tabs.layer(), "tabs.separator.2").hidden(), "Separators hidden beside the active tab");
    const auto& frame = must(tabs.layer(), "tabs.frame");
    check(tabs.layer().children()[0] == &frame && near(*border(frame), w.env.palette.token(d::ThemeToken::ringTab)), "The ring frame lies beneath the tabs (round-1 fix)");
    check(near(must(tabs.layer(), "tabs.keycap.e").frame(), {590 - 21.5, 9.5, 15, 15}), "E keycap at w - 21.5");
    w.move(w.centre(*tabs.controls()[2]));
    const auto& hovered = tabs.controls()[2]->layer();
    check(model(must(hovered, "tab.hover"), Channel::opacity) == 1 && model(hovered, Channel::translateY) == -1 && near(textColor(must(hovered, "tab.text")), w.env.palette.foreground(1)),
        "Inactive hover: fill, lift 1, white text");
    w.move(std::nullopt);
    std::vector<std::pair<int, double>> selections;
    tabs.onSelect = [&](int i, double direction) { selections.emplace_back(i, direction); };
    tabs.controls()[2]->activate();
    check(tabs.selectedIndex() == 2 && selections.back() == std::pair{2, 1.0} && must(hovered, "tab.fill").slots[std::size_t(d::MotionSlot::wipe)][0].present()
        && must(hovered, "tab.fill").wipeOrigin == d::WipeOrigin::centerX, "Selection wipes from the centre; +1 forward");
    tabs.cycle(1); tabs.cycle(1); tabs.cycle(-1);
    check(tabs.selectedIndex() == 3 && selections.back() == std::pair{3, -1.0} && selections[selections.size() - 2] == std::pair{0, 1.0}, "Q/E wrap around");
    tabs.setNews(1, true);
    check(!must(tabs.controls()[1]->layer(), "tab.dot").hidden(), "The news diamond");
    check(items(*tabs.controls()[3], w.root)[0].on && !items(*tabs.controls()[0], w.root)[0].on, "Tabs are tabs with their selection");
    d::Tabs compact(w.b, "compact", {{"A", std::nullopt}, {"B", std::nullopt}}, 1, 300, false, true);
    check(near(compact.controls()[0]->layer().frame(), {0, 0, 150, 34}) && d::textOf(must(compact.controls()[0]->layer(), "tab.text")).style().size == 11
        && d::textOf(must(compact.controls()[0]->layer(), "tab.text")).style().tracking == 0, "No keycaps: no inset; compact 11 px without tracking (:23)");
}

// ---------------------------------------------------------------------------
// Badges, keycaps, field frames, empty states, numerals, plates, cells (HUDDesignBadges.swift)
// ---------------------------------------------------------------------------
void badges() {
    World w;
    d::Chip chip(w.b, "chip", "Pinned", d::Chip::Variant::dark, d::Icon::pin);
    const double text = fakeWidth("PINNED", 9.5, .1);
    check(near(d::localBounds(chip.layer()), {0, 0, std::ceil(7 + 15 + text + 7), 18}) && near(*fill(chip.layer()), d::fixed::counterBackground), "Chip width ceil(x + text + 7), 18 tall (:37-46)");
    check(near(must(chip.layer(), "chip.icon").frame(), {7, 4, 10, 10}) && near(d::textOf(must(chip.layer(), "chip.text")).lineFrame, {22, 4.25, text + 1, 9.5}), "Icon at (7, 4), text at y 4.25");
    d::Chip outline(w.b, "outline", "x", d::Chip::Variant::outline);
    check(near(*border(outline.layer()), w.env.palette.tone(0x5A5A5A)) && near(textColor(must(outline.layer(), "chip.text")), w.env.palette.tone(0xCFCFCF)), "Outline chip");
    auto& badge = d::badge::newBadge(w.b, "new");
    check(near(badge.bounds().width, std::ceil(fakeWidth("NEW", 8, .06)) + 10) && near(badge.baseTransform().c, std::tan(-8 * 3.14159265358979323846 / 180)) && badge.zPosition() == 2,
        "NEW: skew m21 tan(-8 deg), z 2");
    auto& stamp = d::badge::stamp(w.b, "stamp", "Classified");
    check(near(stamp.bounds().width, std::ceil(fakeWidth("CLASSIFIED", 10, .16)) + 20) && near(*border(stamp), w.env.palette.accentInk()) && stamp.animatable(), "Stamp width + 20, accentInk, poppable");
    auto& diamond = d::badge::diamond(w.b, "dot");
    check(near(diamond.baseTransform().b, std::sin(3.14159265358979323846 / 4)) && near(*fill(diamond), d::fixed::orange), "Diamond: orange rotated 45 deg");

    auto& keys = w.b.make<d::KeycapGroup>("keys", "Summon", std::vector<std::string>{"⌥", "Space"}, true);
    const double cap0 = std::max(34.0, std::ceil(fakeWidth("⌥", 11)) + 16), cap1 = std::max(34.0, std::ceil(fakeWidth("Space", 11)) + 16);
    check(near(keys.layer().bounds().width, std::max(178.0, 10 + cap0 + 6 + 14 + cap1 + 6 + 4)) && keys.layer().bounds().height == 34, "Width max(178, x + 4) (:134)");
    check(keys.accessibilityRole() == d::AccessibilityRole::button && keys.pointerDown({1, 1}), "Interactive keycaps are buttons");
    keys.setRecording(true, "Press a shortcut");
    const auto& stripes = must(keys.layer(), "keys.scan.stripes");
    check(!must(keys.layer(), "keys.scan").hidden() && must(keys.layer(), "keys.caps").hidden() && near(*border(keys.layer()), w.env.palette.accentInk()), "Recording: accent ring, keys hidden");
    check(std::isinf(stripes.slots[std::size_t(d::MotionSlot::effect)][0].repeat) && near(stripes.slots[std::size_t(d::MotionSlot::effect)][0].duration, 1)
        && std::isinf(must(keys.layer(), "keys.cursor").slots[std::size_t(d::MotionSlot::effect)][0].repeat), "Scan 1 s and blink loops, infinite");
    keys.setRecording(false, "");
    check(!stripes.animating(w.env.now) && !must(keys.layer(), "keys.cursor").animating(w.env.now) && must(keys.layer(), "keys.scan").hidden(), "Loops only while recording");
    w.env.reduceMotion = true; keys.setRecording(true, "Reduced");
    check(!stripes.animating(w.env.now) && !must(keys.layer(), "keys.scan").hidden(), "Reduce Motion: static stripes, solid cursor");
    w.env.reduceMotion = false;
    auto& display = w.b.make<d::KeycapGroup>("display", "Shortcut", std::vector<std::string>{"Esc"}, false);
    check(near(display.layer().bounds().width, 10 + std::max(34.0, std::ceil(fakeWidth("Esc", 11)) + 16) + 6 + 4) && display.accessibilityRole() == std::nullopt && !display.pointerDown({1, 1}), "Display keycaps: natural width, not a control");

    d::FieldFrame field(w.b, "field", {240, 34});
    check(near(*fill(field.layer()), w.env.palette.token(d::ThemeToken::field)) && near(*border(field.layer()), w.env.palette.token(d::ThemeToken::ringField)) && must(field.layer(), "field.bar").hidden(), "Normal");
    field.setState(d::FieldFrame::State::focused);
    check(near(*border(field.layer()), w.env.palette.accentInk()) && near(*fill(must(field.layer(), "field.bar")), w.env.palette.accentInk()), "Focused: accent ring and bar");
    field.setState(d::FieldFrame::State::invalid);
    check(near(*border(field.layer()), d::fixed::red) && near(*fill(must(field.layer(), "field.bar")), d::fixed::red), "Invalid: red");
    d::FieldFrame lightField(w.b, "light", {240, 34}, true);
    check(near(*fill(lightField.layer()), d::rgb(0xF2F3EE)) && near(*border(lightField.layer()), d::rgb(0xB4B5B0)), "Light field");
    d::FieldFrame plain(w.b, "plain", {240, 34}, false, true);
    plain.setState(d::FieldFrame::State::focused);
    check(!plain.layer().content() && must(plain.layer(), "field.bar").hidden(), "Plain: no fill, ring or bar");

    for (auto language : {endfield::core::Language::english, endfield::core::Language::simplifiedChinese, endfield::core::Language::japanese}) {
        World e; e.env.language = language;
        auto& empty = d::emptyState::bilingual(e.b, "empty", "NO RECORDS", "剪贴板为空");
        const auto& title = d::textOf(must(empty, "empty.title"));
        check(title.text() == "[ NO RECORDS ]" && near(title.runs[0].style.color, e.env.palette.accentInk()), "English always in the brackets (:246-250)");
        const auto* subtitle = find(empty, "empty.subtitle");
        if (language == endfield::core::Language::english) check(!subtitle && near(title.lineFrame.y, (250 - 20) / 2.0), "English: no translation line");
        else check(subtitle && d::textOf(*subtitle).text() == endfield::core::localized("NO RECORDS", "剪贴板为空", language) && near(title.lineFrame.y, (250 - 41.5) / 2),
            "Other languages: the translation below");
    }
    check(near(d::emptyState::textBottom({400, 200}, true, 40), (200 - 41.5 - 40) / 2 + 41.5), "textBottom (:287-289)");
    check(near(d::localBounds(d::emptyState::crosshair(w.b, "cross")), {0, 0, 11, 11}), "Crosshair 11 x 11");

    d::BigNumeral numeral(w.b, "numeral");
    numeral.set("72", "%");
    const double n = fakeWidth("72", 64, -.02), u = fakeWidth("%", 64 * .34);
    check(near(d::localBounds(numeral.layer()), {0, 0, n + 4 + u, 64 * .9}) && near(d::textOf(must(numeral.layer(), "numeral.unit")).lineFrame, {n + 4, 64 * .08, u + 1, 64 * .34}), "Unit top-aligned at x width + 4 (:476-487)");
    check(near(textColor(must(numeral.layer(), "numeral.unit")), w.env.palette.yellow()), "Unit in legible(yellow)");
    d::StatPlate plate(w.b, "stat", 180);
    plate.set("Cycle count", "412", std::string("cycles"), true);
    check(near(plate.layer().bounds().height, 53) && d::textOf(must(plate.layer(), "stat.value")).text() == "412 cycles"
        && near(d::textOf(must(plate.layer(), "stat.value")).runs[0].style.color, w.env.palette.accentInk()), "53 tall; highlighted value in accentInk with a unit run");
    const auto grid = d::StatPlate::gridFrames(4, 590);
    check(near(grid[3], {0, 61, (590 - 16) / 3.0, 53}), "3-column grid, gap 8");

    for (bool capacity : {true, false}) {
        d::Cells cells(w.b, capacity ? "capacity" : "level", 10, 300, capacity);
        cells.set(6, std::nullopt, false, false);
        const auto& on = must(cells.layer(), "cell.5.fill"), &off = must(cells.layer(), "cell.6.fill");
        check(near(must(cells.layer(), "cell.0").frame(), {0, 0, (300 - 27) / 10.0, capacity ? 22.0 : 8.0}), "Cells: gap 3, 22 or 8 tall");
        if (capacity) check(!on.tint() && on.content() && (*on.content())["children"].array()[0]["gradient"].isObject(), "Filled capacity cells are a gradient");
        else check(near(*fill(on), w.env.palette.yellow()), "Filled segments are solid yellow");
        check(near(*fill(off), capacity ? w.env.palette.tone(0x2A2A2A) : w.env.palette.token(d::ThemeToken::iconButton)), "Empty cells");
        cells.set(7, std::nullopt, true, true, std::nullopt, 3);
        check(cells.pulseCount(w.env.now) == 3 && near(must(cells.layer(), "cell.8.fill").tintEffect.start - w.env.now, .08) && near(must(cells.layer(), "cell.8.fill").tintEffect.duration, 2.4),
            "The next 3 empty cells pulse, 80 ms apart, 2.4 s");
        check(near(must(cells.layer(), "cell.7.fill").tintEffect.values[1], d::rgb(0x4D4A1C)), "Pulse mid 4D4A1C without a tone");
    }
    World lightCells(true);
    d::Cells pale(lightCells.b, "pale", 4, 100, true);
    pale.set(1, std::nullopt, true);
    const auto empty = lightCells.env.palette.tone(0x2A2A2A);
    check(near(must(pale.layer(), "cell.1.fill").tintEffect.values[1], d::mixed(empty, lightCells.env.palette.yellowHigh(), .35)), "Light: the tone mixed 35 % into the empty cell");
    w.env.reduceMotion = true;
    d::Cells reduced(w.b, "reduced", 4, 100, true);
    reduced.set(1, std::nullopt, true);
    check(reduced.pulseCount(w.env.now) == 0, "No pulse under Reduce Motion");
}

// ---------------------------------------------------------------------------
// HUDScrollArea + HUDScrollBarVisibility (HUDDesignBadges.swift:305-458)
// ---------------------------------------------------------------------------
void scrolling() {
    World w;
    d::ScrollArea area(w.b, "rows", {0, 125, 590, 300});
    w.host(area.layer());
    check(area.layer().masksToBounds() && area.track().hidden() && near(area.contentWidth(), 590) && area.maximumOffset() == 0, "Short content: no bar, full width");
    area.setContentHeight(900);
    check(!area.track().hidden() && near(area.contentWidth(), 580) && area.maximumOffset() == 600, "Long content: the bar takes 10 px");
    const auto& thumb = must(area.layer(), "scroll.thumb");
    const auto thumbContent = thumb.content();
    check(near(thumb.frame().y, 0) && near(d::thumbLength(thumb), 100) && model(area.track(), Channel::opacity) == 0, "Thumb max(18, h^2 / content); hidden at rest");
    // One 4 x 18 raster stretched: a length change never repaints it.
    area.setContentHeight(1800);
    check(near(d::thumbLength(thumb), 50) && thumb.bounds() == d::Size{4, 18} && thumb.content() == thumbContent, "A length change only rescales the thumb");
    area.setContentHeight(900);
    check(area.scroll(120) && near(area.offset(), 120) && near(area.content().frame().y, -120) && near(must(area.layer(), "scroll.thumb").frame().y, 120.0 / 600 * 200), "scroll(by:) moves");
    check(area.bar().shown() && model(area.track(), Channel::opacity) == 1 && near(area.track().property[std::size_t(Channel::opacity)].duration, .15), "Scrolling shows the bar (.15 easeOut)");
    w.at(w.env.now + .5);
    check(area.bar().shown(), "Still shown .5 s later");
    w.at(w.env.now + .6);
    check(!area.bar().shown() && model(area.track(), Channel::opacity) == 0 && near(area.track().property[std::size_t(Channel::opacity)].duration, .3), "Hidden 1 s after the last scroll (.30 easeIn)");
    area.setOffset(10000, true);
    const auto& content = area.content();
    check(near(area.offset(), 600) && content.slots[std::size_t(d::MotionSlot::effect)][0].present() && near(content.presented(Channel::translateY, w.env.now), 480), "Animated: glides from where it is (.2 s)");
    w.env.now += .3;
    check(near(content.presented(Channel::translateY, w.env.now), 0), "The glide lands");
    area.reveal({0, 100, 10, 40});
    check(near(area.offset(), 100) && !area.scroll(-1000) == false && near(area.offset(), 0), "reveal keeps a rect visible");
    // The strip: a container target showing the bar while hovered.
    w.move(Point{585, 200});
    check(area.bar().shown(), "Hovering the strip shows the bar");
    w.at(w.env.now + 5);
    check(area.bar().shown(), "Never hides while hovered");
    w.move(std::nullopt); w.at(w.env.now + 1.01);
    check(!area.bar().shown(), "Leaving the strip arms the hide");
    area.setDragging(true); w.at(w.env.now + 3);
    check(area.bar().shown(), "Shown while dragging");
    area.setDragging(false); area.resetBar();
    check(!area.bar().shown() && model(area.track(), Channel::opacity) == 0, "reset hides at once");
    w.env.reduceMotion = true; area.scroll(10);
    check(area.bar().shown() && !area.track().animating(w.env.now) && model(area.track(), Channel::opacity) == 1, "Reduce Motion shows at once");
    check(near(area.viewport(30), {0, 30, 590, 300}), "Viewport at an offset");
}

// ---------------------------------------------------------------------------
// HUDTaskCard, HUDListCard, HUDArchiveTile (HUDDesignCards.swift)
// ---------------------------------------------------------------------------
void cards() {
    World w;
    auto& task = w.b.make<d::TaskCard>("task", "Calibrate the relay", false, true, false, 280.0);
    w.host(task.layer()); task.layer().setOrigin({10, 100});
    const auto controls = task.controls();
    check(controls.size() == 3 && controls[1]->id() == "task:toggle" && controls[2]->id() == "task:delete" && task.target()->container && !task.pointerDown({5, 5}), "[card, action, delete]");
    check(controls[1]->label == "Calibrate the relay, In progress" && controls[2]->label == "Delete Calibrate the relay", "Action and delete labels");
    const auto& action = controls[1]->layer();
    const double pill = 13 + std::ceil(fakeWidth("In progress", 11.5)) + 13;
    check(near(action.frame(), {168 - 12 - pill, 17, pill, 26}) && near(controls[2]->layer().frame(), {168 - 12 - pill - 30, 19, 22, 22}), "Pill right-aligned 12 px in the side; x 8 px left of it");
    check(model(controls[2]->layer(), Channel::opacity) == 0 && items(task, w.root).size() == 1, "The x is transparent and still reachable by screen readers");
    check(!must(task.layer(), "task.attention").hidden() && near(must(task.layer(), "task.attention").position().x, 281), "Attention diamond at (w + 1, 1)");
    check(d::textOf(must(task.layer(), "task.count")).text() == "0/1" && model(must(task.layer(), "task.bar.fill"), Channel::scaleX) == .0001, "Open: 0/1, empty bar");
    w.move(w.centre(task));
    check(model(controls[2]->layer(), Channel::opacity) == 1 && near(controls[2]->layer().property[std::size_t(Channel::opacity)].duration, .12) && items(task, w.root).empty(),
        "Card hover fades the x in (.12)");
    w.move(w.centre(*controls[1]));
    check(near(*must(action, "task.action").tint(), d::white(1)), "Hovering the pill whitens it");
    bool toggled = false; task.onToggle = [&](bool done) { toggled = done; };
    controls[1]->activate();
    check(task.done() && toggled && model(must(task.layer(), "task.body.done"), Channel::opacity) == 1 && near(must(task.layer(), "task.body.done").property[std::size_t(Channel::opacity)].duration, .25),
        "Toggling flips the plates over .25 s");
    check(model(must(task.layer(), "task.watermark"), Channel::translateX) == 0 && near(must(task.layer(), "task.watermark").property[std::size_t(Channel::translateX)].duration, .45)
        && model(must(task.layer(), "task.ring"), Channel::scaleX) == 1 && model(must(task.layer(), "task.bar.fill"), Channel::scaleX) == 1, "Watermark, ring and bar");
    check(d::textOf(must(task.layer(), "task.count")).text() == "1/1" && near(textColor(must(task.layer(), "task.title")), d::rgb(0xF2F2F2)) && must(task.layer(), "task.attention").hidden(),
        "Done: 1/1, light title, no diamond");
    check(controls[1]->label == "Calibrate the relay, Done" && near(*must(action, "task.action").tint(), d::fixed::counterBackground), "Done pill");
    bool deleted = false; task.onDelete = [&] { deleted = true; }; controls[2]->activate();
    check(deleted, "The x deletes");
    auto& secret = w.b.make<d::TaskCard>("secret", "Secret", true, false, true, 280.0);
    check(secret.label == "Redacted" && find(secret.layer(), "task.title") == nullptr && find(secret.layer(), "task.title.redacted.redaction.bar"), "Recording: a bar and the Redacted label");

    auto& pin = w.b.make<d::IconButton>("pin", "Pin", d::Icon::pin, d::IconButton::Style::dark, false, false, 24.0, 12.0);
    auto& card = w.b.make<d::ListCard>("card", d::ListCard::Kind::link, "A clipboard entry", "Safari", "2m", true, 580.0, std::nullopt, "COPY", false, std::vector<d::Control*>{&pin});
    w.host(card.layer()); card.layer().setOrigin({0, 300});
    check(card.controls().size() == 2 && card.target()->container, "[card] + actions; a container with actions");
    check(near(d::textOf(must(card.layer(), "card.title")).lineFrame, {64, 13, 580 - 64 - 80 - 32, 16.9}), "Text width w - 64 - 80 - (strip + 8)");
    check(near(must(card.layer(), "card.actions").frame(), {580 - 80 - 24, 0, 24, 58}) && near(pin.layer().frame().y, 17), "Action strip right-aligned at w - 80");
    check(!card.contains({580 - 90, 20}) && card.contains({100, 20}), "A press on the strip never copies");
    check(items(card, w.root).size() == 2, "Actions stay reachable while transparent");
    check(find(card.layer(), "card.pinned") && near(must(card.layer(), "card.pinned").zPosition(), 1), "Pinned corner");
    w.move(w.centre(card));
    check(model(card.layer(), Channel::translateX) == 3 && model(must(card.layer(), "card.copy"), Channel::opacity) == 1 && model(must(card.layer(), "card.copy"), Channel::translateX) == 0
        && near(*must(card.layer(), "card.bar").tint(), d::fixed::nearBlack) && model(must(card.layer(), "card.actions"), Channel::opacity) == 1, "Hover reveals COPY and the actions");
    check(items(card, w.root).size() == 1, "Opaque actions are routed, not repeated");
    w.move(std::nullopt);
    card.setFocused(true);
    check(model(card.layer(), Channel::translateX) == 3, "Keyboard focus shows the hover state");
    card.flashCopied();
    check(model(must(card.layer(), "card.flash"), Channel::opacity) == 0 && near(must(card.layer(), "card.flash").presented(Channel::opacity, w.env.now), 1), "The flash fades from 1 (.6 easeOut)");
    card.setAge("3m");
    check(d::textOf(must(card.layer(), "card.age")).text() == "3m", "setAge in place");
    auto& hidden = w.b.make<d::ListCard>("hidden", d::ListCard::Kind::text, "Secret", "", "now", false, 580.0, std::nullopt, "COPY", true);
    check(find(hidden.layer(), "card.title.redacted.redaction.bar") && near(must(hidden.layer(), "card.meta.redacted.redaction.bar").bounds().width, 96), "Recording: title and meta bars");

    auto& tile = w.b.make<d::ArchiveTile>("tile", 7, "Operation manual", d::ArchiveTile::Extension::pdf, std::nullopt, std::string("2.4 MB"), std::string("2026.10.22"), 136.0);
    w.host(tile.layer()); tile.layer().setOrigin({300, 300});
    check(near(d::localBounds(tile.layer()), {0, 0, 136, 124}) && d::textOf(must(tile.layer(), "tile.code")).text() == "#07", "Height well + 24; #nn code");
    const auto& doc = must(tile.layer(), "tile.doc");
    check(near(model(doc, Channel::rotation), -6 * 3.14159265358979323846 / 180) && near(doc.position().x, 68) && d::textOf(must(doc, "ext")).text() == "PDF"
        && near(textColor(must(doc, "ext")), d::fixed::red), "Doc glyph rotated -6 deg, centred, PDF in red");
    check(model(must(tile.layer(), "tile.brackets"), Channel::opacity) == 0, "Brackets hidden");
    tile.setSelected(true);
    check(model(must(tile.layer(), "tile.brackets"), Channel::opacity) == 1 && model(must(tile.layer(), "tile.brackets.selected"), Channel::opacity) == 1
        && model(must(tile.layer(), "tile.brackets.rest"), Channel::opacity) == 0 && near(must(tile.layer(), "tile.brackets.selected").property[std::size_t(Channel::opacity)].duration, .16),
        "Selection: brackets in (.1) and the -4 inset cross-faded in over .16");
    check(near(*fill(must(tile.layer(), "tile.label")), d::fixed::counterBackground) && near(textColor(must(tile.layer(), "tile.label.text")), w.env.palette.yellow()), "Selected strip #151515, yellow label");
    w.move(w.centre(tile));
    check(model(must(tile.layer(), "tile.well"), Channel::translateY) == -2 && near(model(doc, Channel::rotation), -2 * 3.14159265358979323846 / 180) && model(doc, Channel::translateY) == -3,
        "Hover: well -2, doc straightens and lifts");
    bool removed = false;
    tile.playRemoval([&] { removed = true; });
    check(!removed && model(tile.layer(), Channel::opacity) == 0 && model(tile.layer(), Channel::scaleX) == .85, "Removal fades and shrinks");
    w.at(w.env.now + .3);
    check(removed, "Then the completion runs");
    auto& classified = w.b.make<d::ArchiveTile>("classified", 3, "Secret", d::ArchiveTile::Extension::txt, std::nullopt, std::nullopt, std::nullopt, 136.0, 100.0, false,
        d::ArchiveTile::Style::classified);
    check(find(classified.layer(), "tile.stamp") && d::textOf(must(classified.layer(), "tile.stamp.text")).text() == "CLASSIFIED" && near(textColor(must(classified.layer(), "tile.label.text")), d::fixed::red),
        "Classified: the red stamp and label");
    w.env.reduceMotion = true; bool now = false; classified.playRemoval([&] { now = true; });
    check(now, "Reduce Motion removes at once");
}

// ---------------------------------------------------------------------------
// HUDModuleHeader (HUDDesignHeader.swift:31-167)
// ---------------------------------------------------------------------------
void header() {
    for (auto language : {endfield::core::Language::english, endfield::core::Language::japanese}) {
        World w; w.env.language = language;
        d::ModuleHeader header(w.env, "clipboard");
        d::ControlSet page; d::LayerArena pageArena("page", &w.env.deadlines); d::Builder pb{w.env, pageArena, page};
        auto& clear = pb.make<d::Button>("header.clear", "Clear", d::Icon::trash, d::Button::Kind::secondary, d::Button::Height::compact);
        auto& pin = pb.make<d::IconButton>("header.pin", "Pin", d::Icon::pin);
        w.host(header.layer());
        header.configure({"CLIPBOARD", "Detail"}, "MOD-03", d::ModuleTitle::make("Clipboard", "剪贴板缓存", language), "07", true, {&clear, &pin});
        check(header.crumbTexts() == std::vector<std::string>{"// HUD", "/", "CLIPBOARD", "/", "Detail"}, "Crumbs: // HUD then / and each crumb");
        const auto& last = must(header.layer(), "header.crumb.4");
        check(near(d::textOf(last).style().color, w.env.palette.token(d::ThemeToken::crumbCurrent)) && near(d::textOf(must(header.layer(), "header.crumb.2")).style().color, w.env.palette.token(d::ThemeToken::steel)),
            "The last crumb is current, others steel");
        const double hud = fakeWidth("// HUD", 10.5, .06);
        check(near(d::textOf(must(header.layer(), "header.crumb.1")).lineFrame, {hud + 6, 1.75, fakeWidth("/", 10.5, .06) + 1, 10.5}), "Crumb boxes w + 1, 6 apart, y 1.75");
        const double code = fakeWidth("MOD-03", 10.5, .06);
        check(near(d::textOf(must(header.layer(), "header.code")).lineFrame, {590 - 34 - 6 - code - 1, 1.75, code + 1, 10.5}), "Code right-aligned 6 px left of the tail");
        check(header.back() && near(header.back()->layer().frame(), {0, 20, 30, 30}) && header.back()->label == endfield::core::localized("Back", "返回", language), "Back button");
        const auto titles = header.titleTexts();
        if (language == endfield::core::Language::english) check(titles.first == "Clipboard" && titles.second.empty(), "English: the English title alone (LIB-22)");
        else check(titles.first == endfield::core::localized("Clipboard", "剪贴板缓存", language) && titles.second == "Clipboard"
            && d::textOf(must(header.layer(), "header.title.text")).language == d::TextRegion::japanese, "Other languages: their title with the English subtitle");
        check(near(must(header.layer(), "header.title").frame().x, 40), "The title moves to x 40 with back");
        // Baselines (:105-112) with the fake metrics: height = size, baseline = .8 size.
        const auto& subtitle = d::textOf(must(header.layer(), "header.title.subtitle"));
        const double baseline = 35 - 13.0 + 26 * .8 * 1;
        check(near(subtitle.lineFrame.y, baseline - 20 - 13 * .8), "The subtitle shares the title's baseline");
        check(near(pin.layer().frame(), {590 - 30, 20, 30, 30}) && near(clear.layer().frame().x, 590 - 30 - 8 - clear.layer().bounds().width), "Actions right-aligned, 8 apart, centred on y 35");
        check(header.counterText() == "07" && near(must(header.layer(), "header.rule.plate").frame(), {590 - fakeWidth("07", 11) - 7, -2, fakeWidth("07", 11) + 7, 12}), "Counter plate w + 7");
        check(near(*fill(must(header.layer(), "header.rule.lead")), w.env.palette.yellow()) && near(must(header.layer(), "header.rule.ladder").frame(), {454, 0, 90, 9}), "Lead in yellow (the fill); ladder at 454");
        check(header.controls().size() == 3, "back + actions");
        header.setCounter("1,284");
        check(header.counterText() == "1,284", "setCounter in place");
        header.playEntrance(w.env.clock());
        const auto& group = must(header.layer(), "header.title"), &rule = must(header.layer(), "header.rule");
        check(group.wipeOrigin == d::WipeOrigin::left && near(group.slots[std::size_t(d::MotionSlot::wipe)][0].duration, .45) && group.slots[std::size_t(d::MotionSlot::fade)][0].values[0] == 0
            && near(rule.slots[std::size_t(d::MotionSlot::grow)][0].start - w.env.now, .05) && near(rule.anchorPoint().x, 0), "Title wipe .45 with fade; rule grows after .05");
        header.configure({}, "SYS-01", d::ModuleTitle::make("System", "系统", language), "", false);
        check(!header.back() && header.crumbTexts() == std::vector<std::string>{"// HUD"} && clear.layer().parent() == nullptr && near(must(header.layer(), "header.title").frame().x, 0),
            "Reconfigured: no back, one crumb, actions removed");
        check(near(d::textOf(must(header.layer(), "header.crumb.0")).style().color, w.env.palette.token(d::ThemeToken::steel)), "A lone // HUD is not current");
    }
}

// ---------------------------------------------------------------------------
// HUDRedaction (HUDDesignRedaction.swift)
// ---------------------------------------------------------------------------
void redaction() {
    World w;
    check(d::redaction::width("", characters()) == 44 && d::redaction::width("Endministrator", characters()) == 14 * 7.2 && d::redaction::width(std::string(60, 'x'), characters()) == 260
        && d::redaction::width("管理员", characters()) == 44, "min(260, max(44, characters x 7.2)) (:14)");
    auto& line = d::redaction::bar(w.b, "line", {20, 10, 300, 18.6}, d::type::rowLabel(), d::redaction::width("Endministrator", characters()));
    check(near(line.frame(), {20, 10 + 9.3 - 8.5, 100.8, 17}) && line.masksToBounds() && line.animatable(), "Replacing a line: round(1.1 size) high, centred (:44-56)");
    check(d::textOf(must(line, "line.redaction.label")).text() == "[ REDACTED ]" && !d::textOf(must(line, "line.redaction.label")).truncates
        && near(d::textOf(must(line, "line.redaction.label")).style().color, w.env.palette.yellow()), "Bars >= 70 carry the yellow label, never truncated");
    auto& centred = d::redaction::bar(w.b, "centred", {6, 6.25, 124, 11.5}, d::type::rowLabel(), 84, d::TextAlignment::center);
    check(near(centred.frame().x, 6 + 62 - 42), "Centred bars");
    auto& shortBar = d::redaction::bar(w.b, "short", d::Size{50, 14});
    check(find(shortBar, "short.redaction.label") == nullptr, "Short bars carry no label");
    World zh; zh.env.language = endfield::core::Language::simplifiedChinese;
    auto& chinese = d::redaction::bar(zh.b, "zh", d::Size{120, 14});
    check(d::textOf(must(chinese, "zh.redaction.label")).text() == "[ 已隐藏 ]", "The label is localized");
    const std::string text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Supercalifragilisticexpialidociousnessandmore words";
    auto& block = d::redaction::block(w.b, "block", text, {400, 120});
    const auto lines = d::redaction::wrapped(text, 46, characters());
    check(block.children().size() == 4 && near(block.children()[1]->frame(), {14, 38, std::min(372.0, std::max(60.0, double(characters().count(lines[1])) * 9.2)), 13}),
        "Block: 13 px bars 22 apart from (14, 16), lines that fit (:61-76)");
    check(find(*block.children()[0], "redaction.label") && !find(*block.children()[1], "redaction.label"), "Only the first bar is labelled");
    const auto wrapped = d::redaction::wrapped("Lorem ipsum dolor sit amet, consectetur adipiscing elit, Supercalifragilisticexpialidociousnessandmorewordsandmorewords", 46, characters());
    check(wrapped.size() == 4 && wrapped[0] == "Lorem ipsum dolor sit amet, consectetur" && wrapped[1] == "adipiscing elit," && characters().count(wrapped[2]) == 46 && wrapped[3] == "ordsandmorewords",
        "Greedy wrap, long words split (:89-106)");
    check(d::redaction::wrapped("a\r\nb", 46, characters()) == std::vector<std::string>{"a", "", "b"}, "Every newline character separates (CharacterSet.newlines)");
    std::vector<d::Layer*> layers{&block};
    d::redaction::playEntrance(layers, w.env.clock());
    check(block.children()[0]->slots[std::size_t(d::MotionSlot::wipe)][0].present() && near(block.children()[0]->slots[std::size_t(d::MotionSlot::wipe)][0].duration, .35), "rdIn wipes each bar .35 s");
}

// ---------------------------------------------------------------------------
// HUDDesignTextField (HUDDesignTextField.swift:9-294) with a fake editor host
// ---------------------------------------------------------------------------
struct FakeHost final : d::TextEditorHost {
    std::optional<d::TextEditRequest> request; std::string text_; bool marked{}; int ends{}; bool accept{true};
    bool begin(const d::TextEditRequest& r) override { if (!accept) return false; request = r; text_ = r.text; return true; }
    void end(d::TextField&) override { ++ends; request.reset(); }
    std::string text(const d::TextField&) const override { return text_; }
    std::optional<std::pair<std::uint32_t, std::uint32_t>> selection(const d::TextField&) const override { return std::pair<std::uint32_t, std::uint32_t>{1, 0}; }
    bool hasMarkedText(const d::TextField&) const override { return marked; }
    void setText(d::TextField&, std::string_view v) override { text_ = v; }
    void reproject(d::TextField&, Rect) override {}
};
void textFields() {
    World w; FakeHost host;
    auto& field = w.b.make<d::TextField>("name", "Name", "Endministrator", "Name", d::Size{260, 34});
    field.setEditorHost(&host);
    w.host(field.layer()); field.layer().setOrigin({100, 100});
    const auto& idle = must(field.layer(), "field.idle");
    check(field.idleText() == "Endministrator" && near(d::textOf(idle).lineFrame, {10, (34 - 15.6) / 2, 240, 15.6}) && field.layer().masksToBounds(), "Idle label centred, line 13 x 1.2 (:131-133)");
    check(items(field, w.root)[0].role == d::AccessibilityRole::edit && items(field, w.root)[0].value == "Endministrator", "An edit item with its value");
    std::vector<std::string> events;
    field.onBegin = [&] { events.push_back("begin"); }; field.onCommit = [&](const std::string& t) { events.push_back("commit:" + t); };
    field.onCancel = [&] { events.push_back("cancel"); }; field.onReturn = [&] { events.push_back("return"); };
    field.onTab = [&](bool shift) { events.push_back(shift ? "tab:shift" : "tab"); }; field.onChange = [&](const std::string& t) { events.push_back("change:" + t); };
    int others = 0; field.willBeginEditing = [&](d::TextField&) { ++others; };
    check(field.pointerDown({20, 10}), "An idle field takes the press");
    field.pointerUp({20, 10});
    check(field.editing() && others == 1 && events == std::vector<std::string>{"begin"} && host.request && host.request->click && near(host.request->click->x, 120)
        && near(host.request->frame, {110, 100, 240, 34}), "A click begins editing at the click (box px)");
    check(near(host.request->caretColor, w.env.palette.accentInk()) && near(host.request->selectionColor, d::withAlpha(w.env.palette.yellow(), .35)), "Caret accentInk, selection yellow .35");
    check(field.frame().state() == d::FieldFrame::State::focused && idle.hidden() && items(field, w.root).empty() && !field.pointerDown({1, 1}), "Focused; the editor is the element");
    host.text_ = "Changed"; field.editorChanged("Changed");
    d::KeyInput key; key.virtualKey = d::key::enter;
    check(field.handleEditingKey(key) && !field.editing() && field.text() == "Changed" && events.back() == "return" && events[events.size() - 2] == "commit:Changed", "Return commits then onReturn");
    field.beginEditing(); host.text_ = "Discarded";
    key.virtualKey = d::key::escape;
    check(field.handleEditingKey(key) && field.text() == "Changed" && events.back() == "cancel", "Esc restores the original");
    field.beginEditing(); key.virtualKey = d::key::tab; key.shift = true;
    check(field.handleEditingKey(key) && events.back() == "tab:shift", "Tab commits then onTab(shift)");
    field.beginEditing(); host.marked = true; key.virtualKey = d::key::escape;
    check(!field.handleEditingKey(key) && field.editing(), "Keys pass while IME text is marked");
    host.marked = false; field.editorFocusLost();
    check(!field.editing() && events.back() == "commit:Changed", "Focus loss commits");
    field.beginEditing(std::pair<std::uint32_t, std::uint32_t>{2, 50});
    check(host.request->restoredSelection == std::pair<std::uint32_t, std::uint32_t>{2, 5} && !host.request->click, "A restored selection is clamped");
    field.endEditing(true);
    host.accept = false; field.beginEditing();
    check(!field.editing(), "Without an editor the field stays idle"); host.accept = true;
    field.setInvalid(true, true);
    check(field.frame().state() == d::FieldFrame::State::invalid && field.layer().slots[std::size_t(d::MotionSlot::effect)][0].additive
        && near(field.layer().slots[std::size_t(d::MotionSlot::effect)][0].duration, .32), "Invalid + shake (additive, .32 s)");
    field.beginEditing(); field.editorChanged("x");
    check(field.frame().state() == d::FieldFrame::State::focused, "An edit clears the invalid state");
    field.endEditing(false);

    d::TextField::Options limited; limited.maximumLength = 5;
    auto& code = w.b.make<d::TextField>("code", "Code", "line\nbreak", "", d::Size{200, 34}, limited);
    check(code.text() == "line\nbreak", "The initial text is taken as given (init, :66)");
    code.setText("line\nbreak");
    check(code.text() == "line ", "setText joins newlines and truncates to maximumLength (:94-98, :244-248)");
    code.setEditorHost(&host); code.beginEditing(); host.text_ = "abcdefgh"; code.editorChanged("abcdefgh");
    check(host.text_ == "abcde" && code.text() == "abcde", "Edits are bounded");
    code.endEditing(true);
    // The limits go through the injected rules (production: ICU), not a local count.
    World injected;
    injected.env.characters.count = [](std::string_view t) { return t.size() / 2; };
    injected.env.characters.prefix = [](std::string_view t, std::size_t n) { return std::string(t.substr(0, n * 2)); };
    auto& routed = injected.b.make<d::TextField>("routed", "Routed", "", "", d::Size{200, 34}, limited);
    routed.setText("abcdefghijklmnop");
    check(routed.text() == "abcdefghij", "maximumLength counts with Environment::characters");
    d::TextField::Options secure; secure.secure = true;
    auto& password = w.b.make<d::TextField>("pw", "Password", "hunter2", "", d::Size{260, 34}, secure);
    check(password.idleText() == "•••••••" && items(password, w.root).empty() == false && items(password, w.root)[0].value.empty(), "Secure: bullets, no spoken value");
    auto& empty = w.b.make<d::TextField>("search", "Search", "", "Search", d::Size{260, 34});
    check(empty.idleText() == "Search" && near(d::textOf(must(empty.layer(), "field.idle")).style().color, w.env.palette.token(d::ThemeToken::muted)), "Placeholder in muted");
    d::TextField::Options multi; multi.multiline = true; multi.light = true;
    auto& note = w.b.make<d::TextField>("note", "Note", "First\nSecond", "", d::Size{220, 90}, multi);
    note.setEditorHost(&host);
    const auto& noteIdle = d::textOf(must(note.layer(), "field.idle"));
    check(near(noteIdle.lineFrame, {10, 6, 200, 78}) && noteIdle.maximumLines == int(78 / (13 * 1.5)) && near(noteIdle.style().color, d::fixed::ink) && note.text() == "First\nSecond",
        "Multi-line: inset 6 vertically, lines h / (size x 1.5), light ink");
    note.beginEditing(); key = {}; key.virtualKey = d::key::enter; key.shift = true;
    check(!note.handleEditingKey(key) && note.editing(), "Shift-Return inserts a line");
    key.shift = false;
    check(note.handleEditingKey(key) && !note.editing(), "Return commits multi-line with returnCommits");
    note.beginEditing(); key.virtualKey = d::key::tab;
    check(!note.handleEditingKey(key), "Tab is text in multi-line fields");
    check(host.request->light && near(host.request->caretColor, d::fixed::ink), "Light fields use the ink caret");
}

// ---------------------------------------------------------------------------
// Emission: the components' surface split works with the shared emitter
// (animatable feedback layers are their own surfaces, list units their own parts).
// ---------------------------------------------------------------------------
void emission() {
    World w;
    auto& page = w.arena.make("page");
    w.root.add(page);
    auto& row = w.b.make<d::RowPlate>("row", "Launch at login", std::optional<std::string>("Starts with macOS"));
    auto& toggle = w.b.make<d::Toggle>("row.toggle", "Toggle", true);
    row.setCluster({&toggle.layer()});
    auto& card = w.b.make<d::ListCard>("card", d::ListCard::Kind::text, "Entry", "Meta", "2m", true, 580.0);
    card.layer().setOrigin({0, 60});
    d::Tabs tabs(w.b, "tabs", {{"All", std::nullopt}, {"Text", d::Icon::text}}, 0, 590);
    for (d::Layer* l : {&row.layer(), &card.layer(), &tabs.layer()}) page.add(*l);
    d::Menu menu(w.env, "menu", {{"a", "English"}, {"b", "Deutsch", {}, true}}, 178);
    page.add(menu.layer());
    const auto art = d::emit(w.root, {&w.env.text, "design."});
    std::size_t surfaces = 0;
    for (const auto& part : art.parts) surfaces += part.surfaces.size();
    const auto hasPart = [&](std::string_view suffix) { return std::any_of(art.parts.begin(), art.parts.end(), [&](const d::Part& p) { return std::string_view(p.id).ends_with(suffix); }); };
    check(hasPart(row.layer().id()) && hasPart(card.layer().id()) && hasPart(menu.layer().id()), "Rows, cards and menus are their own parts");
    const auto surface = [&](std::string_view id) {
        return std::any_of(art.bindings.begin(), art.bindings.end(), [&](const d::SurfaceBinding& b) { return b.placement->id() == id; });
    };
    check(surface(must(row.layer(), "row.notch").id()) && surface(must(row.layer(), "row.brighten").id()) && surface(must(toggle.layer(), "toggle.knob.fill").id()),
        "Hover and state feedback layers are surfaces");
    std::vector<d::SurfacePose> poses(art.bindings.size());
    w.move(w.centre(row));
    d::samplePoses(art, w.env.now + .05, poses);
    check(d::requiresFrames(art, w.env.now + .05) && surfaces == art.bindings.size(), "Hover motion is sampled, not re-emitted");
}

// ---------------------------------------------------------------------------
// The Mac oracle (tools/hud_design_reference.*): node by node, recorded widths.
//
// Both trees are flattened into what they DRAW, in host (component root)
// coordinates: fills (backgroundColor), borders, shapes (path bounds + fill),
// gradients, cached artwork (the Mac's HUDArtwork rasters: plates, tags,
// hatches) and text lines (line box, runs, colours, alignment, line budget),
// each with its effective opacity (hidden = 0). Invisible primitives are
// dropped; every remaining Mac primitive needs a Windows twin and vice versa.
// The tree SHAPES differ on purpose (tinted white surfaces, a child ring
// above a fill where CA draws a layer border on top, cross-faded copies), so
// the comparison is on the pixels' description, not on the layer structure;
// controls (hover, press, enabled, model rects, accessibility) compare 1:1.
// ---------------------------------------------------------------------------
struct OracleRun {
    const Json& data;
    RecordedMeasurer& measurer;
    std::vector<std::string> failures;
    std::size_t compared{}, skipped{};
    // The oracle records the full state set only for the default theme: a
    // missing default-theme row means a scenario name drifted from the oracle.
    void skip(const std::string& label, bool light, const std::string& accent) {
        ++skipped;
        // (The oracle stops some interactions after hover: only first states must exist.)
        const bool first = label.find("/rest") != std::string::npos || label.find("/hovered") != std::string::npos || label.find("/open") != std::string::npos
            || label.find("/idle") != std::string::npos || label.find("/configured") != std::string::npos;
        if (!light && accent == "FAD41F" && first) failures.push_back(label + ": not in the oracle");
    }
    const Json* row(std::string_view name, std::string_view state, bool light, std::string_view accent, std::string_view language) const {
        for (const auto& r : data["components"].array())
            if (r["component"].string() == name && r["state"].string() == state && r["light"].boolean() == light && r["accent"].string() == accent
                && r["language"].string() == language) return &r;
        return nullptr;
    }
    void compare(std::string label, const Json& expected, const d::Layer& layer, const d::Layer& host, std::span<d::Control* const> controls) {
        ++compared;
        endfield::tests::design_oracle::compare(label, expected, layer, host, controls, failures);
    }
};
struct Theme { std::string_view accent; bool light; };
// HUDColor.apply + a fresh world per component, like the oracle's `themed` and `host`.
struct OracleWorld : World {
    explicit OracleWorld(RecordedMeasurer& m, Theme t, endfield::core::Language language = endfield::core::Language::english) : World(t.light, t.accent) {
        env.text.measurer = &m; env.language = language; env.text.uiRegion = d::regionFor(language, d::TextRegion::simplifiedChinese);
    }
};
void oracle(const Json& data) {
    RecordedMeasurer recorded;
    recordText(recorded, data["text"]);
    for (const auto& row : data["components"].array()) if (recordMicroTag(recorded, row["layer"])) break;
    OracleRun run{data, recorded, {}};
    const std::vector<Theme> themes{{"FAD41F", false}, {"FAD41F", true}, {"22A6F2", false}, {"E5383B", false}, {"8CE03C", false}, {"22A6F2", true}};
    const auto hex = [](Theme t) { return std::string(t.accent); };
    // rest -> hovered -> pressed -> left -> disabled through the highlight pass (the oracle's `interact`).
    using State = std::function<void(std::string_view)>;
    using Tree = std::function<void(std::string_view state, const char* key, const d::Layer&)>; // an extra tree of a state row
    using Extra = std::function<void(OracleWorld&, d::Control&, const State&, const Tree&)>;
    const auto interact = [&](std::string_view name, Theme theme, bool full, auto make, Extra extra = {}) {
        OracleWorld w(recorded, theme);
        d::Control& control = make(w);
        w.host(control.layer());
        std::vector<d::Control*> one{&control};
        const State state = [&](std::string_view s) {
            const Json* row = run.row(name, s, theme.light, hex(theme), "en");
            if (!row) { run.skip(std::string(name) + "/" + std::string(s), theme.light, hex(theme)); return; }
            run.compare(std::string(name) + "/" + std::string(s) + (theme.light ? "/light/" : "/") + hex(theme), *row, control.layer(), w.root, one);
        };
        state("rest");
        const auto p = w.centre(control);
        w.move(p); state("hovered");
        if (!full) return;
        w.move(p, true); state("pressed");
        w.move(std::nullopt); state("left");
        const Tree tree = [&](std::string_view s, const char* key, const d::Layer& layer) {
            const Json* row = run.row(name, s, theme.light, hex(theme), "en");
            if (!row || !(*row)[key].isObject()) return;
            run.compare(std::string(name) + "/" + std::string(s) + "/" + key + (theme.light ? "/light/" : "/") + hex(theme), Json::Object{{"layer", (*row)[key]}}, layer, w.root, {});
        };
        if (extra) extra(w, control, state, tree);
        control.setEnabled(false); state("disabled");
    };
    // The oracle's extraState steps (hud_design_reference.swift `interact` callers).
    const Extra toggleExtra = [](OracleWorld&, d::Control& c, const State& state, const Tree&) {
        c.activate(); state("activated");
        static_cast<d::Toggle&>(c).setOn(false, false); state("setOffUnanimated");
    };
    const Extra swatchExtra = [](OracleWorld&, d::Control& c, const State& state, const Tree&) { static_cast<d::Swatch&>(c).setSelected(true); state("selected"); };
    const Extra dropdownExtra = [](OracleWorld&, d::Control& c, const State& state, const Tree&) { static_cast<d::Dropdown&>(c).setOpen(true); state("open"); };
    const Extra taskExtra = [](OracleWorld&, d::Control& c, const State& state, const Tree&) { static_cast<d::TaskCard&>(c).toggle(); state("toggledDone"); };
    const Extra sliderExtra = [](OracleWorld& w, d::Control& c, const State& state, const Tree& tree) {
        auto& slider = static_cast<d::Slider&>(c);
        auto& top = w.arena.make("design.top");
        top.setFrame(d::metric::box); top.setZPosition(d::order::menu); w.root.add(top);
        slider.setBubbleHost(&top);
        const Rect r = slider.rect(w.root);
        w.move(Point{r.x + 30, r.y + r.height / 2}, true);
        slider.pointerDown(*d::convert(Point{r.x + 30, r.y + r.height / 2}, w.root, slider.layer()));
        slider.pointerDragged(*d::convert(Point{r.x + 150, r.y + r.height / 2}, w.root, slider.layer()));
        state("dragging");
        // The bubble in the top layer (the oracle's "bubble" tree of `top`).
        tree("dragging", "bubble", top);
        slider.pointerUp(*d::convert(Point{r.x + 150, r.y + r.height / 2}, w.root, slider.layer()));
        w.move(std::nullopt);
        slider.nudge(1);
        state("released");
    };
    using K = d::Button::Kind; using H = d::Button::Height; using S = d::IconButton::Style;
    for (const auto theme : themes) {
        const bool full = theme.accent == "FAD41F" && !theme.light;
        interact("row.standard", theme, full, [](World& w) -> d::Control& { return w.b.make<d::RowPlate>("row", "Launch at login", std::optional<std::string>("Starts with macOS")); });
        if (full) {
            interact("row.compact", theme, full, [](World& w) -> d::Control& { return w.b.make<d::RowPlate>("row", "Compact row", std::nullopt, d::RowPlate::Variant::compact); });
            interact("row.nav", theme, full, [](World& w) -> d::Control& { return w.b.make<d::RowPlate>("row", "Navigation row", std::nullopt, d::RowPlate::Variant::nav); });
            interact("row.navExternal", theme, full, [](World& w) -> d::Control& { return w.b.make<d::RowPlate>("row", "Open folder", std::nullopt, d::RowPlate::Variant::nav, "↗", 400.0); });
            interact("row.custom56", theme, full, [](World& w) -> d::Control& { return w.b.make<d::RowPlate>("row", "Custom height", std::optional<std::string>("56 px"), d::RowPlate::Variant::standard, "›", 590.0, 56.0); });
        }
        interact("toggle.off", theme, full, [](World& w) -> d::Control& { return w.b.make<d::Toggle>("toggle", "Ambient", false); }, toggleExtra);
        interact("toggle.on", theme, full, [](World& w) -> d::Control& { return w.b.make<d::Toggle>("toggle", "Ambient", true); });
        for (const auto [kind, kindName] : {std::pair{K::primary, "primary"}, std::pair{K::secondary, "secondary"}, std::pair{K::light, "light"}}) {
            for (const auto [size, sizeName] : {std::pair{H::compact, "small"}, std::pair{H::regular, "regular"}, std::pair{H::large, "large"}}) {
                if (!full && size != H::regular) continue;
                interact(std::string("button.") + kindName + "." + sizeName, theme, full, [kind, size](World& w) -> d::Control& { return w.b.make<d::Button>("button", "Export", d::Icon::export_, kind, size); });
            }
            if (full) interact(std::string("button.") + kindName + ".untitledIcon", theme, full, [kind](World& w) -> d::Control& { return w.b.make<d::Button>("button", "", d::Icon::plus, kind, H::compact); });
        }
        for (const auto [style, styleName] : {std::pair{S::dark, "dark"}, std::pair{S::on, "on"}, std::pair{S::light, "light"}, std::pair{S::back, "back"}}) {
            interact(std::string("iconButton.") + styleName, theme, full, [style](World& w) -> d::Control& { return w.b.make<d::IconButton>("icon", "Pin", d::Icon::pin, style); });
            if (!full) continue;
            interact(std::string("iconButton.") + styleName + ".round", theme, full, [style](World& w) -> d::Control& { return w.b.make<d::IconButton>("icon", "Play", d::Icon::play, style, true); });
            interact(std::string("iconButton.") + styleName + ".big", theme, full, [style](World& w) -> d::Control& { return w.b.make<d::IconButton>("icon", "Play", d::Icon::play, style, false, true); });
            interact(std::string("iconButton.") + styleName + ".side24", theme, full, [style](World& w) -> d::Control& { return w.b.make<d::IconButton>("icon", "X", d::Icon::x, style, false, false, 24.0, 10.0); });
        }
        for (const auto [kind, kindName] : {std::pair{d::Pill::Kind::cancel, "cancel"}, std::pair{d::Pill::Kind::confirm, "confirm"}, std::pair{d::Pill::Kind::danger, "danger"}})
            interact(std::string("pill.") + kindName, theme, full, [kind](World& w) -> d::Control& {
                return w.b.make<d::Pill>("pill", kind == d::Pill::Kind::cancel ? "Cancel" : "Delete everything now", kind == d::Pill::Kind::cancel ? d::Icon::x : d::Icon::trash, kind); });
        interact("swatch.colour", theme, full, [](World& w) -> d::Control& { return w.b.make<d::Swatch>("swatch", "Cyan", d::fixed::cyan); }, swatchExtra);
        if (full) interact("grip", theme, full, [](World& w) -> d::Control& { return w.b.make<d::DragGrip>("grip", "", d::Size{30, 40}, Point{12, 15}); });
        for (const auto [dark, variant] : {std::pair{false, "plate"}, std::pair{true, "dark"}})
            if (full || dark || theme.light)
                interact(std::string("slider.") + variant, theme, full, [dark](World& w) -> d::Control& {
                    return w.b.make<d::Slider>("slider", "Opacity", 62.0, d::Slider::Range{0, 100, 1}, d::ValueFormat::percent, 300.0, dark); }, sliderExtra);
        interact("dropdown.regular", theme, full, [](World& w) -> d::Control& { return w.b.make<d::Dropdown>("dropdown", "Language", "简体中文", d::TextRegion::simplifiedChinese); }, dropdownExtra);
        if (full) {
            interact("dropdown.small", theme, full, [](World& w) -> d::Control& { return w.b.make<d::Dropdown>("dropdown", "Sort", "Newest first", std::nullopt, d::Dropdown::Height::compact); });
            interact("dropdown.width", theme, full, [](World& w) -> d::Control& { return w.b.make<d::Dropdown>("dropdown", "Screen", "A very long display name that truncates", std::nullopt, d::Dropdown::Height::regular, 160.0); });
        }
        interact("taskCard", theme, full, [](World& w) -> d::Control& { return w.b.make<d::TaskCard>("task", "Calibrate the relay", false, true, false, 280.0); }, taskExtra);
        if (full) interact("taskCard.redacted", theme, full, [](World& w) -> d::Control& { return w.b.make<d::TaskCard>("task", "Secret", true, false, true, 280.0); });
        // Static components.
        std::vector<d::Control*> stillControls;
        const auto still = [&](std::string_view name, std::string_view state, auto make) {
            OracleWorld w(recorded, theme);
            stillControls.clear();
            d::Layer& layer = make(w);
            w.host(layer);
            if (const Json* row = run.row(name, state, theme.light, hex(theme), "en")) run.compare(std::string(name) + "/" + std::string(state) + (theme.light ? "/light/" : "/") + hex(theme), *row, layer, w.root, stillControls);
            else run.skip(std::string(name) + "/" + std::string(state), theme.light, hex(theme));
        };
        for (const auto [variantName, variant] : {std::pair{"yellow", d::Chip::Variant::yellow}, std::pair{"dark", d::Chip::Variant::dark}, std::pair{"light", d::Chip::Variant::light},
                 std::pair{"outline", d::Chip::Variant::outline}, std::pair{"cyan", d::Chip::Variant::cyan}, std::pair{"custom", d::Chip::Variant::custom}}) {
            std::vector<std::unique_ptr<d::Chip>> keep;
            still(std::string("chip.") + variantName, "rest", [&, variant, variantName](World& w) -> d::Layer& {
                keep.push_back(std::make_unique<d::Chip>(w.b, "chip", "Pinned", variant, std::string_view(variantName) == "dark" ? std::optional(d::Icon::pin) : std::nullopt,
                    std::pair{d::fixed::red, d::white(1)}));
                return keep.back()->layer(); });
        }
        still("badge.new", "rest", [](World& w) -> d::Layer& { return d::badge::newBadge(w.b, "new"); });
        still("badge.stamp", "rest", [](World& w) -> d::Layer& { return d::badge::stamp(w.b, "stamp", "Classified"); });
        still("badge.diamond", "rest", [](World& w) -> d::Layer& { return d::badge::diamond(w.b, "dot"); });
        std::vector<std::unique_ptr<d::FieldFrame>> fields;
        for (const auto [light, plain, name] : {std::tuple{false, false, "field"}, std::tuple{true, false, "field.light"}, std::tuple{false, true, "field.plain"}})
            for (const auto [state, stateName] : {std::pair{d::FieldFrame::State::normal, "normal"}, std::pair{d::FieldFrame::State::focused, "focused"}, std::pair{d::FieldFrame::State::invalid, "invalid"}})
                still(name, stateName, [&, light, plain, state](World& w) -> d::Layer& {
                    fields.push_back(std::make_unique<d::FieldFrame>(w.b, "field", d::Size{240, 34}, light, plain)); fields.back()->setState(state); return fields.back()->layer(); });
        std::vector<std::unique_ptr<d::BigNumeral>> numerals;
        still("bigNumeral", "rest", [&](World& w) -> d::Layer& { numerals.push_back(std::make_unique<d::BigNumeral>(w.b, "numeral")); numerals.back()->set("72", "%"); return numerals.back()->layer(); });
        std::vector<std::unique_ptr<d::StatPlate>> plates;
        still("statPlate", "highlighted", [&](World& w) -> d::Layer& {
            plates.push_back(std::make_unique<d::StatPlate>(w.b, "stat", 180)); plates.back()->set("Cycle count", "412", std::string("cycles"), true); return plates.back()->layer(); });
        still("statPlate", "toned", [&](World& w) -> d::Layer& {
            plates.push_back(std::make_unique<d::StatPlate>(w.b, "stat", 180)); plates.back()->set("Cycle count", "412", std::string("cycles"), true);
            plates.back()->set("Temperature", "31.5", std::string("°C"), false, d::fixed::orange); return plates.back()->layer(); });
        std::vector<std::unique_ptr<d::Cells>> cells;
        for (const bool capacity : {true, false}) {
            const std::string name = std::string("cells.") + (capacity ? "capacity" : "level");
            still(name, "six", [&, capacity](World& w) -> d::Layer& { cells.push_back(std::make_unique<d::Cells>(w.b, "cells", 10, 300, capacity)); cells.back()->set(6, std::nullopt, false, false); return cells.back()->layer(); });
        }
        std::vector<std::unique_ptr<d::Tabs>> bars;
        still("tabs", "rest", [&](World& w) -> d::Layer& {
            bars.push_back(std::make_unique<d::Tabs>(w.b, "tabs", std::vector<d::Tabs::Title>{{"All", std::nullopt}, {"Text", d::Icon::text}, {"Links", d::Icon::link}, {"Images", d::Icon::image}}, 0, 590.0));
            stillControls.assign(bars.back()->controls().begin(), bars.back()->controls().end());
            return bars.back()->layer(); });
        // Scripted sequences (the oracle's do-blocks), one world each.
        const auto snap = [&](OracleWorld& w, std::string_view name, std::string_view state, const d::Layer& layer, std::vector<d::Control*> controls) {
            if (const Json* row = run.row(name, state, theme.light, hex(theme), "en"))
                run.compare(std::string(name) + "/" + std::string(state) + (theme.light ? "/light/" : "/") + hex(theme), *row, layer, w.root, controls);
            else run.skip(std::string(name) + "/" + std::string(state), theme.light, hex(theme));
        };
        const auto withSegments = [](d::SegmentedControl& c) { std::vector<d::Control*> out{&c}; out.insert(out.end(), c.segments().begin(), c.segments().end()); return out; };
        {
            OracleWorld w(recorded, theme);
            auto& seg = w.b.make<d::SegmentedControl>("seg", "Mode", std::vector<std::string>{"Dark", "Light", "System"}, 1, 300.0);
            w.host(seg.layer());
            snap(w, "segmented", "rest", seg.layer(), withSegments(seg));
            w.move(w.centre(*seg.segments()[0])); snap(w, "segmented", "hovered0", seg.layer(), withSegments(seg));
            seg.select(2, true); snap(w, "segmented", "selected2", seg.layer(), withSegments(seg));
            auto& yellow = w.b.make<d::SegmentedControl>("seg.yellow", "Mode", std::vector<std::string>{"25", "50", "Custom"}, 0, 240.0, true);
            (void)yellow;
        }
        {
            OracleWorld w(recorded, theme);
            auto& yellow = w.b.make<d::SegmentedControl>("seg", "Mode", std::vector<std::string>{"25", "50", "Custom"}, 0, 240.0, true);
            w.host(yellow.layer());
            snap(w, "segmented.yellow", "rest", yellow.layer(), withSegments(yellow));
        }
        if (full) {
            OracleWorld w(recorded, theme);
            auto& row = w.b.make<d::RowPlate>("row", "Slider row", std::nullopt, d::RowPlate::Variant::slider);
            auto& toggle = w.b.make<d::Toggle>("row.toggle", "Toggle", true);
            auto& button = w.b.make<d::Button>("row.button", "Open", std::nullopt, K::secondary, H::compact);
            row.setCluster({&button.layer(), &toggle.layer()}); row.setTitleInset(56);
            w.host(row.layer());
            const std::vector<d::Control*> three{&row, &toggle, &button};
            const Json* rest = run.row("row.cluster", "rest", false, "FAD41F", "en");
            check(rest && near(rectOf((*rest)["controlArea"]), row.controlArea(), 1e-6) && near(rectOf((*rest)["titleFrame"]), row.titleFrame(), 1e-3), "row.cluster: controlArea and titleFrame");
            snap(w, "row.cluster", "rest", row.layer(), three);
            w.move(w.centre(toggle)); snap(w, "row.cluster", "toggleHovered", row.layer(), three);
            w.move(w.centre(toggle), true); snap(w, "row.cluster", "togglePressed", row.layer(), three);
        }
        if (full) {
            OracleWorld w(recorded, theme);
            auto& toggled = w.b.make<d::IconButton>("icon", "Mute", d::Icon::vol);
            toggled.setOn(true); toggled.setIcon(d::Icon::mute);
            w.host(toggled.layer());
            snap(w, "iconButton.dark.isOn", "rest", toggled.layer(), {&toggled});
            for (const auto [kind, kindName] : {std::pair{K::primary, "primary"}, std::pair{K::secondary, "secondary"}, std::pair{K::light, "light"}}) {
                OracleWorld f(recorded, theme);
                auto& flex = f.b.make<d::Button>("button", "Flex", d::Icon::check, kind);
                flex.place({12, 30}, 220.0);
                f.host(flex.layer());
                snap(f, std::string("button.") + kindName + ".flex", "rest", flex.layer(), {&flex});
            }
            OracleWorld z(recorded, theme);
            auto& zero = z.b.make<d::Slider>("slider", "Balance", -20.0, d::Slider::Range{-100, 100, 5, 0.0}, d::ValueFormat::balance, 260.0, false, true, false);
            z.host(zero.layer()); snap(z, "slider.zero", "rest", zero.layer(), {&zero});
            OracleWorld n(recorded, theme);
            auto& bare = n.b.make<d::Slider>("slider", "Seek", .25, d::Slider::Range{0, 1, 0}, d::ValueFormat::decimal, 200.0, false, false);
            n.host(bare.layer()); snap(n, "slider.noReadout", "rest", bare.layer(), {&bare});
            interact("swatch.custom", theme, full, [](World& ww) -> d::Control& { return ww.b.make<d::Swatch>("swatch", "Custom", std::nullopt, 28.0); });
        }
        // Menus and pop-overs, placed as a canvas places them.
        using Item = d::Menu::Item;
        std::vector<Item> shortItems{{"a", "English"}, {"b", "简体中文", d::TextRegion::simplifiedChinese, true}, {"c", "繁體中文", d::TextRegion::traditionalChinese},
            {"d", "日本語", d::TextRegion::japanese}, {"e", "한국어", d::TextRegion::korean, false, false, true}, {"f", "Delete", {}, false, true}, {"g", "Disabled", {}, false, false, false, false}};
        std::vector<Item> longItems, gridItems;
        for (int i = 0; i < 12; ++i) longItems.push_back({"i" + std::to_string(i), "Display " + std::to_string(i), {}, i == 9});
        for (int i = 0; i < 9; ++i) gridItems.push_back({"t" + std::to_string(i), "Icon " + std::to_string(i), {}, i == 4, false, false, true, d::Menu::IconTile{{d::ImageSource::Kind::memory, "icon"}}});
        gridItems.push_back({"custom", "Custom…"});
        for (const auto& [name, items, minimum] : {std::tuple{std::string("menu.short"), shortItems, 178.0}, std::tuple{std::string("menu.long"), longItems, 178.0}, std::tuple{std::string("menu.grid"), gridItems, 120.0}}) {
            if (!full && name != "menu.short") continue;
            OracleWorld w(recorded, theme);
            d::Menu menu(w.env, name, items, minimum);
            menu.place({380, 300, 178, 30}, {590, 510});
            w.root.add(menu.layer());
            std::vector<std::string> picks;
            menu.onPick = [&](const Item& i) { picks.push_back(i.id); }; menu.onClose = [&] { picks.push_back("close"); };
            menu.playEntrance(w.env.clock());
            const Json* open = run.row(name, "open", theme.light, hex(theme), "en");
            if (open) check((*open)["isLong"].boolean() == menu.isLong() && near((*open)["scroll"].number(), menu.scrollOffset())
                && ((*open)["keyboardIndex"].isNull() ? !menu.keyboardIndex() : menu.keyboardIndex() == std::size_t((*open)["keyboardIndex"].number())), name + ": open state");
            snap(w, name, "open", menu.layer(), menu.controls());
            if (!full) continue;
            const auto controls = menu.controls();
            w.move(w.centre(*controls[std::min<std::size_t>(2, controls.size() - 1)])); snap(w, name, "hovered2", menu.layer(), menu.controls());
            const Json* end = run.row(name, "scrolledToEnd", false, "FAD41F", "en");
            std::size_t k = 0;
            for (const std::uint32_t code : {d::key::down, d::key::down, d::key::up, d::key::right, d::key::left, std::uint32_t('Q'), d::key::enter, d::key::escape}) {
                d::KeyInput key; key.virtualKey = code;
                const bool consumed = menu.handleKey(key);
                if (!end) continue;
                const auto& e = (*end)["keys"].array()[k++];
                std::vector<std::string> expected; for (const auto& p : e["picks"].array()) expected.push_back(p.string());
                check(e["consumed"].boolean() == consumed && near(e["scroll"].number(), menu.scrollOffset()) && expected == picks
                    && (e["keyboardIndex"].isNull() ? !menu.keyboardIndex() : menu.keyboardIndex() == std::size_t(e["keyboardIndex"].number())), name + ": key " + e["key"].string());
            }
            menu.scroll(1000); snap(w, name, "scrolledToEnd", menu.layer(), menu.controls());
            menu.setScroll(37); snap(w, name, "setScroll37", menu.layer(), menu.controls());
        }
        for (const auto [anchor, name] : {std::pair{Rect{400, 300, 30, 30}, "popover.above"}, std::pair{Rect{40, 90, 30, 30}, "popover.below"}}) {
            if (!full && std::string_view(name) != "popover.above") continue;
            OracleWorld w(recorded, theme);
            d::Popover popover(w.env, "Pen", std::string("画笔"), 270, 120);
            popover.place(anchor, {590, 510});
            w.host(popover.layer()); popover.playEntrance(w.env.clock());
            snap(w, name, "open", popover.layer(), {popover.closeButton()});
        }
        if (full) {
            OracleWorld w(recorded, theme);
            d::Popover headerless(w.env, "", std::nullopt, 270, 80, false);
            headerless.place({300, 200, 30, 30}, {590, 510});
            w.host(headerless.layer());
            snap(w, "popover.headerless", "open", headerless.layer(), {});
        }
        // Tabs.
        for (const auto [compact, keycaps, name] : {std::tuple{false, true, "tabs"}, std::tuple{true, true, "tabs.compact"}, std::tuple{false, false, "tabs.noKeycaps"}}) {
            if (!full && std::string_view(name) != "tabs") continue;
            OracleWorld w(recorded, theme);
            d::Tabs tabs(w.b, "tabs", {{"All", std::nullopt}, {"Text", d::Icon::text}, {"Links", d::Icon::link}, {"Images", d::Icon::image}}, 0, 590, keycaps, compact);
            std::vector<std::pair<int, double>> selections;
            tabs.onSelect = [&](int i, double direction) { selections.emplace_back(i, direction); };
            w.host(tabs.layer());
            const std::vector<d::Control*> controls(tabs.controls().begin(), tabs.controls().end());
            snap(w, name, "rest", tabs.layer(), controls);
            w.move(w.centre(*controls[2])); snap(w, name, "hovered2", tabs.layer(), controls);
            w.move(std::nullopt); controls[2]->activate(); snap(w, name, "selected2", tabs.layer(), controls);
            tabs.cycle(1); tabs.cycle(1); tabs.cycle(-1); tabs.setNews(1, true);
            snap(w, name, "cycled", tabs.layer(), controls);
            if (const Json* row = run.row(name, "cycled", theme.light, hex(theme), "en")) {
                const auto& expected = (*row)["selections"].array();
                bool same = expected.size() == selections.size() && int((*row)["selectedIndex"].number()) == tabs.selectedIndex();
                for (std::size_t i = 0; same && i < expected.size(); ++i) same = int(expected[i].array()[0].number()) == selections[i].first && expected[i].array()[1].number() == selections[i].second;
                check(same, std::string(name) + ": selections and direction");
            }
        }
        // Keycaps.
        {
            OracleWorld w(recorded, theme);
            auto& keys = w.b.make<d::KeycapGroup>("keys", "Summon", std::vector<std::string>{"⌥", "Space"}, true);
            w.host(keys.layer());
            snap(w, "keycaps", "rest", keys.layer(), {&keys});
            keys.setRecording(true, endfield::core::localized("Press a shortcut", "按下快捷键", w.env.language)); snap(w, "keycaps", "recording", keys.layer(), {&keys});
            w.env.reduceMotion = true; keys.setRecording(false, ""); keys.setRecording(true, "Reduced");
            snap(w, "keycaps", "recordingReduced", keys.layer(), {&keys}); w.env.reduceMotion = false;
            if (full) {
                keys.setRecording(false, ""); keys.setKeys({"⌃", "⇧", "K"}); snap(w, "keycaps", "setKeys", keys.layer(), {&keys});
                auto& display = w.b.make<d::KeycapGroup>("display.keys", "Shortcut", std::vector<std::string>{"Esc"}, false);
                OracleWorld v(recorded, theme);
                auto& shown = v.b.make<d::KeycapGroup>("keys", "Shortcut", std::vector<std::string>{"Esc"}, false);
                v.host(shown.layer()); snap(v, "keycaps.display", "rest", shown.layer(), {&shown}); (void)display;
            }
        }
        // Text fields (idle faces).
        {
            OracleWorld w(recorded, theme);
            auto& field = w.b.make<d::TextField>("name", "Name", "Endministrator", "Name", d::Size{260, 34});
            w.host(field.layer()); snap(w, "textField", "idle", field.layer(), {&field});
            if (full) {
                OracleWorld e(recorded, theme);
                auto& empty = e.b.make<d::TextField>("name", "Name", "", "Search", d::Size{260, 34});
                e.host(empty.layer()); snap(e, "textField.placeholder", "idle", empty.layer(), {&empty});
                OracleWorld s(recorded, theme);
                d::TextField::Options secure; secure.secure = true;
                auto& password = s.b.make<d::TextField>("pw", "Password", "hunter2", "", d::Size{260, 34}, secure);
                s.host(password.layer()); snap(s, "textField.secure", "idle", password.layer(), {&password});
                OracleWorld m(recorded, theme);
                d::TextField::Options multi; multi.multiline = true; multi.light = true;
                auto& note = m.b.make<d::TextField>("note", "Note", "First line\nSecond line that is long enough to wrap in the field", "", d::Size{220, 90}, multi);
                m.host(note.layer()); snap(m, "textField.multilineLight", "idle", note.layer(), {&note});
                note.setInvalid(true, true); snap(m, "textField.multilineLight", "invalidShake", note.layer(), {&note});
            }
        }
        if (full) {
            for (const bool capacity : {true, false}) {
                OracleWorld w(recorded, theme);
                d::Cells cells(w.b, "cells", 10, 300, capacity);
                w.host(cells.layer());
                cells.set(6, std::nullopt, false, false);
                cells.set(7, std::nullopt, true, true);
                snap(w, std::string("cells.") + (capacity ? "capacity" : "level"), "pulsing", cells.layer(), {});
            }
            // Cards.
            for (const auto [kind, kindName] : {std::pair{d::ListCard::Kind::text, "TEXT"}, std::pair{d::ListCard::Kind::link, "LINK"}, std::pair{d::ListCard::Kind::image, "IMAGE"}, std::pair{d::ListCard::Kind::file, "FILE"}}) {
                interact(std::string("listCard.") + kindName, theme, full, [kind](World& ww) -> d::Control& {
                    auto& pin = ww.b.make<d::IconButton>("pin", "Pin", d::Icon::pin, d::IconButton::Style::dark, false, false, 24.0, 12.0);
                    return ww.b.make<d::ListCard>("card", kind, "A clipboard entry with a title long enough to wrap onto two lines", "Safari · 12 KB", "2m",
                        kind == d::ListCard::Kind::link, 580.0, std::nullopt, "COPY", false, std::vector<d::Control*>{&pin}); },
                    [](OracleWorld&, d::Control& c, const State& state, const Tree&) { static_cast<d::ListCard&>(c).flashCopied(); state("flashCopied"); });
            }
            interact("listCard.redacted", theme, full, [](World& ww) -> d::Control& { return ww.b.make<d::ListCard>("card", d::ListCard::Kind::text, "Secret", "", "now", false, 580.0, std::nullopt, "COPY", true); });
            for (const auto [ext, style, name] : {std::tuple{d::ArchiveTile::Extension::pdf, d::ArchiveTile::Style::standard, "archiveTile.pdf.standard"},
                     std::tuple{d::ArchiveTile::Extension::txt, d::ArchiveTile::Style::classified, "archiveTile.txt.classified"},
                     std::tuple{d::ArchiveTile::Extension::img, d::ArchiveTile::Style::standard, "archiveTile.img.standard"}})
                interact(name, theme, full, [ext, style](World& ww) -> d::Control& {
                    return ww.b.make<d::ArchiveTile>("tile", 7, "Operation manual", ext, std::nullopt, std::string("2.4 MB"), std::string("2026.10.22"), 136.0, 100.0, false, style); },
                    [](OracleWorld&, d::Control& c, const State& state, const Tree&) { static_cast<d::ArchiveTile&>(c).setSelected(true); state("selected"); });
        }
        // Scroll area.
        {
            OracleWorld w(recorded, theme);
            d::ScrollArea area(w.b, "scroll", {0, 125, 590, 300});
            w.host(area.layer());
            const auto extras = [&](std::string_view state, std::initializer_list<std::pair<const char*, double>> values) {
                if (const Json* row = run.row("scrollArea", state, theme.light, hex(theme), "en"))
                    for (const auto& [key, value] : values) check(near((*row)[key].number(), value), std::string("scrollArea/") + std::string(state) + ": " + key);
            };
            snap(w, "scrollArea", "short", area.layer(), {}); extras("short", {{"contentWidth", area.contentWidth()}, {"maximumOffset", area.maximumOffset()}});
            area.setContentHeight(900); snap(w, "scrollArea", "long", area.layer(), {}); extras("long", {{"contentWidth", area.contentWidth()}, {"maximumOffset", area.maximumOffset()}});
            area.scroll(120); snap(w, "scrollArea", "scrolled120", area.layer(), {}); extras("scrolled120", {{"offset", area.offset()}});
            area.setOffset(10000, true); snap(w, "scrollArea", "glideToEnd", area.layer(), {}); extras("glideToEnd", {{"offset", area.offset()}});
            area.reveal({0, 100, 10, 40}); extras("reveal100", {{"offset", area.offset()}});
            check(area.scroll(50), "scrollArea: scroll(by: 50) after reveal moves");  // evaluated inside the oracle's extra dictionary
            snap(w, "scrollArea", "reveal100", area.layer(), {});
        }
        if (full) {
            OracleWorld w(recorded, theme);
            auto& line = d::redaction::bar(w.b, "line", {20, 10, 300, 18.6}, d::type::rowLabel(), d::redaction::width("Endministrator", characters()));
            w.host(line); snap(w, "redaction.line", "rest", line, {});
            if (const Json* row = run.row("redaction.line", "rest", false, "FAD41F", "en"))
                for (const auto& e : (*row)["widths"].array()) check(near(e["width"].number(), d::redaction::width(e["text"].string(), characters())), "redaction width of '" + e["text"].string() + "'");
            OracleWorld s(recorded, theme);
            auto& shortBar = d::redaction::bar(s.b, "short", d::Size{50, 14});
            s.host(shortBar); snap(s, "redaction.short", "rest", shortBar, {});
            OracleWorld b(recorded, theme);
            const std::string text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Supercalifragilisticexpialidociousnessandmore words";
            auto& block = d::redaction::block(b.b, "block", text, {400, 120});
            b.host(block);
            std::vector<d::Layer*> layers{&block};
            d::redaction::playEntrance(layers, b.env.clock());
            snap(b, "redaction.block", "entrance", block, {});
            if (const Json* row = run.row("redaction.block", "entrance", false, "FAD41F", "en")) {
                std::vector<std::string> expected; for (const auto& l : (*row)["wrapped"].array()) expected.push_back(l.string());
                check(expected == d::redaction::wrapped("Lorem ipsum dolor sit amet, consectetur adipiscing elit, Supercalifragilisticexpialidociousnessandmorewordsandmorewords", 46, characters()), "redaction.wrapped");
            }
        }
    }
    // Language-dependent components: header and empty states.
    for (const auto& [tag, language] : {std::pair{"en", endfield::core::Language::english}, std::pair{"zh-Hans", endfield::core::Language::simplifiedChinese},
             std::pair{"zh-Hant", endfield::core::Language::traditionalChinese}, std::pair{"ja", endfield::core::Language::japanese}, std::pair{"ko", endfield::core::Language::korean}}) {
        for (const auto theme : {Theme{"FAD41F", false}, Theme{"FAD41F", true}, Theme{"22A6F2", false}}) {
            if (language != endfield::core::Language::english && (theme.light || theme.accent != "FAD41F")) continue;
            const auto row = [&](std::string_view name, std::string_view state) { return run.row(name, state, theme.light, hex(theme), tag); };
            const auto label = [&](std::string_view name, std::string_view state) { return std::string(name) + "/" + std::string(state) + "/" + tag + (theme.light ? "/light/" : "/") + hex(theme); };
            OracleWorld w(recorded, theme, language);
            d::ModuleHeader header(w.env, "header");
            d::LayerArena pageArena("page", &w.env.deadlines); d::ControlSet page; d::Builder pb{w.env, pageArena, page};
            auto& action = pb.make<d::Button>("header.clear", endfield::core::localized("Clear", "清空", language), d::Icon::trash, K::secondary, H::compact);
            auto& pin = pb.make<d::IconButton>("header.pin", "Pin", d::Icon::pin);
            header.configure({"CLIPBOARD", "Detail"}, "MOD-03", d::ModuleTitle::make("Clipboard", "剪贴板缓存", language), "07", true, {&action, &pin});
            w.host(header.layer());
            if (const Json* r = row("header", "configured")) {
                run.compare(label("header", "configured"), *r, header.layer(), w.root, header.controls());
                const auto titles = header.titleTexts();
                std::vector<std::string> crumbs; for (const auto& c : (*r)["crumbs"].array()) crumbs.push_back(c.string());
                check((*r)["titles"].array()[0].string() == titles.first && (*r)["titles"].array()[1].string() == titles.second && (*r)["counter"].string() == header.counterText()
                    && crumbs == header.crumbTexts(), label("header", "configured") + ": titles, counter, crumbs");
            } else run.skip(label("header", "configured"), theme.light, hex(theme));
            if (language == endfield::core::Language::english && !theme.light && theme.accent == "FAD41F") {
                header.setCounter("1,284"); header.playEntrance(w.env.clock());
                if (const Json* r = row("header", "counterAndEntrance")) run.compare(label("header", "counterAndEntrance"), *r, header.layer(), w.root, header.controls());
                header.configure({}, "SYS-01", d::ModuleTitle::make("System", "系统", language), "", false);
                if (const Json* r = row("header", "reconfiguredNoBack")) run.compare(label("header", "reconfiguredNoBack"), *r, header.layer(), w.root, header.controls());
            }
            OracleWorld e(recorded, theme, language);
            auto& empty = d::emptyState::bilingual(e.b, "empty", "NO RECORDS", "剪贴板为空");
            e.host(empty);
            if (const Json* r = row("emptyState.bilingual", "rest")) run.compare(label("emptyState.bilingual", "rest"), *r, empty, e.root, {}); else run.skip(label("emptyState.bilingual", "rest"), theme.light, hex(theme));
            if (language == endfield::core::Language::english && !theme.light && theme.accent == "FAD41F") {
                OracleWorld m(recorded, theme, language);
                auto& made = d::emptyState::make(m.b, "empty", "NOTHING HERE", std::string("暂无内容"), d::TextRegion::simplifiedChinese, {400, 200}, 40);
                m.host(made);
                if (const Json* r = row("emptyState.make", "reserving40")) {
                    run.compare(label("emptyState.make", "reserving40"), *r, made, m.root, {});
                    check(near((*r)["textBottom"].number(), d::emptyState::textBottom({400, 200}, true, 40)), "emptyState.textBottom");
                }
                OracleWorld c(recorded, theme, language);
                auto& cross = d::emptyState::crosshair(c.b, "cross");
                c.host(cross);
                if (const Json* r = row("emptyState.crosshair", "rest")) run.compare(label("emptyState.crosshair", "rest"), *r, cross, c.root, {});
            }
        }
    }
    std::cout << "oracle: " << run.compared << " component states compared, " << run.skipped << " not in the oracle, " << recorded.misses.size() << " unrecorded measurements\n";
    for (const auto& m : recorded.misses) std::cout << "  unrecorded: " << m << '\n';
    for (const auto& f : run.failures) std::cout << "  MISMATCH " << f << '\n';
    check(run.failures.empty(), std::to_string(run.failures.size()) + " oracle mismatches");
}
}

int main(int argc, char** argv) {
    try {
        controlBase(); rows(); toggles(); buttons(); sliders(); menus(); tabs(); badges(); scrolling(); cards(); header(); redaction(); textFields(); emission();
        if (const auto data = loadOracle(argc > 1 ? argv[1] : nullptr)) oracle(*data);
        else std::cout << "hud-design-source.json not present: Swift-derived contracts only\n";
        std::cout << "hud_design_components_tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "hud_design_components_tests failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
