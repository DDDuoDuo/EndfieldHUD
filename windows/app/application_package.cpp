#include "app/application_package.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace endfield::app {
namespace {
using M = core::Module;
constexpr std::array<std::string_view, 11> ids{
    "shell.runtimeInput", "shell.catalog", "shell.shader", "shell.watchBlur", "shell.cursor",
    "common.notesFormat", "common.applicationIcons",
    "common.watchAppearance", "common.mapPlayer",
    "map.geography", "orbipom"};
constexpr std::array<M, 19> provided{
    M::notes, M::fileShelf, M::clipboard, M::volume, M::workMode, M::eventLog, M::map, M::projection, M::reader,
    M::archive, M::calendar, M::minigame, M::system, M::display, M::hotkeys, M::about, M::storage, M::activityMonitor, M::power};
}

std::span<const std::string_view> packagedResourceIDs() noexcept { return ids; }
bool packagedResourceRequired(std::string_view id) noexcept { return id != "map.geography" && id != "orbipom"; }
std::span<const core::Module> packagedModules() noexcept { return provided; }

PackagedResources resolvePackagedResources(core::ResourceLocator& locator) {
    PackagedResources r;
    std::set<std::string, std::less<>> unavailable;
    // A resource is usable when every listed file verifies and the files its
    // owner opens by name are part of it.
    const auto use = [&](std::string_view id, std::initializer_list<std::string_view> entries = {}) -> std::filesystem::path {
        const auto& resolution = locator.resolve(id);
        bool usable = resolution.status == core::ResourceLocator::Status::verified;
        for (const auto entry : entries) usable = usable && locator.fileSHA256(id, entry).has_value();
        if (usable) return resolution.path;
        unavailable.emplace(id);
        return {};
    };
    r.packet = use("shell.runtimeInput", {"runtime-input.json"});
    r.cache = use("shell.catalog", {""});
    r.shader = use("shell.shader", {""});
    r.watchBlur = use("shell.watchBlur", {""});
    r.cursor = use("shell.cursor", {""});
    const auto notesFormat = use("common.notesFormat");
    const auto icons = use("common.applicationIcons", {"manifest.json"});
    const auto appearance = use("common.watchAppearance", {"manifest.json"});
    const auto mapPlayer = use("common.mapPlayer");
    const auto geography = use("map.geography");
    const auto game = use("orbipom");

    r.shell = !r.packet.empty() && !r.cache.empty() && !r.shader.empty() && !r.watchBlur.empty();
    if (r.shell) r.cachePin = locator.fileSHA256("shell.catalog", "").value_or("");
    // The tray stays available without Notes: it carries Quit.
    r.residentIcons = icons;
    r.modules = r.shell && !notesFormat.empty();
    std::set<M> disabled;
    if (!r.modules) {
        disabled.insert(provided.begin(), provided.end());
    } else {
        r.notesFormatAssets = notesFormat;
        // Settings resolves its folders below the shared common/ root,
        // exactly like the development layout.
        const bool settings = !icons.empty() && !appearance.empty();
        if (settings) r.settingsAssets = icons.parent_path();
        else disabled.insert({M::system, M::display, M::hotkeys, M::about});
        if (!geography.empty() && !mapPlayer.empty()) { r.mapGeography = geography; r.mapPlayerAssets = mapPlayer; }
        else disabled.insert(M::map);
        // The Minigame runtime root contains OrbiPom/ (staged as orbipom/OrbiPom)
        // and keeps its best score in the Settings record.
        if (!game.empty() && settings) r.orbipomAssets = game.parent_path();
        else disabled.insert(M::minigame);
    }
    r.unavailable.assign(unavailable.begin(), unavailable.end());
    for (const auto module : provided) if (disabled.contains(module)) r.disabled.push_back(module);
    return r;
}
} // namespace endfield::app
