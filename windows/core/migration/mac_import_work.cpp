#include "core/migration/mac_import_work.hpp"
#include "core/data/json.hpp"
#include "core/migration/mac_import_codecs.hpp"
#include "core/migration/mac_import_files.hpp"
#include "core/migration/mac_import_outcome.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <system_error>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ehud::migration {
namespace {
namespace fs = std::filesystem;
using data::Json;
constexpr std::string_view markerFormat = "EndfieldHUD.macImportWork";
constexpr std::string_view journalFormat = "EndfieldHUD.macImportActivation";
constexpr std::int64_t noteVersion = 1;
constexpr std::string_view workInfix = ".import-";
constexpr std::string_view backupInfix = ".backup-";
// Top-level entries the importer writes into its own work folder.
constexpr std::array<std::string_view, 4> workEntries{macImportWorkMarker, macImportActivationJournal, "source", "root"};

[[noreturn]] void fail(MacImportErrorCode code, const std::string& message) { throw MacImportError(code, message); }
std::string show(const fs::path& path) { const auto text = path.u8string(); return std::string(reinterpret_cast<const char*>(text.data()), text.size()); }
fs::path utf8Path(std::string_view text) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())); }
std::string rootName(const fs::path& destination) { return show(destination.filename()); }
bool explicitRoot(const fs::path& destination) noexcept {
    try { return destination.is_absolute() && destination.has_filename() && destination.lexically_normal() == destination && destination != destination.root_path(); }
    catch (...) { return false; }
}
files::EntryKind kindOf(const fs::path& path) noexcept {
    try { return files::entryKind(path); } catch (...) { return files::EntryKind::other; }
}
// A plain sibling name (no separators, no dot segments).
bool plainName(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 255 && name != "." && name != ".." && name.find_first_of("/\\:") == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

// --- The marker handle: created locked by a session, or opened exclusively by
// a sweep after no live owner holds it. ---------------------------------------
struct MarkerHandle {
#ifdef _WIN32
    HANDLE value{INVALID_HANDLE_VALUE};
    bool open() const noexcept { return value != INVALID_HANDLE_VALUE; }
    void close() noexcept { if (open()) CloseHandle(value); value = INVALID_HANDLE_VALUE; }
    // Deletes the marker through this handle (it has DELETE access), then closes.
    bool erase() noexcept {
        if (!open()) return false;
        FILE_DISPOSITION_INFO disposition{TRUE};
        const bool ok = SetFileInformationByHandle(value, FileDispositionInfo, &disposition, sizeof disposition) != FALSE;
        close();
        return ok;
    }
#else
    int value{-1};
    fs::path path;
    bool open() const noexcept { return value >= 0; }
    void close() noexcept { if (open()) ::close(value); value = -1; }
    bool erase() noexcept {
        if (!open()) return false;
        const bool ok = ::unlink(path.c_str()) == 0 || errno == ENOENT;
        close();
        return ok;
    }
#endif
    MarkerHandle() = default;
    MarkerHandle(const MarkerHandle&) = delete;
    MarkerHandle& operator=(const MarkerHandle&) = delete;
    MarkerHandle(MarkerHandle&& other) noexcept { take(other); }
    MarkerHandle& operator=(MarkerHandle&& other) noexcept {
        if (this != &other) { close(); take(other); }
        return *this;
    }
    ~MarkerHandle() { close(); }
private:
    void take(MarkerHandle& other) noexcept {
#ifdef _WIN32
        value = std::exchange(other.value, INVALID_HANDLE_VALUE);
#else
        value = std::exchange(other.value, -1);
        path = std::move(other.path);
#endif
    }
};

// Creates the marker exclusively and keeps it locked (shared reads only).
MarkerHandle createMarker(const fs::path& marker, std::string_view bytes) {
    MarkerHandle h;
#ifdef _WIN32
    h.value = CreateFileW(marker.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!h.open()) fail(MacImportErrorCode::unavailable, "Cannot mark the private import folder " + show(marker.parent_path()));
    std::size_t offset{};
    while (offset < bytes.size()) {
        DWORD count{};
        if (!WriteFile(h.value, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &count, nullptr) || !count)
            fail(MacImportErrorCode::unavailable, "Cannot mark the private import folder " + show(marker.parent_path()));
        offset += count;
    }
    if (!FlushFileBuffers(h.value)) fail(MacImportErrorCode::unavailable, "Cannot mark the private import folder " + show(marker.parent_path()));
#else
    h.path = marker;
    h.value = ::open(marker.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (!h.open()) fail(MacImportErrorCode::unavailable, "Cannot mark the private import folder " + show(marker.parent_path()));
    // Locked before any byte exists: a sweep sees either a locked marker or an
    // empty (invalid) one, never a valid unlocked marker of a live session.
    if (::flock(h.value, LOCK_EX | LOCK_NB) != 0) fail(MacImportErrorCode::unavailable, "Cannot lock the private import folder " + show(marker.parent_path()));
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto count = ::write(h.value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) fail(MacImportErrorCode::unavailable, "Cannot mark the private import folder " + show(marker.parent_path()));
        offset += static_cast<std::size_t>(count);
    }
    if (::fsync(h.value) != 0) fail(MacImportErrorCode::unavailable, "Cannot mark the private import folder " + show(marker.parent_path()));
#endif
    return h;
}

enum class Claim { claimed, live, absent, foreign };
// Opens an existing marker exclusively: `live` while its session (or another
// sweep) holds it. Reads at most macImportWorkNoteLimit bytes.
Claim claimMarker(const fs::path& marker, MarkerHandle& h, std::string& bytes) noexcept {
    bytes.clear();
#ifdef _WIN32
    h.value = CreateFileW(marker.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (!h.open()) {
        const auto code = GetLastError();
        if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) return Claim::live;
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return Claim::absent;
        return Claim::foreign;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h.value, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nFileSizeHigh || info.nFileSizeLow > macImportWorkNoteLimit) { h.close(); return Claim::foreign; }
    try { bytes.resize(info.nFileSizeLow); } catch (...) { h.close(); return Claim::foreign; }
    DWORD filled{};
    while (filled < info.nFileSizeLow) {
        DWORD count{};
        if (!ReadFile(h.value, bytes.data() + filled, info.nFileSizeLow - filled, &count, nullptr) || !count) { h.close(); return Claim::foreign; }
        filled += count;
    }
#else
    h.path = marker;
    h.value = ::open(marker.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (!h.open()) return errno == ENOENT ? Claim::absent : Claim::foreign;
    struct stat info {};
    if (::fstat(h.value, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) > macImportWorkNoteLimit) {
        h.close(); return Claim::foreign;
    }
    if (::flock(h.value, LOCK_EX | LOCK_NB) != 0) { const bool held = errno == EWOULDBLOCK; h.close(); return held ? Claim::live : Claim::foreign; }
    try { bytes.resize(static_cast<std::size_t>(info.st_size)); } catch (...) { h.close(); return Claim::foreign; }
    std::size_t filled{};
    while (filled < bytes.size()) {
        const auto count = ::read(h.value, bytes.data() + filled, bytes.size() - filled);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { h.close(); return Claim::foreign; }
        filled += static_cast<std::size_t>(count);
    }
#endif
    return Claim::claimed;
}

std::optional<std::string> readNote(const fs::path& path) noexcept {
    try {
        if (kindOf(path) != files::EntryKind::file) return std::nullopt;
        return files::readFile(path, macImportWorkNoteLimit);
    } catch (...) { return std::nullopt; }
}

// --- Bounded removal of one folder's contents (never following a link). -----
class Eraser {
public:
    Eraser() = default;
    explicit Eraser(fs::path folder) { levels_.push_back({std::move(folder), {}, 0, false}); }
    bool done() const noexcept { return levels_.empty(); }
    bool failed() const noexcept { return failed_; }
    // Removes the folder's contents (the folder itself stays, with its marker:
    // the caller removes those last). At most `budget` operations.
    void advance(std::size_t& budget, std::uint64_t& entries) noexcept {
        while (budget && !levels_.empty()) {
            auto& top = levels_.back();
            try {
                if (!top.listed) {
                    --budget;
                    top.listed = true;
                    std::error_code error;
                    for (fs::directory_iterator it(top.path, error), end; !error && it != end; it.increment(error)) {
                        // The work folder's own marker is removed by the caller, last.
                        if (levels_.size() == 1 && it->path().filename() == utf8Path(macImportWorkMarker)) continue;
                        top.entries.push_back(it->path());
                    }
                    if (error) failed_ = true;
                    continue;
                }
                if (top.next < top.entries.size()) {
                    const auto entry = top.entries[top.next++];
                    if (kindOf(entry) == files::EntryKind::directory) { levels_.push_back({entry, {}, 0, false}); continue; }
                    --budget;
                    std::error_code error;
                    // Files, links and junctions are removed as entries.
                    if (kindOf(entry) != files::EntryKind::missing && !fs::remove(entry, error)) failed_ = true;
                    else ++entries;
                    continue;
                }
                const bool base = levels_.size() == 1;
                if (!base) {
                    --budget;
                    std::error_code error;
                    if (!fs::remove(top.path, error)) failed_ = true;
                    else ++entries;
                }
                levels_.pop_back();
            } catch (...) {
                failed_ = true;
                levels_.pop_back();
            }
        }
    }
private:
    struct Level { fs::path path; std::vector<fs::path> entries; std::size_t next{}; bool listed{}; };
    std::vector<Level> levels_;
    bool failed_{};
};

// Candidates: ordinary-or-not entries named exactly "<root>.import-<token>".
std::vector<fs::path> candidates(const fs::path& destination) noexcept {
    std::vector<fs::path> out;
    try {
        const auto prefix = rootName(destination) + std::string(workInfix);
        std::error_code error;
        for (fs::directory_iterator it(destination.parent_path(), error), end; !error && it != end; it.increment(error)) {
            const auto name = show(it->path().filename());
            if (name.size() > prefix.size() && name.starts_with(prefix) && validMacImportWorkToken(std::string_view(name).substr(prefix.size())))
                out.push_back(it->path());
        }
        std::sort(out.begin(), out.end());
    } catch (...) { out.clear(); }
    return out;
}

// Classifies one candidate. When the result is removable (abandoned or
// activated), `claim` holds the marker exclusively.
struct Classified { MacImportWorkState state{MacImportWorkState::unrecognized}; fs::path backup; bool empty{}; };
Classified classify(const fs::path& destination, const fs::path& folder, MarkerHandle& claim) noexcept {
    Classified out;
    try {
        if (kindOf(folder) != files::EntryKind::directory) return out;
        const auto name = show(folder.filename());
        const auto token = std::string_view(name).substr(rootName(destination).size() + workInfix.size());
        std::string bytes;
        const auto marker = folder / utf8Path(macImportWorkMarker);
        switch (claimMarker(marker, claim, bytes)) {
        case Claim::live: out.state = MacImportWorkState::live; return out;
        case Claim::foreign: return out;
        case Claim::absent: {
            std::error_code error;
            out.empty = fs::is_empty(folder, error) && !error;
            return out;
        }
        case Claim::claimed: break;
        }
        const auto marked = decodeMacImportWorkMarker(bytes, rootName(destination));
        if (!marked || *marked != token) { claim.close(); return out; }
        std::error_code error;
        for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            const auto entry = show(it->path().filename());
            if (std::find(workEntries.begin(), workEntries.end(), entry) == workEntries.end()) { claim.close(); return out; }
        }
        if (error) { claim.close(); return out; }
        const auto stage = kindOf(folder / "root") != files::EntryKind::missing;
        const auto journalPath = folder / utf8Path(macImportActivationJournal);
        if (kindOf(journalPath) == files::EntryKind::missing) { out.state = MacImportWorkState::abandoned; return out; }
        // A journal is flushed before the first rename: an unreadable one means
        // no rename happened (or, without a stage, both did).
        const auto journal = readNote(journalPath);
        const auto backupName = journal ? decodeMacImportActivation(*journal, rootName(destination)) : std::nullopt;
        if (!stage) { out.state = MacImportWorkState::activated; return out; }
        if (backupName && !backupName->empty()) {
            const auto backup = destination.parent_path() / utf8Path(*backupName);
            if (kindOf(backup) == files::EntryKind::directory) {
                out.state = MacImportWorkState::interrupted;
                out.backup = backup;
                claim.close();
                return out;
            }
        }
        out.state = MacImportWorkState::abandoned;
        return out;
    } catch (...) {
        claim.close();
        return Classified{};
    }
}
}

