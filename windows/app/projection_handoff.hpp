#pragma once
#include <cstdint>
#include <optional>

namespace endfield::app {
// Serialized handoff from OverlayController.swift: the existing HUD exit must
// complete before Projection is shown, and Projection must disappear before the
// existing HUD entrance starts. The owner retains all platform handles/actions;
// this value state owns no windows, timers, resources, or completion closures.
class ProjectionHandoff final {
public:
    enum class Phase { inactive, closingHUD, projection, closingProjection };
    enum class Action { closeHUD, showProjection, closeProjection, showHUD, closeImmediately, external };
    struct Context {
        std::uintptr_t display{}, previousApplication{};
        friend bool operator==(const Context&,const Context&)=default;
    };
    struct Command {
        Action action;
        std::uint64_t generation;
        Context context;
        std::optional<std::uint64_t> externalID;
    };
    struct OpeningConditions { bool hudOpen{}, quitting{}, closeAlreadyPending{}, shelfDragging{}; };
    std::optional<Command> open(OpeningConditions,Context);
    std::optional<Command> hudClosed(std::uint64_t generation,bool displayAvailable=true,
        std::optional<std::uintptr_t> resolvedDisplay={});
    bool reposition(std::uintptr_t resolvedDisplay)noexcept;
    std::optional<Command> returnToHUD();
    std::optional<Command> projectionClosed(std::uint64_t generation);
    // A latest external action replaces an older deferred request, as in the
    // source. The caller routes non-Projection presentations itself.
    std::optional<Command> external(std::uint64_t actionID);
    // Cancellation invalidates queued completions; it never reopens the HUD or
    // invokes a deferred action. Session drawings live in ProjectionModel.
    std::optional<Command> cancel();
    Phase phase()const noexcept{return phase_;}
    bool active()const noexcept{return phase_!=Phase::inactive;}
    bool acceptsInput()const noexcept{return phase_==Phase::projection;}
    std::uint64_t generation()const noexcept{return generation_;}
private:
    Command command(Action)const noexcept;
    std::optional<Command> finish(Action);
    Phase phase_{Phase::inactive};
    std::uint64_t generation_{};
    Context context_{};
    std::optional<std::uint64_t> external_;
};
}
