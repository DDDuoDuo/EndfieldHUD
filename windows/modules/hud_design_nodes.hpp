#pragma once
// THE node builders of the redesign: CALayer-shaped JSON in the vocabulary of
// tools/module_reference_layers.swift (the Mac oracle encoder) and of
// native/layer_raster.cpp, plus the shapes of Sources/HUDDesignDrawing.swift
// (HUDShape, HUDArtwork motifs, HUDHatchLayer, HUDCentreBackdrop). LIB-7..10.
//
// These replace the per-presentation copies that exist today (`Json layer(`
// in 16 files, `shape(` 14, `color(` 19, `feedback(` 20, chamfer `cut(` 9;
// windows-architecture.md section 0.6). New design code never writes its own.
// The Mac draws small motifs once into a shared CGImage cache (HUDArtwork,
// keyed by name/size/ceil(scale)); here a motif is a plain node subtree that
// the shared LayerRasterizer paints into the surface that contains it, cached
// by surface id + revision like every other leaf. Building JSON is a content-
// event operation; nothing here runs per frame.
//
// Coordinates are content-box px, top-down (top = min y), as on the Mac.
// Nodes are anchored (0, 0) at their frame origin; the layer tree
// (hud_design_layer.hpp) owns placement, transforms and animation.
#include "modules/hud_design_text.hpp"
#include <span>
#include <string>
#include <vector>

