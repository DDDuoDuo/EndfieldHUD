#include "app/hud_elements_editor.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "core/source_desktop_chrome.hpp"
#include "modules/hud_design_cards.hpp"
#include "native/hud_design_automation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::app {
namespace {
namespace d = modules::design;
namespace gpu = native;
using Matrix = core::Matrix4;
using G = core::OverlayGeometry;
using Layout = ehud::data::OverlayLayout;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
constexpr core::Point offStrip{-10000, -10000};
// HUDOverlayLayout.Element.sizeTitle: the strip's Size slider of an element.
std::string sizeTitle(G::Element element, core::Language language) {
    switch (element) {
    case G::Element::title: return core::localized("Title size", "标题大小", language);
    case G::Element::sanity: return core::localized("Sanity size", "理智大小", language);
    case G::Element::alerts: return core::localized("Alerts size", "警报大小", language);
    }
    return {};
}
std::string_view elementID(G::Element element) {
    switch (element) {
    case G::Element::title: return "title";
    case G::Element::sanity: return "sanity";
    case G::Element::alerts: return "alerts";
    }
    return "title";
}
std::optional<G::Element> elementNamed(std::string_view id) {
    for (const auto e : G::elements) if (elementID(e) == id) return e;
    return std::nullopt;
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
core::Rect unite(core::Rect a, core::Rect b) {
    const double x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(a.x + a.width, b.x + b.width) - x, std::max(a.y + a.height, b.y + b.height) - y};
}
}

