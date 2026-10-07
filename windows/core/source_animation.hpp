#pragma once
#include "core/data/json.hpp"
#include "core/motion.hpp"
#include "core/scene.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace endfield::core::source {
using Json = ehud::data::Json;
using Vec2 = std::array<double,2>;
using Vec3 = std::array<double,3>;
using Quaternion = std::array<double,4>; // x,y,z,w, exactly as the Mac source
std::optional<Quaternion> normalizedQuaternion(Quaternion value) noexcept;
Quaternion multiplyQuaternion(const Quaternion& a, const Quaternion& b) noexcept;
// Camera shortest-arc SLERP. Baked source curves do NOT use this function.
std::optional<Quaternion> slerpQuaternion(Quaternion a, Quaternion b, double fraction) noexcept;
std::optional<Matrix4> quaternionMatrix(Quaternion value) noexcept;

struct ScalarKey {
    double time{}, value{}, inSlope{}, outSlope{};
    unsigned weightedMode{};
    double inWeight{1.0/3}, outWeight{1.0/3};
};
// HUDSourceScene.swift: binary search + Hermite/weighted time-value Bezier.
// Infinite tangents mean a stepped segment; invalid sample times return null.
class ScalarCurve final {
public:
    explicit ScalarCurve(std::vector<ScalarKey> keys);
    static ScalarCurve fromJson(const Json& rawBody);
    std::optional<double> sample(double time) const noexcept;
    std::span<const ScalarKey> keys() const noexcept { return keys_; }
private:
    std::vector<ScalarKey> keys_;
};
enum class ValueKind { scalar, vector3, quaternion };
struct Value {
    ValueKind kind{ValueKind::scalar};
    std::array<double,4> components{};
    unsigned count() const noexcept;
    static Value scalar(double value);
    static Value vector(Vec3 value);
    static Value quaternion(Quaternion value);
    static Value fromJson(const Json& value);
    bool operator==(const Value&) const = default;
};
struct Key {
    double time{};
    Value value, inSlope, outSlope;
    unsigned weightedMode{};
    Value inWeight, outWeight;
};
class Curve final {
public:
    Curve(std::string group, std::string path, std::string attribute,
          std::vector<std::string> nodeIDs, std::vector<Key> keys,
          std::optional<int> classID = {}, int preInfinity = 2, int postInfinity = 2);
    static Curve fromJson(const Json& value);
    std::optional<Value> sample(double time) const noexcept;
    const std::string& group() const noexcept { return group_; }
    const std::string& path() const noexcept { return path_; }
    const std::string& attribute() const noexcept { return attribute_; }
    const std::vector<std::string>& nodeIDs() const noexcept { return nodes_; }
    std::optional<int> classID() const noexcept { return classID_; }
    std::span<const ScalarCurve> channels() const noexcept { return channels_; }
private:
    std::string group_, path_, attribute_;
    std::vector<std::string> nodes_;
    std::optional<int> classID_;
    ValueKind kind_{};
    std::vector<ScalarCurve> channels_;
};
struct Clip {
    std::string binding, id, name;
    double sampleRate{}, lastKeyTime{};
    int wrapMode{};
    std::vector<Curve> curves;
    static Clip fromJson(const Json& value);
    std::optional<double> localTime(double time) const noexcept;
};
class Library final {
public:
    static Library fromJson(const Json& value);
    explicit Library(std::vector<Clip> clips);
    std::span<const Clip> clips() const noexcept { return clips_; }
    const Clip* clip(std::string_view id) const noexcept;
    const Clip& uniqueBinding(std::string_view binding) const;
private:
    std::vector<Clip> clips_;
    std::map<std::string,std::size_t,std::less<>> indices_;
};
struct RectTransform {
    Vec2 anchorMin{}, anchorMax{}, anchoredPosition{}, sizeDelta{}, pivot{.5,.5};
};
struct Node {
    std::string id, name, path;
    std::optional<std::string> parent;
    std::vector<std::string> children;
    bool active{true};
    Vec3 position{}, scale{1,1,1};
    Quaternion rotation{0,0,0,1};
    std::optional<RectTransform> rect;
};
// Identity/transform definitions only. Rendering/layout writers still own world
// resolution; this is not an alternate scene graph or hierarchy timer.
class SceneDefinition final {
public:
    SceneDefinition(std::string rootID, std::vector<Node> nodes);
    static SceneDefinition fromJson(const Json& value);
    const Node* node(std::string_view id) const noexcept;
    const std::string& rootID() const noexcept { return root_; }
    std::span<const Node> nodes() const noexcept { return nodes_; }
private:
    std::string root_;
    std::vector<Node> nodes_;
    std::map<std::string,std::size_t,std::less<>> indices_;
};
struct TransformOverride {
    std::optional<Vec3> localPosition, localScale, anchoredPosition3D;
    std::optional<Quaternion> localRotation;
    std::optional<Vec2> sizeDelta, anchorMin, anchorMax, pivot;
    std::optional<bool> active;
    std::map<unsigned,double> positionComponents;
    bool operator==(const TransformOverride&) const = default;
};
using Overrides = std::map<std::string,TransformOverride,std::less<>>;
struct Pose {
    Overrides transforms;
    std::map<std::string,std::map<std::string,double,std::less<>>,std::less<>> properties;
    std::set<std::string,std::less<>> unboundPaths, unregisteredBindings;
    double value(std::string_view attribute, std::string_view node, double fallback) const;
    bool operator==(const Pose&) const = default;
};
// Same bound-node channel rules used by Watch and Domain. Never bind by basename.
// A supplied membership set matches the Mac resolved-base membership filter.
void applyClip(const Clip&, double time, Pose&, const SceneDefinition&,
               const std::set<std::string,std::less<>>* resolvedMembership = nullptr);

