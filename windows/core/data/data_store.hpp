#pragma once
#include "core/data/json.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ehud::data {
enum class StoreErrorCode { invalid, newerVersion, changedOnDisk, unavailable, tooLarge };
class StoreError : public std::runtime_error {
public:
    StoreError(StoreErrorCode code, std::string message) : std::runtime_error(std::move(message)), code_(code) {}
    StoreErrorCode code() const noexcept { return code_; }
private: StoreErrorCode code_;
};

std::string makeUUID();
bool validUUID(std::string_view value) noexcept;
double foundationNow(); // seconds since 2001-01-01 UTC, matching Mac Codable Date
bool validWindowsFilePath(std::string_view path) noexcept;
// Native references use an additive v1 locator: referencePlatform="windows"
// and windowsPath, without bookmark/isSecurityScoped/lastKnownPath. Common
// Mac metadata remains unchanged. A mixed locator is invalid; no fake bookmark
// is made and a native descriptor is never written to an actual Mac data root.
std::string makeWindowsMediaReference(std::string path, std::string displayName,
    int pixelWidth, int pixelHeight, std::string kind = "image",
    std::optional<double> duration = {}, int frameCount = 1);

// Mac HUDModuleLayout (Sources/HUDNavigationTarget.swift:84-144): order and
// visibility of the 17 right-hand modules (System > Modules), by HUDModule
// raw value. Every value is normalized like the Mac init: unknown and
// repeated names are dropped, configurable modules missing from the order are
// appended in default order (modules a later version adds), hidden is kept in
// list order, and when all are hidden the first of the order is shown again.
// Add App is not configurable. No UI state (drag, scroll) lives here.
struct ModuleLayout {
    static constexpr std::array<std::string_view, 17> configurable{"notes", "fileShelf", "clipboard", "archive", "mediaAssembly",
        "minigame", "nowPlaying", "volume", "projection", "reader", "workMode", "calendar", "map", "eventLog", "profile", "account", "power"};
    std::vector<std::string> order{configurable.begin(), configurable.end()}, hidden;
    static ModuleLayout normalized(std::vector<std::string> order, std::vector<std::string> hidden);
    std::vector<std::string> visible() const;
    // The module to reopen the HUD on: a hidden one is not restored, the
    // first visible module stands in for it (HUDModuleLayout.restorable).
    std::string restorable(std::string_view module) const;
    bool isDefault() const;
    bool isHidden(std::string_view module) const;
    bool canSetHidden(std::string_view module, bool value) const; // false when hiding the last visible
    bool setHidden(std::string_view module, bool value);
    bool move(std::string_view module, std::ptrdiff_t index);    // clamped; false when unchanged
    bool operator==(const ModuleLayout&) const = default;
};
// Mac ProjectionSettings.Values (Sources/ProjectionSettings.swift:17-41),
// stored under the Mac key "projection.settings.v1" as {color: 0xRRGGBB
// number, width, erasing, background}. color nil = the theme colour (absent
// key or field; it follows the theme). Reads mirror the Mac: colour masked
// & 0xFFFFFF, width finite and clamped 1...80, any missing or invalid field
// takes its default. Darkness and blur are not part of it.
struct ProjectionSettings {
    std::optional<std::uint32_t> color;
    double width{12};
    bool erasing{}, background{true};
    bool operator==(const ProjectionSettings&) const = default;
};

