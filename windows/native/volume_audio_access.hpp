#pragma once
#include "native/audio_service.hpp"
#include "native/volume_provider.hpp"

namespace endfield::native {
// VolumeProviderBinding access over the shared AudioService. The binding's
// activation vote is AudioVoter::volume; other owners (Now Playing, Event
// Log) vote independently on the same service. Writes are asynchronous: an
// accepted command returns S_OK and the confirmed readback arrives through
// the service's next snapshot (call binding.receive() after drain()).
// The Output and Input menus switch the Windows default device through the
// service's default-device selector; snapshot.canSetDefault* says whether it
// is available (off without the interface or after one of its calls failed).
// Construction performs no API call. The service outlives the binding.
VolumeProviderAccess volumeProviderAccess(AudioService&);
} // namespace endfield::native
