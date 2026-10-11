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
    mig::MacRelinkLedger ledger(appRoot); // a missing file is an empty ledger
    ShortcutRelinkSync out;
    const auto listed = ledger.unresolved(mig::relinkAppShortcut);
    if (listed.empty()) return out;
    // One atomic write: rows whose record now names a Windows file resolve
    // with it; rows whose record the user removed are dismissed.
    (void)ledger.reconcile(mig::relinkAppShortcut, [&](const mig::MacRelinkItem& item) -> mig::RelinkRecordCheck {
        const auto* current = record(committed, item.recordID);
        if (!current) return {mig::RelinkRecordState::missing, {}};
        auto target = shortcutRelinkTarget(*current);
        if (target.empty() || !ehud::data::validWindowsFilePath(target)) return {}; // Mac locator, or a packaged target (below)
        return {mig::RelinkRecordState::relinked, std::move(target)};
    });
    // A packaged application has no file path. The ledger decides whether it
    // records its AppsFolder target; while it refuses one (an invalid
    // Windows path), the row stays listed. Other failures propagate.
    for (const auto& item : ledger.unresolved(mig::relinkAppShortcut)) {
        const auto* current = record(committed, item.recordID);
        if (!current) continue;
        auto target = shortcutRelinkTarget(*current);
        if (target.empty()) continue;
        try { ledger.markResolved(item.id, std::move(target)); }
        catch (const ehud::data::StoreError& e) { if (e.code() != ehud::data::StoreErrorCode::invalid) throw; }
    }
    for (const auto& before : listed) {
        const auto* after = ledger.find(before.id);
        if (!after || after->state == mig::RelinkState::unresolved) ++out.open;
        else if (after->state == mig::RelinkState::resolved) ++out.resolved;
        else ++out.dismissed;
    }
    return out;
}
} // namespace endfield::modules
