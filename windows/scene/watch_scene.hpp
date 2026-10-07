#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ehud::scene {

// CAB:path stays a string: signed Unity IDs must never pass through a double.
using SourceId = std::string;
struct Vec2 {
    double x{}, y{};
};
struct Vec3 {
    double x{}, y{}, z{};
};
struct Quaternion {
    double x{}, y{}, z{}, w{1};
};
struct Rect {
    Vec2 origin{}, size{};
    bool contains(Vec2 point, double tolerance = 1e-9) const;
};
// Column vectors, column-major storage: world = parent * T * R * S.
struct Mat4 {
    std::array<double, 16> values{};
    static Mat4 identity();
};
Mat4 operator*(const Mat4 &left, const Mat4 &right);
std::optional<Mat4> inverse(const Mat4 &matrix);

struct ScalarKey {
    double time{}, value{}, inSlope{}, outSlope{};
    int weightedMode{};
    double inWeight{1.0 / 3}, outWeight{1.0 / 3};
};
class ScalarCurve {
  public:
    explicit ScalarCurve(std::vector<ScalarKey> keys);
    double sample(double time) const;

  private:
    std::vector<ScalarKey> keys_;
};

struct Camera {
    Mat4 viewProjection{Mat4::identity()};
    Vec2 viewport{}; // client physical pixels, origin top-left
    std::optional<Vec2> project(Vec3 local, const Mat4 &world) const;
    std::optional<Vec2> pointOnPlane(Vec2 screen, const Mat4 &world) const;
    std::optional<Vec2> hit(Vec2 screen, const Mat4 &world, Rect rect) const;
};

enum class Phase { concealed, opening, visible, closing };
struct PlaybackSample {
    Phase phase{Phase::concealed};
    double entranceTime{};
    std::optional<double> ambientTime, exitTime;
    double phaseElapsed{};
};
class Playback {
  public:
    Playback(double entranceDuration, double exitDuration);
    void open(double time, bool reduceMotion = false);
    void close(double time, bool reduceMotion = false);
    void conceal();
    void showStable(double time);
    PlaybackSample sample(double time, bool reduceMotion = false, bool ambient = true);
    Phase phase() const { return phase_; }
    std::uint64_t generation() const { return generation_; }
    static double clipTime(double elapsed, double length);

  private:
    double entranceDuration_{}, exitDuration_{}, phaseStart_{}, loopStart_{};
    Phase phase_{Phase::concealed};
    std::uint64_t generation_{};
};

class GyroMotion {
  public:
    explicit GyroMotion(Quaternion initial = {});
    Quaternion rotation(double time) const;
    bool retarget(Vec3 eulerDegrees, double time, double duration, bool reduceMotion = false);
    void finishIfNeeded(double time);
    void stop(double time);
    bool animating() const { return animating_; }

  private:
    Quaternion start_{}, end_{};
    Vec3 lastEuler_{};
    double startedAt_{}, duration_{};
    bool animating_{};
};

struct FlickerSequence {
    std::vector<double> keyTimes, opacityOffsets;
    double duration{}, delay{};
    double offset(double elapsed, bool opening, bool gateVisibility = true) const;
    double opacity(double elapsed, bool opening, bool gateVisibility = true) const;
};
// Ported from HUDDeploymentFlicker. The owner seeds once per deployment and
// measures each group's settled center before the mechanical fold begins.
FlickerSequence flickerSequence(bool opening, double duration, double delay, std::uint64_t seed);
double sweepDelay(double y, double lower, double upper, bool opening, double span);

// Ordered source layer snapshots. opacity is the layer's local model opacity,
// not its inherited/reveal alpha; settledCenterY is already converted into the
// caller's coordinate space before deployment transforms change.
struct DeploymentGroup {
    SourceId id;
    std::vector<SourceId> ancestors;
    bool hidden{};
    double opacity{1}, settledCenterY{}, additionalDelay{};
};
struct DeploymentOptions {
    std::optional<double> duration;
    double delay{};
    bool gateVisibility{};
    std::optional<double> sweepSpan;
    double verticalLower{}, verticalUpper{640};
    static DeploymentOptions legacySweep(bool opening);
};
struct DeploymentTrack {
    SourceId id;
    std::size_t candidateIndex{};
    std::uint64_t seed{};
    double baseline{}, settledCenterY{}, startedAt{};
    bool opening{}, gateVisibility{};
    FlickerSequence sequence;
};
class DeploymentFlicker {
  public:
    // Replaces tracks on every supplied candidate, including filtered ones.
    // Unselected tracks remain, matching source apply(to:); cancel() clears all.
    void begin(const std::vector<DeploymentGroup> &orderedGroups, bool opening, double time,
               std::uint64_t seed, DeploymentOptions options = {}, bool reduceMotion = false);
    void cancel();
    void cancel(const std::vector<SourceId> &ids);
    double additiveOffset(std::string_view id, double time) const;
    double opacity(std::string_view id, double currentModelOpacity, double time) const;
    bool requiresFrames(double time) const;
    const std::vector<DeploymentTrack> &tracks() const { return tracks_; }
    std::uint64_t generation() const { return generation_; }

