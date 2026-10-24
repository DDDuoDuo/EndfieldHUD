#include "tools/map_owner.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "modules/map_geography.hpp"
#include "core/source_camera.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/localization.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace d = modules::design;
namespace gpu = native;
using Matrix = core::Matrix4;
using Point = core::Point;
using G = modules::MapSurfaceGeometry;
void need(bool value, const char* message) { if (!value) throw std::invalid_argument(message); }
std::optional<double> earlier(std::optional<double> a, std::optional<double> b) { return a && b ? std::min(*a, *b) : a ? a : b; }
// Scene units (v1.2.0's 440-unit viewport) -> map px of the surface layer:
// map px = (430, 430) + (u - 220) x unitScale.
const Matrix unitsToMap = Matrix::translation(G::center.x - 220 * G::unitScale, G::center.y - 220 * G::unitScale, 0) * Matrix::scale(G::unitScale, G::unitScale);
bool down(const app::PointerEvent& e) { return e.kind == app::PointerKind::down || e.kind == app::PointerKind::doubleClick; }
}

struct MapOwner::Impl {
    modules::MapState state;
    gpu::LayerRasterizer& paint;
    d::Context& context;
    std::shared_ptr<const modules::MapGeography> geography;
    std::function<void()> changed, requestGeography;
    std::function<std::optional<modules::MapSourceView>()> sourceFrame;
    std::unique_ptr<DesignOwnerCore> core;
    modules::MapCanvas* canvas{};
    std::unique_ptr<gpu::NativeMapRaster> raster;
    std::unique_ptr<gpu::NativeMapScene> scene;
    app::ClientMetrics metrics;
    bool alive{true}, ready{}, posed{}, rasterActive{}, overlay{true}, prewarming{}, releasePending{};
    double time{};
    std::uint64_t cameraRevision{}, gestureRevision{}, playerBytes{};
    modules::MapColor accent{};
    gpu::MapAppearance appearance;
    // Desktop Map occlusion: the plates of the presented source frame
    // (client points), the same plates in the static box plane, the
    // published host cut and the entries carrying it.
    modules::MapSourceCutoutPlan cutoutPlan;
    modules::MapCutouts boxPlates;
    Matrix staticBox;
    std::optional<gpu::PlaneAlphaMask> cut;
    bool cutValid{};
    std::vector<gpu::LayerCompositionEntry> composed;
    // Pin pulse fallback: the last presented frame and whether its wake fired.
    static constexpr double pulseFallback = 1. / 30 + 1. / 120;
    std::optional<double> lastFrame;
    bool pulseWakeTaken{};
    Impl(gpu::LayerRasterizer& r, d::Context& c, MapOwnerOptions& o)
        : state(std::move(o.initial), std::move(o.persistence)), paint(r), context(c), geography(std::move(o.geography)),
          changed(std::move(o.changed)), requestGeography(std::move(o.requestGeography)), sourceFrame(std::move(o.sourceFrame)) { composed.reserve(16); }
    void clock(double t) { need(std::isfinite(t), "Map owner needs finite time"); time = std::max(time, t); }
    void notify() { if (!alive || !ready) return; auto callback = changed; if (callback) callback(); }
    // The raster follows the surface's camera (its WorldMapGeometry space) and
    // paints the final detail once a gesture settles (applyCamera / endGesture).
    // The posed surface takes the change at once (its finite motion starts now).
    void synchronize() {
        if (!ready) return;
        if (cameraRevision != state.cameraRevision()) { cameraRevision = state.cameraRevision(); raster->update(modules::mapConstrained(state.camera()), state.cameraAnimated(), time); }
        if (gestureRevision != state.gestureRevision()) { gestureRevision = state.gestureRevision(); raster->finishGesture(time); }
        if (posed) scene->syncContent(time);
    }
    // WorldMapSurface.prepareForPresentation / releaseWorkingSet. The launch
    // prewarm (Windows only) builds the page without starting the painter.
    void prepare() { if (prewarming) return; if (!rasterActive) { rasterActive = true; raster->setActive(true, time); } if (requestGeography) requestGeography(); }
    void releaseWorkingSet() { if (rasterActive) { rasterActive = false; raster->setActive(false, time); } }
    // The raster's style: always dark, the palette's yellow for the centre
    // country (WorldMapCanvas.swift:59, 154), painted at the map's density.
    void configure() {
        const auto y = context.env().palette.yellow();
        accent = {y.r, y.g, y.b, 1};
        raster->configure(true, accent, scene->density() * G::unitScale, time);
    }
    bool plate(Point p) const { return cutoutPlan.contains(p); }
    std::optional<double> pulseWake() const { if (!posed || !overlay || !lastFrame || pulseWakeTaken || !scene->pulsing()) return std::nullopt; return *lastFrame + pulseFallback; }
    // SystemHUDView.applySourceMapOcclusion: the plates of the presented
    // source frame (HUDSourceWatchView.updateDesktopBottomSilhouettes), each
    // vertex inverted through the static box plane; a loop that does not
    // invert is dropped. Allocation-free.
    void occlude(const core::source::DesktopChromeSettings& settings, const Matrix& camera) {
        std::optional<modules::MapSourceView> view;
        if (sourceFrame && settings.sourceShell) view = sourceFrame();
        if (view) (void)cutoutPlan.update(*view, {0, 0, metrics.width, metrics.height}); else (void)cutoutPlan.clear();
        const auto& plates = cutoutPlan.cutouts();
        const auto plane = core::Projection::viewport(camera * staticBox, metrics.pixelWidth, metrics.pixelHeight);
        boxPlates.count = 0;
        for (std::size_t k = 0; k < plates.count; ++k) {
            auto& polygon = boxPlates.polygons[boxPlates.count]; polygon.count = 0; bool inverted = true;
            for (const auto p : plates.polygons[k].view()) {
                const auto q = plane.unproject({p.x * metrics.scale, p.y * metrics.scale});
                if (!q || !std::isfinite(q->x) || !std::isfinite(q->y)) { inverted = false; break; }
                polygon.points[polygon.count++] = *q;
            }
            if (inverted) ++boxPlates.count;
        }
        (void)scene->setHostCut(boxPlates, boxPlates.count > 0);
    }
};