struct HudElementsEditor::Impl {
    gpu::LayerRasterizer& raster;
    d::Context& context;
    HudElementsEditorOptions options;
    // The strip (installOverlayEditor); a replaced one waits in `retired`
    // until the strip scene holds none of its layers.
    std::unique_ptr<d::PlacementEditor> editor;
    std::vector<std::unique_ptr<d::PlacementEditor>> retired;
    gpu::NativeDesignScene strip;
    const d::Layer* stripTree{};
    // The corner marks (overlayFrames): one layer per element in core-plane
    // points, each its own scene posed at its grown frame, so a drag moves
    // them without drawing them again.
    d::LayerArena marks{"hud.elements.frames"};
    std::array<d::Layer*, 3> markLayers{};
    std::array<std::unique_ptr<gpu::NativeDesignScene>, 3> markScenes;
    std::array<std::uint64_t, 3> markRevisions{};
    std::array<d::Size, 3> markSizes{};
    std::array<core::Rect, 3> grownFrames{};
    std::optional<Layout> draft;
    std::optional<G::Element> dragged, focus;
    core::Point stripOrigin;              // the strip's top-left in content-box px
    ClientMetrics metrics;
    core::Matrix4 center;
    core::Projection coreProjection, boxProjection;
    std::optional<bool> dark;
    std::vector<gpu::LayerCompositionEntry> composed;
    std::unique_ptr<gpu::NativeDesignAutomation> automation;
    double time{};
    bool visible{}, posed{}, pressed{}, uploadedMarks{};
    // From the HUD close's start until the hidden HUD (overlayConcealed,
    // overlayRetracted) or a reopen: the strip, the marks and the draft stay
    // drawn where they are and fade with the close; nothing takes input.
    bool closing{};
    Impl(gpu::LayerRasterizer& r, d::Context& c, HudElementsEditorOptions o)
        : raster(r), context(c), options(std::move(o)), strip(r, options.raster, "hud.elements.editor") {
        for (const auto e : G::elements) {
            const auto n = G::index(e);
            markLayers[n] = &marks.make(elementID(e));
            markScenes[n] = std::make_unique<gpu::NativeDesignScene>(r, options.raster, "hud.elements.frames." + std::string(elementID(e)));
        }
        composed.reserve(64);
    }
    void clock(double t) {
        need(std::isfinite(t), "HUD elements editor needs finite time");
        time = std::max(time, t);
        context.env().now = std::max(context.env().now, time);
    }
    void changed() const { if (options.changed) options.changed(); }
    core::OverlaySize natural(G::Element e, const Layout& layout) const {
        if (options.natural) return options.natural(e, layout);
        return e == G::Element::sanity ? G::gauge : core::OverlaySize{};
    }
    // overlayPlacement / overlayFrame: an element as drawn (kept on the plane).
    Layout::Placement placement(G::Element e, const Layout& layout, const Layout::Placement& raw) const { return G::clamped(e, raw, natural(e, layout)); }
    core::Rect frame(G::Element e, const Layout& layout) const { return G::drawnFrame(e, layout[e], natural(e, layout)); }
    std::array<core::Rect, 3> frames(const Layout& layout) const {
        std::array<core::Rect, 3> out{};
        for (const auto e : G::elements) out[G::index(e)] = frame(e, layout);
        return out;
    }
    // installOverlayEditor: the strip for `layout`; the theme rebuild passes
    // entrance false (its pills, band and sliders bake their palette).
    void install(const Layout& layout, bool entrance) {
        const d::Slider::Range range{Layout::minimumSize * 100, Layout::maximumSize * 100, 1, {}};
        std::vector<d::PlacementEditor::SliderSpec> specs;
        for (const auto e : G::elements) specs.push_back({std::string(elementID(e)), sizeTitle(e, context.env().language), layout[e].size * 100, range, d::ValueFormat::percent});
        if (editor) { editor->cancelPointer(); retired.push_back(std::move(editor)); }
        editor = std::make_unique<d::PlacementEditor>(context.env(), "hudElements", std::move(specs));
        editor->onChange = [this](const std::string& id, double value, bool) { sizeChanged(id, value); };
        editor->onDecision = [this](d::ConfirmState::Decision decision) { finish(decision == d::ConfirmState::Decision::confirm); };
        dragged.reset();
        // The marks take the accent ink of this palette.
        markSizes = {};
        if (entrance) editor->show(context.env().clock());
    }
    void sizeChanged(const std::string& id, double value) {
        const auto element = elementNamed(id);
        if (!draft || !element) return;
        (*draft)[*element].size = value / 100;
        layoutChanged();
    }
    // finishOverlayPlacement: Confirm saves the placements as drawn.
    void finish(bool confirm) {
        if (!editor || !draft) return;
        auto value = *draft;
        if (confirm) for (const auto e : G::elements) value[e] = placement(e, *draft, (*draft)[e]);
        editor->cancelPointer();
        retired.push_back(std::move(editor));
        draft.reset(); dragged.reset(); focus.reset(); pressed = false;
        if (confirm && options.save) options.save(value);
        if (options.layoutChanged) options.layoutChanged();
        changed();
    }
    void layoutChanged() {
        layout();
        if (options.layoutChanged) options.layoutChanged();
        changed();
    }
    // layoutOverlayEditor: the marks around every element, and the strip
    // beside the last one dragged (still while a slider or an element has
    // the pointer).
    void layout() {
        if (!editor || !draft) return;
        const auto drawn = frames(*draft);
        for (const auto e : G::elements) {
            const auto n = G::index(e);
            grownFrames[n] = G::grown(drawn[n], G::hitSlop);
            const d::Size size{grownFrames[n].width, grownFrames[n].height};
            if (markSizes[n] == size) continue;
            markSizes[n] = size;
            auto& l = *markLayers[n];
            l.setBounds({size.width, size.height});
            d::setPath(l, d::ArchiveTile::bracketPath({0, 0, size.width, size.height}, 0), context.env().palette.accentInk());
            ++markRevisions[n];
        }
        if (editor->adjusting() || editor->dragging()) return;
        const auto box = d::geometry::boxRect();
        const double unit = d::geometry::baseUnit;
        const auto local = [&](core::Rect r) { return d::Rect{(r.x - box.x) / unit, (r.y - box.y) / unit, r.width / unit, r.height / unit}; };
        auto all = grownFrames[0];
        for (std::size_t n = 1; n < grownFrames.size(); ++n) all = unite(all, grownFrames[n]);
        // Beside the last element dragged; at first beside all three while
        // they sit together, else beside the title.
        const auto at = focus ? std::optional(*focus) : all.height > G::plane.height / 3 ? std::optional(G::Element::title) : std::nullopt;
        const auto target = at ? grownFrames[G::index(*at)] : all;
        const auto size = editor->size();
        const auto placed = d::PlacementEditor::frame({size.width, size.height + d::PlacementEditor::bubbleClearance}, local(target), local(G::plane), 8);
        stripOrigin = {placed.x, placed.y + d::PlacementEditor::bubbleClearance};
    }
    std::optional<core::Point> corePoint(core::Point client) const {
        if (!posed) return std::nullopt;
        return coreProjection.unproject({client.x * metrics.scale, client.y * metrics.scale});
    }
    // A logical client point on the strip's own layer.
    std::optional<d::Point> stripPoint(core::Point client) const {
        if (!posed) return std::nullopt;
        const auto box = boxProjection.unproject({client.x * metrics.scale, client.y * metrics.scale});
        if (!box) return std::nullopt;
        return d::Point{box->x - stripOrigin.x, box->y - stripOrigin.y};
    }
    std::optional<G::Element> elementAt(core::Point corePlane) const {
        if (!draft) return std::nullopt;
        return G::elementAt(frames(*draft), corePlane);
    }
    // overlayPointerDown: a press on the strip goes to its controls; on an
    // element it starts a drag from where the element is drawn.
    void down(core::Point client) {
        if (!editor || !draft) return;
        const auto local = stripPoint(client).value_or(d::Point{offStrip.x, offStrip.y});
        const auto plane = corePoint(client);
        if (editor->stripContains(local, editor->layer())) {
            const auto p = plane.value_or(offStrip);
            editor->pointerDown(local, editor->layer(), {p.x, p.y}, {0, 0});
        } else if (plane) if (const auto element = elementAt(*plane)) {
            dragged = element; focus = element;
            const auto start = placement(*element, *draft, (*draft)[*element]);
            editor->pointerDown(local, editor->layer(), {plane->x, plane->y}, {start.x, start.y});
        }
        pressed = true;
    }
    void drag(core::Point client) {
        if (!editor) return;
        const auto local = stripPoint(client).value_or(d::Point{offStrip.x, offStrip.y});
        const auto plane = corePoint(client);
        const auto pointer = plane.value_or(core::Point{});
        const auto proposed = editor->pointerDragged(local, editor->layer(), {pointer.x, pointer.y});
        if (!proposed || !plane || !dragged || !draft) return;
        auto moved = (*draft)[*dragged];
        moved.x = proposed->x; moved.y = proposed->y;
        (*draft)[*dragged] = placement(*dragged, *draft, moved);
        layoutChanged();
    }
    void up(core::Point client) {
        if (!editor) return;
        editor->pointerUp(stripPoint(client).value_or(d::Point{offStrip.x, offStrip.y}), editor->layer());
        dragged.reset(); pressed = false;
        // The strip settles beside the element again.
        layout();
        changed();
    }
    void collectEditors() {
        if (retired.empty()) return;
        if (stripTree && (!editor || stripTree != &editor->layer())) return;
        retired.clear();
    }
};

