#pragma once
#include "app/overlay_host.hpp"
#include "core/localization.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "modules/profile_presentation.hpp"
#include "native/layer_image_source.hpp"
#include "native/profile_editor_field.hpp"
#include "native/profile_image.hpp"
#include "native/profile_scene.hpp"
#ifdef _WIN32
namespace endfield::tools {
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
    // silhouette / no backdrop. Large avatars keep a 1024 px working copy here.
    void setAvatar(std::shared_ptr<const native::ProfileDecodedImage>,double time);
    void setBackground(std::shared_ptr<const native::ProfileDecodedImage>,double time);
    // The owner mutated ProfileState outside this page (account sync, lock,
    // image import, persistence failure, Work Mode checkpoint).
    void stateChanged(double time);
    void setOverlayVisible(bool,double time); // initially true
    std::optional<double>nextWakeTime()const noexcept;
    void wake(double time);
    void update(const core::Matrix4& sourceCenter,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double time)const;
    bool covers(core::Point logical)const;
    bool capturesPointer()const noexcept;  // an open popover consumes every click
    bool pointerLocked()const noexcept;    // slider drag, editor or text selection holds the plane
    bool pointer(const app::PointerEvent&,double time);
    bool key(const app::KeyEvent&,double time);
    bool filterKey(const app::NativeMessage&);
    bool message(const app::NativeMessage&,double time);
    void focus(bool,double time);
    void cancelInteraction(double time);
    // The page is the shown, requested module with the overlay visible (the
    // source interaction's active flag): picker requests run only then.
    bool active()const noexcept;
    bool editing()const noexcept;
    std::optional<modules::ProfileField>editingField()const noexcept;
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
