#include "native/windows_account.hpp"
#ifdef _WIN32
#include "core/data/application_root.hpp"
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <windows.h>
#include <lm.h>
#include <security.h>

namespace endfield::native {
namespace {
// Issue #17: the UID line shows the account's RID until a Player ID or game
// UID is set (the Mac shows the macOS uid). Not yet confirmed for Windows:
// false keeps the stored random UID on the line.
constexpr bool showsAccountRID = true;
constexpr std::uintmax_t pictureLimit = 16ull * 1024 * 1024;
constexpr std::size_t textLimit = 1024; // UTF-16 units kept of any name
std::string utf8(std::wstring_view text) {
    text = text.substr(0, std::min(text.size(), textLimit));
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
// Some text besides spaces and control characters (Mac: normalizedName != nil).
bool visible(const std::optional<std::wstring>& text) {
    return text && std::any_of(text->begin(), text->end(), [](wchar_t c) { return !std::iswspace(c) && !std::iswcntrl(c); });
}
template<class F> auto guarded(const F& call) -> decltype(call()) {
    if (!call) return std::nullopt;
    try { return call(); } catch (...) { return std::nullopt; }
}
std::optional<std::wstring> stringFrom(BOOL ok, std::wstring text, DWORD length) {
    if (!ok) return std::nullopt;
    text.resize(std::min<std::size_t>(length, text.size()));
    while (!text.empty() && text.back() == L'\0') text.pop_back();
    return text;
}
}

WindowsAccountCalls currentWindowsAccountCalls() {
    WindowsAccountCalls calls;
    calls.sid = []() -> std::optional<std::wstring> { return ehud::data::currentUserSid(); };
    calls.displayName = []() -> std::optional<std::wstring> {
        ULONG length = 0;
        GetUserNameExW(NameDisplay, nullptr, &length);
        if (length == 0 || length > 4096) return std::nullopt;
        std::wstring text(length, L'\0');
        const BOOL ok = GetUserNameExW(NameDisplay, text.data(), &length);
        return stringFrom(ok, std::move(text), length);
    };
    calls.userName = []() -> std::optional<std::wstring> {
        std::wstring text(UNLEN + 1, L'\0');
        DWORD length = static_cast<DWORD>(text.size());
        const BOOL ok = GetUserNameW(text.data(), &length);
        return stringFrom(ok, std::move(text), length);
    };
    calls.fullName = [user = calls.userName]() -> std::optional<std::wstring> {
        const auto name = user();
        if (!name || name->empty()) return std::nullopt;
        LPBYTE buffer = nullptr;
        if (NetUserGetInfo(nullptr, name->c_str(), 2, &buffer) != NERR_Success || !buffer) return std::nullopt;
        std::optional<std::wstring> full;
        if (const auto* info = reinterpret_cast<const USER_INFO_2*>(buffer); info->usri2_full_name) full = std::wstring(info->usri2_full_name);
        NetApiBufferFree(buffer);
        return full;
    };
    calls.machineString = [](const std::wstring& key, const std::wstring& value) -> std::optional<std::wstring> {
        DWORD bytes = 0;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), value.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS) return std::nullopt;
        if (bytes == 0 || bytes > 65536) return std::nullopt;
        std::wstring text(bytes / sizeof(wchar_t), L'\0');
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), value.c_str(), RRF_RT_REG_SZ, nullptr, text.data(), &bytes) != ERROR_SUCCESS) return std::nullopt;
        text.resize(bytes / sizeof(wchar_t));
        while (!text.empty() && text.back() == L'\0') text.pop_back();
        return text;
    };
    calls.readFile = [](const std::filesystem::path& path, std::uintmax_t limit) -> std::optional<std::vector<std::uint8_t>> {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) return std::nullopt;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0 || size > limit) return std::nullopt;
        std::ifstream in(path, std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        if (!in.eof() && in.fail()) return std::nullopt;
        if (bytes.empty() || bytes.size() > limit) return std::nullopt;
        return bytes;
    };
    return calls;
}

std::optional<std::string> windowsAccountRID(std::wstring_view sid) {
    const auto dash = sid.rfind(L'-');
    if (!sid.starts_with(L"S-") || dash == std::wstring_view::npos) return std::nullopt;
    const auto digits = sid.substr(dash + 1);
    if (digits.empty() || digits.size() > 10 || !std::all_of(digits.begin(), digits.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) return std::nullopt;
    return utf8(digits);
}

ehud::data::SystemAccount readWindowsAccount(const WindowsAccountCalls& calls) {
    ehud::data::SystemAccount account;
    if constexpr (showsAccountRID) if (const auto sid = guarded(calls.sid)) account.uid = windowsAccountRID(*sid);
    for (const auto* source : {&calls.displayName, &calls.fullName, &calls.userName})
        if (const auto name = guarded(*source); visible(name)) { account.name = utf8(*name); break; }
    return account;
}

std::optional<std::vector<std::uint8_t>> readWindowsAccountPicture(const WindowsAccountCalls& calls) {
    const auto sid = guarded(calls.sid);
    if (!sid || !sid->starts_with(L"S-") || !calls.machineString || !calls.readFile) return std::nullopt;
    const std::wstring key = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AccountPicture\\Users\\" + *sid;
    for (const auto* value : {L"Image448", L"Image1080", L"Image240"}) {
        std::optional<std::wstring> path;
        try { path = calls.machineString(key, value); } catch (...) { path.reset(); }
        if (!path || path->empty()) continue;
        try {
            if (auto bytes = calls.readFile(std::filesystem::path(*path), pictureLimit)) return bytes;
        } catch (...) {}
    }
    return std::nullopt;
}
}
#endif
