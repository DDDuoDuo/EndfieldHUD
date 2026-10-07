#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace ehud::render::profile {
using Pixel = std::array<std::uint8_t, 4>;
using Accent = std::array<double, 3>;
struct Crop { unsigned x{}, y{}, width{}, height{}; };
inline void validate(const Accent& accent) {
    for (double value : accent)
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error("Invalid source profile accent");
}
inline std::uint8_t rounded(double value) {
    return static_cast<std::uint8_t>(std::min(255L, std::max(0L, std::lround(value))));
}
// HUDSourceProfileArtwork.backgroundArtwork draws straight source BGRA into
// an eight-bit premultiplied sRGB bitmap. Keep the decoded mip's bottom-origin
// rows: backgroundArtwork and texturePixels apply opposite row flips.
inline std::vector<Pixel> cropPremultipliedBGRA(std::span<const std::uint8_t> bgra,
                                              unsigned width, unsigned height, Crop crop) {
    if (!width || !height || width > 16384 || height > 16384 ||
        !crop.width || !crop.height || crop.x > width || crop.y > height ||
        crop.width > width - crop.x || crop.height > height - crop.y ||
        bgra.size() < std::size_t(width) * height * 4)
        throw std::runtime_error("Invalid source profile sprite crop");
    std::vector<Pixel> result(std::size_t(crop.width) * crop.height);
    for (unsigned row = 0; row < crop.height; ++row)
        for (unsigned column = 0; column < crop.width; ++column) {
            const auto index = (std::size_t(crop.y + row) * width + crop.x + column) * 4;
            auto& pixel = result[std::size_t(row) * crop.width + column];
            const unsigned alpha = bgra[index + 3];
            for (unsigned channel = 0; channel < 3; ++channel)
                pixel[channel] = static_cast<std::uint8_t>((unsigned(bgra[index + 2 - channel]) * alpha + 127) / 255);
            pixel[3] = static_cast<std::uint8_t>(alpha);
        }
    return result;
}
// Exact byte-domain chroma replacement from themedBackgroundArtwork. Neutral
// pixels and alpha stay unchanged; neither source pixels nor color settings
// are rewritten. Swift's positive .rounded() is C++ lround's half-up rule.
inline Pixel themed(Pixel pixel, const Accent& accent) {
    const int yellow = std::max(0, std::min(int(pixel[0]), int(pixel[1])) - int(pixel[2]));
    for (unsigned channel = 0; channel < 3; ++channel) {
        const int neutral = int(pixel[channel]) - (channel < 2 ? yellow : 0);
        pixel[channel] = rounded(neutral + std::lround(yellow * accent[channel]));
    }
    return pixel;
}
// SourceIn accent at .38, then DestinationIn .05/.38 inside the rounded panel.
// Coverage belongs to the native antialiased clip, not to the original alpha.
inline Pixel hover(Pixel source, const Accent& accent, std::uint8_t panelCoverage) {
    Pixel pixel{};
    for (unsigned channel = 0; channel < 3; ++channel)
        pixel[channel] = rounded(source[3] * .38 * accent[channel]);
    pixel[3] = rounded(source[3] * .38);
    const double coverage = double(panelCoverage) / 255;
    const double multiplier = (1 - coverage) + coverage * (.05 / .38);
    for (auto& channel : pixel) channel = rounded(channel * multiplier);
    return pixel;
}
// HUDSourceProfileArtwork.texturePixels supplies straight RGBA because the
// source UI shader premultiplies once. Preserve its integer rounding formula.
inline Pixel straight(Pixel pixel) {
    const unsigned alpha = pixel[3];
    for (unsigned channel = 0; channel < 3; ++channel)
        pixel[channel] = alpha == 0 ? 0 : static_cast<std::uint8_t>(
            std::min(255u, (unsigned(pixel[channel]) * 255 + alpha / 2) / alpha));
    return pixel;
}
}
