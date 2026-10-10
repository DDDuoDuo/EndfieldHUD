#pragma once
// Personal Profile <-> Account Linking link (area-to-area service directory,
// app/application_host.hpp). The Account area is constructed before the
// Personal Profile area (sorted fragments: account.cmake < profile.cmake) and
// destroyed after it, so it publishes this link and the profile area, which
// owns the one ProfileStore writer, attaches to it:
//
//   link_ = context.host->find<endfield::app::areas::AccountProfileLink>(
//       endfield::app::areas::AccountProfileLink::key);   // null: no Account area
//   if (link_) link_->attachProfile(&sink);
//   ProfileOwnerOptions::loaded        -> link_->profileLoaded(record.avatarFilename)
//   ProfileOwnerOptions::avatarChanged -> link_->profileAvatarChanged(filename)
//   ~ProfileArea(): if (link_) link_->attachProfile(nullptr);
//
// Keep the pointer: the Account area outlives the profile area, and the host
// clears the service directory before any area owner is destroyed, so a
// lookup from the destructor would find nothing.
//
// `sink` implements modules::hypergryph::AccountProfileSink over
// tools::ProfileOwner (setSyncLocked / acceptFromGame / importGameAvatar).
// Until a sink is attached the account keeps the latest sync lock and game
// profile update and replays both on attach (Mac order: the profile store
// exists before the account controller applies its cached snapshot), so a
// missing Personal Profile module never fails account linking. The link
// header is self-contained: the profile area includes it without linking the
// account libraries.
#include "modules/hypergryph_account_controller.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace endfield::app::areas {
class AccountProfileLink {
public:
    static constexpr std::string_view key = "account.profileLink";
    // Null detaches (before the sink is destroyed). Attaching replays the
    // current sync lock, then the latest pending game profile update.
    virtual void attachProfile(modules::hypergryph::AccountProfileSink*) = 0;
    // The loaded profile record's avatar: the known baseline (once per load).
    virtual void profileLoaded(const std::optional<std::string>& avatarFilename) = 0;
    // A later committed avatar change; one made outside game avatar sync
    // turns avatar sync off (Mac profile observer).
    virtual void profileAvatarChanged(const std::optional<std::string>& avatarFilename) = 0;

protected:
    ~AccountProfileLink() = default;
};
} // namespace endfield::app::areas
