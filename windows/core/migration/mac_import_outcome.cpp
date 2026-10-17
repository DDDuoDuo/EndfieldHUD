#include "core/migration/mac_import_outcome.hpp"
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_files.hpp"
#include <algorithm>
#include <cmath>

namespace ehud::migration {
namespace {
using data::Json;
namespace fs = std::filesystem;
constexpr std::string_view outcomeFormat = "EndfieldHUD.macImportOutcome";
constexpr std::int64_t outcomeVersion = 1;
[[noreturn]] void invalid(const std::string& why) { throw MacImportError(MacImportErrorCode::invalid, "Import outcome note: " + why); }
std::string show(const fs::path& path) { const auto text = path.u8string(); return std::string(reinterpret_cast<const char*>(text.data()), text.size()); }
fs::path utf8Path(std::string_view text) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())); }

template<class E, std::size_t N> std::optional<E> named(std::string_view name, const E (&values)[N], std::string_view (*label)(E) noexcept) {
    for (const auto value : values) if (label(value) == name) return value;
    return std::nullopt;
}
constexpr MacImportStatus statuses[]{MacImportStatus::pending, MacImportStatus::imported, MacImportStatus::importedWithWarnings,
    MacImportStatus::notInExport, MacImportStatus::rejectedNewer, MacImportStatus::rejectedInvalid};
constexpr MacImportErrorCode codes[]{MacImportErrorCode::invalid, MacImportErrorCode::newerVersion, MacImportErrorCode::tooLarge,
    MacImportErrorCode::unavailable, MacImportErrorCode::conflict, MacImportErrorCode::cancelled, MacImportErrorCode::corrupt, MacImportErrorCode::alreadyImported};
constexpr MacImportOutcome::State states[]{MacImportOutcome::State::completed, MacImportOutcome::State::failed, MacImportOutcome::State::cancelled};
MacImportStore storeNamed(std::string_view name) {
    for (const auto store : macImportStores) if (macImportStoreName(store) == name) return store;
    invalid("unknown store");
}
std::string text(const Json& value, std::string_view key, std::size_t limit) {
    const auto& field = value[key];
    if (!field.isString()) invalid(std::string("missing ") + std::string(key));
    auto out = field.string();
    if (out.size() > limit) invalid(std::string("oversized ") + std::string(key));
    return out;
}
bool flag(const Json& value, std::string_view key) {
    const auto& field = value[key];
    if (!field.isBool()) invalid(std::string("missing ") + std::string(key));
    return field.boolean();
}
std::size_t count(const Json& value, std::string_view key) {
    const auto& field = value[key];
    if (!field.isNumber()) invalid(std::string("missing ") + std::string(key));
    const auto n = field.integer();
    if (n < 0 || n > 100000000) invalid(std::string("invalid ") + std::string(key));
    return static_cast<std::size_t>(n);
}
MacImportOutcome fromSummary(const MacImportSummary& s) {
    MacImportOutcome out;
    out.sourceVersion = s.sourceVersion;
    out.sourceBuild = s.sourceBuild;
    for (const auto& r : s.stores) out.stores.push_back({r.store, r.status, r.records, r.relinkItems});
    out.relinkItems = s.relink.size();
    out.warnings = s.warnings;
    out.profileSyncLocked = s.profileSyncLocked;
    out.launchAtLogin = s.launchAtLogin;
    out.customShortcutUntranslated = s.customShortcutUntranslated;
    out.remindersNeedReconcile = s.remindersNeedReconcile;
    out.replacesPreviousImport = s.replacesPreviousImport;
    return out;
}
}

std::string_view macImportOutcomeStateName(MacImportOutcome::State state) noexcept {
    switch (state) {
    case MacImportOutcome::State::completed: return "completed";
    case MacImportOutcome::State::failed: return "failed";
    case MacImportOutcome::State::cancelled: return "cancelled";
    }
    return "failed";
}
std::string_view macImportErrorCodeName(MacImportErrorCode code) noexcept {
    switch (code) {
    case MacImportErrorCode::invalid: return "invalid";
    case MacImportErrorCode::newerVersion: return "newerVersion";
    case MacImportErrorCode::tooLarge: return "tooLarge";
    case MacImportErrorCode::unavailable: return "unavailable";
    case MacImportErrorCode::conflict: return "conflict";
    case MacImportErrorCode::cancelled: return "cancelled";
    case MacImportErrorCode::corrupt: return "corrupt";
    case MacImportErrorCode::alreadyImported: return "alreadyImported";
    }
    return "invalid";
}

