#pragma once
#include "app/overlay_host.hpp"
#include "native/app_shortcut_scene.hpp"
#include "native/app_shortcut_service.hpp"
#include "native/projected_editor.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#ifdef _WIN32
namespace endfield::tools {
enum class ShortcutRequestKind {choose,inspect,edit,save,remove,launch};
struct ShortcutRequest {
    std::uint64_t generation{};ShortcutRequestKind kind{};
    std::optional<native::AppShortcutSelection>selection;
    std::optional<modules::ShortcutRecord>record;
    std::optional<modules::ShortcutCandidate>candidate;
    std::optional<std::string>editingID;std::string name,icon;
};
struct AppShortcutPreviewOptions {
    modules::ShortcutFile initial;
    std::optional<std::string>initialError;
    modules::ShortcutAppearance appearance;
    native::LayerRasterOptions raster;
    UINT textMessage{WM_APP+219};
};
// Retained source App Launcher with one borrowed-TSF plain-history name field.
// All OS/repository work is explicit takeRequest/complete*. One request is in
// flight: no service, worker, clipboard read, window, timer or store IO here.
// Root performs launch only after closing the HUD; save/remove completion must
// contain the file actually committed by its one repository worker. Conceal
// invalidates selection/dialog work, but never discards an accepted write or
// an already-dispatched close-first launch. The current shared editor supports
// at most 65,536 UTF16 units; oversized source names reject, never truncate.
// Caller keeps assets/raster/TSF alive, detaches shared composition entries,
// calls release(), then destroys this owner before deactivating TSF.
class AppShortcutPreview final {
public:
    AppShortcutPreview(HWND,native::LayerRasterizer&,const native::NativeAppShortcutAssets&,
        ITfThreadMgr*,TfClientId,AppShortcutPreviewOptions={});
    ~AppShortcutPreview();
    void resize(const app::ClientMetrics&);
    void setAppearance(modules::ShortcutAppearance,double);
    void setLanguage(core::Language,double);void setReduceMotion(bool,double);
    void setOriginalImages(modules::ShortcutOriginalImages);
    void showError(std::optional<std::string>,double);
    void setOverlayVisible(bool,double);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;bool covers(core::Point logicalClientPoint)const;
    bool pointer(const app::PointerEvent&,double);bool wheel(const app::WheelEvent&,double);
    bool scroll(core::Point logicalClientPoint,double logicalDelta,double);
    bool key(const app::KeyEvent&,double);bool key(const app::KeyEvent&,bool modified,double);
    bool filterKey(const app::NativeMessage&);bool message(const app::NativeMessage&,double);
    void focus(bool,double);void cancelInteraction(double);
    bool pointerLocked()const noexcept;bool presentingPanel()const noexcept;
    bool editingName()const noexcept;bool finishEditing(bool commit,double);
    bool perform(const modules::ShortcutAction&,double);
    bool inspectSelection(native::AppShortcutSelection,double);
    void setExternalDrag(bool,double);
    std::optional<ShortcutRequest>takeRequest();
    // Root consumes cancellation even when this module is hidden.
    std::optional<std::uint64_t>takeCancelledRequest()noexcept;
    bool completeCandidate(std::uint64_t,modules::ShortcutCandidate,double);
    bool completeFile(std::uint64_t,modules::ShortcutFile,double);
    bool completeRequest(std::uint64_t,std::optional<std::string>error,double);
    bool busy()const noexcept;
    const modules::AppShortcutState&state()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
