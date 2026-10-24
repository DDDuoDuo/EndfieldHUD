#pragma once
// Merge! OrbiPom! (spec section 3.11): the port of Sources/OrbiPomCanvas.swift
// on the design library. The original session and physics (OrbiPomSession over
// the bundled Matter 0.20.0 + orbipom.js, modules/orbipom_runtime.hpp) drawn on
// the redesigned board, with the SCORE / NEXT plates, the SP bar, the four
// skills and Pause / Restart in a narrow column on its right; the header "?"
// opens the How to play pop-over (play holds while it is open). The space left
// of the board stays empty (owner: reserved). Inventory MODS-200..230.
//
// The frame clock (the Mac's 60 Hz Timer, 30 Hz in low-power visual mode) is
// this canvas's nextWakeTime / wake: it runs only while the module is the
// visible active screen, the app is in the foreground and awake, not manually
// paused, no restart confirm, no instructions open and no session error. Each
// tick advances the session by the measured elapsed time and updates the board
// in place: bodies are retained layers placed numerically (position, rotation,
// scale, opacity), so a tick rasterizes nothing; only new pieces, the texts and
// the plates' contents are content events.
//
// Labels SCORE / NEXT / SP / BEST and the board titles are English literals in
// every language, as on the Mac (owner decision Q3: copy the Mac, although
// WINDOWS-MIGRATION.md section 6 named 技力). No sound.
//
// Deliberately not done here (Windows differences, see the C9 report):
//  - the artwork is the original 15 PNGs (Resources/OrbiPom) as pinned asset
//    nodes: the shared rasterizer decodes each once and draws it at the
//    piece's drawn size (the Mac decodes ImageIO thumbnails per pixel size and
//    drops them on deactivate; here the raster entries go with the scene's
//    park and the decoded originals stay in the rasterizer's bounded cache);
//  - the danger line's colour pulse (CABasicAnimation, autoreverse) is set per
//    tick as the line's tint: it runs only while the clock runs, exactly when
//    the Mac's pulse runs (canAdvance), so the samples are the same;
//  - the wind field moves as one numeric surface (its top edge follows the
//    wind surface) with its 1 px bottom edge a separate static line, instead
//    of a path rebuilt every frame.
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_page.hpp"
#include "modules/orbipom_state.hpp"
#include <unordered_map>

namespace endfield::modules {
// The original artwork (Resources/OrbiPom, as in v1.2.0), pinned by SHA-256:
// level-1..11 then skill-clear, wind, shake, swap. Relative to the minigame
// resource root (the owner's raster asset root).
struct OrbiPomAsset { std::string_view file, sha256; unsigned width{}, height{}; };
std::span<const OrbiPomAsset> orbiPomAssets() noexcept;
design::ImageSource orbiPomLevelImage(int level) noexcept;   // clamped to 1...11 (OrbiPomArtwork.level)
design::ImageSource orbiPomSkillImage(OrbiPomSkill) noexcept;

class OrbiPomCanvas final : public design::Canvas {
public:
    // The original container (230 x 280) plus its drop zone, scaled to the
    // demo's 375 px board height: one game unit is 1.2 px (:10-19).
    static constexpr double unit = 1.2, dropZone = 32.5, worldWidth = 230, worldHeight = 280;
    static constexpr design::Rect board{157, design::metric::headerHeight, worldWidth * unit, 375}; // x ((590 - 276) / 2).rounded()
    static constexpr design::Rect column{board.x + board.width + 12, board.y, design::metric::boxWidth - board.x - board.width - 12, board.height};
    static design::Point world(design::Point) noexcept;              // box px -> game units
    static design::Point boardPoint(double x, double y) noexcept;    // game units -> board px
    // OrbiPomPiece.side: a level's sprite box in board px (the original's
    // 24 ... 192 x its 0.8 scale, in game units x 1.2), level clamped 1...11.
    static double pieceSide(int level) noexcept;
    static constexpr std::array<std::string_view, 4> skillIDs{"clear", "wind", "shake", "swap"};

    // `foreground`: NSApp.isActive (the HUD's application activation),
    // re-read on activation and on wake from sleep.
    OrbiPomCanvas(design::Context&, OrbiPomSession&, std::function<bool()> foreground = {});
    ~OrbiPomCanvas() override;
    OrbiPomSession& session() const noexcept { return *session_; }
    bool restartConfirmation() const noexcept { return restartConfirmation_; }
    bool rulesPresented() const noexcept { return rulesPresented_; }
    bool hasFrameTimer() const noexcept { return timer_.has_value(); }
    std::size_t bodyLayerCount() const noexcept { return bodies_.size(); }
    // Harness: false keeps the clock from scheduling ticks (the Mac's
    // automaticallySchedulesFrames); tests then call advance themselves.
    bool automaticallySchedulesFrames{true};
    double frameInterval() const noexcept { return lowPower_ ? 1. / 30 : 1. / 60; }

