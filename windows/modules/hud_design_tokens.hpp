#pragma once
// Design tokens of the centre redesign: the port of Sources/HUDDesignTokens.swift
// (HUDColor, HUDTheme, HUDAccentPalette, HUDTextStyle, HUDType, HUDMetric,
// HUDLayerOrder, HUDEasing, HUDDuration), the box geometry of
// Sources/HUDDesignScreen.swift (HUDDesignGeometry) and the render-scale rule
// of Sources/HUDRenderScale.swift. Library item ids: LIB-1..6, LIB-70, LIB-73,
// LIB-97 (build/windows-1.3/inventory-LIB.md).
//
// Colours are sRGB with straight alpha, built on content events (palette,
// theme or language change) and never per frame. The Mac keeps the palette in
// statics (HUDColor.palette/theme/paletteGeneration); here one Palette object
// lives in the shared design::Context (hud_design_page.hpp) and every builder
// receives it explicitly: no global, no cached palette colour outside it
// (ROUND2 owner rule 3: modules never write the yellow literals FFFA00/FFFC6B/
// E9CF12 themselves; semantic status colours stay fixed).
//
// Deliberately not ported as behaviour: HUDDuration.perspective (.5) and
// HUDDuration.scaleConfirmation (15) are referenced nowhere on the Mac; the
// shipped "Keep this UI scale?" countdown stays 12 s
// (modules::SettingsController::confirmationDuration, LIB-6).
#include "core/motion.hpp"
#include "core/scene.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace endfield::modules::design {
using Hex = std::uint32_t;
using core::Point;
using core::Rect;
struct Size {
    double width{}, height{};
    bool operator==(const Size&) const = default;
};
// sRGB components 0...1, straight alpha (CGColor(srgbRed:green:blue:alpha:)).
struct Color {
    double r{}, g{}, b{}, a{1};
    bool operator==(const Color&) const = default;
};
constexpr Color rgb(Hex hex, double alpha = 1) noexcept {
    return {double((hex >> 16) & 255) / 255, double((hex >> 8) & 255) / 255, double(hex & 255) / 255, alpha};
}
constexpr Color white(double alpha) noexcept { return {1, 1, 1, alpha}; }
constexpr Color black(double alpha) noexcept { return {0, 0, 0, alpha}; }
inline constexpr Color clear{0, 0, 0, 0};
constexpr Color withAlpha(Color c, double alpha) noexcept { c.a = alpha; return c; }
// HUDTheme.mixed: `a` toward `b` by `amount` in sRGB, alpha 1.
Color mixed(Color a, Color b, double amount) noexcept;
// HUDTheme.darkened: every channel x (1 - amount), alpha kept.
Color darkened(Color, double amount) noexcept;
// Channel spread (max - min) of the sRGB components: > .1 is "tinted".
double channelSpread(Color) noexcept;
// HUDTheme.lightTone: a neutral (spread <= .1) maps to its light counterpart:
// v = mean; v < .32 -> 1 - 4.5 v^2, else .52 - (v - .32) x .68; each channel
// clamp(c + mapped - v) rounded to 8 bit. Tinted colours pass unchanged.
Hex lightTone(Hex) noexcept;

