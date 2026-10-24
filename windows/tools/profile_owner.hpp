#pragma once
// The design headers first: rpcndr.h (through windows.h) defines `small`.
#include "tools/hud_design_owner.hpp"
#include "app/utility_executor.hpp"
#include "modules/id_card_binding.hpp"
#include "modules/profile_artwork.hpp"
#include "modules/profile_canvas.hpp"
#include "modules/profile_events.hpp"
#include "modules/profile_service.hpp"
#include "modules/work_mode.hpp"
#include "modules/work_mode_profile_hours.hpp"
#include "native/layer_image_source.hpp"
#include "native/profile_image.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace endfield::tools {
enum class IdCardTextureKind {avatar,background,hover};
// One bottom-left card texture in the source renderer's texturePixels layout
// (straight RGBA8, bottom-origin rows) for desktop.profile.<kind>.
struct IdCardTexture {modules::ProfileImage pixels;std::uint64_t revision{};};
struct ProfileOwnerOptions {
    // The core's options (ApplicationHost::designOwnerOptions: name, routing,
    // raster, clock, change notice, text-field services). The owner adds its
    // hooks (appearance, host events, private message, frame, flush, wake).
    DesignOwnerOptions design;
    std::filesystem::path appRoot;   // explicit absolute versioned data root (Profile/ below it)
    std::filesystem::path resources; // staged windows/resources/profile
    std::u16string timeZone;         // IANA zone; empty uses the current system zone
    UINT pickerMessage{WM_APP+242};  // the deferred colour dialog, run outside page input
    // Asynchronous image chooser (the application's shared file dialog,
    // ApplicationHost::chooseFiles): false when it cannot open now. The
    // completion runs later on the owner thread exactly once with the chosen
    // file (nullopt: cancelled, failed, or closed by cancelPicker()).
    using ImageCompletion=std::function<void(std::optional<std::filesystem::path>)>;
    std::function<bool(modules::ProfileImageKind,ImageCompletion)>requestImage;
    // NSColorPanel counterpart, called from the posted picker message: streams
    // sRGB 0...1 through live while open and returns the final colour
    // (nullopt: cancelled; the owner restores the colour it started from).
    std::function<std::optional<std::array<double,3>>(std::array<double,3>initial,
        const std::function<void(std::array<double,3>)>&live)>chooseColor;
    // Closes an open chooser or colour dialog (it then returns as cancelled).
    std::function<void()>cancelPicker;
    // OrbiPomSession.bestSoFar: the live best while a game runs, else the saved best.
    std::function<std::int64_t()>minigameBest;
    // SystemEventRecorder profileCropChanged {target: background|thumbnail|both}.
    std::function<void(std::string_view target)>cropChanged;
    // Once, after the worker read profile.json: the committed record (its
    // avatarFilename is the account owner's initial known game avatar).
    std::function<void(const modules::PersonalProfile&)>loaded;
    // A later committed avatarFilename change, never the loaded baseline.
    std::function<void(const std::optional<std::string>&)>avatarChanged;
    // IdCardBinding revision or a card texture moved.
    std::function<void()>cardChanged;
};
struct ProfileOwnerStats {
    std::uint64_t portraitJobs{},portraitPublications{},artUploads{},artDraws{};
    bool artResident{},rampResident{};
    std::size_t portraitBytes{},artBytes{};
};
// Production owner of the Personal ID module (modules/profile_canvas.hpp in a
// DesignOwnerCore), the bottom-left ID card binding and the Work Mode
// lifetime hours. It owns the single ProfileService (profile.json on the
// shared UtilityExecutor), ProfileImageService (imports, decodes), the record
// model (ProfileState), WorkModeHours, the profileCropChanged recorder, the
// IdCardBinding and its textures (made on the worker, coalesced), the
// portrait's panning images (HUDPortraitArtwork.panningImage per zoom step,
// on the worker) and the background art, drawn as one owner record inserted
// right after the canvas's art slot (the decoded <= 2048 px photo aspect-
// filled in the picture layer, clipped by the art box and the shared centre
// backdrop ramp). Borrows HWND, executor, rasterizer, the design Context and
// the read-only Work Mode controller; creates no window, timer, thread or
// picker UI. The area forwards the ModuleOwner contract to core() and calls
// upload / collected / release here (the art records).
class ProfileOwner final {
public:
    ProfileOwner(HWND,app::UtilityExecutor&,native::LayerRasterizer&,modules::design::Context&,
        const modules::WorkModeController&,ProfileOwnerOptions);
    ~ProfileOwner();
    ProfileOwner(const ProfileOwner&)=delete;
    ProfileOwner&operator=(const ProfileOwner&)=delete;
    DesignOwnerCore&core()noexcept;
    void start(double time); // schedules the profile load on the worker
    // The HUD shows (the area forwards ModuleOwner::setOverlayVisible to the
    // core and here): a hidden HUD closes the pickers and keeps no art texture.
    void setOverlayVisible(bool,double time);
    // Recording mode for the shell card (the Context carries the canvas's).
    void setRecordingMode(bool,double time);
    void upload(native::Renderer&);
    void collected(native::Renderer&);
    void release(native::Renderer&);
    // Shared services.
    void queueCapacityAvailable();
    void workModeCheckpoint(double absoluteControllerTotal);
    // Account owner (AccountProfileSink): the lock mirrors gameSyncedFields; an
    // update applies the game fields to the current record as an account write.
    void setSyncLocked(ehud::data::ProfileSyncFields,double time);
    void acceptFromGame(std::function<void(modules::PersonalProfile&)>apply,double time);
    std::string importGameAvatar(std::span<const std::uint8_t>png,double time);
    void setTimeZone(std::u16string,double time);
    // Observation.
    bool loaded()const noexcept;
    const std::optional<std::string>&loadError()const noexcept;
    const modules::ProfileState*state()const noexcept;
    modules::ProfileCanvas*canvas()noexcept;
    const modules::ProfileServiceStatus*serviceStatus()const;
    const modules::IdCardBinding*card()const noexcept;
    const IdCardTexture*cardTexture(IdCardTextureKind)const noexcept;
    bool pickerOpen()const noexcept;
    ProfileOwnerStats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
