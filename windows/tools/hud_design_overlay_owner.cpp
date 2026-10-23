#include "tools/hud_design_overlay_owner.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "native/screen_reader_peers.hpp"
#include "tools/hud_design_owner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace d = modules::design;
namespace gpu = native;
using Matrix = core::Matrix4;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
// The overlay's trees span several arenas (the overlay, the confirm bar, the
// toast generation): a content key over every layer's arena revision,
// content revision and identity, so a change anywhere re-emits. Allocation-
// free; a few dozen layers.
std::uint64_t treeRevision(const d::Layer& layer, unsigned depth = 0) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix(reinterpret_cast<std::uintptr_t>(&layer)); mix(layer.arena().revision()); mix(layer.contentRevision()); mix(layer.children().size());
    if (depth < 64) for (const auto* child : layer.children()) mix(treeRevision(*child, depth + 1));
    return h;
}
std::optional<core::Rect> projected(const core::Projection& projection, double scale, core::Rect r) {
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (const core::Point c : {core::Point{r.x, r.y}, core::Point{r.x + r.width, r.y}, core::Point{r.x, r.y + r.height}, core::Point{r.x + r.width, r.y + r.height}}) {
        const auto q = projection.project(c);
        if (!q || !std::isfinite(q->x) || !std::isfinite(q->y)) return std::nullopt;
        x0 = std::min(x0, q->x); y0 = std::min(y0, q->y); x1 = std::max(x1, q->x); y1 = std::max(y1, q->y);
    }
    return core::Rect{x0 / scale, y0 / scale, (x1 - x0) / scale, (y1 - y0) / scale};
}
std::string hex(const std::array<double, 3>& c) {
    char out[7]{};
    const auto byte = [](double v) { return unsigned(std::lround(std::clamp(std::isfinite(v) ? v : 0., 0., 1.) * 255)); };
    std::snprintf(out, sizeof out, "%02X%02X%02X", byte(c[0]), byte(c[1]), byte(c[2]));
    return out;
}
}

struct DesignOverlayOwner::Impl {
    gpu::LayerRasterizer& raster;
    DesignOverlayOptions options;
    gpu::NativeDesignTextMeasurer measurer;
    // CenterOverlay's queries are non-const members of the Context.
    mutable d::Context context;
    gpu::NativeDesignScene scene;
    std::unique_ptr<gpu::NativeDesignAutomation> automation;
    app::ClientMetrics metrics;
    core::Projection projection;
    core::Matrix4 center;
    std::optional<app::ModuleAppearance> appearance;
    d::TextRegion region{gpu::preferredSystemRegion()};
    double density{2}, time{};
    std::uint64_t synced{}; // the overlay tree revision the scene holds
    bool overlay{}, posed{}, composed{}, pressed{};
    Impl(gpu::LayerRasterizer& r, DesignOverlayOptions o)
        : raster(r), options(std::move(o)), measurer(r), context(measurer, gpu::nativeUppercase()), scene(r, options.raster, "design.overlay") {
        context.env().characters = gpu::nativeCharacterRules(); // grapheme clusters through ICU, never the portable approximation
    }
    void clock(double t) {
        need(std::isfinite(t), "Design overlay needs finite time");
        time = std::max(time, t);
        context.env().now = std::max(context.env().now, time);
    }
    double density0() const { return std::clamp(context.boxScale(), 1., 4.); }
    bool capturing() const { return context.overlay().capturesInput(); }
    bool dimAnimating(double now) const {
        auto& o = context.overlay();
        const auto amount = o.dimFilter(now);
        return o.dimmed() ? amount < 1 : amount > 0;
    }
    // What the overlay shows: a bar or toast (the dim is on while either
    // shows), or the motion of their hide.
    // The scene's tree is the overlay's only while synced to its revision.
    bool presented(double now) const {
        auto& o = context.overlay();
        return o.dimmed() || o.capturesInput() || o.filterNeeded(now) || (posed && synced == treeRevision(o.layer()) && scene.requiresFrames(now));
    }
    bool apply() {
        if (!appearance) return false;
        d::Context::Appearance a;
        a.language = appearance->language == core::Language::system ? core::Language::english : appearance->language;
        a.systemRegion = region;
        a.accentHex = hex(appearance->accentSRGB);
        a.light = !appearance->dark;
        a.reduceMotion = appearance->reduceMotion;
        a.recordingMode = options.recordingMode && options.recordingMode();
        return context.apply(a, time);
    }
    std::optional<core::Point> local(core::Point logical) const { return projection.unproject({logical.x * metrics.scale, logical.y * metrics.scale}); }
};

