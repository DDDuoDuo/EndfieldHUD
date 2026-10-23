#pragma once
// The v1.2.0 source confirmation card's shared pieces (Mac HUDScaleSafetyView
// / HUDQuitConfirmationView, both deleted in 1.3.0: the layout prompt and the
// quit prompt are confirm requests on the shared bar now). They remain only
// for the Windows-only "Import macOS data" card (app/areas/mac_import_card.*),
// which keeps that card's look: its appearance, the .12 s button feedback
// curve and colour interpolation, and the card's centred plane transform.
#include "core/scene.hpp"
#include <array>

namespace endfield::modules {
struct SettingsAppearance {bool dark{true};double scale{2};std::array<double,4>accent{250./255,212./255,31./255,1};bool operator==(const SettingsAppearance&)const=default;};
using SettingsSafetyColor=std::array<double,4>;
struct SettingsSafetyButtonPaint {SettingsSafetyColor fill,stroke;double width{};bool operator==(const SettingsSafetyButtonPaint&)const=default;};
struct SettingsSafetyPose {double opacity{},travel{};bool visible{},dismissing{},animated{};};
// The card's implicit .12 s button colour / width transaction (timing .25 .1 .25 1).
double settingsSafetyFeedbackProgress(double phase);
SettingsSafetyColor settingsSafetyInterpolateColor(const SettingsSafetyColor&,const SettingsSafetyColor&,double phase);
struct SettingsSafety {
 // The card's plane: the source centre transform without its scale and position.
 static core::Matrix4 centeredSourceTransform(const core::Matrix4&,double designScale);
};
}
