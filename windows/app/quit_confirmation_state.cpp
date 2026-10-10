#include "app/quit_confirmation_state.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::app {
namespace {
namespace m = modules;
using Json = ehud::data::Json;
using Rect = core::Rect;
using Color = std::array<double, 4>;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
void finite(double t) { need(std::isfinite(t), "Quit confirmation needs finite caller time"); }
// CAMediaTimingFunction(controlPoints:) evaluated as Core Animation does (the
// same sampled Float cubic used by the shared source confirmation port).
double curve(double t, double cx1, double cy1, double cx2, double cy2) {
    finite(t); if (t <= 0) return 0; if (t >= 1) return 1;
    const double x = static_cast<float>(t), x1 = static_cast<float>(cx1), x2 = static_cast<float>(cx2), y1 = static_cast<float>(cy1), y2 = static_cast<float>(cy2);
    const double ax = 1 - 3 * x2 + 3 * x1, bx = 3 * x2 - 6 * x1, cx = 3 * x1, ay = 1 - 3 * y2 + 3 * y1, by = 3 * y2 - 6 * y1, cy = 3 * y1;
    double p = x;
    for (unsigned n = 0; n < 8; ++n) { const double e = ((ax * p + bx) * p + cx) * p - x; if (std::abs(e) < 1e-5) break; p -= e / ((3 * ax * p + 2 * bx) * p + cx); }
    return static_cast<float>(((ay * p + by) * p + cy) * p);
}
double timing(double t) { return curve(t, .2, .7, .3, 1); } // animate(...) controlPoints 0.2, 0.7, 0.3, 1
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
}

QuitConfirmationStrings quitConfirmationStrings(core::Language language) {
    const auto t = [language](std::string_view en, std::string_view zh) { return core::localized(en, zh, language); };
    return {t("Quit EndfieldHUD?", "退出 EndfieldHUD？"), t("The application will quit after the HUD closes.", "界面收起后，将退出应用。"),
            t("Cancel", "取消"), t("Quit", "退出"), t("Cancel quitting EndfieldHUD", "取消退出 EndfieldHUD"), t("Quit EndfieldHUD application", "退出 EndfieldHUD 应用")};
}

