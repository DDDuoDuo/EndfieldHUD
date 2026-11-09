// Production storage entry-point selection for the Notes and File Shelf owners
// (app/application_persistence.hpp) over synthetic owner types: development
// keeps the NEW-root fixture constructor, production uses the persistent entry
// point, and an owner without that entry point cannot be selected at all (the
// former runtime refusal is now a compile-time requirement). The real owners
// are exercised on pre-existing roots by application_data_root_tests.
// Also: a Windows 1.3.0 settings.json opened by this build loads with the
// same values and writes nothing (synthetic record in a temporary root).
#include "app/application_persistence.hpp"
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace app = endfield::app;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }

struct Media { int client{}; };
// Mirrors NotesPreview's signature with and without the persistent flag.
struct FixtureOnlyNotes {
    std::string root; bool persistent{};
    FixtureOnlyNotes(std::string r, int, Media) : root(std::move(r)) {}
};
struct PersistentNotes {
    std::string root; bool persistent{};
    PersistentNotes(std::string r, int, Media, bool p = false) : root(std::move(r)), persistent(p) {}
};
struct FixtureShelfOptions { std::string newDataRoot; bool nativeIcons{true}; };
struct PersistentShelfOptions { std::string newDataRoot; bool nativeIcons{true}; bool persistentDataRoot{}; };

// The selection is a compile-time requirement: fixture-only owners are not
// accepted by makeNotesOwner/persistentShelfRoot (static_assert).
static_assert(app::persistentNotesSupported<PersistentNotes, std::string, int, const Media&>);
static_assert(!app::persistentNotesSupported<FixtureOnlyNotes, std::string, int, const Media&>);
static_assert(app::persistentShelfSupported<PersistentShelfOptions> && !app::persistentShelfSupported<FixtureShelfOptions>);

// What Windows 1.3.0 writes: every 1.3.0 key, the Windows keys, the D2
// mirror and the first-run marker, with a retired "battery" icon.
void windowsVersion130Settings() {
    namespace fs = std::filesystem;
    using ehud::data::Json;
    const auto root = fs::canonical(fs::temp_directory_path()) / ("EndfieldHUD-persistence-" + ehud::data::makeUUID());
    check(fs::create_directory(root), "Create a new temporary data root");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); } } cleanup{root};
    const Json record = Json::Object{
        {"displayMode", "never"}, {"displayDuration", 5}, {"accentHex", "6EDFE8"}, {"theme", "light"}, {"scale", 1.25}, {"placement", "custom"},
        {"customScreenID", nullptr}, {"customPositionX", 0.25}, {"customPositionY", 0.8}, {"language", "japanese"}, {"hudScale", 1.1},
        {"hudOffsetX", -0.05}, {"hudOffsetY", 0.1}, {"parallaxIntensity", 0.5}, {"perspectiveIntensity", 1.5}, {"backgroundDarkness", 0.63},
        {"blurAmount", 0.4}, {"reduceMotion", true}, {"ambientAnimation", false}, {"closeOnFocusLost", false}, {"openOnActiveDisplay", false},
        {"hudDisplayUUID", nullptr}, {"hudDisplayName", nullptr}, {"launchAtLogin", false}, {"batteryAlertsEnabled", true},
        {"devicePopupEnabled", false}, {"lowPowerVisualMode", true}, {"applicationIcon", "battery"}, {"clockFormat", "twelveHour"},
        {"clockStyle", "dial"}, {"centerLogo", "endfield"}, {"centerLogoRevision", nullptr}, {"alertMetric", "gpu"},
        {"summonShortcut", Json::Object{{"keyCode", 50}, {"modifiers", 1}}}, {"summonHotkeyEnabled", false}, {"windowsSummonHotkeyDisabled", true},
        {"recordingMode", true}, {"dailyBootLog", false}, {"facilityAlerts", false}, {"dailyBootLogLastDay", "2026-11-01"},
        {"hudModuleOrder", Json::Array{"map", "notes"}}, {"hudHiddenModules", Json::Array{"volume"}}, {"windowsDisplayID", "DISPLAY-B"},
        {"windowsDisplayName", "Studio"}, {"windowsSummonShortcut", Json::Object{{"virtualKey", 0x77}, {"modifiers", 3}}},
        {"storageVolume", "D:"}, {"orbipom.bestScore.v1", 71}, {"projection.settings.v1", Json::Object{{"width", 24}, {"erasing", false}, {"background", true}}}};
    const auto bytes = Json(Json::Object{{"version", 1}, {"settings", record}, {"hasLaunched", true}}).encode();
    { std::ofstream file(root / "settings.json", std::ios::binary); file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); check(bool(file), "Write the 1.3.0 record"); }
    const auto written = fs::last_write_time(root / "settings.json");
    {
        const ehud::data::SettingsStore store(root);
        const auto& value = store.value();
        for (const auto& [key, saved] : record.object())
            if (key != "windowsSummonHotkeyDisabled") check(value.fields[key] == saved, "A 1.3.0 key loads unchanged: " + key);
        check(!value.fields.contains("windowsSummonHotkeyDisabled") && !value.summonHotkeyEnabled() && store.hasLaunched(), "The D2 mirror and the first-run marker load as before");
        check(value.showsCloseHint() && !value.chargeBarLength() && value.overlayLayout().isDefault() && value.applicationIcon() == "watch" &&
              store.lastOpenedModule() == "map" && !store.sanityMigrated() && !value.fields.contains("hudOverlayLayout"),
              "The 1.3.1 keys read their defaults and the battery icon reads as the watch");
    }
    check(*ehud::data::detail::readFile(root / "settings.json", 1024 * 1024) == bytes && fs::last_write_time(root / "settings.json") == written,
          "Loading a 1.3.0 record writes nothing");
}
}

int main() {
    try {
        const Media media{7};
        auto fixture = app::makeNotesOwner<PersistentNotes>(false, std::string("new-root"), 1, media);
        check(fixture->root == "new-root" && !fixture->persistent, "Development keeps the NEW-root fixture entry point");
        auto production = app::makeNotesOwner<PersistentNotes>(true, std::string("v1"), 1, media);
        check(production->root == "v1" && production->persistent, "Production opens the persistent root");
        check(app::persistentNotesSupported<PersistentNotes, std::string, int, const Media&> &&
              !app::persistentNotesSupported<FixtureOnlyNotes, std::string, int, const Media&>, "Entry point detection");
        PersistentShelfOptions shelf{"v1"};
        check(!shelf.persistentDataRoot, "Shelf options default to the fixture contract");
        app::persistentShelfRoot(shelf);
        check(shelf.persistentDataRoot && shelf.newDataRoot == "v1", "Production Shelf opens the persistent root");
        check(app::persistentShelfSupported<PersistentShelfOptions> && !app::persistentShelfSupported<FixtureShelfOptions>, "Shelf detection");
        windowsVersion130Settings();
        std::cout << "Persistent storage entry points: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Persistent storage entry points failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
