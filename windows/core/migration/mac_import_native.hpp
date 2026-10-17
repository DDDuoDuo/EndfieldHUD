#pragma once
#include "core/migration/mac_import.hpp"
#include "modules/app_shortcut_model.hpp"
#include "modules/archive_model.hpp"

namespace ehud::migration {
// Platform wiring for MacImportSession: the installed ICU rules already used by
// Archive, App Shortcuts and Calendar (Windows: system icu.dll), and on Windows
// a WIC first-frame decode check for managed images. Built wherever hud_archive
// and hud_shortcuts are (WIN32, or a portable build with a local ICU).
MacImportPlatform nativeMacImportPlatform();
// Opens the staged Archive directory through ArchiveSQLiteRepository, decodes
// every category, summary and full entry body, and lists Mac attachment
// locators for relinking. The repository is closed before returning.
std::vector<MacRelinkItem> validateStagedArchive(const std::filesystem::path& archiveDirectory, const endfield::modules::ArchiveTextRules&);
// Loads AppShortcuts/shortcuts.json through ShortcutRepository; every non-
// Windows locator (.app path, bundle identifier, bookmark) needs relinking.
std::vector<MacRelinkItem> validateStagedShortcuts(const std::filesystem::path& shortcutDirectory, const endfield::modules::ShortcutTextRules&);
// Explicit app relink through the unchanged App Shortcuts editor transaction:
// keeps the shortcut's id, position, name, icon preset and creation date and
// replaces only its locator with the reinspected Windows target.
std::string relinkShortcut(endfield::modules::ShortcutFile&, std::string_view shortcutID,
    const endfield::modules::ShortcutCandidate& selected, const endfield::modules::ShortcutCandidate& reinspected,
    double now, const endfield::modules::ShortcutTextRules&, const endfield::modules::ShortcutSameTarget&);
// Archive attachment relink validated by the Archive codec.
endfield::modules::ArchiveJson relinkedArchiveMedia(const endfield::modules::ArchiveJson& macMedia, const WindowsMediaDescriptor&);
#ifdef _WIN32
// Decodes the first frame through WIC into a bounded 256-pixel thumbnail.
std::optional<std::string> decodeImageWithWIC(const std::filesystem::path&);
#endif
}
