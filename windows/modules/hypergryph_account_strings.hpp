#pragma once
#include "core/localization.hpp"
#include <string>
#include <string_view>

// Account text with the Windows credential-store wording. Two Mac strings
// name the Mac's Keychain, which does not exist on Windows (credentials live
// in a DPAPI current-user record, native/hypergryph_account_vault.*). Windows
// wording replaces only those references, in all five languages; every other
// string is the unchanged Mac text from the shared catalog. `mac` keeps the
// Mac text byte-for-byte (oracle replays compare against the Mac sources).
namespace endfield::modules::hypergryph {
enum class AccountWording {windows,mac};
inline constexpr std::string_view macConsentDetail=
    "Sign in to the official website in a private session. EndfieldHUD will save only this community session in this Mac’s Keychain to read your game profile. Your password and passport token are not collected. This is an unofficial integration.";
inline constexpr std::string_view macConsentDetailChinese=
    "在独立会话中登录官方网站。EndfieldHUD 仅将此次社区会话保存在本机钥匙串，用于读取游戏资料，不收集密码或通行证令牌。这是非官方关联功能。";
inline constexpr std::string_view macVaultFailure="Keychain could not be updated. Try again.";
inline constexpr std::string_view macVaultFailureChinese="无法更新钥匙串，请重试。";
std::string accountText(std::string_view english,std::string_view chinese,core::Language,AccountWording);
}
