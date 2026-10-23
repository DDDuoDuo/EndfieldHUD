#pragma once
// The retained layer tree of a design canvas: a portable stand-in for the
// CALayer trees the Mac redesign builds (HUDLayer.plain/shape/gradient,
// HUDTextLayer, HUDIconLayer, HUDHatchLayer) and the HUDMotion verbs that act
// on them (Sources/HUDDesignMotion.swift :33-184). It exists so a module canvas
// ports line by line: buildPage creates layers, sets frames and content,
// nests them, and later calls motion::animate / enter / leave / wipe on them,
// exactly where the Swift does.
//
// From layers to pixels (one build pass, then numbers):
//  1. Content events (buildPage, rerender, a control's setValue) set a layer's
//     model: frame, base transform, channel values, hidden, opacity, mask,
//     and its CONTENT (one node subtree from hud_design_nodes / a TextBlock).
//  2. emit() turns the tree into Parts (hud_design_nodes.hpp): a layer marked
//     animatable() is a SURFACE BOUNDARY (its own surface; its children
//     recurse), consecutive static content is compacted into one surface,
//     and every surface records the boundary layer that places it. The
//     native scene reuses a part whose JSON is byte-equal, replaces a single
//     surface whose JSON changed (LayerScene::updateLocalContent) and
//     rasterizes nothing else: scrolling, hover, press, entrance, wipe, menu
//     and swap motion never re-rasterize.
//  3. Every frame samplePoses() composes model + tracks into one SurfacePose
//     per surface (box-px world affine, opacity, sRGB tint, up to 8 clip
//     rects): allocation-free, no JSON, no text.
//
// Rules that keep the raster budget (LayerRasterizer::maximumEntries 4096,
// shared by every owner) and the frame path honest:
//  - Only layers marked animatable() may be animated, and only numerically
//    (translation, scale, rotation, opacity, tint, wipe). Animating any other
//    layer throws std::logic_error: decide the surface split at build time.
//  - A colour that animates is a TINT: the layer's content is authored in
//    white and the model tint colours it (the renderer multiplies in linear
//    light; DrawObject::linearTint). Where the Mac cross-fades two copies of
//    a glyph only to change its colour, one tinted surface is the default
//    (record the substitution in the port report if it is visible).
//  - Layers that show only on hover (sheen, tint flashes) are SurfaceRole::lazy:
//    painted when they first show, released after they fade (Clipboard's
//    lazy feedback, v1.2.0 native/clipboard_scene.hpp).
//  - Hit testing uses MODEL geometry (CALayer.convert uses the model tree),
//    never sampled poses: hits are stable during motion, as on the Mac.
#include "modules/hud_design_motion.hpp"
#include "modules/hud_design_nodes.hpp"
#include <deque>
#include <set>
#include <memory>

