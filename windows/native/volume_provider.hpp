#pragma once
#include "native/volume_scene.hpp"
#include "native/audio_endpoint_snapshot.hpp"

namespace endfield::native {
class AudioDefaultDeviceSelector;
enum class VolumeProviderFailureKind { unavailable, unsupported, deviceChanged, invalidValue, native };
struct VolumeProviderFailure {
    VolumeProviderFailureKind kind{};
    std::int32_t status{};
    bool operator==(const VolumeProviderFailure&)const=default;
};
using VolumeProviderErrorText=std::function<std::string(VolumeProviderFailure)>;
// HRESULT-shaped provider/worker status -> source failure category:
// ERROR_DEVICE_NOT_CONNECTED/AUDCLNT_E_DEVICE_INVALIDATED -> deviceChanged,
// ERROR_NOT_SUPPORTED/E_NOTIMPL -> unsupported, E_INVALIDARG -> invalidValue,
// E_PENDING -> unavailable, anything else -> native(status).
VolumeProviderFailure volumeProviderFailure(std::int32_t status)noexcept;

// Borrowed owner-thread operations. The native factories use the shared audio
// service; the injected form never opens COM, a window, or audio.
// readSnapshot returns storage valid until the next operation. requestActive
// updates this owner's vote in the application's shared activation policy.
// Endpoint writes carry the endpoint the gesture began on, so an asynchronous
// provider can refuse a write that would land on a different device.
struct VolumeProviderAccess {
    std::function<const AudioEndpointSnapshot&()>readSnapshot;
    std::function<std::int32_t(bool)>requestActive;
    std::function<std::int32_t(std::wstring_view,float)>setVolume,setBalance;
    std::function<std::int32_t(std::wstring_view,bool)>setMute;
    std::function<std::int32_t(std::string_view,float)>setAppGain;
    std::function<std::int32_t(std::string_view)>stopApp;
    std::function<std::int32_t()>stopAllApps;
    // A real routing/default-device setter only. Selecting the endpoint whose
    // volume SystemServices controls is NOT a substitute. Missing operations
    // disable the corresponding chooser instead of claiming routing support.
    // Windows has no public API for this (see WINDOWS-MIGRATION.md platform
    // gaps), so production leaves both empty.
    std::function<std::int32_t(std::wstring_view)>setDefaultOutput,setDefaultInput;
};

// One event-only bridge, with no service, worker, timer, or audio resource.
// All methods/callbacks run on the shared service's owner thread. Deliver only
// ServiceChange::audio from its existing Changed callback (or call receive()
// after AudioService::drain() reports a change). Snapshot receivers may update
// UI synchronously, but must not issue another audio write.
// Close/destroy before borrowed service/activation owner destruction. Copied
// VolumeCallbacks become harmless after close/destruction.
class VolumeProviderBinding final {
public:
    using Receiver=std::function<void(const VolumeSnapshot&)>;
    explicit VolumeProviderBinding(VolumeProviderAccess,VolumeProviderErrorText={});
    ~VolumeProviderBinding();
    VolumeProviderBinding(const VolumeProviderBinding&)=delete;
    VolumeProviderBinding&operator=(const VolumeProviderBinding&)=delete;
    VolumeCallbacks callbacks()const;
    const VolumeSnapshot&snapshot()const noexcept;
    // Does not immediately call receiver; use snapshot() for initial content.
    void setReceiver(Receiver);
    bool receive(ServiceChange);
    bool receive(); // the shared audio snapshot changed
    // Independent of module selection: conceal suspends this owner's vote even
    // if Volume remains selected; reopening restores it without a second clock.
    bool setVisible(bool);
    bool visible()const noexcept;
    bool active()const noexcept;
    void setErrorText(VolumeProviderErrorText);
    std::optional<VolumeProviderFailure>lastFailure()const noexcept;
    void close()noexcept;
private:
    struct State;std::shared_ptr<State>state_;
};

#ifdef _WIN32
// Older SystemServices-backed access (owner-thread STA audio). Without
// optionalDefaultDevices this only captures references. With it, the factory
// additionally probes version/COM capability, never endpoint state.
// Neither form starts audio or changes routing during construction.
// The explicit activation vote is mandatory so sharing SystemServices with
// other modules cannot accidentally pause their audio observations.
VolumeProviderAccess volumeProviderAccess(SystemServices&,
    std::function<std::int32_t(bool)> sharedAudioActivation,
    std::shared_ptr<AudioDefaultDeviceSelector> optionalDefaultDevices={});
#endif
} // namespace endfield::native
