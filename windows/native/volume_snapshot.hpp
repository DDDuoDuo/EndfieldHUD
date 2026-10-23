#pragma once
// The Volume module's model as the shared audio runtime publishes it: the
// Mac's AudioDeviceSnapshot + AudioDeviceController.statusMessage and
// PerAppAudioController's sessions / statusMessage, merged into one value
// (VolumeSnapshot), and the writes the Volume canvas makes (VolumeCallbacks).
// Portable values; the producer is native/volume_provider.hpp
// (VolumeProviderBinding over the AudioRuntime), the consumer
// modules/volume_canvas.hpp. Nothing here opens audio, COM or a window.
#include "native/audio_endpoint_snapshot.hpp"
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
struct AudioSnapshot; // native/system_services.hpp
struct VolumeDevice {
    std::string id,name;bool headphones{},bluetooth{},canBeDefaultOutput{true},canBeDefaultInput{true};
    bool operator==(const VolumeDevice&)const=default;
};
// PerAppAudioSession.State, with `direct` for an app without a session.
enum class VolumeAppState {direct,active,preparing,stopping,failed};
// Exact shared-cache descriptor. Only a successful instance-icon binding may
// set this; missing metadata/failed/type-only fallback keeps source nil icon.
struct VolumeApplicationIcon {std::string imageKey;std::uint64_t revision{};bool operator==(const VolumeApplicationIcon&)const=default;};
// An audio process: `available` is the Mac's
// perAppAudio.availability(application:output:) == nil (a route could start).
struct VolumeApplication {
    std::string id,name;std::uint32_t pid{};bool available{};VolumeAppState state{VolumeAppState::direct};
    std::optional<double> gain;std::optional<std::string> error;
    std::optional<AudioApplicationExecutable> executable;
    std::optional<VolumeApplicationIcon> icon;
    bool operator==(const VolumeApplication&)const=default;
};
struct VolumeSnapshot {
    std::vector<VolumeDevice>outputs,inputs;
    std::string outputID,inputID;std::optional<double>volume,balance;std::optional<bool>muted;
    bool canSetVolume{},canSetMute{},canSetBalance{},canSetDefaultOutput{},canSetDefaultInput{},applicationActivitySupported{};
    // Includes retained enabled routes even when their process is silent, in
    // the original source list order. No process/route inference is performed.
    std::vector<VolumeApplication>applications;
    // status: AudioDeviceController.statusMessage; applicationMessage:
    // applicationActivityMessage; perAppStatus: PerAppAudioController.statusMessage.
    std::optional<std::string>status,applicationMessage,perAppStatus;
    // Owned routes were restored because the default output changed; shown
    // as the source per-app status until the next explicit per-app command.
    bool routingStopped{};
    bool operator==(const VolumeSnapshot&)const=default;
};
struct VolumeCallbacks {
    // AudioDeviceController.start / stop (the module's activation vote).
    std::function<void(bool)>setActive;
    // Bound endpoint IDs accompany writes: callers reject a stale endpoint
    // rather than retargeting an in-flight gesture to a different device.
    std::function<bool(std::string_view,double)>setVolume,setBalance,setAppGain;
    std::function<bool(std::string_view,bool)>setMute;
    std::function<bool(std::string_view)>setDefaultOutput,setDefaultInput,stopApp;
    std::function<void()>stopAllApps;
};
// Throws std::invalid_argument for malformed text, identities, ranges or
// metadata beyond the retained budgets (16 MiB of names and paths).
void validateVolumeSnapshot(const VolumeSnapshot&);
// Pure mapping of the shared Windows provider's supported master, input,
// balance, headphone/Bluetooth and explicit relative application-route fields.
// Icons remain asynchronous shared-cache bindings supplied by the presentation
// owner. Endpoint-control selection is NOT public default-device switching.
VolumeSnapshot volumeSnapshotFromEndpoints(const AudioEndpointSnapshot&);
// Adapter for the older SystemServices audio snapshot (no Bluetooth or
// fixed-volume information; the controlled endpoint stands for the output).
AudioEndpointSnapshot audioEndpointSnapshotFromSystem(const AudioSnapshot&);
} // namespace endfield::native
