#pragma once
#include "modules/now_playing_lyrics_sources.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::native {
class NowPlayingWebCache; // native/now_playing_web_cache.hpp

// Visible-only public metadata downloads for Now Playing: LRCLIB/NetEase JSON
// (1 MiB, status 200, application/json|text/plain) and allow-listed cover
// images (8 MiB, 2xx, image/*). No cookies, credentials, HTTP cache, redirects,
// retry or polling. Owner-thread API; the transport may complete on any
// thread, and completions run only inside drain() on the owner thread.
//
// With a NowPlayingWebCache, an accepted body is also kept on disk (bounded,
// keyed by the exact request) and a later identical request is answered from
// it through the shared utility executor instead of the network.
class NowPlayingWeb final {
public:
    enum class Kind {lyrics,artwork};
    struct Request {std::string url;Kind kind{Kind::lyrics};};
    // One in-flight transport request. cancel is thread-safe, nonblocking and
    // idempotent; a transport may still call Done afterwards (ignored).
    class Task {public:virtual ~Task()=default;virtual void cancel()noexcept=0;};
    using Done=std::function<void(modules::NowPlayingBody)>;
    // Starts the request without blocking the owner thread. Done is called at
    // most once with the complete accepted body, or null on any failure.
    using Transport=std::function<std::shared_ptr<Task>(const Request&,Done)>;
    struct Stats {
        std::uint64_t started{},completed{},failed{},cancelled{},timedOut{},rejected{};std::size_t inFlight{};
        std::uint64_t cacheHits{},cacheMisses{},cacheStores{};
    };
    static constexpr std::size_t maximumInFlight=4;
    // notify posts one coalesced owner message (the host's existing utility
    // wake); it never drains here. now is the host monotonic clock. timeout
    // is the source URLSession resource bound (10 s); it covers a cache read
    // and the network request that may follow it.
    NowPlayingWeb(Transport,std::function<void()>notify,std::function<double()>now,double timeout=10,
        std::shared_ptr<NowPlayingWebCache>cache={});
    ~NowPlayingWeb();
    NowPlayingWeb(const NowPlayingWeb&)=delete;
    NowPlayingWeb&operator=(const NowPlayingWeb&)=delete;
    // Disallowed URL or capacity: the completion runs synchronously with null,
    // exactly like the source download's rejected-URL path.
    std::function<void()>fetch(Request,std::function<void(modules::NowPlayingBody)>);
    modules::NowPlayingFetch lyricsFetch();
    // Deliver finished requests and expire overdue ones. true if any ran.
    bool drain();
    // Earliest request deadline; the host folds it into its one scheduler.
    std::optional<double>nextWakeTime()const;
    void cancelAll();
    // Owner thread: indexes the disk cache (once, on the shared executor), so
    // the lookups of the presentation that follows can be answered from it.
    void prepare();
    Stats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};

// Pure acceptance rules used by every transport (source didReceive response).
bool nowPlayingWebAccepts(NowPlayingWeb::Kind,int status,std::string_view mediaType,std::optional<std::uint64_t>contentLength)noexcept;
std::size_t nowPlayingWebLimit(NowPlayingWeb::Kind)noexcept;
// The Referer a request carries: https://music.163.com/ for the NetEase
// catalog (source NowPlayingCatalog), none otherwise.
std::optional<std::string>nowPlayingWebReferer(std::string_view url);
// The media type of a Content-Type value ("image/jpeg; q=1" -> "image/jpeg").
std::string nowPlayingWebMediaType(std::string_view contentType);

#ifdef _WIN32
// WinHTTP transport with ONE kept-alive asynchronous session: every request
// of an owner reuses the session's pooled connections (keep-alive, HTTP/2 when
// the server offers it) instead of a new TLS handshake per lookup. No redirect,
// cookie, credential or cache; User-Agent "EndfieldHUD/1.0", Referer
// https://music.163.com/ for NetEase; gzip/deflate decoded by WinHTTP; the
// 1 MiB / 8 MiB bounds are enforced while streaming. Requests start without
// blocking the caller and complete on WinHTTP's own pool threads.
//
// The session opens with the first request and stays open while retained
// (setRetained(true): Now Playing presented); once released it closes as soon
// as no request is in flight, and WinHTTP closes its idle pooled connections
// after their idle period (about 20 s measured), so a hidden Now Playing
// keeps no session and soon no socket. Thread-safe.
class NowPlayingHttp final {
public:
    struct Options {
        std::wstring agent{L"EndfieldHUD/1.0"};
        // https only (production). Tests: plain http to a loopback server.
        bool secure{true};
        // No proxy (tests); production uses the system proxy settings.
        bool directOnly{};
        // Test seam: the TCP endpoint for a URL's host (the URL keeps its own
        // host for the Referer rule); nullopt: the URL's host and port.
        std::function<std::optional<std::pair<std::wstring,std::uint16_t>>(std::wstring_view host)>endpoint;
    };
    struct Stats {std::uint64_t sessionsOpened{},sessionsClosed{},requests{},connects{};std::size_t inFlight{};bool open{},retained{};};
    NowPlayingHttp();
    explicit NowPlayingHttp(Options);
    ~NowPlayingHttp();
    NowPlayingHttp(const NowPlayingHttp&)=delete;
    NowPlayingHttp&operator=(const NowPlayingHttp&)=delete;
    // The transport over this session; it holds the session weakly.
    NowPlayingWeb::Transport transport();
    void setRetained(bool);
    Stats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
// A transport over its own retained session (now_playing_probe).
NowPlayingWeb::Transport windowsNowPlayingTransport();
#endif
}
