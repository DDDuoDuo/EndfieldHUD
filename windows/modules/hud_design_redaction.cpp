// Recording mode's redaction: the port of Sources/HUDDesignRedaction.swift
// (HUDRedaction :8-123). Bars are recognised for the entrance by their layer
// id suffix ".redaction.bar" (the Mac's layer name "redaction.bar").
// Characters are Swift Characters (Environment::characters, hud_design_control.hpp).
#include "modules/hud_design_redaction.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules::design::redaction {
namespace {
constexpr std::string_view barSuffix = ".redaction.bar";
std::string sub(std::string_view base, std::string_view name) { std::string s(base); s += '.'; s += name; return s; }
// labelStyle (:108): 8 bold palette yellow (the fill), tracking .2.
TextStyle labelStyle(const Palette& p) { return {8, FontWeight::bold, p.yellow(), 1, .2}; }
// CharacterSet.newlines: LF, VT, FF, CR, NEL, LS, PS (each one separates).
std::size_t newlineLength(std::string_view s, std::size_t at) noexcept {
    const auto c = static_cast<unsigned char>(s[at]);
    if (c >= 0x0A && c <= 0x0D) return 1;
    if (c == 0xC2 && at + 1 < s.size() && static_cast<unsigned char>(s[at + 1]) == 0x85) return 2;
    if (c == 0xE2 && at + 2 < s.size() && static_cast<unsigned char>(s[at + 1]) == 0x80) {
        const auto d = static_cast<unsigned char>(s[at + 2]);
        if (d == 0xA8 || d == 0xA9) return 3;
    }
    return 0;
}
}

// width(for:) (:14): min(260, max(44, characters x 7.2)).
double width(std::string_view text, const CharacterRules& rules) { return std::min(260.0, std::max(44.0, double(rules.count(text)) * 7.2)); }

// bar(size:labelled:fill:stripe:) (:18-39).
Layer& bar(Builder& b, std::string_view id, Size size, std::optional<bool> labelled, Color fillColor, Color stripeColor) {
    Layer& bar = b.layer(sub(id, "redaction.bar"));
    const Rect bounds{0, 0, std::max(1.0, size.width), std::max(1.0, size.height)};
    bar.setAnimatable();                                 // rdIn: a wipe from the left
    bar.setBounds({bounds.width, bounds.height});
    bar.setMasksToBounds(true);
    // The fill, the stripes, then the 1 px black border (a CALayer border draws above its sublayers).
    Json node = solid(bar.id(), bounds, fillColor);
    addChildren(node, {shape(bar.id() + ".stripes", bounds, diagonalStripes({bounds.width, bounds.height}, 3 * std::sqrt(2.0), 6 * std::sqrt(2.0)), stripeColor),
        bordered(bar.id() + ".border", bounds, std::nullopt, black(1), 1)});
    bar.setContent(std::move(node));
    if (labelled.value_or(bounds.width >= labelMinimumWidth)) {
        const auto style = labelStyle(b.env.palette);
        TextBlock label("[ " + core::localized("REDACTED", "已隐藏", b.env.language) + " ]", style, TextAlignment::center);
        label.truncates = false;
        label.setLineFrame({0, (bounds.height - style.size) / 2, bounds.width, style.size});
        Layer& text = b.layer(sub(id, "redaction.label"));
        text.setText(std::move(label));
        bar.add(text);
    }
    return bar;
}
// bar(replacing:style:width:alignment:) (:44-56).
Layer& bar(Builder& b, std::string_view id, Rect line, const TextStyle& style, double barWidth, TextAlignment alignment) {
    const Size size{std::min(barWidth, line.width), std::round(style.size * 1.1)};
    Layer& result = bar(b, id, size);
    double x = line.x;
    if (alignment == TextAlignment::center) x = line.x + line.width / 2 - size.width / 2;
    else if (alignment == TextAlignment::right) x = line.x + line.width - size.width;
    result.setOrigin({x, line.y + line.height / 2 - size.height / 2});
    return result;
}
// block (:61-76).
Layer& block(Builder& b, std::string_view id, std::string_view text, Size size) {
    Layer& block = b.layer(sub(id, "redaction.block"));
    const double maximumWidth = std::max(1.0, size.width - 28);
    block.setBounds(size);
    double y = 16;
    std::size_t index = 0;
    const auto& rules = b.env.characters;
    for (const auto& line : wrapped(text, 46, rules)) {
        if (y + 13 > size.height - 16 + .5) break;
        Layer& bar = redaction::bar(b, sub(id, "line." + std::to_string(index)),
            {std::min(maximumWidth, std::max(60.0, double(rules.count(line)) * 9.2)), 13}, index == 0);
        bar.setOrigin({14, y});
        block.add(bar);
        y += 13 + 9;
        ++index;
    }
    return block;
}
// playEntrance(in:) (:79-86).
void playEntrance(std::span<Layer* const> layers, const motion::Clock& clock) {
    if (clock.reduceMotion) return;
    const auto visit = [&](auto& self, Layer& layer) -> void {
        if (std::string_view(layer.id()).ends_with(barSuffix)) { motion::wipe(layer, WipeOrigin::left, entranceDuration, clock); return; }
        for (Layer* child : layer.children()) self(self, *child);
    };
    for (Layer* layer : layers) visit(visit, *layer);
}
// wrapped (:89-106): greedy, per newline-separated paragraph, words split on " ".
std::vector<std::string> wrapped(std::string_view text, std::size_t limit, const CharacterRules& rules) {
    std::vector<std::string> lines;
    const auto paragraph = [&](std::string_view part) {
        std::string line;
        std::size_t lineCount = 0;
        for (std::size_t start = 0;;) {
            const std::size_t space = part.find(' ', start);
            std::string_view word = part.substr(start, space == std::string_view::npos ? std::string_view::npos : space - start);
            std::size_t count = rules.count(word);
            while (count > limit) {
                if (!line.empty()) { lines.push_back(std::move(line)); line.clear(); lineCount = 0; }
                lines.push_back(rules.prefix(word, limit));
                word = rules.dropFirst(word, limit); count -= limit;
            }
            if (line.empty()) { line = std::string(word); lineCount = count; }
            else if (lineCount + 1 + count <= limit) { line += ' '; line += word; lineCount += 1 + count; }
            else { lines.push_back(std::move(line)); line = std::string(word); lineCount = count; }
            if (space == std::string_view::npos) break;
            start = space + 1;
        }
        lines.push_back(std::move(line));
    };
    std::size_t start = 0;
    for (std::size_t at = 0; at < text.size();) {
        if (const auto n = newlineLength(text, at)) { paragraph(text.substr(start, at - start)); at += n; start = at; }
        else ++at;
    }
    paragraph(text.substr(start));
    return lines;
}
} // namespace endfield::modules::design::redaction