  private:
    std::vector<DeploymentTrack> tracks_;
    std::uint64_t generation_{};
};
struct DeploymentCandidate {
    SourceId nodeId;
    double settledCenterY{}, additionalDelay{};
};

struct HitRegion {
    SourceId graphicId, buttonId;
    Rect rect{};
    Mat4 world{Mat4::identity()};
    struct Mask {
        Rect rect{};
        Mat4 world{Mat4::identity()};
        Mat4 sceneWorld{Mat4::identity()};
        SourceId nodeId;
    };
    std::vector<Mask> masks;
    Mat4 sceneWorld{Mat4::identity()};
};
struct Graphic {
    SourceId nodeId, componentId, materialId, textureId;
    std::string path, kind, text;
    std::filesystem::path texturePath;
    Rect rect{};
    Mat4 world{Mat4::identity()};
    std::array<double, 4> color{1, 1, 1, 1};
    // Each quad is BL, TL, TR, BR in Unity local space. UV y is Unity +up.
    std::vector<std::array<Vec3, 4>> quads;
    std::vector<std::array<Vec2, 4>> uvQuads;
    // Original mesh vertex channels, read directly by source FX shaders. Empty
    // means white; UI's separate Canvas vertex policy is already in color.
    std::vector<std::array<std::array<double, 4>, 4>> colorQuads;
    std::vector<HitRegion::Mask> masks;
    double fontSize{24};
    std::map<std::string, double> sampledProperties;
    Mat4 sceneWorld{Mat4::identity()};
    int sortingOrder{};
    bool vertexColorReady{}; // UI Color32/tint/canvas color-space policy already applied.
    bool normalMaterial{};   // Desktop's explicit normal-alpha replacement policy.
    // Native source-plane offsets survive slant/gyro reprojection. Screen/design
    // overlays set fixedWorld so their already-projected world matrix is retained.
    Mat4 nodeLocal{Mat4::identity()};
    bool fixedWorld{};
};
struct FilledGeometry {
    std::vector<std::array<Vec3, 4>> quads;
    std::vector<std::array<Vec2, 4>> uvQuads;
};
// Exact source Image filled methods: horizontal, vertical, radial90/180/360.
FilledGeometry filledGeometry(Rect rect, std::array<Vec2, 4> uv, int method, int origin,
                              bool clockwise, double amount);
struct NodeGeometry {
    SourceId id;
    std::string path;
    std::optional<Rect> rect;
    Mat4 world{Mat4::identity()}, sceneWorld{Mat4::identity()};
    bool active{};
    int sortingOrder{};
    double localOpacity{1};
    bool locallyHidden{};
    double inheritedAlpha{1};
    std::vector<HitRegion::Mask> masks;
};
struct ScrollInfo {
    SourceId nodeId, contentId, viewportId;
    double hiddenLength{}, sensitivity{}, normalizedPosition{};
};
struct ScrollIndicator {
    int direction{}; // -1 up, +1 down, as the source desktop adapter.
    SourceId nodeId;
    Rect rect{};
    Mat4 world{Mat4::identity()}, sceneWorld{Mat4::identity()};
    std::vector<HitRegion::Mask> masks;
    // Active includes the visible-phase input guard. Disabled active arrows
    // still consume a hit; the host checks enabled/canScroll on release.
    bool active{}, enabled{};
};
struct FrameState;
struct Frame {
    Camera camera{};
    Mat4 worldRoot{Mat4::identity()};
    Vec2 canvasSize{};
    std::vector<Graphic> graphics;
    std::vector<HitRegion> hits;
    std::vector<std::string> diagnostics;
    double backdropAlpha{};
    double sceneTime{};
    std::vector<NodeGeometry> nodes;
    std::optional<ScrollInfo> scroll;
    std::vector<ScrollIndicator> scrollIndicators;
    // Current recycled physical slot -> logical right-side desktop entry.
    std::map<SourceId, std::size_t> desktopRightAssignments;
    std::optional<SourceId> buttonAt(Vec2 point) const;
    std::optional<int> scrollDirectionAt(Vec2 point) const;
    const NodeGeometry *node(std::string_view id) const;

