#pragma once
#include "core/scene.hpp"
#include "core/localization.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
// Only retained preview geometry: no decoder, renderer, store, clock or allocation
// in pan/zoom/crop transforms. Image editing and output pixels are separate.
class MediaAssemblyViewport final {
public:
    double zoom()const noexcept{return zoom_;}core::Point pan()const noexcept{return pan_;}
    void reset()noexcept;core::Rect imageRect(core::Point imageSize,core::Rect viewport)const noexcept;
    void magnify(double factor,core::Point at,core::Point imageSize,core::Rect viewport)noexcept;
    void move(core::Point delta,core::Point imageSize,core::Rect viewport)noexcept;
    static core::Rect stickerRect(const MediaAssemblySticker&,core::Rect image);
    static core::Point unrotate(core::Point,core::Point center,double degrees)noexcept;
    static core::Point displayPoint(core::Point,int quarterTurns,bool mirrored)noexcept;
    static core::Point sourcePoint(core::Point,int quarterTurns,bool mirrored)noexcept;
    static core::Rect displayCrop(MediaAssemblyCrop,int quarterTurns,bool mirrored)noexcept;
    static MediaAssemblyCrop sourceCrop(core::Rect,int quarterTurns,bool mirrored)noexcept;
private:
    double zoom_{1};core::Point pan_;
    void clamp(core::Point imageSize,core::Rect viewport)noexcept;
};
}
