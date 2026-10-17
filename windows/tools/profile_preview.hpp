#pragma once
#include "app/overlay_host.hpp"
#include "app/utility_executor.hpp"
#include "core/localization.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "modules/profile_presentation.hpp"
#include "native/layer_image_source.hpp"
#include "native/notes_clipboard.hpp"
#include "native/profile_editor_field.hpp"
#include "native/profile_image.hpp"
#include "native/profile_scene.hpp"
#ifdef _WIN32
namespace endfield::tools {
// Modifier state of a key event; system is Alt or either Win key.
struct ProfileKeyModifiers {bool shift{},control{},system{};};
struct ProfilePreviewOptions {
    native::LayerRasterOptions raster;     // memoryImages resolves avatar/frame contents
    modules::ProfileAppearance appearance; // dark, render scale, HUD accent
    modules::ProfileContents contents;     // avatar/frame descriptors, background presence
    bool reduceMotion{};
    UINT textMessage{WM_APP+241};
    // Owner fulfils source requests: image chooser + import (then
    // ProfileState::imageImported/Failed), and the card colour picker (then
    // ProfileState::setCustomColor). Neither is called while another is open.
    std::function<void(modules::ProfileImageKind)>chooseImage;
    std::function<void(std::array<double,3>initial)>chooseColor;
    // Optional page imagery. With the borrowed creating-thread image source
    // (also installed as raster.memoryImages) the owner publishes the portrait
    // crop and the accent-tinted avatar frame itself; frameSprite is the
    // straight 254x254 source frame (loadProfileSourceArtwork().frame).
    native::LayerImageSource*images{};
    std::optional<modules::ProfileImage>frameSprite;
    // AppDelegate Edit menu over the open field: Ctrl+X/C/V cut, copy and
    // paste plain text, read or written once per key (no listener, no
    // polling); the field then normalizes it like typing. Null declines those
    // keys (no clipboard access at all).
    std::shared_ptr<native::NotesClipboard>clipboard;
    // Optional shared utility executor: the page portrait crop
    // (HUDPortraitArtwork at 55 pt, about 14 ms for a photo) runs there, one
    // job at a time; the newest finished crop is shown while the next one
    // (the latest zoom/offset, e.g. of a slider drag) is made. Null crops on
    // the calling thread.
    app::UtilityExecutor*executor{};
    // A worker crop arrived outside a frame: schedule one.
    std::function<void()>changed;
};
// What the page keeps resident (ProfilePreview::resources).
struct ProfilePageResources {
    std::size_t sceneParts{},scenePixelBytes{},ownedPhotoBytes{};std::uint64_t sceneLoads{};
    bool gpuResident{};            // the scene's textures/backdrop group are on the renderer
    bool built{};                  // the page artwork exists (shown since the last park, or prewarmed)
    std::uint64_t parks{},cropJobs{},cropPublications{};
    bool cropPending{};            // a portrait crop is on the worker
    std::size_t retainedCropBytes{};
};
// Personal Profile module owner: the app's single ProfileState, the retained
// NativeProfileScene, one projected TSF field at a time, slider drags, popover
// capture and Escape unwinding, plus the separate background plane with its
// 0.24 s section fade/travel. Driven by the shell's continuous clock; the 30 s
// work-hours refresh is exposed through nextWakeTime(), never a timer.
// Borrows HWND, ProfileState, rasterizer and the activated TSF manager/client;
// creates no window, renderer, clock, thread, file picker, store or service.
// Callbacks must not destroy this owner. Destroy before deactivating TSF.
class ProfilePreview final {
public:
    ProfilePreview(HWND,modules::ProfileState&,native::LayerRasterizer&,ProfilePreviewOptions,
        ITfThreadMgr* borrowedManager=nullptr,TfClientId=TF_CLIENTID_NULL);
    ~ProfilePreview();
    void resize(const app::ClientMetrics&);
    void setLanguage(core::Language,double time);
    void setAppearance(modules::ProfileAppearance,double time);
    void setReduceMotion(bool,double time);
    void setContents(modules::ProfileContents,double time);
    void setBackdropImage(std::optional<native::NativeProfileBackdropImage>,double time);
    // Decoded managed images (ProfileImageService). The portrait crop follows
    // zoom/offset, including a slider drag preview; null restores the source
    // silhouette / no backdrop. Large avatars are cropped from the decode
    // service's 1024 px working copy (ProfileDecodedImage::working, made on
    // the worker); a caller-built image without one is reduced here.
    void setAvatar(std::shared_ptr<const native::ProfileDecodedImage>,double time);
    void setBackground(std::shared_ptr<const native::ProfileDecodedImage>,double time);
    // The owner mutated ProfileState outside this page (account sync, lock,
    // image import, persistence failure, Work Mode checkpoint).
    void stateChanged(double time);
    void setOverlayVisible(bool,double time); // initially true
    // HUDModuleContent makes a screen's layers when its module is shown and
    // drops the old screen's layers when another module is shown. The page
    // builds nothing (no raster, crop or upload) until it is first drawn or
    // prewarmed for the first opening. Once the HUD closes
    // (setOverlayVisible(false)) while the page is not shown, it releases its
    // GPU resources (the parts' textures, the backdrop photo texture and group
    // target) and its artwork (the parts' rasters) at the next publication
    // without it (collected) or when the HUD has finished closing (conceal(),
    // the host's overlayConcealed); content events while it is hidden then
    // only update the state. Showing it again rebuilds the same artwork from
    // the kept decoded images and portrait crop (no decode or crop). A module
    // switch inside the open HUD keeps everything.
    void conceal();
    ProfilePageResources resources()const;
    void queueCapacityAvailable(); // the executor has room again (a deferred crop)
    // Launch prewarm (ModuleOwner::prewarm): with the HUD still closed, a
    // first opening that will show Personal Profile builds the page artwork
    // now (rasters and portrait crop, nothing uploaded or shown).
    void prewarm(const core::ModulePresentationSample&);
    std::optional<double>nextWakeTime()const noexcept;
    void wake(double time);
    void update(const core::Matrix4& sourceCenter,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double time)const;
    bool covers(core::Point logical)const;
    bool capturesPointer()const noexcept;  // an open popover consumes every click
    bool pointerLocked()const noexcept;    // slider drag, editor or text selection holds the plane
    bool pointer(const app::PointerEvent&,double time);
    // Source HUDPersonalProfileInteraction.keyDown commits, cancels, unwinds
    // and nudges on unmodified keys only (command/control/option/shift). The
    // first form is for the HUD window's key dispatch: it reads GetKeyState,
    // which Win32 synchronizes with the key message being dispatched. Any
    // other caller (tests, synthetic input) states the modifiers explicitly:
    // off that dispatch a thread's GetKeyState follows the live keyboard of
    // the interactive session.
    bool key(const app::KeyEvent&,double time);
    bool key(const app::KeyEvent&,const ProfileKeyModifiers&,double time);
    bool filterKey(const app::NativeMessage&);
    bool message(const app::NativeMessage&,double time);
    void focus(bool,double time);
    void cancelInteraction(double time);
    // The page is the shown, requested module with the overlay visible (the
    // source interaction's active flag): picker requests run only then.
    bool active()const noexcept;
    // Source accessibility projection (HUDPersonalProfileInteraction project):
    // a 400 x 334 canvas rect -> the logical client bounding rect of its
    // projected corners while the page accepts input; none otherwise.
    std::optional<core::Rect>clientRect(core::Rect canvas)const;
    // HUDProjectedTextView.projectedCursor: true when a logical client point
    // falls inside the open projected field (unprojected through the module
    // plane, the same test as its press) while the page accepts input; the
    // host then shows the system I-beam. Allocates nothing, changes nothing.
    bool textCursorAt(core::Point logicalClient)const noexcept;
    bool editing()const noexcept;
    std::optional<modules::ProfileField>editingField()const noexcept;
    // The open projected field (the source NSTextView accessibility peer):
    // its field, canvas rect and current draft text.
    struct EditingView{modules::ProfileField field;core::Rect rect;std::string text;};
    std::optional<EditingView>editingView()const;
    // UI Automation ValuePattern.SetValue on that peer: replaces the whole
    // draft through the editor's TSF document like typed text, then applies
    // the field's normalization. False without an open field, while marked
    // text is composing, for invalid UTF-8 or past the editor capacity.
    bool setEditingText(std::string_view utf8,double time);
    bool finishEditing(bool commit,double time);
    const modules::ProfileArtwork&artwork()const noexcept;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
