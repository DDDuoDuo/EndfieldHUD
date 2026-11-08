// ProfileStore load normalization against the Mac oracle. The decode cases of
// tests/fixtures/profile-state-source.json were produced by the unchanged Mac
// UserProfileStore(directory:) (windows/tools/profile_reference.sh): each input
// record is written to a temporary versioned root, reopened through
// ehud::data::ProfileStore and compared with the Mac result. Temporary
// directories only; no real profile is read.
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <string>

using namespace ehud::data;
namespace fs = std::filesystem;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
void rejects(const std::function<void()>& operation, const std::string& why) {
    try { operation(); } catch (const StoreError& error) { check(error.code() == StoreErrorCode::invalid, why); return; }
    throw std::runtime_error(why + " (accepted)");
}
struct Temporary {
    // Canonical: validateRoot refuses symbolic links such as macOS /var.
    fs::path path = fs::canonical(fs::temp_directory_path()) / ("EndfieldHUD-data-store-" + makeUUID());
    Temporary() { check(fs::create_directory(path), "Create a new temporary data root"); }
    ~Temporary() { std::error_code ignored; fs::remove_all(path, ignored); }
};
void writeProfile(const fs::path& root, const Json& profile) {
    fs::create_directories(root / "Profile");
    const auto bytes = Json(Json::Object{{"version", 1}, {"profile", profile}}).encode();
    std::ofstream file(root / "Profile" / "profile.json", std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(bool(file), "Write the synthetic profile record");
}
std::size_t scalarCount(std::string_view text) {
    std::size_t count{};
    for (const auto c : text) count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return count;
}
std::optional<std::string> optionalText(const Json& object, std::string_view key) {
    if (!object.contains(key) || object[key].isNull()) return std::nullopt;
    return object[key].string();
}

// Exact equality for every scalar, numeric and optional field. Text fields the
// Mac also truncates to a grapheme count (applied by the profile owner with ICU
// rules) must equal the Mac value, or extend it only when the trimmed text is
// longer than that limit (every oracle input uses single-scalar graphemes).
void matches(const Profile& actual, const Json& expected, const std::string& name) {
    const auto text = [&](std::string_view key, const std::string& value, std::size_t limit) {
        const auto mac = expected[key].string();
        check(value.starts_with(mac) && (value == mac || (scalarCount(value) > limit && scalarCount(mac) == limit)),
              name + ": " + std::string(key) + " normalizes like the Mac store");
    };
    text("name", actual.name, 20);
    text("tag", actual.tag, 10);
    text("introduction", actual.introduction, 150);
    check(actual.uid == expected["uid"].string(), name + ": uid");
    check(actual.awakeningDate == expected["awakeningDate"].number(), name + ": awakening date");
    check(actual.hasManualAwakeningDate == expected["hasManualAwakeningDate"].boolean() && actual.showsBirthday == expected["showsBirthday"].boolean(), name + ": flags");
    check(actual.permissionLevel == expected["permissionLevel"].integer() && actual.explorationLevel == expected["explorationLevel"].integer(), name + ": levels clamp");
    check(actual.operatorsCount == expected["operatorsCount"].integer() && actual.weaponsCount == expected["weaponsCount"].integer() &&
          actual.archivesCount == expected["archivesCount"].integer(), name + ": counts");
    check(actual.accumulatedWorkSeconds == expected["accumulatedWorkSeconds"].number(), name + ": lifetime work");
    check(actual.playerIDOverride == optionalText(expected, "playerIDOverride") && actual.gamePlayerID == optionalText(expected, "gamePlayerID"), name + ": player identity");
    check(actual.themeColorHex == optionalText(expected, "themeColorHex"), name + ": theme colour");
    check(actual.avatarFilename == optionalText(expected, "avatarFilename") && actual.backgroundFilename == optionalText(expected, "backgroundFilename"), name + ": images");
    for (const auto& [key, value] : {std::pair<const char*, double>{"avatarZoom", actual.avatarZoom}, {"avatarOffsetX", actual.avatarOffsetX},
             {"avatarOffsetY", actual.avatarOffsetY}, {"backgroundWidth", actual.backgroundWidth}, {"backgroundZoom", actual.backgroundZoom},
             {"backgroundOffsetX", actual.backgroundOffsetX}, {"backgroundOffsetY", actual.backgroundOffsetY}, {"thumbnailZoom", actual.thumbnailZoom},
             {"thumbnailOffsetX", actual.thumbnailOffsetX}, {"thumbnailOffsetY", actual.thumbnailOffsetY}})
        check(value == expected[key].number(), name + ": " + key + " clamps");
}

void oracle(const fs::path& fixture) {
    const auto bytes = detail::readFile(fixture, 32 * 1024 * 1024);
    check(bytes.has_value(), "Read the Mac profile oracle");
    const auto source = Json::parse(*bytes, 32 * 1024 * 1024);
    const auto& cases = source["decode"].array();
    check(cases.size() == 11, "Every Mac decode case is present");
    unsigned accepted{}, rejected{};
    for (const auto& item : cases) {
        const auto name = item["name"].string();
        Temporary root;
        writeProfile(root.path, item["input"]);
        if (item.contains("error")) {
            rejects([&] { ProfileStore store(root.path); }, name + ": the Mac rejects this record and so does the store");
            ++rejected;
            continue;
        }
        const ProfileStore store(root.path);
        auto expected = item["output"];
        const auto& input = item["input"];
        if (!input.contains("birthdayMonth")) {
            // The Mac derives a missing annual birthday from the awakening date
            // in the current time zone; this run's zone gives the reference.
            const auto unixSeconds = static_cast<std::time_t>(std::floor(input["awakeningDate"].number() + 978307200.0));
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &unixSeconds);
#else
            localtime_r(&unixSeconds, &local);
#endif
            expected["birthdayMonth"] = local.tm_mon + 1;
            expected["birthdayDay"] = local.tm_mday;
        }
        check(store.value().birthdayMonth == expected["birthdayMonth"].integer() && store.value().birthdayDay == expected["birthdayDay"].integer(),
              name + ": annual birthday");
        matches(store.value(), expected, name);
        ++accepted;
    }
    check(accepted == 4 && rejected == 7, "Mac acceptance and rejection split is unchanged");
}

