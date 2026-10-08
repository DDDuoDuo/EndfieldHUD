#pragma once
#include "core/source_button_motion.hpp"
#include "core/source_camera.hpp"
#include "core/source_selectable_color.hpp"
#include "core/source_watch_frame.hpp"
#include <functional>

namespace endfield::app {
namespace source=core::source;
struct WatchSessionSettings {
    bool reduceMotion{},ambientEnabled{true},lowPower{};
    double parallax{1},perspective{1},hudScale{1};
    source::Vec2 hudOffset{};
    bool operator==(const WatchSessionSettings&)const=default;
};
struct WatchSessionEnvironment {
    source::Vec2 viewport{};
    bool presented{},onScreen{},canAdvanceTransition{},pointerLocked{};
    std::optional<core::Point> pointer;
    bool operator==(const WatchSessionEnvironment&)const=default;
};
// These opaque action numbers are supplied by the desktop navigation adapter.
// Scrolling changes bindings, never action meaning or a module's own state.
struct WatchSessionNavigation {
    std::map<std::string,std::uint64_t,std::less<>> fixedActions;
    std::vector<std::uint64_t> rightActions;
    // Main/supplemental desktop buttons whose empty slots must be hidden.
    // Profile, quit and close regions remain separate fixed actions.
    std::set<std::string,std::less<>> managedButtons;
    bool operator==(const WatchSessionNavigation&)const=default;
};
struct WatchActivation {std::string_view buttonID;std::uint64_t action{};};
struct WatchSessionFrame {
    const source::SourceWatchFrame* sourceFrame{};
    source::CameraFrame camera;
    source::GPUCamera gpuCamera;
    float shaderTime{};
    core::VisibilitySample visibility;
};
struct WatchSessionStats {
    std::uint64_t samples{},concealedSamples{},fullPoses{},retainedPoses{},ambientPatches{},hitQueries{},hoverRebuilds{},bindingChanges{};
};
// Pure HUDSourceWatchView desktop coordinator. Every operation uses the one
// caller-supplied monotonic time. There is no timer, cadence gate, OS callback,
// file access, backdrop capture, renderer, module UI or user-data provider here.
// Immutable dependencies must outlive this object. Returned frame/ID views last
// until the next mutation. Use demand() with OverlayHost's existing frame gate.
class SourceWatchSession final {
public:
    using HitFilter=std::function<bool(std::string_view,core::Point)>;
    SourceWatchSession(const source::SceneDefinition&,const source::MountedLayoutDocument&,
        const source::Library&,const source::SourceCamera&,const source::SourceWatchFrameResources&,
        std::span<const source::AnimatorBinding>,const source::Json& transitions,
        std::optional<source::DesktopHoverProfile> profile={},source::SourceDesktopFrameSettings desktopSettings={});
    ~SourceWatchSession();
    SourceWatchSession(const SourceWatchSession&)=delete;
    SourceWatchSession& operator=(const SourceWatchSession&)=delete;
    void setSettings(WatchSessionSettings,double time);
    void setEnvironment(WatchSessionEnvironment,double time);
    void setInputEnabled(bool,double time);
    void setDesktopSettings(source::SourceDesktopFrameSettings);
    void setNavigation(WatchSessionNavigation,double time);
    void setHitFilter(HitFilter,double time); // e.g. source Map cutout; caller-owned geometry
    // Each lifecycle operation invalidates prior opening tokens. A held open
    // samples the exact first pose, requests no animation wake, and starts only
    // after releaseOpening(token,time). Stale releases return false unchanged.
    std::uint64_t open(double time,std::uint64_t ambientSeed,bool held=false);
    bool releaseOpening(std::uint64_t token,double time);
    void showStable(double time,std::uint64_t ambientSeed);
    void close(double time);
    void conceal(double time);
    void pointerMove(std::optional<core::Point>,double time);
    void pointerDown(core::Point,double time);
    std::optional<WatchActivation> pointerUp(core::Point,double time);
    bool navigationPointerActive()const noexcept;
    bool navigationDragging()const noexcept;
    // Native wheel settings: lines per detent; UINT32_MAX means one viewport.
    bool wheel(core::Point,double steps,std::uint32_t linesPerStep,double time);
    std::optional<WatchActivation> activate(std::string_view buttonID,double time); // same clipped source hit/cooldown checks
    // Wheel deltas are logical points. Exact Mac nonprecise factor (10), plane
    // scale, viewport ownership and gesture/momentum continuation are retained.
    bool scroll(core::Point,double deltaY,bool precise,core::GesturePhase,core::GesturePhase momentum,double time);
    bool scrollDirection(int direction,bool animated,double time);
    void setScrollPosition(double,double time); // explicit restored/navigation position
    const WatchSessionFrame* sample(double time);
    core::FrameDemand demand(double time);
    core::VisibilityPhase phase()const noexcept;
    std::uint64_t generation()const noexcept;
    // Host lifecycle token, separate from visibility's internal playback
    // generation (releasing a held opening restarts that playback clock).
    std::optional<std::uint64_t> completedTransition()const noexcept;
    bool inputEnabled()const noexcept;
    bool pendingOpening()const noexcept;
    double scrollPosition()const noexcept;
    std::optional<std::string_view> hovered()const noexcept;
    std::optional<std::string_view> pressed()const noexcept;
    const std::map<std::string,std::uint64_t,std::less<>>& actions()const noexcept;
    const WatchSessionFrame* currentFrame()const noexcept;
    source::SourceWatchFrameStats frameStats()const noexcept;
    WatchSessionStats stats()const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::app
