// Node builders and shapes (Sources/HUDDesignDrawing.swift HUDShape,
// HUDArtwork, HUDHatchLayer, HUDCentreBackdrop; HUDDesignButtons.swift
// barPath / HUDDragGrip.dots; HUDDesignRedaction.swift stripePath). JSON in
// the vocabulary of tools/module_reference_layers.swift and
// native/layer_raster.cpp. Content-event code only.
#include "modules/hud_design_nodes.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

// Swift never fuses multiply-adds; neither does this port, so its numbers
// stay bit-equal with the Mac oracle's.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::modules::design {
namespace {
enum Op : std::uint8_t { opMove, opLine, opQuadratic, opCubic, opClose };
constexpr const char* opNames[]{"move", "line", "quadratic", "cubic", "close"};
constexpr std::uint8_t opPoints[]{1, 1, 2, 3, 0};
Json point(Point p) { return Json::Array{p.x, p.y}; }
Json rect(Rect r) { return Json::Array{r.x, r.y, r.width, r.height}; }
// Bezier flattening for hit paths and bounds (16 steps per curve: the hit
// shapes are chamfers, ellipses and icons a few px across).
template <class F> void flatten(Point from, std::uint8_t op, const std::array<Point, 3>& p, F&& add) {
    constexpr int steps = 16;
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps, u = 1 - t;
        if (op == opQuadratic) add(Point{u * u * from.x + 2 * u * t * p[0].x + t * t * p[1].x, u * u * from.y + 2 * u * t * p[0].y + t * t * p[1].y});
        else add(Point{u * u * u * from.x + 3 * u * u * t * p[0].x + 3 * u * t * t * p[1].x + t * t * t * p[2].x,
                       u * u * u * from.y + 3 * u * u * t * p[0].y + 3 * u * t * t * p[1].y + t * t * t * p[2].y});
    }
}
// HUDArtwork.drawHatch (:100-114) / HUDRedaction.stripePath (:112-122):
// parallelograms bottom-left -> top-right, `band` wide along x.
PathBuilder stripes(Size size, double band, double period, double start, double end) {
    PathBuilder path;
    for (double x = start; x < end; x += period)
        path.move({x, size.height}).line({x + band, size.height}).line({x + band + size.height, 0}).line({x + size.height, 0}).close();
    return path;
}
PathBuilder hatchPath(Size size) { return stripes(size, std::sqrt(2.0), 3.5 * std::sqrt(2.0), -size.height, size.width + size.height); }
}

