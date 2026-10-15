#include "modules/now_playing_players.hpp"
#include "core/data/json.hpp"
#include "modules/archive_model.hpp"
#include "native/archive_text_rules.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <icu.h>
#else
#include <unicode/ubrk.h>
#endif

namespace endfield::modules {
namespace {
// UTF-8 literals for the player names (the source files are not assumed to be
// compiled as UTF-8).
constexpr std::string_view neteaseName = "\xe7\xbd\x91\xe6\x98\x93\xe4\xba\x91\xe9\x9f\xb3\xe4\xb9\x90"; // 网易云音乐
constexpr std::string_view qqMusicName = "QQ\xe9\x9f\xb3\xe4\xb9\x90";                                  // QQ音乐
constexpr std::string_view kugouName = "\xe9\x85\xb7\xe7\x8b\x97\xe9\x9f\xb3\xe4\xb9\x90";                // 酷狗音乐
constexpr std::string_view kuwoName = "\xe9\x85\xb7\xe6\x88\x91\xe9\x9f\xb3\xe4\xb9\x90";                 // 酷我音乐
// NetEase Cloud Music's Win32 client: main window class OrpheusBrowserHost,
// titled "Title - Artist" while a track is loaded (confirmed on the test
// laptop with 3.1.41, which publishes no SMTC session while its own Windows
// media control switch is off). QQ Music and Kugou use the same mechanism
// with their usual title order. The other rows are common Win32 music and
// media players whose window caption carries the playing item; their grammar
// is unconfirmed until a live run (now_playing_probe --live --windows) checks
// it. Players that publish a media session (Spotify, browsers, the Store
// apps, foobar2000 2.x with its Windows media controls on) come from GSMTC
// and are never listed twice.
constexpr std::array<NowPlayingWindowPlayer, 8> players{{
    {"netease", "cloudmusic.exe", NowPlayingSource::netease, {"OrpheusBrowserHost", ""}, {neteaseName, "NetEase Cloud Music", ""},
        NowPlayingTitleOrder::titleArtist, true},
    {"qqMusic", "QQMusic.exe", NowPlayingSource::qqMusic, {"QQMusic_Daemon_Wnd", ""}, {qqMusicName, "QQ Music", ""},
        NowPlayingTitleOrder::titleArtist, false},
    {"kugou", "KuGou.exe", NowPlayingSource::kugou, {"kugou_ui", ""}, {kugouName, "KuGou Music", "KuGou"},
        NowPlayingTitleOrder::artistTitle, false},
    {"kuwo", "KwMusic.exe", NowPlayingSource::system, {"", ""}, {kuwoName, "Kuwo Music", ""}, NowPlayingTitleOrder::titleArtist, false},
    {"foobar2000", "foobar2000.exe", NowPlayingSource::system, {"{97E27FAA-C0B3-4b8e-A693-ED7881E99FC1}", ""}, {"foobar2000", "", ""},
        NowPlayingTitleOrder::artistTitle, false, "", {" [foobar2000]", ""}},
    {"aimp", "AIMP.exe", NowPlayingSource::system, {"TAIMPMainForm", ""}, {"AIMP", "", ""}, NowPlayingTitleOrder::artistTitle, false},
    {"potPlayer", "PotPlayerMini64.exe", NowPlayingSource::system, {"PotPlayer64", "PotPlayer"}, {"PotPlayer", "", ""},
        NowPlayingTitleOrder::titleOnly, false, "PotPlayerMini.exe"},
    // VLC 3 (no media session) shows the item's title meta, or its file name.
    {"vlc", "vlc.exe", NowPlayingSource::system, {"", ""}, {"VLC media player", "", ""}, NowPlayingTitleOrder::titleOnly, false},
}};
constexpr std::size_t maximumTitleBytes = 64 * 1024;

char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
bool caseless(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); ++k)
        if (lower(a[k]) != lower(b[k])) return false;
    return true;
}
std::string_view fileName(std::string_view path) noexcept {
    const auto slash = path.find_last_of("\\/");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}
