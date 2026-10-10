#include "native/charge_indicator_scene.hpp"
#ifdef _WIN32
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace endfield::native {
namespace {
using Json = ehud::data::Json;
using Matrix = core::Matrix4;
namespace m = modules;
constexpr double pi = 3.14159265358979323846;

// Nine-slice texel layout shared by every capsule texture: inset I = margin +
// radius, a stretchable centre of 2/9 I, total 20/9 I, so the slice fraction
// is exactly 0.45 and all nine-slices share the same nine UV meshes.
struct SliceSpec { double margin, radius; };
constexpr SliceSpec baseSpec{10, 19}, emblemLargeSpec{1, 11.5}, emblemSmallSpec{1, 3};
constexpr std::array<float, 4> sliceUV{0.f, 0.45f, 0.55f, 1.f};
struct SliceLayout { unsigned inset{}, center{}, size{}; double density{}; };
SliceLayout sliceLayout(const SliceSpec& spec, double density) {
    const double inset = spec.margin + spec.radius;
    const unsigned k = std::max(1u, static_cast<unsigned>(std::ceil(inset * density / 9)));
    return {9 * k, 2 * k, 20 * k, 9.0 * k / inset};
}
struct Pixels { unsigned width{}, height{}; std::vector<std::uint8_t> rgba; };

double roundedDistance(double px, double py, double x, double y, double w, double h, double r) {
    const double qx = std::abs(px - (x + w / 2)) - (w / 2 - r), qy = std::abs(py - (y + h / 2)) - (h / 2 - r);
    return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - r;
}
double coverage(double distance, double density) { return std::clamp(0.5 - distance * density, 0.0, 1.0); }
std::uint8_t byte(double v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); }

// The plate and body share geometry: shadow (black 0.20, radius 4, offset
// (0,1)) under two 0.99-alpha backgrounds, composited in encoded sRGB like
// Core Animation. The centre slice samples the middle of a long straight edge
// so stretching never carries corner falloff along an edge.
Pixels baseTexture(bool dark, double requested) {
    const auto l = sliceLayout(baseSpec, requested);
    const double d = l.density, margin = baseSpec.margin, radius = baseSpec.radius;
    const unsigned extension = 2 * static_cast<unsigned>(std::ceil(10 * d));
    const unsigned n = l.size + extension;
    const double body = n / d - 2 * margin;
    // Core Animation's shadow blur measured from the unchanged renderer: a
    // Gaussian of about 0.9 x shadowRadius, vanishing near 2 x shadowRadius.
    const double sigma = 0.9 * 4 * d;
    const int support = static_cast<int>(std::ceil(8 * d));
    std::vector<float> kernel(static_cast<std::size_t>(2 * support + 1));
    double sum = 0;
    for (int k = -support; k <= support; ++k) sum += kernel[static_cast<std::size_t>(k + support)] = static_cast<float>(std::exp(-0.5 * k * k / (sigma * sigma)));
    for (auto& k : kernel) k = static_cast<float>(k / sum);
    std::vector<float> mask(static_cast<std::size_t>(n) * n), temp(mask.size());
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x)
            mask[y * n + x] = static_cast<float>(0.99 * coverage(roundedDistance((x + 0.5) / d, (y + 0.5) / d - 1, margin, margin, body, body, radius), d));
    auto blur = [&](const std::vector<float>& in, std::vector<float>& out, bool horizontal) {
        for (unsigned y = 0; y < n; ++y)
            for (unsigned x = 0; x < n; ++x) {
                double total = 0;
                for (int k = -support; k <= support; ++k) {
                    const int sx = horizontal ? static_cast<int>(x) + k : static_cast<int>(x), sy = horizontal ? static_cast<int>(y) : static_cast<int>(y) + k;
                    if (sx < 0 || sy < 0 || sx >= static_cast<int>(n) || sy >= static_cast<int>(n)) continue;
                    total += in[static_cast<std::size_t>(sy) * n + static_cast<std::size_t>(sx)] * kernel[static_cast<std::size_t>(k + support)];
                }
                out[y * n + x] = static_cast<float>(total);
            }
    };
    blur(mask, temp, true);
    blur(temp, mask, false);
    auto map = [&](unsigned t) { return t < l.inset ? t : t >= l.inset + l.center ? t + extension : t + extension / 2; };
    Pixels result{l.size, l.size, std::vector<std::uint8_t>(static_cast<std::size_t>(l.size) * l.size * 4)};
    const double g = dark ? 0.105 : 0.975;
    for (unsigned y = 0; y < l.size; ++y)
        for (unsigned x = 0; x < l.size; ++x) {
            const unsigned vx = map(x), vy = map(y);
            const double a = 0.99 * coverage(roundedDistance((vx + 0.5) / d, (vy + 0.5) / d, margin, margin, body, body, radius), d);
            const double s = 0.20 * mask[vy * n + vx];
            const double a1 = a + s * (1 - a), c1 = a1 > 0 ? g * a / a1 : 0;
            const double a2 = a + a1 * (1 - a), c2 = a2 > 0 ? (g * a + c1 * a1 * (1 - a)) / a2 : 0;
            auto* p = &result.rgba[(static_cast<std::size_t>(y) * l.size + x) * 4];
            p[0] = p[1] = p[2] = byte(c2);
            p[3] = byte(a2);
        }
    return result;
}
// White Core Animation border: inside the bounds, following the corner.
Pixels borderTexture(double width, double requested) {
    const auto l = sliceLayout(baseSpec, requested);
    const double d = l.density, margin = baseSpec.margin, radius = baseSpec.radius, body = l.size / d - 2 * margin;
    Pixels result{l.size, l.size, std::vector<std::uint8_t>(static_cast<std::size_t>(l.size) * l.size * 4, 255)};
    for (unsigned y = 0; y < l.size; ++y)
        for (unsigned x = 0; x < l.size; ++x) {
            const double px = (x + 0.5) / d, py = (y + 0.5) / d;
            const double outer = coverage(roundedDistance(px, py, margin, margin, body, body, radius), d);
            const double inner = coverage(roundedDistance(px, py, margin + width, margin + width, body - 2 * width, body - 2 * width,
                                                          std::max(0.0, radius - width)), d);
            result.rgba[(static_cast<std::size_t>(y) * l.size + x) * 4 + 3] = byte(outer - inner);
        }
    return result;
}
Pixels fillTexture(const SliceSpec& spec, double requested) {
    const auto l = sliceLayout(spec, requested);
    const double d = l.density, body = l.size / d - 2 * spec.margin;
    Pixels result{l.size, l.size, std::vector<std::uint8_t>(static_cast<std::size_t>(l.size) * l.size * 4, 255)};
    for (unsigned y = 0; y < l.size; ++y)
        for (unsigned x = 0; x < l.size; ++x)
            result.rgba[(static_cast<std::size_t>(y) * l.size + x) * 4 + 3] =
                byte(coverage(roundedDistance((x + 0.5) / d, (y + 0.5) / d, spec.margin, spec.margin, body, body, spec.radius), d));
    return result;
}

