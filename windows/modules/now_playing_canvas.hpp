#pragma once
// Now Playing (spec section 3.12): the port of Sources/NowPlayingCanvas.swift
// on the design library. There is no module header: the 480 px cover reaches
// 84 px above the box with the title shade on it, three centred lyric lines
// and the timeline sit under it, and one button row runs level with the
// shell's battery chip (transport on the left, volume and lyrics on the
// right). Metadata, transport and lyric changes update the page in place;
// another track re-enters from the right (from the left after Previous).
// Player observation follows the owner's presentation, which starts while
// the HUD reveals; one visible-only deadline advances the readouts at whole
// seconds and lyric cues, and the bar runs numerically in between without
// sampling a player. Inventory MODS-300..338, MODS-361.
//
// The controller is the owner's (tools/now_playing_session.hpp: GSMTC, the
// window-title players, NetEase's bar and queue file, LRCLIB / NetEase
// lyrics and covers, the per-app mixer route), reached through
// NowPlayingCanvasServices; the canvas reads its published
// NowPlayingViewInput. The decoded cover is drawn by the owner as a texture
// between the cover plate and its shade (native/now_playing_cover.hpp, at
// the art layers' sampled poses): the canvas lays out two content-less art
// layers and cross-fades them (.2 s ease-in-out, the CATransition fade)
// whenever the cover changes, which keeps the NetEase thumbnail -> catalog
// cover cross-fade. A cover that fills the slot is drawn aspect-filled; a
// small one (Mac 1.3.1, 45f75dc: one with a backdrop) is drawn whole at its
// own pixels, centred over its blurred backdrop, which fills the slot and
// fades with its cover (coverFrame).
//
// Windows differences (C9 report): GSMTC capabilities gate the transport and
// the seek bar besides the Mac's rules; the elapsed time runs at the
// published GSMTC playback rate; the playing app's volume is the per-app
// mixer route only (SMTC exposes no player volume, no permission prompt);
// the single end-of-track refresh and the window-title players' once-a-second
// re-read stay on the controller's own clock (the canvas does not call
// refresh itself).
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_page.hpp"
#include "modules/now_playing_model.hpp"
#include <map>

namespace endfield::modules {
struct NowPlayingCanvasServices {
    std::function<const NowPlayingViewInput&()> input;   // controller.snapshot (+ lyrics, volume, cover state)
    std::function<NowPlayingCoverArt()> artwork;         // controller.artwork (the image is its identity)
    std::function<void(bool)> setPresented;               // controller.activate / deactivate + the audio watch
    std::function<bool(const NowPlayingIntent&)> perform; // transport, seek, app volume
    std::function<void(NowPlayingCoverSlot)> setArtworkPixels; // controller.artworkPixelSize
};

class NowPlayingCanvas final : public design::Canvas {
public:
    struct Layout {
        // Centred and 480 px (the demo's 420 enlarged): it reaches 84 px above
        // the box, still clear of the shell's charge readout over it.
        static constexpr design::Rect cover{55, -84, 480, 480};
        static constexpr design::Rect lyrics{55, 400, 480, 60};
        static constexpr design::Rect timeline{55, 464, 480, 34};
        // Previous, play and next with a 10 px gap, top 516; the spec's left
        // -6 moves 30 px out so Next clears the shell's charge capsule.
        static constexpr design::Rect transport{-36, 516, 162, 54};
        static constexpr design::Rect extras{528, 527, 68, 30};   // volume and lyrics, 8 apart
        static constexpr design::Rect popup{518, 364, 44, 150};
        static constexpr design::Rect hitBounds{-36, -84, 632, 654};
    };
    // One cover the owner draws: an art layer, the image it shows at `frame`
    // (in the layer's bounds) and the backdrop that fills the layer under it.
    struct CoverView { const design::Layer* layer{}; NowPlayingCover image, backdrop; design::Rect frame{}; };
    // Where a width x height pixel cover is drawn in the 480 px slot
    // (showArt, :325-347): without a backdrop aspect-filled (cropped by the
    // cover's mask); with one at its own pixels (px / boxScale, the Mac's
    // contentsScale = renderScale with .center), centred and, should it
    // exceed the slot, fitted inside it.
    static design::Rect coverFrame(unsigned width, unsigned height, bool backdrop, double boxScale) noexcept;

    NowPlayingCanvas(design::Context&, NowPlayingCanvasServices);
    ~NowPlayingCanvas() override;
    // setPresented (:116-129): observation from the start of the reveal.
    void setPresented(bool, double now);
    bool presented() const noexcept { return presented_; }
    // The controller published a new snapshot (its observers, :100).
    void refresh(double now);
    bool volumeOpen() const noexcept { return volumeOpen_; }
    bool showsLyrics() const noexcept { return showsLyrics_; }
    bool clockActive() const noexcept { return clockDeadline_.has_value(); }
    // The covers to draw (bottom first) and the cover layer (their clip).
    // While a track change's page swap runs (or a module leave settled it
    // concealed), these are the leaving page's: its cover leaves with it
    // (OPEN-B5-5: d6370b0's departing page keeps its cover). The next page's
    // own cover is drawn from its landing.
    std::array<CoverView, 2> coverViews(double now) const noexcept;
    const design::Layer* coverLayer() const noexcept { return leavingCover_ ? leavingCover_->plate : cover_; }
    static std::string time(std::optional<double>);   // NowPlayingCanvas.time (:395-398)
    // previous, playPause, next, volume, lyrics (perform(actionID:), :588).
    bool perform(std::string_view actionID, double now) { return performAccessibilityAction(actionID, now); }

