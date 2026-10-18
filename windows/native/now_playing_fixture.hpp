#pragma once
#include "native/audio_session_worker.hpp"
#include "native/now_playing_service.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

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
// The fixture player's GSMTC-style identity (its session AppUserModelID).
inline constexpr std::string_view nowPlayingFixtureApplication="EndfieldHUD.Fixture";

// Synthetic per-app audio for the fixture player (development previews and
// hidden coverage only): ONE process route whose worker-captured
// AppUserModelID is the fixture session's, behind exactly the AudioRuntime
// members the Now Playing volume bridge uses (nowPlayingAppVolume in
// native/now_playing_volume.hpp). Like the runtime, rows exist only while Now
// Playing votes, route writes are accepted asynchronously, and publish() (the
// runtime's drain) adopts them; `notice` then asks the owner to drain, like
// the runtime's coalesced notice. It touches no audio device, mixer level,
// process or file. The Mac diagnostic fixture has no per-app route; this one
// exists so previews and coverage exercise the Windows volume bridge.
class NowPlayingAudioFixture final {
public:
    static constexpr std::string_view route="4242:133713371337133713";
    explicit NowPlayingAudioFixture(std::function<void()>notice={});
    NowPlayingAudioFixture(const NowPlayingAudioFixture&)=delete;
    NowPlayingAudioFixture&operator=(const NowPlayingAudioFixture&)=delete;
    // AudioRuntime members used by nowPlayingAppVolume.
    std::int32_t setNowPlayingVisible(bool);
    std::span<const AudioApplicationRoute>applications()const noexcept{return published_;}
    std::uint64_t applicationsRevision()const noexcept{return revision_;}
    bool setApplicationGain(std::string_view route,double gain);
    bool stopApplication(std::string_view route);
    // Adopts the vote and accepted writes; true when the published rows changed.
    bool publish();
    bool nowPlayingVisible()const noexcept{return visible_;}
    bool pending()const noexcept{return pending_;}
    std::uint64_t accepted()const noexcept{return accepted_;}
    // The synthetic process route as the worker currently holds it.
    const AudioApplicationRoute&process()const noexcept{return process_;}
private:
    void wake();
    std::function<void()>notice_;
    AudioApplicationRoute process_;
    std::vector<AudioApplicationRoute>published_;
    std::uint64_t revision_{},accepted_{};
    bool visible_{},pending_{};
};
}
