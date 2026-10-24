#pragma once
// App shortcut artwork shared by the redesigned + Add App screen and the
// right-hand navigation: the port of Sources/AppShortcutArtwork.swift
// (placedImage / trimmedImage) and of HUDSourceDesktopIconLayout
// (Sources/HUDSourceDesktopNavigationLayout.swift:9-58). SHELL-41/46/106/108.
//
// The tile artwork of every preset (the near-black glyph on the accent tile,
// and the App icon fallback glyph in white .93 when an application has no
// icon) is the Mac's own trimmed raster: tools/app_shortcut_reference.sh
// renders AppShortcutArtwork.placedImage from the unchanged sources and
// writes it to resources/app-shortcut/presets/<preset>.png; artwork.json
// lists each file with its SHA-256, pixel size and the frame the Mac places
// it in (the 40 x 40 tile's 30-point box at (5, 5)). The redesigned glyphs
// (Music, Play, Brush, Camera, the fallback grid) are therefore drawn exactly
// as the Mac draws them, cut-outs included; nothing re-implements them here.
// The side list keeps its own exported templates
// (resources/watch-appearance, NativeWatchAppearance); only an application's
// own icon is trimmed at run time (trimDesktopIcon, below).
//
// Content events only: parsing once, trimming and averaging once per
// extracted icon on the owner's worker. No platform call.
#include "core/data/json.hpp"
#include "core/localization.hpp"
#include "core/scene.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::modules {
// HUDSourceDesktopIconLayout.box / glyphFrame: every desktop button icon is
// drawn in a 32-point box with the bounding area of a 23 x 23 square (never
// past 28 x 28), centred; a non-positive or non-finite size is 26 x 26.
// Inline (header only): the native side list (native/watch_appearance.cpp)
// places a trimmed application icon with it without linking this library.
inline constexpr double desktopIconBox = 32;
inline core::Rect desktopIconGlyphFrame(double width, double height) noexcept {
    double w = 26, h = 26;
    if (width > 0 && height > 0 && std::isfinite(width) && std::isfinite(height)) {
        const double scale = std::min(28 / std::max(width, height), 23 / std::sqrt(width * height));
        w = width * scale; h = height * scale;
    }
    return {(desktopIconBox - w) / 2, (desktopIconBox - h) / 2, w, h};
}

// HUDSourceDesktopIconLayout.image: a 96 x 96 straight-RGBA icon cropped to
// its pixels with alpha above 8 (the Mac draws the source aspect-fit into a
// 96-pixel square first; Windows icons are already that square,
// native::appShortcutIconPixels). Nothing visible: nullopt (the Mac keeps the
// source). Throws std::invalid_argument for another size or a short buffer.
struct TrimmedShortcutIcon {
    unsigned width{}, height{};
    std::vector<std::uint8_t> straightRGBA;
};
inline constexpr unsigned trimmedIconSide = 96;
std::optional<TrimmedShortcutIcon> trimDesktopIcon(unsigned width, unsigned height, std::span<const std::uint8_t> straightRGBA);

// AppShortcutCanvas.averageColor (:296-314): the icon's premultiplied colour
// sums over its coverage, x .72, then scaled down to a Rec. 709 luminance of
// at most .55; nullopt without coverage (the canvas then uses #2A2A2A). The
// Mac first resamples to 8 x 8 (CGContext, medium interpolation); this sums
// every pixel (the same area average up to the resampler's filter).
std::optional<std::array<double, 3>> shortcutTileColor(unsigned width, unsigned height, std::span<const std::uint8_t> straightRGBA);

// resources/app-shortcut/artwork.json (version 2).
struct ShortcutPresetArtwork {
    std::string id, english, simplifiedChinese; // AppShortcutIcon raw value and title pair
    std::string file, sha256;                   // presets/<id>.png under the asset root
    unsigned width{}, height{};                 // the trimmed raster's pixels
    core::Rect frame;                           // its frame in the 40 x 40 tile (placedImage in (5, 5, 30, 30))
    bool operator==(const ShortcutPresetArtwork&) const = default;
};
class AppShortcutArtwork final {
public:
    explicit AppShortcutArtwork(const ehud::data::Json&); // throws std::invalid_argument
    // AppShortcutIcon.allCases order; validShortcutIcon names exactly these.
    std::span<const ShortcutPresetArtwork> presets() const noexcept { return presets_; }
    const ShortcutPresetArtwork* preset(std::string_view id) const noexcept;
    std::string title(std::string_view id, core::Language) const; // AppShortcutIcon.title
private:
    std::vector<ShortcutPresetArtwork> presets_;
};
} // namespace endfield::modules
