#include "packaging/update_platform.hpp"
#ifdef _WIN32
#include "packaging/package_io.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <winhttp.h>
#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>

namespace endfield::packaging::updates {
namespace fs = std::filesystem;
namespace {
std::wstring wide(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) throw std::invalid_argument("Invalid UTF-8 text");
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}
std::string narrow(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
std::string lowerASCII(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
bool samePath(const fs::path& a, const fs::path& b) {
    std::error_code e1, e2;
    const auto x = fs::weakly_canonical(a, e1), y = fs::weakly_canonical(b, e2);
    if (e1 || e2) return false;
    auto l = x.native(), r = y.native();
    while (l.size() > 3 && (l.back() == L'\\' || l.back() == L'/')) l.pop_back();
    while (r.size() > 3 && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    return CompareStringOrdinal(l.c_str(), static_cast<int>(l.size()), r.c_str(), static_cast<int>(r.size()), TRUE) == CSTR_EQUAL;
}
using Failure = TransportResponse::Failure;
} // namespace

// --- WinHTTP transport ----------------------------------------------------------------
struct WinHttpTransport::Impl : std::enable_shared_from_this<Impl> {
    struct Shared {
        std::mutex mutex;
        std::deque<std::pair<std::uint64_t, TransportResponse>> done;
        std::function<void()> notify;
        void complete(std::uint64_t id, TransportResponse response) {
            std::function<void()> wake;
            {
                std::lock_guard lock(mutex);
                const bool first = done.empty();
                done.emplace_back(id, std::move(response));
                if (first) wake = notify;
            }
            if (wake) wake();
        }
    };
    struct Context {
        static constexpr DWORD readChunk = 64 * 1024;
        std::shared_ptr<Shared> shared;
        std::uint64_t id{}, maximum{};
        fs::path file;
        std::mutex mutex;
        HINTERNET connect{}, request{};
        HANDLE output{INVALID_HANDLE_VALUE};
        bool finished{}, closed{};
        TransportResponse response;
        std::unique_ptr<char[]> buffer;
        // Under `mutex`. A failed or cancelled download leaves no file behind.
        void closeFile(bool remove) {
            if (output == INVALID_HANDLE_VALUE) return;
            if (!remove) FlushFileBuffers(output);
            CloseHandle(output);
            output = INVALID_HANDLE_VALUE;
            if (remove) DeleteFileW(file.c_str());
        }
        void finish(Failure failure) {
            TransportResponse out;
            {
                std::lock_guard lock(mutex);
                if (finished) return;
                finished = true;
                closeFile(failure != Failure::none);
                out = std::move(response);
                out.failure = failure;
            }
            shared->complete(id, std::move(out));
            close();
        }
        void abandon() {
            {
                std::lock_guard lock(mutex);
                finished = true;
                closeFile(true);
            }
            close();
        }
        void close() {
            HINTERNET r{}, c{};
            {
                std::lock_guard lock(mutex);
                if (closed) return;
                closed = true;
                r = request;
                c = connect;
            }
            if (r) WinHttpCloseHandle(r);
            if (c) WinHttpCloseHandle(c);
        }
    };
    Options options;
    HINTERNET session{};
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::map<std::uint64_t, Completion> pending;
    std::map<std::uint64_t, std::pair<double, std::weak_ptr<Context>>> started; // deadline
    std::uint64_t next{};

    static Headers parseHeaders(std::wstring_view raw) {
        Headers headers;
        std::size_t at = raw.find(L"\r\n"); // skip the status line
        while (at != std::wstring_view::npos && at + 2 < raw.size()) {
            at += 2;
            const auto end = raw.find(L"\r\n", at);
            const auto line = raw.substr(at, end == std::wstring_view::npos ? std::wstring_view::npos : end - at);
            if (const auto colon = line.find(L':'); colon != std::wstring_view::npos && colon > 0) {
                auto value = line.substr(colon + 1);
                while (!value.empty() && (value.front() == L' ' || value.front() == L'\t')) value.remove_prefix(1);
                while (!value.empty() && (value.back() == L' ' || value.back() == L'\t')) value.remove_suffix(1);
                headers.emplace_back(narrow(line.substr(0, colon)), narrow(value));
            }
            at = end;
        }
        return headers;
    }
    static void CALLBACK callback(HINTERNET handle, DWORD_PTR value, DWORD status, LPVOID info, DWORD length) {
        if (!value) return;
        auto* holder = reinterpret_cast<std::shared_ptr<Context>*>(value);
        auto context = *holder;
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { if (handle == context->request) delete holder; return; }
        if (handle != context->request) return;
        {
            std::lock_guard lock(context->mutex);
            if (context->finished) return;
        }
        switch (status) {
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            if (!WinHttpReceiveResponse(handle, nullptr)) context->finish(Failure::network);
            break;
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: {
            DWORD code{}, size = sizeof code;
            if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX)) {
                context->finish(Failure::network);
                break;
            }
            DWORD rawBytes{};
            WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &rawBytes, WINHTTP_NO_HEADER_INDEX);
            Headers headers;
            if (rawBytes > 0 && rawBytes <= 64 * 1024) {
                std::wstring raw(rawBytes / sizeof(wchar_t) + 1, L'\0');
                if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &rawBytes, WINHTTP_NO_HEADER_INDEX)) {
                    raw.resize(rawBytes / sizeof(wchar_t));
                    headers = parseHeaders(raw);
                }
            } else if (rawBytes > 64 * 1024) {
                context->finish(Failure::network);
                break;
            }
            std::optional<std::uint64_t> declared;
            if (const auto h = findHeader(headers, "Content-Length")) {
                std::uint64_t v{};
                bool ok = !h->empty() && h->size() <= 19;
                for (const char c : *h) { if (c < '0' || c > '9') { ok = false; break; } v = v * 10 + static_cast<std::uint64_t>(c - '0'); }
                if (ok) declared = v;
            }
            bool body = true;
            {
                std::lock_guard lock(context->mutex);
                context->response.response.status = static_cast<int>(code);
                context->response.response.headers = std::move(headers);
                body = context->file.empty() || code == 200;
            }
            if (body && declared && *declared > context->maximum) { context->finish(Failure::tooLarge); break; }
            if (!body) { context->finish(Failure::none); break; } // file mode: only a 200 body is stored
            if (!context->file.empty()) {
                std::lock_guard lock(context->mutex);
                context->output = CreateFileW(context->file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            }
            if (!context->file.empty() && context->output == INVALID_HANDLE_VALUE) {
                // Never touch a file this request did not create.
                {
                    std::lock_guard lock(context->mutex);
                    context->file.clear();
                }
                context->finish(Failure::file);
                break;
            }
            if (!WinHttpQueryDataAvailable(handle, nullptr)) context->finish(Failure::network);
            break;
        }
        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE: {
            const DWORD available = *static_cast<DWORD*>(info);
            if (available == 0) { context->finish(Failure::none); break; }
            std::uint64_t received{};
            {
                std::lock_guard lock(context->mutex);
                received = context->response.bytes;
            }
            if (available > context->maximum - std::min(received, context->maximum)) { context->finish(Failure::tooLarge); break; }
            if (!context->buffer) context->buffer = std::make_unique<char[]>(Context::readChunk);
            if (!WinHttpReadData(handle, context->buffer.get(), std::min<DWORD>(available, Context::readChunk), nullptr)) context->finish(Failure::network);
            break;
        }
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: {
            if (length == 0) { context->finish(Failure::none); break; }
            bool written = true;
            {
                std::lock_guard lock(context->mutex);
                if (context->finished) break;
                if (context->output != INVALID_HANDLE_VALUE) {
                    DWORD count{};
                    written = WriteFile(context->output, info, length, &count, nullptr) && count == length;
                } else {
                    context->response.response.body.append(static_cast<const char*>(info), length);
                }
                context->response.bytes += length;
            }
            if (!written) { context->finish(Failure::file); break; }
            if (!WinHttpQueryDataAvailable(handle, nullptr)) context->finish(Failure::network);
            break;
        }
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: {
            const auto* result = static_cast<WINHTTP_ASYNC_RESULT*>(info);
            context->finish(result && result->dwError == ERROR_WINHTTP_OPERATION_CANCELLED ? Failure::cancelled : Failure::network);
            break;
        }
        default: break;
        }
    }
    explicit Impl(Options o) : options(std::move(o)) {
        if (!options.now) throw std::invalid_argument("The update transport needs the host clock");
        shared->notify = options.notify;
        if (options.loopbackOnly)
            session = WinHttpOpen(options.userAgent.c_str(), WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        else {
            session = WinHttpOpen(options.userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
            if (!session) session = WinHttpOpen(options.userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        }
        if (!session) throw std::runtime_error("The WinHTTP session is unavailable");
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
        }
        if (WinHttpSetStatusCallback(session, callback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
            WinHttpCloseHandle(session);
            throw std::runtime_error("The WinHTTP callback could not be installed");
        }
    }
    ~Impl() {
        {
            std::lock_guard lock(shared->mutex);
            shared->notify = {};
        }
        for (auto& [id, entry] : started) if (auto context = entry.second.lock()) context->abandon();
        pending.clear();
        started.clear();
        if (session) WinHttpCloseHandle(session);
    }
    std::uint64_t open(TransportRequest request, Completion completion) {
        auto context = std::make_shared<Context>();
        context->shared = shared;
        context->id = ++next;
        context->maximum = request.maximumBytes;
        context->file = request.file;
        context->response.response.url = request.url;
        const auto id = context->id;
        pending[id] = std::move(completion);
        const auto fail = [&](Failure failure) {
            TransportResponse r;
            r.failure = failure;
            r.response.url = request.url;
            shared->complete(id, std::move(r));
            return id;
        };
        std::wstring url;
        try { url = wide(request.url); } catch (...) { return fail(Failure::network); }
        URL_COMPONENTS parts{sizeof parts};
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
        if (url.size() > 8192 || !WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts) || parts.dwUserNameLength || parts.dwPasswordLength)
            return fail(Failure::network);
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring object(parts.lpszUrlPath, parts.dwUrlPathLength);
        object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        if (object.empty()) object = L"/";
        const bool https = parts.nScheme == INTERNET_SCHEME_HTTPS;
        // Production: HTTPS only. Tests: plain HTTP to the loopback only.
        if (options.loopbackOnly ? (https || host != L"127.0.0.1") : !https) return fail(Failure::network);
        context->connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
        if (!context->connect) return fail(Failure::network);
        context->request = WinHttpOpenRequest(context->connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              (https ? WINHTTP_FLAG_SECURE : 0) | WINHTTP_FLAG_REFRESH | WINHTTP_FLAG_ESCAPE_DISABLE | WINHTTP_FLAG_ESCAPE_DISABLE_QUERY);
        if (!context->request) { WinHttpCloseHandle(context->connect); context->connect = nullptr; return fail(Failure::network); }
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(context->request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
        DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
        WinHttpSetOption(context->request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof disabled);
        DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        WinHttpSetOption(context->request, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof logon);
#ifdef WINHTTP_OPTION_DECOMPRESSION
        if (request.file.empty()) { // metadata only; a setup is stored byte for byte
            DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
            WinHttpSetOption(context->request, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof decompression);
        }
#endif
        const int ms = static_cast<int>(options.operationTimeout * 1000);
        WinHttpSetTimeouts(context->request, ms, ms, ms, ms);
        std::wstring headerText;
        try {
            for (const auto& [name, value] : request.headers) headerText += wide(name) + L": " + wide(value) + L"\r\n";
        } catch (...) {
            context->close();
            return fail(Failure::network);
        }
        auto* holder = new std::shared_ptr<Context>(context);
        if (!WinHttpSetOption(context->request, WINHTTP_OPTION_CONTEXT_VALUE, &holder, sizeof holder)) {
            delete holder;
            context->close();
            return fail(Failure::network);
        }
        started[id] = {options.now() + request.timeout, context};
        if (!WinHttpSendRequest(context->request, headerText.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerText.c_str(),
                                headerText.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(holder))) {
            started.erase(id);
            context->finish(Failure::network);
        }
        return id;
    }
};

WinHttpTransport::WinHttpTransport(Options options) : impl_(std::make_shared<Impl>(std::move(options))) {}
WinHttpTransport::~WinHttpTransport() = default;
std::uint64_t WinHttpTransport::start(TransportRequest request, Completion completion) { return impl_->open(std::move(request), std::move(completion)); }
void WinHttpTransport::cancel(std::uint64_t id) noexcept {
    auto& i = *impl_;
    i.pending.erase(id);
    if (const auto it = i.started.find(id); it != i.started.end()) {
        if (auto context = it->second.second.lock()) context->abandon();
        i.started.erase(it);
    }
}
std::size_t WinHttpTransport::drain() {
    auto& i = *impl_;
    auto keep = impl_;
    std::size_t count{};
    while (true) {
        std::deque<std::pair<std::uint64_t, TransportResponse>> ready;
        {
            std::lock_guard lock(i.shared->mutex);
            ready.swap(i.shared->done);
        }
        if (ready.empty()) return count;
        for (auto& [id, response] : ready) {
            i.started.erase(id);
            const auto found = i.pending.find(id);
            if (found == i.pending.end()) continue;
            auto completion = std::move(found->second);
            i.pending.erase(found);
            completion(std::move(response));
            ++count;
        }
    }
}
std::optional<double> WinHttpTransport::nextDeadline() const {
    std::optional<double> next;
    for (const auto& [id, entry] : impl_->started) next = next ? std::min(*next, entry.first) : entry.first;
    return next;
}
bool WinHttpTransport::deadline(double now) {
    auto& i = *impl_;
    std::vector<std::uint64_t> overdue;
    for (const auto& [id, entry] : i.started) if (now >= entry.first) overdue.push_back(id);
    for (const auto id : overdue) {
        auto context = i.started[id].second.lock();
        i.started.erase(id);
        if (context) context->finish(Failure::timeout);
    }
    return !overdue.empty();
}
std::size_t WinHttpTransport::active() const { return impl_->pending.size(); }

// --- Installed copy, verification, hand-off -----------------------------------------------
fs::path runningExecutable() {
    std::wstring buffer(32768, L'\0');
    const auto n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!n || n >= buffer.size()) throw std::runtime_error("The running executable cannot be located");
    buffer.resize(n);
    return fs::path(buffer);
}
bool installedCopy(const fs::path& executable, const ProductVersion& current, std::wstring_view uninstallKey) {
    try {
        if (CompareStringOrdinal(executable.filename().c_str(), -1, L"EndfieldHUD.exe", -1, TRUE) != CSTR_EQUAL) return false;
        const auto folder = executable.parent_path();
        std::ifstream input(folder / std::string(installManifestName), std::ios::binary);
        if (!input) return false;
        std::string bytes;
        bytes.resize(maximumManifestBytes + 1);
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(input.gcount()));
        if (bytes.size() > maximumManifestBytes) return false;
        const auto manifest = decodeInstallManifest(bytes);
        if (!(manifest.version == current)) return false;
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, std::wstring(uninstallKey).c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
        std::wstring location(MAX_PATH * 4, L'\0');
        DWORD size = static_cast<DWORD>(location.size() * sizeof(wchar_t));
        const auto status = RegGetValueW(key, nullptr, L"InstallLocation", RRF_RT_REG_SZ, nullptr, location.data(), &size);
        RegCloseKey(key);
        if (status != ERROR_SUCCESS || size < sizeof(wchar_t)) return false;
        location.resize(size / sizeof(wchar_t) - 1);
        return samePath(fs::path(location), folder);
    } catch (...) {
        return false;
    }
}
std::optional<std::string> authenticodeSigner(const fs::path& file) {
    WINTRUST_FILE_INFO info{};
    info.cbStruct = sizeof info;
    info.pcwszFilePath = file.c_str();
    WINTRUST_DATA data{};
    data.cbStruct = sizeof data;
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &info;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    std::optional<std::string> signer;
    if (status == ERROR_SUCCESS)
        if (auto* provider = WTHelperProvDataFromStateData(data.hWVTStateData))
            if (auto* sgnr = WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0); sgnr && sgnr->csCertChain && sgnr->pasCertChain && sgnr->pasCertChain[0].pCert) {
                wchar_t name[512]{};
                if (CertGetNameStringW(sgnr->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 512) > 1) signer = narrow(name);
            }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return signer;
}
void verifySetup(const fs::path& setup, const std::string& sha256, const ProductVersion& expected, const fs::path& running) {
    if (lowerASCII(fileSha256Hex(setup)) != lowerASCII(sha256)) throw PackageError("The download does not match its published SHA-256");
    {
        const auto payload = SetupPayload::open(setup); // verifies the payload hash and manifest
        const auto& m = payload.manifest();
        if (!(m.version == expected)) throw PackageError("The download installs " + m.version.display() + ", not " + expected.display());
        if (!m.complete) throw PackageError("The download is an incomplete developer package");
        if (m.architecture != "x64") throw PackageError("The download is not an x64 package");
    }
    // Publisher continuity once builds are signed (none are yet).
    if (const auto signer = running.empty() ? std::nullopt : authenticodeSigner(running)) {
        const auto other = authenticodeSigner(setup);
        if (!other || *other != *signer) throw PackageError("The download is not signed by this build's publisher");
    }
}
std::shared_ptr<void> holdSetup(const fs::path& setup, const std::string& sha256) {
    HANDLE handle = CreateFileW(setup.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw PackageError("The verified download cannot be opened");
    std::shared_ptr<void> held(handle, [](void* h) { CloseHandle(h); });
    Sha256Stream hash;
    std::vector<std::uint8_t> buffer(1 << 20);
    while (true) {
        DWORD read{};
        if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) throw PackageError("The verified download cannot be read");
        if (read == 0) break;
        hash.update(std::span(buffer.data(), read));
    }
    const auto digest = hash.finish();
    if (hex(digest) != lowerASCII(sha256)) throw PackageError("The verified download changed before installation");
    return held;
}
std::wstring commandLineArgument(std::wstring_view value) {
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++backslashes; out += c; continue; }
        if (c == L'"') out.append(backslashes + 1, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes, L'\\');
    return out + L"\"";
}
namespace {
struct ExitNotice { HWND window{}; UINT message{}; WPARAM wParam{}; };
VOID CALLBACK setupExitedCallback(PVOID data, BOOLEAN) {
    const auto* n = static_cast<ExitNotice*>(data);
    PostMessageW(n->window, n->message, n->wParam, 0);
}
class LaunchedSetup final : public SetupProcess {
public:
    LaunchedSetup(HANDLE process, std::unique_ptr<ExitNotice> notice) : process_(process), notice_(std::move(notice)) {
        if (!RegisterWaitForSingleObject(&wait_, process_, setupExitedCallback, notice_.get(), INFINITE, WT_EXECUTEONLYONCE)) wait_ = nullptr;
    }
    ~LaunchedSetup() override {
        if (wait_) UnregisterWaitEx(wait_, INVALID_HANDLE_VALUE); // waits for a running callback
        CloseHandle(process_);
    }
    LaunchedSetup(const LaunchedSetup&) = delete;
    LaunchedSetup& operator=(const LaunchedSetup&) = delete;
    std::optional<int> exitCode() const override {
        if (WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) return std::nullopt;
        DWORD code{};
        if (!GetExitCodeProcess(process_, &code)) return 1;
        return static_cast<int>(code);
    }
private:
    HANDLE process_{};
    HANDLE wait_{};
    std::unique_ptr<ExitNotice> notice_;
};
} // namespace
std::unique_ptr<SetupProcess> launchSetup(const fs::path& setup, const std::vector<std::string>& arguments, void* window, std::uint32_t message,
                                          std::uintptr_t wParam) {
    std::wstring command = commandLineArgument(setup.native());
    for (const auto& a : arguments) command += L" " + commandLineArgument(wide(a));
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    const auto folder = setup.parent_path();
    // Setup outlives this run (it closes and restarts EndfieldHUD): leave a
    // job that would end it with us when the job allows breaking away.
    auto line = command;
    BOOL created = CreateProcessW(setup.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_BREAKAWAY_FROM_JOB, nullptr, folder.c_str(), &startup, &process);
    if (!created && GetLastError() == ERROR_ACCESS_DENIED) {
        line = command;
        created = CreateProcessW(setup.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup, &process);
    }
    if (!created) throw std::runtime_error("Setup could not be started (" + std::to_string(GetLastError()) + ")");
    CloseHandle(process.hThread);
    auto notice = std::make_unique<ExitNotice>();
    notice->window = static_cast<HWND>(window);
    notice->message = message;
    notice->wParam = static_cast<WPARAM>(wParam);
    return std::make_unique<LaunchedSetup>(process.hProcess, std::move(notice));
}
} // namespace endfield::packaging::updates
#endif
