#pragma once
#include "core/localization.hpp"
#include "native/volume_provider.hpp"

namespace endfield::native {
// Source AudioDeviceError messages in the five interface languages (the
// provider binding's failure text, reported by the Volume canvas as a
// Notice toast). "macOS could not complete the audio operation" is reused
// with the platform name replaced by "Windows" in every language, followed
// by the source " (code)." suffix.
VolumeProviderErrorText volumeErrorText(core::Language);
} // namespace endfield::native
