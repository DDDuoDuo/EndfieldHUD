#pragma once
#include "core/source_animation.hpp"
#include "core/scene.hpp"

namespace endfield::core::source {
// Pure port of HUDSourceWatchCamera and HUDSourceWatchGyroMotion. The host
// supplies its one animation clock; pointer movement creates no extra timer.
struct CameraLayout {
    Vec2 canvasSize{};
    double scale{}, screenAspect{}, worldHeight{}, verticalFieldOfViewDegrees{};
};
struct CameraFrame {
    Matrix4 view, projection, worldRoot;
    CameraLayout layout;
};
struct GPUCamera {
    Matrix4 projection, viewProjection, viewNoTranslationProjection, inverseView;
    Vec3 worldPosition{};
    std::array<float,4> uiProjectionParameters{};
};
struct GyroDefinition {
    bool enabled{};
    ScalarCurve pitchCurve, yawCurve;
    double maxPitch{},maxYaw{},duration{};
    static GyroDefinition fromJson(const Json&);
    // Unity input +Y up. A Windows client point needs a single height-y flip.
    Vec3 targetEuler(Vec2 mouseUnity,Vec2 screenSize,bool detect=true) const;
    Vec3 desktopTarget(Vec2 mouseTopLeft,Vec2 screenSize,double parallax=1,double perspective=1,bool detect=true) const;
};
class SourceCamera final {
public:
    explicit SourceCamera(const Json& runtimeRoot,double referenceResolutionScale=1.25);
    const GyroDefinition& gyro() const noexcept {return gyro_;}
    const Quaternion& rootRotation() const noexcept {return rootRotation_;}
    const std::string& worldRootID() const noexcept {return worldRootID_;}
    double runtimeFieldOfView(Vec2 screenSize) const;
    CameraLayout layout(Vec2 screenSize) const;
    CameraFrame frame(Vec2 screenSize,std::optional<Quaternion> localRotation={}) const;
    CameraFrame desktopFrame(Vec2 screenSize,const Quaternion& rotation,Vec2 hudOffset={},double hudScale=1) const;
    GPUCamera gpu(const CameraFrame&) const;
    static Quaternion eulerQuaternion(Vec3 degrees);
private:
    std::string worldRootID_;
    GyroDefinition gyro_;
    Matrix4 cameraWorld_,worldParent_,view_;
    Vec3 rootPosition_{};
    Quaternion rootRotation_{};
    double fieldOfView_{},near_{},far_{},referenceScale_{};
};
class GyroMotion final {
public:
    explicit GyroMotion(Quaternion initial={0,0,0,1});
    Quaternion rotation(double time) const;
    bool retarget(Vec3 eulerDegrees,double time,double duration,bool reduceMotion=false);
    void finishIfNeeded(double time);
    void stop(double time);
    bool isAnimating() const noexcept {return animating_;}
    Vec3 lastEuler() const noexcept {return lastEuler_;}
private:
    Quaternion start_,end_;
    Vec3 lastEuler_{};
    double startedAt_{},duration_{};
    bool animating_{};
};
Matrix4 inverseSourceMatrix(const Matrix4&);
} // namespace endfield::core::source
