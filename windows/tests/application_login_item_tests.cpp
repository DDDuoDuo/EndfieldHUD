// Launch-at-login registration over a PRIVATE temporary HKCU subtree. The
// real Run/StartupApproved keys are never read or written by this test.
#ifdef _WIN32
#include "app/application_login_item.hpp"
#include <objbase.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace app = endfield::app;
namespace core = endfield::core;
namespace {
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
std::wstring guid() { GUID value{}; check(SUCCEEDED(CoCreateGuid(&value)), "Fresh GUID"); wchar_t text[64]{}; StringFromGUID2(value, text, 64); return text; }
struct PrivateTree {
    std::wstring root;
    PrivateTree() : root(L"Software\\EndfieldHUD-LoginItem-Test-" + guid()) {}
    ~PrivateTree() { RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str()); }
};
void approve(const app::LoginItemLocation& at, BYTE first) {
    HKEY key{};
    check(RegCreateKeyExW(at.root, at.approvedKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS, "Create private approval key");
    BYTE data[12]{first};
    check(RegSetValueExW(key, at.valueName.c_str(), 0, REG_BINARY, data, sizeof(data)) == ERROR_SUCCESS, "Write private approval");
    RegCloseKey(key);
}
std::wstring value(const app::LoginItemLocation& at) {
    wchar_t text[1024]{}; DWORD bytes = sizeof(text);
    if (RegGetValueW(at.root, at.runKey.c_str(), at.valueName.c_str(), RRF_RT_REG_SZ, nullptr, text, &bytes) != ERROR_SUCCESS) return {};
    return text;
}
void contracts() {
    PrivateTree tree;
    app::LoginItemLocation at{HKEY_CURRENT_USER, tree.root + L"\\Run", tree.root + L"\\StartupApproved\\Run", L"EndfieldHUD", L"\"C:\\Program Files\\EndfieldHUD\\EndfieldHUD.exe\" --login"};
    const app::LoginItemRegistration login(at);
    check(!login.read().enabled() && !login.read().registered, "A fresh profile has no startup entry");
    check(app::LoginItemRegistration::statusDescription(login.read(), core::Language::english) == "Disabled", "Disabled status");
    check(!login.ensureEnabled(core::Language::english) && login.read().enabled() && value(at) == at.command, "Launch registers the saved preference with --login");
    check(app::LoginItemRegistration::statusDescription(login.read(), core::Language::simplifiedChinese) == "已启用", "Localized enabled status");
    check(!login.setEnabled(false, core::Language::english) && value(at).empty() && !login.read().registered, "Explicit disable removes only our value");
    check(!login.setEnabled(false, core::Language::english), "Disabling twice is harmless");
    // A moved executable leaves a stale command that launch repairs.
    auto old = at; old.command = L"\"D:\\Old\\EndfieldHUD.exe\" --login";
    check(!app::LoginItemRegistration(old).setEnabled(true, core::Language::english) && login.read().stale, "Stale command detected");
    check(!login.ensureEnabled(core::Language::english) && value(at) == at.command && login.read().registered, "Launch repairs a stale path");
    // The user's Settings/Task Manager choice is reported and never forced.
    approve(at, 0x03);
    const auto off = login.read();
    check(off.registered && off.disabledByUser && !off.enabled(), "User-disabled entry is detected");
    check(app::LoginItemRegistration::statusDescription(off, core::Language::english).find("Startup") != std::string::npos, "Status names the Windows Startup setting");
    check(!login.ensureEnabled(core::Language::english) && login.read().disabledByUser, "Launch never overrides the user's choice");
    check(login.setEnabled(true, core::Language::english).has_value() && login.read().disabledByUser, "Explicit enable reports the user's choice instead of forcing it");
    approve(at, 0x02);
    check(login.read().enabled(), "Re-approved entry is enabled");
    bool rejected{};
    try { app::LoginItemRegistration bad({HKEY_CURRENT_USER, L"", L"", L"", L""}); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Incomplete registrations are rejected");
    const auto production = app::LoginItemLocation::production(L"C:\\Apps\\EndfieldHUD.exe");
    check(production.runKey == L"Software\\Microsoft\\Windows\\CurrentVersion\\Run" && production.command == L"\"C:\\Apps\\EndfieldHUD.exe\" --login" &&
          production.valueName == L"EndfieldHUD", "Production location (not touched by this test)");
}
} // namespace
int main() {
    try { contracts(); std::cout << "Launch at login: " << checks << " checks passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "Launch at login failed after " << checks << ": " << e.what() << '\n'; return 1; }
}
#else
int main() { return 0; }
#endif
