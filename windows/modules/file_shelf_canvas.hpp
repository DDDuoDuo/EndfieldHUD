#pragma once
// File Shelf (spec section 3.7): the port of Sources/FileShelfCanvas.swift on
// the design library. Bookmarked references as archive tiles (spec 2.15) and
// the properties card (spec 2.16) beside a 3-column grid while exactly one
// tile is selected (Windows checklist A1, in place of Quick Look). Only
// references and metadata belong to the shelf: nothing here copies, moves or
// deletes a file. The store, the file chooser, opening, File Explorer, the
// property reads, the drag session and drops belong to the platform service
// (tools/shelf_owner.hpp, the HUDFileShelfInteraction counterpart), reached
// through FileShelfServices on the UI thread.
//
// Replaces the v1.2.0 port (modules/file_shelf_state.*, shelf_presentation.*,
// module_strings.*, native/shelf_scene.*, shelf_automation.*, shelf_assets.*):
// no heading or status line, no toolbar or inline Clear confirmation, no
// 184 x 74 cards with an eye / folder / x strip, no collection reveal shutter,
// no selection depth motion, no modal card with a Properties heading, Name /
// Type / Dimensions / Duration rows and a close plate (inventory DOCS-521,
// DOCS-522, DOCS-507).
//
// Windows adaptations (recorded in the port report):
//  - UTType conformance (tileKind) is an extension table producing the Mac's
//    five kinds; Windows has no packages, so every folder is a folder.
//  - WHERE abbreviates the user profile folder to "~" like
//    abbreviatingWithTildeInPath; separators stay the path's own.
//  - The Finder names are Windows' (File Explorer) through shelfText.
//  - A Mac-imported item awaiting relink (FileShelfItem::awaitingRelink) is
//    an unavailable tile whose card and context menu offer Relink in place of
//    Open; nothing is matched automatically.
//  - A Clear still animating when the module leaves finishes at once (no
//    deadline runs while the HUD is hidden).
#include "modules/hud_design_page.hpp"
#include <map>
#include <set>

namespace endfield::modules {
// ShelfItem as the canvas reads it (FileShelfStore.swift:5-32): metadata of
// one reference, never its bookmark / identity.
struct FileShelfItem {
    std::string id, name, lastKnownPath, typeDescription;
    std::optional<std::int64_t> byteCount;
    bool isDirectory{};
    std::optional<std::string> availabilityError;
    bool awaitingRelink{};                 // Windows: Migration/relink.json entry
    // ShelfItem.sizeLabel: ByteCountFormatter .file; none for a directory.
    std::optional<std::string> sizeLabel() const;
    bool operator==(const FileShelfItem&) const = default;
};
// Local calendar fields of a file time (the native side converts in the
// time zone rules of that date, as HUDFormat.dotted formats in the current
// calendar).
struct ShelfLocalTime {
    int year{}, month{}, day{}, hour{}, minute{};
    bool operator==(const ShelfLocalTime&) const = default;
};
// ShelfFileProperties (FileShelfCanvas.swift:14-37): read on a utility worker.
struct ShelfFileProperties {
    static constexpr std::int64_t itemBound = 10'000; // folders larger than this show no count
    std::string path;
    std::optional<ShelfLocalTime> created, modified;
    std::optional<std::int64_t> itemCount;
    bool operator==(const ShelfFileProperties&) const = default;
};
// Windows-only Shelf text (no Mac catalog pair: the platform names and the
// Windows store / relink situations), in the five source languages.
enum class ShelfText : std::uint8_t {
    revealInExplorer, revealedInExplorer, revealFailed, relink, relinkNeeded, relinkTitle, relinkAccept, relinkChooseOne,
    pickerFailed, virtualFiles, chooseAvailable, unavailableReason, tooManyReferences, changedOnDisk, newerVersion
};
std::string shelfText(ShelfText, core::Language);

// The platform half (HUDFileShelfInteraction + FileShelfStore + SystemHUDView's
// Event Log wiring). Every callback runs on the UI thread.
struct FileShelfServices {
    // FileShelfStore: a store that could not open answers nothing (available
    // false: "File shelf storage is unavailable."). refresh / add / remove /
    // clear throw on failure; describe() gives the error's text for a toast.
    bool available{true};
    std::function<std::vector<FileShelfItem>()> items;
    std::function<void()> refresh;
    std::function<void(std::span<const std::string>)> add;
    std::function<void(std::string_view)> remove;
    std::function<void()> clear;
    enum class Failure : std::uint8_t { importing, changing, refreshing, action };
    std::function<std::string(const std::exception&, Failure)> describe;
    // onChooseFiles, onOpen / onReveal (false: the service already reported
    // why), onBeginDrag (ids, primary), onDeactivate, and Windows' relink.
    std::function<void()> chooseFiles, deactivate;
    std::function<bool(std::string_view)> open, reveal;
    std::function<void(std::vector<std::string>, std::string)> beginDrag;
    std::function<void(std::string_view)> relink;
    // Cmd-V (importFiles(from: .general)): true when the key is taken.
    std::function<bool()> paste;
    // ShelfFileProperties.read for one item on the utility worker; the result
    // comes back through FileShelfCanvas::propertiesRead. False: not queued.
    std::function<bool(const FileShelfItem&)> readProperties;
    // SystemHUDView: onItemsAdded / onItemRemoved / onShelfCleared.
    std::function<void(const std::vector<std::string>&)> itemsAdded;
    std::function<void(const std::string&)> itemRemoved;
    std::function<void(std::size_t)> cleared;
    // The items whose Shell icons a drag-out may need (pressed, selected and
    // built tiles, in that order), asked on content events only.
    std::function<void(const std::vector<std::string>&)> iconsWanted;
    // isInputLocked / isPresentingPanel (external drag, drag-out, chooser).
    std::function<bool()> inputLocked, presentingPanel;
    std::string homeFolder;               // WHERE's "~" (the user profile)
};

class FileShelfCanvas final : public design::Canvas {
public:
    using Extension = design::ArchiveTile::Extension;
    FileShelfCanvas(design::Context&, FileShelfServices, std::optional<std::string> error = {});

