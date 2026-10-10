// Acceptance tool for the offline macOS import. Dry-run by default: it stages
// and validates the whole export next to DESTINATION, prints the report as
// JSON and removes its private folder without touching DESTINATION.
//
//   mac_import_tool EXPORT_DIR DESTINATION [--commit] [--replace] [--accept store,...]
//   mac_import_tool --fixture GOLDEN.json SCRATCH_DIR [--commit]   (synthetic fixture only)
//
// Use a scratch DESTINATION for acceptance; never point it at real app data
// unless you intend to import. --commit keeps a timestamped backup of an
// existing DESTINATION and never deletes it.
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_files.hpp"
#include "core/migration/mac_import_native.hpp"
#include "core/migration/plist.hpp"
#include <fstream>
#include <iostream>
#include <set>

namespace m = ehud::migration;
using J = ehud::data::Json;
namespace fs = std::filesystem;

namespace {
fs::path utf8Path(std::string_view text) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())); }
std::string show(const fs::path& path) { const auto t = path.u8string(); return std::string(reinterpret_cast<const char*>(t.data()), t.size()); }
J report(const m::MacImportSummary& s) {
    J::Array stores;
    for (const auto& r : s.stores) {
        J::Array warnings;
        for (const auto& w : r.warnings) warnings.push_back(w);
        stores.push_back(J::Object{{"store", std::string(m::macImportStoreName(r.store))}, {"status", std::string(m::macImportStatusName(r.status))},
            {"detail", r.detail}, {"records", static_cast<std::int64_t>(r.records)}, {"relinkItems", static_cast<std::int64_t>(r.relinkItems)},
            {"replacesWindowsData", r.replacesWindowsData}, {"warnings", std::move(warnings)}});
    }
    J::Array relink;
    for (const auto& item : s.relink) relink.push_back(J::Object{{"store", item.store}, {"recordID", item.recordID}, {"displayName", item.displayName}, {"lastKnownPath", item.lastKnownPath}});
    J::Array settings;
    for (const auto& row : s.settings) settings.push_back(J::Object{{"key", row.key}, {"outcome", std::string(m::macSettingOutcomeName(row.outcome))}, {"detail", row.detail}});
    J::Array ignored, warnings;
    for (const auto& path : s.ignoredFiles) ignored.push_back(path);
    for (const auto& warning : s.warnings) warnings.push_back(warning);
    return J::Object{{"stores", std::move(stores)}, {"relink", std::move(relink)}, {"settings", std::move(settings)}, {"ignoredFiles", std::move(ignored)},
        {"warnings", std::move(warnings)},
        {"profileSyncLocked", s.profileSyncLocked}, {"launchAtLogin", s.launchAtLogin}, {"customShortcutUntranslated", s.customShortcutUntranslated},
        {"remindersNeedReconcile", s.remindersNeedReconcile}, {"destinationExisted", s.destinationExisted},
        {"backup", s.backup.empty() ? J(nullptr) : J(show(s.backup))}, {"sourceVersion", s.sourceVersion}, {"sourceBuild", s.sourceBuild}};
}
fs::path extractFixture(const fs::path& fixture, const fs::path& scratch) {
    const auto document = J::parse(m::files::readFile(fixture, 64 * 1024 * 1024), 64 * 1024 * 1024);
    const auto root = scratch / "export";
    for (const auto& [path, encoded] : document["files"].object()) {
        const auto bytes = m::base64Decode(encoded.string());
        if (!bytes || !(m::validExportPath(path) || path == "manifest.json")) throw std::runtime_error("Invalid fixture entry " + path);
        m::files::writeNewFile(root / utf8Path(path), *bytes);
    }
    return root;
}
}
int main(int argc, char** argv) {
    try {
        std::vector<std::string> args(argv + 1, argv + argc);
        bool commit = false, replace = false, fixture = false;
        std::vector<m::MacImportStore> accept;
        std::vector<std::string> positional;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--commit") commit = true;
            else if (args[i] == "--replace") replace = true;
            else if (args[i] == "--fixture") fixture = true;
            else if (args[i] == "--accept" && i + 1 < args.size()) {
                std::string list = args[++i];
                for (std::size_t start = 0; start <= list.size();) {
                    auto end = list.find(',', start); if (end == std::string::npos) end = list.size();
                    const auto name = list.substr(start, end - start);
                    bool found = false;
                    for (const auto store : m::macImportStores) if (m::macImportStoreName(store) == name) { accept.push_back(store); found = true; }
                    if (!found) throw std::runtime_error("Unknown store " + name);
                    start = end + 1;
                }
            } else positional.push_back(args[i]);
        }
        if (positional.size() != 2) {
            std::cerr << "Usage: mac_import_tool EXPORT_DIR DESTINATION [--commit] [--replace] [--accept store,...]\n"
                         "       mac_import_tool --fixture GOLDEN.json SCRATCH_DIR [--commit]\n";
            return 2;
        }
        fs::path exportRoot = fs::absolute(utf8Path(positional[0])), destination = fs::absolute(utf8Path(positional[1])).lexically_normal();
        if (fixture) {
            const auto scratch = destination;
            fs::create_directories(scratch);
            if (!fs::is_empty(scratch)) throw std::runtime_error("The fixture scratch folder must be new or empty");
            exportRoot = extractFixture(exportRoot, scratch);
            destination = scratch / "EndfieldHUD";
        }
        m::MacImportOptions options;
        options.replaceExistingImport = replace;
        options.acceptRejected = accept;
        m::MacImportSession session(exportRoot, destination, m::nativeMacImportPlatform(), options);
        std::size_t stagingSteps{}, commitSteps{};
        while (session.stageNext()) ++stagingSteps;
        ++stagingSteps;
        const auto staged = session.progress();
        auto out = report(session.summary());
        out["committable"] = session.committable();
        out["verifiedBytes"] = static_cast<std::int64_t>(staged.verifiedBytes);
        out["stagingSteps"] = static_cast<std::int64_t>(stagingSteps);
        if (commit) {
            if (!session.committable()) { out["committed"] = false; std::cout << out.encode(64 * 1024 * 1024) << '\n'; return 3; }
            while (session.commitNext()) ++commitSteps;
            ++commitSteps;
            const auto& result = *session.commitResult();
            out = report(session.summary()); // refreshed at commit from the current root
            out["committable"] = true;
            out["verifiedBytes"] = static_cast<std::int64_t>(staged.verifiedBytes);
            out["stagingSteps"] = static_cast<std::int64_t>(stagingSteps);
            out["commitSteps"] = static_cast<std::int64_t>(commitSteps);
            out["carriedBytes"] = static_cast<std::int64_t>(session.progress().carriedBytes);
            out["committed"] = true;
            out["workRemoved"] = result.workRemoved;
        } else {
            session.discard();
            out["committed"] = false;
        }
        std::cout << out.encode(64 * 1024 * 1024) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mac_import_tool: " << error.what() << '\n';
        return 1;
    }
}
