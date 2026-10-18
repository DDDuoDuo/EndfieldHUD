#pragma once
// Window-title fallback for Win32 players that publish no SMTC session, and
// the composite provider that puts it beside GSMTC (WINDOWS-MIGRATION.md
// section 6, Now Playing; Mac NowPlayingAccessibility is the source of the
// compatibility fallback for NetEase Cloud Music, QQ Music and Kugou).
//
// What a window-title player provides: its process identity (the image path,
// mapped to the Event Log source by file name), title and artist parsed from
// its main window title, and - read from the player's own on-screen controls
// through its accessibility tree (NowPlayingPlayerReading; the Mac
// NowPlayingAccessibility read) - its elapsed and total time, its play state
// and its previous / play-pause / next controls. Readings are taken in the
// background and resynchronized on every change: a new title, a change of
// the process's audio activity, a HUD command, the first presentation.
// Without them: the playing state comes from the process's audio activity
// (the shared per-app audio publication) or is INFERRED from title changes
// and the HUD's own commands, and the elapsed time is ESTIMATED from the
// observed title change and the pauses seen since
// (modules::NowPlayingWindowTimeline), anchored on the last real reading.
// Transport: the player's own control (its accessibility default action, the
// player alone receives it), else its registered media-key hotkey, else
// WM_APPCOMMAND (modules::NowPlayingWindowTransport). Recorded platform gaps:
// album, seek, a player thumbnail (NetEase covers come from the public
// catalog lookup), timed lyrics from the player, the elapsed label of a
// minimized player (it may stop updating) and a pause made inside a player
// whose controls cannot be read and that keeps its title and audio stream.
// GSMTC sessions keep precedence: a playing media session always wins, a
// player already listed by GSMTC is never listed twice, and a window-title
// player is presented only when it ranks above the current media session.
//
// Event driven: WinEvent hooks (title change, show, hide, destroy) on the
// owner's message loop while Now Playing is active, and one process-scoped
// title hook per known player while it is hidden (so a later opening knows
// when the current track began; removed for a player whose page announces
// its own elements); no timer or polling.
#include "modules/now_playing_players.hpp"
#include "modules/now_playing_volume.hpp"
#include "native/now_playing_service.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace endfield::native {
inline constexpr std::size_t nowPlayingMaximumPlayerWindows = 64;
inline constexpr std::size_t nowPlayingMaximumPlayerProcesses = 16;
// Set on the window-title sessions' tokens inside a composite snapshot.
inline constexpr std::uint64_t nowPlayingWindowTokenBit = std::uint64_t{1} << 63;

// A reading of a window-title player's own controls through its
// accessibility tree (modules::NowPlayingControlScan; the Mac
// NowPlayingAccessibility read): the elapsed and total labels or the played
// part of its progress bar, the play/pause control's state, which transport
// controls exist and the cover its bar shows, as of `sampledAt` (host
// clock). `startedAt` is when the reading began: a reading started before a
// track change describes the previous track.
struct NowPlayingPlayerReading {
    std::optional<double> position, duration;
    std::optional<bool> playing;
    bool playPause{}, previous{}, next{};
    // The labels were read from a window that was shown (a minimized or
    // hidden player may stop updating its elapsed label or progress bar).
    bool visible{};
    double startedAt{}, sampledAt{};
    // The played part of the player's progress bar (0...1) when it shows one
    // instead of elapsed/total labels (NetEase Cloud Music 3).
    std::optional<double> fraction;
    // The cover the player's bar shows (modules::nowPlayingPlayerArtworkURL).
    std::optional<std::string> artwork;
    // The main window title whose track the bar showed when it was read (one
    // of the bar's texts is that title's track): `fraction` and `artwork`
    // describe the track only while the window still shows this title.
    std::optional<std::string> title;
    bool operator==(const NowPlayingPlayerReading&) const = default;
};

