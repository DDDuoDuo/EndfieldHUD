#pragma once
// Archive (spec sections 3.9, 3.27): the port of Sources/ArchiveCanvas.swift
// on the design library. The category column and the document grid, the
// document editor with the format bar and media, the classified flow and the
// in-place category rename. modules::ArchiveState (the Mac ArchiveController)
// owns the data and the classification; this canvas only presents and edits
// them. The body editor is ArchiveBodyArea (shared with the Notes centre TEXT
// note); the format bar is the shared NotesFormatBar (C4).
//
// Replaces the v1.2.0 port (modules/archive_presentation.*, native/
// archive_scene.*, native/archive_dates.*, native/archive_editor_text.*,
// tools/archive_preview.*): no category chooser on New (DOCS-416), no
// editable date (DOCS-415), no title formatting (DOCS-421), no status line or
// Retry button (DOCS-454), no gallery scroller (DOCS-456), no frozen
// outgoing text (DOCS-470).
//
// Platform services (the Mac's NotesMediaPresentation, NSOpenPanel, the
// shelf access, the poster decoder, Event Log, owner authentication) are
// injected; the native owner (tools/archive_owner.hpp) provides them, tests
// provide fakes. Windows additions kept from v1.2.0: an imported Mac
// attachment shows its error as a control that chooses its file again
// (relink, keeping its place; WINDOWS-MIGRATION.md section 6), and the body
// keeps the chunked editor, rich paste and the stand-by while switched away.
#include "modules/archive_body_area.hpp"
#include "modules/archive_rich_text.hpp"
#include "modules/archive_state.hpp"
#include "modules/hud_design_page.hpp"
#include "modules/notes_format_bar.hpp"
#include "modules/notes_shelf_choice.hpp"