namespace endfield::modules::design {

// 2D affine (CGAffineTransform order: x' = a x + c y + tx, y' = b x + d y + ty).
struct Affine {
    double a{1}, b{}, c{}, d{1}, tx{}, ty{};
    static Affine translation(double x, double y) noexcept { return {1, 0, 0, 1, x, y}; }
    static Affine scale(double x, double y) noexcept { return {x, 0, 0, y, 0, 0}; }
    static Affine rotation(double radians) noexcept;
    static Affine skew(double m21) noexcept { return {1, 0, m21, 1, 0, 0}; } // the NEW badge: m21 = tan(-8 deg)
    Point apply(Point) const noexcept;
    std::optional<Affine> inverse() const noexcept;
    bool operator==(const Affine&) const = default;
};
Affine operator*(const Affine&, const Affine&) noexcept;

// Animatable model channels. A layer's presented transform is
//   T(position) * T(translateX, translateY) * base * R(rotation) * S(scaleX, scaleY) * T(-anchor x bounds)
// i.e. CA's transform.translation.* in the parent's space, rotation and scale
// about the anchor (HUDLayer transform key paths as the Mac uses them).
enum class Channel : std::uint8_t { translateX, translateY, scaleX, scaleY, rotation, opacity, wipe, count };
inline constexpr std::size_t channelCount = std::size_t(Channel::count);
// The Mac animation keys a layer can hold at once (later-started absolute
// tracks override earlier ones; additive tracks add to the result).
enum class MotionSlot : std::uint8_t { enter, leave, hold, wipe, fade, pop, grow, effect, count };
enum class WipeOrigin : std::uint8_t { left, top, centerX };

// An invisible hover/press target (HUDControlHighlightLayer.addTarget, Sources/
// HUDControlHighlightLayer.swift:94-115) in its host layer's coordinates.
// The highlight pass (hud_design_control.hpp updateHighlights) walks the
// layer tree in paint order, so a target's rank is its host's place in it.
struct HitTarget {
    Rect rect;
    // nullopt: the rect; chamfer / ellipse otherwise, in the TARGET's own
    // coordinates (origin = rect's origin), as HUDControlHighlightLayer.controlPath.
    std::optional<PathBuilder> path;
    bool container{};                  // row plates, cards, scroll strips
    bool enabled{true}, hidden{};
    bool highlighted{}, pressed{};     // last reported state
    std::function<void(bool highlighted, bool pressed)> onChange;
    bool contains(Point local) const;  // `local` in the host layer's coordinates
};

class LayerArena;
class Layer final {
public:
    // Only LayerArena::make constructs layers (passkey).
    class Key { Key() = default; friend class LayerArena; };
    Layer(Key, LayerArena&, std::string id);
    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;
    const std::string& id() const noexcept { return id_; }

    // --- Model geometry (CALayer) -------------------------------------------
    Size bounds() const noexcept { return bounds_; }
    Point position() const noexcept { return position_; }
    Point anchorPoint() const noexcept { return anchor_; }
    Rect frame() const noexcept;              // untransformed frame in the parent
    void setFrame(Rect);                      // keeps the anchor point
    void setBounds(Size);
    void setPosition(Point);
    void setAnchorPoint(Point);
    void setOrigin(Point);                    // frame.origin = p
    const Affine& baseTransform() const noexcept { return base_; }
    void setBaseTransform(const Affine&);     // static rotation/skew (up/down chevrons, stamps)
    double zPosition() const noexcept { return z_; }
    void setZPosition(double);
    bool hidden() const noexcept { return hidden_; }
    void setHidden(bool);
    // masksToBounds: clips descendants to bounds (circular cornerRadius) as a
    // plane mask on every descendant surface; hit tests skip clipped points
    // (isUnclipped, HUDDesignScreen.swift:663-672). At most 8 masks per draw.
    bool masksToBounds() const noexcept { return clips_; }
    void setMasksToBounds(bool, double cornerRadius = 0);
    double maskCornerRadius() const noexcept { return clipRadius_; }

    // --- Model channel values (end state) -----------------------------------
    double value(Channel) const noexcept;     // defaults: scale 1, opacity 1, others 0
    void setValue(Channel, double);           // immediate, cancels that channel's property track
    std::optional<Color> tint() const noexcept { return tint_; }
    void setTint(std::optional<Color>);       // immediate; animatable layers only (throws otherwise)

    // --- Content (this layer's own drawing, below its sublayers) -------------
    void setContent(Json node);               // a node from hud_design_nodes (its frame = bounds)
    const std::optional<Json>& content() const noexcept { return content_; }
    void setText(TextBlock);                  // HUDTextLayer: frame follows block.layerFrame()
    // Null when the content is not text. The mutable accessor counts as a
    // content change (the text may be edited through it); the frame does not
    // follow such edits (call setText for a new line box).
    TextBlock* text() noexcept;
    const TextBlock* text() const noexcept;
    void clearContent();
    std::uint64_t contentRevision() const noexcept { return contentRevision_; }

    // --- Tree ----------------------------------------------------------------
    Layer* parent() const noexcept { return parent_; }
    const std::vector<Layer*>& children() const noexcept { return children_; }
    void add(Layer&);                         // appended on top (addSublayer)
    void insert(Layer&, std::size_t index);   // insertSublayer(at:)
    void insertBelow(Layer&, const Layer& sibling);
    void removeFromParent() noexcept;
    bool isDescendant(const Layer& ancestor) const noexcept;