Json baseline() {
    return Json::Object{{"name", "Endministrator"}, {"tag", "0000"}, {"uid", "1000000000"}, {"awakeningDate", 721692800.0},
        {"permissionLevel", 60}, {"explorationLevel", 7}, {"operatorsCount", 24}, {"weaponsCount", 54}, {"archivesCount", 325},
        {"accumulatedWorkSeconds", 3600.0}, {"futureField", "kept"}};
}

void edges() {
    // Values the Mac store clamps that the former decoder rejected outright.
    auto record = baseline();
    record["permissionLevel"] = 5000; record["explorationLevel"] = -9; record["birthdayMonth"] = 4; record["birthdayDay"] = 31;
    record["themeColorHex"] = "\xEF\xAC\x80" "a#0b1\xE2\x80\x8B"; // U+FB00 "ff", "a#0b1", trailing ZERO WIDTH SPACE
    record["name"] = "\t\xE3\x80\x80" "Endmin\xE2\x80\x8B\n";    // tab, U+3000 ... U+200B LF
    record["tag"] = " 12#34 ";                                    // '#' survives trimming and is then rejected
    Temporary root;
    writeProfile(root.path, record);
    rejects([&] { ProfileStore store(root.path); }, "A tag with '#' stays invalid after trimming");
    record["tag"] = "\xC2\xA0" "12\xE2\x80\xA9";                 // U+00A0 ... U+2029
    record["playerIDOverride"] = "\xE2\x80\xA8 12\r\n3\x0B" "4 \t";
    record["gamePlayerID"] = nullptr; record["avatarZoom"] = nullptr; record["introduction"] = nullptr; record["showsBirthday"] = nullptr;
    writeProfile(root.path, record);
    ProfileStore store(root.path);
    const auto& value = store.value();
    check(value.permissionLevel == 60 && value.explorationLevel == 1, "Any Swift Int level clamps to 1...60 and 1...7");
    check(value.birthdayMonth == 4 && value.birthdayDay == 30, "The annual birthday clamps to its month");
    check(value.themeColorHex == std::optional<std::string>("FFA0B1"), "Theme hex is trimmed, loses '#' and uppercases (U+FB00 -> FF)");
    check(value.name == "Endmin" && value.tag == "12", "Name and tag trim Swift whitespacesAndNewlines including U+200B and U+3000");
    check(value.playerIDOverride == std::optional<std::string>("1234"), "The override drops line breaks, then trims Swift whitespaces");
    check(value.avatarZoom == 1 && value.introduction.empty() && !value.showsBirthday && !value.gamePlayerID, "Explicit null means the Mac decodeIfPresent default");
    // Loading never rewrites the original record; the next edit persists the
    // normalized values and keeps unknown fields.
    const auto before = detail::readFile(store.path(), 1024 * 1024);
    check(before && Json::parse(*before)["profile"]["permissionLevel"].integer() == 5000, "Loading leaves the Mac record unchanged on disk");
    auto edited = value;
    edited.introduction = "Edited";
    check(store.update(edited), "An edit after a normalized load saves");
    const ProfileStore restarted(root.path);
    check(restarted.value().permissionLevel == 60 && restarted.value().introduction == "Edited" && restarted.value().themeColorHex == value.themeColorHex,
          "Normalized values persist across restart");
    const auto after = Json::parse(*detail::readFile(store.path(), 1024 * 1024));
    check(after["profile"]["permissionLevel"].integer() == 60 && after["profile"]["futureField"].string() == "kept", "The saved record is normalized and keeps unknown fields");

    // Theme colour edge cases (UserProfile.normalizedThemeHex).
    const auto theme = [&](std::string input) {
        auto profile = value; profile.themeColorHex = std::move(input);
        return normalizedEditableProfile(profile).themeColorHex;
    };
    check(theme(" #fad41f\n") == std::optional<std::string>("FAD41F"), "Trimmed lowercase colour with '#'");
    check(theme("##F#A#D#4#1#F") == std::optional<std::string>("FAD41F"), "Every '#' is removed, not only a prefix");
    check(theme("\xEF\xAC\x80" "0000") == std::optional<std::string>("FF0000"), "U+FB00 uppercases to FF");
    check(!theme("FAD41") && !theme("FAD41F0") && !theme("FAD 41F") && !theme("xyz123") && !theme("\xEF\xBC\xA1" "BCDEF"), "Anything else drops the colour");
    // Geometry: non-finite values take the default, finite ones clamp.
    auto geometry = value;
    geometry.avatarOffsetX = std::nan(""); geometry.backgroundWidth = 1e9; geometry.thumbnailZoom = -INFINITY; geometry.backgroundOffsetY = -1e9;
    const auto normalized = normalizedEditableProfile(geometry);
    check(normalized.avatarOffsetX == 0 && normalized.backgroundWidth == 900 && normalized.thumbnailZoom == 1 && normalized.backgroundOffsetY == -250,
          "Geometry clamps and replaces non-finite values");
    auto blank = value; blank.playerIDOverride = " \xE3\x80\x80\n ";
    check(!normalizedEditableProfile(blank).playerIDOverride, "A blank override becomes nil");
}

