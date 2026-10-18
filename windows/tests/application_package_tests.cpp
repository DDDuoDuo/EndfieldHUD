// EndfieldHUD.exe package -> module policy over an owned temporary package:
// a missing or damaged resource disables exactly the modules that read it
// (WINDOWS-MIGRATION.md section 10), the source shell is required, Notes gates
// the module set and the tray keeps Quit reachable.
#include "app/application_package.hpp"
#include "core/data/json.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace app = endfield::app;
namespace core = endfield::core;
using ehud::data::Json;
using M = core::Module;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
struct Temporary {
    fs::path root;
    Temporary() {
        std::random_device random;
        root = fs::weakly_canonical(fs::temp_directory_path()) /
               ("EndfieldHUD package contracts " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
        check(fs::create_directory(root), "New owned temporary package");
    }
    ~Temporary() { std::error_code ignored; fs::remove_all(root, ignored); }
};
fs::path utf8(std::string_view value) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size())); }
void write(const fs::path& path, std::string_view bytes) { fs::create_directories(path.parent_path()); std::ofstream out(path, std::ios::binary); out << bytes; }
std::string sha(std::string_view bytes) { return core::packet::sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())); }

// One synthetic package with the production layout (cmake/modules/app.cmake).
struct Package {
    fs::path root;
    Json manifest = Json::Object{{"format", "EndfieldHUD.Windows.Resources"}, {"schema", 1},
                                 {"sourceCommit", "ca04f142185c7de40acd8523bdb563195d90a1d1"}, {"resources", Json::Object{}}};
    void file(const std::string& id, const std::string& path, const std::string& bytes) {
        write(root / utf8(path), bytes);
        manifest["resources"][id] = Json::Object{{"path", path}, {"kind", "file"},
            {"files", Json::Array{Json::Object{{"path", ""}, {"bytes", std::int64_t(bytes.size())}, {"sha256", sha(bytes)}}}}};
    }
    void directory(const std::string& id, const std::string& path, std::vector<std::pair<std::string, std::string>> files) {
        Json::Array listed;
        for (const auto& [name, bytes] : files) {
            write(root / utf8(path) / utf8(name), bytes);
            listed.push_back(Json::Object{{"path", name}, {"bytes", std::int64_t(bytes.size())}, {"sha256", sha(bytes)}});
        }
        manifest["resources"][id] = Json::Object{{"path", path}, {"kind", "directory"}, {"files", std::move(listed)}};
    }
    void save() const { write(root / "resources.json", manifest.encode()); }
    core::ResourceLocator open() const { save(); return core::ResourceLocator::open(root); }
};
Package production(const fs::path& root) {
    Package p{root};
    p.directory("shell.runtimeInput", "shell/runtime-input", {{"runtime-input.json", "{}"}, {"frames/top.bin", "top"}});
    p.file("shell.catalog", "shell/catalog.ehscene", "EHSCENE catalog");
    p.file("shell.shader", "shell/hud.hlsl", "float4 main() : SV_Target { return 1; }\n");
    p.file("shell.watchBlur", "shell/watch-blur.json", "{\"entrance\":[]}");
    p.file("shell.cursor", "shell/cursor.png", "PNG cursor");
    p.directory("notes.controls", "notes-controls", {{"notes-controls-assets.json", "{\"controls\":[]}"}, {"raster/a.png", "a"}});
    p.directory("common.notesFormat", "common/notes-format", {{"raster/wheel.png", "wheel"}});
    p.directory("common.shelf", "common/shelf", {{"reveal-mask.bin", std::string(64, '\x01')}, {"icons.json", "{}"}});
    p.directory("common.clipboard", "common/clipboard", {{"artwork.json", "{}"}});
    p.directory("common.applicationIcons", "common/application-icons", {{"manifest.json", "{}"}, {"endfield-app.png", "icon"}});
    p.directory("common.watchAppearance", "common/watch-appearance", {{"manifest.json", "{}"}});
    p.directory("common.archive", "common/archive", {{"raster/archive.png", "archive"}});
    p.directory("common.storage", "common/storage", {{"source-paths.json", "{}"}});
    p.directory("common.activity", "common/activity", {{"sort-mask.bin", "mask"}});
    p.directory("common.mapPlayer", "common/map-player", {{"player.png", "player"}});
    p.directory("map.geography", "map", {{"Terrain.bin", std::string(128, '\x7f')}, {"Countries.bin", "countries"}});
    p.directory("orbipom", "orbipom/OrbiPom", {{"orbipom.js", "game"}});
    return p;
}
bool has(const std::vector<M>& list, M module) { return std::find(list.begin(), list.end(), module) != list.end(); }