std::string trimmed(std::string_view text) {
    static const auto rules = native::nativeArchiveTextRules();
    return rules.trimmed(text);
}
// UTF-16 offsets of the Character (extended grapheme cluster) boundaries.
std::vector<std::int32_t> boundaries(const std::u16string& text) {
    std::vector<std::int32_t> out;
    out.reserve(text.size() + 1);
    out.push_back(0);
    if (text.empty()) return out;
    UErrorCode error = U_ZERO_ERROR;
    auto* iterator = ubrk_open(UBRK_CHARACTER, "", reinterpret_cast<const UChar*>(text.data()), static_cast<std::int32_t>(text.size()), &error);
    if (U_FAILURE(error) || !iterator) {
        if (iterator) ubrk_close(iterator);
        throw std::runtime_error("Cannot segment a player window title");
    }
    struct Close { UBreakIterator* value; ~Close() { ubrk_close(value); } } close{iterator};
    ubrk_first(iterator);
    for (auto next = ubrk_next(iterator); next != UBRK_DONE; next = ubrk_next(iterator)) out.push_back(next);
    return out;
}
// The first or last " - " made of three whole Characters, as UTF-16 [begin, end).
std::optional<std::pair<std::size_t, std::size_t>> separator(const std::u16string& text, bool last) {
    const auto b = boundaries(text);
    if (b.size() < 4) return std::nullopt;
    const auto is = [&](std::size_t k, char16_t c) { return b[k + 1] - b[k] == 1 && text[static_cast<std::size_t>(b[k])] == c; };
    const std::size_t clusters = b.size() - 1;
    for (std::size_t n = 0; n + 3 <= clusters; ++n) {
        const std::size_t k = last ? clusters - 3 - n : n;
        if (is(k, u' ') && is(k + 1, u'-') && is(k + 2, u' '))
            return std::pair{static_cast<std::size_t>(b[k]), static_cast<std::size_t>(b[k + 3])};
    }
    return std::nullopt;
}
std::optional<NowPlayingTitleTrack> split(std::string_view heading, bool last, bool artistFirst) {
    if (heading.empty() || heading.size() > maximumTitleBytes || !ehud::data::Json::validUtf8(heading)) return std::nullopt;
    const auto text = archiveUTF16(heading);
    const auto found = separator(text, last);
    if (!found) return std::nullopt;
    auto before = trimmed(archiveUTF8(std::u16string_view(text).substr(0, found->first)));
    auto after = trimmed(archiveUTF8(std::u16string_view(text).substr(found->second)));
    NowPlayingTitleTrack track{artistFirst ? std::move(after) : std::move(before), artistFirst ? std::move(before) : std::move(after)};
    if (track.title.empty()) return std::nullopt;
    return track;
}
bool named(const NowPlayingWindowPlayer& player, std::string_view value) noexcept {
    return !value.empty() && std::any_of(player.names.begin(), player.names.end(), [&](std::string_view name) { return !name.empty() && name == value; });
}
} // namespace

std::span<const NowPlayingWindowPlayer> nowPlayingWindowPlayers() noexcept { return players; }
const NowPlayingWindowPlayer* nowPlayingWindowPlayer(std::string_view executable) noexcept {
    const auto name = fileName(executable);
    for (const auto& player : players)
        if (caseless(name, player.executable) || (!player.alternate.empty() && caseless(name, player.alternate))) return &player;
    return nullptr;
}
bool nowPlayingSameExecutable(std::string_view appUserModelID, std::string_view executablePath) noexcept {
    const auto a = fileName(appUserModelID), b = fileName(executablePath);
    constexpr std::string_view exe = ".exe";
    return a.size() > exe.size() && caseless(a.substr(a.size() - exe.size()), exe) && caseless(a, b);
}

std::optional<NowPlayingTitleTrack> nowPlayingHeadingTrack(std::string_view heading) { return split(heading, true, false); }