    // Host compatibility (:93-97).
    std::size_t itemCount() const noexcept { return items_.size(); }
    const std::vector<FileShelfItem>& items() const noexcept { return items_; }
    double scrollOffset() const noexcept;
    const std::set<std::string, std::less<>>& selectedIDs() const noexcept { return selected_; }
    const std::optional<std::string>& focusID() const noexcept { return focus_; }

    // Selection (:390-443): a plain click selects only that tile (again: none);
    // Ctrl or Shift (the Mac's Cmd, Ctrl or Shift) toggles it in a
    // multi-selection; a double-click keeps it selected.
    void click(std::string_view id, bool toggle = false, unsigned clickCount = 1);
    void select(std::set<std::string, std::less<>>);
    void moveSelection(int step, bool extending = false);
    std::vector<std::string> dragSelection(std::string_view primary) const;
    void toggleProperties();

    // Actions (:460-592).
    bool importPaths(std::span<const std::string>);  // importURLs
    // References a drop added (animated: the HUD's own drop, importURLs) or a
    // menu-bar style handoff (revealItems): the grid scrolls to its bottom.
    void revealItems(std::span<const std::string> ids, bool animated = false);
    // open(_:): onOpen, then the "Opened" toast. A service that opens later
    // (outside the canvas event) returns false and calls opened() on success.
    void open(std::string_view id);
    void opened(std::string_view id);
    void reveal(std::string_view id);
    void remove(std::vector<std::string> ids);
    void confirmClear();
    void clearShelf();
    void setDropTarget(bool);
    void showError(std::string);
    // A failure reported after the HUD closed (the reveal handoff): shown
    // when the shelf is next active (the Mac's pendingError).
    void deferError(std::string);
    // The store changed outside the canvas (a relink, the import ledger read):
    // reload, re-render when the structure changed (no selection change).
    void storeChanged();
    // Accessibility and verification actions (:596-612): shelf:add,
    // shelf:clear, shelf:<id>:select|open|reveal|remove (+ Windows :relink).
    bool perform(std::string_view);
    // ShelfFileProperties.read's completion (:378-385).
    void propertiesRead(const std::string& id, ShelfFileProperties);
    // Box rect of a built tile (drag images, verification).
    std::optional<design::Rect> tileRect(std::string_view id) const;

    // Formatting (:682-721), exposed for the contract suites.
    static bool isPackage(const FileShelfItem&) noexcept;            // false on Windows
    static Extension tileKind(const FileShelfItem&);
    static std::string extensionLabel(const FileShelfItem&);
    static std::string kindText(const FileShelfItem&, core::Language);
    static std::string location(const FileShelfItem&, std::string_view homeFolder);
    static std::optional<std::string> sizeTag(const FileShelfItem&, core::Language);
    static std::string extensionOf(std::string_view path);          // URL.pathExtension

