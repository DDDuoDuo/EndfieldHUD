#include "app/application_login_item.hpp"
#ifdef _WIN32
#include <cwchar>
#include <stdexcept>
#include <vector>

namespace endfield::app {
namespace {
std::optional<std::wstring> readString(HKEY root, const std::wstring& key, const std::wstring& name) {
    DWORD type{}, bytes{};
    const auto first = RegGetValueW(root, key.c_str(), name.c_str(), RRF_RT_REG_SZ, &type, nullptr, &bytes);
    if (first != ERROR_SUCCESS || bytes == 0 || bytes > 64 * 1024) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key.c_str(), name.c_str(), RRF_RT_REG_SZ, &type, value.data(), &bytes) != ERROR_SUCCESS) return {};
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}
bool approvedOff(HKEY root, const std::wstring& key, const std::wstring& name) {
    BYTE data[64]{};
    DWORD type{}, bytes = sizeof(data);
    if (RegGetValueW(root, key.c_str(), name.c_str(), RRF_RT_REG_BINARY, &type, data, &bytes) != ERROR_SUCCESS || bytes == 0) return false;
    return (data[0] & 1) != 0; // 02/06 enabled, 03/07 turned off by the user
}
std::string failure(core::Language language) {
    return core::localized("Startup registration could not be changed", "无法更改登录时启动设置", language);
}
}

LoginItemLocation LoginItemLocation::production(const std::filesystem::path& executable) {
    return {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run", L"EndfieldHUD",
            L"\"" + executable.wstring() + L"\" --login"};
}
LoginItemRegistration::LoginItemRegistration(LoginItemLocation location) : location_(std::move(location)) {
    if (location_.runKey.empty() || location_.valueName.empty() || location_.command.empty() || location_.command.size() > 4096)
        throw std::invalid_argument("Launch-at-login registration needs a key, value and command");
}
LoginItemStatus LoginItemRegistration::read() const {
    LoginItemStatus status;
    if (const auto value = readString(location_.root, location_.runKey, location_.valueName)) {
        status.registered = *value == location_.command;
        status.stale = !status.registered;
    }
    status.disabledByUser = !location_.approvedKey.empty() && approvedOff(location_.root, location_.approvedKey, location_.valueName);
    return status;
}
std::optional<std::string> LoginItemRegistration::setEnabled(bool enabled, core::Language language) const {
    const auto status = read();
    if (!enabled) {
        if (!status.registered && !status.stale) return {};
        const auto result = RegDeleteKeyValueW(location_.root, location_.runKey.c_str(), location_.valueName.c_str());
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) return failure(language);
        return {};
    }
    if (status.disabledByUser)
        return core::localized("Turned off in Windows Settings → Apps → Startup.", "已在 Windows 设置 → 应用 → 启动 中关闭。", language);
    if (status.registered) return {};
    HKEY key{};
    if (RegCreateKeyExW(location_.root, location_.runKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return failure(language);
    const auto bytes = static_cast<DWORD>((location_.command.size() + 1) * sizeof(wchar_t));
    const auto result = RegSetValueExW(key, location_.valueName.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(location_.command.c_str()), bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS ? std::nullopt : std::optional<std::string>(failure(language));
}
std::optional<std::string> LoginItemRegistration::ensureEnabled(core::Language language) const {
    const auto status = read();
    if (status.registered || status.disabledByUser) return {};
    return setEnabled(true, language);
}
std::string LoginItemRegistration::statusDescription(const LoginItemStatus& status, core::Language language) {
    if (status.disabledByUser && (status.registered || status.stale))
        return core::localized("Turned off in Windows Settings → Apps → Startup.", "已在 Windows 设置 → 应用 → 启动 中关闭。", language);
    return status.enabled() ? core::localized("Enabled", "已启用", language) : core::localized("Disabled", "已停用", language);
}
} // namespace endfield::app
#endif
