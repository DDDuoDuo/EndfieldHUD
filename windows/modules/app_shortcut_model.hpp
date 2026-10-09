#pragma once
#include "core/data/data_store.hpp"
#include <functional>
#include <thread>

namespace endfield::modules {
using ShortcutJson = ehud::data::Json;
inline constexpr std::size_t shortcutMaximumBytes = 4 * 1024 * 1024;

// Store limits follow Swift Characters and CharacterSet.controlCharacters.
// The platform supplies Unicode rules; pointer/animation frames never call them.
struct ShortcutTextRules {
    std::function<bool(std::string_view)> validName;
    std::function<std::string(std::string_view)> trimmed;
};
enum class ShortcutErrorCode {
    invalidRecord, newerVersion, invalidName, duplicate, missing, changedOnDisk,
    applicationChanged, unavailable, capacity
};
class ShortcutError final : public std::runtime_error {
public:
    explicit ShortcutError(ShortcutErrorCode);
    ShortcutErrorCode code() const noexcept { return code_; }
private:
    ShortcutErrorCode code_;
};
bool validShortcutIcon(std::string_view) noexcept;

struct ShortcutRecord {
    std::string id, name, originalName, iconPreset{"original"};
    std::optional<std::string> bundleIdentifier;
    double createdAt{}; // Original Foundation epoch.
    // Original Mac access context stays opaque. A Windows locator is separate,
    // never a fabricated Mac bookmark. Imported unavailable apps remain editable.
    ShortcutJson locator{ShortcutJson::Object{}};
    ShortcutJson originalFields{ShortcutJson::Object{}};
    ShortcutJson originalDate;
    bool native() const noexcept;
    bool operator==(const ShortcutRecord&) const;
};
struct ShortcutFile {
    std::vector<ShortcutRecord> items;
    ShortcutJson originalFields{ShortcutJson::Object{}};
    bool operator==(const ShortcutFile&) const = default;
};
void validateShortcut(const ShortcutRecord&, const ShortcutTextRules&);
ShortcutFile decodeShortcuts(const ShortcutJson&, const ShortcutTextRules&);
ShortcutJson encodeShortcuts(const ShortcutFile&, const ShortcutTextRules&);

// The caller obtains both candidates from the same native inspector on the
// shared utility worker. Reinspection at save prevents a stale selection from
// silently changing its target. No scanning or app launch takes place here.
struct ShortcutCandidate {
    std::string name;
    std::optional<std::string> bundleIdentifier;
    ShortcutJson locator{ShortcutJson::Object{}};
    bool operator==(const ShortcutCandidate&) const = default;
};
using ShortcutSameTarget = std::function<bool(const ShortcutJson&, const ShortcutJson&)>;
std::string saveShortcutDraft(ShortcutFile&, const ShortcutCandidate& selected,
    const ShortcutCandidate& reinspected, std::string name, std::string icon,
    std::optional<std::string> editingID, std::string newID, double now,
    const ShortcutTextRules&, const ShortcutSameTarget&);
bool removeShortcut(ShortcutFile&, std::string_view);

// One borrowed FIFO executor owns reads and atomic writes. Construction is IO
// free; a failed read/write leaves the prior state and original file untouched.
class ShortcutRepository final {
public:
    ShortcutRepository(std::filesystem::path directory, ShortcutTextRules);
    ShortcutFile load();
    void save(const ShortcutFile&);
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path directory_, path_;
    ShortcutTextRules rules_;
    ShortcutFile value_;
    std::optional<std::string> persisted_;
    std::optional<std::thread::id> owner_;
    bool loaded_{};
    void onOwner();
};
}
