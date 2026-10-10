#pragma once
#include "native/audio_session_worker.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace endfield::native {
// One active Windows audio endpoint (render or capture). Traits come only from
// documented endpoint properties: PKEY_AudioEndpoint_FormFactor for
// headphones/headsets and PKEY_Device_EnumeratorName for Bluetooth buses.
// Nothing is inferred from a device or driver name.
struct AudioEndpoint {
    std::wstring id,name;
    bool headphones{},bluetooth{};
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
    // HRESULT-shaped: endpoint read, capture enumeration, last explicit command.
    std::int32_t error{},inputError{},commandError{};
    // Application tier.
    bool applicationsSupported{},applicationsPaused{true};
    std::vector<AudioApplicationRoute>applications;
    std::int32_t applicationError{};
    // Source PerAppAudioController.stopAll(reason:) after an output change.
    // Cleared by the next explicit per-app command.
    bool routesStoppedByDeviceChange{};
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
