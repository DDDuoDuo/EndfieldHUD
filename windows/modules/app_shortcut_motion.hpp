#pragma once
#include "modules/app_shortcut_state.hpp"
namespace endfield::modules {
struct ShortcutScreenSample {core::Matrix4 incoming,outgoing;double phase{},eased{};bool active{};};
// Exact source endpoint/component interpolation, verified against detached CA.
// Masks are separate original CA curve samples: interpolating the four authored
// quad vertices is NOT equivalent to Core Animation's normalized path morph.
double shortcutScreenProgress(double normalizedTime);
ShortcutScreenSample shortcutScreenSample(const ShortcutScreenTransition&,double time);
core::Matrix4 shortcutPresetTransform(const ShortcutPresetTransition&,double time);
double shortcutPresetProgress(const ShortcutPresetTransition&,double time);
}
