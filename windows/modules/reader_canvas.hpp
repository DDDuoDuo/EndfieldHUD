#pragma once
// E-Reader (spec section 3.15): the port of Sources/ReaderCanvas.swift (Mac
// 1.3.0) on the design library: Open / Library / Reading / Marks. The
// library, progress, bookmarks and the PDF / EPUB / TXT rendering stay in
// modules::ReaderState (ReaderController); this canvas presents them. The
// page renders on the paper tone at 1/1.4 of the panel (420 x 249.29 page
// pt shown in the 588 x 349 viewport), so the 10 pt default reads at 14 px.
// Turning a page moves only the paper's pages: left to right a sliding page
// (.3 s E, additive translation), top to bottom one continuous, freely
// scrolled column (pages joined at (textHeight + lineSpacing) x 1.4, cropped
// by the margin inset); the paper, header, tabs and controls never animate.
//
// The page rasters are not design artwork: pageViews() names the page
// layers (animatable, content-less, at most six, recycled like the Mac's)
// with their pixels and crop, and the native owner (tools/
// reader_canvas_owner.hpp) draws the provider's textures at those layers'
// sampled poses inside the viewport clip, below the bookmark ribbon (its own
// part, ribbon()). Nothing is rasterized for a page turn or a scroll step.
//
// Replaces the v1.2.0 port (modules/reader_presentation, reader_viewport,
// reader_accessibility, tools/reader_preview, tools/reader_automation,
// native/reader_scene): no Open chooser menu, no Library / Bookmarks list
// menus, no settings menu, no ★ / zoom buttons, no in-page status text, no
// push transition of the whole clip, no point-scrolling keys (inventory
// DOCS-755..762).
#include "modules/hud_design_page.hpp"
#include "modules/reader_state.hpp"
#include <set>

namespace endfield::modules {
// A shelf entry the Reader can open (SystemHUDView's shelfChoices for the
// Reader: pdf / epub / txt, filtered to isSupported && isAvailable).
struct ReaderShelfChoice {
    std::string id, title;
    bool operator==(const ReaderShelfChoice&) const = default;
};
// The host's services (SystemHUDView.swift:3163-3178 and the NSOpenPanel
// sheet). All run on the UI thread; results come back later through the
// canvas's host entries below.
struct ReaderCanvasServices {
    std::function<std::vector<ReaderShelfChoice>()> shelfChoices;
    // The file chooser (ReaderDocument.extensions, one file), modal above
    // the HUD. `relinkBook` empty: Choose file (the chosen file is imported);
    // otherwise Relink... of that book with the message `title` (Locate {0}).
    // False when the chooser could not be shown.
    std::function<bool(std::optional<std::string> title, std::string relinkBook)> presentPanel;
    std::function<void()> closePanel;
    // From shelf: import the shelf entry through its retained access.
    std::function<void(std::string shelfID)> openShelf;
    // ReaderCanvas.isReachable for every book, off the UI thread; the
    // completion gets the ids of the books whose file is missing.
    std::function<void(std::vector<ReaderBook>, std::function<void(std::set<std::string>)>)> checkMissing;
    // NSFontManager.availableFontFamilies.sorted() (the installed families).
    std::function<std::vector<std::string>()> fontFamilies;
    std::function<std::string()> newID;            // bookmark UUIDs (uppercase)
};

class ReaderCanvas;
// ReaderBookTile (:1016-1119): the archive tile's well, code bar and label
// strip with a rotated cover in the book's colour, the format tag, a
// progress bar, the MISSING overlay and the ⋯ button; the open book has the
// brackets (inset -5 -> -4 cross-faded like the archive tile's, G2).
class ReaderBookTile final : public design::Control {
public:
    class More;
    static constexpr double wellHeight = 120;
    ReaderBookTile(design::Builder&, std::string id, int number, const ReaderBook&, design::Color cover, bool missing, double width);
    design::Control& more() const noexcept;
    bool selected() const noexcept { return selected_; }
    void setSelected(bool, bool animated = true);
    void hoverChanged() override;
private:
    design::Control* more_;
    design::Layer *well_, *brighten_, *strip_, *label_, *brackets_, *bracketsRest_, *bracketsSelected_;
    bool selected_{};
    void renderSelection(bool animated);
};

class ReaderCanvas final : public design::Canvas {
public:
    enum class Tab : std::uint8_t { open, library, reading, marks };
    // The paper page (box px) between the tabs and the control row, and the
    // rendered page inside it (paper px, :80-89).
    static constexpr design::Rect pageFrame{0, design::metric::headerHeight + 34 + 6, design::metric::boxWidth, 351};
    static constexpr design::Rect pageViewport{1, 1, design::metric::boxWidth - 2, 349};
    static constexpr double controlsTop = pageFrame.y + pageFrame.height + 6;
    static constexpr double textScale = 1.4;
    static constexpr core::Point renderSize{pageViewport.width / textScale, pageViewport.height / textScale};
    static constexpr std::array<std::uint32_t, 8> coverColors{0x2A5D7A, 0x7A2A2A, 0x555555, 0x6B5A12, 0x3D5A2A, 0x4A3A6B, 0x2A4A4A, 0x6B3A1E};
    static constexpr std::size_t maximumPageLayers = 6;

