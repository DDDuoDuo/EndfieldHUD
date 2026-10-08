#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
// Internal provider for SystemServices, not another application service. Exact
// session IDs are backend lease keys; processKey is PID + creation-time identity.
// A backend must never invent process ownership for multi-process/system audio.
struct AudioSessionRecord {
    std::string id,processKey;
    std::wstring name;
    std::uint32_t pid{};
    bool active{},controllable{};
    std::optional<float> volume;
    std::int32_t error{};
    bool operator==(const AudioSessionRecord&)const=default;
};
enum class AudioApplicationRouteState { direct,active,failed };
struct AudioApplicationRoute {
    std::string id;
    std::wstring name;
    std::uint32_t pid{};
    bool available{};
    AudioApplicationRouteState state{};
    std::optional<float>gain;
    std::int32_t error{};
    bool operator==(const AudioApplicationRoute&)const=default;
};
struct AudioSessionSnapshot {
    std::uint64_t revision{};
    bool paused{true},supported{};
    std::wstring endpoint;
    std::vector<AudioApplicationRoute>applications;
    std::int32_t error{};
    bool operator==(const AudioSessionSnapshot&)const=default;
};
// All methods, construction and destruction are called on the ONE worker.
// Wake may run on native COM callback threads and must be nonblocking. Backend
// pause unregisters notifications but retains leased volume interfaces, allowing
// identity-bound cleanup without a second service or polling. close is terminal.
class AudioSessionBackend {
public:
    using Wake=std::function<void()>;
    virtual ~AudioSessionBackend()=default;
    virtual std::int32_t open(std::wstring_view endpoint,Wake)=0;
    virtual std::int32_t resume(Wake)=0;
    virtual void pause()noexcept=0;
    virtual std::int32_t read(std::vector<AudioSessionRecord>&)=0;
    virtual std::int32_t readVolume(std::string_view,float&)=0;
    virtual std::int32_t writeVolume(std::string_view,float)=0;
    virtual void close()noexcept=0;
};
// Source-owned attenuation transaction. Direct100% does not alter the user's
// native mixer; each opted-in session remembers its original scalar. Stop only
// restores scalars still equal to this owner’s last write, preserving external
// changes observed before cleanup. Public WASAPI has no atomic compare-and-set;
// a concurrent external write between native read/write cannot be excluded.
// No OS calls, clocks, threads or persistence exist in this typed core.
class AudioSessionRoutes final {
public:
    static constexpr std::size_t maximumSessions=512,maximumApplications=512;
    AudioSessionRoutes();~AudioSessionRoutes();
    AudioSessionRoutes(AudioSessionRoutes&&)noexcept;
    AudioSessionRoutes&operator=(AudioSessionRoutes&&)noexcept;
    AudioSessionRoutes(const AudioSessionRoutes&)=delete;
    AudioSessionRoutes&operator=(const AudioSessionRoutes&)=delete;
    void update(std::span<const AudioSessionRecord>,AudioSessionBackend&);
    std::int32_t setGain(std::string_view,float,AudioSessionBackend&);
    std::int32_t stop(std::string_view,AudioSessionBackend&);
    std::int32_t stopAll(AudioSessionBackend&);
    const std::vector<AudioApplicationRoute>&applications()const noexcept;
private:class Impl;std::unique_ptr<Impl>impl_;
};
// All lifecycle/command/drain calls belong to one owner thread. Notice must not
// destroy/stop this object on its worker; post an owner message instead.
// Caller-owned lifecycle. One sleeping atomic-wait worker, bounded/coalesced
// explicit commands, no timers. The notice runs on that worker and should only
// PostMessage the owner's existing coalesced WM_APP generation. Factory injection
// lets isolated tests exercise lifecycle without activating native audio.
class AudioSessionWorker final {
public:
    using Factory=std::function<std::unique_ptr<AudioSessionBackend>()>;
    using Notice=std::function<void()>;
    explicit AudioSessionWorker(Factory,Notice={});~AudioSessionWorker();
    AudioSessionWorker(const AudioSessionWorker&)=delete;
    AudioSessionWorker&operator=(const AudioSessionWorker&)=delete;
    // Empty endpoint means unsupported, never silently follows another device.
    bool activate(std::wstring endpoint);
    bool pause();
    bool setGain(std::string application,float scalar);
    bool stopApplication(std::string application);
    bool stopApplications();
    bool takeSnapshot(AudioSessionSnapshot&);
    void stop()noexcept;
private:class Impl;std::unique_ptr<Impl>impl_;
};
#ifdef _WIN32
// Constructed on the worker MTA only. Merely obtaining the factory does not
// activate COM/audio; tests must inject their own backend rather than invoke it.
AudioSessionWorker::Factory nativeAudioSessionBackendFactory();
#endif
} // namespace endfield::native
