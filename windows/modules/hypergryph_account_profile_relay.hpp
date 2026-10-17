#pragma once
#include "modules/hypergryph_account_controller.hpp"

// The account controller's AccountProfileSink while the Personal Profile owner
// lives in another module area. The controller is constructed first and
// applies its cached Endfield snapshot immediately (Mac order: the profile
// store already exists), so until a profile sink attaches the relay keeps the
// latest sync lock and the latest game profile update, and replays both, in
// the controller's construction order (update, then lock), when one attaches.
// Attaching null detaches; a detached relay never touches the old sink.
// Owner thread only; no allocation beyond the retained update.
namespace endfield::modules::hypergryph {
class AccountProfileRelay final:public AccountProfileSink {
public:
    // Null detaches. Replay failures (the profile could not be saved) keep
    // the update pending for a later attach and are counted, never thrown.
    void attach(AccountProfileSink*);
    AccountProfileSink* attached() const noexcept {return sink_;}
    void applyGameProfile(const GameProfileUpdate&) override;   // forwards (throws as the sink does) or keeps it pending
    void setProfileSyncLocked(bool) override;                   // forwards and remembers
    std::optional<std::string> importGameAvatar(const std::vector<std::uint8_t>&) override; // throws without a sink
    const std::optional<GameProfileUpdate>& pending() const noexcept {return pending_;}
    std::optional<bool> locked() const noexcept {return locked_;}
    unsigned replayFailures() const noexcept {return failures_;}
private:
    AccountProfileSink* sink_{};
    std::optional<GameProfileUpdate> pending_;
    std::optional<bool> locked_;
    unsigned failures_{};
};
}
