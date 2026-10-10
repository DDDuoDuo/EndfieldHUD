#pragma once
#include <array>
#include <cstdint>
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
private:
    std::vector<NowPlayingLyricLine>lines_;
    std::size_t firstAfter(double)const noexcept;
};
std::string nowPlayingTime(std::optional<double>);
std::optional<double>nowPlayingDisplayDeadline(const NowPlayingTrack&,double now,
    bool active,const NowPlayingLyrics*lyrics=nullptr,bool lyricsVisible=false,bool seeking=false)noexcept;
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
struct NowPlayingSession {
    std::uint64_t token{}; // unique within one visible service activation
    std::string appUserModelID,displayName;
    bool operator==(const NowPlayingSession&)const=default;
};
enum class NowPlayingCommandKind {playPause,previous,next,seek};
struct NowPlayingCommand {NowPlayingCommandKind kind{};double seconds{};};
// No mutation or OS call. Native transport must recheck the same session and
// capabilities immediately before sending the returned request.
std::optional<NowPlayingCommand>nowPlayingCommand(const NowPlayingTrack&,
    const NowPlayingCapabilities&,NowPlayingCommand)noexcept;
}
