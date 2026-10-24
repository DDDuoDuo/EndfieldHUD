#pragma once
#include "core/data/data_store.hpp"
#include "core/localization.hpp"
#include "core/scene.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The Personal ID record model: the in-memory counterpart of the Mac's
// UserProfileStore (Sources/UserProfileStore.swift: profile, update {...},
// removeImage, importImage's record half, setWorkSeconds, observe) over the
// owner's ProfilePersistence (modules/profile_service.hpp: profile.json on the
// shared utility worker). The page itself is modules/profile_canvas.hpp (Mac
// 1.3.0 PersonalProfileCanvas); this file holds no UI state any more (the
// v1.2.0 Windows actions, pop-overs, sliders, menu rows and inline error line
// of PersonalProfileCanvasAction / PersonalProfileSliderValue are gone with the
// Mac's). No IO, clock or timer here.
namespace endfield::modules {
using PersonalProfile=ehud::data::Profile;
// PersonalProfileField (PersonalProfileCanvas.swift:6-67), raw values as ids.
enum class ProfileField {name,tag,playerID,introduction,awakeningDate,birthday,permissionLevel,explorationLevel,operatorsCount,weaponsCount,archivesCount,
    backgroundWidth,backgroundZoom,backgroundOffsetX,backgroundOffsetY,thumbnailZoom,thumbnailOffsetX,thumbnailOffsetY,avatarZoom,avatarOffsetX,avatarOffsetY};
enum class ProfileImageKind {avatar,background};
// UserProfileStoreError plus the storage messages (the v1.2.0 date-format
// errors are gone with the Mac's inline error line: an invalid date is now
// discarded, MODS-828/842).
enum class ProfileFailure {name,tag,value,image,imageTooLarge,imageDimensions,record,newerVersion,changedOnDisk,persistence,unavailable};
// Localized exactly like the Mac errorDescription. Persistence appends detail.
std::string profileFailureMessage(ProfileFailure,core::Language,std::string_view detail={});
class ProfileError final:public std::runtime_error {
public:
    explicit ProfileError(ProfileFailure failure,std::string detail={});
    ProfileFailure code()const noexcept{return code_;}
    const std::string&detail()const noexcept{return detail_;}
private:ProfileFailure code_;std::string detail_;
};
// error.localizedDescription of anything a store call throws (ProfileError,
// StoreError, other): the text the canvas toasts.
std::string profileErrorText(const std::exception&,core::Language);
// Swift Character limits and Foundation character sets are injected. No byte
// or UTF16 truncation replaces grapheme segmentation. All strings are UTF8.
struct ProfileTextRules {
    std::function<std::size_t(std::string_view)>characters;          // String.count
    std::function<std::string(std::string_view,std::size_t)>prefix;  // String(prefix(n))
    std::function<std::string(std::string_view,bool includeNewlines)>trim; // .whitespaces[AndNewlines]
    std::function<std::string(std::string_view)>removeNewlines;       // filter { !$0.isNewline }
    std::function<bool(std::string_view)>hasControl,hasWhitespace;   // .controlCharacters, .whitespacesAndNewlines
    std::function<std::string(std::string_view)>uppercase;            // String.uppercased(), full Unicode mapping
};
struct ProfileDateRules {
    // Gregorian local noon, years 1..9999 (Julian before the 1582 cutover,
    // exactly like Foundation's .gregorian calendar); never the Calendar module.
    std::function<std::optional<double>(int year,int month,int day)>noon;
    std::function<std::string(double foundationSeconds)>format; // yyyy/MM/dd
};
std::string_view profileFieldID(ProfileField)noexcept;
std::optional<ProfileField>profileField(std::string_view)noexcept;
std::string profileFieldTitle(ProfileField,core::Language);          // PersonalProfileField.title
bool profileGeometryField(ProfileField)noexcept;                    // isGeometry
bool profileNumericField(ProfileField)noexcept;                     // isNumeric (levels and counters)
// PersonalProfileField.textLimit (Mac 1.3.0): name 20, tag 11 (room for a
// typed "#"), playerID 64, introduction 150, dates 10, levels 2, counters 5.
std::optional<std::size_t>profileTextLimit(ProfileField)noexcept;
// PersonalProfileField.syncField: the Account field chip that lets an
// Endfield sync own this value (introduction, birthday and geometry: none).
std::optional<ehud::data::ProfileSyncField>profileSyncField(ProfileField)noexcept;
// UserProfile.resolvedAccent: a saved valid hex, otherwise the caller's HUD accent.
std::array<double,3>profileAccent(const PersonalProfile&,std::array<double,3>hudAccent)noexcept;
// Swift Int(String)/Double(String) acceptance: C-locale strtod grammar with
// hexadecimal, inf/nan; overflow is infinite and underflow is zero/subnormal.
// Never locale dependent.
std::optional<std::int64_t>swiftInteger(std::string_view)noexcept;
std::optional<double>swiftDouble(std::string_view)noexcept;
// Every field except retained unknown JSON members.
bool sameProfileValues(const PersonalProfile&,const PersonalProfile&)noexcept;
PersonalProfile normalizedProfile(PersonalProfile,const ProfileTextRules&);
void validatePersonalProfile(const PersonalProfile&,const ProfileTextRules&); // throws ProfileError
// PersonalProfileCanvas.parseAwakeningDate / parseBirthday ("-" reads as "/").
std::optional<double>profileParseAwakeningDate(std::string_view,const ProfileDateRules&);
std::optional<std::pair<int,int>>profileParseBirthday(std::string_view);
// PersonalProfileCanvas.value(_:in:) (geometry fields "%.2f").
std::string profileValue(ProfileField,const PersonalProfile&,const ProfileDateRules&);
// PersonalProfileCanvas.geometryValue / applyGeometry (offsets x 100 as slider values).
double profileGeometryValue(ProfileField,const PersonalProfile&)noexcept;
void profileApplyGeometry(ProfileField,double,PersonalProfile&)noexcept;

struct ProfilePersistence {
    // Synchronous acceptance of one complete normalized record; returns the
    // canonical snapshot the owner will persist. The owner may queue file IO on
    // the shared utility worker and report a later failure separately. Throw
    // ProfileError/StoreError to reject. Calls are serialized on the owner
    // thread; callbacks must not destroy this state.
    std::function<PersonalProfile(const PersonalProfile&)>commit;
};
// UserProfileStore in memory. A store without persistence (the load failed:
// the Mac canvas's nil store) refuses every update with
// ProfileFailure::unavailable. Observers run after each adopted change, in
// registration order (UserProfileStore.observe).
class ProfileState final {
public:
    ProfileState(PersonalProfile,ProfileTextRules,ProfileDateRules,ProfilePersistence={});
    const PersonalProfile&profile()const noexcept{return profile_;}
    const ProfileTextRules&text()const noexcept{return text_;}
    const ProfileDateRules&dates()const noexcept{return dates_;}
    bool persistent()const noexcept{return static_cast<bool>(persistence_.commit);}
    // The card fields the account sync owns (Mac gameSyncedFields): the store
    // refuses local edits of them (ProfileService::accept), the canvas locks them.
    ehud::data::ProfileSyncFields syncLocked()const noexcept{return locked_;}
    void setSyncLocked(ehud::data::ProfileSyncFields);
    // A store/account change adopted as committed (official game sync).
    void refresh(PersonalProfile);
    // WM_TIMECHANGE/WM_SETTINGCHANGE: a new immutable zone snapshot. The stored
    // instants are unchanged; only the shown yyyy/MM/dd and later parsing move.
    void setDateRules(ProfileDateRules);
    // UserProfileStore.update { ... }: normalizes, validates, skips an
    // identical record (false), commits and notifies. Throws on rejection.
    template<class F>bool update(F&&mutate){auto next=profile_;mutate(next);return commit(std::move(next));}
    bool commit(PersonalProfile);
    // UserProfileStore.removeImage: avatar zoom 1 / offsets 0; background
    // width 600, zooms 1, offsets 0 (also the thumbnail).
    bool removeImage(ProfileImageKind);
    // importImage's record half: the managed filename (an avatar resets its
    // crop unless preserved: the account's game avatar keeps it).
    bool imageImported(ProfileImageKind,std::string filename,bool preserveAvatarCrop=false);
    // UserProfileStore.setWorkSeconds: absolute, finite and never lower.
    bool setWorkSeconds(double absoluteSeconds);
    std::uint64_t observe(std::function<void()>);
    void removeObserver(std::uint64_t)noexcept;
    std::uint64_t revision()const noexcept{return revision_;}
private:
    PersonalProfile profile_;ProfileTextRules text_;ProfileDateRules dates_;ProfilePersistence persistence_;
    ehud::data::ProfileSyncFields locked_{};std::uint64_t revision_{1},nextObserver_{1};
    std::vector<std::pair<std::uint64_t,std::function<void()>>>observers_;
    void adopt(PersonalProfile);void notify();
};
}
