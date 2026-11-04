#pragma once
#include "native/now_playing_service.hpp"
#include "native/now_playing_image.hpp"
#include "native/now_playing_web.hpp"
#include "modules/now_playing_model.hpp"
#include "modules/now_playing_lyrics_sources.hpp"
#include "modules/now_playing_volume.hpp"
namespace endfield::tools {
struct NowPlayingSessionOptions {
    // A fixed-size decoder (tests, the acceptance probe), or `decodeAt`, which
    // decodes at the size the canvas draws the cover (production:
    // native::decodeNowPlayingCover; setArtworkPixels names the size).
    native::NativeNowPlayingArtwork::Decoder decode{native::decodeNowPlayingImage};
    native::NativeNowPlayingArtwork::SizedDecoder decodeAt;
    // Public LRCLIB/NetEase lookups and catalog covers. Null mirrors the source's
    // injected backends: no network request and no NetEase catalog at all.
    // The host constructs it with the same owner wake as the service. Their
    // JSON answers are decoded on the borrowed utility executor (like covers).
    std::shared_ptr<native::NowPlayingWeb>web;
    // Playing-app volume through the shared per-app audio service. Null keeps
    // the source's unavailable submenu.
    std::shared_ptr<const modules::NowPlayingAppVolume>volume;
    // Reads NetEase Cloud Music's own queue state file (its player bar carries
    // no time labels and its progress slider no accessible value, so the HUD
    // reads the bar's played fraction; this file supplies the track's exact
    // length and song id). Null keeps the fuzzy catalog title search, which is
    // what the injected test/offline backends use.
    modules::NowPlayingPlayingListReader neteasePlayingList;
    // Successful transport commands only (Event Log playbackAction).
    std::function<void(const modules::NowPlayingPlaybackEvent&)>playback;
};
// What the owner keeps resident for Now Playing (bytes), for the memory audit.
struct NowPlayingSessionMemory {
    std::size_t recentCovers{},recentCoverBytes{}; // compressed catalog covers kept by URL
    std::size_t coverBytes{};                      // the current cover when not among them
    std::size_t artworkBytes{};                    // decoded thumbnails (current and cache)
};
// The Now Playing controller (Mac NowPlayingController): non-rendering owner
// state, executable with injected providers on every host. It publishes one
// modules::NowPlayingViewInput per content event for the canvas
// (modules/now_playing_canvas.hpp) and the decoded cover for the owner's
// cover carrier. The caller is the sole service activation/drain owner and
// supplies time. Its own clock (nextWakeTime / deadline) covers the
// provider's bounds, downloads, lyric retries, the seek rollback and, once
// per displayed second while playing, the window-title player re-read and
// the single end-of-track refresh (the canvas runs the display itself).
class NowPlayingSession final {
public:
    NowPlayingSession(native::NativeNowPlayingService&,app::UtilityExecutor&,std::function<void()>changed,
        native::NativeNowPlayingArtwork::Decoder=native::decodeNowPlayingImage);
    NowPlayingSession(native::NativeNowPlayingService&,app::UtilityExecutor&,std::function<void()>changed,NowPlayingSessionOptions);
    ~NowPlayingSession();
    void setActive(bool,double time);bool active()const noexcept;
    // After the shared utility/service owner message: drains the service, the
    // web downloads and decoded artwork, then republishes changed content.
    bool utilityCompleted(double time);bool deadline(double time);
    // After the shared per-app audio notification (volume route snapshot).
    bool audioChanged(double time);
    // Source refreshManually(): one read, then retire negative lyric/cover results.
    void refreshManually(double time);
    bool dispatch(modules::NowPlayingIntent,double time);
    std::optional<double>nextWakeTime(double time)const;
    // The published snapshot and its revision (moves on every change).
    const modules::NowPlayingViewInput&input()const noexcept;
    std::uint64_t inputRevision()const noexcept;
    std::shared_ptr<const native::NotesImageFrame>artwork()const noexcept;
    native::NativeNowPlayingArtwork::Stats artworkStats()const;
    bool optimisticSeek()const noexcept;
    // Route currently driving the volume submenu (empty when unavailable).
    const std::string&volumeRoute()const noexcept;
    const modules::NowPlayingLyricsSources&lyricsSources()const noexcept;
    NowPlayingSessionMemory memory()const;
    // controller.artworkPixelSize (MODS-332/353): the cover slot's drawn
    // pixels. Decodes follow it (the shown cover stays until its re-decode is
    // ready) and catalog downloads ask for nowPlayingCatalogCoverPixels of it.
    void setArtworkPixels(unsigned);
    unsigned artworkPixels()const noexcept;
    // While inactive: keeps only the presented cover (its warm reopen) and
    // drops the other decoded thumbnails and the recent compressed covers
    // (the disk cache answers those). true when something was released.
    bool park();
    // At most this many recently downloaded catalog covers are kept, together
    // no larger than maximumRecentCoverBytes.
    static constexpr std::size_t maximumRecentCovers=4,maximumRecentCoverBytes=2*1024*1024;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
