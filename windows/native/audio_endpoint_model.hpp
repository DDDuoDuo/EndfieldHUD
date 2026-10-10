#pragma once
#include "native/audio_endpoint_snapshot.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
// Pure endpoint rules shared by the native WASAPI backend, the audio worker
// and isolated tests. No OS call, clock, thread, allocation-free guarantees
// beyond what each function states.

// EndpointFormFactor values (mmdeviceapi.h). Only Headphones and Headset are
// headphone endpoints, matching the source's explicit terminal-type check.
enum class AudioEndpointFormFactor : std::uint32_t {
    remoteNetworkDevice=0,speakers=1,lineLevel=2,headphones=3,microphone=4,
    headset=5,handset=6,unknownDigitalPassthrough=7,spdif=8,digitalAudioDisplayDevice=9,unknownFormFactor=10
};
bool audioEndpointHeadphones(std::optional<std::uint32_t> formFactor)noexcept;
// PKEY_Device_EnumeratorName of the endpoint's audio device. Bluetooth Classic
// (A2DP/HFP: BTHENUM, BTHHFENUM) and Bluetooth LE Audio (BTHLEDEVICE, BTHLE)
// buses are recognised case-insensitively; any other or missing value is not
// Bluetooth (source: transport type Bluetooth or BluetoothLE only).
bool audioEndpointBluetooth(std::wstring_view enumeratorName)noexcept;

// Raw IAudioEndpointVolume readback of the controlled output, HRESULT-shaped.
struct AudioEndpointVolumeState {
    std::int32_t status{};float scalar{};
    std::int32_t muteStatus{};bool muted{};
    std::int32_t channelStatus{};std::uint32_t channels{};float left{},right{};
    std::int32_t rangeStatus{};float minimumDecibels{},maximumDecibels{},incrementDecibels{};
    std::int32_t stepStatus{};std::uint32_t steps{};
    std::uint32_t hardwareSupport{}; // ENDPOINT_HARDWARE_SUPPORT_* mask
    bool operator==(const AudioEndpointVolumeState&)const=default;
};
struct AudioEndpointControls {
    bool available{};
    std::optional<float>volume,balance;std::optional<bool>muted;
    bool canSetVolume{},canSetMute{},canSetBalance{};
    std::int32_t error{};
    bool operator==(const AudioEndpointControls&)const=default;
};
// Capabilities from readback, never optimistic:
//  - volume: a finite scalar in [0,1]. It is settable only when the endpoint
//    reports a real dB range (maximum > minimum) with at least two steps, and
//    the endpoint has not refused a write (fixed/pass-through outputs keep
//    the source "Use this device's controls" hint).
//  - mute: settable when the mute state is readable.
//  - balance: exactly two readable channels with a nonzero peak, using the
//    source AudioVolumeMath (peak-preserving; all-zero has no balance).
AudioEndpointControls audioEndpointControls(const AudioEndpointVolumeState&,bool writeRefused=false)noexcept;
// A write status that proves the endpoint cannot take software volume.
bool audioEndpointWriteRefused(std::int32_t status)noexcept;

// Source sorts endpoints and applications by localizedStandardCompare. The
// native worker passes CompareStringEx(LINGUISTIC_IGNORECASE|
// SORT_DIGITSASNUMBERS); tests use the portable fallback below (ASCII
// case-insensitive with numeric runs compared by value).
using AudioNameOrder=std::function<int(std::wstring_view,std::wstring_view)>;
int audioNameCompare(std::wstring_view,std::wstring_view)noexcept;
// Stable: equal names keep enumeration order (the source's stable sort).
void sortAudioEndpoints(std::vector<AudioEndpoint>&,const AudioNameOrder& = {});
// Name, then identity (source: name then HAL object id).
void sortAudioApplications(std::vector<AudioApplicationRoute>&,const AudioNameOrder& = {});

// Union of active render and capture endpoints for the Event Log, ordered by
// identity. An empty identity or name, or a repeated identity with a
// different name, makes the whole read incomplete (nullopt) exactly as the
// source watcher returns without publishing.
std::optional<std::vector<AudioTopologyDevice>>audioTopology(std::span<const AudioEndpoint>outputs,std::span<const AudioEndpoint>inputs);

// SystemEventRecorder.recordTopology: the first list is a baseline and logs
// nothing; afterwards every vanished identity (in identity order) is logged as
// disconnected, then every new identity (in identity order) as connected.
// Only the human label is reported; identities never leave this object.
struct AudioTopologyEvent {
    bool connected{};std::string device; // UTF-8 label
    bool operator==(const AudioTopologyEvent&)const=default;
};
class AudioTopologyRecorder final {
public:
    std::vector<AudioTopologyEvent>receive(std::span<const AudioTopologyDevice>);
    void reset()noexcept{previous_.reset();}
    bool hasBaseline()const noexcept{return previous_.has_value();}
private:
    std::optional<std::vector<AudioTopologyDevice>>previous_;
};

// Strict UTF-16/32 -> UTF-8 (unpaired surrogates and NUL are rejected).
std::optional<std::string>audioUtf8(std::wstring_view)noexcept;
// Inverse, for UTF-8 identities stored by the recovery journal.
std::optional<std::wstring>audioWide(std::string_view)noexcept;
// Bounds shared by every endpoint snapshot producer and consumer.
inline constexpr std::size_t maximumAudioEndpoints=512;
bool validAudioEndpointSnapshot(const AudioEndpointSnapshot&)noexcept;
} // namespace endfield::native
