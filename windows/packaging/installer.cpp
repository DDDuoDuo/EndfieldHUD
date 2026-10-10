#include "packaging/installer.hpp"
#ifdef _WIN32
#include "app/application_login_item.hpp"
#include "app/single_instance.hpp"
#include "core/data/application_root.hpp"
#include "core/data/data_store.hpp"
#include "core/data/json.hpp"
#include "native/app_notification_identity.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <set>
#include <stdexcept>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <knownfolders.h>
#include <objbase.h>
#include <restartmanager.h>
#include <shlobj.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Notifications.h>

namespace endfield::packaging {
namespace fs = std::filesystem;
namespace {
using ehud::data::Json;

[[noreturn]] void fail(const std::string& what) { throw PackageError(what); }
[[noreturn]] void windowsFailure(const std::string& what, DWORD error = GetLastError()) {
    throw PackageError(what + " (Windows error " + std::to_string(error) + ")");
}
std::string pathText(const fs::path& path) { return narrow(path.native()); }

struct Key {
    HKEY value{};
    Key() = default;
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    ~Key() { if (value) RegCloseKey(value); }
};
struct Apartment {
    HRESULT hr{};
    Apartment() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {
        if (FAILED(hr)) throw PackageError("The setup thread could not join a single-threaded apartment");
    }
    ~Apartment() { CoUninitialize(); }
};

std::string token() {
    std::array<std::uint8_t, 4> bytes{};
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        fail("No random source is available");
    return hex(bytes);
}
std::string utcNow() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    char text[32]{};
    std::snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02uZ", static_cast<unsigned>(t.wYear), static_cast<unsigned>(t.wMonth),
                  static_cast<unsigned>(t.wDay), static_cast<unsigned>(t.wHour), static_cast<unsigned>(t.wMinute), static_cast<unsigned>(t.wSecond));
    return text;
}
std::wstring installDate() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t text[16]{};
    std::swprintf(text, 16, L"%04u%02u%02u", static_cast<unsigned>(t.wYear), static_cast<unsigned>(t.wMonth), static_cast<unsigned>(t.wDay));
    return text;
}
DWORD attributesOf(const fs::path& path) {
    const auto a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        const auto e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) windowsFailure("Inspecting " + pathText(path) + " failed", e);
    }
    return a;
}
bool pathExists(const fs::path& path) { return attributesOf(path) != INVALID_FILE_ATTRIBUTES; }
bool ordinaryDirectory(const fs::path& path) {
    const auto a = attributesOf(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}
bool transientShareError(DWORD e) noexcept {
    return e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION || e == ERROR_DIR_NOT_EMPTY;
}
// Antivirus scanners and Explorer briefly hold freshly written files; retry
// a bounded number of times (installer only; the app itself never waits).
bool moveWithRetry(const fs::path& from, const fs::path& to, DWORD* error = nullptr) {
    DWORD e = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
        e = GetLastError();
        if (!transientShareError(e)) break;
        Sleep(250);
    }
    if (error) *error = e;
    return false;
}
bool deleteFileWithRetry(const fs::path& path) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (DeleteFileW(path.c_str())) return true;
        const auto e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true;
        if (e == ERROR_ACCESS_DENIED) {
            const auto a = GetFileAttributesW(path.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
        }
        if (!transientShareError(e)) return false;
        Sleep(150);
    }
    return false;
}
bool removeDirectoryWithRetry(const fs::path& path) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (RemoveDirectoryW(path.c_str())) return true;
        const auto e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true;
        if (e == ERROR_DIR_NOT_EMPTY || !transientShareError(e)) return false;
        Sleep(150);
    }
    return false;
}
std::vector<fs::path> children(const fs::path& folder);
// A folder whose entries are all gone may still be "not empty" for a moment
// while a scanner closes a deleted file; retry only while nothing is visible.
bool removeEmptyFolder(const fs::path& path) {
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (RemoveDirectoryW(path.c_str())) return true;
        const auto e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true;
        if (!transientShareError(e) || !children(path).empty()) return false;
        Sleep(100);
    }
    return false;
}
void createDirectories(const fs::path& path) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error || !ordinaryDirectory(path)) fail("Creating " + pathText(path) + " failed");
}
std::vector<fs::path> children(const fs::path& folder) {
    std::vector<fs::path> out;
    WIN32_FIND_DATAW entry{};
    const auto pattern = folder / L"*";
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &entry, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring_view name = entry.cFileName;
        if (name != L"." && name != L"..") out.push_back(folder / entry.cFileName);
    } while (FindNextFileW(find, &entry));
    FindClose(find);
    return out;
}
void removeTreeInto(const fs::path& path, std::vector<fs::path>& remaining) {
    const auto a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) {
        // Remove the link itself, never its target.
        const bool removed = (a & FILE_ATTRIBUTE_DIRECTORY) ? removeDirectoryWithRetry(path) : deleteFileWithRetry(path);
        if (!removed) remaining.push_back(path);
        return;
    }
    if (a & FILE_ATTRIBUTE_DIRECTORY) {
        for (const auto& child : children(path)) removeTreeInto(child, remaining);
        if (!removeDirectoryWithRetry(path)) remaining.push_back(path);
        return;
    }
    if (!deleteFileWithRetry(path)) remaining.push_back(path);
}
void writeNewFile(const fs::path& path, std::string_view bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) windowsFailure("Creating " + pathText(path.filename()) + " failed");
    DWORD written{};
    const bool ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size() && FlushFileBuffers(h);
    const auto e = GetLastError();
    CloseHandle(h);
    if (!ok) { DeleteFileW(path.c_str()); windowsFailure("Writing " + pathText(path.filename()) + " failed", e); }
}
std::optional<std::string> readSmallFile(const fs::path& path, std::size_t limit) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    struct Close { HANDLE h; ~Close() { CloseHandle(h); } } close{h};
    FILE_ATTRIBUTE_TAG_INFO tag{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)) || (tag.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        !GetFileSizeEx(h, &size) || size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > limit)
        return std::nullopt;
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read{};
    if (!bytes.empty() && (!ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size())) return std::nullopt;
    return bytes;
}
fs::path packagePathIn(const fs::path& root, std::string_view path) {
    auto native = widen(path);
    std::replace(native.begin(), native.end(), L'/', L'\\');
    return root / native;
}
std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR value{};
    const auto hr = SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &value);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(value, &CoTaskMemFree);
    if (FAILED(hr) || !value || !*value) fail("A Windows user folder is unavailable");
    return value;
}

