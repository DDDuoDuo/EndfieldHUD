#pragma once
// The redesign controls of one desktop note card, drawn on the design library
// over the card's artwork: the port of the controls half of
// Sources/NotesWorkspace.swift (Mac 1.3.0) render(_:) "the card's buttons"
// (:1228-1278), makeChecklistControls (:1435-1464), the title strip's caption
// (:1187-1191) and the note scroll thumb with its HUDScrollBarVisibility
// (layoutScrollableContent :1361-1375, mouseMoved :606-615, scroll :561).
// DOCS-203, DOCS-205..210, DOCS-212, DOCS-215, DOCS-216.
//
// NotesCardPresentation decides WHAT (controls(), title(), thumb(): frames,
// glyphs, on / enabled / hidden, labels); this builds the library's own
// HUDIconButton and NotesFormatToggle for them, so hover (ring #444 -> #888
// .22 ease, scale 1.06), press (.94) and the disabled .35 are the library's.
// A content event rebuilds the controls settled (the Mac's BuildGate: the
// controls' environment reports Reduce Motion while a card is built, so a
// resize drag or a checked item does not fade every button in again); the
// title, the viewport clip and the thumb stay (the thumb keeps its shown
// state). Presses, hit tests and accessibility stay the card's actions (the
// same rects); this layer tree only draws and takes the highlight pass.
//
// Adaptation (recorded): the highlight pass runs over the topmost card under
// the pointer only (the Mac's pass walks the whole workspace layer, so a
// lower card's button under an upper card could light).
#include "modules/notes_format_bar.hpp"
#include "modules/notes_presentation.hpp"

namespace endfield::modules {
class NotesCardControls final {
public:
    NotesCardControls(design::Environment&, std::string noteID);
    ~NotesCardControls();
    NotesCardControls(const NotesCardControls&) = delete;
    NotesCardControls& operator=(const NotesCardControls&) = delete;
    // A content event of the card (its presentation was updated).
    void update(const NotesCardPresentation&);
    design::Layer& root() const noexcept { return *root_; }
    // Bumps whenever the tree's content changed (the native scene re-emits).
    std::uint64_t revision() const noexcept;
    // The highlight pass (HUDControlHighlightLayer.update) at a card-local
    // point; nullopt: the pointer left the card.
    void pointer(std::optional<design::Point>, bool pressed);
    // HUDScrollBarVisibility of the note's thumb: a content scroll shows it
    // (hidden 1 s later); the pointer over a text or TODO card keeps it shown.
    void noteScroll();
    void setScrollHovered(bool);
    bool requiresFrames(double now) const;
    std::span<design::Control* const> controls() const noexcept;
    const design::Control* control(std::string_view id) const noexcept;
private:
    struct Build;
    design::Environment* env_;
    std::string id_;
    design::LayerArena arena_;                  // root, viewport, thumb: kept across builds
    design::Layer *root_, *viewport_, *thumb_;
    std::unique_ptr<design::ScrollBarDriver> bar_;
    std::unique_ptr<Build> build_, previous_;
    std::optional<design::Point> pointer_;
    bool pressed_{}, scrollable_{};
    std::uint64_t builds_{};
};
} // namespace endfield::modules
