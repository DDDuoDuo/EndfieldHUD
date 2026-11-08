#pragma once
#include "core/data/data_store.hpp"
#include <filesystem>
#include <string>
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
// Data left by the Import from Mac of earlier Windows versions (removed in
// 1.3.1): an import switched the data root with two renames, v1 to
// "v1.backup-*" and the staged "v1.import-<token>\root" to v1, after writing
// "activation.json" (naming the backup) next to the "import-work.json" marker.
// A process killed between the two renames left no v1. Before anything opens
// the root (the host's startup, under its single-instance guard, so no import
// can be live), this moves that backup back to v1, only while v1 is absent and
// only when the marker and journal are exactly the importer's. Never replaces
// or deletes anything; the work folder stays. True when a backup was restored.
// Never throws.
bool restoreInterruptedImportRoot(const std::filesystem::path& base) noexcept;
#ifdef _WIN32
// The process token user's SID as text ("S-1-5-21-...-1001"): the data
// root's private DACL and the Windows account on the personal cards. Throws
// StoreError(unavailable).
std::wstring currentUserSid();
#endif
// Read-only resolution of the production base. Never creates it.
// Windows: SHGetKnownFolderPath(FOLDERID_LocalAppData)\EndfieldHUD.
std::filesystem::path localApplicationDataBase();
} // namespace ehud::data
