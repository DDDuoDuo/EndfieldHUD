#include "tools/now_playing_acceptance.hpp"
#include "app/utility_executor.hpp"
#include "native/now_playing_fixture.hpp"
#include "tools/now_playing_session.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <tuple>

namespace endfield::tools {
namespace {
namespace m = endfield::modules;
namespace n = endfield::native;
namespace fs = std::filesystem;
using Json = ehud::data::Json;
using V = NowPlayingVerdict;
using C = NowPlayingCheck;

constexpr std::array<std::string_view, nowPlayingCheckCount> checkKeys{
    "session", "current", "source", "metadata", "timeline", "progress", "playback", "trackChange",
    "controls", "seek", "artwork", "lyrics", "volume", "command", "service"};
constexpr std::array<std::string_view, 4> verdictKeys{"unverified", "pass", "gap", "fail"};
constexpr std::array<std::string_view, 4> rowVerdictKeys{"unverified", "partial", "pass", "fail"};
constexpr std::size_t maximumReportBytes = 4 * 1024 * 1024;
constexpr std::size_t maximumText = 4096;

// The live acceptance players. Identity hints: see NowPlayingKnownPlayer.
constexpr std::array<NowPlayingKnownPlayer, 11> knownPlayers{{
    {"spotifyDesktop", "Spotify (desktop)", m::NowPlayingSource::spotify, {"Spotify.exe", ""}, true},
    {"spotifyStore", "Spotify (Microsoft Store)", m::NowPlayingSource::spotify, {"SpotifyAB.SpotifyMusic_", ""}, true},
    {"neteaseDesktop", "NetEase Cloud Music (desktop)", m::NowPlayingSource::netease, {"cloudmusic.exe", ""}, true},
    {"neteaseStore", "NetEase Cloud Music (Microsoft Store)", m::NowPlayingSource::netease, {"1F8B0F94.122165AE053F_", ""}, true},
    {"qqMusic", "QQ Music", m::NowPlayingSource::qqMusic, {"QQMusic.exe", ""}, true},
    {"kugou", "Kugou Music", m::NowPlayingSource::kugou, {"KuGou.exe", ""}, true},
    {"appleMusic", "Apple Music", m::NowPlayingSource::music, {"AppleInc.AppleMusicWin_", ""}, true},
    {"mediaPlayer", "Windows Media Player", m::NowPlayingSource::system, {"Microsoft.ZuneMusic_", ""}, true},
    {"edge", "Microsoft Edge", m::NowPlayingSource::system, {"MSEdge", "msedge.exe"}, false},
    {"chrome", "Google Chrome", m::NowPlayingSource::system, {"Chrome", "chrome.exe"}, false},
    // 308046B0AF4A39CB: the AppUserModelID of a default-location Firefox install.
    {"firefox", "Mozilla Firefox", m::NowPlayingSource::system, {"firefox.exe", "308046B0AF4A39CB"}, false},
}};

char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
bool caseless(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); ++k)
        if (lower(a[k]) != lower(b[k])) return false;
    return true;
}
bool caselessPrefix(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && caseless(value.substr(0, prefix.size()), prefix);
}
std::string_view fileName(std::string_view path) noexcept {
    const auto slash = path.find_last_of("\\/");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}
bool hintMatches(std::string_view hint, std::string_view id) noexcept {
    if (hint.empty() || id.empty()) return false;
    if (hint.size() > 4 && caseless(hint.substr(hint.size() - 4), ".exe"))
        return m::nowPlayingWin32Identity(id) && caseless(fileName(id), hint);
    if (hint.back() == '_') return caselessPrefix(id, hint);
    return caseless(id, hint);
}
std::string hintText(std::string_view hint) { return std::string(hint) + (hint.empty() || hint.back() != '_' ? "" : "*"); }
std::string fixed(double value, int digits) {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.*f", digits, value);
    return buffer;
}
std::string percent(double value) { return std::to_string(std::lround(std::clamp(value, 0., 1.) * 100)) + "%"; }
std::string shortest(double value) {
    char buffer[48];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string("?");
}
std::string join(const std::vector<std::string>& values, std::string_view separator) {
    std::string out;
    for (const auto& value : values) {
        if (!out.empty()) out += separator;
        out += value;
    }
    return out;
}
template <class T>
std::optional<T> fromKey(std::span<const std::string_view> keys, std::string_view key) {
    for (std::size_t k = 0; k < keys.size(); ++k)
        if (keys[k] == key) return static_cast<T>(k);
    return std::nullopt;
}
// Media-session failures that mean something went wrong (stale and cancelled
// are ordinary races with the player).
bool serious(n::NowPlayingServiceFailure failure) noexcept {
    return failure != n::NowPlayingServiceFailure::none && failure != n::NowPlayingServiceFailure::stale &&
           failure != n::NowPlayingServiceFailure::cancelled;
}
m::NowPlayingIntentKind intentKind(m::NowPlayingCommandKind kind) noexcept {
    switch (kind) {
    case m::NowPlayingCommandKind::playPause: return m::NowPlayingIntentKind::playPause;
    case m::NowPlayingCommandKind::previous: return m::NowPlayingIntentKind::previous;
    case m::NowPlayingCommandKind::next: return m::NowPlayingIntentKind::next;
    case m::NowPlayingCommandKind::seek: return m::NowPlayingIntentKind::seek;
    }
    return m::NowPlayingIntentKind::playPause;
}
Json optionalNumber(std::optional<double> value) { return value && std::isfinite(*value) ? Json(*value) : Json(); }
// The service snapshot as a JSON event line (stdout only; it carries the
// track metadata so the user can compare it with the player).
Json::Object describe(const n::NowPlayingServiceSnapshot& s) {
    Json::Object out{{"event", "snapshot"}, {"fresh", s.fresh}, {"busy", s.busy},
        {"failure", std::string(nowPlayingServiceFailureKey(s.failure))},
        {"nativeError", static_cast<std::int64_t>(s.nativeError)},
        {"sessions", static_cast<std::int64_t>(s.media.sessions.size())}};
    Json::Array ids;
    for (const auto& session : s.media.sessions) ids.emplace_back(session.appUserModelID);
    out["sessionIDs"] = std::move(ids);
    if (s.media.session) {
        out["selected"] = s.media.session->appUserModelID;
        out["origin"] = s.media.session->origin == m::NowPlayingSessionOrigin::playerWindow ? "playerWindow" : "mediaSession";
        out["source"] = std::string(m::nowPlayingSourceKey(m::nowPlayingSourceForAppUserModelID(s.media.session->appUserModelID)));
    }
    if (const auto& t = s.media.track)
        out["track"] = Json::Object{{"title", t->title}, {"artist", t->artist}, {"album", t->album},
            {"duration", optionalNumber(t->duration)}, {"position", optionalNumber(t->position)}, {"playing", t->isPlaying},
            {"supportsSeeking", t->supportsSeeking}, {"timedLyrics", t->timedLyrics.has_value()}};
    const auto& c = s.media.capabilities;
    out["capabilities"] = Json::Object{{"play", c.play}, {"pause", c.pause}, {"toggle", c.toggle},
        {"previous", c.previous}, {"next", c.next}, {"seek", c.seek}};
    if (s.media.timeline && std::isfinite(s.media.timeline->rate)) out["playbackRate"] = s.media.timeline->rate;
    out["thumbnailBytes"] = static_cast<std::int64_t>(s.media.artwork ? s.media.artwork->size() : 0);
    return out;
}
std::string escapeCell(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '|') out += "\\|";
        else if (c == '\n' || c == '\r') out += ' ';
        else out += c;
    }
    return out;
}
std::size_t knownIndex(const NowPlayingAcceptanceRow& row) noexcept {
    for (std::size_t k = 0; k < knownPlayers.size(); ++k)
        if (row.player == knownPlayers[k].key) return k;
    return knownPlayers.size();
}
bool sameRow(const NowPlayingAcceptanceRow& a, const NowPlayingAcceptanceRow& b) noexcept {
    if (a.player != b.player) return false;
    return a.player != "other" || caseless(a.application, b.application);
}
bool boundedText(const Json& value) { return value.isString() && value.string().size() <= maximumText && Json::validUtf8(value.string()); }
} // namespace

std::string_view nowPlayingCheckKey(NowPlayingCheck c) noexcept {
    const auto index = static_cast<std::size_t>(c);
    return index < checkKeys.size() ? checkKeys[index] : std::string_view("unknown");
}
std::string_view nowPlayingVerdictKey(NowPlayingVerdict v) noexcept {
    const auto index = static_cast<std::size_t>(v);
    return index < verdictKeys.size() ? verdictKeys[index] : std::string_view("unverified");
}
std::string_view nowPlayingRowVerdictKey(NowPlayingRowVerdict v) noexcept {
    const auto index = static_cast<std::size_t>(v);
    return index < rowVerdictKeys.size() ? rowVerdictKeys[index] : std::string_view("unverified");
}
std::string_view nowPlayingServiceFailureKey(n::NowPlayingServiceFailure f) noexcept {
    switch (f) {
    case n::NowPlayingServiceFailure::none: return "none";
    case n::NowPlayingServiceFailure::unavailable: return "unavailable";
    case n::NowPlayingServiceFailure::accessDenied: return "accessDenied";
    case n::NowPlayingServiceFailure::unsupported: return "unsupported";
    case n::NowPlayingServiceFailure::cancelled: return "cancelled";
    case n::NowPlayingServiceFailure::stale: return "stale";
    case n::NowPlayingServiceFailure::capacity: return "capacity";
    case n::NowPlayingServiceFailure::timedOut: return "timedOut";
    }
    return "unknown";
}