MapOwner::MapOwner(gpu::LayerRasterizer& r, app::UtilityExecutor& utility, d::Context& context, MapOwnerOptions o) : impl_(std::make_unique<Impl>(r, context, o)) {
    auto& i = *impl_;
    need(bool(o.clock), "Map owner needs the shared completion clock");
    i.time = o.clock();
    need(std::isfinite(i.time) && i.time >= 0, "Invalid initial Map clock");
    const auto sceneOptions = o.owner.raster;
    i.core = std::make_unique<DesignOwnerCore>(r, context, std::move(o.owner));
    auto canvas = std::make_unique<modules::MapCanvas>(context, i.state, std::move(o.storeError));
    i.canvas = canvas.get();
    canvas->onPinStyleChanged = std::move(o.pinStyleChanged);
    canvas->onRecenter = std::move(o.recentered);
    canvas->onPrepare = [&i] { i.prepare(); };
    canvas->onRelease = [&i] { i.releaseWorkingSet(); };
    i.core->add(std::move(canvas));
    Impl* self = &i;
    i.raster = std::make_unique<gpu::NativeMapRaster>(utility, [self, clock = std::move(o.clock)] { const auto now = clock(); need(std::isfinite(now), "Invalid Map completion clock"); return std::max(now, self->time); },
        [self] { self->notify(); }, std::move(o.paint), std::move(o.releaseWorkerCaches));
    for (const auto* image : {o.player.halo.get(), o.player.beam.get(), o.player.glyph.get()}) if (image) i.playerBytes += image->straightRGBA.size();
    i.scene = std::make_unique<gpu::NativeMapScene>(i.state, *i.raster, r, sceneOptions, std::move(o.player), i.appearance);
    i.configure();
    i.raster->setGeography(i.geography, i.time);
    i.raster->update(modules::mapConstrained(i.state.camera()), false, i.time);
    i.cameraRevision = i.state.cameraRevision(); i.gestureRevision = i.state.gestureRevision();
    i.ready = true;
}
MapOwner::~MapOwner() {
    auto& i = *impl_;
    i.alive = false; i.ready = false; i.changed = {};
    i.raster.reset();
}
// WorldMapSurface.requestData's completion (:240-260): a failed dataset is a
// notice (the country shapes' after the terrain's), the other still renders
// and the pins stay interactive.
void MapOwner::setGeography(std::shared_ptr<const modules::MapGeography> g, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (i.geography == g) return;
    i.geography = std::move(g);
    i.raster->setGeography(i.geography, i.time);
    if (!i.geography) return;
    auto& env = i.context.env();
    env.now = std::max(env.now, i.time);
    if (i.geography->terrainFailed) i.canvas->reportError(core::localized("Terrain data could not be loaded.", "无法载入地形数据。", env.language));
    if (i.geography->countriesFailed) i.canvas->reportError(core::localized("Country shapes could not be loaded.", "无法载入地图轮廓。", env.language));
}
bool MapOwner::utilityCompleted(double t) { auto& i = *impl_; i.clock(t); return i.raster->submitPending(i.time); }
bool MapOwner::perform(std::string_view id, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.canvas->isActive()) return false;
    const bool used = i.canvas->performAccessibilityAction(id, i.time);
    i.synchronize();
    return used;
}
const gpu::PlaneAlphaMask* MapOwner::hostCut() const noexcept { return impl_->cutValid ? &*impl_->cut : nullptr; }
const modules::MapState& MapOwner::state() const noexcept { return impl_->state; }
modules::MapCanvas& MapOwner::canvas() noexcept { return *impl_->canvas; }
DesignOwnerCore& MapOwner::core() noexcept { return *impl_->core; }
gpu::NativeMapRaster::Stats MapOwner::rasterStats() const noexcept { return impl_->raster->stats(); }
gpu::MapSceneStats MapOwner::sceneStats() const noexcept { return impl_->scene->stats(); }
bool MapOwner::pulsing() const noexcept { return impl_->posed && impl_->overlay && impl_->scene->pulsing(); }
const modules::MapCutouts& MapOwner::sourceCutouts() const noexcept { return impl_->cutoutPlan.cutouts(); }

