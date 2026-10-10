#include "modules/hypergryph_account_profile_relay.hpp"
#include <stdexcept>

namespace endfield::modules::hypergryph {
void AccountProfileRelay::attach(AccountProfileSink* sink) {
    if(sink==this) throw std::invalid_argument("A profile relay cannot attach to itself");
    sink_=sink;
    if(!sink_) return;
    if(pending_) {
        try {sink_->applyGameProfile(*pending_);pending_.reset();}
        catch(const std::exception&) {++failures_;}
    }
    if(locked_) sink_->setProfileSyncLocked(*locked_);
}
void AccountProfileRelay::applyGameProfile(const GameProfileUpdate& update) {
    if(!sink_) {pending_=update;return;}
    pending_.reset();
    sink_->applyGameProfile(update);
}
void AccountProfileRelay::setProfileSyncLocked(bool locked) {
    locked_=locked;
    if(sink_) sink_->setProfileSyncLocked(locked);
}
std::optional<std::string> AccountProfileRelay::importGameAvatar(const std::vector<std::uint8_t>& png) {
    if(!sink_) throw std::runtime_error("The personal profile is not available");
    return sink_->importGameAvatar(png);
}
}
