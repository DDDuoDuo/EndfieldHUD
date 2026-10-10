#pragma once
#include "core/data/json.hpp"
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

struct Settings {
    Json fields{Json::Object{}};
    static Settings defaults();
    std::string string(std::string_view key) const;
    double number(std::string_view key) const;
    bool boolean(std::string_view key) const;
    void set(std::string key, Json value);
    bool operator==(const Settings&) const = default;
};
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
class ProfileStore final {
public:
    explicit ProfileStore(const std::filesystem::path& appRoot);
    const Profile& value() const noexcept { return value_; }
    bool update(const Profile& value);
    // The account owner derives this from enabled sync + linked Endfield role.
    // Cached/reconnecting roles stay locked; the store creates no account timer.
    void setProfileSyncLocked(bool locked) noexcept { syncLocked_ = locked; }
    bool profileSyncLocked() const noexcept { return syncLocked_; }
    bool updateFromGame(const Profile& value);
    const std::filesystem::path& path() const noexcept { return path_; }
    std::filesystem::path imagePath(std::string_view filename) const;
private:
    std::filesystem::path path_;
    Profile value_;
    Json envelope_{Json::Object{}};
    std::optional<std::string> persisted_;
    bool syncLocked_{};
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
