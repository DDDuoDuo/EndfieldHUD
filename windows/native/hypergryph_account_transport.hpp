#pragma once
#include "modules/hypergryph_account_api.hpp"
#include <functional>
#include <memory>

// Asynchronous WinHTTP transport for the fixed community endpoints and public
// avatar images. HTTPS only (TLS 1.2+), no cookies, no redirects (every 3xx is
// unsafeRedirect, so a credential is never forwarded), no automatic logon, no
// cache, bounded bodies (8 MiB API / 4 MiB image) and no request/response
// logging. WinHTTP's own completion threads only queue results; `notify` asks
// the host to call drain() on the owner thread, where every completion runs.
// The 20 s per-request resource limit is a deadline folded into the host's
// existing scheduler: no timer, worker thread or polling loop is created.
namespace endfield::native {
#ifdef _WIN32
// Response-head gate shared by the WinHTTP callbacks (exposed for tests).
struct AccountResponseHead {std::optional<modules::hypergryph::Error> failure;bool readBody{};};
AccountResponseHead classifyAccountResponseHead(int status,std::optional<unsigned long long> contentLength,
    std::size_t maximumBytes,bool allowAuthenticationStatus,std::optional<std::string> mimeType,bool imageRequest);
class WinHttpAccountTransport final:public modules::hypergryph::HttpTransport {
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
    std::size_t drain();                       // owner thread
    std::optional<double> nextDeadline() const;
    bool deadline(double now);                 // expires overdue requests as transport failures
    std::size_t activeRequests() const;
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
#endif
}
