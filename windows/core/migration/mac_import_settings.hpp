#pragma once
#include "core/data/data_store.hpp"
#include "core/migration/plist.hpp"
#include <functional>

namespace ehud::migration {
// Foundation String semantics that cannot be reproduced without the installed
// Unicode engine are injected (Windows: system ICU, see mac_import_native).
struct MacImportTextRules {
    // First `count` extended grapheme clusters (Swift String.prefix).
    std::function<std::string(std::string_view, std::size_t)> prefix;
};
// Foundation CharacterSet.whitespacesAndNewlines, pinned scalar set.
bool foundationWhitespaceOrNewline(std::uint32_t scalar) noexcept;
std::string foundationTrimmed(std::string_view utf8);
// Foundation UUID(uuidString:)?.uuidString.
std::optional<std::string> foundationUUID(std::string_view);

// Read-only view over an exported UserDefaults domain with the coercions of
// -[NSUserDefaults stringForKey:/doubleForKey:/boolForKey:/integerForKey:/
// dataForKey:] that ConfigurationStore and OrbiPomSession actually rely on.
class MacDefaultsDomain final {
public:
    explicit MacDefaultsDomain(PlistValue root); // root must be a dictionary
    const PlistValue* object(std::string_view key) const noexcept { return root_.find(key); }
    std::optional<std::string> string(std::string_view key) const;
    double doubleValue(std::string_view key) const;
    bool boolValue(std::string_view key) const;
    std::int64_t integerValue(std::string_view key) const;
    std::optional<std::string> data(std::string_view key) const;
    const PlistValue& root() const noexcept { return root_; }
private:
    PlistValue root_;
};

enum class MacSettingOutcome {
    imported,        // value read and kept (possibly after source normalization)
    normalized,      // value read, then clamped/canonicalized exactly like AppConfiguration.normalized
    defaulted,       // key absent: original default
    invalidFallback, // present but unusable: original per-key fallback
    migrated,        // legacy D9F36B accent became FAD41F (schema marker absent)
    remapped,        // platform value translated by intent (default summon shortcut)
    untranslated,    // platform value kept for reference but not applied on Windows
    requiresAction,  // intent imported; Windows platform state must be applied by its owner
    excluded,        // updater/Sparkle state never transfers
    notImported      // unknown key; preserved only in the archived original plist
};
struct MacSettingReport {
    std::string key;
    MacSettingOutcome outcome{MacSettingOutcome::imported};
    std::string detail;
};
struct MacSettingsImport {
    data::Settings settings;            // validated Windows SettingsStore record
    std::vector<MacSettingReport> report;
    bool launchAtLogin{true};           // intent for the Windows startup provider
    bool customShortcutUntranslated{};  // a non-default Mac shortcut needs re-binding
    std::optional<std::string> centerLogoRevision; // when centerLogo == custom
    std::optional<bool> hasLaunched;    // AppDelegate first-run key (envelope marker on Windows)
};
// Replicates ConfigurationStore.init (schema marker + legacy accent migration,
// per-key fallbacks, AppConfiguration.normalized, unsigned customScreenID),
// OrbiPomSession's best score and the first-launch flag. Throws only when the
// domain itself is not a dictionary or rules are missing.
MacSettingsImport mapMacSettings(const PlistValue& domain, const MacImportTextRules&);
std::string_view macSettingOutcomeName(MacSettingOutcome) noexcept;
// The Mac physical-key table used by SummonShortcut.validationError.
bool macSummonKeyNamed(std::uint32_t keyCode) noexcept;
}
