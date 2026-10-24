#pragma once
// A desktop note's formatting pop-up on the notes plane: the port of
// NotesDesktopPopup (Sources/NotesFormattingControls.swift:458-631, Mac
// 1.3.0) as HUDNotesInteraction hosts it (showFormatting, layoutFormatting,
// positionPopup, dismiss: Sources/HUDNotesInteraction.swift:303-455).
// DOCS-139, DOCS-140, DOCS-237..240, DOCS-246.
//  - font / size: the redesign HUDMenu (30 px rows, the long-menu bar past
//    seven entries), "notes.desk.font" (families: the default face, the
//    bundled CJK faces, then the installed ones; minimum width 200) /
//    "notes.desk.size" (8 ... 144 plus the current size; 96), scrolled so the
//    selected entry is the fourth row; a pick applies AND closes;
//  - color: the shared colour pop-over (notesColorPicker "notes.desk.color",
//    "TEXT COLOR"/文字颜色, a drawing's "PEN COLOR"/画笔颜色), applied once per
//    release, its close button;
//  - special: B / I / U / S (20 x 20, 24 pitch) and a close icon button in
//    the panel chrome, 124 x 28; a press flips the toggle and applies; the bar
//    stays open.
// Placed at the note's bottom-left 4 px below it, clamped into the
// workspace; enters with the top-down wipe (.18 E from .4), and leaves with
// input stopped at once while the artwork fades out over .14 s ease (at once
// under Reduce Motion). Windows also lists the media relink sources and the
// File Shelf here (menu kind): the v1.2.0 relink of a moved media note kept
// on Windows, now in the redesign's menu under its card.
//
// The pop-up never takes the keyboard focus from an open note editor (the
// owner keeps routing text to the editor and gives the pop-up only menu
// keys and Esc). Presses route through a HUDInteractionRouter, hover through
// the highlight pass, the screen reader through the owner's peers
// (automationElements / perform / setValue). Workspace points throughout.
#include "core/notes_rich_text.hpp"
#ifdef _WIN32
#include "modules/notes_format_bar.hpp"
#include "native/hud_design_scene.hpp"
#include "native/notes_automation.hpp"

namespace endfield::tools {
class NotesDesktopPopup final {
public:
    enum class Kind : std::uint8_t { font, size, color, special, menu };
    // NotesDesktopPopup.sizes (:490).
    static constexpr std::array<int, 17> sizes{8, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 64, 72, 96, 144};
    static constexpr double buttonSide = 20, buttonPitch = 24;
    NotesDesktopPopup(native::LayerRasterizer&, native::LayerRasterOptions);  // raster.assetRoot: resources/notes-format (the wheel)
    ~NotesDesktopPopup();
    NotesDesktopPopup(const NotesDesktopPopup&) = delete;
    NotesDesktopPopup& operator=(const NotesDesktopPopup&) = delete;
    void setEnvironment(modules::design::Environment*);
    struct Format {
        Kind kind{Kind::font};
        std::string noteID;
        core::notes::TextStyle style;           // the selection's style (a drawing: its pen colour)
        std::string selectedFamily;             // the font menu's checked family
        std::vector<std::string> families;      // the font menu
        std::string colorTitle;                 // TEXT COLOR / PEN COLOR
        std::uint32_t color{};                  // the colour pop-over's start (0xRRGGBB)
    };
    std::function<void(const core::notes::FormatChange&)> onChange;
    std::function<void()> onClose;              // the pop-up asked to close (its x, a pick, Esc)
    std::function<void(const std::string& id)> onPick; // the menu kind's pick
    void openFormat(Format, core::Rect note, core::Rect workspace, double time);
    void openMenu(std::string noteID, std::string id, std::vector<modules::design::Menu::Item>, double minimumWidth,
        core::Rect note, core::Rect workspace, double time);
    void close(double time);                    // fades out (or at once under Reduce Motion)
    bool open() const noexcept;
    std::optional<Kind> kind() const noexcept;
    const std::string& noteID() const noexcept;
    void place(core::Rect note, core::Rect workspace); // the note moved or resized
    // Input (workspace points). pointerDown is true inside the open pop-up.
    bool contains(core::Point) const noexcept;
    bool pointerDown(core::Point, double time);
    void pointerDragged(core::Point, double time);
    bool pointerUp(core::Point, double time);
    void hover(std::optional<core::Point>, bool pressed, double time);
    bool tracking() const noexcept;
    bool wheel(double deltaPoints, double time); // + down
    bool key(const modules::design::KeyInput&, double time);
    // Frames and composition: the workspace's plane.
    void pose(const core::Matrix4& workspaceToScreen, float opacity, double time);
    bool requiresFrames(double time) const;
    std::span<const native::LayerCompositionEntry> entries() const noexcept;
    std::uint64_t compositionRevision() const noexcept;
    void upload(native::Renderer&);
    void collected(native::Renderer&);
    bool release(native::Renderer&);
    // HUDAccessibilityStandIns: the open pop-up's controls ("notes.desk:<id>"),
    // physical client rects through `project` (workspace point -> client).
    void automationElements(std::vector<native::NotesAutomationElement>&, const std::function<std::optional<core::Point>(core::Point)>& project) const;
    bool perform(std::string_view id, double time);
    bool setValue(std::string_view id, double value, double time);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