std::span<const NowPlayingKnownPlayer> nowPlayingKnownPlayers() noexcept { return knownPlayers; }
std::optional<std::size_t> nowPlayingKnownPlayer(std::string_view id) noexcept {
    for (std::size_t k = 0; k < knownPlayers.size(); ++k)
        for (const auto hint : knownPlayers[k].identities)
            if (hintMatches(hint, id)) return k;
    return std::nullopt;
}
std::string nowPlayingReportIdentity(std::string_view id) {
    auto name = std::string(fileName(id));
    if (name.size() > 256) name.resize(256);
    while (!name.empty() && !Json::validUtf8(name)) name.pop_back(); // never split a UTF-8 sequence
    return name;
}

NowPlayingRowVerdict NowPlayingAcceptanceRow::verdict() const noexcept {
    bool unverified{};
    for (const auto& check : checks) {
        if (check.verdict == V::fail) return NowPlayingRowVerdict::fail;
        unverified = unverified || check.verdict == V::unverified;
    }
    if ((*this)[C::session].verdict != V::pass || (*this)[C::current].verdict != V::pass) return NowPlayingRowVerdict::unverified;
    return unverified ? NowPlayingRowVerdict::partial : NowPlayingRowVerdict::pass;
}
const NowPlayingAcceptanceRow* NowPlayingAcceptanceMatrix::find(std::string_view player) const noexcept {
    for (const auto& row : rows)
        if (row.player == player || (row.player == "other" && caseless(row.application, player))) return &row;
    return nullptr;
}

Json nowPlayingMatrixJson(const NowPlayingAcceptanceMatrix& matrix) {
    Json::Array runs;
    for (const auto& r : matrix.runs)
        runs.emplace_back(Json::Object{{"run", static_cast<std::int64_t>(r.run)}, {"mode", r.mode}, {"recorded", r.recorded},
            {"seconds", std::isfinite(r.seconds) ? r.seconds : 0.}, {"lyrics", r.lyrics}, {"volume", r.volume},
            {"command", r.command}, {"service", r.service}});
    Json::Array rows;
    for (const auto& row : matrix.rows) {
        Json::Object checks;
        for (std::size_t k = 0; k < nowPlayingCheckCount; ++k)
            checks[std::string(checkKeys[k])] = Json::Object{{"verdict", std::string(nowPlayingVerdictKey(row.checks[k].verdict))},
                {"detail", row.checks[k].detail}, {"run", static_cast<std::int64_t>(row.checks[k].run)}};
        rows.emplace_back(Json::Object{{"player", row.player}, {"label", row.label}, {"application", row.application},
            {"source", row.source}, {"verdict", std::string(nowPlayingRowVerdictKey(row.verdict()))}, {"checks", std::move(checks)}});
    }
    return Json::Object{{"format", std::string(nowPlayingReportFormat)}, {"version", 1}, {"runs", std::move(runs)}, {"rows", std::move(rows)}};
}

std::optional<NowPlayingAcceptanceMatrix> nowPlayingMatrixFromJson(const Json& root) try {
    if (!root.isObject() || !root["format"].isString() || root["format"].string() != nowPlayingReportFormat) return std::nullopt;
    if (!root["version"].isNumber() || root["version"].integer() != 1) return std::nullopt;
    const auto& runs = root["runs"];
    const auto& rows = root["rows"];
    if (!runs.isArray() || !rows.isArray() || runs.array().size() > nowPlayingMaximumReportRuns ||
        rows.array().size() > knownPlayers.size() + nowPlayingMaximumOtherPlayers)
        return std::nullopt;
    NowPlayingAcceptanceMatrix out;
    std::uint64_t previous{};
    for (const auto& value : runs.array()) {
        if (!value.isObject() || !value["run"].isNumber() || !value["seconds"].isNumber() || !value["lyrics"].isBool() ||
            !value["volume"].isBool())
            return std::nullopt;
        for (const auto key : {"mode", "recorded", "command", "service"})
            if (!boundedText(value[key])) return std::nullopt;
        const auto run = value["run"].integer();
        const auto seconds = value["seconds"].number();
        if (run <= 0 || static_cast<std::uint64_t>(run) <= previous || !std::isfinite(seconds) || seconds < 0) return std::nullopt;
        previous = static_cast<std::uint64_t>(run);
        out.runs.push_back({previous, value["mode"].string(), value["recorded"].string(), seconds, value["lyrics"].boolean(),
            value["volume"].boolean(), value["command"].string(), value["service"].string()});
    }
    for (const auto& value : rows.array()) {
        if (!value.isObject() || !value["checks"].isObject()) return std::nullopt;
        for (const auto key : {"player", "label", "application", "source"})
            if (!boundedText(value[key])) return std::nullopt;
        NowPlayingAcceptanceRow row{value["player"].string(), value["label"].string(), value["application"].string(), value["source"].string(), {}};
        if (row.player.empty() || (row.player != "other" && knownIndex(row) == knownPlayers.size())) return std::nullopt;
        for (std::size_t k = 0; k < nowPlayingCheckCount; ++k) {
            const auto& check = value["checks"][checkKeys[k]];
            if (!check.isObject() || !check["verdict"].isString() || !boundedText(check["detail"]) || !check["run"].isNumber()) return std::nullopt;
            const auto verdict = fromKey<V>(verdictKeys, check["verdict"].string());
            const auto run = check["run"].integer();
            if (!verdict || run < 0 || static_cast<std::uint64_t>(run) > previous) return std::nullopt;
            row.checks[k] = {*verdict, check["detail"].string(), static_cast<std::uint64_t>(run)};
        }
        for (const auto& prior : out.rows)
            if (sameRow(prior, row)) return std::nullopt;
        out.rows.push_back(std::move(row));
    }
    return out;
} catch (...) {
    return std::nullopt;
}

NowPlayingAcceptanceMatrix nowPlayingMergeMatrix(NowPlayingAcceptanceMatrix stored, const NowPlayingAcceptanceMatrix& run) {
    const std::uint64_t base = stored.runs.empty() ? 0 : stored.runs.back().run;
    const auto renumber = [base](std::uint64_t value) { return value ? base + value : 0; };
    for (auto next : run.runs) {
        next.run = renumber(next.run);
        stored.runs.push_back(std::move(next));
    }
    if (stored.runs.size() > nowPlayingMaximumReportRuns)
        stored.runs.erase(stored.runs.begin(), stored.runs.begin() + static_cast<std::ptrdiff_t>(stored.runs.size() - nowPlayingMaximumReportRuns));
    for (const auto& row : run.rows) {
        const auto found = std::find_if(stored.rows.begin(), stored.rows.end(), [&](const auto& r) { return sameRow(r, row); });
        if (found == stored.rows.end()) {
            auto copy = row;
            for (auto& check : copy.checks) check.run = renumber(check.run);
            stored.rows.push_back(std::move(copy));
            continue;
        }
        auto& target = *found;
        target.label = row.label;
        // An unseen player keeps the identity and source it was last seen with.
        if (row[C::session].verdict != V::unverified) {
            target.application = row.application;
            target.source = row.source;
        }
        for (std::size_t k = 0; k < nowPlayingCheckCount; ++k) {
            const auto& next = row.checks[k];
            auto& kept = target.checks[k];
            if (next.verdict != V::unverified) kept = {next.verdict, next.detail, renumber(next.run)};
            else if (kept.verdict == V::unverified) kept.detail = next.detail; // the newest hint
        }
    }
    // Known players in catalog order, then the others in first-seen order;
    // only the most recently confirmed other players are kept.
    std::stable_sort(stored.rows.begin(), stored.rows.end(), [](const auto& a, const auto& b) { return knownIndex(a) < knownIndex(b); });
    const auto others = static_cast<std::size_t>(std::count_if(stored.rows.begin(), stored.rows.end(), [](const auto& r) { return r.player == "other"; }));
    if (others > nowPlayingMaximumOtherPlayers) {
        std::vector<std::pair<std::uint64_t, std::size_t>> ages;
        for (std::size_t k = 0; k < stored.rows.size(); ++k)
            if (stored.rows[k].player == "other") {
                std::uint64_t latest{};
                for (const auto& check : stored.rows[k].checks) latest = std::max(latest, check.run);
                ages.emplace_back(latest, k);
            }
        std::stable_sort(ages.begin(), ages.end());
        std::vector<bool> drop(stored.rows.size());
        for (std::size_t k = 0; k < others - nowPlayingMaximumOtherPlayers; ++k) drop[ages[k].second] = true;
        std::vector<NowPlayingAcceptanceRow> kept;
        for (std::size_t k = 0; k < stored.rows.size(); ++k)
            if (!drop[k]) kept.push_back(std::move(stored.rows[k]));
        stored.rows = std::move(kept);
    }
    return stored;
}

