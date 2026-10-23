// Centre text model (Sources/HUDDesignText.swift HUDText / HUDTextLayer,
// HUDDesignFonts.swift HUDTextLanguage, HUDDesignHeader.swift HUDModuleTitle).
// Decides runs, region, line box and line budget; measuring is the injected
// TextMeasurer's, painting the rasterizer's (A1 keys, native/layer_raster.hpp).
#include "modules/hud_design_nodes.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
// UTF-16 code units of a UTF-8 string (the rasterizer's run ranges, NSRange).
std::size_t utf16Length(std::string_view s) noexcept {
    std::size_t n{};
    for (const unsigned char c : s) if ((c & 0xC0) != 0x80) n += c >= 0xF0 ? 2 : 1;
    return n;
}
const char* alignmentName(TextAlignment a) noexcept { return a == TextAlignment::center ? "center" : a == TextAlignment::right ? "right" : "left"; }
}

std::string asciiUppercase(std::string_view s) {
    std::string out(s);
    for (auto& c : out) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return out;
}

double TextMeasurer::width(std::string_view text, const TextStyle& style, TextRegion region) {
    const TextRun run{std::string(text), style};
    return width(std::span(&run, 1), region);
}

// HUDTextLayer (:97-208) as a value.
TextBlock::TextBlock(std::string text, TextStyle style, TextAlignment a) : runs{TextRun{std::move(text), std::move(style)}}, alignment(a) {}
std::string TextBlock::text() const { std::string out; for (const auto& r : runs) out += r.text; return out; }
const TextStyle& TextBlock::style() const {
    static const TextStyle fallback = type::rowLabel(); // HUDTextLayer.textStyle without runs
    return runs.empty() ? fallback : runs.front().style;
}
void TextBlock::setText(std::string text) { const auto s = style(); runs = {TextRun{std::move(text), s}}; }
void TextBlock::setStyle(const TextStyle& s) {
    if (runs.empty()) runs = {TextRun{{}, s}};
    else for (auto& r : runs) r.style = s;
}
void TextBlock::setColor(Color c) { setStyle(style().colored(c)); }
// lineFrame didSet (:124-130): a change freezes the padding of the runs now.
void TextBlock::setLineFrame(Rect r) {
    if (r == lineFrame) return;
    framePadding.reset();
    framePadding = padding();
    lineFrame = r;
}
// HUDTextLayer.padding (:134-137): ceil(maxSize x .25) x ceil(maxSize x .45), 12 without runs.
Size TextBlock::padding() const noexcept {
    if (framePadding) return *framePadding;
    double size = runs.empty() ? 12 : 0;
    for (const auto& r : runs) size = std::max(size, r.style.size);
    return {std::ceil(size * .25), std::ceil(size * .45)};
}
Rect TextBlock::layerFrame() const noexcept {
    const auto p = padding();
    return {lineFrame.x - p.width, lineFrame.y - p.height, lineFrame.width + 2 * p.width, lineFrame.height + 2 * p.height};
}

TextRegion TextServices::region(const TextBlock& b) const noexcept { return b.language.value_or(uiRegion); }
// HUDText.displayed (:14-16): uppercase with en_US_POSIX.
std::string TextServices::displayed(const TextRun& r) const {
    if (!r.style.uppercase) return r.text;
    return uppercase ? uppercase(r.text) : asciiUppercase(r.text);
}
// HUDText.width (:38-46). The measurer receives the DISPLAYED runs (already
// uppercased, as the rasterizer paints them); empty runs are dropped like
// HUDText.attributed, and no text measures 0 without asking.
double TextServices::width(std::span<const TextRun> runs, std::optional<TextRegion> region) const {
    std::vector<TextRun> shown;
    for (const auto& r : runs) if (!r.text.empty()) shown.push_back({displayed(r), r.style});
    if (shown.empty()) return 0;
    if (!measurer) throw std::logic_error("Design text needs a measurer");
    return measurer->width(shown, region.value_or(uiRegion));
}
double TextServices::width(std::string_view text, const TextStyle& style, std::optional<TextRegion> region) const {
    const TextRun run{std::string(text), style};
    return width(std::span(&run, 1), region);
}
LineMetrics TextServices::metrics(const TextStyle& style, std::optional<TextRegion> region, double scale) const {
    if (!measurer) throw std::logic_error("Design text needs a measurer");
    return measurer->metrics(style, region.value_or(uiRegion), scale);
}
// HUDTextLayer.preferredWidth (:164).
double preferredWidth(const TextBlock& b, const TextServices& s) { return s.width(b.runs, s.region(b)); }

// The line-box leaf (see the header). Run attributes mirror HUDText.attributed
// (:22-34): size, resolved weight, tabular, tracking, language, colour.
Json textNode(std::string id, const TextBlock& b, const TextServices& s) {
    const auto& first = b.style();
    const auto tag = std::string(regionTag(s.region(b)));
    std::string string;
    Json::Array runs;
    std::size_t at{};
    for (const auto& r : b.runs) {
        if (r.text.empty()) continue;
        auto shown = s.displayed(r);
        const auto length = utf16Length(shown);
        runs.push_back(Json::Object{{"utf16Range", Json::Array{std::int64_t(at), std::int64_t(length)}},
            {"attributes", Json::Object{{"fontSize", r.style.size}, {"fontWeight", int(resolved(r.style.weight))},
                {"tabularFigures", r.style.tabular}, {"tracking", r.style.tracking}, {"NSLanguage", tag}, {"NSColor", color(r.style.color)}}}});
        at += length;
        string += shown;
    }
    if (!Json::validUtf8(string)) throw std::invalid_argument("Design text must be valid UTF-8");
    Json::Object text{{"string", std::move(string)}, {"fontSize", first.size}, {"fontWeight", int(resolved(first.weight))},
        {"tabularFigures", first.tabular}, {"tracking", first.tracking}, {"foregroundColor", color(first.color)},
        {"alignment", alignmentName(b.alignment)}, {"lineHeight", first.lineHeight}, {"maximumLines", b.maximumLines},
        {"minimumScale", b.minimumScale}, {"truncation", b.truncates ? "end" : "none"}, {"language", tag}, {"runs", std::move(runs)}};
    auto node = frameNode(std::move(id), b.lineFrame, "text");
    node["contentsScale"] = textContentsScale(s.boxScale);
    node["text"] = std::move(text);
    return node;
}

// HUDModuleTitle (HUDDesignHeader.swift:7-25, HUDDesignScreen.swift:655-661).
ModuleTitle ModuleTitle::make(std::string_view english, std::string_view chinese, core::Language language) {
    return {std::string(english), std::string(chinese), core::localized(english, chinese, language)};
}
ModuleTitle::Lines ModuleTitle::lines(core::Language resolved) const {
    const auto region = regionNaming(resolved);
    if (!region) return {english, std::nullopt, {}};
    return {localized, region, english};
}
} // namespace endfield::modules::design
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