std::string_view MapOwner::name() const noexcept { return impl_->core->options().name; }
app::ModuleRouting MapOwner::routing() const noexcept { return impl_->core->options().routing; }
void MapOwner::resize(const app::ClientMetrics& m) { impl_->core->resize(m); impl_->metrics = m; }
void MapOwner::setAppearance(const app::ModuleAppearance& a, double t) {
    auto& i = *impl_;
    i.clock(t);
    i.core->setAppearance(a, t);
    // The Mac stops the pin pulse with ambient motion off and in low-power visual mode.
    i.appearance = {a.reduceMotion, a.ambient && !a.lowPower};
    (void)i.scene->setAppearance(i.appearance);
    i.state.setReducedMotion(a.reduceMotion);
    if (a.reduceMotion) i.scene->settle();
    i.configure();
}
void MapOwner::setOverlayVisible(bool v, double t) {
    auto& i = *impl_;
    i.clock(t);
    i.overlay = v;
    i.core->setOverlayVisible(v, t);
    i.synchronize();
}
void MapOwner::overlayClosing(double t) { auto& i = *impl_; i.clock(t); i.core->overlayClosing(t); i.overlay = false; i.synchronize(); }
void MapOwner::update(const app::ModuleFrame& f) {
    auto& i = *impl_;
    i.clock(f.time);
    i.core->update(f);
    i.synchronize();
    const auto pose = i.core->pose(core::Module::map);
    const auto& sample = f.presentation;
    const bool shown = sample.current.module == core::Module::map || (sample.incoming && sample.incoming->module == core::Module::map);
    // Module input stopped on another module (releaseRasterWorkingSet: only
    // while the canvas is inactive).
    if (!shown && !i.canvas->isActive()) i.releaseWorkingSet();
    i.cutValid = false;
    if (!pose || !i.canvas->surfaceLayer()) { i.posed = false; (void)i.cutoutPlan.clear(); i.lastFrame.reset(); return; }
    if (i.scene->setDensity(i.context.boxScale())) i.configure();
    i.scene->syncContent(i.time);
    // The surface layer's presented world (the well's entrance stagger and a
    // sub-page swap included) and opacity, then units -> map px.
    const auto layer = d::sampleLayer(*i.canvas->surfaceLayer(), i.canvas->root(), i.time);
    const auto world = pose->boxWorld * gpu::designMatrix(layer.world) * unitsToMap;
    i.scene->updatePose({world, layer.visible ? float(pose->opacity * layer.opacity) : 0.f, i.time, pose->ownerMasks});
    i.posed = true; i.releasePending = false;
    i.staticBox = core::source::designBoxWorld(f.center);
    const auto camera = gpu::layerViewportProjection(i.metrics.pixelWidth, i.metrics.pixelHeight) * Matrix::scale(i.metrics.scale, i.metrics.scale);
    i.occlude(f.chrome, camera);
    i.lastFrame = i.time; i.pulseWakeTaken = false;
}
// The launch prewarm: the canvas's first page (the core) and the surface's
// content, nothing activated, posed or presented.
void MapOwner::prewarm(const app::ModuleFrame& f) {
    auto& i = *impl_;
    i.clock(f.time);
    i.prewarming = true;
    try { i.core->prewarm(f); } catch (...) { i.prewarming = false; throw; }
    i.prewarming = false;
    if (f.presentation.requested == core::Module::map) { (void)i.scene->setDensity(i.context.boxScale()); i.scene->syncContent(i.time); }
}
bool MapOwner::requiresFrames(double t) const {
    const auto& i = *impl_;
    return i.core->requiresFrames(t) || (i.posed && i.overlay && i.scene->requiresFrames(std::max(t, i.time)));
}
// Pulse fallback: no frame came for a tick and a quarter while the pins
// pulse (the ambient cadence is off): one frame; the next presented frame re-arms it.
std::optional<double> MapOwner::nextWakeTime(double t) const {
    const auto& i = *impl_;
    return earlier(earlier(i.core->nextWakeTime(t), i.raster->nextWakeTime()), i.pulseWake());
}
bool MapOwner::deadline(double t) {
    auto& i = *impl_;
    i.clock(t);
    const auto before = i.state.revision();
    bool woke = i.core->deadline(t);
    i.synchronize();
    woke |= i.raster->advance(i.time);
    if (const auto due = i.pulseWake(); due && i.time >= *due) { i.pulseWakeTaken = true; woke = true; }
    return woke || before != i.state.revision();
}
void MapOwner::upload(gpu::Renderer& r) {
    auto& i = *impl_;
    i.core->upload(r);
    i.cutValid = false;
    // Hidden on another module: the host cut goes once no published draw samples it.
    if (!i.posed) { if (i.scene->stats().hostCutBytes) (void)i.scene->releaseHostCut(r); return; }
    i.scene->uploadResources(r);
    if (const auto* texture = i.scene->hostCutTexture()) {
        if (!i.cut) i.cut = gpu::PlaneAlphaMask{{}, gpu::mapHostCutBounds(), *texture};
        i.cut->worldToLocal = core::source::inverseSourceMatrix(i.staticBox);
        i.cutValid = true;
    }
}
std::span<const gpu::LayerCompositionEntry> MapOwner::entries(core::Module m) {
    auto& i = *impl_;
    i.composed.clear();
    if (m != core::Module::map || !i.posed) return {};
    const auto* cut = i.cutValid ? &*i.cut : nullptr;
    for (const auto& e : i.scene->entries()) { i.composed.push_back(e); if (cut && !i.composed.back().cut) i.composed.back().cut = cut; }
    for (const auto& e : i.core->entries(m)) { i.composed.push_back(e); if (cut && !i.composed.back().cut) i.composed.back().cut = cut; }
    return i.composed;
}
void MapOwner::collected(gpu::Renderer& r) {
    auto& i = *impl_;
    i.core->collected(r);
    // After the close's publication no draw samples the surface any more.
    if (i.releasePending && !i.posed && i.scene->releaseResources(r)) i.releasePending = false;
}
void MapOwner::release(gpu::Renderer& r) {
    auto& i = *impl_;
    i.cutValid = false;
    i.core->release(r);
    need(i.scene->releaseResources(r), "Map resources still published at teardown");
}
bool MapOwner::covers(Point p) const { return !impl_->plate(p) && impl_->core->covers(p); }
bool MapOwner::pointerLocked() const { return impl_->core->pointerLocked(); }
bool MapOwner::capturesPointer() const { return impl_->core->capturesPointer(); }
bool MapOwner::capturesWheel() const { return impl_->core->capturesWheel(); }
bool MapOwner::capturesKeys() const { return impl_->core->capturesKeys(); }
// A press inside a plate belongs to the navigation button above the Map
// (SystemHUDView.mouseDown / rightMouseDown skip the Map at a navigation target).
bool MapOwner::pointer(const app::PointerEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (down(e) && i.plate({e.x, e.y}) && !i.core->capturesPointer()) return false;
    const bool used = i.core->pointer(e, t);
    i.synchronize();
    return used;
}
// One Win32 wheel detent is one Mac notch (12 px for MapCanvas::handleScroll;
// a touchpad's fractional detents stay fractions). A pinch skips the plates
// (SystemHUDView.magnify asks the Map only off them); the wheel still zooms there.
bool MapOwner::wheel(const app::WheelEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (app::pinchMagnification(e) && i.plate({e.x, e.y})) return false;
    auto detents = e;
    if (!app::pinchMagnification(e)) detents.linesPerStep = 1;
    const bool used = i.core->wheel(detents, t);
    i.synchronize();
    return used;
}
bool MapOwner::key(const app::KeyEvent& e, double t) { const bool used = impl_->core->key(e, t); impl_->synchronize(); return used; }
bool MapOwner::key(const app::KeyEvent& e, std::uint32_t modifiers, double t) { const bool used = impl_->core->key(e, modifiers, t); impl_->synchronize(); return used; }
bool MapOwner::message(const app::NativeMessage& m, double t) { return impl_->core->message(m, t); }
void MapOwner::focus(bool v, double t) { auto& i = *impl_; i.core->focus(v, t); if (!v) { i.core->cancelInteraction(t); i.synchronize(); } }
void MapOwner::cancelInteraction(double t) { auto& i = *impl_; i.clock(t); i.core->cancelInteraction(t); i.synchronize(); }
bool MapOwner::hostEvent(const app::HostEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    const bool changed = i.core->hostEvent(e, t);
    if (e.kind == app::HostEvent::Kind::overlayConcealed) {
        // The Mac 1.3.0 closed HUD keeps no map (OverlayController's closed-heap
        // cleanup after releasing the view): the decoded geography, the terrain
        // images and the surface's textures go; the next show decodes and
        // paints again on the worker (the well and pins show meanwhile).
        if (i.geography) { i.geography.reset(); i.raster->setGeography(nullptr, i.time); }
        i.raster->releaseImages(i.time);
        i.posed = false; i.cutValid = false; i.lastFrame.reset();
        i.releasePending = true;
    }
    return changed;
}
bool MapOwner::needsGeography() const noexcept { return !impl_->geography; }
std::optional<app::ModuleCursor> MapOwner::cursorAt(Point p) const { return impl_->plate(p) ? std::nullopt : impl_->core->cursorAt(p); }
bool MapOwner::automationRefresh(IRawElementProviderFragmentRoot* root) { return impl_->core->automationRefresh(root); }
std::size_t MapOwner::automationCount() const { return impl_->core->automationCount(); }
long MapOwner::automationChild(std::size_t n, IRawElementProviderFragment** out) const { return impl_->core->automationChild(n, out); }
long MapOwner::automationFromPoint(double x, double y, IRawElementProviderFragment** out) const { return impl_->core->automationFromPoint(x, y, out); }
void MapOwner::automationDisconnect() noexcept { impl_->core->automationDisconnect(); }
std::optional<core::Module> MapOwner::automationModule() const noexcept { return impl_->core->automationModule(); }
app::ModuleResidency MapOwner::residency() const {
    const auto& i = *impl_;
    app::ModuleResidency r;
    r.cpuBytes = i.playerBytes;
    for (const auto& image : {i.raster->detail(), i.raster->backdrop()}) if (image) r.cpuBytes += image->straightRGBA.size();
    r.cpuBytes += i.scene->stats().cutoutMaskBytes; // staging of the host cut's updates
    // Decoded geography: every retained vertex (country paths keep their whole path and its components).
    if (i.geography) {
        if (i.geography->terrain) r.cpuBytes += i.geography->terrain->vertexCount * sizeof(Point);
        if (i.geography->countries) r.cpuBytes += i.geography->countries->vertexCount * 2 * sizeof(Point);
    }
    return r;
}
} // namespace endfield::tools
#endif
