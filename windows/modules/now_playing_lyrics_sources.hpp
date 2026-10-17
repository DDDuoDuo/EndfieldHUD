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
// NetEase's own queue state file (a local file, not a download): the reader's
// 16 MiB bound. A long queue is far larger than any lyric answer (1352
// entries measured 3.2 MB), so it never shares the 1 MiB response bound.
inline constexpr std::size_t nowPlayingMaximumPlayingListBytes=16*1024*1024;

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
    // The matched recording's catalog length ("dt", seconds), when positive.
    // Windows supplies it to window-title players, whose titles carry none.
    std::optional<double>duration;
    bool operator==(const NowPlayingCatalogMatch&)const=default;
};
std::optional<NowPlayingCatalogMatch>nowPlayingCatalogMatch(std::span<const std::uint8_t>,const NowPlayingTrack&);
// The NetEase Cloud Music desktop client writes its current queue to a local
// state file (%LOCALAPPDATA%\NetEase\CloudMusic\webdata\file\playingList): a
// JSON object with a "list" array whose entries carry the public song "id",
// the "track" with its "name", "artists"[].name, "album".cover and "duration"
// (milliseconds). Its player bar carries no elapsed/total labels and its
// progress slider exposes no accessible value (read live on 3.1.41), so the
// bar only yields a played fraction; this file supplies the authoritative
// length that turns that fraction into seconds and the exact song id that
// makes the lyric lookup match the track instead of a fuzzy catalog search.
// Read only to resolve the current track; never stored.
struct NowPlayingNetEaseEntry {
    std::int64_t identifier{};              // the public catalog song id (> 0)
    std::optional<double>duration;          // seconds, from "duration" (ms)
    std::optional<std::string>artwork;      // allow-listed catalog cover, when present
    bool operator==(const NowPlayingNetEaseEntry&)const=default;
};
// The queue file parsed once into the rows the match needs (immutable, so it
// is shared between the owner and the decode worker). A queue file is parsed
// only when its contents change, not on every track change.
struct NowPlayingNetEaseQueue {
    struct Row {
        std::string titleKey,title,album;   // titleKey: NowPlayingTrackMatcher::normalized(title)
        std::vector<std::string>artists;
        std::int64_t identifier{};
        std::optional<double>length;         // raw "duration" in seconds (the matcher's input)
        std::optional<std::string>artwork;   // allow-listed catalog cover
    };
    std::vector<Row>rows;
    static constexpr std::size_t maximumRows=16384;
};
// nullptr for a malformed file or one beyond nowPlayingMaximumPlayingListBytes.
std::shared_ptr<const NowPlayingNetEaseQueue>nowPlayingNetEaseQueue(std::span<const std::uint8_t>);
// The queue entry whose track the window title names (conservative identity:
// the shared NowPlayingTrackMatcher, the same rule the catalog match uses, so
// a wrong queue entry is never taken). nullopt for no confident match.
std::optional<NowPlayingNetEaseEntry>nowPlayingNetEaseQueueEntry(const NowPlayingNetEaseQueue&,const NowPlayingTrack&);
// Both steps at once (parses the file every call).
std::optional<NowPlayingNetEaseEntry>nowPlayingNetEasePlayingListEntry(std::span<const std::uint8_t>,const NowPlayingTrack&);
// p1-p4.music.126.net only; scheme forced to https and query to param=600y600.
std::optional<std::string>nowPlayingCatalogCoverURL(std::string_view);
// Windows: the same cover at `pixels` square (param=<pixels>y<pixels>,
// 64...1024) instead of the source's 600, so a lookup downloads what the slot
// draws (the NetEase image CDN scales to any requested square).
std::optional<std::string>nowPlayingCatalogCoverURL(std::string_view,unsigned pixels);
// The 330-point artwork slot in pixels at `pixelsPerPoint` (rounded up,
// 64...nowPlayingMaximumCoverPixels): the size covers are decoded to.
unsigned nowPlayingCoverDrawnPixels(double pixelsPerPoint)noexcept;
// The catalog cover download for that slot: the smallest of
// 200/300/400/500/600/800/1024 at least its drawn size (1024 at most), so the
// decoder only ever scales the download down to the drawn size.
unsigned nowPlayingCatalogCoverPixels(double pixelsPerPoint)noexcept;
// Original-language lrc only; translated variants are ignored. NetEase also
// stores some lines as [mm:ss:cc] (a colon before the hundredths); those
// read as [mm:ss.cc] instead of hours:minutes:seconds, as NetEase shows them.
std::optional<NowPlayingLyrics>nowPlayingCatalogDecodeLyrics(std::span<const std::uint8_t>);
// The catalog answered (a JSON object with code 200). Anything else (no body,
// a refusal such as code -460 or 405, a damaged answer) is a transient
// failure, retried later, never a "no lyrics" result.
bool nowPlayingCatalogAnswered(std::span<const std::uint8_t>);
// Whether a downloaded lyric/catalog body may be kept on disk: only answers
// that carry something (catalog code 200 with songs or lyric text; LRCLIB
// lyrics or a non-empty search). Refusals, errors and empty answers are
// never persisted, so a later launch asks again instead of reading a
// remembered "nothing".
bool nowPlayingLyricsBodyCacheable(std::string_view url,std::span<const std::uint8_t>);
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