// --- Registry ----------------------------------------------------------------
struct RegistryValue {
    std::wstring name;
    DWORD type{};
    std::vector<BYTE> data;
};
struct KeySnapshot {
    bool existed{};
    std::vector<RegistryValue> values;
};
KeySnapshot snapshotKey(const std::wstring& path) {
    KeySnapshot s;
    Key k;
    const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &k.value);
    if (opened == ERROR_FILE_NOT_FOUND) return s;
    if (opened != ERROR_SUCCESS) windowsFailure("Reading the uninstall entry failed", static_cast<DWORD>(opened));
    s.existed = true;
    DWORD count{}, nameMax{}, dataMax{};
    if (RegQueryInfoKeyW(k.value, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &count, &nameMax, &dataMax, nullptr, nullptr) != ERROR_SUCCESS)
        fail("Reading the uninstall entry failed");
    for (DWORD n = 0; n < count; ++n) {
        std::wstring name(nameMax + 1, L'\0');
        std::vector<BYTE> data(dataMax);
        DWORD nameLength = nameMax + 1, type{}, dataLength = dataMax;
        if (RegEnumValueW(k.value, n, name.data(), &nameLength, nullptr, &type, data.data(), &dataLength) != ERROR_SUCCESS) continue;
        name.resize(nameLength);
        data.resize(dataLength);
        s.values.push_back({std::move(name), type, std::move(data)});
    }
    return s;
}
void restoreKey(const std::wstring& path, const KeySnapshot& s) {
    if (!s.existed) {
        // Only Setup's own values: delete them, then the key when empty.
        Key k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k.value) == ERROR_SUCCESS) {
            for (const auto& v : snapshotKey(path).values) RegDeleteValueW(k.value, v.name.c_str());
        }
        RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str());
        return;
    }
    Key k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &k.value, nullptr) != ERROR_SUCCESS)
        fail("Restoring the uninstall entry failed");
    for (const auto& v : snapshotKey(path).values) RegDeleteValueW(k.value, v.name.c_str());
    for (const auto& v : s.values)
        RegSetValueExW(k.value, v.name.c_str(), 0, v.type, v.data.empty() ? nullptr : v.data.data(), static_cast<DWORD>(v.data.size()));
}
std::optional<std::wstring> readString(const std::wstring& key, std::wstring_view name) {
    DWORD bytes{};
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), std::wstring(name).c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS || !bytes || bytes > 65536)
        return std::nullopt;
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), std::wstring(name).c_str(), RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) return std::nullopt;
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}
std::optional<RegistryValue> readValue(const std::wstring& key, std::wstring_view name) {
    Key k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE, &k.value) != ERROR_SUCCESS) return std::nullopt;
    const std::wstring n(name);
    DWORD type{}, bytes{};
    if (RegQueryValueExW(k.value, n.c_str(), nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || bytes > 65536) return std::nullopt;
    RegistryValue v{n, type, std::vector<BYTE>(bytes)};
    if (RegQueryValueExW(k.value, n.c_str(), nullptr, &type, v.data.data(), &bytes) != ERROR_SUCCESS) return std::nullopt;
    v.data.resize(bytes);
    return v;
}
void restoreValue(const std::wstring& key, std::wstring_view name, const std::optional<RegistryValue>& value) {
    if (!value) { RegDeleteKeyValueW(HKEY_CURRENT_USER, key.c_str(), std::wstring(name).c_str()); return; }
    Key k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k.value, nullptr) != ERROR_SUCCESS) return;
    RegSetValueExW(k.value, value->name.c_str(), 0, value->type, value->data.empty() ? nullptr : value->data.data(), static_cast<DWORD>(value->data.size()));
}
void setString(HKEY key, const wchar_t* name, const std::wstring& value) {
    if (RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS)
        fail("Writing the uninstall entry failed");
}
void setNumber(HKEY key, const wchar_t* name, DWORD value) {
    if (RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) != ERROR_SUCCESS) fail("Writing the uninstall entry failed");
}

// --- Notification identity ---------------------------------------------------
native::AppNotificationIdentity identityFor(const InstallLayout& l, const fs::path& root) {
    return {std::string(installedAppUserModelID), std::string(installedActivatorCLSID), pathText(root / applicationExecutable), pathText(root),
            pathText(l.shortcut())};
}
void openClasses(const InstallLayout& l, Key& k, bool create) {
    const auto status = create
        ? RegCreateKeyExW(HKEY_CURRENT_USER, l.classesKey().c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &k.value, nullptr)
        : RegOpenKeyExW(HKEY_CURRENT_USER, l.classesKey().c_str(), 0, KEY_READ | KEY_WRITE, &k.value);
    if (status != ERROR_SUCCESS && !(status == ERROR_FILE_NOT_FOUND && !create)) windowsFailure("Opening the per-user Classes key failed", static_cast<DWORD>(status));
}
native::AppNotificationRegistration inspectIdentity(HKEY classes, const native::AppNotificationIdentity& id) {
    native::AppNotificationRegistration state{native::AppNotificationRegistration::conflict};
    const auto hr = native::inspectAppNotificationRegistration(classes, id, &state);
    if (FAILED(hr)) throw PackageError("Inspecting the Start menu registration failed (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ")");
    return state;
}

