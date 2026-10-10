#include "tools/charge_badge_preview.hpp"
#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace gpu = native;
namespace m = modules;
using Matrix = core::Matrix4;
using Json = ehud::data::Json;
void need(bool value, const char* message) { if (!value) throw std::invalid_argument(message); }
// Effective device pixels per canvas point of a design->world matrix.
double density(const Matrix& center, double clientScale) {
    const double sx = std::hypot(center.values[0], center.values[1]), sy = std::hypot(center.values[4], center.values[5]);
    const double value = std::max(sx, sy) * m::ChargeBadgeState::rendererScale * clientScale;
    // Quantize so ordinary tilt/parallax never re-rasterizes.
    return std::clamp(std::ceil(value * 4) / 4, 1.0, 4.0);
}
} // namespace

struct ChargeBadgePreview::Impl {
    m::ChargeBadgeState state;
    gpu::LayerScene anchor;
    gpu::ChargeIndicatorScene scene;
    std::function<void()> selectPower;
    gpu::ChargeIndicatorSceneContent content;
    bool hasContent{}, visible{}, pressed{}, reduced{};
    app::ClientMetrics metrics;
    core::Projection projection;
    std::optional<core::Point> pointerDesign;
    std::array<gpu::LayerCompositionEntry, 1> composed;
    double time{};
    std::optional<Matrix> lastCenter;
    float lastOpacity{1};

    Impl(gpu::LayerRasterizer& raster, std::function<void()> select)
        : anchor(raster), scene(raster, "hud.chargeBadge"), selectPower(std::move(select)) {
        anchor.load(Json::Object{{"bounds", Json::Array{0, 0, 1, 1}}, {"children", Json::Array{}}}, {});
        composed[0] = {&anchor, scene.draws()};
    }
    void clock(double t) { need(std::isfinite(t), "Invalid charge badge time"); time = std::max(time, t); }
    static Matrix canvasToWorld(const Matrix& center) {
        const auto frame = m::ChargeBadgeState::frame();
        return center * Matrix::translation(frame.x, frame.y + sourceOffset) *
               Matrix::scale(m::ChargeBadgeState::rendererScale, m::ChargeBadgeState::rendererScale);
    }
    std::optional<core::Point> designPoint(core::Point client) const {
        if (!visible) return std::nullopt;
        const auto canvas = projection.unproject({client.x * metrics.scale, client.y * metrics.scale});
        if (!canvas) return std::nullopt;
        const auto frame = m::ChargeBadgeState::frame();
        return core::Point{frame.x + canvas->x * m::ChargeBadgeState::rendererScale, frame.y + canvas->y * m::ChargeBadgeState::rendererScale};
    }
    void refreshHover() {
        // onHitRegionChanged -> refreshChargeHover with the current pointer.
        const bool inside = pointerDesign && state.contains(*pointerDesign, time);
        state.setHovered(inside, !reduced, time);
    }
    void border() {
        const auto colors = [&](bool hovered) {
            return m::chargeIndicatorColors(content.appearance, content.battery, content.metric, std::nullopt, m::ChargeStage::compact, hovered);
        };
        const auto normal = colors(false), hovered = colors(true);
        state.setBorder(normal.borderWidth, normal.border, hovered.borderWidth, hovered.border, time);
    }
};

ChargeBadgePreview::ChargeBadgePreview(gpu::LayerRasterizer& raster, std::function<void()> selectPower)
    : impl_(std::make_unique<Impl>(raster, std::move(selectPower))) {}
ChargeBadgePreview::~ChargeBadgePreview() = default;
void ChargeBadgePreview::resize(const app::ClientMetrics& m) {
    need(m.pixelWidth && m.pixelHeight && std::isfinite(m.scale) && m.scale > 0, "Invalid charge badge viewport");
    impl_->metrics = m;
}
void ChargeBadgePreview::setContent(const m::BatteryReading& reading, const m::ChargeIndicatorAppearance& appearance, m::ChargeMetric metric,
                                    const std::optional<m::ChargeTelemetry>& telemetry) {
    auto& i = *impl_;
    need(appearance.language != core::Language::system, "Resolve the badge language in the owner");
    auto next = i.content;
    next.battery = reading;
    next.appearance = appearance;
    next.metric = metric;
    next.telemetry = m::chargeMetricRequiresTelemetry(metric) ? telemetry : std::nullopt;
    if (i.hasContent && next == i.content) return;
    const bool appearanceChanged = !i.hasContent || next.appearance != i.content.appearance;
    i.content = next;
    i.hasContent = true;
    i.scene.setContent(i.content);
    if (appearanceChanged) i.border();
}
void ChargeBadgePreview::setReduceMotion(bool value, double now) {
    auto& i = *impl_;
    i.clock(now);
    i.reduced = value;
    i.state.setReduceMotion(value);
}
void ChargeBadgePreview::animateEntrance(double now, std::uint64_t seed) { impl_->clock(now); impl_->state.animateEntrance(impl_->time, seed); }
void ChargeBadgePreview::setStable(bool visible, double now) { impl_->clock(now); impl_->state.setStable(visible, impl_->time); }
void ChargeBadgePreview::animateExit(double now, std::uint64_t seed) {
    auto& i = *impl_;
    i.clock(now);
    i.pressed = false;
    i.pointerDesign.reset();
    i.state.animateExit(i.time, seed);
}
void ChargeBadgePreview::cancel(double now) { impl_->clock(now); impl_->pressed = false; impl_->state.cancel(impl_->time); }

