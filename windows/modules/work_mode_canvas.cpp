// Work Mode on the redesign: the port of Sources/WorkModeCanvas.swift (see the
// header). Line references are that file's (Mac 1.3.0).
//
// Removed with the v1.2.0 page (MODS-739, MODS-743): the .44 s expanded
// layout, the clock "index" travel, the .985 button press, the Ready /
// RUNNING / PAUSED / Stopped / Completed phase words, the invalid-duration hint
// and the ". Space starts, pauses or resumes." help (MODS-729, MODS-741), and
// the auto-cancel of the custom editor when a session starts (MODS-734: a new
// duration now ends a running session, and leaving commits the field).
//
// Not copied (a Mac bug, reported): the Mac's custom field is a lazy var, so
// its label and placeholder keep the language, and its frame the theme, of
// its first use; here they follow the UI language and palette at every build.
#include "modules/work_mode_canvas.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace endfield::modules {
namespace d = design;
namespace {
using d::Point; using d::Rect; using d::Channel;
constexpr double pi = std::numbers::pi;
std::string pad2(std::int64_t v) { return d::format::pad2(v); }
// CGPath.addArc(center:radius:startAngle:endAngle:clockwise: false) in the
// HUD's flipped space (angles grow clockwise on screen) as cubics of at most
// 90 degrees.
void arc(d::PathBuilder& path, Point c, double r, double start, double end, bool move) {
    const int segments = std::max(1, int(std::ceil((end - start) / (pi / 2) - 1e-9)));
    const double step = (end - start) / segments, k = 4.0 / 3 * std::tan(step / 4) * r;
    const auto at = [&](double a) { return Point{c.x + r * std::cos(a), c.y + r * std::sin(a)}; };
    if (move) path.move(at(start));
    for (int i = 0; i < segments; ++i) {
        const double a0 = start + i * step, a1 = a0 + step;
        const Point p0 = at(a0), p1 = at(a1);
        path.cubic({p0.x - k * std::sin(a0), p0.y + k * std::cos(a0)}, {p1.x + k * std::sin(a1), p1.y - k * std::cos(a1)}, p1);
    }
}
d::PathBuilder circle(Point c, double r) { return d::PathBuilder().ellipse({c.x - r, c.y - r, r * 2, r * 2}); }
// A filled triangle (HUDLayer.shape "workMode.triangle").
d::PathBuilder triangle(Point a, Point b, Point c) { return d::PathBuilder().move(a).line(b).line(c).close(); }
d::PathBuilder shifted(const d::PathBuilder& path, Point by) { return path.transformed(1, 0, 0, 1, -by.x, -by.y); }
d::StrokeStyle stroke(double width, std::vector<double> dash = {}) { d::StrokeStyle s; s.width = width; s.dash = std::move(dash); return s; }
Rect outset(Rect r, double by) { return {r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by}; }
Rect integral(Rect r) {
    const double x = std::floor(r.x), y = std::floor(r.y);
    return {x, y, std::ceil(r.x + r.width) - x, std::ceil(r.y + r.height) - y};
}
}

// ---------------------------------------------------------------------------
// WorkModeDialGeometry (:6-45)
// ---------------------------------------------------------------------------
namespace workModeDial {
double fraction(const WorkModeSnapshot& v) {
    if (v.kind == WorkModeKind::countdown) return v.duration > 0 ? std::min(1.0, std::max(0.0, double(v.displayedSeconds()) / v.duration)) : 0;
    return double(v.displayedSeconds() % 60) / 60;
}
int litTicks(double f) { return std::min(tickCount, std::max(0, int(std::ceil(f * tickCount - 0.000001)))); }
d::PathBuilder ticks(int first, int last) {
    d::PathBuilder path;
    for (int k = first; k < last; ++k) {
        const double angle = double(k) / tickCount * 2 * pi - pi / 2;
        const bool major = k % 10 == 0;
        const double inner = major ? 268 : 278, half = (major ? 4 : 2.4) / 2;
        const Point u{std::cos(angle), std::sin(angle)}, n{-u.y, u.x};
        const auto point = [&](double r, double side) { return Point{center.x + u.x * r + n.x * side, center.y + u.y * r + n.y * side}; };
        path.move(point(292, -half)).line(point(292, half)).line(point(inner, half)).line(point(inner, -half)).close();
    }
    return path;
}
}

