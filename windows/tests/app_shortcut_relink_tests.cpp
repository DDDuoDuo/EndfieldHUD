// App shortcuts' rows of the offline Mac import ledger
// (modules/app_shortcut_relink, Migration/relink.json). Synthetic records and
// ledgers in NEW temporary app roots only; nothing is launched, inspected or
// looked up by path or name.
#include "modules/app_shortcut_relink.hpp"
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_relink.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
namespace m = endfield::modules;
namespace mig = ehud::migration;
using J = m::ShortcutJson;
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
struct Temp {
    fs::path path = fs::canonical(fs::temp_directory_path()) / ("ehud-shortcut-relink-" + ehud::data::makeUUID());
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};
std::string upper(std::string value) { for (auto& c : value) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A'); return value; }

m::ShortcutRecord macRecord(std::string id, std::string name) {
    m::ShortcutRecord r;
    r.id = std::move(id);r.name = r.originalName = name;r.createdAt = 812345678.125;
    r.locator = J::Object{{"bookmark", "AQIDBA=="}, {"securityScoped", true}, {"lastKnownPath", "/Applications/" + name + ".app"}};
    return r;
}
m::ShortcutRecord windowsRecord(std::string id, std::string name, J target) {
    auto r = macRecord(std::move(id), std::move(name));
    r.locator = J::Object{{"referencePlatform", "windows"}, {"windowsTarget", std::move(target)}};
    return r;
}
J executable(std::string path) { return J::Object{{"kind", "executable"}, {"applicationKey", "path:synthetic"}, {"path", std::move(path)}}; }
J shellLink(std::string path) { return J::Object{{"kind", "shellLink"}, {"applicationKey", "path:synthetic-link"}, {"path", std::move(path)}}; }
J packaged(std::string aumid) { return J::Object{{"kind", "packagedApp"}, {"applicationKey", "package:" + aumid}, {"appUserModelID", std::move(aumid)}}; }

// One ledger row as the import writes it (core/migration/mac_import_native
// validateStagedShortcuts): the record, its name and opaque Mac locator.
mig::MacRelinkItem row(std::string_view store, std::string recordID, std::string name) {
    mig::MacRelinkItem item;
    item.id = ehud::data::makeUUID();item.store = std::string(store);item.recordID = std::move(recordID);
    item.displayName = name;item.lastKnownPath = "/Applications/" + name + ".app";item.kind = "app";
    item.macReference = J::Object{{"bookmark", "AQIDBA=="}, {"securityScoped", true}, {"lastKnownPath", item.lastKnownPath}};
    return item;
}
fs::path ledgerPath(const fs::path& root) { return root / "Migration" / "relink.json"; }
void writeLedger(const fs::path& root, const std::vector<mig::MacRelinkItem>& rows) {
    ehud::data::detail::replaceFile(ledgerPath(root), ehud::data::detail::readFile(ledgerPath(root), mig::MacRelinkLedger::maximumBytes),
                                    mig::MacRelinkLedger::encode(rows), mig::MacRelinkLedger::maximumBytes);
}
std::optional<std::string> bytes(const fs::path& root) { return ehud::data::detail::readFile(ledgerPath(root), mig::MacRelinkLedger::maximumBytes); }
template <class F> bool throwsStore(F&& f, ehud::data::StoreErrorCode code) {
    try { f(); } catch (const ehud::data::StoreError& e) { return e.code() == code; } catch (...) { return false; }
    return false;
}

void targets() {
    check(m::shortcutRelinkTarget(macRecord("abcdef00-0000-4000-8000-000000000001", "Mac")).empty(), "A Mac record names no Windows target");
    check(m::shortcutRelinkTarget(windowsRecord("abcdef00-0000-4000-8000-000000000002", "Exe", executable("C:\\Apps\\A.exe"))) == "C:\\Apps\\A.exe" &&
          m::shortcutRelinkTarget(windowsRecord("abcdef00-0000-4000-8000-000000000003", "Link", shellLink("C:\\Links\\B.lnk"))) == "C:\\Links\\B.lnk",
          "An .exe or .lnk record names its selected file");
    check(m::shortcutRelinkTarget(windowsRecord("abcdef00-0000-4000-8000-000000000004", "Store", packaged("Synthetic.App_8wekyb3d8bbwe!App"))) ==
          "shell:AppsFolder\\Synthetic.App_8wekyb3d8bbwe!App", "A packaged record names its AppsFolder target");
}

void syncLedger() {
    Temp temp;
    const auto root = temp.path;
    const std::string a = "abcdef00-0000-4000-8000-00000000000a", b = "abcdef00-0000-4000-8000-00000000000b", c = "abcdef00-0000-4000-8000-00000000000c",
                      d = "abcdef00-0000-4000-8000-00000000000d", e = "abcdef00-0000-4000-8000-00000000000e", f = "abcdef00-0000-4000-8000-00000000000f";
    m::ShortcutFile file;
    file.items = {macRecord(a, "Alpha"), windowsRecord(b, "Beta", executable("C:\\Apps\\Beta.exe")), windowsRecord(d, "Delta", packaged("Synthetic.Delta_8wekyb3d8bbwe!App")),
                  windowsRecord(e, "Echo", shellLink("C:\\Links\\Echo.lnk")), windowsRecord(f, "Foxtrot", executable("C:\\Apps\\Foxtrot.exe"))};

    // 1. Without a ledger nothing is read into existence or written.
    check(m::syncShortcutRelinks(root, file) == m::ShortcutRelinkSync{} && !fs::exists(root / "Migration"), "Without a ledger the sync writes nothing");

    // 2. The import's rows: still Mac (A), relinked to an .exe (B), removed
    //    by the user (C), relinked to a packaged app (D), relinked to a .lnk
    //    with the ledger's id in upper case (E), resolved earlier (F), a
    //    Reader row sharing B's record id (G) and a dismissed row (H).
    auto rows = std::vector<mig::MacRelinkItem>{row(mig::relinkAppShortcut, a, "Alpha"), row(mig::relinkAppShortcut, b, "Beta"), row(mig::relinkAppShortcut, c, "Charlie"),
        row(mig::relinkAppShortcut, d, "Delta"), row(mig::relinkAppShortcut, upper(e), "Echo"), row(mig::relinkAppShortcut, f, "Foxtrot"), row(mig::relinkReaderBook, b, "Book"),
        row(mig::relinkAppShortcut, "abcdef00-0000-4000-8000-000000000011", "Hotel")};
    rows[5].state = mig::RelinkState::resolved;rows[5].windowsPath = "C:\\Earlier\\Foxtrot.exe";
    rows[7].state = mig::RelinkState::dismissed;
    writeLedger(root, rows);
    // The ledger records a packaged application's AppsFolder target for app
    // shortcut rows only (validMacRelinkTarget).
    check(mig::validMacRelinkTarget(mig::relinkAppShortcut, "shell:AppsFolder\\Synthetic.Delta_8wekyb3d8bbwe!App") &&
          !mig::validMacRelinkTarget(mig::relinkReaderBook, "shell:AppsFolder\\Synthetic.Delta_8wekyb3d8bbwe!App"),
          "An AppsFolder target is a valid relink target of app shortcuts only");
    const auto first = m::syncShortcutRelinks(root, file);
    check(first.resolved == 3 && first.dismissed == 1 && first.open == 1,
          "The sync resolves relinked rows, dismisses removed records and keeps the others listed");
    {
        const mig::MacRelinkLedger ledger(root);
        const auto state = [&](std::size_t n) { return ledger.find(rows[n].id); };
        check(state(0)->state == mig::RelinkState::unresolved && !state(0)->windowsPath, "A record that still holds its Mac locator stays listed");
        check(state(1)->state == mig::RelinkState::resolved && state(1)->windowsPath == std::optional<std::string>("C:\\Apps\\Beta.exe"),
              "A record relinked to an .exe resolves its row with that file");
        check(state(2)->state == mig::RelinkState::dismissed && !state(2)->windowsPath, "A record the user removed dismisses its row");
        check(state(3)->state == mig::RelinkState::resolved && state(3)->windowsPath == std::optional<std::string>("shell:AppsFolder\\Synthetic.Delta_8wekyb3d8bbwe!App"),
              "A record relinked to a packaged application resolves its row with its AppsFolder target");
        check(state(4)->state == mig::RelinkState::resolved && state(4)->windowsPath == std::optional<std::string>("C:\\Links\\Echo.lnk"),
              "Record ids match regardless of case and a .lnk resolves with the selected link");
        check(*state(5) == rows[5] && *state(6) == rows[6] && *state(7) == rows[7], "Earlier decisions and other stores' rows are untouched");
        check(ledger.items().size() == rows.size(), "The sync adds and removes no rows");
    }

    // 3. Idempotent: nothing changes, nothing is written.
    const auto settled = bytes(root);
    check(m::syncShortcutRelinks(root, file) == m::ShortcutRelinkSync{0, 0, first.open} && bytes(root) == settled, "A second sync writes nothing");

    // 4. A later relink of A resolves the last listed row.
    file.items[0] = windowsRecord(a, "Alpha", executable("C:\\Apps\\Alpha 应用.exe"));
    const auto later = m::syncShortcutRelinks(root, file);
    check(later.resolved == 1 && later.dismissed == 0 && later.open == 0 &&
          mig::MacRelinkLedger(root).find(rows[0].id)->windowsPath == std::optional<std::string>("C:\\Apps\\Alpha 应用.exe"), "A later relink resolves its row with its Unicode path");

    // 5. An unreadable or newer ledger throws and is preserved byte for byte.
    const std::string broken = "{\"version\":1,\"items\":[";
    ehud::data::detail::replaceFile(ledgerPath(root), bytes(root), broken, mig::MacRelinkLedger::maximumBytes);
    check(throwsStore([&] { (void)m::syncShortcutRelinks(root, file); }, ehud::data::StoreErrorCode::invalid) && bytes(root) == broken, "An unreadable ledger throws and is kept");
    const std::string newer = "{\"version\":2,\"items\":[]}";
    ehud::data::detail::replaceFile(ledgerPath(root), bytes(root), newer, mig::MacRelinkLedger::maximumBytes);
    check(throwsStore([&] { (void)m::syncShortcutRelinks(root, file); }, ehud::data::StoreErrorCode::newerVersion) && bytes(root) == newer, "A newer ledger throws and is kept");

    // 6. A ledger without app shortcut rows is never rewritten.
    Temp other;
    writeLedger(other.path, {row(mig::relinkReaderBook, a, "Book")});
    const auto readerOnly = bytes(other.path);
    file.items = {};
    check(m::syncShortcutRelinks(other.path, file) == m::ShortcutRelinkSync{} && bytes(other.path) == readerOnly, "Other stores' rows are left alone even when no shortcut remains");
}
} // namespace

int main() {
    try {
        targets();
        syncLedger();
        std::cout << "App shortcut relink ledger: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