// Owner clock for retrying transient failures (seconds, monotonic). Without
// one (the source and the injected backends) a failure stays the cached
// negative result until an explicit refresh, exactly as the source.
using NowPlayingClock=std::function<double()>;
// Windows: the wait before retrying a lookup that failed for the n-th time in
// a row (no answer, a timeout or a refusal): 5 s, 15 s, 45 s, 135 s, then
// every 300 s while the track stays presented. A failure is never cached as
// "no lyrics"; hiding Now Playing forgets it, so the next opening asks again.
double nowPlayingRetryDelay(unsigned failures)noexcept;

// Source NowPlayingLyricsLoader (LRCLIB get, then search): at most two requests
// per distinct track, eight-entry memory cache, no polling, embedded timed
// lyrics first. With a clock (Windows), a search that got no answer is retried
// with nowPlayingRetryDelay. Owner thread only.
class NowPlayingLyricsLoader final {
public:
    explicit NowPlayingLyricsLoader(NowPlayingFetch,NowPlayingDecoder=nowPlayingInlineDecoder());
    ~NowPlayingLyricsLoader();
    NowPlayingLyricsLoader(const NowPlayingLyricsLoader&)=delete;
    NowPlayingLyricsLoader&operator=(const NowPlayingLyricsLoader&)=delete;
    void setOnChange(std::function<void()>);
    void setClock(NowPlayingClock);
    const std::shared_ptr<const NowPlayingLyrics>&lyrics()const noexcept;
    void request(const NowPlayingLyricsKey&,const NowPlayingTrack&);
    void invalidateMissing(const NowPlayingLyricsKey&);
    void clear();
    // The wanted track's lyrics are shown (not loading, not missing).
    bool holds(const NowPlayingLyricsKey&)const;
    // When the wanted track's failed lookup is retried (none: no failure).
    std::optional<double>retryTime()const;
    // Drops failed lookups and their backoff (Now Playing hidden).
    void forgetFailures();
    std::size_t cacheSize()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};

// Returns the raw bytes of NetEase's queue state file, or null (no file, not
// NetEase, or no reader). Called on the owner thread once per distinct track;
// the parse itself runs on the decoder. The host returns cached bytes so the
// repeated calls are cheap; nothing is stored here.
using NowPlayingPlayingListReader=std::function<NowPlayingBody()>;

// Source NowPlayingCatalog (NetEase public cloudsearch + song/lyric): one
// search supplies cover and song ID; only that song's lyrics are read. When a
// queue-file reader is set, a NetEase track first resolves its exact song id
// and length from that file (nowPlayingNetEaseQueueEntry; the file is parsed
// once per contents), reads that song's lyrics by id and keeps the length,
// and only falls back to the fuzzy title search when the file gives no
// confident match. With a clock (Windows), a lookup that got no answer (no
// body, a refusal, a damaged answer) is finished without lyrics, so LRCLIB
// may answer meanwhile, and retried with nowPlayingRetryDelay.
class NowPlayingCatalog final {
public:
    explicit NowPlayingCatalog(NowPlayingFetch,NowPlayingDecoder=nowPlayingInlineDecoder());
    ~NowPlayingCatalog();
    NowPlayingCatalog(const NowPlayingCatalog&)=delete;
    NowPlayingCatalog&operator=(const NowPlayingCatalog&)=delete;
    void setOnChange(std::function<void()>);
    void setPlayingList(NowPlayingPlayingListReader);
    void setClock(NowPlayingClock);
    // When the wanted track's failed lookup is retried (none: no failure).
    std::optional<double>retryTime()const;
    // Drops failed lookups and their backoff (Now Playing hidden).
    void forgetFailures();
    // How often the queue file was parsed (verification: once per contents).
    std::uint64_t queueParses()const noexcept;
    std::optional<std::string>artwork()const;
    std::shared_ptr<const NowPlayingLyrics>lyrics()const;
    // The current match's catalog length (NowPlayingCatalogMatch::duration).
    std::optional<double>duration()const;
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
    // NetEase queue-file reader (NowPlayingCatalog::setPlayingList); null keeps
    // the fuzzy title search, which is what the injected source backends do.
    void setPlayingList(NowPlayingPlayingListReader);
    // Owner clock: failed lookups of the presented track are retried with
    // nowPlayingRetryDelay (the owner calls update() again once retryTime()
    // has passed). Null (default) keeps the source's cached negatives.
    void setClock(NowPlayingClock);
    std::optional<double>retryTime()const;
    // Source changed(): the active presented session/track (or none).
    void update(std::optional<NowPlayingLyricsKey>,const std::optional<NowPlayingTrack>&);
    // Source deactivate(): cancel in-flight work, keep bounded caches.
    void hide();
    // Source refreshManually(): retire negative results for the current track.
    void invalidateMissing();
    std::shared_ptr<const NowPlayingLyrics>lyrics()const;
    std::optional<std::string>catalogArtwork()const;
    std::optional<double>catalogDuration()const;
    bool catalogLoading()const;
    const std::optional<NowPlayingLyricsKey>&key()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