double srgbToLinear(double c) {
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
std::array<float, 4> linearTint(const m::ChargeColor& c) {
    return {static_cast<float>(srgbToLinear(c[0])), static_cast<float>(srgbToLinear(c[1])), static_cast<float>(srgbToLinear(c[2])),
            static_cast<float>(std::clamp(c[3], 0.0, 1.0))};
}
float unit(double v) { return static_cast<float>(std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0); }
Matrix rect(double x, double y, double w, double h) { return Matrix::translation(x, y) * Matrix::scale(w, h); }

// General 4x4 inverse (column-major), used once per placement.
std::optional<Matrix> inverse(const Matrix& matrix) {
    double a[4][8]{};
    for (unsigned r = 0; r < 4; ++r) { for (unsigned c = 0; c < 4; ++c) a[r][c] = matrix.values[c * 4 + r]; a[r][r + 4] = 1; }
    for (unsigned c = 0; c < 4; ++c) {
        unsigned pivot = c;
        for (unsigned r = c + 1; r < 4; ++r) if (std::abs(a[r][c]) > std::abs(a[pivot][c])) pivot = r;
        if (!(std::abs(a[pivot][c]) > 1e-15)) return std::nullopt;
        if (pivot != c) for (unsigned k = 0; k < 8; ++k) std::swap(a[c][k], a[pivot][k]);
        const double scale = a[c][c];
        for (unsigned k = 0; k < 8; ++k) a[c][k] /= scale;
        for (unsigned r = 0; r < 4; ++r) if (r != c) { const double f = a[r][c]; for (unsigned k = 0; k < 8; ++k) a[r][k] -= f * a[c][k]; }
    }
    Matrix result;
    for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c) result.values[c * 4 + r] = a[r][c + 4];
    return result;
}
// Inverse of translate(x,y) * scale(s,s) * translate(u,v).
Matrix inverseAffine(double x, double y, double s, double u, double v) {
    const double inv = std::abs(s) > 1e-12 ? 1 / s : 1e12;
    return Matrix::translation(-u, -v) * Matrix::scale(inv, inv) * Matrix::translation(-x, -y);
}

