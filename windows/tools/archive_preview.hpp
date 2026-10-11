#pragma once
#include "app/overlay_host.hpp"
#include "native/archive_scene.hpp"
#include "native/projected_editor.hpp"
#include "native/notes_long_editor.hpp"
#include "native/notes_clipboard.hpp"
#include "native/notes_automation.hpp"
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
    // Bodies longer than chunkedBodyUnits use the paragraph-chunked editor, up
    // to the source ArchiveStore 2 MiB body (maximumBodyUnits UTF-16 units);
    // typing cost then follows the edited paragraphs, not the body length.
    std::uint32_t chunkedBodyUnits{4096},maximumBodyUnits{2*1024*1024};
    native::NativeNotesLongEditorOptions longEditor;
    UINT textMessage{WM_APP+220},mediaMessage{WM_APP+221};
};
// Archive owner borrowing the shared state/FIFO executor, HWND/rasterizer and
// already-activated TSF manager. No new window, renderer, clock, timer, service,
// picker, media decoder, filesystem or database is created. Callbacks may not
// synchronously destroy/reenter this owner. Remove entries before release;
// destroy fields before the shared TSF manager and drain accepted state saves.
// Full stored bodies are retained. Title/date/category leaves reject
// >editorCapacity UTF-16 units; a long body opens in the chunked editor, whose
// only limit is one paragraph per painted leaf. Nothing is ever truncated.
// Source rich title/date single-line inset + natural one-point spacing remains
// an explicit shared-editor layout gap. Body uses the actual natural +1 seam.
// Modifier state for one key event; absent means the thread's key state.
struct ArchiveKeyModifiers {bool control{},shift{},system{};};
class ArchivePreview final {
public:
    ArchivePreview(modules::ArchiveState&,HWND,native::LayerRasterizer&,
        ITfThreadMgr* borrowedManager,TfClientId,ArchivePreviewOptions);
    ~ArchivePreview();
    void resize(const app::ClientMetrics&);void setAppearance(modules::ArchiveAppearance);
    bool setStrings(modules::ArchiveStrings,modules::NotesControlsStrings,
        std::string categoryNamePlaceholder,double ownerTime);
    void setReduceMotion(bool,double);void setOverlayVisible(bool,double);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double)const;std::optional<double>nextWakeTime()const noexcept;
    bool deadline(double);void queueCapacityAvailable();
    bool covers(core::Point logical)const;bool pointer(const app::PointerEvent&,double);
    bool wheel(const app::WheelEvent&,double);bool key(const app::KeyEvent&,double,std::optional<ArchiveKeyModifiers> =std::nullopt);
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
    void setMediaPlayback(std::size_t,bool playing,double time,
        std::optional<std::string>error,double ownerTime);
    void setMediaPresentation(std::size_t,bool playing,double time,
        std::map<std::string,modules::ArchiveJson,std::less<>>posters,
        std::optional<std::string>error,double ownerTime);
    // Selected media lives inside the source grouped face (before footer/error),
    // including section opacity/shutter. Binding is rejected for another doc.
    bool setMediaDraw(std::string_view document,std::size_t index,
        std::optional<native::DrawObject>,double);
    std::size_t mediaIndex()const noexcept;
    bool mediaVisible()const noexcept;
    bool mediaRequestCurrent(std::string_view document,std::uint64_t generation)const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
    const modules::ArchiveState&state()const noexcept;
    std::optional<std::string>activeField()const;
    // Edit-menu Cut/Copy/Paste (Ctrl+X/C/V) in the focused field. Defaults to
    // the system clipboard, touched only on those keys; tests inject a
    // synthetic one. Null disables the keys.
    void setClipboard(std::shared_ptr<native::NotesClipboard>);
    // Source documentDropURLs(.archive) + importFiles: media files dropped on
    // the open document attach like chosen ones (16 in all). The returned
    // generation-tagged import goes to the owner's media importer; nullopt
    // without a document or room, or while a menu or drag owns the pointer.
    std::optional<ArchiveMediaAction> dropMediaAction(double time);
    // UI Automation (native/notes_automation.hpp): HUDArchiveInteraction.
    // layoutAccessibility - every ArchiveCanvas action as a button (source
    // action ids, labels from accessibleTitle), the gallery scroller
    // ("archive:gallery": the scroll offset, steps of 122, hidden under a
    // menu), the open document's projected fields and the category name
    // ("archive:field:<title|date|body|category>", their text), and the open
    // menu's items ("archive.menu:<item>") with its color wheel
    // ("archive.menu:wheel"). Physical client rectangles; nothing while the
    // owner accepts no input. Content events only (allocates).
    void automationElements(std::vector<native::NotesAutomationElement>&)const;
    // ArchiveActionButton.activate -> ArchiveCanvas.perform, or the open
    // menu's item; false for an unknown or disabled id.
    bool perform(std::string_view id,double time);
    bool setAutomationValue(std::string_view id,double value,double time);  // gallery offset or color wheel
    bool setAutomationText(std::string_view id,std::string_view utf8,double time); // replaces a field's text (one undo group)
    std::unique_ptr<native::NativeNotesAutomation> automation(IRawElementProviderFragmentRoot* group,
        std::function<double()> clock,std::function<void()> changed={});
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
