// Design tokens (Sources/HUDDesignTokens.swift, HUDDesignScreen.swift
// HUDDesignGeometry, HUDRenderScale.swift). Pure value code: built on content
// events (palette, theme, language), never per frame. No global palette.
#include "modules/hud_design_tokens.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

// Swift never fuses multiply-adds; neither does this port, so its numbers
// stay bit-equal with the Mac oracle's.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::modules::design {
namespace {
double unit(double v) noexcept { return std::min(1.0, std::max(0.0, v)); }
// String.trimmingCharacters(in: .whitespacesAndNewlines): ASCII whitespace and
// the Unicode White_Space set (NEL, NBSP, U+1680, U+2000-200A, LS, PS, NNBSP,
// MMSP, U+3000), UTF-8 encoded.
std::size_t spaceAt(std::string_view s, std::size_t at, bool backwards) noexcept {
    const auto u = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    if (!backwards) {
        if (at >= s.size()) return 0;
        const auto c = u(at);
        if (c == ' ' || (c >= '\t' && c <= '\r')) return 1;
        if (c == 0xC2 && at + 1 < s.size() && (u(at + 1) == 0x85 || u(at + 1) == 0xA0)) return 2;
        if (at + 2 < s.size()) {
            const auto b = u(at + 1), d = u(at + 2);
            if ((c == 0xE1 && b == 0x9A && d == 0x80) || (c == 0xE3 && b == 0x80 && d == 0x80)) return 3;
            if (c == 0xE2 && b == 0x80 && (d <= 0x8A || d == 0xA8 || d == 0xA9 || d == 0xAF)) return 3;
            if (c == 0xE2 && b == 0x81 && d == 0x9F) return 3;
        }
        return 0;
    }
    for (std::size_t n = 1; n <= 3 && n <= at; ++n) if (spaceAt(s, at - n, false) == n) return n;
    return 0;
}
// NSColor hue/saturation/brightness of an sRGB colour (a grey has hue 0).
std::array<double, 3> hsb(Color c) noexcept {
    const double high = std::max({c.r, c.g, c.b}), low = std::min({c.r, c.g, c.b}), d = high - low;
    double h = 0;
    if (d > 0) {
        if (high == c.r) h = std::fmod((c.g - c.b) / d, 6.0);
        else if (high == c.g) h = (c.b - c.r) / d + 2;
        else h = (c.r - c.g) / d + 4;
        h /= 6; if (h < 0) h += 1;
    }
    return {h, high > 0 ? d / high : 0, high};
}
}

Color mixed(Color a, Color b, double t) noexcept { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1}; }
Color darkened(Color c, double amount) noexcept { return {c.r * (1 - amount), c.g * (1 - amount), c.b * (1 - amount), c.a}; }
double channelSpread(Color c) noexcept { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); }
// HUDTheme.lightTone (:170-178).
Hex lightTone(Hex hex) noexcept {
    const std::array<double, 3> ch{double((hex >> 16) & 255) / 255, double((hex >> 8) & 255) / 255, double(hex & 255) / 255};
    const double high = std::max({ch[0], ch[1], ch[2]}), low = std::min({ch[0], ch[1], ch[2]});
    if (!(high - low <= .1)) return hex;
    const double v = (ch[0] + ch[1] + ch[2]) / 3, mapped = v < .32 ? 1 - 4.5 * v * v : .52 - (v - .32) * .68;
    Hex result{};
    for (const double c : ch) result = result << 8 | Hex(std::round(unit(c + mapped - v) * 255));
    return result;
}

// HUDAccentPalette.init (:217-218): trim, drop '#', six characters that
// UInt32(_:radix: 16) accepts (an optional sign, hex digits of either case).
Hex normalizedAccent(std::string_view text) noexcept {
    std::size_t begin = 0, end = text.size();
    while (const auto n = spaceAt(text, begin, false)) { if (begin + n > end) break; begin += n; }
    while (end > begin) { const auto n = spaceAt(text, end, true); if (!n || end - n < begin) break; end -= n; }
    std::string raw;
    for (auto c : text.substr(begin, end - begin)) if (c != '#') raw.push_back(c);
    if (raw.size() != 6) return standardAccent;
    std::size_t at = 0; bool negative{};
    if (raw[0] == '+' || raw[0] == '-') { negative = raw[0] == '-'; at = 1; }
    Hex value{};
    for (; at < raw.size(); ++at) {
        const char c = raw[at];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return standardAccent;
        value = value << 4 | Hex(digit);
    }
    return negative && value ? standardAccent : value; // "-00000" is 0; any other negative fails UInt32
}

