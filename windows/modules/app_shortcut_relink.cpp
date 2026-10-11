#include "modules/app_shortcut_relink.hpp"
#include "core/migration/mac_import_relink.hpp"
#include <algorithm>
#include <vector>

namespace endfield::modules {
namespace {
namespace mig = ehud::migration;
using J = ShortcutJson;
// Record identifiers are UUID strings; the ledger may carry another case.
bool sameID(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t n = 0; n < a.size(); ++n) {
        char x = a[n], y = b[n];
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
const ShortcutRecord* record(const ShortcutFile& file, std::string_view id) noexcept {
    const auto found = std::find_if(file.items.begin(), file.items.end(), [&](const auto& item) { return sameID(item.id, id); });
    return found == file.items.end() ? nullptr : &*found;
}
} // namespace

std::string shortcutRelinkTarget(const ShortcutRecord& r) {
    if (!r.native()) return {};
    const auto& target = r.locator["windowsTarget"];
    if (!target.isObject()) return {};
    if (target["kind"] == J("packagedApp")) return target["appUserModelID"].isString() ? "shell:AppsFolder\\" + target["appUserModelID"].string() : std::string{};
    if ((target["kind"] == J("executable") || target["kind"] == J("shellLink")) && target["path"].isString()) return target["path"].string();
    return {};
}

ShortcutRelinkSync syncShortcutRelinks(const std::filesystem::path& appRoot, const ShortcutFile& committed) {
    // The import's owner API (reconcileMacRelinks): one serialized, atomic
    // write at most. Rows whose record now names its Windows application
    // (an .exe/.lnk file, or a packaged application's AppsFolder target)
    // resolve with it; rows whose record the user removed are dismissed; a
    // record that still holds its Mac locator stays listed.
    const auto change = mig::reconcileMacRelinks(appRoot, mig::relinkAppShortcut, [&](const mig::MacRelinkItem& item) -> mig::RelinkRecordCheck {
        const auto* current = record(committed, item.recordID);
        if (!current) return {mig::RelinkRecordState::missing, {}};
        auto target = shortcutRelinkTarget(*current);
        if (target.empty() || !mig::validMacRelinkTarget(mig::relinkAppShortcut, target)) return {};
        return {mig::RelinkRecordState::relinked, std::move(target)};
    });
    return {change.resolved, change.dismissed, change.open};
}
} // namespace endfield::modules
