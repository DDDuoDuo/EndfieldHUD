// Windows updater services (packaging/update_platform.hpp) in isolation:
// WinHTTP against an in-process loopback HTTP fixture server (127.0.0.1 only;
// nothing beyond the loopback is resolved or contacted), the installed-copy
// check in a private HKCU\Software\EndfieldHUD-Test-<id> subtree and temporary
// folders, verification of synthetic setups, the held hand-off file, the Setup
// launcher with a stand-in Setup (argv[1]) and the updater end to end over
// those services. Never the real installer, install folder, uninstall entry,
// network or user data.
#include "core/data/json.hpp"
#include "packaging/package_io.hpp"
#include "packaging/update_fixture.hpp"
#include "packaging/update_platform.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>

namespace u = endfield::packaging::updates;
namespace p = endfield::packaging;
namespace fs = std::filesystem;

namespace {
unsigned checks{};
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void writeText(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}
struct TemporaryFolder {
    fs::path path;
    TemporaryFolder() {
        std::random_device seed;
        path = fs::canonical(fs::temp_directory_path()) / ("ehud-update-platform-" + std::to_string(seed()) + "-" + std::to_string(seed()));
        fs::create_directories(path);
    }
    ~TemporaryFolder() { std::error_code e; fs::remove_all(path, e); }
};

// --- Loopback HTTP/1.1 fixture server ------------------------------------------------------
class LoopbackServer {
public:
    struct Route {
        std::string head; // status line and headers, without the final blank line
        std::string body;
        bool contentLength{true};
        bool stall{};     // send the head and part of the body, then wait until released
    };
    LoopbackServer() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("Winsock");
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (listener_ == INVALID_SOCKET || bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || listen(listener_, 16) != 0)
            throw std::runtime_error("Loopback listener");
        int length = sizeof address;
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        acceptor_ = std::thread([this] { accept(); });
    }
    ~LoopbackServer() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            released_ = true;
        }
        changed_.notify_all();
        closesocket(listener_);
        acceptor_.join();
        for (auto& t : connections_) t.join();
        WSACleanup();
    }
    std::string url(std::string_view path) const { return "http://127.0.0.1:" + std::to_string(port_) + std::string(path); }
    void route(std::string path, Route r) { std::lock_guard lock(mutex_); routes_[std::move(path)] = std::move(r); }
    std::vector<std::string> requests() { std::lock_guard lock(mutex_); return requests_; }
    void forget() { std::lock_guard lock(mutex_); requests_.clear(); }
    void release() { { std::lock_guard lock(mutex_); released_ = true; } changed_.notify_all(); }
private:
    SOCKET listener_{INVALID_SOCKET};
    unsigned short port_{};
    std::thread acceptor_;
    std::vector<std::thread> connections_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::map<std::string, Route> routes_;
    std::vector<std::string> requests_;
    bool stopping_{}, released_{};
    void accept() {
        while (true) {
            SOCKET client = ::accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            std::lock_guard lock(mutex_);
            if (stopping_) { closesocket(client); return; }
            connections_.emplace_back([this, client] { serve(client); });
        }
    }
    void serve(SOCKET client) {
        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const int n = recv(client, buffer, sizeof buffer, 0);
            if (n <= 0) { closesocket(client); return; }
            request.append(buffer, static_cast<std::size_t>(n));
        }
        const auto path = request.substr(4, request.find(' ', 4) - 4);
        Route r;
        {
            std::lock_guard lock(mutex_);
            requests_.push_back(request);
            const auto found = routes_.find(path);
            r = found == routes_.end() ? Route{"HTTP/1.1 404 Not Found", "", true, false} : found->second;
        }
        std::string out = r.head + "\r\n";
        if (r.contentLength) out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
        out += "Connection: close\r\n\r\n";
        const auto sendAll = [&](std::string_view bytes) {
            while (!bytes.empty()) {
                const int n = send(client, bytes.data(), static_cast<int>(bytes.size()), 0);
                if (n <= 0) return false;
                bytes.remove_prefix(static_cast<std::size_t>(n));
            }
            return true;
        };
        if (r.stall) {
            sendAll(out + r.body.substr(0, r.body.size() / 2));
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, std::chrono::seconds(30), [this] { return released_; });
        } else {
            sendAll(out + r.body);
        }
        shutdown(client, SD_SEND);
        closesocket(client);
    }
};

