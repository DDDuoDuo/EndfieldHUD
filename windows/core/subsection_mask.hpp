#pragma once
#include "core/scene.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace endfield::core {
// Pinned, actual Core Animation mask samples for explicit original viewports.
// Each asset carries its own measured holdout report; this is not a general CA
// path interpolator, a rescaling of another module, or a pixel-parity claim.
struct SubsectionCurvePath {
    std::array<std::uint8_t,40> opcodes{};
    std::array<double,192> coordinates{};
    std::size_t opcodeCount{},coordinateCount{};
    bool operator==(const SubsectionCurvePath&)const=default;
    bool topologyGap{}; // nearest recorded endpoint within a <=1e-12 phase gap
};
class SubsectionMaskSampler final {
public:
    static constexpr std::string_view assetSHA256="3c1b0c8b7a2c399a09212a60d0c2fe56df92615f71bb4d209d1906aa88bb70d0";
    static constexpr Rect viewport{9,40,382,248};
    static constexpr double duration=.26;
    // Content-event parse only. Owner reads explicit bytes; no I/O or clocks.
    explicit SubsectionMaskSampler(std::span<const std::uint8_t>,std::string_view expectedSHA256);
    // Additional source-generated module fixture. Both digest and viewport are
    // explicit caller expectations; never rescales another module's CA paths.
    SubsectionMaskSampler(std::span<const std::uint8_t>,std::string_view expectedSHA256,Rect expectedViewport);
    Rect sourceViewport()const noexcept;
    ~SubsectionMaskSampler();
    SubsectionMaskSampler(const SubsectionMaskSampler&)=delete;
    SubsectionMaskSampler&operator=(const SubsectionMaskSampler&)=delete;
    // Finite phase clamps to [0,1], direction<0 selects left. No allocations.
    SubsectionCurvePath sample(double direction,double phase)const;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::core
