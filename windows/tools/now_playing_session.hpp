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
    // Successful transport commands only (Event Log playbackAction).
    std::function<void(const modules::NowPlayingPlaybackEvent&)>playback;
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
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