// --- Running EndfieldHUD (Restart Manager) -------------------------------------
class RestartSession {
public:
    RestartSession() {
        WCHAR key[CCH_RM_SESSION_KEY + 1]{};
        if (const auto e = RmStartSession(&session_, 0, key); e != ERROR_SUCCESS) windowsFailure("The Restart Manager is unavailable", e);
    }
    ~RestartSession() { RmEndSession(session_); }
    RestartSession(const RestartSession&) = delete;
    RestartSession& operator=(const RestartSession&) = delete;
    void files(const std::vector<std::wstring>& paths) {
        std::vector<LPCWSTR> raw;
        raw.reserve(paths.size());
        for (const auto& p : paths) raw.push_back(p.c_str());
        // Registered in bounded batches.
        for (std::size_t at = 0; at < raw.size(); at += 256) {
            const auto n = static_cast<UINT>(std::min<std::size_t>(256, raw.size() - at));
            if (const auto e = RmRegisterResources(session_, n, raw.data() + at, 0, nullptr, 0, nullptr); e != ERROR_SUCCESS)
                windowsFailure("Registering files with the Restart Manager failed", e);
        }
    }
    void processes(std::vector<RM_UNIQUE_PROCESS> list) {
        if (list.empty()) return;
        if (const auto e = RmRegisterResources(session_, 0, nullptr, static_cast<UINT>(list.size()), list.data(), 0, nullptr); e != ERROR_SUCCESS)
            windowsFailure("Registering EndfieldHUD with the Restart Manager failed", e);
    }
    std::vector<RM_PROCESS_INFO> list() {
        for (int attempt = 0; attempt < 8; ++attempt) {
            UINT needed{}, count{};
            DWORD reasons{};
            auto e = RmGetList(session_, &needed, &count, nullptr, &reasons);
            if (e == ERROR_SUCCESS) return {};
            if (e != ERROR_MORE_DATA) windowsFailure("Listing programs that use EndfieldHUD files failed", e);
            std::vector<RM_PROCESS_INFO> out(needed);
            count = needed;
            e = RmGetList(session_, &needed, &count, out.data(), &reasons);
            if (e == ERROR_SUCCESS) { out.resize(count); return out; }
            if (e != ERROR_MORE_DATA) windowsFailure("Listing programs that use EndfieldHUD files failed", e);
        }
        fail("Programs using EndfieldHUD files kept changing");
    }
    DWORD shutdown() { return RmShutdown(session_, 0, nullptr); }
    DWORD restart() { return RmRestart(session_, 0, nullptr); }
private:
    DWORD session_{};
};
std::uint64_t fileTime(const FILETIME& t) noexcept { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
std::optional<std::wstring> processImage(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return std::nullopt;
    std::wstring image(32768, L'\0');
    DWORD size = static_cast<DWORD>(image.size());
    const bool ok = QueryFullProcessImageNameW(p, 0, image.data(), &size);
    CloseHandle(p);
    if (!ok) return std::nullopt;
    image.resize(size);
    return image;
}
// EndfieldHUD.exe processes started from `root` (only these are ever closed).
std::vector<RunningApplication> runningFrom(const fs::path& root, const std::optional<InstallManifest>& manifest, std::uint32_t excluded) {
    std::vector<std::wstring> files;
    if (manifest) for (const auto& f : manifest->files) {
        const auto p = packagePathIn(root, f.path);
        if (pathExists(p)) files.push_back(p.native());
    }
    const auto exe = root / applicationExecutable;
    if (pathExists(exe) && std::find(files.begin(), files.end(), exe.native()) == files.end()) files.push_back(exe.native());
    if (files.empty()) return {};
    RestartSession session;
    session.files(files);
    std::vector<RunningApplication> out;
    for (const auto& info : session.list()) {
        const auto pid = info.Process.dwProcessId;
        if (pid == GetCurrentProcessId() || pid == excluded) continue;
        const auto image = processImage(pid);
        if (!image || !samePath(*image, exe)) continue;
        out.push_back({pid, fileTime(info.Process.ProcessStartTime), info.strAppName, info.bRestartable != FALSE});
    }
    return out;
}
class ApplicationStop {
public:
    explicit ApplicationStop(std::vector<RunningApplication> apps, fs::path executable) : apps_(std::move(apps)), executable_(std::move(executable)) {}
    bool stop() {
        std::vector<RM_UNIQUE_PROCESS> list;
        for (const auto& a : apps_) {
            RM_UNIQUE_PROCESS p{};
            p.dwProcessId = a.processId;
            p.ProcessStartTime.dwLowDateTime = static_cast<DWORD>(a.startTime);
            p.ProcessStartTime.dwHighDateTime = static_cast<DWORD>(a.startTime >> 32);
            list.push_back(p);
        }
        session_.processes(std::move(list));
        // Graceful only: WM_QUERYENDSESSION/WM_ENDSESSION (ENDSESSION_CLOSEAPP);
        // EndfieldHUD saves its documents before it exits.
        if (session_.shutdown() != ERROR_SUCCESS) return false;
        for (const auto& a : apps_) {
            HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, a.processId);
            if (!p) continue;
            const auto waited = WaitForSingleObject(p, 15000);
            CloseHandle(p);
            if (waited != WAIT_OBJECT_0) return false;
        }
        stopped_ = true;
        return true;
    }
    // Brings back what was closed: the Restart Manager restarts the processes
    // registered with RegisterApplicationRestart ("--login"); otherwise the
    // executable is started once with --login.
    bool restart() {
        if (!stopped_) return false;
        stopped_ = false;
        const bool registered = std::all_of(apps_.begin(), apps_.end(), [](const RunningApplication& a) { return a.restartable; });
        if (registered && session_.restart() == ERROR_SUCCESS) return true;
        std::wstring command = L"\"" + executable_.native() + L"\" --login";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        const auto folder = executable_.parent_path();
        if (!CreateProcessW(executable_.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup, &process)) return false;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }
private:
    std::vector<RunningApplication> apps_;
    fs::path executable_;
    RestartSession session_;
    bool stopped_{};
};

void inject(const std::optional<InstallStep>& failAfter, InstallStep step) {
    if (failAfter && *failAfter == step) throw PackageError("Injected failure after " + std::string(installStepName(step)));
}
std::string text(SetupText id, core::Language language) { return std::string(setupText(id, language)); }

bool validDataRoot(const fs::path& base) {
    const auto marker = readSmallFile(base / "root.json", 64 * 1024);
    if (!marker) return false;
    try {
        const auto json = Json::parse(*marker, 64 * 1024);
        return json.isObject() && json["format"].isString() && json["format"].string() == ehud::data::applicationRootFormat;
    } catch (...) {
        return false;
    }
}
// --- Install transaction -----------------------------------------------------
class Transaction {
public:
    Transaction(const InstallLayout& layout, std::vector<std::string>& warnings) : l_(layout), warnings_(warnings) {}

