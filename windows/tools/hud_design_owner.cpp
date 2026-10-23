#include "tools/hud_design_owner.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "native/module_scene.hpp"
#include "native/screen_reader_peers.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace d = modules::design;
namespace gpu = native;
using Matrix = core::Matrix4;
using Json = ehud::data::Json;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
Json blank() { return Json::Object{{"bounds", Json::Array{0, 0, 590, 510}}, {"position", Json::Array{0, 0}}, {"anchorPoint", Json::Array{0, 0}}, {"children", Json::Array{}}}; }
// Bounding rect of a box rect's projected corners, in logical client points
// (the helper tools/settings_preview.cpp keeps for the v1.2.0 pages).
std::optional<core::Rect> projected(const core::Projection& projection, double scale, core::Rect r) {
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) || !std::isfinite(r.height) || r.width < 0 || r.height < 0 || !(scale > 0)) return std::nullopt;
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (const core::Point c : {core::Point{r.x, r.y}, core::Point{r.x + r.width, r.y}, core::Point{r.x, r.y + r.height}, core::Point{r.x + r.width, r.y + r.height}}) {
        const auto q = projection.project(c);
        if (!q || !std::isfinite(q->x) || !std::isfinite(q->y)) return std::nullopt;
        x0 = std::min(x0, q->x); y0 = std::min(y0, q->y); x1 = std::max(x1, q->x); y1 = std::max(y1, q->y);
    }
    return core::Rect{x0 / scale, y0 / scale, (x1 - x0) / scale, (y1 - y0) / scale};
}
bool inside(core::Rect r, core::Point p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
// Win32 MK_* bits of a pointer event.
constexpr std::uint32_t mkShift = 0x0004, mkControl = 0x0008;
}

double designDensity(const core::Matrix4& boxWorld, double dpiScale, double current) {
    if (!boxWorld.finite() || !(dpiScale > 0) || !std::isfinite(dpiScale)) return current;
    double raw{};
    try { raw = core::source::planeScale(boxWorld, {d::metric::boxWidth / 2, d::metric::boxHeight / 2}) * dpiScale; } catch (const std::exception&) { return current; }
    if (!std::isfinite(raw) || raw <= 0) return current;
    raw = std::clamp(raw, 1., 4.);
    // Quantized to .5 with hysteresis: the parallax tilt never flips a step.
    if (current >= 1 && current <= 4 && std::abs(raw - current) <= .35) return current;
    return std::clamp(std::round(raw * 2) / 2, 1., 4.);
}

