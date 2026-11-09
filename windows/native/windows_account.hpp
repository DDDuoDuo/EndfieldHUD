#pragma once
// The signed-in Windows account for the personal cards (issue #17; the Mac
// reads the macOS account through SystemAccountIdentity.readCurrent and
// CSIdentity). Every source is permission-free and shows no prompt:
//  - UID: the process token user's SID (ehud::data::currentUserSid) and its
//    last sub-authority, the RID (for example 1001);
//  - name: GetUserNameExW(NameDisplay), then NetUserGetInfo level 2's
//    usri2_full_name (NameDisplay fails with ERROR_NONE_MAPPED on many local
//    accounts), then GetUserNameW;
//  - picture: HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\AccountPicture\
//    Users\{SID}, values Image448, then Image1080, then Image240 (files under
//    C:\Users\Public\AccountPictures\{SID}). Never SHGetUserPicturePathEx (an
//    undocumented ordinal), the .accountpicture-ms container or WinRT
//    User.GetPictureAsync (it honours the Account info privacy switch).
// Every OS call is injected (WindowsAccountCalls), so tests feed a fake token,
// directory and registry and never read the real account. The reads run on
// the profile owner's utility worker. Any failure is "none", and the card
// keeps Endministrator, the stored UID and the drawn portrait.
#include "core/data/data_store.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
namespace endfield::native {
struct WindowsAccountCalls {
    std::function<std::optional<std::wstring>()> sid;          // the token user's SID string
    std::function<std::optional<std::wstring>()> displayName;  // GetUserNameExW(NameDisplay)
    std::function<std::optional<std::wstring>()> fullName;     // NetUserGetInfo(level 2).usri2_full_name
    std::function<std::optional<std::wstring>()> userName;     // GetUserNameW
    // A REG_SZ value under HKEY_LOCAL_MACHINE (key path, value name).
    std::function<std::optional<std::wstring>(const std::wstring& key, const std::wstring& value)> machineString;
    // A regular file's bytes, at most `limit` (none: missing, larger or unreadable).
    std::function<std::optional<std::vector<std::uint8_t>>(const std::filesystem::path&, std::uintmax_t limit)> readFile;
};
// The real calls (production only; never in tests, previews or hidden coverage).
WindowsAccountCalls currentWindowsAccountCalls();
// The SID's RID: the digits after its last '-' ("S-1-5-21-...-1001" -> "1001").
std::optional<std::string> windowsAccountRID(std::wstring_view sid);
// The name and UID as read (raw: ProfileState applies the card's name rule),
// with no picture key. Never throws.
ehud::data::SystemAccount readWindowsAccount(const WindowsAccountCalls&);
// The login picture's encoded bytes (at most 16 MB), or none. Never throws.
std::optional<std::vector<std::uint8_t>> readWindowsAccountPicture(const WindowsAccountCalls&);
}
#endif
