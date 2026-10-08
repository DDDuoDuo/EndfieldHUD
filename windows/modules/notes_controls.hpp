#pragma once
#include "modules/notes_presentation.hpp"

namespace endfield::modules {
enum class NotesControlsKind {center,mediaSource,font,size,special,color,shelfMedia};
struct NotesControlsStrings {
    std::string heading{"NOTES"},saveErrorPrefix{"Could not save: "},storageUnavailable{"Notes storage unavailable"};
    std::array<std::string,4> tools{"Text","TODO","Image/Video","Drawing"};
    // Original Mac labels are intentionally caller-localized. A Windows owner
    // chooses its platform's native file-picker label; no picker is opened here.
    std::string chooseFile{"Choose in Finder"},chooseShelf{"Choose from Shelf"},close{"Close"};
    std::array<std::string,4> traits{"Bold","Italic","Underline","Strikethrough"};
    std::string currentColor{"Current color"},mediaOnly{"Media only"},useMedia{"Use selected media"},shelfHeading{"SHELF · IMAGE/VIDEO"};
    bool operator==(const NotesControlsStrings&)const=default;
};
struct NotesShelfChoice {
    std::string id,title,detail;bool supported{},available{};
    bool operator==(const NotesShelfChoice&)const=default;
};
struct NotesControlsInput {
    NotesControlsKind kind{NotesControlsKind::center};bool dark{true},notesSelected{true},storageAvailable{true};
    std::optional<std::string> error;NotesControlsStrings strings;
    NotesColor accent{.98,.83,.12,1},currentColor{1,1,1,1};
    std::optional<NotesColor> systemOrange; // actual owner's system color required for an error
    double contentsScale{2};
    // Actual icon availability is explicit. A missing original icon selects
    // only the SAME source fallback glyph; no replacement artwork is invented.
    bool manualIconAvailable{true},missionIconAvailable{true};
    // Font family enumeration/selected name belong to the native owner. Size
    // lists use sizeValues() once; sample/build never enumerates installed fonts.
    std::vector<std::string> values;std::string selectedValue;std::size_t firstRow{};
    std::array<bool,4> traits{};
    core::Point colorWheelSelection{.5,.5}; // source wheel's normalized selected point
    std::vector<NotesShelfChoice> choices;bool mediaOnly{true};std::optional<std::string> selectedID;
    bool operator==(const NotesControlsInput&)const=default;
};
struct NotesControlsAction {
    std::string id,label;core::Rect rect;bool enabled{true},selected{};
};
struct NotesControlsImage {
    std::string layerID,sourceResource;core::Rect rect;
    NotesColor tint{1,1,1,1};unsigned requestedPixels{};bool sourceInTint{},resizeAspect{};
    // Asset references are dependencies, NOT loaded pixels. The owner resolves
    // sourceResource from its pinned package. Never publish incomplete artwork.
};
struct NotesControlsFeedback {
    std::string actionID,tintLayerID,rimLayerID;
    // Duration applies only to a changed target. Native adapters preserve an
    // already-running animation when these targets are unchanged.
    double tintOpacity{},rimOpacity{},duration{};bool easeOut{true};
};
// Source NotesCanvas / NotesRetainedMenu presentation only. Explicit content
// events produce a local CALayer-shaped descriptor and actions; unchanged input
// retains all storage. Pointer feedback returns numeric targets separately and
// does not edit/recreate artwork. The native adapter MUST split animated tint,
// rim and toolbar plates into retained surfaces before loading LayerScene; a
// single grouped raster cannot animate those independently.
// Source wheel pointer/keyboard/AX slider behavior, font metrics, image decoding
// and native file/media selection remain owner dependencies, not fake services.
// No notes mutations, providers, files, font lookup, UUIDs, timers or editor.
// Center has exactly four add controls. Current source has no Clear/Arrange UI.
class NotesControls final {
public:
    bool update(const NotesControlsInput&);
    bool setFeedback(std::optional<std::string_view> actionID,bool pressed,bool reduceMotion);
    const ehud::data::Json& artwork()const noexcept{return artwork_;}
    std::span<const NotesControlsAction> actions()const noexcept{return actions_;}
    std::span<const NotesControlsImage> images()const noexcept{return images_;}
    std::span<const NotesControlsFeedback> feedback()const noexcept{return feedback_;}
    core::Rect bounds()const noexcept{return bounds_;}
    std::optional<std::string_view> actionAt(core::Point)const noexcept;
    std::uint64_t contentRevision()const noexcept{return revision_;}
    std::uint64_t feedbackRevision()const noexcept{return feedbackRevision_;}
    static std::vector<std::string> sizeValues(double currentFontSize);
    static std::size_t initialFirstRow(std::span<const std::string>,std::string_view selected);
    // Menu offsets are module-local origins converted to the workspace by the
    // owner's actual module homography, never clipped by the center host.
    static core::Rect mediaSourceModuleRect()noexcept{return {96,212,208,75};}
    static core::Rect shelfModuleRect()noexcept{return {30,28,340,260};}
private:
    std::optional<NotesControlsInput> input_;ehud::data::Json artwork_;
    std::vector<NotesControlsAction> actions_;std::vector<NotesControlsImage> images_;
    std::vector<NotesControlsFeedback> feedback_;core::Rect bounds_;
    std::optional<std::size_t> hovered_;bool pressed_{},reduceMotion_{};
    std::uint64_t revision_{},feedbackRevision_{};
};
} // namespace endfield::modules