MacImportOutcome completedMacImportOutcome(const MacImportSummary& summary, const MacImportCommitResult& result, double finishedAt) {
    auto out = fromSummary(summary);
    out.state = MacImportOutcome::State::completed;
    out.backup = result.backup.empty() ? std::string{} : show(result.backup);
    out.finishedAt = finishedAt;
    return out;
}
MacImportOutcome failedMacImportOutcome(MacImportOutcome::State state, std::optional<MacImportErrorCode> code, std::string error,
                                        const MacImportSummary* summary, double finishedAt) {
    auto out = summary ? fromSummary(*summary) : MacImportOutcome{};
    out.state = state == MacImportOutcome::State::completed ? MacImportOutcome::State::failed : state;
    out.code = code;
    // The importer's detail is bounded like every other note field.
    if (error.size() > 4096) error.resize(4096);
    if (!Json::validUtf8(error)) error = "The import failed";
    out.error = std::move(error);
    out.finishedAt = finishedAt;
    return out;
}

std::string encodeMacImportOutcome(const MacImportOutcome& o) {
    Json::Array stores, warnings;
    for (const auto& s : o.stores)
        stores.push_back(Json::Object{{"store", std::string(macImportStoreName(s.store))}, {"status", std::string(macImportStatusName(s.status))},
            {"records", static_cast<std::int64_t>(s.records)}, {"relinkItems", static_cast<std::int64_t>(s.relinkItems)}});
    for (const auto& w : o.warnings) warnings.push_back(w);
    Json document = Json::Object{{"format", std::string(outcomeFormat)}, {"version", outcomeVersion},
        {"state", std::string(macImportOutcomeStateName(o.state))}, {"error", o.error},
        {"code", o.code ? Json(std::string(macImportErrorCodeName(*o.code))) : Json(nullptr)},
        {"sourceVersion", o.sourceVersion}, {"sourceBuild", o.sourceBuild}, {"backup", o.backup},
        {"stores", std::move(stores)}, {"relinkItems", static_cast<std::int64_t>(o.relinkItems)}, {"warnings", std::move(warnings)},
        {"profileSyncLocked", o.profileSyncLocked}, {"launchAtLogin", o.launchAtLogin}, {"customShortcutUntranslated", o.customShortcutUntranslated},
        {"remindersNeedReconcile", o.remindersNeedReconcile}, {"replacesPreviousImport", o.replacesPreviousImport},
        {"finishedAt", std::isfinite(o.finishedAt) ? o.finishedAt : 0.0}};
    return document.encode(macImportOutcomeMaximumBytes);
}
MacImportOutcome decodeMacImportOutcome(std::string_view bytes) {
    if (bytes.size() > macImportOutcomeMaximumBytes) throw MacImportError(MacImportErrorCode::tooLarge, "Import outcome note is too large");
    Json d;
    try { d = Json::parse(bytes, macImportOutcomeMaximumBytes); } catch (const std::exception&) { invalid("not JSON"); }
    if (!d.isObject() || !d["format"].isString() || d["format"].string() != outcomeFormat || !d["version"].isNumber()) invalid("unknown format");
    if (d["version"].integer() > outcomeVersion) throw MacImportError(MacImportErrorCode::newerVersion, "Import outcome note is from a newer EndfieldHUD");
    if (d["version"].integer() != outcomeVersion) invalid("unknown version");
    MacImportOutcome o;
    const auto state = named(text(d, "state", 32), states, macImportOutcomeStateName);
    if (!state) invalid("unknown state");
    o.state = *state;
    o.error = text(d, "error", 4096);
    if (!d["code"].isNull()) {
        if (!d["code"].isString()) invalid("invalid code");
        const auto code = named(d["code"].string(), codes, macImportErrorCodeName);
        if (!code) invalid("unknown code");
        o.code = *code;
    }
    o.sourceVersion = text(d, "sourceVersion", 256);
    o.sourceBuild = text(d, "sourceBuild", 256);
    o.backup = text(d, "backup", 32768);
    if (!d["stores"].isArray() || d["stores"].array().size() > macImportStores.size()) invalid("invalid stores");
    for (const auto& row : d["stores"].array()) {
        if (!row.isObject()) invalid("invalid store row");
        MacImportOutcomeStore s;
        s.store = storeNamed(text(row, "store", 64));
        const auto status = named(text(row, "status", 64), statuses, macImportStatusName);
        if (!status) invalid("unknown status");
        s.status = *status;
        s.records = count(row, "records");
        s.relinkItems = count(row, "relinkItems");
        if (std::any_of(o.stores.begin(), o.stores.end(), [&](const auto& seen) { return seen.store == s.store; })) invalid("duplicate store");
        o.stores.push_back(s);
    }
    o.relinkItems = count(d, "relinkItems");
    if (!d["warnings"].isArray() || d["warnings"].array().size() > 1024) invalid("invalid warnings");
    for (const auto& w : d["warnings"].array()) {
        if (!w.isString() || w.string().size() > 4096) invalid("invalid warning");
        o.warnings.push_back(w.string());
    }
    o.profileSyncLocked = flag(d, "profileSyncLocked");
    o.launchAtLogin = flag(d, "launchAtLogin");
    o.customShortcutUntranslated = flag(d, "customShortcutUntranslated");
    o.remindersNeedReconcile = flag(d, "remindersNeedReconcile");
    o.replacesPreviousImport = flag(d, "replacesPreviousImport");
    if (!d["finishedAt"].isNumber() || !std::isfinite(d["finishedAt"].number())) invalid("invalid finishedAt");
    o.finishedAt = d["finishedAt"].number();
    return o;
}

