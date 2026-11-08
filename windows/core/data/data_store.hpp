#pragma once
#include "core/data/json.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
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
// Foundation UUID(uuidString:)?.uuidString: the canonical uppercase form, or
// nullopt when the text is not 8-4-4-4-12 hexadecimal digits (either case).
std::optional<std::string> canonicalUUID(std::string_view value);
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
template <std::size_t A, std::size_t B>
constexpr std::array<std::string_view, A + B> joinedModules(const std::array<std::string_view, A>& first, const std::array<std::string_view, B>& second) {
    std::array<std::string_view, A + B> out{};
    for (std::size_t i = 0; i < A; ++i) out[i] = first[i];
    for (std::size_t i = 0; i < B; ++i) out[A + i] = second[i];
    return out;
}
struct ModuleLayout {
    static constexpr std::array<std::string_view, 17> configurable{"notes", "fileShelf", "clipboard", "archive", "mediaAssembly",
        "minigame", "nowPlaying", "volume", "projection", "reader", "workMode", "calendar", "map", "eventLog", "profile", "account", "power"};
    // HUDDesktopWatchNavigation.fixedModules (Sources/HUDNavigationTarget.swift:59):
    // the modules outside Display > Modules, always in the navigation.
    static constexpr std::array<std::string_view, 6> fixed{"system", "display", "hotkeys", "about", "storage", "activityMonitor"};
    // HUDOverlayLayout.openableModules: every module the navigation can open,
    // in navigation order (fixed, then configurable). Add App is an action,
    // never a place to open on.
    static constexpr auto openable = joinedModules(fixed, configurable);
    static bool isOpenable(std::string_view module) noexcept;
    // A HUDModule raw value: an openable module or Add App.
    static bool isModule(std::string_view module) noexcept;
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

// Swift String(prefix(n)): the first n extended grapheme clusters. The owner
// passes the ICU rule the profile owner uses (native::graphemePrefix): the
// app gives it to its SettingsStore, so every save stores the cut text as the
// Mac does. The data layer has no ICU, and without a rule text is kept whole.
using CharacterPrefix = std::function<std::string(std::string_view, std::size_t)>;
// Mac HUDOverlayLayout (Sources/HUDOverlayLayout.swift): Display > HUD
// elements (the title, the Sanity gauge and the facility alert tags: their
// place, size, Show switches and the title's text) and Display > Open on.
// Stored as one object under "hudOverlayLayout", removed while it holds the
// defaults. Reading mirrors Swift JSONDecoder on its Stored form: every field
// is optional and null reads as missing, but a field of the wrong type (or a
// stored value that is not an object) drops the WHOLE layout to the defaults;
// an unknown openModule is no error and reads as nil. The rest of the
// settings record always loads (validateSettings never checks this key).
struct OverlayLayout {
    enum class Element : std::uint8_t { title, sanity, alerts };
    // An offset in core-plane points (the 1000 x 640 plane, y down) from the
    // element's authored anchor, and its size (1 = as designed). Normalized:
    // x and y clamp to -1000...1000 (non-finite 0), size to 0.5...2 (non-finite 1).
    struct Placement {
        double x{}, y{}, size{1};
        Placement normalized() const noexcept;
        bool operator==(const Placement&) const = default;
    };
    static constexpr std::string_view key{"hudOverlayLayout"};
    static constexpr double minimumSize{.5}, maximumSize{2}, offsetLimit{1000};
    static constexpr std::size_t textLimit{24};
    static constexpr std::string_view defaultTitle{"ENDFIELDHUD"}, defaultSubtitle{"SYSTEM INTERFACE"};
    Placement title, sanity, alerts;
    bool showsTitle{true}, showsSanity{true};
    std::string titleText, subtitleText; // one string for every language; empty shows the default
    std::optional<std::string> openModule; // nil opens on the module last opened
    Placement& operator[](Element) noexcept;
    const Placement& operator[](Element) const noexcept;
    std::string_view shownTitle() const noexcept { return titleText.empty() ? defaultTitle : std::string_view(titleText); }
    std::string_view shownSubtitle() const noexcept { return subtitleText.empty() ? defaultSubtitle : std::string_view(subtitleText); }
    bool isDefault() const { return *this == OverlayLayout{}; }
    // cleanText: split on newlines, trim each line's spaces, drop empty lines,
    // join with one space, keep at most textLimit Characters.
    static std::string cleanText(std::string_view, const CharacterPrefix& prefix = {});
    OverlayLayout normalized(const CharacterPrefix& prefix = {}) const; // openModule outside ModuleLayout::openable is nil
    static std::optional<OverlayLayout> decode(const Json&); // nullopt where JSONDecoder throws
    Json encode() const; // every field, except a nil openModule
    bool operator==(const OverlayLayout&) const = default;
};

// The Restore defaults scopes (Mac HUDSettingsScope, Sources/Models.swift):
// the System page; the Display page and every page under it (Open on,
// Modules, HUD elements, App / menu bar icon, Battery alert with the bar);
// Display > HUD elements alone.
enum class SettingsScope : std::uint8_t { system, display, hudElements };
// Every saved key and the Restore that resets it, in one list (Mac
// HUDSettingsScope.fields and unscopedFields), so a new key joins a Restore by
// adding one line. hudElements keys are reset by Display's Restore too, and
// HUD elements keeps hudOverlayLayout's Open on. nullopt: no Restore touches
// the key (the hotkey has its own Restore; the others are not settings rows).
// A key not listed here (another build's) is never reset either.
struct ScopedSetting { std::string_view key; std::optional<SettingsScope> scope; };
inline constexpr std::array<ScopedSetting, 50> scopedSettings{{
    // System, plus the Windows display choice beside the Mac one.
    {"language", SettingsScope::system}, {"launchAtLogin", SettingsScope::system}, {"closeOnFocusLost", SettingsScope::system},
    {"openOnActiveDisplay", SettingsScope::system}, {"hudDisplayUUID", SettingsScope::system}, {"hudDisplayName", SettingsScope::system},
    {"windowsDisplayID", SettingsScope::system}, {"windowsDisplayName", SettingsScope::system},
    // Display
    {"hudScale", SettingsScope::display}, {"hudOffsetX", SettingsScope::display}, {"hudOffsetY", SettingsScope::display},
    {"parallaxIntensity", SettingsScope::display}, {"perspectiveIntensity", SettingsScope::display}, {"backgroundDarkness", SettingsScope::display},
    {"blurAmount", SettingsScope::display}, {"theme", SettingsScope::display}, {"clockFormat", SettingsScope::display},
    {"clockStyle", SettingsScope::display}, {"centerLogo", SettingsScope::display}, {"centerLogoRevision", SettingsScope::display},
    {"accentHex", SettingsScope::display}, {"ambientAnimation", SettingsScope::display}, {"reduceMotion", SettingsScope::display},
    {"lowPowerVisualMode", SettingsScope::display}, {"recordingMode", SettingsScope::display}, {"dailyBootLog", SettingsScope::display},
    {"showsCloseHint", SettingsScope::display},
    // App / menu bar icon
    {"applicationIcon", SettingsScope::display},
    // Battery alert, with the bar's position, size and length
    {"alertMetric", SettingsScope::display}, {"displayMode", SettingsScope::display}, {"batteryAlertsEnabled", SettingsScope::display},
    {"displayDuration", SettingsScope::display}, {"placement", SettingsScope::display}, {"customScreenID", SettingsScope::display},
    {"customPositionX", SettingsScope::display}, {"customPositionY", SettingsScope::display}, {"scale", SettingsScope::display},
    {"chargeBarLength", SettingsScope::display},
    // Modules
    {"hudModuleOrder", SettingsScope::display}, {"hudHiddenModules", SettingsScope::display},
    // Open on and HUD elements
    {"hudOverlayLayout", SettingsScope::hudElements}, {"facilityAlerts", SettingsScope::hudElements},
    // No Restore: the hotkey (windowsSummonHotkeyDisabled is its derived
    // mirror, never in the record), the device popup, the Storage disk, the
    // Projection pen, the minigame best score and the boot log's last day.
    {"summonShortcut", std::nullopt}, {"summonHotkeyEnabled", std::nullopt}, {"windowsSummonShortcut", std::nullopt},
    {"devicePopupEnabled", std::nullopt}, {"storageVolume", std::nullopt}, {"projection.settings.v1", std::nullopt},
    {"orbipom.bestScore.v1", std::nullopt}, {"dailyBootLogLastDay", std::nullopt},
}};

// The record's open string choices a reader knows, default first: the Mac
// HUDClockStyle, HUDCenterLogo, HUDChargeMetric and DisplayMode raw values.
// A saved value outside its list (another build's) loads and round-trips
// unchanged, and knownOr reads it as the list's first entry, as the Mac
// rawValue decoders fall back to the default. (theme, language, clockFormat
// and placement are closed sets validateSettings already checks.)
struct SettingsChoices {
    static constexpr std::array<std::string_view, 5> clockStyles{"digital", "split", "dial", "rail", "stacked"};
    static constexpr std::array<std::string_view, 5> centerLogos{"endfield", "rhodesIsland", "babel", "rhineLab", "custom"};
    static constexpr std::array<std::string_view, 6> alertMetrics{"battery", "ram", "cpu", "network", "disk", "gpu"};
    static constexpr std::array<std::string_view, 3> displayModes{"whenChargingStarts", "always", "never"};
};
std::string_view knownOr(std::string_view value, std::span<const std::string_view> choices) noexcept;

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
// read by a Mac, so it never writes the Mac's displayModeExtension/
// alertMetricExtension pair. Windows 1.2.0 round-trips every new key.
// D2: summonHotkeyEnabled is the switch; the store migrates a 1.2.0
// windowsSummonHotkeyDisabled on load (the Mac key wins when both exist) and,
// for Windows 1.2.0 readers, writes it back as a mirror (true while the hotkey
// is off, removed while it is on). The legacy key is never in `fields`.
struct Settings {
    Json fields{Json::Object{}};
    static Settings defaults();
    using Scope = SettingsScope;
    // AppConfiguration.restoring(_:): this record with only `scope`'s keys
    // (scopedSettings) copied from defaults(), a key absent there removed;
    // HUD elements keeps the layout's Open on. Every other key is kept.
    Settings restoring(Scope scope) const;
    // `restoring(scope) == self` on typed values, never JSON tokens, as
    // AppConfiguration.normalized compares them: numbers by value (1 and 1.0
    // match), Booleans through their readers (a wrong type is the default),
    // the layouts through moduleLayout() and overlayLayout(), the length and
    // the icon through their accessors ("battery" is the watch), accentHex
    // through normalizedThemeHex, the SettingsChoices keys through knownOr,
    // the UUIDs canonical (an invalid one is nil, and the display name with
    // it), and an absent key as its default. An icon name outside the roster
    // compares as saved: the roster is a resource, not part of this layer.
    bool isDefault(Scope scope) const;
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
    // Mac 1.3.1 keys (Sources/Models.swift). chargeBarLength is the compact
    // battery bar's length as a fraction of its display's usable width; absent
    // (nil) keeps the classic 264 pt bar and it is not in defaults(). A finite
    // value above 0 reads as min(1, v); anything else, another type included,
    // reads as nil, and the next save stores it that way. showsCloseHint
    // (Display > Close hint) is in defaults(); absent or not a Boolean reads
    // as on (the file still loads: validateSettings leaves its type to this
    // reader), and the next save stores the Boolean read.
    std::optional<double> chargeBarLength() const;
    void setChargeBarLength(std::optional<double>); // removed when nil, as the Mac does
    bool showsCloseHint() const;
    // HUDApplicationIcon(savedValue:): 1.3.0's retired "battery" icon reads as
    // "watch" (loading rewrites nothing; the next save of any setting stores
    // "watch"); absent or not a string reads as "endfield". Any other name is
    // returned as saved: the icon roster lives with the resources.
    std::string applicationIcon() const;
    // The decoded and normalized layout (the defaults when absent or invalid).
    OverlayLayout overlayLayout(const CharacterPrefix& prefix = {}) const;
    void setOverlayLayout(const OverlayLayout&, const CharacterPrefix& prefix = {}); // normalized; removed at the defaults
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
// Mac ProjectionSettings.init's reading of a stored dictionary.
ProjectionSettings decodeProjectionSettings(const Json&);
Json encodeProjectionSettings(const ProjectionSettings&);
class SettingsStore final {
public:
    // `characters` cuts the HUD elements text to OverlayLayout::textLimit
    // Characters on every save (CharacterPrefix); without it text is kept whole.
    explicit SettingsStore(const std::filesystem::path& appRoot, CharacterPrefix characters = {});
    const Settings& value() const noexcept { return value_; }
    bool update(const Settings& value); // false for an unchanged record; no write
    const std::filesystem::path& path() const noexcept { return path_; }
    // AppDelegate's separate "hasLaunched" defaults key. It is kept in the
    // envelope beside (never inside) the preference record, so Restore
    // Defaults cannot repeat first-run onboarding.
    bool hasLaunched() const noexcept { return launched_; }
    bool markLaunched(); // false when already marked; no write
    // Two more Mac defaults keys outside AppConfiguration, kept in the envelope
    // the same way: no Restore touches them and saving one changes no setting.
    // "hudLastOpenedModule" (HUDOverlayLayout.lastModuleKey): the module the
    // HUD last opened on, map when missing or not a module. A save writes only
    // when the saved string differs (false: no write) and refuses a name that
    // is not a module; diagnostic and test runs keep it in memory instead.
    std::string lastOpenedModule() const;
    bool saveLastOpenedModule(std::string_view module);
    // "hudOverlaySanityMigrated" (ConfigurationStore.sanityMigrationKey): set
    // once by the hidden-Sanity migration; any saved value counts as set.
    bool sanityMigrated() const noexcept;
    bool markSanityMigrated(); // false when already set; no write
private:
    std::filesystem::path path_;
    CharacterPrefix characters_;
    Settings value_;
    Json envelope_{Json::Object{}};
    std::optional<std::string> persisted_;
    bool launched_{};
    void write(Json envelope, const Settings& value); // version + record + the keys beside it
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
