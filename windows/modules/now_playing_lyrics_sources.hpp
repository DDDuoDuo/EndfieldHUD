#pragma once
#include "modules/now_playing_model.hpp"
#include <functional>
#include <map>
#include <memory>
#include <span>

namespace endfield::modules {
// Source NowPlayingSource. Mac derives it from the player's bundle identifier;
// Windows derives it from the GSMTC SourceAppUserModelId. Unknown players are
// `system`, exactly like the source's automatic MediaRemote snapshot.
enum class NowPlayingSource {music,spotify,netease,qqMusic,kugou,system};
std::string_view nowPlayingSourceKey(NowPlayingSource)noexcept;
// Win32 identities ("Spotify.exe", case-insensitive, optional directory) and
// packaged family prefixes. Never inferred from window titles or metadata.
NowPlayingSource nowPlayingSourceForAppUserModelID(std::string_view)noexcept;

// Source Event Log payload for a successfully completed transport command.
// Only these two values are recorded; title/artist/album never are.
struct NowPlayingPlaybackEvent {
    std::string_view action; // playPause|previous|next|seek
    NowPlayingSource source{NowPlayingSource::system};
    bool operator==(const NowPlayingPlaybackEvent&)const=default;
};
// EventKind::playbackAction metadata exactly as the source records it:
// {"action": <action>, "source": <NowPlayingSource raw value>} (same map type
// as modules::EventMetadata).
std::map<std::string,std::string,std::less<>>nowPlayingPlaybackMetadata(const NowPlayingPlaybackEvent&);

using NowPlayingBody=std::shared_ptr<const std::vector<std::uint8_t>>;
// NowPlayingLyricsDownload.maximumBytes; also bounds catalog JSON.
inline constexpr std::size_t nowPlayingMaximumLyricsResponseBytes=1024*1024;

// NowPlayingTrackMatcher: conservative public catalog/lyric matching. Output
// strings are NFC so byte equality equals Swift String (canonical) equality.
struct NowPlayingTrackMatcher {
    // Case-, width- and (Latin/Greek/Cyrillic) diacritic-insensitive folding
    // followed by the Foundation alphanumerics (L*, M*, N*) filter.
    static std::string normalized(std::string_view);
    // Split on "/,、;&", normalized, empty entries dropped; sorted and unique.
    static std::vector<std::string>artists(std::string_view);
    static std::optional<int>score(std::string_view title,std::span<const std::string>aliases,
        std::span<const std::string>artists,std::string_view album,std::optional<double>duration,
        const NowPlayingTrack&target);
};

// Swift String equality (canonical equivalence), with a byte fast path.
bool nowPlayingCanonicalEqual(std::string_view,std::string_view);
bool nowPlayingLyricsEqual(const NowPlayingLyrics*,const NowPlayingLyrics*);

// URLComponents.queryItems value encoding (macOS 15 Foundation, oracle-pinned).
std::string nowPlayingQueryValue(std::string_view);
std::optional<std::string>nowPlayingLyricsRequestURL(const NowPlayingTrack&);
std::optional<std::string>nowPlayingLyricsSearchURL(const NowPlayingTrack&);
std::optional<NowPlayingLyrics>nowPlayingDecodeLyricsResponse(std::span<const std::uint8_t>);
std::optional<NowPlayingLyrics>nowPlayingDecodeLyricsSearch(std::span<const std::uint8_t>,const NowPlayingTrack&);
std::optional<std::string>nowPlayingCatalogSearchURL(const NowPlayingTrack&);
std::string nowPlayingCatalogLyricsURL(std::int64_t identifier);
struct NowPlayingCatalogMatch {
    std::int64_t identifier{};std::optional<std::string>artwork;int score{};
    bool operator==(const NowPlayingCatalogMatch&)const=default;
};
std::optional<NowPlayingCatalogMatch>nowPlayingCatalogMatch(std::span<const std::uint8_t>,const NowPlayingTrack&);
// p1-p4.music.126.net only; scheme forced to https and query to param=600y600.
std::optional<std::string>nowPlayingCatalogCoverURL(std::string_view);
// Original-language lrc only; translated variants are ignored.
std::optional<NowPlayingLyrics>nowPlayingCatalogDecodeLyrics(std::span<const std::uint8_t>);
// NowPlayingLyricsDownload.isAllowedURL / NowPlayingArtworkLoader.isAllowedRemoteURL.
bool nowPlayingLyricsURLAllowed(std::string_view);
bool nowPlayingArtworkURLAllowed(std::string_view);

// NowPlayingArtworkKey(includeArtworkRevision:false). `application` is the
// stable player identity (Windows: SourceAppUserModelId; it replaces bundle
// identifier + pid because GSMTC session tokens are per activation).
struct NowPlayingLyricsKey {
    std::string application;NowPlayingSource source{NowPlayingSource::system};
    std::optional<std::string>identifier;std::string title,artist,album;std::optional<double>duration;
    static NowPlayingLyricsKey make(std::string_view application,const NowPlayingTrack&);
    bool operator==(const NowPlayingLyricsKey&)const;
};

// Owner-thread fetch. The completion runs on the owner thread exactly once
// unless cancelled first (it may run synchronously inside the call). The
// returned callable cancels; after cancellation the completion never runs.
using NowPlayingFetch=std::function<std::function<void()>(const std::string&url,std::function<void(NowPlayingBody)>)>;
// Injected-provider default: complete synchronously with no data, no request.
NowPlayingFetch nowPlayingOfflineFetch();
// Runs one pure response decode (JSON, matching, LRC parsing). `work` touches
// only its own captured bytes/track/result; `done` then runs on the owner
// thread at most once. The inline default runs both immediately, in source
// order; a host may run `work` on its shared utility worker so a large search
// answer never blocks a frame. Stale results are discarded by the caller.
using NowPlayingDecoder=std::function<void(std::function<void()>work,std::function<void()>done)>;
NowPlayingDecoder nowPlayingInlineDecoder();

// Source NowPlayingLyricsLoader (LRCLIB get, then search): at most two requests
// per distinct track, eight-entry memory cache, no retry/polling, embedded
// timed lyrics first. Owner thread only.
class NowPlayingLyricsLoader final {
public:
    explicit NowPlayingLyricsLoader(NowPlayingFetch,NowPlayingDecoder=nowPlayingInlineDecoder());
    ~NowPlayingLyricsLoader();
    NowPlayingLyricsLoader(const NowPlayingLyricsLoader&)=delete;
    NowPlayingLyricsLoader&operator=(const NowPlayingLyricsLoader&)=delete;
    void setOnChange(std::function<void()>);
    const std::shared_ptr<const NowPlayingLyrics>&lyrics()const noexcept;
    void request(const NowPlayingLyricsKey&,const NowPlayingTrack&);
    void invalidateMissing(const NowPlayingLyricsKey&);
    void clear();
    std::size_t cacheSize()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};

// Source NowPlayingCatalog (NetEase public cloudsearch + song/lyric): one
// search supplies cover and song ID; only that song's lyrics are read.
class NowPlayingCatalog final {
public:
    explicit NowPlayingCatalog(NowPlayingFetch,NowPlayingDecoder=nowPlayingInlineDecoder());
    ~NowPlayingCatalog();
    NowPlayingCatalog(const NowPlayingCatalog&)=delete;
    NowPlayingCatalog&operator=(const NowPlayingCatalog&)=delete;
    void setOnChange(std::function<void()>);
    std::optional<std::string>artwork()const;
    std::shared_ptr<const NowPlayingLyrics>lyrics()const;
    bool isLoading()const;
    void request(const NowPlayingLyricsKey&,const NowPlayingTrack&);
    void clear();
    void invalidateMissing(const NowPlayingLyricsKey&);
    std::size_t cacheSize()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};

// NowPlayingController's lyric precedence (lyrics getter, changed() and
// catalogChanged()): embedded timed lyrics, then the NetEase catalog when the
// player is NetEase, then LRCLIB. Catalog covers are reported through
// onArtworkURL for a player that supplied no thumbnail. Owner thread only.
class NowPlayingLyricsSources final {
public:
    // catalog=false mirrors injected source backends (no NowPlayingCatalog).
    NowPlayingLyricsSources(NowPlayingFetch,bool catalog=true,NowPlayingDecoder=nowPlayingInlineDecoder());
    ~NowPlayingLyricsSources();
    NowPlayingLyricsSources(const NowPlayingLyricsSources&)=delete;
    NowPlayingLyricsSources&operator=(const NowPlayingLyricsSources&)=delete;
    // Called inside loader callbacks; must not re-enter update/hide.
    std::function<void()>onChange;
    std::function<void(const std::string&url,const NowPlayingLyricsKey&)>onArtworkURL;
    // Source changed(): the active presented session/track (or none).
    void update(std::optional<NowPlayingLyricsKey>,const std::optional<NowPlayingTrack>&);
    // Source deactivate(): cancel in-flight work, keep bounded caches.
    void hide();
    // Source refreshManually(): retire negative results for the current track.
    void invalidateMissing();
    std::shared_ptr<const NowPlayingLyrics>lyrics()const;
    std::optional<std::string>catalogArtwork()const;
    bool catalogLoading()const;
    const std::optional<NowPlayingLyricsKey>&key()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
