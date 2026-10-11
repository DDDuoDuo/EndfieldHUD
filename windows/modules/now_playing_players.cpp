#include "modules/now_playing_players.hpp"
#include "core/data/json.hpp"
#include "modules/archive_model.hpp"
#include "native/archive_text_rules.hpp"
#include <algorithm>
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
// NetEase Cloud Music's Win32 client: main window class OrpheusBrowserHost,
// titled "Title - Artist" while a track is loaded (the reported laptop
// diagnosis: it publishes no SMTC session). QQ Music and Kugou use the same
// mechanism with their usual title order; their grammar is unconfirmed.
constexpr std::array<NowPlayingWindowPlayer, 3> players{{
    {"netease", "cloudmusic.exe", NowPlayingSource::netease, {"OrpheusBrowserHost", ""}, {neteaseName, "NetEase Cloud Music", ""},
        NowPlayingTitleOrder::titleArtist, true},
    {"qqMusic", "QQMusic.exe", NowPlayingSource::qqMusic, {"QQMusic_Daemon_Wnd", ""}, {qqMusicName, "QQ Music", ""},
        NowPlayingTitleOrder::titleArtist, false},
    {"kugou", "KuGou.exe", NowPlayingSource::kugou, {"kugou_ui", ""}, {kugouName, "KuGou Music", "KuGou"},
        NowPlayingTitleOrder::artistTitle, false},
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
        if (caseless(name, player.executable)) return &player;
    return nullptr;
}

std::optional<NowPlayingTitleTrack> nowPlayingHeadingTrack(std::string_view heading) { return split(heading, true, false); }

std::optional<NowPlayingTitleTrack> nowPlayingWindowTitleTrack(const NowPlayingWindowPlayer& player, std::string_view title) {
    if (title.empty() || title.size() > maximumTitleBytes || !ehud::data::Json::validUtf8(title)) return std::nullopt;
    // The title itself is split exactly like the source heading; only a
    // trailing " - <player name>" is removed first.
    std::string body(title);
    const auto clean = trimmed(title);
    for (const auto name : player.names) {
        if (name.empty()) continue;
        const std::string suffix = " - " + std::string(name);
        if (clean.size() > suffix.size() && clean.compare(clean.size() - suffix.size(), suffix.size(), suffix) == 0) {
            body = clean.substr(0, clean.size() - suffix.size());
            break;
        }
    }
    if (const auto bare = trimmed(body); bare.empty() || named(player, bare)) return std::nullopt;
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
} // namespace endfield::modules
