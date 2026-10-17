#pragma once
#include "core/localization.hpp"
#include "native/volume_provider.hpp"

namespace endfield::native {
// Every Volume caption resolved from the shared five-language catalog, keyed
// by the source's (English, Simplified Chinese) pair exactly as VolumeCanvas
// calls L10n.text. Resolve System to a concrete language first.
VolumeStrings volumeStrings(core::Language);
// Source AudioDeviceError messages. "macOS could not complete the audio
// operation" is reused with the platform name replaced by "Windows" in every
// language, followed by the source " (code)." suffix.
VolumeProviderErrorText volumeErrorText(core::Language);
// Accessibility-only source text (HUDVolumeInteraction / VolumeCanvas).
struct VolumeAccessibilityStrings {
    std::string status{"Adjust each audio process independently. 100% is full volume and keeps an enabled route running."};
    std::string notAdjustable{" · Not adjustable on this device"};
    std::string unavailable{"Unavailable"},centered{"Centered"};
    std::string appActive{"Adjusts only this audio process; 100% keeps its route running at full volume"};
    std::string appFailed{"Return to 100% to retry restoring normal playback"};
    std::string retryCleanup{" Return to 100% to retry cleanup."};
    bool operator==(const VolumeAccessibilityStrings&)const=default;
};
VolumeAccessibilityStrings volumeAccessibilityStrings(core::Language);
} // namespace endfield::native