// ---------------------------------------------------------------------------
// Accent family (HUDAccentPalette, HUDDesignTokens.swift:203-252, LIB-1)
// ---------------------------------------------------------------------------
inline constexpr Hex standardAccent = 0xFAD41F;
// AppConfiguration.accentHex normalisation (:217-218): trim whitespace and
// newlines, drop every '#', exactly six hex digits (either case) else
// standardAccent. Same rule as modules::ChargeAlertSettings::accentHex.
Hex normalizedAccent(std::string_view accentHex) noexcept;
// String.trimmingCharacters(in: .whitespacesAndNewlines) on UTF-8 (the same
// White_Space set normalizedAccent trims): Notes' task titles.
std::string trimmedWhitespace(std::string_view);
// The HSB shift of HUDAccentPalette.shifted (:237-251): NSColor hue/
// saturation/brightness (grey hue 0), h' = ((h x 360 + hue + 360) mod 360)/60,
// s' = min(1, s x saturation), v' = min(1, v x brightness), textbook HSV->RGB
// with sector Int(h') 0...4 and the default branch for 5.
Color hsbShifted(Color, double hueDegrees, double saturation, double brightness) noexcept;
struct AccentPalette {
    Hex accent{standardAccent};
    Color yellow, yellowHigh, yellowLow, yellowDeep;
    // HUDColor.accentInk: yellow in dark; in Light yellowLow x .58 (darkened .42).
    Color ink;
    // The primary button gradient top to bottom: high, yellow, low (:231).
    std::array<Color, 3> primaryButton() const noexcept { return {yellowHigh, yellow, yellowLow}; }
    // The default FAD41F keeps its AUTHORED stops (never computed): FFFA00 /
    // FFFC6B / E9CF12 / F1DD10; running the formula on FAD41F would give
    // FAD41F / FCE67D / E4B22C / ECBD2B, which is not what the Mac shows. Any
    // other accent: yellow = accent, high = 42 % toward white, low =
    // hsbShifted(-6.08, .923, .914), deep = hsbShifted(-4.16, .934, .945).
    // Test vectors (inventory LIB-1): 22A6F2 -> 7FCBF7 / 2EAFDD / 2DAEE5, light
    // ink 1B6580; E5383B -> F08C8D / D13F51 / D8404D, ink 79252F; 8CE03C ->
    // BCED8E / 94CD42 / 94D443, ink 567727; default Light ink 87780A (inks
    // from the unrounded low stop; the inventory's 1B6680 / 567726 rounded it first).
    static AccentPalette make(std::string_view accentHex, bool light);
    bool operator==(const AccentPalette&) const = default;
};
inline constexpr Hex authoredYellow = 0xFFFA00, authoredYellowHigh = 0xFFFC6B, authoredYellowLow = 0xE9CF12, authoredYellowDeep = 0xF1DD10;

// ---------------------------------------------------------------------------
// Theme surface family (HUDTheme, :137-196, LIB-2). Dark is the redesign as
// designed; Light maps each neutral through lightTone unless an explicit light
// value is given (secondaryButton, secondaryButtonHover, danger). The light
// column below is the Mac result (verified with lightTone); the contract test
// recomputes it and checks the explicit three.
// ---------------------------------------------------------------------------
enum class ThemeToken : std::uint8_t {
    foreground, panel, field, control, toggleTrack, tabBar, iconButton, secondaryButton, secondaryButtonHover,
    ringField, ringTab, ringToggle, ringIcon, ringMenu, separator, titleText, crumbCurrent, textOnDark, tabText,
    menuText, segmentedText, muted, caption, description, steel, danger, count
};
struct ThemeHex { ThemeToken token; Hex dark, light; bool explicitLight{}; };
inline constexpr std::array<ThemeHex, std::size_t(ThemeToken::count)> themeTable{{
    {ThemeToken::foreground, 0xFFFFFF, 0x0F0F0F}, {ThemeToken::panel, 0x1A1A1A, 0xF3F3F3},
    {ThemeToken::field, 0x1D1D1D, 0xF0F0F0}, {ThemeToken::control, 0x212121, 0xECECEC},
    {ThemeToken::toggleTrack, 0x202020, 0xEDEDED}, {ThemeToken::tabBar, 0x1E1E1E, 0xEFEFEF},
    {ThemeToken::iconButton, 0x2B2B2B, 0xDEDEDE}, {ThemeToken::secondaryButton, 0x383838, 0xDEDEDA, true},
    {ThemeToken::secondaryButtonHover, 0x484848, 0xCDCDC9, true}, {ThemeToken::ringField, 0x3A3A3A, 0xC4C4C4},
    {ThemeToken::ringTab, 0x343434, 0xCFCFCF}, {ThemeToken::ringToggle, 0x393939, 0xC6C6C6},
    {ThemeToken::ringIcon, 0x444444, 0xADADAD}, {ThemeToken::ringMenu, 0x3D3D3D, 0xBDBDBD},
    {ThemeToken::separator, 0x333333, 0xD1D1D1}, {ThemeToken::titleText, 0xF4F4F4, 0x161616},
    {ThemeToken::crumbCurrent, 0xE6E6E6, 0x202020}, {ThemeToken::textOnDark, 0xEEEEEE, 0x1A1A1A},
    {ThemeToken::tabText, 0xD8D8D8, 0x292929}, {ThemeToken::menuText, 0xD9D9D9, 0x292929},
    {ThemeToken::segmentedText, 0xCCCCCC, 0x313131}, {ThemeToken::muted, 0x777777, 0x6B6B6B},
    {ThemeToken::caption, 0x9A9A9A, 0x535353}, {ThemeToken::description, 0xA9A9A9, 0x494949},
    {ThemeToken::steel, 0x8A8A8A, 0x5E5E5E}, {ThemeToken::danger, 0xFF8A8C, 0xC62828, true}}};