// HUDAccentPalette.shifted (:237-251).
Color hsbShifted(Color color, double hue, double saturation, double brightness) noexcept {
    const auto [h0, s0, v0] = hsb(color);
    const double h = std::fmod(h0 * 360 + hue + 360, 360) / 60;
    const double s = std::min(1.0, s0 * saturation), v = std::min(1.0, v0 * brightness);
    const double c = v * s, x = c * (1 - std::abs(std::fmod(h, 2) - 1)), m = v - c;
    double r{}, g{}, b{};
    switch (int(h)) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
    }
    return {r + m, g + m, b + m, 1};
}

// HUDAccentPalette.init (:216-233).
AccentPalette AccentPalette::make(std::string_view accentHex, bool light) {
    AccentPalette p;
    p.accent = normalizedAccent(accentHex);
    if (p.accent == standardAccent) {
        p.yellow = rgb(authoredYellow); p.yellowHigh = rgb(authoredYellowHigh);
        p.yellowLow = rgb(authoredYellowLow); p.yellowDeep = rgb(authoredYellowDeep);
    } else {
        const auto base = rgb(p.accent);
        p.yellow = base;
        p.yellowHigh = {base.r + (1 - base.r) * .42, base.g + (1 - base.g) * .42, base.b + (1 - base.b) * .42, 1};
        p.yellowLow = hsbShifted(base, -6.08, .923, .914);
        p.yellowDeep = hsbShifted(base, -4.16, .934, .945);
    }
    p.ink = light ? darkened(p.yellowLow, .42) : p.yellow;
    return p;
}

Palette::Palette() : accent_(AccentPalette::make("FAD41F", false)) {}
// HUDColor.apply(accentHex:light:) (:46-54).
bool Palette::apply(std::string_view accentHex, bool light) {
    auto next = AccentPalette::make(accentHex, light);
    if (next.accent == accent_.accent && light == light_) return false;
    accent_ = next; light_ = light; ++generation_;
    return true;
}
Color Palette::token(ThemeToken t) const noexcept {
    const auto& row = themeTable[std::size_t(t)];
    return rgb(light_ ? row.light : row.dark);
}
Color Palette::tone(Hex hex, double alpha) const noexcept { return rgb(light_ ? lightTone(hex) : hex, alpha); }
Color Palette::foreground(double alpha) const noexcept { return light_ ? withAlpha(token(ThemeToken::foreground), alpha) : white(alpha); }
// HUDColor.legible (:66-74).
Color Palette::legible(Color c) const noexcept {
    if (!light_) return c;
    if (c == accent_.yellow) return accent_.ink;
    for (const auto& s : lightStatusInks) if (c == rgb(s.base)) return rgb(s.ink);
    return channelSpread(c) > .1 ? darkened(c, .4) : c;
}