// Settings record schema for the Windows battery-alert additions (no Mac
// oracle). displayMode and alertMetric are open string choices: "never"
// (Don't display) and "gpu" persist and reload verbatim, as does another
// build's unknown value (its readers fall back); a record without them (an
// older build) takes the Mac defaults; a wrong type is still invalid.
void settingsSchema() {
    Temporary root;
    {
        SettingsStore store(root.path);
        check(store.value() == Settings::defaults() && store.value().string("displayMode") == "whenChargingStarts" &&
              store.value().string("alertMetric") == "battery", "A new settings record starts from the Mac defaults");
        auto next = store.value();
        next.set("displayMode", "never");
        next.set("alertMetric", "gpu");
        check(store.update(next) && !store.update(next), "The Windows choices save once");
    }
    const auto written = detail::readFile(root.path / "settings.json", 1024 * 1024);
    check(written.has_value(), "The settings record is on disk");
    const auto envelope = Json::parse(*written);
    check(envelope["version"].integer() == 1 && envelope["settings"]["displayMode"].string() == "never" &&
          envelope["settings"]["alertMetric"].string() == "gpu", "The v1 record stores Don't display and GPU verbatim");
    {
        const SettingsStore reloaded(root.path);
        check(reloaded.value().string("displayMode") == "never" && reloaded.value().string("alertMetric") == "gpu",
              "Don't display and GPU round-trip across a restart");
    }
    const auto writeSettings = [&](const Json& settings) {
        const auto bytes = Json(Json::Object{{"version", 1}, {"settings", settings}}).encode();
        std::ofstream file(root.path / "settings.json", std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        check(bool(file), "Write the synthetic settings record");
    };
    writeSettings(Json::Object{{"displayMode", "sometimes"}, {"alertMetric", "tpu"}});
    {
        const SettingsStore future(root.path);
        check(future.value().string("displayMode") == "sometimes" && future.value().string("alertMetric") == "tpu" &&
              future.value().string("theme") == "dark", "Another build's unknown choices load verbatim");
    }
    writeSettings(Json::Object{{"theme", "light"}});
    {
        const SettingsStore older(root.path);
        check(older.value().string("displayMode") == "whenChargingStarts" && older.value().string("alertMetric") == "battery" &&
              older.value().string("theme") == "light", "A record without them takes the Mac defaults");
    }
    writeSettings(Json::Object{{"displayMode", 3}});
    rejects([&] { SettingsStore invalid(root.path); }, "A non-string display method is still invalid");
}
// Mac 1.3.0 AppConfiguration keys (Sources/Models.swift:82-91, 228-232,
// 281-287), HUDModuleLayout (HUDNavigationTarget.swift:88-144),
// ProjectionSettings (ProjectionSettings.swift:31-41, 98-101) and decisions
// D2/D3. Hand-written records; no Mac oracle exists for these keys.
void settingsVersion130() {
    Temporary root;
    const auto writeSettings = [&](const Json& settings) {
        const auto bytes = Json(Json::Object{{"version", 1}, {"settings", settings}}).encode();
        std::ofstream file(root.path / "settings.json", std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        check(bool(file), "Write the synthetic settings record");
    };
    const auto defaults = Settings::defaults();
    check(defaults.summonHotkeyEnabled() && !defaults.recordingMode() && defaults.dailyBootLog() && defaults.facilityAlerts() &&
          !defaults.fields.contains("hudModuleOrder") && !defaults.fields.contains("hudHiddenModules") && !defaults.dailyBootLogLastDay() &&
          defaults.moduleLayout().isDefault() && defaults.projection() == ProjectionSettings{} && defaults.orbipomBestScore() == 0,
          "The 1.3.0 defaults: hotkey on, recording off, boot log on, facility alerts on, default layout, pen 12/theme colour");
    // A Windows 1.2.0 record (no 1.3.0 keys) reads with the Mac defaults.
    writeSettings(Json::Object{{"theme", "light"}, {"storageVolume", "\\\\?\\Volume{1}\\"}});
    {
        const SettingsStore older(root.path);
        check(older.value().summonHotkeyEnabled() && older.value().dailyBootLog() && older.value().facilityAlerts() && !older.value().recordingMode() &&
              older.value().moduleLayout().isDefault(), "A Windows 1.2.0 record reads the 1.3.0 defaults");
    }
    // D2: windowsSummonHotkeyDisabled migrates (opposite polarity), never lives
    // in memory, and is written back as a mirror for Windows 1.2.0 readers.
    writeSettings(Json::Object{{"windowsSummonHotkeyDisabled", true}});
    {
        SettingsStore migrated(root.path);
        check(!migrated.value().summonHotkeyEnabled() && !migrated.value().fields.contains("windowsSummonHotkeyDisabled"), "A disabled Windows hotkey becomes summonHotkeyEnabled=false");
        auto next = migrated.value();next.set("recordingMode", true);
        check(migrated.update(next), "The migrated record saves");
    }
    {
        const auto envelope = Json::parse(*detail::readFile(root.path / "settings.json", 1024 * 1024));
        check(envelope["settings"]["windowsSummonHotkeyDisabled"] == Json(true) && envelope["settings"]["summonHotkeyEnabled"] == Json(false) &&
              envelope["settings"]["recordingMode"] == Json(true), "While off, the record mirrors windowsSummonHotkeyDisabled: true");
    }
    // A Windows 1.2.0-style reader (application_display.cpp at 1.2.0: on unless
    // windowsSummonHotkeyDisabled is true) sees the state this build saved.
    const auto legacyEnabled = [&] { const auto envelope = Json::parse(*detail::readFile(root.path / "settings.json", 1024 * 1024));
        const auto& disabled = envelope["settings"]["windowsSummonHotkeyDisabled"]; return !(disabled.isBool() && disabled.boolean()); };
    check(!legacyEnabled(), "Windows 1.2.0 reads the hotkey as off");
    {
        SettingsStore store(root.path);auto next = store.value();
        check(!next.fields.contains("windowsSummonHotkeyDisabled") && !next.summonHotkeyEnabled(), "The mirror reloads as the Mac key only");
        next.set("summonHotkeyEnabled", true);check(store.update(next), "Switch the hotkey back on");
        const auto envelope = Json::parse(*detail::readFile(root.path / "settings.json", 1024 * 1024));
        check(!envelope["settings"].contains("windowsSummonHotkeyDisabled") && envelope["settings"]["summonHotkeyEnabled"] == Json(true) && legacyEnabled(),
              "While on, the legacy key is removed and Windows 1.2.0 reads it as on");
        next.set("summonHotkeyEnabled", false);store.update(next);check(!legacyEnabled(), "Off again: mirrored again");
        check(store.markLaunched() && !legacyEnabled(), "The first-run marker write keeps the mirror");
    }
    writeSettings(Json::Object{{"windowsSummonHotkeyDisabled", true}, {"summonHotkeyEnabled", true}});
    check(SettingsStore(root.path).value().summonHotkeyEnabled(), "A saved summonHotkeyEnabled wins over a stale Windows key");
    writeSettings(Json::Object{{"windowsSummonHotkeyDisabled", false}});
    check(SettingsStore(root.path).value().summonHotkeyEnabled(), "An enabled Windows hotkey stays enabled");
    // D3: layout keys absent = default; string arrays validated; unknown names dropped on read.
    writeSettings(Json::Object{{"hudModuleOrder", Json::Array{"map", "notes", "map", "teleporter"}}, {"hudHiddenModules", Json::Array{"notes", "addApp"}}});
    {
        const SettingsStore store(root.path);
        const auto layout = store.value().moduleLayout();
        check(layout.order.size() == 17 && layout.order[0] == "map" && layout.order[1] == "notes" && layout.order[2] == "fileShelf" &&
              layout.hidden == std::vector<std::string>{"notes"} && layout.visible().front() == "map" && layout.restorable("notes") == "map" &&
              layout.restorable("clock") == "clock", "Unknown/repeated names drop, missing modules append, Add App is not configurable");
        check(store.value().fields["hudModuleOrder"].array().size() == 4, "The stored arrays round-trip verbatim until the layout changes");
    }
    writeSettings(Json::Object{{"hudModuleOrder", "notes"}});
    rejects([&] { SettingsStore invalid(root.path); }, "A non-array module order is invalid");
    writeSettings(Json::Object{{"hudHiddenModules", Json::Array{1}}});
    rejects([&] { SettingsStore invalid(root.path); }, "A non-string hidden module is invalid");
    {
        Settings value = Settings::defaults();
        ModuleLayout layout;
        for (const auto name : ModuleLayout::configurable) check(layout.setHidden(name, true) == (name != ModuleLayout::configurable.back()), "Hiding stops at the last visible module");
        check(layout.visible() == std::vector<std::string>{"power"} && !layout.canSetHidden("power", true), "One module always stays visible");
        auto all = ModuleLayout::normalized({}, std::vector<std::string>(ModuleLayout::configurable.begin(), ModuleLayout::configurable.end()));
        check(all.hidden.size() == 16 && !all.isHidden("notes"), "All hidden: the first of the order is shown again");
        layout = {};check(layout.move("power", -4) && layout.order.front() == "power" && !layout.move("power", 0) && layout.move("notes", 99) && layout.order.back() == "notes", "Moves clamp and report no-ops");
        value.setModuleLayout(layout);
        check(value.fields["hudModuleOrder"].array().size() == 17 && !value.fields.contains("hudHiddenModules"), "A changed order is stored in full; no hidden key");
        layout.setHidden("reader", true);layout.setHidden("archive", true);value.setModuleLayout(layout);
        check(value.fields["hudHiddenModules"] == Json(Json::Array{"archive", "reader"}), "Hidden modules are stored in list order");
        value.setModuleLayout(ModuleLayout{});
        check(!value.fields.contains("hudModuleOrder") && !value.fields.contains("hudHiddenModules"), "The default layout leaves no keys behind");
    }
    // Daily boot log: consumed once per local day; Restore Defaults keeps the day.
    {
        Settings value = Settings::defaults();
        check(value.consumeDailyBootLog("2026-10-22") && !value.consumeDailyBootLog("2026-10-22") && value.consumeDailyBootLog("2026-10-23") &&
              value.dailyBootLogLastDay() == std::optional<std::string>("2026-10-23"), "The boot log plays on the first open of each day");
        value.set("orbipom.bestScore.v1", std::int64_t{42});value.set("storageVolume", "C:");value.set("recordingMode", true);
        value.setProjection({0x22A6F2, 30, true, false});
        const auto restored = Settings::restoredDefaults(value);
        check(restored.dailyBootLogLastDay() == value.dailyBootLogLastDay() && restored.orbipomBestScore() == 42 && restored.string("storageVolume") == "C:" &&
              restored.projection() == value.projection() && !restored.recordingMode(), "Restore Defaults resets AppConfiguration and keeps the keys outside it");
    }
    // Battery alert method: false shows as Don't display; any choice turns alerts back on.
    {
        Settings value = Settings::defaults();
        value.set("batteryAlertsEnabled", false);value.set("displayMode", "always");
        check(value.batteryAlertMethod() == "never", "batteryAlertsEnabled=false shows as Don't display");
        value.setBatteryAlertMethod("always");
        check(value.batteryAlertMethod() == "always" && value.boolean("batteryAlertsEnabled"), "Choosing a method turns battery alerts back on");
    }
    // projection.settings.v1 read rules.
    {
        check(decodeProjectionSettings(Json::Object{{"color", std::int64_t{0x1FFFA00}}, {"width", 200.}, {"erasing", true}}) == ProjectionSettings{0xFFFA00, 80, true, true},
              "Colour masked, width clamped to 80, background defaults to true");
        check(decodeProjectionSettings(Json::Object{{"color", "red"}, {"width", .2}, {"background", false}}) == ProjectionSettings{std::nullopt, 1, false, false},
              "A non-number colour keeps the theme colour; width clamps to 1");
        check(decodeProjectionSettings(Json("x")) == ProjectionSettings{}, "A non-dictionary takes every default");
        const ProjectionSettings pen{0xE5383B, 24, false, true};
        check(decodeProjectionSettings(encodeProjectionSettings(pen)) == pen && encodeProjectionSettings(pen)["color"].integer() == 0xE5383B, "The pen round-trips");
    }
    // Every 1.3.0 key survives a store round trip, and a Windows 1.2.0-style
    // reader (keys outside its defaults round-trip verbatim) keeps them.
    {
        writeSettings(Json::Object{});
        SettingsStore store(root.path);auto next = store.value();
        next.set("recordingMode", true);next.set("facilityAlerts", false);next.set("dailyBootLog", false);next.consumeDailyBootLog("2026-10-22");
        ModuleLayout layout;layout.move("map", 0);layout.setHidden("volume", true);next.setModuleLayout(layout);next.setProjection({0x8CE03C, 6, false, true});
        check(store.update(next), "Save every 1.3.0 key");
        const SettingsStore reloaded(root.path);
        check(reloaded.value() == next && reloaded.value().moduleLayout() == next.moduleLayout(), "Every 1.3.0 key reloads unchanged");
    }
}
// Mac 1.3.1 settings keys (Sources/Models.swift, HUDOverlayLayout.swift,
// HUDApplicationIcon.swift at 2c047e0). Hand-written records; the typed
// accessors read a missing or wrong-typed value as its default and only a save
// rewrites what the Mac's next save rewrites.
Json settingsFile(const fs::path& root) { return Json::parse(*detail::readFile(root / "settings.json", 1024 * 1024)); }
// A stand-in for String(prefix(n)) counting UTF-8 scalars (the owner passes
// the ICU grapheme rule; these strings have one scalar per Character).
std::string scalarPrefix(std::string_view text, std::size_t count) {
    std::size_t end = 0;
    for (std::size_t seen = 0; end < text.size() && seen < count; ++seen)
        do ++end; while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80);
    return std::string(text.substr(0, end));
}
// HUDOverlayLayout: Stored decoding, normalization, the default removal.
void overlayLayout131(const std::function<void(const Json&)>& writeSettings, const fs::path& root) {
    using L = OverlayLayout;
    check(ModuleLayout::openable.size() == 23 && ModuleLayout::openable[0] == "system" && ModuleLayout::openable[5] == "activityMonitor" &&
          ModuleLayout::openable[6] == "notes" && ModuleLayout::openable.back() == "power" && !ModuleLayout::isOpenable("addApp") &&
          ModuleLayout::isModule("addApp") && ModuleLayout::isOpenable("hotkeys") && !ModuleLayout::isModule("clock"),
          "Openable modules: the fixed six, then the configurable list; Add App is a module but never openable");
    const L defaults;
    check(Settings::defaults().overlayLayout() == defaults && defaults.isDefault() && !Settings::defaults().fields.contains("hudOverlayLayout") &&
          defaults.shownTitle() == "ENDFIELDHUD" && defaults.shownSubtitle() == "SYSTEM INTERFACE" && !defaults.openModule &&
          defaults.title == L::Placement{0, 0, 1} && defaults.showsTitle && defaults.showsSanity, "The layout's defaults; no key");
    // cleanText: one line, trimmed, at most 24 Characters.
    check(L::cleanText("  Rhodes \n\n\t Island  \r\n") == "Rhodes Island" && L::cleanText(" \n \u3000") == "" &&
          L::cleanText("a\xE2\x80\xA8" "b\xC2\x85" "c") == "a b c" && L::cleanText("\xC2\xA0x\xC2\xA0") == "x",
          "Lines split on Swift newlines, trimmed of Swift whitespace, joined by one space");
    check(L::cleanText("ABCDEFGHIJKLMNOPQRSTUVWXYZ", scalarPrefix) == "ABCDEFGHIJKLMNOPQRSTUVWX" &&
          L::cleanText("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == "ABCDEFGHIJKLMNOPQRSTUVWXYZ" &&
          L::cleanText("\xE7\xBB\x88\xE6\x9C\xAB\xE5\x9C\xB0\n\xE5\x9C\xB0", [](std::string_view t, std::size_t n) { return scalarPrefix(t, n == 24 ? 2 : n); }) == "\xE7\xBB\x88\xE6\x9C\xAB",
          "The 24-Character cut goes through the owner's prefix rule");
    // Decoding mirrors JSONDecoder: missing and null read as defaults.
    Settings value = Settings::defaults();
    const auto read = [&](const Json& stored) { value.set("hudOverlayLayout", stored); return value.overlayLayout(scalarPrefix); };
    auto expected = defaults;expected.title = {12, -8, 1};expected.sanity = {0, 0, 1.5};expected.showsSanity = false;expected.titleText = "Talos II";expected.openModule = "hotkeys";
    check(read(Json::Object{{"title", Json::Object{{"x", 12}, {"y", -8}}}, {"sanity", Json::Object{{"size", 1.5}, {"x", nullptr}}}, {"alerts", nullptr},
                             {"showsSanity", false}, {"showsTitle", nullptr}, {"titleText", " Talos II "}, {"openModule", "hotkeys"}, {"future", 7}}) == expected,
          "Missing and null fields read as defaults; unknown members are ignored; text is cleaned");
    check(read(Json::Object{{"title", Json::Object{{"x", 5000}, {"y", -4000}, {"size", .1}}}, {"alerts", Json::Object{{"size", 9}}}}).title == L::Placement{1000, -1000, .5} &&
          value.overlayLayout().alerts.size == 2, "Offsets clamp to 1000 and sizes to 0.5...2");
    check(!read(Json::Object{{"openModule", "addApp"}}).openModule && !read(Json::Object{{"openModule", "teleporter"}}).openModule &&
          read(Json::Object{{"openModule", "activityMonitor"}}).openModule == std::optional<std::string>("activityMonitor"),
          "Open on keeps an openable module; Add App and unknown names read as Last opened");
    for (const auto& bad : {Json(Json::Object{{"showsTitle", 1}}), Json(Json::Object{{"title", Json::Object{{"x", "4"}}}}), Json(Json::Object{{"sanity", 2}}),
                            Json(Json::Object{{"titleText", Json::Array{}}}), Json(Json::Object{{"openModule", 3}}), Json("layout"), Json(Json::Array{})}) {
        auto mixed = bad;if (mixed.isObject()) mixed["showsSanity"] = false;
        check(read(mixed) == defaults, "A wrong-typed field drops the whole layout to the defaults");
    }
    // Writing: normalized; removed while it holds the defaults.
    auto layout = defaults;layout.alerts = {-20, 30, 3};layout.subtitleText = "\n  RHODES  ";layout.openModule = "addApp";
    value.setOverlayLayout(layout);
    check(value.fields["hudOverlayLayout"] == Json(Json::Object{{"title", Json::Object{{"x", 0}, {"y", 0}, {"size", 1}}}, {"sanity", Json::Object{{"x", 0}, {"y", 0}, {"size", 1}}},
              {"alerts", Json::Object{{"x", -20}, {"y", 30}, {"size", 2}}}, {"showsTitle", true}, {"showsSanity", true}, {"titleText", ""}, {"subtitleText", "RHODES"}}),
          "Every field is written normalized, except a nil Open on");
    layout.openModule = "map";value.setOverlayLayout(layout);
    check(value.fields["hudOverlayLayout"]["openModule"] == Json("map"), "Open on is written when chosen");
    auto blank = defaults;blank.titleText = " \n ";blank.title.size = std::nan("");
    value.setOverlayLayout(blank);
    check(!value.fields.contains("hudOverlayLayout"), "A layout that normalizes to the defaults leaves no key");
    // A wrong-typed layout leaves the rest of settings.json loading; the
    // next save removes it (the Mac saves the defaults it read).
    writeSettings(Json::Object{{"theme", "light"}, {"hudOverlayLayout", Json::Object{{"showsSanity", "off"}}}});
    {
        SettingsStore store(root);
        check(store.value().string("theme") == "light" && store.value().overlayLayout() == defaults &&
              settingsFile(root)["settings"]["hudOverlayLayout"]["showsSanity"] == Json("off"), "A bad layout reads as the defaults and loads the record unchanged");
        auto next = store.value();next.set("recordingMode", true);
        check(store.update(next) && !settingsFile(root)["settings"].contains("hudOverlayLayout") && !store.value().fields.contains("hudOverlayLayout"),
              "The next save removes the unusable layout");
        next = store.value();auto placed = defaults;placed.sanity = {40, 10, 1.25};placed.showsTitle = false;next.setOverlayLayout(placed);
        check(store.update(next) && SettingsStore(root).value().overlayLayout() == placed, "A layout round-trips through the store");
    }
}
void settingsVersion131() {
    Temporary root;
    const auto writeSettings = [&](const Json& settings) {
        const auto bytes = Json(Json::Object{{"version", 1}, {"settings", settings}}).encode();
        std::ofstream file(root.path / "settings.json", std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        check(bool(file), "Write the synthetic settings record");
    };
    // chargeBarLength: absent is the classic bar; finite > 0 reads min(1, v).
    {
        const auto defaults = Settings::defaults();
        check(!defaults.fields.contains("chargeBarLength") && !defaults.chargeBarLength() && defaults.showsCloseHint() &&
              defaults.fields["showsCloseHint"] == Json(true), "Defaults: the classic bar (no key) and the close hint on");
        Settings value = defaults;
        const auto read = [&](const Json& stored) { value.set("chargeBarLength", stored); return value.chargeBarLength(); };
        check(read(.5) == std::optional(.5) && read(1) == std::optional(1.) && read(7.25) == std::optional(1.) && !read(0) && !read(-.2) &&
              !read("0.5") && !read(true) && !read(nullptr), "A length reads min(1, v) when finite and above 0, otherwise nil");
        value.setChargeBarLength(.4);check(value.fields["chargeBarLength"] == Json(.4), "A length is written when set");
        value.setChargeBarLength(3);check(value.fields["chargeBarLength"] == Json(1), "A length is written normalized");
        for (const double bad : {0., -1., std::nan(""), std::numeric_limits<double>::infinity()}) {
            value.setChargeBarLength(.4);value.setChargeBarLength(bad);
            check(!value.fields.contains("chargeBarLength"), "A length that is not finite and above 0 removes the key");
        }
        value.setChargeBarLength(.4);value.setChargeBarLength(std::nullopt);
        check(!value.fields.contains("chargeBarLength"), "nil removes the key");
        value.set("showsCloseHint", false);check(!value.showsCloseHint(), "The close hint switches off");
        value.fields.erase("showsCloseHint");check(value.showsCloseHint(), "A missing close hint reads as on");
    }
    // A 1.3.0 record (no 1.3.1 keys) reads the defaults; a bad length loads
    // unchanged and the next save of any setting stores it normalized.
    writeSettings(Json::Object{{"theme", "light"}, {"chargeBarLength", 4}});
    {
        SettingsStore store(root.path);
        check(store.value().showsCloseHint() && store.value().chargeBarLength() == std::optional(1.) &&
              settingsFile(root.path)["settings"]["chargeBarLength"] == Json(4), "Loading reads the length as 1 and writes nothing");
        check(!store.update(store.value()), "An update that changes nothing writes nothing");
        auto next = store.value();next.set("recordingMode", true);
        check(store.update(next) && settingsFile(root.path)["settings"]["chargeBarLength"] == Json(1) &&
              store.value().fields["chargeBarLength"] == Json(1), "The next save stores the length normalized");
    }
    writeSettings(Json::Object{{"chargeBarLength", "wide"}, {"showsCloseHint", false}});
    {
        SettingsStore store(root.path);
        check(!store.value().chargeBarLength() && !store.value().showsCloseHint() && settingsFile(root.path)["settings"]["chargeBarLength"] == Json("wide"),
              "A wrong-typed length reads as the classic bar and the rest of the record loads");
        auto next = store.value();next.set("theme", "light");
        check(store.update(next) && !settingsFile(root.path)["settings"].contains("chargeBarLength") && !settingsFile(root.path)["settings"]["showsCloseHint"].boolean(),
              "The next save removes the unusable length and keeps the close hint off");
    }
    writeSettings(Json::Object{{"showsCloseHint", "no"}});
    rejects([&] { SettingsStore invalid(root.path); }, "A non-Boolean close hint is invalid, like the other 1.3.0 switches");
    overlayLayout131(writeSettings, root.path);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: data_store_tests profile-state-source.json");
        oracle(argv[1]);
        edges();
        settingsSchema();
        settingsVersion130();
        settingsVersion131();
        std::cout << checks << " profile store normalization checks passed; temporary roots only\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Profile store normalization failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
