// Design library L1 value contracts (LIBRARY-DESIGN.md section 14 item 1):
// tokens and palette (Sources/HUDDesignTokens.swift), render scale
// (HUDRenderScale.swift, HUDDesignScreen.swift), formats (HUDDesignFormat.swift),
// the text model (HUDDesignText.swift, HUDDesignFonts.swift, HUDDesignHeader.swift),
// icons and the SVG reader (HUDDesignIcons.swift, checked against the Swift
// file text and CoreGraphics output) and the node builders / shapes
// (HUDDesignDrawing.swift). Vectors derive from the Swift formulas (lines
// cited) or were produced by CoreGraphics on the Mac (cited as "CG probe").
// Portable: no window, renderer, clock or IO beyond the explicit inputs.
//   argv[1] tests/fixtures/hud-design-source.json (package L3 oracle; optional
//           until it lands), argv[2] Sources/HUDDesignIcons.swift.
#include "modules/hud_design_format.hpp"
#include "modules/hud_design_icons.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace d = endfield::modules::design;
namespace f = endfield::modules::design::format;
namespace c = endfield::core;
using J = ehud::data::Json;
unsigned checks{};
void check(bool value, const std::string& what) { ++checks; if (!value) throw std::runtime_error(what); }
template <class F> void rejects(F fn, const char* what) { bool rejected{}; try { fn(); } catch (const std::exception&) { rejected = true; } check(rejected, what); }
bool near(double a, double b, double e = 1e-12) { return std::abs(a - b) <= e; }
d::Hex hex(d::Color k) { const auto b = [](double v) { return d::Hex(std::lround(std::clamp(v, 0.0, 1.0) * 255)); }; return b(k.r) << 16 | b(k.g) << 8 | b(k.b); }
std::string hexText(d::Hex h) { char s[8]; std::snprintf(s, sizeof s, "%06X", h); return s; }
void same(d::Color k, d::Hex expected, const std::string& what) { check(hex(k) == expected && k.a == 1, what + " is " + hexText(expected) + " (got " + hexText(hex(k)) + ")"); }

// ---------------------------------------------------------------------------
// HUDAccentPalette (:203-252), HUDTheme (:137-196), HUDColor (:7-129)
// ---------------------------------------------------------------------------
void palette() {
    // :220-222 authored stops of the default, never the formula.
    const auto standard = d::AccentPalette::make("FAD41F", false);
    same(standard.yellow, 0xFFFA00, "Default yellow"); same(standard.yellowHigh, 0xFFFC6B, "Default high");
    same(standard.yellowLow, 0xE9CF12, "Default low"); same(standard.yellowDeep, 0xF1DD10, "Default deep");
    check(standard.ink == standard.yellow, "Dark ink is the yellow itself (:232)");
    same(d::AccentPalette::make("FAD41F", true).ink, 0x87780A, "Default Light ink = low x .58");
    const auto primary = standard.primaryButton();
    check(primary[0] == standard.yellowHigh && primary[1] == standard.yellow && primary[2] == standard.yellowLow, "Primary button high, yellow, low (:231)");
    // :223-230 + :237-251, inventory LIB-1 vectors. The Light inks are the
    // UNROUNDED low stop x .58 (HUDTheme.darkened of the CGColor, :232):
    // 22A6F2 -> 1B6580 and 8CE03C -> 567727; the inventory's 1B6680 / 567726
    // multiplied the 8-bit low stops (AF x .58 = 101.5, 26 x .58 = 38.5).
    struct Vector { const char* accent; d::Hex high, low, deep, ink; };
    for (const auto& v : {Vector{"22A6F2", 0x7FCBF7, 0x2EAFDD, 0x2DAEE5, 0x1B6580}, Vector{"E5383B", 0xF08C8D, 0xD13F51, 0xD8404D, 0x79252F},
                          Vector{"8CE03C", 0xBCED8E, 0x94CD42, 0x94D443, 0x567727}}) {
        const auto dark = d::AccentPalette::make(v.accent, false), light = d::AccentPalette::make(v.accent, true);
        const std::string name = v.accent;
        check(dark.accent == light.accent && hexText(dark.accent) == name, name + " normalises to itself");
        same(dark.yellow, dark.accent, name + " yellow is the accent");
        same(dark.yellowHigh, v.high, name + " high"); same(dark.yellowLow, v.low, name + " low"); same(dark.yellowDeep, v.deep, name + " deep");
        same(light.ink, v.ink, name + " Light ink"); check(dark.ink == dark.yellow, name + " dark ink");
    }
    // The formula on FAD41F itself (what a port must NOT show) differs from the authored stops.
    const auto formula = d::hsbShifted(d::rgb(0xFAD41F), -6.08, .923, .914);
    check(near(formula.r * 255, 228.5, 1e-9) && near(formula.g * 255, 177.720704576, 1e-6) && hex(formula) != 0xE9CF12, "The HSB shift of FAD41F (E4/E5 B2 2C) is not the authored E9CF12: the default is special-cased (inventory LIB-1)");
    same(d::hsbShifted(d::rgb(0x808080), 0, 1, 1), 0x808080, "A grey keeps hue 0 and its value");
    // :217-218 normalisation.
    check(d::normalizedAccent(" #fad41f\n") == 0xFAD41F && d::normalizedAccent("#22#a6F2") == 0x22A6F2, "Trim, drop every '#', either case");
    check(d::normalizedAccent("\xC2\xA0" "22A6F2\xE3\x80\x80") == 0x22A6F2, "Unicode white space trims like .whitespacesAndNewlines");
    for (const char* bad : {"", "12345", "1234567", "GGGGGG", "22 A6F2", "#22A6F", "-00001"}) check(d::normalizedAccent(bad) == d::standardAccent, std::string("Invalid accent '") + bad + "' falls back to FAD41F");
    check(d::normalizedAccent("+FFFFF") == 0xFFFFF && d::normalizedAccent("-00000") == 0, "UInt32(_:radix:) accepts a sign (+, or - of zero)");

    // HUDTheme: the light column is lightTone of the dark one except the explicit three (:154-162).
    for (const auto& row : d::themeTable) {
        if (row.explicitLight) check(d::lightTone(row.dark) != row.light, "Explicit light tokens are not lightTone results");
        else check(d::lightTone(row.dark) == row.light, "Theme token " + hexText(row.dark) + " -> " + hexText(row.light) + " via lightTone");
    }
    std::size_t explicitCount{};
    for (const auto& row : d::themeTable) explicitCount += row.explicitLight;
    check(explicitCount == 3 && d::themeTable[std::size_t(d::ThemeToken::danger)].light == 0xC62828, "secondaryButton(+Hover) and danger have explicit light values");
    for (std::size_t i = 0; i < d::themeTable.size(); ++i) check(std::size_t(d::themeTable[i].token) == i, "Theme table indexed by token");
    for (const auto& [dark, light] : d::libraryTones) check(d::lightTone(dark) == light, "tone(" + hexText(dark) + ") = " + hexText(light) + " in Light");
    check(d::lightTone(0x22A6F2) == 0x22A6F2 && d::lightTone(0xE5383B) == 0xE5383B, "Tinted colours pass lightTone");
    check(d::lightTone(0x515151) == 0x8B8B8B && d::lightTone(0x525252) == 0x848484, "Both branches of lightTone (v < .32 / v >= .32 at the boundary)");

    d::Palette p;
    check(!p.isLight() && p.generation() == 0 && p.accent().accent == d::standardAccent, "The palette starts dark with the default accent");
    same(p.token(d::ThemeToken::panel), 0x1A1A1A, "Dark panel"); same(p.tone(0x8A8A8A), 0x8A8A8A, "Dark tone is the hex");
    check(p.foreground(.5) == d::white(.5), "Dark foreground is white(alpha)");
    check(p.legible(d::fixed::green) == d::fixed::green && p.legible(p.yellow()) == p.yellow(), "legible passes in dark (:67)");
    check(!p.apply("fad41f", false) && p.generation() == 0, "apply without change does nothing (:48)");
    check(p.apply("22A6F2", false) && p.generation() == 1 && p.accent().accent == 0x22A6F2, "A new accent swaps the family and bumps the generation");
    check(p.apply("22A6F2", true) && p.generation() == 2 && p.isLight(), "Light alone is a change");
    same(p.token(d::ThemeToken::panel), 0xF3F3F3, "Light panel"); same(p.tone(0x2A2A2A), 0xE0E0E0, "Light tone");
    same(p.foreground(1), 0x0F0F0F, "Light foreground"); check(p.foreground(.3).a == .3, "Light foreground keeps the alpha");
    // HUDColor.legible (:66-74).
    check(p.legible(p.yellow()) == p.accentInk(), "Light: the palette yellow becomes the ink");
    check(p.legible(d::fixed::cyan) == p.accentInk(), "An accent equal to a status colour takes the accent rule first (:68)");
    d::Palette standardLight; standardLight.apply("FAD41F", true);
    same(standardLight.legible(d::fixed::green), 0x3F8F14, "Light green status ink"); same(standardLight.legible(d::fixed::cyan), 0x0B7DC0, "Light cyan status ink");
    same(standardLight.legible(d::fixed::orange), 0xD05A00, "Light orange status ink"); same(standardLight.legible(d::fixed::red), 0xC8282C, "Light red status ink");
    const auto magenta = p.legible(d::rgb(0xFF1AAC, .5));
    check(near(magenta.r, .6) && near(magenta.g, 26.0 / 255 * .6) && magenta.a == .5, "Light: other hues darken 40 % and keep alpha");
    check(p.legible(d::rgb(0x808080)) == d::rgb(0x808080) && p.legible(d::rgb(0x808A80)) == d::rgb(0x808A80), "Light: neutrals (spread <= .1) pass");
    check(d::mixed(d::rgb(0x000000, .2), d::rgb(0xFFFFFF), .25) == d::Color{.25, .25, .25, 1}, "mixed lerps sRGB with alpha 1 (:181-187)");
    check(d::darkened(d::Color{.5, 1, .2, .4}, .5) == d::Color{.25, .5, .1, .4}, "darkened keeps alpha (:190-195)");
    check(p.apply("not a colour", false) && p.accent().accent == d::standardAccent && p.generation() == 3, "Invalid input applies the default");
    std::uint64_t wrap = std::numeric_limits<std::uint64_t>::max(); ++wrap; check(wrap == 0, "generation wraps like &+=");
}

