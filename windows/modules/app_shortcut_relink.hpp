#pragma once
// App shortcuts' rows of the offline Mac import ledger (WINDOWS-MIGRATION.md
// section 7, core/migration/mac_import_relink.hpp). An imported Mac shortcut
// keeps its opaque bookmark until the user edits it and chooses a Windows
// application; Migration/relink.json lists it ("appShortcuts.app") until then.
// The ledger follows the committed shortcuts.json on the repository's own
// worker - record first, ledger second, as MacRelinkLedger asks of owners:
// a record that now carries an explicit Windows target resolves its row with
// that target, a record the user removed dismisses its row, and a record that
// still holds its Mac locator stays listed. Nothing is looked up by path or
// name, and nothing is relinked automatically. Windows-only bookkeeping: the
// Mac keeps no such list.
#include "modules/app_shortcut_model.hpp"
#include <cstddef>
#include <filesystem>
#include <string>

namespace endfield::modules {
struct ShortcutRelinkSync {
    std::size_t resolved{}, dismissed{}; // rows this call resolved / dismissed
    std::size_t open{};                  // app shortcut rows still listed afterwards
    bool operator==(const ShortcutRelinkSync&) const = default;
};
// The explicit Windows target a relinked record names in the ledger: its
// selected .exe/.lnk path, or "shell:AppsFolder\<AUMID>" for a packaged
// application; empty for a record without a Windows locator.
std::string shortcutRelinkTarget(const ShortcutRecord&);
// Worker-only, after `committed` was read from or atomically written to
// <appRoot>/AppShortcuts. Without a ledger, or without a listed app shortcut
// row, nothing is written. Each change commits atomically (the ledger's
// compare-and-replace); an unreadable or newer ledger throws and is kept as
// it is. The ledger records explicit Windows file paths: a row relinked to a
// packaged application stays listed while the ledger refuses its AppsFolder
// target (a recorded gap), and is resolved as soon as the ledger accepts it.
ShortcutRelinkSync syncShortcutRelinks(const std::filesystem::path& appRoot, const ShortcutFile& committed);
} // namespace endfield::modules
