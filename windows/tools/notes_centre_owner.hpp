#pragma once
// The Notes centre's owner on the design library: the HUD's ModuleOwner of
// modules::NotesCanvas (Mac NotesCanvas.swift) over tools::DesignOwnerCore,
// with the centre's platform services around it:
//  - Notes/centre.json (ehud::data::NotesCentreStore, written after each
//    edit; a store that cannot be opened runs the canvas memory-only with
//    its storage notice, a set-aside file shows the Mac's notice);
//  - the TEXT note's persistent rich editor (native/archive_body_editor.hpp,
//    the editor the Archive body uses), routed before the canvas through
//    tools/design_body_route.hpp and drawn after the note's part of the
//    canvas scene (NativeDesignScene::setInsertion), so menus and pop-overs
//    cover it;
//  - the desktop notes owner (tools/notes_preview.hpp): the add bar's tools,
//    the Media menu's sources and the workspace's toasts reach it and come
//    back through the canvas (NotesPreview::setCentre).
// The host marks Notes a design screen (ApplicationHost::setDesignScreen):
// this is not a DesignModuleOwner. No window, timer or thread of its own.
#ifdef _WIN32
#include "core/data/notes_centre_store.hpp"
#include "modules/notes_canvas.hpp"
#include "native/archive_body_editor.hpp"
#include "tools/hud_design_owner.hpp"
#include <filesystem>

namespace endfield::tools {
class NotesPreview;
struct NotesCentreOwnerOptions {
    // name "notes", routing (routing0()), raster, images (the colour wheel),
    // the shared text-field services, now, changed.
    DesignOwnerOptions owner;
    // The application data root (<root>/Notes/centre.json); nullopt: storage
    // unavailable (the canvas's notice).
    std::optional<std::filesystem::path> dataRoot;
    std::function<double()> unixNow;                       // the set-aside file's stamp (default: system clock)
    modules::ArchiveFontServices fonts;
    std::function<std::string(core::Language)> chooseFileTitle;
    std::uint32_t bodyMessage{};                           // private WM_APP-range message (ApplicationHost::registerMessage)
    std::shared_ptr<native::NotesClipboard> clipboard;     // the note's Ctrl+X / C / V (null: system clipboard)
    native::DesignEditMenu editMenu;                       // the note's right-click menu (null: none)
};
class NotesCentreOwner final : public app::ModuleOwner {
public:
    static app::ModuleRouting routing0() noexcept;
    // `desktop` outlives this owner (Notes owns the TSF manager every editor borrows).
    NotesCentreOwner(native::LayerRasterizer&, modules::design::Context&, NotesPreview& desktop, NotesCentreOwnerOptions);
    ~NotesCentreOwner() override;
    NotesCentreOwner(const NotesCentreOwner&) = delete;
    NotesCentreOwner& operator=(const NotesCentreOwner&) = delete;
    modules::NotesCanvas& canvas() noexcept;
    const ehud::data::NotesCentreStore& store() const noexcept;
    DesignOwnerCore& core() noexcept;
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
