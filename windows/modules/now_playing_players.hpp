#pragma once
// Window-title fallback rules for Win32 players that publish no SMTC session
// (Windows counterpart of the Mac NowPlayingAccessibility compatibility
// fallback, which reads the "Title - Artist" heading of NetEase Cloud Music,
// QQ Music and Kugou). Pure and portable: no window, process or OS call.
#include "modules/now_playing_lyrics_sources.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace endfield::modules {
// How a player writes the current track into its main window title.
//   titleArtist: "Title - Artist" (the last separator, as the source heading)
//   artistTitle: "Artist - Title" (the first separator)
//   titleOnly:   the whole remaining caption is the title (media players that
//                show a file or stream name); no artist.
enum class NowPlayingTitleOrder : std::uint8_t { titleArtist, artistTitle, titleOnly };
struct NowPlayingWindowPlayer {
    std::string_view key;        // stable report key
    std::string_view executable; // image file name, ASCII case-insensitive
    NowPlayingSource source;     // must equal nowPlayingSourceForAppUserModelID(executable)
    // Main window classes, most specific first ("" ends the list). When none
    // of the player's top-level windows has one, its unowned windows are used.
    std::array<std::string_view, 2> classes;
    // The player's own names: its idle window title and a " - Name" suffix.
    std::array<std::string_view, 3> names;
    NowPlayingTitleOrder order;
    // The title grammar was confirmed on a real install (live acceptance).
    bool confirmed;
    // Another image name of the same player (32/64-bit builds), "" for none.
    std::string_view alternate{};
    // Other trailing decorations removed before the split (e.g. " [foobar2000]").
    std::array<std::string_view, 2> suffixes{};
};
std::span<const NowPlayingWindowPlayer> nowPlayingWindowPlayers() noexcept;
// The fallback player for a process image path or file name, else null.
const NowPlayingWindowPlayer* nowPlayingWindowPlayer(std::string_view executable) noexcept;
// A GSMTC session identity (Win32 "Player.exe", optionally with a directory)
// names the same image as a process path (file names, ASCII case-insensitive).
// Packaged identities never match an image path.
bool nowPlayingSameExecutable(std::string_view appUserModelID, std::string_view executablePath) noexcept;

struct NowPlayingTitleTrack {
    std::string title, artist;
    bool operator==(const NowPlayingTitleTrack&) const = default;
};
// Source NowPlayingAccessibility.track(heading:): the last " - " (three whole
// Characters) splits title from artist, both trimmed of whitespace and
// newlines; a title is required. Unbounded here: NowPlayingTrack::bounded
// applies the source 512-Character bound.
std::optional<NowPlayingTitleTrack> nowPlayingHeadingTrack(std::string_view heading);
// A player's main window title: strips a trailing " - <player name>" (and the
// player's other suffixes), treats the bare player name as idle (nullopt),
// then splits it like the source heading (artist-first players split at the
// first separator instead; title-only players keep the whole caption).
std::optional<NowPlayingTitleTrack> nowPlayingWindowTitleTrack(const NowPlayingWindowPlayer&, std::string_view windowTitle);

// One top-level window of a running fallback player process.
struct NowPlayingPlayerWindow {
    std::uintptr_t window{};  // native window handle value
    std::uint32_t pid{};
    std::string executable;   // full image path (UTF-8): the session identity
    std::string className, title;
    bool visible{}, owned{};  // owned: it has an owner window (popup/dialog)
    // Host monotonic time at which this window's title was observed to change
    // to `title` (a WinEvent while presented, or a process-scoped one while
    // hidden); nullopt when it already showed this title when first seen, or
    // when it changed while nothing was observing it.
    std::optional<double> titleChangedAt;
    bool operator==(const NowPlayingPlayerWindow&) const = default;
};
// The window whose title carries the player's track among one process's
// top-level windows: its main window class when present, else its unowned
// windows; within those the first titled with a track (visible first), else
// the first visible titled one, else the first. nullopt when none qualifies.
std::optional<std::size_t> nowPlayingMainWindow(const NowPlayingWindowPlayer&, std::span<const NowPlayingPlayerWindow> processWindows);

// Transport of a window-title player's play/pause, previous and next.
//   mediaKey:   the matching media key (VK_MEDIA_PLAY_PAUSE / NEXT / PREV) is
//               injected while another program holds it as a registered
//               global hotkey (RegisterHotKey refuses it): the key then goes
//               to that program as WM_HOTKEY, never to the foreground window.
//               Used only while this player is the only fallback player
//               running, since the holder cannot be identified.
//   appCommand: WM_APPCOMMAND posted to the player's main window.
// NetEase Cloud Music 3.x ignores WM_APPCOMMAND (live RC 128) and registers
// its global shortcuts, the media keys included, with RegisterHotKey.
enum class NowPlayingWindowTransport : std::uint8_t { mediaKey, appCommand };
NowPlayingWindowTransport nowPlayingWindowTransport(bool mediaKeyHeld, bool onlyPlayer) noexcept;
std::string_view nowPlayingWindowTransportKey(NowPlayingWindowTransport) noexcept;

// Elapsed time of a window-title player's track, estimated from when its
// title changed to this track (the track's start as observed) and the time
// it spent paused since then. Window titles carry no position; the estimate
// exists only when the change was observed. Pure.
struct NowPlayingWindowTimeline {
    std::optional<double> startedAt;      // observed title change to this track
    double pausedTotal{};                 // closed pauses since then
    std::optional<double> pausedSince;    // the current pause, if paused
    // Starts a new track (all pause history is dropped).
    void restart(std::optional<double> changedAt) noexcept;
    // Folds the playing state observed at `time` into the pause history.
    void observe(bool playing, double time) noexcept;
    // Elapsed seconds at `time` (frozen while paused), when the start is known.
    std::optional<double> position(double time) const noexcept;
};
} // namespace endfield::modules
