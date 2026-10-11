// Mac import relink ledger upkeep for Notes media and Archive attachments
// (modules/notes_relink_ledger.hpp) over the import ledger's owner API.
// Synthetic ledgers below a fresh temporary directory only; no picker, store
// or user file is touched.
#include "modules/notes_relink_ledger.hpp"
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_relink.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace mig = ehud::migration;
namespace data = ehud::data;
namespace mod = endfield::modules;
namespace fs = std::filesystem;
using J = data::Json;
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
struct Temporary {
    fs::path root = fs::canonical(fs::temp_directory_path()) / ("Endfield-notes-relink-" + data::makeUUID());
    Temporary() { fs::create_directories(root); }
    ~Temporary() { std::error_code e; fs::remove_all(root, e); }
    Temporary(const Temporary&) = delete;
    Temporary& operator=(const Temporary&) = delete;
};
fs::path ledgerPath(const fs::path& root) { return root / "Migration" / "relink.json"; }
std::optional<std::string> bytes(const fs::path& root) { return data::detail::readFile(ledgerPath(root), mig::MacRelinkLedger::maximumBytes); }
void writeLedger(const fs::path& root, const std::vector<mig::MacRelinkItem>& rows) {
    data::detail::replaceFile(ledgerPath(root), bytes(root), mig::MacRelinkLedger::encode(rows), mig::MacRelinkLedger::maximumBytes);
}
std::vector<mig::MacRelinkItem> rows(const fs::path& root) { return mig::MacRelinkLedger::decode(*bytes(root)); }
std::string id(unsigned n) {
    auto text = std::to_string(n);
    return "abcdef00-0000-4000-8000-" + std::string(12 - text.size(), '0') + text;
}
std::string upper(std::string s) { for (auto& c : s) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A'); return s; }
J macMedia(const std::string& name) {
    return J::Object{{"version", 1}, {"kind", "image"}, {"bookmark", "Ym9va21hcms="}, {"isSecurityScoped", true},
                     {"lastKnownPath", "/Users/doctor/" + name}, {"displayName", name}, {"pixelWidth", 4}, {"pixelHeight", 3}, {"frameCount", 1}};
}
J windowsMedia(const std::string& path) {
    return J::Object{{"version", 1}, {"kind", "image"}, {"referencePlatform", "windows"}, {"windowsPath", path}, {"displayName", "x.png"},
                     {"pixelWidth", 4}, {"pixelHeight", 3}, {"frameCount", 1}};
}
mig::MacRelinkItem row(std::string_view store, std::string record, const J& reference, std::optional<std::size_t> index = {}) {
    mig::MacRelinkItem item;
    item.id = data::makeUUID();
    item.store = std::string(store);
    item.recordID = std::move(record);
    item.index = index;
    item.displayName = reference["displayName"].isString() ? reference["displayName"].string() : std::string("book.epub");
    item.lastKnownPath = reference["lastKnownPath"].isString() ? reference["lastKnownPath"].string() : std::string("/Users/doctor/book.epub");
    item.kind = "image";
    item.macReference = reference;
    return item;
}
const mig::MacRelinkItem& find(const std::vector<mig::MacRelinkItem>& all, std::string_view store, std::string_view record, std::optional<std::size_t> index = {}) {
    for (const auto& item : all) if (item.store == store && item.recordID == record && item.index == index) return item;
    throw std::runtime_error("Missing ledger row");
}

void notes() {
    Temporary t;
    // Without a ledger (no Mac import) nothing is created.
    check(mod::notesMediaRelinked(t.root, id(1), "C:\\Media\\a.png") == mod::RelinkLedgerResult{} && !fs::exists(t.root / "Migration"),
          "Without a ledger a relink creates and writes nothing");
    const J a = macMedia("a.png"), b = macMedia("b.png"), c = macMedia("c.png"), d = macMedia("d.png"), e = macMedia("e.png");
    const auto book = row(mig::relinkReaderBook, id(90), J::Object{{"bookmark", "Ym9vaw=="}, {"lastKnownPath", "/Users/doctor/book.epub"}});
    writeLedger(t.root, {row(mig::relinkNotesMedia, id(1), a), row(mig::relinkNotesMedia, id(2), b), row(mig::relinkNotesMedia, id(3), c),
                         row(mig::relinkNotesMedia, id(4), d), row(mig::relinkNotesMedia, id(5), e), book});
    // Relink: the note's record was written first; its row resolves (any id case).
    const auto relinked = mod::notesMediaRelinked(t.root, upper(id(1)), "C:\\Media\\a.png");
    check(relinked.resolved == 1 && relinked.dismissed == 0 && !relinked.failed, "A relinked note resolves its one row");
    auto now = rows(t.root);
    check(find(now, mig::relinkNotesMedia, id(1)).state == mig::RelinkState::resolved && find(now, mig::relinkNotesMedia, id(1)).windowsPath == "C:\\Media\\a.png",
          "The resolved row records the chosen Windows path");
    const auto before = bytes(t.root);
    check(mod::notesMediaRelinked(t.root, id(1), "C:\\Media\\other.png") == mod::RelinkLedgerResult{} && bytes(t.root) == before,
          "Repeating a relink changes and writes nothing");
    // Deletion dismisses the note's row.
    check(mod::notesMediaRemoved(t.root, id(2)).dismissed == 1 && find(rows(t.root), mig::relinkNotesMedia, id(2)).state == mig::RelinkState::dismissed,
          "A deleted note's row is dismissed");
    // Load: note 3 was relinked but its ledger write was lost, note 4 was
    // deleted, note 5 still holds its Mac locator.
    std::vector<data::Note> stored(2);
    stored[0].id = upper(id(3)); stored[0].kind = data::NoteKind::image; stored[0].media = windowsMedia("C:\\Media\\c.png").encode();
    stored[1].id = id(5); stored[1].kind = data::NoteKind::image; stored[1].media = e.encode();
    const auto reconciled = mod::notesMediaReconcile(t.root, stored);
    check(reconciled.resolved == 1 && reconciled.dismissed == 1 && !reconciled.failed, "Reconcile resolves the relinked note and dismisses the deleted one");
    now = rows(t.root);
    check(find(now, mig::relinkNotesMedia, id(3)).state == mig::RelinkState::resolved && find(now, mig::relinkNotesMedia, id(3)).windowsPath == "C:\\Media\\c.png" &&
          find(now, mig::relinkNotesMedia, id(4)).state == mig::RelinkState::dismissed && find(now, mig::relinkNotesMedia, id(5)).state == mig::RelinkState::unresolved,
          "A note still holding its Mac locator stays listed");
    check(find(now, mig::relinkReaderBook, id(90)).state == mig::RelinkState::unresolved && !find(now, mig::relinkReaderBook, id(90)).windowsPath && book.state == mig::RelinkState::unresolved,
          "Other stores' rows are untouched");
    // An unreadable ledger is reported, never thrown, and keeps its bytes.
    data::detail::replaceFile(ledgerPath(t.root), bytes(t.root), "{not json", mig::MacRelinkLedger::maximumBytes);
    check(mod::notesMediaRelinked(t.root, id(5), "C:\\Media\\e.png").failed && mod::notesMediaRemoved(t.root, id(5)).failed &&
          mod::notesMediaReconcile(t.root, stored).failed && bytes(t.root) == std::optional<std::string>("{not json"),
          "An unreadable ledger fails the upkeep without changing it");
    check(mod::notesMediaRelinked(t.root, id(5), "relative.png").failed, "A target that is not an explicit Windows path is refused");
}

void archive() {
    Temporary t;
    const auto entry = id(10), other = id(11), gone = id(12), kept = id(13);
    const J m0 = macMedia("0.png"), m1 = macMedia("1.png"), m2 = macMedia("2.png"), g0 = macMedia("g.png"), k0 = macMedia("k0.png"), k1 = macMedia("k1.png");
    writeLedger(t.root, {row(mig::relinkArchiveMedia, entry, m0, 0), row(mig::relinkArchiveMedia, entry, m1, 1), row(mig::relinkArchiveMedia, entry, m2, 2),
                         row(mig::relinkArchiveMedia, other, m0, 0), row(mig::relinkArchiveMedia, gone, g0, 0),
                         row(mig::relinkArchiveMedia, kept, k0, 0), row(mig::relinkArchiveMedia, kept, k1, 1)});
    // Relink of attachment 0 (named by its Mac locator, never by position).
    const auto relinked = mod::archiveMediaRelinked(t.root, upper(entry), m0, "D:\\Archive\\0.png");
    check(relinked.resolved == 1 && !relinked.failed, "A relinked attachment resolves only its own row");
    auto now = rows(t.root);
    check(find(now, mig::relinkArchiveMedia, entry, 0).state == mig::RelinkState::resolved && find(now, mig::relinkArchiveMedia, entry, 0).windowsPath == "D:\\Archive\\0.png" &&
          find(now, mig::relinkArchiveMedia, other, 0).state == mig::RelinkState::unresolved && find(now, mig::relinkArchiveMedia, entry, 1).state == mig::RelinkState::unresolved,
          "Another entry's row from the same Mac file and the sibling attachments stay listed");
    // Removal of attachment 1; deletion of the whole other entry.
    check(mod::archiveMediaRemoved(t.root, entry, &m1).dismissed == 1, "A removed attachment's row is dismissed");
    check(mod::archiveMediaRemoved(t.root, other, nullptr).dismissed == 1, "A deleted entry's rows are dismissed");
    // Load: `gone` no longer exists, `kept` still holds k0 but not k1, and
    // `entry` still holds m2.
    const auto reconciled = mod::archiveMediaReconcile(t.root, [&](std::string_view record) -> std::optional<std::vector<J>> {
        if (record == gone) return std::nullopt;
        if (record == kept) return std::vector<J>{k0, windowsMedia("D:\\Archive\\k1.png")};
        if (record == entry) return std::vector<J>{windowsMedia("D:\\Archive\\0.png"), m2};
        throw std::runtime_error("Reconcile asked about a record without an unresolved row");
    });
    check(reconciled.dismissed == 2 && reconciled.resolved == 0 && !reconciled.failed, "Reconcile dismisses a deleted entry's row and an attachment no longer held");
    now = rows(t.root);
    check(find(now, mig::relinkArchiveMedia, gone, 0).state == mig::RelinkState::dismissed && find(now, mig::relinkArchiveMedia, kept, 1).state == mig::RelinkState::dismissed &&
          find(now, mig::relinkArchiveMedia, kept, 0).state == mig::RelinkState::unresolved && find(now, mig::relinkArchiveMedia, entry, 2).state == mig::RelinkState::unresolved,
          "Attachments still holding their Mac locator stay listed");
    // A reconcile lookup that fails (the store cannot be read) is reported.
    const auto unreadable = mod::archiveMediaReconcile(t.root, [](std::string_view) -> std::optional<std::vector<J>> { throw std::runtime_error("synthetic read failure"); });
    check(unreadable.failed && rows(t.root) == now, "A failing store lookup leaves the ledger as it was");
}
}

int main() {
    try {
        notes();
        archive();
        std::cout << "PASS " << checks << " Notes and Archive relink ledger checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
