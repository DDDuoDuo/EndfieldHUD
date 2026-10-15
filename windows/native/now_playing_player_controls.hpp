#pragma once
// Windows reader of a window-title player's own on-screen controls (the
// counterpart of the Mac NowPlayingAccessibility read of NetEase Cloud Music,
// QQ Music and Kugou): the elapsed and total labels, the play/pause control's
// state and the previous/play/next controls, found in the player window's
// accessibility tree (MSAA IAccessible, modules::nowPlayingWalk and
// modules::nowPlayingPlayerControls) and pressed through their default
// action.
//
// Every accessibility call runs on the OS thread pool, never on the owner
// (UI) thread or the shared utility executor: a busy or hung player can only
// hold one pool thread, and at most one job runs per player process (a hung
// window is skipped after a 250 ms responsiveness check). Elements found are
// kept per process (at most nowPlayingMaximumPlayerProcesses) for re-reading
// and pressing, and released on the pool when the player leaves.
//
// Chromium/CEF players (NetEase Cloud Music 3) expose their page to
// assistive clients only on request: when a walk finds an empty web document
// the reader asks for its tree once (IServiceProvider::QueryService for
// IAccessible2, as screen readers do) and reads again when the player
// announces its first change (awaitingTree). This turns on the player's
// accessibility tree until the player exits, which costs it some CPU.
//
// No label text is kept or logged: a reading holds numbers and flags only.
#ifdef _WIN32
#include "native/now_playing_players.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace endfield::native {
class NowPlayingControlReader final {
public:
    struct Stats {
        std::uint64_t readings{},walks{},rereads{},nodes{},activations{},presses{},pressFailures{},hung{},submitted{},refused{};
        std::size_t processes{},elements{};
    };
    // `now`: the host's thread-safe monotonic clock (dates readings).
    explicit NowPlayingControlReader(std::function<double()> now);
    ~NowPlayingControlReader();
    NowPlayingControlReader(const NowPlayingControlReader&) = delete;
    NowPlayingControlReader& operator=(const NowPlayingControlReader&) = delete;
    // Called on a pool thread after each completed reading (any thread may
    // replace it; null stops notifications).
    void setNotify(std::function<void()>);
    // NowPlayingWindowSystem::reading / press (see there).
    std::optional<NowPlayingPlayerReading> reading(std::uintptr_t window, std::uint32_t pid, bool refresh);
    bool press(std::uintptr_t window, std::uint32_t pid, modules::NowPlayingCommandKind, std::function<void(bool)> done);
    // Whether some process waits for its page tree (cheap, any thread).
    bool awaiting() const noexcept;
    // An event of `pid` after its page tree was requested: reads it again
    // once. true when it did.
    bool treeChanged(std::uint32_t pid);
    // Forgets processes that are not listed (their elements are released on
    // the pool).
    void retain(std::span<const std::uint32_t> pids);
    // Terminal: no new job; queued presses complete with false; the elements
    // are released on the pool.
    void shutdown() noexcept;
    Stats stats() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
#endif
