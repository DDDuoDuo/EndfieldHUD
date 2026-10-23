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
    // The display adaptation (DesktopChromeProjectionPlan::contentScale),
    // multiplying the design scale of the design canvas and of the centre
    // plane. 1 is the reference display.
    double contentScale{1};
};
// SystemHUDView.designScale before the HUD scale: min(width / 1100,
// height / 740, 1.15) points per design point, at least .1.
double desktopBaseDesignScale(Rect viewport);
inline constexpr Rect desktopReferenceViewport{0,0,1470,956};
// The redesigned 590 x 510 px content box on the centre plane (DesignBox):
// center · T(500 − 295u, 320 − 305u) · S(u), box px to screen. The display
// adaptation is already inside `center` (its calibration), so the box keeps
// its share of the ring with no extra term. Not clipped (LIB-100).
Matrix4 designBoxWorld(const Matrix4& sourceCenter,double unit=DesignBox::baseUnit);
// Screen length of one local unit at `local` on a projective plane (the
// geometric mean of the homography's two axis scales there), e.g. the output
// pixels of ContentDimTrack::blurRadius design units at the disc centre.
double planeScale(const Matrix4& world,Point local);
struct DesktopChromeLayout {
    // reportScale/reportCenterY: the v1.2.0 placement of unported (shutter)
    // modules only; design screens use boxWorld (the Mac 1.3 host has none).
    double designScale{},reportScale{};Point designOrigin,canvasPosition;double reportCenterY{};
    Rect header{270,2,600,74},footer{0,0,1000,640},footerText{275,622,450,18};
    Matrix4 designToScreen,centerSpatial,statusLocal,statusToScreen,moduleLocalToScreen,boxWorld;
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
    // contentScale (DesktopChromeSettings::contentScale) multiplies the
    // calibrated design scale of the centre plane; 1 is the reference display.
    bool update(std::span<const ResolvedNode>,const CameraFrame&,Rect viewport,double contentScale=1);
    const DesktopChromeProjection& projection()const noexcept{return result_;}
    // The display adaptation (checklist B13, originated on Windows; the Mac's
    // HUDSourceWatchView.desktopContentScale(for:) since f2c5d80, measured on
    // the centre plane): SystemHUDView caps its design scale at 1.15 points
    // while the source watch grows with the screen, so the whole design canvas
    // grows by clamp(ratio(here) / ratio(1470 x 956), 1, 3), ratio = resting
    // points per source unit at the centre node (deployed pose, neutral
    // camera) / desktopBaseDesignScale. 1 wherever the Mac content already
    // covers the ring and when the plane cannot be measured. Content event
    // only (a resolve per new size; the last sizes are cached, as the Mac's).
    double contentScale(Rect viewport);
    // Points per source unit at the centre node in the deployed pose under the
    // neutral camera (the centre calibration's measure); nullopt off-screen.
    std::optional<double> restingPointsPerUnit(Rect viewport);
private:
    struct Resting {Rect viewport;std::optional<double> points;};
    std::array<std::optional<Resting>,4> resting_;std::size_t restingNext_{};
    const SceneDefinition* scene_;const SourceCamera* camera_;const WatchAnimation* animation_;
    std::size_t center_{},status_{};DesktopChromeProjection result_;
    std::optional<Rect> calibrationViewport_,centerViewport_,statusViewport_;double calibrationScale_{1},centerScale_{1};
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