// HUDType (:271-307).
namespace type {
using W = FontWeight;
namespace {
TextStyle style(double size, W weight, Color color, double lineHeight = 1, double tracking = 0, bool uppercase = false, bool tabular = false) {
    return {size, weight, color, lineHeight, tracking, uppercase, tabular};
}
Color t(const Palette& p, ThemeToken token) { return p.token(token); }
}
TextStyle moduleTitle(const Palette& p) { return style(26, W::bold, t(p, ThemeToken::titleText), 1, .08, true); }
TextStyle titleSubtitle(const Palette& p) { return style(13, W::medium, t(p, ThemeToken::steel), 1, .2); }
TextStyle breadcrumb(const Palette& p) { return style(10.5, W::regular, t(p, ThemeToken::steel), 1, .06, true); }
TextStyle ruleCounter(const Palette& p) { return style(11, W::bold, p.yellow(), 1, 0, false, true); }
TextStyle rowLabel() { return style(15.5, W::medium, fixed::ink, 1.2); }
TextStyle rowSublabel() { return style(11, W::regular, fixed::steel2, 1.2); }
TextStyle button() { return style(12, W::bold, fixed::nearBlack, 1, .08, true); }
TextStyle pill() { return style(14, W::medium, fixed::nearBlack, 1, .06); }
TextStyle toggleLabel(const Palette& p) { return style(10, W::bold, t(p, ThemeToken::muted), 1, .12); }
TextStyle dropdownValue(const Palette& p) { return style(12.5, W::medium, t(p, ThemeToken::textOnDark)); }
TextStyle menuItem(const Palette& p) { return style(12.5, W::regular, t(p, ThemeToken::menuText)); }
TextStyle segmented(const Palette& p) { return style(12.5, W::medium, t(p, ThemeToken::segmentedText)); }
TextStyle tab(const Palette& p) { return style(13, W::medium, t(p, ThemeToken::tabText), 1, .04); }
TextStyle tabKeycap(const Palette& p) { return style(9, W::bold, p.tone(0x8A8A8A)); }
TextStyle chip() { return style(9.5, W::bold, fixed::nearBlack, 1, .1, true); }
TextStyle newBadge() { return style(8, W::black, fixed::nearBlack, 1, .06); }
TextStyle stamp(const Palette& p) { return style(10, W::black, p.accentInk(), 1, .16, true); }
TextStyle numeral(const Palette& p) { return style(64, W::light, t(p, ThemeToken::titleText), .9, -.02, false, true); }
TextStyle numeralUnit(const Palette& p) { return style(64 * .34, W::medium, p.accentInk()); }
TextStyle label(const Palette& p) { return style(10, W::regular, t(p, ThemeToken::steel), 1, .1, true); }
TextStyle labelCJK(const Palette& p) { return style(11.5, W::regular, t(p, ThemeToken::steel), 1, .12); }
TextStyle statValue(const Palette& p) { return style(18, W::regular, p.tone(0xF2F2F2), 1, 0, false, true); }
TextStyle statUnit(const Palette& p) { return style(11, W::regular, t(p, ThemeToken::steel)); }
TextStyle readout() { return style(12, W::bold, fixed::counterBackground, 1, 0, false, true); }
TextStyle readoutBracket() { return readout().colored(fixed::bracket); }
TextStyle sliderBubble() { return style(11, W::bold, fixed::nearBlack, 1, 0, false, true); }
TextStyle field(const Palette& p) { return style(13, W::regular, t(p, ThemeToken::textOnDark)); }
TextStyle emptyTitle(const Palette& p) { return style(20, W::bold, p.tone(0xE9E9E9), 1, .24); }
TextStyle keycap() { return style(11, W::bold, fixed::nearBlack); }
TextStyle toastTitle(const Palette& p) { return style(14, W::bold, p.foreground(1), 1, .22, true); }
TextStyle toastCaption(const Palette& p) { return style(11.5, W::regular, t(p, ThemeToken::caption), 1, .3); }
TextStyle confirmTitle(const Palette& p) { return style(17, W::medium, t(p, ThemeToken::titleText), 1.25); }
TextStyle confirmDescription(const Palette& p) { return style(12, W::regular, t(p, ThemeToken::description), 1.5); }
TextStyle confirmCountdown(const Palette& p) { return style(22, W::bold, p.accentInk(), 1, 0, false, true); }
TextStyle microTag() { return style(5.5, W::bold, fixed::microTagText, 7 / 5.5, .08, true); }
}

// HUDDesignContext.update (:56-58) and HUDRenderScale.contentScale (:7-10).
double boxScale(double windowScale, double unit) noexcept {
    const double s = windowScale * unit;
    return std::isfinite(windowScale) && std::isfinite(s) ? std::min(8.0, std::max(1.0, s)) : 2;
}
double layerContentsScale(double s) noexcept { return std::isfinite(s) ? std::min(4.0, std::max(1.0, s)) : 2; }
double textContentsScale(double s) noexcept { return std::min(4.0, std::ceil(layerContentsScale(s) * 1.35)); }
// HUDArtwork.image / HUDHatchLayer / showArtwork: clamp(ceil(s), 1, 8), non-finite 2.
double artworkScale(double s) noexcept { return std::isfinite(s) ? std::max(1.0, std::min(8.0, std::ceil(s))) : 2; }
} // namespace endfield::modules::design
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