    // --- Surface policy --------------------------------------------------------
    void setAnimatable(SurfaceRole = SurfaceRole::animated);
    bool animatable() const noexcept { return role_ != SurfaceRole::staticContent; }
    SurfaceRole role() const noexcept { return role_; }
    // A part root: its subtree becomes one Part (reuse unit). emit() makes a
    // Part of its root and of every layer marked here; the canvas marks the
    // direct children of its page, header and top layer, scroll areas their
    // rows, and a module may mark a large composite explicitly.
    void setPartRoot(bool);
    bool partRoot() const noexcept { return partRoot_; }
    // Clip this (lazy) layer's surfaces to the alpha of `source`'s own
    // surface: the sheen band inside its chamfered plate (LIB-28). The native
    // scene binds source's texture as the PlaneAlphaMask (no extra raster).
    void setAlphaClip(const Layer* source) noexcept { alphaClip_ = source; }
    const Layer* alphaClip() const noexcept { return alphaClip_; }

    // --- Interaction (model geometry only) -----------------------------------
    HitTarget& addTarget(Rect, std::optional<PathBuilder> path, bool container, std::function<void(bool, bool)> onChange);
    std::span<const std::unique_ptr<HitTarget>> targets() const noexcept { return targets_; }
    // HUDDecorativeContentLayer: the highlight pass skips this subtree.
    void setDecorative(bool v) noexcept { decorative_ = v; }
    bool decorative() const noexcept { return decorative_; }

    // --- Motion storage (sampled by samplePoses; started by motion::*) --------
    // Presented value of a channel: the model, overridden by the last-added
    // active absolute track (Track::serial), plus every active additive track
    // added after it (Core Animation's application order).
    std::array<Track, channelCount> property{};              // "hud.<keyPath>" absolute tracks
    std::array<std::array<Track, 4>, std::size_t(MotionSlot::count)> slots{}; // channel per entry below (pop uses 4)
    std::array<std::array<Channel, 4>, std::size_t(MotionSlot::count)> slotChannels{};
    ColorTrack tintTrack{}, tintEffect{};
    WipeOrigin wipeOrigin{WipeOrigin::left};
    double presented(Channel, double now) const noexcept;    // model + tracks
    std::optional<Color> presentedTint(double now) const noexcept;
    bool animating(double now) const noexcept;
    void removeMotion(MotionSlot) noexcept;
    void removeAllMotion() noexcept;

