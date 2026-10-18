#include "core/map_source_cutout.hpp"
#include "core/source_native_labels.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
using P = core::Point;
template <std::size_t N> constexpr MapCutoutPolygon loop(const P (&points)[N]) {
    static_assert(N >= 3 && N <= MapCutoutPolygon::capacity);
    MapCutoutPolygon result{};
    for (std::size_t i = 0; i < N; ++i) result.points[i] = points[i];
    result.count = N;
    return result;
}
// tools/map_cutout_reference.py --cpp, authority ca04f142185c7de40acd8523bdb563195d90a1d1
// (tests/fixtures/map_cutout_source.json). Normalized RectTransform coordinates, Unity y up.
// /TechtreeBtn/Icon (watchfactory_bg)
constexpr P storagePlate[]{{0.830097090850756,2.70930206047737e-09},{0.6699029097279299,0.06818182360042224},{0.6140776696135697,0.10795454680919647},{0.5024271844117676,0.18181818046353082},{0.37135923024520134,0.3068181797862053},{0.2645630998518861,0.43750000033866276},{0.16262136269541622,0.5909090888771144},{0.06310680769022231,0.7784090963276951},{1.388846093686169e-08,0.9261363636363636},{1.388846093686169e-08,0.9999999972906979},{0.7742718435028224,0.9999999972906979},{0.7742718435028224,0.9943181845274838},{0.7694174792002706,0.9829545590010556},{0.8422330016071357,0.9204545508731495},{0.9684465892106584,0.9034090787172318},{0.9975728039602632,0.8693181682716716},{0.9999999861115391,0.6931818202137947},{0.9999999861115391,2.70930206047737e-09}};
// /ReportBtn/Icon (watchfactory_r_bg)
constexpr P activityTab[]{{1.388846093686169e-08,0.9829545590010556},{1.388846093686169e-08,0.9999999972906979},{0.2451456426416786,0.9999999972906979},{0.2451456426416786,0.9943181845274838},{0.24029124940483315,0.9829545590010556}};
constexpr P activityNotch[]{{0.9927184396577113,0.4602272725579414},{0.9927184396577113,0.5511363656683401},{0.9999999861115391,0.5568181826648387},{0.9999999861115391,0.4488636343316598},{0.9975728039602632,0.4488636343316598}};
constexpr P activityPlate[]{{1.388846093686169e-08,0.011363628235730283},{1.388846093686169e-08,0.8579545427452434},{0.01213592464484059,0.8806818276643753},{0.20145630605012466,0.9318181763995778},{0.29611651121991356,0.9886363717642697},{0.7184465961548888,0.9886363717642697},{0.7160194140036129,0.9999999972906979},{0.9999999861115391,0.9999999972906979},{0.9999999861115391,0.9261363636363636},{0.8980582489550692,0.7045454626733607},{0.8446601837584116,0.6079545440998945},{0.7257281570758635,0.42613636634566565},{0.5922330085513661,0.26704545657743106},{0.49514563117646476,0.1761363677003167},{0.3932038840738315,0.10227273404598236},{0.3300970902720701,0.06250001083720813},{0.1844660309911932,2.70930206047737e-09},{0.0024271960397368453,2.70930206047737e-09}};
constexpr std::array storageLoops{loop(storagePlate)};
constexpr std::array activityLoops{loop(activityTab), loop(activityNotch), loop(activityPlate)};
const std::array<MapSourceSilhouette, 2> silhouettes{{
    {"/TechtreeBtn/Icon", "storage", "watchfactory_bg", storageLoops},
    {"/ReportBtn/Icon", "activityMonitor", "watchfactory_r_bg", activityLoops}}};
static_assert(storageLoops.size() + activityLoops.size() <= MapCutouts::capacity);
bool finite(P p) noexcept { return std::isfinite(p.x) && std::isfinite(p.y); }
} // namespace

bool MapCutoutPolygon::operator==(const MapCutoutPolygon& other) const noexcept {
    return count == other.count && std::equal(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(count), other.points.begin());
}
bool MapCutouts::operator==(const MapCutouts& other) const noexcept {
    return count == other.count && std::equal(polygons.begin(), polygons.begin() + static_cast<std::ptrdiff_t>(count), other.polygons.begin());
}
bool MapCutouts::contains(P p) const noexcept {
    for (std::size_t i = 0; i < count; ++i) if (core::polygonContains(polygons[i].view(), p)) return true;
    return false;
}
std::span<const MapSourceSilhouette> mapSourceSilhouettes() noexcept { return silhouettes; }