// Completion notices of the transport reach this event (the owner message in the app).
struct Notice {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Notice() { CloseHandle(event); }
    std::function<void()> notify() { return [e = event] { SetEvent(e); }; }
    bool wait(DWORD ms = 15000) { return WaitForSingleObject(event, ms) == WAIT_OBJECT_0; }
};
u::TransportResponse fetch(u::WinHttpTransport& transport, Notice& notice, u::TransportRequest request) {
    std::optional<u::TransportResponse> out;
    transport.start(std::move(request), [&](u::TransportResponse r) { out = std::move(r); });
    for (int n = 0; n < 4 && !out; ++n) {
        if (!notice.wait()) break;
        transport.drain();
    }
    if (!out) throw std::runtime_error("The transport did not complete");
    return std::move(*out);
}

// Threads of this process now (Toolhelp snapshot).
std::size_t processThreads() {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    std::size_t count{};
    THREADENTRY32 entry{sizeof(entry)};
    for (auto more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
        if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++count;
    CloseHandle(snapshot);
    return count;
}
// What the transport keeps running: measured first, while this process runs
// nothing else (the loopback server is created after the constructions).
// Constructing a transport, in the test and the production configuration,
// opens no WinHTTP session and starts no thread; a production-style session
// (what the windowsUpdates area held from launch before) is measured for
// comparison, and the session closes once a request completed.
void sessionThreads() {
    const auto before = processThreads();
    Notice notice;
    u::WinHttpTransport::Options options;
    options.notify = notice.notify();
    options.now = [] { return 1.0; };
    options.loopbackOnly = true;
    std::optional<u::WinHttpTransport> t;
    t.emplace(options);
    auto productionOptions = options;
    productionOptions.loopbackOnly = false;
    std::optional<u::WinHttpTransport> production;
    production.emplace(productionOptions);
    const auto constructed = processThreads();
    check(!t->sessionOpen() && t->sessionOpens() == 0 && !production->sessionOpen() && constructed <= before,
          "Constructing the transports opens no WinHTTP session and starts no thread (" + std::to_string(before) + " -> " + std::to_string(constructed) + ")");
    production.reset();
    // For comparison: one asynchronous session as the production transport
    // opens it (automatic proxy, completion callback). No request is sent.
    HINTERNET raw = WinHttpOpen(L"EndfieldHUD-thread-measure", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (raw) WinHttpSetStatusCallback(raw, [](HINTERNET, DWORD_PTR, DWORD, LPVOID, DWORD) {}, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0);
    const auto session = processThreads();
    if (raw) WinHttpCloseHandle(raw);
    std::size_t requested{};
    {
        LoopbackServer server;
        server.route("/x", {"HTTP/1.1 200 OK", "x"});
        u::TransportRequest request;
        request.url = server.url("/x");
        request.maximumBytes = 16;
        check(fetch(*t, notice, request).response.status == 200, "Thread measurement request");
        check(!t->sessionOpen() && t->sessionOpens() == 1 && t->active() == 0, "The session closes once the request completed");
    }
    requested = processThreads();
    t.reset();
    const auto destroyed = processThreads();
    std::cout << "WinHTTP threads: " << before << " before, " << constructed << " with both transports constructed (no session), " << session
              << " with one production-style session open, " << requested << " after one request (session closed), " << destroyed << " destroyed\n";
}

// The session opened on a background queue (the area's shared utility queue):
// requests wait for it, keep their deadline, and a cancelled or expired one
// never reaches the network.
void backgroundSession() {
    LoopbackServer server;
    Notice notice;
    double now = 50;
    std::deque<std::pair<std::function<void()>, std::function<void(std::exception_ptr)>>> queue;
    bool accept = true;
    u::WinHttpTransport::Options options;
    options.notify = notice.notify();
    options.now = [&now] { return now; };
    options.loopbackOnly = true;
    options.background = [&](std::function<void()> work, std::function<void(std::exception_ptr)> done) {
        if (!accept) return false;
        queue.emplace_back(std::move(work), std::move(done));
        return true;
    };
    const auto run = [&] {
        while (!queue.empty()) {
            auto [work, done] = std::move(queue.front());
            queue.pop_front();
            std::exception_ptr error;
            try { work(); } catch (...) { error = std::current_exception(); }
            done(error);
        }
    };
    std::optional<u::WinHttpTransport> t;
    t.emplace(options);
    server.route("/feed", {"HTTP/1.1 200 OK", "feed"});
    u::TransportRequest request;
    request.url = server.url("/feed");
    request.maximumBytes = 64;
    request.timeout = 20;
    std::optional<u::TransportResponse> out;
    t->start(request, [&](u::TransportResponse r) { out = std::move(r); });
    check(!t->sessionOpen() && queue.size() == 1 && t->active() == 1 && t->nextDeadline() == 70.0 && server.requests().empty(),
          "A request waits for the session opening on the queue, with its deadline");
    std::optional<u::TransportResponse> second;
    t->start(request, [&](u::TransportResponse r) { second = std::move(r); });
    check(queue.size() == 1, "One session opening serves every waiting request");
    run();
    check(t->sessionOpen() && t->sessionOpens() == 1, "The opened session is adopted on the owner thread");
    for (int n = 0; n < 8 && !(out && second); ++n) {
        if (!notice.wait()) break;
        t->drain();
    }
    check(out && second && out->response.status == 200 && out->response.body == "feed" && second->response.body == "feed" && !t->sessionOpen() &&
          t->active() == 0, "Both waiting requests complete, then the session closes");

    // Cancelled while the session opens: nothing is requested or completed.
    server.forget();
    bool completed{};
    const auto id = t->start(request, [&](u::TransportResponse) { completed = true; });
    t->cancel(id);
    run();
    t->drain();
    check(!completed && !t->sessionOpen() && t->active() == 0 && server.requests().empty() && t->sessionOpens() == 2,
          "A request cancelled before the session opened never reaches the network");
    // Its deadline passes while the session opens: a timeout, no request.
    std::optional<u::TransportResponse> late;
    t->start(request, [&](u::TransportResponse r) { late = std::move(r); });
    check(t->deadline(now + 20), "The waiting request's deadline expires");
    t->drain();
    run();
    t->drain();
    check(late && late->failure == u::TransportResponse::Failure::timeout && !t->sessionOpen() && server.requests().empty(),
          "A request that waited past its deadline times out without a request");
    // A full queue: the session opens on the owner thread instead.
    accept = false;
    out.reset();
    t->start(request, [&](u::TransportResponse r) { out = std::move(r); });
    check(queue.empty() && t->sessionOpen(), "A queue that refuses the work leaves the session to the owner thread");
    for (int n = 0; n < 4 && !out; ++n) {
        if (!notice.wait()) break;
        t->drain();
    }
    check(out && out->response.status == 200 && !t->sessionOpen(), "That request completes and the session closes");
    // Destroyed while its session opens: the opened session is closed by its slot.
    accept = true;
    t->start(request, [](u::TransportResponse) {});
    t.reset();
    run();
    check(queue.empty(), "A session opened for a destroyed transport is released");
}

void transport() {
    LoopbackServer server;
    Notice notice;
    double now = 1000;
    u::WinHttpTransport::Options options;
    options.notify = notice.notify();
    options.now = [&now] { return now; };
    options.loopbackOnly = true;
    options.operationTimeout = 10;
    u::WinHttpTransport t(options);
    TemporaryFolder temp;
    const std::string feed = "[{\"tag_name\":\"windows-v1.2.1\"}]";
    server.route("/releases", {"HTTP/1.1 200 OK\r\nETag: W/\"loop\"\r\nX-RateLimit-Remaining: 59\r\nRetry-After: 7\r\nContent-Type: application/json", feed});
    u::TransportRequest request;
    request.url = server.url("/releases");
    request.headers = {{"Accept", "application/vnd.github+json"}, {"If-None-Match", "W/\"old\""}, {"User-Agent", "EndfieldHUD-release-check"}};
    request.maximumBytes = 4096;
    check(!t.sessionOpen(), "No session before the first request");
    auto r = fetch(t, notice, request);
    check(!t.sessionOpen() && t.sessionOpens() == 1, "The first request's session is closed once it completed");
    check(r.failure == u::TransportResponse::Failure::none && r.response.status == 200 && r.response.body == feed && r.bytes == feed.size() &&
          r.response.url == request.url && u::findHeader(r.response.headers, "etag") == "W/\"loop\"" && u::findHeader(r.response.headers, "Retry-After") == "7" &&
          u::findHeader(r.response.headers, "X-RateLimit-Remaining") == "59", "A 200 body and its headers");
    const auto sent = server.requests().back();
    check(sent.starts_with("GET /releases HTTP/1.1\r\n") && sent.find("If-None-Match: W/\"old\"\r\n") != std::string::npos &&
          sent.find("Accept: application/vnd.github+json\r\n") != std::string::npos && sent.find("User-Agent: EndfieldHUD-release-check\r\n") != std::string::npos &&
          sent.find("Cookie") == std::string::npos && sent.find("Authorization") == std::string::npos, "The request carries exactly the given headers");
    check(t.active() == 0 && !t.nextDeadline(), "Nothing is left in flight");

    server.forget();
    server.route("/moved", {"HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:1/elsewhere", ""});
    request.url = server.url("/moved");
    r = fetch(t, notice, request);
    check(r.failure == u::TransportResponse::Failure::none && r.response.status == 302 && u::findHeader(r.response.headers, "Location") == "http://127.0.0.1:1/elsewhere" &&
          server.requests().size() == 1, "Redirects are reported, never followed");
    server.route("/same", {"HTTP/1.1 304 Not Modified", "", false});
    request.url = server.url("/same");
    r = fetch(t, notice, request);
    check(r.failure == u::TransportResponse::Failure::none && r.response.status == 304 && r.response.body.empty(), "Not modified");
    server.route("/large", {"HTTP/1.1 200 OK", std::string(5000, 'x')});
    request.url = server.url("/large");
    check(fetch(t, notice, request).failure == u::TransportResponse::Failure::tooLarge, "A declared length over the bound is refused");
    server.route("/stream", {"HTTP/1.1 200 OK", std::string(5000, 'y'), false});
    request.url = server.url("/stream");
    check(fetch(t, notice, request).failure == u::TransportResponse::Failure::tooLarge, "A body over the bound without a length is refused");

    // File mode.
    const std::string setup = std::string(100000, 's') + "end";
    server.route("/setup", {"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream", setup});
    u::TransportRequest download;
    download.url = server.url("/setup");
    download.maximumBytes = setup.size();
    download.file = temp.path / "setup.exe.partial";
    download.timeout = 60;
    r = fetch(t, notice, download);
    check(r.failure == u::TransportResponse::Failure::none && r.response.status == 200 && r.bytes == setup.size() && r.response.body.empty() &&
          readText(download.file) == setup, "A 200 body streams into its new file");
    r = fetch(t, notice, download);
    check(r.failure == u::TransportResponse::Failure::file && readText(download.file) == setup, "An existing file is never overwritten");
    fs::remove(download.file);
    download.url = server.url("/missing");
    r = fetch(t, notice, download);
    check(r.failure == u::TransportResponse::Failure::none && r.response.status == 404 && !fs::exists(download.file), "Only a 200 body creates the file");
    server.route("/huge", {"HTTP/1.1 200 OK", std::string(200000, 'z'), false});
    download.url = server.url("/huge");
    download.maximumBytes = 150000;
    r = fetch(t, notice, download);
    check(r.failure == u::TransportResponse::Failure::tooLarge && !fs::exists(download.file), "An oversized download is removed");

    // Cancellation and the resource deadline (host clock) of a stalled body.
    server.route("/stall", {"HTTP/1.1 200 OK", std::string(200000, 'w'), true, true});
    download.url = server.url("/stall");
    download.maximumBytes = 200000;
    bool completed{};
    const auto id = t.start(download, [&](u::TransportResponse) { completed = true; });
    for (int n = 0; n < 100 && !fs::exists(download.file); ++n) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    check(fs::exists(download.file) && t.nextDeadline() == now + 60 && t.sessionOpen(), "A stalled download holds its partial file, deadline and session");
    t.cancel(id);
    check(!t.sessionOpen(), "Cancelling the last request closes the session");
    notice.wait(500);
    t.drain();
    check(!completed && !fs::exists(download.file) && t.active() == 0 && !t.nextDeadline(), "A cancelled download completes nothing and leaves no file");
    std::optional<u::TransportResponse> timed;
    t.start(download, [&](u::TransportResponse x) { timed = std::move(x); });
    for (int n = 0; n < 100 && !fs::exists(download.file); ++n) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    check(!t.deadline(now + 59.9) && t.deadline(now + 60), "The deadline expires on the host clock");
    t.drain();
    check(timed && timed->failure == u::TransportResponse::Failure::timeout && !fs::exists(download.file) && !t.sessionOpen(),
          "A request past its deadline fails, leaves no file and closes the session");
    server.release();

    // Refusals without any connection.
    server.forget();
    u::TransportRequest refused;
    refused.maximumBytes = 10;
    refused.url = "http://127.0.0.1:1/closed";
    check(fetch(t, notice, refused).failure == u::TransportResponse::Failure::network, "A refused connection is a network failure");
    refused.url = "https://api.github.com/repos/DDDuoDuo/EndfieldHUD/releases?per_page=100";
    check(fetch(t, notice, refused).failure == u::TransportResponse::Failure::network, "Loopback tests never reach the internet");
    Notice other;
    options.notify = other.notify();
    options.loopbackOnly = false;
    u::WinHttpTransport production(options);
    refused.url = server.url("/releases");
    check(fetch(production, other, refused).failure == u::TransportResponse::Failure::network && server.requests().empty(),
          "The production transport refuses plain HTTP before connecting");
    refused.url = "https://user:secret@github.com/";
    check(fetch(production, other, refused).failure == u::TransportResponse::Failure::network, "URLs with credentials are refused");
}

// --- Installed copy ---------------------------------------------------------------------------
struct TestRegistry {
    std::wstring root;
    TestRegistry() {
        GUID id{};
        CoCreateGuid(&id);
        wchar_t text[64]{};
        StringFromGUID2(id, text, 64);
        root = std::wstring(L"Software\\EndfieldHUD-Test-") + text;
    }
    ~TestRegistry() { RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str()); }
    std::wstring uninstallKey() const { return root + L"\\Uninstall\\EndfieldHUD"; }
    void location(const fs::path& folder) const {
        HKEY key{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, uninstallKey().c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            throw std::runtime_error("Test registry");
        const auto& v = folder.native();
        RegSetValueExW(key, L"InstallLocation", 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
};
void installed() {
    TemporaryFolder temp;
    TestRegistry registry;
    const auto folder = temp.path / "Programs" / "EndfieldHUD";
    fs::create_directories(folder);
    const auto exe = folder / "EndfieldHUD.exe";
    writeText(exe, "synthetic");
    p::InstallManifest manifest;
    manifest.version = {1, 2, 0, 18};
    manifest.commit = std::string(40, 'a');
    manifest.installedAt = "2026-10-10T00:00:00Z";
    manifest.files = {{"EndfieldHUD.exe", 9, std::string(64, '1')}, {"uninstall.exe", 9, std::string(64, '2')}};
    writeText(folder / "EndfieldHUD.install.json", p::encodeInstallManifest(manifest));
    const p::ProductVersion current{1, 2, 0, 18};
    check(!u::installedCopy(exe, current, registry.uninstallKey()), "No uninstall entry: not installed");
    registry.location(folder);
    check(u::installedCopy(exe, current, registry.uninstallKey()), "The registered install with its manifest is the installed copy");
    registry.location(fs::path(folder.native() + L"\\"));
    check(u::installedCopy(exe, current, registry.uninstallKey()), "A trailing separator names the same folder");
    check(!u::installedCopy(exe, {1, 2, 0, 19}, registry.uninstallKey()) && !u::installedCopy(exe, {1, 2, 1, 18}, registry.uninstallKey()),
          "Another version is a development build");
    check(!u::installedCopy(folder / "Other.exe", current, registry.uninstallKey()), "Another executable is not the installed copy");
    registry.location(temp.path / "Elsewhere");
    check(!u::installedCopy(exe, current, registry.uninstallKey()), "A copy outside the registered folder is not the installed copy");
    registry.location(folder);
    writeText(folder / "EndfieldHUD.install.json", "{}");
    check(!u::installedCopy(exe, current, registry.uninstallKey()), "A damaged manifest is not an install");
    fs::remove(folder / "EndfieldHUD.install.json");
    check(!u::installedCopy(exe, current, registry.uninstallKey()), "A portable copy is not an install");
    check(!u::installedCopy(u::runningExecutable(), current, u::productionUninstallKey), "This test program is never the installed copy");
}

// --- Verification, hold, launch -----------------------------------------------------------------
fs::path syntheticSetup(const fs::path& root, const fs::path& stub, p::ProductVersion version, bool complete = true) {
    fs::create_directories(root / "tree");
    writeText(root / "tree" / "EndfieldHUD.exe", "synthetic EndfieldHUD " + version.display() + "\n");
    writeText(root / "uninstall.exe", "synthetic uninstall\n");
    auto sources = p::collectTree(root / "tree");
    sources.push_back({"uninstall.exe", root / "uninstall.exe"});
    p::PackOptions pack;
    pack.version = version;
    pack.commit = std::string(40, 'b');
    pack.complete = complete;
    const auto out = root / u::setupAssetName(version.release(), version.build);
    p::writeSetup(stub, std::move(sources), pack, out);
    return out;
}
template <class F> bool throws(F&& f) {
    try { f(); } catch (const std::exception&) { return true; }
    return false;
}
void verification(const fs::path& standIn) {
    TemporaryFolder temp;
    const auto stub = temp.path / "stub.exe";
    const auto bytes = u::syntheticSetupStub();
    writeText(stub, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const p::ProductVersion target{1, 2, 1, 19};
    const auto setup = syntheticSetup(temp.path / "a", stub, target);
    const auto sha = p::fileSha256Hex(setup);
    const auto self = u::runningExecutable();
    check(!u::authenticodeSigner(self) && !u::authenticodeSigner(setup), "Test images are unsigned (no publisher continuity applies)");
    u::verifySetup(setup, sha, target, self);
    check(true, "A matching download verifies");
    check(throws([&] { u::verifySetup(setup, std::string(64, '0'), target, self); }), "A different SHA-256 is refused");
    check(throws([&] { u::verifySetup(setup, sha, {1, 2, 1, 20}, self); }), "A payload of another build is refused");
    check(throws([&] { u::verifySetup(setup, sha, {1, 2, 2, 19}, self); }), "A payload of another version is refused");
    const auto incomplete = syntheticSetup(temp.path / "b", stub, target, false);
    check(throws([&] { u::verifySetup(incomplete, p::fileSha256Hex(incomplete), target, self); }), "An incomplete developer payload is refused");
    auto damaged = readText(setup);
    damaged[damaged.size() - 100] ^= 0x5A;
    const auto tampered = temp.path / "tampered.exe";
    writeText(tampered, damaged);
    check(throws([&] { u::verifySetup(tampered, p::fileSha256Hex(tampered), target, self); }), "A payload that fails its own hash is refused even with a matching checksum");
    writeText(temp.path / "not-a-setup.exe", "plain bytes");
    check(throws([&] { u::verifySetup(temp.path / "not-a-setup.exe", p::fileSha256Hex(temp.path / "not-a-setup.exe"), target, self); }), "A file without a payload is refused");

    // Hold: no writer or deleter while held; a changed file is refused.
    {
        auto held = u::holdSetup(setup, sha);
        HANDLE w = CreateFileW(setup.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        const auto writeError = GetLastError();
        if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
        check(w == INVALID_HANDLE_VALUE && writeError == ERROR_SHARING_VIOLATION && !DeleteFileW(setup.c_str()), "A held setup cannot be changed or deleted");
    }
    HANDLE w = CreateFileW(setup.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    check(w != INVALID_HANDLE_VALUE, "Released after the hold");
    CloseHandle(w);
    check(throws([&] { (void)u::holdSetup(setup, std::string(64, 'f')); }), "A changed setup is refused at hand-off");

    // Launch: the stand-in Setup records the hand-off outcome; its exit posts the notice.
    const std::wstring className = L"EndfieldHUDUpdateLaunchTest";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className.c_str();
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, className.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    check(window != nullptr, "Message-only test window");
    const UINT message = WM_APP + 77;
    const auto waitNotice = [&](WPARAM expected) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < until) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_POSTMESSAGE);
            MSG m{};
            while (PeekMessageW(&m, window, 0, 0, PM_REMOVE))
                if (m.message == message && m.wParam == expected) return true;
        }
        return false;
    };
    const auto updates = temp.path / "Updates";
    fs::create_directories(updates);
    const auto launched = updates / u::setupAssetName("1.2.1", 19);
    fs::copy_file(standIn, launched);
    const std::string outcome = "{\"outcome\":\"refused\",\"refusal\":\"close-failed\"}";
    writeText(updates / "fixture-outcome.txt", "3\n" + outcome);
    const auto result = updates / u::handoffResultName;
    {
        auto process = u::launchSetup(launched, u::handoffArguments(result), window, message, 2);
        check(waitNotice(2), "The Setup exit posts the owner notice");
        check(process->exitCode() == 3 && readText(result) == outcome, "Setup ran with the hand-off command line and reported its outcome");
    }
    // The right to take the foreground this run holds from the Install
    // click goes on to Setup (which passes it on to the EndfieldHUD it
    // reopens): granted to Setup's own process ID while it is still
    // suspended - it has not written its result yet - and Setup runs even
    // when Windows refuses the grant (this run held no right to pass on).
    {
        std::error_code ignored;
        fs::remove(result, ignored);
        std::vector<std::uint32_t> granted;
        std::wstring grantedImage;
        bool ranBeforeGrant{};
        auto process = u::launchSetup(launched, u::handoffArguments(result), window, message, 2, [&](std::uint32_t pid) {
            granted.push_back(pid);
            if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
                std::wstring image(32768, L'\0');
                DWORD size = static_cast<DWORD>(image.size());
                if (QueryFullProcessImageNameW(h, 0, image.data(), &size)) grantedImage.assign(image.data(), size);
                CloseHandle(h);
            }
            Sleep(300); // a running stand-in Setup writes its result well within this
            ranBeforeGrant = fs::exists(result);
            return false;
        });
        check(granted.size() == 1 && !grantedImage.empty() && fs::equivalent(fs::path(grantedImage), launched), "The grant names the started Setup's process ID, once");
        check(!ranBeforeGrant, "Setup is granted the foreground before it runs");
        check(waitNotice(2) && process->exitCode() == 3 && readText(result) == outcome, "Setup then runs the hand-off although the grant was refused");
    }
    bool missingGranted{};
    check(throws([&] {
              (void)u::launchSetup(updates / "missing-setup.exe", u::handoffArguments(result), window, message, 2, [&](std::uint32_t) { missingGranted = true; return true; });
          }) && !missingGranted, "A missing setup cannot start and is granted nothing");
    for (const auto* value : {L"plain", L"with space", L"quote\"inside", L"C:\\path with space\\", L"trailing\\\\", L""}) {
        const auto line = L"x.exe " + u::commandLineArgument(value);
        int count{};
        auto** argv = CommandLineToArgvW(line.c_str(), &count);
        const bool same = argv && count == 2 && std::wstring(argv[1]) == value;
        if (argv) LocalFree(argv);
        check(same, "Command-line quoting round-trips");
    }

    // The updater end to end over the real verification, hold and launcher:
    // a synthetic release whose setup is the stand-in Setup with a payload.
    const auto release = syntheticSetup(temp.path / "c", standIn, target);
    const auto releaseBytes = readText(release);
    const auto releaseSha = p::fileSha256Hex(release);
    TemporaryFolder data;
    const auto folder = data.path / "Updates";
    u::RouteTransport routes;
    const std::string tag = "windows-v1.2.1";
    const auto setupName = u::setupAssetName("1.2.1", 19), sumsName = u::sumsAssetName("1.2.1", 19);
    const auto sums = u::sumsText({{releaseSha, setupName}});
    routes.serve(std::string(u::releasesEndpoint), {200, {}, u::releaseFeed({{tag, "", "", false, false, true, u::installerAssets(tag, "1.2.1", 19, releaseBytes.size(), sums.size())}}), {}});
    routes.serve(u::releaseDownloadURL(tag, sumsName), {200, {}, sums, {}});
    routes.serve(u::releaseDownloadURL(tag, setupName), {302, {{"Location", "https://objects.githubusercontent.com/a?b"}}, {}, {}});
    routes.serve("https://objects.githubusercontent.com/a?b", {200, {}, releaseBytes, {}});
    std::deque<std::pair<std::function<void()>, std::function<void(std::exception_ptr)>>> queue;
    u::UpdaterOptions o;
    o.current = {1, 2, 0, 18};
    o.folder = folder;
    o.automaticAllowed = o.installAllowed = true;
    u::UpdaterServices s;
    s.transport = &routes;
    s.submit = [&](std::function<void()> work, std::function<void(std::exception_ptr)> done) { queue.emplace_back(std::move(work), std::move(done)); return true; };
    s.clock = [] { return 1.0; };
    s.wallClock = [] { return 1791676800.0; };
    s.installedCopy = [] { return true; };
    s.verify = [self](const fs::path& f, const std::string& d, const p::ProductVersion& v) { u::verifySetup(f, d, v, self); };
    s.hold = [](const fs::path& f, const std::string& d) { return u::holdSetup(f, d); };
    s.launch = [&](const fs::path& f, const std::vector<std::string>& a) { return u::launchSetup(f, a, window, message, 2); };
    s.overlayVisible = [] { return false; };
    s.closeOverlay = [] {};
    u::Updater updater(o, s);
    const auto settle = [&] {
        for (int n = 0; n < 20; ++n) {
            while (!queue.empty()) {
                auto [work, done] = std::move(queue.front());
                queue.pop_front();
                std::exception_ptr error;
                try { work(); } catch (...) { error = std::current_exception(); }
                done(error);
            }
            updater.drain();
            if (queue.empty() && !routes.active()) return;
        }
    };
    updater.start(1);
    settle();
    check(updater.check(1), "End to end: check");
    settle();
    check(updater.state().phase == u::Phase::available, "End to end: the synthetic release is available");
    updater.activate(1);
    settle();
    check(updater.state().phase == u::Phase::ready && p::fileSha256Hex(folder / setupName) == releaseSha, "End to end: the download verified with the real reader");
    writeText(folder / "fixture-outcome.txt", "3\n" + outcome);
    updater.activate(1);
    settle();
    check(updater.state().phase == u::Phase::installing && updater.setup(), "End to end: Setup started");
    check(waitNotice(2), "End to end: the Setup exit is noticed");
    updater.setupExited(1);
    settle();
    check(updater.state().phase == u::Phase::failed && updater.state().detail.kind == u::Detail::Kind::closeRefused && !updater.setup(),
          "End to end: the refused hand-off is reported");
    settle();
    check(!fs::exists(folder / setupName) && !fs::exists(folder / u::handoffResultName), "End to end: nothing is left behind");
    DestroyWindow(window);
    UnregisterClassW(className.c_str(), wc.hInstance);
}

// The updater's metadata check over the real WinHTTP transport (loopback feed).
void metadataOverWinHttp() {
    LoopbackServer server;
    Notice notice;
    TemporaryFolder data;
    u::WinHttpTransport::Options options;
    options.notify = notice.notify();
    options.now = [] { return 1.0; };
    options.loopbackOnly = true;
    u::WinHttpTransport transport(options);
    const auto feed = u::releaseFeed({{"v9.0.0", "Mac", "", false, false, true, {}}, {"windows-v1.2.1", "", "", false, false, true, u::installerAssets("windows-v1.2.1", "1.2.1", 19, 4096, 100)}});
    server.route("/repos/DDDuoDuo/EndfieldHUD/releases?per_page=100", {"HTTP/1.1 200 OK\r\nETag: \"loop-etag\"", feed});
    std::deque<std::pair<std::function<void()>, std::function<void(std::exception_ptr)>>> queue;
    u::UpdaterOptions o;
    o.current = {1, 2, 0, 18};
    o.folder = data.path / "Updates";
    o.automaticAllowed = o.installAllowed = true;
    o.endpoint = server.url("/repos/DDDuoDuo/EndfieldHUD/releases?per_page=100");
    u::UpdaterServices s;
    s.transport = &transport;
    s.submit = [&](std::function<void()> work, std::function<void(std::exception_ptr)> done) { queue.emplace_back(std::move(work), std::move(done)); return true; };
    s.clock = [] { return 1.0; };
    s.wallClock = [] { return 1791676800.0; };
    s.installedCopy = [] { return true; };
    u::Updater updater(o, s);
    const auto run = [&] {
        while (!queue.empty()) {
            auto [work, done] = std::move(queue.front());
            queue.pop_front();
            std::exception_ptr error;
            try { work(); } catch (...) { error = std::current_exception(); }
            done(error);
        }
    };
    updater.start(1);
    run();
    check(updater.check(1) && updater.nextWakeTime(1) == 21.0, "WinHTTP check in flight with its 20 s deadline");
    check(notice.wait(), "WinHTTP completion notice");
    updater.drain();
    run();
    check(updater.state().phase == u::Phase::available && updater.candidate() && updater.candidate()->tag == "windows-v1.2.1" &&
          updater.state().latestVersion == "1.2.1 (19)", "The updater reads the loopback feed through WinHTTP");
    server.route("/repos/DDDuoDuo/EndfieldHUD/releases?per_page=100", {"HTTP/1.1 304 Not Modified", "", false});
    server.forget();
    check(updater.check(1) && notice.wait(), "Second WinHTTP check");
    updater.drain();
    run();
    const auto sent = server.requests();
    check(sent.size() == 1 && sent[0].find("If-None-Match: \"loop-etag\"\r\n") != std::string::npos && updater.state().phase == u::Phase::available,
          "The ETag is sent and a 304 keeps the cached release");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2 && fs::is_regular_file(argv[1]), "Pass the stand-in Setup (packaging_update_setup_fixture)");
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        sessionThreads();
        transport();
        backgroundSession();
        installed();
        verification(fs::path(argv[1]));
        metadataOverWinHttp();
        CoUninitialize();
        std::cout << "PASS " << checks << " Windows update platform checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
