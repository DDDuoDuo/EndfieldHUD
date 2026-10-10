#pragma once
#include "native/now_playing_service.hpp"

namespace endfield::native {
// Source NowPlayingFixtureBackend (OverlayController diagnostic mode): an
// explicit synthetic player for isolated diagnostics and module coverage. It
// cannot read a real media session, launch a player, touch audio or perform a
// network request. Track "EndfieldHUD Fixture N" / "Local verification" /
// "Now Playing", 210 s from 42 s, source timed lyrics and a synthetic 96x96
// cover (never used as a fallback cover for a real player).
NativeNowPlayingService::Factory nowPlayingFixtureProvider(std::function<double()>now);
// The synthetic 96x96 cover as an uncompressed 32-bit BMP (decoded by WIC).
std::shared_ptr<const std::vector<std::uint8_t>>nowPlayingFixtureCover();
}
