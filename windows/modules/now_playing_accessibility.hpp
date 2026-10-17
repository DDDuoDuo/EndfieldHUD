#pragma once
#include "modules/now_playing_presentation.hpp"
#include <vector>

namespace endfield::modules {
// Source HUDNowPlayingInteraction/NowPlayingCanvas accessibility contract for
// a native (UIA) provider: projected buttons and sliders that never paint a
// second face. Rectangles are in the 440-point canvas; the host projects them
// exactly like pointer hit tests. Content-event only; not sampled per frame.
struct NowPlayingAccessibleButton {
    NowPlayingAction action{};std::string id,label,help;core::Rect rect;bool enabled{},selected{};
};
struct NowPlayingAccessibleSlider {
    NowPlayingSliderKind kind{};std::string id,label,help,valueText;core::Rect rect;
    std::optional<double>value;double minimum{},maximum{1},step{};
    // Seeking commits once at release; volume follows each adjustment.
    bool enabled{},continuous{};
};
struct NowPlayingAccessibility {
    std::string status; // title · artist · album, or the source's fixed "No music playing"
    std::vector<NowPlayingAccessibleButton>buttons;std::vector<NowPlayingAccessibleSlider>sliders;
};
NowPlayingAccessibility nowPlayingAccessibility(const NowPlayingPresentation&,double time);
// Increment/decrement (source accessibilityPerformIncrement): 5 s or 2%.
std::optional<double>nowPlayingAccessibleStep(const NowPlayingAccessibleSlider&,int direction)noexcept;
}