// statusInks (Light only, :163-164): the status colour as text or a thin
// line on a light surface (HUDColor.legible).
struct StatusInk { Hex base, ink; };
inline constexpr std::array<StatusInk, 4> lightStatusInks{{{0x8CE03C, 0x3F8F14}, {0x22A6F2, 0x0B7DC0}, {0xF97501, 0xD05A00}, {0xE5383B, 0xC8282C}}};
// Library greys drawn through HUDColor.tone (dark hex -> Light value), the
// values the library itself uses (LIB-2). Modules call Palette::tone with any
// neutral; this table pins the expected results for the contract test.
inline constexpr std::array<std::pair<Hex, Hex>, 25> libraryTones{{
    {0x8A8A8A, 0x5E5E5E}, {0xF2F2F2, 0x181818}, {0xE9E9E9, 0x1E1E1E}, {0xDDDDDD, 0x262626}, {0x111111, 0xFAFAFA},
    {0x888888, 0x606060}, {0xECECEC, 0x1C1C1C}, {0x161616, 0xF6F6F6}, {0x2A2A2A, 0xE0E0E0}, {0xE2E0E1, 0x242223},
    {0x1D1D1D, 0xF0F0F0}, {0x4A4A4A, 0x9E9E9E}, {0xEDEDED, 0x1B1B1B}, {0x1B1B1B, 0xF2F2F2}, {0x606060, 0x7B7B7B},
    {0x181818, 0xF5F5F5}, {0x000000, 0xFFFFFF}, {0xCFCFCF, 0x2F2F2F}, {0x5A5A5A, 0x7F7F7F}, {0x222222, 0xEBEBEB},
    {0x232323, 0xE9E9E9}, {0x6A6A6A, 0x747474}, {0x4C4C4C, 0x999999}, {0x2C2C2C, 0xDDDDDD}, {0x262414, 0xF4F2E2}}};