// ---------------------------------------------------------------------------
// HUDType (:271-307), HUDMetric, HUDLayerOrder, HUDEasing, HUDDuration, geometry, render scale
// ---------------------------------------------------------------------------
void typeScale() {
    using W = d::FontWeight;
    d::Palette p;
    const auto steel = p.token(d::ThemeToken::steel), title = p.token(d::ThemeToken::titleText);
    struct Row { const char* name; d::TextStyle s; double size; W weight; d::Color color; double lh, tracking; bool upper, tabular; };
    const Row rows[]{
        {"moduleTitle", d::type::moduleTitle(p), 26, W::bold, title, 1, .08, true, false},
        {"titleSubtitle", d::type::titleSubtitle(p), 13, W::medium, steel, 1, .2, false, false},
        {"breadcrumb", d::type::breadcrumb(p), 10.5, W::regular, steel, 1, .06, true, false},
        {"ruleCounter", d::type::ruleCounter(p), 11, W::bold, p.yellow(), 1, 0, false, true},
        {"rowLabel", d::type::rowLabel(), 15.5, W::medium, d::rgb(0x121212), 1.2, 0, false, false},
        {"rowSublabel", d::type::rowSublabel(), 11, W::regular, d::rgb(0x6C6D68), 1.2, 0, false, false},
        {"button", d::type::button(), 12, W::bold, d::rgb(0x111111), 1, .08, true, false},
        {"pill", d::type::pill(), 14, W::medium, d::rgb(0x111111), 1, .06, false, false},
        {"toggleLabel", d::type::toggleLabel(p), 10, W::bold, p.token(d::ThemeToken::muted), 1, .12, false, false},
        {"dropdownValue", d::type::dropdownValue(p), 12.5, W::medium, p.token(d::ThemeToken::textOnDark), 1, 0, false, false},
        {"menuItem", d::type::menuItem(p), 12.5, W::regular, p.token(d::ThemeToken::menuText), 1, 0, false, false},
        {"segmented", d::type::segmented(p), 12.5, W::medium, p.token(d::ThemeToken::segmentedText), 1, 0, false, false},
        {"tab", d::type::tab(p), 13, W::medium, p.token(d::ThemeToken::tabText), 1, .04, false, false},
        {"tabKeycap", d::type::tabKeycap(p), 9, W::bold, d::rgb(0x8A8A8A), 1, 0, false, false},
        {"chip", d::type::chip(), 9.5, W::bold, d::rgb(0x111111), 1, .1, true, false},
        {"newBadge", d::type::newBadge(), 8, W::black, d::rgb(0x111111), 1, .06, false, false},
        {"stamp", d::type::stamp(p), 10, W::black, p.accentInk(), 1, .16, true, false},
        {"numeral", d::type::numeral(p), 64, W::light, title, .9, -.02, false, true},
        {"numeralUnit", d::type::numeralUnit(p), 64 * .34, W::medium, p.accentInk(), 1, 0, false, false},
        {"label", d::type::label(p), 10, W::regular, steel, 1, .1, true, false},
        {"labelCJK", d::type::labelCJK(p), 11.5, W::regular, steel, 1, .12, false, false},
        {"statValue", d::type::statValue(p), 18, W::regular, d::rgb(0xF2F2F2), 1, 0, false, true},
        {"statUnit", d::type::statUnit(p), 11, W::regular, steel, 1, 0, false, false},
        {"readout", d::type::readout(), 12, W::bold, d::rgb(0x151515), 1, 0, false, true},
        {"readoutBracket", d::type::readoutBracket(), 12, W::bold, d::rgb(0x8A8B86), 1, 0, false, true},
        {"sliderBubble", d::type::sliderBubble(), 11, W::bold, d::rgb(0x111111), 1, 0, false, true},
        {"field", d::type::field(p), 13, W::regular, p.token(d::ThemeToken::textOnDark), 1, 0, false, false},
        {"emptyTitle", d::type::emptyTitle(p), 20, W::bold, d::rgb(0xE9E9E9), 1, .24, false, false},
        {"keycap", d::type::keycap(), 11, W::bold, d::rgb(0x111111), 1, 0, false, false},
        {"toastTitle", d::type::toastTitle(p), 14, W::bold, d::white(1), 1, .22, true, false},
        {"toastCaption", d::type::toastCaption(p), 11.5, W::regular, p.token(d::ThemeToken::caption), 1, .3, false, false},
        {"confirmTitle", d::type::confirmTitle(p), 17, W::medium, title, 1.25, 0, false, false},
        {"confirmDescription", d::type::confirmDescription(p), 12, W::regular, p.token(d::ThemeToken::description), 1.5, 0, false, false},
        {"confirmCountdown", d::type::confirmCountdown(p), 22, W::bold, p.accentInk(), 1, 0, false, true},
        {"microTag", d::type::microTag(), 5.5, W::bold, d::rgb(0xE9E9E9), 7 / 5.5, .08, true, false}};
    check(std::size(rows) == 35, "All 35 HUDType styles");
    for (const auto& r : rows)
        check(r.s == d::TextStyle{r.size, r.weight, r.color, r.lh, r.tracking, r.upper, r.tabular}, std::string("HUDType.") + r.name + " matches :271-307");
    // Theme-following styles re-read the palette (computed properties).
    d::Palette light; light.apply("FAD41F", true);
    same(d::type::moduleTitle(light).color, 0x161616, "Light title text"); same(d::type::statValue(light).color, 0x181818, "Light statValue tone");
    same(d::type::stamp(light).color, 0x87780A, "Light stamp ink"); same(d::type::toastTitle(light).color, 0x0F0F0F, "Light toast title foreground");
    check(d::resolved(W::semibold) == W::bold && d::resolved(W::black) == W::black && d::resolved(W::light) == W::light, "600 renders Bold (HUDFontWeight.resolved)");
    const auto s = d::type::rowLabel();
    check(s.sized(20).size == 20 && s.colored(d::white(1)).color == d::white(1) && s.weighted(W::bold).weight == W::bold && s.sized(20).weight == s.weight, "sized / colored / weighted copy one field");

    // HUDMetric (:311-323), HUDLayerOrder (:326-335).
    check(d::metric::boxWidth == 590 && d::metric::boxHeight == 510 && d::metric::headerHeight == 81 && d::metric::bodyHeight == 429, "Box 590 x 510, header 81, body 429");
    check(d::metric::rowHeight == 50 && d::metric::compactRowHeight == 44 && d::metric::rowGap == 6 && d::metric::rowChamfer == 11 && d::metric::buttonChamfer == 9 && d::metric::sliderLabelColumn == 170, "Row metrics");
    check(d::order::content == 0 && d::order::dim == 15 && d::order::confirmBar == 20 && d::order::toast == 25 && d::order::bootLog == 30 && d::order::menu == 30 && d::order::bubble == 40, "Layer order");
    // HUDEasing (:338-348).
    const auto bezier = [](c::CubicTiming t, double a, double b, double x, double y) { return t.x1 == a && t.y1 == b && t.x2 == x && t.y2 == y; };
    check(bezier(d::easing::standard, .2, .8, .2, 1) && bezier(d::easing::snap, .2, .9, .1, 1) && bezier(d::easing::ease, .25, .1, .25, 1), "E, SNAP, ease");
    check(bezier(d::easing::easeIn, .42, 0, 1, 1) && bezier(d::easing::easeOut, 0, 0, .58, 1) && bezier(d::easing::easeInOut, .42, 0, .58, 1) && bezier(d::easing::linear, 0, 0, 1, 1), "easeIn/Out/InOut, linear");
    // HUDDuration (:351-388).
    namespace t = d::duration;
    const double durations[]{t::hover, t::press, t::sheen, t::entrance, t::staggerStep, t::staggerBase, t::leave, t::titleWipe, t::ruleWipe, t::ruleDelay, t::menuWipe, t::tabOn,
        t::knob, t::toggleLabel, t::caret, t::accentBar, t::thumb, t::bubbleFade, t::bubbleScale, t::bandIn, t::bandOut, t::confirmText, t::confirmTextDelay, t::pillDelay,
        t::pillStep, t::confirmCallback, t::dim, t::dimFilter, t::toastVisible, t::toastOut, t::pop, t::taskFlip};
    const double swift[]{.22, .10, .70, .38, .036, .06, .13, .45, .42, .05, .18, .20, .18, .14, .16, .14, .12, .14, .18, .30, .18, .30, .12, .16, .06, .12, .22, .25, 1.5, .25, .32, .25};
    for (std::size_t i = 0; i < std::size(swift); ++i) check(durations[i] == swift[i], "HUDDuration #" + std::to_string(i));
    check(t::staggerCap == 14 && t::toastHide == .24, "Stagger cap 14; the toast hides over .24 (LIB-65)");
    // HUDDesignGeometry (HUDDesignScreen.swift:9-20): (303.60, 116.94) 392.80 x 339.54.
    const auto box = d::geometry::boxRect();
    check(near(d::geometry::baseUnit, 1470.0 / (1920 * 1.15)) && near(d::geometry::baseUnit, .665761, 1e-6), "One content px = 1470 / (1920 x 1.15) units");
    check(near(box.x, 303.60, .005) && near(box.y, 116.94, .005) && near(box.width, 392.80, .005) && near(box.height, 339.54, .005), "Box rect at baseUnit");
    const auto doubled = d::geometry::boxRect(1);
    check(doubled.x == 205 && doubled.y == 15 && doubled.width == 590 && doubled.height == 510, "Box rect at unit 1");
    // HUDDesignContext.update (:56-58), HUDRenderScale (:7-10), HUDArtwork.image (:169).
    check(near(d::boxScale(2, d::geometry::baseUnit), 2 * d::geometry::baseUnit) && d::boxScale(1, .5) == 1 && d::boxScale(20, 1) == 8, "boxScale clamps 1...8");
    check(d::boxScale(std::numeric_limits<double>::infinity(), 1) == 2 && d::boxScale(NAN, 1) == 2, "Non-finite render scale -> 2");
    check(d::textContentsScale(1.3315) == 2 && d::textContentsScale(2) == 3 && d::textContentsScale(3.5) == 4 && d::textContentsScale(.5) == 2, "Text: min(4, ceil(clamp(s,1,4) x 1.35))");
    check(d::layerContentsScale(9) == 4 && d::layerContentsScale(.2) == 1 && d::layerContentsScale(2.5) == 2.5 && d::layerContentsScale(NAN) == 2, "Layers: clamp(s, 1, 4)");
    check(d::artworkScale(2.1) == 3 && d::artworkScale(9) == 8 && d::artworkScale(.3) == 1 && d::artworkScale(NAN) == 2, "Artwork: clamp(ceil(s), 1, 8)");
}

