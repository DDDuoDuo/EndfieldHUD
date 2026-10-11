#include "app/areas/mac_import_card.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::areas {
namespace {
using Json = ehud::data::Json;
using Rect = core::Rect;
using Color = std::array<double, 4>;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
void finite(double t) { need(std::isfinite(t), "The import card needs finite caller time"); }
// CAMediaTimingFunction(controlPoints:) as Core Animation samples it (the
// shared source confirmation port, app/quit_confirmation_state.cpp).
double curve(double t, double cx1, double cy1, double cx2, double cy2) {
    finite(t); if (t <= 0) return 0; if (t >= 1) return 1;
    const double x = static_cast<float>(t), x1 = static_cast<float>(cx1), x2 = static_cast<float>(cx2), y1 = static_cast<float>(cy1), y2 = static_cast<float>(cy2);
    const double ax = 1 - 3 * x2 + 3 * x1, bx = 3 * x2 - 6 * x1, cx = 3 * x1, ay = 1 - 3 * y2 + 3 * y1, by = 3 * y2 - 6 * y1, cy = 3 * y1;
    double p = x;
    for (unsigned n = 0; n < 8; ++n) { const double e = ((ax * p + bx) * p + cx) * p - x; if (std::abs(e) < 1e-5) break; p -= e / ((3 * ax * p + 2 * bx) * p + cx); }
    return static_cast<float>(((ay * p + by) * p + cy) * p);
}
double timing(double t) { return curve(t, .2, .7, .3, 1); }
Color gray(double w, double a = 1) { return {w, w, w, a}; }
Color alpha(Color c, double a) { c[3] = a; return c; }
Json color(Color c) { return Json::Object{{"sRGB", Json::Array{c[0], c[1], c[2], c[3]}}}; }
Json layer(std::string id, Rect r, const char* kind = "layer") {
    return Json::Object{{"id", std::move(id)}, {"kind", kind}, {"bounds", Json::Array{0, 0, r.width, r.height}}, {"position", Json::Array{r.x, r.y}},
                        {"anchorPoint", Json::Array{0, 0}}, {"children", Json::Array{}}};
}
Json command(const char* name, double x, double y) { Json::Array points; points.push_back(Json::Array{x, y}); return Json::Object{{"op", name}, {"points", std::move(points)}}; }
// HUDQuitConfirmationView.cutCorner.
Json shape(std::string id, Rect r, double corner, Color fill, Color stroke, double width) {
    auto node = layer(std::move(id), r, "shape");
    Json::Array path{command("move", corner, 0), command("line", r.width, 0), command("line", r.width, r.height - corner), command("line", r.width - corner, r.height),
                     command("line", 0, r.height), command("line", 0, corner), Json::Object{{"op", "close"}, {"points", Json::Array{}}}};
    node["shape"] = Json::Object{{"path", std::move(path)}, {"fillColor", color(fill)}, {"strokeColor", color(stroke)}, {"lineWidth", width},
                                 {"lineCap", "butt"}, {"lineJoin", "miter"}, {"miterLimit", 10}};
    return node;
}
Json text(std::string id, Rect r, std::string value, double size, Color ink, double density, const char* face, const char* alignment = "left", bool wrapped = false) {
    auto node = layer(std::move(id), r, "text");
    const auto name = std::string_view(face);
    node["contentsScale"] = density;
    node["text"] = Json::Object{{"string", std::move(value)}, {"fontSize", size},
        {"font", Json::Object{{"postScriptName", face}, {"familyName", name.find("Monospaced") != std::string_view::npos ? ".AppleSystemUIFontMonospaced" : ".AppleSystemUIFont"},
                              {"pointSize", size}, {"symbolicTraits", name.find("Monospaced") != std::string_view::npos ? 1026 : name.ends_with("Demi") || name.ends_with("Bold") ? 2 : 0}}},
        {"foregroundColor", color(ink)}, {"alignment", alignment}, {"wrapped", wrapped}, {"truncation", wrapped ? "none" : "end"}};
    return node;
}
// systemRed (the source eraser/alert red in dark and light appearance).
Color alertRed(bool dark) { return dark ? Color{1, 69. / 255., 58. / 255., 1} : Color{1, 59. / 255., 48. / 255., 1}; }
}