// Fixed colours (:76-128), identical in both themes: plates, the ink on them,
// small dark tags and the semantic statuses never follow Theme.
namespace fixed {
inline constexpr Color ink = rgb(0x121212), charcoal = rgb(0x262626), steel2 = rgb(0x6C6D68);
inline constexpr Color plate = rgb(0xD3D4CF), plateHigh = rgb(0xE4E5E0), plateLow = rgb(0xB3B4AF);
inline constexpr Color cyan = rgb(0x22A6F2), orange = rgb(0xF97501), red = rgb(0xE5383B), green = rgb(0x8CE03C);
inline constexpr std::array<Color, 3> triStrip{rgb(0xFF1AAC), rgb(0x00FFA2), rgb(0xFFFA00)}; // fixed, NOT the accent
inline constexpr Color nearBlack = rgb(0x111111), counterBackground = rgb(0x151515), microTagBlack = rgb(0x161616);
inline constexpr Color microTagText = rgb(0xE9E9E9), onDarkTag = rgb(0xEEEEEE), notch = rgb(0x2A2A2A);
inline constexpr std::array<Color, 3> rowPlate{rgb(0xE4E5E0), rgb(0xD3D4CF), rgb(0xC6C7C2)};
inline constexpr std::array<Color, 2> lightButton{rgb(0xECEDE8), rgb(0xCDCEC9)}, heroPlate{rgb(0xE7E8E3), rgb(0xD1D2CD)};
inline constexpr Color bracket = rgb(0x8A8B86), sliderTrack = rgb(0x252525), sliderTick = rgb(0x8D8E89), knobOff = rgb(0x8E8F8A);
}

// The palette in use (HUDColor's statics). One per design::Context; apply()
// returns false when the normalised accent and the Light flag are unchanged
// (no work), otherwise swaps both and increments generation() (wrapping):
// canvases compare it to rebuild lazily (LIB-77). The Mac also drops its
// HUDArtwork raster cache here; on Windows the generation change makes every
// part's JSON differ, so the native scene retires the old rasters itself.
class Palette final {
public:
    Palette();
    bool apply(std::string_view accentHex, bool light);
    std::uint64_t generation() const noexcept { return generation_; }
    bool isLight() const noexcept { return light_; }
    const AccentPalette& accent() const noexcept { return accent_; }
    Color token(ThemeToken) const noexcept;
    Color yellow() const noexcept { return accent_.yellow; }
    Color yellowHigh() const noexcept { return accent_.yellowHigh; }
    Color yellowLow() const noexcept { return accent_.yellowLow; }
    Color yellowDeep() const noexcept { return accent_.yellowDeep; }
    Color accentInk() const noexcept { return accent_.ink; }
    // HUDColor.tone: `hex` in dark, lightTone(hex) in Light, for greys drawn on
    // theme surfaces (plates, photos and fixed dark tags keep rgb()).
    Color tone(Hex, double alpha = 1) const noexcept;
    // HUDColor.foreground: white(alpha) in dark, the Light foreground 0F0F0F.
    Color foreground(double alpha) const noexcept;
    // HUDColor.legible (:66-74): dark unchanged; Light: the palette yellow ->
    // ink, a status colour -> its status ink, any other colour with spread
    // > .1 darkened .4, neutrals pass.
    Color legible(Color) const noexcept;
private:
    AccentPalette accent_;
    bool light_{};
    std::uint64_t generation_{};
};