std::string nowPlayingMatrixMarkdown(const NowPlayingAcceptanceMatrix& matrix) {
    std::string out = "# Now Playing acceptance matrix\n\n";
    out += "Recorded by `now_playing_probe` through the same Now Playing owner EndfieldHUD.exe runs (GSMTC sessions and the window titles "
           "of players that publish none, cover decode, lyric sources, per-app volume route and transport). ";
    out += std::to_string(matrix.runs.size()) + " run(s)";
    if (!matrix.runs.empty()) {
        const auto& last = matrix.runs.back();
        out += "; the last: run " + std::to_string(last.run) + ", " + last.mode + ", " + shortest(last.seconds) + " s";
        std::vector<std::string> options;
        if (last.lyrics) options.emplace_back("--lyrics");
        if (last.volume) options.emplace_back("--volume");
        if (!last.command.empty()) options.push_back("--command " + last.command);
        out += options.empty() ? ", no options" : ", " + join(options, " ");
        if (!last.recorded.empty()) out += ", " + last.recorded;
    }
    out += ".\nThe report stores no title, artist, album, lyrics, URL or directory.\n\n";
    out += "| Player | Identity | Source | Verdict |";
    for (const auto key : checkKeys) out += " " + std::string(key) + " |";
    out += "\n| --- | --- | --- | --- |";
    for (std::size_t k = 0; k < checkKeys.size(); ++k) out += " --- |";
    out += '\n';
    for (const auto& row : matrix.rows) {
        out += "| " + escapeCell(row.label) + " | " + escapeCell(row.application) + " | " + escapeCell(row.source) + " | " +
               std::string(nowPlayingRowVerdictKey(row.verdict())) + " |";
        for (const auto& check : row.checks) out += " " + std::string(nowPlayingVerdictKey(check.verdict)) + " |";
        out += '\n';
    }
    out += "\n## Details\n";
    for (const auto& row : matrix.rows) {
        out += "\n### " + row.label + ": " + std::string(nowPlayingRowVerdictKey(row.verdict())) + "\n\n";
        for (std::size_t k = 0; k < nowPlayingCheckCount; ++k) {
            const auto& check = row.checks[k];
            if (check.detail.empty()) continue;
            out += "- " + std::string(checkKeys[k]) + ": " + std::string(nowPlayingVerdictKey(check.verdict));
            if (check.run) out += " (run " + std::to_string(check.run) + ")";
            out += ". " + check.detail + "\n";
        }
    }
    out += "\n## Verdicts\n\n"
           "- pass: observed working.\n"
           "- gap: a player or recorded platform limitation; the HUD shows the source fallback.\n"
           "- fail: observed wrong; investigate.\n"
           "- unverified: not exercised in any run yet; the detail says what to do.\n\n"
           "A row is pass when every check is pass or gap, partial while something is unverified, fail when any check failed, "
           "and unverified until the player was presented. A later run replaces a check only with pass, gap or fail.\n"
           "The probe shows what the Now Playing owner computed; also look at the HUD itself (cover, progress, lyrics, transitions).\n";
    out += "\n## Runs\n\n| Run | Mode | Seconds | Options | Recorded | Media-session failures |\n| --- | --- | --- | --- | --- | --- |\n";
    for (const auto& r : matrix.runs) {
        std::vector<std::string> options;
        if (r.lyrics) options.emplace_back("lyrics");
        if (r.volume) options.emplace_back("volume");
        if (!r.command.empty()) options.push_back("command " + r.command);
        out += "| " + std::to_string(r.run) + " | " + escapeCell(r.mode) + " | " + shortest(r.seconds) + " | " +
               escapeCell(options.empty() ? std::string("none") : join(options, ", ")) + " | " +
               escapeCell(r.recorded.empty() ? std::string("-") : r.recorded) + " | " +
               escapeCell(r.service.empty() ? std::string("none") : r.service) + " |\n";
    }
    return out;
}

std::optional<NowPlayingAcceptanceMatrix> nowPlayingReadMatrix(const fs::path& path) {
    std::error_code error;
    const auto status = fs::status(path, error);
    if (status.type() == fs::file_type::not_found) return std::nullopt;
    if (error) throw std::runtime_error("The report path cannot be inspected");
    if (!fs::is_regular_file(status)) throw std::runtime_error("The report path is not a regular file");
    const auto size = fs::file_size(path, error);
    if (error || size > maximumReportBytes) throw std::runtime_error("The report file is unreadable or too large");
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw std::runtime_error("The report file is unreadable");
    std::optional<NowPlayingAcceptanceMatrix> matrix;
    try {
        matrix = nowPlayingMatrixFromJson(Json::parse(text, maximumReportBytes));
    } catch (...) {
    }
    if (!matrix) throw std::runtime_error("The file is not a Now Playing acceptance report of this version; it is left unchanged");
    return matrix;
}
void nowPlayingWriteReport(const fs::path& path, std::string_view text) {
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            throw std::runtime_error("The report could not be written");
        }
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary, error);
        throw std::runtime_error("The report could not be replaced");
    }
}

std::optional<m::NowPlayingCommand> nowPlayingProbeCommand(std::string_view value) {
    if (value == "playPause") return m::NowPlayingCommand{m::NowPlayingCommandKind::playPause};
    if (value == "previous") return m::NowPlayingCommand{m::NowPlayingCommandKind::previous};
    if (value == "next") return m::NowPlayingCommand{m::NowPlayingCommandKind::next};
    if (value.starts_with("seek=") && value.size() > 5 && value.size() < 64) {
        // Plain decimal seconds only (strtod would also take hex, inf or nan).
        const std::string text(value.substr(5));
        if (text.find_first_not_of("0123456789.") != std::string::npos || text.front() == '.') return std::nullopt;
        char* end{};
        const double seconds = std::strtod(text.c_str(), &end);
        if (end == text.c_str() + text.size() && std::isfinite(seconds) && seconds >= 0 && seconds <= m::nowPlayingMaximumSeconds)
            return m::NowPlayingCommand{m::NowPlayingCommandKind::seek, seconds};
    }
    return std::nullopt;
}
std::string nowPlayingProbeCommandKey(const m::NowPlayingCommand& command) {
    switch (command.kind) {
    case m::NowPlayingCommandKind::playPause: return "playPause";
    case m::NowPlayingCommandKind::previous: return "previous";
    case m::NowPlayingCommandKind::next: return "next";
    case m::NowPlayingCommandKind::seek: return "seek=" + shortest(command.seconds);
    }
    return "playPause";
}

// ---------------------------------------------------------------------------
// The probe: one NowPlayingSession activation observed per player.
// ---------------------------------------------------------------------------
struct NowPlayingProbe::Impl {
    enum Lookup : std::size_t { catalog, lrclib, covers, lookupKinds };
    struct Counts { std::uint64_t started{}, answered{}; };
    // Download outcomes per player. The transport runs on the owner thread;
    // its completions may arrive on any thread.
    struct Tally {
        std::mutex mutex;
        std::map<std::string, std::array<Counts, lookupKinds>, std::less<>> players;
        std::vector<std::string> covers; // catalog cover URLs answered with bytes (newest last, bounded)
    };
    struct Sample {
        m::NowPlayingTrack track;
        double position{}, sampledAt{}, rate{1};
        // The GSMTC timeline update this sample was extrapolated from: its
        // position and the monotonic time it describes (LastUpdatedTime).
        std::int64_t ticks{};
        double base{};
        bool playing{};
        std::uint64_t commands{};
    };
    struct Observed {
        std::string application; // full GSMTC identity (never stored in a report)
        bool listed{}, presented{};
        bool window{}; // found by its main window title (no media session)
        std::optional<std::size_t> commandTracks; // tracks seen when its command completed
        bool title{}, artist{}, album{}, duration{}, position{};
        std::optional<double> length;
        std::optional<Sample> sample;
        std::size_t pairs{};
        double drift{};
        double quiet{}; // longest playback without a new timeline update (seconds)
        bool playing{}, paused{};
        std::optional<m::NowPlayingTrack> track;
        std::size_t tracks{};
        m::NowPlayingCapabilities controls;
        bool seekable{}, thumbnail{};
        std::optional<std::array<unsigned, 2>> playerCover, catalogCover;
        std::size_t decodeFailures{}, catalogFailures{}, lyricLines{};
        bool embedded{};
        bool volumeAvailable{}, silentWhilePlaying{};
        std::size_t matches{};
        std::optional<double> level;
        std::uint32_t pid{};
        std::vector<std::string> failures;
    };

