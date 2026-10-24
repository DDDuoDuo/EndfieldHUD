#include "modules/archive_rich_text.hpp"
#include "modules/hud_design_tokens.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace endfield::modules::archive_rich_text {
namespace {
using core::notes::RichText;
using core::notes::TextRun;
// The style a UTF-16 index draws in (sparse runs: omitted ranges use TextStyle()).
TextStyle styleAt(std::span<const TextRun> runs, std::uint32_t index) {
    for (const auto& run : runs) if (index >= run.location && index < run.location + run.length) return run.style;
    return {};
}
}

std::vector<std::string> families(const ArchiveFontServices& fonts) {
    std::vector<std::string> names{std::string(defaultFamily)};
    std::set<std::string, std::less<>> seen{std::string(defaultFamily)};
    for (const auto& family : fonts.bundledCJKFamilies) if (seen.insert(family).second) names.push_back(family);
    if (fonts.installedFamilies) {
        auto installed = fonts.installedFamilies();
        std::sort(installed.begin(), installed.end());
        for (auto& family : installed) if (!family.empty() && family.front() != '.' && seen.insert(family).second) names.push_back(std::move(family));
    }
    return names;
}

std::uint32_t hex(const std::optional<RGBA>& color) noexcept {
    if (!color) return inkHex;
    const auto channel = [](double v) { return std::uint32_t(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
    return channel(color->red) << 16 | channel(color->green) << 8 | channel(color->blue);
}

bool isInk(const RGBA& color) noexcept {
    if (!(color.alpha > .996)) return false;
    for (const design::Hex ink : {design::Hex(inkHex), design::lightTone(inkHex)}) {
        const double channels[]{double(ink >> 16 & 255) / 255, double(ink >> 8 & 255) / 255, double(ink & 255) / 255};
        if (std::abs(color.red - channels[0]) < .004 && std::abs(color.green - channels[1]) < .004 && std::abs(color.blue - channels[2]) < .004) return true;
    }
    return false;
}

std::string family(const TextStyle& style, const ArchiveFontServices& fonts) {
    if (!style.fontName || style.fontName->empty() || style.fontName->front() == '.' || !fonts.familyOf) return std::string(defaultFamily);
    return fonts.familyOf(*style.fontName).value_or(std::string(defaultFamily));
}

RichText loaded(std::u16string_view text, const std::optional<RichText>& rich, const TextStyle& base) {
    RichText result;
    if (text.empty()) return result;
    const auto length = std::uint32_t(text.size());
    if (!rich || !rich->validFor(text)) { result.runs.push_back({0, length, base, {}}); return result; }
    result.extra = rich->extra;
    std::uint32_t at = 0;
    for (const auto& run : rich->runs) {
        if (run.location > at) result.runs.push_back({at, run.location - at, base, {}});
        result.runs.push_back(run);
        at = run.location + run.length;
    }
    if (at < length) result.runs.push_back({at, length - at, base, {}});
    return result;
}

TextStyle typingStyle(const ArchiveEntry& entry) {
    auto style = entry.bodyStyle;
    if (entry.body.empty() && !entry.bodyRichText && style == TextStyle{}) style.fontSize = newDocumentSize;
    return style;
}

TextStyle displayedStyle(const core::notes::RichDocument& document, bool focused) {
    const auto text = document.text();
    if (text.empty()) return document.typingStyle();
    const auto selection = document.selection().range;
    if (focused && selection.start == selection.end) return document.typingStyle();
    const core::text::Range range = focused && selection.end <= text.size() ? selection : core::text::Range{0, std::uint32_t(text.size())};
    const auto runs = document.runs();
    auto result = styleAt(runs, range.start);
    bool bold = true, italic = true, underline = true, strikethrough = true;
    // Every UTF-16 piece of the range: runs and the gaps between them.
    std::uint32_t at = range.start;
    const auto take = [&](const TextStyle& s) { bold = bold && s.bold; italic = italic && s.italic; underline = underline && s.underline; strikethrough = strikethrough && s.strikethrough; };
    for (const auto& run : runs) {
        const auto end = run.location + run.length;
        if (end <= at || run.location >= range.end) continue;
        if (run.location > at) take(TextStyle{});
        take(run.style);
        at = std::max(at, end);
        if (!bold && !italic && !underline && !strikethrough) break;
    }
    if (at < range.end) take(TextStyle{});
    result.bold = bold; result.italic = italic; result.underline = underline; result.strikethrough = strikethrough;
    return result;
}

core::notes::FormatChange stored(core::notes::FormatChange change) {
    switch (change.kind) {
    case core::notes::FormatKind::font: change.clears = change.clears || change.fontName == defaultFamily; break;
    case core::notes::FormatKind::size: change.fontSize = std::clamp(change.fontSize, 6.0, 144.0); break;
    case core::notes::FormatKind::color: change.clears = change.clears || isInk(change.color); break;
    default: break;
    }
    return change;
}

core::text::Range formatRange(const core::notes::RichDocument& document, bool focused) noexcept {
    return focused ? document.selection().range : core::text::Range{0, std::uint32_t(document.text().size())};
}
} // namespace endfield::modules::archive_rich_text