// ---------------------------------------------------------------------------
// Type scale (HUDTextStyle / HUDType, :256-307, LIB-4; weights LIB-F12)
// ---------------------------------------------------------------------------
// HUDFontWeight: 600 renders as Bold (resolved): the demo's CSS 600 has no
// face and browsers render Bold. The rasterizer receives resolved weights only.
enum class FontWeight : std::uint16_t { light = 300, regular = 400, medium = 500, semibold = 600, bold = 700, black = 900 };
constexpr FontWeight resolved(FontWeight w) noexcept { return w == FontWeight::semibold ? FontWeight::bold : w; }
// Sizes are content-box px, tracking em, lineHeight a multiple of size.
struct TextStyle {
    double size{12};
    FontWeight weight{FontWeight::regular};
    Color color;
    double lineHeight{1}, tracking{};
    bool uppercase{}, tabular{};
    TextStyle sized(double v) const { auto c = *this; c.size = v; return c; }
    TextStyle colored(Color v) const { auto c = *this; c.color = v; return c; }
    TextStyle weighted(FontWeight v) const { auto c = *this; c.weight = v; return c; }
    bool operator==(const TextStyle&) const = default;
};
// Styles whose colour follows Theme take the palette (the Mac's computed
// properties); fixed ones do not need it but take it for one calling shape.
namespace type {
TextStyle moduleTitle(const Palette&);   // 26 bold titleText, tracking .08, uppercase
TextStyle titleSubtitle(const Palette&); // 13 medium steel, tracking .2
TextStyle breadcrumb(const Palette&);    // 10.5 regular steel, tracking .06, uppercase
TextStyle ruleCounter(const Palette&);   // 11 bold yellow (the fill), tabular
TextStyle rowLabel();                    // 15.5 medium ink 121212, lh 1.2
TextStyle rowSublabel();                 // 11 regular steel2, lh 1.2
TextStyle button();                      // 12 bold nearBlack, tracking .08, uppercase
TextStyle pill();                        // 14 medium nearBlack, tracking .06
TextStyle toggleLabel(const Palette&);   // 10 bold muted, tracking .12
TextStyle dropdownValue(const Palette&); // 12.5 medium textOnDark
TextStyle menuItem(const Palette&);      // 12.5 regular menuText
TextStyle segmented(const Palette&);     // 12.5 medium segmentedText
TextStyle tab(const Palette&);           // 13 medium tabText, tracking .04
TextStyle tabKeycap(const Palette&);     // 9 bold tone(8A8A8A)
TextStyle chip();                        // 9.5 bold nearBlack, tracking .1, uppercase
TextStyle newBadge();                    // 8 black nearBlack, tracking .06
TextStyle stamp(const Palette&);         // 10 black accentInk, tracking .16, uppercase
TextStyle numeral(const Palette&);       // 64 light titleText, lh .9, tracking -.02, tabular
TextStyle numeralUnit(const Palette&);   // 64 x .34 medium accentInk
TextStyle label(const Palette&);         // 10 regular steel, tracking .1, uppercase
TextStyle labelCJK(const Palette&);      // 11.5 regular steel, tracking .12
TextStyle statValue(const Palette&);     // 18 regular tone(F2F2F2), tabular
TextStyle statUnit(const Palette&);      // 11 regular steel
TextStyle readout();                     // 12 bold counterBackground, tabular
TextStyle readoutBracket();              // readout in bracket 8A8B86
TextStyle sliderBubble();                // 11 bold nearBlack, tabular
TextStyle field(const Palette&);         // 13 regular textOnDark
TextStyle emptyTitle(const Palette&);    // 20 bold tone(E9E9E9), tracking .24
TextStyle keycap();                      // 11 bold nearBlack
TextStyle toastTitle(const Palette&);    // 14 bold foreground(1), tracking .22, uppercase
TextStyle toastCaption(const Palette&);  // 11.5 regular caption, tracking .3
TextStyle confirmTitle(const Palette&);  // 17 medium titleText, lh 1.25
TextStyle confirmDescription(const Palette&); // 12 regular description, lh 1.5
TextStyle confirmCountdown(const Palette&);   // 22 bold accentInk, tabular
TextStyle microTag();                    // 5.5 bold microTagText, lh 7/5.5, tracking .08, uppercase
}