// ---------------------------------------------------------------------------
// WorkModeRing (:49-139): built once per page; set(_:animated:) moves the
// progress arc (.6 s E), the lit ticks and the travelling marker in place.
// ---------------------------------------------------------------------------
class WorkModeCanvas::Ring {
public:
    static constexpr int chunkTicks = 10, chunks = workModeDial::tickCount / chunkTicks;
    Ring(d::Builder& b, double fraction) : env_(b.env) {
        namespace g = workModeDial;
        const auto& p = b.env.palette;
        const Point c = g::center;
        const Rect bounds{0, 0, g::frame.width, g::frame.height};
        layer_ = &b.layer("workMode.ring");
        layer_->setFrame(g::frame);
        const auto stroked = [&](std::string_view id, const d::PathBuilder& path, d::Color color, d::StrokeStyle style) {
            auto& l = b.layer(id);
            l.setFrame(bounds);
            l.setContent(d::shape(l.id(), bounds, path, std::nullopt, color, std::move(style)));
            layer_->add(l);
        };
        // Static: outer hairline r 304, track r 252, dotted ring r 232 (:78-86).
        stroked("workMode.circle.outer", circle(c, 304), p.foreground(.18), stroke(1));
        stroked("workMode.circle.track", circle(c, g::arcRadius), p.tone(0x2A2A2A), stroke(10));
        stroked("workMode.circle.dots", circle(c, 232), p.foreground(.12), stroke(1, {2, 7}));
        // Quarter labels 00 / 15 / 30 / 45: SVG text, y is the baseline 4 px below the r 322 point (:90-99).
        const d::TextStyle style{11, d::FontWeight::regular, p.token(d::ThemeToken::steel)};
        const auto metrics = b.env.text.metrics(style);
        const std::array<const char*, 4> labels{"00", "15", "30", "45"};
        for (int quarter = 0; quarter < 4; ++quarter) {
            const double angle = quarter * pi / 2 - pi / 2, baseline = c.y + std::sin(angle) * 322 + 4;
            layer_->add(d::makeText(b, "workMode.quarter" + std::to_string(quarter), labels[std::size_t(quarter)], style,
                {c.x + std::cos(angle) * 322 - 15, baseline - metrics.baseline(), 30, 11}, d::TextAlignment::center));
        }
        // The fixed top marker (:104).
        const auto fixed = triangle({c.x, 4}, {c.x + 7, 14}, {c.x - 7, 14});
        auto& top = b.layer("workMode.marker.fixed");
        top.setFrame(integral(fixed.bounds()));
        d::setPath(top, shifted(fixed, {top.frame().x, top.frame().y}), p.accentInk());
        layer_->add(top);
        // The progress arc (:80-85): 12 o'clock clockwise, accentInk, 10 px, butt
        // caps. strokeEnd f = the right half (12 -> 6 o'clock) turned clockwise
        // under the clip x >= c.x by min(f, .5) x 2 pi, and the left half turned
        // under x < c.x by max(0, f - .5) x 2 pi. Each half runs 1 degree past its
        // trailing end, which always stays outside its clip.
        constexpr double overrun = pi / 180;
        for (int side = 0; side < 2; ++side) {
            auto& clip = b.layer(side == 0 ? "workMode.arc.right" : "workMode.arc.left");
            clip.setFrame(side == 0 ? Rect{c.x, 0, bounds.width - c.x, bounds.height} : Rect{0, 0, c.x, bounds.height});
            clip.setMasksToBounds(true);
            d::PathBuilder half;
            // Right clip: the LEFT half (6 -> 9 -> 12) turns in; left clip: the right half (12 -> 3 -> 6).
            if (side == 0) arc(half, c, g::arcRadius, pi / 2 - overrun, 3 * pi / 2, true);
            else arc(half, c, g::arcRadius, -pi / 2 - overrun, pi / 2, true);
            auto& turn = b.layer(side == 0 ? "workMode.arc.right.half" : "workMode.arc.left.half");
            const Rect area = integral(outset(half.bounds(), 6));
            turn.setBounds({area.width, area.height});
            turn.setAnchorPoint({(c.x - area.x) / area.width, (c.y - area.y) / area.height});
            turn.setPosition({c.x - clip.frame().x, c.y});
            turn.setAnimatable();
            turn.setContent(d::shape(turn.id(), {0, 0, area.width, area.height}, shifted(half, {area.x, area.y}), std::nullopt, p.accentInk(), stroke(10)));
            clip.add(turn);
            layer_->add(clip);
            halves_[std::size_t(side)] = &turn;
        }
        // Ticks (:87-89): twelve chunks of ten, unlit tone(3D3D3D) below lit accentInk.
        for (int chunk = 0; chunk < chunks; ++chunk) {
            auto& l = b.layer("workMode.ticks" + std::to_string(chunk));
            l.setFrame(integral(outset(g::ticks(chunk * chunkTicks, (chunk + 1) * chunkTicks).bounds(), 1)));
            l.setAnimatable();                   // its own surface: a lit-count change repaints only this chunk
            layer_->add(l);
            chunks_[std::size_t(chunk)] = &l;
        }
        // The travelling marker (:100-103): rotates about the ring centre; its triangle sits at 12 o'clock.
        orbit_ = &b.layer("workMode.marker");
        orbit_->setFrame(bounds);
        orbit_->setAnimatable();
        const double topY = c.y - g::arcRadius;
        const auto marker = triangle({c.x, topY - 17}, {c.x + 9, topY - 2}, {c.x - 9, topY - 2});
        auto& mark = b.layer("workMode.marker.triangle");
        mark.setFrame(integral(marker.bounds()));
        d::setPath(mark, shifted(marker, {mark.frame().x, mark.frame().y}), p.accentInk());
        orbit_->add(mark);
        layer_->add(*orbit_);
        set(fraction, false);
    }
    d::Layer& layer() const noexcept { return *layer_; }
    double fraction() const noexcept { return fraction_; }