HudElementsEditor::HudElementsEditor(gpu::LayerRasterizer& r, d::Context& c, HudElementsEditorOptions o)
    : impl_(std::make_unique<Impl>(r, c, std::move(o))) {}
HudElementsEditor::~HudElementsEditor() { impl_->automation.reset(); }
bool HudElementsEditor::begin(const Layout& saved, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (i.draft || !i.visible) return false;
    i.draft = saved; i.focus.reset(); i.dragged.reset(); i.pressed = false;
    i.install(saved, true);
    i.layoutChanged();
    return true;
}
void HudElementsEditor::cancel(double t) { impl_->clock(t); impl_->finish(false); }
bool HudElementsEditor::placing() const noexcept { return impl_->draft.has_value(); }
const Layout* HudElementsEditor::draft() const noexcept { return impl_->draft ? &*impl_->draft : nullptr; }
const d::PlacementEditor* HudElementsEditor::strip() const noexcept { return impl_->editor.get(); }
std::array<core::Rect, 3> HudElementsEditor::frames() const { const auto& i = *impl_; return i.draft ? i.frames(*i.draft) : std::array<core::Rect, 3>{}; }
std::optional<core::Point> HudElementsEditor::clientPoint(core::Point p) const {
    const auto& i = *impl_;
    if (!i.posed) return std::nullopt;
    const auto q = i.coreProjection.project(p);
    if (!q) return std::nullopt;
    return core::Point{q->x / i.metrics.scale, q->y / i.metrics.scale};
}
std::optional<core::Point> HudElementsEditor::stripClientPoint(core::Point p) const {
    const auto& i = *impl_;
    if (!i.posed) return std::nullopt;
    const auto q = i.boxProjection.project({p.x + i.stripOrigin.x, p.y + i.stripOrigin.y});
    if (!q) return std::nullopt;
    return core::Point{q->x / i.metrics.scale, q->y / i.metrics.scale};
}
std::optional<HudElementsEditor::Element> HudElementsEditor::dragged() const noexcept { return impl_->dragged; }
// Placing takes every press, wheel and key ahead of every module owner
// (SystemHUDView.mouseDown / keyDown ask the editor first, after the boot log).
ModuleRouting HudElementsEditor::routing() const noexcept {
    ModuleRouting r;
    r.pointerCapture = 0; r.wheelCapture = 0; r.keyCapture = 0;
    return r;
}
void HudElementsEditor::resize(const ClientMetrics& m) {
    need(m.pixelWidth && m.pixelHeight && std::isfinite(m.scale) && m.scale > 0, "Invalid HUD elements editor viewport");
    impl_->metrics = m;
}
// viewDidChangeEffectiveAppearance: a theme flip while placing rebuilds the
// strip from the draft with no entrance (the shared Context already took the
// palette: the design overlay is registered first).
void HudElementsEditor::setAppearance(const ModuleAppearance& a, double t) {
    auto& i = *impl_;
    i.clock(t);
    const bool flipped = i.dark && *i.dark != a.dark;
    i.dark = a.dark;
    if (!flipped || !i.draft) return;
    i.install(*i.draft, false);
    i.layout();
    i.changed();
}
// A hidden or reopened HUD (deactivateModuleInput): placing is cancelled.
void HudElementsEditor::setOverlayVisible(bool v, double t) {
    auto& i = *impl_;
    i.clock(t);
    i.visible = v;
    i.closing = false;
    i.finish(false);
}
// The HUD close (48584b5, SystemHUDView.deactivateModuleInput(forRetraction:)):
// the editor's press ends at once and nothing takes input; the strip, the
// marks and the draft stay drawn to fade with the close, and the closed HUD
// drops the draft once hidden (hostEvent).
void HudElementsEditor::overlayClosing(double t) {
    auto& i = *impl_;
    i.clock(t);
    i.visible = false;
    if (!i.draft || !i.editor) { i.finish(false); return; }
    i.closing = true;
    i.editor->cancelPointer(); i.dragged.reset(); i.pressed = false;
    i.changed();
}
bool HudElementsEditor::hostEvent(const HostEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (e.kind != HostEvent::Kind::overlayConcealed && e.kind != HostEvent::Kind::overlayRetracted) return false;
    const bool placed = placing();
    i.closing = false;
    i.finish(false);
    return placed;
}
void HudElementsEditor::update(const ModuleFrame& frame) {
    auto& i = *impl_;
    i.clock(frame.time);
    i.center = frame.center;
    if (!i.metrics.pixelWidth || !i.metrics.pixelHeight) return;
    const auto camera = gpu::layerViewportProjection(i.metrics.pixelWidth, i.metrics.pixelHeight) * Matrix::scale(i.metrics.scale, i.metrics.scale);
    const auto boxWorld = core::source::designBoxWorld(frame.center);
    i.coreProjection = core::Projection::viewport(camera * frame.center, i.metrics.pixelWidth, i.metrics.pixelHeight);
    i.boxProjection = core::Projection::viewport(camera * boxWorld, i.metrics.pixelWidth, i.metrics.pixelHeight);
    i.posed = true;
    if (!i.editor) { i.collectEditors(); return; }
    // The marks (and a strip beside them) follow the elements' natural sizes:
    // an alert arriving or clearing while placing (facilityChanged). The
    // close keeps them where they are drawn.
    if (!i.closing) i.layout();
    // The strip on the content box at its origin (overlayEditorHost in box
    // coordinates), the marks on the core plane; the top layer fades with the
    // HUD close as every native layer does (5b2a9d3: topLayout).
    const double density = d::layerContentsScale(i.context.boxScale());
    i.strip.syncContent(i.editor->layer(), i.context.env().text, i.editor->contentRevision(), density, i.time);
    i.stripTree = &i.editor->layer();
    i.collectEditors();
    i.strip.updatePose({boxWorld * Matrix::translation(i.stripOrigin.x, i.stripOrigin.y), frame.opacity, i.time, {}, {}});
    const double markDensity = d::layerContentsScale(i.context.boxScale() / d::geometry::baseUnit);
    for (std::size_t n = 0; n < i.markScenes.size(); ++n) {
        const auto& r = i.grownFrames[n];
        if (!(r.width > 0) || !(r.height > 0)) continue;
        i.markScenes[n]->syncContent(*i.markLayers[n], i.context.env().text, i.markRevisions[n], markDensity, i.time);
        i.markScenes[n]->updatePose({frame.center * Matrix::translation(r.x, r.y), frame.opacity, i.time, {}, {}});
    }
}
bool HudElementsEditor::requiresFrames(double t) const {
    const auto& i = *impl_;
    return i.editor && i.stripTree == &i.editor->layer() && i.strip.requiresFrames(std::max(t, i.time));
}
void HudElementsEditor::upload(gpu::Renderer& r) {
    auto& i = *impl_;
    if (!i.editor || !i.posed) return;
    i.strip.uploadAnimations(r);
    for (auto& scene : i.markScenes) scene->uploadAnimations(r);
}
std::span<const gpu::LayerCompositionEntry> HudElementsEditor::topEntries() {
    auto& i = *impl_;
    i.composed.clear();
    if (!i.editor || !i.posed || i.stripTree != &i.editor->layer()) return {};
    // The marks first, then the strip (a snapshot draws siblings in order).
    for (std::size_t n = 0; n < i.markScenes.size(); ++n) {
        if (!(i.grownFrames[n].width > 0)) continue;
        for (const auto& e : i.markScenes[n]->entries()) i.composed.push_back(e);
    }
    for (const auto& e : i.strip.entries()) i.composed.push_back(e);
    return i.composed;
}
void HudElementsEditor::collected(gpu::Renderer& r) {
    auto& i = *impl_;
    need(i.strip.collectRetired(r), "HUD elements strip still published");
    for (auto& scene : i.markScenes) need(scene->collectRetired(r), "HUD elements marks still published");
    // The strip and the marks exist only while placing.
    if (!i.editor) {
        if (i.strip.park(r)) i.stripTree = nullptr;
        for (auto& scene : i.markScenes) (void)scene->park(r);
        i.collectEditors();
    }
}
void HudElementsEditor::release(gpu::Renderer& r) {
    auto& i = *impl_;
    need(i.strip.releaseResources(r), "HUD elements strip still published at teardown");
    for (auto& scene : i.markScenes) need(scene->releaseResources(r), "HUD elements marks still published at teardown");
}
// Placing takes input until the HUD close starts; the parallax stays frozen
// while the strip is drawn.
bool HudElementsEditor::covers(core::Point) const { return placing() && !impl_->closing; }
bool HudElementsEditor::pointerLocked() const { return placing(); } // freezes the parallax (lockParallaxForActiveInput)
bool HudElementsEditor::capturesPointer() const { return placing() && !impl_->closing; }
bool HudElementsEditor::capturesWheel() const { return placing() && !impl_->closing; }
bool HudElementsEditor::capturesKeys() const { return placing() && !impl_->closing; }
bool HudElementsEditor::suppressesKeyFilters() const { return placing() && !impl_->closing; }
bool HudElementsEditor::pointer(const PointerEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.draft || !i.editor) return false;
    // The close: no press, drag or release, and a move changes nothing; the
    // host's leave (the pointer leaving the HUD, or the hide) clears the
    // highlights (clearControlHighlights).
    if (i.closing) {
        if (e.kind == PointerKind::leave) { i.editor->updateHighlights(i.editor->layer(), std::nullopt); i.changed(); }
        return false;
    }
    const core::Point p{e.x, e.y};
    const bool left = e.button == PointerButton::left;
    switch (e.kind) {
    case PointerKind::down: case PointerKind::doubleClick:
        if (left) i.down(p);
        break;
    case PointerKind::move:
        if (i.pressed) i.drag(p);
        if (i.editor) i.editor->updateHighlights(i.editor->layer(), i.stripPoint(p), i.pressed);
        break;
    case PointerKind::up:
        if (left && i.pressed) i.up(p);
        break;
    case PointerKind::leave: case PointerKind::captureLost:
        if (i.pressed) i.up({offStrip.x, offStrip.y});
        if (i.editor) i.editor->updateHighlights(i.editor->layer(), std::nullopt);
        break;
    }
    i.changed();
    return true;
}
bool HudElementsEditor::wheel(const WheelEvent&, double t) { impl_->clock(t); return capturesWheel(); }
// keyDown: Esc cancels and Return confirms (unmodified); every key is the
// editor's while placing (the host checks its summon shortcut first).
bool HudElementsEditor::key(const KeyEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.draft || !i.editor || i.closing) return false;
    if (e.kind != KeyKind::down) return true;
    const auto held = [](int key) { return (GetKeyState(key) & 0x8000) != 0; };
    if (e.alt || held(VK_CONTROL) || held(VK_SHIFT) || held(VK_LWIN) || held(VK_RWIN)) return true;
    d::KeyInput k;
    k.virtualKey = e.value; k.extended = e.extended; k.repeat = e.previouslyDown;
    (void)i.editor->key(k);
    i.changed();
    return true;
}
void HudElementsEditor::cancelInteraction(double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.editor || !i.pressed) return;
    i.editor->cancelPointer(); i.dragged.reset(); i.pressed = false;
    i.layout();
}
// SystemHUDView.designCursor while placing: the closed hand while an element
// is dragged, the normal cursor over the strip, the open hand over an
// element's hit frame (shown or hidden), the normal cursor elsewhere.
std::optional<ModuleCursor> HudElementsEditor::cursorAt(core::Point p) const {
    const auto& i = *impl_;
    if (!i.draft || !i.editor || i.closing) return std::nullopt;
    if (i.editor->dragging()) return ModuleCursor::closedHand;
    if (const auto local = i.stripPoint(p); local && i.editor->stripContains(*local, i.editor->layer())) return ModuleCursor::source;
    if (const auto plane = i.corePoint(p); plane && i.elementAt(*plane)) return ModuleCursor::openHand;
    return ModuleCursor::source;
}
bool HudElementsEditor::automationRefresh(IRawElementProviderFragmentRoot* root) {
    auto& i = *impl_;
    if (!i.automation) {
        gpu::DesignAutomationHost host;
        host.root = root;
        // The screen-reader stand-ins for the strip (overlayEditorStandIns).
        host.items = [&i] {
            std::vector<d::AccessibilityItem> items;
            if (i.draft && i.editor && !i.closing) i.editor->accessibilityItems(i.editor->layer(), items);
            return items;
        };
        host.toScreen = [&i](core::Rect r) -> std::optional<core::Rect> {
            if (!i.posed) return std::nullopt;
            return gpu::screenReaderClientToScreen(i.options.window, i.metrics.scale,
                projected(i.boxProjection, i.metrics.scale, {r.x + i.stripOrigin.x, r.y + i.stripOrigin.y, r.width, r.height}));
        };
        host.invoke = [&i](std::string_view id) {
            if (!i.draft || !i.editor || i.closing) return false;
            i.clock(i.options.now ? i.options.now() : i.time);
            i.editor->perform(id);
            return true;
        };
        host.setValue = [&i](std::string_view id, double value) {
            if (!i.draft || !i.editor || i.closing) return false;
            i.clock(i.options.now ? i.options.now() : i.time);
            i.editor->adjust(id, value);
            return true;
        };
        host.changed = [&i] { i.changed(); };
        i.automation = std::make_unique<gpu::NativeDesignAutomation>(std::move(host));
    }
    return i.automation->refresh(root);
}
std::size_t HudElementsEditor::automationCount() const { return impl_->automation ? impl_->automation->size() : 0; }
long HudElementsEditor::automationChild(std::size_t n, IRawElementProviderFragment** out) const {
    if (!impl_->automation) { if (out) *out = nullptr; return 0x80070057L; } // E_INVALIDARG
    return impl_->automation->child(n, out);
}
long HudElementsEditor::automationFromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!impl_->automation) { if (out) *out = nullptr; return 0; }
    return impl_->automation->fromPoint(x, y, out);
}
void HudElementsEditor::automationDisconnect() noexcept { if (impl_->automation) impl_->automation->disconnect(); }
} // namespace endfield::app
#endif
