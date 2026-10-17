#pragma once
#include "modules/settings_presentation.hpp"
namespace endfield::modules {
struct SettingsSafetyStrings {std::string title,message,cancel,confirm;bool operator==(const SettingsSafetyStrings&)const=default;};
struct SettingsSafetyGeometry {core::Rect card,cancel,confirm;};
using SettingsSafetyColor=std::array<double,4>;
struct SettingsSafetyButtonPaint {SettingsSafetyColor fill,stroke;double width{};bool operator==(const SettingsSafetyButtonPaint&)const=default;};
double settingsSafetyFeedbackProgress(double phase);
SettingsSafetyColor settingsSafetyInterpolateColor(const SettingsSafetyColor&,const SettingsSafetyColor&,double phase);
struct SettingsSafetyPose {double opacity{},travel{};bool visible{},dismissing{},animated{};};
// Actual HUDScaleSafetyView/HUDQuitConfirmationView lifecycle. The caller's
// clock drives both deadline and finite transitions. Focus/press are local
// semantic state, with no global keyboard service or native window.
class SettingsSafety final {
public:
 explicit SettingsSafety(SettingsController&);bool refresh(double);void setReduceMotion(bool,double);void hideImmediately(double);
 void setAppearance(SettingsAppearance);SettingsSafetyButtonPaint buttonPaint(unsigned,double)const;bool feedbackAnimating(double)const;
 SettingsSafetyPose pose(double)const;bool requiresFrames(double)const;bool modal()const noexcept{return shown_;}
 const SettingsSafetyStrings&strings()const noexcept{return strings_;}std::uint64_t revision()const noexcept{return revision_;}
 int focused()const noexcept{return focused_;}std::optional<int>hovered()const noexcept{return hovered_;}std::optional<int>pressed()const noexcept{return pressed_;}
 bool pointerDown(std::optional<int>,double);bool pointerUp(std::optional<int>,double);void pointerMove(std::optional<int>,double);bool key(unsigned virtualKey,bool repeated,double);
 static SettingsSafetyGeometry geometry(core::Rect viewport);
 static core::Matrix4 centeredSourceTransform(const core::Matrix4&,double designScale);
private:
 SettingsController*controller_;SettingsSafetyStrings strings_;bool shown_{},dismissing_{},submitted_{},reduced_{};int focused_{1};std::optional<int>hovered_,pressed_;double start_{};std::uint64_t revision_{};std::optional<int>remaining_;core::Language language_{};bool position_{};
 struct ColorTrack{SettingsSafetyColor from{},to{};double start{};};struct WidthTrack{double from{},to{},start{};};struct ButtonTrack{ColorTrack fill,stroke;WidthTrack width;};
 SettingsAppearance appearance_;std::array<ButtonTrack,2>buttons_{};bool buttonsReady_{};
 void updateButtons(double,bool);void action(bool,double);void changed()noexcept{++revision_;}
};
// Source card artwork includes real detached NSButton title-cell rectangles.
// The dim surround is provided by the native exact-opacity decomposition.
SettingsArtworkPart prepareSettingsSafetyArtwork(const SettingsSafety&,core::Rect viewport,const SettingsAppearance&,std::optional<double> feedbackTime={});
}
