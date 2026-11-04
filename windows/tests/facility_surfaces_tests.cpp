// Facility banner and tag stack contracts (Sources/HUDFacilityBanner.swift,
// Sources/HUDFacilityTags.swift; SHELL-70..72): geometry from the Swift
// (with a deterministic stand-in measurer), the English-only and bilingual
// texts, accessibility labels, the queue, the 5 s countdown paused while
// hovered, fbIn / fbOut motion, click and withdraw, the hazard loops, and
// the stack's diff: new tags slide in, removed ones collapse, the rest move,
// x strikes through and collapses at 300 ms, removal at 560 ms, Reduce Motion.
#include "modules/facility_tags.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
namespace m = endfield::modules;
namespace d = endfield::modules::design;
namespace f = endfield::modules::facility;
using endfield::core::Language;
using d::Channel;
using d::Point;
using d::Rect;
using m::FacilityAlertKind;
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) <= e; }
std::string str(Rect r) { std::ostringstream s; s << "(" << r.x << ", " << r.y << ", " << r.width << ", " << r.height << ")"; return s.str(); }
void same(Rect a, Rect b, const std::string& what) { check(near(a.x, b.x) && near(a.y, b.y) && near(a.width, b.width) && near(a.height, b.height), what + ": " + str(a) + " vs " + str(b)); }
const d::CharacterRules& characters() { static const auto rules = d::portableCharacterRules(); return rules; }
// .55 em per character plus tracking, rounded up to .5 px like HUDText.width.
struct FakeMeasurer final : d::TextMeasurer {
    double width(std::span<const d::TextRun> runs, d::TextRegion) override {
        double w = 0;
        for (const auto& r : runs) w += double(characters().count(r.text)) * r.style.size * (.55 + r.style.tracking);
        return std::ceil(w * 2) / 2;
    }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion, double) override { return {s.size * s.lineHeight, s.size * .8, s.size * .2}; }
};
double fakeWidth(std::string_view text, double size, double tracking = 0) { return std::ceil(double(characters().count(text)) * size * (.55 + tracking) * 2) / 2; }
struct World {
    FakeMeasurer measurer;
    d::Environment env;
    explicit World(Language l = Language::english, bool reduce = false) {
        env.text.measurer = &measurer; env.text.uppercase = d::asciiUppercase; env.now = 10; env.language = l; env.reduceMotion = reduce;
    }
    void at(double t) { env.now = t; env.deadlines.advance(t); }
};
m::FacilityAlert disk(std::int64_t available = 40'000'000'000) {
    return {{FacilityAlertKind::disk, m::FacilityDiskReading::make("Local Disk", 500'000'000'000, available), {}, {}}, true};
}
m::FacilityAlert memory(std::optional<m::FacilityMemoryUsage> usage = m::FacilityMemoryUsage::make(14'720'000'000, 16'000'000'000)) {
    return {{FacilityAlertKind::memory, {}, m::FacilityMemoryReading{m::FacilityMemoryPressure::warning, usage}, {}}, true};
}
const d::Layer* child(const d::Layer& l, std::string_view suffix) {
    if (std::string_view(l.id()).ends_with(suffix)) return &l;
    for (const auto* c : l.children()) if (const auto* r = child(*c, suffix)) return r;
    return nullptr;
}

void bannerGeometry() {
    World w;
    f::FacilityBanner banner(w.env);
    check(banner.layer().bounds() == d::Size{540, 70} && banner.layer().hidden() && !banner.control().enabled(), "the idle banner is hidden and inert");
    banner.show(disk());
    check(!banner.layer().hidden() && banner.isShowing() && banner.control().enabled(), "show presents");
    const auto t = banner.texts();
    check(t[0] == "FACILITY ALERT" && t[1].empty() && t[2] == "LOW DISK SPACE" && t[3] == "Local Disk  40.0 GB left" && t[4] == "8%" && t[5] == "FREE",
        "English shows English only");
    // render (:244-255): value column right-aligned at 500; text column from 104.
    const double column = std::ceil(std::max(fakeWidth("8%", 34), fakeWidth("FREE", 9, .14)));
    const double width = std::max(40.0, 500 - column - 16 - 104), label = std::min(width, fakeWidth("FACILITY ALERT", 10, .18));
    const auto line = [&](std::string_view s) { return d::textOf(*child(banner.layer(), s)).lineFrame; };
    same(line("banner.value"), {500 - column, 11.7, column, 34}, "value line");
    same(line("banner.unit"), {500 - column, 45.7, column, 12.6}, "unit line");
    same(line("banner.label"), {104, 10.5, label, 10}, "label line (with a detail)");
    same(line("banner.name"), {104 + label + 10, 10.5, std::max(0.0, width - label - 10), 10}, "name line");
    same(line("banner.title"), {104, 26.5, width, 17}, "title line");
    same(line("banner.detail"), {104, 48.5, width, 11}, "detail line");
    check(banner.control().label == "FACILITY ALERT, LOW DISK SPACE, 8% FREE", "accessibility label: " + banner.control().label);
    // Without a detail the column moves down to 18.5.
    banner.show(memory(std::nullopt));
    check(banner.queue().size() == 1, "another kind queues");
    f::FacilityBanner second(w.env);
    second.show(memory(std::nullopt));
    same(d::textOf(*child(second.layer(), "banner.label")).lineFrame, {104, 18.5, std::min(std::max(40.0, 500 - std::ceil(std::max(fakeWidth("WARNING", 34), fakeWidth("PRESSURE", 9, .14))) - 16 - 104), fakeWidth("FACILITY ALERT", 10, .18)), 10},
        "no detail: the label at 18.5");
    check(child(second.layer(), "banner.detail")->hidden(), "an empty detail is hidden");
    // Fixed artwork: hazard 78 x 70 at x 10, the countdown rule (28, 68, 484 x 2) from its left edge.
    same(child(banner.layer(), "banner.hazard")->frame(), {10, 0, 78, 70}, "hazard block");
    same(banner.rule().frame(), {28, 68, 484, 2}, "countdown rule");
    check(banner.rule().anchorPoint() == Point{0, .5}, "the rule scales from its left edge");
    same(child(banner.layer(), "banner.hazard.ring")->frame(), {17, 13, 44, 44}, "warn ring");
    same(child(banner.layer(), "banner.hazard.box")->frame(), {19, 15, 40, 40}, "warn box");
    // Bilingual outside English: the English name beside the label, the localized title below.
    World zh(Language::simplifiedChinese);
    f::FacilityBanner chinese(zh.env);
    chinese.show(disk());
    const auto c = chinese.texts();
    check(c[0] == "设施警报" && c[1] == "LOW DISK SPACE" && c[2] == "存储空间不足" && c[3] == "Local Disk  剩余 40.0 GB" && c[5] == "可用", "Chinese is bilingual");
    check(chinese.control().label == "设施警报, 存储空间不足, 8% 可用", "Chinese accessibility label");
}

void bannerMotion() {
    World w;
    f::FacilityBanner banner(w.env);
    std::vector<FacilityAlertKind> clicked;
    unsigned hidden{};
    banner.onClick = [&](FacilityAlertKind k) { clicked.push_back(k); };
    banner.onHidden = [&] { ++hidden; };
    banner.show(disk());
    const auto& plate = banner.plate();
    // fbIn: down from -90 over .42 x .6 SNAP while the left wipe unfolds over .42.
    check(near(plate.presented(Channel::translateY, 10), -90) && near(plate.presented(Channel::wipe, 10), 0), "fbIn starts above and folded");
    check(near(plate.presented(Channel::translateY, 10.252), 0) && near(plate.presented(Channel::wipe, 10.42), 1), "fbIn ends in place and unfolded");
    check(banner.isHazardRunning() && banner.isCountingDown(), "hazard loop and countdown run");
    w.at(12);
    check(near(banner.countdownRemaining(), 3) && near(banner.rule().presented(Channel::scaleX, 12), .6), "the rule shrinks linearly");
    // Hover pauses with the remaining share; leaving resumes.
    d::updateHighlights(banner.layer(), Point{200, 30}, false);
    check(!banner.isCountingDown() && near(banner.countdownRemaining(), 3) && near(banner.rule().value(Channel::scaleX), .6), "hover pauses the countdown");
    w.at(30);
    check(banner.isShowing() && near(banner.countdownRemaining(), 3), "no expiry while hovered");
    d::updateHighlights(banner.layer(), std::nullopt, false);
    check(banner.isCountingDown(), "leaving resumes");
    w.at(32.9);
    check(banner.isShowing(), "still counting");
    w.at(33);
    check(near(plate.presented(Channel::opacity, 33.13), 1 - d::easing::easeIn.value(.5), 1e-3) && !banner.control().enabled(), "fbOut fades over .26 ease-in");
    w.at(33.26);
    check(!banner.isShowing() && banner.layer().hidden() && hidden == 1 && !banner.isHazardRunning(), "removed at 260 ms, then idle");
    // Queue: same kind refreshes in place, other kinds follow one by one.
    w.at(40);
    banner.show(disk());
    banner.show(memory());
    banner.show(memory(m::FacilityMemoryUsage::make(15'000'000'000, 16'000'000'000)));
    check(banner.queue().size() == 1 && banner.queue()[0].measurement.memory->usage->usedBytes == 15'000'000'000, "a queued alert refreshes in place");
    banner.show(disk(10'000'000'000));
    check(banner.texts()[4] == "2%", "the alert on screen refreshes in place");
    // Click: leaves and opens the module; the queued banner follows.
    const Point centre{270, 35};
    d::Router router;
    router.set({&banner.control()});
    check(router.pointerDown(centre, banner.layer()), "the plate takes the press");
    router.pointerUp(centre, banner.layer());
    check(clicked == std::vector{FacilityAlertKind::disk}, "a click opens the alert's module");
    w.at(40.26);
    check(banner.isShowing() && banner.current() && banner.current()->kind() == FacilityAlertKind::memory && hidden == 1, "the queued banner follows");
    // Withdraw: the shown one leaves; hide drops the queue at once.
    banner.show(disk());
    banner.withdraw(FacilityAlertKind::memory);
    w.at(40.52);
    check(banner.current() && banner.current()->kind() == FacilityAlertKind::disk, "a withdrawn banner leaves and the queue goes on");
    banner.show(memory());
    banner.hide(false);
    check(!banner.isShowing() && banner.queue().empty() && banner.layer().hidden() && hidden == 2, "hide(false) drops everything at once");
    // Reduce Motion: no drop or unfold, the rule stays whole, the leave is instant, no hazard loop.
    World r(Language::english, true);
    f::FacilityBanner reduced(r.env);
    reduced.show(disk());
    check(near(reduced.plate().presented(Channel::translateY, 10), 0) && near(reduced.plate().presented(Channel::wipe, 10), 1) && !reduced.isHazardRunning(), "Reduce Motion presents at rest");
    r.at(12);
    check(near(reduced.rule().presented(Channel::scaleX, 12), 1), "Reduce Motion keeps the rule whole");
    r.at(15);
    check(!reduced.isShowing(), "the countdown still ends it after 5 s, at once");
}

void tagStack() {
    World w;
    f::FacilityTagStack stack(w.env);
    std::vector<FacilityAlertKind> opened, dismissed;
    stack.onOpen = [&](FacilityAlertKind k) { opened.push_back(k); };
    stack.onDismiss = [&](FacilityAlertKind k) { dismissed.push_back(k); };
    stack.update({disk()}, true);
    const auto text = std::string("DISK 8% FREE");
    const double natural = std::ceil(fakeWidth("DISK ", 11, .06) + fakeWidth("8%", 11, .06) + fakeWidth(" FREE", 11, .06));
    // The stand-in measures runs together; recompute exactly as the measurer does.
    const double measured = std::ceil(d::preferredWidth(d::TextBlock(text, {11, d::FontWeight::bold, d::rgb(0xF2F2F2), 1, .06}), w.env.text));
    check(near(natural, measured, 1), "the run widths add up");
    const double tagWidth = 41 + measured + 32;
    auto tag = stack.tag(FacilityAlertKind::disk);
    check(tag && tag->text == text && tag->looping && !tag->struck, "the disk tag shows with a running hazard strip");
    same(tag->frame, {272 - tagWidth, 0, tagWidth, 28}, "right-aligned, first row");
    check(near(stack.preferredHeight(), 28), "one tag tall");
    const auto controls = stack.controls();
    check(controls.size() == 2 && controls[0]->id() == "facility.tag.disk" && controls[1]->id() == "facility.tag.disk.dismiss", "tag and x controls");
    check(controls[0]->label == "LOW DISK SPACE, DISK 8% FREE" && controls[1]->label == "Dismiss, LOW DISK SPACE", "tag accessibility labels");
    // ftIn: from +16 px and transparent over .35 E.
    const auto* layer = stack.tagLayer(FacilityAlertKind::disk);
    check(near(layer->presented(Channel::opacity, 10), 0) && near(layer->presented(Channel::translateX, 10), 16), "ftIn starts offset and clear");
    check(near(layer->presented(Channel::opacity, 10.35), 1) && near(layer->presented(Channel::translateX, 10.35), 0), "ftIn settles");
    // A second alert stacks below in raise order.
    w.at(11);
    stack.update({disk(), memory()}, true);
    same(stack.tag(FacilityAlertKind::memory)->frame, {272 - stack.tag(FacilityAlertKind::memory)->frame.width, 34, stack.tag(FacilityAlertKind::memory)->frame.width, 28}, "second row at 34");
    check(near(stack.preferredHeight(), 62), "two tags and one gap");
    check(stack.tag(FacilityAlertKind::memory)->text == "RAM 92% PRESSURE", "memory tag text");
    // Clicking a tag opens its module; hover lifts it.
    d::Router router;
    router.set(stack.controls());
    const auto press = [&](const d::Control& c) { const auto r = c.rect(stack.layer()); const Point p{r.x + r.width / 2, r.y + r.height / 2}; check(router.pointerDown(p, stack.layer()), "press " + c.id()); router.pointerUp(p, stack.layer()); };
    press(*stack.controls()[0]);
    check(opened == std::vector{FacilityAlertKind::disk}, "a tag click opens its module");
    d::updateHighlights(stack.layer(), Point{271, 14}, false);
    check(near(stack.tagLayer(FacilityAlertKind::disk)->presented(Channel::translateX, 11.22), -3), "hover lifts 3 px left");
    d::updateHighlights(stack.layer(), std::nullopt, false);
    // x: strike-through, collapse at 300 ms, removal at 560 ms; the next tag moves up then.
    w.at(12);
    press(*stack.controls()[1]);
    check(dismissed == std::vector{FacilityAlertKind::disk}, "x reports the dismiss at once");
    check(stack.tag(FacilityAlertKind::disk)->struck && stack.controls().size() == 2, "the struck tag holds its place and leaves the router");
    w.at(12.29);
    check(stack.tag(FacilityAlertKind::memory)->frame.y == 34, "the others wait for the collapse");
    w.at(12.3);
    check(stack.tag(FacilityAlertKind::memory)->frame.y == 0, "the collapse gives up the place");
    const auto* memoryLayer = stack.tagLayer(FacilityAlertKind::memory);
    (void)memoryLayer;
    check(stack.shownKinds().size() == 2 && stack.kinds() == std::vector{FacilityAlertKind::memory}, "the collapsing tag is still shown");
    w.at(12.56);
    check(stack.shownKinds() == std::vector{FacilityAlertKind::memory}, "removed at 560 ms");
    // The monitor dropping the alert collapses the tag on the next update; a new raise slides in again.
    stack.update({memory()}, true);
    stack.update({}, true);
    check(stack.kinds().empty() && stack.shownKinds().size() == 1, "a recovered alert collapses");
    stack.update({memory()}, true);
    check(stack.kinds() == std::vector{FacilityAlertKind::memory} && stack.shownKinds().size() == 2, "a new tag of the same kind appears beside the leaving one");
    // Loops only while active.
    stack.setActive(false);
    check(!stack.tag(FacilityAlertKind::memory)->looping, "inactive stacks stop the hazard loops");
    stack.setActive(true);
    // Reduce Motion: dismiss removes at once.
    World r(Language::english, true);
    f::FacilityTagStack reduced(r.env);
    reduced.update({disk(), memory()}, true);
    check(near(reduced.tagLayer(FacilityAlertKind::disk)->presented(Channel::opacity, 10), 1), "no ftIn under Reduce Motion");
    reduced.controls()[1]->activate();
    check(reduced.shownKinds() == std::vector{FacilityAlertKind::memory} && reduced.tag(FacilityAlertKind::memory)->frame.y == 0, "Reduce Motion removes at once");
    // Long text caps the tag at 272.
    World zh(Language::simplifiedChinese);
    f::FacilityTagStack chinese(zh.env);
    chinese.update({disk()}, false);
    check(chinese.tag(FacilityAlertKind::disk)->text == "磁盘 8% 可用" && chinese.controls()[1]->label == "忽略, 存储空间不足", "Chinese tag");
}
}

int main() {
    try {
        bannerGeometry();
        bannerMotion();
        tagStack();
        std::cout << "facility surface contracts passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
