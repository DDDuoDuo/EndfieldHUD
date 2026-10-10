#pragma once
#include "native/audio_endpoint_model.hpp"
#include "native/audio_session_worker.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
enum class AudioFlow { output,input };
// One documented endpoint property set, read on the audio worker.
struct AudioEndpointRecord {
    std::wstring id,name,enumerator;
    std::optional<std::uint32_t>formFactor;
    bool operator==(const AudioEndpointRecord&)const=default;
};
// Endpoint access, constructed, used and destroyed on the ONE audio worker.
// Wake callbacks may run on native notification threads: they must only set
// state and return (no locks held across calls, no COM, no owner UI).
class AudioEndpointBackend {
public:
    using Wake=std::function<void()>;
    virtual ~AudioEndpointBackend()=default;
    // Registers device add/remove/state/default/property notifications.
    virtual std::int32_t open(Wake topology)=0;
    // Unregisters every notification and releases the bound volume control.
    virtual void close()noexcept=0;
    // Active endpoints only. Never opens a stream or changes routing.
    virtual std::int32_t enumerate(AudioFlow,std::vector<AudioEndpointRecord>&)=0;
    // Console-role default; HRESULT_FROM_WIN32(ERROR_NOT_FOUND) when none.
    virtual std::int32_t defaultEndpoint(AudioFlow,std::wstring&)=0;
    // Binds IAudioEndpointVolume (+ change notification) for one render
    // endpoint; an empty identity unbinds. Rebinding the same id is a no-op.
    virtual std::int32_t bind(std::wstring_view endpoint,Wake volume)=0;
    virtual std::int32_t read(AudioEndpointVolumeState&)=0;
    virtual std::int32_t setVolume(float)=0;
    virtual std::int32_t setMute(bool)=0;
    virtual std::int32_t readChannel(unsigned,float&)=0;
    virtual std::int32_t writeChannel(unsigned,float)=0;
};
// Shared activation. Votes are owner-thread state; the worker only sees the
// resulting demand. Volume needs every tier, Now Playing needs per-app routes,
// and the Event Log needs only the device list (allowed while the HUD is
// closed, as the source keeps one device-list listener for the app lifetime).
enum class AudioVoter : unsigned { volume=1,nowPlaying=2,eventLog=4 };
struct AudioServiceOptions {
    std::function<std::unique_ptr<AudioEndpointBackend>()>endpoints;
    AudioSessionWorker::Factory sessions;
    AudioNameOrder order; // invoked on the worker; empty = portable fallback
    // Optional owned-attenuation recovery journal (see audio_route_journal.hpp).
    // Without it, per-app attenuation is not crash-recoverable.
    std::optional<std::filesystem::path>journal;
    // Deadline that coalesces journal `written` updates while dragging.
    double journalFlushSeconds{.25};
    // Source AudioTopologyWatcher: device-list notifications are coalesced
    // for 0.12 s before the Event Log list is published (the Volume lists
    // refresh immediately, as the source AudioDeviceController does).
    double topologySettleSeconds{.12};
};
struct AudioServiceStats {
    std::uint64_t wakes{},topologyReads{},volumeReads{},sessionReads{},published{},journalWrites{};
    bool workerStarted{};
};
// ONE sleeping worker for all Windows audio (endpoints, per-app sessions and
// the device list): no timer, no polling, no frame work. It sleeps on a
// condition variable and wakes only for owner commands and native
// notifications; the only timed wait is a pending journal flush deadline.
// Writes are asynchronous: true/0 means accepted; the confirmed readback
// arrives in a later snapshot. Volume, mute and balance commands carry the
// endpoint the gesture began on and are dropped if it is no longer the
// default output (a gesture is never retargeted). Newer commands of the same
// kind replace queued ones (source AudioDeviceWorker mailbox).
//
// Notice runs on the worker after a changed publication: post ONE owner
// message, then call drain() on the owner thread.
class AudioService final {
public:
    using Notice=std::function<void()>;
    AudioService(AudioServiceOptions,Notice);
    ~AudioService(); // stop()
    AudioService(const AudioService&)=delete;
    AudioService&operator=(const AudioService&)=delete;

    // Owner thread. Returns >= 0 when the demand was accepted.
    std::int32_t vote(AudioVoter,bool);
    unsigned votes()const noexcept;
    bool setVolume(std::wstring endpoint,float);
    bool setMute(std::wstring endpoint,bool);
    bool setBalance(std::wstring endpoint,float);
    bool setApplicationGain(std::string application,float);
    bool stopApplication(std::string application);
    bool stopApplications();
    // System suspend: restore every owned route (source: sleep stops routes).
    // Resume re-reads the device list once.
    bool powerSuspend();
    bool powerResume();
    // One-shot startup recovery of a previous run's journal (no-op without
    // journal entries). Opens only the journaled endpoints' session managers.
    bool recover();
    // Adopts the latest published snapshot; true when it changed.
    bool drain();
    const AudioEndpointSnapshot&snapshot()const noexcept;
    std::uint64_t revision()const noexcept;
    AudioServiceStats stats()const;
    // Restores owned routes, unregisters everything and joins the worker.
    void stop()noexcept;
private:
    class Impl;std::shared_ptr<Impl>impl_;
};
#ifdef _WIN32
// Native factories. Obtaining them performs no COM or audio call; the worker
// constructs the backends inside its own MTA.
std::function<std::unique_ptr<AudioEndpointBackend>()>nativeAudioEndpointBackendFactory();
// CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE |
// SORT_DIGITSASNUMBERS): the closest Windows equivalent of Finder ordering.
AudioNameOrder nativeAudioNameOrder();
// Production options: native backends, native ordering and the journal at
// <data root>/Audio/RouteJournal.json when a data root is supplied.
AudioServiceOptions nativeAudioServiceOptions(std::optional<std::filesystem::path> dataRoot);
#endif
} // namespace endfield::native