    NowPlayingProbeOptions options;
    NowPlayingProbeServices services;
    std::shared_ptr<Tally> tally = std::make_shared<Tally>();
    app::UtilityExecutor utility;
    n::NativeNowPlayingService service;
    std::shared_ptr<n::NowPlayingWeb> web;
    std::unique_ptr<NowPlayingSession> session;
    std::vector<Observed> observed;
    std::vector<std::string> globalFailures;
    std::uint64_t serviceRevision{~std::uint64_t{}};
    std::string coverApplication, lyricsApplication;
    std::string followed; // the player whose timeline samples are being paired
    const void* coverShown{};
    const void* lyricsShown{};
    const void* failedArtwork{};
    std::vector<std::string> failedCovers; // catalog cover URLs already judged undecodable (bounded)
    std::optional<std::tuple<std::string, bool, std::size_t, std::optional<double>>> volumeShown;
    bool commandDone{};
    std::string commandApplication;
    std::uint64_t commandToken{}, commands{};
    std::optional<NowPlayingCheckResult> commandResult;
    std::string commandLog; // "Event Log playbackAction action=..., source=..." of a completed command
    std::optional<m::NowPlayingPlaybackEvent> playback;
    bool active{}, stopped{};

    static std::function<void()> wakeOf(const NowPlayingProbeServices& s) {
        if (!s.wake || !s.clock) throw std::invalid_argument("The Now Playing probe needs a clock and an owner wake");
        return s.wake;
    }
    Impl(NowPlayingProbeOptions o, NowPlayingProbeServices s)
        : options(std::move(o)), services(std::move(s)), utility(wakeOf(services)),
          service(utility, services.provider, services.wake, services.clock) {
        if (options.lyrics && services.transport)
            web = std::make_shared<n::NowPlayingWeb>(tallied(services.transport), services.wake, services.clock);
        NowPlayingSessionOptions so;
        so.decode = services.decode;
        so.web = web;
        if (options.volume) so.volume = services.volume;
        // Successful transport commands only, exactly what the Event Log records.
        so.playback = [this](const m::NowPlayingPlaybackEvent& event) {
            playback = event;
            emit({{"event", "playbackAction"}, {"action", std::string(event.action)},
                {"source", std::string(m::nowPlayingSourceKey(event.source))}});
        };
        session = std::make_unique<NowPlayingSession>(service, utility, m::NowPlayingAppearance{}, [] {}, std::move(so));
    }
    ~Impl() {
        try {
            if (active) session->setActive(false, services.clock());
        } catch (...) {
        }
        session.reset();
        web.reset();
    }
    n::NowPlayingWeb::Transport tallied(n::NowPlayingWeb::Transport inner) {
        return [this, inner = std::move(inner), counts = tally](const n::NowPlayingWeb::Request& request,
                   n::NowPlayingWeb::Done done) -> std::shared_ptr<n::NowPlayingWeb::Task> {
            // Requests start inside the session's publish, after the service
            // drain: the published session is the player they are for.
            const auto& s = service.snapshot();
            std::string player = s.media.session ? s.media.session->appUserModelID : std::string();
            const Lookup kind = request.kind == n::NowPlayingWeb::Kind::artwork ? covers
                                : caselessPrefix(request.url, "https://music.163.com/") ? catalog : lrclib;
            {
                std::lock_guard lock(counts->mutex);
                ++counts->players[player][kind].started;
            }
            return inner(request, [counts, player, kind, url = kind == covers ? request.url : std::string(), done = std::move(done)](m::NowPlayingBody body) {
                if (body) {
                    std::lock_guard lock(counts->mutex);
                    ++counts->players[player][kind].answered;
                    if (kind == covers && std::find(counts->covers.begin(), counts->covers.end(), url) == counts->covers.end()) {
                        if (counts->covers.size() >= 32) counts->covers.erase(counts->covers.begin());
                        counts->covers.push_back(url);
                    }
                }
                done(std::move(body));
            });
        };
    }
    void emit(Json::Object value) {
        if (!services.emit) return;
        Json event(std::move(value));
        event["t"] = std::round(services.clock() * 1000) / 1000;
        services.emit(std::move(event));
    }
    Observed* find(std::string_view application) {
        if (application.empty()) return nullptr;
        for (auto& o : observed)
            if (caseless(o.application, application)) return &o;
        if (!nowPlayingKnownPlayer(application)) {
            const auto others = std::count_if(observed.begin(), observed.end(), [](const auto& o) { return !nowPlayingKnownPlayer(o.application); });
            if (static_cast<std::size_t>(others) >= nowPlayingMaximumOtherPlayers) return nullptr;
        }
        auto& next = observed.emplace_back();
        next.application = std::string(application);
        return &next;
    }
    std::size_t matching(std::string_view application, const std::vector<m::NowPlayingAudioRoute>& routes) const {
        std::size_t count{};
        for (const auto& route : routes)
            if (m::nowPlayingAudioRouteFor(application, std::span(&route, 1), services.volume->processAppUserModelID)) ++count;
        return count;
    }

