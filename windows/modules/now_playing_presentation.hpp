#pragma once
#include "modules/now_playing_model.hpp"
#include "core/localization.hpp"
#include "core/motion.hpp"
#include "core/scene.hpp"
#include <memory>
#include <span>

namespace endfield::modules {
using NowPlayingColor=std::array<double,4>;
struct NowPlayingAppearance {
    bool dark{true},reducedMotion{};core::Language language{core::Language::english};
    NowPlayingColor accent{.98,.87,.13,1};
    bool operator==(const NowPlayingAppearance&)const=default;
};
struct NowPlayingGeometry {
    static constexpr core::Rect canvas{0,0,440,440},cover{55,0,330,330};
    static constexpr core::Rect shade{55,250,330,80},title{69,279,302,21},artist{69,303,302,16},empty{75,184,290,23};
    static constexpr core::Rect placeholder{190,113,60,60},lyrics{55,331,330,48},seek{55,379,330,18};
    static constexpr core::Rect elapsed{55,390,70,11},duration{311,390,74,11};
    static constexpr core::Rect volumeMenu{357,218,53,180},volumeSlider{371,252,25,117};
};
enum class NowPlayingAction {lyrics,previous,playPause,next,volume};
std::string_view nowPlayingActionID(NowPlayingAction)noexcept;
struct NowPlayingActionFace {NowPlayingAction action;core::Rect rect;std::string label;bool enabled{},selected{};};
enum class NowPlayingSliderKind {seek,volume};
struct NowPlayingSliderFace {NowPlayingSliderKind kind;core::Rect rect;std::optional<double>value;double minimum{},maximum{1};bool enabled{};};
struct NowPlayingViewInput {
    std::uint64_t session{};std::optional<NowPlayingTrack>track;
    NowPlayingCapabilities capabilities;bool failed{};
    std::shared_ptr<const NowPlayingLyrics>lyrics;
    // Borrow source-prepared image separately in the native scene. A missing
    // image keeps the original outlined placeholder; no cover is synthesized.
    bool coverAvailable{};std::uint64_t coverRevision{};
    // A native provider must explicitly supply an exact adjustable route.
    // Unsupported volume still opens the source unavailable submenu.
    std::optional<double>volume;bool volumeAvailable{};
    // The source is1x; Windows may supply its actual positive PlaybackRate.
    // Zero means unknown and holds the sampled position without fabricating it.
    double playbackRate{1};
};
struct NowPlayingProgress {
    double width{},destination{},handleX{55},destinationHandleX{55},duration{};
    bool interpolates{};
};
NowPlayingProgress nowPlayingProgress(std::optional<double>elapsed,std::optional<double>duration,
    bool playing,bool dragging,bool animated,bool reduced,double advanceSeconds=1)noexcept;
struct NowPlayingVolumeGeometry {core::Rect back,face,rail,fill,handle,label;double handleOpacity{};};
NowPlayingVolumeGeometry nowPlayingVolumeGeometry(double value,bool available)noexcept;
struct NowPlayingViewPose {
    double progressWidth{},progressHandleX{55},lyricsOpacity{},lyricTranslationY{},lyricOpacityFactor{1},volumeOpacity{};
};
enum class NowPlayingIntentKind {playPause,previous,next,seek,volume};
struct NowPlayingIntent {NowPlayingIntentKind kind;std::uint64_t session{};double value{};};
struct NowPlayingPointerResult {bool handled{};std::optional<NowPlayingIntent>intent;};
// Source local controls, progress/lyric timing and finite transitions. This
// owns no provider, timer, renderer, UI callback or resource. sync is a bounded
// content event; sample/requiresFrames/nextDisplayDeadline allocate nothing.
// Metadata/cover crossfade revisions are exposed for a native retained scene.
class NowPlayingPresentation final {
public:
    explicit NowPlayingPresentation(NowPlayingAppearance={});
    bool setAppearance(NowPlayingAppearance,double time);
    void setActive(bool,double time);
    void sync(NowPlayingViewInput,double time);
    // Call only at the host's exported display deadline. true requests the
    // source's single end-of-track refresh; it is never a periodic provider read.
    bool tick(double time);
    std::optional<double>nextDisplayDeadline(double time)const noexcept;
    NowPlayingViewPose sample(double time)const noexcept;
    bool requiresFrames(double time)const noexcept;
    std::span<const NowPlayingActionFace,5>actions()const noexcept{return actions_;}
    std::array<NowPlayingSliderFace,2>sliders(double time)const noexcept;
    std::size_t sliderCount()const noexcept{return showVolume_?2:1;}
    bool containsControl(core::Point,double time)const noexcept;
    NowPlayingPointerResult pointerDown(core::Point,double time);
    std::optional<NowPlayingIntent>pointerMove(core::Point,double time);
    std::optional<NowPlayingIntent>pointerUp(double time);
    std::optional<NowPlayingIntent>perform(NowPlayingAction,double time);
    std::optional<NowPlayingIntent>setSlider(NowPlayingSliderKind,double value,double time);
    void cancelDrag()noexcept;void dismissVolume(double time);
    bool active()const noexcept{return active_;}bool dragging()const noexcept{return drag_.has_value();}
    bool capturesPointer()const noexcept{return showVolume_;}bool lyricsSelected()const noexcept{return showLyrics_;}
    bool lyricsVisible()const noexcept;
    const NowPlayingViewInput&input()const noexcept{return input_;}
    const NowPlayingAppearance&appearance()const noexcept{return appearance_;}
    const std::array<std::string,3>&lyricRows()const noexcept{return rows_;}
    const std::string&elapsedText()const noexcept{return elapsedText_;}
    const std::string&durationText()const noexcept{return durationText_;}
    const std::string&volumeText()const noexcept{return volumeText_;}
    std::uint64_t contentRevision()const noexcept{return revision_;}
    std::uint64_t titleTransitionRevision()const noexcept{return titleTransition_;}
    std::uint64_t artistTransitionRevision()const noexcept{return artistTransition_;}
    std::uint64_t coverTransitionRevision()const noexcept{return coverTransition_;}
    static constexpr double contentFadeDuration=.20,lyricsFadeDuration=.24,lyricChangeDuration=.20,volumeFadeDuration=.16;
private:
    struct Scalar {double from{},to{},began{},duration{};bool eased{};double sample(double)const noexcept;bool live(double)const noexcept;void set(double,double,double,bool);void settle()noexcept;};
    struct Drag {NowPlayingSliderKind kind;std::uint64_t session{};std::optional<NowPlayingTrack>track;};
    NowPlayingAppearance appearance_;NowPlayingViewInput input_;std::array<NowPlayingActionFace,5>actions_;
    std::array<std::string,3>rows_;std::string elapsedText_{"—:—"},durationText_{"—:—"},volumeText_{"—"};
    bool active_{},rendered_{},showVolume_{},showLyrics_{true},requestedEndRefresh_{};
    std::optional<Drag>drag_;std::optional<double>dragValue_,lastLyricPosition_;
    Scalar progress_,lyricsVisibility_,lyricMovement_,lyricOpacity_{1,1},volumeVisibility_;
    std::uint64_t revision_{},titleTransition_{},artistTransition_{},coverTransition_{};
    std::optional<double>elapsed(double)const noexcept;
    std::optional<double>playbackElapsed(double)const noexcept;
    void repaint(double);void updateProgress(double,bool);void updateLyrics(double);void updateActions();void settle()noexcept;
};
}
