#pragma once
#include "app/utility_executor.hpp"
#include "modules/now_playing_model.hpp"
#include <functional>
#include <memory>

namespace endfield::native {
enum class NowPlayingServiceFailure {none,unavailable,accessDenied,unsupported,cancelled,stale,capacity,timedOut};
struct NowPlayingTimeline {
    // Absolute GSMTC media ticks (100ns), sampled at monotonic sampledAt after
    // accounting for LastUpdatedTime. Source's 1x Track remains unchanged.
    std::int64_t start{},end{},position{},minimumSeek{},maximumSeek{};
    double rate{1},sampledAt{};bool playing{};
    std::optional<double>elapsed(double now)const noexcept;
    std::optional<std::int64_t>seekTicks(double relativeSeconds)const noexcept;
};
struct NowPlayingMediaSnapshot {
    std::vector<modules::NowPlayingSession>sessions;
    std::optional<modules::NowPlayingSession>session;
    std::optional<modules::NowPlayingTrack>track;
    modules::NowPlayingCapabilities capabilities;
    std::optional<NowPlayingTimeline>timeline;
    std::uint64_t metadataRevision{};
    // Bounded original compressed thumbnail; decoding belongs to the existing
    // utility/image path. No substitute artwork or arbitrary URL fetch.
    std::shared_ptr<const std::vector<std::uint8_t>>artwork;
};
struct NowPlayingReadResult {NowPlayingMediaSnapshot value;NowPlayingServiceFailure failure{NowPlayingServiceFailure::none};std::int32_t nativeError{};};
struct NowPlayingCommandResult {NowPlayingServiceFailure failure{NowPlayingServiceFailure::none};std::int32_t nativeError{};};
class NowPlayingProvider {
public:
    using Changed=std::function<void()>;
    using Ready=std::function<void(NowPlayingCommandResult)>;
    using Read=std::function<void(NowPlayingReadResult)>;
    virtual ~NowPlayingProvider()=default;
    // Invoked by the borrowed utility queue. Async callbacks may arrive on any
    // thread and must run at most once; they never execute owner/UI code.
    virtual void start(Changed,Ready)=0;
    virtual void read(Read)=0;
    virtual void perform(std::uint64_t session,std::uint64_t metadataRevision,
        modules::NowPlayingCommand,Ready)=0;
    // Thread-safe, terminal, nonthrowing. Revokes events/cancels async work;
    // neither waits for completion nor starts a timer or another worker.
    virtual void stop()noexcept=0;
};
// Additive owner evidence; token is monotonic across this service lifetime.
// An accepted request may wait behind an async read/command. Only completion
// timestamps start the source's seek rollback bound, never acceptance time.
struct NowPlayingCommandReceipt {
    std::uint64_t token{},session{},metadataRevision{};
    modules::NowPlayingCommand command;
    std::optional<std::uint64_t>replacedPending;
};
struct NowPlayingCommandCompletion {
    NowPlayingCommandReceipt request;
    NowPlayingServiceFailure failure{NowPlayingServiceFailure::none};
    std::int32_t nativeError{};double completedAt{};
    std::uint64_t mediaReadRevision{};
};
struct NowPlayingServiceSnapshot {
    NowPlayingMediaSnapshot media;
    NowPlayingServiceFailure failure{NowPlayingServiceFailure::none};
    std::int32_t nativeError{};
    bool active{},busy{},fresh{};
    std::uint64_t revision{};
    std::optional<NowPlayingCommandReceipt>commandAccepted;
    std::optional<NowPlayingCommandCompletion>commandCompleted;
    // Successful accepted reads only; metadata/timestamps can remain unchanged.
    // A seek acknowledgement must come after completion.mediaReadRevision.
    std::uint64_t mediaReadRevision{};
};
class NativeNowPlayingService final {
public:
    using Factory=std::function<std::shared_ptr<NowPlayingProvider>()>;
    // notify only posts one owner message; it must not directly drain or run
    // UI from a provider/worker callback. executor outlives this owner.
    NativeNowPlayingService(app::UtilityExecutor&,Factory,std::function<void()>notify,
        std::function<double()>now,double requestTimeout=3);
    ~NativeNowPlayingService();
    NativeNowPlayingService(const NativeNowPlayingService&)=delete;
    NativeNowPlayingService&operator=(const NativeNowPlayingService&)=delete;
    void setActive(bool);
    void refresh();
    bool perform(modules::NowPlayingCommand);
    // Call after the shared utility completion/message drain. Capacity retry
    // occurs only here, never in a private timer or a busy loop.
    bool drain();
    // Host folds this one-shot bound into its existing deadline scheduler, then
    // calls drain. No periodic polling or private timer is installed.
    std::optional<double>nextWakeTime()const;
    const NowPlayingServiceSnapshot&snapshot()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
#ifdef _WIN32
// Factory construction is IO-free. Actual RequestAsync begins only in start.
// now is the host's thread-safe monotonic clock; the provider creates no clock.
NativeNowPlayingService::Factory windowsNowPlayingProvider(std::function<double()>now);
#endif
}