class DesktopAmbientMotion;
// Immutable evaluator. Scene/library must outlive this object and any ambient
// sampler built from it. VisibilityClock remains the only visibility lifecycle.
class WatchAnimation final {
public:
    WatchAnimation(const SceneDefinition&, const Library&);
    const Clip& entrance() const noexcept { return *entrance_; }
    const Clip& ambient() const noexcept { return *ambient_; }
    const Clip& exit() const noexcept { return *exit_; }
    const SceneDefinition& scene() const noexcept { return *scene_; }
    Pose pose(double entranceTime, std::optional<double> ambientTime, std::optional<double> exitTime,
              Vec2 canvasResolution, const Overrides& runtimeOverrides = {}) const;
    // Concealed completion samples are lifecycle events, not render frames.
    // Passing one throws instead of recreating visible source geometry.
    Pose pose(const VisibilitySample&, Vec2 canvasResolution, const Overrides& runtimeOverrides = {},
              const DesktopAmbientMotion* desktopAmbient = nullptr) const;
private:
    const SceneDefinition* scene_;
    const Clip *entrance_, *ambient_, *exit_;
};
class DesktopAmbientMotion final {
public:
    struct Channel { std::string node; const Curve* curve{}; Quaternion base{0,0,0,1}; double rate{}; };
    DesktopAmbientMotion(const WatchAnimation&, std::uint64_t seed);
    void apply(double elapsed, Pose&) const;
    std::span<const Channel> channels() const noexcept { return channels_; }
    double duration() const noexcept { return duration_; }
private:
    std::vector<Channel> channels_;
    double duration_{};
};
// Exact leaf operations of HUDSourceWatchButtonAnimation. Button state-machine
// transition selection, interrupted blend snapshots, hover enable and demand
// caching are deliberately not implemented by this staged evaluator.
std::optional<double> buttonClipTime(double time, double started, double speed,
                                     double cycleOffset, double length, bool endpoint) noexcept;
Value blendButtonValue(const Value& from, const Value& to, double fraction);
} // namespace endfield::core::source