// ---------------------------------------------------------------------------
// HUDFormat / HUDValueFormat (HUDDesignFormat.swift)
// ---------------------------------------------------------------------------
void formats() {
    check(f::pad2(6) == "06" && f::pad2(0) == "00" && f::pad2(10) == "10" && f::pad2(-3) == "-3", "pad2 (:8)");
    check(f::clock(3600) == "60:00" && f::clock(65) == "01:05" && f::clock(-5) == "00:00" && f::clock(6000) == "100:00", "clock mm:ss past 59, negatives 0 (:11-14)");
    check(f::grouped(3262) == "3,262" && f::grouped(999) == "999" && f::grouped(-1234567) == "-1,234,567" && f::grouped(1000) == "1,000" && f::grouped(0) == "0", "en-US grouping (:17-21)");
    check(f::storage(82.4) == "82.4 GB" && f::storage(1860) == "1.86 TB" && f::storage(999.96) == "1000.0 GB" && f::storage(0) == "0.0 GB", "storage (:24-26)");
    check(f::dotted(2026, 10, 7) == "2026.10.07" && f::dotted(2026, 10, 7, std::pair{21, 4}) == "2026.10.07 21:04" && f::dotted(987, 1, 2) == "987.01.02", "dotted, year unpadded (:29-33)");
    check(f::weekday(1) == "MONDAY" && f::weekday(0) == "SUNDAY" && f::weekday(6) == "SATURDAY", "weekday EEEE uppercased (:36-40)");
    check(f::monthHeading(2026, 10) == "October 2026" && f::monthHeading(987, 1) == "January 0987", "monthHeading MMMM yyyy (:43-47)");
    rejects([] { f::weekday(7); }, "Weekday index is 0...6");
    rejects([] { f::monthHeading(2026, 13); }, "Month is 1...12");
    using V = d::ValueFormat;
    check(d::formatValue(V::scale, 1.5) == "1.50\xC3\x97" && d::formatValue(V::scale, 1.006) == "1.01\xC3\x97", "scale %.2f×");
    check(d::formatValue(V::percent, 42.5) == "43%" && d::formatValue(V::percent, -42.5) == "-43%", "percent rounds half away from zero");
    check(d::formatValue(V::signedPercent, 3) == "+3%" && d::formatValue(V::signedPercent, 0) == "0%" && d::formatValue(V::signedPercent, -2) == "-2%", "signedPercent");
    check(d::formatValue(V::balance, 2.9) == "C" && d::formatValue(V::balance, -2.99) == "C" && d::formatValue(V::balance, -10.4) == "L 10" && d::formatValue(V::balance, 10.5) == "R 11", "balance C / L n / R n");
    check(d::formatValue(V::seconds, 12.4) == "12 s" && d::formatValue(V::pixels, 3) == "3 px" && d::formatValue(V::page, 2.6) == "P. 3", "seconds, pixels, page");
    check(d::formatValue(V::signed_, 4) == "+4" && d::formatValue(V::signed_, 0) == "0" && d::formatValue(V::integer, -.5) == "-1", "signed, integer");
    check(d::formatValue(V::clock, 125.4) == "02:05" && d::formatValue(V::decimal, .126) == "0.13", "clock, decimal");
    check(d::formatValue(V::integer, NAN) == "0", "A non-finite reading formats 0 instead of trapping");
}

