#pragma once
#include "modules/archive_state.hpp"
#include <map>

namespace endfield::modules {
using ArchiveColor=std::array<double,4>;
struct ArchiveAppearance {
    bool dark{true};double contentsScale{2};ArchiveColor accent{250./255,212./255,31./255,1};
    // NSColor.systemOrange depends on the source owner appearance. A source-
    // prepared or explicitly supplied value is required only for an error.
    std::optional<ArchiveColor>systemOrange;
    bool operator==(const ArchiveAppearance&)const=default;
};
struct ArchiveStrings {
    std::string heading{"Archive"},all{"All"},uncategorized{"Uncategorized"},untitled{"Untitled"},
        documents{"Documents"},createDocument{"Create a document"},title{"Title"},content{"Content"},
        category{"Category"},newCategory{"New category"},deleteDocument{"Delete this document?"},
        deleteCategory{"Remove this category?"},categoryRemovalDetail{"Documents will move to Uncategorized."},
        cancel{"Cancel"},remove{"Delete"};
    bool operator==(const ArchiveStrings&)const=default;
};
struct ArchiveViewInput {
    ArchiveAppearance appearance;ArchiveStrings strings;
    // Source date format is Gregorian en_US_POSIX yyyy-MM-dd in the owner's
    // local timezone. Caption width uses actual system 10pt medium metrics.
    // Both are injected: no timezone/font/character-width approximation here.
    std::function<std::string(double foundationDate)>dateString;
    std::function<double(std::string_view)>categoryCaptionWidth;
    ArchiveJson iconContents; // exact original Archive_Icon binding; required for visible fallback
    std::map<std::string,ArchiveJson,std::less<>>posterContents;
    std::optional<std::string>formattingField,statusMessage,mediaError;
    ArchiveColor formattingColor{1,1,1,1};
    std::size_t mediaIndex{};bool mediaPlaying{};double mediaTime{};
};
struct ArchiveAction {
    std::string id,title;core::Rect rect;bool enabled{true},selected{};
    bool operator==(const ArchiveAction&)const=default;
};
struct ArchiveFeedback {
    std::string action,tintID,rimID;core::Rect rect;
    // Full control region plus ancestor clipping, independent of action's
    // >=2pt clipped accessibility rect. Hover tests the original path.
    std::optional<core::Rect>clip;bool cutCorner{true},framed{},enabled{true};
};
struct ArchiveArtwork {
    ArchiveJson root;std::vector<ArchiveAction>actions;std::vector<ArchiveFeedback>feedback;
};
// Pure source-shaped local CALayer tree. Native adapters split feedback/rows
// into cached surfaces, then use the EXISTING GPU group for face fade. This
// factory owns no renderer, clock, editor, provider, image bytes or database.
ArchiveArtwork prepareArchiveArtwork(const ArchiveState&,const ArchiveViewInput&);
enum class ArchiveMenuKind {category,deleteDocument,deleteCategory};
struct ArchiveMenuInput {
    ArchiveMenuKind kind{ArchiveMenuKind::category};ArchiveAppearance appearance;ArchiveStrings strings;
    std::span<const ArchiveCategory>categories;double scrollOffset{};bool editingName{};
};
ArchiveArtwork prepareArchiveMenu(const ArchiveMenuInput&);
std::optional<std::string_view>archiveActionAt(std::span<const ArchiveAction>,core::Point)noexcept;
bool archiveFeedbackContains(const ArchiveFeedback&,core::Point)noexcept;
// Source anchors: creation below the gallery '+', move below the top-right
// category action, media chooser DIRECTLY below its '+'; no centered fallback.
core::Point archiveCategoryMenuOrigin(bool moving)noexcept;
core::Point archiveMediaMenuOrigin(core::Rect menuBounds)noexcept;
} // namespace endfield::modules
