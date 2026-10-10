#pragma once
#include "native/now_playing_service.hpp"
#include "native/now_playing_image.hpp"
#include "modules/now_playing_presentation.hpp"
namespace endfield::tools {
// Non-rendering owner state, executable with injected providers on every host.
// The caller is the sole service activation/drain owner and supplies time.
class NowPlayingSession final {
public:
    NowPlayingSession(native::NativeNowPlayingService&,app::UtilityExecutor&,
        modules::NowPlayingAppearance,std::function<void()>changed,
        native::NativeNowPlayingArtwork::Decoder=native::decodeNowPlayingImage);
    ~NowPlayingSession();
    void setActive(bool,double time);bool active()const noexcept;
    bool utilityCompleted(double time);bool deadline(double time);
    bool dispatch(modules::NowPlayingIntent,double time);
    // After a source control changes the borrowed presentation (drag/menu).
    void presentationChanged(double time);
    std::optional<double>nextWakeTime(double time)const;
    modules::NowPlayingPresentation&presentation()noexcept;
    const modules::NowPlayingPresentation&presentation()const noexcept;
    std::shared_ptr<const native::NotesImageFrame>artwork()const noexcept;
    native::NativeNowPlayingArtwork::Stats artworkStats()const;
    bool optimisticSeek()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
