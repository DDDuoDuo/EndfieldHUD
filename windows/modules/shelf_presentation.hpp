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
struct ShelfPresentationStats {std::uint64_t updates{},chromeBuilds{},cardBuilds{},placementUpdates{},propertiesBuilds{};};
// The properties card that replaces the Quick Look preview (Windows): the
// HUD's retained secondary menu (NotesRetainedMenu - offset shadow, face with
// an accent border, "// caption", action plates with control highlights and a
// "x" close plate) drawn on the Shelf canvas, so it follows the HUD tilt like
// every other secondary menu. The owner formats the rows in the HUD language.
struct ShelfPropertiesRow {
    std::string label,value;
    bool operator==(const ShelfPropertiesRow&)const=default;
};
struct ShelfPropertiesContent {
    std::string itemID,heading,close,open,reveal;
    std::vector<ShelfPropertiesRow> rows; // at most shelfPropertiesMaximumRows
    bool operator==(const ShelfPropertiesContent&)const=default;
};
inline constexpr std::size_t shelfPropertiesMaximumRows=9;
inline constexpr double shelfPropertiesWidth=300,shelfPropertiesValueX=90,shelfPropertiesRowTop=44,shelfPropertiesRowPitch=19;
// Width available to a row value (the owner shortens long paths to fit).
inline constexpr double shelfPropertiesValueWidth=shelfPropertiesWidth-shelfPropertiesValueX-10;
inline constexpr std::string_view shelfPropertiesClose="shelf.properties:close",shelfPropertiesOpen="shelf.properties:open",shelfPropertiesReveal="shelf.properties:reveal";
// The card's geometry for a row count (hit tests and UI Automation need no
// artwork): the face, the artwork origin and the close/open/reveal plates,
// all on the Shelf canvas.
struct ShelfPropertiesLayout {core::Rect frame;core::Point origin;std::array<core::Rect,3> plates;};
ShelfPropertiesLayout shelfPropertiesLayout(std::size_t rows);
inline constexpr std::array<std::string_view,3> shelfPropertiesActions{shelfPropertiesClose,shelfPropertiesOpen,shelfPropertiesReveal};
// The source-equivalent text descriptor of a row value (measurement).
ehud::data::Json shelfPropertiesValueText(std::string value,double contentsScale=2);
struct ShelfPropertiesCard {
    ehud::data::Json artwork;  // local (0,0,width+3,height+4): shadow and face; no fade or tilt baked in
    core::Rect frame;          // the face, centred on the 400 x 334 Shelf canvas
    core::Point origin;        // where the artwork's (0,0) sits on the canvas
    std::vector<FileShelfState::Action> actions; // canvas rects: close, open, reveal
    std::vector<ShelfPresentationFeedback> feedback;
};
ShelfPropertiesCard prepareShelfProperties(const ShelfPropertiesContent&,const ShelfPresentationStyle&);
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
    // The properties card (nullopt: none). An interactive card is modal: its
    // close/open/reveal plates are the only actions(). interactive=false keeps
    // a closing card's artwork for its fade-out without any action. Content
    // event only; an equal card returns false without rebuilding.
    bool setProperties(std::optional<ShelfPropertiesContent>,bool interactive=true);
    const std::optional<ShelfPropertiesCard>& properties()const noexcept;
    bool propertiesInteractive()const noexcept;
    std::uint64_t propertiesRevision()const noexcept;
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
