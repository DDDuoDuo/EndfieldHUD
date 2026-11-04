#pragma once
#include "core/source_camera.hpp"
#include "core/source_layout.hpp"

namespace endfield::core::source {
enum class NativeLabelKind { caption, icon };
struct NativeLabelBinding {
    std::string surfaceID,sourceNodeID,buttonID;
    NativeLabelKind kind{NativeLabelKind::caption};
    bool profileViewportClip{},rightButton{};
    // Mac 1.3.0 (HUDSourceWatchView.desktopLeftLabelIDs): the icons of the
    // four left buttons (document.buttons.prefix(4)) sit in their caption's
    // plane; this names that caption node. Empty for every other surface.
    std::string captionNodeID{};
};
struct NativeClipPlane {
    SourceRect rect;
    Matrix4 world; // already includes camera.worldRoot, exactly like Mac Hit.masks
    bool operator==(const NativeClipPlane&) const = default;
};
struct NativeLabelButtonState {
    bool actionBound{},hasHit{},expandFileShelfCaption{}; // Japanese/Korean shelf sizing only
    std::span<const NativeClipPlane> masks;
};
struct NativeLabelMask {
    Matrix4 worldToLocal;
    Rect bounds;
    bool operator==(const NativeLabelMask&) const = default;
};
struct NativeLabelPlacement {
    std::string surfaceID; // raw native layer ID; resolve renderer surface once
    Matrix4 world;
    Rect contentBounds; // a change requires local content revision; tilt does not
    float opacity{}; // zero for hidden/missing/unbound/depth-clipped content
    bool visible{};
    std::vector<NativeLabelMask> masks;
    std::vector<Point> clippedPolygon; // exact Mac intersection, not its bounding box
};
struct NativeLabelStats {
    std::uint64_t updates{},unchangedUpdates{},projectionComputations{},clipComputations{},changedPlacements{},contentBoundsChanges{};
};
// One-time source binding extraction. It follows document button.labels plus
// the Mac's two explicit supplemental buttons, verifies authored icon selection
// against the exported native container name, and resolves the mounted profile
// root via its PlayerInfo ancestor. Ambiguous/missing bindings throw.
std::vector<NativeLabelBinding> nativeLabelBindingsFromExport(const SceneDefinition&,
    const Json& mountedButtons,const Json& nativeLayers,const Json& nativeProfileBindings);

std::optional<Point> projectNativePoint(Vec3 local,const Matrix4& world,const Matrix4& viewProjection,const Rect& viewport);
Matrix4 nativeProjectiveTextTransform(const std::array<Point,4>& corners,Vec2 localSize);
std::vector<Point> nativeClipPolygon(std::span<const Point> subject,std::span<const Point> clip); // diagnostic convenience

// The scene definition and its stable node order must outlive this plan. Update
// consumes evaluated source state only: no JSON, I/O, texture/layout/raster work,
// timer or services. Identical relevant state skips all projection. Vectors are
// reserved once; the ordinary pointer path performs no allocation.
class NativeLabelPlan final {
public:
    static constexpr std::size_t maximumBindings=256,maximumButtonMasks=7;
    NativeLabelPlan(const SceneDefinition&,std::vector<NativeLabelBinding>);
    std::span<const std::string> buttonIDs() const noexcept {return buttonIDs_;}
    std::span<const NativeLabelBinding> bindings() const noexcept {return bindings_;}
    bool update(std::span<const ResolvedNode>,std::span<const double> inheritedAlpha,
                std::span<const NativeLabelButtonState>,const CameraFrame&,const Rect& viewport);
    std::span<const NativeLabelPlacement> placements() const noexcept {return placements_;}
    // The visible glyph of the icon bound to `surfaceID` in its 32-point box
    // (HUDSourceWatchView.desktopIconGlyphs: the trimmed artwork's
    // glyphFrame, or the vector path's bounds). A left icon is placed from it:
    // an 80 x 80 box centred over its caption with the glyph's visible bottom
    // desktopLeftIconGap (5) units above the caption's top. Content event; the
    // next update() reprojects. False when unchanged or not a left icon.
    bool setIconGlyph(std::string_view surfaceID,Rect glyph);
    static constexpr double leftIconSide=80,leftIconGap=5,iconBox=32;
    // A left-to-right reveal of one surface (a wipe: HUDMotion.wipe from
    // .left, e.g. the ID card's Recording-mode bar): content right of `edge`
    // (surface-local x) is clipped by one more plane mask; nullopt removes
    // it. Per frame while a wipe runs; the next update() reprojects. False
    // when unchanged or unknown.
    bool setRevealEdge(std::string_view surfaceID,std::optional<double> edge);
    NativeLabelStats stats() const noexcept {return stats_;}
private:
    struct Compiled {std::size_t node,buttonNode,button;std::optional<std::size_t> caption;std::optional<Rect> glyph;std::optional<double> reveal;};
    struct NodeSnapshot {Matrix4 world;std::optional<SourceRect> rect;bool active{};double alpha{};bool operator==(const NodeSnapshot&)const=default;};
    struct ButtonSnapshot {bool actionBound{},hasHit{},expanded{};std::vector<NativeClipPlane> masks;};
    struct ClipCache {std::vector<Point> polygon;std::vector<NativeLabelMask> masks;};
    const SceneDefinition* scene_;
    std::vector<NativeLabelBinding> bindings_;
    std::vector<Compiled> compiled_;
    std::vector<std::string> buttonIDs_;
    std::vector<std::size_t> relevantNodes_;
    std::vector<NodeSnapshot> previousNodes_;
    std::vector<ButtonSnapshot> previousButtons_;
    std::vector<ClipCache> clips_;
    std::vector<NativeLabelPlacement> placements_,staged_;
    std::vector<Point> scratchA_,scratchB_;
    Matrix4 previousViewProjection_,previousWorldRoot_;
    Rect previousViewport_;
    bool initialized_{};
    NativeLabelStats stats_;
};
} // namespace endfield::core::source