std::string sliceUtf16(const std::string& text, std::size_t start, std::size_t length) {
    std::string out;
    std::size_t unit = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto b = static_cast<unsigned char>(text[i]);
        const std::size_t bytes = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        const std::size_t units = bytes == 4 ? 2 : 1;
        if (unit >= start && unit + units <= start + length) out.append(text, i, bytes);
        unit += units;
        i += bytes;
    }
    return out;
}

enum Texture : std::size_t { base, border05, border15, emblemLarge, emblemSmall, ripple, bolt, ring, cap, laptop, text0, text1, text2, text3, textureCount };
constexpr std::array<const char*, textureCount> textureNames{"base", "border05", "border15", "emblemL", "emblemS", "ripple", "bolt", "ring",
                                                              "cap", "laptop", "text0", "text1", "text2", "text3"};
// Draw slots (paint order of the source canvas).
constexpr std::size_t drawBase = 0, drawRipple = 9, drawBorder05 = 12, drawBorder15 = 21, drawEmblemLarge = 30, drawEmblemSmall = 39,
                      drawBolt = 48, drawText = 49, drawTrack = 53, drawProgress = 54, drawCap = 55, drawLaptop = 57;
static_assert(drawLaptop + 1 == ChargeIndicatorScene::drawCount);
} // namespace

struct ChargeIndicatorScene::Impl {
    LayerRasterizer* raster;
    std::string prefix;
    ChargeIndicatorSceneContent key;
    bool hasContent{};
    m::ChargeIndicatorContent content;
    std::uint64_t contentRevision{}, densityRevision{};
    double density{};
    bool dark{};
    struct Resource {
        std::string id;
        std::uint64_t revision{}, uploaded{};
        Pixels pixels;
        std::shared_ptr<const LayerRasterImage> image;
        core::Rect bounds; // raster leaf bounds in its local points
    };
    std::array<Resource, textureCount> textures;
    std::string quadMesh;
    std::array<std::string, 9> sliceMeshes;
    bool meshesUploaded{};
    std::array<DrawObject, ChargeIndicatorScene::drawCount> draws;
    core::Rect body;

    Impl(LayerRasterizer& r, std::string p) : raster(&r), prefix(std::move(p)) {
        if (prefix.empty()) throw std::invalid_argument("Charge indicator scene needs an ID prefix");
        for (std::size_t i = 0; i < textureCount; ++i) textures[i].id = prefix + "/" + textureNames[i];
        quadMesh = prefix + "/quad";
        for (std::size_t i = 0; i < 9; ++i) sliceMeshes[i] = prefix + "/slice" + std::to_string(i);
        auto texture = [&](std::size_t slot) -> const std::string& {
            if (slot < drawRipple) return textures[base].id;
            if (slot < drawBorder05) return textures[ripple].id;
            if (slot < drawBorder15) return textures[border05].id;
            if (slot < drawEmblemLarge) return textures[border15].id;
            if (slot < drawEmblemSmall) return textures[emblemLarge].id;
            if (slot < drawBolt) return textures[emblemSmall].id;
            if (slot == drawBolt) return textures[bolt].id;
            if (slot < drawTrack) return textures[text0 + (slot - drawText)].id;
            if (slot == drawTrack || slot == drawProgress) return textures[ring].id;
            if (slot == drawLaptop) return textures[laptop].id;
            return textures[cap].id;
        };
        for (std::size_t slot = 0; slot < draws.size(); ++slot) {
            auto& d = draws[slot];
            d.sourceID = prefix + "/draw" + std::to_string(slot);
            d.textureID = texture(slot);
            const bool sliced = slot < drawRipple || (slot >= drawBorder05 && slot < drawBolt);
            d.meshID = sliced ? sliceMeshes[(slot < drawRipple ? slot : (slot - drawBorder05) % 9)] : quadMesh;
            d.opacity = 0;
            d.masks.reserve(8);
        }
    }

