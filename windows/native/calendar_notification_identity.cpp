#include "native/calendar_notification_identity.hpp"
#include <stdexcept>

namespace endfield::native {
namespace {
bool separator(char c) noexcept { return c == '\\' || c == '/'; }
} // namespace

AppNotificationIdentity calendarNotificationIdentity(std::string_view executable, std::string_view programsFolder) {
    // The working directory is the executable's folder (an executable in a
    // drive root has none and is rejected by the path validation below).
    const auto last = executable.find_last_of("\\/");
    if (last == std::string_view::npos || last + 1 >= executable.size()) throw std::invalid_argument("Calendar notification executable must be an absolute file path");
    auto folder = std::string(executable.substr(0, last));
    while (!programsFolder.empty() && separator(programsFolder.back())) programsFolder.remove_suffix(1);
    AppNotificationIdentity identity{std::string(calendarNotificationAppUserModelID), std::string(calendarNotificationActivatorCLSID),
                                     std::string(executable), std::move(folder),
                                     std::string(programsFolder) + "\\" + std::string(calendarNotificationShortcutName)};
    if (!validAppNotificationIdentity(identity)) throw std::invalid_argument("Invalid Calendar notification identity paths");
    return identity;
}
} // namespace endfield::native

#ifdef _WIN32
#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <system_error>

namespace endfield::native {
namespace {
std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::invalid_argument("Calendar notification path is not valid Unicode");
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}
struct CoText {
    PWSTR value{};
    ~CoText() { CoTaskMemFree(value); }
};
struct Key {
    HKEY value{};
    ~Key() { if (value) RegCloseKey(value); }
};
void hresult(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::system_error(static_cast<int>(hr), std::system_category(), what);
}
} // namespace

AppNotificationIdentity currentCalendarNotificationIdentity() {
    std::wstring executable(MAX_PATH, L'\0');
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Locate EndfieldHUD.exe");
        if (length < executable.size()) { executable.resize(length); break; }
        if (executable.size() >= 32768) throw std::runtime_error("EndfieldHUD.exe path exceeds the Windows path limit");
        executable.resize(executable.size() * 2);
    }
    return calendarNotificationIdentity(utf8(executable), currentUserProgramsFolder());
}

std::string currentUserProgramsFolder() {
    CoText programs;
    hresult(SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_DEFAULT, nullptr, &programs.value), "Locate the Start menu Programs folder");
    return utf8(programs.value);
}

std::optional<std::string> installedCalendarNotificationAppID(HKEY classesRoot, const AppNotificationIdentity& identity) {
    AppNotificationRegistration state{AppNotificationRegistration::conflict};
    hresult(inspectAppNotificationRegistration(classesRoot, identity, &state), "Inspect the Calendar notification registration");
    if (state != AppNotificationRegistration::matching) return std::nullopt;
    return identity.appUserModelID;
}

std::optional<AppNotificationIdentity> installedCurrentCalendarNotificationIdentity() {
    auto identity = currentCalendarNotificationIdentity();
    Key classes;
    const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, KEY_READ, &classes.value);
    if (opened == ERROR_FILE_NOT_FOUND) return std::nullopt;
    if (opened != ERROR_SUCCESS) throw std::system_error(static_cast<int>(opened), std::system_category(), "Open the per-user Classes root");
    if (!installedCalendarNotificationAppID(classes.value, identity)) return std::nullopt;
    return identity;
}

std::optional<std::string> processAppUserModelID() {
    CoText id;
    if (FAILED(GetCurrentProcessExplicitAppUserModelID(&id.value)) || !id.value) return std::nullopt;
    std::string value;
    try { value = utf8(id.value); } catch (const std::invalid_argument&) { return std::nullopt; }
    if (!validAppNotificationID(value)) return std::nullopt;
    return value;
}

HRESULT establishCalendarNotificationProcessIdentity(std::string_view appUserModelID) noexcept {
    try {
        if (const auto current = processAppUserModelID(); current && *current == appUserModelID) return S_FALSE;
    } catch (...) { return E_OUTOFMEMORY; }
    return establishAppNotificationProcessIdentity(appUserModelID);
}

HRESULT removeCalendarNotificationIdentity(HKEY classesRoot, const AppNotificationIdentity& identity, CalendarNotificationProvider& scheduled) noexcept {
    try {
        AppNotificationRegistration state{};
        if (const auto hr = inspectAppNotificationRegistration(classesRoot, identity, &state); FAILED(hr)) return hr;
        if (state == AppNotificationRegistration::missing) return S_OK;
        if (state == AppNotificationRegistration::conflict) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        // Toasts are cancelled while the identity still resolves them; keeping
        // no Calendar ID removes every owned request and nothing foreign.
        cancelObsoleteCalendarNotifications(scheduled, {});
        return removeAppNotificationIdentity(classesRoot, identity);
    } catch (const std::system_error& e) {
        return e.code().category() == std::system_category() && e.code().value() < 0 ? static_cast<HRESULT>(e.code().value()) : E_FAIL;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}
} // namespace endfield::native
#endif
