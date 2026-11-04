// Calendar reminder identity of an unpackaged per-user install
// (native/calendar_notification_identity.hpp). Portable path rules, then on
// Windows the read-only installed lookup and the uninstall order against a
// PRIVATE volatile registry subtree and a temporary folder with a synthetic
// executable that is never run. Test-only AppUserModelID/CLSID values: no
// Start-menu shortcut, HKCU\Software\Classes entry, process identity, COM
// class or scheduled notification is ever created.
#include "native/calendar_notification_identity.hpp"
#include "modules/calendar_model.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace endfield;
namespace n = native;
namespace m = modules;
unsigned checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template <class F> void rejects(F&& f, const char* message) {
    bool rejected{};
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, message);
}

void portable() {
    check(n::validAppNotificationID(n::calendarNotificationAppUserModelID) && n::calendarNotificationAppUserModelID == "io.github.endfieldcharge.EndfieldCharge",
          "The AppUserModelID is the Mac bundle identifier and a valid Windows identity");
    check(n::validAppNotificationCLSID(n::calendarNotificationActivatorCLSID), "The toast activator is one fixed braced CLSID");
    const auto identity = n::calendarNotificationIdentity("C:\\Users\\Owner\\AppData\\Local\\Programs\\EndfieldHUD\\EndfieldHUD.exe",
                                                          "C:\\Users\\Owner\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\");
    check(identity.appUserModelID == n::calendarNotificationAppUserModelID && identity.activatorCLSID == n::calendarNotificationActivatorCLSID,
          "The installed identity carries the fixed AppUserModelID and activator");
    check(identity.workingDirectory == "C:\\Users\\Owner\\AppData\\Local\\Programs\\EndfieldHUD", "The working directory is the executable's folder");
    check(identity.shortcut == "C:\\Users\\Owner\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\EndfieldHUD.lnk",
          "The shortcut is EndfieldHUD.lnk directly in the Programs folder (trailing separators ignored)");
    check(n::appNotificationServerCommand(identity) == "\"C:\\Users\\Owner\\AppData\\Local\\Programs\\EndfieldHUD\\EndfieldHUD.exe\" --notification-activation",
          "LocalServer32 relaunches exactly that executable in activation mode");
    const auto unc = n::calendarNotificationIdentity("\\\\host\\share\\EndfieldHUD\\EndfieldHUD.exe", "\\\\host\\share\\Programs");
    check(unc.workingDirectory == "\\\\host\\share\\EndfieldHUD" && unc.shortcut == "\\\\host\\share\\Programs\\EndfieldHUD.lnk", "UNC installs keep their server root");
    rejects([] { (void)n::calendarNotificationIdentity("EndfieldHUD.exe", "C:\\Programs"); }, "A relative executable is never resolved");
    rejects([] { (void)n::calendarNotificationIdentity("C:\\EndfieldHUD.exe", "C:\\Programs"); }, "A drive-root executable has no working folder");
    rejects([] { (void)n::calendarNotificationIdentity("C:\\Apps\\End\"field.exe", "C:\\Programs"); }, "A quote cannot reach the activation command");
    rejects([] { (void)n::calendarNotificationIdentity("C:\\Apps\\EndfieldHUD.exe", "Programs"); }, "A relative Programs folder is rejected");
    rejects([] { (void)n::calendarNotificationIdentity("C:\\Apps\\EndfieldHUD.exe", ""); }, "An empty Programs folder is rejected");
    rejects([] { (void)n::calendarNotificationIdentity("C:\\Apps\\", "C:\\Programs"); }, "A folder is not an executable");
}

// Records every provider call; owns no notification center.
struct Scheduled final : n::CalendarNotificationProvider {
    std::vector<n::CalendarScheduledNotification> values;
    std::vector<std::string> removed;
    unsigned calls{};
    m::CalendarPermission authorization(bool) override { ++calls; return m::CalendarPermission::authorized; }
    std::vector<n::CalendarScheduledNotification> pending() override { ++calls; return values; }
    void remove(std::string_view id) override {
        ++calls;
        removed.emplace_back(id);
        std::erase_if(values, [&](const auto& v) { return v.identifier == id; });
    }
    void add(const n::CalendarScheduledNotification& v) override { ++calls; values.push_back(v); }
};
} // namespace

#ifdef _WIN32
#include <filesystem>
#include <fstream>

