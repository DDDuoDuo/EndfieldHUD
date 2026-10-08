#pragma once
#include "modules/file_shelf_state.hpp"
#include "core/data/json.hpp"
#include <memory>

namespace endfield::modules {
using ShelfColor=std::array<double,4>; // straight sRGB
struct ShelfPresentationStyle {
    bool dark{true},depotIconAvailable{true};double contentsScale{2};
    ShelfColor accent{.98,.83,.12,1};
    // Exact owner colors, including macOS dynamic systemRed and NSColor blend,
    // are dependencies. A missing color is rejected when that state needs it.
    std::optional<ShelfColor> errorColor,lightDropColor;
    std::string heading{"Temporary File Shelf"},emptyTitle{"Drop files or folders here"};
    std::string emptyHelp{"Keep references. Drag them out whenever you need."};
    std::string clearQuestion{"Clear references only?"},unavailable{"Unavailable"};
    bool operator==(const ShelfPresentationStyle&)const=default;
};
struct ShelfPresentationImage {
    enum class Kind {sourceDepot,nativeFileIcon};Kind kind{};
    std::string layerID,itemID,sourceResource,lastKnownPath;
    core::Rect rect;std::optional<ShelfColor> tint;
    unsigned requestedPixels{};bool isDirectory{},unavailable{},sourceInTint{},resizeAspect{true};
};
struct ShelfPresentationFeedback {
    std::string actionID,tintLayerID,rimLayerID;
    double tintOpacity{},rimOpacity{},duration{};bool framed{};
};
struct ShelfPresentedCard {
    std::string itemID;ehud::data::Json artwork; // local (0,0,184,74), no selected pose baked in
    core::Rect full,clipped;double selectionY{},selectionZ{};
    std::uint64_t contentRevision{};
    std::vector<ShelfPresentationImage> images;
    std::vector<ShelfPresentationFeedback> feedback;
};
struct ShelfPresentationStats {std::uint64_t updates{},chromeBuilds{},cardBuilds{},placementUpdates{};};
// Source FileShelfCanvas artwork only. FileShelfState remains the sole semantic
// state/selection/action owner. update is a content/scroll event, not a frame
// callback; unchanged state/style returns before allocation or formatting.
// At most eight visible source cards are retained, independent of shelf size.
// The owner's projected collection applies contentClip to the local cards and
// empty-state children. Selection transforms and collection/toolbar/drop
// animation tracks remain numeric caller-owned presentation (no hidden clock).
// Paint order is collection/its cards, numeric scrollbar, drop outline,
// heading, status, toolbar. Insert card draws at chrome ID "shelf.collection";
// appending them after all chrome would incorrectly cover the drop outline.
// Original .26s subsection perspective/four-strip reveal and native icon/drag
// integration are not implemented by this descriptor. The original tracks
// are exported by shelf_presentation_reference for a later exact native bridge.
// Native system file icons, original tinted Depot artwork and font substitution
// are explicit dependencies; no filesystem, OS icon query, service or renderer.
class ShelfPresentation final {
public:
    ShelfPresentation();~ShelfPresentation();
    ShelfPresentation(const ShelfPresentation&)=delete;
    ShelfPresentation&operator=(const ShelfPresentation&)=delete;
    bool update(const FileShelfState&,const ShelfPresentationStyle&);
    // Numeric targets only; caller samples the existing source .06/.14 ease-out
    // highlight tracks. A nested action wins over its containing card.
    bool setFeedback(std::optional<std::string_view>,bool pressed,bool reduceMotion);
    const ehud::data::Json& chrome()const noexcept;
    std::span<const ShelfPresentedCard> cards()const noexcept;
    std::span<const ShelfPresentationImage> chromeImages()const noexcept;
    std::span<const ShelfPresentationFeedback> chromeFeedback()const noexcept;
    std::span<const FileShelfState::Action> actions()const noexcept;
    std::optional<core::Rect> scrollIndicator()const noexcept;
    ShelfColor scrollIndicatorColor()const noexcept; // source muted with alpha .6; cornerRadius 1
    static constexpr core::Rect contentClip(){return FileShelfState::contentRect();}
    std::uint64_t chromeRevision()const noexcept;
    std::uint64_t placementRevision()const noexcept;
    ShelfPresentationStats stats()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::modules
