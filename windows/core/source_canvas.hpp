#pragma once
#include "core/source_watch_layout.hpp"

namespace endfield::core::source {
struct SourceCanvasState {
    std::optional<std::size_t> nearestCanvas;
    std::int64_t sortingOrder{};
    std::optional<std::int64_t> localSortingOrder;
    bool overrideSorting{};
    bool startsSortingBoundary() const noexcept {return localSortingOrder.has_value()&&overrideSorting;}
};
struct SourceGraphicAncestry {
    std::optional<std::size_t> button,softMask;
    bool acceptsInput{true};
    std::vector<std::size_t> rectMasks;
};
struct SourceClipUniforms {
    std::array<float,4> rectangle{},parameters{},hgSoftness{};
    bool hasSoftness{};
};
// Exact mounted Canvas/UISortingOrder, CanvasGroup and RectMask2D semantics.
// All vectors use the SceneDefinition node order. Immutable relationships are
// compiled once; alpha updates retain their buffers and own no clock/service.
class SourceCanvasPlan final {
public:
    SourceCanvasPlan(const SceneDefinition&,const MountedLayoutDocument&,std::int64_t panelBase,
        const std::set<std::string,std::less<>>* registeredSortingComponents=nullptr);
    std::span<const SourceCanvasState> sorting()const noexcept{return sorting_;}
    std::span<const SourceGraphicAncestry> ancestry()const noexcept{return ancestry_;}
    bool updateAlpha(const Pose&);
    std::span<const double> inheritedAlpha()const noexcept{return alpha_;}
    std::uint64_t alphaRebuilds()const noexcept{return alphaRebuilds_;}
    // Canvas-local bounds keep the original mask-padding and nearest-child
    // softness rules, including exclusions for masks on the graphic itself.
    std::optional<SourceClipUniforms> clip(std::size_t node,std::span<const ResolvedNode>,
        const Matrix4& inverseCanvas)const;
private:
    struct Group {std::size_t node;double fallback;};
    struct Mask {std::array<double,4> padding{};std::array<float,4> parameters{},hgSoftness{};bool present{};};
    const SceneDefinition* scene_;
    SourceLayout layout_;
    std::vector<std::optional<std::size_t>> parents_;
    std::vector<SourceCanvasState> sorting_;
    std::vector<SourceGraphicAncestry> ancestry_;
    std::vector<Group> groups_;
    std::vector<Mask> masks_;
    std::vector<double> inputs_,nextInputs_,alpha_,nextAlpha_;
    bool initialized_{};
    std::uint64_t alphaRebuilds_{};
};
} // namespace endfield::core::source