// Shared geometry (HUDMetric, :311-323, LIB-5), content-box px.
namespace metric {
inline constexpr double boxWidth = 590, boxHeight = 510;
inline constexpr double headerHeight = 81; // breadcrumb 14 + 6 + title row 30 + 10 + rule 9 + 12
inline constexpr double bodyHeight = boxHeight - headerHeight;
inline constexpr double rowHeight = 50, compactRowHeight = 44, rowGap = 6, rowChamfer = 11, buttonChamfer = 9;
inline constexpr double sliderLabelColumn = 170;
inline constexpr core::Rect box{0, 0, boxWidth, boxHeight};
}
// Stacking (HUDLayerOrder, :326-335). Plane order: content 0, dim 15, confirm
// bar 20, toast 25, boot log 30; inside the content box: menus and pop-overs
// 30, the slider bubble 40.
namespace order {
inline constexpr double content = 0, dim = 15, confirmBar = 20, toast = 25, bootLog = 30, menu = 30, bubble = 40;
}
// Easings (HUDEasing, :338-348): the ONE definition is core::DesignEasing
// (core/motion.hpp, shared with the module switch); these are aliases.
namespace easing {
inline constexpr core::CubicTiming standard = core::DesignEasing::standard; // "E", the default motion
inline constexpr core::CubicTiming snap = core::DesignEasing::snap;         // "SNAP": toggle knob, confirm band, pop
inline constexpr core::CubicTiming ease = core::DesignEasing::ease;
inline constexpr core::CubicTiming easeIn = core::DesignEasing::easeIn;
inline constexpr core::CubicTiming easeOut = core::DesignEasing::easeOut;
inline constexpr core::CubicTiming easeInOut = core::DesignEasing::easeInOut;
inline constexpr core::CubicTiming linear{0, 0, 1, 1};
}
// Durations in seconds (HUDDuration, :351-388). toastOut is .25 as a token;
// the toast's own hide uses .24 (HUDDesignOverlay.swift HUDToast.hide, LIB-65).
// entrance, leave and the stagger constants alias core::DesignMotion (the one
// definition, shared with the module switch).
namespace duration {
inline constexpr double hover = .22, press = .10, sheen = .70;
inline constexpr double entrance = core::DesignMotion::entrance, staggerStep = core::DesignMotion::staggerStep;
inline constexpr double staggerBase = core::DesignMotion::staggerBase, leave = core::DesignMotion::leave;
inline constexpr int staggerCap = core::DesignMotion::staggerCap;
inline constexpr double titleWipe = .45, ruleWipe = .42, ruleDelay = .05, menuWipe = .18, tabOn = .20;
inline constexpr double knob = .18, toggleLabel = .14, caret = .16, accentBar = .14, thumb = .12, bubbleFade = .14, bubbleScale = .18;
inline constexpr double bandIn = .30, bandOut = .18, confirmText = .30, confirmTextDelay = .12, pillDelay = .16, pillStep = .06;
inline constexpr double confirmCallback = .12, dim = .22, dimFilter = .25, toastVisible = 1.5, toastOut = .25, toastHide = .24;
inline constexpr double pop = .32, taskFlip = .25;
}

// Box geometry (HUDDesignGeometry, HUDDesignScreen.swift:9-20, LIB-70): the
// ONE definition is core::DesignBox (core/motion.hpp): the host's 1000 x 640
// design space, disc centre (500, 320); one content px is 1470 / (1920 x
// 1.15) = 0.665761 design units, so the box sits at (303.60, 116.94),
// 392.80 x 339.54 units. The plane matrix is
// core::source::DesktopChromeLayout::boxWorld (center * T(500 - 295u,
// 320 - 305u) * S(u)); contentScale is already in `center`.
namespace geometry {
inline constexpr double baseUnit = core::DesignBox::baseUnit;
constexpr Rect boxRect(double unit = baseUnit) noexcept {
    const auto r = core::DesignBox::rect(unit);
    return {r.x, r.y, r.width, r.height};
}
}
// Render scale (HUDDesignContext.update, :56-65, LIB-73; HUDRenderScale,
// LIB-97). boxScale: backing px per box px = clamp(windowScale x unit, 1, 8),
// non-finite -> 2. Text leaves request min(4, ceil(clamp(s, 1, 4) x 1.35));
// other layers clamp(s, 1, 4); cached artwork ceil(s) capped at 8.
double boxScale(double windowScale, double unit) noexcept;
double textContentsScale(double boxScale) noexcept;
double layerContentsScale(double boxScale) noexcept;
double artworkScale(double boxScale) noexcept;
} // namespace endfield::modules::design