MacImportCardGeometry macImportCardGeometry(Rect v) {
    need(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.width) && std::isfinite(v.height) && v.width > 0 && v.height > 0, "Invalid import card viewport");
    // HUDQuitConfirmationView width and button layout; taller for the list.
    const double width = std::min(390., std::max(220., v.width - 40)), button = (width - 58) / 2;
    constexpr double buttonsY = 358, rowTop = 150, rowHeight = 16;
    MacImportCardGeometry g;
    g.card = {v.x + (v.width - width) / 2, v.y + (v.height - macImportCardHeight) / 2, width, macImportCardHeight};
    g.cancel = {23, buttonsY, button, 32};
    g.confirm = {35 + button, buttonsY, button, 32};
    g.list = {19, 146, width - 38, rowHeight * double(macImportCardRows) + 8};
    g.rowTop = rowTop;
    g.rowHeight = rowHeight;
    g.title = {23, 42, width - 46, 28};
    g.message = {23, 74, width - 46, 58};
    g.bar = {23, 138, width - 46, 2};
    g.detail = {g.list.x + 10, g.rowTop + 2, g.list.width - 20, g.list.height - 12};
    g.position = {width / 2, 18, width / 2 - 23, 13};
    return g;
}

MacImportCardState::MacImportCardState() { updateButtons(0, false); }
void MacImportCardState::setAppearance(modules::SettingsAppearance a) {
    for (double c : a.accent) need(std::isfinite(c) && c >= 0 && c <= 1, "Invalid import card accent");
    if (a == appearance_) return;
    appearance_ = a; updateButtons(0, false); changed();
}
void MacImportCardState::setReduceMotion(bool value, double now) {
    finite(now);
    if (reduced_ == value) return;
    reduced_ = value;
    if (value && dismissing_) { shown_ = dismissing_ = false; hovered_.reset(); pressed_.reset(); }
    updateButtons(now, false); changed();
}
void MacImportCardState::setEnabled(std::array<bool, 2> value, double now) {
    finite(now);
    if (value == enabled_) return;
    enabled_ = value;
    if (hovered_ && !enabled_[*hovered_]) hovered_.reset();
    if (pressed_ && !enabled_[*pressed_]) pressed_.reset();
    if (!enabled_[focused_]) focused_ = enabled_[0] ? 0 : enabled_[1] ? 1 : focused_;
    updateButtons(now, false); changed();
}
void MacImportCardState::resetFocus(double now) {
    finite(now);
    const int next = enabled_[0] || !enabled_[1] ? 0 : 1;
    if (next == focused_ && !pressed_) return;
    focused_ = next; pressed_.reset();
    updateButtons(now, false); changed();
}
bool MacImportCardState::focus(int button, double now) {
    finite(now); need(button == 0 || button == 1, "Invalid import card focus");
    if (!shown_ || dismissing_ || !enabled_[static_cast<std::size_t>(button)]) return false;
    if (focused_ != button) { focused_ = button; updateButtons(now, true); changed(); }
    return true;
}
void MacImportCardState::updateButtons(double now, bool animated) {
    animated = animated && !reduced_ && shown_ && buttonsReady_;
    for (unsigned n = 0; n < 2; ++n) {
        const auto previous = buttonPaint(n, now);
        const bool on = enabled_[n];
        const bool engaged = on && (hovered_ == int(n) || pressed_ == int(n)), focus = on && focused_ == int(n);
        const auto rest = gray(static_cast<float>(appearance_.dark ? .9 : .1), on ? .065 : .03);
        const auto targetFill = engaged ? alpha(appearance_.accent, .20) : rest;
        const auto targetStroke = focus || engaged ? alpha(appearance_.accent, .90) : gray(static_cast<float>(appearance_.dark ? .7 : .3), on ? .44 : .2);
        const auto width = focus ? 1.25 : .7;
        auto& b = buttons_[n];
        const auto track = [&](ColorTrack& t, const Color& target, const Color& prior) {
            if (t.to == target && animated) return;
            t.from = animated ? prior : target; t.to = target; t.start = now;
        };
        track(b.fill, targetFill, previous.fill);
        track(b.stroke, targetStroke, previous.stroke);
        if (b.width.to != width || !animated) b.width = {animated ? previous.width : width, width, now};
    }
    buttonsReady_ = true;
}
modules::SettingsSafetyButtonPaint MacImportCardState::buttonPaint(unsigned n, double now) const {
    finite(now); need(n < 2, "Invalid import card button");
    const auto& b = buttons_[n];
    return {modules::settingsSafetyInterpolateColor(b.fill.from, b.fill.to, (now - b.fill.start) / .12),
            modules::settingsSafetyInterpolateColor(b.stroke.from, b.stroke.to, (now - b.stroke.start) / .12),
            b.width.from + (b.width.to - b.width.from) * modules::settingsSafetyFeedbackProgress((now - b.width.start) / .12)};
}
bool MacImportCardState::feedbackAnimating(double now) const {
    finite(now);
    if (!shown_ || reduced_) return false;
    for (const auto& b : buttons_)
        if ((b.fill.from != b.fill.to && now < b.fill.start + .12) || (b.stroke.from != b.stroke.to && now < b.stroke.start + .12) ||
            (b.width.from != b.width.to && now < b.width.start + .12)) return true;
    return false;
}
void MacImportCardState::show(double now) {
    finite(now);
    const bool wasVisible = shown_;
    shown_ = true; dismissing_ = false; pressed_.reset();
    focused_ = enabled_[0] || !enabled_[1] ? 0 : 1;
    // A reveal plays only from hidden; re-showing a fading card restores it.
    start_ = wasVisible || reduced_ ? now - .2 : now;
    updateButtons(now, false); changed();
}
void MacImportCardState::dismiss(bool animated, double now) {
    finite(now);
    if (!shown_) return;
    pressed_.reset();
    if (!animated || reduced_) { shown_ = dismissing_ = false; hovered_.reset(); changed(); return; }
    if (!dismissing_) { dismissing_ = true; start_ = now; }
    changed();
}
void MacImportCardState::hideImmediately(double now) {
    finite(now);
    if (!shown_ && !dismissing_) return;
    shown_ = dismissing_ = false; hovered_.reset(); pressed_.reset(); changed();
}
bool MacImportCardState::refresh(double now) {
    finite(now);
    if (dismissing_ && now >= start_ + .14) { shown_ = dismissing_ = false; hovered_.reset(); changed(); return true; }
    return false;
}
modules::SettingsSafetyPose MacImportCardState::pose(double now) const {
    finite(now);
    if (!shown_) return {};
    if (reduced_) return {1, 0, true, false, false};
    const double elapsed = std::max(0., now - start_);
    if (dismissing_) { const double p = timing(elapsed / .14); return {1 - p, 8 * p, true, true, elapsed < .14}; }
    return {timing(elapsed / .16), 12 * (1 - timing(elapsed / .2)), true, false, elapsed < .2};
}
bool MacImportCardState::requiresFrames(double now) const { return pose(now).animated || feedbackAnimating(now); }
std::optional<MacImportCardAction> MacImportCardState::pointerDown(std::optional<int> a, double now) {
    finite(now);
    if (!shown_) return {};
    if (dismissing_) return MacImportCardAction::none;
    need(!a || *a == 0 || *a == 1, "Invalid import card action");
    if (a && !enabled_[*a]) a.reset();
    pressed_ = a; if (a) focused_ = *a;
    updateButtons(now, true); changed();
    return MacImportCardAction::none;
}
std::optional<MacImportCardAction> MacImportCardState::pointerUp(std::optional<int> a, double now) {
    finite(now);
    if (!shown_) return {};
    const auto pressed = pressed_;
    pressed_.reset(); updateButtons(now, true); changed();
    if (dismissing_ || !pressed || pressed != a || !enabled_[*a]) return MacImportCardAction::none;
    return *a == 0 ? MacImportCardAction::left : MacImportCardAction::right;
}
void MacImportCardState::pointerMove(std::optional<int> a, double now) {
    finite(now); need(!a || *a == 0 || *a == 1, "Invalid import card hover");
    if (a && !enabled_[*a]) a.reset();
    if (!shown_ || hovered_ == a) return;
    hovered_ = a; updateButtons(now, true); changed();
}
std::optional<MacImportCardAction> MacImportCardState::key(unsigned key, bool repeated, double now) {
    finite(now);
    if (!shown_) return {};
    if (dismissing_) return MacImportCardAction::none;
    switch (key) {
    case 0x26: return MacImportCardAction::scrollUp;   // Up
    case 0x28: return MacImportCardAction::scrollDown; // Down
    case 0x21: return MacImportCardAction::pageUp;     // Page Up
    case 0x22: return MacImportCardAction::pageDown;   // Page Down
    default: break;
    }
    if (repeated) return MacImportCardAction::none;
    switch (key) {
    case 0x1B: return MacImportCardAction::escape;     // Escape
    case 0x09:                                         // Tab
        if (enabled_[1 - focused_]) { focused_ = 1 - focused_; updateButtons(now, true); changed(); }
        return MacImportCardAction::none;
    case 0x25: if (enabled_[0]) { focused_ = 0; updateButtons(now, true); changed(); } return MacImportCardAction::none; // Left
    case 0x27: if (enabled_[1]) { focused_ = 1; updateButtons(now, true); changed(); } return MacImportCardAction::none; // Right
    case 0x0D: case 0x20:                              // Return/Enter, Space
        if (!enabled_[focused_]) return MacImportCardAction::none;
        return focused_ == 1 ? MacImportCardAction::right : MacImportCardAction::left;
    default: return MacImportCardAction::none;
    }
}