m::ChargeBadgeState::Events ChargeBadgePreview::update(const Matrix& center, float opacity, double now) {
    auto& i = *impl_;
    need(i.hasContent, "Charge badge content is required before update");
    need(center.finite() && std::isfinite(opacity), "Invalid charge badge placement");
    i.clock(now);
    const auto events = i.state.advance(i.time);
    if (events.hitRegionChanged) i.refreshHover();
    // Nothing is submitted while closed or during the delayed reveal.
    const auto phase = i.state.phase();
    i.visible = phase != m::ChargeBadgeState::Phase::hidden && phase != m::ChargeBadgeState::Phase::waiting && opacity > 0;
    if (!i.visible) { i.pressed = false; return events; }
    // Raster density follows the effective on-screen size (content event).
    const double d = density(center, i.metrics.scale);
    if (d != i.content.pixelsPerPoint) { i.content.pixelsPerPoint = d; i.scene.setContent(i.content); }
    const auto pose = i.state.sample(i.time);
    gpu::ChargeIndicatorPlacement placement;
    placement.canvasToWorld = Impl::canvasToWorld(center);
    placement.pose = pose;
    placement.stage = i.state.stage();
    placement.embeddedBorder = true;
    placement.hovered = i.state.hovered();
    placement.opacity = static_cast<float>(std::clamp(pose.canvasOpacity * opacity, 0.0, 1.0));
    i.scene.place(placement);
    if (i.metrics.pixelWidth && i.metrics.pixelHeight) {
        const auto camera = gpu::layerViewportProjection(i.metrics.pixelWidth, i.metrics.pixelHeight) * Matrix::scale(i.metrics.scale, i.metrics.scale);
        i.projection = core::Projection::viewport(camera * placement.canvasToWorld, i.metrics.pixelWidth, i.metrics.pixelHeight);
    }
    return events;
}
bool ChargeBadgePreview::requiresFrames(double now) const {
    const auto& i = *impl_;
    return i.visible && i.state.requiresFrames(std::max(now, i.time));
}
std::optional<double> ChargeBadgePreview::nextWakeTime() const { return impl_->state.nextDeadline(); }
bool ChargeBadgePreview::exitFinished() const noexcept { return impl_->state.phase() == m::ChargeBadgeState::Phase::hidden; }
bool ChargeBadgePreview::covers(core::Point p) const {
    const auto& i = *impl_;
    const auto q = i.designPoint(p);
    return q && i.state.contains(*q, i.time);
}
bool ChargeBadgePreview::pointer(const app::PointerEvent& e, double now) {
    auto& i = *impl_;
    i.clock(now);
    if (e.kind == app::PointerKind::leave || e.kind == app::PointerKind::captureLost) {
        const bool used = i.pressed || i.state.hovered();
        i.pressed = false;
        i.pointerDesign.reset();
        i.state.setHovered(false, !i.reduced, i.time);
        return used;
    }
    i.pointerDesign = i.designPoint({e.x, e.y});
    const bool inside = i.pointerDesign && i.state.contains(*i.pointerDesign, i.time);
    if (e.kind == app::PointerKind::move) {
        i.state.setHovered(inside, !i.reduced, i.time);
        return inside;
    }
    if (e.button != app::PointerButton::left) return false;
    if (e.kind == app::PointerKind::down || e.kind == app::PointerKind::doubleClick) {
        if (!inside) return false;
        i.pressed = true;
        i.state.setHovered(true, !i.reduced, i.time);
        return true;
    }
    if (e.kind == app::PointerKind::up) {
        const bool activate = i.pressed && inside;
        const bool used = i.pressed;
        i.pressed = false;
        if (activate && i.selectPower) i.selectPower();
        return used;
    }
    return false;
}
bool ChargeBadgePreview::hovered() const noexcept { return impl_->state.hovered(); }
std::string ChargeBadgePreview::accessibilityLabel() const {
    const auto& i = *impl_;
    if (!i.hasContent) return core::localized("Power / Device Battery", "电量 / 设备电池", core::Language::english);
    return i.scene.content().accessibility;
}
bool ChargeBadgePreview::telemetryDemand() const noexcept {
    return impl_->visible && m::chargeMetricRequiresTelemetry(impl_->content.metric);
}
const m::ChargeBadgeState& ChargeBadgePreview::state() const noexcept { return impl_->state; }
void ChargeBadgePreview::upload(gpu::Renderer& renderer) { if (impl_->visible) impl_->scene.upload(renderer); }
std::span<const gpu::LayerCompositionEntry> ChargeBadgePreview::entries() {
    return impl_->visible ? std::span<const gpu::LayerCompositionEntry>(impl_->composed) : std::span<const gpu::LayerCompositionEntry>{};
}
void ChargeBadgePreview::collected(gpu::Renderer&) {}
void ChargeBadgePreview::release(gpu::Renderer& renderer) {
    need(impl_->scene.release(renderer) && impl_->anchor.releaseResources(renderer), "Detach the charge badge composition before release");
}
} // namespace endfield::tools
#endif
