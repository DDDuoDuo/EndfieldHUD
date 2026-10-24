// App shortcut artwork (Sources/AppShortcutArtwork.swift, HUDSourceDesktopIconLayout in
// Sources/HUDSourceDesktopNavigationLayout.swift:9-58, AppShortcutCanvas.averageColor :296-314).
#include "modules/app_shortcut_artwork.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace endfield::modules {
namespace {
using ehud::data::Json;
void need(bool value, const char* why) { if (!value) throw std::invalid_argument(std::string("App shortcut artwork: ") + why); }
bool hex64(const std::string& s) { return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }); }
// AppShortcutIcon.allCases.
constexpr std::array<std::string_view, 14> presetOrder{"original", "bolt", "star", "terminal", "globe", "folder", "music", "play",
    "brush", "code", "game", "camera", "grid", "textBubble"};
}

// image(_:) (:41-57): alpha > 8 in the 96-pixel square, then the crop.
std::optional<TrimmedShortcutIcon> trimDesktopIcon(unsigned width, unsigned height, std::span<const std::uint8_t> rgba) {
    need(width == trimmedIconSide && height == trimmedIconSide && rgba.size() == std::size_t(width) * height * 4, "a 96 x 96 straight RGBA icon");
    unsigned minX = width, minY = height, maxX = 0, maxY = 0; bool any = false;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            if (rgba[(std::size_t(y) * width + x) * 4 + 3] > 8) { any = true; minX = std::min(minX, x); minY = std::min(minY, y); maxX = std::max(maxX, x); maxY = std::max(maxY, y); }
    if (!any) return std::nullopt;
    TrimmedShortcutIcon out; out.width = maxX - minX + 1; out.height = maxY - minY + 1;
    out.straightRGBA.reserve(std::size_t(out.width) * out.height * 4);
    for (unsigned y = minY; y <= maxY; ++y) {
        const auto row = rgba.subspan((std::size_t(y) * width + minX) * 4, std::size_t(out.width) * 4);
        out.straightRGBA.insert(out.straightRGBA.end(), row.begin(), row.end());
    }
    return out;
}

std::optional<std::array<double, 3>> shortcutTileColor(unsigned width, unsigned height, std::span<const std::uint8_t> rgba) {
    need(rgba.size() == std::size_t(width) * height * 4, "a straight RGBA icon");
    double r = 0, g = 0, b = 0, a = 0;
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        const double alpha = rgba[i + 3];
        r += rgba[i] * alpha; g += rgba[i + 1] * alpha; b += rgba[i + 2] * alpha; a += alpha;
    }
    if (!(a > 0)) return std::nullopt;
    // Premultiplied sums divided by coverage; then darkened to at most 55 % luminance.
    std::array<double, 3> rgb{r / a / 255 * .72, g / a / 255 * .72, b / a / 255 * .72};
    const double luminance = .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2];
    if (luminance > .55) for (auto& c : rgb) c *= .55 / luminance;
    return rgb;
}

AppShortcutArtwork::AppShortcutArtwork(const Json& j) {
    need(j.isObject() && j["version"].isNumber() && j["version"].integer() == 2 && j["presets"].isArray(), "version 2 with presets");
    const auto& rows = j["presets"].array();
    need(rows.size() == presetOrder.size(), "one row per AppShortcutIcon case");
    std::set<std::string> files;
    for (std::size_t n = 0; n < rows.size(); ++n) {
        const auto& row = rows[n];
        ShortcutPresetArtwork p;
        need(row["id"].isString() && row["id"].string() == presetOrder[n], "presets in AppShortcutIcon.allCases order");
        p.id = row["id"].string();
        need(row["english"].isString() && row["simplifiedChinese"].isString() && !row["english"].string().empty(), "title pairs");
        p.english = row["english"].string(); p.simplifiedChinese = row["simplifiedChinese"].string();
        need(row["file"].isString() && row["file"].string() == "presets/" + p.id + ".png" && files.insert(row["file"].string()).second, "one presets/<id>.png file");
        p.file = row["file"].string();
        need(row["sha256"].isString() && hex64(row["sha256"].string()), "a lowercase SHA-256");
        p.sha256 = row["sha256"].string();
        const auto& size = row["size"];
        need(size.isArray() && size.array().size() == 2 && size.array()[0].isNumber() && size.array()[1].isNumber(), "a pixel size");
        const auto w = size.array()[0].integer(), h = size.array()[1].integer();
        need(w > 0 && h > 0 && w <= 96 && h <= 96, "a trimmed raster of at most 96 pixels");
        p.width = unsigned(w); p.height = unsigned(h);
        const auto& f = row["frame"];
        need(f.isArray() && f.array().size() == 4, "a tile frame");
        p.frame = {f.array()[0].number(), f.array()[1].number(), f.array()[2].number(), f.array()[3].number()};
        need(p.frame.x >= 5 - 1e-9 && p.frame.y >= 5 - 1e-9 && p.frame.width > 0 && p.frame.height > 0 &&
             p.frame.x + p.frame.width <= 35 + 1e-9 && p.frame.y + p.frame.height <= 35 + 1e-9, "a frame inside the 30-point box");
        presets_.push_back(std::move(p));
    }
}
const ShortcutPresetArtwork* AppShortcutArtwork::preset(std::string_view id) const noexcept {
    const auto found = std::find_if(presets_.begin(), presets_.end(), [&](const auto& p) { return p.id == id; });
    return found == presets_.end() ? nullptr : &*found;
}
std::string AppShortcutArtwork::title(std::string_view id, core::Language language) const {
    const auto* p = preset(id);
    return p ? core::localized(p->english, p->simplifiedChinese, language) : std::string(id);
}
} // namespace endfield::modules
