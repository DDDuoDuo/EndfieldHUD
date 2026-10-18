#include "modules/display_center_logo.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
constexpr std::array<std::string_view, 5> identifiers{"endfield", "rhodesIsland", "babel", "rhineLab", "custom"};
void need(bool value, const char* message) { if (!value) throw std::invalid_argument(message); }
int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// A normalized 1-D Gaussian (standard deviation sigma, radius ceil(3 sigma)).
std::vector<double> gaussian(double sigma) {
    if (!(sigma > 1e-6)) return {1.0};
    const auto radius = static_cast<int>(std::ceil(3 * sigma));
    std::vector<double> weights(static_cast<std::size_t>(radius) * 2 + 1);
    double total = 0;
    for (int k = -radius; k <= radius; ++k) {
        const double w = std::exp(-double(k) * k / (2 * sigma * sigma));
        weights[static_cast<std::size_t>(k + radius)] = w; total += w;
    }
    for (auto& w : weights) w /= total;
    return weights;
}
} // namespace

std::optional<CenterLogoChoice> centerLogoChoice(std::string_view value) noexcept {
    for (std::size_t n = 0; n < identifiers.size(); ++n) if (identifiers[n] == value) return static_cast<CenterLogoChoice>(n);
    return std::nullopt;
}
std::string_view centerLogoIdentifier(CenterLogoChoice choice) noexcept { return identifiers[static_cast<std::size_t>(choice)]; }
std::string_view centerLogoPresetFile(CenterLogoChoice choice) noexcept {
    switch (choice) {
    case CenterLogoChoice::rhodesIsland: return "rhodesIsland.png";
    case CenterLogoChoice::babel: return "babel.png";
    case CenterLogoChoice::rhineLab: return "rhineLab.png";
    default: return {};
    }
}
std::optional<std::string> centerLogoRevision(std::string_view value) noexcept {
    // Foundation UUID(uuidString:): 8-4-4-4-12 hexadecimal digits in either case.
    if (value.size() != 36) return std::nullopt;
    std::string out(value);
    for (std::size_t n = 0; n < out.size(); ++n) {
        if (n == 8 || n == 13 || n == 18 || n == 23) { if (out[n] != '-') return std::nullopt; continue; }
        const int digit = hexValue(out[n]);
        if (digit < 0) return std::nullopt;
        out[n] = "0123456789ABCDEF"[digit];
    }
    return out;
}

std::optional<CenterLogoBounds> centerLogoVisibleBounds(const CenterLogoBitmap& image) {
    need(image.width > 0 && image.height > 0 && image.rgba.size() == std::size_t(image.width) * image.height * 4, "Invalid center logo bitmap");
    unsigned left = image.width, top = image.height, right = 0, bottom = 0;
    bool any = false;
    for (unsigned y = 0; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x)
            if (image.rgba[(std::size_t(y) * image.width + x) * 4 + 3] > 8) {
                left = std::min(left, x); right = std::max(right, x);
                top = std::min(top, y); bottom = std::max(bottom, y); any = true;
            }
    if (!any) return std::nullopt;
    return CenterLogoBounds{left, top, right - left + 1, bottom - top + 1};
}
double centerLogoShadowBlur(unsigned croppedHeight) noexcept { return double(croppedHeight) * 3.5 / centerLogoDefaultHeight; }

std::optional<CenterLogoArtwork> centerLogoArtwork(const CenterLogoBitmap& image) {
    const auto bounds = centerLogoVisibleBounds(image);
    if (!bounds) return std::nullopt;
    const unsigned w = bounds->width, h = bounds->height;
    need(w <= 16384 && h <= 16384, "Center logo exceeds the source context limit");
    // The cropped content drawn into a premultipliedLast context.
    const std::size_t count = std::size_t(w) * h;
    std::vector<double> alpha(count);
    std::vector<std::array<double, 3>> premultiplied(count);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const auto* p = &image.rgba[(std::size_t(y + bounds->y) * image.width + (x + bounds->x)) * 4];
            const double a = p[3] / 255.0;
            const auto i = std::size_t(y) * w + x;
            alpha[i] = a;
            for (unsigned c = 0; c < 3; ++c) premultiplied[i][c] = std::round(p[c] * a) / 255.0;
        }
    // CGContext.setShadow(offset: .zero, blur: b, white .7): the content's
    // alpha blurred, clipped to the context, composited under the content
    // (source over). Core Graphics' kernel is matched by a Gaussian of
    // variance (b / 2)^2 + 1/2 (tests/display_center_logo_tests.cpp compares
    // the Mac oracle within a few premultiplied levels).
    const double blur = centerLogoShadowBlur(h);
    const auto kernel = gaussian(std::sqrt(blur * blur / 4 + .5));
    const int radius = static_cast<int>(kernel.size() / 2);
    std::vector<double> horizontal(count), shadow(count);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            double sum = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = int(x) + k;
                if (sx >= 0 && sx < int(w)) sum += kernel[std::size_t(k + radius)] * alpha[std::size_t(y) * w + std::size_t(sx)];
            }
            horizontal[std::size_t(y) * w + x] = sum;
        }
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            double sum = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = int(y) + k;
                if (sy >= 0 && sy < int(h)) sum += kernel[std::size_t(k + radius)] * horizontal[std::size_t(sy) * w + x];
            }
            shadow[std::size_t(y) * w + x] = .7 * sum;
        }
    CenterLogoArtwork out;
    out.width = w; out.height = h;
    out.texturePixels.resize(count * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const auto i = std::size_t(y) * w + x;
            const double a = alpha[i], s = shadow[i] * (1 - a);
            // 8-bit premultiplied result of the shadowed draw.
            const auto quantize = [](double v) { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
            const unsigned outA = quantize(a + s);
            // texturePixels: unpremultiplied once, bottom-origin rows.
            auto* t = &out.texturePixels[(std::size_t(h - 1 - y) * w + x) * 4];
            for (unsigned c = 0; c < 3; ++c) {
                const unsigned premultipliedValue = std::min(quantize(premultiplied[i][c] + s), outA);
                t[c] = outA == 0 ? 0 : static_cast<std::uint8_t>(std::min(255u, (premultipliedValue * 255u + outA / 2) / outA));
            }
            t[3] = static_cast<std::uint8_t>(outA);
        }
    out.displayHeight = centerLogoDefaultHeight;
    out.displayWidth = double(w) * centerLogoDefaultHeight / double(h);
    return out;
}

std::string centerLogoErrorText(CenterLogoError error, core::Language language, std::string_view detail) {
    switch (error) {
    case CenterLogoError::invalidImage: return core::localized("Choose a readable image file.", "请选择可读取的图片文件。", language);
    case CenterLogoError::imageTooLarge: return core::localized("Choose an image no larger than 32 MB.", "请选择不超过 32 MB 的图片。", language);
    case CenterLogoError::imageDimensionsTooLarge:
        return core::localized("Choose an image no larger than 64 megapixels or 16,384 pixels on either side.", "请选择不超过 6400 万像素、且单边不超过 16,384 像素的图片。", language);
    case CenterLogoError::persistence: return core::localized("The center logo could not be saved: ", "无法保存中心标志：", language) + std::string(detail);
    }
    return {};
}
} // namespace endfield::modules
