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
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: data_store_tests profile-state-source.json");
        oracle(argv[1]);
        edges();
        settingsSchema();
        settingsVersion130();
        std::cout << checks << " profile store normalization checks passed; temporary roots only\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Profile store normalization failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