    // Application activation (didBecomeActive / didResignActive) and sleep /
    // wake (willSleep / didWake: wake re-reads the provider).
    void setForeground(bool, double now);
    void systemSuspended(bool, double now);
    // HUDRuntimeAppearance.configuration.lowPowerVisualMode (ModuleAppearance::lowPower).
    void setLowPowerVisualMode(bool, double now);
    // The original action ids (accessibility, keyboard and the harness):
    // start, pause, rules, restart, cancelRestart, confirmRestart,
    // cancelSkill and the skill ids.
    void perform(std::string_view id);
    void advance(double seconds);

    // --- design::Canvas ---
    void buildPage(design::Page&) override;
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override;
    void didClosePopover() override;
    void handlePointerMoved(std::optional<design::Point>) override;
    bool handlePointerDown(design::Point, const design::PointerInput&) override;
    void handlePointerDragged(design::Point) override;
    void handlePointerUp(design::Point) override;
    bool handleKey(const design::KeyInput&) override;
    std::optional<double> nextWakeTime(double) const override;
    void wake(double now) override;

    class Piece;
    class SkillButton;
private:
    struct Rendered {
        std::int64_t score{}, best{}; int energy{}, progress{}, next{}, ghost{};
        std::optional<std::string> danger, overlay;
        // The controls key (:463-464): state, pauses, confirm, energy, charge, skill, phase, error.
        std::string state; bool paused{}, manual{}, confirm{}; int controlsEnergy{}, charge{};
        std::optional<OrbiPomSkill> skill; std::optional<std::string> phase, error;
        bool sameControls(const Rendered&) const noexcept;
    };
    OrbiPomSession* session_;
    std::function<bool()> foregroundProvider_;
    bool foreground_{true}, lowPower_{}, restartConfirmation_{}, rulesPresented_{}, pointerPressed_{};
    std::optional<double> timer_;           // the last tick (the Timer's lastTime) while the clock runs
    double timerInterval_{};
    design::Point keyboardPoint_{115, 120};
    std::optional<OrbiPomSkill> lastSkill_;
    std::int64_t lastSkillUses_{};
    std::string lastState_{"idle"};
    // Board and column of the current page (rebuilt with the page, updated per tick).
    design::Layer *pieces_{}, *guide_{}, *danger_{}, *countdown_{}, *wind_{}, *windEdge_{}, *overlay_{}, *overlayText_{}, *nextWell_{};
    Piece *ghost_{}, *nextPiece_{};
    design::StatPlate* scorePlate_{};
    design::Layer *bestText_{}, *energyText_{};
    design::Cells* cells_{};
    std::array<SkillButton*, 4> skillButtons_{};
    design::Button *pauseButton_{}, *resumeButton_{};
    design::IconButton* rulesButton_{};
    std::vector<std::unique_ptr<Piece>> pool_;   // body pieces; released ones are reused (a page arena never shrinks)
    std::unique_ptr<Piece> ghostOwned_, nextOwned_;
    std::unordered_map<std::int64_t, Piece*> bodies_;
    std::uint64_t sweep_{};
    std::optional<Rendered> rendered_;
    bool dangerShown_{};
    std::optional<double> dangerPulse_;     // the pulse's start while it runs
    std::optional<bool> dangerCounting_;    // the line's drawn style (solid while counting)
    bool shouldRun() const noexcept;
    void reconcileClock();
    void stopClock() noexcept { timer_.reset(); }
    void startGame(bool announce);
    void showError();
    void togglePause();
    void openRules();
    void requestRestart();
    design::Point gamePoint(design::Point) const noexcept;
    void aim(design::Point);
    void buildBoard(design::Page&);
    void buildColumn(design::Page&);
    void render();
    void announce(const OrbiPomSnapshot&);
    void renderOverlay(const std::optional<std::string>&);
    void renderDanger(const std::optional<std::string>&, bool pulsing);
    void renderPulse();
    void pulse(bool);
    void renderControls(const OrbiPomSnapshot&);
    void renderBoard(const OrbiPomSnapshot&, bool ghostChanged);
    Piece& piece(std::int64_t id, int level, double side);
};
} // namespace endfield::modules
