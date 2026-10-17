// The relink ledger's owner API (core/migration/mac_import_relink.hpp): the
// calls Notes, Archive, Reader, App shortcuts and the File Shelf make after
// their own relinks, removals and loads, addressed by store and record id.
// Synthetic ledgers below a fresh temporary directory only; no picker, store
// or user file is touched.
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_relink.hpp"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace mig = ehud::migration;
namespace data = ehud::data;
namespace fs = std::filesystem;
using J = data::Json;
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
template <class F> bool throwsStore(F&& f, data::StoreErrorCode code) {
    try { f(); } catch (const data::StoreError& e) { return e.code() == code; } catch (...) { return false; }
    return false;
}
struct Temporary {
    fs::path root = fs::canonical(fs::temp_directory_path()) / ("Endfield-relink-" + data::makeUUID());
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
std::string id(unsigned n) {
    auto text = std::to_string(n);
    return "abcdef00-0000-4000-8000-" + std::string(12 - text.size(), '0') + text;
}
std::string upper(std::string s) { for (auto& c : s) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A'); return s; }
J macMedia(const std::string& name) {
    return J::Object{{"version", 1}, {"kind", "image"}, {"bookmark", "Ym9va21hcms="}, {"isSecurityScoped", true}, {"lastKnownPath", "/Users/doctor/" + name},
                     {"displayName", name}, {"pixelWidth", 4}, {"pixelHeight", 3}, {"frameCount", 1}};
}
J windowsMedia(const std::string& path) {
    return J::Object{{"version", 1}, {"kind", "image"}, {"referencePlatform", "windows"}, {"windowsPath", path}, {"displayName", "x.png"},
                     {"pixelWidth", 4}, {"pixelHeight", 3}, {"frameCount", 1}};
}
mig::MacRelinkItem row(std::string_view store, std::string record, std::string name, std::optional<std::size_t> index = {}, J reference = J::Object{}) {
    mig::MacRelinkItem item;
    item.id = data::makeUUID();
    item.store = std::string(store);
    item.recordID = std::move(record);
    item.index = index;
    item.displayName = name;
    item.lastKnownPath = "/Users/doctor/" + name;
    item.kind = "image";
    item.macReference = reference.isObject() && !reference.object().empty() ? std::move(reference) : macMedia(name);
    return item;
}
const mig::MacRelinkItem& at(const mig::MacRelinkLedger& ledger, const mig::MacRelinkItem& item) {
    const auto* found = ledger.find(item.id);
    check(found != nullptr, "Every row stays in the ledger");
    return *found;
}

void targets() {
    using mig::validMacRelinkTarget;
    for (const auto store : {mig::relinkNotesMedia, mig::relinkArchiveMedia, mig::relinkReaderBook, mig::relinkAppShortcut, mig::relinkShelfItem})
        check(validMacRelinkTarget(store, "C:\\Users\\Doctor\\Pictures\\Plan 计划.png") && validMacRelinkTarget(store, "\\\\server\\share\\book.epub") &&
              !validMacRelinkTarget(store, "relative\\file.png") && !validMacRelinkTarget(store, "/Users/doctor/file.png") && !validMacRelinkTarget(store, ""),
              "Every store records explicit absolute Windows file paths only");
    const std::string packaged = "shell:AppsFolder\\Synthetic.Delta_8wekyb3d8bbwe!App";
    check(validMacRelinkTarget(mig::relinkAppShortcut, packaged) && validMacRelinkTarget(mig::relinkAppShortcut, "shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"),
          "App shortcuts also record a packaged application's AppsFolder target");
    for (const auto store : {mig::relinkNotesMedia, mig::relinkArchiveMedia, mig::relinkReaderBook, mig::relinkShelfItem})
        check(!validMacRelinkTarget(store, packaged), "Only App shortcuts record packaged applications");
    for (const auto* bad : {"shell:AppsFolder\\", "shell:AppsFolder\\NoBang", "shell:AppsFolder\\!App", "shell:AppsFolder\\Family!", "shell:AppsFolder\\A!B!C",
                            "shell:AppsFolder\\Fam ily!App", "shell:AppsFolder\\Family\\x!App", "shell:AppsFolder\\Family/x!App", "shell:appsfolder\\Family!App",
                            "shell:AppsFolder\\Famille\xC3\xA9!App", "AppsFolder\\Family!App"})
        check(!validMacRelinkTarget(mig::relinkAppShortcut, bad), std::string("A malformed AppsFolder target is refused: ") + bad);
    check(validMacRelinkTarget(mig::relinkAppShortcut, "shell:AppsFolder\\" + std::string(64, 'a') + "!" + std::string(65, 'b')) &&
          !validMacRelinkTarget(mig::relinkAppShortcut, "shell:AppsFolder\\" + std::string(65, 'a') + "!" + std::string(65, 'b')),
          "An application user model id has at most 130 characters");
    check(!validMacRelinkTarget("notes.other", "C:\\file.png"), "Unknown stores record nothing");
    // The ledger itself (markResolved, decode) follows the same rule.
    Temporary t;
    auto shortcut = row(mig::relinkAppShortcut, id(1), "Delta.app"), book = row(mig::relinkReaderBook, id(2), "Book.epub");
    writeLedger(t.root, {shortcut, book});
    mig::MacRelinkLedger ledger(t.root);
    ledger.markResolved(shortcut.id, packaged);
    check(throwsStore([&] { ledger.markResolved(book.id, packaged); }, data::StoreErrorCode::invalid), "A book cannot be relinked to an application");
    const mig::MacRelinkLedger reread(t.root);
    check(at(reread, shortcut).state == mig::RelinkState::resolved && at(reread, shortcut).windowsPath == std::optional<std::string>(packaged) &&
          at(reread, book).state == mig::RelinkState::unresolved, "A recorded AppsFolder target reads back");
    auto forged = mig::encodeRelinkItem(book);
    forged["state"] = "resolved";
    forged["windowsPath"] = packaged;
    check(throwsStore([&] { (void)mig::decodeRelinkItem(forged); }, data::StoreErrorCode::invalid), "A book row naming an application is invalid");
}

void ownerCalls() {
    Temporary t;
    const auto& root = t.root;
    // 1. Without a ledger (no Mac import) nothing is read into existence.
    check(mig::resolveMacRelink(root, {mig::relinkNotesMedia, id(1)}, "C:\\Pictures\\a.png") == mig::MacRelinkChange{} &&
          mig::dismissMacRelink(root, {mig::relinkArchiveMedia, id(2)}) == mig::MacRelinkChange{} &&
          mig::reconcileMacRelinks(root, mig::relinkReaderBook, [](const auto&) { return mig::RelinkRecordCheck{mig::RelinkRecordState::missing, {}}; }) == mig::MacRelinkChange{} &&
          !fs::exists(root / "Migration"), "Without a ledger the owner calls write nothing");

    // 2. The import's rows: two notes, an Archive entry with three
    //    attachments and one with one, a book, a shortcut, a shelf item, and
    //    earlier decisions (a resolved and a dismissed note).
    const auto a0 = macMedia("a0.png"), a1 = macMedia("a1.png"), a2 = macMedia("a2.png"), b0 = macMedia("b0.png");
    std::vector<mig::MacRelinkItem> rows{
        row(mig::relinkNotesMedia, upper(id(10)), "note-a.png"), row(mig::relinkNotesMedia, id(11), "note-b.png"),
        row(mig::relinkArchiveMedia, id(20), "a0.png", 0, a0), row(mig::relinkArchiveMedia, id(20), "a1.png", 1, a1), row(mig::relinkArchiveMedia, id(20), "a2.png", 2, a2),
        row(mig::relinkArchiveMedia, id(21), "b0.png", 0, b0),
        row(mig::relinkReaderBook, id(30), "Book.epub"), row(mig::relinkAppShortcut, id(40), "Delta.app"), row(mig::relinkShelfItem, id(50), "Plan.key"),
        row(mig::relinkNotesMedia, id(12), "earlier.png"), row(mig::relinkNotesMedia, id(13), "dismissed.png")};
    rows[9].state = mig::RelinkState::resolved; rows[9].windowsPath = "C:\\Earlier\\earlier.png";
    rows[10].state = mig::RelinkState::dismissed;
    writeLedger(root, rows);

    // Notes: the owner's id in another case still names the row.
    auto change = mig::resolveMacRelink(root, {mig::relinkNotesMedia, id(10)}, "C:\\Pictures\\Note A 图.png");
    check(change == mig::MacRelinkChange{1, 0, 1}, "A relinked note resolves its one row; the other note stays open");
    const auto settled = bytes(root);
    check(mig::resolveMacRelink(root, {mig::relinkNotesMedia, id(10)}, "C:\\Pictures\\Other.png") == mig::MacRelinkChange{0, 0, 1} && bytes(root) == settled,
          "Repeating a resolve writes nothing and keeps the first choice");
    check(mig::resolveMacRelink(root, {mig::relinkNotesMedia, id(12)}, "C:\\Later\\x.png") == mig::MacRelinkChange{0, 0, 1} &&
          mig::resolveMacRelink(root, {mig::relinkNotesMedia, id(13)}, "C:\\Later\\y.png") == mig::MacRelinkChange{0, 0, 1} && bytes(root) == settled,
          "Earlier decisions are final");
    check(mig::resolveMacRelink(root, {mig::relinkNotesMedia, id(99)}, "C:\\Later\\z.png") == mig::MacRelinkChange{0, 0, 1} && bytes(root) == settled,
          "A record the import never listed changes nothing");

    // Archive: by the attachment's Mac object, by position, then the entry.
    change = mig::resolveMacRelink(root, {mig::relinkArchiveMedia, id(20), &a1, 0}, "C:\\Pictures\\a1.png");
    check(change == mig::MacRelinkChange{1, 0, 3}, "The Mac object names its attachment even when its position moved");
    change = mig::resolveMacRelink(root, {mig::relinkArchiveMedia, id(20), nullptr, 2}, "C:\\Pictures\\a2.png");
    check(change == mig::MacRelinkChange{1, 0, 2}, "Without the object, the import position names the attachment");
    change = mig::dismissMacRelink(root, {mig::relinkArchiveMedia, id(20)});
    check(change == mig::MacRelinkChange{0, 1, 1}, "Deleting the entry dismisses its open attachments only");
    {
        const mig::MacRelinkLedger ledger(root);
        check(at(ledger, rows[0]).state == mig::RelinkState::resolved && at(ledger, rows[0]).windowsPath == std::optional<std::string>("C:\\Pictures\\Note A 图.png"),
              "The note row records the chosen Unicode path");
        check(at(ledger, rows[2]).state == mig::RelinkState::dismissed && !at(ledger, rows[2]).windowsPath &&
              at(ledger, rows[3]).windowsPath == std::optional<std::string>("C:\\Pictures\\a1.png") &&
              at(ledger, rows[4]).windowsPath == std::optional<std::string>("C:\\Pictures\\a2.png") && at(ledger, rows[5]).state == mig::RelinkState::unresolved,
              "Each attachment keeps its own decision; the other entry is untouched");
        for (std::size_t n : {1u, 6u, 7u, 8u, 9u, 10u}) check(at(ledger, rows[n]) == rows[n], "Other rows and stores are untouched");
        check(ledger.items().size() == rows.size(), "Owner calls add and remove no rows");
    }

    // Reader, App shortcuts (an application chosen in the dialog), Shelf.
    check(mig::resolveMacRelink(root, {mig::relinkAppShortcut, id(40)}, "shell:AppsFolder\\Synthetic.Delta_8wekyb3d8bbwe!App") == mig::MacRelinkChange{1, 0, 0},
          "A shortcut relinked to a packaged application resolves its row");
    check(mig::dismissMacRelink(root, {mig::relinkShelfItem, id(50)}) == mig::MacRelinkChange{0, 1, 0}, "A removed shelf item dismisses its row");
    // Refusals happen before anything is read or written.
    const auto before = bytes(root);
    check(throwsStore([&] { (void)mig::resolveMacRelink(root, {mig::relinkReaderBook, id(30)}, "shell:AppsFolder\\Synthetic.Delta_8wekyb3d8bbwe!App"); },
                      data::StoreErrorCode::invalid) &&
          throwsStore([&] { (void)mig::resolveMacRelink(root, {mig::relinkReaderBook, id(30)}, "Book.epub"); }, data::StoreErrorCode::invalid) &&
          throwsStore([&] { (void)mig::resolveMacRelink(root, {"reader.other", id(30)}, "C:\\Book.epub"); }, data::StoreErrorCode::invalid) &&
          throwsStore([&] { (void)mig::dismissMacRelink(root, {mig::relinkReaderBook, "not-a-uuid"}); }, data::StoreErrorCode::invalid) &&
          throwsStore([&] { (void)mig::reconcileMacRelinks(root, mig::relinkReaderBook, nullptr); }, data::StoreErrorCode::invalid) &&
          bytes(root) == before, "Invalid targets, stores, ids and checks are refused without a write");
    {
        const J notObject = J::Array{1};
        check(throwsStore([&] { (void)mig::dismissMacRelink(root, {mig::relinkArchiveMedia, id(21), &notObject}); }, data::StoreErrorCode::invalid),
              "An attachment key's Mac reference is an object");
    }
    check(mig::macRelinkMatches(rows[0], {mig::relinkNotesMedia, id(10)}) && !mig::macRelinkMatches(rows[0], {mig::relinkArchiveMedia, id(10)}) &&
          mig::macRelinkMatches(rows[3], {mig::relinkArchiveMedia, id(20), &a1}) && !mig::macRelinkMatches(rows[3], {mig::relinkArchiveMedia, id(20), &a2, 1}) &&
          mig::macRelinkMatches(rows[3], {mig::relinkArchiveMedia, upper(id(20)), nullptr, 1}) && !mig::macRelinkMatches(rows[3], {mig::relinkArchiveMedia, id(20), nullptr, 0}),
          "Matching: store and record id (any case), then the attachment by object or position");

    // 3. Unreadable and newer ledgers throw and are kept byte for byte.
    const std::string broken = "{\"version\":1,\"items\":[";
    data::detail::replaceFile(ledgerPath(root), bytes(root), broken, mig::MacRelinkLedger::maximumBytes);
    check(throwsStore([&] { (void)mig::resolveMacRelink(root, {mig::relinkNotesMedia, id(11)}, "C:\\x.png"); }, data::StoreErrorCode::invalid) && bytes(root) == broken,
          "An unreadable ledger throws and is kept");
    const std::string newer = "{\"version\":2,\"items\":[]}";
    data::detail::replaceFile(ledgerPath(root), bytes(root), newer, mig::MacRelinkLedger::maximumBytes);
    check(throwsStore([&] { (void)mig::dismissMacRelink(root, {mig::relinkNotesMedia, id(11)}); }, data::StoreErrorCode::newerVersion) && bytes(root) == newer,
          "A newer ledger throws and is kept");
}

void mediaRule() {
    using S = mig::RelinkRecordState;
    const auto mac = macMedia("m.png"), other = macMedia("other.png");
    // The owner's store may re-encode metadata (a number token, an added
    // field): the Mac locator (bookmark and last known path) still names it.
    auto reencoded = mac;
    reencoded["pixelWidth"] = J::parse("4.0");
    reencoded["futureField"] = true;
    auto note = row(mig::relinkNotesMedia, id(1), "m.png", {}, mac);
    const auto state = [](const mig::RelinkRecordCheck& c) { return c.state; };
    check(state(mig::macRelinkMediaCheck(note, std::vector<J>{mac})) == S::macReference && state(mig::macRelinkMediaCheck(note, std::vector<J>{reencoded})) == S::macReference,
          "A note still holding its Mac payload stays listed");
    const auto relinked = mig::macRelinkMediaCheck(note, std::vector<J>{windowsMedia("C:\\Pictures\\m.png")});
    check(relinked.state == S::relinked && relinked.windowsPath == "C:\\Pictures\\m.png", "A note holding a Windows payload resolves with its path");
    check(state(mig::macRelinkMediaCheck(note, std::vector<J>{})) == S::missing, "A note without media dismisses its row");
    check(state(mig::macRelinkMediaCheck(note, std::vector<J>{windowsMedia("relative.png")})) == S::macReference &&
          state(mig::macRelinkMediaCheck(note, std::vector<J>{other})) == S::macReference &&
          state(mig::macRelinkMediaCheck(note, std::vector<J>{windowsMedia("C:\\a.png"), windowsMedia("C:\\b.png")})) == S::macReference &&
          state(mig::macRelinkMediaCheck(note, std::vector<J>{J("text")})) == S::macReference,
          "Anything the rule cannot attribute stays listed");
    // Attachments: identity only, never by position.
    auto attachment = row(mig::relinkArchiveMedia, id(2), "m.png", 1, mac);
    check(state(mig::macRelinkMediaCheck(attachment, std::vector<J>{reencoded})) == S::macReference, "An attachment that moved up keeps its row by its locator");
    check(state(mig::macRelinkMediaCheck(attachment, std::vector<J>{other, windowsMedia("C:\\Pictures\\m.png")})) == S::missing &&
          state(mig::macRelinkMediaCheck(attachment, std::vector<J>{other, other})) == S::missing && state(mig::macRelinkMediaCheck(attachment, std::vector<J>{})) == S::missing,
          "An attachment whose locator is gone is dismissed; no Windows file is attributed by position");
    auto windowsish = mac;
    windowsish["referencePlatform"] = "windows";
    check(state(mig::macRelinkMediaCheck(attachment, std::vector<J>{windowsish})) == S::missing, "A Windows reference never carries a Mac locator");
}

void reconcile() {
    Temporary t;
    const auto& root = t.root;
    // The Archive owner's records after the app stopped between its two
    // writes: entry 20 removed attachment 0 and relinked attachment 1 (now
    // at position 0), attachment 2 untouched (now at position 1); entry 21
    // was deleted; entry 22 is unchanged. Notes: note 30 relinked, note 31
    // untouched, note 32 deleted.
    const auto a0 = macMedia("a0.png"), a1 = macMedia("a1.png"), a2 = macMedia("a2.png"), b0 = macMedia("b0.png"), c0 = macMedia("c0.png");
    std::vector<mig::MacRelinkItem> rows{row(mig::relinkArchiveMedia, id(20), "a0.png", 0, a0), row(mig::relinkArchiveMedia, id(20), "a1.png", 1, a1),
        row(mig::relinkArchiveMedia, id(20), "a2.png", 2, a2), row(mig::relinkArchiveMedia, id(21), "b0.png", 0, b0), row(mig::relinkArchiveMedia, id(22), "c0.png", 0, c0),
        row(mig::relinkNotesMedia, id(30), "n30.png"), row(mig::relinkNotesMedia, id(31), "n31.png"), row(mig::relinkNotesMedia, upper(id(32)), "n32.png")};
    writeLedger(root, rows);
    const std::map<std::string, std::vector<J>> archive{{id(20), {windowsMedia("C:\\Pictures\\a1.png"), a2}}, {id(22), {c0}}};
    const std::map<std::string, std::vector<J>> notes{{id(30), {windowsMedia("C:\\Pictures\\n30.png")}}, {id(31), {rows[6].macReference}}};
    std::size_t asked{};
    const auto lookup = [&](const std::map<std::string, std::vector<J>>& records) {
        return [&](const mig::MacRelinkItem& item) -> mig::RelinkRecordCheck {
            ++asked;
            auto lower = item.recordID;
            for (auto& c : lower) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            const auto found = records.find(lower);
            if (found == records.end()) return {mig::RelinkRecordState::missing, {}};
            return mig::macRelinkMediaCheck(item, found->second);
        };
    };
    const auto attachments = mig::reconcileMacRelinks(root, mig::relinkArchiveMedia, lookup(archive));
    check(asked == 5 && attachments == mig::MacRelinkChange{0, 3, 2}, "Attachments: two gone from entry 20 and the deleted entry's one are dismissed");
    asked = 0;
    const auto media = mig::reconcileMacRelinks(root, mig::relinkNotesMedia, lookup(notes));
    check(asked == 3 && media == mig::MacRelinkChange{1, 1, 1}, "Notes: the relinked note resolves, the deleted one is dismissed");
    {
        const mig::MacRelinkLedger ledger(root);
        check(at(ledger, rows[0]).state == mig::RelinkState::dismissed && at(ledger, rows[1]).state == mig::RelinkState::dismissed && !at(ledger, rows[1]).windowsPath,
              "No Windows attachment is attributed to a row by position");
        check(at(ledger, rows[2]) == rows[2] && at(ledger, rows[4]) == rows[4] && at(ledger, rows[3]).state == mig::RelinkState::dismissed,
              "Attachments still holding their Mac locator stay listed");
        check(at(ledger, rows[5]).windowsPath == std::optional<std::string>("C:\\Pictures\\n30.png") && at(ledger, rows[6]) == rows[6] &&
              at(ledger, rows[7]).state == mig::RelinkState::dismissed, "Note rows follow their notes");
    }
    const auto settled = bytes(root);
    asked = 0;
    check(mig::reconcileMacRelinks(root, mig::relinkArchiveMedia, lookup(archive)) == mig::MacRelinkChange{0, 0, 2} &&
          mig::reconcileMacRelinks(root, mig::relinkNotesMedia, lookup(notes)) == mig::MacRelinkChange{0, 0, 1} && asked == 3 && bytes(root) == settled,
          "A second reconcile looks up only the open rows and writes nothing");
}

// Another writer replaced the ledger between the read and the write: the
// call reads it again and applies its change on top (bounded).
void concurrentWriters() {
    Temporary t;
    const auto& root = t.root;
    std::vector<mig::MacRelinkItem> rows{row(mig::relinkReaderBook, id(1), "One.epub"), row(mig::relinkReaderBook, id(2), "Two.epub"), row(mig::relinkShelfItem, id(3), "Three.key")};
    writeLedger(root, rows);
    unsigned calls{};
    const auto change = mig::reconcileMacRelinks(root, mig::relinkReaderBook, [&](const mig::MacRelinkItem& item) -> mig::RelinkRecordCheck {
        if (item.recordID == id(1) && ++calls == 1) {
            // The File Shelf (another writer) dismisses its row meanwhile.
            mig::MacRelinkLedger shelf(root);
            shelf.dismiss(rows[2].id);
        }
        return item.recordID == id(1) ? mig::RelinkRecordCheck{mig::RelinkRecordState::relinked, "C:\\Books\\One.epub"} : mig::RelinkRecordCheck{};
    });
    const mig::MacRelinkLedger ledger(root);
    check(calls == 2 && change == mig::MacRelinkChange{1, 0, 1}, "A changed ledger is read again and the reconcile repeated");
    check(at(ledger, rows[0]).windowsPath == std::optional<std::string>("C:\\Books\\One.epub") && at(ledger, rows[2]).state == mig::RelinkState::dismissed,
          "Neither writer's change is lost");
    // A writer that never stops: bounded attempts, then the conflict surfaces.
    unsigned attempts{};
    std::optional<std::string> last;
    check(throwsStore([&] {
        (void)mig::reconcileMacRelinks(root, mig::relinkReaderBook, [&](const mig::MacRelinkItem&) -> mig::RelinkRecordCheck {
            ++attempts;
            auto next = mig::MacRelinkLedger::decode(*bytes(root));
            next.push_back(row(mig::relinkShelfItem, id(100 + attempts), "Extra.key"));
            writeLedger(root, next);
            last = bytes(root);
            return {mig::RelinkRecordState::missing, {}};
        });
    }, data::StoreErrorCode::changedOnDisk), "An endlessly changing ledger reports the conflict");
    check(attempts == 4 && bytes(root) == last, "Four attempts, and the other writer's bytes are kept");

    // Owners on different threads (the App shortcuts worker, the shelf, the
    // UI) never lose each other's decisions within the process.
    std::vector<mig::MacRelinkItem> many;
    for (unsigned n = 0; n < 24; ++n) many.push_back(row(n % 2 ? mig::relinkShelfItem : mig::relinkNotesMedia, id(200 + n), "item" + std::to_string(n)));
    writeLedger(root, many);
    std::atomic<unsigned> failures{};
    const auto worker = [&](unsigned parity) {
        for (unsigned n = parity; n < many.size(); n += 2) {
            try {
                if (parity) (void)mig::dismissMacRelink(root, {mig::relinkShelfItem, id(200 + n)});
                else (void)mig::resolveMacRelink(root, {mig::relinkNotesMedia, id(200 + n)}, "C:\\Notes\\item" + std::to_string(n) + ".png");
            } catch (...) { ++failures; }
        }
    };
    std::thread first(worker, 0u), second(worker, 1u);
    first.join(); second.join();
    const mig::MacRelinkLedger after(root);
    check(failures == 0 && std::all_of(after.items().begin(), after.items().end(), [](const auto& item) { return item.state != mig::RelinkState::unresolved; }),
          "Concurrent owner calls in one process all land");
}
} // namespace

int main() {
    try {
        targets();
        ownerCalls();
        mediaRule();
        reconcile();
        concurrentWriters();
        std::cout << "PASS " << checks << " relink ledger owner API checks; synthetic ledgers only\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