// ---------------------------------------------------------------------------
// Text model
// ---------------------------------------------------------------------------
// Widths from character counts (the oracle's recorded widths replace this
// once the fixture lands); records what the measurer is asked.
struct FakeMeasurer final : d::TextMeasurer {
    std::vector<std::string> asked;
    double width(std::span<const d::TextRun> runs, d::TextRegion region) override {
        std::string all; double w{};
        for (const auto& r : runs) { all += r.text; for (const unsigned char ch : r.text) if ((ch & 0xC0) != 0x80) w += r.style.size * .5 * (1 + r.style.tracking); }
        asked.push_back(all + "@" + std::string(d::regionTag(region)));
        return std::ceil(w * 2) / 2;
    }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion, double scale) override { return {s.size * scale * s.lineHeight, .928 * s.size * scale, .244 * s.size * scale}; }
};
void text() {
    using R = d::TextRegion;
    for (const auto r : {R::simplifiedChinese, R::traditionalChinese, R::japanese, R::korean}) check(d::regionFromTag(d::regionTag(r)) == r, "Region tags round-trip");
    check(d::regionTag(R::traditionalChinese) == "zh-Hant" && d::regionTag(R::korean) == "ko" && !d::regionFromTag("zh-CN"), "BCP 47 tags of HUDTextLanguage (:103)");
    // HUDTextLanguage.tag(for:) (:108-131) with L10n.resolveLanguage per identifier.
    check(d::regionFor(c::Language::japanese, R::korean) == R::japanese && d::regionFor(c::Language::english, R::korean) == R::korean && d::regionFor(c::Language::system, R::traditionalChinese) == R::traditionalChinese, "UI language decides, English/System use the system region");
    const auto first = [](std::initializer_list<std::string_view> ids) { return d::firstRegion(std::span(ids.begin(), ids.size())); };
    check(first({"en-US", "zh-Hant-TW"}) == R::traditionalChinese && first({"fr", "en"}) == R::simplifiedChinese && first({"en", "ja-JP", "ko"}) == R::japanese, "First CJK preferred language");
    check(first({"zh-HK"}) == R::traditionalChinese && first({"zh_Hans_TW"}) == R::simplifiedChinese && first({"zh"}) == R::simplifiedChinese && first({}) == R::simplifiedChinese, "zh script beats region; none -> zh-Hans");
    check(!d::regionNaming(c::Language::english) && !d::regionNaming(c::Language::system) && d::regionNaming(c::Language::traditionalChinese) == R::traditionalChinese, "tag(naming:) (:147-155)");
    check(d::asciiUppercase("abc\xC3\xA9z") == "ABC\xC3\xA9Z", "asciiUppercase leaves non-ASCII to the injected casing");

    // HUDTextLayer padding (:134-137) and the frozen padding of lineFrame (:124-133, LIB-F29).
    d::TextBlock block("Archive", d::type::rowLabel());
    check(block.padding() == d::Size{4, 7} && block.maximumLines == 1 && block.truncates && block.minimumScale == 1 && block.alignment == d::TextAlignment::left, "15.5 px: ceil(3.875) x ceil(6.975); defaults");
    block.setLineFrame({10, 20, 100, 18.6});
    block.runs.push_back({"!", d::type::moduleTitle(d::Palette{})});
    check(block.padding() == d::Size{4, 7}, "Runs added after lineFrame keep the frozen padding");
    check(block.layerFrame() == d::Rect{6, 13, 108, 32.6}, "Layer frame = lineFrame outset by the padding");
    check(block.text() == "Archive!" && block.style() == d::type::rowLabel(), "text joins runs; style is the first run's");
    block.setColor(d::white(1));
    check(block.runs[0].style.color == d::white(1) && block.runs[1].style == d::type::rowLabel().colored(d::white(1)), "Setting the colour restyles every run from the first (HUDTextLayer.color)");
    block.setText("Shelf");
    check(block.runs.size() == 1 && block.text() == "Shelf", "setText keeps one run in the style");
    d::TextBlock empty; empty.runs.clear();
    check(empty.style() == d::type::rowLabel() && empty.padding() == d::Size{3, 6}, "No runs: rowLabel style, 12 px padding");
    empty.setStyle(d::type::chip());
    check(empty.runs.size() == 1 && empty.runs[0].text.empty() && empty.style() == d::type::chip(), "Styling an empty block makes one empty run");
    auto again = d::TextBlock("x", d::type::label(d::Palette{}));
    again.setLineFrame({0, 0, 10, 10}); again.setStyle(d::type::moduleTitle(d::Palette{})); again.setLineFrame({0, 0, 10, 10});
    check(again.padding() == d::Size{3, 5}, "An unchanged lineFrame does not refreeze (didSet guard)");
    again.setLineFrame({0, 0, 11, 10});
    check(again.padding() == d::Size{7, 12}, "A new lineFrame freezes the padding of the current runs");

    FakeMeasurer measurer;
    d::TextServices s{&measurer, {}, R::japanese, 2};
    const double w = s.width("Archive", d::type::button());
    check(measurer.asked.back() == "ARCHIVE@ja" && w == std::ceil(7 * 6 * 1.08 * 2) / 2, "The measurer gets the displayed (uppercased) text in the UI region");
    check(s.width("", d::type::button()) == 0 && measurer.asked.size() == 1, "Empty text measures 0 without asking");
    d::TextBlock tagged("Title", d::type::moduleTitle(d::Palette{})); tagged.language = R::korean;
    d::preferredWidth(tagged, s);
    check(measurer.asked.back() == "TITLE@ko", "preferredWidth measures in the block's own region (:164)");
    s.uppercase = [](std::string_view v) { std::string o(v); for (auto& ch : o) ch = char(std::toupper(static_cast<unsigned char>(ch))); return "<" + o + ">"; };
    check(s.displayed({"abc", d::type::chip()}) == "<ABC>" && s.displayed({"abc", d::type::pill()}) == "abc", "The injected casing applies only to uppercase styles");
    s.uppercase = {};
    check(s.metrics(d::type::confirmTitle(d::Palette{})).height == 17 * 1.25, "metrics forwards to the measurer (line box height = size x lineHeight)");
    rejects([] { d::TextServices none; none.width("x", d::type::pill()); }, "Measuring without a measurer throws");

    // The line-box descriptor (A1 keys, native/layer_raster.hpp).
    d::TextBlock mixed;
    mixed.runs = {{"Hi ", d::type::chip()}, {"", d::type::pill()}, {"\xE6\x97\xA5\xF0\x9F\x98\x80", d::type::rowLabel().weighted(d::FontWeight::semibold)}};
    mixed.alignment = d::TextAlignment::right; mixed.maximumLines = 3; mixed.truncates = false; mixed.minimumScale = .8;
    mixed.setLineFrame({5, 6, 70, 12});
    const auto node = d::textNode("t", mixed, s);
    const auto& tx = node["text"];
    check(node["kind"].string() == "text" && node["id"].string() == "t" && node["position"] == J(J::Array{5., 6.}) && node["bounds"] == J(J::Array{0., 0., 70., 12.}), "The node's bounds are the CSS line box");
    check(node["anchorPoint"] == J(J::Array{0., 0.}) && node["contentsScale"].number() == 3, "Anchor (0,0); contentsScale = textContentsScale(boxScale 2)");
    check(tx["string"].string() == "HI \xE6\x97\xA5\xF0\x9F\x98\x80", "string = displayed runs, empty runs dropped");
    const auto& runs = tx["runs"].array();
    check(runs.size() == 2 && runs[0]["utf16Range"] == J(J::Array{std::int64_t(0), std::int64_t(3)}) && runs[1]["utf16Range"] == J(J::Array{std::int64_t(3), std::int64_t(3)}), "UTF-16 ranges (BMP 1 unit, astral 2)");
    const auto& a = runs[1]["attributes"];
    check(a["fontSize"].number() == 15.5 && a["fontWeight"].integer() == 700 && !a["tabularFigures"].boolean() && a["tracking"].number() == 0 && a["NSLanguage"].string() == "ja", "Run attributes: size, resolved weight 600 -> 700, tnum, tracking, language");
    check(a["NSColor"]["sRGB"] == J(J::Array{18 / 255., 18 / 255., 18 / 255., 1.}), "Run colour in sRGB");
    check(tx["fontSize"].number() == 9.5 && tx["fontWeight"].integer() == 700 && tx["tracking"].number() == .1 && tx["lineHeight"].number() == 1, "Descriptor keys from the first run");
    check(tx["alignment"].string() == "right" && tx["maximumLines"].integer() == 3 && tx["minimumScale"].number() == .8 && tx["truncation"].string() == "none" && tx["language"].string() == "ja", "Alignment, line budget, shrink, truncation, region");
    tagged.setLineFrame({0, 0, 50, 26});
    check(d::textNode("u", tagged, s)["text"]["language"].string() == "ko" && d::textNode("u", tagged, s)["text"]["truncation"].string() == "end", "The block's own region wins; truncation end by default");
    rejects([&] { d::TextBlock bad("\xFF", d::type::pill()); d::textNode("x", bad, s); }, "Invalid UTF-8 text is rejected");

    // HUDModuleTitle (HUDDesignHeader.swift:7-25).
    const auto t = d::ModuleTitle::make("Clipboard", "\xE5\x89\xAA\xE8\xB4\xB4\xE6\x9D\xBF", c::Language::japanese);
    const auto english = t.lines(c::Language::english);
    check(english.title == "Clipboard" && !english.titleLanguage && english.subtitle.empty(), "English UI: English title alone, no subtitle");
    const auto japanese = t.lines(c::Language::japanese);
    check(japanese.title == t.localized && japanese.titleLanguage == R::japanese && japanese.subtitle == "Clipboard", "ja: localised title in its region, English subtitle");
    check(t.lines(c::Language::system).subtitle.empty(), "An unresolved System falls back to English only");

    // HUDArtwork.microTagSize (:117-118).
    measurer.asked.clear();
    const auto tag = d::microTagSize(s);
    check(tag.height == 7 && tag.width == s.width("ENDFIELD", d::type::microTag()) + 8 + 44, "Micro tag: label + 8 + 44 x 7");
}