    void extract(const SetupPayload& payload, const std::function<void(double)>& progress) {
        const auto parent = l_.installRoot.parent_path();
        createDirectories(parent);
        staging_ = parent / (L"." + l_.installRoot.filename().native() + L".staging-" + widen(token()));
        if (!CreateDirectoryW(staging_.c_str(), nullptr)) windowsFailure("Creating the staging folder failed");
        const auto& m = payload.manifest();
        const double total = static_cast<double>(std::max<std::uint64_t>(m.storedBytes(), 1));
        std::uint64_t done = 0;
        for (const auto& f : m.files) {
            const auto destination = packagePathIn(staging_, f.path);
            createDirectories(destination.parent_path());
            payload.extract(f, destination, [&](std::uint64_t n) { done += n; if (progress) progress(static_cast<double>(done) / total); });
        }
        writeNewFile(staging_ / installManifestName, encodeInstallManifest(installManifestFor(m, utcNow())));
    }
    void discardStaging() {
        if (!staging_.empty()) { (void)removeTree(staging_); staging_.clear(); }
    }
    void swap() {
        const auto parent = l_.installRoot.parent_path();
        DWORD e{};
        if (pathExists(l_.installRoot)) {
            backup_ = parent / (L"." + l_.installRoot.filename().native() + L".backup-" + widen(token()));
            if (!moveWithRetry(l_.installRoot, backup_, &e)) { backup_.clear(); windowsFailure("The EndfieldHUD folder is in use and could not be replaced", e); }
        }
        if (!moveWithRetry(staging_, l_.installRoot, &e)) {
            if (!backup_.empty() && moveWithRetry(backup_, l_.installRoot)) backup_.clear();
            windowsFailure("The new EndfieldHUD folder could not be moved into place", e);
        }
        staging_.clear();
        swapped_ = true;
    }
    void writeUninstallEntry(const PayloadManifest& m, const std::wstring& isolationArguments) {
        uninstall_ = snapshotKey(l_.uninstallKey());
        Key k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, l_.uninstallKey().c_str(), 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &k.value, nullptr) != ERROR_SUCCESS)
            fail("Creating the uninstall entry failed");
        const auto exe = l_.executable().native(), uninstaller = l_.uninstaller().native();
        const std::set<std::wstring> ours{L"DisplayName", L"DisplayVersion", L"Publisher", L"InstallLocation", L"InstallDate", L"DisplayIcon",
            L"UninstallString", L"QuietUninstallString", L"URLInfoAbout", L"HelpLink", L"EstimatedSize", L"NoModify", L"NoRepair",
            L"VersionMajor", L"VersionMinor", L"Comments"};
        for (const auto& v : uninstall_->values)
            if (!ours.contains(v.name)) RegDeleteValueW(k.value, v.name.c_str());
        setString(k.value, L"DisplayName", widen(productName));
        setString(k.value, L"DisplayVersion", widen(m.version.dotted()));
        setString(k.value, L"Publisher", widen(publisherName));
        setString(k.value, L"InstallLocation", l_.installRoot.native());
        setString(k.value, L"InstallDate", installDate());
        setString(k.value, L"DisplayIcon", exe + L",0");
        setString(k.value, L"UninstallString", L"\"" + uninstaller + L"\"" + isolationArguments);
        setString(k.value, L"QuietUninstallString", L"\"" + uninstaller + L"\" --quiet" + isolationArguments);
        setString(k.value, L"URLInfoAbout", widen(productURL));
        setString(k.value, L"HelpLink", widen(productURL));
        setString(k.value, L"Comments", widen("Windows build of EndfieldHUD " + m.version.display() + " (" + m.commit.substr(0, 12) + ")"));
        setNumber(k.value, L"EstimatedSize", static_cast<DWORD>(std::min<std::uint64_t>((m.installedBytes() + 1023) / 1024, 0xffffffffu)));
        setNumber(k.value, L"NoModify", 1);
        setNumber(k.value, L"NoRepair", 1);
        setNumber(k.value, L"VersionMajor", static_cast<DWORD>(m.version.major));
        setNumber(k.value, L"VersionMinor", static_cast<DWORD>(m.version.minor));
    }
    void registerIdentity(const std::optional<fs::path>& previousRoot) {
        const auto identity = identityFor(l_, l_.installRoot);
        Key classes;
        openClasses(l_, classes, true);
        auto state = inspectIdentity(classes.value, identity);
        if (state == native::AppNotificationRegistration::matching) return;
        if (state == native::AppNotificationRegistration::conflict && previousRoot && !samePath(*previousRoot, l_.installRoot)) {
            // A previous install elsewhere: replace exactly its own registration.
            const auto previous = identityFor(l_, *previousRoot);
            if (inspectIdentity(classes.value, previous) == native::AppNotificationRegistration::matching &&
                SUCCEEDED(native::removeAppNotificationIdentity(classes.value, previous))) {
                removedPrevious_ = previous;
                state = inspectIdentity(classes.value, identity);
            }
        }
        if (state == native::AppNotificationRegistration::conflict) {
            warnings_.push_back("The Start menu entry and Calendar reminder notifications were not registered: another EndfieldHUD registration already uses " +
                                pathText(l_.shortcut()));
            return;
        }
        const auto hr = native::installAppNotificationIdentity(classes.value, identity);
        if (FAILED(hr)) throw PackageError("Creating the Start menu entry failed (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ")");
        installedIdentity_ = identity;
    }
    void applyLoginItem(bool enabled, core::Language language) {
        run_ = readValue(l_.runKey(), registrationName);
        const app::LoginItemRegistration registration(loginLocation(l_));
        if (const auto error = registration.setEnabled(enabled, language)) warnings_.push_back(*error);
    }
    // The explicit launch-at-login choice is also the app's saved preference
    // (app/application.cpp registers at launch only when it is on).
    void saveLaunchAtLogin(bool enabled) {
        try {
            // A fresh profile already defaults to on: nothing to create.
            if (enabled && !pathExists(l_.dataBase / "root.json")) return;
            baseExisted_ = pathExists(l_.dataBase);
            const auto root = ehud::data::openApplicationRoot(l_.dataBase);
            settingsTouched_ = true;
            ehud::data::SettingsStore store(root.data);
            auto value = store.value();
            if (value.boolean("launchAtLogin") == enabled) return;
            previousLaunchAtLogin_ = value.boolean("launchAtLogin");
            value.set("launchAtLogin", enabled);
            (void)store.update(value);
        } catch (const std::exception& e) {
            warnings_.push_back(std::string("The EndfieldHUD launch-at-login setting was not saved: ") + e.what());
        }
    }
    void commit(const std::optional<InstallManifest>& previous) {
        if (backup_.empty()) return;
        // Carry over anything Setup did not install (never deleted).
        std::set<std::string> known{foldedPackagePath(installManifestName)};
        if (previous) for (const auto& f : previous->files) known.insert(foldedPackagePath(f.path));
        bool keep = false;
        std::vector<fs::path> pending{backup_};
        while (!pending.empty()) {
            const auto folder = pending.back();
            pending.pop_back();
            for (const auto& child : children(folder)) {
                const auto a = GetFileAttributesW(child.c_str());
                if (a == INVALID_FILE_ATTRIBUTES) continue;
                const auto relative = pathText(child.lexically_relative(backup_));
                auto key = relative;
                std::replace(key.begin(), key.end(), '\\', '/');
                key = foldedPackagePath(key);
                if ((a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT)) { pending.push_back(child); continue; }
                if (known.contains(key)) continue;
                const auto target = l_.installRoot / child.lexically_relative(backup_);
                if (!pathExists(target)) {
                    std::error_code error;
                    fs::create_directories(target.parent_path(), error);
                    if (!error && moveWithRetry(child, target)) continue;
                }
                keep = true;
            }
        }
        if (keep) {
            const auto kept = backup_.parent_path() / (L"." + l_.installRoot.filename().native() + L".kept-" + widen(token()));
            if (moveWithRetry(backup_, kept)) backup_ = kept;
            warnings_.push_back("Files that Setup did not install were kept in " + pathText(backup_));
        } else if (const auto left = removeTree(backup_); !left.empty()) {
            warnings_.push_back("The previous EndfieldHUD folder could not be removed completely: " + pathText(backup_));
        }
        backup_.clear();
    }
    void rollback() noexcept {
        try {
            if (settingsTouched_ && !baseExisted_) {
                if (validDataRoot(l_.dataBase)) (void)removeTree(l_.dataBase); // created by this run only
            } else if (previousLaunchAtLogin_) {
                const auto root = ehud::data::openApplicationRoot(l_.dataBase);
                ehud::data::SettingsStore store(root.data);
                auto value = store.value();
                value.set("launchAtLogin", *previousLaunchAtLogin_);
                (void)store.update(value);
            }
        } catch (...) {}
        try {
            if (run_) restoreValue(l_.runKey(), registrationName, *run_);
        } catch (...) {}
        try {
            Key classes;
            openClasses(l_, classes, false);
            if (classes.value && installedIdentity_) (void)native::removeAppNotificationIdentity(classes.value, *installedIdentity_);
            if (classes.value && removedPrevious_) (void)native::installAppNotificationIdentity(classes.value, *removedPrevious_);
        } catch (...) {}
        try {
            if (uninstall_) restoreKey(l_.uninstallKey(), *uninstall_);
        } catch (...) {}
        try {
            if (swapped_) {
                const auto failed = l_.installRoot.parent_path() / (L"." + l_.installRoot.filename().native() + L".failed-" + widen(token()));
                if (moveWithRetry(l_.installRoot, failed)) {
                    if (!backup_.empty() && moveWithRetry(backup_, l_.installRoot)) backup_.clear();
                    (void)removeTree(failed);
                }
            }
        } catch (...) {}
        discardStaging();
    }
    static app::LoginItemLocation loginLocation(const InstallLayout& l) {
        if (!l.isolated()) return app::LoginItemLocation::production(l.executable());
        return {HKEY_CURRENT_USER, l.runKey(), l.approvedKey(), std::wstring(registrationName), l.loginCommand()};
    }
