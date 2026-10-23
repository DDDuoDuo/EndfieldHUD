// Contract suite of native/hud_design_text_metrics: HUDText.width and
// HUDText.metrics over the shared rasterizer's paint resolver (one line,
// trailing tracking, .5 px rounding, POSIX uppercase, the primary-face strut,
// the cache and its flush on a font event), the shared ICU uppercase and the
// preferred-language region. When the Mac oracle fixture exists (L3,
// tests/fixtures/hud-design-source.json) every recorded designText width is
// compared within the rasterizer tolerance (DirectWrite vs CoreText,
// LIBRARY-DESIGN.md section 17). No window, device or user data.
#include "native/hud_design_text_metrics.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace endfield;
using namespace native;
namespace d = modules::design;
using Json = ehud::data::Json;
namespace {
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
d::TextStyle style(double size, d::FontWeight weight = d::FontWeight::regular, double tracking = 0, bool uppercase = false) {
    d::TextStyle s; s.size = size; s.weight = weight; s.tracking = tracking; s.uppercase = uppercase; s.lineHeight = 1.2; s.color = d::white(1);
    return s;
}
double width(NativeDesignTextMeasurer& m, std::string text, const d::TextStyle& s, d::TextRegion r = d::TextRegion::simplifiedChinese) {
    const d::TextRun run{std::move(text), s};
    return m.width(std::span<const d::TextRun>(&run, 1), r);
}
// The oracle's designText layers (tools/hud_design_reference.swift): kind
// "designText" with a `designText` record of runs {text, style {size,
// weight, tracking, uppercase, tabular, lineHeight}}, resolvedLanguage and
// preferredWidth (HUDTextLayer.preferredWidth, CoreText).
std::size_t outliers{};
void oracle(NativeDesignTextMeasurer& m, const Json& node, std::size_t& compared, double& worst, unsigned depth = 0) {
    if (depth > 256) return;
    if (node.isArray()) { for (const auto& c : node.array()) oracle(m, c, compared, worst, depth + 1); return; }
    if (!node.isObject()) return;
    const auto& record = node["designText"];
    if (node["kind"].isString() && node["kind"].string() == "designText" && record.isObject() && record["preferredWidth"].isNumber() && record["runs"].isArray()) {
        std::vector<d::TextRun> runs;
        for (const auto& r : record["runs"].array()) {
            const auto& st = r["style"];
            if (!r["text"].isString() || !st.isObject() || !st["size"].isNumber()) { runs.clear(); break; }
            auto s = style(st["size"].number(), d::FontWeight(st["weight"].isNumber() ? unsigned(st["weight"].number()) : 400u), st["tracking"].isNumber() ? st["tracking"].number() : 0,
                st["uppercase"].isBool() && st["uppercase"].boolean());
            s.tabular = st["tabular"].isBool() && st["tabular"].boolean();
            if (st["lineHeight"].isNumber()) s.lineHeight = st["lineHeight"].number();
            runs.push_back({r["text"].string(), s});
        }
        const auto region = record["resolvedLanguage"].isString() ? d::regionFromTag(record["resolvedLanguage"].string()) : std::nullopt;
        if (!runs.empty()) {
            const auto measured = m.width(runs, region.value_or(d::TextRegion::simplifiedChinese));
            const auto expected = record["preferredWidth"].number();
            worst = std::max(worst, std::abs(measured - expected) / std::max(1., expected));
            ++compared;
            std::string text;
            for (const auto& r : runs) text += r.text;
            // Glyphs outside the bundled faces (the keycaps' ⌃ ⌥ ⌘ ⇧, U+2300-U+23FF
            // and arrows) come from each system's symbol fallback (Apple Symbols /
            // Segoe UI Symbol): reported, not counted (recorded gap).
            const bool symbol = text.find("\xE2\x8C") != std::string::npos || text.find("\xE2\x87") != std::string::npos;
            if (symbol && std::abs(measured - expected) > std::max(2., expected * .06)) std::cout << "  symbol fallback width: \"" << text << "\" Mac " << expected << " Windows " << measured << "\n";
            else if (std::abs(measured - expected) > std::max(2., expected * .06)) {
                if (++outliers <= 20) std::cout << "  width outlier: \"" << text << "\" " << record["resolvedLanguage"].string() << " size " << runs[0].style.size << " Mac " << expected << " Windows " << measured << "\n";
            }
        }
    }
    for (const auto& [key, value] : node.object()) { (void)key; if (value.isObject() || value.isArray()) oracle(m, value, compared, worst, depth + 1); }
}
void run(const std::filesystem::path& fixture) {
    LayerRasterizer raster;
    NativeDesignTextMeasurer m(raster);
    // HUDText.width: .5 px steps, empty is zero, larger type is wider.
    const auto title = width(m, "Clipboard", style(26, d::FontWeight::bold, .08, true));
    const auto body = width(m, "Clipboard", style(13));
    check(title > body && body > 0 && std::fmod(title * 2, 1) == 0 && std::fmod(body * 2, 1) == 0, "Widths round up to .5 px and grow with the style");
    check(width(m, "", style(13)) == 0, "An empty line has no width");
    // Uppercase is the display rule (POSIX): authored case does not matter.
    check(width(m, "clipboard", style(26, d::FontWeight::bold, .08, true)) == title, "Uppercase styles measure their displayed text");
    // Tracking applies after every character, the last one included.
    const auto plain = width(m, "ABCD", style(20)), tracked = width(m, "ABCD", style(20, d::FontWeight::regular, .1));
    check(std::abs((tracked - plain) - 4 * 2.) <= 1, "Tracking adds size x em after every character, the trailing one included");
    // CJK and regions.
    check(width(m, "剪贴板缓存", style(13), d::TextRegion::simplifiedChinese) > 0 && width(m, "クリップボード", style(13), d::TextRegion::japanese) > 0, "CJK text measures in its region's face");
    // The cache: equal requests hit; a font event flushes.
    const auto before = m.stats();
    (void)width(m, "Clipboard", style(13));
    check(m.stats().cacheHits == before.cacheHits + 1 && m.stats().measurements == before.measurements, "Equal requests come from the cache");
    // HUDText.metrics: the CSS line box with the primary face's strut.
    const auto lines = m.metrics(style(15.5, d::FontWeight::medium), d::TextRegion::simplifiedChinese);
    check(lines.height == 15.5 * 1.2 && lines.ascent > 0 && lines.descent > 0 && lines.ascent + lines.descent < lines.height * 1.2, "Line metrics are the strut of the primary face");
    const auto scaled = m.metrics(style(15.5, d::FontWeight::medium), d::TextRegion::simplifiedChinese, 2);
    check(std::abs(scaled.ascent - 2 * lines.ascent) < .01 && scaled.height == 2 * lines.height, "Metrics scale with the render scale");
    const auto korean = m.metrics(style(15.5, d::FontWeight::medium), d::TextRegion::korean);
    check(korean.ascent == lines.ascent && korean.descent == lines.descent, "A CJK fallback never changes the strut");
    check(raster.setDefaultFontLanguage(raster.fontLanguage() == LayerFontLanguage::korean ? LayerFontLanguage::simplifiedChinese : LayerFontLanguage::korean), "A font event");
    (void)width(m, "Clipboard", style(13));
    check(m.stats().flushes == 1 && m.stats().entries == 1, "A font revision flushes the cache");
    check(raster.stats().entries == 0, "Measuring creates no raster entry");
    // The shared ICU uppercase (en_US_POSIX: no dotted capital I).
    const auto upper = nativeUppercase();
    check(upper("straße") == "STRASSE" && upper("iı") == "II" && upper("é") == "É", "Uppercase maps fully without the UI locale");
    const auto characters = nativeCharacterRules();
    check(characters.count("e\u0301\U0001F1EF\U0001F1F5a") == 3 && characters.prefix("e\u0301bc", 2) == "e\u0301b" && characters.dropFirst("e\u0301bc", 1) == "bc",
        "Characters are ICU grapheme clusters (Profile's rules)");
    std::string longText(40000, 'x'); longText += "e\u0301";
    check(characters.count(longText) == 40001 && characters.prefix(longText, 40000) == std::string(40000, 'x') && characters.dropFirst(longText, 40000) == "e\u0301",
        "Long text (beyond Profile's 32 KiB payload) keeps grapheme semantics");
    const auto region = preferredSystemRegion();
    check(region == d::TextRegion::simplifiedChinese || region == d::TextRegion::traditionalChinese || region == d::TextRegion::japanese || region == d::TextRegion::korean, "A CJK region always");
    // The Mac oracle, when present.
    if (std::filesystem::exists(fixture)) {
        std::ifstream in(fixture, std::ios::binary);
        const auto json = Json::parse(std::string{std::istreambuf_iterator<char>(in), {}}, 64 * 1024 * 1024);
        std::size_t compared{}; double worst{};
        oracle(m, json, compared, worst);
        std::cout << "oracle widths compared: " << compared << " (worst relative difference " << worst << ")\n";
        check(compared > 100, "The oracle records designText widths");
        check(outliers == 0, "Every designText width agrees with the Mac within max(2 px, 6 %)");
    } else std::cout << "oracle fixture absent: recorded widths not compared\n";
}
}
int wmain(int argc, wchar_t** argv) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(com) && argc == 2, "Pass the oracle fixture path");
        run(argv[1]);
        CoUninitialize();
        std::cout << "HUD design text metrics: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(com)) CoUninitialize();
        std::cerr << "HUD design text metrics after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