// The OS surface of the window-title fallback: Win32 in production
// (windowsNowPlayingWindowSystem), NowPlayingWindowFixture in tests and the
// hidden coverage.
class NowPlayingWindowSystem {
public:
    virtual ~NowPlayingWindowSystem() = default;
    // Owner thread, which pumps messages. Delivers `changed` on that thread
    // when a player window returned by windows() changes its title, hides or
    // is destroyed, another top-level window of a player process changes its
    // title, or a top-level window of a newly started player process appears.
    // The returned function stops delivery; it is meant for the same thread
    // (elsewhere it retires the subscription, which the thread's next watch
    // removes). Nonthrowing.
    virtual std::function<void()> watch(std::function<void()> changed) = 0;
    // Any thread: top-level windows of running fallback players (z-order,
    // at most nowPlayingMaximumPlayerWindows). Never sends a message.
    virtual std::vector<modules::NowPlayingPlayerWindow> windows() = 0;
    // Any thread: sends the media command for the player whose main window is
    // `window` when it still belongs to `pid`, through the transport
    // modules::nowPlayingWindowTransport chooses (onlyPlayer: no other
    // fallback player process is running). none: delivered; stale: the window
    // is gone; unsupported: seek; unavailable: it could not be sent.
    virtual NowPlayingServiceFailure command(std::uintptr_t window, std::uint32_t pid, modules::NowPlayingCommandKind, bool onlyPlayer) = 0;
    // Any thread: the latest reading of the player's own controls for the
    // process's main window `window`, if one exists. With `refresh` a new
    // reading starts in the background (at most one per process at a time; a
    // request during one queues exactly one more); the watch callback fires,
    // on any thread, when it completes. Default: no reader.
    virtual std::optional<NowPlayingPlayerReading> reading(std::uintptr_t window, std::uint32_t pid, bool refresh) {
        (void)window; (void)pid; (void)refresh;
        return std::nullopt;
    }
    // Any thread: a refresh reads in the background and its completion calls
    // the watch callback (the Win32 reader). Otherwise (the default, and the
    // synthetic desktop, whose refresh is immediate) a refresh is asked for
    // by the provider's next read.
    virtual bool notifiesReadings() const noexcept { return false; }
    // Any thread: presses the player's own control for the command (its
    // accessibility default action) in the background and calls `done(true)`
    // once pressed, `done(false)` when it could not (on any thread, once).
    // false (and no `done`): this system has no such control for the process.
    virtual bool press(std::uintptr_t window, std::uint32_t pid, modules::NowPlayingCommandKind, std::function<void(bool)> done) {
        (void)window; (void)pid; (void)done;
        return false;
    }
    // Owner thread, terminal: removes every hook, including the hidden-time
    // title hooks. Later watch() calls deliver nothing.
    virtual void shutdown() noexcept {}
};

// Per-process audio activity (any of the process's sessions renders audio),
// fed on the owner thread from the shared per-app audio publication and read
// by the window-title provider on the utility worker. Thread-safe, bounded.
class NowPlayingProcessActivity final {
public:
    static constexpr std::size_t maximumProcesses = 512;
    // Replaces the published state with these routes (routes whose rendering
    // was not captured are left out). true when any process's state changed.
    bool update(std::span<const modules::NowPlayingAudioRoute>);
    std::optional<bool> rendering(std::uint32_t pid) const;
    std::uint64_t revision() const;
private:
    mutable std::mutex mutex_;
    std::vector<std::pair<std::uint32_t, bool>> processes_;
    std::uint64_t revision_{};
};

// One session per running fallback player process with a main window
// (modules::nowPlayingMainWindow), at most nowPlayingMaximumPlayerProcesses.
// Its identity is the process image path and its origin playerWindow; the
// current one is the playing player, else one with a track, else the first.
// Capabilities: toggle, previous, next. Playing state: the process's audio
// activity when `activity` knows it; otherwise inferred: a new track title
// (or the title returning after the idle caption) is playing, the idle
// caption keeps the last track as paused, and a delivered play/pause flips
// it; a reading of the player's own controls states it when it is newer
// than the audio activity. Elapsed time: the player's own elapsed label, else
// its progress bar (NowPlayingMediaSnapshot::progress: the played fraction
// of a reading taken while its bar showed this window title's track, plus
// the playing time since; the owner converts it with the track's length; a
// minimized window's bar counts once it moved between readings, is shown
// provisionally before, and withdrawn when it stands still while playing),
// else estimated from the window's titleChangedAt and the pauses observed by
// this provider (none when the change was not observed).
// The cover the bar shows is NowPlayingMediaSnapshot::artworkURL.
// resynchronize() reads the controls of the players that have them anew in
// the next read; refreshControls() (refreshesControls) starts those readings
// at once in the background when the system notifies them
// (notifiesReadings), so the read their completion brings is the only one,
// and otherwise marks them and asks for that read.
// Commands press the player's own control, else (modules::
// nowPlayingWindowTransport) its media-key hotkey while it is the only
// window-title player APPLICATION running (other processes of the same
// player, e.g. its CEF helpers, do not count), else WM_APPCOMMAND. The
// factory runs on the owner thread (watch); `system` is called once per
// provider.
NativeNowPlayingService::Factory nowPlayingWindowProvider(std::function<std::shared_ptr<NowPlayingWindowSystem>()> system,
    std::function<double()> now, std::shared_ptr<const NowPlayingProcessActivity> activity = {});

// GSMTC first, then the window-title players. A source that fails to start
// is left out; the composite is ready as soon as one source is, and a source
// that starts (or fails) later triggers one re-read. Commands go to the
// source that listed the session (nowPlayingWindowTokenBit).
NativeNowPlayingService::Factory nowPlayingCompositeProvider(NativeNowPlayingService::Factory mediaSessions,
    NativeNowPlayingService::Factory playerWindows);
