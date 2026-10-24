// WorldMapCanvas (Sources/WorldMapCanvas.swift) over the WorldMapSurface
// model (modules/map_state.hpp), statement by statement (line numbers cite
// WorldMapCanvas.swift).
#include "modules/map_canvas.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_format.hpp"
#include <cmath>
#include <cstdio>

namespace endfield::modules {
namespace d = design;
namespace {
using G = MapSurfaceGeometry;
// CGRect.contains: half-open on the maximum edges.
bool inside(d::Rect r, d::Point p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
std::string formatted(const char* format, double value) { char buffer[64]; std::snprintf(buffer, sizeof buffer, format, value); return buffer; }
// Swift String.count of the readout ("3.0×"): its characters, not its UTF-8 bytes.
std::size_t characters(std::string_view s) { std::size_t n{}; for (const unsigned char c : s) n += (c & 0xC0) != 0x80; return n; }
d::TextStyle readoutStyle(const d::Palette& p) { d::TextStyle s; s.size = 11; s.weight = d::FontWeight::bold; s.color = p.yellow(); s.tabular = true; return s; }
d::TextStyle coordinateStyle() { d::TextStyle s; s.size = 11; s.weight = d::FontWeight::bold; s.color = d::fixed::onDarkTag; s.lineHeight = 1.5; s.tabular = true; return s; }
// The readout sits just under the header that replaced v1.2.0's title (:184).
constexpr double readoutTop = d::metric::headerHeight + 10;
}

MapCanvas::MapCanvas(d::Context& context, MapState& state, std::optional<std::string> storeError)
    : Canvas(core::Module::map, context), state_(&state), message_(std::move(storeError)) {}
MapCanvas::~MapCanvas() = default;

// v1.2.0's map viewport (its 440 units) in content-box px (:41-51).
d::Point MapCanvas::v1(double x, double y) noexcept {
    const double minX = 295 - 220 * G::unitScale, minY = 305 - 23 / d::geometry::baseUnit - 220 * G::unitScale;
    return {minX + x * G::unitScale, minY + y * G::unitScale};
}
bool MapCanvas::onMap(d::Point point) noexcept { return G::contains(local(point)); }
// On the map and not on a control or plate drawn over it (:95-97).
bool MapCanvas::onTerrain(d::Point point) const noexcept {
    if (!onMap(point)) return false;
    for (const auto* layer : chrome_) if (!layer->hidden() && inside(layer->frame(), point)) return false;
    return true;
}
// The input circle overflows the content box on every side (:87).
d::Rect MapCanvas::hitBounds() const {
    const auto box = Canvas::hitBounds();
    const double x0 = std::min(box.x, circleFrame.x), y0 = std::min(box.y, circleFrame.y);
    const double x1 = std::max(box.x + box.width, circleFrame.x + circleFrame.width), y1 = std::max(box.y + box.height, circleFrame.y + circleFrame.height);
    return {x0, y0, x1 - x0, y1 - y0};
}

// MARK: Lifecycle (:101-127)
void MapCanvas::show(double now) {
    Canvas::show(now);
    if (onPrepare) onPrepare();
}
void MapCanvas::didActivate() {
    if (onPrepare) onPrepare();
    apply(state_->setActive(true));
    cameraChanged();
    if (message_ && !messageShown_) { messageShown_ = true; toast(d::Icon::warn, "Notice", "提示", *message_); }
}
void MapCanvas::didDeactivate() {
    releaseGesture(false);
    apply(state_->setActive(false));
    if (onRelease) onRelease();
}
void MapCanvas::reportError(const std::string& text) { toast(d::Icon::warn, "Notice", "提示", text); }
void MapCanvas::stateChanged() { cameraChanged(); pinsChanged(); }

// MARK: Page (:131-180)
void MapCanvas::buildPage(d::Page& page) {
    header().configure({"MAP"}, "MOD-13", title("Map", "地图"), counterText(), false);
    const auto& palette = env().palette;
    d::Layer& well = page.makeLayer("map.well");
    d::Layer& crosshair = d::emptyState::crosshair(page.builder(), "map.crosshair");
    d::Layer& plate = page.makeLayer("map.zoom.plate");
    d::Layer& readout = d::makeText(page.builder(), "map.zoom.text", "", readoutStyle(palette), {7, 4, 0, 13}, d::TextAlignment::center);
    d::Layer& coordinatePlate = page.makeLayer("map.coordinates");
    std::array<d::Layer*, 3> lines{};
    for (std::size_t row = 0; row < lines.size(); ++row)
        lines[row] = &d::makeText(page.builder(), "map.coordinates." + std::to_string(row), "", row == 0 ? coordinateStyle().colored(palette.yellow()) : coordinateStyle(), {10, 6 + double(row) * 16.5, 0, 16.5});
    // The surface (raster, scrim, pins) is native: this layer only places it.
    d::Layer& surface = page.makeLayer("map.surface");
    well.setFrame({0, 0, d::metric::boxWidth, d::metric::boxHeight});
    surface.setFrame(mapFrame);
    crosshair.setPosition(mapCenter);
    plate.add(readout);
    for (auto* line : lines) coordinatePlate.add(*line);
    for (d::Layer* layer : {&surface, &crosshair, &plate, &coordinatePlate}) well.add(*layer);
    page.layer().add(well);
    surface_ = &surface; readoutPlate_ = &plate; readoutText_ = &readout; coordinatePlate_ = &coordinatePlate; coordinateLines_ = lines;
    // v1.2.0's controls inside the circle, as redesign buttons at its centres:
    // the zoom column on the left (the spec's 4 px gap), the centre placement on the right.
    const auto column = v1(37, 223);
    constexpr double pitch = 34;
    struct Item { const char* id; std::string label; d::Icon icon; d::Point centre; };
    const std::array<Item, 4> buttons{{
        {"map.zoomIn", text("Zoom in", "放大"), d::Icon::plus, {column.x, column.y - pitch}},
        {"map.zoomOut", text("Zoom out", "缩小"), d::Icon::minus, column},
        {"map.reset", text("Reset view", "重置视图"), d::Icon::reset, {column.x, column.y + pitch}},
        {"map.addPin", text("Place pin at map center", "在地图中心放置标记"), d::Icon::rec, v1(403, 218)}}};
    std::array<d::IconButton*, 4> controls{};
    for (std::size_t n = 0; n < buttons.size(); ++n) {
        const auto& item = buttons[n];
        auto& button = page.make<d::IconButton>(item.id, item.label, item.icon, d::IconButton::Style::dark, n == 3);
        button.layer().setPosition(item.centre);
        well.add(button.layer());
        page.add(button);
        controls[n] = &button;
    }
    controls[0]->onActivate = [this] { zoomStep(1.5); };
    controls[1]->onActivate = [this] { zoomStep(1 / 1.5); };
    controls[2]->onActivate = [this] { resetView(); };
    controls[3]->onActivate = [this] { addCenterPin(); };
    zoomIn_ = controls[0]; zoomOut_ = controls[1]; placePin_ = controls[3];
    placePin_->setEnabled(canPlacePin());
    chrome_ = {&controls[0]->layer(), &controls[1]->layer(), &controls[2]->layer(), &controls[3]->layer(), &plate, &coordinatePlate};
    page.stagger(well);
    updateCoordinates();
    cameraChanged();
}
std::string MapCanvas::counterText() const { return d::format::pad2(std::int64_t(state_->pins().size())) + " " + text("PINS", "标记"); }
bool MapCanvas::canPlacePin() const noexcept { return state_->editable() && state_->pins().size() < MapState::maximumPins; }

// MARK: Live chrome, in place (:190-238)
void MapCanvas::cameraChanged() {
    if (zoomIn_) zoomIn_->setEnabled(state_->canZoomIn());
    if (zoomOut_) zoomOut_->setEnabled(state_->canZoomOut());
    if (!readoutText_) return;
    const auto value = formatted("%.1f×", state_->camera().zoom);
    if (d::textOf(*readoutText_).text() == value) return;
    d::editText(*readoutText_, [&](d::TextBlock& block) { block.setText(value); });
    // Tabular figures: the width depends only on the length.
    const auto length = characters(value);
    auto found = readoutWidths_.find(length);
    if (found == readoutWidths_.end()) found = readoutWidths_.emplace(length, std::ceil(d::preferredWidth(d::textOf(*readoutText_), env().text))).first;
    const double width = found->second;
    readoutPlate_->setFrame({mapCenter.x - (width + 14) / 2, readoutTop, width + 14, 21});
    d::setBackground(*readoutPlate_, d::fixed::counterBackground);
    d::setLine(*readoutText_, {7, 4, width, 13});
}
void MapCanvas::pinsChanged() {
    if (placePin_) placePin_->setEnabled(canPlacePin());
    if (renderedLanguage()) header().setCounter(counterText());
    updateCoordinates();
    if (context().onInputStateChange) context().onInputStateChange();
}
void MapCanvas::updateCoordinates() {
    if (!coordinatePlate_) return;
    const auto pins = state_->pins();
    std::size_t index = pins.size();
    if (const auto& selected = state_->selection()) for (std::size_t n = 0; n < pins.size(); ++n) if (pins[n].id == *selected) { index = n; break; }
    if (index == pins.size()) { coordinatePlate_->setHidden(true); return; }
    const auto& pin = pins[index];
    const double latitude = 90 - pin.y * 180, longitude = mapWrappedX(pin.x) * 360 - 180;
    const std::array<std::string, 3> texts{text("PIN", "标记") + " " + d::format::pad2(std::int64_t(index + 1)),
        std::string(latitude < 0 ? "S" : "N") + formatted(" %.2f", std::abs(latitude)),
        std::string(longitude < 0 ? "W" : "E") + formatted(" %.2f", std::abs(longitude))};
    double widest = 0;
    for (std::size_t row = 0; row < texts.size(); ++row) {
        d::editText(*coordinateLines_[row], [&](d::TextBlock& block) { block.setText(texts[row]); });
        widest = std::max(widest, d::preferredWidth(d::textOf(*coordinateLines_[row]), env().text));
    }
    const double width = std::ceil(widest);
    const d::Size size{width + 20, 3 * 16.5 + 12};
    // Where v1.2.0 showed the selected pin's coordinates: centred above the bottom plates.
    coordinatePlate_->setFrame({mapCenter.x - size.width / 2, v1(0, 301).y, size.width, size.height});
    d::setBorder(*coordinatePlate_, d::rgb(0x121212, .92), d::rgb(0x444444));
    for (std::size_t row = 0; row < coordinateLines_.size(); ++row) d::setLine(*coordinateLines_[row], {10, 6 + double(row) * 16.5, width + 1, 16.5});
    coordinatePlate_->setHidden(false);
}
// The surface's callbacks (:62-66): camera and pins in place, errors toast.
void MapCanvas::apply(const MapChange& change) {
    if (change.pinsChanged) pinsChanged();
    if (change.changed) cameraChanged();
    if (change.error) reportError(*change.error);
    if (change.styleChanged && onPinStyleChanged) onPinStyleChanged(*change.styleChanged);
    if (change.recentered && onRecenter) onRecenter();
}

// MARK: Actions (:242-265)
void MapCanvas::zoomStep(double factor) {
    releaseGesture(true);
    apply(state_->zoom(G::center, factor, true));
    apply(state_->endGesture());
}
void MapCanvas::resetView() {
    releaseGesture(false);
    apply(state_->reset());
}
void MapCanvas::deletePin(std::string id) {
    const auto change = state_->removePin(id);
    apply(change);
    if (!change.pinsChanged) return;
    toast(d::Icon::trash, "PIN DELETED", "标记已删除");
}
void MapCanvas::addCenterPin() { if (canPlacePin()) addPin(G::center); }
void MapCanvas::addPin(d::Point point) {
    if (!state_->editable()) { toast(d::Icon::warn, "Notice", "提示", message_ ? *message_ : text("Map", "地图")); return; }
    apply(state_->addPin(point));
}

// MARK: Pointer, wheel and keyboard (:273-347)
bool MapCanvas::handlePointerDown(d::Point point, const d::PointerInput&) {
    if (!onTerrain(point)) return false;
    releaseGesture(true);
    state_->beginDrag(local(point));
    if (context().onLockParallax) context().onLockParallax();
    return true;
}
void MapCanvas::handlePointerDragged(d::Point point) { apply(state_->drag(local(point))); }
void MapCanvas::handlePointerUp(d::Point) {
    if (!state_->dragging()) return;
    MapChange pan;
    if (const auto press = state_->endDrag(&pan)) {
        if (const auto* pin = state_->pinAt(*press)) apply(state_->activatePin(std::string(pin->id)));
        else if (state_->selection()) apply(state_->deselect());
        else apply(state_->recenter(*press));
    } else apply(pan);
    if (context().onInputStateChange) context().onInputStateChange();
}
bool MapCanvas::handleRightPointerDown(d::Point point) {
    if (!onTerrain(point)) return false;
    if (state_->dragging()) return true;
    const auto target = local(point);
    if (const auto* pin = state_->pinAt(target)) deletePin(pin->id); else addPin(target);
    return true;
}
// x1.12 per wheel notch at the pointer. Without an NSEvent the Mac counts
// -delta.y / 12 notches (:306); the owner passes 12 px per Win32 detent
// (a precision touchpad's fractions stay fractions), as the Mac counts one
// per line of a mouse wheel and a trackpad's ten points as one.
bool MapCanvas::handleScroll(d::Point point, d::Point delta) {
    if (!onMap(point)) return false;
    const double notches = -delta.y / 12;
    if (std::isfinite(notches) && notches != 0) {
        apply(state_->zoom(local(point), std::pow(1.12, std::min(6., std::max(-6., notches)))));
        holdGesture();
    }
    return true;
}
bool MapCanvas::handleMagnify(d::Point point, double magnification) {
    if (!onMap(point)) return false;
    if (std::isfinite(magnification) && magnification != 0) {
        apply(state_->zoom(local(point), std::min(2., std::max(.5, 1 + magnification))));
        holdGesture();
    }
    return true;
}
// Arrows pan, + / - zoom, Delete removes the selected pin, Esc deselects it.
// Command / Control / Option keys pass (Shift is allowed, unlike v1.2.0).
bool MapCanvas::handleKey(const d::KeyInput& k) {
    if (k.commandModifiers()) return false;
    constexpr double step = 40;
    switch (k.virtualKey) {
    case d::key::left: apply(state_->pan({step, 0})); holdGesture(); break;
    case d::key::right: apply(state_->pan({-step, 0})); holdGesture(); break;
    case d::key::down: apply(state_->pan({0, -step})); holdGesture(); break;
    case d::key::up: apply(state_->pan({0, step})); holdGesture(); break;
    case 0xBB: case 0x6B: zoomStep(1.5); break;      // VK_OEM_PLUS (=), VK_ADD (keypad +): keys 24, 69
    case 0xBD: case 0x6D: zoomStep(1 / 1.5); break;  // VK_OEM_MINUS, VK_SUBTRACT: keys 27, 78
    case d::key::back: case d::key::deleteForward: { // keys 51, 117
        const auto& selected = state_->selection();
        if (!selected) return false;
        deletePin(*selected);
        break;
    }
    case d::key::escape:
        if (!state_->selection()) return false;
        apply(state_->deselect());
        break;
    default: return false;
    }
    return true;
}
// Wheel, pinch and key steps send no end event: the camera is committed (one
// archive write, final detail) once input pauses for .18 s (:349-376). The
// one deadline is the canvas's wake time; nothing is allocated per event.
void MapCanvas::holdGesture() {
    if (!gestureHeld_) { gestureHeld_ = true; if (context().onLockParallax) context().onLockParallax(); }
    gestureDeadline_ = now() + .18;
}
std::optional<double> MapCanvas::nextWakeTime(double) const { return gestureHeld_ ? std::optional(gestureDeadline_) : std::nullopt; }
void MapCanvas::wake(double now) { if (gestureHeld_ && now >= gestureDeadline_) releaseGesture(true); }
void MapCanvas::releaseGesture(bool commit) {
    if (!gestureHeld_) return;
    gestureHeld_ = false;
    if (commit) apply(state_->endGesture());
    if (context().onInputStateChange) context().onInputStateChange();
}
// The map's pointer (spec: grab, grabbing while panning); none leaves the
// HUD's own cursor (:380-384).
std::optional<d::Cursor> MapCanvas::cursor(std::optional<d::Point> point) const {
    if (state_->dragging()) return d::Cursor::closedHand;
    if (!isActive() || !point || !onTerrain(*point)) return std::nullopt;
    return d::Cursor::openHand;
}

// MARK: Accessibility (:388-416): pins and the selected pin's removal are
// actions too (a screen reader cannot right-click, and pins are drawn by the
// map rather than controls).
void MapCanvas::accessibilityItems(std::vector<d::AccessibilityItem>& out) const {
    Canvas::accessibilityItems(out);
    if (!isActive() || transitioning()) return;
    const auto pins = state_->pins();
    for (std::size_t index = 0; index < pins.size(); ++index) {
        const auto& pin = pins[index];
        const auto point = G::screen(pin.x, pin.y, state_->camera());
        if (!G::contains(point)) continue;
        const char* title = pin.style == MapPinStyle::green ? "Green pin" : pin.style == MapPinStyle::player ? "Player marker" : "Yellow pin";
        const char* zh = pin.style == MapPinStyle::green ? "绿色标记" : pin.style == MapPinStyle::player ? "玩家标记" : "黄色标记";
        const double r = G::pinHitRadius(pin.style);
        d::AccessibilityItem item;
        item.id = "map.pin:" + pin.id;
        item.label = text(title, zh) + " " + d::format::pad2(std::int64_t(index + 1)) + ", " + mapCoordinateDescription(pin.x, pin.y);
        item.rect = {mapFrame.x + point.x - r, mapFrame.y + point.y - r, 2 * r, 2 * r};
        out.push_back(std::move(item));
    }
    if (state_->selection() && coordinatePlate_) {
        d::AccessibilityItem item;
        item.id = "map.removePin";
        item.label = text("Remove selected pin", "移除选中标记");
        item.rect = d::convert(d::localBounds(*coordinatePlate_), *coordinatePlate_, root());
        out.push_back(std::move(item));
    }
}
bool MapCanvas::accessibilityAction(std::string_view id) {
    constexpr std::string_view pinPrefix = "map.pin:";
    if (id.starts_with(pinPrefix)) { apply(state_->activatePin(id.substr(pinPrefix.size()))); return true; }
    if (id == "map.removePin") { if (const auto& selected = state_->selection()) deletePin(*selected); return true; }
    return false;
}
} // namespace endfield::modules
