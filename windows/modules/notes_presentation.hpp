#pragma once
#include "modules/notes_state.hpp"
#include <memory>

namespace endfield::modules {
class NotesChecklistLayout;
struct NotesMediaCardContent;
using NotesColor = std::array<double,4>; // straight sRGB; no premultiplication here
// The redesign's card colours (NotesWorkspace.swift:316-322, DOCS-204):
// primary textOnDark #EEEEEE / nearBlack #111111, muted steel #8A8A8A /
// steel2 #6C6D68, border ringField #3A3A3A / plateLow #B3B4AF, the card face
// #1C1C1C / plateHigh #E4E5E0, the title strip charcoal #262626 / plate
// #D3D4CF, accent HUDColor.yellow (the design palette's yellow of the theme
// colour: the caller passes it, never the raw theme colour); rail: the media
// seek rail, sliderTrack #252525 / plateLow #B3B4AF.
struct NotesPalette {
    NotesColor primary{},muted{},border{},card{},header{},accent{},rail{};
    static NotesPalette source(bool dark,NotesColor yellow);
    bool operator==(const NotesPalette&)const=default;
};
struct NotesStrings {
    std::string textTitle{"TEXT"},placeholder{"Start writing"};
    std::string pin{"Pin note"},unpin{"Unpin note"},remove{"Delete note"},edit{"Edit text"};
    // Caller supplies the full AX selection label (source: title + grapheme prefix64).
    std::string select{"Text"},grow{"Enlarge note"},shrink{"Reduce note size"};
    std::array<std::string,4> format{"Font size","Font","Color","Text style"};
    std::string todoTitle{"TODO"},itemPlaceholder{"New item…"},addItem{"+ Add item"};
    std::string checkItem{"Check"},uncheckItem{"Uncheck"},editItem{"Edit item"},moveUp{"Move up"},moveDown{"Move down"},removeItem{"Delete item"},addItemAction{"Add item"};
    std::string imageTitle{"IMAGE/VIDEO"};
    std::string drawingTitle{"DRAWING"},drawingColor{"Drawing color"};
    bool operator==(const NotesStrings&)const=default;
};
// Caller-owned measurement made by the SAME text layout that will paint/edit.
// UTF-8 ranges cover text in order, including newlines; visibleTextEnd excludes
// trailing newlines exactly as NotesWrappedText.attributedLine. No text engine,
// font lookup, line breaking or UTF-16 conversion occurs in this module.
struct NotesMeasuredLine {std::size_t begin{},end{},visibleTextEnd{};double y{},height{};std::uint32_t utf16Begin{},utf16VisibleEnd{};};
struct NotesMeasuredText {
    std::string text; double width{},fontSize{12},height{};
    std::vector<NotesMeasuredLine> lines;
    std::optional<core::notes::RichText> richText;
    std::optional<std::string> sourceRichPayload; // exact cache identity, not re-encoded on scroll
};
enum class NotesLayerKind {layer,shape,text};
enum class NotesPathKind {polyline,roundedRect};
// `close` ends a subpath (CGPath closeSubpath): the chamfered plates and the
// grip's squares.
struct NotesPathPoint {bool move{};core::Point point;bool close{};};
struct NotesShape {
    NotesPathKind kind{NotesPathKind::polyline};std::vector<NotesPathPoint> points;
    double radius{},lineWidth{1};bool roundCaps{},roundJoins{};
    std::optional<NotesColor> fill,stroke;
};
// HUDShape.chamfer (HUDDesignDrawing.swift:16-30): the top-right corner cut
// at 45 degrees by min(cut, w / 2, h / 2), from the top-left corner.
std::vector<NotesPathPoint> notesChamfer(core::Rect,double cut);
struct NotesText {
    std::string text;double fontSize{};bool semibold{},truncateEnd{};
    NotesColor color{}; // system font role; actual native font parity remains external
    std::vector<core::notes::TextRun> runs; // local UTF-16 ranges, only visible line runs
    bool medium{},strikethrough{},wrapped{};
};
struct NotesLayer {
    static constexpr std::size_t noParent=std::size_t(-1);
    std::string id,name;
    std::size_t parent{noParent}; // parent-first, sibling insertion order is paint order
    NotesLayerKind kind{NotesLayerKind::layer};
    core::Rect frame,bounds;
    double opacity{1},cornerRadius{},borderWidth{};
    bool hidden{},masksToBounds{},allowsGroupOpacity{true};
    std::optional<NotesColor> background,border;
    NotesShape shape;NotesText text;
};
struct NotesAction {
    std::string id,verb,label;core::Rect localRect;
    bool accessibilityOnly{}; // select/grow/shrink are source AX actions, not extra buttons
};
// The card's redesign controls (NotesWorkspace.render, "the card's buttons",
// and makeChecklistControls, :1228-1269, :1438-1463; DOCS-205..210, DOCS-216):
// HUDIconButtons (pin .pin 10 on = pinned, delete .x 9, playback .play /
// .pause 9, the TODO rows' move up / down chevrons 8 turned +-90 degrees and
// their .x 7) and NotesFormatToggles (the editing tools A↕, Aa, the colour
// chip and B; the drawing colour chip; "+ Add item"; the rows' check). The
// owner draws them over the card artwork on the design library
// (modules/notes_card_controls.hpp) with the library's hover and press
// motion; a card rebuilt for a content change shows them settled (the
// Mac's BuildGate). Their frames are card-local; inViewport controls lie in
// the content viewport (clipped by it, the TODO rows). Each one's action has
// the same rect, so presses, hover and accessibility agree.
enum class NotesControlKind {iconButton,formatToggle};
enum class NotesControlGlyph {pin,x,up,down,play,pause,label,bold,check,chip};
struct NotesControl {
    std::string id,label;NotesControlKind kind{NotesControlKind::iconButton};NotesControlGlyph glyph{NotesControlGlyph::x};
    std::string text;core::Rect frame;double iconSize{};
    bool on{},enabled{true},hidden{},inViewport{};std::optional<NotesColor> chip;
    bool operator==(const NotesControl&)const=default;
};
// The title strip's caption (HUDTextLayer 9.5 bold muted, tracking .12,
// uppercase, line box (20, 7.5, max(10, w - 76), 9.5)), drawn with the controls.
struct NotesTitle {std::string text;core::Rect line;bool operator==(const NotesTitle&)const=default;};
// The note scroll thumb (layoutScrollableContent :1367-1375, DOCS-212): 3 px
// at the viewport's right edge, theme yellow, square, min height 10; shown by
// the shared HUDScrollBarVisibility while the note scrolls or is hovered.
struct NotesThumb {core::Rect frame;bool scrollable{};bool operator==(const NotesThumb&)const=default;};
struct NotesEditorLeaf {
    // A standalone text surface for native editing, NOT a grouped card raster.
    // Same workspace projection/card ancestor clip as the painted text. Native
    // adapter must supply full text and its retained measured editor layout.
    core::Rect localRect;double scrollOffset{},fontSize{12};bool multiline{true};
};
struct NotesCardPlacement {
    core::Rect workspaceBounds,workspaceRect,localRect;
    core::Projection projection;double layerOrder{};bool visible{};
};
struct NotesPresentationInput {
    NotesPalette palette;NotesStrings strings;
    std::shared_ptr<const NotesMeasuredText> measured;
    double scrollOffset{};std::optional<NotesColor> editingColor;
    std::shared_ptr<const NotesChecklistLayout> checklist;
    std::shared_ptr<const NotesMediaCardContent> media;
    std::optional<NotesColor> drawingColor; // caller-owned brush setting, separate from text style
};
// Exact text, TODO and media NotesCanvas local artwork. No services, clock, renderer, I/O
// or center-module clip. The owner composes each card on the workspace plane.
// Media pixels/drawing and deletion-popup artwork are explicit separate
// adapters, never flattened. Source visibility/deployment animations are owned
// by the module host; this class does not invent transform interpolation.
class NotesCardPresentation final {
public:
    explicit NotesCardPresentation(std::string noteID);
    // Content/settings/measurement events only. Validation + construction is
    // transactional. Measurement must match current text (placeholder if empty)
    // and displayed viewport width. Empty/tiny source rects are preserved.
    bool updateContent(const NotesState&,const NotesPresentationInput&);
    // Frame/move/closing path: no allocation, text scan, JSON, raster or I/O.
    // Fails if size/selection/pin/edit state changed: call updateContent first.
    bool updatePlacement(const NotesState&);
    const std::string& noteID()const noexcept{return noteID_;}
    std::span<const NotesLayer> layers()const noexcept{return layers_;}
    std::span<const NotesAction> actions()const noexcept{return actions_;}
    std::span<const NotesControl> controls()const noexcept{return controls_;}
    const NotesTitle& title()const noexcept{return title_;}
    const NotesThumb& thumb()const noexcept{return thumb_;}
    const std::optional<NotesEditorLeaf>& editor()const noexcept{return editor_;}
    const NotesCardPlacement& placement()const noexcept{return placement_;}
    const std::shared_ptr<const NotesMeasuredText>& measuredText()const noexcept{return measured_;}
    std::uint64_t contentRevision()const noexcept{return contentRevision_;}
    std::uint64_t placementRevision()const noexcept{return placementRevision_;}
    double scrollOffset()const noexcept{return scrollOffset_;}
    core::Rect contentViewport()const noexcept{return viewport_;}
    const std::shared_ptr<const NotesMediaCardContent>& media()const noexcept{return media_;}
    const NotesPalette& palette()const noexcept{return palette_;}
    bool drawing()const noexcept{return drawing_;}
private:
    std::string noteID_;std::vector<NotesLayer> layers_;std::vector<NotesAction> actions_;
    std::vector<NotesControl> controls_;NotesTitle title_;NotesThumb thumb_;std::optional<NotesEditorLeaf> editor_;
    std::shared_ptr<const NotesMeasuredText> measured_;std::shared_ptr<const NotesChecklistLayout> checklist_;
    std::shared_ptr<const NotesMediaCardContent> media_;NotesPalette palette_;
    std::optional<std::string> editingItem_;
    NotesCardPlacement placement_;core::Rect viewport_;
    double width_{},height_{},scrollOffset_{};bool selected_{},pinned_{},editing_{};
    bool initialized_{},drawing_{};
    std::uint64_t contentRevision_{},placementRevision_{};
};
} // namespace endfield::modules
