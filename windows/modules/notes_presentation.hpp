#pragma once
#include "modules/notes_state.hpp"
#include <memory>

namespace endfield::modules {
class NotesChecklistLayout;
struct NotesMediaCardContent;
using NotesColor = std::array<double,4>; // straight sRGB; no premultiplication here
struct NotesPalette {
    NotesColor primary{},muted{},border{},card{},header{},formatPlate{},accent{};
    static NotesPalette source(bool dark,NotesColor accent);
    bool operator==(const NotesPalette&)const=default;
};
struct NotesStrings {
    std::string textTitle{"TEXT"},placeholder{"Double-click to write…"};
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
struct NotesPathPoint {bool move{};core::Point point;};
struct NotesShape {
    NotesPathKind kind{NotesPathKind::polyline};std::vector<NotesPathPoint> points;
    double radius{},lineWidth{1};bool roundCaps{},roundJoins{};
    std::optional<NotesColor> fill,stroke;
};
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
struct NotesHighlight {
    std::string actionID;std::size_t tintLayer{},rimLayer{};
    // Caller uses its current presented opacity as the animation origin. These
    // are source targets, not a second clock: easeOut, .06 pressed/.14 otherwise.
    double duration{};bool easeOut{true};
};
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
    // Pointer events update only retained tint/rim targets. Geometry/text stay
    // untouched. No implicit animation timers; caller evaluates source easeOut.
    bool setFeedback(std::optional<std::string_view> verb,bool pressed,bool reduceMotion);
    const std::string& noteID()const noexcept{return noteID_;}
    std::span<const NotesLayer> layers()const noexcept{return layers_;}
    std::span<const NotesAction> actions()const noexcept{return actions_;}
    std::span<const NotesHighlight> highlights()const noexcept{return highlights_;}
    const std::optional<NotesEditorLeaf>& editor()const noexcept{return editor_;}
    const NotesCardPlacement& placement()const noexcept{return placement_;}
    const std::shared_ptr<const NotesMeasuredText>& measuredText()const noexcept{return measured_;}
    std::uint64_t contentRevision()const noexcept{return contentRevision_;}
    std::uint64_t feedbackRevision()const noexcept{return feedbackRevision_;}
    std::uint64_t placementRevision()const noexcept{return placementRevision_;}
    double scrollOffset()const noexcept{return scrollOffset_;}
    core::Rect contentViewport()const noexcept{return viewport_;}
    const std::shared_ptr<const NotesMediaCardContent>& media()const noexcept{return media_;}
    const NotesPalette& palette()const noexcept{return palette_;}
    bool drawing()const noexcept{return drawing_;}
private:
    std::string noteID_;std::vector<NotesLayer> layers_;std::vector<NotesAction> actions_;
    std::vector<NotesHighlight> highlights_;std::optional<NotesEditorLeaf> editor_;
    std::shared_ptr<const NotesMeasuredText> measured_;std::shared_ptr<const NotesChecklistLayout> checklist_;
    std::shared_ptr<const NotesMediaCardContent> media_;NotesPalette palette_;
    std::optional<std::string> editingItem_;
    NotesCardPlacement placement_;core::Rect viewport_;
    double width_{},height_{},scrollOffset_{};bool selected_{},pinned_{},editing_{};
    bool initialized_{},pressed_{},reduceMotion_{},drawing_{};std::optional<std::size_t> feedback_;
    std::uint64_t contentRevision_{},placementRevision_{},feedbackRevision_{};
};
} // namespace endfield::modules