QuitConfirmationState::QuitConfirmationState() : strings_(quitConfirmationStrings(core::Language::english)) { updateButtons(0, false); }
void QuitConfirmationState::setLanguage(core::Language language) {
    if (language == core::Language::system) language = core::Language::english;
    if (language == language_ && !strings_.title.empty()) return;
    language_ = language;
    auto next = quitConfirmationStrings(language);
    if (next != strings_) { strings_ = std::move(next); changed(); }
}
void QuitConfirmationState::setAppearance(modules::SettingsAppearance a) {
    for (double c : a.accent) need(std::isfinite(c) && c >= 0 && c <= 1, "Invalid quit confirmation accent");
    if (a == appearance_) return;
    appearance_ = a; updateButtons(0, false); changed();
}
void QuitConfirmationState::setReduceMotion(bool value, double now) {
    finite(now);
    if (reduced_ == value) return;
    reduced_ = value;
    if (value && dismissing_) { shown_ = dismissing_ = false; hovered_.reset(); pressed_.reset(); }
    updateButtons(now, false); changed();
}
void QuitConfirmationState::updateButtons(double now, bool animated) {
    animated = animated && !reduced_ && shown_ && buttonsReady_;
    for (unsigned n = 0; n < 2; ++n) {
        const auto previous = buttonPaint(n, now);
        const bool engaged = hovered_ == int(n) || pressed_ == int(n), focus = focused_ == int(n);
        const auto targetFill = engaged ? alpha(appearance_.accent, .20) : gray(static_cast<float>(appearance_.dark ? .9 : .1), .065);
        const auto targetStroke = focus || engaged ? alpha(appearance_.accent, .90) : gray(static_cast<float>(appearance_.dark ? .7 : .3), .44);
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
modules::SettingsSafetyButtonPaint QuitConfirmationState::buttonPaint(unsigned n, double now) const {
    finite(now); need(n < 2, "Invalid quit confirmation button");
    const auto& b = buttons_[n];
    return {modules::settingsSafetyInterpolateColor(b.fill.from, b.fill.to, (now - b.fill.start) / .12),
            modules::settingsSafetyInterpolateColor(b.stroke.from, b.stroke.to, (now - b.stroke.start) / .12),
            b.width.from + (b.width.to - b.width.from) * modules::settingsSafetyFeedbackProgress((now - b.width.start) / .12)};
}
bool QuitConfirmationState::feedbackAnimating(double now) const {
    finite(now);
    if (!shown_ || reduced_) return false;
    for (const auto& b : buttons_)
        if ((b.fill.from != b.fill.to && now < b.fill.start + .12) || (b.stroke.from != b.stroke.to && now < b.stroke.start + .12) ||
            (b.width.from != b.width.to && now < b.width.start + .12)) return true;
    return false;
}
void QuitConfirmationState::show(double now) {
    finite(now);
    const bool wasVisible = shown_;
    shown_ = true; dismissing_ = submitted_ = false; focused_ = 0; pressed_.reset();
    // A reveal plays only from hidden; re-showing a fading card restores it.
    start_ = wasVisible || reduced_ ? now - .2 : now;
    updateButtons(now, false); changed();
}
void QuitConfirmationState::dismiss(bool animated, double now) {
    finite(now);
    if (!shown_) return;
    submitted_ = true; pressed_.reset();
    if (!animated || reduced_) { shown_ = dismissing_ = false; hovered_.reset(); changed(); return; }
    if (!dismissing_) { dismissing_ = true; start_ = now; }
    changed();
}
void QuitConfirmationState::hideImmediately(double now) {
    finite(now);
    if (!shown_ && !dismissing_) return;
    shown_ = dismissing_ = submitted_ = false; hovered_.reset(); pressed_.reset(); changed();
}
bool QuitConfirmationState::refresh(double now) {
    finite(now);
    if (dismissing_ && now >= start_ + .14) { shown_ = dismissing_ = false; hovered_.reset(); changed(); return true; }
    return false;
}
modules::SettingsSafetyPose QuitConfirmationState::pose(double now) const {
    finite(now);
    if (!shown_) return {};
    if (reduced_) return {1, 0, true, false, false};
    const double elapsed = std::max(0., now - start_);
    if (dismissing_) { const double p = timing(elapsed / .14); return {1 - p, 8 * p, true, true, elapsed < .14}; }
    return {timing(elapsed / .16), 12 * (1 - timing(elapsed / .2)), true, false, elapsed < .2};
}
bool QuitConfirmationState::requiresFrames(double now) const { return pose(now).animated || feedbackAnimating(now); }
std::optional<QuitAnswer> QuitConfirmationState::action(bool confirm) {
    if (!shown_ || dismissing_ || submitted_) return QuitAnswer::none;
    submitted_ = true; changed();
    return confirm ? QuitAnswer::confirm : QuitAnswer::cancel;
}
std::optional<QuitAnswer> QuitConfirmationState::pointerDown(std::optional<int> a, double now) {
    finite(now);
    if (!shown_) return {};
    if (dismissing_ || submitted_) return QuitAnswer::none;
    need(!a || *a == 0 || *a == 1, "Invalid quit confirmation action");
    pressed_ = a; if (a) focused_ = *a;
    updateButtons(now, true); changed();
    return QuitAnswer::none;
}
std::optional<QuitAnswer> QuitConfirmationState::pointerUp(std::optional<int> a, double now) {
    finite(now);
    if (!shown_) return {};
    const auto pressed = pressed_;
    pressed_.reset(); updateButtons(now, true); changed();
    if (!pressed || pressed != a) return QuitAnswer::none;
    return action(*a == 1);
}
void QuitConfirmationState::pointerMove(std::optional<int> a, double now) {
    finite(now); need(!a || *a == 0 || *a == 1, "Invalid quit confirmation hover");
    if (!shown_ || hovered_ == a) return;
    hovered_ = a; updateButtons(now, true); changed();
}
std::optional<QuitAnswer> QuitConfirmationState::key(unsigned key, bool repeated, double now) {
    finite(now);
    if (!shown_) return {};
    if (dismissing_ || submitted_ || repeated) return QuitAnswer::none;
    switch (key) {
    case 0x1B: return action(false);                                  // Escape
    case 0x09: focused_ = 1 - focused_; updateButtons(now, true); changed(); return QuitAnswer::none; // Tab
    case 0x25: focused_ = 0; updateButtons(now, true); changed(); return QuitAnswer::none;            // Left
    case 0x27: focused_ = 1; updateButtons(now, true); changed(); return QuitAnswer::none;            // Right
    case 0x0D: case 0x20: return action(focused_ == 1);               // Return/Enter, Space
    default: return QuitAnswer::none;
    }
}

modules::SettingsArtworkPart prepareQuitConfirmationArtwork(const QuitConfirmationState& s, Rect viewport, const modules::SettingsAppearance& a, std::optional<double> feedbackTime) {
    need(std::isfinite(a.scale) && a.scale >= 1 && a.scale <= 8, "Invalid quit confirmation density");
    for (double c : a.accent) need(std::isfinite(c) && c >= 0 && c <= 1, "Invalid quit confirmation accent");
    const auto geometry = modules::SettingsSafety::geometry(viewport);
    const double width = geometry.card.width;
    const auto primary = gray(a.dark ? .95 : .10);
    modules::SettingsArtworkPart part;
    part.layers = layer("quit.confirmation", {});
    Json::Array children;
    const auto add = [&](Json node) {
        const auto& p = node["position"].array();
        part.surfaces.push_back({node["id"].string(), {}, core::Matrix4::translation(p[0].number(), p[1].number()), false, false, 1});
        children.push_back(std::move(node));
    };
    add(shape("quit.confirmation.plate", {.5, .5, width - 1, 189}, 10, gray(a.dark ? .095 : .90, .98), alpha(a.accent, .58), .8));
    auto line = layer("quit.confirmation.line", {23, 0, 62, 2});
    line["backgroundColor"] = color(alpha(a.accent, .82));
    add(std::move(line));
    for (int n = 0; n < 2; ++n) {
        const auto r = n == 0 ? geometry.cancel : geometry.confirm;
        const bool engaged = s.hovered() == n || s.pressed() == n, focused = s.focused() == n;
        const auto paint = feedbackTime ? s.buttonPaint(unsigned(n), *feedbackTime)
            : modules::SettingsSafetyButtonPaint{engaged ? alpha(a.accent, .20) : gray(a.dark ? .9 : .1, .065), focused || engaged ? alpha(a.accent, .90) : gray(a.dark ? .7 : .3, .44), focused ? 1.25 : .7};
        add(shape(n ? "quit.confirmation.confirmPlate" : "quit.confirmation.cancelPlate", {r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 4, paint.fill, paint.stroke, paint.width));
    }
    add(text("quit.confirmation.title", {23, 42, width - 46, 28}, s.strings().title, 18, primary, a.scale, ".AppleSystemUIFontBold"));
    add(text("quit.confirmation.message", {23, 78, width - 46, 42}, s.strings().message, 12, gray(a.dark ? .73 : .32), a.scale, ".AppleSystemUIFontMedium", "left", true));
    add(text("quit.confirmation.caption", {23, 18, width - 46, 13}, "ENDFIELDHUD / SYSTEM", 8, gray(a.dark ? .53 : .44), a.scale, ".AppleSystemUIFontMonospaced-Semibold"));
    for (int n = 0; n < 2; ++n) {
        const auto r = n == 0 ? geometry.cancel : geometry.confirm;
        add(text(n ? "quit.confirmation.confirm" : "quit.confirmation.cancel", {r.x, r.y + 8.5, r.width, 15}, n ? s.strings().confirm : s.strings().cancel, 12, primary, a.scale, ".AppleSystemUIFontDemi", "center"));
    }
    part.layers["children"] = std::move(children);
    return part;
}
} // namespace endfield::app
