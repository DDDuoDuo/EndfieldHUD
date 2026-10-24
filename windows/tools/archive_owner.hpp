#pragma once
// Archive's owner on the design library: the HUD's ModuleOwner of
// modules::ArchiveCanvas (Mac ArchiveCanvas.swift) over tools::DesignOwnerCore,
// with Archive's platform services around it:
//  - the shared ArchiveState of app::ArchiveService (repository, FIFO file
//    executor, DPAPI vault), its change notices (stateChanged) and its .35 s
//    save deadline (nextWakeTime / deadline);
//  - the persistent body editor (native/archive_body_editor.hpp), routed
//    before the canvas: presses in its text, its keys (TSF first), its
//    I-beam; drawn after the body / media strip part of the canvas scene
//    (NativeDesignScene::setInsertion), so menus and pop-overs cover it;
//  - the media binding (tools/archive_media_binding.hpp): the strip's
//    selected attachment as one unit-quad record in the preview, the gallery
//    posters as memory images, import inspection of picker, Shelf and dropped
//    files; the picker and the Shelf access stay the host's (requestMedia,
//    answered with importFiles / importFailed);
//  - recovery's owner proof (native/archive_owner_authentication.hpp), or the
//    caller's (tests inject a fake: no prompt);
//  - the stand-by of v1.2.0 inside the open HUD: a module switch keeps the
//    document, the editors and their pixels (textures parked by the core);
//    the HUD's end (overlayConcealed) ends the canvas's session like the
//    Mac's deinit and releases the media.
// It replaces tools/archive_preview.*, tools/archive_options' view options,
// native/archive_scene.*, the archive adapter of app/application_modules.cpp.
// The host marks Archive a design screen (ApplicationHost::setDesignScreen):
// this is not a DesignModuleOwner. No window, timer or thread of its own.
#ifdef _WIN32
#include "app/archive_service.hpp"
#include "native/archive_body_editor.hpp"
#include "tools/archive_media_binding.hpp"
#include "tools/hud_design_owner.hpp"

namespace endfield::native { class NativeArchiveOwnerAuthentication; }
namespace endfield::tools {
struct ArchiveOwnerOptions {
    // name "archive", routing (routing() below), raster (memoryImages = the
    // media binding's image source), images (the colour wheel), the shared
    // text-field services, now, changed.
    DesignOwnerOptions owner;
    std::function<std::string()> newID;
    std::function<double()> foundationNow;
    std::function<std::string(double foundation)> dotted;   // HUDFormat.dotted in the local calendar
    modules::ArchiveFontServices fonts;
    std::function<std::vector<modules::NotesShelfChoice>()> shelfChoices;
    // The host's native picker or Shelf access for a canvas request (async);
    // false when it cannot start. Answers: importFiles / importFailed.
    std::function<bool(const modules::ArchiveMediaRequest&)> requestMedia;
    std::function<void()> cancelMedia;
    ArchiveMediaBindingOptions media;
    // Recovery's owner proof; null builds the Windows Hello / credential
    // prompt one on the HUD window (authenticationMessage).
    modules::ArchiveState::OwnerAuthentication ownerAuthentication;
    std::uint32_t bodyMessage{}, authenticationMessage{};   // private WM_APP-range messages (ApplicationHost::registerMessage)
    std::shared_ptr<native::NotesClipboard> clipboard;     // the body's Ctrl+X / C / V (null: system clipboard)
    native::DesignEditMenu editMenu;                       // the body's right-click menu (null: none)
};
// HUDFormat.dotted of a Foundation date in Calendar.current: the local date
// (the system time zone, read at each call), "yyyy.MM.dd".
std::string archiveDottedDate(double foundation);
// The font menus' services over the shared rasterizer: its installed
// families, its bundled CJK families (the manifest's Noto Sans SC / TC / JP /
// KR) and a stored name's family (a shared face's Mac name, a Mac font's
// Windows counterpart, core/notes_font_names.hpp, or an installed family).
modules::ArchiveFontServices archiveFontServices(native::LayerRasterizer&);
class ArchiveOwner final : public app::ModuleOwner {
public:
    static app::ModuleRouting routing0() noexcept;
    ArchiveOwner(native::LayerRasterizer&, modules::design::Context&, app::ArchiveService&, native::NativeMediaRequestBroker&,
        native::NativeMediaRequestBroker::Client, native::Renderer&, native::LayerImageSource&, ArchiveOwnerOptions);
    ~ArchiveOwner() override;
    ArchiveOwner(const ArchiveOwner&) = delete;
    ArchiveOwner& operator=(const ArchiveOwner&) = delete;
    // The ArchiveService's change notice (its executor completions).
    void stateChanged(double time);
    // The host's answers to requestMedia (picker result, Shelf lease, drop).
    bool importFiles(const modules::ArchiveMediaRequest&, std::span<const ArchiveMediaImportFile>, double time);
    void importFailed(const modules::ArchiveMediaRequest&, std::optional<std::string> error, double time);
    // A HUD file drop on the open, unlocked document (<= 16): the request the
    // host imports; nullopt when the canvas refuses it.
    std::optional<modules::ArchiveMediaRequest> dropRequest(double time);
    // After the root accepted / sampled the shared media broker.
    bool refreshMedia(double time);
    modules::ArchiveCanvas& canvas() noexcept;
    const modules::ArchiveState& state() const noexcept;
    DesignOwnerCore& core() noexcept;
    ArchiveMediaBinding& media() noexcept;
    native::NativeArchiveBodyEditor& body() noexcept;
    // ModuleOwner
    std::string_view name() const noexcept override;
    app::ModuleRouting routing() const noexcept override;
    bool presents(core::Module) const noexcept override;
    void resize(const app::ClientMetrics&) override;
    void setAppearance(const app::ModuleAppearance&, double) override;
    void setOverlayVisible(bool, double) override;
    void overlayClosing(double) override;
    void update(const app::ModuleFrame&) override;
    void prewarm(const app::ModuleFrame&) override;
    bool requiresFrames(double) const override;
    std::optional<double> nextWakeTime(double) const override;
    bool deadline(double) override;
    void upload(native::Renderer&) override;
    std::span<const native::LayerCompositionEntry> entries(core::Module) override;
    void collected(native::Renderer&) override;
    void release(native::Renderer&) override;
    bool covers(core::Point) const override;
    bool pointerLocked() const override;
    bool capturesPointer() const override;
    bool capturesWheel() const override;
    bool capturesKeys() const override;
    bool suppressesKeyFilters() const override;
    bool pointer(const app::PointerEvent&, double) override;
    bool wheel(const app::WheelEvent&, double) override;
    bool key(const app::KeyEvent&, double) override;
    bool filterKey(const app::NativeMessage&) override;
    bool message(const app::NativeMessage&, double) override;
    void focus(bool, double) override;
    void cancelInteraction(double) override;
    bool finishEditing(double) override;
    bool flush(double) override;
    bool hostEvent(const app::HostEvent&, double) override;
    std::optional<app::ModuleCursor> cursorAt(core::Point) const override;
    bool automationRefresh(IRawElementProviderFragmentRoot*) override;
    std::size_t automationCount() const override;
    long automationChild(std::size_t, IRawElementProviderFragment**) const override;
    long automationFromPoint(double, double, IRawElementProviderFragment**) const override;
    void automationDisconnect() noexcept override;
    std::optional<core::Module> automationModule() const noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
