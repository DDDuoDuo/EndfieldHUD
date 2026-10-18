#pragma once
#include "modules/notes_controls.hpp"
#include "core/localization.hpp"

namespace endfield::modules {
enum class ProjectionControlsKind {toolbar,brush,appearance,clearConfirmation};
struct ProjectionControlsInput {
    ProjectionControlsKind kind{ProjectionControlsKind::toolbar};
    core::Language language{core::Language::english};
    NotesColor accent{.98,.83,.12,1},color{.98,.83,.12,1};
    double brushWidth{5},darkness{.5},blur{.5};
    bool erasing{},backgroundEnabled{true};
    bool operator==(const ProjectionControlsInput&)const=default;
};
struct ProjectionControlSlider {
    std::string id,label;
    core::Rect rail,hitRect,labelRect,track,fill,thumb;
    double minimum{},maximum{},value{};
    // Menu-space x; source sliders clamp the drag, including outside the rail.
    std::optional<double> valueAt(double x)const noexcept;
};
struct ProjectionInkLabel {
    std::string layerID,text;core::Rect rect;NotesColor color;
    double fontSize{10},contentsScale{2};bool semibold{true};
};
// Retained, dark-only ProjectionControls.swift artwork. Common color, media
// source and shelf menus remain NotesControls, selected by the workspace owner.
// Update only on content/preferences/language events. Toolbar text nodes carry
// projectionInkCentered=true and appear in inkLabels(): the native adapter must
// rasterize actual glyph ink centered in the full item rect, NOT line-box align.
// Tint/rim surfaces have stable IDs and independent numeric feedback targets;
// pointer feedback never changes artwork or rerasterizes labels.
class ProjectionControls final {
public:
    bool update(const ProjectionControlsInput&);
    bool setFeedback(std::optional<std::string_view> actionID,bool pressed,bool reduceMotion);
    const ehud::data::Json& artwork()const noexcept{return artwork_;}
    std::span<const NotesControlsAction> actions()const noexcept{return actions_;}
    std::span<const NotesControlsFeedback> feedback()const noexcept{return feedback_;}
    std::span<const ProjectionControlSlider> sliders()const noexcept{return sliders_;}
    std::span<const ProjectionInkLabel> inkLabels()const noexcept{return inkLabels_;}
    core::Rect bounds()const noexcept{return bounds_;}
    std::optional<std::string_view> actionAt(core::Point)const noexcept;
    std::optional<std::size_t> sliderAt(core::Point)const noexcept;
    std::uint64_t contentRevision()const noexcept{return revision_;}
    std::uint64_t feedbackRevision()const noexcept{return feedbackRevision_;}
    static constexpr double menuTransitionDuration=.16,menuCloseFadeDuration=.14,menuSlideDistance=5;
private:
    std::optional<ProjectionControlsInput>input_;ehud::data::Json artwork_;
    std::vector<NotesControlsAction>actions_;std::vector<NotesControlsFeedback>feedback_;
    std::vector<ProjectionControlSlider>sliders_;std::vector<ProjectionInkLabel>inkLabels_;
    core::Rect bounds_;std::optional<std::size_t>hovered_;bool pressed_{},reduceMotion_{};
    std::uint64_t revision_{},feedbackRevision_{};
};
}