    // The archive grid (:197-222): 4 columns (3 beside the card), gap 12,
    // padding 5 for the brackets, at most 420 tall.
    struct GridLayout {
        static constexpr double gap = 12, padding = 5, tileHeight = 124;
        int columns{};
        double tileWidth{}, height{}, contentHeight{};
        GridLayout(double width, int columns, std::size_t count);
        double pitch() const noexcept { return tileHeight + gap; }
        design::Rect frame(std::size_t index) const noexcept;
        std::pair<std::size_t, std::size_t> indices(double offset, std::size_t count) const noexcept; // [begin, end)
    };

    // --- design::Canvas ---
    void buildPage(design::Page&) override;
    void show(double now) override;               // makeContent (:103-107)
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override;
    void playEntrance(design::Point, double now) override;
    void recordingModeChanged() override;
    void didInstall(design::Page&) override;      // the rerender override (:510-513)
    bool interceptPointerDown(design::Point, const design::PointerInput&) override;
    bool handlePointerDown(design::Point, const design::PointerInput&) override;
    bool handleScroll(design::Point, design::Point delta) override;
    bool handleKey(const design::KeyInput&) override;
    bool handleRightPointerDown(design::Point) override;
    bool inputLocked() const override;
    bool presentingPanel() const override;
    // Windows addition: the card's name and rows as UI Automation text items
    // ("props.name", "props.row<n>": term and value), after the Mac's controls,
    // so screen readers keep the details the v1.2.0 card peer read out.
    void accessibilityItems(std::vector<design::AccessibilityItem>&) const override;

    // Testing (:725-728).
    std::optional<int> columnsForTesting() const noexcept { return layout_ ? std::optional(layout_->columns) : std::nullopt; }
    std::size_t builtTileCountForTesting() const noexcept { return tiles_.size(); }
    bool hasPropertiesCardForTesting() const noexcept { return card_ != nullptr; }
    const std::map<std::string, ShelfFileProperties, std::less<>>& propertiesForTesting() const noexcept { return properties_; }
    bool clearingForTesting() const noexcept { return clearing_; }
private:
    class Tile;
    friend class Tile;
    struct Structure {
        std::vector<FileShelfItem> items; std::optional<std::string> card;
        bool operator==(const Structure&) const = default;
    };
    FileShelfServices services_;
    std::vector<FileShelfItem> items_;
    std::set<std::string, std::less<>> selected_;
    std::optional<std::string> focus_, anchor_, pendingError_;
    std::set<std::string, std::less<>> pendingReveal_;
    bool dropTarget_{}, clearing_{};
    std::map<std::string, ShelfFileProperties, std::less<>> properties_;
    std::optional<std::string> propertiesRequest_;
    design::PointerInput press_;                  // NSApp.currentEvent at the latest press
    // The current page.
    design::ScrollArea* grid_{};
    std::optional<GridLayout> layout_;
    std::map<std::string, Tile*, std::less<>> tiles_, parked_;
    std::optional<std::string> pressedTile_;
    design::Layer *card_{}, *dropRing_{};
    std::function<void()> cardBinder_;
    struct CardRow { std::string id, label; const design::Layer* value{}; std::string redacted; design::Rect line; };
    std::vector<CardRow> cardRows_;
    std::optional<Structure> renderedStructure_;
    std::set<std::string, std::less<>> renderedSelection_;
    std::optional<std::string> renderedCard_;
    design::Deadlines::Token clearTimer_{};

    Structure structure() const;
    std::optional<std::string> cardID() const;
    const FileShelfItem* item(std::string_view) const;
    void refreshStore();
    void reloadItems();
    void syncTiles(design::Page&, double offset, bool routing);
    void scrollGrid(double offset);
    void revealTile(std::string_view id);
    void buildCard(const FileShelfItem&, design::Page&, double top, bool replaying);
    void loadProperties(const FileShelfItem&);
    void selectionChanged();
    void beginDrag(const std::string& id);
    void chooseFiles();
    void reveal(const std::set<std::string, std::less<>>&, bool animated);
    void finishClear();
    void reportUnavailable();
    void notifyIcons() const;
    void inputChanged() const;
};
} // namespace endfield::modules