    void record() {
        const auto& s = service.snapshot();
        if (s.revision != serviceRevision) {
            serviceRevision = s.revision;
            emit(describe(s));
        }
        for (const auto& listed : s.media.sessions)
            if (auto* o = find(listed.appUserModelID)) {
                o->listed = true;
                o->window = o->window || listed.origin == m::NowPlayingSessionOrigin::playerWindow;
            }
        const std::string application = s.media.session ? s.media.session->appUserModelID : std::string();
        if (serious(s.failure)) {
            const std::string name(nowPlayingServiceFailureKey(s.failure));
            auto* o = find(application);
            auto& list = o ? o->failures : globalFailures;
            if (std::find(list.begin(), list.end(), name) == list.end() && list.size() < 8) list.push_back(name);
        }
        completed(s);
        const auto& in = session->presentation().input();
        const bool presenting = s.fresh && s.media.session && s.media.track && in.track && !in.failed;
        follow(presenting ? application : std::string());
        if (!presenting) return;
        auto* o = find(application);
        if (!o) return;
        const auto& track = *s.media.track;
        o->presented = true;
        o->title = o->title || !track.title.empty();
        o->artist = o->artist || !track.artist.empty();
        o->album = o->album || !track.album.empty();
        if (track.duration) {
            o->duration = true;
            o->length = track.duration;
        }
        o->position = o->position || track.position.has_value();
        // A window-title player's playing state is what the HUD shows: the
        // inferred title state, or its process route's audio activity.
        const bool playing = o->window && in.track ? in.track->isPlaying : track.isPlaying;
        (playing ? o->playing : o->paused) = true;
        if (!o->track || !o->track->sameIdentity(track)) {
            o->track = track;
            ++o->tracks;
        }
        const auto& c = s.media.capabilities;
        o->controls.play = o->controls.play || c.play;
        o->controls.pause = o->controls.pause || c.pause;
        o->controls.toggle = o->controls.toggle || c.toggle;
        o->controls.previous = o->controls.previous || c.previous;
        o->controls.next = o->controls.next || c.next;
        o->seekable = o->seekable || (c.seek && track.supportsSeeking);
        progress(*o, s, track);
        cover(*o, s, application);
        lyrics(*o, in, track, application);
        volume(*o, in, track, application);
        command(s, in, track, application); // last: it may republish the session
    }
    // Timeline samples pair only while one player stays presented: during
    // another player's turn, a failed read or a gap without a track, its
    // pauses and seeks are not seen, so its chain restarts.
    void follow(const std::string& application) {
        if (caseless(followed, application)) return;
        if (auto* previous = find(followed)) {
            quietUntil(*previous, services.clock());
            previous->sample.reset();
        }
        followed = application;
    }
    static void quietUntil(Observed& o, double now) {
        if (o.sample && o.sample->playing && std::isfinite(now)) o.quiet = std::max(o.quiet, now - o.sample->sampledAt);
    }
    void progress(Observed& o, const n::NowPlayingServiceSnapshot& s, const m::NowPlayingTrack& track) {
        if (!track.position || !std::isfinite(*track.position) || !std::isfinite(track.sampledAt)) return;
        // Without a timeline the position itself identifies the update.
        const auto& timeline = s.media.timeline;
        const double rate = timeline && std::isfinite(timeline->rate) && timeline->rate > 0 ? timeline->rate : 1;
        const std::int64_t ticks = timeline ? timeline->position : static_cast<std::int64_t>(std::llround(*track.position * 10000000));
        const double base = timeline && std::isfinite(timeline->sampledAt) ? timeline->sampledAt : track.sampledAt;
        Sample next{track, *track.position, track.sampledAt, rate, ticks, base, track.isPlaying, commands};
        if (o.sample) {
            const auto& a = *o.sample;
            if (a.sampledAt == next.sampledAt) return; // the same read again
            // A read for another reason (cover, metadata) extrapolates the same
            // timeline update again: it carries no clock evidence, and pairing
            // it would always agree. Keep the older sample.
            if (a.playing && next.playing && a.ticks == next.ticks && std::abs(a.base - next.base) < 0.25 && a.track.sameIdentity(track)) return;
            const double spacing = next.sampledAt - a.sampledAt;
            if (a.playing && a.commands == next.commands && a.track.sameIdentity(track)) {
                o.quiet = std::max(o.quiet, spacing);
                if (next.playing && spacing >= nowPlayingProgressSpacing) {
                    const double predicted = a.position + spacing * a.rate;
                    o.drift = std::max(o.drift, std::abs(next.position - predicted));
                    ++o.pairs;
                }
            }
        }
        o.sample = std::move(next);
    }
    void cover(Observed& o, const n::NowPlayingServiceSnapshot& s, const std::string& application) {
        const auto image = session->artwork();
        if (coverApplication != application) {
            coverApplication = application;
            coverShown = nullptr;
        }
        if (image && image.get() != coverShown) {
            coverShown = image.get();
            const bool player = bool(s.media.artwork);
            (player ? o.playerCover : o.catalogCover) = std::array{image->width, image->height};
            emit({{"event", "cover"}, {"player", nowPlayingReportIdentity(application)}, {"origin", player ? "player" : "catalog"},
                {"width", static_cast<std::int64_t>(image->width)}, {"height", static_cast<std::int64_t>(image->height)}});
        }
        if (!image) coverShown = nullptr;
        const auto work = session->artworkStats();
        const bool decoding = work.inFlight || work.pending;
        if (!s.media.artwork) {
            // The current track's catalog cover reaches the decoder within the
            // owner drain that delivered its bytes; once nothing downloads or
            // decodes, a missing image means those bytes could not be decoded.
            const auto url = session->lyricsSources().catalogArtwork();
            if (!image && url && !decoding && (!web || !web->stats().inFlight) && coverAnswered(*url) &&
                std::find(failedCovers.begin(), failedCovers.end(), *url) == failedCovers.end()) {
                if (failedCovers.size() >= 32) failedCovers.erase(failedCovers.begin());
                failedCovers.push_back(*url);
                ++o.catalogFailures;
                emit({{"event", "coverFailed"}, {"player", nowPlayingReportIdentity(application)}, {"origin", "catalog"}});
            }
            return;
        }
        o.thumbnail = true;
        if (image) return;
        if (!decoding && s.media.artwork.get() != failedArtwork) {
            failedArtwork = s.media.artwork.get();
            ++o.decodeFailures;
            emit({{"event", "coverFailed"}, {"player", nowPlayingReportIdentity(application)}, {"origin", "player"},
                {"bytes", static_cast<std::int64_t>(s.media.artwork->size())}});
        }
    }
    void lyrics(Observed& o, const m::NowPlayingViewInput& in, const m::NowPlayingTrack& track, const std::string& application) {
        if (lyricsApplication != application) {
            lyricsApplication = application;
            lyricsShown = nullptr;
        }
        if (!in.lyrics) {
            lyricsShown = nullptr;
            return;
        }
        if (in.lyrics.get() == lyricsShown) return;
        lyricsShown = in.lyrics.get();
        // Source precedence: parseable embedded timed lyrics always win.
        const bool embedded = track.timedLyrics && m::NowPlayingLyrics::parse(*track.timedLyrics).has_value();
        o.embedded = o.embedded || embedded;
        o.lyricLines = std::max(o.lyricLines, in.lyrics->lines().size());
        emit({{"event", "lyrics"}, {"player", nowPlayingReportIdentity(application)}, {"origin", embedded ? "embedded" : "downloaded"},
            {"lines", static_cast<std::int64_t>(in.lyrics->lines().size())}});
    }
    void volume(Observed& o, const m::NowPlayingViewInput& in, const m::NowPlayingTrack& track, const std::string& application) {
        if (!options.volume || !services.volume || !services.volume->routes) return;
        const auto routes = services.volume->routes();
        const auto count = matching(application, routes);
        o.matches = std::max(o.matches, count);
        if (!count && track.isPlaying) o.silentWhilePlaying = true;
        if (in.volumeAvailable) {
            o.volumeAvailable = true;
            o.level = in.volume;
            for (const auto& route : routes)
                if (route.id == session->volumeRoute()) o.pid = route.pid;
        }
        auto state = std::tuple{application, in.volumeAvailable, count, in.volume};
        if (volumeShown == state) return;
        volumeShown = std::move(state);
        emit({{"event", "volume"}, {"player", nowPlayingReportIdentity(application)}, {"available", in.volumeAvailable},
            {"matches", static_cast<std::int64_t>(count)}, {"level", optionalNumber(in.volume)}});
    }
    void command(const n::NowPlayingServiceSnapshot& s, const m::NowPlayingViewInput& in, const m::NowPlayingTrack& track, const std::string& application) {
        if (!options.command || commandDone || s.failure != n::NowPlayingServiceFailure::none) return;
        commandDone = true;
        commandApplication = application;
        const auto key = nowPlayingProbeCommandKey(*options.command);
        if (!m::nowPlayingCommand(track, s.media.capabilities, *options.command)) {
            commandResult = NowPlayingCheckResult{V::gap, "the player does not allow " + key, 1};
            emit({{"event", "commandUnsupported"}, {"player", nowPlayingReportIdentity(application)}, {"command", key}});
            return;
        }
        // Exactly the HUD's click path (NowPlayingSession::dispatch).
        const m::NowPlayingIntent intent{intentKind(options.command->kind), in.session, options.command->seconds};
        const bool accepted = session->dispatch(intent, services.clock());
        emit({{"event", "commandSent"}, {"player", nowPlayingReportIdentity(application)}, {"command", key}, {"accepted", accepted}});
        if (!accepted) {
            commandResult = NowPlayingCheckResult{V::fail, key + " was refused by the Now Playing owner", 1};
            return;
        }
        if (const auto& receipt = service.snapshot().commandAccepted) commandToken = receipt->token;
        ++commands;
    }
    void completed(const n::NowPlayingServiceSnapshot& s) {
        if (!commandToken || commandResult || !s.commandCompleted || s.commandCompleted->request.token != commandToken) return;
        const auto key = nowPlayingProbeCommandKey(*options.command);
        const auto failure = s.commandCompleted->failure;
        emit({{"event", "commandCompleted"}, {"command", key}, {"failure", std::string(nowPlayingServiceFailureKey(failure))}});
        if (failure == n::NowPlayingServiceFailure::stale)
            commandResult = NowPlayingCheckResult{V::unverified, "the session changed before " + key + " completed", 0};
        else if (failure != n::NowPlayingServiceFailure::none)
            commandResult = NowPlayingCheckResult{V::fail, key + " failed: " + std::string(nowPlayingServiceFailureKey(failure)), 1};
        else if (!playback)
            commandResult = NowPlayingCheckResult{V::fail, key + " completed without an Event Log playbackAction", 1};
        else {
            commandLog = "Event Log playbackAction action=" + std::string(playback->action) + ", source=" + std::string(m::nowPlayingSourceKey(playback->source));
            commandResult = NowPlayingCheckResult{V::pass, key + " completed; " + commandLog, 1};
            if (auto* o = find(commandApplication); o && o->window) o->commandTracks = o->tracks;
        }
    }

