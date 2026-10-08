#pragma once
#include "core/data/data_store.hpp"
#include "core/scene.hpp"
#include <functional>
#include <optional>
#include <set>
#include <span>

namespace endfield::modules {
// Source: NotesStore.swift NotesGeometry and NotesCanvas.swift. This controller
// has no renderer, clock, OS service or autonomous I/O. One owner calls it on its
// UI thread; persistence callbacks are synchronous and must not reenter it.
class NotesState final {
public:
    using Note=ehud::data::Note;
    using Point=core::Point;
    using Rect=core::Rect;
    struct Persistence {
        // A NotesStore adapter discards upsert/remove's bool: false means an
        // already-saved no-op, not failure. Throw to report persistence failure.
        std::function<void(const Note&)> upsert;
        std::function<void(std::string_view)> remove;
    };
    struct EditRequest {
        std::string noteID,text;
        Rect rect; // full workspace coordinates, NOT clipped by the center module
        double fontSize{12};bool multiline{true};
    };
    struct Card {
        Rect rect,localRect,resizeHandle;
        bool visible{},selected{},pinned{};
        double layerOrder{};
    };
    enum class Gesture {move,resize};
    NotesState(std::vector<Note> initial,Persistence persistence,bool notesSelected=true);
    std::span<const Note> notes() const noexcept {return notes_;}
    const Note* note(std::string_view id) const noexcept;
    const std::optional<std::string>& selection() const noexcept {return selected_;}
    const std::optional<std::string>& pendingDeletion() const noexcept {return pendingDeletion_;}
    const std::optional<EditRequest>& editing() const noexcept {return editing_;}
    const std::set<std::string,std::less<>>& unsaved() const noexcept {return unsaved_;}
    const std::optional<std::string>& error() const noexcept {return error_;}
    std::uint64_t revision() const noexcept {return revision_;}
    std::uint64_t placementRevision() const noexcept {return placementRevision_;}
    bool notesSelected() const noexcept {return notesSelected_;}
    bool dragging() const noexcept {return drag_.has_value();}
    Rect workspaceBounds() const noexcept {return workspace_;}
    const core::Projection& workspaceProjection() const noexcept {return projection_;}
    // Camera is caller-owned and remains updateable during closing. A placement
    // change never persists notes, copies content or changes their layout revision.
    bool setWorkspaceProjection(const core::Projection&);
    // Caller finishes its editor before resize/tab/hide, as HUDNotesInteraction
    // does. Geometry is constrained only for display; resizing never saves it.
    bool setWorkspaceBounds(Rect,std::optional<Point> creationPoint={});
    void setPresentation(bool notesSelected); // commits active geometry gesture
    void cancelInteraction(); // source semantics: finish gesture, not rollback
    bool visible(std::string_view id) const noexcept;
    std::optional<Card> card(std::string_view id) const noexcept;
    std::optional<std::string_view> topNote(Point) const noexcept;
    static Note constrained(Note,std::optional<Rect> bounds={});
    // First slice creates plain text only. Identity/time are supplied by the
    // caller; no hidden UUID generator or OS clock runs here. Selects and starts
    // editing with source defaults, even when the view retains an unsaved draft.
    bool createText(std::string id,double createdAt);
    bool select(std::optional<std::string> id);
    bool togglePin(std::string_view id);
    bool requestDeletion(std::string_view id);
    void cancelDeletion();
    bool confirmDeletion(std::string_view id); // remove only after persistence succeeds
    std::optional<std::array<Rect,2>> deletionControls() const noexcept;
    bool beginGesture(std::string_view id,Point start,Gesture);
    bool dragTo(Point);
    bool endGesture(); // only a changed gesture saves; source threshold >1 point
    // Plain text editor handoff only; rich text/TODO rows require their actual
    // measured, style-preserving adapter and reject explicitly in this slice.
    const EditRequest& beginEditing(std::string_view id);
    bool finishEditing(std::string text);
    void detachEditor(); // caller already committed or explicitly discarded input
private:
    struct Drag {std::string noteID;ehud::data::NoteKind noteKind;Rect original;Point start;Gesture kind;bool changed{};};
    std::vector<Note> notes_;Persistence persistence_;
    Rect workspace_{0,0,400,334};std::optional<Point> creationPoint_;
    core::Projection projection_;
    bool notesSelected_{true},persisting_{};
    std::optional<std::string> selected_,pendingDeletion_,error_;
    std::optional<EditRequest> editing_;std::optional<Drag> drag_;
    std::set<std::string,std::less<>> unsaved_;
    std::uint64_t revision_{1},placementRevision_{1};
    void writable() const;
    Note* find(std::string_view) noexcept;
    void replace(const Note&);
    bool save(const Note&);
    std::int64_t nextZ();
};
} // namespace endfield::modules