    // set(_:animated:) (:111-136).
    void set(double value, bool animated) {
        const auto clock = env_.clock();
        const double shown = (halves_[0]->presented(Channel::rotation, clock.now) + halves_[1]->presented(Channel::rotation, clock.now)) / (2 * pi);
        fraction_ = value;
        const int count = workModeDial::litTicks(value);
        if (count != litCount_) { paint(litCount_, count); litCount_ = count; }
        // A stopwatch minute wraps forward: jump instead of rewinding the arc.
        const bool animate = animated && !clock.reduceMotion && value >= shown - .5 && std::abs(value - shown) > .0001;
        orbit_->setValue(Channel::rotation, value * 2 * pi);   // removes "hud.workMode.marker"
        if (!animate) {
            for (int side = 0; side < 2; ++side) {
                halves_[std::size_t(side)]->removeMotion(d::MotionSlot::effect);
                halves_[std::size_t(side)]->setValue(Channel::rotation, turn(side, value));
            }
            return;
        }
        // HUDMotion.animate(arc, "strokeEnd") .6 E: strokeEnd is linear in the
        // eased time, so each half's clamped turn is exact as linear keyframes
        // on the same curve (a break where the arc passes half way).
        for (int side = 0; side < 2; ++side) {
            auto& half = *halves_[std::size_t(side)];
            const double from = turn(side, shown), to = turn(side, value);
            half.setValue(Channel::rotation, to);
            half.removeMotion(d::MotionSlot::effect);
            if (from == to) continue;
            if ((shown - .5) * (value - .5) < 0) {
                const double at = (.5 - shown) / (value - shown);
                const std::array<double, 3> values{from, turn(side, .5), to}, times{0, at, 1};
                d::motion::keyframes(half, Channel::rotation, values, times, ringTransition, clock, d::easing::standard);
            } else {
                const std::array<double, 2> values{from, to}, times{0, 1};
                d::motion::keyframes(half, Channel::rotation, values, times, ringTransition, clock, d::easing::standard);
            }
        }
        orbit_->setValue(Channel::rotation, shown * 2 * pi);
        d::motion::animate(*orbit_, Channel::rotation, value * 2 * pi, ringTransition, clock, d::easing::standard);
    }
private:
    d::Environment& env_;
    d::Layer* layer_{};
    std::array<d::Layer*, 2> halves_{};
    std::array<d::Layer*, chunks> chunks_{};
    d::Layer* orbit_{};
    double fraction_{-1};
    int litCount_{-1};
    static double turn(int side, double f) { return std::clamp(f - side * .5, 0.0, .5) * 2 * pi; }
    // Repaints the chunks whose lit ticks changed (all on the first call).
    void paint(int before, int count) {
        const auto& p = env_.palette;
        for (int chunk = 0; chunk < chunks; ++chunk) {
            const int first = chunk * chunkTicks, last = first + chunkTicks;
            const auto lit = [&](int n) { return std::clamp(n, first, last); };
            if (before >= 0 && lit(before) == lit(count)) continue;
            auto& l = *chunks_[std::size_t(chunk)];
            const Point origin{l.frame().x, l.frame().y};
            const Rect local{0, 0, l.bounds().width, l.bounds().height};
            auto node = d::frameNode(l.id(), local);
            d::Json::Array parts;
            if (lit(count) < last) parts.push_back(d::shape(l.id() + ".dim", local, shifted(workModeDial::ticks(lit(count), last), origin), p.tone(0x3D3D3D)));
            if (lit(count) > first) parts.push_back(d::shape(l.id() + ".lit", local, shifted(workModeDial::ticks(first, lit(count)), origin), p.accentInk()));
            d::addChildren(node, std::move(parts));
            l.setContent(std::move(node));
        }
    }
};

