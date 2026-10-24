#pragma once
// The Notes centre on the redesign: the port of Sources/NotesCanvas.swift
// (Mac 1.3.0, spec section 3.6). DOCS-100..130.
//
// TEXT / TODO tabs, the TODO task cards with their add field, the rich TEXT
// note with its format bar, Hide/Show, and the add bar that creates notes on
// the DESKTOP. The desktop cards themselves stay the workspace of the Notes
// owner (tools/notes_preview.*, native/notes_workspace.*); this canvas reaches
// it only through NotesCanvasOptions (the Mac canvas forwards the host's
// workspace calls to its NotesWorkspace). Centre data is NotesCentreStore
// (core/data/notes_centre_store.hpp, Notes/centre.json), written after each
// edit, never on a timer; desktop notes stay in notes.sqlite3.
//
// Not logged: centre TODO / TEXT edits write nothing to the Event Log (the Mac
// canvas forwards only the desktop workspace's events, DOCS-128).
//
// Adaptations (recorded): the Hide/Show fold animates a top-anchored clip
// (Channel::wipe, from 1 to 6 / body height) instead of the clip layer's
// bounds height, and the add bar's position.y glide is a translation from its
// expanded slot; the reorder glides are translations from each card's built
// slot. The pixels and the model hit geometry are the Mac's. The storage
// problem notice is localized when it shows (the Mac localizes the default
// "Notes storage is unavailable." when the canvas is made).
#include "core/data/notes_centre_store.hpp"
#include "modules/archive_body_area.hpp"
#include "modules/archive_rich_text.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/notes_format_bar.hpp"
#include "modules/notes_shelf_choice.hpp"
#include <map>

namespace endfield::modules {

struct NotesCanvasOptions {
    // NotesStore was unavailable (Mac: store == nil): no tabs, the empty
    // state, no Hide/Show, the add buttons disabled. `storageError` is the
    // store's own message; nullopt shows "Notes storage is unavailable.".
    bool storageAvailable{true};
    std::optional<std::string> storageError;
    // workspace.perform(actionID: "tool:<kind>") (text, todo, image, drawing):
    // creates the desktop note; "tool:image" makes the workspace ask for a
    // source (presentMediaSources), after finishing desktop editing.
    std::function<void(std::string_view action)> onTool;
    // The Media menu's two sources, at the workspace creation point
    // (HUDNotesInteraction: the native file chooser, the File Shelf list).
    std::function<void(design::Point)> onChooseFiles, onChooseShelf;
    // Windows names its own file browser ("Choose in Finder" is the Mac's).
    std::function<std::string(core::Language)> chooseFileTitle;
    // The format bar's families (notesFormat::families of the rasterizer's
    // bundled and installed families) and the family of a stored font name
    // (ArchiveRichText.family: the bar's font value).
    std::vector<std::string> families;
    ArchiveFontServices fonts;
    // The TEXT note's persistent editor (native/archive_body_editor.hpp, the
    // Mac ArchiveBodyArea's projected text view); null: the note shows its
    // drawn placeholder only.
    ArchiveBodyEditorHost* noteEditor{};
};

class NotesCanvas final : public design::Canvas {
public:
    static constexpr double cardHeight = design::TaskCard::height, listLimit = 250;
    NotesCanvas(design::Context&, ehud::data::NotesCentreStore&, NotesCanvasOptions);
    ~NotesCanvas() override;
    const ehud::data::NotesCentreDocument& document() const noexcept { return store_->document(); }

    // --- Workspace, as the host addresses it ------------------------------------
    // workspace.onChooseImage: the Media button's menu (Choose in Finder /
    // Choose from Shelf) on that button; only while active.
    void presentMediaSources(design::Point creationPoint);
    // The File Shelf's items as a menu on the Media button (long lists get the
    // scroll bar); unsupported / unavailable items are listed but disabled;
    // an empty shelf toasts "Shelf empty". One press imports at once.
    void presentShelfMedia(std::vector<NotesShelfChoice>, std::function<void(const std::string& id)> onSelect);
    // workspace.onAction (NotesCanvasEvent raw value): the creation toasts,
    // only while active.
    void workspaceDidAct(std::string_view action);
    // workspace.statusMessage changed: a "Notice" toast once per distinct
    // message, only while active (the v1.2.0 centre status line is gone).
    void reportStatus(const std::optional<std::string>&);

