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
enum class NowPlayingTitleOrder : std::uint8_t { titleArtist, artistTitle };
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
};
std::span<const NowPlayingWindowPlayer> nowPlayingWindowPlayers() noexcept;
// The fallback player for a process image path or file name, else null.
const NowPlayingWindowPlayer* nowPlayingWindowPlayer(std::string_view executable) noexcept;

struct NowPlayingTitleTrack {
    std::string title, artist;
    bool operator==(const NowPlayingTitleTrack&) const = default;
};
// Source NowPlayingAccessibility.track(heading:): the last " - " (three whole
// Characters) splits title from artist, both trimmed of whitespace and
// newlines; a title is required. Unbounded here: NowPlayingTrack::bounded
// applies the source 512-Character bound.
std::optional<NowPlayingTitleTrack> nowPlayingHeadingTrack(std::string_view heading);
// A player's main window title: strips a trailing " - <player name>", treats
// the bare player name as idle (nullopt), then splits it like the source
// heading (artist-first players split at the first separator instead).
std::optional<NowPlayingTitleTrack> nowPlayingWindowTitleTrack(const NowPlayingWindowPlayer&, std::string_view windowTitle);

// One top-level window of a running fallback player process.
struct NowPlayingPlayerWindow {
    std::uintptr_t window{};  // native window handle value
    std::uint32_t pid{};
    std::string executable;   // full image path (UTF-8): the session identity
    std::string className, title;
    bool visible{}, owned{};  // owned: it has an owner window (popup/dialog)
    bool operator==(const NowPlayingPlayerWindow&) const = default;
};
// The window whose title carries the player's track among one process's
// top-level windows: its main window class when present, else its unowned
// windows; within those the first titled with a track (visible first), else
// the first visible titled one, else the first. nullopt when none qualifies.
std::optional<std::size_t> nowPlayingMainWindow(const NowPlayingWindowPlayer&, std::span<const NowPlayingPlayerWindow> processWindows);
} // namespace endfield::modules