// ---------------------------------------------------------------------------
// WorkModeCanvas (:147-568)
// ---------------------------------------------------------------------------
WorkModeCanvas::WorkModeCanvas(d::Context& context, WorkModeController& controller, WorkModeCanvasOptions options)
    : Canvas(core::Module::workMode, context), controller_(&controller), options_(std::move(options)), lastPhase_(snapshot().phase) {
    setShowsHeader(false);
}
// The session belongs to the controller; the visible ticker ends with this view (:190-194).
WorkModeCanvas::~WorkModeCanvas() { if (isActive()) controller_->setVisible(false, time()); }

double WorkModeCanvas::time() const { return options_.clock ? std::max(env().now, options_.clock()) : env().now; }
WorkModeSnapshot WorkModeCanvas::snapshot() const { return controller_->snapshot(time()); }
d::Rect WorkModeCanvas::hitBounds() const {
    const Rect a = workModeDial::frame, b = Canvas::hitBounds();
    const double x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(a.x + a.width, b.x + b.width) - x, std::max(a.y + a.height, b.y + b.height) - y};
}

// --- Host interface (:201-215) ------------------------------------------------------
void WorkModeCanvas::setFocusStatusMessage(std::optional<std::string> value, bool needsAccessibilityPermission) {
    std::optional<std::string> message;
    if (value) {
        // trimmingCharacters(in: .whitespacesAndNewlines) for the HUD's messages (ASCII and the ideographic space).
        std::string s = *value;
        const auto blank = [](unsigned char ch) { return ch == ' ' || (ch >= '\t' && ch <= '\r'); };
        while (!s.empty() && blank(static_cast<unsigned char>(s.back()))) s.pop_back();
        std::size_t first = 0;
        while (first < s.size() && blank(static_cast<unsigned char>(s[first]))) ++first;
        s.erase(0, first);
        if (!s.empty()) message = std::move(s);
    }
    const bool appeared = message && message != focusMessage_;
    focusMessage_ = message;
    focusNeedsAccess_ = needsAccessibilityPermission && message;
    if (appeared && isActive()) toast(d::Icon::warn, "Focus mode", "专注模式", *message);
    refresh(false);
}
void WorkModeCanvas::reduceMotionChanged() {
    if (!renderedLanguage()) return;
    rerender(); applyBlink();
}

// --- Lifecycle (:219-245) -------------------------------------------------------------
void WorkModeCanvas::didActivate() {
    controller_->setVisible(true, time());
    refresh(false);
    applyBlink();
}
void WorkModeCanvas::didDeactivate() {
    controller_->setVisible(false, time());
    if (stateDot_) stateDot_->removeMotion(d::MotionSlot::effect);
    if (editingCustom_ && customField_ && !customField_->editing()) { editingCustom_ = false; customField_->setInvalid(false); }
}
void WorkModeCanvas::willLeave() { if (stateDot_) stateDot_->removeMotion(d::MotionSlot::effect); }
// The flick of the inner title; the column staggers in (the ring has no entrance).
void WorkModeCanvas::playEntrance(Point offset, double now) {
    Canvas::playEntrance(offset, now);
    if (titleGroup_) d::motion::wipe(*titleGroup_, d::WipeOrigin::left, d::duration::titleWipe, env().clock(), d::easing::standard, 0, 0.0);
}