    ReaderCanvas(design::Context&, ReaderState&, ReaderCanvasServices);
    ~ReaderCanvas() override;

    // --- Host entries (SystemHUDView wiring, the picker and import route) ---
    // controller.onChange: the state's revision moved (whether shown or not).
    void stateChanged();
    // controller.onEvent: "imported", "deleted", "bookmarked", "settings",
    // "progress" (handle, :144-163).
    void handleEvent(std::string_view action);
    // The chooser closed (any result; the Mac panel's completion).
    void panelClosed();
    // importBook (:709-712): a chosen / dropped / shelf file starts opening.
    void importBegan(std::string name, bool shelf);
    // The file could not be opened (ReaderController.fail / a shelf access
    // error): the Notice toast with the error text.
    void importFailed(std::string message);
    // ReaderController.relink's completion (:684-690).
    void relinked(std::string_view bookID, bool success);
    // importFiles (:703-707): a drop over the active Reader closes popups
    // first; the host then imports the first pdf / epub / txt.
    bool acceptsImport() const noexcept;
    void prepareImport();
    // The wheel's kind for the next handleScroll (fractional detents: a
    // precision touchpad, hasPreciseScrollingDeltas).
    void setWheelPrecise(bool v) noexcept { wheelPrecise_ = v; }

    // --- The page layers for the native owner -----------------------------
    struct PageView {
        const design::Layer* layer{};              // its frame: the shown part of the page (box px under the column)
        std::shared_ptr<const ReaderPagePixels> pixels;
        double cropTop{0}, cropHeight{1};         // contentsRect y / height (unit of the raster)
        double zPosition{};
    };
    std::span<const PageView> pageViews() const noexcept { return views_; }
    const design::Layer* pageClip() const noexcept { return clip_; }
    const design::Layer* ribbon() const noexcept { return ribbon_; }
    bool pagesAnimating(double now) const noexcept;

    // --- State and test hooks (the Mac's private(set) / ForTesting) --------
    Tab tab() const noexcept { return tab_; }
    const std::set<std::string>& missing() const noexcept { return missing_; }
    std::uint64_t pageTurnSequence() const noexcept { return pageTurnSequence_; }
    bool turningPage() const noexcept { return pendingTurn_.has_value() || turnToken_.has_value(); }
    double scrollOffset() const noexcept { return scrollOffset_; }
    const ReaderImageView& imageView() const noexcept { return imageView_; }
    bool panelOpen() const noexcept { return panelOpen_; }
    void select(Tab);
    struct PageNumber { std::int64_t current{}, total{}; bool operator==(const PageNumber&) const = default; };
    std::optional<PageNumber> pageNumber() const;
    static std::uint32_t coverColor(std::string_view uuid);       // :634-637 (the UUID's 16 bytes)
    static std::int64_t page(const ReaderBookmark&, const ReaderBook&); // :640-643