private:
    const InstallLayout& l_;
    std::vector<std::string>& warnings_;
    fs::path staging_, backup_;
    bool swapped_{};
    std::optional<KeySnapshot> uninstall_;
    std::optional<native::AppNotificationIdentity> installedIdentity_, removedPrevious_;
    std::optional<std::optional<RegistryValue>> run_;
    bool settingsTouched_{}, baseExisted_{true};
    std::optional<bool> previousLaunchAtLogin_;
};

std::wstring isolationArguments(const InstallLayout& l) {
    if (!l.isolated()) return {};
    std::wstring out = L" --test-registry \"" + l.registryRoot + L"\" --programs-folder \"" + l.programsFolder.native() + L"\" --data-base \"" +
                       l.dataBase.native() + L"\"";
    if (!l.workFolder.empty()) out += L" --work-folder \"" + l.workFolder.native() + L"\"";
    return out;
}
bool dataRootSettingsReadable(const InstallLayout& l) {
    return ordinaryDirectory(l.dataBase) && pathExists(l.dataBase / "root.json") &&
           pathExists(l.dataBase / std::string(ehud::data::applicationRootDataDirectory) / "settings.json");
}
std::optional<ehud::data::Settings> savedSettings(const InstallLayout& l) {
    try {
        if (!dataRootSettingsReadable(l)) return std::nullopt;
        const auto root = ehud::data::openApplicationRoot(l.dataBase);
        return ehud::data::SettingsStore(root.data).value();
    } catch (...) {
        return std::nullopt;
    }
}
bool removeUserData(const InstallLayout& l, std::vector<std::string>& warnings) {
    const auto a = attributesOf(l.dataBase);
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT) || !validDataRoot(l.dataBase)) {
        warnings.push_back("The data folder was not recognized as EndfieldHUD data and was kept: " + pathText(l.dataBase));
        return false;
    }
    if (!l.isolated()) {
        // Another EndfieldHUD (e.g. a portable copy) still owns the data root.
        const auto names = app::productionSingleInstanceNames();
        if (HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, names.mutex.c_str())) {
            CloseHandle(m);
            warnings.push_back("EndfieldHUD is still running, so its data was kept: " + pathText(l.dataBase));
            return false;
        }
    }
    if (const auto left = removeTree(l.dataBase); !left.empty()) {
        warnings.push_back("Some EndfieldHUD data could not be deleted: " + pathText(left.front()));
        return false;
    }
    return true;
}
} // namespace

std::wstring widen(std::string_view value) {
    if (value.empty()) return {};
    const auto n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (n <= 0) throw PackageError("Text is not valid UTF-8");
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), n);
    return out;
}
std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const auto n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
bool samePath(const fs::path& a, const fs::path& b) noexcept {
    const auto& x = a.native();
    const auto& y = b.native();
    if (x.size() > 32767 || y.size() > 32767) return false;
    return CompareStringOrdinal(x.c_str(), static_cast<int>(x.size()), y.c_str(), static_cast<int>(y.size()), TRUE) == CSTR_EQUAL;
}
std::vector<fs::path> removeTree(const fs::path& path) {
    std::vector<fs::path> remaining;
    removeTreeInto(path, remaining);
    return remaining;
}

std::wstring InstallLayout::uninstallKey() const {
    return isolated() ? registryRoot + L"\\Uninstall\\" + std::wstring(registrationName)
                      : L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + std::wstring(registrationName);
}
std::wstring InstallLayout::runKey() const {
    return isolated() ? registryRoot + L"\\Run" : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}
