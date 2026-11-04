#pragma once
// Media Assembly's panel components and its stage on the design library: the
// ports of Sources/MediaAssemblyControls.swift (MediaAssemblyTool,
// MediaAssemblyParameter, MediaAssemblyToolTile, MediaAssemblyPresetCell,
// MediaAssemblyZoomValue, MediaAssemblyParameterRow) and of the
// MediaAssemblyStage of Sources/MediaAssemblyViewport.swift:181-620.
//
// The stage is built once per canvas (canvas-owned layers and controls, the
// Mac's `lazy var stage`) and joins each page when it is installed; every
// change is an in-place layer update. What the rasterizer cannot paint (the
// rendered preview, the cross-faded previous one and the playing video) is
// drawn by the module owner from the picture layer's presented pose
// (tools/media_assembly_owner.hpp: NativeDesignScene::setInsertion right after
// the picture slot, i.e. between the dot grid and the stickers). The rotator /
// picture pair carries the Mac's bounds, position, scale and rotation
// animations as channels of two unit layers: rotator scaleX/Y = the local
// size (the Mac's bounds), rotation = the cumulative angle, picture scaleX/Y =
// the transition flips.
//
// Adaptations (the library has no border-width or contents animation): the
// preset cell's 1 -> 2 px yellow ring cross-fades a 2 px ring over the tinted
// 1 px one (.22 s ease, identical at rest); the stage's contents cross-fade
// (.12 s ease) is the owner drawing the previous preview under the new one
// at the fade layer's opacity; the seek slider is built with each page (the
// Mac keeps one per duration; a page rebuild ends a seek drag).
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_page.hpp"
#include "modules/media_assembly_controller.hpp"