bool MapSourceCutoutPlan::clear() noexcept {
    key_.reset();
    if (cutouts_.empty()) return false;
    cutouts_.count = 0;
    return true;
}
bool MapSourceCutoutPlan::update(const MapSourceView& view, core::Rect viewport) {
    if (!view.camera || !(viewport.width > 0) || !(viewport.height > 0)) return clear();
    const auto plates = mapSourceSilhouettes();
    Key key;
    key.viewProjection = view.camera->projection * view.camera->view;
    key.viewport = viewport;
    const bool fresh = view.resolved.data() != searched_ || view.resolved.size() != searchedSize_;
    searched_ = view.resolved.data(); searchedSize_ = view.resolved.size();
    for (std::size_t s = 0; s < slots_.size(); ++s) {
        auto& slot = slots_[s];
        const bool valid = slot.node && slot.index < view.resolved.size() && view.resolved[slot.index].node == slot.node;
        if (!valid && (slot.node || fresh)) {
            // Found by path suffix (the source's first match), then kept by
            // identity: the resolved order is the scene's serialized order.
            slot = {};
            for (std::size_t i = 0; i < view.resolved.size(); ++i) {
                const auto* node = view.resolved[i].node;
                if (node && std::string_view(node->path).ends_with(plates[s].nodeSuffix)) { slot = {node, i}; break; }
            }
        }
        if (!slot.node) continue;
        const auto& resolved = view.resolved[slot.index];
        const double alpha = slot.index < view.inheritedAlpha.size() ? view.inheritedAlpha[slot.index] : 1;
        if (!resolved.activeInHierarchy || !resolved.rect || !(alpha > .001)) continue;
        key.visible[s] = true;
        key.world[s] = view.camera->worldRoot * resolved.worldMatrix;
        key.rect[s] = *resolved.rect;
    }
    if (key_ && *key_ == key) return false;
    key_ = key;
    MapCutouts next;
    for (std::size_t s = 0; s < slots_.size(); ++s) {
        if (!key.visible[s]) continue;
        const auto& rect = key.rect[s];
        for (const auto& normalized : plates[s].loops) {
            auto& polygon = next.polygons[next.count];
            polygon.count = 0;
            bool projected = true;
            for (std::size_t k = 0; k < normalized.count && projected; ++k) {
                const auto n = normalized.points[k];
                const core::source::Vec3 local{rect.origin[0] + n.x * rect.size[0], rect.origin[1] + n.y * rect.size[1], 0};
                const auto point = core::source::projectNativePoint(local, key.world[s], key.viewProjection, viewport);
                if (point && finite(*point)) polygon.points[polygon.count++] = *point;
                else projected = false;
            }
            if (projected) ++next.count;
        }
    }
    if (next == cutouts_) return false;
    cutouts_ = next;
    return true;
}