    // --- design::Canvas ---
    void buildPage(design::Page&) override;
    void recordingModeChanged() override {}   // no identity data (:131)
    // A new HUD session (the Mac's new NowPlayingCanvas, :21): lyrics shown.
    void resetSession() override;
    void renderScaleChanged() override;
    void closePopups() override;
    bool capturesPointer() const override { return Canvas::capturesPointer() || volumeOpen_; }
    design::Rect hitBounds() const override { return Layout::hitBounds; }
    bool interceptPointerDown(design::Point, const design::PointerInput&) override;
    bool handleKey(const design::KeyInput&) override;
    bool handlesKeyDuringSwap(const design::KeyInput&) override;
    bool adjustAccessibilitySlider(std::string_view id, double value, double now) override;
    std::optional<double> nextWakeTime(double) const override;
    void wake(double now) override;

    class SeekBar;
    class VolumePopup;
    class PlayButton;
    class LyricWindow;
private:
    struct Structure { core::Language language{}; bool playing{}; bool operator==(const Structure&) const = default; };
    // NowPlayingCanvas.finiteKeys (:144): what the stop keeps on the hidden
    // page, the entrance slots (the cover cross-fade included) and the leave.
    static constexpr design::MotionSlots finiteKeys = design::entranceSlots | design::MotionSlots{design::MotionSlot::leave};
    NowPlayingCanvasServices services_;
    bool presented_{}, volumeOpen_{}, showsLyrics_{true}, lyricsVisible_{};
    std::optional<Structure> renderedStructure_;
    std::optional<std::string> shownTrack_;
    std::optional<std::pair<double, double>> pendingDirection_;   // (dx, until)
    std::optional<double> clockDeadline_;
    // Gestures belong to the player (and track / audio route) on which they began.
    std::optional<std::pair<std::uint64_t, NowPlayingTrack>> seekGesture_;
    std::optional<std::pair<std::uint64_t, std::string>> volumeGesture_;
    NowPlayingCover displayed_;
    std::array<NowPlayingCoverArt, 2> covers_{}; // what the two art layers show (bottom, top)
    double fadeEnd_{};               // the cross-fade's end: the old cover drops then
    // The shown page's cover when a swap builds the next page (buildPage
    // overwrites cover_, art_ and covers_ for it), until that page lands.
    struct LeavingCover {
        design::Layer* plate{};
        std::array<design::Layer*, 2> art{};
        std::array<NowPlayingCoverArt, 2> covers{};
        double fadeEnd{};
    };
    std::optional<LeavingCover> leavingCover_;
    // Elements of the current page, updated in place between rebuilds.
    design::Layer *cover_{}, *placeholder_{}, *titleText_{}, *artistText_{}, *elapsedText_{}, *durationText_{};
    std::array<design::Layer*, 2> art_{};
    LyricWindow* lyricWindow_{};
    SeekBar* seek_{};
    PlayButton* playButton_{};
    design::IconButton *previousButton_{}, *nextButton_{}, *volumeButton_{}, *lyricsButton_{};
    VolumePopup* popup_{};
    design::Readout* trackReadout_{};
    std::vector<std::shared_ptr<void>> parts_;   // the lyric window (page-lifetime helper)
    const NowPlayingViewInput& input() const;
    Structure structure() const;
    std::optional<std::string> identity() const;
    double takeDirection();
    design::Layer& buildCover(design::Page&, const std::optional<NowPlayingTrack>&);
    design::Layer& buildTimeline(design::Page&);
    void update(bool animated);
    bool canSeek() const;
    void showArt(bool animated);
    void didInstall(design::Page&) override;
    void setLyricsVisible(bool visible, bool animated);
    std::optional<double> displayedElapsed() const;
    void updateClock(bool animated);
    void reconcileClock();
    void stopClock() noexcept { clockDeadline_.reset(); }
    void tick();
    void previousTrack();
    void nextTrack();
    void toggleLyrics();
    void scrub(double value);
    void endScrub(std::optional<double> value);
    void skip(double seconds);
    bool setAppVolume(double value);
    void toggleVolume();
    void openVolume();
    // `keepingDrawn` (the module leaving, NowPlayingCanvas.swift:584): the
    // pop-up stops taking input but stays drawn to leave with the page until
    // the next showing (Canvas::retirePopup).
    void closeVolume(bool keepingDrawn = false);
    void adjustVolume(double value, bool final);
    void renderPopup();
    std::string trackStatus() const;
};
} // namespace endfield::modules
