#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::modules {
inline constexpr double nowPlayingMaximumSeconds=604800;
inline constexpr std::size_t nowPlayingMaximumLyricsBytes=512*1024;
inline constexpr std::size_t nowPlayingMaximumArtworkBytes=8*1024*1024;
inline constexpr std::uint64_t nowPlayingMaximumArtworkPixels=32000000;
inline constexpr unsigned nowPlayingArtworkThumbnailSize=512;
// NowPlayingArtworkLoader (Mac 1.3.0, B11, MODS-339): covers are decoded at
// the size they are drawn (nowPlayingCoverPixelSize: the 480 px slot at the
// live render scale), 720 px until the canvas names its slot, within
// 64...2048 per side (2048 px is 16 MiB of decoded pixels).
inline constexpr unsigned nowPlayingDefaultCoverPixels=720,nowPlayingMinimumCoverPixels=64,nowPlayingMaximumCoverPixels=2048;
// pixelSize(points:scale:): ceil(points x scale - 1e-6) within the bounds;
// a non-finite or non-positive product gives the default.
unsigned nowPlayingCoverPixelSize(double points,double scale)noexcept;

// Transient Unicode metadata only; never persisted or included in Event Log.
// Construction applies the original Swift Character and numeric bounds once.
struct NowPlayingTrack {
    std::string title,artist,album;
    std::optional<double>duration,position;
    bool isPlaying{};
    double sampledAt{};
    std::optional<std::string>identifier,timedLyrics,artworkRevision;
    bool supportsSeeking{true};
    static NowPlayingTrack bounded(NowPlayingTrack);
    std::optional<double>elapsed(double now)const noexcept;
    bool sameIdentity(const NowPlayingTrack&)const;
};
struct NowPlayingLyricLine {double time{};std::string text;bool operator==(const NowPlayingLyricLine&)const=default;};
class NowPlayingLyrics final {
public:
    static std::optional<NowPlayingLyrics>parse(std::string_view lrc);
    const std::vector<NowPlayingLyricLine>&lines()const noexcept{return lines_;}
    // Borrowed three-row windows and binary-search cue lookup allocate nothing.
    std::array<std::string_view,3>window(double elapsed)const noexcept;
    std::optional<double>nextBoundary(double elapsed)const noexcept;
    // NowPlayingLyrics.currentIndex(at:) (MODS-361): the line sung at
    // `elapsed`, -1 before the first cue or for a non-finite time.
    std::ptrdiff_t currentIndex(double elapsed)const noexcept;
    // hasContent: some line has text (the lyric window shows only then, MODS-314).
    bool hasContent()const noexcept;
private:
    std::vector<NowPlayingLyricLine>lines_;
    std::size_t firstAfter(double)const noexcept;
};
// NowPlayingCanvas.time (Mac 1.3.0, MODS-319): HUDFormat.clock, zero-padded
// mm:ss with minutes past 59 ("03:34", "90:00"), capped at 604800 s; "—:—"
// for an unknown, negative or non-finite time.
std::string nowPlayingTime(std::optional<double>);
// The display clock (NowPlayingCanvas.reconcileClock): the next whole second,
// the next lyric cue while lyrics show and no seek drags, or the track's end,
// at least 1/30 s ahead; none while inactive, paused or ended. `rate` is the
// player's positive playback rate (Windows: GSMTC; 1 on the Mac, 0 unknown).
std::optional<double>nowPlayingDisplayDeadline(const NowPlayingTrack&,double now,
    bool active,const NowPlayingLyrics*lyrics=nullptr,bool lyricsVisible=false,bool seeking=false,double rate=1)noexcept;
bool nowPlayingAcknowledgesSeek(const NowPlayingTrack&observed,double requested,double sampledAt)noexcept;
bool nowPlayingArtworkMetadata(unsigned width,unsigned height,std::size_t encodedBytes)noexcept;

// Capability flags come from the selected platform session, never app-name/PID
// guesses. GSMTC supplies transport/seek; volume, lyrics and app activation are
// separate unsupported capabilities until an actual provider supplies them.
struct NowPlayingCapabilities {
    bool play{},pause{},toggle{},previous{},next{},seek{};
    bool volume{},lyrics{},activateApplication{};
    bool operator==(const NowPlayingCapabilities&)const=default;
};
// Where a session was found: a GSMTC media session, or the main window title
// of a Win32 player that publishes none (native/now_playing_players.hpp).
enum class NowPlayingSessionOrigin:std::uint8_t {mediaSession,playerWindow};
struct NowPlayingSession {
    std::uint64_t token{}; // unique within one visible service activation
    std::string appUserModelID,displayName;
    NowPlayingSessionOrigin origin{NowPlayingSessionOrigin::mediaSession};
    bool operator==(const NowPlayingSession&)const=default;
};
// What the owner publishes for the canvas (the Mac canvas reads
// controller.snapshot, controller.lyrics, artworkImage and the volume
// routes): one immutable value per content event.
struct NowPlayingViewInput {
    std::uint64_t session{};std::optional<NowPlayingTrack>track;
    NowPlayingCapabilities capabilities;bool failed{};
    // The media session's AppUserModelID or executable path (the Mac's
    // NowPlayingApplication: the track identity's source and the gestures' player).
    std::string application;
    std::shared_ptr<const NowPlayingLyrics>lyrics;
    // The decoded cover is drawn by the owner (native/now_playing_cover.hpp);
    // the canvas sees whether one exists and its identity for the cross-fade.
    bool coverAvailable{};std::uint64_t coverRevision{};
    // A native provider must explicitly supply an exact adjustable route
    // (the per-app mixer route; SMTC exposes no player volume).
    std::optional<double>volume;bool volumeAvailable{};std::string volumeRoute;
    // The source is 1x; Windows may supply its actual positive PlaybackRate.
    // Zero means unknown and holds the sampled position without fabricating it.
    double playbackRate{1};
};
// The elapsed time the display shows (NowPlayingTrack.elapsed(at:) at the
// published playback rate).
std::optional<double>nowPlayingElapsed(const NowPlayingViewInput&,double now)noexcept;
// Which controls act (NowPlayingCanvas.update / canSeek, :292-316): previous,
// play and next need a track and no failure, volume and lyrics a track, the
// seek bar a seekable track with a known duration and position. Windows also
// requires the media session's capability for each (GSMTC).
struct NowPlayingControls {bool previous{},playPause{},next{},volume{},lyrics{},seek{};};
NowPlayingControls nowPlayingControls(const NowPlayingViewInput&)noexcept;
enum class NowPlayingIntentKind {playPause,previous,next,seek,volume};
struct NowPlayingIntent {NowPlayingIntentKind kind;std::uint64_t session{};double value{};};
enum class NowPlayingCommandKind {playPause,previous,next,seek};
struct NowPlayingCommand {NowPlayingCommandKind kind{};double seconds{};};
// No mutation or OS call. Native transport must recheck the same session and
// capabilities immediately before sending the returned request.
std::optional<NowPlayingCommand>nowPlayingCommand(const NowPlayingTrack&,
    const NowPlayingCapabilities&,NowPlayingCommand)noexcept;
}