void complete(const fs::path& root) {
    auto package = production(root);
    auto locator = package.open();
    check(locator.unlistedFiles().empty(), "A staged package lists every file it contains");
    const auto r = app::resolvePackagedResources(locator);
    check(r.shell && r.modules && r.unavailable.empty() && r.disabled.empty(), "A complete package enables the shell and every module");
    check(r.packet == root / "shell" / "runtime-input" && r.cache == root / "shell" / "catalog.ehscene" && r.shader == root / "shell" / "hud.hlsl" &&
          r.watchBlur == root / "shell" / "watch-blur.json" && r.cursor == root / "shell" / "cursor.png", "Shell resources resolve below the package");
    check(r.cachePin == sha("EHSCENE catalog") && r.notesAssetsSHA == sha("{\"controls\":[]}"), "Pins come from the verified manifest");
    check(r.notesAssets == root / "notes-controls" && r.notesFormatAssets == root / "common" / "notes-format", "Notes resources");
    check(r.shelfAssets == root / "common" / "shelf" && r.shelfMask == root / "common" / "shelf" / "reveal-mask.bin", "Shelf assets and original reveal samples");
    check(r.settingsAssets == root / "common" && r.archiveAssets == root / "common" && r.storageAssets == root / "common",
          "Settings, Archive and Storage read their folders below the shared common root");
    check(r.clipboardAssets == root / "common" / "clipboard" && r.activityAssets == root / "common" / "activity" &&
          r.residentIcons == root / "common" / "application-icons", "Per-module folders");
    check(r.mapGeography == root / "map" && r.mapPlayerAssets == root / "common" / "map-player" && r.orbipomAssets == root / "orbipom", "Map and Minigame roots");
    const auto verified = locator.verifications();
    (void)app::resolvePackagedResources(locator);
    check(locator.verifications() == verified && verified == app::packagedResourceIDs().size(), "Each resource is hashed exactly once");
    write(root / "common" / "shelf" / "stray.bin", "unlisted");
    write(root / "leftover" / "old.png", "unlisted");
    const auto stray = locator.unlistedFiles();
    check(stray == std::vector<std::string>{"common/shelf/stray.bin", "leftover/old.png"}, "Unlisted files are reported by relative path");
}

