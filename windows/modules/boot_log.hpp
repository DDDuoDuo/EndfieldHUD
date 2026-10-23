#pragma once
// The daily boot log (spec §3.28): the port of Sources/HUDBootLog.swift
// (HUDBootLog, HUDBootLog.Line, HUDBootLogContent). SHELL-69, SHELL-24,
// SHELL-25, SHELL-78.
//
// A terminal panel on the centre plane that types out a short status of the
// HUD's own data, then wipes up. It is a terminal, so its text stays English
// in every language; only the data varies. The host plays it on the first
// open of the day (never under Reduce Motion or in diagnostic runs), hides
// the centre meanwhile and calls skip() on any click or key
// (native/facility_owner.hpp). Coordinates are content px; the host places
// the root layer on the content box at (10, 30), above the shared overlay
// (HUDLayerOrder.bootLog 30).
//
// Built on the design library: the rows are retained layers (the panel holds
// at most `capacity`, prepared once and reused), motion is numbers; the
// cursor's discrete blink (1 s steps(2): 1, .6, .2, .6) steps through
// Environment::deadlines every 250 ms while the log plays (a Track samples
// linearly between keys, a step needs a key pair per edge), the reveal
// timers are deadlines too.
#include "modules/hud_design_control.hpp"

namespace endfield::modules::boot {
namespace d = design;

// HUDBootLog.Line (:17-92). One terminal line; its parts are flex items 8 px apart.
struct BootLogLine {
    enum class Kind : std::uint8_t { header, entry, sub, footer };
    // The value's colour class: yellow value (the accent), green good,
    // orange warning, or a data colour (the battery's tone).
    struct Tone {
        enum class Kind : std::uint8_t { value, good, warning, color } kind{Kind::value};
        d::Color color{};
        static Tone of(d::Color c) noexcept { return {Kind::color, c}; }
        d::Color resolve(const d::Palette&) const noexcept;
        bool operator==(const Tone&) const = default;
    };
    struct Segment { std::string text; d::Color color; bool operator==(const Segment&) const = default; };
    Kind kind{Kind::entry};
    std::string key;                  // header: the title; entry: the key before the dot leader; sub: the text; footer: the green status
    std::string value;                // header: the timestamp; entry: the coloured value; footer: the greeting
    Tone tone;
    std::optional<std::string> trailing; // plain text two spaces after the value: CHARGE MODE, NEXT +1 IN 04:31
    static BootLogLine header(std::string title, std::string stamp);
    static BootLogLine entry(std::string key, std::string value, Tone tone, std::optional<std::string> trailing = {});
    static BootLogLine entry(std::string key, std::string value);   // the yellow value tone
    static BootLogLine sub(std::string text);
    static BootLogLine footer(std::string status, std::string message);
    // KEY + ' ' + '.' x max(2, 22 - len(KEY)) + ' ' (keys are the fixed ASCII terminal keys).
    static std::string dotLeader(std::string_view key);
    static constexpr d::Color text = d::rgb(0xCFCFCF), subText = d::rgb(0x9A9A9A);
    // The log floats over the shell, dark in both themes: fixed greys.
    static constexpr d::Color steel = d::rgb(0x8A8A8A);
    // The flex items in order (spaces preserved), empty ones dropped.
    std::vector<Segment> segments(const d::Palette&) const;
    bool operator==(const BootLogLine&) const = default;
};

// HUDBootLogContent.lines (:274-308) inputs; a nullopt source leaves its line out.
struct BootLogDate { int year{}, month{}, day{}, hour{}, minute{}; };
struct BootLogBattery {
    int percent{};
    bool charging{};
    // snapshot.levelTone: green and red are the fixed data colours; anything
    // else (the Mac's yellow band) is the accent yellow (the value tone).
    enum class Level : std::uint8_t { green, red, other } level{Level::other};
};
struct BootLogSanity { int current{}, maximum{}; std::optional<double> nextIn; }; // seconds to the next point
struct BootLogWorkStreak { int days{}; double totalHours{}; };
struct BootLogDisk { int freePercent{}; bool low{}; };
std::vector<BootLogLine> bootLogLines(const BootLogDate&, double uptime, const std::optional<BootLogBattery>&,
    std::span<const std::string> eventsToday, const std::optional<BootLogSanity>&, const std::optional<BootLogWorkStreak>&,
    const std::optional<BootLogDisk>&, std::string_view operatorName, const d::TextCasing& uppercase);
// SystemHUDView.bootLogLines' streak (:3077-3083): consecutive local days with
// a Work Mode start or completion, ending today or (if today has none)
// yesterday. `days` are local day numbers of those records, `today` the
// local day number of now.
int bootLogWorkStreak(std::span<const std::int64_t> days, std::int64_t today);
// The streak line's data: nullopt when both the streak and the hours are 0.
std::optional<BootLogWorkStreak> bootLogWork(int streak, double trackedWorkSeconds);
// The local date as "yyyy-MM-dd" (OverlayController.consumeDailyBootLog's day key).
std::string bootLogDay(const BootLogDate&);
// Days since 1970-01-01 of a proleptic Gregorian date (the streak's day numbers).
std::int64_t bootLogDayNumber(int year, int month, int day) noexcept;

// What the boot log reads from the rest of the HUD besides the Event Log,
// Work Mode, the facility monitor and the profile card: published by the
// host before the module areas are built (ApplicationHost::publish under
// bootLogSourcesKey); the host fills `battery` and `eventsToday`, the Account
// area `sanity`. An empty member (or a nullopt answer) leaves its line out,
// as on the Mac when the data is unavailable. Read when the log starts.
struct BootLogSources {
    std::function<std::optional<BootLogBattery>()> battery;            // the Power page's reading (snapshot.percentage, isCharging, levelTone)
    std::function<std::vector<std::string>(double now)> eventsToday;  // Calendar: today's event titles in list order
    std::function<std::optional<BootLogSanity>(double now)> sanity;   // accountController.sanityPresentation()
};
inline constexpr std::string_view bootLogSourcesKey = "hud.bootLog.sources";
// The centre while the log plays (SystemHUDView.playsBootLogOnOpen,
// moduleContent.layer.opacity, finishBootLog's playEntrance): published by
// the facility area (native/facility_owner.hpp) under bootLogStageKey, filled
// by the host once the areas are built. Empty members do nothing.
struct BootLogStage {
    // Called during the opening's setOverlayVisible(true), before the host
    // starts the deploy entrance: true skips it for this opening.
    std::function<void(bool)> deferEntrance;
    // The module host's opacity 0 (true) or 1 (false).
    std::function<void(bool)> concealCentre;
    // The selected design screen plays its entrance from `offset` (content px).
    std::function<void(core::MotionPoint offset, double now)> enterCentre;
};
inline constexpr std::string_view bootLogStageKey = "hud.bootLog.stage";

// HUDBootLog (:94-270).
class BootLog final {
public:
    static constexpr d::Size size{570, 440};
    static constexpr double firstLineTop = 61, lineHeight = 22;
    // Lines that fit above the cursor's own line: Int((440 - 20 - 61) / 22) - 1 = 15.
    static constexpr int capacity = int((size.height - 20 - firstLineTop) / lineHeight) - 1;
    explicit BootLog(d::Environment&);
    ~BootLog();
    BootLog(const BootLog&) = delete;
    BootLog& operator=(const BootLog&) = delete;
    d::Layer& layer() const noexcept { return *layer_; }
    // After the wipe-up: the host shows the centre (entering from below) and
    // runs the facility checks.
    std::function<void()> onFinish;
    // From play until onFinish.
    bool isPlaying() const noexcept { return playing_; }
    int revealedCount() const noexcept { return revealed_; }
    // Opens the panel (bandIn .3 s SNAP) and reveals `lines`: the first at
    // 280 ms, the second 260 ms later, then every 190 ms; 900 ms after the
    // last one (or with none) it finishes. Lines past `capacity` are dropped.
    void play(std::vector<BootLogLine>, std::string_view version);
    // Finishes at once: the wipe-up, then onFinish.
    void skip() { finish(); }
    // Back to the idle, hidden panel without onFinish (the HUD closed under
    // it: the Mac tears the whole view down).
    void stop() { reset(); }
    static double slotTop(int index) noexcept { return firstLineTop + double(index) * lineHeight; }
    // The segments as runs; the last character of every segment but the last
    // carries the 8 px flex gap (as extra tracking).
    static std::vector<d::TextRun> runs(const BootLogLine&, const d::Palette&, const d::CharacterRules&);
    std::uint64_t contentRevision() const noexcept { return arena_.revision(); }
    // Verification.
    d::Rect cursorFrame() const;
    std::vector<std::string> rowTexts() const;
    bool isBlinking() const noexcept { return blink_ != 0; }
    bool isFinishing() const noexcept { return finishing_; }
    std::string terminalText() const;
    const d::Layer& term() const noexcept { return *term_; }
    const d::Layer& row(int index) const { return *rows_.at(std::size_t(index)); }
    const d::Layer& cursor() const noexcept { return *cursor_; }
private:
    d::Environment* env_;
    d::LayerArena arena_;
    d::ControlSet controls_;
    d::Builder builder_;
    d::Layer *layer_, *term_, *title_, *terminal_, *cursor_, *bar_, *tri_;
    std::vector<d::Layer*> rows_;   // `capacity` prepared rows; the first `shown_` play
    int shown_{}, revealed_{}, blinkPhase_{};
    double blinkStart_{};
    bool playing_{}, finishing_{};
    d::Deadlines::Token pending_{}, blink_{};
    void step();
    void finish();
    void complete();
    void reset();
    void startBlink();
    void blinkStep();
    void moveCursor(int slot);
    void cancel(d::Deadlines::Token&) noexcept;
};
} // namespace endfield::modules::boot