namespace endfield::modules::design {

// Path commands {op: move|line|quadratic|cubic|close, points} (the encoder's
// CGPath dump). Arcs are converted to cubics by the SVG reader.
class PathBuilder final {
public:
    PathBuilder& move(Point);
    PathBuilder& line(Point);
    PathBuilder& quadratic(Point control, Point end);
    PathBuilder& cubic(Point c1, Point c2, Point end);
    PathBuilder& close();
    PathBuilder& rect(Rect);
    PathBuilder& ellipse(Rect);   // four cubics, CGPath(ellipseIn:) point order and its constant .5522847498
    PathBuilder& append(const PathBuilder&);
    PathBuilder transformed(double a, double b, double c, double d, double tx, double ty) const; // affine
    bool empty() const noexcept { return commands_.empty(); }
    Json json() const;            // [{op, points:[[x,y]...]}]
    bool contains(Point, bool evenOdd = false) const; // hit paths (chamfer/ellipse targets)
    Rect bounds() const noexcept;
private:
    struct Command { std::uint8_t op{}; std::array<Point, 3> points{}; };
    std::vector<Command> commands_;
};

// HUDShape (HUDDesignDrawing.swift:7-79).
enum Corner : unsigned { topLeft = 1, topRight = 2, bottomRight = 4, bottomLeft = 8 };
// "Chamfer N": the listed corners cut at 45 degrees, n = clamp(cut, 0,
// min(w/2, h/2)); default top-right only. Start at top-left (+n when cut) ->
// TR -> BR -> BL -> TL, exactly the Mac point order (LIB-7, LIB-94).
PathBuilder chamfer(Rect, double cut, unsigned corners = topRight);
// 1 px ticks from x 0 while x <= width - tick + .001.
PathBuilder tickLadder(double width, double height, double pitch, double tick = 1);
// `count` ticks evenly with both ends included (none when count <= 1).
PathBuilder ticks(int count, double width, double height, double tick = 1);
// Two 1 px lines through (size - 1) / 2.
PathBuilder crosshair(double size = 11);
// An L of two rects at `corner` of `rect`.
PathBuilder cornerMark(Rect, double length, double width, Corner corner);
PathBuilder triangleDown(double width, double height);
// HUDDragGrip.dots: columns x rows of 2 px squares 4 px apart.
PathBuilder gripDots(int columns, int rows);
// HUDButton.barPath: the 4-point secondary-button bar (arrow: the ▶ shape
// (2,2.4) (9,6) (9,6) (2,9.6)).
PathBuilder buttonBar(bool arrow);
// The redaction stripes (HUDDesignRedaction.swift stripePath :112-122):
// parallelograms of `band` px every `period` px along x, bottom-left to
// top-right (-45 deg CSS repeating-linear-gradient), from x = -h - period
// while x < w + period. Redaction: band 3 sqrt2, period 6 sqrt2. hatch()
// draws the same shape with HUDArtwork.drawHatch's range (:100-114).
PathBuilder diagonalStripes(Size, double band, double period);

// ---------------------------------------------------------------------------
// Nodes
// ---------------------------------------------------------------------------
Json color(Color);                         // {"sRGB":[r,g,b,a]}
Json frameNode(std::string id, Rect frame, const char* kind = "layer"); // bounds/position/anchor (0,0)/children
// backgroundColor (+ optional border, circular cornerRadius).
Json solid(std::string id, Rect frame, Color fill, double cornerRadius = 0);
Json bordered(std::string id, Rect frame, std::optional<Color> fill, Color border, double borderWidth = 1, double cornerRadius = 0);
struct StrokeStyle {
    double width{1}, miterLimit{10};
    const char* cap{"butt"};       // butt | round | square
    const char* join{"miter"};     // miter | round | bevel
    std::vector<double> dash;      // lineDashPattern (the mirror icon's 2/2)
    bool operator==(const StrokeStyle&) const = default;
};
Json shape(std::string id, Rect frame, const PathBuilder& path, std::optional<Color> fill,
    std::optional<Color> stroke = {}, StrokeStyle = {}, bool evenOdd = false);
enum class GradientKind : std::uint8_t { axial, radial };
// CAGradientLayer: colours, locations (empty = even), start/end in unit space.
// Radial (the confirm dim, the centre backdrop mask) needs the A1 rasterizer
// extension (ID2D1RadialGradientBrush; startPoint = centre, endPoint = the
// unit-space radius corner as CA defines it).
Json gradient(std::string id, Rect frame, std::span<const Color> colors, std::span<const double> locations,
    Point start, Point end, GradientKind = GradientKind::axial);
// Baked layer shadow (CALayer shadowColor/Opacity/Radius/Offset/Path) for the
// panel chrome (black .6 / Light .25, radius 20, offset y +18), the slider
// bubble (.45, 8, y 6) and the archive doc glyph (.25, 6, y 6). Needs the A1
// "baked shadow" extension; until then the rasterizer reports it unsupported.
struct Shadow { Color color{black(1)}; double opacity{}, radius{3}; Point offset{0, -3}; std::optional<PathBuilder> path; };
void addShadow(Json& node, const Shadow&);
// A filled shape mask (the only mask kind the rasterizer clips with).
void addShapeMask(Json& node, const PathBuilder& path);
void addChildren(Json& node, Json::Array children);
// Images: a pinned asset {asset, sha256}, a LayerImageSource memory image
// {key, revision}, or a requiredSourceImage key the owner resolves before
// the scene loads (native/settings_scene.cpp pattern). Gravity names are CA's.
struct ImageSource {
    enum class Kind : std::uint8_t { asset, memory, required } kind{Kind::required};
    std::string key, sha256; std::uint64_t revision{};
    bool operator==(const ImageSource&) const = default;
};
Json image(std::string id, Rect frame, const ImageSource&, const char* gravity = "resizeAspect",
    std::optional<Color> sourceInTint = {}, bool trilinear = false);

// ---------------------------------------------------------------------------
// Artwork motifs (HUDArtwork, :84-195)
// ---------------------------------------------------------------------------
// plate(): a vertical gradient (top -> bottom, even locations by default)
// clipped to the chamfer, optional 1 px top highlight white .75. Shared by
// rows, light and primary buttons, cards, the About hero, keycaps, stat
// plates, cells and tabs.
Json plate(std::string id, Rect frame, std::span<const Color> colors, std::span<const double> locations,
    double cut, unsigned corners = topRight, bool highlight = false);
enum class Hatch : std::uint8_t { dark, light }; // dark: black .42; light: foreground(.14)
Json hatch(std::string id, Rect frame, Hatch, const Palette&);
// HUDHatchLayer (LIB-9): a large unique hatched panel (empty states) drawn as
// one node with `frame` size; the Mac paints these straight into their own
// backing store instead of a cached image. Same here: no shared motif.
Json hatchPanel(std::string id, Rect frame, Hatch, const Palette&);
// Micro ENDFIELD tag (:117-129): label width = width("ENDFIELD", microTag) + 8,
// then a 44 x 7 dark hatch; height 7. Normal 161616 / E9E9E9, inverted (done
// task card) E9E9E9 / 111111; text always zh-Hans, from x 4, centred on the
// CSS line box.
Size microTagSize(const TextServices&);
Json microTag(std::string id, Point origin, bool inverted, const TextServices&, const Palette&);
// Tri-strip (:132-139): three equal horizontal bands magenta, mint, yellow.
Json triStrip(std::string id, Rect frame);
// HUDCentreBackdrop (LIB-10): 430 px radius disc at box (295, 304) masked by
// the radial ramp opaque to 52 %, .55 at 74 %, clear at 100 % (one shared
// half-resolution mask on the Mac). Returns the ramp as a radial gradient
// node over the backdrop frame (-135, -126, 860 x 860). The rasterizer clips
// only with filled-shape masks, so the caller's artwork (Personal ID, Map)
// uses a layer holding this node as its alpha clip (Layer::setAlphaClip, R4b).
inline constexpr Point centreBackdropCentre{295, 304};
inline constexpr double centreBackdropRadius = 430;
Json centreBackdropMask(std::string id);

// ---------------------------------------------------------------------------
// Part assembly (generalizes SettingsBuild + compact(): modules/
// settings_presentation.cpp:174-179, native/settings_scene.cpp:10-15)
// ---------------------------------------------------------------------------
// A part is one LayerScene's source tree: a zero-bounds root whose children
// are the part's SURFACES, each an id'd node rasterized as one image with its
// whole subtree (native/layer_scene.cpp rasterGroup). The emitter of the layer
// tree (hud_design_layer.hpp) fills parts; non-layer-tree callers (chrome-only
// modules such as Map) may use PartBuilder directly.
enum class SurfaceRole : std::uint8_t {
    staticContent, // compacted with consecutive static siblings into one surface
    animated,      // its own surface: placement / opacity / tint / masks change numerically
    lazy           // its own surface, painted only while it shows (hover sheen, tints) and released after
};
struct PartSurface {
    std::string id;
    SurfaceRole role{SurfaceRole::staticContent};
    Rect localBounds;                 // the node's frame in part coordinates
    bool operator==(const PartSurface&) const = default;
};
struct Part {
    std::string id;                   // stable part identity (reuse key with the JSON)
    Json layers;                      // root with one child per surface, paint order
    std::vector<PartSurface> surfaces;
};
class PartBuilder final {
public:
    explicit PartBuilder(std::string partID);
    // Adds one node (its position is its frame origin in part coordinates).
    void add(Json node, SurfaceRole = SurfaceRole::staticContent);
    // Consecutive static nodes become one "<part>.static.N" group surface
    // (allowsGroupOpacity false), like native/settings_scene.cpp compact().
    Part finish();
    std::size_t surfaceCount() const noexcept; // after compaction (raster budget planning)
private:
    std::string id_;
    std::vector<std::pair<Json, SurfaceRole>> nodes_;
};
} // namespace endfield::modules::design