namespace endfield::modules {
// What the open document's selected media shows (NotesMediaPresentation):
// the native binding decodes and plays it and draws its frames into the
// strip's preview (ArchiveCanvas::mediaPreview) above the canvas artwork.
class ArchiveMediaPresenter {
public:
    virtual ~ArchiveMediaPresenter() = default;
    // syncMedia (:1110-1130): the presentation of `reference` (index of the
    // document's media); the same key keeps the presentation.
    virtual void show(std::string_view document, std::size_t index, const ArchiveJson& reference) = 0;
    virtual void clear() = 0;                                // clearMedia (:1132-1136)
    virtual bool playing() const = 0;
    virtual std::optional<std::string> error() const = 0;    // the localized failure
    virtual double currentTime() const = 0;                  // seconds
    virtual void togglePlayback() = 0;
    virtual void seek(double seconds) = 0;
    // Set by the canvas: state changes (loaded, playing, failed) and progress.
    std::function<void()> onStateChange, onProgress;
};
// ArchiveGalleryThumbnails' decoder (:1904-1956): one 256 px static poster
// of a media reference, delivered later on the owner thread (nullopt: no poster).
using ArchivePosterDecoder = std::function<void(const ArchiveJson& reference, std::function<void(std::optional<design::ImageSource>)>)>;
// A media import (importFiles :1047-1074): files from the native picker
// (Choose in File Explorer), one Shelf item, or a dropped list; or the
// Windows relink of an imported Mac attachment. The owner reads the files on
// its worker and answers ArchiveCanvas::receiveMedia with the generation.
struct ArchiveMediaRequest {
    enum class Kind : std::uint8_t { chooseFiles, shelf, relink };
    Kind kind{Kind::chooseFiles};
    std::string document, shelfItem;
    std::size_t remaining{}, index{};
    std::uint64_t generation{};
};
struct ArchiveCanvasServices {
    std::function<std::string()> newID;                      // UUID
    std::function<double()> foundationNow;                   // Date() in Foundation seconds
    // Calendar.current components for HUDFormat.dotted (local time), Foundation seconds.
    std::function<std::string(double)> dotted;
    ArchiveFontServices fonts;
    ArchiveMediaPresenter* media{};
    ArchivePosterDecoder posters;
    // A poster the cache let go (evicted, cleared, or a stale decode): the
    // decoder may free it once no artwork names it.
    std::function<void(const design::ImageSource&)> releasePoster;
    std::function<std::vector<NotesShelfChoice>()> shelfChoices;
    // The picker, a Shelf item or a relink; false: not started (a busy
    // picker): the canvas shows the error the owner reports through receiveMedia.
    std::function<bool(const ArchiveMediaRequest&)> requestMedia;
    std::function<void()> cancelMedia;                       // the panel and the import ticket (didDeactivate)
};

class ArchiveCanvas final : public design::Canvas {
public:
    using Point = design::Point;
    using Rect = design::Rect;
    enum class FilterKind : std::uint8_t { all, uncategorized, category };
    struct Filter {
        FilterKind kind{FilterKind::all}; std::string category;
        bool operator==(const Filter&) const = default;
    };
    enum class PageKind : std::uint8_t { list, editor, locked };
    struct PageState {
        PageKind kind{PageKind::list}; std::string document;
        bool operator==(const PageState&) const = default;
    };
    // Geometry (content-box px, :22-45).
    static constexpr double columnWidth = 150, columnHeight = 420, plateGap = 6, plateHeight = 48;
    static constexpr Rect addRect{0, design::metric::headerHeight + columnHeight - 30, 30, 30};
    static constexpr Rect gridRect{columnWidth + 12, design::metric::headerHeight, design::metric::boxWidth - columnWidth - 12, columnHeight};
    static constexpr double tileHeight = 96 + 24;
    static constexpr Rect titleRect{0, design::metric::headerHeight, design::metric::boxWidth, 36};
    static constexpr Rect barRect{0, titleRect.y + titleRect.height + 6, design::metric::boxWidth, 26};
    static constexpr double footerY = design::metric::boxHeight - 26;
    static constexpr Rect bodyRectFull{0, barRect.y + barRect.height + 6, design::metric::boxWidth, footerY - 8 - (barRect.y + barRect.height) - 6};
    static constexpr Rect mediaRect{0, bodyRectFull.y + bodyRectFull.height - 150, design::metric::boxWidth, 150};
    static constexpr Rect lockedRect{0, design::metric::headerHeight, design::metric::boxWidth, design::metric::boxHeight - design::metric::headerHeight};

    // `state` is the shared ArchiveState (one per HUD, the service's); the
    // canvas relocks a document unlocked in an earlier HUD session (:121-122).
    ArchiveCanvas(design::Context&, ArchiveState&, ArchiveCanvasServices);
    ~ArchiveCanvas() override;
    // The controller's onChange (:201-208): the host calls it after every
    // state revision and completion, whether or not Archive is shown.
    void stateChanged();
    // The body shrinks for the media strip (:41-45).
    Rect bodyRect() const;
    const Filter& filter() const noexcept { return filter_; }
    PageState displayedPage() const;
    std::size_t mediaIndex() const noexcept { return mediaIndex_; }
    // The native owner's hooks.
    ArchiveBodyArea* bodyArea() const noexcept { return documentID_ ? body_ : nullptr; }
    const design::Layer* mediaPreview() const noexcept { return preview_; }   // the strip's 16:10 preview (frames draw here)
    void setBodyHost(ArchiveBodyEditorHost*);
    // Media replies (generation-tagged, :1047-1074): references to add (or
    // the relinked one), the last failure; false when stale.
    bool receiveMedia(const ArchiveMediaRequest&, std::vector<ArchiveJson> references, std::optional<std::string> error);
    // A drop on the HUD (SystemHUDView.swift:3492-3510): accepted only on the
    // open, unlocked document's editor page with room, no menu or pop-over.
    std::optional<ArchiveMediaRequest> dropRequest();
    // Actions, also the verification and test hooks.
    void selectFilter(const Filter&);
    void addCategory();
    void beginRename();
    void openDocument(const std::string& id);
    void createDocument();
    void closeDocument();
    bool presentingPanel() const override { return panel_ || recovering_; }
    // The HUD closed (the owner's setOverlayVisible(false)): the Mac's canvas
    // ends with the HUD (deinit deactivates the controller, :128-137) and the
    // next opening builds a new one (init relocks, the filter starts at All).
    // Windows keeps this object, so the session ends here.
    void endSession();
    // Test state (:1256-1259).
    std::optional<std::string> formattingField() const;