    bool coverAnswered(const std::string& url) {
        std::lock_guard lock(tally->mutex);
        return std::find(tally->covers.begin(), tally->covers.end(), url) != tally->covers.end();
    }
    std::string lookups(const std::string& application) {
        std::array<Counts, lookupKinds> counts{};
        {
            std::lock_guard lock(tally->mutex);
            if (const auto found = tally->players.find(application); found != tally->players.end()) counts = found->second;
        }
        std::vector<std::string> parts;
        if (counts[catalog].started) parts.push_back("NetEase catalog " + std::to_string(counts[catalog].answered) + "/" + std::to_string(counts[catalog].started));
        if (counts[lrclib].started) parts.push_back("LRCLIB " + std::to_string(counts[lrclib].answered) + "/" + std::to_string(counts[lrclib].started));
        if (counts[covers].started) parts.push_back("catalog cover " + std::to_string(counts[covers].answered) + "/" + std::to_string(counts[covers].started));
        return parts.empty() ? std::string("no lookup was possible; title and artist are needed") : join(parts, ", ") + " answered";
    }
    std::string labelOf(std::string_view application) const {
        if (const auto known = nowPlayingKnownPlayer(application)) return std::string(knownPlayers[*known].label);
        return nowPlayingReportIdentity(application);
    }
    NowPlayingAcceptanceRow evaluate(const Observed* o, const NowPlayingKnownPlayer* k) {
        NowPlayingAcceptanceRow row;
        const std::string identity = o ? nowPlayingReportIdentity(o->application) : hintText(k->identities[0]);
        row.player = k ? std::string(k->key) : std::string("other");
        row.label = k ? std::string(k->label) : identity;
        row.application = identity;
        const auto mapped = o ? m::nowPlayingSourceForAppUserModelID(o->application) : k->source;
        row.source = std::string(m::nowPlayingSourceKey(mapped));
        const auto set = [&row](C check, V verdict, std::string detail) { row[check] = {verdict, std::move(detail), verdict == V::unverified ? 0u : 1u}; };
        if (!o || !o->listed) {
            set(C::session, V::unverified, "not running during this run: start " + row.label + " and play a track");
            return row;
        }
        // A window-title player (native/now_playing_players.hpp) has no
        // media session: its timeline, seek and playback reports are gaps.
        const bool window = o->window;
        set(C::session, V::pass, window ? "found by its main window title as " + identity + " (it publishes no media session)" : "listed by GSMTC as " + identity);
        if (k) set(C::source, mapped == k->source ? V::pass : V::fail,
                   mapped == k->source ? "Event Log source " + row.source
                                       : "maps to " + row.source + ", expected " + std::string(m::nowPlayingSourceKey(k->source)));
        else set(C::source, V::pass, "Event Log source " + row.source + " (not one of the source players)");
        if (!o->presented) {
            set(C::current, V::unverified, "listed but never the current session: play a track in it");
            return row;
        }
        set(C::current, V::pass, "presented as the current session");

        if (!o->title) set(C::metadata, V::fail, "the player published no title");
        else set(C::metadata, V::pass, std::string("title") + (o->artist ? ", artist" : ", no artist") + (o->album ? ", album" : ", no album"));

        if (o->duration && o->position) set(C::timeline, V::pass, "duration " + m::nowPlayingTime(o->length) + " with position");
        else if (window) set(C::timeline, V::gap, "a window title carries no duration or position");
        else set(C::timeline, V::gap, o->duration ? "the player publishes no position" : o->position ? "the player publishes no duration"
                                                                                                      : "the player publishes no duration or position");

        if (window && !o->position)
            set(C::progress, V::gap, "no timeline: the HUD shows no progress for a window-title player");
        else if (!o->pairs && o->quiet >= nowPlayingProgressQuiet)
            set(C::progress, V::gap, "no timeline update during " + fixed(o->quiet, 0) +
                                         " s of playback: the player publishes its timeline only when playback changes; the HUD advances the "
                                         "progress from the last sample, as in the source");
        else if (!o->pairs)
            set(C::progress, V::unverified, "no second timeline update at least 1 s into playback: let it play longer without pausing or seeking");
        else if (o->drift <= nowPlayingProgressTolerance)
            set(C::progress, V::pass, std::to_string(o->pairs) + " playing timeline update pair(s), max drift " + fixed(o->drift, 2) + " s");
        else set(C::progress, V::fail, "the timeline drifted " + fixed(o->drift, 1) + " s from the playing clock over " + std::to_string(o->pairs) +
                                           " pair(s); a seek inside the player also does this");

        if (o->playing && o->paused)
            set(C::playback, V::pass, window ? "followed playing and paused (from its window title, audio activity and HUD commands)" : "followed playing and paused");
        else if (window)
            set(C::playback, V::unverified, o->playing ? "only playing observed: pause the player (its idle title or a silent audio session shows it), or run with --command playPause"
                                                       : "only paused observed: play a track in the player");
        else set(C::playback, V::unverified, o->playing ? "only playing observed: pause and resume in the player" : "only paused observed: resume playback in the player");

        if (o->tracks >= 2) set(C::trackChange, V::pass, std::to_string(o->tracks) + " tracks followed");
        else set(C::trackChange, V::unverified, "one track observed: change track in the player");

        std::vector<std::string> enabled;
        const auto& c = o->controls;
        for (const auto& [on, name] : {std::pair{c.play, "play"}, std::pair{c.pause, "pause"}, std::pair{c.toggle, "toggle"},
                 std::pair{c.previous, "previous"}, std::pair{c.next, "next"}})
            if (on) enabled.emplace_back(name);
        if (enabled.empty()) set(C::controls, V::gap, "the player enables no transport control");
        else set(C::controls, V::pass, join(enabled, " "));

        if (o->seekable) set(C::seek, V::pass, "seek enabled");
        else if (window) set(C::seek, V::gap, "a window-title player cannot seek; the seek rail is disabled");
        else set(C::seek, V::gap, "the player does not allow seeking; the seek rail is disabled, as in the source");

        const auto dimensions = [](const std::array<unsigned, 2>& d) { return std::to_string(d[0]) + "x" + std::to_string(d[1]); };
        if (o->decodeFailures)
            set(C::artwork, V::fail, std::to_string(o->decodeFailures) + " player thumbnail(s) could not be decoded" +
                                         (o->playerCover ? "; another decoded at " + dimensions(*o->playerCover) : std::string()));
        else if (o->playerCover) set(C::artwork, V::pass, "player thumbnail decoded at " + dimensions(*o->playerCover));
        else if (o->catalogCover) set(C::artwork, V::pass, "NetEase catalog cover decoded at " + dimensions(*o->catalogCover));
        else if (o->catalogFailures) set(C::artwork, V::fail, "the downloaded NetEase catalog cover could not be decoded");
        else if (o->thumbnail) set(C::artwork, V::unverified, "the thumbnail was still decoding when the run ended");
        else if (mapped == m::NowPlayingSource::netease && !options.lyrics)
            set(C::artwork, V::unverified, "no player thumbnail: run with --lyrics for the NetEase catalog cover");
        else set(C::artwork, V::gap, mapped == m::NowPlayingSource::netease ? "no player thumbnail and no NetEase catalog cover; the outlined placeholder is shown"
                                     : window ? "a window title carries no thumbnail; the outlined placeholder is shown"
                                              : "the player publishes no thumbnail; the outlined placeholder is shown");

        if (o->lyricLines)
            set(C::lyrics, V::pass, o->embedded ? "embedded timed lyrics, " + std::to_string(o->lyricLines) + " lines"
                                                : "downloaded timed lyrics, " + std::to_string(o->lyricLines) + " lines (" + lookups(o->application) + ")");
        else if (!options.lyrics) set(C::lyrics, V::unverified, "no embedded timed lyrics: run with --lyrics for the NetEase catalog and LRCLIB");
        else set(C::lyrics, V::unverified, "no timed lyrics found for " + std::to_string(o->tracks) + " track(s) (" + lookups(o->application) + ")");

        // Packaged identities ("Family!App") and Win32 executables resolve to
        // one process; other explicit identities (browsers) cannot.
        const bool expectRoute = k ? k->volume : m::nowPlayingWin32Identity(o->application) || o->application.find('!') != std::string::npos;
        if (!options.volume) set(C::volume, V::unverified, "run with --volume to read its per-app audio route");
        else if (o->volumeAvailable)
            set(C::volume, V::pass, "one process route" + (o->pid ? " (pid " + std::to_string(o->pid) + ")" : std::string()) + ", level " + percent(o->level.value_or(1)));
        else if (o->matches > 1)
            set(C::volume, V::gap, std::to_string(o->matches) + " processes match; the unavailable submenu is shown (no single adjustable process)");
        else if (o->matches == 1) set(C::volume, V::fail, "its process route cannot be adjusted (failed route)");
        else if (!expectRoute) set(C::volume, V::gap, "no single process carries this identity (platform gap); the unavailable submenu is shown");
        else if (o->silentWhilePlaying) set(C::volume, V::fail, "no per-app audio session of " + identity + " on the default output while it was playing");
        else set(C::volume, V::unverified, "no per-app audio session while paused: play to verify");

        if (!options.command) set(C::command, V::unverified, "run with --command playPause|previous|next|seek=SECONDS");
        else if (!commandDone) set(C::command, V::unverified, "no presented track to send the command to");
        else if (!caseless(commandApplication, o->application))
            set(C::command, V::unverified, "the command went to " + labelOf(commandApplication) + "; rerun while this player is current");
        else if (commandResult && window && commandResult->verdict == V::pass) {
            // WM_APPCOMMAND is posted: a skip shows as a new window title; a
            // play/pause is not reported by the title at all.
            const auto key = nowPlayingProbeCommandKey(*options.command);
            if (options.command->kind == m::NowPlayingCommandKind::playPause)
                set(C::command, V::gap, key + " was delivered (WM_APPCOMMAND); " + commandLog + "; a window title does not report playback, so confirm it in the player");
            else if (o->commandTracks && o->tracks > *o->commandTracks)
                set(C::command, V::pass, key + " was delivered (WM_APPCOMMAND) and the player's title changed afterwards; " + commandLog);
            else
                set(C::command, V::unverified, key + " was delivered (WM_APPCOMMAND) but the player's title did not change afterwards: if it did not skip, "
                                                     "the player ignores WM_APPCOMMAND");
        } else if (commandResult) row[C::command] = *commandResult;
        else set(C::command, V::unverified, nowPlayingProbeCommandKey(*options.command) + " was still pending when the run ended");

        if (o->failures.empty()) set(C::service, V::pass, "no media-session failure while current");
        else set(C::service, V::fail, "media-session failure: " + join(o->failures, ", "));
        return row;
    }
    NowPlayingAcceptanceMatrix build() {
        NowPlayingAcceptanceMatrix out;
        out.runs.push_back({1, options.mode, options.recorded, std::isfinite(options.seconds) ? std::max(0., options.seconds) : 0., options.lyrics,
            options.volume, options.command ? nowPlayingProbeCommandKey(*options.command) : std::string(), join(globalFailures, ", ")});
        for (std::size_t k = 0; k < knownPlayers.size(); ++k) {
            const Observed* best{};
            for (const auto& o : observed)
                if (nowPlayingKnownPlayer(o.application) == k && (!best || (o.presented && !best->presented) || (o.listed && !best->listed))) best = &o;
            out.rows.push_back(evaluate(best, &knownPlayers[k]));
        }
        for (const auto& o : observed)
            if (!nowPlayingKnownPlayer(o.application)) out.rows.push_back(evaluate(&o, nullptr));
        return out;
    }
};

