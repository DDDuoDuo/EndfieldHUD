#pragma once
// The desktop notes' redesign controls on the notes plane: each card's
// buttons, title and scroll thumb (modules/notes_card_controls.hpp, drawn
// right after the card's own scenes) and the two-step deletion confirmation
// (NotesWorkspace.renderDeletionControls, Sources/NotesWorkspace.swift
// :1465-1508, Mac 1.3.0; DOCS-213): two HUDIconButtons 25 x 25, cancel the
// dark one with a .x 11, confirm the lit one (theme yellow, #111 tick 11),
// beneath the card's delete button, rising 6 px over .16 s easeOut. The
// Notes owner (tools/notes_preview.*) routes the workspace's observer here,
// composes decorate() into its entries and keeps presses on the workspace's
// actions (the same rects); these scenes only draw and take the highlight
// pass (hover, press) on the shared design Context's environment. Without
// an environment (no design overlay) nothing is drawn.
//
// One NativeDesignScene per retained card (its controls, the title and the
// thumb: small surfaces, released with the card), one for the confirmation.
// Content events rebuild the controls settled; hover, press, the thumb and
// the rise are numbers on retained surfaces. No timer: the thumb's delayed
// hide is a deadline of the shared Context (run by the overlay owner).
#include "modules/notes_card_controls.hpp"
#ifdef _WIN32
#include "native/hud_design_scene.hpp"

namespace endfield::tools {
class NotesDesktopControls final {
public:
    NotesDesktopControls(native::LayerRasterizer&, native::LayerRasterOptions);
    ~NotesDesktopControls();
    NotesDesktopControls(const NotesDesktopControls&) = delete;
    NotesDesktopControls& operator=(const NotesDesktopControls&) = delete;
    void setEnvironment(modules::design::Environment*);
    modules::design::Environment* environment() const noexcept;
    // --- Cards: native::NativeNotesCardObserver -----------------------------------
    void cardContent(std::uint64_t card, const modules::NotesCardPresentation&);
    void cardPosed(std::uint64_t card, const core::Matrix4& cardToScreen, float opacity, double time);
    void cardRemoved(std::uint64_t card);
    void decorate(std::uint64_t card, std::vector<native::LayerCompositionEntry>&) const;
    // The highlight pass over the topmost card under the pointer (card-local);
    // nullopt card: the pointer is over no card.
    void hover(std::optional<std::uint64_t> card, std::optional<core::Point> local, bool pressed, double time);
    // The thumb (HUDScrollBarVisibility): the pointer over a text or TODO card, a content scroll.
    void scrollHover(std::optional<std::uint64_t> card, double time);
    void noteScroll(std::uint64_t card, double time);
    // --- The deletion confirmation ---------------------------------------------------
    struct Deletion {
        std::string noteID;
        std::array<core::Rect, 2> rects;        // workspace points: cancel, confirm
        std::array<std::string, 2> labels;      // Cancel deletion, Confirm deletion
    };
    // A new request rises in; nullopt hides it at once.
    void setDeletion(std::optional<Deletion>, double time);
    void hoverDeletion(std::optional<core::Point> workspace, bool pressed, double time);
    void poseDeletion(const core::Matrix4& workspaceToScreen, float opacity, double time);
    std::span<const native::LayerCompositionEntry> deletionEntries() const noexcept;
    // --- Frames and resources ----------------------------------------------------------
    bool requiresFrames(double time) const;
    // Bumps when an entry list changed (the owner republishes).
    std::uint64_t compositionRevision() const noexcept;
    void upload(native::Renderer&);
    void collected(native::Renderer&);
    bool release(native::Renderer&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
