#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "modules/projection_interaction.hpp"
#include "modules/projection_presentation.hpp"
#include "native/projection_controls_scene.hpp"
#include "native/projection_ink.hpp"
#include "native/projection_plane.hpp"
#include "native/notes_controls_scene.hpp"
#include "native/notes_automation.hpp"
#include <functional>
namespace endfield::tools {
struct ProjectionPreviewAction {
    enum class Kind {returnToHUD,chooseLocal,useShelf,togglePlayback,seek};Kind kind{};
    std::uint64_t generation{},mediaID{};std::string shelfID;double seconds{};
};
struct ProjectionPreviewEvent {
    enum class Kind {strokeCompleted,erased,brushChanged,backgroundChanged,cleared,mediaAdded,mediaRemoved};Kind kind{};
    std::optional<modules::NotesMediaKind>mediaKind;
};
// Source OverlayController.recordProjectionEvent: the Event Log
// projectionAction name for one workspace event (strokeCompleted is
// "drawingEdited"); the root records the name only.
std::string_view projectionEventAction(const ProjectionPreviewEvent&)noexcept;
struct ProjectionPreviewOptions {
    native::LayerRasterOptions raster;
    core::Language language{core::Language::english};modules::NotesColor accent{.98,.83,.12,1};
    modules::NotesControlsStrings menuStrings;
    // Original192px source wheel, explicitly pinned by the application package.
    // A missing binding rejects color-menu opening; no invented replacement.
    std::optional<native::NativeNotesControlsImage>colorWheel;
    std::function<std::vector<modules::NotesShelfChoice>()>shelfChoices;
    std::function<void(const ProjectionPreviewEvent&)>event;
    bool reduceMotion{};
};
// Fullscreen source workspace on the EXISTING host/renderer. Model and gesture
// controller are borrowed and survive hide/show. Root owns handoff, final panel
// opacity, shared blur, native picker/import validation, media providers and
// resource leases. No clock, timer, thread, HWND, store or real-data read.
class ProjectionPreview final {
public:
    ProjectionPreview(modules::ProjectionModel&,modules::ProjectionInteraction&,
        native::LayerRasterizer&,ProjectionPreviewOptions);
    ~ProjectionPreview();
    void resize(const app::ClientMetrics&,double safeAreaTop=0,double visibleTop=0);
    void setActive(bool,double);bool active()const noexcept;
    void setPreferences(core::Language,modules::NotesColor,modules::NotesControlsStrings,bool reduceMotion,double);
    // Only the flat logical-to-physical DPI transform is used. The root fades
    // the final composited window once, outside these individual entries.
    void update(double);bool requiresFrames(double)const;
    double backgroundOpacity()const; // original finite background-enabled fade
    bool pointer(const app::PointerEvent&,double);
    bool wheel(const app::WheelEvent&,double);
    bool key(const app::KeyEvent&,double);
    bool pointerLocked()const noexcept;void cancelInteraction(double);
    bool menuOpen()const noexcept;
    bool importBusy()const noexcept;
    std::optional<ProjectionPreviewAction>takeAction();
    bool importRequestCurrent(std::uint64_t)const noexcept;
    // Explicit dropped-file route: acquire a token before the existing shared
    // importer validates selected references; no file read or picker here.
    std::optional<std::uint64_t> prepareImportRequest();
    bool beginImport(std::uint64_t,std::size_t count,std::optional<core::Point>,double);
    bool receiveImport(std::uint64_t,std::span<const std::shared_ptr<const modules::ProjectionMediaReference>>,double);
    bool receiveImportError(std::uint64_t,std::optional<std::string>,double);
    void showError(std::string,double);
    // Borrowed provider output. Content replacement is an event, not a pixel
    // upload here. Root retains referenced mesh/texture until collected() has
    // removed every outgoing projection entry. Draw is in media-card LOCAL
    // coordinates and uses the original content rect/aspect-fit convention.
    bool setMediaDraw(std::uint64_t,std::optional<native::DrawObject>,double);
    bool setMediaPlayback(std::uint64_t,bool playing,double seconds,std::optional<std::string>error,double);
    // UI Automation (native/notes_automation.hpp) of the source
    // ProjectionWorkspaceView group: the ProjectionToolbar buttons
    // ("projection.toolbar:<item>"), each media card's ProjectionMediaAXButton
    // pair ("projection.media:<id>:play" for moving media, labelled Play or
    // Pause, and "projection.media:<id>:remove"), then the open menu's items
    // ("projection.menu:<item>") with the color wheel ("projection.menu:wheel",
    // the marker's x on the 166-point wheel). Physical client rectangles;
    // nothing while inactive. Content events only (allocates).
    void automationElements(std::vector<native::NotesAutomationElement>&)const;
    // The source button actions (toolbar perform, NotesRetainedMenu.perform,
    // the media node's onAction "play"/"remove"); false when unavailable.
    bool perform(std::string_view id,double time);
    bool setAutomationValue(std::string_view id,double value,double time); // color wheel
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry>entries();
    native::ProjectionInkStats inkStats()const noexcept; // diagnostics
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