NowPlayingProbe::NowPlayingProbe(NowPlayingProbeOptions options, NowPlayingProbeServices services)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(services))) {}
NowPlayingProbe::~NowPlayingProbe() = default;
void NowPlayingProbe::start() {
    auto& i = *impl_;
    if (i.active || i.stopped) return;
    i.active = true;
    i.emit({{"event", "activated"}, {"lyrics", i.options.lyrics}, {"volume", i.options.volume},
        {"command", i.options.command ? Json(nowPlayingProbeCommandKey(*i.options.command)) : Json()}});
    i.session->setActive(true, i.services.clock());
    i.record();
}
bool NowPlayingProbe::wake() {
    auto& i = *impl_;
    if (!i.active) return false;
    i.utility.drain();
    const bool changed = i.session->utilityCompleted(i.services.clock());
    i.record();
    return changed;
}
bool NowPlayingProbe::deadline() {
    auto& i = *impl_;
    if (!i.active) return false;
    const bool changed = i.session->deadline(i.services.clock());
    i.record();
    return changed;
}
bool NowPlayingProbe::audioChanged() {
    auto& i = *impl_;
    if (!i.active) return false;
    const bool changed = i.session->audioChanged(i.services.clock());
    i.record();
    return changed;
}
std::optional<double> NowPlayingProbe::nextWakeTime() const {
    const auto& i = *impl_;
    return i.active ? i.session->nextWakeTime(i.services.clock()) : std::nullopt;
}
bool NowPlayingProbe::settle() {
    impl_->utility.waitIdle();
    return wake();
}
bool NowPlayingProbe::settled() const {
    const auto& i = *impl_;
    const auto work = i.utility.stats();
    if (work.pending || work.running || work.completed) return false;
    if (i.service.snapshot().busy) return false;
    if (i.web && i.web->stats().inFlight) return false;
    const auto images = i.session->artworkStats();
    return !images.inFlight && !images.pending;
}
NowPlayingAcceptanceMatrix NowPlayingProbe::stop() {
    auto& i = *impl_;
    if (!i.stopped) {
        if (i.active) {
            i.record();
            if (auto* followed = i.find(i.followed)) Impl::quietUntil(*followed, i.services.clock());
            i.session->setActive(false, i.services.clock());
            i.active = false;
        }
        if (i.web) i.web->cancelAll();
        i.stopped = true;
        Json::Object stopped{{"event", "stopped"}};
        if (i.web) {
            const auto stats = i.web->stats();
            stopped["requests"] = static_cast<std::int64_t>(stats.started);
            stopped["answered"] = static_cast<std::int64_t>(stats.completed);
        }
        i.emit(std::move(stopped));
    }
    return i.build();
}