// --- State (:249-304) -----------------------------------------------------------------
WorkModeCanvas::Structure WorkModeCanvas::structure() const {
    const auto value = snapshot();
    return {value.kind, value.phase, value.duration, laps_, editingCustom_, focusMessage_, focusNeedsAccess_};
}
void WorkModeCanvas::controllerChanged() {
    // Motion and the toast run on the controller's clock.
    if (const double t = time(); t > env().now) env().now = t;
    const auto value = snapshot();
    const bool finished = lastPhase_ == WorkModePhase::running && value.phase == WorkModePhase::completed;
    lastPhase_ = value.phase;
    // The countdown keeps its deadline while the module is hidden; the toast
    // appears whenever the HUD is on screen.
    if (finished && options_.hudVisible && options_.hudVisible()) toast(d::Icon::timer, "Session complete", "专注结束");
    if (!applying_) refresh(true);
}
// Structure changes re-render in place; anything else updates the digits, the
// ring and the primary button's label.
void WorkModeCanvas::refresh(bool animated) {
    if (!renderedLanguage() || transitioning()) return;
    if (structure() != renderedStructure_) { rerender(); applyBlink(); return; }
    const auto value = snapshot();
    if (timerText_) d::editText(*timerText_, [&](d::TextBlock& block) { block.setText(timeText(value)); });
    if (ring_) ring_->set(workModeDial::fraction(value), animated && isActive());
    if (primary_) primary_->label = primary_->title() + ", " + accessibilityStatus(value);
}
// The FOCUS ON square blinks only while the countdown or stopwatch runs (:283-291).
void WorkModeCanvas::applyBlink() {
    if (!stateDot_) return;
    stateDot_->removeMotion(d::MotionSlot::effect);
    if (!isActive() || snapshot().phase != WorkModePhase::running || env().reduceMotion) return;
    const std::array<double, 3> values{1, .2, 1}, times{0, .5, 1};
    d::motion::keyframes(*stateDot_, Channel::opacity, values, times, 1.6, env().clock(), d::easing::easeInOut, std::numeric_limits<double>::infinity());
}
std::string WorkModeCanvas::timeText(const WorkModeSnapshot& v) { return d::format::clock(v.displayedSeconds()); }
std::string WorkModeCanvas::accessibilityStatus(const WorkModeSnapshot& v) const {
    return (v.kind == WorkModeKind::countdown ? text("Countdown", "倒计时") : text("Stopwatch", "秒表")) + " " + timeText(v);
}
std::string WorkModeCanvas::durationTitle(double seconds, core::Language language) {
    const auto value = std::int64_t(std::round(seconds));
    if (value % 60 == 0) {
        const auto minutes = std::to_string(value / 60);
        const std::array<std::string_view, 1> args{minutes};
        return core::localized("{0} min", "{0} 分", language, args);
    }
    return d::format::clock(value);
}