    // --- design::Canvas ---
    void buildPage(design::Page&) override;
    void show(double now) override;                          // makeContent (:142-150)
    void didActivate() override;
    void didDeactivate() override;
    void recordingModeChanged() override {}                  // (:191) Archive is not redacted (DOCS-460)
    bool handleKey(const design::KeyInput&) override;
    bool handleScroll(Point, Point) override;
    bool interceptPointerDown(Point, const design::PointerInput&) override;
    bool editingOtherText() const override;
    void endOtherEditing(bool commit) override;
    void willEndInteraction() override;
    void accessibilityItems(std::vector<design::AccessibilityItem>&) const override;
    bool accessibilityAction(std::string_view id) override;
    bool accessibilityText(std::string_view id, std::string_view value) override;
    void didInstall(design::Page&) override;
    void didCloseMenu() override;
    void didClosePopover() override;
private:
    class CategoryPlate; class SeekBar; class MediaStrip; class LockedPanel;
    // What the current page's layers depend on (:49-62).
    struct Structure {
        struct Category { std::string id, name; bool isNew{}; std::size_t count{}; bool operator==(const Category&) const = default; };
        struct Document { std::string id; std::size_t number{}; std::string title; double date{}; bool classified{}; std::size_t media{}; bool operator==(const Document&) const = default; };
        PageState page;
        core::Language language{};
        Filter filter;
        std::vector<Category> categories;
        std::vector<Document> documents;
        std::size_t total{}, uncategorized{};
        std::string categoryName;
        ArchiveClassificationStatus classification{ArchiveClassificationStatus::unclassified};
        std::size_t media{}, mediaIndex{}; bool playing{}, mediaFailed{};
        bool operator==(const Structure&) const = default;
    };
    enum class Field : std::uint8_t { title, body };
    struct Focus { std::string document; Field field{}; std::pair<std::uint32_t, std::uint32_t> selection; };
    ArchiveState* state_;
    ArchiveCanvasServices services_;
    Filter filter_;
    std::size_t mediaIndex_{};
    std::optional<Structure> renderedStructure_;
    bool needsRefresh_{}, leaving_{}, holding_{}, edited_{}, recovering_{}, panel_{}, mediaRefreshQueued_{}, controllerActivated_{};
    std::vector<std::function<void()>> binders_, popoverBinders_;
    std::optional<std::string> pendingOpen_, unlocking_, opening_, sealing_, reportedError_;
    design::Deadlines::Token openingTimer_{}, soonTimer_{};   // soon: DispatchQueue.main.async work (never cancelled)
    std::optional<Focus> focusMemory_;
    std::optional<double> keptCategoryOffset_;
    std::uint64_t mediaGeneration_{};
    // List page
    design::ScrollArea *categoryArea_{}, *gridArea_{};
    std::vector<std::pair<std::string, CategoryPlate*>> plates_;
    std::vector<ArchiveSummary> shownDocuments_;
    std::vector<std::pair<std::string, design::ArchiveTile*>> tiles_;
    double tileWidth_{132};
    struct Rename { std::string id; design::TextField* field{}; };
    std::optional<Rename> rename_;
    design::TextField* renameField_{};
    // Gallery posters (ArchiveGalleryThumbnails, :1904-1956)
    struct Posters;
    std::unique_ptr<Posters> posters_;
    std::vector<std::pair<std::string, ArchiveJson>> thumbnailReferences_;
    std::vector<std::string> metadataRequests_;
    std::uint64_t thumbnailGeneration_{};
    // Editor page: the title field and body survive in-place re-renders of one
    // document; both are made once in the canvas's arena and reloaded per document.
    std::optional<std::string> documentID_;
    design::TextField* title_{};
    ArchiveBodyArea* body_{};
    ArchiveBodyEditorHost* bodyHost_{};
    NotesFormatBar* formatBar_{};
    design::Layer *progressFill_{}, *preview_{};
    std::optional<std::string> presentationKey_;
    // Locked page and the password pop-overs
    LockedPanel* lockedPanel_{};
    design::TextField* passwordField_{};
    design::Button* unlockButton_{};
    std::vector<design::TextField*> popoverFields_;