struct DesignOwnerCore::Impl {
    struct Slot {
        std::unique_ptr<d::Canvas> canvas;
        core::Module module;
        std::unique_ptr<gpu::NativeDesignScene> scene;
        gpu::LayerScene geometry;
        gpu::NativeModuleSurface surface;
        core::Projection projection;
        std::optional<gpu::ModuleSurfacePose> pose; // presented this frame (drawn and hit)
        bool shown{}, active{}, input{}, parkWanted{};
        std::optional<double> entrance;             // ModuleEntrance::start played
        std::uint64_t synced{};                     // canvas contentRevision the scene holds
        Slot(std::unique_ptr<d::Canvas> c, gpu::LayerRasterizer& r, const gpu::LayerRasterOptions& o, const DesignOwnerOptions& options)
            : canvas(std::move(c)), module(canvas->module()), geometry(r), surface(prepare(o), module) {
            scene = std::make_unique<gpu::NativeDesignScene>(r, o, options.name + "." + std::string(core::moduleIdentifier(module)), options.images);
        }
        gpu::LayerScene& prepare(const gpu::LayerRasterOptions& o) { geometry.load(blank(), o); return geometry; }
    };
    gpu::LayerRasterizer& raster;
    d::Context& context;
    DesignOwnerOptions options;
    std::vector<std::unique_ptr<Slot>> slots;
    std::unique_ptr<gpu::NativeDesignTextFields> fields;
    Slot* fieldSlot{};
    std::unique_ptr<gpu::NativeDesignAutomation> automation;
    app::ClientMetrics metrics;
    bool overlay{true}, focused{true}, pressed{};
    Slot* captured{};
    gpu::Renderer* renderer{};          // the one that holds the scenes (upload / collected)
    std::optional<core::Point> pointer; // logical client
    std::vector<gpu::LayerCompositionEntry> composed;
    double time{};
    Impl(gpu::LayerRasterizer& r, d::Context& c, DesignOwnerOptions o) : raster(r), context(c), options(std::move(o)) {
        need(!options.name.empty(), "Design owner needs a name");
        if (options.fields.window) fields = std::make_unique<gpu::NativeDesignTextFields>(r, options.fields);
        composed.reserve(64);
    }
    void clock(double t) { need(std::isfinite(t), "Design owner needs finite time"); time = std::max(time, t); }
    // The context's one editor host follows the owner whose canvas runs.
    void bind() { context.setEditorHost(fields.get()); }
    Slot* input() const { for (const auto& s : slots) if (s->input && s->pose) return s.get(); return nullptr; }
    std::optional<core::Point> local(const Slot& s, core::Point logical) const { return s.projection.unproject({logical.x * metrics.scale, logical.y * metrics.scale}); }
    double density() const { return std::clamp(context.boxScale(), 1., 4.); }
    gpu::LayerRasterOptions raster0() const { auto o = options.raster; o.pixelsPerPoint = density(); return o; }
    void sync(Slot& s) {
        bind();
        const auto revision = s.canvas->contentRevision();
        const bool rebuilt = s.scene->syncContent(s.canvas->root(), context.env().text, revision, density(), time);
        // Content replaced under a still pointer: the highlight follows it
        // (HUDControlHighlightLayer refreshes after a repaint).
        if (rebuilt && s.synced != revision && s.input && pointer) if (const auto q = local(s, *pointer)) s.canvas->pointerMoved(*q, pressed, time);
        s.synced = revision;
    }
    void leave(Slot& s) {
        bind();
        if (captured == &s) { captured = nullptr; pressed = false; }
        s.canvas->prepareToLeave(time);
        s.canvas->setActive(false, time);
        s.active = false; s.input = false;
        // SystemHUDView.deactivateModuleInput: a module switch closes the
        // confirm bar and toast at once (spec §1.10), after the canvas left.
        context.overlay().reset();
    }
    void cancel() {
        if (fields) fields->pointerUp();
        if (!captured) return;
        auto* s = captured;
        captured = nullptr; pressed = false;
        bind();
        if (pointer) if (const auto q = local(*s, *pointer)) s->canvas->pointerUp(*q, time);
        s->canvas->pointerMoved(std::nullopt, false, time);
    }
    Slot* slot(core::Module m) const { for (const auto& s : slots) if (s->module == m) return s.get(); return nullptr; }
    // Clipboard's v1.2.0 policy (tools/clipboard_preview.hpp, removed in C1): a canvas that is not
    // shown when the HUD closes, or after the close finished, keeps no
    // artwork; the shown one keeps it so reopening on it only re-uploads; a
    // switch inside the open HUD only parks textures (collected). A park that
    // a still-published part refuses is retried by collected().
    void parkHidden() noexcept {
        for (auto& s : slots) {
            if (s->pose) continue;
            s->parkWanted = true;
            if (renderer && s->scene->park(*renderer)) s->parkWanted = false;
        }
    }
};