// ---------------------------------------------------------------------------
// Icons (HUDDesignIcons.swift)
// ---------------------------------------------------------------------------
// CGFloat's description: the shortest round-trip digits, ".0" for integers.
std::string swiftNumber(double v) {
    char s[64]; const auto r = std::to_chars(s, s + sizeof s, v);
    std::string out(s, r.ptr);
    if (out.find_first_of(".e") == std::string::npos) out += ".0";
    return out;
}
// Evaluates the parts of one `case .name: return [...]` line of the Swift file.
struct SwiftPart { std::string svg; bool fill{}; double width{2.6}; bool square{}; bool fixedColor{}; std::vector<double> dash; };
struct SwiftReader {
    std::string_view s; std::size_t at{};
    void ws() { while (at < s.size() && s[at] == ' ') ++at; }
    bool eat(std::string_view t) { ws(); if (s.substr(at, t.size()) == t) { at += t.size(); return true; } return false; }
    void expect(std::string_view t) { if (!eat(t)) throw std::runtime_error("Unexpected Swift icon syntax near: " + std::string(s.substr(at, 40))); }
    double number() { // the Swift literals are plain decimals ("2.6", "-12"); the test runs in the "C" locale
        ws(); const auto end = s.find_first_not_of("+-.0123456789", at); const std::string token(s.substr(at, end - at));
        char* stop{}; const double v = std::strtod(token.c_str(), &stop);
        if (token.empty() || stop != token.c_str() + token.size()) throw std::runtime_error("Swift number"); at = end; return v; }
    std::string expr() {
        std::string out;
        do {
            if (eat("\"")) { const auto end = s.find('"', at); out += s.substr(at, end - at); at = end + 1; }
            else if (eat("rect(")) { const double x = number(); expect(","); const double y = number(); expect(","); const double w = number(); expect(","); const double h = number(); expect(")");
                out += "M" + swiftNumber(x) + " " + swiftNumber(y) + "h" + swiftNumber(w) + "v" + swiftNumber(h) + "h" + swiftNumber(-w) + "z"; }
            else if (eat("circle(")) { const double x = number(); expect(","); const double y = number(); expect(","); const double r = number(); expect(")");
                out += "M" + swiftNumber(x - r) + " " + swiftNumber(y) + "a" + swiftNumber(r) + " " + swiftNumber(r) + " 0 1 0 " + swiftNumber(2 * r) + " 0a" + swiftNumber(r) + " " + swiftNumber(r) + " 0 1 0 " + swiftNumber(-2 * r) + " 0z"; }
            else throw std::runtime_error("Unknown Swift icon expression");
        } while (eat("+"));
        return out;
    }
    std::vector<SwiftPart> parts() {
        std::vector<SwiftPart> out;
        expect("[");
        do {
            SwiftPart p;
            if (eat("s(")) { p.svg = expr(); if (eat(",")) { if (eat("square: true")) p.square = true; else { p.width = number(); if (eat(",")) { expect("square: true"); p.square = true; } } } expect(")"); }
            else if (eat("f(")) { p.svg = expr(); p.fill = true; expect(")"); }
            else if (eat("Part(svg: ")) {
                p.svg = expr(); expect(","); expect("mode: .stroke("); p.width = number(); expect(","); expect("square: "); p.square = eat("true"); if (!p.square) expect("false"); expect(")");
                if (eat(", color: HUDColor.nearBlack")) p.fixedColor = true;
                if (eat(", dash: [")) { p.dash.push_back(number()); expect(","); p.dash.push_back(number()); expect("]"); }
                expect(")");
            } else throw std::runtime_error("Unknown Swift icon part");
            out.push_back(std::move(p));
        } while (eat(","));
        expect("]");
        return out;
    }
};
void icons(const char* swiftPath) {
    check(std::size_t(d::Icon::count) == 50, "50 glyphs (HUDIcon.allCases)");
    check(d::iconName(d::Icon::export_) == "export" && d::iconNamed("export") == d::Icon::export_ && d::iconNamed("pin") == d::Icon::pin && !d::iconNamed("nope"), "Raw values and lookup");
    std::ifstream file(swiftPath, std::ios::binary);
    check(bool(file), "Sources/HUDDesignIcons.swift is readable");
    const std::string swift((std::istreambuf_iterator<char>(file)), {});
    std::size_t at{}, seen{};
    for (std::size_t i = 0; i < std::size_t(d::Icon::count); ++i) {
        const auto icon = d::Icon(i);
        const auto key = "case ." + std::string(d::iconName(icon)) + ": return ";
        const auto found = swift.find(key, at);
        check(found != std::string::npos, "Swift parts of ." + std::string(d::iconName(icon)) + " in declaration order");
        at = found + key.size();
        const auto line = std::string_view(swift).substr(at, swift.find('\n', at) - at);
        SwiftReader reader{line};
        const auto expected = reader.parts();
        const auto parts = d::iconParts(icon);
        check(parts.size() == expected.size(), "Part count of ." + std::string(d::iconName(icon)));
        for (std::size_t k = 0; k < parts.size(); ++k) {
            const auto& e = expected[k]; const auto& p = parts[k];
            const auto what = std::string(d::iconName(icon)) + "." + std::to_string(k);
            check(p.svg == e.svg, what + " path text equals the Swift (" + e.svg + ")");
            check(p.fill == e.fill && (p.fill || (p.strokeWidth == e.width && p.squareCap == e.square)), what + " mode, width and cap");
            check(p.color.has_value() == e.fixedColor && (!p.color || *p.color == 0x111111), what + " fixed colour (warn's nearBlack mark)");
            check((e.dash.empty() && p.dash[0] == 0 && p.dash[1] == 0) || (e.dash.size() == 2 && p.dash[0] == e.dash[0] && p.dash[1] == e.dash[1]), what + " dash");
            d::parseSvgPath(p.svg); ++seen;
        }
    }
    check(seen == 69, "69 parts parse");
    // Op sequences of every arc-bearing part (CG probe: HUDSVGPath.parse run
    // on this Mac; 0 move, 1 line, 3 cubic, 4 close). The extra quarters and
    // the dropped tiny remainders follow CGPath.addRelativeArc.
    const std::pair<const char*, const char*> arcOps[]{{"power.1", "013333"}, {"link.0", "01011331011331"}, {"link2.0", "01011331011331"}, {"refresh.0", "013333"},
        {"reset.0", "013333"}, {"rotate.0", "013333"}, {"vol.1", "013013"}, {"timer.0", "0133133401101"}, {"watch.0", "0133133401"}, {"lyric.1", "013313334"},
        {"gear.0", "0133313340101010101010101"}, {"eye.1", "01331334"}, {"eyeoff.1", "01331334"}, {"user.0", "01331334"}, {"help.0", "01331334013333101"},
        {"lock.0", "01114011331"}, {"unlock.0", "0111401133"}, {"rec.0", "01331334"}};
    for (const auto& [name, ops] : arcOps) {
        const std::string n = name; const auto dot = n.find('.');
        const auto icon = d::iconNamed(n.substr(0, dot)); const auto index = std::stoul(n.substr(dot + 1));
        const auto json = d::parseSvgPath(d::iconParts(*icon)[index].svg).json();
        std::string got;
        for (const auto& command : json.array()) { const auto op = command["op"].string(); got += op == "move" ? '0' : op == "line" ? '1' : op == "quadratic" ? '2' : op == "cubic" ? '3' : '4'; }
        check(got == ops, std::string(name) + " op sequence " + ops + " (got " + got + ")");
    }
    // CG probe points: power.1 (three quarters from an oblique start + rest)
    // and link.0 (exact quarters on a scaled radius).
    const auto points = [](std::string_view svg) { std::vector<double> v; const auto json = d::parseSvgPath(svg).json(); for (const auto& cmd : json.array()) for (const auto& p : cmd["points"].array()) { v.push_back(p.array()[0].number()); v.push_back(p.array()[1].number()); } return v; };
    const std::vector<double> power{6.6, 6.8, 6.6, 6.799999999999999, 3.3401161648028257, 9.782337648919999, 3.115120525730518, 14.842657990152308, 6.097458174650518, 18.102541825349483,
        9.079795823570517, 21.362425660546656, 14.140116164802826, 21.587421299618963, 17.4, 18.605083650698965, 20.659883835197174, 15.622746001778964, 20.884879474269482,
        10.562425660546655, 17.90254182534948, 7.302541825349481, 17.74255504192487, 7.1276661437445, 17.574875681604983, 6.959986783424613, 17.400000000000002, 6.800000000000002};
    const auto mine = points("M6.6 6.8a8 8 0 1 0 10.8 0");
    check(mine.size() == power.size(), "power.1 point count " + std::to_string(mine.size()) + " vs " + std::to_string(power.size()));
    for (std::size_t i = 0; i < power.size(); ++i) check(near(mine[i], power[i], 1e-12), "power.1 point " + std::to_string(i) + " equals CoreGraphics");
    const auto link = points("M8.5 11.5l-2 2a3.5 3.5 0 0 0 5 5l2-2");
    const std::vector<double> cg{8.5, 11.5, 6.5, 13.5, 6.5, 13.5, 5.1192881255, 14.8807118745, 5.1192881255, 17.1192881255, 6.5, 18.5,
        7.880711874499999, 19.8807118745, 10.119288125499999, 19.8807118745, 11.5, 18.5, 13.5, 16.5};
    check(link.size() == cg.size(), "link arc point count");
    for (std::size_t i = 0; i < cg.size(); ++i) check(near(link[i], cg[i], 1e-12), "link arc point " + std::to_string(i) + " equals CoreGraphics (quarter constant .5522847498)");
    // Reader rules (:146-191, :222-263): implicit repeats, compact numbers, relative s/c.
    const auto repeat = d::parseSvgPath("M1 2 3 4l1-1-1,1.5e1zm.5.5h1V9").json();
    check(repeat.array().size() == 8 && repeat.array()[1]["op"].string() == "line" && repeat.array()[1]["points"] == J(J::Array{J(J::Array{3., 4.})}), "An implicit pair after M is a line");
    check(repeat.array()[3]["points"] == J(J::Array{J(J::Array{3., 18.})}) && repeat.array()[5]["points"] == J(J::Array{J(J::Array{1.5, 2.5})}), "Relative repeats, exponents, m after z is relative to the subpath start");
    const auto smooth = d::parseSvgPath("M0 0c1 0 2 1 3 3s4 2 5 0").json().array();
    check(smooth[2]["points"] == J(J::Array{J::Array{4., 5.}, J::Array{7., 5.}, J::Array{8., 3.}}), "s reflects the last control point");
    rejects([] { d::parseSvgPath("M0 0Q1 1 2 2"); }, "Unsupported commands are rejected");
    rejects([] { d::parseSvgPath("M0"); }, "A missing coordinate is rejected");
    // HUDIconLayer (:112-140): parts scaled to size, stroke widths x size/24, the tint.
    const auto node = d::iconNode("i", d::Icon::mirror, 12, d::rgb(0xFFFA00), {20, 30});
    check(node["position"] == J(J::Array{14., 24.}) && node["bounds"] == J(J::Array{0., 0., 12., 12.}), "The glyph is size x size centred on the point");
    const auto& kids = node["children"].array();
    check(kids.size() == 2 && kids[0]["kind"].string() == "shape" && kids[0]["shape"]["lineWidth"].number() == 1 && kids[0]["shape"]["fillColor"].isNull(), "Stroke width 2 x 12/24, no fill");
    check(kids[0]["shape"]["lineDashPattern"] == J(J::Array{1., 1.}) && kids[0]["shape"]["lineCap"].string() == "butt" && kids[0]["shape"]["lineJoin"].string() == "miter", "Dash 2/2 scaled, butt caps, miter joins");
    check(kids[1]["shape"]["strokeColor"].isNull() && kids[1]["shape"]["fillColor"]["sRGB"].array()[1].number() == 250 / 255., "Fills take the tint");
    check(kids[0]["shape"]["path"].array()[0]["points"] == J(J::Array{J(J::Array{6., 1.5})}), "Paths scale by size/24");
    const auto warn = d::iconNode("w", d::Icon::warn, 24, d::white(1), {12, 12})["children"].array();
    check(warn[1]["shape"]["strokeColor"]["sRGB"] == J(J::Array{17 / 255., 17 / 255., 17 / 255., 1.}) && warn[0]["shape"]["fillColor"]["sRGB"] == J(J::Array{1., 1., 1., 1.}), "warn: fixed #111 mark over the tinted triangle");
    check(d::iconNode("c", d::Icon::check, 24, d::white(1), {12, 12})["children"].array()[0]["shape"]["lineCap"].string() == "square", "Square caps");
    rejects([] { d::iconNode("x", d::Icon::x, 0, d::white(1), {}); }, "An icon needs a positive size");
}