MacImportArtwork prepareMacImportCardArtwork(const MacImportCardContent& content, const MacImportCardState& s, Rect viewport,
                                             const modules::SettingsAppearance& a, std::string_view prefix, std::optional<double> feedbackTime) {
    need(std::isfinite(a.scale) && a.scale >= 1 && a.scale <= 8, "Invalid import card density");
    for (double c : a.accent) need(std::isfinite(c) && c >= 0 && c <= 1, "Invalid import card accent");
    const auto g = macImportCardGeometry(viewport);
    const double width = g.card.width;
    const auto primary = gray(a.dark ? .95 : .10), secondary = gray(a.dark ? .73 : .32), tertiary = gray(a.dark ? .53 : .44);
    const auto ink = [&](MacImportTone tone) {
        switch (tone) {
        case MacImportTone::primary: return primary;
        case MacImportTone::secondary: return tertiary;
        case MacImportTone::accent: return alpha(a.accent, 1);
        case MacImportTone::alert: return alertRed(a.dark);
        }
        return primary;
    };
    const std::string base(prefix);
    const auto id = [&](std::string_view name) { return base + "." + std::string(name); };
    MacImportArtwork out;
    out.layers = layer(base, {});
    Json::Array children;
    children.reserve(macImportCardLeaves);
    out.leaves.reserve(macImportCardLeaves);
    const auto add = [&](Json node, float opacity = 1, std::optional<double> scaleX = std::nullopt) {
        const auto& p = node["position"].array();
        auto local = core::Matrix4::translation(p[0].number(), p[1].number());
        if (scaleX) local = local * core::Matrix4::scale(*scaleX, 1);
        out.leaves.push_back({node["id"].string(), local, opacity});
        children.push_back(std::move(node));
    };
    add(shape(id("plate"), {.5, .5, width - 1, macImportCardHeight - 1}, 10, gray(a.dark ? .095 : .90, .98), alpha(a.accent, .58), .8));
    auto line = layer(id("line"), {23, 0, 62, 2});
    line["backgroundColor"] = color(alpha(a.accent, .82));
    add(std::move(line));
    for (int n = 0; n < 2; ++n) {
        const auto r = n == 0 ? g.cancel : g.confirm;
        const bool on = s.enabled()[n];
        const bool engaged = on && (s.hovered() == n || s.pressed() == n), focused = on && s.focused() == n;
        const auto paint = feedbackTime ? s.buttonPaint(unsigned(n), *feedbackTime)
            : modules::SettingsSafetyButtonPaint{engaged ? alpha(a.accent, .20) : gray(a.dark ? .9 : .1, on ? .065 : .03),
                                                 focused || engaged ? alpha(a.accent, .90) : gray(a.dark ? .7 : .3, on ? .44 : .2), focused ? 1.25 : .7};
        add(shape(id(n ? "confirmPlate" : "cancelPlate"), {r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 4, paint.fill, paint.stroke, paint.width));
    }
    add(shape(id("list"), g.list, 4, gray(a.dark ? .9 : .1, .035), gray(a.dark ? .7 : .3, .18), .7));
    const Rect bar = g.bar;
    const bool showsProgress = content.progress.has_value();
    auto track = layer(id("track"), bar);
    track["backgroundColor"] = color(gray(a.dark ? .9 : .1, .12));
    add(std::move(track), showsProgress ? 1.f : 0.f);
    auto fill = layer(id("fill"), bar);
    fill["backgroundColor"] = color(alpha(a.accent, .82));
    const double progress = showsProgress ? std::clamp(*content.progress, 0.0, 1.0) : 0.0;
    // The fill is one retained surface scaled on the GPU: progress never
    // re-rasterizes it.
    add(std::move(fill), showsProgress && progress > 0 ? 1.f : 0.f, std::max(progress, 1e-3));
    add(text(id("caption"), {23, 18, width - 46, 13}, "ENDFIELDHUD / SYSTEM", 8, tertiary, a.scale, ".AppleSystemUIFontMonospaced-Semibold"));
    add(text(id("position"), g.position, content.position, 8, tertiary, a.scale, ".AppleSystemUIFontMonospaced-Semibold", "right"));
    add(text(id("title"), g.title, content.title, 18, primary, a.scale, ".AppleSystemUIFontBold"));
    add(text(id("message"), g.message, content.message, 12, secondary, a.scale, ".AppleSystemUIFontMedium", "left", true));
    add(text(id("detail"), g.detail, content.detail, 11, secondary, a.scale,
             ".AppleSystemUIFontMedium", "left", true), content.detail.empty() ? 0.f : 1.f);
    for (int n = 0; n < 2; ++n) {
        const auto r = n == 0 ? g.cancel : g.confirm;
        add(text(id(n ? "confirm" : "cancel"), {r.x, r.y + 8.5, r.width, 15}, content.buttons[n], 12, s.enabled()[n] ? primary : alpha(primary, .35), a.scale,
                 ".AppleSystemUIFontDemi", "center"));
    }
    const double split = std::round(width * .56);
    for (std::size_t n = 0; n < macImportCardRows; ++n) {
        const auto& row = content.rows[n];
        const double y = g.rowTop + g.rowHeight * double(n);
        const auto suffix = std::to_string(n);
        const float shown = content.detail.empty() ? 1.f : 0.f;
        add(text(id("row" + suffix), {29, y, split - 33, 14}, row.label, 11, primary, a.scale, ".AppleSystemUIFontMedium"), shown);
        add(text(id("value" + suffix), {split, y, width - 29 - split, 14}, row.value, 11, ink(row.tone), a.scale, ".AppleSystemUIFontDemi", "right"), shown);
    }
    need(out.leaves.size() == macImportCardLeaves, "Unexpected import card leaf count");
    out.layers["children"] = std::move(children);
    return out;
}
} // namespace endfield::areas
