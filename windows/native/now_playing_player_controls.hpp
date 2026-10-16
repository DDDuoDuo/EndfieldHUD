#pragma once
// Windows reader of a window-title player's own on-screen controls (the
// counterpart of the Mac NowPlayingAccessibility read of NetEase Cloud Music,
// QQ Music and Kugou): the elapsed and total labels or the played part of
// its progress bar, the play/pause control's state, the previous/play/next
// controls and the bar's cover image, found in the player window's
// accessibility tree (MSAA IAccessible with IAccessible2 object attributes,
// modules::nowPlayingWalk and modules::NowPlayingControlScan) and pressed
// through their default action.
//
// Every accessibility call runs on the OS thread pool, never on the owner
// (UI) thread or the shared utility executor: a busy or hung player can only
// hold one pool thread, and at most one job runs per player process (a hung
// window is skipped after a 250 ms responsiveness check). Elements found are
// kept per process (at most nowPlayingMaximumPlayerProcesses) for re-reading
// and pressing, and released on the pool when the player leaves.
//
// Chromium/CEF players (NetEase Cloud Music 3, CEF 91): the walk starts at
// the page document itself (the Chrome_RenderWidgetHostHWND child's client,
// or, when Chromium has dropped that window, the first document a few levels
// into the window's tree), so the player bar lies within the source walk's
// depth. The page is exposed only once an assistive client asks: when a walk
// finds an empty web document the reader answers Chromium's screen-reader
// probe once (WM_GETOBJECT with its object id 1 to the page's windows, then
// the document's name) and asks for IAccessible2, as screen readers do, then
// reads again on the page's first events (awaiting). Live on NetEase 3.1.41
// the IAccessible2 request alone left the page empty; the probe built it
// within 300 ms. This turns on the player's accessibility tree until the
// player exits, which costs it some CPU.
//
// No label text is logged. A reading holds numbers, flags, an allow-listed
// cover URL and, when the bar shows the track its window title names, that
// window title (the same text the window-title fallback already reads).
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
        std::uint64_t readings{},walks{},rereads{},nodes{},activations{},presses{},pressFailures{},hung{},submitted{},refused{},details{};
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
    // Whether some process waits for its page (cheap, any thread): its tree
    // was requested, or its bar did not yet show the track its title names.
    bool awaiting() const noexcept;
    // An event of `pid`'s page while it is awaited: reads it again (at most
    // four times per request or window title). true when it did.
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