    void activateController();
    Structure structure() const;
    PageState currentPage() const;
    std::vector<ArchiveSummary> filteredDocuments() const;
    std::string categoryName(const std::optional<std::string>&) const;
    void refreshWhenPossible();
    void refresh();
    template <class F> void holding(F&& change) { holding_ = true; change(); holding_ = false; }
    void refreshSoon();
    void reportError();
    void reportEdits();
    double ownerTime() const noexcept { return now(); }
    // List
    void buildList(design::Page&, const Structure&);
    void buildCategories(design::Page&, const Structure&);
    void buildGrid(design::Page&, const std::vector<ArchiveSummary>&);
    void layoutTiles(design::Page&, bool stagger);
    void confirmRemoveCategory();
    void finishRename(const std::string& id, std::optional<std::string> text);
    // Editor
    void buildEditor(design::Page&);
    void documentParts(const ArchiveEntry&);
    void discardDocument();
    void titleChanged(const std::string&);
    void bodyChanged();
    NotesFormatState formatState() const;
    void updateFormatBar();
    void applyFormat(const core::notes::FormatChange&);
    void openCategoryMenu(const design::Control& anchor);
    void confirmDelete();
    void confirmDeclassify();
    void openClassify(const design::Control& anchor);
    struct PasswordForm { std::unique_ptr<design::Popover> popover; std::vector<design::Control*> controls; std::vector<design::TextField*> fields; };
    PasswordForm passwordForm(const design::ModuleTitle&, double width, std::string submitID, std::string submitTitle,
        std::optional<std::pair<std::string, std::string>> extra, std::function<void(std::string)> submit, std::function<void()> extraAction);
    void closePopoverAndFields();
    // Locked
    void buildLocked(design::Page&, const std::string& id);
    void focusPasswordSoon();
    void submitUnlock();
    void playOpening(const std::string& id);
    void finishOpening(bool announce);
    void openRecovery(const design::Control& anchor);
    void recover(const std::string& id, std::optional<std::string> password);
    // Media
    void openMediaMenu(const design::Control& anchor);
    void chooseFiles();
    void buildMedia(design::Page&, const ArchiveEntry&);
    void stepMedia(int);
    void removeMedia();
    void relinkMedia();
    void syncMedia(const ArchiveEntry&);
    void clearMedia();
    void updateProgress();
    void mediaStateChanged();
    // Focus (Windows B14 :1143-1167)
    std::optional<Focus> currentFocus() const;
    void restoreFocus();
    // Thumbnails
    void loadVisibleThumbnails();
    std::string errorText(std::string_view) const;
    const CategoryPlate* plate(std::string_view id) const;
    design::ArchiveTile* tile(std::string_view id) const;
};

// User-visible text of an Archive failure (ArchiveError.errorDescription,
// ArchiveStore.swift:99-111) in `language`: the Mac pairs of the store
// errors; the classification errors in the Windows wording
// (archiveClassificationErrorText); other (Windows diagnostic) messages
// unchanged, as v1.2.0 showed them.
std::string archiveErrorText(std::string_view message, core::Language);
} // namespace endfield::modules