    // --- design::Canvas ---
    void buildPage(design::Page&) override;
    void didInstall(design::Page&) override;
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override;
    bool presentingPanel() const override { return panelOpen_; }
    bool inputLocked() const override { return Canvas::inputLocked() || panelOpen_ || panStart_.has_value(); }
    bool handleKey(const design::KeyInput&) override;
    bool handleExtraKey(const design::KeyInput&) override;
    bool handlePointerDown(design::Point, const design::PointerInput&) override;
    void handlePointerDragged(design::Point) override;
    void handlePointerUp(design::Point) override;
    bool handleScroll(design::Point, design::Point delta) override;
    bool handleMagnify(design::Point, double) override;
    void accessibilityItems(std::vector<design::AccessibilityItem>&) const override;
    bool accessibilityAction(std::string_view) override;
private:
    struct BookKey {
        std::string id, title; std::int64_t progress{}; bool missing{};
        bool operator==(const BookKey&) const = default;
    };
    // What the current page's layout depends on (:61-67); anything else updates in place.
    struct Structure {
        Tab tab{}; core::Language language{}; std::vector<BookKey> books; std::optional<std::string> selected;
        std::vector<std::string> marks; bool hasPage{}, loading{}; std::optional<std::string> error;
        std::optional<std::int64_t> pageCount; bool awaiting{}, vertical{};
        bool operator==(const Structure&) const = default;
    };
    struct Slot {
        design::Layer* layer{};
        std::optional<ReaderLocation> location;
        std::shared_ptr<const ReaderPagePixels> pixels;
        double cropTop{0}, cropHeight{1};
    };
    struct PendingTurn { ReaderLocation from; double direction{}; };
    ReaderState* state_;
    ReaderCanvasServices services_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    Tab tab_{Tab::library};
    bool tabSettled_{};
    std::set<std::string> missing_;
    std::uint64_t missingGeneration_{};
    std::vector<std::string> checkedBooks_;
    bool panelOpen_{};
    std::optional<std::pair<std::string, bool>> pendingImport_;
    std::optional<std::pair<bool, std::int64_t>> pendingBookmark_;
    bool pendingRemoval_{};
    std::optional<std::string> lastError_;
    ReaderImageView imageView_;
    std::optional<std::string> imageBook_, awaitingBook_;
    std::optional<std::pair<design::Point, core::Point>> panStart_;
    design::Deadlines::Token detailTimer_{};
    struct Wheel { double remainder{}; bool consumed{}; double last{-1e9}; } wheel_;
    bool wheelPrecise_{};
    std::uint64_t pageTurnSequence_{};
    std::optional<PendingTurn> pendingTurn_;
    std::optional<std::uint64_t> turnToken_;
    std::uint64_t turnSerial_{};
    double scrollOffset_{};
    std::optional<ReaderPreferences> columnKey_;
    std::optional<ReaderLocation> columnLocation_;
    std::optional<Structure> renderedStructure_;
    std::vector<std::function<void()>> binders_;
    std::array<Slot, maximumPageLayers> slots_{};
    std::vector<PageView> views_;
    design::Layer *clip_{}, *column_{}, *ribbon_{}, *pageNumberText_{};
    design::Slider* pageSlider_{};
    design::IconButton *bookmarkButton_{}, *previousButton_{}, *nextButton_{}, *settingsButton_{};

    void setTab(Tab);
    Structure structure() const;
    std::string counter() const;
    std::string errorText() const;
    void controllerChanged();
    void refreshMissing();
    void buildOpen(design::Page&);
    void buildLibrary(design::Page&);
    void buildReading(design::Page&);
    void buildMarks(design::Page&);
    void renderPage();
    // Page layers (:387-543).
    double columnInset() const;
    double columnHeight(const ReaderPage&) const;
    struct Column { const ReaderPage* before{}; std::vector<const ReaderPage*> after; };
    Column column(const ReaderPage&) const;
    Slot& pageLayer(const ReaderPage&);
    void recycle(std::span<const ReaderLocation> keep);
    void layoutPages();
    void slide(const ReaderLocation& from, const ReaderPage& to, double direction);
    void settleTurn();
    void clampOffset();
    std::optional<double> columnTop(const ReaderLocation&) const;
    void scrollColumn(double delta, bool animated);
    void publishViews();
    // Actions (:632-886).
    void openBook(const ReaderBook&);
    void openBookMenu(const ReaderBook&, const design::Control& anchor);
    void confirmRemove(const ReaderBook&);
    void relink(const ReaderBook&);
    std::vector<ReaderShelfChoice> availableShelfChoices() const;
    void chooseFile();
    void openShelfMenu(const design::Control& anchor);
    bool presentPanel(std::optional<std::string> title, std::string relinkBook);
    void closePanel();
    void turn(int direction);
    void jump(std::int64_t page);
    void toggleBookmark();
    void toggleSettings();
    void openSettings(const design::Control& anchor);
    void openFontMenu(const design::Control& anchor);
    void updatePreferences(const std::function<void(ReaderPreferences&)>&);
    // Zoom and wheel (illustrated pages, :888-973).
    core::Point renderPoint(design::Point) const;
    void zoom(double amount, design::Point);
    void setImageView(ReaderImageView);
    design::Rect viewport() const noexcept;
};
} // namespace endfield::modules