fs::path macImportWorkPath(const fs::path& destination, std::string_view token) {
    if (!explicitRoot(destination)) fail(MacImportErrorCode::invalid, "The data root must be an explicit absolute folder");
    if (!validMacImportWorkToken(token)) fail(MacImportErrorCode::invalid, "Invalid private import folder token");
    return destination.parent_path() / utf8Path(rootName(destination) + std::string(workInfix) + std::string(token));
}
bool validMacImportWorkToken(std::string_view token) noexcept {
    if (token.empty() || token.size() > 64) return false;
    return std::all_of(token.begin(), token.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-'; });
}
std::string encodeMacImportWorkMarker(std::string_view root, std::string_view token, double createdAt) {
    if (!plainName(root) || !validMacImportWorkToken(token) || !std::isfinite(createdAt)) fail(MacImportErrorCode::invalid, "Invalid private import folder marker");
    return Json(Json::Object{{"format", std::string(markerFormat)}, {"version", noteVersion}, {"root", std::string(root)},
                             {"token", std::string(token)}, {"createdAt", createdAt}}).encode(macImportWorkNoteLimit);
}
std::optional<std::string> decodeMacImportWorkMarker(std::string_view bytes, std::string_view root) noexcept {
    try {
        const auto d = Json::parse(bytes, macImportWorkNoteLimit);
        if (!d.isObject() || d.object().size() != 5 || !d["format"].isString() || d["format"].string() != markerFormat || !d["version"].isNumber() ||
            d["version"].integer() != noteVersion || !d["root"].isString() || d["root"].string() != root || !d["token"].isString() ||
            !validMacImportWorkToken(d["token"].string()) || !d["createdAt"].isNumber() || !std::isfinite(d["createdAt"].number()))
            return std::nullopt;
        return d["token"].string();
    } catch (...) { return std::nullopt; }
}
std::string encodeMacImportActivation(std::string_view root, std::string_view backup) {
    if (!plainName(root) || (!backup.empty() && (!plainName(backup) || !backup.starts_with(std::string(root) + std::string(backupInfix)))))
        fail(MacImportErrorCode::invalid, "Invalid activation journal");
    return Json(Json::Object{{"format", std::string(journalFormat)}, {"version", noteVersion}, {"root", std::string(root)},
                             {"backup", backup.empty() ? Json(nullptr) : Json(std::string(backup))}}).encode(macImportWorkNoteLimit);
}
std::optional<std::string> decodeMacImportActivation(std::string_view bytes, std::string_view root) noexcept {
    try {
        const auto d = Json::parse(bytes, macImportWorkNoteLimit);
        if (!d.isObject() || d.object().size() != 4 || !d["format"].isString() || d["format"].string() != journalFormat || !d["version"].isNumber() ||
            d["version"].integer() != noteVersion || !d["root"].isString() || d["root"].string() != root || !d.contains("backup"))
            return std::nullopt;
        if (d["backup"].isNull()) return std::string{};
        if (!d["backup"].isString()) return std::nullopt;
        auto backup = d["backup"].string();
        if (!plainName(backup) || !backup.starts_with(std::string(root) + std::string(backupInfix))) return std::nullopt;
        return backup;
    } catch (...) { return std::nullopt; }
}
std::string_view macImportWorkStateName(MacImportWorkState state) noexcept {
    switch (state) {
    case MacImportWorkState::abandoned: return "abandoned";
    case MacImportWorkState::activated: return "activated";
    case MacImportWorkState::interrupted: return "interrupted";
    case MacImportWorkState::live: return "live";
    case MacImportWorkState::unrecognized: return "unrecognized";
    }
    return "unrecognized";
}

// --- MacImportWorkLock ----------------------------------------------------------
struct MacImportWorkLock::Impl {
    fs::path folder, root;
    MarkerHandle marker;
};
MacImportWorkLock::MacImportWorkLock() noexcept = default;
MacImportWorkLock::~MacImportWorkLock() = default;
MacImportWorkLock::MacImportWorkLock(MacImportWorkLock&&) noexcept = default;
MacImportWorkLock& MacImportWorkLock::operator=(MacImportWorkLock&&) noexcept = default;
MacImportWorkLock MacImportWorkLock::create(const fs::path& destination, std::string_view token, double createdAt) {
    const auto folder = macImportWorkPath(destination, token);
    const auto bytes = encodeMacImportWorkMarker(rootName(destination), token, createdAt);
    files::requireOrdinaryDirectory(folder.parent_path());
    std::error_code error;
    if (!fs::create_directory(folder, error) || error) fail(MacImportErrorCode::unavailable, "Cannot create the private import folder next to " + show(destination));
    MacImportWorkLock lock;
    lock.impl_ = std::make_unique<Impl>();
    lock.impl_->folder = folder;
    lock.impl_->root = destination;
    try {
        lock.impl_->marker = createMarker(folder / utf8Path(macImportWorkMarker), bytes);
    } catch (...) {
        lock.impl_->marker.close();
        fs::remove(folder / utf8Path(macImportWorkMarker), error);
        fs::remove(folder, error);
        throw;
    }
    return lock;
}
const fs::path& MacImportWorkLock::folder() const noexcept {
    static const fs::path none;
    return impl_ ? impl_->folder : none;
}
bool MacImportWorkLock::held() const noexcept { return impl_ && impl_->marker.open(); }
void MacImportWorkLock::journalActivation(const fs::path& backup) {
    if (!held()) fail(MacImportErrorCode::invalid, "The private import folder is not held");
    const auto name = backup.empty() ? std::string{} : show(backup.filename());
    if (!backup.empty() && backup.parent_path() != impl_->folder.parent_path()) fail(MacImportErrorCode::invalid, "The backup must sit next to the data root");
    files::writeNewFile(impl_->folder / utf8Path(macImportActivationJournal), encodeMacImportActivation(rootName(impl_->root), name));
}
bool MacImportWorkLock::remove() noexcept {
    if (!impl_ || impl_->folder.empty()) return true;
    auto& i = *impl_;
    Eraser eraser(i.folder);
    std::size_t budget = static_cast<std::size_t>(-1);
    std::uint64_t entries{};
    eraser.advance(budget, entries);
    if (eraser.failed() || !eraser.done()) { i.marker.close(); return false; } // the marker stays for the next sweep
    const bool marked = !i.marker.open() || i.marker.erase();
    std::error_code error;
    const bool removed = marked && fs::remove(i.folder, error) && !error;
    if (removed) i.folder.clear();
    return removed;
}

// --- Inspection, sweep and recovery ----------------------------------------------
std::vector<MacImportWorkFolder> inspectMacImportWork(const fs::path& destination) noexcept {
    std::vector<MacImportWorkFolder> out;
    if (!explicitRoot(destination)) return out;
    try {
        for (const auto& folder : candidates(destination)) {
            MarkerHandle claim;
            auto c = classify(destination, folder, claim);
            out.push_back({folder, std::move(c.backup), c.state});
        }
    } catch (...) {}
    return out;
}

struct MacImportWorkSweep::Impl {
    fs::path destination;
    std::vector<fs::path> queue;
    std::size_t next{};
    bool listed{}, finished{};
    // The folder being removed and its exclusively held marker.
    std::optional<Eraser> eraser;
    fs::path current;
    MarkerHandle claim;
    Stats stats;
    void finishFolder() noexcept {
        if (eraser->failed()) {
            // Something could not be removed (in use): the marker stays for the
            // next run's sweep.
            claim.close();
            ++stats.failed;
        } else {
            std::error_code error;
            if (claim.erase() && fs::remove(current, error) && !error) { ++stats.removed; stats.entries += 2; }
            else ++stats.failed;
        }
        claim.close();
        eraser.reset();
        current.clear();
    }
};
MacImportWorkSweep::MacImportWorkSweep(fs::path destination) : impl_(std::make_unique<Impl>()) { impl_->destination = std::move(destination); }
MacImportWorkSweep::~MacImportWorkSweep() = default;
bool MacImportWorkSweep::finished() const noexcept { return impl_->finished; }
const MacImportWorkSweep::Stats& MacImportWorkSweep::stats() const noexcept { return impl_->stats; }
void MacImportWorkSweep::run() noexcept { while (next(static_cast<std::size_t>(-1))) {} }
bool MacImportWorkSweep::next(std::size_t budget) noexcept {
    auto& i = *impl_;
    if (i.finished) return false;
    ++i.stats.steps;
    budget = std::max<std::size_t>(budget, 1);
    if (!i.listed) {
        i.listed = true;
        if (explicitRoot(i.destination)) i.queue = candidates(i.destination);
        i.stats.candidates = i.queue.size();
        --budget;
    }
    while (budget) {
        if (i.eraser) {
            i.eraser->advance(budget, i.stats.entries);
            if (!i.eraser->done() || !budget) break;
            --budget; // the marker and the folder itself
            i.finishFolder();
            continue;
        }
        if (i.next >= i.queue.size()) { i.finished = true; break; }
        const auto folder = i.queue[i.next++];
        --budget;
        const auto c = classify(i.destination, folder, i.claim);
        switch (c.state) {
        case MacImportWorkState::abandoned:
        case MacImportWorkState::activated:
            i.current = folder;
            i.eraser.emplace(folder);
            break;
        case MacImportWorkState::interrupted: ++i.stats.interrupted; break;
        case MacImportWorkState::live: ++i.stats.live; break;
        case MacImportWorkState::unrecognized:
            if (c.empty) {
                // Named like a work folder and empty: nothing to lose.
                std::error_code error;
                if (fs::remove(folder, error) && !error) { ++i.stats.removed; ++i.stats.entries; }
                else ++i.stats.unrecognized;
            } else ++i.stats.unrecognized;
            break;
        }
    }
    if (i.next >= i.queue.size() && !i.eraser) i.finished = true;
    return !i.finished;
}

MacImportRecovery recoverMacImportWork(const fs::path& destination, double finishedAt) noexcept {
    MacImportRecovery out;
    if (!explicitRoot(destination)) return out;
    try {
        for (const auto& folder : candidates(destination)) {
            MarkerHandle claim;
            const auto c = classify(destination, folder, claim);
            if (c.state != MacImportWorkState::interrupted) continue;
            if (kindOf(destination) != files::EntryKind::missing) { ++out.kept; continue; }
            // Hold the marker while restoring: no other process may sweep or
            // recover this folder meanwhile.
            std::string bytes;
            if (claimMarker(folder / utf8Path(macImportWorkMarker), claim, bytes) != Claim::claimed) { ++out.kept; continue; }
            try { files::renameNoReplace(c.backup, destination); }
            catch (...) { ++out.kept; continue; }
            ++out.restored;
            out.backups.push_back(c.backup);
            try {
                writeMacImportOutcome(destination, failedMacImportOutcome(MacImportOutcome::State::failed, std::nullopt,
                    "EndfieldHUD stopped while switching to the imported data. The previous data was restored; nothing was imported.", nullptr, finishedAt));
            } catch (...) {}
        }
    } catch (...) {}
    return out;
}
}
