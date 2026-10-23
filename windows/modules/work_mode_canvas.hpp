#pragma once
// Work Mode on the redesign: the port of Sources/WorkModeCanvas.swift (Mac
// 1.3.0, spec section 3.16). No module header; the 620 px ring around the
// disc and a column of title, Countdown / Stopwatch tabs, state label, 112 px
// timer, presets (or the custom minutes field) or laps, and the actions.
// MODS-722..742.
//
// The controller (modules/work_mode.hpp) owns the session, its deadlines and
// the tracked work time (the Personal Profile area observes its checkpoints
// and is the only profile.json writer); the app keeps it running while the
// canvas is not shown and routes WorkModeHooks::changed to controllerChanged().
// This canvas presents it: the one-second tick changes only the digits, the
// ring and the primary button's label in place; phase, mode, preset, lap,
// editor and Focus changes re-render the page in place (MODS-740).
//
// Ring adaptations (numbers per frame, pixels per content event): the arc's
// strokeEnd (.6 s E) is two half rings rotating under fixed half-plane clips
// (rotation is a pose; the half's rotation keyframes follow the clamped
// strokeEnd exactly, also across the half-way point); the 120 lit/unlit ticks
// are painted in twelve 30-degree chunks, so a lit-count change repaints one
// small surface (the Mac re-paths both tick layers). The static circles,
// labels and the fixed marker are painted below the arc, ticks and marker
// (they do not overlap them, so the pixels equal the Mac's order).
#include "modules/hud_design_page.hpp"
#include "modules/work_mode.hpp"

namespace endfield::modules {

// WorkModeDialGeometry (:6-45): a 620 px square centred on the disc, outside
// the content box (box px; the ring's own centre is (310, 310)).
namespace workModeDial {
inline constexpr design::Rect frame{-15, -5, 620, 620};
inline constexpr design::Point center{310, 310};
inline constexpr double radius = 310, arcRadius = 252;
inline constexpr int tickCount = 120;
// Countdown: displayed seconds / total (full at the start); stopwatch: the
// second hand's position. Both follow the displayed seconds.
double fraction(const WorkModeSnapshot&);
// Ticks k with k / 120 < fraction are lit.
int litTicks(double fraction);
// Filled quads for ticks [first, last): r 292 inward to 268 (every 10th, 4
// wide) or 278 (2.4 wide), in ring coordinates.
design::PathBuilder ticks(int first, int last);
}

struct WorkModeCanvasOptions {
    // The controller's clock (the owner's continuous host time); the canvas
    // reads snapshots and starts actions at this time.
    std::function<double()> clock;
    // context.host?.window?.isVisible: the HUD window is on screen (the
    // completion toast needs it, not the Work Mode page).
    std::function<bool()> hudVisible;
};

class WorkModeCanvas final : public design::Canvas {
public:
    static constexpr std::array<double, 3> presets{5 * 60, 30 * 60, 60 * 60};
    static constexpr double ringTransition = .6;   // layoutTransitionDuration (:175)
    WorkModeCanvas(design::Context&, WorkModeController&, WorkModeCanvasOptions);
    ~WorkModeCanvas() override;
    WorkModeController& controller() const noexcept { return *controller_; }

    // --- Host interface --------------------------------------------------------
    // Real Focus failures from the app owner (:201-209): a toast when one
    // appears and a warning button beside the state label (it opens the
    // accessibility settings when that permission is missing). Windows has no
    // Focus executor (WINDOWS-MIGRATION.md section 6): nothing calls it today.
    void setFocusStatusMessage(std::optional<std::string>, bool needsAccessibilityPermission = false);
    std::function<void()> onRequestFocusAccess;
    // WorkModeHooks::changed (controller.observe): the completion toast, then
    // an in-place refresh unless an action of this canvas is running.
    void controllerChanged();

    // --- Actions (accessibility stand-ins, tests and diagnostics) -----------------
    void openCustom();
    void commitCustom(const std::string&);
    void primaryAction();
    void recordLap();
    void reset();

    static std::string timeText(const WorkModeSnapshot&);   // HUDFormat.clock: MM:SS, minutes unbounded
    // `5 min` for whole minutes, `1:30` otherwise (L10n "{0} min" / "{0} 分").
    static std::string durationTitle(double seconds, core::Language);

    // --- Testing (the Mac's ...ForTesting) ------------------------------------------
    bool editingCustom() const noexcept { return editingCustom_; }
    const std::optional<std::string>& focusMessage() const noexcept { return focusMessage_; }
    std::vector<std::string> lapTexts() const;
    std::optional<std::string> timerText() const;
    std::optional<double> ringFraction() const;
    design::TextField& customField();

    // --- design::Canvas ---------------------------------------------------------------
    design::Rect hitBounds() const override;
    void buildPage(design::Page&) override;
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override;
    void playEntrance(design::Point offset, double now) override;
    void recordingModeChanged() override {}       // nothing is redacted (:245)
    void reduceMotionChanged() override;          // refreshMotionPreference (:212-215)
    bool handleExtraKey(const design::KeyInput&) override;
private:
    class Ring;
    struct Lap { int number{}, seconds{}; bool operator==(const Lap&) const = default; };
    struct Structure {
        WorkModeKind kind{}; WorkModePhase phase{}; double duration{};
        std::vector<Lap> laps; bool editingCustom{}; std::optional<std::string> focus; bool focusAccess{};
        bool operator==(const Structure&) const = default;
    };
    WorkModeController* controller_;
    WorkModeCanvasOptions options_;
    std::optional<Structure> renderedStructure_;
    WorkModePhase lastPhase_;
    std::vector<Lap> laps_;
    int lapCount_{};
    bool editingCustom_{}, applying_{}, focusNeedsAccess_{};
    std::optional<std::string> focusMessage_;
    std::unique_ptr<Ring> ring_;
    design::Layer *timerText_{}, *stateDot_{}, *titleGroup_{};
    design::Button* primary_{};
    design::TextField* customField_{};          // lazy, canvas-owned (canvasBuilder)

    double time() const;
    WorkModeSnapshot snapshot() const;
    Structure structure() const;
    void refresh(bool animated);
    template <class F> void apply(F&& change) { applying_ = true; change(); applying_ = false; lastPhase_ = snapshot().phase; }
    void applyBlink();
    std::string accessibilityStatus(const WorkModeSnapshot&) const;
    void buildTitle(design::Page&);
    void buildTabs(design::Page&, const WorkModeSnapshot&);
    void buildState(design::Page&, const WorkModeSnapshot&);
    void buildPresets(design::Page&, const WorkModeSnapshot&);
    double buildLaps(design::Page&);
    void buildActions(design::Page&, const WorkModeSnapshot&, double top);
    void chooseKind(WorkModeKind, double direction);
    void pickPreset(int index);
    void closeCustom();
    void focusAlertPressed();
};
} // namespace endfield::modules
