// now_playing_probe acceptance matrix: the per-player rules, the report
// format, merging across runs, Markdown, the player catalog and the replay
// driver over the production Now Playing owner (NowPlayingSession). Synthetic
// scenario, injected transport and audio rows only: no GSMTC session, network
// request, audio device or user data is touched. Report files live in a
// private temporary directory that is removed afterwards.
#include "tools/now_playing_acceptance.hpp"
#include "modules/now_playing_lyrics_sources.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>

namespace t = endfield::tools;
namespace m = endfield::modules;
namespace n = endfield::native;
namespace fs = std::filesystem;
using Json = ehud::data::Json;
using V = t::NowPlayingVerdict;
using R = t::NowPlayingRowVerdict;
using C = t::NowPlayingCheck;

namespace {
unsigned checks{};
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

// Windows decodes the scenario's cover with the production WIC decoder; other
// hosts (portable configure) use a strict BMP header reader with the same
// outcome: the fixture cover decodes at 96x96, anything else fails.
n::NativeNowPlayingArtwork::Decoder decoder() {
#ifdef _WIN32
    return n::decodeNowPlayingImage;
#else
    return [](std::span<const std::uint8_t> b) -> n::NowPlayingImageResult {
        const auto u32 = [&](std::size_t at) { return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24; };
        if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return {{}, n::NowPlayingImageFailure::decode};
        auto frame = std::make_shared<n::NotesImageFrame>();
        frame->width = u32(18);
        frame->height = u32(22);
        frame->straightRGBA.assign(std::size_t{frame->width} * frame->height * 4, 255);
        return {frame};
    };
#endif
}

Json scenario(const char* path) {
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(!text.empty(), "The replay scenario fixture is readable");
    return Json::parse(text);
}
t::NowPlayingProbeOptions full() {
    t::NowPlayingProbeOptions o;
    o.lyrics = o.volume = true;
    o.command = m::NowPlayingCommand{m::NowPlayingCommandKind::next};
    return o;
}
const t::NowPlayingAcceptanceRow& row(const t::NowPlayingAcceptanceMatrix& matrix, std::string_view player) {
    const auto* found = matrix.find(player);
    if (!found) throw std::runtime_error("Matrix row missing: " + std::string(player));
    return *found;
}
void expect(const t::NowPlayingAcceptanceRow& r, C c, V verdict, std::string_view detail = {}) {
    const auto& value = r[c];
    check(value.verdict == verdict, r.player + "/" + std::string(t::nowPlayingCheckKey(c)) + " is " + std::string(t::nowPlayingVerdictKey(verdict)) +
                                        " (got " + std::string(t::nowPlayingVerdictKey(value.verdict)) + ": " + value.detail + ")");
    if (!detail.empty()) check(value.detail == detail, r.player + "/" + std::string(t::nowPlayingCheckKey(c)) + " detail: expected '" + std::string(detail) + "', got '" + value.detail + "'");
}

void catalog() {
    const auto players = t::nowPlayingKnownPlayers();
    check(players.size() == 11, "The live acceptance names eleven player identities");
    const auto index = [&](std::string_view key) {
        for (std::size_t k = 0; k < players.size(); ++k)
            if (players[k].key == key) return std::optional(k);
        return std::optional<std::size_t>();
    };
    check(t::nowPlayingKnownPlayer("C:\\Users\\someone\\AppData\\Roaming\\Spotify\\Spotify.exe") == index("spotifyDesktop"), "A Win32 identity matches by file name");
    check(t::nowPlayingReportIdentity("C:\\Users\\someone\\AppData\\Roaming\\Spotify\\Spotify.exe") == "Spotify.exe", "Reports never keep a user profile directory");
    check(t::nowPlayingReportIdentity("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify") == "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify", "Packaged identities stay verbatim");
    check(t::nowPlayingKnownPlayer("spotifyab.spotifymusic_zpdnekdrzrea0!Spotify") == index("spotifyStore"), "Package family prefixes compare case-insensitively");
    check(t::nowPlayingKnownPlayer("1F8B0F94.122165AE053F_j2p0p5q0044a6!cloudmusic") == index("neteaseStore"), "NetEase Store package");
    check(t::nowPlayingKnownPlayer("Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic") == index("mediaPlayer"), "Windows Media Player package");
    check(t::nowPlayingKnownPlayer("MSEdge") == index("edge") && t::nowPlayingKnownPlayer("msedge.exe") == index("edge"), "Edge explicit and Win32 identities");
    check(t::nowPlayingKnownPlayer("Chrome") == index("chrome") && t::nowPlayingKnownPlayer("308046B0AF4A39CB") == index("firefox"), "Browser identities");
    check(!t::nowPlayingKnownPlayer("NotSpotify.exe") && !t::nowPlayingKnownPlayer("Spotify.exe.bak") && !t::nowPlayingKnownPlayer("Spotify.exe!App") &&
              !t::nowPlayingKnownPlayer("ChromeBeta") && !t::nowPlayingKnownPlayer(""),
        "Nothing else is mistaken for a known player");
    // The catalog agrees with the production source mapping for every hint.
    for (const auto& player : players)
        for (const auto hint : player.identities) {
            if (hint.empty()) continue;
            const std::string id = hint.back() == '_' ? std::string(hint) + "abc123!App" : std::string(hint);
            check(t::nowPlayingKnownPlayer(id).has_value() && players[*t::nowPlayingKnownPlayer(id)].key == player.key, "Hint resolves to its own player: " + id);
            check(m::nowPlayingSourceForAppUserModelID(id) == player.source, "Source mapping of " + id + " is the catalog's");
        }
    check(t::nowPlayingProbeCommand("next")->kind == m::NowPlayingCommandKind::next && t::nowPlayingProbeCommand("playPause") && t::nowPlayingProbeCommand("previous"),
        "Transport commands parse");
    const auto seek = t::nowPlayingProbeCommand("seek=12.5");
    check(seek && seek->kind == m::NowPlayingCommandKind::seek && seek->seconds == 12.5 && t::nowPlayingProbeCommandKey(*seek) == "seek=12.5", "Seek parses and round-trips");
    for (const auto bad : {"seek=", "seek=-1", "seek=1e3", "seek=inf", "seek=nan", "seek=0x10", "seek=.5", "pause", "", "seek=9999999"})
        check(!t::nowPlayingProbeCommand(bad), std::string("Rejected command: ") + bad);
}

void replay(const char* fixture) {
    const auto source = scenario(fixture);
    std::vector<Json> events;
    const auto b = t::nowPlayingReplay(source, full(), decoder(), [&](Json e) { events.push_back(std::move(e)); });
    check(b.runs.size() == 1 && b.runs[0].run == 1 && b.runs[0].mode == "replay" && b.runs[0].seconds == 24 && b.runs[0].lyrics && b.runs[0].volume &&
              b.runs[0].command == "next" && b.runs[0].recorded.empty() && b.runs[0].service.empty(),
        "The run record states mode, length and options");
    check(b.rows.size() == 12 && b.rows.back().player == "other", "Every known player, then the other player");

    const auto& spotify = row(b, "spotifyDesktop");
    check(spotify.application == "Spotify.exe" && spotify.source == "spotify" && spotify.label == "Spotify (desktop)", "Spotify row identity");
    expect(spotify, C::session, V::pass, "listed by GSMTC as Spotify.exe");
    expect(spotify, C::current, V::pass);
    expect(spotify, C::source, V::pass, "Event Log source spotify");
    expect(spotify, C::metadata, V::pass, "title, artist, album");
    expect(spotify, C::timeline, V::pass, "duration 3:00 with position");
    // The command at t=1 separates the first pair; 10 s -> 12 s remains.
    expect(spotify, C::progress, V::pass, "1 playing timeline update pair(s), max drift 0.00 s");
    expect(spotify, C::playback, V::pass, "followed playing and paused");
    expect(spotify, C::trackChange, V::pass, "2 tracks followed");
    expect(spotify, C::controls, V::pass, "play pause toggle previous next");
    expect(spotify, C::seek, V::pass);
    expect(spotify, C::artwork, V::pass, "player thumbnail decoded at 96x96");
    expect(spotify, C::lyrics, V::pass, "downloaded timed lyrics, 3 lines (LRCLIB 1/3 answered)");
    expect(spotify, C::volume, V::pass, "one process route (pid 4100), level 100%");
    expect(spotify, C::command, V::pass, "next completed; Event Log playbackAction action=next, source=spotify");
    expect(spotify, C::service, V::pass);
    check(spotify.verdict() == R::pass, "Spotify passes every check");

    const auto& netease = row(b, "neteaseDesktop");
    expect(netease, C::source, V::pass, "Event Log source netease");
    expect(netease, C::timeline, V::gap, "the player publishes no duration or position");
    expect(netease, C::progress, V::unverified);
    expect(netease, C::playback, V::unverified, "only playing observed: pause and resume in the player");
    expect(netease, C::trackChange, V::unverified, "one track observed: change track in the player");
    expect(netease, C::controls, V::pass, "toggle previous next");
    expect(netease, C::seek, V::gap);
    expect(netease, C::artwork, V::pass, "NetEase catalog cover decoded at 96x96");
    expect(netease, C::lyrics, V::pass, "downloaded timed lyrics, 2 lines (NetEase catalog 2/2, catalog cover 1/1 answered)");
    expect(netease, C::volume, V::gap, "2 processes match; the unavailable submenu is shown (no single adjustable process)");
    expect(netease, C::command, V::unverified, "the command went to Spotify (desktop); rerun while this player is current");
    check(netease.verdict() == R::partial, "NetEase stays partial until play/pause and track changes are seen");

    const auto& qq = row(b, "qqMusic");
    check(qq.application == "QQMusic.exe", "The QQ Music directory is not stored");
    expect(qq, C::metadata, V::pass, "title, artist, no album");
    expect(qq, C::timeline, V::pass, "duration 4:00 with position");
    expect(qq, C::controls, V::pass, "play pause");
    expect(qq, C::seek, V::gap, "the player does not allow seeking; the seek rail is disabled, as in the source");
    expect(qq, C::artwork, V::fail, "1 player thumbnail(s) could not be decoded");
    expect(qq, C::lyrics, V::unverified, "no timed lyrics found for 1 track(s) (LRCLIB 0/2 answered)");
    expect(qq, C::volume, V::fail, "no per-app audio session of QQMusic.exe on the default output while it was playing");
    check(qq.verdict() == R::fail, "QQ Music fails on its undecodable cover and missing audio route");

    const auto& chrome = row(b, "chrome");
    expect(chrome, C::metadata, V::pass, "title, no artist, no album");
    expect(chrome, C::controls, V::pass, "play pause toggle");
    expect(chrome, C::seek, V::pass);
    expect(chrome, C::artwork, V::gap, "the player publishes no thumbnail; the outlined placeholder is shown");
    expect(chrome, C::lyrics, V::unverified, "no timed lyrics found for 1 track(s) (no lookup was possible; title and artist are needed)");
    expect(chrome, C::volume, V::gap, "no single process carries this identity (platform gap); the unavailable submenu is shown");
    check(chrome.verdict() == R::partial, "The browser is partial with its recorded volume gap");

    const auto& kugou = row(b, "kugou");
    expect(kugou, C::playback, V::unverified, "only paused observed: resume playback in the player");
    expect(kugou, C::artwork, V::pass, "player thumbnail decoded at 96x96");
    expect(kugou, C::lyrics, V::pass, "embedded timed lyrics, 2 lines");
    expect(kugou, C::volume, V::pass, "one process route (pid 4300), level 50%");
    expect(kugou, C::service, V::fail, "media-session failure: accessDenied");
    check(kugou.verdict() == R::fail, "A media-session failure while current fails the row");

    const auto& other = row(b, "Replay.Unknown_1a2b3c!App");
    expect(other, C::session, V::pass);
    expect(other, C::source, V::pass, "Event Log source system (not one of the source players)");
    expect(other, C::current, V::unverified, "listed but never the current session: play a track in it");
    check(other.verdict() == R::unverified && other.label == "Replay.Unknown_1a2b3c!App", "A listed-only player stays unverified");

    const auto& apple = row(b, "appleMusic");
    expect(apple, C::session, V::unverified, "not running during this run: start Apple Music and play a track");
    check(apple.application == "AppleInc.AppleMusicWin_*" && apple.source == "music" && apple.verdict() == R::unverified, "An unseen player shows its identity hint");
    for (const auto& c : apple.checks) check(c.run == 0, "Unverified checks carry no run");

    // Event lines: the HUD click path and its Event Log record.
    const auto count = [&](std::string_view kind, std::string_view key = {}, std::string_view value = {}) {
        return std::count_if(events.begin(), events.end(), [&](const Json& e) {
            return e["event"].isString() && e["event"].string() == kind && (key.empty() || (e[key].isString() && e[key].string() == value));
        });
    };
    check(std::all_of(events.begin(), events.end(), [](const Json& e) { return e["t"].isNumber(); }), "Every event carries the probe clock");
    check(count("activated") == 1 && count("stopped") == 1, "One activation, one stop");
    check(count("commandSent", "player", "Spotify.exe") == 1 && count("playbackAction", "action", "next") == 1 && count("playbackAction", "source", "spotify") == 1,
        "One explicit command and its Event Log playbackAction");
    check(count("coverFailed", "player", "QQMusic.exe") == 1 && count("coverFailed") == 1 && count("cover", "origin", "catalog") == 1 &&
              count("cover", "origin", "player") == 2,
        "Cover events per origin");
    check(count("lyrics", "origin", "embedded") == 1 && count("lyrics", "origin", "downloaded") == 2, "Lyric events per origin");

    // Without options: only what needs no network, audio or command.
    const auto a = t::nowPlayingReplay(source, {}, decoder());
    expect(row(a, "spotifyDesktop"), C::progress, V::pass, "2 playing timeline update pair(s), max drift 0.00 s");
    expect(row(a, "spotifyDesktop"), C::lyrics, V::unverified, "no embedded timed lyrics: run with --lyrics for the NetEase catalog and LRCLIB");
    expect(row(a, "spotifyDesktop"), C::volume, V::unverified, "run with --volume to read its per-app audio route");
    expect(row(a, "spotifyDesktop"), C::command, V::unverified, "run with --command playPause|previous|next|seek=SECONDS");
    expect(row(a, "neteaseDesktop"), C::artwork, V::unverified, "no player thumbnail: run with --lyrics for the NetEase catalog cover");
    expect(row(a, "kugou"), C::lyrics, V::pass, "embedded timed lyrics, 2 lines");
    check(row(a, "spotifyDesktop").verdict() == R::partial, "Spotify is partial without the optional checks");

    // A downloaded catalog cover that is not an image fails the cover check.
    auto badCover = source;
    {
        auto responses = badCover["responses"].array();
        for (auto& r : responses)
            if (r["prefix"].isString() && r["prefix"].string() == "https://p1.music.126.net/") r["body"] = "EndfieldHUD replay: not an image";
        badCover["responses"] = std::move(responses);
    }
    const auto undecodable = t::nowPlayingReplay(badCover, full(), decoder());
    expect(row(undecodable, "neteaseDesktop"), C::artwork, V::fail, "the downloaded NetEase catalog cover could not be decoded");
    expect(row(undecodable, "neteaseDesktop"), C::lyrics, V::pass);
    expect(row(undecodable, "spotifyDesktop"), C::artwork, V::pass, "player thumbnail decoded at 96x96");

    // A refused command: the replay's player reports a failure.
    auto failing = source;
    failing["commandFailure"] = "accessDenied";
    const auto refused = t::nowPlayingReplay(failing, full(), decoder());
    expect(row(refused, "spotifyDesktop"), C::command, V::fail, "next failed: accessDenied");
    // A command the player does not allow is a recorded gap, never sent.
    auto seekOnly = full();
    seekOnly.command = m::NowPlayingCommand{m::NowPlayingCommandKind::seek, 30};
    std::vector<Json> seekEvents;
    auto blocked = source;
    {
        auto steps = blocked["steps"].array();
        for (auto& step : steps)
            if (step["current"].isString() && step["current"].string() == "Spotify.exe") step["capabilities"] = Json::Array{Json("toggle"), Json("next")};
        blocked["steps"] = std::move(steps);
    }
    const auto unsupported = t::nowPlayingReplay(blocked, seekOnly, decoder(), [&](Json e) { seekEvents.push_back(std::move(e)); });
    expect(row(unsupported, "spotifyDesktop"), C::command, V::gap, "the player does not allow seek=30");
    check(std::none_of(seekEvents.begin(), seekEvents.end(), [](const Json& e) { return e["event"].string() == "commandSent"; }), "An unsupported command is never sent");

    // Reports: JSON round trip, merging, Markdown and privacy.
    auto merged = t::nowPlayingMergeMatrix({}, a);
    merged = t::nowPlayingMergeMatrix(std::move(merged), b);
    check(merged.runs.size() == 2 && merged.runs[0].run == 1 && merged.runs[1].run == 2, "Runs are numbered in order");
    expect(row(merged, "spotifyDesktop"), C::lyrics, V::pass);
    check(row(merged, "spotifyDesktop")[C::lyrics].run == 2 && row(merged, "spotifyDesktop")[C::progress].run == 2, "Decisive checks carry the run that produced them");
    check(row(merged, "spotifyDesktop").verdict() == R::pass && row(merged, "neteaseDesktop")[C::artwork].verdict == V::pass, "Evidence accumulates across runs");
    merged = t::nowPlayingMergeMatrix(std::move(merged), a);
    check(merged.runs.size() == 3, "Third run appended");
    expect(row(merged, "spotifyDesktop"), C::lyrics, V::pass, "downloaded timed lyrics, 3 lines (LRCLIB 1/3 answered)");
    check(row(merged, "spotifyDesktop")[C::lyrics].run == 2 && row(merged, "spotifyDesktop")[C::command].run == 2, "An unverified check never erases evidence");
    expect(row(merged, "spotifyDesktop"), C::progress, V::pass, "2 playing timeline update pair(s), max drift 0.00 s");
    check(row(merged, "spotifyDesktop")[C::progress].run == 3, "A decisive check is replaced by the newest run");
    check(row(merged, "spotifyDesktop").verdict() == R::pass, "Spotify keeps its full pass");

    const auto json = t::nowPlayingMatrixJson(merged);
    const auto text = json.encode();
    const auto parsed = t::nowPlayingMatrixFromJson(Json::parse(text));
    check(parsed && *parsed == merged, "The report round-trips");
    const auto markdown = t::nowPlayingMatrixMarkdown(merged);
    for (const auto secret : {"Replay One", "Replay Two", "Replay Three", "Replay Four", "Replay Five", "Replay Six", "Replay Artist", "Replay Album",
             "Embedded line", "Catalog", "lrclib.net", "music.163.com", "C:\\\\", "C:\\", "4100:1"}) {
        check(!contains(text, secret), std::string("The report stores no '") + secret + "'");
        check(!contains(markdown, secret), std::string("The Markdown stores no '") + secret + "'");
    }
    check(contains(markdown, "# Now Playing acceptance matrix") && contains(markdown, "| Spotify (desktop) | Spotify.exe | spotify | pass |") &&
              contains(markdown, "| QQ Music | QQMusic.exe | qqMusic | fail |") && contains(markdown, "| Apple Music | AppleInc.AppleMusicWin_* | music | unverified |") &&
              contains(markdown, "| Replay.Unknown_1a2b3c!App | Replay.Unknown_1a2b3c!App | system | unverified |") &&
              contains(markdown, "- volume: gap (run 2). 2 processes match") && contains(markdown, "| 3 | replay | 24 | none | - | none |"),
        "Markdown table, details and runs");

    // Strict parsing: anything but this report format and version is refused.
    const auto broken = [&](auto edit) {
        auto value = Json::parse(text);
        edit(value);
        return !t::nowPlayingMatrixFromJson(value);
    };
    check(broken([](Json& v) { v["format"] = "other"; }) && broken([](Json& v) { v["version"] = 2; }), "Foreign format or version");
    check(broken([](Json& v) { auto rows = v["rows"].array(); rows[0]["player"] = "bogus"; v["rows"] = std::move(rows); }), "Unknown player key");
    check(broken([](Json& v) { auto rows = v["rows"].array(); rows.push_back(rows[0]); v["rows"] = std::move(rows); }), "Duplicate row");
    check(broken([](Json& v) { auto rows = v["rows"].array(); rows[0]["checks"]["seek"]["verdict"] = "maybe"; v["rows"] = std::move(rows); }), "Unknown verdict");
    check(broken([](Json& v) { auto rows = v["rows"].array(); rows[0]["checks"]["seek"]["run"] = 9; v["rows"] = std::move(rows); }), "A check from a future run");
    check(broken([](Json& v) { auto rows = v["rows"].array(); rows[0]["checks"].erase("lyrics"); v["rows"] = std::move(rows); }), "A missing check");
    check(broken([](Json& v) { auto runs = v["runs"].array(); runs[1]["run"] = 1; v["runs"] = std::move(runs); }), "Runs must increase");
    check(!t::nowPlayingMatrixFromJson(Json::Array{}) && !t::nowPlayingMatrixFromJson(Json("matrix")), "Non-objects");
}

// The progress rules on small synthetic scenarios: re-reads of one timeline
// update carry no evidence, a player's chain restarts after another player's
// turn, a player that never re-publishes its timeline is a recorded gap and a
// re-published stale position fails.
void progressRules() {
    const auto run = [](std::string_view steps) {
        return t::nowPlayingReplay(Json::parse(std::string(R"({"format":"endfieldhud.nowPlaying.replay","version":1,"steps":[)") + std::string(steps) + "]}"),
            {}, decoder());
    };
    const std::string chrome = R"("sessions":["Chrome"],"current":"Chrome","capabilities":["play","pause","toggle"])";
    // 0 s and 6 s are distinct updates; the 3 s read extrapolates the 0 s one.
    const auto reread = run("{\"at\":0," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":100,"playing":true}},)"
                            "{\"at\":3," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":103,"playing":true,"updatedAt":0}},)"
                            "{\"at\":6," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":106,"playing":true}})");
    expect(row(reread, "chrome"), C::progress, V::pass, "1 playing timeline update pair(s), max drift 0.00 s");

    // A new update that repeats the old position while playing: the HUD's
    // progress would jump back 10 s.
    const auto stale = run("{\"at\":0," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":100,"playing":true}},)"
                           "{\"at\":10," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":100,"playing":true}})");
    expect(row(stale, "chrome"), C::progress, V::fail, "the timeline drifted 10.0 s from the playing clock over 1 pair(s); a seek inside the player also does this");
    check(row(stale, "chrome").verdict() == R::fail, "A drifting timeline fails the row");

    // Paused after 40 s of playback without any timeline update.
    const auto quiet = run("{\"at\":0," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":100,"playing":true}},)"
                           "{\"at\":40," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":140,"playing":false}})");
    expect(row(quiet, "chrome"), C::progress, V::gap,
        "no timeline update during 40 s of playback: the player publishes its timeline only when playback changes; the HUD advances the progress "
        "from the last sample, as in the source");
    // Still playing, with no new update, when the run ends after 35 s.
    const auto ending = run("{\"at\":0," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":100,"playing":true}},)"
                            "{\"at\":35," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":100,"playing":true,"sampledAt":0}})");
    expect(row(ending, "chrome"), C::progress, V::gap);
    check(row(ending, "chrome")[C::progress].detail.starts_with("no timeline update during 35 s of playback"), "The run end closes the quiet span");
    // Too short to tell.
    const auto brief = run("{\"at\":0," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":100,"playing":true}},)"
                           "{\"at\":20," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":120,"playing":false}})");
    expect(row(brief, "chrome"), C::progress, V::unverified, "no second timeline update at least 1 s into playback: let it play longer without pausing or seeking");

    // Spotify pauses at 12 s while Chrome is current (unseen); its chain
    // restarts when it returns, so no false drift is reported.
    const std::string both = R"("sessions":["Spotify.exe","Chrome"],"capabilities":["play","pause","toggle"])";
    const auto turns = run("{\"at\":0," + both + R"(,"current":"Spotify.exe","track":{"title":"One","duration":200,"position":10,"playing":true}},)"
                           "{\"at\":5," + both + R"(,"current":"Chrome","track":{"title":"Two","duration":300,"position":50,"playing":true}},)"
                           "{\"at\":20," + both + R"(,"current":"Spotify.exe","track":{"title":"One","duration":200,"position":12,"playing":true}},)"
                           "{\"at\":22," + both + R"(,"current":"Spotify.exe","track":{"title":"One","duration":200,"position":14,"playing":true}})");
    expect(row(turns, "spotifyDesktop"), C::progress, V::pass, "1 playing timeline update pair(s), max drift 0.00 s");
    expect(row(turns, "chrome"), C::progress, V::unverified);

    bool refused{};
    try {
        (void)run("{\"at\":0," + chrome + R"(,"track":{"title":"Clock","duration":600,"position":100,"playing":true,"updatedAt":1}})");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    check(refused, "A timeline update after its read is a malformed scenario");
}

void bounds() {
    // Other players are bounded; the most recently confirmed ones stay.
    t::NowPlayingAcceptanceMatrix stored;
    for (unsigned run = 0; run < 3; ++run) {
        t::NowPlayingAcceptanceMatrix next;
        next.runs.push_back({1, "replay", "", 1, false, false, "", ""});
        for (unsigned k = 0; k < 30; ++k) {
            t::NowPlayingAcceptanceRow r;
            r.player = "other";
            r.application = r.label = "Player" + std::to_string(run * 30 + k) + ".exe";
            r.source = "system";
            r[C::session] = {V::pass, "listed", 1};
            next.rows.push_back(std::move(r));
        }
        stored = t::nowPlayingMergeMatrix(std::move(stored), next);
    }
    check(stored.rows.size() == t::nowPlayingMaximumOtherPlayers && stored.find("Player89.exe") && !stored.find("Player0.exe") && stored.find("player26.EXE"),
        "At most 64 other players, oldest dropped, identities compared case-insensitively");
    t::NowPlayingAcceptanceMatrix runs;
    for (unsigned k = 0; k < 40; ++k) {
        t::NowPlayingAcceptanceMatrix next;
        next.runs.push_back({1, "replay", "", 1, false, false, "", ""});
        runs = t::nowPlayingMergeMatrix(std::move(runs), next);
    }
    check(runs.runs.size() == t::nowPlayingMaximumReportRuns && runs.runs.front().run == 9 && runs.runs.back().run == 40, "At most 32 runs are kept");
}

void files(const char* fixture) {
    std::mt19937_64 random(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = fs::temp_directory_path() / ("endfieldhud-now-playing-acceptance-" + std::to_string(random()));
    fs::create_directories(root);
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{root};
    const auto report = root / "matrix.json";
    check(!t::nowPlayingReadMatrix(report), "A missing report starts empty");
    const auto run = t::nowPlayingReplay(scenario(fixture), full(), decoder());
    t::nowPlayingWriteReport(report, t::nowPlayingMatrixJson(t::nowPlayingMergeMatrix({}, run)).encode() + "\n");
    t::nowPlayingWriteReport(report, t::nowPlayingMatrixJson(t::nowPlayingMergeMatrix(*t::nowPlayingReadMatrix(report), run)).encode() + "\n");
    const auto stored = t::nowPlayingReadMatrix(report);
    check(stored && stored->runs.size() == 2 && !fs::exists(root / "matrix.json.tmp"), "A report is replaced in place and merges");
    const auto foreign = root / "notes.json";
    {
        std::ofstream out(foreign, std::ios::binary);
        out << "{\"other\":1}";
    }
    bool refused{};
    try {
        (void)t::nowPlayingReadMatrix(foreign);
    } catch (const std::runtime_error&) {
        refused = true;
    }
    std::ifstream in(foreign, std::ios::binary);
    const std::string kept((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(refused && kept == "{\"other\":1}", "A foreign file is refused and left unchanged");
    bool directory{};
    try {
        (void)t::nowPlayingReadMatrix(root);
    } catch (const std::runtime_error&) {
        directory = true;
    }
    check(directory, "A directory is not a report");
}
// A window-title player (NetEase Cloud Music without a media session,
// native/now_playing_players.hpp) through the same owner: its timeline,
// progress and seek are recorded gaps, its playback is inferred, and a
// posted command counts only when the player's title follows it.
void windowPlayers(const char* fixture) {
    const auto source = scenario(fixture);
    std::vector<Json> events;
    const auto w = t::nowPlayingReplay(source, full(), decoder(), [&](Json e) { events.push_back(std::move(e)); });
    const auto& ne = row(w, "neteaseDesktop");
    check(ne.application == "cloudmusic.exe" && ne.source == "netease" && ne.label == "NetEase Cloud Music (desktop)", "The window-title player keeps only its image name");
    expect(ne, C::session, V::pass, "found by its main window title as cloudmusic.exe (it publishes no media session)");
    expect(ne, C::current, V::pass, "presented as the current session");
    expect(ne, C::source, V::pass, "Event Log source netease");
    expect(ne, C::metadata, V::pass, "title, artist, no album");
    expect(ne, C::timeline, V::gap, "a window title carries no duration or position");
    expect(ne, C::progress, V::gap, "no timeline: the HUD shows no progress for a window-title player");
    expect(ne, C::playback, V::pass, "followed playing and paused (from its window title, audio activity and HUD commands)");
    expect(ne, C::trackChange, V::pass, "2 tracks followed");
    expect(ne, C::controls, V::pass, "toggle previous next");
    expect(ne, C::seek, V::gap, "a window-title player cannot seek; the seek rail is disabled");
    expect(ne, C::artwork, V::pass, "NetEase catalog cover decoded at 96x96");
    expect(ne, C::lyrics, V::pass);
    check(contains(ne[C::lyrics].detail, "downloaded timed lyrics, 2 lines (NetEase catalog"), "Window-title tracks use the NetEase catalog lyrics");
    expect(ne, C::volume, V::pass, "one process route (pid 4200), level 100%");
    expect(ne, C::command, V::pass, "next was delivered (WM_APPCOMMAND) and the player's title changed afterwards; Event Log playbackAction action=next, source=netease");
    expect(ne, C::service, V::pass);
    check(ne.verdict() == R::pass, "A window-title player passes with its recorded gaps");
    const auto& spotify = row(w, "spotifyDesktop");
    expect(spotify, C::session, V::pass, "listed by GSMTC as Spotify.exe");
    expect(spotify, C::current, V::unverified, "listed but never the current session: play a track in it");
    const auto snapshots = std::count_if(events.begin(), events.end(), [](const Json& e) {
        return e["event"].isString() && e["event"].string() == "snapshot" && e["origin"].isString() && e["origin"].string() == "playerWindow";
    });
    check(snapshots >= 3, "Snapshot events name the window-title origin");
    const auto markdown = t::nowPlayingMatrixMarkdown(w);
    check(contains(markdown, "| NetEase Cloud Music (desktop) | cloudmusic.exe | netease | pass |") && contains(markdown, "found by its main window title") &&
              !contains(markdown, "Window One") && !contains(markdown, "Replay Artist") && !contains(markdown, "C:\\Replay"),
          "The Markdown names the window-title evidence and stores no metadata or directory");

    // Play/pause cannot be observed in a window title: a recorded gap.
    auto toggle = full();
    toggle.command = m::NowPlayingCommand{m::NowPlayingCommandKind::playPause};
    expect(row(t::nowPlayingReplay(source, toggle, decoder()), "neteaseDesktop"), C::command, V::gap,
        "playPause was delivered (WM_APPCOMMAND); Event Log playbackAction action=playPause, source=netease; a window title does not report playback, so confirm it in the player");
    // A skip the title never followed stays unverified.
    auto still = source;
    {
        auto steps = still["steps"].array();
        steps.resize(2);
        still["steps"] = std::move(steps);
    }
    const auto ignored = t::nowPlayingReplay(still, full(), decoder());
    expect(row(ignored, "neteaseDesktop"), C::command, V::unverified,
        "next was delivered (WM_APPCOMMAND) but the player's title did not change afterwards: if it did not skip, the player ignores WM_APPCOMMAND");
    expect(row(ignored, "neteaseDesktop"), C::trackChange, V::unverified, "one track observed: change track in the player");
    // Playing only: the hint names the idle caption and the explicit toggle.
    auto playingOnly = source;
    {
        auto steps = playingOnly["steps"].array();
        steps.erase(steps.begin() + 1);
        playingOnly["steps"] = std::move(steps);
    }
    expect(row(t::nowPlayingReplay(playingOnly, {}, decoder()), "neteaseDesktop"), C::playback, V::unverified,
        "only playing observed: pause the player (its idle title or a silent audio session shows it), or run with --command playPause");
    // A silent player process shows the window-title track paused (--volume
    // reads the route's audio activity, as the HUD does); without --volume
    // the title inference stands.
    auto silent = playingOnly;
    {
        auto routes = silent["routes"].array();
        routes[0]["rendering"] = false;
        silent["routes"] = std::move(routes);
    }
    expect(row(t::nowPlayingReplay(silent, full(), decoder()), "neteaseDesktop"), C::playback, V::unverified, "only paused observed: play a track in the player");
    auto noAudio = full();
    noAudio.volume = false;
    expect(row(t::nowPlayingReplay(silent, noAudio, decoder()), "neteaseDesktop"), C::playback, V::unverified,
        "only playing observed: pause the player (its idle title or a silent audio session shows it), or run with --command playPause");
    // Without options nothing needs the network, an audio route or a command.
    const auto bare = t::nowPlayingReplay(source, {}, decoder());
    expect(row(bare, "neteaseDesktop"), C::artwork, V::unverified, "no player thumbnail: run with --lyrics for the NetEase catalog cover");
    expect(row(bare, "neteaseDesktop"), C::command, V::unverified, "run with --command playPause|previous|next|seek=SECONDS");
    // Only the two documented session origins are accepted.
    auto unknown = source;
    {
        auto steps = unknown["steps"].array();
        auto sessions = steps[0]["sessions"].array();
        sessions[1]["origin"] = "screenScrape";
        steps[0]["sessions"] = std::move(sessions);
        unknown["steps"] = std::move(steps);
    }
    bool refused{};
    try {
        (void)t::nowPlayingReplay(unknown, {}, decoder());
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    check(refused, "An unknown session origin is a malformed scenario");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::runtime_error("usage: now_playing_acceptance_tests <now-playing-acceptance-replay.json> <now-playing-acceptance-windows.json>");
        catalog();
        replay(argv[1]);
        windowPlayers(argv[2]);
        progressRules();
        bounds();
        files(argv[1]);
        std::cout << "now_playing_acceptance_contracts: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "now_playing_acceptance_contracts failed: " << e.what() << '\n';
        return 1;
    }
}