// ---------------------------------------------------------------------------
// Synthetic replay (tests and --replay): no OS media session, network or audio.
// ---------------------------------------------------------------------------
namespace {
struct ReplayState {
    std::mutex mutex;
    n::NowPlayingReadResult current;
    n::NowPlayingProvider::Changed changed;
    n::NowPlayingCommandResult command;
};
class ReplayProvider final : public n::NowPlayingProvider {
public:
    explicit ReplayProvider(std::shared_ptr<ReplayState> state) : state_(std::move(state)) {}
    void start(Changed changed, Ready ready) override {
        {
            std::lock_guard lock(state_->mutex);
            if (stopped_) return;
            state_->changed = std::move(changed);
        }
        ready({});
    }
    void read(Read completion) override {
        n::NowPlayingReadResult copy;
        {
            std::lock_guard lock(state_->mutex);
            if (stopped_) return;
            copy = state_->current;
        }
        completion(std::move(copy));
    }
    void perform(std::uint64_t, std::uint64_t, m::NowPlayingCommand, Ready ready) override {
        n::NowPlayingCommandResult result;
        {
            std::lock_guard lock(state_->mutex);
            if (stopped_) return;
            result = state_->command;
        }
        ready(result);
    }
    void stop() noexcept override {
        std::lock_guard lock(state_->mutex);
        stopped_ = true;
        state_->changed = nullptr;
    }
private:
    std::shared_ptr<ReplayState> state_;
    bool stopped_{}; // guarded by state_->mutex
};
struct ReplayTask final : n::NowPlayingWeb::Task {
    void cancel() noexcept override {}
};
struct ReplayResponse {
    std::string url;
    bool prefix{};
    m::NowPlayingBody body;
};
struct ReplayStep {
    double at{};
    n::NowPlayingReadResult read;
};
struct Replay {
    std::vector<ReplayStep> steps;
    std::vector<ReplayResponse> responses;
    std::vector<m::NowPlayingAudioRoute> routes;
    n::NowPlayingCommandResult command;
};
[[noreturn]] void malformed(const std::string& why) { throw std::invalid_argument("Malformed Now Playing replay: " + why); }
std::string text(const Json& value, const char* what) {
    if (!boundedText(value)) malformed(what);
    return value.string();
}
std::optional<double> number(const Json& value, const char* what) {
    if (value.isNull()) return std::nullopt;
    if (!value.isNumber() || !std::isfinite(value.number())) malformed(what);
    return value.number();
}
bool flag(const Json& value, bool fallback, const char* what) {
    if (value.isNull()) return fallback;
    if (!value.isBool()) malformed(what);
    return value.boolean();
}
n::NowPlayingServiceFailure failure(const Json& value) {
    if (value.isNull()) return n::NowPlayingServiceFailure::none;
    const auto key = text(value, "failure");
    for (const auto f : {n::NowPlayingServiceFailure::none, n::NowPlayingServiceFailure::unavailable, n::NowPlayingServiceFailure::accessDenied,
             n::NowPlayingServiceFailure::unsupported, n::NowPlayingServiceFailure::cancelled, n::NowPlayingServiceFailure::stale,
             n::NowPlayingServiceFailure::capacity, n::NowPlayingServiceFailure::timedOut})
        if (nowPlayingServiceFailureKey(f) == key) return f;
    malformed("unknown failure " + key);
}
m::NowPlayingBody bytes(std::string_view value) { return std::make_shared<const std::vector<std::uint8_t>>(value.begin(), value.end()); }
m::NowPlayingBody artwork(const Json& value) {
    if (value.isNull()) return nullptr;
    const auto kind = text(value, "artwork");
    if (kind == "cover") return n::nowPlayingFixtureCover();
    // Not an image: the decoder must reject it.
    static const auto corrupt = bytes("EndfieldHUD replay: not an image");
    if (kind == "corrupt") return corrupt;
    malformed("unknown artwork " + kind);
}
Replay parseReplay(const Json& root) {
    if (!root.isObject() || !root["format"].isString() || root["format"].string() != nowPlayingReplayFormat || !root["version"].isNumber() ||
        root["version"].integer() != 1)
        malformed("format or version");
    Replay out;
    out.command.failure = failure(root["commandFailure"]);
    if (const auto& routes = root["routes"]; !routes.isNull()) {
        if (!routes.isArray() || routes.array().size() > 64) malformed("routes");
        for (const auto& r : routes.array()) {
            if (!r.isObject()) malformed("route");
            m::NowPlayingAudioRoute route;
            route.id = text(r["id"], "route id");
            const auto pid = number(r["pid"], "route pid");
            if (!pid || *pid < 0 || *pid > 4294967295. || *pid != std::trunc(*pid)) malformed("route pid");
            route.pid = static_cast<std::uint32_t>(*pid);
            route.executablePath = r["executable"].isNull() ? std::string() : text(r["executable"], "route executable");
            route.available = flag(r["available"], true, "route available");
            const auto state = r["state"].isNull() ? std::string("direct") : text(r["state"], "route state");
            if (state == "direct") route.state = m::NowPlayingAudioRouteState::direct;
            else if (state == "active") route.state = m::NowPlayingAudioRouteState::active;
            else if (state == "failed") route.state = m::NowPlayingAudioRouteState::failed;
            else malformed("route state");
            route.gain = number(r["gain"], "route gain");
            if (!r["rendering"].isNull()) route.rendering = flag(r["rendering"], false, "route rendering");
            if (!r["appUserModelID"].isNull()) route.appUserModelID = text(r["appUserModelID"], "route appUserModelID");
            out.routes.push_back(std::move(route));
        }
    }
    if (const auto& responses = root["responses"]; !responses.isNull()) {
        if (!responses.isArray() || responses.array().size() > 64) malformed("responses");
        for (const auto& r : responses.array()) {
            if (!r.isObject() || r["url"].isNull() == r["prefix"].isNull()) malformed("response needs url or prefix");
            ReplayResponse response;
            response.prefix = !r["prefix"].isNull();
            response.url = text(response.prefix ? r["prefix"] : r["url"], "response url");
            const auto body = text(r["body"], "response body");
            response.body = body == "@cover" ? n::nowPlayingFixtureCover() : bytes(body);
            out.responses.push_back(std::move(response));
        }
    }
    const auto& steps = root["steps"];
    if (!steps.isArray() || steps.array().empty() || steps.array().size() > 256) malformed("steps");
    std::vector<std::string> identities; // stable session tokens across the scenario
    std::optional<m::NowPlayingTrack> previous;
    std::uint64_t metadata{1};
    for (const auto& s : steps.array()) {
        if (!s.isObject()) malformed("step");
        ReplayStep step;
        const auto at = number(s["at"], "step at");
        if (!at || *at < 0 || (!out.steps.empty() && *at < out.steps.back().at)) malformed("step times must be finite and non-decreasing");
        step.at = *at;
        step.read.failure = failure(s["failure"]);
        if (step.read.failure != n::NowPlayingServiceFailure::none) {
            out.steps.push_back(std::move(step));
            continue;
        }
        auto& media = step.read.value;
        const auto& sessions = s["sessions"];
        if (!sessions.isArray() || sessions.array().size() > 128) malformed("sessions");
        for (const auto& id : sessions.array()) {
            // "identity", or {"id": "identity", "origin": "mediaSession"|"playerWindow"}.
            auto origin = m::NowPlayingSessionOrigin::mediaSession;
            std::string identity;
            if (id.isObject()) {
                identity = text(id["id"], "session identity");
                const auto kind = id["origin"].isNull() ? std::string("mediaSession") : text(id["origin"], "session origin");
                if (kind == "playerWindow") origin = m::NowPlayingSessionOrigin::playerWindow;
                else if (kind != "mediaSession") malformed("session origin " + kind);
            } else identity = text(id, "session identity");
            if (identity.empty()) malformed("session identity");
            auto found = std::find(identities.begin(), identities.end(), identity);
            if (found == identities.end()) found = identities.insert(identities.end(), identity);
            const auto token = static_cast<std::uint64_t>(found - identities.begin()) + 1;
            for (const auto& listed : media.sessions)
                if (listed.token == token) malformed("duplicate session");
            media.sessions.push_back({token, identity, identity, origin});
        }
        if (!s["current"].isNull()) {
            const auto current = text(s["current"], "current");
            const auto found = std::find_if(media.sessions.begin(), media.sessions.end(), [&](const auto& x) { return x.appUserModelID == current; });
            if (found == media.sessions.end()) malformed("the current session must be listed");
            media.session = *found;
        }
        if (const auto& t = s["track"]; !t.isNull()) {
            if (!media.session || !t.isObject()) malformed("a track needs the current session");
            m::NowPlayingTrack track;
            track.title = text(t["title"], "title");
            track.artist = t["artist"].isNull() ? std::string() : text(t["artist"], "artist");
            track.album = t["album"].isNull() ? std::string() : text(t["album"], "album");
            track.duration = number(t["duration"], "duration");
            track.position = number(t["position"], "position");
            track.isPlaying = flag(t["playing"], false, "playing");
            track.sampledAt = number(t["sampledAt"], "sampledAt").value_or(step.at);
            track.supportsSeeking = flag(t["supportsSeeking"], true, "supportsSeeking");
            if (!t["timedLyrics"].isNull()) track.timedLyrics = text(t["timedLyrics"], "timedLyrics");
            const double rate = number(s["rate"], "rate").value_or(1);
            // The player's timeline update (GSMTC LastUpdatedTime) may precede
            // the read: like the service, `position` is then extrapolated from
            // the update's raw position while playing.
            const double updatedAt = number(t["updatedAt"], "updatedAt").value_or(track.sampledAt);
            if (updatedAt > track.sampledAt || rate < 0) malformed("updatedAt must not follow sampledAt; rate must not be negative");
            if (track.duration && track.position) {
                constexpr double ticks = 10000000;
                n::NowPlayingTimeline timeline;
                timeline.end = static_cast<std::int64_t>(std::llround(*track.duration * ticks));
                const double raw = *track.position - (track.isPlaying ? (track.sampledAt - updatedAt) * rate : 0);
                timeline.position = static_cast<std::int64_t>(std::llround(raw * ticks));
                timeline.maximumSeek = timeline.end;
                timeline.rate = rate;
                timeline.sampledAt = updatedAt;
                timeline.playing = track.isPlaying;
                media.timeline = timeline;
            }
            if (previous && !previous->sameIdentity(track)) ++metadata;
            previous = track;
            media.track = std::move(track);
            media.artwork = artwork(s["artwork"]);
            media.metadataRevision = metadata;
            if (const auto& list = s["capabilities"]; !list.isNull()) {
                if (!list.isArray()) malformed("capabilities");
                auto& c = media.capabilities;
                for (const auto& value : list.array()) {
                    const auto name = text(value, "capability");
                    if (name == "play") c.play = true;
                    else if (name == "pause") c.pause = true;
                    else if (name == "toggle") c.toggle = true;
                    else if (name == "previous") c.previous = true;
                    else if (name == "next") c.next = true;
                    else if (name == "seek") c.seek = true;
                    else malformed("unknown capability " + name);
                }
            }
        }
        out.steps.push_back(std::move(step));
    }
    return out;
}
} // namespace

NowPlayingAcceptanceMatrix nowPlayingReplay(const Json& scenario, NowPlayingProbeOptions options, n::NativeNowPlayingArtwork::Decoder decode,
    std::function<void(Json)> emit) {
    const auto replay = parseReplay(scenario);
    auto state = std::make_shared<ReplayState>();
    state->command = replay.command;
    const auto clock = std::make_shared<std::atomic<double>>(replay.steps.front().at);
    NowPlayingProbeServices services;
    services.provider = [state] { return std::make_shared<ReplayProvider>(state); };
    services.transport = [responses = replay.responses](const n::NowPlayingWeb::Request& request, n::NowPlayingWeb::Done done) -> std::shared_ptr<n::NowPlayingWeb::Task> {
        m::NowPlayingBody body;
        const auto exact = std::find_if(responses.begin(), responses.end(), [&](const auto& r) { return !r.prefix && r.url == request.url; });
        if (exact != responses.end()) body = exact->body;
        else {
            std::size_t best{};
            for (const auto& r : responses)
                if (r.prefix && request.url.starts_with(r.url) && r.url.size() > best) {
                    best = r.url.size();
                    body = r.body;
                }
        }
        done(std::move(body)); // an unanswered URL fails like a 404
        return std::make_shared<ReplayTask>();
    };
    auto volume = std::make_shared<m::NowPlayingAppVolume>();
    volume->setActive = [](bool) {};
    volume->revision = [] { return std::uint64_t{1}; };
    volume->routes = [routes = replay.routes] { return routes; };
    volume->setGain = [](const std::string&, double) { return false; };
    volume->stop = [](const std::string&) { return false; };
    services.volume = std::move(volume);
    services.decode = std::move(decode);
    services.clock = [clock] { return clock->load(std::memory_order_acquire); };
    services.wake = [] {}; // the replay settles synchronously
    services.emit = std::move(emit);
    options.mode = "replay";
    options.recorded.clear();
    options.seconds = replay.steps.back().at - replay.steps.front().at;

    NowPlayingProbe probe(std::move(options), std::move(services));
    const auto settle = [&] {
        for (unsigned pass = 0; pass < 256; ++pass) {
            bool changed = probe.settle();
            if (const auto due = probe.nextWakeTime(); due && *due <= clock->load()) changed = probe.deadline() || changed;
            if (!changed && probe.settled()) return;
        }
        throw std::runtime_error("The Now Playing replay did not settle");
    };
    probe.start();
    settle();
    for (const auto& step : replay.steps) {
        clock->store(step.at, std::memory_order_release);
        settle(); // display deadlines that came due before the player's change
        n::NowPlayingProvider::Changed changed;
        {
            std::lock_guard lock(state->mutex);
            state->current = step.read;
            changed = state->changed;
        }
        if (changed) changed();
        settle();
    }
    return probe.stop();
}
} // namespace endfield::tools
