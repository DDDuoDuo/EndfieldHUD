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
// HUDDirectionalContent.playEntrance(from:) as a clock: the redesigned page
// plays its own header wipe and row stagger (DesignMotion) from `offset`
// (content px, ShellPosition) starting at `start`; the host only says when.
// Present from the swap (or beginEntrance) until the next swap or settle; the
// page decides when its own entrance has ended. Never under Reduce Motion.
struct ModuleEntrance {
    MotionPoint offset{};
    double start{},elapsed{};
};
struct ModulePresentationSurface {
    std::uint64_t identity{};
    Module module{Module::power};
    MotionRect contentFrame{}; // Within the original 440x440 wrapper (unused by design screens).
    ModulePresentationTransform transform;
    std::optional<ShutterPath> shutter;
    std::optional<ModulePresentationRegistration> registration;
    // The wrapper's opacity: 1 through a shutter swap (source content stays
    // opaque), the directional leave's fade, 0 while not attached.
    double opacity{1};
    // Sources/HUDModuleContent.swift isDesignScreen/place (LIB-70, LIB-100): a
    // redesigned module's 590 x 510 px box sits at DesignBox through
    // DesktopChromeLayout::boxWorld, without the v1.2.0 report scale.
    bool designScreen{};
    // The module host's 440 x 440 masksToBounds, shared by both screens: off
    // during a directional switch and while a design screen is current; a
    // shutter swap keeps the state the outgoing screen settled with (its
    // shutter masks clip both screens to the viewport anyway).
    bool hostClip{true};
    // HUDMotion.leave on the wrapper: additive translation in centre-plane
    // design units (-0.6 x the switch vector x the content unit at the end).
    MotionPoint planeOffset{};
    // An unported screen's v1.2.0 report placement follows the requested
    // module (the v1.2.0 host re-placed both screens at the click); while a
    // design screen is requested it keeps its own module's placement instead.
    bool keepsOwnPlacement{};
    // false: in the hierarchy only as content to prepare (the incoming screen
    // during the leave, built as the Mac builds it at begin) or a superseded
    // destination that is skipped; never drawn and never hit.
    bool attached{true};
    std::optional<ModuleEntrance> entrance;
};
struct ModulePresentationSample {
    Module selected{Module::power},requested{Module::power};
    std::uint64_t generation{};
    ModulePresentationSurface current;
    std::optional<ModulePresentationSurface> incoming; // Draw after current.
    bool transitioning{};
    bool directional{}; // The running swap is a directional leave, not the shutter.
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
// Two switch styles, chosen by the destination (HUDModuleContent.begin): a
// module marked as a design screen (setDesignScreen, the Mac's
// HUDDirectionalContent canvases) is entered directionally (beginDirectional:
// the current screen leaves for .13 s, 0 when it was never attached; the swap
// attaches the new screen, finishes and starts its entrance in the same
// sample, so it is never drawn before its rows hide; a module requested
// during the leave supersedes the pending one, which is skipped unseen).
// Every other module keeps the v1.2.0 shutter, so unported modules keep
// working during the port. Input waits for the swap only, not the stagger.
// Not here: prepareToLeave (the owner of ModulePresentationChange::from does
// it on the change), the module contrast fade (the shell's, over .13 s).
class ModulePresentation final {
public:
    explicit ModulePresentation(Module initial=Module::power);
    static constexpr MotionRect hostFrame=ModuleTransitionStyle::viewport;
    static constexpr MotionRect wrapperBounds{0,0,440,440};
    static constexpr MotionPoint wrapperAnchor{.5,.5},wrapperPosition{220,220};
    // The v1.2.0 host clip; ModulePresentationSurface::hostClip carries the
    // design-screen opt-out per sample.
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
    // A module whose owner is a design-library canvas (the Mac registers a
    // HUDDesignCanvas factory). Read when a switch begins and for placement;
    // owners register before the HUD first shows, as on the Mac.
    void setDesignScreen(Module,bool);
    bool isDesignScreen(Module module) const noexcept {return design_[static_cast<std::size_t>(module)];}
    // SystemHUDView's deploy: the first module of a session plays its entrance
    // from the disc centre (ShellPosition::entranceOffset(nullopt, module)).
    // Only a settled, attached design screen and never under Reduce Motion.
    // `offset` replaces the disc-centre offset (finishBootLog: from below,
    // ShellPosition::subpageOffset(0, 1)).
    ModulePresentationUpdate beginEntrance(double time,bool reduceMotion=false,std::optional<MotionPoint> offset={});
    // SystemHUDView.playsBootLogOnOpen (moduleContent.layer.opacity 0): every
    // sampled surface is transparent until revealed; nothing else changes.
    void setConcealed(bool concealed) noexcept {concealed_=concealed;}
    bool concealed() const noexcept {return concealed_;}
    ModulePresentationUpdate select(Module module,double time,bool animated=true,bool reduceMotion=false,
                                    std::optional<std::uint64_t> completion={});
    ModulePresentationUpdate sample(double time);
    // Optional native completion delivery. Stale or premature tokens do not
    // commit anything; sample() normally completes the finite track itself.
    ModulePresentationUpdate complete(std::uint64_t generation,double time);
    ModulePresentationUpdate settle(double time);
    ModulePresentationUpdate cancel(double time);
private:
    struct Screen {Module module;std::uint64_t identity;bool attached{true};};
    ModuleSelection state_;
    Screen current_;
    std::optional<Screen> incoming_;
    std::optional<ModuleTransition> transition_;
    std::optional<std::uint64_t> completion_;
    std::optional<double> lastTime_;
    double started_{},swapAt_{};
    bool directional_{};
    MotionPoint leaveOffset_{}; // Content px (ShellPosition), the entrance's offset too.
    std::optional<ModuleEntrance> entrance_; // Of current_.
    std::array<bool,24> design_{};
    std::uint64_t nextIdentity_{1};
    bool dark_{true},concealed_{};
    void advance(double time);
    void begin(const ModuleTransition&,double time);
    ModulePresentationUpdate snapshot(double time) const;
    ModulePresentationUpdate finish(double time);
    bool due(double time)const noexcept;
    ModulePresentationUpdate run(double time);
    ModulePresentationUpdate settleOn(Module,double time);
    void attachCompletion(ModulePresentationUpdate&);
};
} // namespace endfield::core
