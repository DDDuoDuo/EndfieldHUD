// The importer's private work folders: marker, lock, activation journal, the
// startup sweep and the pre-open recovery of an interrupted activation
// (core/migration/mac_import_work.hpp). Synthetic temporary folders only.
#include "mac_import_test_support.hpp"
#include "core/migration/mac_import_outcome.hpp"
#include "core/migration/mac_import_work.hpp"

namespace {
using State = m::MacImportWorkState;
std::string token(unsigned n) { return uuid(n); }
// The footprint a process leaves when it ends while owning a work folder: the
// marker stays, its lock is gone with the process.
fs::path abandon(const fs::path& destination, unsigned n) {
    auto lock = m::MacImportWorkLock::create(destination, token(n), 1760000000.0);
    auto folder = lock.folder();
    return folder; // the destructor releases the lock and keeps the folder
}
State stateOf(const fs::path& destination, const fs::path& folder) {
    for (const auto& f : m::inspectMacImportWork(destination)) if (f.folder == folder) return f.state;
    throw std::runtime_error("Work folder not listed: " + folder.filename().string());
}
bool listed(const fs::path& destination, const fs::path& folder) {
    for (const auto& f : m::inspectMacImportWork(destination)) if (f.folder == folder) return true;
    return false;
}

void codecs() {
    const auto marker = m::encodeMacImportWorkMarker("v1", token(1), 1760000000.5);
    check(m::decodeMacImportWorkMarker(marker, "v1") == token(1), "A marker names its root and token");
    check(!m::decodeMacImportWorkMarker(marker, "v2"), "A marker of another root is not ours");
    check(!m::decodeMacImportWorkMarker(R"({"format":"EndfieldHUD.macImportWork","version":2,"root":"v1","token":"A","createdAt":1})", "v1"), "A later marker version is not ours");
    check(!m::decodeMacImportWorkMarker(R"({"format":"EndfieldHUD.macImportWork","version":1,"root":"v1","token":"A","createdAt":1,"x":1})", "v1"), "Unknown marker fields are refused");
    check(!m::decodeMacImportWorkMarker(R"({"format":"EndfieldHUD.macImportWork","version":1,"root":"v1","token":"A/B","createdAt":1})", "v1"), "A token with a separator is refused");
    check(!m::decodeMacImportWorkMarker("", "v1") && !m::decodeMacImportWorkMarker("{", "v1"), "Partial markers are refused");
    check(m::validMacImportWorkToken(token(3)) && !m::validMacImportWorkToken("") && !m::validMacImportWorkToken("a_b") && !m::validMacImportWorkToken(std::string(65, 'A')),
          "Tokens are UUID-like");
    rejects([] { (void)m::encodeMacImportWorkMarker("v1", "..", 1); }, "An unsafe token cannot be encoded");
    const auto journal = m::encodeMacImportActivation("v1", "v1.backup-20251009T000000Z-ABCDEF12");
    check(m::decodeMacImportActivation(journal, "v1") == std::string("v1.backup-20251009T000000Z-ABCDEF12"), "The journal names the backup");
    check(m::decodeMacImportActivation(m::encodeMacImportActivation("v1", ""), "v1") == std::string{}, "A journal without backup (no earlier root)");
    rejects([] { (void)m::encodeMacImportActivation("v1", "other"); }, "Only the importer's backup names are journaled");
    rejects([] { (void)m::encodeMacImportActivation("v1", "v1.backup-x/../y"); }, "A backup name with separators is refused");
    check(!m::decodeMacImportActivation(R"({"format":"EndfieldHUD.macImportActivation","version":1,"root":"v1","backup":"v1"})", "v1") &&
          !m::decodeMacImportActivation(R"({"format":"EndfieldHUD.macImportActivation","version":1,"root":"v1","backup":"..\\v1.backup-a"})", "v1") &&
          !m::decodeMacImportActivation(R"({"format":"EndfieldHUD.macImportActivation","version":1,"root":"v1"})", "v1"),
          "Journals naming anything but a sibling backup are refused");
    Temporary t;
    check(m::macImportWorkPath(t.root / "v1", token(4)) == t.root / ("v1.import-" + token(4)), "Work folders sit next to the root");
    rejects([&] { (void)m::macImportWorkPath(fs::path("v1"), token(4)); }, "Only an explicit absolute root has work folders");
}

void lockAndSweep() {
    Temporary t;
    const auto destination = t.root / "EndfieldHUD" / "v1";
    fs::create_directories(destination);
    write(destination / "settings.json", "{}");
    const auto parent = destination.parent_path();
    write(parent / "v1.mac-import-outcome.json", "note");
    fs::create_directories(parent / "v1.backup-20251009T000000Z-AAAAAAAA" / "Notes");
    write(parent / "v1.backup-20251009T000000Z-AAAAAAAA" / "Notes" / "kept.txt", "backup");
    const auto outside = t.root / "Outside";
    write(outside / "user.txt", "user data");

    // A live session's folder: marked, locked, never swept.
    auto live = m::MacImportWorkLock::create(destination, token(1), 1760000000.0);
    check(live.held() && fs::exists(live.folder() / "import-work.json") && stateOf(destination, live.folder()) == State::live, "A running import's folder is live");
    rejects([&] { (void)m::MacImportWorkLock::create(destination, token(1), 1); }, "A work folder is created exclusively");
    write(live.folder() / "source" / "a.bin", "copy");

    // Abandoned (never activated): a deep tree, removed over bounded steps.
    const auto abandoned = abandon(destination, 2);
    for (unsigned d = 0; d < 6; ++d)
        for (unsigned f = 0; f < 120; ++f) write(abandoned / "source" / ("d" + std::to_string(d)) / "x" / ("f" + std::to_string(f)), "copy");
    fs::create_directories(abandoned / "root" / "Notes" / "Images");
    write(abandoned / "root" / "settings.json", "{}");
    // Activated: the journal was written and the stage moved into place.
    const auto activated = abandon(destination, 3);
    write(activated / "activation.json", m::encodeMacImportActivation("v1", "v1.backup-20251009T000000Z-AAAAAAAA"));
    write(activated / "source" / "b.bin", "copy");
    // Interrupted: the root is at its backup, the stage still here.
    const auto interrupted = abandon(destination, 4);
    write(interrupted / "activation.json", m::encodeMacImportActivation("v1", "v1.backup-20251009T000000Z-AAAAAAAA"));
    write(interrupted / "root" / "settings.json", "{}");
    // The journal names a backup that never appeared: the first rename did not happen.
    const auto beforeRename = abandon(destination, 5);
    write(beforeRename / "activation.json", m::encodeMacImportActivation("v1", "v1.backup-20251009T000000Z-BBBBBBBB"));
    write(beforeRename / "root" / "settings.json", "{}");
    // A partial journal (the process ended while writing it): no rename happened.
    const auto partialJournal = abandon(destination, 6);
    write(partialJournal / "activation.json", "{\"format\":\"EndfieldHUD.macImp");
    write(partialJournal / "root" / "settings.json", "{}");
    // Look-alikes that are not provably the importer's.
    const auto unmarked = parent / ("v1.import-" + token(7));
    write(unmarked / "user.txt", "user data");
    const auto foreignEntry = abandon(destination, 8);
    write(foreignEntry / "notes.txt", "user data");
    const auto otherRoot = parent / ("v1.import-" + token(9));
    fs::create_directories(otherRoot);
    write(otherRoot / "import-work.json", m::encodeMacImportWorkMarker("v2", token(9), 1));
    const auto otherToken = parent / ("v1.import-" + token(10));
    fs::create_directories(otherToken);
    write(otherToken / "import-work.json", m::encodeMacImportWorkMarker("v1", token(11), 1));
    const auto markerDirectory = parent / ("v1.import-" + token(12));
    fs::create_directories(markerDirectory / "import-work.json");
    const auto empty = parent / ("v1.import-" + token(13));
    fs::create_directories(empty);
    const auto badName = parent / "v1.import-not_a_token";
    write(badName / "import-work.json", m::encodeMacImportWorkMarker("v1", "A", 1));
    const auto otherRootWork = m::MacImportWorkLock::create(parent / "v2", token(14), 1).folder(); // another root's folder
    // A link inside an abandoned folder is removed as an entry, never followed.
    const auto linked = abandon(destination, 15);
    bool linkCreated = false;
    {
        std::error_code error;
        fs::create_directories(linked / "source");
        fs::create_directory_symlink(outside, linked / "source" / "link", error);
        linkCreated = !error;
    }
    const auto userBefore = tree(outside);
    const auto rootBefore = tree(destination);
    const auto backupBefore = tree(parent / "v1.backup-20251009T000000Z-AAAAAAAA");

    check(stateOf(destination, abandoned) == State::abandoned && stateOf(destination, activated) == State::activated &&
          stateOf(destination, interrupted) == State::interrupted && stateOf(destination, beforeRename) == State::abandoned &&
          stateOf(destination, partialJournal) == State::abandoned, "Marked folders are classified by their journal and stage");
    check(stateOf(destination, unmarked) == State::unrecognized && stateOf(destination, foreignEntry) == State::unrecognized &&
          stateOf(destination, otherRoot) == State::unrecognized && stateOf(destination, otherToken) == State::unrecognized &&
          stateOf(destination, markerDirectory) == State::unrecognized, "Unmarked, foreign or mismatched folders are not the importer's");
    check(!listed(destination, badName) && !listed(destination, otherRootWork) && !listed(destination, parent / "v1.backup-20251009T000000Z-AAAAAAAA"),
          "Only exact work-folder names of this root are candidates");
    for (const auto& f : m::inspectMacImportWork(destination)) if (f.state == State::interrupted) check(f.backup == parent / "v1.backup-20251009T000000Z-AAAAAAAA", "The interrupted folder names its backup");

    m::MacImportWorkSweep sweep(destination);
    std::uint64_t lastEntries{};
    unsigned steps{};
    while (sweep.next(64)) {
        ++steps;
        check(sweep.stats().entries - lastEntries <= 2 * 64, "One sweep step stays within its budget");
        lastEntries = sweep.stats().entries;
        check(steps < 10000, "The sweep finishes");
    }
    check(steps > 5 && sweep.finished(), "A large folder is removed over many bounded steps");
    const auto& s = sweep.stats();
    check(s.candidates == 13 && s.removed == 6 && s.live == 1 && s.interrupted == 1 && s.unrecognized == 5 && s.failed == 0,
          "Abandoned, activated and empty folders are swept; live, interrupted and unrecognized ones are kept");
    for (const auto& gone : {abandoned, activated, beforeRename, partialJournal, empty, linked}) check(!fs::exists(fs::symlink_status(gone)), "Stale folders are removed completely");
    for (const auto& kept : {interrupted, unmarked, foreignEntry, otherRoot, otherToken, markerDirectory, badName, otherRootWork, live.folder()})
        check(fs::exists(kept), "Unprovable, interrupted and live folders are kept");
    check(read(unmarked / "user.txt") == "user data" && read(foreignEntry / "notes.txt") == "user data" && fs::exists(foreignEntry / "import-work.json"),
          "Kept folders are untouched");
    check(tree(outside) == userBefore && (!linkCreated || read(outside / "user.txt") == "user data"), "A link's target is never followed");
    check(tree(destination) == rootBefore && tree(parent / "v1.backup-20251009T000000Z-AAAAAAAA") == backupBefore && read(parent / "v1.mac-import-outcome.json") == "note",
          "The data root, backups and the outcome note are never touched");

    // The live session ends normally: its folder disappears with it.
    const auto liveFolder = live.folder();
    check(live.remove() && !fs::exists(liveFolder) && !live.held(), "A session removes its own folder, marker last");
    // A second sweep finds nothing new to remove.
    m::MacImportWorkSweep again(destination);
    again.run();
    check(again.stats().removed == 0 && again.stats().interrupted == 1, "The sweep is idempotent");
}

void recovery() {
    Temporary t;
    const auto destination = t.root / "EndfieldHUD" / "v1";
    const auto parent = destination.parent_path();
    const auto backup = parent / "v1.backup-20251009T000000Z-CCCCCCCC";
    write(backup / "Notes" / "previous.txt", "previous data");
    const auto folder = abandon(destination, 20);
    write(folder / "activation.json", m::encodeMacImportActivation("v1", backup.filename().string()));
    write(folder / "root" / "Migration" / "import.json", "{}");
    const auto previous = tree(backup);

    // A root exists (e.g. recreated by a run that started without recovery):
    // nothing is moved.
    fs::create_directories(destination);
    auto kept = m::recoverMacImportWork(destination, 1760000100.0);
    check(kept.restored == 0 && kept.kept == 1 && fs::exists(backup) && stateOf(destination, folder) == State::interrupted, "Recovery never replaces an existing root");
    fs::remove(destination);

    // Before any owner opened the root: the previous data comes back.
    auto restored = m::recoverMacImportWork(destination, 1760000100.0);
    check(restored.restored == 1 && restored.backups.size() == 1 && restored.backups.front() == backup && !fs::exists(backup) && tree(destination) == previous,
          "An interrupted activation is rolled back to the previous root");
    const auto note = m::readMacImportOutcome(destination);
    check(note && note->state == m::MacImportOutcome::State::failed && note->error.find("previous data was restored") != std::string::npos && note->finishedAt == 1760000100.0,
          "The next import card reports the interrupted import");
    check(stateOf(destination, folder) == State::abandoned, "The rolled-back folder is an ordinary abandoned one");
    m::MacImportWorkSweep sweep(destination);
    sweep.run();
    check(sweep.stats().removed == 1 && !fs::exists(folder) && tree(destination) == previous, "The sweep then removes the staged copy only");
    check(m::recoverMacImportWork(destination, 1).restored == 0, "Recovery is idempotent");
}

void sessionLifecycle() {
    using S = m::MacImportStore;
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.write();
    const auto destination = t.root / "EndfieldHUD" / "v1";
    write(destination / "WindowsOnly" / "keep.txt", "kept by the import");
    m::MacImportOptions options;
    options.acceptRejected = {S::archive};

    // While staging and reviewing, the folder is live; the journal appears
    // only right before the first rename and names the backup being made.
    std::optional<fs::path> journaledBackup;
    bool interruptedSeen = false;
    {
        auto o = options;
        fs::path work;
        o.beforeCommitStep = [&](m::MacImportCommitStep step) {
            if (step == m::MacImportCommitStep::backupDestination) {
                const auto journal = read(work / "activation.json");
                const auto name = m::decodeMacImportActivation(journal, "v1");
                check(name && !name->empty(), "The journal is flushed before the root is renamed");
                journaledBackup = destination.parent_path() / *name;
                check(!fs::exists(*journaledBackup) && stateOf(destination, work) == State::live, "Journal first, rename second; the folder stays live");
            }
            if (step == m::MacImportCommitStep::activateStage) {
                interruptedSeen = journaledBackup && fs::exists(*journaledBackup) && fs::exists(work / "root") && !fs::exists(destination);
                throw std::runtime_error("injected failure between the two renames");
            }
        };
        m::MacImportSession session(e.root, destination, platform(), o);
        session.stageNext();
        work = session.workDirectory();
        check(fs::exists(work / "import-work.json") && stateOf(destination, work) == State::live, "A staging session holds its marked folder");
        session.stage();
        check(stateOf(destination, work) == State::live && !fs::exists(work / "activation.json"), "A reviewed session is live and unjournaled");
        rejects([&] { (void)session.commit(); }, "The injected failure propagates");
        check(interruptedSeen, "Between the renames the root is at its journaled backup and the stage is still private");
        check(fs::exists(destination) && !fs::exists(*journaledBackup) && siblingsClean(destination), "A failed second rename is rolled back and the folder removed");
    }

    // A cleanup failure after activation: the outcome note is already
    // recorded; the folder stays (unlocked once the session is gone) and the
    // next run's sweep removes it.
    {
        auto o = options;
        o.replaceExistingImport = true;
        o.beforeCommitStep = [](m::MacImportCommitStep step) { if (step == m::MacImportCommitStep::removeWork) throw std::runtime_error("injected cleanup failure"); };
        fs::path work;
        {
            m::MacImportSession session(e.root, destination, platform(), o);
            session.stage();
            work = session.workDirectory();
            const auto outcome = m::commitMacImportSession(session, destination, 1760000200.0);
            check(outcome.state == m::MacImportOutcome::State::completed && m::readMacImportOutcome(destination) == outcome, "The completed import is recorded");
            check(fs::exists(work) && stateOf(destination, work) == State::live, "The folder outlives a failed cleanup while its session exists");
        }
        check(stateOf(destination, work) == State::activated, "After the session, the leftover is an activated folder");
        m::MacImportWorkSweep sweep(destination);
        sweep.run();
        check(sweep.stats().removed == 1 && siblingsClean(destination) && backups(destination) == 1 && m::macImportCompleted(destination),
              "The sweep removes only the leftover copies; the import and its backup stay");
    }
}
}

int main() {
    try {
        codecs();
        lockAndSweep();
        recovery();
        sessionLifecycle();
        std::cout << "PASS " << checks << " Mac import work-folder checks; temporary synthetic folders only\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