PathBuilder& PathBuilder::move(Point p) { commands_.push_back({opMove, {p}}); return *this; }
PathBuilder& PathBuilder::line(Point p) { commands_.push_back({opLine, {p}}); return *this; }
PathBuilder& PathBuilder::quadratic(Point c, Point e) { commands_.push_back({opQuadratic, {c, e}}); return *this; }
PathBuilder& PathBuilder::cubic(Point a, Point b, Point e) { commands_.push_back({opCubic, {a, b, e}}); return *this; }
PathBuilder& PathBuilder::close() { commands_.push_back({opClose, {}}); return *this; }
// CGPath.addRect: (minX,minY) -> (maxX,minY) -> (maxX,maxY) -> (minX,maxY), closed.
PathBuilder& PathBuilder::rect(Rect r) {
    return move({r.x, r.y}).line({r.x + r.width, r.y}).line({r.x + r.width, r.y + r.height}).line({r.x, r.y + r.height}).close();
}
// CGPath(ellipseIn:): from (maxX, midY) through (midX, maxY), (minX, midY),
// (midX, minY), four cubics with CoreGraphics' truncated circle constant.
PathBuilder& PathBuilder::ellipse(Rect r) {
    constexpr double k = .5522847498;
    const double cx = r.x + r.width / 2, cy = r.y + r.height / 2, rx = r.width / 2, ry = r.height / 2;
    move({cx + rx, cy});
    cubic({cx + rx, cy + ry * k}, {cx + rx * k, cy + ry}, {cx, cy + ry});
    cubic({cx - rx * k, cy + ry}, {cx - rx, cy + ry * k}, {cx - rx, cy});
    cubic({cx - rx, cy - ry * k}, {cx - rx * k, cy - ry}, {cx, cy - ry});
    cubic({cx + rx * k, cy - ry}, {cx + rx, cy - ry * k}, {cx + rx, cy});
    return close();
}
PathBuilder& PathBuilder::append(const PathBuilder& o) { commands_.insert(commands_.end(), o.commands_.begin(), o.commands_.end()); return *this; }
PathBuilder PathBuilder::transformed(double a, double b, double c, double d, double tx, double ty) const {
    PathBuilder out = *this;
    for (auto& command : out.commands_)
        for (std::uint8_t i = 0; i < opPoints[command.op]; ++i) { const auto p = command.points[i]; command.points[i] = {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    return out;
}
Json PathBuilder::json() const {
    Json::Array out;
    out.reserve(commands_.size());
    for (const auto& c : commands_) {
        Json::Array points;
        for (std::uint8_t i = 0; i < opPoints[c.op]; ++i) points.push_back(point(c.points[i]));
        out.push_back(Json::Object{{"op", opNames[c.op]}, {"points", std::move(points)}});
    }
    return out;
}
// CGPath.contains(_:using: .winding) (or .evenOdd), on the flattened outline.
bool PathBuilder::contains(Point q, bool evenOdd) const {
    int winding{}, crossings{};
    Point start{}, current{};
    bool open{};
    const auto edge = [&](Point a, Point b) {
        if ((a.y <= q.y) != (b.y <= q.y)) {
            const double x = a.x + (q.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x > q.x) { ++crossings; winding += b.y > a.y ? 1 : -1; }
        }
    };
    const auto closeFigure = [&] { if (open) edge(current, start); open = false; };
    for (const auto& c : commands_) {
        if (c.op == opMove) { closeFigure(); start = current = c.points[0]; open = true; continue; }
        if (c.op == opClose) { closeFigure(); current = start; continue; }
        if (!open) { start = current; open = true; }
        if (c.op == opLine) { edge(current, c.points[0]); current = c.points[0]; continue; }
        flatten(current, c.op, c.points, [&](Point p) { edge(current, p); current = p; });
    }
    closeFigure();
    return evenOdd ? (crossings & 1) != 0 : winding != 0;
}
Rect PathBuilder::bounds() const noexcept {
    double left = std::numeric_limits<double>::infinity(), top = left, right = -left, bottom = -left;
    const auto add = [&](Point p) { left = std::min(left, p.x); right = std::max(right, p.x); top = std::min(top, p.y); bottom = std::max(bottom, p.y); };
    Point current{};
    for (const auto& c : commands_) {
        if (c.op == opClose) continue;
        if (c.op == opMove || c.op == opLine) { add(c.points[0]); current = c.points[0]; continue; }
        add(current);
        flatten(current, c.op, c.points, [&](Point p) { add(p); current = p; });
    }
    if (!(right >= left)) return {};
    return {left, top, right - left, bottom - top};
}

// HUDShape.chamfer (:16-32).
PathBuilder chamfer(Rect r, double cut, unsigned corners) {
    const double n = std::max(0.0, std::min({cut, r.width / 2, r.height / 2}));
    const double minX = r.x, minY = r.y, maxX = r.x + r.width, maxY = r.y + r.height;
    PathBuilder p;
    p.move({minX + (corners & topLeft ? n : 0), minY});
    if (corners & topRight) p.line({maxX - n, minY}).line({maxX, minY + n}); else p.line({maxX, minY});
    if (corners & bottomRight) p.line({maxX, maxY - n}).line({maxX - n, maxY}); else p.line({maxX, maxY});
    if (corners & bottomLeft) p.line({minX + n, maxY}).line({minX, maxY - n}); else p.line({minX, maxY});
    if (corners & topLeft) p.line({minX, minY + n});
    return p.close();
}
// HUDShape.tickLadder (:35-40).
PathBuilder tickLadder(double width, double height, double pitch, double tick) {
    if (!(pitch > 0)) throw std::invalid_argument("Tick ladder needs a positive pitch");
    PathBuilder p;
    for (double x = 0; x <= width - tick + .001; x += pitch) p.rect({x, 0, tick, height});
    return p;
}
// HUDShape.ticks (:43-51).
PathBuilder ticks(int count, double width, double height, double tick) {
    PathBuilder p;
    if (count <= 1) return p;
    for (int i = 0; i < count; ++i) p.rect({(width - tick) * double(i) / double(count - 1), 0, tick, height});
    return p;
}
// HUDShape.crosshair (:54-59).
PathBuilder crosshair(double size) {
    const double mid = (size - 1) / 2;
    PathBuilder p;
    return p.rect({mid, 0, 1, size}).rect({0, mid, size, 1});
}
// HUDShape.cornerMark (:62-71).
PathBuilder cornerMark(Rect r, double length, double width, Corner corner) {
    const bool right = corner & (topRight | bottomRight), bottom = corner & (bottomLeft | bottomRight);
    const double maxX = r.x + r.width, maxY = r.y + r.height;
    const double x = right ? maxX - length : r.x, y = bottom ? maxY - length : r.y;
    const double vx = right ? maxX - width : r.x, hy = bottom ? maxY - width : r.y;
    PathBuilder p;
    return p.rect({x, hy, length, width}).rect({vx, y, width, length});
}
// HUDShape.triangleDown (:74-79).
PathBuilder triangleDown(double width, double height) {
    PathBuilder p;
    return p.move({0, 0}).line({width, 0}).line({width / 2, height}).close();
}
// HUDDragGrip.dots (HUDDesignButtons.swift:310-314).
PathBuilder gripDots(int columns, int rows) {
    PathBuilder p;
    for (int c = 0; c < columns; ++c) for (int r = 0; r < rows; ++r) p.rect({double(c) * 4, double(r) * 4, 2, 2});
    return p;
}
// HUDButton.barPath (HUDDesignButtons.swift:101-107): one 4-point topology.
PathBuilder buttonBar(bool arrow) {
    PathBuilder p;
    if (arrow) p.move({2, 2.4}).line({9, 6}).line({9, 6}).line({2, 9.6});
    else p.move({0, 0}).line({4, 0}).line({4, 12}).line({0, 12});
    return p.close();
}
// HUDRedaction.stripePath (HUDDesignRedaction.swift:112-122) when called
// with band 3 sqrt2, period 6 sqrt2: from x = -h - period while x < w + period.
PathBuilder diagonalStripes(Size size, double band, double period) {
    if (!(period > 0)) throw std::invalid_argument("Stripes need a positive period");
    return stripes(size, band, period, -size.height - period, size.width + period);
}

Json color(Color c) { return Json::Object{{"sRGB", Json::Array{c.r, c.g, c.b, c.a}}}; }
Json frameNode(std::string id, Rect f, const char* kind) {
    return Json::Object{{"id", std::move(id)}, {"kind", kind}, {"bounds", rect({0, 0, f.width, f.height})},
        {"position", point({f.x, f.y})}, {"anchorPoint", point({0, 0})}, {"children", Json::Array{}}};
}
Json solid(std::string id, Rect f, Color fill, double radius) {
    auto n = frameNode(std::move(id), f);
    n["backgroundColor"] = color(fill);
    if (radius > 0) n["cornerRadius"] = radius;
    return n;
}
Json bordered(std::string id, Rect f, std::optional<Color> fill, Color border, double width, double radius) {
    auto n = frameNode(std::move(id), f);
    if (fill) n["backgroundColor"] = color(*fill);
    n["borderColor"] = color(border);
    n["borderWidth"] = width;
    if (radius > 0) n["cornerRadius"] = radius;
    return n;
}
Json shape(std::string id, Rect f, const PathBuilder& path, std::optional<Color> fill, std::optional<Color> stroke, StrokeStyle s, bool evenOdd) {
    auto n = frameNode(std::move(id), f, "shape");
    Json::Object body{{"path", path.json()}, {"fillColor", fill ? color(*fill) : Json{}}, {"strokeColor", stroke ? color(*stroke) : Json{}},
        {"lineWidth", s.width}, {"lineCap", s.cap}, {"lineJoin", s.join}, {"miterLimit", s.miterLimit}, {"fillRule", evenOdd ? "even-odd" : "non-zero"}};
    if (!s.dash.empty()) { Json::Array dash; for (const double d : s.dash) dash.push_back(d); body["lineDashPattern"] = std::move(dash); }
    n["shape"] = std::move(body);
    return n;
}
Json gradient(std::string id, Rect f, std::span<const Color> colors, std::span<const double> locations, Point start, Point end, GradientKind kind) {
    if (!locations.empty() && locations.size() != colors.size()) throw std::invalid_argument("Gradient locations must match its colours");
    auto n = frameNode(std::move(id), f, "gradient");
    Json::Array c, l;
    for (const auto& v : colors) c.push_back(color(v));
    for (const double v : locations) l.push_back(v);
    n["gradient"] = Json::Object{{"colors", std::move(c)}, {"locations", locations.empty() ? Json{} : Json(std::move(l))},
        {"startPoint", point(start)}, {"endPoint", point(end)}, {"type", kind == GradientKind::radial ? "radial" : "axial"}};
    return n;
}
void addShadow(Json& node, const Shadow& s) {
    node["shadowColor"] = color(s.color);
    node["shadowOpacity"] = s.opacity;
    node["shadowRadius"] = s.radius;
    node["shadowOffset"] = point(s.offset);
    if (s.path) node["shadowPath"] = s.path->json();
}
// A filled-shape layer mask in the node's own bounds (CALayer.mask).
void addShapeMask(Json& node, const PathBuilder& path) {
    const auto& b = node["bounds"].array();
    auto mask = frameNode(node["id"].string() + ".mask", {0, 0, b[2].number(), b[3].number()}, "shape");
    mask["shape"] = Json::Object{{"path", path.json()}, {"fillColor", color(black(1))}, {"strokeColor", Json{}}, {"fillRule", "non-zero"}};
    node["mask"] = std::move(mask);
}
void addChildren(Json& node, Json::Array children) {
    if (!node["children"].isArray()) node["children"] = Json::Array{};
    auto& list = node["children"];
    auto all = list.array();
    for (auto& c : children) all.push_back(std::move(c));
    list = std::move(all);
}
Json image(std::string id, Rect f, const ImageSource& source, const char* gravity, std::optional<Color> tint, bool trilinear) {
    auto n = frameNode(std::move(id), f);
    switch (source.kind) {
    case ImageSource::Kind::asset: n["contents"] = Json::Object{{"asset", source.key}, {"sha256", source.sha256}}; break;
    case ImageSource::Kind::memory: n["contents"] = Json::Object{{"memoryImage", source.key}, {"revision", std::int64_t(source.revision)}}; break;
    case ImageSource::Kind::required: n["requiredSourceImage"] = source.key; break;
    }
    n["contentsGravity"] = gravity;
    if (tint) n["contentsSourceInTint"] = color(*tint);
    if (trilinear) n["minificationFilter"] = "trilinear";
    return n;
}

// HUDArtwork.plate (:144-157): the gradient top -> bottom clipped to the
// chamfer, the optional 1 px highlight inside the same clip.
Json plate(std::string id, Rect f, std::span<const Color> colors, std::span<const double> locations, double cut, unsigned corners, bool highlight) {
    auto n = frameNode(id, f);
    Json::Array children{gradient(id + ".fill", {0, 0, f.width, f.height}, colors, locations, {0, 0}, {0, 1})};
    if (highlight) children.push_back(solid(id + ".highlight", {0, 0, f.width, 1}, white(.75)));
    n["children"] = std::move(children);
    addShapeMask(n, chamfer({0, 0, f.width, f.height}, cut, corners));
    return n;
}
// HUDArtwork.hatch / drawHatch (:92-114): black .42 or foreground(.14), clipped to the rect.
Json hatch(std::string id, Rect f, Hatch kind, const Palette& palette) {
    auto n = shape(std::move(id), f, hatchPath({f.width, f.height}), kind == Hatch::dark ? black(.42) : palette.foreground(.14));
    n["masksToBounds"] = true;
    return n;
}
Json hatchPanel(std::string id, Rect f, Hatch kind, const Palette& palette) { return hatch(std::move(id), f, kind, palette); }
// HUDArtwork.microTagSize (:117-118): the label measures in the UI region.
Size microTagSize(const TextServices& s) { return {s.width("ENDFIELD", type::microTag()) + 8 + 44, 7}; }
// HUDArtwork.microTag (:120-129, draw :185-194: zh-Hans, x 4, centred on the line box).
Json microTag(std::string id, Point origin, bool inverted, const TextServices& s, const Palette& palette) {
    const auto size = microTagSize(s);
    const double label = size.width - 44;
    auto n = frameNode(id, {origin.x, origin.y, size.width, size.height});
    TextBlock text("ENDFIELD", type::microTag().colored(inverted ? fixed::nearBlack : fixed::microTagText));
    text.language = TextRegion::simplifiedChinese;
    text.truncates = false;
    text.setLineFrame({4, 0, label - 4, 7});
    n["children"] = Json::Array{solid(id + ".label", {0, 0, label, 7}, inverted ? fixed::microTagText : fixed::microTagBlack),
        textNode(id + ".text", text, s), hatch(id + ".hatch", {label, 0, 44, 7}, Hatch::dark, palette)};
    return n;
}
// HUDArtwork.triStrip (:132-139).
Json triStrip(std::string id, Rect f) {
    auto n = frameNode(id, f);
    Json::Array children;
    for (std::size_t i = 0; i < 3; ++i)
        children.push_back(solid(id + "." + std::to_string(i), {0, f.height * double(i) / 3, f.width, f.height / 3}, fixed::triStrip[i]));
    n["children"] = std::move(children);
    return n;
}
// HUDCentreBackdrop (:247-263): radial-gradient(circle 430px, #000 0 52%,
// rgba(0,0,0,.55) 74%, transparent) over the backdrop frame. The rasterizer
// clips only with filled-shape masks, so callers use this node as the
// artwork surface's alpha clip (Layer::setAlphaClip) rather than as a mask.
Json centreBackdropMask(std::string id) {
    constexpr Color ramp[]{black(1), black(1), black(.55), black(0)};
    constexpr double at[]{0, .52, .74, 1};
    const Rect f{centreBackdropCentre.x - centreBackdropRadius, centreBackdropCentre.y - centreBackdropRadius, 2 * centreBackdropRadius, 2 * centreBackdropRadius};
    return gradient(std::move(id), f, ramp, at, {.5, .5}, {1, 1}, GradientKind::radial);
}

PartBuilder::PartBuilder(std::string id) : id_(std::move(id)) {}
void PartBuilder::add(Json node, SurfaceRole role) { nodes_.emplace_back(std::move(node), role); }
std::size_t PartBuilder::surfaceCount() const noexcept {
    std::size_t n{};
    bool run{};
    for (const auto& [node, role] : nodes_) {
        const bool stat = role == SurfaceRole::staticContent;
        if (!stat || !run) ++n;
        run = stat;
    }
    return n;
}
// The v1.2.0 Settings scene's group()/compact(): a static run is one group
// surface whose bounds are the union of its nodes' frames (positions kept).
Part PartBuilder::finish() {
    Part part{id_, frameNode(id_, {}), {}};
    Json::Array children, pending;
    std::size_t serial{};
    const auto frameOf = [](const Json& n) {
        const auto& b = n["bounds"].array(); const auto& p = n["position"].array();
        return Rect{p[0].number(), p[1].number(), b[2].number(), b[3].number()};
    };
    const auto flush = [&] {
        if (pending.empty()) return;
        double left = 1e300, top = 1e300, right = -1e300, bottom = -1e300;
        for (const auto& n : pending) { const auto r = frameOf(n); left = std::min(left, r.x); top = std::min(top, r.y); right = std::max(right, r.x + r.width); bottom = std::max(bottom, r.y + r.height); }
        auto id = id_ + ".static." + std::to_string(serial++);
        const Rect r{left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)};
        Json group = Json::Object{{"id", id}, {"kind", "layer"}, {"bounds", rect(r)}, {"position", point({r.x, r.y})}, {"anchorPoint", point({0, 0})},
            {"allowsGroupOpacity", false}, {"children", std::move(pending)}};
        pending = {};
        part.surfaces.push_back({std::move(id), SurfaceRole::staticContent, r});
        children.push_back(std::move(group));
    };
    for (auto& [node, role] : nodes_) {
        if (role == SurfaceRole::staticContent) { pending.push_back(std::move(node)); continue; }
        flush();
        part.surfaces.push_back({node["id"].string(), role, frameOf(node)});
        children.push_back(std::move(node));
    }
    flush();
    nodes_.clear();
    part.layers["children"] = std::move(children);
    return part;
}
} // namespace endfield::modules::design
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