// --- Page (:308-459) --------------------------------------------------------------------
void WorkModeCanvas::buildPage(d::Page& page) {
    const auto value = snapshot();
    renderedStructure_ = structure();
    ring_ = std::make_unique<Ring>(page.builder(), workModeDial::fraction(value));
    page.layer().add(ring_->layer());
    buildTitle(page);
    buildTabs(page, value);
    buildState(page, value);
    const auto& p = env().palette;
    const d::TextStyle timerStyle{112, d::FontWeight::light, p.tone(0xF6F6F6), .9, -.03, false, true};
    auto& timer = d::makeText(page.builder(), "workMode.timer", timeText(value), timerStyle, {115, 218, 360, 112 * .9}, d::TextAlignment::center);
    d::editText(timer, [](d::TextBlock& block) { block.minimumScale = .5; });
    page.layer().add(timer);
    timerText_ = &timer;
    page.stagger(timer);
    double slotBottom;
    if (value.kind == WorkModeKind::countdown) { buildPresets(page, value); slotBottom = 331 + 30; }
    else slotBottom = 331 + buildLaps(page);
    buildActions(page, value, slotBottom + 12);
    page.back = editingCustom_ ? std::function<void()>([this] { closeCustom(); }) : nullptr;
}
void WorkModeCanvas::buildTitle(d::Page& page) {
    const auto& p = env().palette;
    const auto lines = title("Work Mode", "工作模式").lines(env().language);
    const auto titleStyle = d::type::moduleTitle(p).sized(20), subtitleStyle = d::type::titleSubtitle(p);
    d::TextBlock heading(lines.title, titleStyle), subtitle(lines.subtitle, subtitleStyle);
    heading.language = lines.titleLanguage;
    const double titleWidth = d::preferredWidth(heading, env().text), subtitleWidth = d::preferredWidth(subtitle, env().text);
    const auto titleMetrics = env().text.metrics(titleStyle, lines.titleLanguage), subMetrics = env().text.metrics(subtitleStyle);
    const double width = titleWidth + (lines.subtitle.empty() ? 0 : 12 + subtitleWidth);
    auto& group = page.makeLayer("workMode.title");
    group.setFrame({std::round(295 - width / 2), 118, width + 2, 20});
    group.setAnimatable();                       // the entrance wipe
    heading.setLineFrame({0, 10 - titleMetrics.height / 2, titleWidth + 1, titleMetrics.height});
    const double baseline = heading.lineFrame.y + titleMetrics.baseline();
    subtitle.setLineFrame({titleWidth + 12, baseline - subMetrics.baseline(), subtitleWidth + 1, subMetrics.height});
    auto& titleLayer = page.makeLayer("workMode.title.text");
    titleLayer.setText(std::move(heading));
    auto& subtitleLayer = page.makeLayer("workMode.title.subtitle");
    subtitleLayer.setText(std::move(subtitle));
    group.add(titleLayer); group.add(subtitleLayer);
    page.layer().add(group);
    titleGroup_ = &group;
}
void WorkModeCanvas::buildTabs(d::Page& page, const WorkModeSnapshot& value) {
    auto& tabs = page.makeTabs("workMode.kind", {{text("Countdown", "倒计时"), d::Icon::timer}, {text("Stopwatch", "秒表"), d::Icon::watch}},
        value.kind == WorkModeKind::countdown ? 0 : 1, 270, false);
    tabs.layer().setOrigin({160, 150});
    page.layer().add(tabs.layer());
    // A running or paused session keeps its mode: switching would end it.
    const bool locked = value.active();
    const auto controls = tabs.controls();
    for (std::size_t index = 0; index < controls.size(); ++index) {
        if (int(index) == tabs.selectedIndex()) continue;
        controls[index]->setEnabled(!locked);
        controls[index]->layer().setValue(Channel::opacity, locked ? .35 : 1);
    }
    tabs.onSelect = [this](int index, double direction) { chooseKind(index == 0 ? WorkModeKind::countdown : WorkModeKind::stopwatch, direction); };
    page.add(tabs.controls());
    if (!locked) page.tabs = &tabs;
    page.stagger(tabs.layer());
}
void WorkModeCanvas::buildState(d::Page& page, const WorkModeSnapshot& value) {
    const auto& p = env().palette;
    auto& group = page.makeLayer("workMode.state");
    auto& label = d::makeText(page.builder(), "workMode.state.text", value.active() ? text("Focus on", "专注中") : text("Standby", "待机"), d::type::label(p), {});
    auto& dot = page.makeLayer("workMode.state.dot");
    d::IconButton* alert = nullptr;
    if (focusMessage_) {
        alert = &page.make<d::IconButton>("workMode.focus", focusNeedsAccess_ ? text("Open Accessibility Settings", "打开辅助功能设置") : *focusMessage_,
            d::Icon::warn, d::IconButton::Style::dark, false, false, 22.0, 12.0);
        alert->onActivate = [this] { focusAlertPressed(); };
    }
    const double textWidth = std::ceil(d::preferredWidth(d::textOf(label), env().text));
    const double width = 7 + 8 + textWidth + (alert ? 10 + 22 : 0);
    group.setFrame({std::round(295 - width / 2), 196, width, 10});
    dot.setFrame({0, 1.5, 7, 7});
    dot.setAnimatable();                         // the blink
    d::setBackground(dot, value.active() ? d::fixed::green : p.tone(0x666666));
    d::setLine(label, {15, 0, textWidth + 1, 10});
    group.add(dot); group.add(label);
    if (alert) { alert->layer().setFrame({width - 22, -6, 22, 22}); group.add(alert->layer()); }
    page.layer().add(group);
    if (alert) page.add(*alert);
    stateDot_ = &dot;
    page.stagger(group);
}
void WorkModeCanvas::buildPresets(d::Page& page, const WorkModeSnapshot& value) {
    if (editingCustom_) {
        auto& field = customField();
        registerField(field);
        field.layer().setOrigin({145, 331});
        page.layer().add(field.layer());
        page.stagger(field.layer());
        return;
    }
    std::vector<std::string> options;
    for (double preset : presets) options.push_back(durationTitle(preset, env().language));
    options.push_back(text("Custom", "自定"));
    int selected = int(presets.size());
    for (std::size_t i = 0; i < presets.size(); ++i) if (presets[i] == value.duration) { selected = int(i); break; }
    auto& control = page.make<d::SegmentedControl>("workMode.preset", text("Duration", "时长"), std::move(options), selected, 300.0, true);
    control.onChange = [this](int index) { pickPreset(index); };
    control.onReselect = [this](int index) { if (index == int(presets.size())) openCustom(); };
    control.layer().setOrigin({145, 331});
    page.layer().add(control.layer());
    page.add(control); page.add(control.segments()); page.stagger(control.layer());
}
// Up to three laps, newest first; the slot is at least 42 px tall (:414-429).
double WorkModeCanvas::buildLaps(d::Page& page) {
    auto& group = page.makeLayer("workMode.laps");
    const d::TextStyle style{12.5, d::FontWeight::regular, env().palette.tone(0xBBBBBB), 1.6, 0, false, true};
    const double height = std::max(42.0, double(laps_.size()) * 20);
    group.setFrame({115, 331, 360, height});
    for (std::size_t index = 0; index < laps_.size(); ++index)
        group.add(d::makeText(page.builder(), "workMode.lap" + std::to_string(index), "#" + pad2(laps_[index].number) + "   " + d::format::clock(laps_[index].seconds),
            style, {0, double(index) * 20, 360, 20}, d::TextAlignment::center));
    page.layer().add(group);
    page.stagger(group);
    return height;
}
void WorkModeCanvas::buildActions(d::Page& page, const WorkModeSnapshot& value, double top) {
    const bool running = value.phase == WorkModePhase::running;
    // Resume names a partly elapsed countdown; a paused stopwatch's Start continues it.
    const bool resumes = value.kind == WorkModeKind::countdown && value.phase == WorkModePhase::paused;
    const auto name = running ? text("Pause", "暂停") : (resumes ? text("Resume", "继续") : text("Start", "开始"));
    auto& primary = page.make<d::Button>("workMode.primary", name, running ? d::Icon::pause : d::Icon::play, d::Button::Kind::primary, d::Button::Height::large);
    primary.place({0, 0}, 150.0);
    primary.label = name + ", " + accessibilityStatus(value);
    primary.onActivate = [this] { primaryAction(); };
    // Countdown: Reset. Stopwatch: Lap while it runs (unavailable before it
    // starts), Reset once paused, so a stopwatch can always be cleared.
    const bool lap = value.kind == WorkModeKind::stopwatch && (running || value.phase == WorkModePhase::idle);
    auto& secondary = page.make<d::Button>(lap ? "workMode.lap" : "workMode.reset", lap ? text("Lap", "计次") : text("Reset", "重置"),
        lap ? d::Icon::plus : d::Icon::reset, d::Button::Kind::secondary, d::Button::Height::large);
    secondary.place({0, 0});
    secondary.setEnabled(!lap || running);
    secondary.onActivate = [this, lap] { if (lap) recordLap(); else reset(); };
    auto& group = page.makeLayer("workMode.actions");
    const double width = primary.layer().bounds().width + 8 + secondary.layer().bounds().width;
    group.setFrame({std::round(295 - width / 2), top, width, 40});
    primary.layer().setOrigin({0, 0});
    secondary.layer().setOrigin({primary.layer().bounds().width + 8, 0});
    group.add(primary.layer()); group.add(secondary.layer());
    page.layer().add(group);
    page.add(std::vector<d::Control*>{&primary, &secondary}); page.stagger(group);
    primary_ = &primary;
}

