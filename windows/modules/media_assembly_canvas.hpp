#pragma once
// Media Assembly (spec section 3.10): the port of Sources/MediaAssemblyCanvas.swift
// on the design library. The 448 x 412 stage on the left and the Adjust /
// Filters / Stickers panel on the right, 610 px across the box (margin -10).
// Edits live in the session's MediaAssemblyController (one worker renders
// the latest preview), so a slider drag only writes adjustments and the
// stage takes the rendered preview in place.
//
// The platform services are injected (MediaAssemblyCanvasServices): the
// Shelf rows and their file access, the native open / save dialogs (the
// owner posts them; the answer comes back through panelFinished), file
// existence and identity, and the drawer / stage artwork (bounded thumbnail
// decodes and the sticker images). The module owner
// (tools/media_assembly_owner.hpp) draws the rendered previews and the
// playing video on the stage's picture and runs the GPU and the player.
//
// Windows adaptations (recorded in the port report):
//  - "Choose in Finder" is the File Explorer chooser row ("Choose in File
//    Explorer", core::chooseInFileExplorer).
//  - The wheel: Windows has no hasPreciseScrollingDeltas; fractional detents
//    are a precision touchpad (pans), whole detents a mouse wheel (x1.1 per
//    notch about the pointer); Alt stands in for ⌘ / ⌥ (zoom with a
//    touchpad); a touchpad pinch (Ctrl + wheel) is the Mac's magnify.
//  - Keys: Win and Ctrl stand in for ⌘ / ⌃ (not handled), Alt for ⌥.
//  - The stage keeps no preview while the module is not composed (the owner
//    releases it; the Mac keeps its last CGImage in the layer).
//  - The stage is built again for a new palette (the Mac's lazy stage keeps
//    its first theme and accent: a Mac bug, reported, not copied).
#include "modules/media_assembly_controls.hpp"

namespace endfield::modules {
// One Temporary File Shelf row offered for import (NotesShelfMediaChoice).
struct MediaAssemblyShelfChoice {
    std::string id, title;
    bool supported{}, available{};
};
// An opened Shelf row (ShelfFileAccess): its path and the access lease
// released after the import.
struct MediaAssemblyShelfFile { std::string path; std::shared_ptr<void> lease; };
struct MediaAssemblyCanvasServices {
    std::function<double()> now;                    // the host clock (controller notices arrive in the executor's drain)
    std::function<std::string()> uuid;              // sticker identities
    std::function<std::vector<MediaAssemblyShelfChoice>()> shelfChoices;
    // Throws MediaAssemblyFailure (reported) or returns nullopt (unavailable).
    std::function<std::optional<MediaAssemblyShelfFile>(std::string_view id)> shelfAccess;
    // NSOpenPanel / NSSavePanel: the owner presents its modal dialog and
    // answers with MediaAssemblyCanvas::panelFinished; false: no dialog.
    std::function<bool()> chooseFile;
    std::function<bool(std::string folder, std::string filename, std::vector<std::string> extensions)> saveAs;
    std::function<void()> cancelPanel;
    std::function<bool(const std::string&)> fileExists;                    // the save panel's replacement already confirmed
    std::function<bool(const std::string&, const std::string&)> samePath;  // standardized, symlinks resolved
    std::function<std::string(const std::string&)> folderOf;
    // MediaAssemblyAssetCatalog: filter thumbnails (168 px), sticker
    // thumbnails (96 px) and sticker images (pixelSize). nullopt: none drawn.
    std::function<std::optional<design::ImageSource>(MediaAssemblyFilter)> filterThumbnail;
    std::function<std::optional<design::ImageSource>(MediaAssemblyStickerKind)> stickerThumbnail, stickerImage;
};

class MediaAssemblyCanvas final : public design::Canvas {
public:
    enum class Tab { adjust, filters, stickers };
    static constexpr design::Point stageOrigin{-10, design::metric::headerHeight};
    static constexpr design::Rect panelFrame{448, design::metric::headerHeight, 152, MediaAssemblyStage::size.height};
    MediaAssemblyCanvas(design::Context&, MediaAssemblyController&, MediaAssemblyCanvasServices);
    ~MediaAssemblyCanvas() override;

