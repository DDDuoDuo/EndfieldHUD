#pragma once
#include "native/audio_session_worker.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace endfield::native {
// One active Windows audio endpoint (render or capture). The headphone trait
// comes only from the documented PKEY_AudioEndpoint_FormFactor; nothing is
// inferred from a device or driver name. The Mac dropped its unused Bluetooth
// flag (d060bd2), so no transport trait is kept here.
struct AudioEndpoint {
    std::wstring id,name;
    bool headphones{};
    bool operator==(const AudioEndpoint&)const=default;
};
// Event Log topology entry (source AudioTopologyWatcher: identity -> label).
struct AudioTopologyDevice {
    std::wstring id,name;
    bool operator==(const AudioTopologyDevice&)const=default;
};
// Immutable value published by the audio worker and adopted on the owner
// thread. Three independent tiers follow the shared activation votes:
//   topology      any vote; device list only (Event Log, closed HUD allowed)
//   endpoints     Volume visible; default-output volume/mute/balance
//   applications  Volume or Now Playing visible; per-app routes on the
//                 default output
// The controlled output is always the Windows default console render endpoint,
// as the source always controls the macOS default output device.
struct AudioEndpointSnapshot {
    // Endpoint tier.
    bool paused{true},available{};
    std::vector<AudioEndpoint>outputs,inputs; // source order: localized name
    std::wstring defaultOutputID,defaultInputID;
    std::optional<float>volume,balance;
    std::optional<bool>muted;
    bool canSetVolume{},canSetMute{},canSetBalance{};
    // The default output / input can be switched: the default-device
    // selector is available and no switch has failed during this run.
    bool canSetDefaultOutput{},canSetDefaultInput{};
    // HRESULT-shaped: endpoint read, capture enumeration, last explicit command.
    std::int32_t error{},inputError{},commandError{};
    // Application tier.
    bool applicationsSupported{},applicationsPaused{true};
    std::vector<AudioApplicationRoute>applications;
    std::int32_t applicationError{};
    // Topology tier. A failed/incomplete enumeration keeps the previous list:
    // a read error is never reported as a disconnection. The generation
    // advances on the first complete read after the listener opens (the
    // baseline, even when empty) and on every settled change; the list is
    // cleared while the listener is closed.
    bool topologyActive{};
    bool topologyListed{}; // a complete baseline exists since the listener opened
    std::uint64_t topologyGeneration{};
    std::vector<AudioTopologyDevice>topology; // ordered by identity
    bool operator==(const AudioEndpointSnapshot&)const=default;
};
} // namespace endfield::native