// Damage one resource at a time: only its modules are disabled.
void isolation(const fs::path& base) {
    struct Case { const char* id; const char* damage; std::vector<M> disabled; bool modules{true}, shell{true}, tray{true}; };
    const std::vector<Case> cases{
        {"common.shelf", "common/shelf/icons.json", {M::fileShelf}},
        {"common.clipboard", "common/clipboard/artwork.json", {M::clipboard}},
        {"common.watchAppearance", "common/watch-appearance/manifest.json", {M::minigame, M::system, M::display, M::hotkeys, M::about}},
        {"common.applicationIcons", "common/application-icons/endfield-app.png", {M::minigame, M::system, M::display, M::hotkeys, M::about}, true, true, false},
        {"common.archive", "common/archive/raster/archive.png", {M::archive}},
        {"common.storage", "common/storage/source-paths.json", {M::storage}},
        {"common.activity", "common/activity/sort-mask.bin", {M::activityMonitor}},
        {"common.mapPlayer", "common/map-player/player.png", {M::map}},
        {"map.geography", "map/Terrain.bin", {M::map}},
        {"orbipom", "orbipom/OrbiPom/orbipom.js", {M::minigame}},
        {"shell.cursor", "shell/cursor.png", {}},
        {"notes.controls", "notes-controls/raster/a.png", {}, false},
        {"common.notesFormat", "common/notes-format/raster/wheel.png", {}, false},
        {"shell.catalog", "shell/catalog.ehscene", {}, false, false},
        {"shell.runtimeInput", "shell/runtime-input/frames/top.bin", {}, false, false},
    };
    unsigned index{};
    for (const auto& c : cases) {
        const auto root = base / ("damage-" + std::to_string(index++));
        auto package = production(root);
        package.save();
        write(root / utf8(c.damage), "damaged bytes of another size");
        auto locator = core::ResourceLocator::open(root);
        const auto r = app::resolvePackagedResources(locator);
        const std::string what = std::string(" (") + c.id + ")";
        check(r.unavailable == std::vector<std::string>{c.id}, "Only the damaged resource is unavailable" + what);
        check(r.shell == c.shell && r.modules == c.modules, "Shell/module gate" + what);
        check(r.residentIcons.empty() != c.tray, "Tray icon follows only its own resource" + what);
        if (!c.modules) {
            check(r.disabled.size() == app::packagedModules().size() && r.notesAssets.empty() && r.shelfAssets.empty() && r.settingsAssets.empty() &&
                  r.mapGeography.empty() && r.orbipomAssets.empty(), "Without Notes no module starts" + what);
            check(!c.shell || !r.residentIcons.empty(), "Without Notes the tray still carries Quit" + what);
            continue;
        }
        auto expected = c.disabled;
        std::vector<M> ordered;
        for (const auto m : app::packagedModules()) if (has(expected, m)) ordered.push_back(m);
        check(r.disabled == ordered, "Exactly the modules that read the resource are disabled" + what);
        check(r.notesAssets == root / "notes-controls" && !r.notesAssetsSHA.empty(), "Notes stays available" + what);
        check(r.shelfAssets.empty() == has(c.disabled, M::fileShelf) && r.shelfMask.empty() == has(c.disabled, M::fileShelf), "Shelf" + what);
        check(r.clipboardAssets.empty() == has(c.disabled, M::clipboard), "Clipboard" + what);
        check(r.settingsAssets.empty() == has(c.disabled, M::system), "Settings" + what);
        check(r.archiveAssets.empty() == has(c.disabled, M::archive), "Archive" + what);
        check(r.storageAssets.empty() == has(c.disabled, M::storage), "Storage" + what);
        check(r.activityAssets.empty() == has(c.disabled, M::activityMonitor), "Activity" + what);
        check(r.mapGeography.empty() == has(c.disabled, M::map) && r.mapPlayerAssets.empty() == has(c.disabled, M::map), "Map needs geography and player art together" + what);
        check(r.orbipomAssets.empty() == has(c.disabled, M::minigame), "Minigame" + what);
        check(r.cursor.empty() == (std::string_view(c.id) == "shell.cursor"), "Cursor is optional" + what);
    }
}

void policy(const fs::path& base) {
    // A resource whose owner-opened file is not part of it is unusable.
    {
        const auto root = base / "no-mask";
        auto package = production(root);
        package.directory("common.shelf", "common/shelf", {{"icons.json", "{}"}});
        auto locator = package.open();
        const auto r = app::resolvePackagedResources(locator);
        check(r.unavailable == std::vector<std::string>{"common.shelf"} && r.disabled == std::vector<M>{M::fileShelf}, "Shelf without its reveal samples is disabled");
    }
    // Optional external inputs absent from the package (no Map/Minigame build inputs).
    {
        const auto root = base / "optional";
        auto package = production(root);
        package.manifest["resources"].erase("map.geography");
        package.manifest["resources"].erase("orbipom");
        auto locator = package.open();
        const auto r = app::resolvePackagedResources(locator);
        check(r.modules && r.disabled == std::vector<M>{M::map, M::minigame}, "Absent optional inputs disable only Map and Minigame");
    }
    check(!app::packagedResourceRequired("map.geography") && !app::packagedResourceRequired("orbipom") && app::packagedResourceRequired("common.shelf") &&
          app::packagedResourceRequired("shell.runtimeInput"), "Only external Map/Minigame inputs are optional");
    const auto ids = app::packagedResourceIDs();
    check(std::adjacent_find(ids.begin(), ids.end()) == ids.end() && ids.size() == 17, "Every packaged resource id is listed once");
    check(app::packagedModules().size() == 19 && !std::count(app::packagedModules().begin(), app::packagedModules().end(), M::nowPlaying),
          "Provided modules exclude areas that are not wired yet");
}
} // namespace

int main() {
    try {
        Temporary temp;
        complete(temp.root / "complete");
        isolation(temp.root);
        policy(temp.root);
        std::cout << "EndfieldHUD package policy: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EndfieldHUD package policy failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