DesignOwnerCore::DesignOwnerCore(gpu::LayerRasterizer& r, d::Context& c, DesignOwnerOptions o) : impl_(std::make_unique<Impl>(r, c, std::move(o))) {}
DesignOwnerCore::~DesignOwnerCore() {
    auto& i = *impl_;
    if (i.context.editorHost() == i.fields.get()) i.context.setEditorHost(nullptr);
    i.automation.reset();
}
void DesignOwnerCore::add(std::unique_ptr<d::Canvas> canvas) {
    auto& i = *impl_;
    need(canvas && &canvas->context() == &i.context, "A design canvas belongs to this owner's context");
    need(!i.slot(canvas->module()), "One canvas per module");
    i.slots.push_back(std::make_unique<Impl::Slot>(std::move(canvas), i.raster, i.raster0(), i.options));
}
d::Canvas* DesignOwnerCore::canvas(core::Module m) const noexcept { const auto* s = impl_->slot(m); return s ? s->canvas.get() : nullptr; }
const DesignOwnerOptions& DesignOwnerCore::options() const noexcept { return impl_->options; }
void DesignOwnerCore::resize(const app::ClientMetrics& m) {
    need(m.pixelWidth && m.pixelHeight && std::isfinite(m.scale) && m.scale > 0, "Invalid design owner viewport");
    impl_->metrics = m;
}
// The Context took the appearance once (DesignOverlayOwner); the module may react.
void DesignOwnerCore::setAppearance(const app::ModuleAppearance& a, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (i.options.appearance) { i.bind(); i.options.appearance(a, i.time); }
}
void DesignOwnerCore::setOverlayVisible(bool value, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (i.overlay == value) return;
    i.overlay = value;
    if (value) { for (auto& s : i.slots) s->parkWanted = false; return; }
    i.cancel();
    i.pointer.reset();
    // The HUD closes: the shown canvas leaves (popups closed, editing
    // committed, gestures cancelled); the others keep no artwork.
    for (auto& s : i.slots) if (s->active) i.leave(*s);
    i.parkHidden();
}
void DesignOwnerCore::overlayClosing(double t) {
    cancelInteraction(t); setOverlayVisible(false, t);
    auto& i = *impl_;
    if (i.options.closing) { i.bind(); i.options.closing(i.time); }
}
void DesignOwnerCore::update(const app::ModuleFrame& frame) {
    auto& i = *impl_;
    i.clock(frame.time);
    i.focused = frame.focused;
    const auto& sample = frame.presentation;
    const auto camera = gpu::layerViewportProjection(i.metrics.pixelWidth, i.metrics.pixelHeight) * Matrix::scale(i.metrics.scale, i.metrics.scale);
    i.fieldSlot = nullptr;
    for (auto& slot : i.slots) {
        auto& s = *slot;
        const auto* shown = sample.current.module == s.module ? &sample.current : sample.incoming && sample.incoming->module == s.module ? &*sample.incoming : nullptr;
        const bool attached = shown && shown->attached;
        const bool active = i.overlay && attached && sample.requested == s.module;
        s.pose.reset();
        if (shown && !s.shown) { i.bind(); s.canvas->show(i.time); }
        s.shown = shown != nullptr;
        if (active != s.active) {
            if (active) { i.bind(); s.canvas->setActive(true, i.time); s.active = true; }
            else i.leave(s);
        }
        s.input = active && sample.acceptsModuleInput;
        if (!shown) { s.entrance.reset(); continue; }
        // HUDDirectionalContent.playEntrance(from:) after the swap.
        if (shown->entrance && s.entrance != shown->entrance->start) {
            s.entrance = shown->entrance->start;
            i.bind();
            s.canvas->playEntrance({shown->entrance->offset.x, shown->entrance->offset.y}, i.time);
        }
        i.sync(s);
        s.surface.update(frame.center, frame.chrome, *shown, frame.opacity);
        const auto& p = s.surface.pose();
        s.projection = core::Projection::viewport(camera * p.contentWorld, i.metrics.pixelWidth, i.metrics.pixelHeight);
        if (!attached) continue; // prepared, never drawn or hit
        s.scene->updatePose({p.contentWorld, p.opacity, i.time, p.hostMasks(), p.shutter});
        s.pose = p;
        s.parkWanted = false;
        if (s.active && i.fields) {
            i.fieldSlot = &s;
            i.fields->setPose({p.contentWorld, camera, i.metrics.pixelWidth, i.metrics.pixelHeight, p.opacity, frame.focused, i.time, i.context.env().reduceMotion});
        }
    }
    if (i.options.updated) { i.bind(); i.options.updated(frame); }
}
void DesignOwnerCore::prewarm(const app::ModuleFrame& frame) {
    auto& i = *impl_;
    i.clock(frame.time);
    // Mac prewarmDesktop: the first opening's page is built and rasterized
    // off the opening frame; nothing is activated, posed or presented.
    auto* s = i.slot(frame.presentation.requested);
    if (!s) return;
    s->parkWanted = false; // the first opening shows it
    i.bind();
    s->canvas->show(i.time);
    i.sync(*s);
}
bool DesignOwnerCore::requiresFrames(double t) const {
    const auto& i = *impl_;
    if (!i.overlay) return false;
    const auto now = std::max(t, i.time);
    for (const auto& s : i.slots) {
        if (!s->pose) continue;
        if (s->canvas->requiresFrames(now)) return true;
        // The scene's tree is the canvas's only while no change is pending.
        if (s->synced == s->canvas->contentRevision() && s->scene->requiresFrames(now)) return true;
    }
    return i.fields && i.fields->requiresFrames(now);
}
std::optional<double> DesignOwnerCore::nextWakeTime(double t) const {
    const auto& i = *impl_;
    std::optional<double> next;
    const auto take = [&](std::optional<double> v) { if (v && std::isfinite(*v)) next = next ? std::min(*next, *v) : *v; };
    if (i.options.nextWakeTime) take(i.options.nextWakeTime(t));   // the services run while the HUD is closed too
    if (!i.overlay) return next;
    for (const auto& s : i.slots) if (s->shown) { take(s->canvas->nextWakeTime(t)); take(s->canvas->nextDeadline(t)); }
    if (i.fields) take(i.fields->nextWakeTime(t));
    return next;
}
// A canvas wakes at the time it reported (its next wake as of the owner's
// last time, not re-asked at the new one: a once-a-second readout would
// already name the following second), with the canvas clock advanced first
// (Canvas::wake code reads now()).
bool DesignOwnerCore::deadline(double t) {
    auto& i = *impl_;
    const double before = i.time;
    i.clock(t);
    bool woke{};
    if (i.options.deadline) { i.bind(); woke = i.options.deadline(i.time); }
    if (!i.overlay) return woke;
    for (auto& s : i.slots)
        if (s->shown) if (const auto at = s->canvas->nextWakeTime(before); at && *at <= i.time) {
            i.bind();
            auto& env = i.context.env();
            env.now = std::max(env.now, i.time);
            s->canvas->wake(i.time);
            woke = true;
        }
    return woke;
}
void DesignOwnerCore::upload(gpu::Renderer& r) {
    auto& i = *impl_;
    i.renderer = &r;
    for (auto& s : i.slots) if (s->pose) s->scene->uploadAnimations(r);
    if (i.fields) i.fields->upload(r);
}
std::span<const gpu::LayerCompositionEntry> DesignOwnerCore::entries(core::Module m) {
    auto& i = *impl_;
    i.composed.clear();
    auto* s = i.slot(m);
    if (!s || !s->pose) return {};
    for (const auto& e : s->scene->entries()) i.composed.push_back(e);
    if (i.fields && i.fieldSlot == s) for (const auto& e : i.fields->entries()) i.composed.push_back(e);
    return i.composed;
}
void DesignOwnerCore::collected(gpu::Renderer& r) {
    auto& i = *impl_;
    i.renderer = &r;
    if (i.fields) need(i.fields->collectRetired(r), "Design field artwork still published");
    for (auto& s : i.slots) {
        need(s->scene->collectRetired(r), "Design artwork still published");
        if (s->pose) continue;
        // Not composed: a module switched away keeps its scenes (textures
        // parked); a module closed elsewhere keeps no artwork at all.
        if (s->parkWanted) { if (s->scene->park(r)) s->parkWanted = false; }
        else need(s->scene->releaseResources(r), "Design inactive artwork still published");
    }
}
void DesignOwnerCore::release(gpu::Renderer& r) {
    auto& i = *impl_;
    if (i.fields) need(i.fields->releaseResources(r), "Design field artwork still published at teardown");
    for (auto& s : i.slots) need(s->scene->releaseResources(r) && s->geometry.releaseResources(r), "Design artwork still published at teardown");
}
bool DesignOwnerCore::covers(core::Point p) const {
    const auto& i = *impl_;
    const auto* s = i.input();
    if (!s) return false;
    const auto q = i.local(*s, p);
    return q && (inside(s->canvas->hitBounds(), *q) || s->canvas->containsControl(*q));
}
bool DesignOwnerCore::pointerLocked() const {
    const auto& i = *impl_;
    if (i.captured) return true;
    for (const auto& s : i.slots) if (s->input && s->canvas->inputLocked()) return true;
    return false;
}
bool DesignOwnerCore::capturesPointer() const {
    const auto& i = *impl_;
    if (i.captured) return true;
    const auto* s = i.input();
    return s && (s->canvas->capturesPointer() || s->canvas->dragging());
}
bool DesignOwnerCore::capturesWheel() const { const auto* s = impl_->input(); return s && s->canvas->capturesPointer(); }
bool DesignOwnerCore::capturesKeys() const {
    const auto& i = *impl_;
    const auto* s = i.input();
    return s && (s->canvas->capturesPointer() || s->canvas->capturesKeys() || (i.fields && i.fields->editing()));
}
// Only a canvas that records keys itself (Settings' shortcut capture) keeps
// the host's key filters away; an editing field needs its TSF filter.
bool DesignOwnerCore::suppressesKeyFilters() const { const auto* s = impl_->input(); return s && s->canvas->capturesKeys(); }
bool DesignOwnerCore::pointer(const app::PointerEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.overlay) return false;
    i.pointer = core::Point{e.x, e.y};
    const core::Point physical{e.x * i.metrics.scale, e.y * i.metrics.scale};
    if (e.kind == app::PointerKind::leave || e.kind == app::PointerKind::captureLost) {
        i.cancel();
        for (auto& s : i.slots) if (s->input) { i.bind(); s->canvas->pointerMoved(std::nullopt, false, i.time); }
        if (e.kind == app::PointerKind::leave) i.pointer.reset();
        return false;
    }
    auto* s = i.captured ? i.captured : i.input();
    if (!s) return false;
    i.bind();
    const auto q = i.local(*s, *i.pointer);
    const bool left = e.button == app::PointerButton::left;
    if (e.kind == app::PointerKind::move) {
        if (i.fields) i.fields->pointerDrag(physical);
        if (i.captured && q) s->canvas->pointerDragged(*q, i.time);
        s->canvas->pointerMoved(q, i.pressed, i.time);
        return q && (inside(s->canvas->hitBounds(), *q) || i.captured);
    }
    if ((e.kind == app::PointerKind::down || e.kind == app::PointerKind::doubleClick) && left) {
        if (!q) return false;
        // The editing field's projected text takes its own presses (the Mac
        // text view lies above the canvas).
        if (i.fields && i.fields->pointerDown(physical, (e.modifiers & mkShift) != 0)) { i.captured = s; i.pressed = true; return true; }
        d::PointerInput input{*q, d::PointerButton::left, (e.modifiers & mkShift) != 0, (e.modifiers & mkControl) != 0, e.kind == app::PointerKind::doubleClick ? 2u : 1u};
        i.pressed = true;
        const bool used = s->canvas->pointerDown(*q, input, i.time);
        if (used) i.captured = s; else i.pressed = false;
        s->canvas->pointerMoved(q, i.pressed, i.time);
        return used;
    }
    if (e.kind == app::PointerKind::up && left) {
        const bool owned = i.captured != nullptr;
        if (i.fields) i.fields->pointerUp();
        i.captured = nullptr; i.pressed = false;
        if (owned) s->canvas->pointerUp(q.value_or(core::Point{-1e6, -1e6}), i.time);
        s->canvas->pointerMoved(q, false, i.time);
        return owned;
    }
    if (e.kind == app::PointerKind::down && e.button == app::PointerButton::right) {
        if (!q) return false;
        if (i.fields && i.fields->rightPointerDown(physical, *i.pointer)) return true;
        return s->canvas->rightPointerDown(*q, i.time);
    }
    return false;
}
bool DesignOwnerCore::wheel(const app::WheelEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.overlay) return false;
    auto* s = i.input();
    if (!s) return false;
    const core::Point logical{e.x, e.y};
    const auto q = i.local(*s, logical);
    if (!q || !std::isfinite(e.steps) || !e.linesPerStep) return false;
    i.bind();
    if (const auto m = app::pinchMagnification(e); m && s->canvas->magnify(*q, *m, i.time)) return true;
    // Lines x 12 px, a page 250 (Settings' wheel); + down.
    const double delta = -e.steps * (e.linesPerStep == UINT32_MAX ? 250 : 12. * e.linesPerStep);
    if (!e.horizontal && i.fields && i.fields->wheel({e.x * i.metrics.scale, e.y * i.metrics.scale}, delta)) return true;
    return s->canvas->scroll(*q, e.horizontal ? core::Point{delta, 0} : core::Point{0, delta}, i.time);
}
bool DesignOwnerCore::key(const app::KeyEvent& e, double t) {
    std::uint32_t modifiers{};
    if (GetKeyState(VK_MENU) < 0) modifiers |= 1;
    if (GetKeyState(VK_CONTROL) < 0) modifiers |= 2;
    if (GetKeyState(VK_SHIFT) < 0) modifiers |= 4;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) modifiers |= 8;
    return key(e, modifiers, t);
}
// Explicit modifiers: 1 Alt, 2 Ctrl, 4 Shift, 8 Win (SettingsPreview's bits).
bool DesignOwnerCore::key(const app::KeyEvent& e, std::uint32_t modifiers, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.overlay) return false;
    auto* s = i.input();
    if (!s) return false;
    i.bind();
    if (e.kind == app::KeyKind::character || e.kind == app::KeyKind::unicodeCharacter)
        return i.fields && i.fields->editing() && i.fields->character(e.value, e.kind == app::KeyKind::unicodeCharacter);
    if (e.kind != app::KeyKind::down) return false;
    d::KeyInput k;
    k.virtualKey = e.value;
    // charactersIgnoringModifiers: the key's unshifted character, lowercased.
    const auto mapped = MapVirtualKeyW(e.value, MAPVK_VK_TO_CHAR) & 0x7fffffffu;
    k.character = char32_t(mapped >= 'A' && mapped <= 'Z' ? mapped - 'A' + 'a' : mapped);
    k.alt = (modifiers & 1) != 0 || e.alt; k.control = (modifiers & 2) != 0; k.shift = (modifiers & 4) != 0; k.win = (modifiers & 8) != 0;
    k.extended = e.extended; k.repeat = e.previouslyDown;
    if (i.fields && i.fields->editing() && i.fields->key(k)) return true;
    return s->canvas->keyDown(k, i.time);
}
bool DesignOwnerCore::filterKey(const app::NativeMessage& m) {
    auto& i = *impl_;
    return i.overlay && i.fields && m.window == i.options.fields.window && i.input() && i.fields->filterKeyMessage(m.message, m.wParam, m.lParam);
}
bool DesignOwnerCore::message(const app::NativeMessage& m, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.fields || m.window != i.options.fields.window) return false;
    i.bind();
    return i.fields->message(m.message, m.wParam, m.lParam);
}
void DesignOwnerCore::focus(bool value, double t) { auto& i = *impl_; i.clock(t); if (i.fields) i.fields->setFocused(value); }
void DesignOwnerCore::cancelInteraction(double t) { auto& i = *impl_; i.clock(t); i.cancel(); }
bool DesignOwnerCore::finishEditing(double t) {
    auto& i = *impl_;
    i.clock(t);
    i.bind();
    for (auto& s : i.slots) if (s->canvas->editingText()) s->canvas->endEditing(true);
    return true;
}
bool DesignOwnerCore::flush(double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.options.flush) return true;
    i.bind();
    return i.options.flush(i.time);
}
bool DesignOwnerCore::hostEvent(const app::HostEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    // The close finished: canvases not shown keep no artwork (Clipboard).
    if (e.kind == app::HostEvent::Kind::overlayConcealed) i.parkHidden();
    if (!i.options.hostEvent) return false;
    i.bind();
    return i.options.hostEvent(e, i.time);
}
std::optional<app::ModuleCursor> DesignOwnerCore::cursorAt(core::Point p) const {
    const auto& i = *impl_;
    if (!i.overlay) return std::nullopt;
    if (i.fields && i.fields->textCursorAt(p)) return app::ModuleCursor::text;
    const auto* s = i.captured ? i.captured : i.input();
    if (!s) return std::nullopt;
    const auto q = i.local(*s, p);
    if (!q) return std::nullopt;
    switch (s->canvas->cursor(*q).value_or(d::Cursor::standard)) {
    case d::Cursor::text: return app::ModuleCursor::text;
    case d::Cursor::openHand: return app::ModuleCursor::openHand;
    case d::Cursor::closedHand: return app::ModuleCursor::closedHand;
    case d::Cursor::standard: break;
    }
    return std::nullopt;
}
bool DesignOwnerCore::automationRefresh(IRawElementProviderFragmentRoot* root) {
    auto& i = *impl_;
    if (!i.automation) {
        gpu::DesignAutomationHost host;
        host.root = root;
        host.items = [&i] {
            std::vector<d::AccessibilityItem> items;
            if (!i.overlay) return items;
            if (const auto* s = i.input()) {
                s->canvas->accessibilityItems(items);
                // The editing field's element stays (and holds the focus).
                if (i.fields && i.fieldSlot == s) if (auto edit = i.fields->accessibilityItem()) items.push_back(std::move(*edit));
            }
            return items;
        };
        host.toScreen = [&i](core::Rect box) -> std::optional<core::Rect> {
            const auto* s = i.input();
            if (!s) return std::nullopt;
            return gpu::screenReaderClientToScreen(i.options.fields.window, i.metrics.scale, projected(s->projection, i.metrics.scale, box));
        };
        const auto now = [&i] { return i.options.now ? std::max(i.options.now(), i.time) : i.time; };
        host.invoke = [&i, now](std::string_view id) { auto* s = i.input(); if (!s) return false; i.clock(now()); i.bind(); return s->canvas->performAccessibilityAction(id, i.time); };
        host.focus = host.invoke; // an edit's press begins editing
        host.setValue = [&i, now](std::string_view id, double v) { auto* s = i.input(); if (!s) return false; i.clock(now()); i.bind(); return s->canvas->adjustAccessibilitySlider(id, v, i.time); };
        host.setText = [&i, now](std::string_view id, std::string_view text) { auto* s = i.input(); if (!s) return false; i.clock(now()); i.bind(); return s->canvas->setAccessibilityText(id, text, i.time); };
        host.expand = [&i, now](std::string_view id, bool expand) {
            auto* s = i.input();
            if (!s) return false;
            std::vector<d::AccessibilityItem> items;
            s->canvas->accessibilityItems(items);
            const auto it = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == id; });
            if (it == items.end()) return false;
            if (it->on == expand) return true;
            i.clock(now()); i.bind();
            return s->canvas->performAccessibilityAction(id, i.time);
        };
        host.changed = [&i] { if (i.options.changed) i.options.changed(); };
        i.automation = std::make_unique<gpu::NativeDesignAutomation>(std::move(host));
    }
    return i.automation->refresh(root);
}
std::size_t DesignOwnerCore::automationCount() const { return impl_->automation ? impl_->automation->size() : 0; }
long DesignOwnerCore::automationChild(std::size_t n, IRawElementProviderFragment** out) const {
    if (!impl_->automation) { if (out) *out = nullptr; return 0x80070057L; } // E_INVALIDARG
    return impl_->automation->child(n, out);
}
long DesignOwnerCore::automationFromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!impl_->automation) { if (out) *out = nullptr; return 0; }
    return impl_->automation->fromPoint(x, y, out);
}
void DesignOwnerCore::automationDisconnect() noexcept { if (impl_->automation) impl_->automation->disconnect(); }
// The UIA group is named after the canvas that takes input (Settings presents four modules).
std::optional<core::Module> DesignOwnerCore::automationModule() const noexcept { return inputModule(); }
app::ModuleResidency DesignOwnerCore::residency() const { return {}; }
std::optional<core::Module> DesignOwnerCore::inputModule() const noexcept { const auto* s = impl_->input(); return s ? std::optional(s->module) : std::nullopt; }
std::optional<core::Rect> DesignOwnerCore::clientRect(core::Rect box) const {
    const auto& i = *impl_;
    const auto* s = i.input();
    return s ? projected(s->projection, i.metrics.scale, box) : std::nullopt;
}
std::optional<gpu::NativeDesignSceneStats> DesignOwnerCore::sceneStats(core::Module module) const {
    const auto* s = impl_->slot(module);
    return s ? std::optional(s->scene->stats()) : std::nullopt;
}
std::optional<core::Point> DesignOwnerCore::boxPoint(core::Point logical) const {
    const auto* s = impl_->input();
    return s ? impl_->local(*s, logical) : std::nullopt;
}