    // --- Actions (accessibility, tests and the Mac's ...ForTesting) --------------
    void submitTask(const std::string& text);    // submitTaskForTesting
    void openTaskMenu(const std::string& id);    // openTaskMenuForTesting (anchored on the card)
    void dragTask(const std::string& id, int toSlot); // dragTaskForTesting
    void selectTab(ehud::data::NotesCentreDocument::Tab, double direction);
    void setFolded(bool);
    design::TaskCard* taskCard(const std::string& id) const;
    design::TextField* addField() const noexcept { return addField_; }
    design::TextField* editingTask() const noexcept { return editingTask_ ? editingTask_->field : nullptr; }
    NotesFormatBar* formatBar() const noexcept { return formatBar_; }
    design::Tabs* tabs() const noexcept { return tabs_; }
    ArchiveBodyArea* noteArea() const noexcept { return noteArea_; }
    // applyFormatForTesting; the format bar's onFormat.
    void applyFormat(const core::notes::FormatChange&);
    NotesFormatState noteFormatState() const;

    // --- design::Canvas ---------------------------------------------------------------
    void buildPage(design::Page&) override;
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override;
    void recordingModeChanged() override;
    bool handleRightPointerDown(design::Point) override;
    // NotesCanvas.pointerDown (:638-650), before the canvas's own handling: a
    // double-click on a task card (not on its pill or x) edits its title in
    // place.
    bool interceptPointerDown(design::Point, const design::PointerInput&) override;
    bool inputLocked() const override;
    bool handleScroll(design::Point, design::Point delta) override;
    bool editingOtherText() const override;
    void endOtherEditing(bool commit) override;
private:
    struct Reorder { std::string id; double grab{}; int index{}; std::vector<std::string> order; };
    struct EditingTask { std::string id; design::TextField* field{}; };
    ehud::data::NotesCentreStore* store_;
    NotesCanvasOptions options_;
    design::Tabs* tabs_{};
    NotesFormatBar* formatBar_{};
    std::vector<std::pair<std::string, design::TaskCard*>> cards_;   // document order
    std::vector<std::pair<std::string, int>> builtSlots_;            // the slot each card was built at
    design::ScrollArea* taskList_{};
    design::Layer *body_{}, *bodyContent_{}, *addBar_{};
    design::Control* mediaButton_{};
    design::Button* foldButton_{};                                    // the page's Hide / Show
    design::TextField* addField_{};                                   // canvas-owned: survives re-renders
    // The TEXT editor, created once with the stored text and runs and kept
    // across re-renders so focus, selection, composition and undo survive.
    ArchiveBodyArea* noteArea_{};
    bool noteEdited_{};
    std::optional<EditingTask> editingTask_;
    std::map<std::string, design::TextField*, std::less<>> editFields_; // canvas-owned, one per edited task
    std::optional<Reorder> reorder_;
    double expandedBodyHeight_{};
    std::optional<std::string> reportedStatus_;
    bool reportedLoadProblem_{};
    design::Deadlines::Token timers_{};

    std::string counterText() const;
    double addBarTop(bool folded) const noexcept;
    design::Button& foldButton(design::Page&, bool folded);
    double buildTasks(design::Page&, design::Layer& content, double top, std::vector<design::Layer*>& staggered);
    double buildNote(design::Page&, design::Layer& content, double top, std::vector<design::Layer*>& staggered);
    design::Layer& buildAddBar(design::Page&);
    static double slotY(int index) noexcept;
    design::TextField& taskField(double width);
    void openTaskMenu(const std::string& id, const design::Control* anchor, std::optional<design::Point> at);
    void setDot(const std::string& id, bool on);
    void refreshDots();
    void moveTask(const std::string& id, int step);
    void beginTaskEdit(const std::string& id);
    void placeTaskField(design::TextField&, design::TaskCard&);
    void finishTaskEdit(const std::string& id, std::optional<std::string> text);
    void dragTask(const std::string& id, design::DragGrip::Phase, design::Point rootPoint);
    void cancelReorder();
    void setTask(const std::string& id, bool done);
    void deleteTask(const std::string& id);
    void submitTask();
    bool save(const std::function<void(ehud::data::NotesCentreDocument&)>&);
    void saveNote();
    ArchiveBodyArea& noteEditor();
    void attachNote();
    std::optional<std::string> taskID(design::Point) const;
    int builtSlot(const std::string& id) const;
};
} // namespace endfield::modules