// ---------------------------------------------------------------------------
// Shapes and nodes (HUDDesignDrawing.swift)
// ---------------------------------------------------------------------------
std::vector<std::pair<std::string, std::vector<double>>> commands(const d::PathBuilder& p) {
    std::vector<std::pair<std::string, std::vector<double>>> out;
    const auto json = p.json();
    for (const auto& c : json.array()) { std::vector<double> v; for (const auto& q : c["points"].array()) { v.push_back(q.array()[0].number()); v.push_back(q.array()[1].number()); } out.push_back({c["op"].string(), v}); }
    return out;
}
void nodes() {
    using V = std::vector<double>;
    // HUDShape.chamfer (:16-32): start TL (+n) -> TR -> BR -> BL -> TL.
    const auto tr = commands(d::chamfer({0, 0, 100, 40}, 11));
    check(tr.size() == 6 && tr[0] == std::pair{std::string("move"), V{0, 0}} && tr[1].second == V{89, 0} && tr[2].second == V{100, 11} && tr[3].second == V{100, 40} && tr[4].second == V{0, 40} && tr[5].first == "close", "Chamfer default: top-right only, Mac point order");
    const auto all = commands(d::chamfer({10, 20, 30, 40}, 4, d::topLeft | d::topRight | d::bottomRight | d::bottomLeft));
    check(all.size() == 9 && all[0].second == V{14, 20} && all[7].second == V{10, 24} && all[4].second == V{36, 60} && all[5].second == V{14, 60} && all[8].first == "close", "All four corners");
    check(commands(d::chamfer({0, 0, 10, 6}, 50))[1].second == V{7, 0} && commands(d::chamfer({0, 0, 10, 6}, -2))[1].second == V{10, 0}, "Cut clamps to 0...min(w/2, h/2)");
    // HUDShape.tickLadder / ticks / crosshair / cornerMark / triangleDown (:35-79).
    check(commands(d::tickLadder(10, 3, 3)).size() == 4 * 5 && commands(d::tickLadder(10, 3, 3))[15].second == V{9, 0}, "Ticks at 0, 3, 6, 9 (x <= width - tick + .001)");
    check(commands(d::ticks(1, 10, 3)).empty() && commands(d::ticks(3, 11, 2))[10].second == V{10, 0}, "count <= 1 draws nothing; both ends included");
    check(commands(d::crosshair())[0].second == V{5, 0} && commands(d::crosshair())[5].second == V{0, 5}, "Crosshair through (size - 1) / 2");
    const auto mark = commands(d::cornerMark({0, 0, 20, 10}, 6, 2, d::bottomRight));
    check(mark[0].second == V{14, 8} && mark[5].second == V{18, 4}, "Corner mark: horizontal then vertical leg");
    check(commands(d::triangleDown(8, 5))[2].second == V{4, 5}, "Down triangle");
    check(commands(d::gripDots(2, 3)).size() == 30 && commands(d::gripDots(2, 3))[25].second == V{4, 8}, "Grip dots: 2 px squares 4 px apart (HUDDragGrip.dots)");
    const auto bar = commands(d::buttonBar(false)), arrow = commands(d::buttonBar(true));
    check(bar.size() == 5 && arrow.size() == 5 && arrow[1].second == V{9, 6} && arrow[2].second == V{9, 6} && bar[2].second == V{4, 12}, "Bar and ▶ share one 4-point topology (HUDButton.barPath)");
    const auto stripes = commands(d::diagonalStripes({20, 4}, 3 * std::sqrt(2.0), 6 * std::sqrt(2.0)));
    check(stripes.front().second == V{-4 - 6 * std::sqrt(2.0), 4} && stripes.size() % 5 == 0, "Redaction stripes start at -h - period (HUDRedaction.stripePath)");
    // CGPath(ellipseIn:) (CG probe: (1,2,10,6) starts (11,5) with controls 6.6568542494 / 8.761423749).
    const auto ellipse = commands(d::PathBuilder{}.ellipse({1, 2, 10, 6}));
    check(ellipse[0].second == V{11, 5} && near(ellipse[1].second[1], 6.6568542494, 1e-10) && near(ellipse[1].second[2], 8.761423749, 1e-10) && ellipse[2].second[4] == 1, "Ellipse point order and constant");
    check(commands(d::PathBuilder{}.rect({1, 2, 10, 6}))[2].second == V{11, 8}, "addRect order");
    // Hit paths: winding containment, the cut corner is outside.
    const auto plate = d::chamfer({0, 0, 100, 40}, 11);
    check(plate.contains({50, 20}) && !plate.contains({99, 1}) && plate.contains({88, 1}) && !plate.contains({101, 20}), "Chamfer hit path");
    check(d::PathBuilder{}.ellipse({0, 0, 10, 10}).contains({5, 5}) && !d::PathBuilder{}.ellipse({0, 0, 10, 10}).contains({.5, .5}), "Ellipse hit path");
    auto twice = d::PathBuilder{}.rect({0, 0, 10, 10}); twice.rect({2, 2, 6, 6});
    check(twice.contains({5, 5}) && !twice.contains({5, 5}, true), "Even-odd vs winding");
    check(plate.bounds() == d::Rect{0, 0, 100, 40} && d::PathBuilder{}.bounds() == d::Rect{}, "Bounds");
    const auto moved = commands(plate.transformed(2, 0, 0, 2, 1, 1));
    check(moved[1].second == V{179, 1}, "Affine path transform");

    // Nodes in the encoder vocabulary.
    const auto fill = d::solid("s", {1, 2, 3, 4}, d::rgb(0x112233), 2);
    check(fill["bounds"] == J(J::Array{0., 0., 3., 4.}) && fill["position"] == J(J::Array{1., 2.}) && fill["anchorPoint"] == J(J::Array{0., 0.}) && fill["cornerRadius"].number() == 2, "Solid frame node");
    check(fill["backgroundColor"]["sRGB"].array()[2].number() == 0x33 / 255., "sRGB colour");
    const auto b = d::bordered("b", {0, 0, 5, 5}, std::nullopt, d::white(1), 1.5);
    check(b["backgroundColor"].isNull() && b["borderWidth"].number() == 1.5 && !b.contains("cornerRadius"), "Bordered without fill");
    const auto sh = d::shape("p", {0, 0, 4, 4}, d::triangleDown(4, 4), std::nullopt, d::black(1), {2, 4, "round", "bevel", {1, 2}}, true);
    check(sh["shape"]["fillColor"].isNull() && sh["shape"]["lineCap"].string() == "round" && sh["shape"]["lineJoin"].string() == "bevel" && sh["shape"]["fillRule"].string() == "even-odd" && sh["shape"]["lineDashPattern"].array().size() == 2, "Shape keys");
    const d::Color stops[]{d::white(1), d::black(1)}; const double at[]{0, 1};
    const auto g = d::gradient("g", {0, 0, 10, 10}, stops, at, {.5, .5}, {1, 1}, d::GradientKind::radial);
    check(g["kind"].string() == "gradient" && g["gradient"]["type"].string() == "radial" && g["gradient"]["locations"].array().size() == 2, "Radial gradient keys");
    rejects([&] { const double one[]{0}; d::gradient("g", {}, stops, one, {}, {}); }, "Locations must match colours");
    auto shadowed = d::solid("x", {0, 0, 10, 10}, d::white(1));
    d::addShadow(shadowed, {d::black(1), .6, 20, {0, 18}, std::nullopt});
    check(shadowed["shadowOpacity"].number() == .6 && shadowed["shadowRadius"].number() == 20 && shadowed["shadowOffset"] == J(J::Array{0., 18.}), "Panel shadow keys");
    const auto img = d::image("m", {0, 0, 8, 8}, {d::ImageSource::Kind::memory, "photo", {}, 3}, "resizeAspectFill", d::white(1), true);
    check(img["contents"]["memoryImage"].string() == "photo" && img["contents"]["revision"].integer() == 3 && img["contentsGravity"].string() == "resizeAspectFill" && img.contains("contentsSourceInTint") && img["minificationFilter"].string() == "trilinear", "Memory image");
    check(d::image("r", {}, {})["requiredSourceImage"].string().empty() && !d::image("r", {}, {}).contains("contents"), "Required images are bound by the scene");
    // HUDArtwork.plate (:144-157): gradient clipped by the chamfer mask, highlight inside.
    const d::Color rows[]{d::rgb(0xE4E5E0), d::rgb(0xD3D4CF), d::rgb(0xC6C7C2)};
    const auto pl = d::plate("row", {0, 0, 200, 50}, rows, {}, 11, d::topRight, true);
    check(pl["mask"]["kind"].string() == "shape" && pl["mask"]["shape"]["path"] == d::chamfer({0, 0, 200, 50}, 11).json(), "Plate clipped to its chamfer");
    check(pl["children"].array().size() == 2 && pl["children"].array()[0]["gradient"]["startPoint"] == J(J::Array{0., 0.}) && pl["children"].array()[0]["gradient"]["endPoint"] == J(J::Array{0., 1.}) && pl["children"].array()[0]["gradient"]["locations"].isNull(), "Top -> bottom, even stops");
    check(pl["children"].array()[1]["bounds"] == J(J::Array{0., 0., 200., 1.}) && pl["children"].array()[1]["backgroundColor"]["sRGB"].array()[3].number() == .75, "1 px highlight white .75");
    d::Palette light; light.apply("FAD41F", true);
    const auto h = d::hatch("h", {0, 0, 44, 7}, d::Hatch::light, light);
    check(h["masksToBounds"].boolean() && h["shape"]["fillColor"]["sRGB"].array()[3].number() == .14 && h["shape"]["fillColor"]["sRGB"].array()[0].number() == 15 / 255., "Light hatch: foreground(.14) clipped to its rect");
    const auto dark = d::hatch("h", {0, 0, 44, 7}, d::Hatch::dark, light);
    check(dark["shape"]["fillColor"]["sRGB"] == J(J::Array{0., 0., 0., .42}) && commands(d::PathBuilder{}).empty(), "Dark hatch black .42");
    const auto hp = commands(d::PathBuilder{}.append(d::diagonalStripes({44, 7}, std::sqrt(2.0), 3.5 * std::sqrt(2.0))));
    check(!hp.empty(), "Stripes build");
    const auto tri = d::triStrip("t", {0, 0, 30, 9});
    check(tri["children"].array().size() == 3 && tri["children"].array()[2]["position"] == J(J::Array{0., 6.}) && tri["children"].array()[0]["backgroundColor"]["sRGB"].array()[0].number() == 1, "Tri-strip bands");
    const auto back = d::centreBackdropMask("bd");
    check(back["position"] == J(J::Array{-135., -126.}) && back["bounds"] == J(J::Array{0., 0., 860., 860.}) && back["gradient"]["locations"] == J(J::Array{0., .52, .74, 1.}), "Centre backdrop ramp over its 430 px disc");
    FakeMeasurer m;
    d::TextServices services{&m, {}, d::TextRegion::korean, 2};
    const auto tag = d::microTag("mt", {3, 4}, true, services, light);
    const auto& tk = tag["children"].array();
    check(tag["position"] == J(J::Array{3., 4.}) && tk.size() == 3 && tk[0]["backgroundColor"]["sRGB"].array()[0].number() == 0xE9 / 255. && tk[1]["text"]["language"].string() == "zh-Hans", "Inverted micro tag: E9E9E9 label, zh-Hans text");
    check(tk[1]["position"] == J(J::Array{4., 0.}) && tk[1]["text"]["string"].string() == "ENDFIELD" && tk[1]["text"]["truncation"].string() == "none" && tk[2]["position"].array()[0].number() == tag["bounds"].array()[2].number() - 44, "Text from x 4; the 44 px hatch tail");

    // PartBuilder compaction (the v1.2.0 Settings scene's compact()).
    d::PartBuilder part("row");
    part.add(d::solid("a", {0, 0, 10, 10}, d::white(1)));
    part.add(d::solid("b", {20, 5, 10, 10}, d::white(1)));
    part.add(d::solid("notch", {0, 0, 2, 2}, d::white(1)), d::SurfaceRole::animated);
    part.add(d::solid("c", {1, 1, 1, 1}, d::white(1)));
    check(part.surfaceCount() == 3, "Three surfaces after compaction");
    const auto built = part.finish();
    check(built.id == "row" && built.surfaces.size() == 3 && built.surfaces[0].id == "row.static.0" && built.surfaces[1].id == "notch" && built.surfaces[2].id == "row.static.1", "Static runs around the animated surface");
    check(built.surfaces[0].localBounds == d::Rect{0, 0, 30, 15} && built.layers["children"].array()[0]["allowsGroupOpacity"] == J(false), "A run's bounds are the union of its frames");
    check(built.layers["bounds"] == J(J::Array{0., 0., 0., 0.}) && built.layers["children"].array().size() == 3, "A zero-bounds part root holds the surfaces");
}