    double measure(std::string_view, const Json& descriptor, const LayerRasterOptions& options) {
        double total = 0;
        const auto text = descriptor["string"].string();
        for (const auto& run : descriptor["runs"].array()) {
            const auto& range = run["utf16Range"].array();
            const auto& font = run["attributes"]["NSFont"];
            const Json single = Json::Object{{"string", sliceUtf16(text, static_cast<std::size_t>(range[0].number()), static_cast<std::size_t>(range[1].number()))},
                                             {"fontSize", font["pointSize"]}, {"font", font}, {"wrapped", false}};
            total += raster->measureSourceText(prefix + "/measure", single, 1e6, options).width;
        }
        return total;
    }
    void setPixels(Texture t, Pixels pixels) {
        auto& r = textures[t];
        r.pixels = std::move(pixels);
        r.image.reset();
        ++r.revision;
    }
    void setImage(Texture t, const Json& layer, std::uint64_t revision, const LayerRasterOptions& options) {
        auto& r = textures[t];
        r.image = raster->rasterize(r.id, revision, layer, options);
        if (!r.image || !r.image->complete() || !r.image->width || !r.image->height)
            throw std::runtime_error("Unsupported charge indicator artwork " + r.id);
        r.bounds = r.image->bounds;
        r.pixels = {};
        ++r.revision;
    }
};

ChargeIndicatorScene::ChargeIndicatorScene(LayerRasterizer& raster, std::string prefix)
    : impl_(std::make_unique<Impl>(raster, std::move(prefix))) {}
ChargeIndicatorScene::~ChargeIndicatorScene() {
    for (const auto& t : impl_->textures) try { impl_->raster->remove(t.id); } catch (...) {}
}

bool ChargeIndicatorScene::setContent(const ChargeIndicatorSceneContent& requested) {
    auto& i = *impl_;
    auto next = requested;
    if (!std::isfinite(next.pixelsPerPoint) || next.pixelsPerPoint <= 0) throw std::invalid_argument("Invalid charge indicator density");
    next.pixelsPerPoint = std::clamp(next.pixelsPerPoint, 1.0, 4.0);
    if (i.hasContent && next == i.key) return false;
    LayerRasterOptions options;
    options.pixelsPerPoint = next.pixelsPerPoint;
    options.paddingPoints = 1;
    const bool densityChanged = !i.hasContent || next.pixelsPerPoint != i.density;
    const bool darkChanged = !i.hasContent || next.appearance.dark != i.dark;
    auto content = m::prepareChargeIndicatorContent(next.appearance, next.battery, next.metric, next.telemetry, next.preview,
        [&](std::string_view id, const Json& descriptor) { return i.measure(id, descriptor, options); });
    ++i.contentRevision;
    if (densityChanged) {
        ++i.densityRevision;
        i.setPixels(border05, borderTexture(0.5, next.pixelsPerPoint));
        i.setPixels(border15, borderTexture(1.5, next.pixelsPerPoint));
        i.setPixels(emblemLarge, fillTexture(emblemLargeSpec, next.pixelsPerPoint));
        i.setPixels(emblemSmall, fillTexture(emblemSmallSpec, next.pixelsPerPoint));
        auto rippleOptions = options;
        rippleOptions.pixelsPerPoint = std::min(2.0, next.pixelsPerPoint); // 320-point ring, masked to the capsule
        i.setImage(ripple, m::chargeRippleLayer(), i.densityRevision, rippleOptions);
        i.setImage(bolt, m::chargeBoltLayer(), i.densityRevision, options);
        i.setImage(ring, m::chargeRingLayer(false), i.densityRevision, options);
        i.setImage(cap, m::chargeCapLayer(), i.densityRevision, options);
        i.setImage(laptop, m::chargeLaptopLayer(), i.densityRevision, options);
    }
    if (densityChanged || darkChanged) i.setPixels(base, baseTexture(next.appearance.dark, next.pixelsPerPoint));
    for (std::size_t t = 0; t < 4; ++t)
        if (densityChanged || !i.hasContent || content.texts[t] != i.content.texts[t])
            i.setImage(static_cast<Texture>(text0 + t), content.texts[t], i.contentRevision, options);
    i.content = std::move(content);
    i.key = next;
    i.density = next.pixelsPerPoint;
    i.dark = next.appearance.dark;
    i.hasContent = true;
    return uploadPending();
}

