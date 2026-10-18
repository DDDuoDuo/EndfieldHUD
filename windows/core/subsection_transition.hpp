#pragma once
#include "core/motion.hpp"
#include "core/scene.hpp"

namespace endfield::core {
// Sources/HUDSubsectionTransition.swift, reveal only. The two-page
// HUDSubsectionHandoff lifecycle is separate and is not implemented here.
using SubsectionMaskPath=std::array<std::array<MotionPoint,6>,4>;
struct SubsectionTransitionStyle {
    static constexpr double duration=.26;
    static constexpr std::array<double,4> maskKeyTimes{0,.3,.68,1};
    static constexpr std::array<double,4> maskProgress{0,.38,.72,1};
    static constexpr CubicTiming transformTiming{.16,.78,.25,1};
    static constexpr CubicTiming maskSegmentTiming{0,0,.58,1}; // CA easeOut
};
struct SubsectionTransformSample {
    Matrix4 sublayerTransform;
    bool active{};
};
// Exact source-specific Float timing boundary verified against real paused CA
// presentation layers. Finite normalized times clamp; no clock or allocation.
double subsectionCoreAnimationProgress(double normalizedTime);
// Source P*T components interpolate before recomposition, so m44 is quadratic
// in remaining progress. direction<0 means left; zero/positive means right.
// Nonanimated/finished samples are the identity model state.
SubsectionTransformSample sampleSubsectionTransform(double direction,double elapsed,bool animated=true);

// Exact authored source path, NOT a claim about intermediate CA path morphs.
// Viewports must have finite positive extents and finite maximum coordinates.
// Actual CA normalizes the degenerate first/final path into curved controls;
// a four-hexagon point lerp does not reproduce those intermediate masks.
// Keep native reveal gated until a verified curved-mask sampler is available.
SubsectionMaskPath subsectionMaskKeyframe(Rect viewport,double direction,std::size_t keyframeIndex);
} // namespace endfield::core
