#pragma once
#include "core/motion.hpp"

namespace endfield::core {
// Source CABasicAnimation endpoints and its sampled timing function. The native
// adapter must interpolate CATransform3D equivalently; these are deliberately
// NOT linearly interpolated matrices or offset components. Construction order
// is the ModuleOffset contract: perspective, translation, X then Y rotation.
struct ModulePresentationTransform {
    ModuleOffset from{},to{};
    double easedProgress{1};
    bool animated{};
};
struct ModulePresentationRegistration {
    RegistrationPath path{};
    double opacity{},white{};
    double colorAlpha{ModuleTransitionStyle::registrationColorAlpha};
    double lineWidth{ModuleTransitionStyle::registrationLineWidth};
    // Source seam uses an open four-point path per strip, butt caps and miter
    // joins. It is the last child inside the incoming wrapper and its mask.
};
struct ModulePresentationSurface {
    std::uint64_t identity{};
    Module module{Module::power};
    MotionRect contentFrame{}; // Within the original 440x440 wrapper.
    ModulePresentationTransform transform;
    std::optional<ShutterPath> shutter;
    std::optional<ModulePresentationRegistration> registration;
    double opacity{1}; // Source content remains opaque throughout the swap.
};
struct ModulePresentationSample {
    Module selected{Module::power},requested{Module::power};
    std::uint64_t generation{};
    ModulePresentationSurface current;
    std::optional<ModulePresentationSurface> incoming; // Draw after current.
    bool transitioning{};
    // The outer HUD owns visibility and input activation; this gate only
    // reflects the source module host's exclusion during a mechanical swap.
    bool acceptsModuleInput{true};
};
struct ModulePresentationChange {
    Module from{},to{};
    bool animated{};
    bool operator==(const ModulePresentationChange&) const = default;
};
struct ModulePresentationUpdate {
    ModulePresentationSample presentation;
    std::optional<ModulePresentationChange> change;
    // Opaque caller completion token, emitted once for only the newest select.
    // No callback is retained or invoked by the portable frame path.
    std::optional<std::uint64_t> completedRequest;
};

// Caller-clock orchestration of HUDModuleContent. No providers, factories,
// timers, rendering or owned module artwork. The caller retains content using
// surface identity; selection can replace wrappers, ordinary samples cannot.
// Nonfinite/backward caller time is rejected without changing state. A delayed
// completion starts a queued swap at the current clock, never catches it up to
// an earlier nominal deadline (matching the deferred source completion).
class ModulePresentation final {
public:
    explicit ModulePresentation(Module initial=Module::power);
    static constexpr MotionRect hostFrame=ModuleTransitionStyle::viewport;
    static constexpr MotionRect wrapperBounds{0,0,440,440};
    static constexpr MotionPoint wrapperAnchor{.5,.5},wrapperPosition{220,220};
    static constexpr bool masksToBounds=true,allowsGroupOpacity=false;
    Module selected() const noexcept {return state_.selected();}
    Module requested() const noexcept {return state_.requested();}
    std::optional<Module> pending() const noexcept {return state_.pending();}
    std::uint64_t generation() const noexcept {return state_.generation();}
    bool isTransitioning() const noexcept {return state_.isTransitioning();}
    bool requiresFrames() const noexcept {return isTransitioning();}
    bool isPresenting(Module module) const noexcept;
    // Theme repaint changes the neutral seam color without restarting tracks
    // or replacing either live wrapper. Content repaint is caller-owned.
    void setDark(bool dark) noexcept {dark_=dark;}
    ModulePresentationUpdate select(Module module,double time,bool animated=true,bool reduceMotion=false,
                                    std::optional<std::uint64_t> completion={});
    ModulePresentationUpdate sample(double time);
    // Optional native completion delivery. Stale or premature tokens do not
    // commit anything; sample() normally completes the finite track itself.
    ModulePresentationUpdate complete(std::uint64_t generation,double time);
    ModulePresentationUpdate settle(double time);
    ModulePresentationUpdate cancel(double time);
private:
    struct Screen {Module module;std::uint64_t identity;};
    ModuleSelection state_;
    Screen current_;
    std::optional<Screen> incoming_;
    std::optional<ModuleTransition> transition_;
    std::optional<std::uint64_t> completion_;
    std::optional<double> lastTime_;
    double started_{};
    std::uint64_t nextIdentity_{1};
    bool dark_{true};
    void advance(double time);
    void begin(const ModuleTransition&,double time);
    ModulePresentationUpdate snapshot(double time) const;
    ModulePresentationUpdate finish(double time);
    ModulePresentationUpdate settleOn(Module,double time);
    void attachCompletion(ModulePresentationUpdate&);
};
} // namespace endfield::core