DesignModuleOwner::DesignModuleOwner(std::unique_ptr<DesignOwnerCore> core) : core_(std::move(core)) { need(core_ != nullptr, "Design module owner needs its core"); }
DesignModuleOwner::~DesignModuleOwner() = default;
std::string_view DesignModuleOwner::name() const noexcept { return core_->options().name; }
app::ModuleRouting DesignModuleOwner::routing() const noexcept { return core_->options().routing; }
bool DesignModuleOwner::presents(core::Module m) const noexcept { return core_->canvas(m) != nullptr; }
void DesignModuleOwner::resize(const app::ClientMetrics& m) { core_->resize(m); }
void DesignModuleOwner::setAppearance(const app::ModuleAppearance& a, double t) { core_->setAppearance(a, t); }
void DesignModuleOwner::setOverlayVisible(bool v, double t) { core_->setOverlayVisible(v, t); }
void DesignModuleOwner::overlayClosing(double t) { core_->overlayClosing(t); }
void DesignModuleOwner::update(const app::ModuleFrame& f) { core_->update(f); }
void DesignModuleOwner::prewarm(const app::ModuleFrame& f) { core_->prewarm(f); }
bool DesignModuleOwner::requiresFrames(double t) const { return core_->requiresFrames(t); }
std::optional<double> DesignModuleOwner::nextWakeTime(double t) const { return core_->nextWakeTime(t); }
bool DesignModuleOwner::deadline(double t) { return core_->deadline(t); }
void DesignModuleOwner::upload(gpu::Renderer& r) { core_->upload(r); }
std::span<const gpu::LayerCompositionEntry> DesignModuleOwner::entries(core::Module m) { return core_->entries(m); }
void DesignModuleOwner::collected(gpu::Renderer& r) { core_->collected(r); }
void DesignModuleOwner::release(gpu::Renderer& r) { core_->release(r); }
bool DesignModuleOwner::covers(core::Point p) const { return core_->covers(p); }
bool DesignModuleOwner::pointerLocked() const { return core_->pointerLocked(); }
bool DesignModuleOwner::capturesPointer() const { return core_->capturesPointer(); }
bool DesignModuleOwner::capturesWheel() const { return core_->capturesWheel(); }
bool DesignModuleOwner::capturesKeys() const { return core_->capturesKeys(); }
bool DesignModuleOwner::suppressesKeyFilters() const { return core_->suppressesKeyFilters(); }
bool DesignModuleOwner::pointer(const app::PointerEvent& e, double t) { return core_->pointer(e, t); }
bool DesignModuleOwner::wheel(const app::WheelEvent& e, double t) { return core_->wheel(e, t); }
bool DesignModuleOwner::key(const app::KeyEvent& e, double t) { return core_->key(e, t); }
bool DesignModuleOwner::filterKey(const app::NativeMessage& m) { return core_->filterKey(m); }
bool DesignModuleOwner::message(const app::NativeMessage& m, double t) { return core_->message(m, t); }
void DesignModuleOwner::focus(bool v, double t) { core_->focus(v, t); }
void DesignModuleOwner::cancelInteraction(double t) { core_->cancelInteraction(t); }
bool DesignModuleOwner::finishEditing(double t) { return core_->finishEditing(t); }
bool DesignModuleOwner::hostEvent(const app::HostEvent& e, double t) { return core_->hostEvent(e, t); }
bool DesignModuleOwner::flush(double t) { return core_->flush(t); }
std::optional<app::ModuleCursor> DesignModuleOwner::cursorAt(core::Point p) const { return core_->cursorAt(p); }
bool DesignModuleOwner::automationRefresh(IRawElementProviderFragmentRoot* r) { return core_->automationRefresh(r); }
std::size_t DesignModuleOwner::automationCount() const { return core_->automationCount(); }
long DesignModuleOwner::automationChild(std::size_t n, IRawElementProviderFragment** out) const { return core_->automationChild(n, out); }
long DesignModuleOwner::automationFromPoint(double x, double y, IRawElementProviderFragment** out) const { return core_->automationFromPoint(x, y, out); }
void DesignModuleOwner::automationDisconnect() noexcept { core_->automationDisconnect(); }
std::optional<core::Module> DesignModuleOwner::automationModule() const noexcept { return core_->automationModule(); }
app::ModuleResidency DesignModuleOwner::residency() const { return core_->residency(); }
} // namespace endfield::tools
#endif
