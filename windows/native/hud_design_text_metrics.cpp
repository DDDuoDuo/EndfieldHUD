#include "native/hud_design_text_metrics.hpp"
#ifdef _WIN32
#include "native/profile_text.hpp"
#include <cmath>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <vector>

namespace endfield::native {
namespace {
namespace d = modules::design;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
// The measurement descriptor's id; never a raster entry (measureSourceText
// creates no image or cache entry).
constexpr const char* measureID = "hud.design.measure";
// A one-line box wide enough for any caption (the natural width is returned).
constexpr double measureWidth = 1e6;
void append(std::string& key, double v) { char bytes[sizeof v]; std::memcpy(bytes, &v, sizeof v); key.append(bytes, sizeof v); }
void append(std::string& key, const d::TextStyle& s) {
    append(key, s.size); append(key, s.lineHeight); append(key, s.tracking);
    key.push_back(char(unsigned(d::resolved(s.weight)) / 100)); key.push_back(char(s.uppercase)); key.push_back(char(s.tabular));
}
}
struct NativeDesignTextMeasurer::Impl {
    LayerRasterizer& raster;
    d::TextCasing uppercase{nativeUppercase()};
    std::uint64_t font{};
    std::unordered_map<std::string, double> widths;
    std::unordered_map<std::string, d::LineMetrics> lines;
    std::deque<std::pair<std::string, bool>> order; // insertion order (true: a width)
    Stats stats;
    explicit Impl(LayerRasterizer& r) : raster(r), font(r.fontRevision()) {}
    // A language or font event repaints every text: the cache follows it.
    void revision() {
        const auto current = raster.fontRevision();
        if (current == font) return;
        font = current; widths.clear(); lines.clear(); order.clear(); ++stats.flushes;
    }
    void remember(std::string key, bool width) {
        order.emplace_back(std::move(key), width);
        if (order.size() <= limit) return;
        // Bounded: the oldest half goes.
        for (std::size_t n = order.size() / 2; n--;) { auto& [k, w] = order.front(); if (w) widths.erase(k); else lines.erase(k); order.pop_front(); }
    }
    // The exact descriptor the leaf is painted from (modules::design::textNode):
    // one line, no truncation, its region, the standard faces.
    // A line count (lineCount) breaks in `width` with the line budget and the
    // last line's truncation of the painted block.
    LayerSourceTextMeasurement measure(std::vector<d::TextRun> runs, d::TextRegion region, double width = measureWidth, int maximumLines = 1) {
        d::TextBlock block;
        block.runs = std::move(runs); block.language = region; block.maximumLines = maximumLines; block.truncates = width != measureWidth; block.minimumScale = 1;
        double height{};
        for (const auto& run : block.runs) height = std::max(height, run.style.size * std::max(1., run.style.lineHeight));
        block.setLineFrame({0, 0, width, std::max(1., std::ceil(height * std::max(1, maximumLines)))});
        d::TextServices services;
        services.uppercase = uppercase; services.uiRegion = region; services.boxScale = 1;
        const auto node = d::textNode(measureID, block, services);
        need(node["text"].isObject(), "Design text node has no text descriptor");
        ++stats.measurements;
        return raster.measureSourceText(measureID, node["text"], width, {});
    }
};
NativeDesignTextMeasurer::NativeDesignTextMeasurer(LayerRasterizer& r) : impl_(std::make_unique<Impl>(r)) {}
NativeDesignTextMeasurer::~NativeDesignTextMeasurer() = default;
// HUDText.width: the typographic advance of one line with the tracking after
// the last glyph, rounded up to .5 px (the rasterizer's natural width of a
// line box is exactly that).
double NativeDesignTextMeasurer::width(std::span<const d::TextRun> runs, d::TextRegion region) {
    auto& i = *impl_;
    i.revision();
    std::string key(1, char(region));
    bool empty = true;
    for (const auto& run : runs) {
        need(std::isfinite(run.style.size) && run.style.size > 0 && std::isfinite(run.style.tracking) && std::isfinite(run.style.lineHeight), "Invalid design text style");
        empty &= run.text.empty();
        key.append(run.text); key.push_back('\x1f'); append(key, run.style); key.push_back('\x1e');
    }
    if (empty) return 0;
    if (const auto it = i.widths.find(key); it != i.widths.end()) { ++i.stats.cacheHits; return it->second; }
    std::vector<d::TextRun> kept;
    for (const auto& run : runs) if (!run.text.empty()) kept.push_back(run);
    const auto result = i.measure(std::move(kept), region).width;
    need(std::isfinite(result) && result >= 0, "Invalid measured design text width");
    i.widths.emplace(key, result);
    i.remember(std::move(key), true);
    return result;
}
// HUDText.lines(...).count: the raster's own breaking of the same block in
// `width` (cached with the widths).
std::size_t NativeDesignTextMeasurer::lineCount(std::span<const d::TextRun> runs, d::TextRegion region, double width, int maximumLines) {
    auto& i = *impl_;
    need(std::isfinite(width) && width > 0 && width < measureWidth && maximumLines >= 0, "Invalid design line count request");
    i.revision();
    std::string key(1, char(region));
    key.push_back('\x1d'); append(key, width); append(key, double(maximumLines));
    std::vector<d::TextRun> kept;
    for (const auto& run : runs) {
        need(std::isfinite(run.style.size) && run.style.size > 0 && std::isfinite(run.style.tracking) && std::isfinite(run.style.lineHeight), "Invalid design text style");
        if (run.text.empty()) continue;
        key.append(run.text); key.push_back('\x1f'); append(key, run.style); key.push_back('\x1e');
        kept.push_back(run);
    }
    if (kept.empty()) return 0;
    if (const auto it = i.widths.find(key); it != i.widths.end()) { ++i.stats.cacheHits; return std::size_t(it->second); }
    const auto result = std::max<std::size_t>(1, i.measure(std::move(kept), region, width, maximumLines).lines);
    i.widths.emplace(key, double(result));
    i.remember(std::move(key), true);
    return result;
}
// HUDText.metrics: the CSS line box (size x scale x lineHeight) with the
// PRIMARY face's ascent and descent at size x scale (the strut).
d::LineMetrics NativeDesignTextMeasurer::metrics(const d::TextStyle& style, d::TextRegion region, double scale) {
    auto& i = *impl_;
    need(std::isfinite(style.size) && style.size > 0 && std::isfinite(scale) && scale > 0 && std::isfinite(style.lineHeight), "Invalid design text metrics request");
    i.revision();
    std::string key(1, char(region));
    append(key, style); append(key, scale);
    if (const auto it = i.lines.find(key); it != i.lines.end()) { ++i.stats.cacheHits; return it->second; }
    const auto measured = i.measure({d::TextRun{std::string{}, style.sized(style.size * scale)}}, region);
    const d::LineMetrics result{style.size * scale * style.lineHeight, measured.ascent, measured.descent};
    need(std::isfinite(result.ascent) && std::isfinite(result.descent), "Invalid measured design line metrics");
    i.lines.emplace(key, result);
    i.remember(std::move(key), false);
    return result;
}
NativeDesignTextMeasurer::Stats NativeDesignTextMeasurer::stats() const noexcept {
    auto s = impl_->stats;
    s.entries = impl_->widths.size() + impl_->lines.size();
    return s;
}
// String.uppercased() with en_US_POSIX: ICU's root-locale full mapping, the
// rule Profile already applies (native/profile_text.cpp, shared, not copied).
d::TextCasing nativeUppercase() { return nativeProfileTextRules().uppercase; }
d::CharacterRules nativeCharacterRules() {
    return {[](std::string_view s) { return graphemeCount(s); }, [](std::string_view s, std::size_t n) { return std::string(graphemePrefix(s, n)); }};
}
} // namespace endfield::native
#endif
