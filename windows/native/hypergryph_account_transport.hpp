#pragma once
#include "modules/hypergryph_account_api.hpp"
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>

// Asynchronous WinHTTP transport for the fixed community endpoints and public
// avatar images. HTTPS only (TLS 1.2+), no cookies, no redirects (every 3xx is
// unsafeRedirect, so a credential is never forwarded), no automatic logon, no
// cache, bounded bodies (8 MiB API / 4 MiB image) and no request/response
// logging. WinHTTP's own completion threads only queue results; `notify` asks
// the host to call drain() on the owner thread, where every completion runs.
// The 20 s per-request resource limit is a deadline folded into the host's
// existing scheduler: no timer, worker thread or polling loop is created.
//
// The WinHTTP session exists only while it is needed: the first request
// opens it (on `background`, the shared utility queue, when given; the
// request waits for it inside its 20 s limit), and it is closed again as soon
// as the owner no longer retains it (setRetained(false): the HUD is hidden)
// and no request is in flight. Constructing the transport opens nothing.
namespace endfield::native {
// A transport whose completions run only when the owner drains it (WinHTTP in
// EndfieldHUD.exe; the in-process synthetic transport in hidden coverage).
// Deadlines are host-clock seconds folded into the host's single scheduler.
class OwnerDrainedAccountTransport:public modules::hypergryph::HttpTransport {
public:
    virtual std::size_t drain()=0;                       // owner thread
    virtual std::optional<double> nextDeadline() const=0;
    virtual bool deadline(double now)=0;                 // expires overdue requests
    virtual std::size_t activeRequests() const=0;
};
#ifdef _WIN32
// Response-head gate shared by the WinHTTP callbacks (exposed for tests).
struct AccountResponseHead {std::optional<modules::hypergryph::Error> failure;bool readBody{};};
AccountResponseHead classifyAccountResponseHead(int status,std::optional<unsigned long long> contentLength,
    std::size_t maximumBytes,bool allowAuthenticationStatus,std::optional<std::string> mimeType,bool imageRequest);
class WinHttpAccountTransport final:public OwnerDrainedAccountTransport {
public:
    struct Options {
        std::function<void()> notify;          // thread-safe, posts one owner message
        std::function<double()> now;           // host frame clock (seconds)
        std::wstring userAgent{L"EndfieldHUD/1.2.0 (Windows)"};
        double requestTimeout{modules::hypergryph::requestTimeoutSeconds};
        double resourceTimeout{modules::hypergryph::resourceTimeoutSeconds};
        double imageTimeout{10};
        // Test-only: TCP port (loopback refusal tests) and no proxy discovery,
        // so tests never resolve or contact anything beyond 127.0.0.1.
        unsigned short port{443};
        bool directOnly{};
        // Runs `work` off the owner thread and `done` on it (production: the
        // shared UtilityExecutor); false when it cannot accept the work. Empty
        // (or refused): the session opens on the calling thread.
        std::function<bool(std::function<void()> work,std::function<void(std::exception_ptr)> done)> background;
    };
    struct SessionStats {
        bool open{},opening{},retained{};  // a session handle exists / is being opened / the owner keeps it
        std::uint64_t opens{},closes{},failures{};
        std::size_t waiting{};             // requests waiting for the session
    };
    explicit WinHttpAccountTransport(Options);
    ~WinHttpAccountTransport() override;
    WinHttpAccountTransport(const WinHttpAccountTransport&)=delete;
    WinHttpAccountTransport& operator=(const WinHttpAccountTransport&)=delete;
    modules::hypergryph::RequestHandle start(modules::hypergryph::HttpRequest,std::function<void(modules::hypergryph::HttpOutcome)>) override;
    void post(std::function<void()>) override;
    // Public image (avatar) GET: only "Accept: image/*", no account headers.
    modules::hypergryph::RequestHandle startImage(const std::string& httpsURL,std::size_t maximumBytes,
        std::function<void(modules::hypergryph::HttpOutcome)>);
    std::size_t drain() override;              // owner thread
    std::optional<double> nextDeadline() const override;
    bool deadline(double now) override;        // expires overdue requests as transport failures
    std::size_t activeRequests() const override;
    // Owner thread. true (default): keep an open session between requests;
    // false: close it now if idle, else once the last request finished.
    void setRetained(bool);
    SessionStats sessionStats() const;
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
#endif
}