// One preference record (the Mac AppConfiguration keys plus additive Windows
// keys). String choices such as displayMode/alertMetric are not closed sets:
// a value another build wrote loads unchanged and its reader falls back.
// Keys the Mac 1.3.0 added (Sources/Models.swift:82-91, 228-232, 281-287):
// summonHotkeyEnabled, recordingMode, dailyBootLog and facilityAlerts are in
// defaults() (always written, like the Mac); hudModuleOrder/hudHiddenModules
// are absent while default (the Mac removes them) and are string arrays when
// present; dailyBootLogLastDay (local yyyy-MM-dd) and projection.settings.v1
// live outside AppConfiguration, so Restore Defaults keeps them. Windows
// stores "never"/"gpu" in displayMode/alertMetric directly: its file is never
// read by a Mac, so the Mac's displayModeExtension/alertMetricExtension pair
// exists only in the Mac import. Windows 1.2.0 round-trips every new key.
// D2: summonHotkeyEnabled is the switch; the store migrates a 1.2.0
// windowsSummonHotkeyDisabled on load (the Mac key wins when both exist) and,
// for Windows 1.2.0 readers, writes it back as a mirror (true while the hotkey
// is off, removed while it is on). The legacy key is never in `fields`.
struct Settings {
    Json fields{Json::Object{}};
    static Settings defaults();
    // Mac restoreDefaults resets AppConfiguration only: keys outside it
    // (orbipom.bestScore.v1, projection.settings.v1, dailyBootLogLastDay and
    // the Windows Storage disk choice storageVolume) survive.
    static Settings restoredDefaults(const Settings& current);
    std::string string(std::string_view key) const;
    double number(std::string_view key) const;
    bool boolean(std::string_view key) const;
    void set(std::string key, Json value);
    // Typed views of the 1.3.0 keys (absent or wrong type = the Mac default).
    bool summonHotkeyEnabled() const;
    bool recordingMode() const;
    bool dailyBootLog() const;
    bool facilityAlerts() const;
    std::optional<std::string> dailyBootLogLastDay() const;
    // OverlayController.consumeDailyBootLog (OverlayController.swift:1079-1087)
    // after its dailyBootLog / Reduce Motion / diagnostic-run guard, which the
    // caller applies: true when `today` (local yyyy-MM-dd) differs from the
    // stored day, which then becomes today (consumed even if the log is skipped).
    bool consumeDailyBootLog(std::string_view today);
    ModuleLayout moduleLayout() const;
    void setModuleLayout(const ModuleLayout&); // removes the keys when default, as the Mac does
    // The Battery alert method menu (SHELL-99): a saved batteryAlertsEnabled
    // = false shows as "never" (Don't display); choosing any method stores it
    // and turns batteryAlertsEnabled back on.
    std::string batteryAlertMethod() const;
    void setBatteryAlertMethod(std::string method);
    ProjectionSettings projection() const;
    void setProjection(const ProjectionSettings&); // written in full, like ProjectionSettings.save
    std::int64_t orbipomBestScore() const;         // "orbipom.bestScore.v1", absent/invalid = 0
    bool operator==(const Settings&) const = default;
};
// Mac ProjectionSettings.init's reading of a stored dictionary (also used by
// the Mac import with the plist coerced to these JSON types).
ProjectionSettings decodeProjectionSettings(const Json&);
Json encodeProjectionSettings(const ProjectionSettings&);
class SettingsStore final {
public:
    explicit SettingsStore(const std::filesystem::path& appRoot);
    const Settings& value() const noexcept { return value_; }
    bool update(const Settings& value); // false for an unchanged record; no write
    const std::filesystem::path& path() const noexcept { return path_; }
    // AppDelegate's separate "hasLaunched" defaults key. It is kept in the
    // envelope beside (never inside) the preference record, so Restore
    // Defaults cannot repeat first-run onboarding and Mac import can map it.
    bool hasLaunched() const noexcept { return launched_; }
    bool markLaunched(); // false when already marked; no write
private:
    std::filesystem::path path_;
    Settings value_;
    Json envelope_{Json::Object{}};
    std::optional<std::string> persisted_;
    bool launched_{};
};

struct Profile {
    std::string name{"Endministrator"}, tag{"0000"}, introduction, uid;
    double awakeningDate{};
    std::optional<std::string> gamePlayerID, playerIDOverride;
    bool hasManualAwakeningDate{}, showsBirthday{};
    int birthdayMonth{1}, birthdayDay{1}, permissionLevel{60}, explorationLevel{7};
    std::int64_t operatorsCount{24}, weaponsCount{54}, archivesCount{325};
    std::optional<std::string> avatarFilename, backgroundFilename, themeColorHex;
    double avatarZoom{1}, avatarOffsetX{}, avatarOffsetY{};
    double backgroundWidth{600}, backgroundZoom{1}, backgroundOffsetX{}, backgroundOffsetY{};
    double thumbnailZoom{1}, thumbnailOffsetX{}, thumbnailOffsetY{}, accumulatedWorkSeconds{};
    Json originalFields{Json::Object{}}; // unknown additive Mac fields survive edits
    static Profile defaults();
    std::string displayedUID() const;
    bool operator==(const Profile&) const = default;
};
// The personal-card fields an Endfield account sync writes (Mac
// HUDAccountPresentation.SyncField, Sources/HUDAccountCanvas.swift:23-24). The
// raw values are the strings Account/profile-cache.json keeps in
// unsyncedFields. uid = gamePlayerID + cleared playerIDOverride; awakening =
// awakeningDate + hasManualAwakeningDate; levels = permission + exploration;
// counts = operators, weapons and archives. Deliberately no implicit
// conversion from bool: a whole-card lock is ProfileSyncFields::all().
enum class ProfileSyncField : std::uint8_t { name, tag, uid, awakening, levels, counts };
inline constexpr ProfileSyncField profileSyncFieldOrder[]{ProfileSyncField::name, ProfileSyncField::tag, ProfileSyncField::uid,
    ProfileSyncField::awakening, ProfileSyncField::levels, ProfileSyncField::counts};