    // controller.onChange (owner: every controller notice).
    void controllerChanged();
    // Drops and the file panel: the first file replaces the source.
    void importFiles(std::span<const std::string> paths, double now);
    // The open / save dialog's answer (nullopt: cancelled).
    void panelFinished(std::optional<std::string> path, double now);
    // The wheel's kind for the next scroll (see the header: precise = a
    // fractional detent, modifier = Alt).
    void setWheelKind(bool precise, bool modifier) noexcept { wheelPrecise_ = precise; wheelModifier_ = modifier; }
    // Activates a visible control by id and settles any page change (scripted checks).
    bool perform(std::string_view id, double now);
    // The owner released the hidden editor's preview.
    void releasePreview() noexcept;

    // --- State (tests, owner) ---
    MediaAssemblyController& controller() const noexcept { return *controller_; }
    Tab tab() const noexcept { return tab_; }
    std::optional<MediaAssemblyTool> tool() const noexcept { return tool_; }
    const MediaAssemblyViewport& viewport() const noexcept { return viewport_; }
    const std::string& selectedSticker() const noexcept { return selectedSticker_; }
    bool isPresented() const noexcept { return presented_; }
    bool isEditable() const noexcept;
    bool isExporting() const noexcept;
    bool closing() const noexcept { return closing_; }
    std::optional<double> exportShown() const noexcept { return exportShown_; }
    MediaAssemblyStage& stage();
    const std::vector<MediaAssemblyParameterRow*>& parameterRows() const noexcept { return parameterRows_; }
    design::Rect imageRect() const;
    design::Rect displayCropRect() const;
    std::string counterText() const;

    // --- design::Canvas ---
    void buildPage(design::Page&) override;
    void didInstall(design::Page&) override;
    void show(double now) override;                 // makeContent (:94-97)
    void willLeave() override;
    void didActivate() override;
    void didDeactivate() override;
    void recordingModeChanged() override {}
    bool pointerDown(design::Point, const design::PointerInput&, double now) override;
    bool handlePointerDown(design::Point, const design::PointerInput&) override;
    void handlePointerDragged(design::Point) override;
    void handlePointerUp(design::Point) override;
    void handlePointerMoved(std::optional<design::Point>) override;
    bool handleScroll(design::Point, design::Point delta) override;
    bool handleMagnify(design::Point, double) override;
    bool handleKey(const design::KeyInput&) override;
    void accessibilityItems(std::vector<design::AccessibilityItem>&) const override;
    bool adjustAccessibilitySlider(std::string_view id, double value, double now) override;
    design::Rect hitBounds() const override { return {-10, 0, design::metric::boxWidth + 20, design::metric::boxHeight}; }
    bool inputLocked() const override { return Canvas::inputLocked() || drag_.has_value(); }
    bool presentingPanel() const override { return panel_.has_value(); }
private:
    struct Drag {
        enum class Kind { pan, move, scale, rotate, crop } kind{};
        design::Point start;                        // pan: the previous point; move: the press
        std::string id;                             // the sticker
        MediaAssemblySticker original;
        int corner{};
    };
    struct Structure {
        core::Language language{}; std::string document; bool video{}, busy{}, exporting{}, closing{};
        Tab tab{}; std::optional<MediaAssemblyTool> tool;
        bool operator==(const Structure&) const = default;
    };
    struct Panel { bool save{}; std::string document; };
    enum class ExportOutcome { saved, overwritten };
    MediaAssemblyController* controller_;
    MediaAssemblyCanvasServices services_;
    Tab tab_{Tab::adjust};
    std::optional<MediaAssemblyTool> tool_;
    MediaAssemblyViewport viewport_;
    std::string selectedSticker_;
    bool presented_{};
    std::unique_ptr<MediaAssemblyStage> stage_;
    std::vector<std::unique_ptr<MediaAssemblyStage>> retiredStages_;
    std::optional<Panel> panel_;
    std::optional<Drag> drag_;
    std::optional<std::string> sourceID_;
    // The geometry the shown preview was rendered with (spec §3.10 transitions).
    MediaAssemblyGeometry shownGeometry_;
    double geometryAnimationEnd_{};
    design::Deadlines::Token pendingPreview_{}, closeWork_{}, exportTicker_{};
    bool animateNextLayout_{}, revealOnPreview_{}, clearOnBuild_{}, closing_{}, reloadingAfterOverwrite_{};
    std::optional<double> exportShown_;
    std::optional<ExportOutcome> exportOutcome_;
    std::string exportName_;
    std::optional<std::string> lastError_;
    bool handlingChange_{}, changePending_{}, quiet_{};
    std::optional<Structure> renderedStructure_;
    std::vector<std::function<void()>> binders_;
    std::vector<MediaAssemblyParameterRow*> parameterRows_;
    // The page whose stage slot is filled at install (the stage joins it then).
    const design::Page* stagePage_{};
    bool wheelPrecise_{}, wheelModifier_{};

