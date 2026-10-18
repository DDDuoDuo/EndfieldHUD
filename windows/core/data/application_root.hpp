#pragma once
#include "core/data/data_store.hpp"
#include <filesystem>
#include <string_view>

namespace ehud::data {
// Versioned Windows data root (WINDOWS-MIGRATION.md section 7):
//
//   %LOCALAPPDATA%\EndfieldHUD\          base: current-user-only ACL
//       root.json                       {"format":..., "schema":1, "data":"v1"}
//       v1\                             every store's explicit appRoot:
//           settings.json, Notes\, FileShelf\, Profile\, Archive\, Reader\,
//           Calendar\, WorldMap\, AppShortcuts\, EventLog\, Account\, CenterLogo\
//
// The relative layout mirrors ~/Library/Application Support/EndfieldCharge.
// A newer schema (written by a later EndfieldHUD) or a malformed marker is
// rejected without modifying, moving or erasing anything. Reparse points and
// links are rejected along the whole path. The single-instance guard is the
// writer lock for this root; every file write still uses its own CAS lock.
inline constexpr int applicationRootSchema = 1;
inline constexpr std::string_view applicationRootFormat = "EndfieldHUD.Windows.DataRoot";
inline constexpr std::string_view applicationRootDataDirectory = "v1";
inline constexpr std::string_view applicationRootDirectoryName = "EndfieldHUD";
struct ApplicationRoot {
    std::filesystem::path base, data, marker;
    bool created{}; // the marker was written by this call
};
// `base` is explicit: production passes localApplicationDataBase(); tests and
// development previews pass an owned temporary directory. Its parent must
// already exist. Throws StoreError (newerVersion / invalid / unavailable).
ApplicationRoot openApplicationRoot(const std::filesystem::path& base);
// Read-only resolution of the production base. Never creates it.
// Windows: SHGetKnownFolderPath(FOLDERID_LocalAppData)\EndfieldHUD.
std::filesystem::path localApplicationDataBase();
} // namespace ehud::data
