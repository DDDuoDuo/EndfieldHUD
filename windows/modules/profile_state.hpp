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

namespace endfield::modules {
using PersonalProfile=ehud::data::Profile;
enum class ProfileField {name,tag,playerID,introduction,awakeningDate,birthday,permissionLevel,explorationLevel,operatorsCount,weaponsCount,archivesCount,
    backgroundWidth,backgroundZoom,backgroundOffsetX,backgroundOffsetY,thumbnailZoom,thumbnailOffsetX,thumbnailOffsetY,avatarZoom,avatarOffsetX,avatarOffsetY};
enum class ProfilePopover {identity,background,portrait,themeColor};
enum class ProfileImageKind {avatar,background};
// UserProfileStoreError plus the canvas' own date/storage messages.
enum class ProfileFailure {name,tag,value,image,imageTooLarge,imageDimensions,record,newerVersion,changedOnDisk,persistence,unavailable,awakeningDate,birthday};
// Localized exactly like the Mac errorDescription. Persistence appends detail.
std::string profileFailureMessage(ProfileFailure,core::Language,std::string_view detail={});
class ProfileError final:public std::runtime_error {
public:
    explicit ProfileError(ProfileFailure failure,std::string detail={});
    ProfileFailure code()const noexcept{return code_;}
    const std::string&detail()const noexcept{return detail_;}
private:ProfileFailure code_;std::string detail_;
};
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
std::string profileFieldTitle(ProfileField,core::Language);
bool profileGeometryField(ProfileField)noexcept;
bool profileZoomField(ProfileField)noexcept;
bool profileNumericField(ProfileField)noexcept;
bool profileDateField(ProfileField)noexcept;
std::optional<std::size_t>profileTextLimit(ProfileField)noexcept;
core::Rect profileFieldRect(ProfileField);
std::span<const std::string_view>profileThemePresets()noexcept; // HUDSettingsController.presetAccentHexes
// HUDPortraitArtwork.crop: unit-space crop of an image for a target, top-left offsets.
core::Rect profilePortraitCrop(core::Point imageSize,core::Point targetSize,double zoom,core::Point offset)noexcept;
core::Rect profileBackgroundRect(const PersonalProfile&)noexcept;
// UserProfile.resolvedAccent: a saved valid hex, otherwise the caller's HUD accent.
std::array<double,3>profileAccent(const PersonalProfile&,std::array<double,3>hudAccent)noexcept;
// Swift Int(String)/Double(String) acceptance: C-locale strtod grammar with
// hexadecimal, inf/nan; overflow is infinite and underflow is zero/subnormal.
// Exposed for the editor and tests; never locale dependent.
std::optional<std::int64_t>swiftInteger(std::string_view)noexcept;
std::optional<double>swiftDouble(std::string_view)noexcept;
// Every field except retained unknown JSON members.
bool sameProfileValues(const PersonalProfile&,const PersonalProfile&)noexcept;
PersonalProfile normalizedProfile(PersonalProfile,const ProfileTextRules&);
void validatePersonalProfile(const PersonalProfile&,const ProfileTextRules&); // throws ProfileError
// HUDPersonalProfileInteraction.normalizeEditor while not composing: tag drops
// one leading "#" Character and text fields keep their Character prefix.
std::string profileEditorText(ProfileField,std::string_view,const ProfileTextRules&);

struct ProfileAction {std::string id,label;core::Rect rect;std::optional<ProfileField>field;bool operator==(const ProfileAction&)const=default;};
struct ProfileSlider {ProfileField field;std::string label;core::Rect rect;double value{},minimum{},maximum{},step{};std::string valueDescription;bool operator==(const ProfileSlider&)const=default;};
struct ProfileRequest {
    enum class Kind {edit,chooseAvatar,chooseBackground,chooseColor};
    Kind kind;std::optional<ProfileField>field;core::Rect rect;std::string text;std::array<double,3>rgb{};
};
struct ProfilePersistence {
    // Synchronous acceptance of one complete normalized record; returns the
    // canonical snapshot the owner will persist. The owner may queue file IO on
    // the shared utility worker and report a later failure with
    // persistenceFailed(). Throw ProfileError/StoreError to reject. Calls are
    // serialized on the owner thread; callbacks must not destroy this state.
    std::function<PersonalProfile(const PersonalProfile&)>commit;
    // Source workSeconds closure: the live absolute Work Mode total, read only
    // when the hours caption is rebuilt (content events and the 30 s refresh).
    std::function<double()>workSeconds;
};
// Exact PersonalProfileCanvas editing, popover and slider model. The account
// owner refreshes snapshots and the sync lock explicitly. Text visibility and
// popovers are session-only; this class performs no IO, owns no clock/timer.
class ProfileState final {
public:
    ProfileState(PersonalProfile,ProfileTextRules,ProfileDateRules,ProfilePersistence={},core::Language=core::Language::english);
    const PersonalProfile&profile()const noexcept{return profile_;}
    const PersonalProfile&preview()const noexcept{return preview_;}
    bool canEdit(ProfileField)const noexcept;
    const ProfileTextRules&text()const noexcept{return text_;}
    bool syncLocked()const noexcept{return locked_;}
    // A store/account change. A slider drag keeps its in-memory value.
    void refresh(PersonalProfile,bool syncLocked);
    void setSyncLocked(bool);
    void setLanguage(core::Language);core::Language language()const noexcept{return language_;}
    // WM_TIMECHANGE/WM_SETTINGCHANGE: a new immutable zone snapshot. The stored
    // instants are unchanged; only the shown yyyy/MM/dd and later parsing move.
    void setDateRules(ProfileDateRules);
    void setHudAccent(std::array<double,3>);std::array<double,3>accent()const noexcept;
    // Work duration refresh follows the source 30 s repeating timer while
    // active, as a caller deadline on the shared clock.
    void activate(double now);void deactivate();bool active()const noexcept{return active_;}
    std::optional<double>nextWorkRefresh()const noexcept;bool wake(double now);
    bool refreshWork(); // refreshWorkDuration: true when the shown caption changed
    bool commit(ProfileField,std::string_view);
    bool setThemeColor(std::optional<std::string>);
    bool setCustomColor(double red,double green,double blue); // sRGB 0...1
    bool restoreImage(ProfileImageKind);
    // Image import is an owner/worker operation. Begin clears the canvas error,
    // completion supplies the managed filename (avatar resets its crop unless
    // preserved); failure shows the store's localized message.
    void beginImageImport();bool imageImported(ProfileImageKind,std::string filename,bool preserveAvatarCrop=false);
    void imageImportFailed(ProfileFailure,std::string_view detail={});
    void persistenceFailed(ProfileFailure,std::string_view detail={});
    // UserProfileStore.setWorkSeconds: absolute, finite and never lower. The
    // source logs a rejected checkpoint; it never becomes a canvas error.
    bool setWorkSeconds(double absoluteSeconds);
    bool perform(std::string_view);
    bool mouseDown(core::Point);void mouseDragged(core::Point);void mouseUp();
    bool setSlider(ProfileField,double);bool nudgeSlider(double);void dismissPopover();
    bool textHidden()const noexcept{return hidden_;}bool dragging()const noexcept{return dragged_.has_value();}
    std::optional<ProfileField>draggedField()const noexcept{return dragged_;}
    std::optional<ProfilePopover>popover()const noexcept{return popover_;}std::optional<core::Rect>popoverBounds()const noexcept;
    std::span<const ProfileAction>actions()const noexcept{return actions_;}std::span<const ProfileSlider>sliders()const noexcept{return sliders_;}
    bool containsPopoverPoint(core::Point)const noexcept;
    std::optional<ProfileRequest>takeRequest();
    const std::optional<std::string>&error()const noexcept{return error_;}
    std::string accessibilityStatus()const;
    std::string value(ProfileField)const;
    std::string value(ProfileField,const PersonalProfile&)const; // same formatting for another snapshot
    std::string formattedHours()const;
    const std::string&shownHours()const noexcept{return shownHours_;}
    // revision: any visible change; geometryRevision: background/crop values;
    // workRevision: only the construction-hours caption.
    std::uint64_t revision()const noexcept{return revision_;}
    std::uint64_t geometryRevision()const noexcept{return geometryRevision_;}
    std::uint64_t workRevision()const noexcept{return workRevision_;}
    // Source animation events: setPopover (open/switch), dismissPopover with a
    // departing copy, and the text-visibility toggle.
    std::uint64_t popoverOpens()const noexcept{return opens_;}
    std::uint64_t popoverDismissals()const noexcept{return dismissals_;}
    std::uint64_t visibilityToggles()const noexcept{return visibilityToggles_;}
private:
    PersonalProfile profile_,preview_;ProfileTextRules text_;ProfileDateRules dates_;ProfilePersistence persistence_;core::Language language_;
    std::array<double,3>hudAccent_{250./255,212./255,31./255};
    bool locked_{},active_{},hidden_{};std::optional<ProfilePopover>popover_;std::optional<ProfileField>dragged_,selected_;
    std::optional<ProfileRequest>request_;std::optional<std::string>error_;std::vector<ProfileAction>actions_;std::vector<ProfileSlider>sliders_;
    std::optional<double>workDeadline_;std::string shownHours_;
    std::uint64_t revision_{1},geometryRevision_{1},workRevision_{1},opens_{},dismissals_{},visibilityToggles_{};
    template<class F>bool update(F&&);void adopt(PersonalProfile);void showError(std::string);bool commitNumber(ProfileField,double);
    void rebuild();void open(ProfilePopover);void moveSlider(core::Point);void restoreFromCommitted();
};
}