namespace endfield::modules {
// Sources/MediaAssemblyControls.swift:5-27: the Adjust tab's tool sub-pages
// (Trim exists for video sources).
enum class MediaAssemblyTool {crop,adjust,curves,levels,trim};
std::string_view mediaAssemblyToolID(MediaAssemblyTool) noexcept;         // rawValue
std::optional<MediaAssemblyTool> mediaAssemblyTool(std::string_view) noexcept;
std::string mediaAssemblyToolTitle(MediaAssemblyTool, core::Language);
design::Icon mediaAssemblyToolIcon(MediaAssemblyTool) noexcept;

// One slider of a tool page (:31-89): its range and default and how it reads
// and writes the session's adjustments. Centre-zero (zero 0) when min < 0.
struct MediaAssemblyParameter {
    std::string id, title;
    design::Slider::Range range;
    double defaultValue{};
    std::function<double(const MediaAssemblyAdjustments&)> read;
    std::function<void(MediaAssemblyAdjustments&, double)> write;
    // Two decimals, or an integer when the step is at least 1.
    design::ValueFormat format() const noexcept { return range.step >= 1 ? design::ValueFormat::integer : design::ValueFormat::decimal; }
    // The sliders of `tool`; trim needs the video's duration; none for Crop.
    static std::vector<MediaAssemblyParameter> parameters(MediaAssemblyTool, double duration, core::Language);
};

// A light tool tile of the Adjust tab (:94-142): 62 px with the 18 px icon
// above the label, or a 36 px wide row with them side by side. Hover lifts it
// 2 px and brightens it (white .06); press .965; `on` turns it yellow (Mirror).
class MediaAssemblyToolTile final : public design::Control {
public:
    MediaAssemblyToolTile(design::Builder&, std::string id, std::string title, design::Icon, design::Size);
    bool isOn() const noexcept { return on_; }
    void setOn(bool);
    void hoverChanged() override;
    void pressChanged() override;
    std::optional<design::AccessibilityRole> accessibilityRole() const override;
    void accessibilityItems(const design::Layer& root, std::vector<design::AccessibilityItem>&) const override;
private:
    design::Builder* builder_;
    design::Layer *plate_, *brighten_;
    design::Size size_;
    bool on_{};
    void paint();
};

// A filter or sticker cell (:147-205): dark well (charcoal for stickers,
// #1E1E1E for filters), the asset thumbnail, the inset ring (#363636, hover
// #777777, selected 2 px yellow, .22 s ease) and for filters a name strip
// that turns yellow when selected; None shows ∅. Hover lifts it 2 px. Each
// cell is its own part (the scroll area's visibility window).
class MediaAssemblyPresetCell final : public design::Control {
public:
    // title: the name strip (filters); nullopt for stickers.
    MediaAssemblyPresetCell(design::Builder&, std::string id, std::string label, std::optional<std::string> title,
        std::optional<design::ImageSource> image, design::Size);
    bool isSelected() const noexcept { return selected_; }
    // Unanimated while the page is built (the Mac's detached layers take no animation).
    void setSelected(bool, bool animated = true);
    void hoverChanged() override;
    std::optional<design::AccessibilityRole> accessibilityRole() const override;
    void accessibilityItems(const design::Layer& root, std::vector<design::AccessibilityItem>&) const override;
private:
    design::Builder* builder_;
    design::Layer *ring_, *ringSelected_, *strip_{}, *name_{};
    bool selected_{}, filter_{};
    std::optional<bool> stripSelected_;
    void paint(bool animated);
};

// The zoom readout between − and + (:209-232): it shows the zoom and resets
// zoom and pan. 58 x 30, field fill, ringField border (hover #888888, .2 s ease).
class MediaAssemblyZoomValue final : public design::Control {
public:
    MediaAssemblyZoomValue(design::Builder&, std::string label);
    void setZoom(double);
    const std::string& valueText() const noexcept { return value_; }
    void hoverChanged() override;
private:
    design::Layer *ring_, *text_;
    std::string value_;
};

// One row of a tool's slider list (:236-269): name and value on the label
// line, the dark slider below. 146 x 44.
class MediaAssemblyParameterRow final {
public:
    static constexpr double height = 44;
    MediaAssemblyParameterRow(design::Builder&, MediaAssemblyParameter, double value, double width);
    design::Layer& layer() const noexcept { return *layer_; }
    design::Slider& slider() const noexcept { return *slider_; }
    const MediaAssemblyParameter& parameter() const noexcept { return parameter_; }
    // Moves the slider (unless dragged) and the label readout, in place.
    void setValue(double);
    void showDragValue(double);
    std::string valueText() const;
private:
    MediaAssemblyParameter parameter_;
    design::Layer *layer_, *value_;
    design::Slider* slider_;
};

// The 448 x 412 stage (:186-620): dot grid, the edited picture (and the video
// while it plays), stickers in image space, the selection handles, the crop
// overlay, info chips, the zoom / playback / export bars and the empty state.
class MediaAssemblyStage final {
public:
    static constexpr design::Size size{448, 412};
    static constexpr design::Rect bounds{0, 0, 448, 412};
    using Image = std::shared_ptr<const MediaAssemblyPreviewImage>;
    // The stage owns its layers and controls (it outlives pages; the canvas
    // tracks its arena). `stickerImage`: the sticker artwork at its pixel size
    // (MediaAssemblyAssetCatalog.stickerImage). Built in the palette of its
    // time: the canvas builds a new one for a new palette generation (the Mac
    // keeps its lazy stage's first colours after a theme or accent change).
    MediaAssemblyStage(design::Environment&, std::string idPrefix,
        std::function<std::optional<design::ImageSource>(MediaAssemblyStickerKind)> stickerImage);
    MediaAssemblyStage(const MediaAssemblyStage&) = delete;
    MediaAssemblyStage& operator=(const MediaAssemblyStage&) = delete;
    // Carries the shown pictures and their transform over from the stage it replaces.
    void adopt(const MediaAssemblyStage&);
    const design::LayerArena& arena() const noexcept { return arena_; }
    std::uint64_t paletteGeneration() const noexcept { return palette_; }
    design::Layer& layer() const noexcept { return *layer_; }
    design::IconButton& zoomOut() const noexcept { return *zoomOut_; }
    design::IconButton& zoomIn() const noexcept { return *zoomIn_; }
    MediaAssemblyZoomValue& zoomValue() const noexcept { return *zoomValue_; }
    design::IconButton& play() const noexcept { return *play_; }
    design::IconButton& pause() const noexcept { return *pause_; }
    design::Button& open() const noexcept { return *open_; }
    design::Slider* seek() const noexcept { return seek_; }
    // The controls the page routes to (hidden ones never receive input).
    std::vector<design::Control*> controls() const;

    // `[ NO SOURCE ]` with its line and the Open button; hides everything else.
    void setEmpty(bool, std::string subtitle);
    bool isEmpty() const noexcept { return empty_; }

