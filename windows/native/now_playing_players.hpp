#pragma once
// Window-title fallback for Win32 players that publish no SMTC session, and
// the composite provider that puts it beside GSMTC (WINDOWS-MIGRATION.md
// section 6, Now Playing; Mac NowPlayingAccessibility is the source of the
// compatibility fallback for NetEase Cloud Music, QQ Music and Kugou).
//
// What a window-title player provides: its process identity (the image path,
// mapped to the Event Log source by file name), title and artist parsed from
// its main window title, a playing/paused state INFERRED from title changes
// and the HUD's own play/pause commands, and play/pause, previous and next
// sent as WM_APPCOMMAND to that window. What it cannot provide (recorded
// platform gaps): album, duration, position and progress, seek, a player
// thumbnail (NetEase covers come from the public catalog lookup), timed
// lyrics from the player, and a pause made inside a player that keeps its
// title. GSMTC sessions keep precedence: a playing media session always wins,
// a player already listed by GSMTC is never listed twice, and a window-title
// player is presented only when it ranks above the current media session.
//
// Event driven: WinEvent hooks (title change, show, hide, destroy) on the
// owner's message loop while Now Playing is active; no timer or polling.
#include "modules/now_playing_players.hpp"
#include "native/now_playing_service.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace endfield::native {
inline constexpr std::size_t nowPlayingMaximumPlayerWindows = 64;
inline constexpr std::size_t nowPlayingMaximumPlayerProcesses = 16;
// Set on the window-title sessions' tokens inside a composite snapshot.
inline constexpr std::uint64_t nowPlayingWindowTokenBit = std::uint64_t{1} << 63;

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
    // Any thread: posts the media command to the window when it still belongs
    // to `pid`. none: posted; stale: the window is gone; unsupported: seek;
    // unavailable: it could not be posted.
    virtual NowPlayingServiceFailure command(std::uintptr_t window, std::uint32_t pid, modules::NowPlayingCommandKind) = 0;
};

// One session per running fallback player process with a main window
// (modules::nowPlayingMainWindow), at most nowPlayingMaximumPlayerProcesses.
// Its identity is the process image path and its origin playerWindow; the
// current one is the playing player, else one with a track, else the first.
// Capabilities: toggle, previous, next. Playing state inference: a new track
// title (or the title returning after the idle caption) is playing; the idle
// caption keeps the last track as paused; a delivered play/pause flips it.
// The factory runs on the owner thread (watch); `system` is called once per
// provider.
NativeNowPlayingService::Factory nowPlayingWindowProvider(std::function<std::shared_ptr<NowPlayingWindowSystem>()> system,
    std::function<double()> now);

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
// failed media read is reported when no window-title player has a session.
NowPlayingReadResult nowPlayingMergeReads(std::optional<NowPlayingReadResult> mediaSessions,
    std::optional<NowPlayingReadResult> playerWindows, bool preferWindows = false);

// Synthetic desktop for tests and the hidden coverage: no window, process or
// message is touched. Thread-safe; notify() delivers one event to every
// watcher on the calling thread.
class NowPlayingWindowFixture final : public NowPlayingWindowSystem {
public:
    struct Command {
        std::uintptr_t window{};
        std::uint32_t pid{};
        modules::NowPlayingCommandKind kind{};
    };
    std::function<void()> watch(std::function<void()> changed) override;
    std::vector<modules::NowPlayingPlayerWindow> windows() override;
    NowPlayingServiceFailure command(std::uintptr_t window, std::uint32_t pid, modules::NowPlayingCommandKind) override;
    void set(std::vector<modules::NowPlayingPlayerWindow>);
    void notify();
    void refuseCommands(bool);
    std::vector<Command> commands() const;
    std::size_t watchers() const;
    std::size_t reads() const;
private:
    mutable std::mutex mutex_;
    std::vector<modules::NowPlayingPlayerWindow> windows_;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> watchers_;
    std::vector<Command> commands_;
    std::uint64_t next_{};
    std::size_t reads_{};
    bool refuse_{};
};

#ifdef _WIN32
// EnumWindows with a bounded per-provider process classification cache,
// GetWindowTextW (no message to the player), WinEvent hooks on the calling
// thread and WM_APPCOMMAND (APPCOMMAND_MEDIA_PLAY_PAUSE / PREVIOUSTRACK /
// NEXTTRACK) posted to the player's main window.
std::shared_ptr<NowPlayingWindowSystem> windowsNowPlayingWindowSystem();
// Production: GSMTC (windowsNowPlayingProvider) composed with the window-title
// players. The factory must run on a thread with a message loop.
NativeNowPlayingService::Factory windowsNowPlayingPlayers(std::function<double()> now);
#endif
} // namespace endfield::native