namespace {
void ok(HRESULT hr, const char* message) {
    if (FAILED(hr)) {
        std::cerr << message << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << '\n';
        throw std::runtime_error(message);
    }
    ++checks;
}
struct Apartment {
    Apartment() { ok(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "The test owns its STA"); }
    ~Apartment() { CoUninitialize(); }
};
std::string utf8(const std::wstring& s) {
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), count, nullptr, nullptr);
    return out;
}
// Private fixture: a NEW temporary folder and a NEW volatile HKCU subtree
// standing in for HKCU\Software\Classes (removed in the destructor).
struct Owned {
    std::filesystem::path directory;
    std::wstring registryPath;
    HKEY classes{};
    Owned() {
        GUID id{};
        ok(CoCreateGuid(&id), "Fixture identifier");
        wchar_t text[40]{};
        check(StringFromGUID2(id, text, 40) > 0, "Fixture identifier formats");
        const auto temporary = std::filesystem::temp_directory_path(); wchar_t longTemporary[32768]{}; const DWORD longLength = GetLongPathNameW(temporary.c_str(), longTemporary, 32768); directory = (longLength && longLength < 32768 ? std::filesystem::path(longTemporary) : temporary) / (std::wstring(L"EndfieldHUD-calendar-identity-") + text);
        check(std::filesystem::create_directory(directory), "A NEW owned temporary folder");
        std::filesystem::create_directory(directory / L"Programs");
        std::filesystem::create_directory(directory / L"App");
        registryPath = L"Software\\EndfieldHUD.Tests.CalendarIdentity." + std::wstring(text);
        DWORD disposition{};
        check(RegCreateKeyExW(HKEY_CURRENT_USER, registryPath.c_str(), 0, nullptr, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr, &classes, &disposition) == ERROR_SUCCESS &&
              disposition == REG_CREATED_NEW_KEY, "A private volatile Classes substitute, never HKCU\\Software\\Classes");
    }
    ~Owned() {
        if (classes) RegCloseKey(classes);
        RegDeleteTreeW(HKEY_CURRENT_USER, registryPath.c_str());
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};
n::AppNotificationIdentity testIdentity(const Owned& owned, const wchar_t* executable) {
    auto identity = n::calendarNotificationIdentity(utf8((owned.directory / L"App" / executable).wstring()), utf8((owned.directory / L"Programs").wstring()));
    // Test-only identity: never the product AppUserModelID or activator.
    identity.appUserModelID = "EndfieldHUD.Tests.CalendarReminders";
    identity.activatorCLSID = "{5C487D23-3A24-491A-832E-8CF00D94D22B}";
    return identity;
}
n::CalendarScheduledNotification scheduled(std::string identifier) { return {std::move(identifier), "signature", "Title", "Today 2026-10-12", 0}; }

void installed(Owned& owned) {
    const auto identity = testIdentity(owned, L"EndfieldHUD.exe");
    std::ofstream(owned.directory / L"App" / L"EndfieldHUD.exe", std::ios::binary) << "synthetic, never executed";
    std::ofstream(owned.directory / L"App" / L"Moved.exe", std::ios::binary) << "synthetic, never executed";
    check(!n::installedCalendarNotificationAppID(owned.classes, identity), "Without the installer the identity is absent");
    ok(n::installAppNotificationIdentity(owned.classes, identity, n::AppNotificationRegistryLifetime::volatileFixture), "The installer step registers the private identity");
    check(n::installedCalendarNotificationAppID(owned.classes, identity) == identity.appUserModelID, "The exact installed registration resolves its AppUserModelID");
    check(std::filesystem::exists(identity.shortcut), "The installer created the shortcut in the (private) Programs folder");
    const auto moved = testIdentity(owned, L"Moved.exe");
    check(!n::installedCalendarNotificationAppID(owned.classes, moved), "Another executable is a conflict and never borrows the registration");

    Scheduled other;
    other.values = {scheduled(std::string(m::calendarNotificationPrefix) + "11111111-2222-4333-8444-555555555555.day")};
    check(n::removeCalendarNotificationIdentity(owned.classes, moved, other) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) && !other.calls &&
          n::installedCalendarNotificationAppID(owned.classes, identity), "Removing a conflicting identity touches neither toasts nor the registration");

    Scheduled toasts;
    toasts.values = {scheduled(std::string(m::calendarNotificationPrefix) + "11111111-2222-4333-8444-555555555555.day"),
                     scheduled(std::string(m::calendarNotificationPrefix) + "11111111-2222-4333-8444-555555555555.before"),
                     scheduled("Other.module.reminder")};
    ok(n::removeCalendarNotificationIdentity(owned.classes, identity, toasts), "Uninstall removes the matching identity");
    check(toasts.removed.size() == 2 && toasts.values.size() == 1 && toasts.values[0].identifier == "Other.module.reminder",
          "Uninstall cancels every scheduled Calendar reminder first and nothing foreign");
    check(!std::filesystem::exists(identity.shortcut) && !n::installedCalendarNotificationAppID(owned.classes, identity),
          "The shortcut and LocalServer32 binding are gone afterwards");
    const auto calls = toasts.calls;
    ok(n::removeCalendarNotificationIdentity(owned.classes, identity, toasts), "Removing a missing identity succeeds");
    check(toasts.calls == calls, "A missing identity schedules nothing to cancel");
}

void process() {
    // Read-only lookups of this test process; nothing is established.
    check(!n::processAppUserModelID(), "A test process has no explicit AppUserModelID");
    const auto programs = n::currentUserProgramsFolder();
    check(!programs.empty() && programs.size() > 3 && programs[1] == ':', "The per-user Programs folder is an absolute path");
    const auto current = n::currentCalendarNotificationIdentity();
    check(current.executable.ends_with("calendar_notification_identity_tests.exe") && current.shortcut == programs + "\\EndfieldHUD.lnk",
          "The current identity names this executable and the per-user Start-menu shortcut");
}
} // namespace
#endif

int main() {
    try {
        portable();
#ifdef _WIN32
        Apartment apartment;
        {
            Owned owned;
            installed(owned);
        }
        process();
#endif
        std::cout << "PASS " << checks << " Calendar notification identity checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
