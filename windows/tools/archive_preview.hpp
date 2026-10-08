#pragma once
#include "app/overlay_host.hpp"
#include "native/archive_scene.hpp"
#include "native/projected_editor.hpp"
#include "modules/notes_controls.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#ifdef _WIN32
namespace endfield::tools {
struct ArchiveMediaAction {
    enum class Kind {chooseLocal,chooseShelf,useShelf,playPause,seek};
    Kind kind{};std::string documentID,itemID;std::size_t mediaIndex{},remaining{};
    std::uint64_t generation{};double seconds{};
};
struct ArchivePreviewOptions {
    native::LayerRasterOptions raster;modules::ArchiveViewInput view;
    modules::NotesControlsStrings menuStrings;
    std::string categoryNamePlaceholder{"Category name"};
    std::function<std::string()>newID;
    std::function<double()>foundationNow;
    std::function<std::optional<double>(std::string_view)>parseLocalDate;
    std::function<std::vector<std::string>()>fontFamilies;
    // Pinned source-generated color wheel binding (not a replacement asset).
    ehud::data::Json colorWheelContents;
    // Shared source Generic-RGB -> sRGB color-wheel conversion. Coordinates are
    // normalized wheel points; owner supplies the existing verified helper.
    std::function<core::notes::RGBA(core::Point)>colorAtWheel;
    // Source NSColor device-RGB hue/saturation inverse for the initial marker.
    std::function<core::Point(const core::notes::RGBA&)>colorWheelPoint;
    std::uint32_t editorCapacity{65536};
    UINT textMessage{WM_APP+220},mediaMessage{WM_APP+221};
};
// Archive owner borrowing the shared state/FIFO executor, HWND/rasterizer and
// already-activated TSF manager. No new window, renderer, clock, timer, service,
// picker, media decoder, filesystem or database is created. Callbacks may not
// synchronously destroy/reenter this owner. Remove entries before release;
// destroy fields before the shared TSF manager and drain accepted state saves.
// Full stored bodies are retained. The current painted leaf explicitly rejects
// >editorCapacity UTF-16 units; it never truncates data to that integration limit.
// Source rich title/date single-line inset + natural one-point spacing remains
// an explicit shared-editor layout gap. Body uses the actual natural +1 seam.
class ArchivePreview final {
public:
    ArchivePreview(modules::ArchiveState&,HWND,native::LayerRasterizer&,
        ITfThreadMgr* borrowedManager,TfClientId,ArchivePreviewOptions);
    ~ArchivePreview();
    void resize(const app::ClientMetrics&);void setAppearance(modules::ArchiveAppearance);
    void setReduceMotion(bool,double);void setOverlayVisible(bool,double);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;std::optional<double>nextWakeTime()const noexcept;
    bool deadline(double);void queueCapacityAvailable();
    bool covers(core::Point logical)const;bool pointer(const app::PointerEvent&,double);
    bool wheel(const app::WheelEvent&,double);bool key(const app::KeyEvent&,double);
    bool filterKey(const app::NativeMessage&);bool message(const app::NativeMessage&,double);
    void focus(bool,double);void cancelInteraction(double);bool pointerLocked()const noexcept;
    // false means marked text or a TSF transaction must finish first. No routine
    // WM_APP refocus occurs; explicit field/focus transitions alone set focus.
    bool finishEditing(double);
    std::optional<ArchiveMediaAction>takeMediaAction();
    UINT_PTR mediaRouteGeneration()const noexcept;
    void presentShelfMedia(std::vector<modules::NotesShelfChoice>,double);
    // Only a matching active selected-document generation may append references.
    // Owner creates/validates references on its EXISTING bounded shared worker.
    bool receiveMedia(std::string_view document,std::uint64_t generation,
        std::vector<modules::ArchiveJson>,std::optional<std::string>error,double);
    void setMediaPresentation(std::size_t,bool playing,double time,
        std::map<std::string,modules::ArchiveJson,std::less<>>posters,
        std::optional<std::string>error,double ownerTime);
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
    const modules::ArchiveState&state()const noexcept;
    std::optional<std::string>activeField()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