  private:
    friend class Document;
    std::shared_ptr<const FrameState> sourceState_;
};
class ButtonMotion;
struct DesktopGraphicStyle {
    std::optional<std::array<double, 3>> tint;
    double opacity{1};
};
struct DesktopPresentation {
    // Native replacements hide only their explicit authored source graphics.
    std::vector<SourceId> hiddenNodes, normalMaterialNodes;
    std::map<SourceId, std::map<std::string, double>> properties;
    std::map<SourceId, DesktopGraphicStyle> graphicStyles;
};
struct FrameInput {
    Vec2 viewport{1920, 1080};
    PlaybackSample playback{};
    Quaternion rootRotation{};
    ButtonMotion *interaction{};
    double time{};
    bool reduceMotion{};
    double verticalNormalizedPosition{1};
    std::optional<int> panelBase;
    using IntrinsicSize = std::function<std::optional<Vec2>(const SourceId &, const Rect &)>;
    IntrinsicSize intrinsicSize;
    // Explicit selected groups only. Never inferred from Watch sprite paths.
    const DeploymentFlicker *deployment{};
    // Used only by loadDesktop; source row pool is recycled, never cloned.
    std::size_t desktopEntryCount{18};
    const DesktopPresentation *desktopPresentation{};
    // Availability follows the motion's bounded target, not rebound position.
    std::optional<double> desktopScrollTarget;
};
struct Button {
    SourceId id;
    std::string path, textId, sourceLabel;
    SourceId captionNodeId, iconNodeId;
};
struct DesktopNavigationRow {
    SourceId id;
    std::vector<SourceId> buttons;
    Vec2 anchored{}, size{};
};
struct DesktopSceneInfo {
    SourceId profileParentId, profileRootId, profileBackgroundId, profileHighlightId;
    SourceId centerNodeId, statusNodeId;
    std::map<std::string, SourceId> profileBindings;
    std::vector<SourceId> profileButtonIds, hiddenNodeIds, profileGlowIds;
    std::vector<DesktopNavigationRow> navigationRows;
    SourceId navigationContentId, navigationViewportId;
    Vec2 navigationContentSize{};
    double rowStep{}, rowScale{}, viewportHeight{};
    std::map<int, SourceId> scrollIndicatorIds;
};

class Document {
  public:
    // The callback permits a bounded EHUDZ01 resource decoder without coupling
    // the scene to a platform, compression library, timer, or real app data.
    using ResourceReader = std::function<std::string(const std::filesystem::path &)>;
    static Document load(const std::filesystem::path &sceneRoot, ResourceReader reader = {});
    static Document loadDesktop(const std::filesystem::path &sceneRoot, ResourceReader reader = {});
    const DesktopSceneInfo *desktopInfo() const;
    Frame frame(const FrameInput &input) const;
    // Retains ordered candidates and centers supplied in the source artwork's
    // coordinate space. Reads ancestry/local model opacity from this snapshot.
    std::vector<DeploymentGroup>
    deploymentGroups(const Frame &settled, const std::vector<DeploymentCandidate> &ordered) const;
    // Camera-only updates reuse source geometry and textures. A changed
    // viewport/layout, clock, content or finite clip requires a new frame.
    void reproject(Frame &frame, Quaternion rootRotation) const;
    Vec3 pointerEuler(Vec2 clientPoint, Vec2 viewport, bool detect = true) const;
    double gyroDuration() const;
    Quaternion initialRootRotation() const;
    double entranceDuration() const;
    double exitDuration() const;
    double ambientDuration() const;
    const std::vector<Button> &buttons() const;
    std::size_t nodeCount() const;

  private:
    friend class ButtonMotion;
    struct Impl;
    static Document loadWithMode(const std::filesystem::path &sceneRoot, ResourceReader reader,
                                 bool desktop);
    explicit Document(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<const Impl> impl_;
};

enum class ButtonState { normal, highlighted, pressed, selected, disabled };
// State ownership is separate from the immutable document. The caller passes
// the same visible monotonic clock used by wrapper/gyro/presentation.
class ButtonMotion {
  public:
    explicit ButtonMotion(const Document &document);
    ~ButtonMotion();
    ButtonMotion(ButtonMotion &&) noexcept;
    ButtonMotion &operator=(ButtonMotion &&) noexcept;
    ButtonMotion(const ButtonMotion &) = delete;
    ButtonMotion &operator=(const ButtonMotion &) = delete;
    void reset(double time, bool reduceMotion = false);
    void setState(ButtonState state, const SourceId &id, double time, bool reduceMotion = false);
    void setHovered(bool hovered, const SourceId &id, double time, bool reduceMotion = false);
    void setEnabled(bool enabled, const SourceId &id, double time);
    std::optional<ButtonState> state(const SourceId &id) const;
    bool requiresFrames(double time) const;
    std::uint64_t generation() const;
    std::vector<SourceId> instanceIds() const;

  private:
    friend class Document;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ehud::scene