DesignOverlayOwner::DesignOverlayOwner(gpu::LayerRasterizer& r, DesignOverlayOptions o) : impl_(std::make_unique<Impl>(r, std::move(o))) {}
DesignOverlayOwner::~DesignOverlayOwner() { impl_->automation.reset(); }
d::Context& DesignOverlayOwner::context() noexcept { return impl_->context; }
gpu::NativeDesignTextMeasurer& DesignOverlayOwner::measurer() noexcept { return impl_->measurer; }
double DesignOverlayOwner::contentDim(double now) const noexcept { return impl_->context.overlay().dimFilter(now); }
bool DesignOverlayOwner::presented() const noexcept { return impl_->composed; }
std::optional<gpu::ContentFilter> DesignOverlayOwner::contentFilter(double now, std::size_t begin, std::size_t count) const {
    const auto& i = *impl_;
    auto& o = i.context.overlay();
    if (!i.overlay || !count || !o.filterNeeded(now)) return std::nullopt;
    double unit{};
    try { unit = core::source::planeScale(i.center, {500, 320}) * i.metrics.scale; } catch (const std::exception&) { return std::nullopt; }
    if (!std::isfinite(unit) || unit <= 0) return std::nullopt;
    return gpu::ContentFilter{begin, count, {}, core::ContentDimTrack::blurRadius * unit, core::ContentDimTrack::exposure, std::clamp(o.dimFilter(now), 0., 1.)};
}
// The quit card's slot (app/quit_confirmation.cpp): every capture chain first.
app::ModuleRouting DesignOverlayOwner::routing() const noexcept {
    app::ModuleRouting r;
    r.pointerCapture = 0; r.wheelCapture = 0; r.keyCapture = 0;
    return r;
}
void DesignOverlayOwner::resize(const app::ClientMetrics& m) {
    need(m.pixelWidth && m.pixelHeight && std::isfinite(m.scale) && m.scale > 0, "Invalid design overlay viewport");
    impl_->metrics = m;
}
// HUDRuntimeAppearance.applyPalette -> HUDColor.apply + refreshPalette, the
// language, Reduce Motion and Recording mode: once here for every canvas.
void DesignOverlayOwner::setAppearance(const app::ModuleAppearance& a, double t) {
    auto& i = *impl_;
    i.clock(t);
    i.appearance = a;
    i.apply();
}
void DesignOverlayOwner::setOverlayVisible(bool v, double t) {
    auto& i = *impl_;
    i.clock(t);
    i.overlay = v;
    if (!v) i.context.overlay().reset();
}
void DesignOverlayOwner::overlayClosing(double t) { setOverlayVisible(false, t); } // CenterOverlay::reset
void DesignOverlayOwner::update(const app::ModuleFrame& frame) {
    auto& i = *impl_;
    i.clock(frame.time);
    // The overlay sits on the content box of the centre plane, without the
    // switch motion (DesktopChromeLayout::boxWorld).
    i.center = frame.center;
    const auto boxWorld = core::source::designBoxWorld(frame.center);
    i.density = designDensity(boxWorld, i.metrics.scale, i.density);
    i.context.update(d::geometry::baseUnit, i.density / d::geometry::baseUnit, i.time);
    const auto camera = gpu::layerViewportProjection(i.metrics.pixelWidth, i.metrics.pixelHeight) * Matrix::scale(i.metrics.scale, i.metrics.scale);
    i.projection = core::Projection::viewport(camera * boxWorld, i.metrics.pixelWidth, i.metrics.pixelHeight);
    const auto& root = i.context.overlay().layer();
    if (!i.overlay || !i.presented(i.time)) { i.composed = false; return; }
    i.synced = treeRevision(root);
    i.scene.syncContent(root, i.context.env().text, i.synced, i.density0(), i.time);
    i.scene.updatePose({boxWorld, frame.opacity, i.time, {}, {}});
    i.posed = true;
    i.composed = true;
}
bool DesignOverlayOwner::requiresFrames(double t) const {
    const auto& i = *impl_;
    if (!i.overlay) return false;
    const auto now = std::max(t, i.time);
    if (i.dimAnimating(now)) return true;
    // A bar or toast that just opened needs its first frame; then only its
    // own motion asks for frames (a settled bar costs nothing).
    auto& o = i.context.overlay();
    if (i.presented(now) != i.composed) return true;
    return i.composed && i.synced == treeRevision(o.layer()) && i.scene.requiresFrames(now);
}
std::optional<double> DesignOverlayOwner::nextWakeTime(double) const { return impl_->context.nextWakeTime(); }
// The shared deadlines (120 ms confirm callback, 1.5 s toast, hides, scroll
// bars, module timers) run here for every canvas.
bool DesignOverlayOwner::deadline(double t) {
    auto& i = *impl_;
    i.clock(t);
    return i.context.advance(i.time);
}
void DesignOverlayOwner::upload(gpu::Renderer& r) { auto& i = *impl_; if (i.composed) i.scene.uploadAnimations(r); }
std::span<const gpu::LayerCompositionEntry> DesignOverlayOwner::modalEntries() {
    auto& i = *impl_;
    if (!i.composed) return {};
    return i.scene.entries();
}
void DesignOverlayOwner::collected(gpu::Renderer& r) {
    auto& i = *impl_;
    need(i.scene.collectRetired(r), "Design overlay artwork still published");
    // At rest the overlay keeps no artwork (it shows a few times a session).
    if (!i.composed && i.scene.park(r)) i.posed = false;
}
void DesignOverlayOwner::release(gpu::Renderer& r) { need(impl_->scene.releaseResources(r), "Design overlay artwork still published at teardown"); }
bool DesignOverlayOwner::covers(core::Point) const { return impl_->overlay && impl_->capturing(); }
bool DesignOverlayOwner::capturesPointer() const { return impl_->overlay && impl_->capturing(); }
bool DesignOverlayOwner::capturesWheel() const { return capturesPointer(); }
bool DesignOverlayOwner::capturesKeys() const { return capturesPointer(); }
bool DesignOverlayOwner::suppressesKeyFilters() const { return capturesPointer(); }
// While a confirm bar captures, every event is consumed: presses go to the
// pills (or a twin-pill request's outside press), Esc / Return decide.
bool DesignOverlayOwner::pointer(const app::PointerEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!capturesPointer()) return false;
    auto& o = i.context.overlay();
    const auto q = i.local({e.x, e.y});
    const bool left = e.button == app::PointerButton::left;
    switch (e.kind) {
    case app::PointerKind::down: case app::PointerKind::doubleClick:
        if (left && q) { i.pressed = true; o.pointerDown(*q); o.pointerMoved(q, true); }
        break;
    case app::PointerKind::move:
        if (i.pressed && q) o.pointerDragged(*q);
        o.pointerMoved(q, i.pressed);
        break;
    case app::PointerKind::up:
        if (left && i.pressed) { i.pressed = false; o.pointerUp(q.value_or(core::Point{-1e6, -1e6})); o.pointerMoved(q, false); }
        break;
    case app::PointerKind::leave: case app::PointerKind::captureLost:
        if (i.pressed) { i.pressed = false; o.pointerUp({-1e6, -1e6}); }
        o.pointerMoved(std::nullopt, false);
        break;
    }
    return true;
}
bool DesignOverlayOwner::wheel(const app::WheelEvent&, double t) { impl_->clock(t); return capturesPointer(); }
bool DesignOverlayOwner::key(const app::KeyEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!capturesKeys()) return false;
    if (e.kind != app::KeyKind::down) return true;
    d::KeyInput k;
    k.virtualKey = e.value; k.extended = e.extended; k.repeat = e.previouslyDown; k.alt = e.alt;
    (void)i.context.overlay().handleKey(k);
    return true;
}
bool DesignOverlayOwner::hostEvent(const app::HostEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (e.kind == app::HostEvent::Kind::systemSettingsChanged) {
        // WM_SETTINGCHANGE "intl": the preferred UI languages may name another
        // CJK region for English / System text.
        const auto region = gpu::preferredSystemRegion();
        if (region == i.region) return false;
        i.region = region;
        return i.apply();
    }
    if (e.kind == app::HostEvent::Kind::settingsChanged) return i.apply();
    return false;
}
bool DesignOverlayOwner::automationRefresh(IRawElementProviderFragmentRoot* root) {
    auto& i = *impl_;
    if (!i.automation) {
        gpu::DesignAutomationHost host;
        host.root = root;
        host.items = [&i] {
            std::vector<d::AccessibilityItem> items;
            if (i.overlay && i.capturing()) i.context.overlay().accessibilityItems(items);
            return items;
        };
        host.toScreen = [&i](core::Rect box) -> std::optional<core::Rect> {
            if (!i.posed) return std::nullopt;
            return gpu::screenReaderClientToScreen(i.options.window, i.metrics.scale, projected(i.projection, i.metrics.scale, box));
        };
        // A pill's press exactly as its click (activate).
        host.invoke = [&i](std::string_view id) {
            if (!i.overlay || !i.capturing()) return false;
            i.clock(i.options.now ? i.options.now() : i.time);
            for (auto* control : i.context.overlay().router().controls())
                if (control->id() == id && control->enabled()) { control->activate(); return true; }
            return false;
        };
        host.changed = [&i] { if (i.options.changed) i.options.changed(); };
        i.automation = std::make_unique<gpu::NativeDesignAutomation>(std::move(host));
    }
    return i.automation->refresh(root);
}
std::size_t DesignOverlayOwner::automationCount() const { return impl_->automation ? impl_->automation->size() : 0; }
long DesignOverlayOwner::automationChild(std::size_t n, IRawElementProviderFragment** out) const {
    if (!impl_->automation) { if (out) *out = nullptr; return 0x80070057L; } // E_INVALIDARG
    return impl_->automation->child(n, out);
}
long DesignOverlayOwner::automationFromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!impl_->automation) { if (out) *out = nullptr; return 0; }
    return impl_->automation->fromPoint(x, y, out);
}
void DesignOverlayOwner::automationDisconnect() noexcept { if (impl_->automation) impl_->automation->disconnect(); }
} // namespace endfield::tools
#endif
