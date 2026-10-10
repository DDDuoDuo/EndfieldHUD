#pragma once
#include "app/utility_executor.hpp"
#include "modules/id_card_binding.hpp"
#include "modules/profile_artwork.hpp"
#include "modules/profile_events.hpp"
#include "modules/profile_service.hpp"
#include "modules/work_mode.hpp"
#include "modules/work_mode_profile_hours.hpp"
#include "tools/profile_preview.hpp"
#ifdef _WIN32
namespace endfield::tools {
// The app's resolved appearance event (app::ModuleAppearance subset).
struct ProfileOwnerAppearance {
    core::Language language{core::Language::english};
    bool dark{true};
    std::array<double,3>accentSRGB{250./255,212./255,31./255}; // configured HUD accent
    double scale{2};                                           // page raster scale
    bool reduceMotion{};
    bool operator==(const ProfileOwnerAppearance&)const=default;
};
enum class IdCardTextureKind {avatar,background,hover};
// One bottom-left card texture in the source renderer's texturePixels layout
// (straight RGBA8, bottom-origin rows) for desktop.profile.<kind>.
struct IdCardTexture {modules::ProfileImage pixels;std::uint64_t revision{};};
struct ProfileOwnerOptions {
    std::filesystem::path appRoot;   // explicit absolute versioned data root (Profile/ below it)
    std::filesystem::path resources; // staged windows/resources/profile
    std::u16string timeZone;         // IANA zone; empty uses the current system zone
    native::LayerRasterOptions raster;
    UINT textMessage{WM_APP+241};    // projected field TSF notices
    UINT pickerMessage{WM_APP+242};  // deferred picker request, run outside page input
    // Host pickers, called on the owner thread from message(pickerMessage),
    // never re-entered and never while the page is closed. chooseImage returns
    // the chosen file (nullopt: cancelled). chooseColor streams sRGB 0...1
    // through live while open and returns the final colour (nullopt: cancel,
    // which restores the card colour the picker started from). Null pickers
    // ignore the request.
    std::function<std::optional<std::filesystem::path>(modules::ProfileImageKind)>chooseImage;
    std::function<std::optional<std::array<double,3>>(std::array<double,3>initial,
        const std::function<void(std::array<double,3>)>&live)>chooseColor;
    // HUDPersonalProfileInteraction.deactivate: close the open picker (it then
    // returns as cancelled), e.g. native::ProfilePickerSession::cancel.
    std::function<void()>cancelPicker;
    // SystemEventRecorder profileCropChanged {target: background|thumbnail|both}.
    std::function<void(std::string_view target)>cropChanged;
    // Once, after the worker read profile.json: the committed record (its
    // avatarFilename is the account owner's initial known game avatar).
    std::function<void(const modules::PersonalProfile&)>loaded;
    // A later committed avatarFilename change, never the loaded baseline
    // (HypergryphAccountController::profileAvatarChanged).
    std::function<void(const std::optional<std::string>&)>avatarChanged;
    // IdCardBinding revision or a card texture moved: refresh NativeIdCardCaptions,
    // IdCardBinding::apply desktop settings and the desktop.profile.* textures.
    std::function<void()>cardChanged;
    // Content changed outside a frame (worker completion, account sync): schedule one.
    std::function<void()>changed;
};
// Production owner of the Personal Profile module, the bottom-left ID card
// binding and the Work Mode lifetime hours. It owns the single ProfileService
// (profile.json on the shared UtilityExecutor), ProfileImageService, the
// app's ProfileState, the ProfilePreview page, WorkModeProfileHours, the
// profileCropChanged recorder, the IdCardBinding and the card textures
// (generated on the utility worker, coalesced). Methods mirror app::ModuleOwner
// so the host adapter only forwards. Borrows HWND, executor, rasterizer, image
// source (also the page's memoryImages), the read-only Work Mode controller
// and the TSF manager/client; creates no window, timer, thread or picker UI.
// Construct the Work Mode owner with a zero restored total and route its
// absolute checkpoints to workModeCheckpoint(); this owner adds the stored
// lifetime (never a second ProfileStore writer). Owner thread only.
class ProfileOwner final {
public:
    ProfileOwner(HWND,app::UtilityExecutor&,native::LayerRasterizer&,native::LayerImageSource&,
        const modules::WorkModeController&,ProfileOwnerOptions,ITfThreadMgr* borrowedManager=nullptr,TfClientId=TF_CLIENTID_NULL);
    ~ProfileOwner();
    ProfileOwner(const ProfileOwner&)=delete;
    ProfileOwner&operator=(const ProfileOwner&)=delete;
    void start(double time); // schedules the profile load on the worker
    // app::ModuleOwner events.
    void resize(const app::ClientMetrics&);
    void setAppearance(const ProfileOwnerAppearance&,double time);
    void setOverlayVisible(bool,double time);
    void overlayClosing(double time);
    void update(const core::Matrix4& center,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double time)const;
    std::optional<double>nextWakeTime(double now)const;
    bool deadline(double time); // true: the work-hours caption changed
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
    bool covers(core::Point logical)const;
    bool pointerLocked()const;
    bool capturesPointer()const;
    bool pointer(const app::PointerEvent&,double time);
    bool key(const app::KeyEvent&,double time);
    bool filterKey(const app::NativeMessage&);
    bool message(const app::NativeMessage&,double time);
    void focus(bool,double time);
    void cancelInteraction(double time);
    bool finishEditing(double time);  // commits an open field like the source close path
    // Quit/session end, after WorkModeController::shutdown delivered its last
    // checkpoint: commits the field, retries unsaved hours, drains profile.json.
    bool flush(double time);
    // Shared services.
    void queueCapacityAvailable();
    void workModeCheckpoint(double absoluteControllerTotal); // WorkModeHooks::trackedWorkSecondsChanged
    // Account owner (AccountProfileSink): the lock mirrors gameSyncActive; an
    // update applies the game fields to the current record as an account write.
    // Before the profile loads the latest update waits; after a failed load it
    // throws ProfileError(unavailable), like a missing store.
    void setSyncLocked(bool,double time);
    void acceptFromGame(std::function<void(modules::PersonalProfile&)>apply,double time);
    // AccountProfileSink::importGameAvatar: the downsampled PNG becomes the
    // managed avatar synchronously (source importImage on the main thread) and
    // its filename is returned. Throws ProfileError on failure.
    std::string importGameAvatar(std::span<const std::uint8_t>png,double time);
    void setTimeZone(std::u16string,double time); // WM_TIMECHANGE / WM_SETTINGCHANGE
    // Accessibility-equivalent activation (HUDProfileActionButton/slider).
    bool perform(std::string_view actionID,double time);
    bool setSlider(modules::ProfileField,double value,double time);
    // Observation.
    bool loaded()const noexcept;
    const std::optional<std::string>&loadError()const noexcept;
    const modules::ProfileState*state()const noexcept;
    const modules::ProfileServiceStatus*serviceStatus()const;
    const modules::IdCardBinding*card()const noexcept; // null until loaded (packet defaults stay)
    const IdCardTexture*cardTexture(IdCardTextureKind)const noexcept; // null: keep the packet texture
    bool pickerOpen()const noexcept;
    ProfilePreview*page()noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