MapCutoutTexels mapCutoutTexels(const MapCutouts& cutouts, double scale, std::uint32_t width, std::uint32_t height) noexcept {
    if (cutouts.empty() || !(scale > 0) || !std::isfinite(scale) || !width || !height) return {};
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (std::size_t i = 0; i < cutouts.count; ++i)
        for (const auto p : cutouts.polygons[i].view()) {
            if (!finite(p)) continue;
            x0 = std::min(x0, p.x * scale); y0 = std::min(y0, p.y * scale);
            x1 = std::max(x1, p.x * scale); y1 = std::max(y1, p.y * scale);
        }
    if (!(x1 >= x0) || !(y1 >= y0)) return {};
    const auto clamp = [](double v, std::uint32_t limit) {
        return static_cast<std::uint32_t>(std::clamp(v, 0., static_cast<double>(limit)));
    };
    const auto left = clamp(std::floor(x0) - 1, width), right = clamp(std::ceil(x1) + 1, width);
    const auto top = clamp(std::floor(y0) - 1, height), bottom = clamp(std::ceil(y1) + 1, height);
    if (right <= left || bottom <= top) return {};
    return {left, top, right - left, bottom - top};
}
MapCutoutTexels mapCutoutUnion(MapCutoutTexels a, MapCutoutTexels b) noexcept {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const auto x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(a.x + a.width, b.x + b.width) - x, std::max(a.y + a.height, b.y + b.height) - y};
}
namespace {
// Alpha x (1 - even-odd coverage) of `region`; RGB and alpha from `base`
// (straight RGBA of baseWidth texels per row) or, without one, opaque white.
void coverageMask(const MapCutouts& cutouts, double scale, const std::uint8_t* base, std::uint32_t baseWidth,
    MapCutoutTexels region, std::span<std::uint8_t> out, std::span<float> row) {
    const auto w = std::size_t(region.width), h = std::size_t(region.height);
    constexpr unsigned rows = 4;
    constexpr float weight = 1.f / rows;
    std::array<double, MapCutouts::capacity * MapCutoutPolygon::capacity> crossings{};
    for (std::size_t j = 0; j < h; ++j) {
        std::fill(row.begin(), row.begin() + static_cast<std::ptrdiff_t>(w), 0.f);
        for (unsigned k = 0; k < rows; ++k) {
            const double y = double(region.y + j) + (k + .5) / rows;
            std::size_t n = 0;
            for (std::size_t c = 0; c < cutouts.count; ++c) {
                const auto polygon = cutouts.polygons[c].view();
                for (std::size_t i = 0, previous = polygon.size() - 1; i < polygon.size(); previous = i++) {
                    const P a{polygon[previous].x * scale, polygon[previous].y * scale}, b{polygon[i].x * scale, polygon[i].y * scale};
                    if (!finite(a) || !finite(b) || (a.y > y) == (b.y > y)) continue;
                    crossings[n++] = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
                }
            }
            std::sort(crossings.begin(), crossings.begin() + static_cast<std::ptrdiff_t>(n));
            // Even-odd: [c0,c1), [c2,c3), ... are inside; exact horizontal coverage.
            for (std::size_t i = 0; i + 1 < n; i += 2) {
                const double u0 = std::clamp(crossings[i] - region.x, 0., double(w)), u1 = std::clamp(crossings[i + 1] - region.x, 0., double(w));
                if (!(u1 > u0)) continue;
                const auto i0 = static_cast<std::size_t>(u0), i1 = static_cast<std::size_t>(u1);
                if (i0 == i1) { row[i0] += float(u1 - u0) * weight; continue; }
                row[i0] += float(double(i0 + 1) - u0) * weight;
                for (auto t = i0 + 1; t < i1; ++t) row[t] += weight;
                if (i1 < w) row[i1] += float(u1 - double(i1)) * weight;
            }
        }
        auto* target = out.data() + j * w * 4;
        if (!base) {
            for (std::size_t i = 0; i < w; ++i) {
                const float covered = std::clamp(row[i], 0.f, 1.f);
                target[i * 4] = target[i * 4 + 1] = target[i * 4 + 2] = 255;
                target[i * 4 + 3] = static_cast<std::uint8_t>(std::lround(255.f * (1.f - covered)));
            }
            continue;
        }
        const auto* source = base + ((std::size_t(region.y) + j) * baseWidth + region.x) * 4;
        for (std::size_t i = 0; i < w; ++i) {
            const float covered = std::clamp(row[i], 0.f, 1.f);
            target[i * 4] = source[i * 4]; target[i * 4 + 1] = source[i * 4 + 1]; target[i * 4 + 2] = source[i * 4 + 2];
            target[i * 4 + 3] = static_cast<std::uint8_t>(std::lround(source[i * 4 + 3] * (1.f - covered)));
        }
    }
}
} // namespace
void mapCutoutMask(const MapCutouts& cutouts, double scale, std::span<const std::uint8_t> base, std::uint32_t baseWidth,
    std::uint32_t baseHeight, MapCutoutTexels region, std::span<std::uint8_t> out, std::span<float> row) {
    const auto w = std::size_t(region.width), h = std::size_t(region.height);
    if (region.empty() || !(scale > 0) || !std::isfinite(scale) || base.size() != std::size_t(baseWidth) * baseHeight * 4 ||
        region.x > baseWidth || region.width > baseWidth - region.x || region.y > baseHeight || region.height > baseHeight - region.y ||
        out.size() != w * h * 4 || row.size() < w)
        throw std::invalid_argument("Invalid Map cutout mask region");
    coverageMask(cutouts, scale, base.data(), baseWidth, region, out, row);
}
void mapCutoutMask(const MapCutouts& cutouts, double scale, MapCutoutTexels region, std::span<std::uint8_t> out, std::span<float> row) {
    const auto w = std::size_t(region.width), h = std::size_t(region.height);
    if (region.empty() || !(scale > 0) || !std::isfinite(scale) || out.size() != w * h * 4 || row.size() < w)
        throw std::invalid_argument("Invalid Map cutout mask region");
    coverageMask(cutouts, scale, nullptr, 0, region, out, row);
}
} // namespace endfield::modules