std::wstring InstallLayout::approvedKey() const {
    return isolated() ? registryRoot + L"\\StartupApproved\\Run" : L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
}
std::wstring InstallLayout::classesKey() const { return isolated() ? registryRoot + L"\\Classes" : L"Software\\Classes"; }
std::wstring InstallLayout::loginCommand() const { return app::LoginItemLocation::production(executable()).command; }

InstallLayout InstallLayout::production() {
    InstallLayout l;
    l.installRoot = fs::path(knownFolder(FOLDERID_UserProgramFiles)).lexically_normal() / productName;
    l.programsFolder = fs::path(knownFolder(FOLDERID_Programs)).lexically_normal();
    l.dataBase = ehud::data::localApplicationDataBase();
    std::wstring temp(MAX_PATH + 1, L'\0');
    const auto n = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (n == 0 || n > temp.size()) fail("The temporary folder is unavailable");
    temp.resize(n);
    l.workFolder = fs::path(temp).lexically_normal();
    if (l.workFolder.has_filename() == false) l.workFolder = l.workFolder.parent_path();
    return l;
}
InstallLayout InstallLayout::fromArguments(const SetupArguments& a) {
    auto l = a.isolated() ? InstallLayout{} : production();
    if (a.isolated()) {
        l.installRoot = widen(a.installRoot);
        l.programsFolder = widen(a.programsFolder);
        l.dataBase = widen(a.dataBase);
        l.registryRoot = widen(a.testRegistry);
        l.workFolder = a.workFolder.empty() ? production().workFolder : fs::path(widen(a.workFolder));
    } else if (!a.installRoot.empty()) {
        l.installRoot = widen(a.installRoot); // the relocated uninstall worker
    }
    return l;
}

std::string_view outcomeName(Outcome o) noexcept {
    switch (o) {
    case Outcome::installed: return "installed";
    case Outcome::upgraded: return "upgraded";
    case Outcome::repaired: return "repaired";
    case Outcome::uninstalled: return "uninstalled";
    case Outcome::refused: return "refused";
    case Outcome::failed: return "failed";
    case Outcome::cancelled: return "cancelled";
    }
    return "failed";
}
std::string_view refusalName(Refusal r) noexcept {
    switch (r) {
    case Refusal::none: return "none";
    case Refusal::newerInstalled: return "newer-installed";
    case Refusal::foreignFolder: return "foreign-folder";
    case Refusal::incomplete: return "incomplete";
    case Refusal::path: return "path";
    case Refusal::notInstalled: return "not-installed";
    case Refusal::closeFailed: return "close-failed";
    }
    return "none";
}
std::string encodeOperationResult(const OperationResult& r) {
    Json::Object root;
    root["outcome"] = std::string(outcomeName(r.outcome));
    root["refusal"] = std::string(refusalName(r.refusal));
    root["previous"] = r.previous ? Json(r.previous->dotted()) : Json(nullptr);
    root["current"] = r.current ? Json(r.current->dotted()) : Json(nullptr);
    root["installRoot"] = pathText(r.installRoot);
    Json::Array warnings, kept, failed;
    for (const auto& w : r.warnings) warnings.emplace_back(w);
    for (const auto& k : r.keptFiles) kept.emplace_back(k);
    for (const auto& f : r.failedFiles) failed.emplace_back(f);
    root["warnings"] = std::move(warnings);
    root["keptFiles"] = std::move(kept);
    root["failedFiles"] = std::move(failed);
    root["error"] = r.error;
    root["stoppedApplications"] = static_cast<std::int64_t>(r.stoppedApplications);
    root["restarted"] = r.restarted;
    root["dataRemoved"] = r.dataRemoved;
    root["launchAtLogin"] = r.launchAtLogin;
    return Json(std::move(root)).encode() + "\n";
}

std::optional<InstallManifest> readInstallManifest(const fs::path& root) {
    try {
        if (!ordinaryDirectory(root)) return std::nullopt;
        const auto bytes = readSmallFile(root / installManifestName, maximumManifestBytes);
        if (!bytes) return std::nullopt;
        return decodeInstallManifest(*bytes);
    } catch (...) {
        return std::nullopt;
    }
}
std::vector<RunningApplication> runningApplications(const InstallLayout& l, const std::optional<InstallManifest>& manifest, std::uint32_t excluded) {
    return runningFrom(l.installRoot, manifest, excluded);
}
bool launchInstalledApplication(const InstallLayout& l, bool login) {
    const auto exe = l.executable();
    std::wstring command = L"\"" + exe.native() + L"\"" + (login ? L" --login" : L"");
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    const auto folder = l.installRoot;
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup, &process)) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
void removeStaleUninstallWorkers(const fs::path& workFolder) {
    if (!ordinaryDirectory(workFolder)) return;
    for (const auto& child : children(workFolder))
        if (child.filename().native().starts_with(L"EndfieldHUD-Uninstall-") && ordinaryDirectory(child)) (void)removeTree(child);
}
bool launchAtLoginPreference(const InstallLayout& l) {
    const auto settings = savedSettings(l);
    return settings ? settings->boolean("launchAtLogin") : true; // Models.swift: launchAtLogin = true
}
core::Language setupLanguage(const InstallLayout& l) {
    if (const auto settings = savedSettings(l))
        if (const auto saved = core::languageFromSetting(settings->string("language")); saved && *saved != core::Language::system) return *saved;
    ULONG count{}, size{};
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) || !size) return core::Language::english;
    std::wstring buffer(size, L'\0');
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size)) return core::Language::english;
    std::vector<std::string> names;
    for (const wchar_t* at = buffer.c_str(); *at; at += wcslen(at) + 1) names.push_back(narrow(at));
    std::vector<std::string_view> views(names.begin(), names.end());
    const auto resolved = core::resolveLanguage(views);
    return resolved == core::Language::system ? core::Language::english : resolved;
}