    Structure structure() const;
    void setPresented(bool);
    void processChange();
    void sourceChanged();
    void refreshInPlace();
    void reportError();
    template <class F> void quietly(F&& body) { const bool previous = quiet_; quiet_ = true; body(); quiet_ = previous; }
    void buildHeader(design::Page&, const MediaAssemblyDocumentInfo*);
    void wire(MediaAssemblyStage&);
    void relabelStageControls();
    void buildPanel(design::Page&, design::Layer& panel);
    void buildTiles(design::Page&, design::Layer& panel, bool enabled);
    void buildToolHeader(MediaAssemblyTool, design::Page&, design::Layer& panel, bool enabled);
    void buildCropPage(design::Page&, design::Layer& panel, bool enabled);
    void buildSliderPage(MediaAssemblyTool, design::Page&, design::Layer& panel, bool enabled);
    void buildFilters(design::Page&, design::Layer& panel, bool enabled);
    void buildStickers(design::Page&, design::Layer& panel, bool enabled);
    void selectTab(Tab, double direction);
    void toolTile(std::string_view name);
    void openTool(MediaAssemblyTool);
    void closeTool();
    void leaveTool();
    void resetTool(MediaAssemblyTool);
    void parameterChanged(MediaAssemblyParameterRow&, double value, bool final);
    bool hits(const design::Control&, design::Point) const;
    void selectFilter(MediaAssemblyFilter);
    void addSticker(MediaAssemblyStickerKind);
    void confirmReset();
    void openSourceMenu(const design::Control& anchor);
    void openShelfMenu(const design::Control* anchor);
    void chooseFile();
    void confirmClose();
    void closeSource();
    void finishClose();
    void exportPressed(const design::Control& anchor);
    void saveAs(const MediaAssemblyDocumentInfo&);
    void confirmOverwrite(const MediaAssemblyDocumentInfo&);
    void startExport(const std::string& path, bool overwrite, const MediaAssemblyDocumentInfo&, bool original);
    void exportFinished(std::optional<std::string> target, bool failed, std::string name, bool original);
    void startExportTicker();
    void scheduleExportTick();
    void stopExportTicker();
    void exportTick();
    void finishExportDisplay();
    MediaAssemblyGeometry targetGeometry() const;
    double duration() const;
    core::Point shownSize() const;
    core::Point layoutSize() const;
    design::Rect stickerBase(design::Rect image) const;
    const MediaAssemblySticker* selectedItem() const;
    const MediaAssemblySticker* sticker(std::string_view id) const;
    void takePreview();
    void layoutStage();
    static design::Point stagePoint(design::Point);
    void zoomStep(double factor);
    bool zoom(double factor, std::optional<design::Point> at);
    void resetZoom();
    void beginDrag(design::Point local);
    void select(std::string id);
    void replaceSticker(std::string id, MediaAssemblySticker);
    void removeSticker(std::string id);
    bool adjustSelectedSticker(double dx, double dy, double size, double rotation);
    void updateAdjustments(MediaAssemblyAdjustments);
    void updateTrim(double start, double end, double scrubTo);
};
} // namespace endfield::modules