// The composite snapshot. A stale or cancelled media read is returned as is
// (the service re-reads); otherwise the usable reads merge: media sessions
// first, window-title players whose source a media session already maps to
// are dropped, and the window-title player is presented only when it ranks
// higher (playing track > paused track > session without a track), or ranks
// the same with a track while it was the one presented (preferWindows). A
// window-title player is a duplicate when a media session maps to the same
// known source or names the same image (modules::nowPlayingSameExecutable).
// A failed media read is reported when no window-title player has a session.
//
// Per field, the most trustworthy source: a presented media session keeps
// identity, metadata, artwork and transport, but when the same player also
// runs as a window-title player (same known source or image) showing the same
// title, and the session's timeline is unusable (nowPlayingTimelineUsable),
// the track's time comes from that player's own window instead: its
// position, length, sample time, progress-bar reading and controls
// (playerControls, so the owner keeps re-reading them) replace the timeline,
// and the snapshot is marked playerTime. NetEase Cloud Music's own SMTC
// (3.1.41, live) is that case: title, artist, cover and transport, but an
// all-zero timeline, no playback rate and position changes disabled.
NowPlayingReadResult nowPlayingMergeReads(std::optional<NowPlayingReadResult> mediaSessions,
    std::optional<NowPlayingReadResult> playerWindows, bool preferWindows = false);
// A media session's timeline can drive the time: it has a length and a
// position, and, while playing, the player stamped LastUpdatedTime (without
// it the position cannot be carried between reads). Well-behaved players
// (Spotify, browsers, the Store apps) pass.
bool nowPlayingTimelineUsable(const NowPlayingMediaSnapshot&) noexcept;

// Synthetic desktop for tests and the hidden coverage: no window, process or
// message is touched. Thread-safe; notify() delivers one event to every
// watcher on the calling thread.
class NowPlayingWindowFixture final : public NowPlayingWindowSystem {
public:
    struct Command {
        std::uintptr_t window{};
        std::uint32_t pid{};
        modules::NowPlayingCommandKind kind{};
        bool onlyPlayer{};
    };
    // A press of the player's own control (pressControls).
    struct Press {
        std::uint32_t pid{};
        modules::NowPlayingCommandKind kind{};
    };
    std::function<void()> watch(std::function<void()> changed) override;
    std::vector<modules::NowPlayingPlayerWindow> windows() override;
    NowPlayingServiceFailure command(std::uintptr_t window, std::uint32_t pid, modules::NowPlayingCommandKind, bool onlyPlayer) override;
    // A reading set with setReading is returned once a refresh asked for it
    // (startedAt and sampledAt are the refresh's time from `clock`).
    std::optional<NowPlayingPlayerReading> reading(std::uintptr_t window, std::uint32_t pid, bool refresh) override;
    bool press(std::uintptr_t window, std::uint32_t pid, modules::NowPlayingCommandKind, std::function<void(bool)> done) override;
    void shutdown() noexcept override;
    void set(std::vector<modules::NowPlayingPlayerWindow>);
    void notify();
    void refuseCommands(bool);
    // The player's controls as its accessibility tree would show them (none:
    // no reader result for that process). `clock` dates refreshes.
    void setReading(std::uint32_t pid, std::optional<NowPlayingPlayerReading>, std::function<double()> clock = {});
    // Whether presses of the player's own controls succeed (default: no controls).
    void pressControls(bool);
    std::vector<Command> commands() const;
    std::vector<Press> presses() const;
    std::size_t refreshes() const;
    std::size_t watchers() const;
    std::size_t reads() const;
    bool stopped() const;
private:
    mutable std::mutex mutex_;
    std::vector<modules::NowPlayingPlayerWindow> windows_;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> watchers_;
    std::vector<Command> commands_;
    std::vector<Press> presses_;
    struct Reader { std::uint32_t pid{}; std::optional<NowPlayingPlayerReading> value, delivered; std::function<double()> clock; };
    std::vector<Reader> readers_;
    std::uint64_t next_{};
    std::size_t reads_{}, refreshes_{};
    bool refuse_{}, stopped_{}, controls_{};
};

#ifdef _WIN32
// EnumWindows with a bounded per-provider process classification cache,
// GetWindowTextW (no message to the player), WinEvent hooks on the calling
// (owner) thread, the title-change times of the players' windows (kept while
// Now Playing is hidden by one process-scoped NAMECHANGE hook per known
// player, at most nowPlayingMaximumPlayerProcesses, removed while a player
// floods it with more than 20 events a second), the players' own controls
// read and pressed through MSAA on the OS thread pool
// (native/now_playing_player_controls.hpp), and the command transport of
// modules::nowPlayingWindowTransport. `now` is the host's thread-safe
// monotonic clock. Create it once per owner and shutdown() it on the owner
// thread before destruction.
std::shared_ptr<NowPlayingWindowSystem> windowsNowPlayingWindowSystem(std::function<double()> now);
// Whether another program holds the command's media key as a registered
// global hotkey (it is registered for an instant on the calling thread and
// released at once when nobody held it). Seek: false.
bool windowsNowPlayingMediaKeyHeld(modules::NowPlayingCommandKind);
// Production: GSMTC (windowsNowPlayingProvider) composed with the window-title
// players of `system`. The factory must run on a thread with a message loop.
NativeNowPlayingService::Factory windowsNowPlayingPlayers(std::function<double()> now, std::shared_ptr<NowPlayingWindowSystem> system,
    std::shared_ptr<const NowPlayingProcessActivity> activity = {});
#endif
} // namespace endfield::native