void recoverInterruptedInstall(const InstallLayout& l, std::vector<std::string>& warnings) {
    const auto parent = l.installRoot.parent_path();
    if (!ordinaryDirectory(parent)) return;
    const auto prefix = L"." + l.installRoot.filename().native() + L".";
    std::vector<fs::path> staging, backups, failed;
    for (const auto& child : children(parent)) {
        const auto name = child.filename().native();
        if (!name.starts_with(prefix)) continue;
        const auto rest = std::wstring_view(name).substr(prefix.size());
        if (rest.starts_with(L"staging-")) staging.push_back(child);
        else if (rest.starts_with(L"backup-")) backups.push_back(child);
        else if (rest.starts_with(L"failed-")) failed.push_back(child);
    }
    if (!pathExists(l.installRoot) && !backups.empty()) {
        // An update stopped between the two renames: restore the previous install.
        std::sort(backups.begin(), backups.end(), [](const fs::path& a, const fs::path& b) {
            WIN32_FILE_ATTRIBUTE_DATA x{}, y{};
            GetFileAttributesExW(a.c_str(), GetFileExInfoStandard, &x);
            GetFileAttributesExW(b.c_str(), GetFileExInfoStandard, &y);
            return CompareFileTime(&x.ftLastWriteTime, &y.ftLastWriteTime) > 0;
        });
        for (auto it = backups.begin(); it != backups.end(); ++it) {
            if (readInstallManifest(*it) && moveWithRetry(*it, l.installRoot)) {
                warnings.push_back("Restored the previous EndfieldHUD after an interrupted update");
                backups.erase(it);
                break;
            }
        }
    }
    for (const auto& p : staging) (void)removeTree(p);
    for (const auto& p : failed) (void)removeTree(p);
    if (readInstallManifest(l.installRoot))
        for (const auto& p : backups) (void)removeTree(p);
}