void ChargeIndicatorScene::place(const ChargeIndicatorPlacement& p) {
    auto& i = *impl_;
    if (!i.hasContent) throw std::logic_error("Charge indicator content is required before placement");
    if (p.clips.size() > 6) throw std::invalid_argument("Too many charge indicator clips");
    const auto& pose = p.pose;
    const auto colors = m::chargeIndicatorColors(i.key.appearance, i.key.battery, i.key.metric, i.content.reading.progress, p.stage, p.hovered);
    const auto canvasInverse = inverse(p.canvasToWorld);
    const float opacity = unit(p.opacity);
    auto masks = [&](DrawObject& d) {
        d.masks.clear();
        for (const auto& clip : p.clips) d.masks.push_back(clip);
    };
    auto setDraw = [&](std::size_t slot, const Matrix& world, float alpha, const std::array<float, 4>& tint) {
        auto& d = i.draws[slot];
        d.world = world;
        d.opacity = alpha;
        d.linearTint = tint;
        masks(d);
    };
    const std::array<float, 4> white{1, 1, 1, 1};
    // Capsule: the plate/shadow and body share bounds, radius, scale, opacity.
    const double bw = pose.bodyWidth, bh = pose.bodyHeight, br = std::clamp(pose.bodyRadius, 0.0, std::min(bw, bh) / 2);
    const Matrix bodyToWorld = p.canvasToWorld * Matrix::translation(150, 42) * Matrix::scale(pose.bodyScale, pose.bodyScale) *
                               Matrix::translation(-bw / 2, -bh / 2);
    auto slices = [&](std::size_t first, const Matrix& toWorld, double w, double h, double r, const SliceSpec& spec, float alpha,
                      const std::array<float, 4>& tint) {
        const double k = spec.radius > 0 ? r / spec.radius : 0;
        const std::array<double, 4> xs{-spec.margin * k, r, w - r, w + spec.margin * k}, ys{-spec.margin * k, r, h - r, h + spec.margin * k};
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                setDraw(first + row * 3 + column, toWorld * rect(xs[column], ys[row], xs[column + 1] - xs[column], ys[row + 1] - ys[row]), alpha, tint);
    };
    const float bodyAlpha = unit(pose.bodyOpacity) * opacity;
    slices(drawBase, bodyToWorld, bw, bh, br, baseSpec, bodyAlpha, white);
    // Ripples are body sublayers: inherit its opacity and clip to its corners.
    const auto bodyInverse = canvasInverse ? std::optional<Matrix>(inverseAffine(150, 42, pose.bodyScale, -bw / 2, -bh / 2) * *canvasInverse)
                                           : std::nullopt;
    const auto& rippleImage = i.textures[ripple].bounds;
    for (std::size_t n = 0; n < 3; ++n) {
        const auto& position = pose.ripplePosition[n];
        const double s = pose.rippleScale[n];
        setDraw(drawRipple + n, bodyToWorld * Matrix::translation(position.x, position.y) * Matrix::scale(s, s) * Matrix::translation(-160, -160) *
                                    rect(rippleImage.x, rippleImage.y, rippleImage.width, rippleImage.height),
                bodyAlpha * unit(pose.rippleOpacity[n]), linearTint(colors.ripple));
        auto& d = i.draws[drawRipple + n];
        if (bodyInverse && bw > 0 && bh > 0) d.masks.push_back({*bodyInverse, {0, 0, bw, bh}, br});
        else d.opacity = 0;
    }
    // Border above the sublayers; widths between 0.5 and 1.5 crossfade.
    const double borderWidth = p.embeddedBorder ? pose.borderWidth : colors.borderWidth;
    const auto borderColor = p.embeddedBorder ? pose.borderColor : colors.border;
    const double blend = std::clamp(borderWidth - 0.5, 0.0, 1.0);
    const auto borderTint = linearTint(borderColor);
    slices(drawBorder05, bodyToWorld, bw, bh, br, baseSpec, bodyAlpha * static_cast<float>(1 - blend), borderTint);
    slices(drawBorder15, bodyToWorld, bw, bh, br, baseSpec, bodyAlpha * static_cast<float>(blend), borderTint);
    // Emblem rounded rectangle (path interpolated as width/height/radius).
    const double ew = pose.emblemWidth, eh = pose.emblemHeight, er = std::clamp(pose.emblemRadius, 0.0, std::min(ew, eh) / 2);
    const Matrix emblemToWorld = p.canvasToWorld * Matrix::translation(pose.emblemCenter.x, pose.emblemCenter.y) *
                                 Matrix::scale(pose.emblemScale, pose.emblemScale) * Matrix::translation(-ew / 2, -eh / 2);
    const bool large = er > (emblemLargeSpec.radius + emblemSmallSpec.radius) / 2;
    const float emblemAlpha = unit(pose.emblemOpacity) * opacity;
    slices(drawEmblemLarge, emblemToWorld, ew, eh, er, emblemLargeSpec, large ? emblemAlpha : 0.f, linearTint(colors.emblem));
    slices(drawEmblemSmall, emblemToWorld, ew, eh, er, emblemSmallSpec, large ? 0.f : emblemAlpha, linearTint(colors.emblem));
    // Bolt.
    const auto& boltImage = i.textures[bolt].bounds;
    setDraw(drawBolt, p.canvasToWorld * Matrix::translation(pose.boltPosition.x, pose.boltPosition.y) * Matrix::scale(pose.boltScale, pose.boltScale) *
                          Matrix::translation(-12, -12) * rect(boltImage.x, boltImage.y, boltImage.width, boltImage.height),
            unit(pose.boltOpacity) * opacity, linearTint(colors.bolt));
    // Labels (anchor 0,0), colours baked into the runs.
    for (std::size_t n = 0; n < 4; ++n) {
        const auto& image = i.textures[text0 + n].bounds;
        setDraw(drawText + n, p.canvasToWorld * Matrix::translation(pose.textPosition[n].x, pose.textPosition[n].y) *
                                  rect(image.x, image.y, image.width, image.height),
                unit(pose.textOpacity[n]) * opacity, white);
    }
    // Ring container (28 x 28, anchor 0.5): track, progress arc, round caps, laptop.
    const Matrix ringToWorld = p.canvasToWorld * Matrix::translation(pose.ringPosition.x - 14, pose.ringPosition.y - 14);
    const float ringAlpha = unit(pose.ringOpacity) * opacity;
    const auto& ringImage = i.textures[ring].bounds;
    const Matrix ringQuad = rect(ringImage.x, ringImage.y, ringImage.width, ringImage.height);
    setDraw(drawTrack, ringToWorld * ringQuad, ringAlpha, linearTint(colors.ringTrack));
    const double progress = std::clamp(i.content.reading.progress.value_or(0), 0.0, 1.0);
    const bool arc = progress > 0 && canvasInverse.has_value();
    setDraw(drawProgress, ringToWorld * ringQuad, arc ? ringAlpha : 0.f, linearTint(colors.ringProgress));
    auto& progressDraw = i.draws[drawProgress];
    if (arc) {
        progressDraw.angularMask = AngularMask{Matrix::translation(14 - pose.ringPosition.x, 14 - pose.ringPosition.y) * *canvasInverse,
                                               {14, 14}, -pi / 2, 2 * pi * progress, std::nullopt};
    } else progressDraw.angularMask = AngularMask{Matrix{}, {14, 14}, -pi / 2, 0, std::nullopt};
    const auto& capImage = i.textures[cap].bounds;
    const std::array<core::Point, 2> ends{core::Point{14, 3}, core::Point{14 + 11 * std::sin(2 * pi * progress), 14 - 11 * std::cos(2 * pi * progress)}};
    for (std::size_t n = 0; n < 2; ++n)
        setDraw(drawCap + n, ringToWorld * Matrix::translation(ends[n].x - 1, ends[n].y - 1) * rect(capImage.x, capImage.y, capImage.width, capImage.height),
                arc ? ringAlpha : 0.f, linearTint(colors.ringProgress));
    const auto& laptopImage = i.textures[laptop].bounds;
    setDraw(drawLaptop, ringToWorld * rect(laptopImage.x, laptopImage.y, laptopImage.width, laptopImage.height), ringAlpha, linearTint(colors.laptop));
    i.body = pose.bodyRect();
}

