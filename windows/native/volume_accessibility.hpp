#pragma once
#include "native/volume_scene.hpp"
#include "native/volume_strings.hpp"
#include <optional>
#include <vector>

namespace endfield::native {
// Source HUDVolumeInteraction accessibility contract for a native UIA
// provider: projected buttons and sliders that never paint a second face.
// Rectangles are in the 400x334 Volume canvas (visible part for clipped app
// rows); the host projects them exactly like pointer hit tests. Content-event
// only (rebuild on VolumeController::contentRevision), never per frame.
struct VolumeAccessibleButton {
    std::string id,label,help;core::Rect rect;bool enabled{},selected{};
    bool operator==(const VolumeAccessibleButton&)const=default;
};
struct VolumeAccessibleSlider {
    std::string id,label,help,valueText;core::Rect rect;
    std::optional<double>value;double minimum{},maximum{1};bool enabled{};
    bool operator==(const VolumeAccessibleSlider&)const=default;
};
struct VolumeAccessibility {
    std::string status; // provider/per-app status, else the source default
    std::vector<VolumeAccessibleButton>buttons;std::vector<VolumeAccessibleSlider>sliders;
    bool operator==(const VolumeAccessibility&)const=default;
};
VolumeAccessibility volumeAccessibility(const VolumeController&,const VolumeAccessibilityStrings& = {});
// accessibilityPerformIncrement/Decrement: 2% of the slider range, clamped.
// None when the slider is disabled or has no reported value.
std::optional<double>volumeAccessibleStep(const VolumeAccessibleSlider&,int direction)noexcept;
} // namespace endfield::native