    // Shows a newly rendered preview; filter and adjustment changes cross-fade (.12 s ease).
    void setImage(Image, bool crossfade);
    const Image& image() const noexcept { return image_; }
    const Image& previousImage() const noexcept { return previous_; }
    // The owner gave the hidden editor's preview back (Windows keeps no
    // preview pixels while the module is not composed).
    void releaseImages() noexcept { image_.reset(); previous_.reset(); }
    // Places the picture in `rect` with `transform` over the shown preview;
    // `animated` plays the rotate / mirror transition (.25 s E).
    void layoutPicture(design::Rect, const MediaAssemblyPictureTransform&, bool animated);
    const MediaAssemblyPictureTransform& transform() const noexcept { return transform_; }
    // The playing video shares the picture's place (shown while playing).
    void setPlayer(bool playing) noexcept { videoShown_ = playing; }
    bool videoShown() const noexcept { return videoShown_; }
    // Image, stickers and selection fade (.16 s ease).
    void setContentOpacity(double, bool animated);
    // The unit-square picture (owner-drawn previews and video), the layer
    // whose opacity is the incoming preview's cross-fade, and the part root
    // the owner's records follow.
    const design::Layer& picture() const noexcept { return *picture_; }
    const design::Layer& pictureFade() const noexcept { return *fade_; }
    const design::Layer& pictureSlot() const noexcept { return *slot_; }

    // Sticker artwork in image space; `base` is the output frame on the stage.
    void setStickers(std::span<const MediaAssemblySticker>, design::Rect base);
    // The yellow outline with ↻, × and the corner squares, turned with the sticker.
    void setSelection(const MediaAssemblySticker*, design::Rect base);
    bool isSelectionVisible() const noexcept;
    // Handles grow to 1.12 under the pointer (.2 s E).
    void setHovered(std::optional<MediaAssemblyHandle>);
    // The crop overlay for a normalized display crop over `image`; nullopt hides it.
    void setCrop(std::optional<design::Rect>, design::Rect image);
    bool isCropVisible() const noexcept;

    // Dark source name, yellow filter name and the outline `n/16` count.
    void setChips(std::string source, std::optional<std::string> filter, std::size_t stickers);
    std::vector<std::string> chipTitles() const;
    // Shortens a file name in the middle, keeping its extension, to `width`.
    static std::string fitted(const std::string& name, double width, const design::Environment&);
    void setZoom(double zoom) { zoomValue_->setZoom(zoom); }
    // The seek slider for a video of `duration` seconds (nullopt: images),
    // built in the page's builder; installPlayback() moves it into the bar
    // when its page is installed.
    design::Slider* configurePlayback(design::Builder& page, std::optional<double> duration, std::string label);
    void installPlayback();
    void setPlayback(bool visible, bool playing, double time, double duration);
    // `EXPORTING n%` with ten segments, right-aligned in the bar; nullopt hides it.
    void setExport(std::optional<double>, const std::string& title);
    std::optional<std::string> exportText() const;
private:
    design::LayerArena arena_;
    design::ControlSet set_;
    design::Builder builder_;
    std::uint64_t palette_;
    std::function<std::optional<design::ImageSource>(MediaAssemblyStickerKind)> stickerImage_;
    design::Layer *layer_, *grid_, *content_, *slot_, *rotator_, *picture_, *fade_, *stickers_, *crop_, *selection_, *outline_;
    design::Layer *chips_, *zoomGroup_, *playback_, *time_, *exportGroup_, *exportText_, *emptyLayer_, *emptyTitle_, *emptySubtitle_, *ring_;
    std::array<design::Layer*, 4> cropHandles_{};
    std::array<design::Layer*, 10> exportCells_{};
    std::array<std::pair<MediaAssemblyHandle, design::Layer*>, 6> handles_{};
    std::array<std::unique_ptr<design::Chip>, 3> chipViews_{};
    design::IconButton *zoomOut_, *zoomIn_, *play_, *pause_;
    MediaAssemblyZoomValue* zoomValue_;
    design::Button* open_;
    design::Slider *seek_{}, *pendingSeek_{};
    bool pendingSeekSet_{};
    std::uint64_t stickerSerial_{};
    // The sticker layers: at most 16 at once (MediaAssemblyAdjustments limit),
    // reused by id (a free slot drops its artwork).
    struct StickerSlot { design::Layer* layer{}; std::string id; MediaAssemblyStickerKind kind{}; double density{}; bool used{}; };
    std::array<StickerSlot, 16> stickerSlots_{};
    std::optional<MediaAssemblyHandle> hoveredHandle_;
    std::array<std::string, 3> chipTitles_;
    Image image_, previous_;
    MediaAssemblyPictureTransform transform_;
    std::optional<design::Rect> pictureRect_;
    std::uint64_t fadeSerial_{};
    double emptyScale_{};
    bool empty_{}, videoShown_{};
    design::Environment& env() const noexcept { return builder_.env; }
};
} // namespace endfield::modules
