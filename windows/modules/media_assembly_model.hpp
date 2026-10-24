#pragma once
#include "core/scene.hpp"
#include "core/localization.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace endfield::modules {
struct MediaAssemblyCrop {
    double x{},y{},width{1},height{1};
    bool valid()const noexcept;
    bool operator==(const MediaAssemblyCrop&)const=default;
};
// Source MediaAssemblyFilter / MediaAssemblyStickerKind cases in allCases
// order (the Filters and Stickers grids follow it; 1.3.0 inserted special7
// and sticker29...34 mid-list). Never persisted: the source session is
// in-memory only, and the source Codable form is the raw id, not an ordinal.
enum class MediaAssemblyFilter {none,special1,special2,special3,special4,special5,special6,special7,
    filter1,filter2,filter3,filter4,filter5,filter6,filter7,filter8};
enum class MediaAssemblyStickerKind {sticker1,sticker2,sticker3,sticker4,sticker5,sticker6,sticker29,
    sticker7,sticker8,sticker9,sticker10,sticker11,sticker12,sticker13,sticker14,
    sticker15,sticker16,sticker17,sticker18,sticker19,sticker20,sticker21,sticker22,sticker30,sticker31,
    sticker27,sticker28,sticker32,sticker33,sticker34};
inline constexpr std::size_t mediaAssemblyFilterCount=16,mediaAssemblyStickerCount=30;
// rawValue and the source title pair. Assets resolve from id alone
// (MediaAssemblyAssetCatalog: "luts/<id>.rgb8", "filter-icons/<id>.png",
// "stickers/<id>.png"); the removed promotional stickers have no case.
struct MediaAssemblyCatalogItem {std::string_view id,english,chinese;};
// Source MediaAssemblyError cases. Messages are the original localized strings;
// unsupportedExport names this platform (no QuickTime MOV writer on Windows).
// sourceUnavailable is the source NotesMediaFactory.resolve failure
// (NotesMediaError.unavailable): the referenced original is missing or no
// longer a regular file when a preview/export re-opens it.
enum class MediaAssemblyError {unsupported,invalidAdjustment,unavailable,changedOnDisk,unsupportedExport,exportFailed,cancelled,exists,sourceUnavailable};
std::string mediaAssemblyErrorMessage(MediaAssemblyError,core::Language);
// Source NotesMediaReference kinds accepted by Media Assembly.
enum class MediaAssemblyKind {image,gif,video};
// Windows equivalent of MediaAssemblyFileIdentity (inode/device/size/mtime):
// volume serial, 128-bit file ID, byte size and last-write FILETIME ticks.
struct MediaAssemblyFileIdentity {
    std::uint64_t volume{};std::array<std::uint8_t,16>file{};std::uint64_t bytes{};std::int64_t modified{};
    bool operator==(const MediaAssemblyFileIdentity&)const=default;
};
// allCases order; the case of entry n is the one whose identity table row is
// n (the table pairs every case with its id and is checked at compile time).
std::span<const MediaAssemblyCatalogItem>mediaAssemblyFilters()noexcept;
std::span<const MediaAssemblyCatalogItem>mediaAssemblyStickers()noexcept;
// .rawValue / init?(rawValue:) by identity; unknown cases and ids are rejected.
const MediaAssemblyCatalogItem&mediaAssemblyFilterItem(MediaAssemblyFilter);
const MediaAssemblyCatalogItem&mediaAssemblyStickerItem(MediaAssemblyStickerKind);
std::optional<MediaAssemblyFilter>mediaAssemblyFilter(std::string_view id)noexcept;
std::optional<MediaAssemblyStickerKind>mediaAssemblySticker(std::string_view id)noexcept;
std::string mediaAssemblyFilterTitle(MediaAssemblyFilter,core::Language);
std::string mediaAssemblyStickerTitle(MediaAssemblyStickerKind,core::Language);
// Source pixelSize: the PNG canvas (sticker21 324, the 1.6 additions
// sticker29...34 640, all others 320). Stage and export draw the canvas at this
// size (MediaAssemblyAssetCatalog.stickerImage decodes to it); the installed
// provenance.json width/height must agree (native/media_assembly_assets.cpp).
core::Point mediaAssemblyStickerPixelSize(MediaAssemblyStickerKind);
struct MediaAssemblySticker {
    // Identity is supplied by the existing app owner when adding a sticker.
    std::string id;MediaAssemblyStickerKind kind{MediaAssemblyStickerKind::sticker7};
    double x{.5},y{.5},size{.2},rotation{};
    bool valid()const noexcept;
    bool operator==(const MediaAssemblySticker&)const=default;
};
struct MediaAssemblyTimeRange {
    std::int64_t startTicks{},durationTicks{};static constexpr std::int32_t timescale=600;
    bool operator==(const MediaAssemblyTimeRange&)const=default;
};
struct MediaAssemblyAdjustments {
    MediaAssemblyCrop crop;int rotationQuarterTurns{};bool mirrored{};
    double brightness{},contrast{1},saturation{1},temperature{6500},tint{};
    double highlights{1},shadows{},exposure{};
    std::array<double,5>curve{0,.25,.5,.75,1};
    double levelsBlack{},levelsWhite{1},levelsGamma{1},trimStart{};
    std::optional<double>trimEnd;MediaAssemblyFilter filter{MediaAssemblyFilter::none};
    std::vector<MediaAssemblySticker>stickers;
    bool valid()const noexcept;
    std::optional<MediaAssemblyTimeRange>timeRange(double duration)const noexcept;
    bool operator==(const MediaAssemblyAdjustments&)const=default;
};
// Source MediaAssemblyHandle (Sources/MediaAssemblyViewport.swift:129): the
// selected sticker's ↻, × and four corner squares (0 ⌜ 1 ⌝ 2 ⌞ 3 ⌟).
struct MediaAssemblyHandle {
    enum class Kind {rotate,remove,corner};
    Kind kind{Kind::rotate};int corner{};
    bool operator==(const MediaAssemblyHandle&)const=default;
};
// Only retained preview geometry: no decoder, renderer, store, clock or allocation
// in pan/zoom/crop transforms. Image editing and output pixels are separate.
// Source MediaAssemblyViewport (Sources/MediaAssemblyViewport.swift:6-127).
class MediaAssemblyViewport final {
public:
    double zoom()const noexcept{return zoom_;}core::Point pan()const noexcept{return pan_;}
    void reset()noexcept;core::Rect imageRect(core::Point imageSize,core::Rect viewport)const noexcept;
    // Wheel and pinch: zooms keeping `at` still (1...20).
    void magnify(double factor,core::Point at,core::Point imageSize,core::Rect viewport)noexcept;
    // The zoom buttons and keys (zoom(by:size:in:), :30-34): the pan is kept and clamped.
    void zoomBy(double factor,core::Point imageSize,core::Rect viewport)noexcept;
    void move(core::Point delta,core::Point imageSize,core::Rect viewport)noexcept;
    static core::Rect stickerRect(const MediaAssemblySticker&,core::Rect image);
    static core::Point unrotate(core::Point,core::Point center,double degrees)noexcept;
    static core::Point displayPoint(core::Point,int quarterTurns,bool mirrored)noexcept;
    static core::Point sourcePoint(core::Point,int quarterTurns,bool mirrored)noexcept;
    static core::Rect displayCrop(MediaAssemblyCrop,int quarterTurns,bool mirrored)noexcept;
    static MediaAssemblyCrop sourceCrop(core::Rect,int quarterTurns,bool mirrored)noexcept;
    // A crop corner dragged to `to` (normalized display space): the box keeps
    // at least .05 of each side; corners 0 ⌜ 1 ⌝ 2 ⌞ 3 ⌟ (:92-98).
    static core::Rect dragCrop(core::Rect crop,int corner,core::Point to)noexcept;
    // Top-left corners of the four 14 x 14 crop L-handles in the stage, offset
    // -3 / -11 and clamped to 4 ... stage - 18 (:102-107).
    static std::array<core::Point,4>cropHandleOrigins(core::Rect crop,core::Rect image,core::Point stageSize)noexcept;
    // Handle centres in the selected sticker's unrotated frame (:111-115):
    // ↻ (w/2, -25), × (w + 13, -13), corners just outside the outline.
    static std::array<std::pair<MediaAssemblyHandle,core::Point>,6>handleCentres(core::Point size)noexcept;
    // Corners within ±7 px, round handles within radius 11 (:117-126); no clamp to the stage.
    static std::optional<MediaAssemblyHandle>handle(core::Point,core::Rect sticker,double rotation)noexcept;
private:
    double zoom_{1};core::Point pan_;
    void clamp(core::Point imageSize,core::Rect viewport)noexcept;
};
// Source MediaAssemblyGeometry (:133-143): what a rendered preview shows (the
// full frame while the crop tool shows the uncropped picture).
struct MediaAssemblyGeometry {
    int quarterTurns{};bool mirrored{};MediaAssemblyCrop crop;
    static MediaAssemblyGeometry make(int quarterTurns,bool mirrored,MediaAssemblyCrop)noexcept; // turns normalized 0...3
    static MediaAssemblyGeometry of(const MediaAssemblyAdjustments&,bool cropPreview)noexcept;
    bool operator==(const MediaAssemblyGeometry&)const=default;
};
// Source MediaAssemblyPictureTransform (:148-179): the rotation and flips the
// stage lays over the shown preview while a rotate / mirror edit renders.
struct MediaAssemblyPictureTransform {
    double angle{};                  // clockwise degrees, cumulative
    double scaleX{1},scaleY{1};
    bool swapsAxes()const noexcept;
    bool isIdentity()const noexcept;
    struct Delta {int turns{};bool reflected{};};
    // R(t1)·M·R(-t0) = R(t1 + t0)·M: quarter turns, plus a horizontal
    // reflection applied first when the mirror differs.
    static Delta delta(const MediaAssemblyGeometry&shown,const MediaAssemblyGeometry&target)noexcept;
    // The least turning (clockwise on a tie), then the fewest flipped axes.
    MediaAssemblyPictureTransform nearest(int turns,bool reflected)const noexcept;
    bool operator==(const MediaAssemblyPictureTransform&)const=default;
};
}