    LayerArena& arena() const noexcept { return *arena_; }
private:
    friend class LayerArena;
    LayerArena* arena_;
    std::string id_;
    Size bounds_;
    Point position_, anchor_;
    Affine base_;
    double z_{}, clipRadius_{};
    bool hidden_{}, clips_{}, partRoot_{};
    std::array<double, channelCount> model_{0, 0, 1, 1, 0, 1, 1};
    std::optional<Color> tint_;
    std::optional<Json> content_;
    std::unique_ptr<TextBlock> text_;
    std::uint64_t contentRevision_{};
    SurfaceRole role_{SurfaceRole::staticContent};
    const Layer* alphaClip_{};
    Layer* parent_{};
    std::vector<Layer*> children_;
    std::vector<std::unique_ptr<HitTarget>> targets_;
    bool decorative_{};
    void touch() noexcept;
};

// Stable-address owner of a group of layers (one per page, header, menu,
// pop-over, overlay generation). Destroying it detaches its layers from
// parents owned by other arenas (the slider bubble hosted in the canvas's top
// layer) and cancels its deadline token: nothing dangles across a rebuild.
class LayerArena final {
public:
    explicit LayerArena(std::string idPrefix, Deadlines* = nullptr);
    ~LayerArena();
    LayerArena(const LayerArena&) = delete;
    LayerArena& operator=(const LayerArena&) = delete;
    Layer& make(std::string_view id);          // id = prefix + "." + id; unique within the arena
    std::size_t size() const noexcept { return layers_.size(); }
    // Bumps on every CONTENT change of its layers (content, text, structure,
    // frames and static opacity/hidden of non-animatable layers): the canvas
    // re-emits only when an arena's revision moved. Channel values and tracks
    // of animatable layers are pose-only and never bump it.
    std::uint64_t revision() const noexcept { return revision_; }
    Deadlines::Token token() const noexcept { return token_; }
    Deadlines* deadlines() const noexcept { return deadlines_; }
private:
    friend class Layer;
    std::string prefix_;
    std::deque<Layer> layers_;
    std::set<std::string, std::less<>> ids_;
    Deadlines* deadlines_{};
    Deadlines::Token token_{};
    std::uint64_t revision_{};
};

// ---------------------------------------------------------------------------
// HUDMotion verbs (HUDDesignMotion.swift :33-184). `now` is the owner's
// event time; `reduce` the context's Reduce Motion. All but hold() skip the
// animation under Reduce Motion (the model already holds the end state).
// ---------------------------------------------------------------------------
namespace motion {
struct Clock { double now{}; bool reduceMotion{}; Deadlines* deadlines{}; };
// HUDMotion.animate: sets the model to `to` and animates from the presented
// value (no track when reduced, duration <= 0, or from == to).
void animate(Layer&, Channel, double to, double duration, const Clock&, core::CubicTiming = easing::standard, double delay = 0);
void animateTint(Layer&, Color to, double duration, const Clock&, core::CubicTiming = easing::standard, double delay = 0);
// stIn: opacity [0, o, o] at [0, .6, 1] and additive translation offset -> 0;
// group timing `timing` over `duration` composes with the translation's own.
void enter(Layer&, Point offset, const Clock&, double duration = duration::entrance, double delay = 0, core::CubicTiming timing = easing::standard);
// The list stagger: enter(layers[i], offset, delay staggerDelay(i)).
void stagger(std::span<Layer* const>, Point offset, const Clock&);
// .13 s ease to opacity 0 and additive -0.6 x offset; holds its end state.
void leave(Layer&, Point toward, const Clock&);
// Keeps a layer transparent until released; applies under Reduce Motion too.
void hold(Layer&);
void release(Layer&, MotionSlot);
// clip-path wipe: a mask grows from `origin` (scaleX for left/centerX, scaleY
// for top) and is REMOVED when done (no settled masking); optional opacity
// fade from fadeFrom x opacity. `completion` (if any) runs at the end.
void wipe(Layer&, WipeOrigin, double duration, const Clock&, core::CubicTiming = easing::standard, double delay = 0,
    std::optional<double> fadeFrom = {});
void removeWipe(Layer&);
// ruleIn: scaleX 0 -> 1 from the layer's anchor.
void grow(Layer&, double duration, const Clock&, double delay = 0, core::CubicTiming = easing::standard);
// pop: scale .4 -> 1, additive rotation -12 deg -> 0, opacity 0 -> o, SNAP group.
void pop(Layer&, const Clock&, double duration = duration::pop, double delay = 0);
// Keyframe effects (shake, pulse, blink, scan): one channel, linear between
// keys on `timing`, `repeat` cycles (infinity: until removed), additive or not.
void keyframes(Layer&, Channel, std::span<const double> values, std::span<const double> keyTimes, double duration,
    const Clock&, core::CubicTiming timing = easing::linear, double repeat = 1, bool additive = false, double delay = 0);
void colorKeyframes(Layer&, std::span<const Color> values, std::span<const double> keyTimes, double duration,
    const Clock&, core::CubicTiming timing, double repeat, double delay = 0);
// A completion block (CATransaction completion): runs at `time` unless the
// layer's arena died first.
void after(Layer&, double time, const Clock&, std::function<void()>);
}

// ---------------------------------------------------------------------------
// Emission and sampling (the bridge to native/hud_design_scene.hpp)
// ---------------------------------------------------------------------------
struct SurfaceBinding {
    const Layer* placement{};   // the boundary (or part root) layer whose presented world places it
    std::size_t part{}, surface{};
    SurfaceRole role{SurfaceRole::staticContent};
    const Layer* alphaClip{};
};
struct Artwork {
    const Layer* root{};                  // the emitted root: poses are in its local space
    std::vector<Part> parts;              // paint order
    std::vector<SurfaceBinding> bindings; // one per surface, paint order
    std::vector<const Layer*> animatable; // every boundary layer (requiresFrames scan)
};
struct EmitOptions { const TextServices* text{}; std::string partPrefix; };
// Emits `root`'s subtree (paint order: children stably by zPosition, a
// layer's own content below its sublayers).
//  - Parts: one for `root` and one per partRoot() layer, id partPrefix +
//    layer id. A part root met inside another part ends it there; the outer
//    part continues after it as "<part id>~N" (paint order across scenes).
//    Parts left without surfaces are dropped.
//  - Placements: the root, part roots and animatable layers. Each places
//    runs of its own content plus its static descendants, authored in the
//    placement's local space: the first run of an animatable layer is the
//    surface named by its layer id, every other run "<part id>.static.N". A
//    static layer cut by a boundary is emitted as shells "<id>", "<id>~1" ...
//    (same frame, CA transform, model opacity, hidden and clip baked).
//  - A run is one group node (allowsGroupOpacity false): bounds = the union of its
//    frames (padded text, transformed shells; the placement's bounds when it
//    clips, then with its masksToBounds/cornerRadius), position = its origin.
//  - Content nodes are copied with ids "<layer id>:<id>"; text becomes a
//    textNode "<layer id>/text" at the padding offset inside the layer.
// Throws (std::invalid_argument) on text without TextServices, a repeated
// surface id in a part, more than 64 nesting levels, or a part above the
// rasterizer's 4096 nodes (LayerRasterizer::maximumNodes).
Artwork emit(const Layer& root, const EmitOptions&);

struct PoseMask { Affine world; Rect bounds; double cornerRadius{}; }; // world: mask-local -> box px
struct SurfacePose {
    Affine world;                // surface-local -> box px
    float opacity{1};            // product of presented opacities up the chain
    std::optional<Color> tint;   // sRGB; the scene converts to linear
    std::array<PoseMask, 8> masks{};
    std::uint8_t maskCount{};
    bool visible{true};          // false: hidden, transparent or clipped away
    bool operator==(const SurfacePose&) const = default;
};
// Allocation-free: `out` has one slot per binding (else invalid_argument).
// Poses are in the ROOT's local space (box px for a canvas root): the root's
// own transform is excluded, its opacity and hidden flag count. world = the
// presented transforms of the placement and its ancestors; opacity = their
// presented product; masks: every masksToBounds ancestor strictly above the
// placement (its own clip is baked into its runs) and every running wipe on
// the chain, the placement's included (at most 8, outermost first); tint:
// the placement's presented tint. A placement detached since emission, a
// hidden chain, zero opacity or a wipe at 0 gives visible = false.
void samplePoses(const Artwork&, double now, std::span<SurfacePose> out);
// One layer composed as samplePoses composes a placement's chain (no masks):
// the presented world (layer-local -> root) and the presented opacity product
// of the layer and its ancestors up to `root`; visible false when detached,
// hidden or transparent. The native scene's group opacity reads a part's
// common ancestor through this (allocation-free).
struct LayerPose { Affine world; double opacity{1}; bool visible{true}; };
LayerPose sampleLayer(const Layer&, const Layer& root, double now) noexcept;
bool requiresFrames(const Artwork&, double now) noexcept;

// Model-tree geometry for hit testing and accessibility (no motion).
Affine modelWorld(const Layer&, const Layer& root) noexcept;   // layer-local -> root
std::optional<Point> convert(Point, const Layer& from, const Layer& to) noexcept;
Rect convert(Rect, const Layer& from, const Layer& to) noexcept; // bounding rect
// CALayer.isVisible(in:): attached under root, no hidden ancestor, model opacity > .01.
bool isVisible(const Layer&, const Layer& root) noexcept;
// CALayer.isUnclipped(_:in:): the root point is inside every masksToBounds ancestor.
bool isUnclipped(const Layer&, Point rootPoint, const Layer& root) noexcept;
} // namespace endfield::modules::design
