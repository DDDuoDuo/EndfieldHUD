#pragma once
#include "core/source_native_labels.hpp"
#include "core/motion.hpp"
#include <string>

namespace endfield::core::source {
enum class DesktopClockStyle { digital,split,dial,rail,stacked };
enum class DesktopWorkPhase { idle,running,paused };
struct DesktopChromeSettings {
    Rect viewport;
    double hudScale{1};Vec2 hudOffset{};
    Module module{Module::power};
    bool sourceShell{true};
};
struct DesktopChromeLayout {
    double designScale{},reportScale{};Point designOrigin,canvasPosition;double reportCenterY{};
    Rect header{270,2,600,74},footer{0,0,1000,640},footerText{275,622,450,18};
    Matrix4 designToScreen,centerSpatial,statusLocal,statusToScreen,moduleLocalToScreen;
    static DesktopChromeLayout make(const DesktopChromeSettings&,const Matrix4& sourceCenter,const Matrix4& sourceStatus);
};
struct DesktopChromeBindings {
    std::string centerNodeID,statusNodeID;
    static DesktopChromeBindings fromJson(const Json&);
};
struct DesktopChromeProjection {
    std::optional<Matrix4> center,status;
    double unitsPerPoint{};
};
// Exact HUDSourceWatchView center calibration/status homographies. Borrowed
// definitions remain immutable. Only viewport changes run neutral calibration;
// ordinary input updates visit the two explicitly exported source nodes.
class DesktopChromeProjectionPlan final {
public:
    DesktopChromeProjectionPlan(const SceneDefinition&,const SourceCamera&,const WatchAnimation&,DesktopChromeBindings);
    bool update(std::span<const ResolvedNode>,const CameraFrame&,Rect viewport);
    const DesktopChromeProjection& projection()const noexcept{return result_;}
private:
    const SceneDefinition* scene_;const SourceCamera* camera_;const WatchAnimation* animation_;
    std::size_t center_{},status_{};DesktopChromeProjection result_;
    std::optional<Rect> calibrationViewport_,centerViewport_,statusViewport_;
    std::optional<Matrix4> centerWorld_,statusWorld_;std::optional<SourceRect> statusRect_;
};
enum class ChromeTextAlignment { left,center,right };
enum class ChromeFontRole { monospacedSystem,monospacedDigitSystem };
struct ChromeTextPlacement {
    Rect frame;double fontSize{};ChromeTextAlignment alignment{ChromeTextAlignment::right};
    ChromeFontRole font{ChromeFontRole::monospacedSystem};bool semibold{},hidden{};std::string text;
};
struct ChromePathElement {
    enum class Kind { move,line,ellipse,rectangle };
    Kind kind{};Rect geometry; // move/line use x,y; ellipse/rectangle use all four
    bool operator==(const ChromePathElement&)const=default;
};
struct DesktopClockReading {std::string time,date;bool operator==(const DesktopClockReading&)const=default;};
struct DesktopClockArtwork {
    ChromeTextPlacement time,date,seconds;
    std::vector<ChromePathElement> instrument,hands,selection;
    Rect body{0,0,340,122},hitBounds{0,0,340,145},pageClip{14,12,312,75},pageFrame{-14,-12,340,122};
    Rect badge{22,89,296,20};double badgeFontSize{14};
};
// No wall clock or formatter. Host supplies the original POSIX-style formatted
// strings once per wall-clock sample; pointer projection never rebuilds art.
class DesktopClockArtworkPlan final {
public:
    DesktopClockArtworkPlan();
    bool update(DesktopClockStyle,const std::optional<DesktopClockReading>&);
    const DesktopClockArtwork& artwork()const noexcept{return artwork_;}
    static Rect indicatorRect(unsigned index);
    static std::optional<unsigned> indicatorAt(Point);
    static std::string_view workBadge(DesktopWorkPhase)noexcept;
    static std::string footerText(std::string_view uppercaseShortcut,std::string_view localizedClose);
private:
    DesktopClockArtwork artwork_;std::optional<DesktopClockStyle> previousStyle_;std::optional<DesktopClockReading> previousReading_;
};
struct DesktopChromeTiming {
    static constexpr CubicTiming fadeCurve{.20,.72,.22,1};
    static constexpr CubicTiming styleCurve{.42,0,.58,1};
    static constexpr double openingDelay=.20,openingDuration=.24,closingDelay=.35,closingDuration=.06,styleDuration=.26;
    static double opacity(bool opening,double elapsed,double capturedOpacity=1,bool reduceMotion=false)noexcept;
    static bool styleMovesForward(unsigned oldIndex,unsigned newIndex)noexcept;
    // CATransition push/.fromRight or .fromLeft applies only to the inner
    // clipped page. Pixel displacement is not asserted without a CA oracle.
};
} // namespace endfield::core::source