// --- Actions (:463-553) ---------------------------------------------------------------------
void WorkModeCanvas::chooseKind(WorkModeKind kind, double direction) {
    const auto value = snapshot();
    if (value.active() || value.kind == kind) return;
    apply([&] { if (kind == WorkModeKind::countdown) (void)controller_->chooseCountdown(value.duration, time()); else controller_->chooseStopwatch(time()); });
    laps_.clear(); lapCount_ = 0; editingCustom_ = false;
    navigate(direction);
}
// 5 / 30 / 60 set the total and stop; Custom opens the minutes field.
void WorkModeCanvas::pickPreset(int index) {
    if (index < 0 || index >= int(presets.size())) { openCustom(); return; }
    apply([&] { (void)controller_->chooseCountdown(presets[std::size_t(index)], time()); });
    refresh(true);
}
d::TextField& WorkModeCanvas::customField() {
    if (!customField_) {
        d::TextFieldOptions options;
        options.maximumLength = 7; options.selectsAllOnBegin = true;
        customField_ = &canvasBuilder().make<d::TextField>("workMode.custom", text("Custom countdown: minutes and seconds", "自定义倒计时：分和秒"),
            "", text("Minutes", "分钟"), d::Size{300, 30}, options);
        customField_->onCommit = [this](const std::string& value) { commitCustom(value); };
        customField_->onCancel = [this] { closeCustom(); };
    } else if (!customField_->editing()) {
        customField_->label = text("Custom countdown: minutes and seconds", "自定义倒计时：分和秒");
        customField_->setPlaceholder(text("Minutes", "分钟"));
        customField_->frame().setState(customField_->frame().state());   // the current palette
    }
    return *customField_;
}
void WorkModeCanvas::openCustom() {
    const auto value = snapshot();
    if (value.kind != WorkModeKind::countdown) return;
    const auto duration = std::int64_t(std::round(value.duration));
    auto& field = customField();
    field.setText(duration % 60 == 0 ? std::to_string(duration / 60) : workModeDurationEditText(value.duration));
    field.setInvalid(false);
    editingCustom_ = true;
    rerender();
    field.beginEditing();
}
// Bare minutes or min:sec (0:01-1440:00). An invalid entry shakes and stays
// open; the same duration closes without restarting a session.
void WorkModeCanvas::commitCustom(const std::string& value) {
    if (!editingCustom_) return;
    const auto seconds = parseWorkModeDuration(value);
    if (!seconds) { customField().setInvalid(true, true); return; }
    editingCustom_ = false;
    const auto current = snapshot();
    if (current.kind == WorkModeKind::countdown && current.duration == *seconds) { refresh(false); return; }
    apply([&] { (void)controller_->chooseCountdown(*seconds, time()); });
    refresh(true);
    // toast(icon: .timer, "Custom \(title)", "自定义 \(title)"): the caption is the English sentence (with the UI-language duration).
    const auto duration = durationTitle(*seconds, env().language);
    const std::array<std::string_view, 1> args{duration};
    const bool english = env().language == core::Language::english;
    toast(d::ToastRequest{d::Icon::timer, text("Custom {0}", "自定义 {0}", args),
        english ? std::string() : core::localized("Custom {0}", "自定义 {0}", core::Language::english, args), std::nullopt});
}
void WorkModeCanvas::closeCustom() {
    if (!editingCustom_) return;
    editingCustom_ = false;
    customField().setInvalid(false);
    refresh(false);
}
// Start (a finished countdown restarts from its total), Pause, Resume.
void WorkModeCanvas::primaryAction() {
    const auto value = snapshot();
    apply([&] {
        switch (value.phase) {
        case WorkModePhase::running: controller_->pause(time()); break;
        case WorkModePhase::paused: controller_->resume(time()); break;
        case WorkModePhase::idle: case WorkModePhase::stopped: case WorkModePhase::completed:
            if (value.kind == WorkModeKind::stopwatch) { laps_.clear(); lapCount_ = 0; }
            controller_->start(time());
        }
    });
    refresh(true);
}
void WorkModeCanvas::recordLap() {
    const auto value = snapshot();
    if (value.kind != WorkModeKind::stopwatch || value.phase != WorkModePhase::running) return;
    ++lapCount_;
    laps_.insert(laps_.begin(), Lap{lapCount_, int(std::min<std::int64_t>(value.displayedSeconds(), std::numeric_limits<int>::max()))});
    if (laps_.size() > 3) laps_.resize(3);
    refresh(false);
}
void WorkModeCanvas::reset() {
    apply([&] { controller_->reset(time()); });
    laps_.clear(); lapCount_ = 0;
    refresh(true);
}
void WorkModeCanvas::focusAlertPressed() {
    if (focusNeedsAccess_) { if (onRequestFocusAccess) onRequestFocusAccess(); }
    else if (focusMessage_) toast(d::Icon::warn, "Focus mode", "专注模式", *focusMessage_);
}
// Space or Return / keypad Enter without modifiers: Start, Pause or Resume (:556-560).
bool WorkModeCanvas::handleExtraKey(const d::KeyInput& input) {
    if ((input.virtualKey != d::key::space && input.virtualKey != d::key::enter) || input.commandModifiers() || input.shift) return false;
    primaryAction();
    return true;
}

// --- Testing (:564-567) ---------------------------------------------------------------------
std::vector<std::string> WorkModeCanvas::lapTexts() const {
    std::vector<std::string> out;
    for (const auto& lap : laps_) out.push_back("#" + pad2(lap.number) + "   " + d::format::clock(lap.seconds));
    return out;
}
std::optional<std::string> WorkModeCanvas::timerText() const { return timerText_ ? std::optional(d::textOf(*timerText_).text()) : std::nullopt; }
std::optional<double> WorkModeCanvas::ringFraction() const { return ring_ ? std::optional(ring_->fraction()) : std::nullopt; }
} // namespace endfield::modules