fs::path macImportOutcomePath(const fs::path& destination) {
    if (!destination.is_absolute() || !destination.has_filename() || destination.lexically_normal() != destination)
        throw MacImportError(MacImportErrorCode::invalid, "The data root must be an explicit absolute folder");
    return destination.parent_path() / utf8Path(show(destination.filename()) + ".mac-import-outcome.json");
}
void writeMacImportOutcome(const fs::path& destination, const MacImportOutcome& outcome) {
    const auto path = macImportOutcomePath(destination);
    const auto bytes = encodeMacImportOutcome(outcome);
    files::requireOrdinaryDirectory(path.parent_path());
    // Exclusive temporary file, flushed, then a same-folder rename: a reader
    // sees either the previous note or the complete new one.
    const auto temporary = path.parent_path() / utf8Path("." + show(path.filename()) + "." + data::makeUUID() + ".tmp");
    files::writeNewFile(temporary, bytes);
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary, error);
        throw MacImportError(MacImportErrorCode::unavailable, "The import outcome could not be recorded next to the data folder");
    }
}
std::optional<MacImportOutcome> readMacImportOutcome(const fs::path& destination) {
    const auto path = macImportOutcomePath(destination);
    std::optional<std::string> bytes;
    try { bytes = data::detail::readFile(path, macImportOutcomeMaximumBytes); }
    catch (const data::StoreError& error) {
        if (error.code() == data::StoreErrorCode::tooLarge) throw MacImportError(MacImportErrorCode::tooLarge, "Import outcome note is too large");
        throw MacImportError(MacImportErrorCode::unavailable, "Import outcome note could not be read");
    }
    if (!bytes) return std::nullopt;
    return decodeMacImportOutcome(*bytes);
}
bool removeMacImportOutcome(const fs::path& destination) noexcept {
    try {
        std::error_code error;
        const auto path = macImportOutcomePath(destination);
        if (!files::ordinaryFileSize(path)) return true;
        return fs::remove(path, error) && !error;
    } catch (...) { return false; }
}

MacImportOutcome commitMacImportSession(MacImportSession& session, const fs::path& destination, double finishedAt) noexcept {
    MacImportOutcome outcome;
    try {
        bool recorded = false;
        try {
            // The note is recorded as soon as the imported root is in place,
            // before the private copies are removed: a process that ends
            // during that cleanup still reports the import, and the next run
            // sweeps the remaining folder (mac_import_work.hpp).
            while (session.commitNext())
                if (!recorded && session.committed() && session.commitResult()) {
                    outcome = completedMacImportOutcome(session.summary(), *session.commitResult(), finishedAt);
                    try { writeMacImportOutcome(destination, outcome); recorded = true; } catch (...) {}
                }
            if (!session.commitResult()) throw MacImportError(MacImportErrorCode::invalid, "The import finished without a result");
            outcome = completedMacImportOutcome(session.summary(), *session.commitResult(), finishedAt);
        } catch (const MacImportError& error) {
            const auto state = error.code() == MacImportErrorCode::cancelled ? MacImportOutcome::State::cancelled : MacImportOutcome::State::failed;
            outcome = failedMacImportOutcome(state, error.code(), error.what(), session.staged() ? &session.summary() : nullptr, finishedAt);
        } catch (const std::exception& error) {
            outcome = failedMacImportOutcome(MacImportOutcome::State::failed, std::nullopt, error.what(), session.staged() ? &session.summary() : nullptr, finishedAt);
        }
        // An activated import is complete even when removing the private
        // folder failed afterwards (commit() reports workRemoved=false).
        if (outcome.state != MacImportOutcome::State::completed && session.committed() && session.commitResult())
            outcome = completedMacImportOutcome(session.summary(), *session.commitResult(), finishedAt);
        if (!recorded || outcome.state != MacImportOutcome::State::completed) try { writeMacImportOutcome(destination, outcome); } catch (...) {}
    } catch (...) {
        // Allocation failure while describing the result: report a bare failure.
        try { outcome = MacImportOutcome{}; outcome.finishedAt = finishedAt; } catch (...) {}
    }
    return outcome;
}
}