void ChargeIndicatorScene::upload(Renderer& renderer) {
    auto& i = *impl_;
    if (!i.meshesUploaded) {
        const std::array<std::uint32_t, 6> indices{0, 1, 2, 2, 1, 3};
        auto quad = [&](const std::string& id, float u0, float v0, float u1, float v1) {
            const std::array<Vertex, 4> vertices{Vertex{{0, 0, 0}, {u0, v0}}, Vertex{{1, 0, 0}, {u1, v0}}, Vertex{{0, 1, 0}, {u0, v1}},
                                                 Vertex{{1, 1, 0}, {u1, v1}}};
            renderer.setMesh(id, 1, {vertices, indices});
        };
        quad(i.quadMesh, 0, 0, 1, 1);
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                quad(i.sliceMeshes[row * 3 + column], sliceUV[column], sliceUV[row], sliceUV[column + 1], sliceUV[row + 1]);
        i.meshesUploaded = true;
    }
    for (auto& t : i.textures) {
        if (t.uploaded == t.revision || t.revision == 0) continue;
        TextureData data;
        if (t.image) { data.width = t.image->width; data.height = t.image->height; data.straightRGBA = t.image->straightRGBA; }
        else { data.width = t.pixels.width; data.height = t.pixels.height; data.straightRGBA = t.pixels.rgba; }
        data.colorSpace = TextureColorSpace::sRGB;
        data.filter = TextureFilter::linear;
        renderer.setTexture(t.id, t.revision, data);
        t.uploaded = t.revision; // small CPU copies stay for a renderer reset
    }
}
bool ChargeIndicatorScene::uploadPending() const noexcept {
    if (!impl_->meshesUploaded) return true;
    for (const auto& t : impl_->textures) if (t.revision && t.uploaded != t.revision) return true;
    return false;
}
std::span<const DrawObject> ChargeIndicatorScene::draws() const noexcept { return impl_->draws; }
bool ChargeIndicatorScene::release(Renderer& renderer) noexcept {
    auto& i = *impl_;
    bool released = true;
    try {
        for (auto& t : i.textures) if (t.uploaded) { released = renderer.removeTexture(t.id) && released; t.uploaded = 0; }
        if (i.meshesUploaded) {
            released = renderer.removeMesh(i.quadMesh) && released;
            for (const auto& mesh : i.sliceMeshes) released = renderer.removeMesh(mesh) && released;
            i.meshesUploaded = false;
        }
    } catch (...) { return false; }
    return released; // a later upload reinstalls everything from retained content

}
const modules::ChargeIndicatorContent& ChargeIndicatorScene::content() const noexcept { return impl_->content; }
const ChargeIndicatorSceneContent& ChargeIndicatorScene::contentKey() const noexcept { return impl_->key; }
std::uint64_t ChargeIndicatorScene::contentRevision() const noexcept { return impl_->contentRevision; }
core::Rect ChargeIndicatorScene::bodyRect() const noexcept { return impl_->body; }
} // namespace endfield::native
#endif