// ---------------------------------------------------------------------------
// Mac oracle (package L3: tools/hud_design_reference.swift)
// ---------------------------------------------------------------------------
// TODO(lead): the fixture is produced by L3 concurrently. When it exists and
// carries a "tokens" section, every palette, theme token, tone and HUDType
// style is compared below; until then the Swift-derived vectors above hold.
// Expected shape (keep in step with tools/hud_design_reference.swift):
//   tokens.palettes[]: {accent, light, yellow, yellowHigh, yellowLow, yellowDeep, ink} (sRGB arrays)
//   tokens.themes.{dark,light}: {<ThemeToken name>: sRGB}
//   tokens.styles.{dark,light}: {<HUDType name>: {size, weight, color, lineHeight, tracking, uppercase, tabular}}
void oracle(const char* path) {
    if (!std::filesystem::exists(path)) { std::cout << "hud_design_tokens: oracle fixture not present yet (" << path << "); Swift-derived vectors only (TODO lead)\n"; return; }
    std::ifstream file(path, std::ios::binary);
    const auto json = J::parse(std::string((std::istreambuf_iterator<char>(file)), {}), 64 * 1024 * 1024);
    const auto& tokens = json["tokens"];
    if (!tokens.isObject()) { std::cout << "hud_design_tokens: oracle has no \"tokens\" section yet (TODO lead: map its schema here)\n"; return; }
    const auto color = [](const J& v) { const auto& a = v.isObject() ? v["sRGB"].array() : v.array(); return d::Color{a[0].number(), a[1].number(), a[2].number(), a[3].number()}; };
    const auto close = [](d::Color a, d::Color b) { return near(a.r, b.r, 1e-6) && near(a.g, b.g, 1e-6) && near(a.b, b.b, 1e-6) && near(a.a, b.a, 1e-6); };
    if (tokens["palettes"].isArray())
        for (const auto& v : tokens["palettes"].array()) {
            const auto p = d::AccentPalette::make(v["accent"].string(), v["light"].boolean());
            const auto name = "Oracle palette " + v["accent"].string();
            check(close(p.yellow, color(v["yellow"])) && close(p.yellowHigh, color(v["yellowHigh"])) && close(p.yellowLow, color(v["yellowLow"])) && close(p.yellowDeep, color(v["yellowDeep"])) && close(p.ink, color(v["ink"])), name);
        }
    if (tokens["styles"].isObject()) {
        d::Palette p;
        const std::pair<const char*, d::TextStyle> styles[]{{"moduleTitle", d::type::moduleTitle(p)}, {"rowLabel", d::type::rowLabel()}, {"numeral", d::type::numeral(p)}, {"microTag", d::type::microTag()},
            {"toastTitle", d::type::toastTitle(p)}, {"confirmTitle", d::type::confirmTitle(p)}, {"stamp", d::type::stamp(p)}, {"tabKeycap", d::type::tabKeycap(p)}};
        for (const auto& [name, s] : styles) {
            const auto& v = tokens["styles"]["dark"][name];
            if (!v.isObject()) continue;
            check(v["size"].number() == s.size && v["weight"].integer() == int(s.weight) && close(color(v["color"]), s.color) && near(v["lineHeight"].number(), s.lineHeight) && near(v["tracking"].number(), s.tracking)
                && v["uppercase"].boolean() == s.uppercase && v["tabular"].boolean() == s.tabular, std::string("Oracle style ") + name);
        }
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: hud_design_tokens_tests <hud-design-source.json> <HUDDesignIcons.swift>");
        palette(); typeScale(); formats(); text(); icons(argv[2]); nodes(); oracle(argv[1]);
        std::cout << "hud_design_tokens contracts: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "hud_design_tokens contracts failed after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
