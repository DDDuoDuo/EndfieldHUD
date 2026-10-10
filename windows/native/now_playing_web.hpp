#pragma once
#include "modules/now_playing_lyrics_sources.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace endfield::native {
// Visible-only public metadata downloads for Now Playing: LRCLIB/NetEase JSON
// (1 MiB, status 200, application/json|text/plain) and allow-listed cover
// images (8 MiB, 2xx, image/*). No cookies, credentials, cache, redirects,
// retry or polling. Owner-thread API; the transport may complete on any
// thread, and completions run only inside drain() on the owner thread.
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
    struct Stats {std::uint64_t started{},completed{},failed{},cancelled{},timedOut{},rejected{};std::size_t inFlight{};};
    static constexpr std::size_t maximumInFlight=4;
    // notify posts one coalesced owner message (the host's existing utility
    // wake); it never drains here. now is the host monotonic clock. timeout
    // is the source URLSession resource bound (10 s).
    NowPlayingWeb(Transport,std::function<void()>notify,std::function<double()>now,double timeout=10);
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
    Stats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};

// Pure acceptance rules used by every transport (source didReceive response).
bool nowPlayingWebAccepts(NowPlayingWeb::Kind,int status,std::string_view mediaType,std::optional<std::uint64_t>contentLength)noexcept;
std::size_t nowPlayingWebLimit(NowPlayingWeb::Kind)noexcept;

#ifdef _WIN32
// Windows.Web.Http transport on the OS thread pool: AllowAutoRedirect=false,
// AllowUI=false, NoCookies, cache read/write disabled, User-Agent
// "EndfieldHUD/1.0" and Referer https://music.163.com/ for NetEase. The
// factory itself performs no IO.
NowPlayingWeb::Transport windowsNowPlayingTransport();
#endif
}