InstallPlan planInstall(const PayloadManifest& payload, InstallLayout layout, bool allowDowngrade) {
    InstallPlan plan;
    if (!layout.isolated()) {
        // An existing registration keeps its folder.
        if (const auto location = readString(layout.uninstallKey(), L"InstallLocation")) {
            const fs::path registered = fs::path(*location).lexically_normal();
            if (registered.is_absolute() && !samePath(registered, layout.installRoot) && readInstallManifest(registered)) layout.installRoot = registered;
        }
    }
    recoverInterruptedInstall(layout, plan.warnings);
    plan.layout = layout;
    plan.installed = readInstallManifest(layout.installRoot);
    plan.launchAtLogin = launchAtLoginPreference(layout);
    const auto refuse = [&](Refusal r) { plan.intended = Outcome::refused; plan.refusal = r; return plan; };
    // Paths must stay below MAX_PATH even without the long-path opt-in.
    std::size_t longest = 0;
    for (const auto& f : payload.files) longest = std::max(longest, f.path.size());
    const auto rootLength = layout.installRoot.native().size();
    if (!layout.installRoot.is_absolute() || !layout.installRoot.has_parent_path() || rootLength + 24 + longest + 1 >= MAX_PATH)
        return refuse(Refusal::path);
    const auto a = attributesOf(layout.installRoot);
    if (a != INVALID_FILE_ATTRIBUTES) {
        if (!(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return refuse(Refusal::path);
        if (!plan.installed && !children(layout.installRoot).empty()) return refuse(Refusal::foreignFolder);
    }
    if (!payload.complete && !layout.isolated()) return refuse(Refusal::incomplete);
    if (plan.installed) {
        if (plan.installed->version > payload.version && !allowDowngrade) return refuse(Refusal::newerInstalled);
        plan.intended = plan.installed->version == payload.version ? Outcome::repaired : Outcome::upgraded;
    } else {
        plan.intended = Outcome::installed;
    }
    return plan;
}

OperationResult install(const SetupPayload& payload, const InstallPlan& plan, const InstallOptions& options, const OperationCallbacks& callbacks) {
    const auto& l = plan.layout;
    if (options.failAfter && !l.isolated()) throw std::logic_error("Failure injection is only available in isolated layouts");
    OperationResult r;
    r.current = payload.manifest().version;
    r.installRoot = l.installRoot;
    r.warnings = plan.warnings;
    if (plan.installed) r.previous = plan.installed->version;
    if (plan.intended == Outcome::refused) { r.outcome = Outcome::refused; r.refusal = plan.refusal; return r; }
    const auto progress = [&](double fraction, SetupText step) { if (callbacks.progress) callbacks.progress(fraction, step); };
    Apartment apartment;
    Transaction tx(l, r.warnings);
    std::optional<ApplicationStop> stop;
    try {
        progress(0, SetupText::stepExtract);
        tx.extract(payload, [&](double f) { progress(0.8 * f, SetupText::stepExtract); });
        inject(options.failAfter, InstallStep::extract);
        auto running = runningFrom(l.installRoot, plan.installed, 0);
        if (!running.empty()) {
            if (callbacks.confirmStop && !callbacks.confirmStop(running)) {
                tx.discardStaging();
                r.outcome = Outcome::cancelled;
                return r;
            }
            progress(0.82, SetupText::stepStop);
            stop.emplace(running, l.executable());
            if (!stop->stop()) {
                tx.discardStaging();
                r.outcome = Outcome::refused;
                r.refusal = Refusal::closeFailed;
                r.error = text(SetupText::closeFailed, options.language);
                return r;
            }
            r.stoppedApplications = running.size();
        }
        inject(options.failAfter, InstallStep::stop);
        progress(0.88, SetupText::stepRegister);
        // The registered folder before this run (a moved install replaces exactly its own Start menu entry).
        std::optional<fs::path> previousRoot;
        if (const auto location = readString(l.uninstallKey(), L"InstallLocation")) previousRoot = fs::path(*location);
        tx.swap();
        inject(options.failAfter, InstallStep::swap);
        tx.writeUninstallEntry(payload.manifest(), isolationArguments(l));
        inject(options.failAfter, InstallStep::uninstallEntry);
        tx.registerIdentity(previousRoot);
        inject(options.failAfter, InstallStep::identity);
        const bool login = options.launchAtLogin.value_or(plan.launchAtLogin);
        tx.applyLoginItem(login, options.language);
        r.launchAtLogin = login;
        inject(options.failAfter, InstallStep::loginItem);
        if (options.launchAtLogin) tx.saveLaunchAtLogin(*options.launchAtLogin);
        inject(options.failAfter, InstallStep::settings);
        progress(0.96, SetupText::stepFinish);
        inject(options.failAfter, InstallStep::commit);
        tx.commit(plan.installed);
    } catch (const std::exception& e) {
        tx.rollback();
        if (stop) r.restarted = stop->restart();
        r.outcome = Outcome::failed;
        r.error = e.what();
        return r;
    }
    if (stop) r.restarted = stop->restart();
    r.outcome = plan.intended;
    progress(1, SetupText::stepFinish);
    return r;
}

OperationResult uninstall(const InstallLayout& l, const UninstallOptions& options, const OperationCallbacks& callbacks, ScheduledNotifications* notifications) {
    OperationResult r;
    r.installRoot = l.installRoot;
    const auto progress = [&](double fraction, SetupText step) { if (callbacks.progress) callbacks.progress(fraction, step); };
    Apartment apartment;
    recoverInterruptedInstall(l, r.warnings);
    const auto manifest = readInstallManifest(l.installRoot);
    const auto entry = snapshotKey(l.uninstallKey());
    const auto registeredRoot = readString(l.uninstallKey(), L"InstallLocation");
    const bool entryOwnsRoot = entry.existed && (!registeredRoot || samePath(fs::path(*registeredRoot), l.installRoot));
    if (!manifest && !entryOwnsRoot) {
        r.outcome = Outcome::refused;
        r.refusal = Refusal::notInstalled;
        return r;
    }
    if (manifest) r.previous = manifest->version;
    progress(0.05, SetupText::stepStop);
    const auto running = runningFrom(l.installRoot, manifest, options.parentProcess);
    if (!running.empty()) {
        if (callbacks.confirmStop && !callbacks.confirmStop(running)) { r.outcome = Outcome::cancelled; return r; }
        ApplicationStop stop(running, l.executable());
        if (!stop.stop()) {
            r.outcome = Outcome::refused;
            r.refusal = Refusal::closeFailed;
            r.error = text(SetupText::closeFailed, options.language);
            return r;
        }
        r.stoppedApplications = running.size();
    }
    progress(0.2, SetupText::stepRegister);
    // Scheduled reminders first, while the identity still resolves them; then
    // exactly the matching Start menu entry and activator binding.
    try {
        Key classes;
        openClasses(l, classes, false);
        const auto identity = identityFor(l, l.installRoot);
        if (classes.value && inspectIdentity(classes.value, identity) == native::AppNotificationRegistration::matching) {
            if (notifications) {
                try { notifications->clear(); }
                catch (const std::exception& e) { r.warnings.push_back(std::string("Scheduled Calendar reminders could not be removed: ") + e.what()); }
            }
            if (const auto hr = native::removeAppNotificationIdentity(classes.value, identity); FAILED(hr))
                r.warnings.push_back("The Start menu entry could not be removed (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ")");
        }
    } catch (const std::exception& e) {
        r.warnings.push_back(std::string("The Start menu entry could not be inspected: ") + e.what());
    }
    // Launch at login: only a Run value that launches this installation.
    {
        const app::LoginItemRegistration registration(Transaction::loginLocation(l));
        if (registration.read().registered) {
            if (const auto error = registration.setEnabled(false, options.language)) r.warnings.push_back(*error);
            RegDeleteKeyValueW(HKEY_CURRENT_USER, l.approvedKey().c_str(), std::wstring(registrationName).c_str());
        }
    }
    progress(0.4, SetupText::stepExtract);
    if (manifest) {
        for (auto it = manifest->files.rbegin(); it != manifest->files.rend(); ++it) {
            const auto path = packagePathIn(l.installRoot, it->path);
            const auto a = GetFileAttributesW(path.c_str());
            if (a == INVALID_FILE_ATTRIBUTES) continue;
            if (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue; // not what Setup wrote: kept below
            if (!options.relocatedImage.empty() && samePath(path, options.relocatedImage)) {
                // A running image cannot be deleted, but it can be moved out of the folder.
                const auto target = options.relocationFolder / (path.filename().native() + L"." + widen(token()) + L".old");
                if (!moveWithRetry(path, target)) r.failedFiles.push_back(it->path);
                continue;
            }
            if (!deleteFileWithRetry(path)) r.failedFiles.push_back(it->path);
        }
        if (!deleteFileWithRetry(l.manifest())) r.failedFiles.emplace_back(installManifestName);
    }
    // Empty folders bottom-up, then the root; anything left was not ours.
    std::vector<fs::path> folders, pending{l.installRoot};
    while (!pending.empty()) {
        const auto folder = pending.back();
        pending.pop_back();
        if (!ordinaryDirectory(folder)) continue;
        folders.push_back(folder);
        for (const auto& child : children(folder)) if (ordinaryDirectory(child)) pending.push_back(child);
    }
    for (auto it = folders.rbegin(); it != folders.rend(); ++it) (void)removeEmptyFolder(*it);
    if (pathExists(l.installRoot)) {
        std::vector<fs::path> walk{l.installRoot};
        while (!walk.empty() && r.keptFiles.size() < 64) {
            const auto folder = walk.back();
            walk.pop_back();
            for (const auto& child : children(folder)) {
                if (ordinaryDirectory(child)) walk.push_back(child);
                else r.keptFiles.push_back(pathText(child.lexically_relative(l.installRoot)));
            }
        }
    }
    progress(0.8, SetupText::stepFinish);
    if (r.failedFiles.empty() && entryOwnsRoot) RegDeleteTreeW(HKEY_CURRENT_USER, l.uninstallKey().c_str());
    if (options.removeUserData) r.dataRemoved = removeUserData(l, r.warnings);
    r.outcome = r.failedFiles.empty() ? Outcome::uninstalled : Outcome::failed;
    if (!r.failedFiles.empty()) r.error = "Some installed files could not be removed";
    progress(1, SetupText::stepFinish);
    return r;
}

namespace {
class WinRtScheduledNotifications final : public ScheduledNotifications {
public:
    void clear() override {
        // WinRT notification calls need a multithreaded apartment; this
        // thread owns one only for the duration of the call.
        std::exception_ptr error;
        std::thread([&] {
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                {
                    using namespace winrt::Windows::UI::Notifications;
                    const auto id = winrt::to_hstring(installedAppUserModelID);
                    const auto notifier = ToastNotificationManager::CreateToastNotifier(id);
                    for (const auto& scheduled : notifier.GetScheduledToastNotifications()) notifier.RemoveFromSchedule(scheduled);
                    ToastNotificationManager::History().Clear(id);
                }
                winrt::uninit_apartment();
            } catch (...) {
                error = std::current_exception();
            }
        }).join();
        if (error) {
            try { std::rethrow_exception(error); }
            catch (const winrt::hresult_error& e) { throw PackageError("WinRT notification error " + std::to_string(static_cast<unsigned long>(e.code().value))); }
        }
    }
};
} // namespace
std::unique_ptr<ScheduledNotifications> installedScheduledNotifications() { return std::make_unique<WinRtScheduledNotifications>(); }
} // namespace endfield::packaging
#endif
