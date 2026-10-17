#pragma once
#include "native/now_playing_service.hpp"
#include "native/now_playing_image.hpp"
#include "native/now_playing_web.hpp"
#include "modules/now_playing_presentation.hpp"
#include "modules/now_playing_lyrics_sources.hpp"
#include "modules/now_playing_volume.hpp"
namespace endfield::tools {
struct NowPlayingSessionOptions {
    native::NativeNowPlayingArtwork::Decoder decode{native::decodeNowPlayingImage};
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
    // Square size of catalog cover downloads (source: 600; the Windows owner
    // passes modules::nowPlayingCatalogCoverPixels of its display density).
    unsigned coverPixels{600};
};
// What the owner keeps resident for Now Playing (bytes), for the memory audit.
struct NowPlayingSessionMemory {
    std::size_t recentCovers{},recentCoverBytes{}; // compressed catalog covers kept by URL
    std::size_t coverBytes{};                      // the current cover when not among them
    std::size_t artworkBytes{};                    // decoded thumbnails (current and cache)
};
// Non-rendering owner state, executable with injected providers on every host.
// The caller is the sole service activation/drain owner and supplies time.
class NowPlayingSession final {
public:
    NowPlayingSession(native::NativeNowPlayingService&,app::UtilityExecutor&,
        modules::NowPlayingAppearance,std::function<void()>changed,
        native::NativeNowPlayingArtwork::Decoder=native::decodeNowPlayingImage);
    NowPlayingSession(native::NativeNowPlayingService&,app::UtilityExecutor&,
        modules::NowPlayingAppearance,std::function<void()>changed,NowPlayingSessionOptions);
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
    // After a source control changes the borrowed presentation (drag/menu).
    void presentationChanged(double time);
    std::optional<double>nextWakeTime(double time)const;
    modules::NowPlayingPresentation&presentation()noexcept;
    const modules::NowPlayingPresentation&presentation()const noexcept;
    std::shared_ptr<const native::NotesImageFrame>artwork()const noexcept;
    native::NativeNowPlayingArtwork::Stats artworkStats()const;
    bool optimisticSeek()const noexcept;
    // Route currently driving the volume submenu (empty when unavailable).
    const std::string&volumeRoute()const noexcept;
    const modules::NowPlayingLyricsSources&lyricsSources()const noexcept;
    NowPlayingSessionMemory memory()const;
    // A new catalog cover size (display density change); the next lookup uses it.
    void setCoverPixels(unsigned);
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