constexpr std::string_view profileSyncFieldName(ProfileSyncField field) noexcept {
    constexpr std::string_view names[]{"name", "tag", "uid", "awakening", "levels", "counts"};
    return names[static_cast<std::size_t>(field) % 6];
}
constexpr std::optional<ProfileSyncField> profileSyncFieldNamed(std::string_view raw) noexcept {
    for (const auto field : profileSyncFieldOrder) if (profileSyncFieldName(field) == raw) return field;
    return std::nullopt;
}
struct ProfileSyncFields {
    std::uint8_t bits{};
    static constexpr ProfileSyncFields all() noexcept { return {63}; }
    constexpr bool contains(ProfileSyncField field) const noexcept { return (bits >> static_cast<unsigned>(field)) & 1u; }
    constexpr ProfileSyncFields with(ProfileSyncField field) const noexcept { return {static_cast<std::uint8_t>(bits | (1u << static_cast<unsigned>(field)))}; }
    constexpr ProfileSyncFields without(ProfileSyncField field) const noexcept { return {static_cast<std::uint8_t>(bits & ~(1u << static_cast<unsigned>(field)))}; }
    constexpr explicit operator bool() const noexcept { return bits != 0; }
    bool operator==(const ProfileSyncFields&) const = default;
};
// Mac UserProfile.normalizedEditableValues(), applied by ProfileStore to the
// record it loads (as the Mac store does) instead of rejecting a record the
// Mac accepts: the player ID override loses line breaks and outer whitespace
// (nil when empty); name and tag are trimmed; levels clamp to 1...60 / 1...7;
// the annual birthday clamps to its month; the theme colour becomes six
// uppercase hex digits or nil; every portrait and card geometry value clamps
// to its range (non-finite values take the default). Grapheme-count limits
// (name 20, tag 10, introduction 150, player ID 64) need Unicode segmentation:
// the profile owner applies them with its ICU text rules
// (modules::normalizedProfile); this store keeps longer text within its byte
// bounds rather than rejecting it.
Profile normalizedEditableProfile(Profile);
class ProfileStore final {
public:
    explicit ProfileStore(const std::filesystem::path& appRoot);
    const Profile& value() const noexcept { return value_; }
    bool update(const Profile& value);
    // The account owner derives this from enabled sync + linked Endfield role
    // (Mac gameSyncedFields: the sync's fields, none while sync is off): local
    // edits of exactly these fields are refused. Cached/reconnecting roles stay
    // locked; the store creates no account timer.
    void setProfileSyncLocked(ProfileSyncFields locked) noexcept { syncLocked_ = locked; }
    ProfileSyncFields profileSyncLocked() const noexcept { return syncLocked_; }
    bool updateFromGame(const Profile& value);
    const std::filesystem::path& path() const noexcept { return path_; }
    std::filesystem::path imagePath(std::string_view filename) const;
private:
    std::filesystem::path path_;
    Profile value_;
    Json envelope_{Json::Object{}};
    std::optional<std::string> persisted_;
    ProfileSyncFields syncLocked_{};
    bool commit(const Profile& value, bool fromGame);
};

enum class NoteKind { text, todo, image, drawing };
struct ChecklistItem {
    std::string id{makeUUID()}, text;
    bool isChecked{};
    Json originalFields{Json::Object{}};
    bool operator==(const ChecklistItem&) const = default;
};
struct Note {
    std::string id{makeUUID()};
    NoteKind kind{NoteKind::text};
    std::string text;
    std::vector<ChecklistItem> items;
    std::optional<std::string> imageName;
    double x{24}, y{50}, width{160}, height{110};
    std::int64_t zIndex{};
    double createdAt{foundationNow()};
    bool isPinned{};
    // These JSON payloads remain byte-for-byte until their editor changes them.
    // Rich-text ranges use UTF-16. Imported Mac bookmarks stay opaque. The
    // media field also accepts the explicit native Windows locator above.
    std::optional<std::string> richText, media, drawing;
    static Note textNote(std::string text = {});
    static Note todoNote();
    bool operator==(const Note&) const = default;
};
class NotesStore final {
public:
    explicit NotesStore(const std::filesystem::path& appRoot);
    ~NotesStore();
    NotesStore(const NotesStore&) = delete;
    NotesStore& operator=(const NotesStore&) = delete;
    const std::vector<Note>& notes() const noexcept;
    bool upsert(const Note& note);
    bool remove(std::string_view id);
    const std::filesystem::path& path() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
