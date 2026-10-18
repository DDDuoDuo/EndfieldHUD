// Installer step for the Calendar reminder identity of an unpackaged per-user
// EndfieldHUD install (native/calendar_notification_identity.hpp).
//
//   calendar_notification_setup inspect <absolute EndfieldHUD.exe>
//   calendar_notification_setup install <absolute EndfieldHUD.exe>
//   calendar_notification_setup remove  <absolute EndfieldHUD.exe>
//
// install creates %APPDATA%\Microsoft\Windows\Start Menu\Programs\EndfieldHUD.lnk
// (AppUserModelID + toast activator, no arguments) and the per-user
// LocalServer32 "<exe>" --notification-activation; an existing conflicting
// shortcut or binding is never overwritten. remove first cancels every
// scheduled Calendar reminder of that identity, then deletes exactly the
// matching shortcut and binding. Without a command the tool is inert: it
// prints its usage and touches nothing (the registered test runs it so).
#include "native/calendar_notification_identity.hpp"
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

namespace {
namespace n = endfield::native;
std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::invalid_argument("The executable path is not valid Unicode");
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}
struct Apartment {
    HRESULT hr;
    Apartment() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~Apartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
struct Key {
    HKEY value{};
    ~Key() { if (value) RegCloseKey(value); }
};
int failed(const char* what, HRESULT hr) {
    std::cerr << what << " failed: HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << '\n';
    return 1;
}
// The WinRT provider enters its own multithreaded apartment per call, so each
// call runs on a worker thread, never on this STA (the HUD runs it on the
// shared UtilityExecutor).
class WorkerProvider final : public n::CalendarNotificationProvider {
public:
    explicit WorkerProvider(n::CalendarNotificationOptions options) : options_(std::move(options)) {}
    endfield::modules::CalendarPermission authorization(bool request) override {
        auto result = endfield::modules::CalendarPermission::unavailable;
        run([&](n::CalendarNotificationProvider& p) { result = p.authorization(request); });
        return result;
    }
    std::vector<n::CalendarScheduledNotification> pending() override {
        std::vector<n::CalendarScheduledNotification> result;
        run([&](n::CalendarNotificationProvider& p) { result = p.pending(); });
        return result;
    }
    void remove(std::string_view id) override { run([&](n::CalendarNotificationProvider& p) { p.remove(id); }); }
    void add(const n::CalendarScheduledNotification& value) override { run([&](n::CalendarNotificationProvider& p) { p.add(value); }); }
private:
    template <class F> void run(F&& call) {
        std::exception_ptr error;
        std::thread([&] {
            try { auto provider = n::nativeCalendarNotificationFactory()(options_); call(*provider); }
            catch (...) { error = std::current_exception(); }
        }).join();
        if (error) std::rethrow_exception(error);
    }
    n::CalendarNotificationOptions options_;
};
int usage() {
    std::cout << "calendar_notification_setup is inert without a command; nothing was inspected or changed.\n"
                 "usage: calendar_notification_setup inspect|install|remove <absolute EndfieldHUD.exe>\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) return usage();
        const std::wstring_view command = argv[1];
        if (command != L"inspect" && command != L"install" && command != L"remove") return usage();
        Apartment apartment;
        if (FAILED(apartment.hr)) return failed("COM initialization", apartment.hr);
        // The identity of the GIVEN executable in this user's Start menu.
        const auto identity = n::calendarNotificationIdentity(utf8(argv[2]), n::currentUserProgramsFolder());
        Key classes;
        const auto opened = command == L"inspect"
            ? RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, KEY_READ, &classes.value)
            : RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr, &classes.value, nullptr);
        if (opened != ERROR_SUCCESS) return failed("Opening HKCU\\Software\\Classes", HRESULT_FROM_WIN32(opened));
        if (command == L"inspect") {
            n::AppNotificationRegistration state{};
            if (const auto hr = n::inspectAppNotificationRegistration(classes.value, identity, &state); FAILED(hr)) return failed("Inspection", hr);
            std::cout << (state == n::AppNotificationRegistration::matching ? "matching" : state == n::AppNotificationRegistration::missing ? "missing" : "conflict")
                      << ": " << identity.appUserModelID << " -> " << identity.shortcut << '\n';
            return state == n::AppNotificationRegistration::conflict ? 2 : 0;
        }
        if (command == L"install") {
            if (const auto hr = n::installAppNotificationIdentity(classes.value, identity); FAILED(hr)) return failed("Installation", hr);
            std::cout << "installed: " << identity.appUserModelID << " -> " << identity.shortcut << '\n';
            return 0;
        }
        // remove: the real scheduled-toast provider of this identity cancels
        // every Calendar reminder before the identity disappears.
        n::CalendarNotificationOptions options;
        options.establishedAppUserModelID = identity.appUserModelID;
        WorkerProvider scheduled(std::move(options));
        if (const auto hr = n::removeCalendarNotificationIdentity(classes.value, identity, scheduled); FAILED(hr)) return failed("Removal", hr);
        std::cout << "removed: " << identity.appUserModelID << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "calendar_notification_setup: " << e.what() << '\n';
        return 1;
    }
}