std::optional<NowPlayingTitleTrack> nowPlayingWindowTitleTrack(const NowPlayingWindowPlayer& player, std::string_view title) {
    if (title.empty() || title.size() > maximumTitleBytes || !ehud::data::Json::validUtf8(title)) return std::nullopt;
    // The title itself is split exactly like the source heading; only a
    // trailing " - <player name>" (or another player decoration) is removed first.
    std::string body(title);
    const auto clean = trimmed(title);
    const auto strip = [&](std::string_view suffix) {
        if (clean.size() > suffix.size() && clean.compare(clean.size() - suffix.size(), suffix.size(), suffix) == 0) {
            body = clean.substr(0, clean.size() - suffix.size());
            return true;
        }
        return false;
    };
    bool stripped = false;
    for (const auto name : player.names) {
        if (name.empty()) continue;
        if ((stripped = strip(" - " + std::string(name)))) break;
    }
    for (const auto suffix : player.suffixes)
        if (!stripped && !suffix.empty()) stripped = strip(suffix);
    const auto bare = trimmed(body);
    if (bare.empty() || named(player, bare)) return std::nullopt;
    if (player.order == NowPlayingTitleOrder::titleOnly) return NowPlayingTitleTrack{bare, {}};
    const bool artistFirst = player.order == NowPlayingTitleOrder::artistTitle;
    auto track = split(body, !artistFirst, artistFirst);
    // "QQ音乐 - 听我想听" and similar app captions are not tracks.
    if (track && (named(player, track->title) || named(player, track->artist))) return std::nullopt;
    return track;
}

std::optional<std::size_t> nowPlayingMainWindow(const NowPlayingWindowPlayer& player, std::span<const NowPlayingPlayerWindow> windows) {
    std::vector<std::size_t> candidates;
    for (const auto preferred : player.classes) {
        if (preferred.empty()) break;
        for (std::size_t k = 0; k < windows.size(); ++k)
            if (caseless(windows[k].className, preferred)) candidates.push_back(k);
    }
    if (candidates.empty())
        for (std::size_t k = 0; k < windows.size(); ++k)
            if (!windows[k].owned) candidates.push_back(k);
    if (candidates.empty()) return std::nullopt;
    const auto track = [&](std::size_t k) { return nowPlayingWindowTitleTrack(player, windows[k].title).has_value(); };
    for (const auto k : candidates)
        if (windows[k].visible && track(k)) return k;
    for (const auto k : candidates)
        if (track(k)) return k;
    for (const auto k : candidates)
        if (windows[k].visible && !windows[k].title.empty()) return k;
    return candidates.front();
}

NowPlayingWindowTransport nowPlayingWindowTransport(bool mediaKeyHeld, bool onlyPlayer) noexcept {
    return mediaKeyHeld && onlyPlayer ? NowPlayingWindowTransport::mediaKey : NowPlayingWindowTransport::appCommand;
}
std::string_view nowPlayingWindowTransportKey(NowPlayingWindowTransport value) noexcept {
    return value == NowPlayingWindowTransport::mediaKey ? "mediaKey" : "appCommand";
}

void NowPlayingWindowTimeline::restart(std::optional<double> changedAt) noexcept {
    startedAt = changedAt && std::isfinite(*changedAt) ? changedAt : std::nullopt;
    pausedTotal = 0;
    pausedSince.reset();
}
void NowPlayingWindowTimeline::observe(bool playing, double time) noexcept {
    if (!std::isfinite(time)) return;
    if (!playing) {
        if (!pausedSince) pausedSince = startedAt ? std::max(time, *startedAt) : time;
    } else if (pausedSince) {
        pausedTotal += std::max(0., time - *pausedSince);
        pausedSince.reset();
    }
}
std::optional<double> NowPlayingWindowTimeline::position(double time) const noexcept {
    if (!startedAt || !std::isfinite(time)) return std::nullopt;
    const double end = pausedSince ? std::min(*pausedSince, time) : time;
    return std::max(0., end - *startedAt - pausedTotal);
}
} // namespace endfield::modules
