// Window-title fallback for Win32 players without an SMTC session, and the
// composite provider beside GSMTC. Source: python3
// windows/tools/now_playing_players_reference.py (unchanged Mac
// NowPlayingAccessibility.track(heading:) split). Synthetic windows from
// NowPlayingWindowFixture and injected providers only: no real window,
// process, media session, message or user data is touched.
#include "native/now_playing_players.hpp"
#include "core/data/json.hpp"
#include "tools/now_playing_session.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace n = endfield::native;
namespace m = endfield::modules;
namespace t = endfield::tools;
using Json = ehud::data::Json;
using Q = endfield::app::UtilityExecutor;
using F = n::NowPlayingServiceFailure;

namespace {
unsigned checks{};
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
const std::string netease = "C:\\EndfieldHUD-Synthetic\\CloudMusic\\cloudmusic.exe";
const std::string qq = "C:\\EndfieldHUD-Synthetic\\QQMusic\\QQMusic.exe";
const std::string kugou = "C:\\EndfieldHUD-Synthetic\\KuGou\\KuGou.exe";
m::NowPlayingPlayerWindow window(std::uintptr_t handle, std::uint32_t pid, std::string executable, std::string className, std::string title,
                                 bool visible = true, bool owned = false) {
    return {handle, pid, std::move(executable), std::move(className), std::move(title), visible, owned};
}
const m::NowPlayingWindowPlayer& rules(std::string_view executable) {
    const auto* found = m::nowPlayingWindowPlayer(executable);
    if (!found) throw std::runtime_error("unknown synthetic player");
    return *found;
}

void oracle(const char* path) {
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto root = Json::parse(text);
    check(root["sourceCommit"].string() == "ca04f142185c7de40acd8523bdb563195d90a1d1", "Oracle names the pinned source commit");
    const auto& rows = root["rows"].array();
    check(rows.size() >= 30, "Oracle covers the source heading cases");
    for (const auto& row : rows) {
        const auto& heading = row["heading"].string();
        const auto parsed = m::nowPlayingHeadingTrack(heading);
        if (row["title"].isNull()) {
            check(!parsed, "No track for source heading: " + heading);
            continue;
        }
        check(parsed.has_value(), "Track for source heading: " + heading);
        // Source NowPlayingTrack bounds both strings to 512 Characters.
        m::NowPlayingTrack track;
        track.title = parsed->title;
        track.artist = parsed->artist;
        track = m::NowPlayingTrack::bounded(std::move(track));
        check(track.title == row["title"].string() && track.artist == row["artist"].string(), "Source split of heading: " + heading);
    }
}

void titles() {
    for (const auto& player : m::nowPlayingWindowPlayers())
        check(m::nowPlayingSourceForAppUserModelID(player.executable) == player.source && m::nowPlayingWindowPlayer(player.executable) == &player,
              "Each fallback player maps to its Event Log source by its image name");
    check(m::nowPlayingWindowPlayer("C:\\X\\CloudMusic.EXE") == &rules(netease) && !m::nowPlayingWindowPlayer("cloudmusic.exe.bak") &&
              !m::nowPlayingWindowPlayer("Spotify.exe") && !m::nowPlayingWindowPlayer(""), "Image names compare ASCII case-insensitively and exactly");
    const auto& ne = rules(netease);
    const auto track = [](const m::NowPlayingWindowPlayer& p, std::string_view title) { return m::nowPlayingWindowTitleTrack(p, title); };
    check(track(ne, "晴天 - 周杰伦") == m::NowPlayingTitleTrack{"晴天", "周杰伦"}, "NetEase main window title is Title - Artist");
    check(track(ne, "Song (Live) - A/B") == m::NowPlayingTitleTrack{"Song (Live)", "A/B"}, "NetEase joins several artists with a slash");
    check(track(ne, "Song - Remix - Artist") == m::NowPlayingTitleTrack{"Song - Remix", "Artist"}, "The last separator splits, as in the source");
    check(!track(ne, "网易云音乐") && !track(ne, " 网易云音乐 ") && !track(ne, "NetEase Cloud Music") && !track(ne, "") && !track(ne, " - "),
          "The idle caption and empty titles carry no track");
    check(track(ne, "Song - Artist - 网易云音乐") == m::NowPlayingTitleTrack{"Song", "Artist"}, "A trailing player name is not the artist");
    check(!track(ne, "Song -Artist") && !track(ne, std::string("Song - ") + "\xcc\x81" + "Artist"), "Separators are whole Characters, as in the source");
    check(!track(ne, std::string("bad \xff - utf8")), "Invalid UTF-8 never becomes a track");
    const auto& q = rules(qq);
    check(track(q, "晴天 - 周杰伦") == m::NowPlayingTitleTrack{"晴天", "周杰伦"} && !track(q, "QQ音乐 - 听我想听") && !track(q, "QQ音乐"),
          "QQ Music: Title - Artist; its own captions are not tracks");
    const auto& k = rules(kugou);
    check(track(k, "周杰伦 - 晴天 - 酷狗音乐") == m::NowPlayingTitleTrack{"晴天", "周杰伦"} && track(k, "A - B - C") == m::NowPlayingTitleTrack{"B - C", "A"} &&
              !track(k, "酷狗音乐"), "Kugou: Artist - Title split at the first separator");
    check(!rules(qq).confirmed && !rules(kugou).confirmed && rules(netease).confirmed, "Unconfirmed title grammars stay marked for live acceptance");
    // Other Win32 players without a media session (grammar unconfirmed).
    const auto& kuwo = rules("C:\\EndfieldHUD-Synthetic\\KwMusic.exe");
    check(track(kuwo, "Song - Singer") == m::NowPlayingTitleTrack{"Song", "Singer"} && !track(kuwo, "\xe9\x85\xb7\xe6\x88\x91\xe9\x9f\xb3\xe4\xb9\x90"),
          "Kuwo: Title - Artist; its idle caption is no track");
    const auto& foobar = rules("foobar2000.exe");
    check(track(foobar, "Artist - Song [foobar2000]") == m::NowPlayingTitleTrack{"Song", "Artist"} && track(foobar, "A - B - C") == m::NowPlayingTitleTrack{"B - C", "A"} &&
              !track(foobar, "foobar2000"), "foobar2000: Artist - Title with its [foobar2000] decoration removed");
    const auto& aimp = rules("AIMP.exe");
    check(track(aimp, "Artist - Song") == m::NowPlayingTitleTrack{"Song", "Artist"} && !track(aimp, "AIMP"), "AIMP: Artist - Title");
    const auto& pot = rules("PotPlayerMini64.exe");
    check(m::nowPlayingWindowPlayer("D:\\Daum\\PotPlayerMini.exe") == &pot && track(pot, "Concert.mkv - PotPlayer") == m::NowPlayingTitleTrack{"Concert.mkv", ""} &&
              track(pot, "A - B.mp3") == m::NowPlayingTitleTrack{"A - B.mp3", ""} && !track(pot, "PotPlayer"),
          "PotPlayer (both image names): the whole item caption is the title");
    const auto& vlc = rules("vlc.exe");
    check(track(vlc, "Song Name - VLC media player") == m::NowPlayingTitleTrack{"Song Name", ""} && !track(vlc, "VLC media player"),
          "VLC: the item title before its player name");
    for (const auto& player : m::nowPlayingWindowPlayers())
        check(player.confirmed == (player.key == "netease"), "Only the NetEase grammar is confirmed on a real install");
    check(m::nowPlayingSameExecutable("foobar2000.exe", "C:\\Apps\\foobar2000\\foobar2000.exe") &&
              m::nowPlayingSameExecutable("C:\\A\\FOOBAR2000.EXE", "D:/foobar2000.exe") &&
              !m::nowPlayingSameExecutable("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify", "C:\\x\\Spotify.exe") &&
              !m::nowPlayingSameExecutable("vlc", "vlc") && !m::nowPlayingSameExecutable("", "") && !m::nowPlayingSameExecutable("aimp.exe", "AIMP2.exe"),
          "A Win32 GSMTC identity names the same image as a process path by file name only");

    // Main window selection.
    const std::vector<m::NowPlayingPlayerWindow> withClass{
        window(1, 7, netease, "DesktopLyrics", "Lyric line - fragment", true, true), window(2, 7, netease, "OrpheusBrowserHost", "网易云音乐", false),
        window(3, 7, netease, "OrpheusBrowserHost", "Song - Artist", true)};
    check(m::nowPlayingMainWindow(ne, withClass) == 2u, "The main window class wins over other titled windows, track titles first");
    const std::vector<m::NowPlayingPlayerWindow> idle{window(1, 7, netease, "Popup", "Song - Artist", true, true), window(2, 7, netease, "OrpheusBrowserHost", "网易云音乐", false)};
    check(m::nowPlayingMainWindow(ne, idle) == 1u, "An idle main window is still the player's window");
    const std::vector<m::NowPlayingPlayerWindow> classless{window(1, 8, qq, "Popup", "Notice - Text", true, true), window(2, 8, qq, "Frame", "", true),
        window(3, 8, qq, "Frame", "晴天 - 周杰伦", false)};
    check(m::nowPlayingMainWindow(q, classless) == 2u, "Without a known class, unowned windows titled with a track win");
    const std::vector<m::NowPlayingPlayerWindow> owned{window(1, 8, qq, "Popup", "Notice - Text", true, true)};
    check(!m::nowPlayingMainWindow(q, owned) && !m::nowPlayingMainWindow(q, {}), "Owned popups alone are no player window");
}

// NetEase Cloud Music registers its global shortcuts, the media keys among
// them, with RegisterHotKey and ignores WM_APPCOMMAND (live RC 128): the media
// key reaches the hotkey's holder, never the foreground window.
void transport() {
    using T = m::NowPlayingWindowTransport;
    check(m::nowPlayingWindowTransport(true, true) == T::mediaKey, "A held media key with a single window-title player is its transport");
    check(m::nowPlayingWindowTransport(true, false) == T::appCommand && m::nowPlayingWindowTransport(false, true) == T::appCommand &&
              m::nowPlayingWindowTransport(false, false) == T::appCommand,
          "Several players (the holder is unknown) or an unheld key: WM_APPCOMMAND to the player's window");
    check(m::nowPlayingWindowTransportKey(T::mediaKey) == "mediaKey" && m::nowPlayingWindowTransportKey(T::appCommand) == "appCommand", "Stable transport keys");
}
void timeline() {
    m::NowPlayingWindowTimeline t;
    check(!t.position(10), "Without an observed start there is no position");
    t.restart(100.);
    check(t.position(130) == 30. && t.position(90) == 0., "Elapsed since the observed title change (never negative)");
    t.observe(true, 130);
    t.observe(false, 140);
    check(t.position(150) == 40. && t.position(145) == 40., "A pause freezes the elapsed time where it began");
    t.observe(false, 160);
    check(t.position(170) == 40., "A continuing pause keeps its first moment");
    t.observe(true, 170);
    check(t.position(180) == 50., "Resuming subtracts the whole pause");
    t.observe(true, std::numeric_limits<double>::quiet_NaN());
    check(t.position(180) == 50., "A non-finite observation changes nothing");
    t.restart(std::nullopt);
    check(!t.position(200) && t.pausedTotal == 0 && !t.pausedSince, "A track whose change was not observed has no position and no pause history");
    t.restart(std::numeric_limits<double>::infinity());
    check(!t.position(200), "A non-finite start is unknown");
    t.restart(100.);
    t.observe(false, 90);
    check(t.position(120) == 0., "A pause seen before the start pauses at the start");
}
void activity() {
    n::NowPlayingProcessActivity a;
    const auto route = [](std::uint32_t pid, std::optional<bool> rendering, std::string id) {
        m::NowPlayingAudioRoute r;
        r.id = std::move(id);
        r.pid = pid;
        r.available = true;
        r.rendering = rendering;
        return r;
    };
    check(!a.rendering(7) && a.revision() == 0, "Nothing is known before a publication");
    std::vector<m::NowPlayingAudioRoute> routes{route(7, false, "7:1"), route(7, true, "7:2"), route(8, false, "8:1"), route(9, std::nullopt, "9:1"), route(0, true, "0:1")};
    check(a.update(routes) && a.rendering(7) == true && a.rendering(8) == false && !a.rendering(9) && !a.rendering(0) && a.revision() == 1,
          "Any rendering route makes its process render; uncaptured routes and PID 0 stay unknown");
    check(!a.update(routes) && a.revision() == 1, "An unchanged publication changes nothing");
    routes[1].rendering = false;
    check(a.update(routes) && a.rendering(7) == false && a.revision() == 2, "Its last rendering route going silent pauses the process");
    std::vector<m::NowPlayingAudioRoute> many;
    for (std::uint32_t pid = 1; pid <= 600; ++pid) many.push_back(route(pid, true, std::to_string(pid)));
    check(a.update(many) && a.rendering(512) == true && !a.rendering(513), "At most 512 processes are kept");
    check(a.update({}) && !a.rendering(7), "An empty publication forgets every process");
}

struct Provider final : n::NowPlayingProvider {
    Changed changed;
    Ready started, command;
    Read reading;
    unsigned starts{}, reads{}, commands{}, stops{};
    std::uint64_t target{};
    m::NowPlayingCommand request;
    void start(Changed c, Ready r) override { ++starts; changed = std::move(c); started = std::move(r); }
    void read(Read r) override { ++reads; reading = std::move(r); }
    void perform(std::uint64_t session, std::uint64_t, m::NowPlayingCommand value, Ready r) override { ++commands; target = session; request = value; command = std::move(r); }
    void stop() noexcept override { ++stops; }
};
n::NowPlayingReadResult media(std::string identity, bool playing, bool withTrack = true, std::uint64_t token = 1) {
    n::NowPlayingReadResult r;
    r.value.sessions = {{token, identity, identity}};
    r.value.session = r.value.sessions.front();
    if (withTrack) {
        m::NowPlayingTrack track;
        track.title = "Media Song";
        track.artist = "Media Artist";
        track.duration = 200;
        track.position = 10;
        track.isPlaying = playing;
        track.sampledAt = 1;
        r.value.track = track;
    }
    r.value.capabilities = {true, true, true, true, true, true};
    r.value.metadataRevision = 9;
    return r;
}
struct Direct {
    std::shared_ptr<n::NowPlayingWindowFixture> desktop = std::make_shared<n::NowPlayingWindowFixture>();
    std::shared_ptr<n::NowPlayingProcessActivity> activity = std::make_shared<n::NowPlayingProcessActivity>();
    double now = 5;
    unsigned changes{};
    std::shared_ptr<n::NowPlayingProvider> provider;
    Direct() {
        provider = n::nowPlayingWindowProvider([d = desktop] { return d; }, [this] { return now; }, activity)();
        bool ready{};
        provider->start([this] { ++changes; }, [&](n::NowPlayingCommandResult r) { ready = r.failure == F::none; });
        check(ready, "The window-title provider is ready at once");
    }
    n::NowPlayingReadResult read() {
        std::optional<n::NowPlayingReadResult> out;
        provider->read([&](n::NowPlayingReadResult r) { out = std::move(r); });
        check(out.has_value(), "Each read completes once");
        return std::move(*out);
    }
    F perform(std::uint64_t token, m::NowPlayingCommandKind kind) {
        std::optional<F> out;
        provider->perform(token, 1, {kind, 0}, [&](n::NowPlayingCommandResult r) { out = r.failure; });
        check(out.has_value(), "Each command completes once");
        return *out;
    }
};

void provider() {
    Direct d;
    check(d.desktop->watchers() == 1, "The provider subscribes to window events when it is created (the owner thread)");
    auto empty = d.read();
    check(empty.failure == F::none && empty.value.sessions.empty() && !empty.value.session && !empty.value.track, "No fallback player: an empty snapshot");
    d.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "晴天 - 周杰伦"), window(11, 99, "C:\\Other\\notepad.exe", "Notepad", "A - B")});
    d.desktop->notify();
    check(d.changes == 1, "A window event asks the service for one read");
    auto first = d.read();
    check(first.value.sessions.size() == 1 && first.value.session && first.value.session->appUserModelID == netease &&
              first.value.session->origin == m::NowPlayingSessionOrigin::playerWindow && first.value.session->token == first.value.sessions[0].token,
          "One session per player process, identified by its image path");
    const auto token = first.value.session->token;
    const auto& track = *first.value.track;
    check(track.title == "晴天" && track.artist == "周杰伦" && track.album.empty() && track.isPlaying && !track.duration && !track.position &&
              !track.supportsSeeking && track.sampledAt == 5 && !first.value.timeline && !first.value.artwork, "Title and artist only; a new track title is playing");
    const auto& c = first.value.capabilities;
    check(c.toggle && c.previous && c.next && !c.play && !c.pause && !c.seek && !c.volume, "Toggle, previous and next; no seek");
    check(m::nowPlayingSourceForAppUserModelID(first.value.session->appUserModelID) == m::NowPlayingSource::netease, "The identity maps to the NetEase source");
    const auto revision = first.value.metadataRevision;

    d.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "网易云音乐")});
    auto paused = d.read();
    check(paused.value.track && paused.value.track->title == "晴天" && !paused.value.track->isPlaying && paused.value.metadataRevision == revision,
          "The idle caption keeps the last track as paused");
    d.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "晴天 - 周杰伦")});
    auto resumed = d.read();
    check(resumed.value.track->isPlaying && resumed.value.metadataRevision == revision && resumed.value.session->token == token, "The title returning means playing again");

    check(d.perform(token, m::NowPlayingCommandKind::playPause) == F::none, "Play/pause is delivered to the player window");
    const auto sent = d.desktop->commands();
    check(sent.size() == 1 && sent[0].window == 10 && sent[0].pid == 7 && sent[0].kind == m::NowPlayingCommandKind::playPause && sent[0].onlyPlayer,
          "The command targets that window and process, the only player running (its media-key hotkey may carry it)");
    check(!d.read().value.track->isPlaying, "A delivered toggle flips the inferred state (the title does not report pausing)");
    check(d.perform(token, m::NowPlayingCommandKind::playPause) == F::none && d.read().value.track->isPlaying, "A second toggle flips it back");
    check(d.perform(token, m::NowPlayingCommandKind::seek) == F::unsupported && d.perform(token + 5, m::NowPlayingCommandKind::next) == F::stale,
          "Seek is unsupported; an unknown session is stale");
    d.desktop->refuseCommands(true);
    check(d.perform(token, m::NowPlayingCommandKind::next) == F::unavailable && d.read().value.track->isPlaying, "An undeliverable command fails and changes nothing");
    d.desktop->refuseCommands(false);
    check(d.perform(token, m::NowPlayingCommandKind::next) == F::none, "Next is delivered");
    d.desktop->set({window(12, 7, netease, "OrpheusBrowserHost", "Next Song - Next Artist")});
    auto next = d.read();
    check(next.value.track->title == "Next Song" && next.value.track->isPlaying && next.value.metadataRevision == revision + 1 && next.value.session->token == token,
          "A new track title is a new metadata revision of the same process session (even in another main window)");

    // Several players: playing beats paused beats idle; the current one keeps ties.
    d.desktop->set({window(12, 7, netease, "OrpheusBrowserHost", "网易云音乐"), window(20, 8, qq, "QQMusic_Daemon_Wnd", "QQ Song - QQ Artist"),
        window(30, 9, kugou, "kugou_ui", "酷狗音乐")});
    auto several = d.read();
    check(several.value.sessions.size() == 3 && several.value.session->appUserModelID == qq && several.value.track->title == "QQ Song" &&
              several.value.track->isPlaying, "The playing player is current among several");
    d.desktop->set({window(12, 7, netease, "OrpheusBrowserHost", "Next Song - Next Artist"), window(20, 8, qq, "QQMusic_Daemon_Wnd", "QQ Song - QQ Artist")});
    auto tie = d.read();
    check(tie.value.session->appUserModelID == qq, "Two playing players: the current one stays current");
    check(d.perform(tie.value.session->token, m::NowPlayingCommandKind::next) == F::none && !d.desktop->commands().back().onlyPlayer,
          "With several players running the command is never a media key (its holder is unknown)");
    // The per-app audio publication says which player renders audio.
    d.activity->update(std::vector<m::NowPlayingAudioRoute>{{"7:1", 7, netease, true, m::NowPlayingAudioRouteState::direct, {}, {}, true},
        {"8:1", 8, qq, true, m::NowPlayingAudioRouteState::direct, {}, {}, false}});
    auto audible = d.read();
    check(audible.value.session->appUserModelID == netease && audible.value.track->isPlaying && audible.value.sessions.size() == 2,
          "The player whose process renders audio is current; a silent one is paused");
    d.activity->update(std::vector<m::NowPlayingAudioRoute>{});
    d.desktop->set({window(12, 7, netease, "OrpheusBrowserHost", "Next Song - Next Artist")});
    auto gone = d.read();
    const auto qqToken = several.value.sessions[1].token;
    check(gone.value.sessions.size() == 1 && gone.value.session->appUserModelID == netease && d.perform(qqToken, m::NowPlayingCommandKind::next) == F::stale,
          "An exited player leaves the list and its commands are stale");
    std::vector<m::NowPlayingPlayerWindow> crowd;
    for (std::uint32_t k = 0; k < 70; ++k) crowd.push_back(window(100 + k, 1000 + k, netease, "OrpheusBrowserHost", "Song - Artist"));
    d.desktop->set(crowd);
    check(d.read().value.sessions.size() == n::nowPlayingMaximumPlayerProcesses, "At most 16 player processes are listed");

    d.provider->stop();
    check(d.desktop->watchers() == 0, "Stop removes the window subscription");
    const auto changes = d.changes;
    d.desktop->notify();
    bool late{};
    d.provider->read([&](n::NowPlayingReadResult) { late = true; });
    d.provider->perform(token, 1, {m::NowPlayingCommandKind::next, 0}, [&](n::NowPlayingCommandResult) { late = true; });
    check(d.changes == changes && !late, "A stopped provider reports nothing");
}

// A window title carries no position: the elapsed time is estimated from the
// observed title change to the track and the pauses seen since.
void estimate() {
    Direct d;
    auto w = window(10, 7, netease, "OrpheusBrowserHost", "Song - Artist");
    w.titleChangedAt = 2;
    d.desktop->set({w});
    auto first = d.read();
    check(first.value.track->position == 3. && first.value.track->sampledAt == 5 && first.value.track->isPlaying && !first.value.track->duration,
          "The track began at its observed title change; it plays on from there");
    d.activity->update(std::vector<m::NowPlayingAudioRoute>{{"7:1", 7, netease, true, m::NowPlayingAudioRouteState::direct, {}, {}, false}});
    d.now = 8;
    auto paused = d.read();
    check(!paused.value.track->isPlaying && paused.value.track->position == 6. && paused.value.metadataRevision == first.value.metadataRevision,
          "Its process going silent pauses the estimate");
    d.now = 20;
    check(d.read().value.track->position == 6., "The estimate stays frozen while the process is silent");
    d.activity->update(std::vector<m::NowPlayingAudioRoute>{{"7:1", 7, netease, true, m::NowPlayingAudioRouteState::direct, {}, {}, true}});
    d.now = 21;
    check(d.read().value.track->position == 6., "Resuming continues from the frozen time");
    d.now = 25;
    const auto resumed = d.read();
    check(resumed.value.track->position == 10. && resumed.value.track->isPlaying, "The pause is subtracted afterwards");
    auto next = window(10, 7, netease, "OrpheusBrowserHost", "Next - Artist");
    next.titleChangedAt = 30;
    d.desktop->set({next});
    check(!d.read().value.track->position, "A title change reported after the read time is not trusted");
    d.now = 31;
    auto later = d.read();
    check(!later.value.track->position, "The estimate starts only with a track change the read itself saw dated");
    auto third = window(10, 7, netease, "OrpheusBrowserHost", "Third - Artist");
    third.titleChangedAt = 30.5;
    d.desktop->set({third});
    const auto dated = d.read();
    check(dated.value.track->title == "Third" && dated.value.track->position == .5, "A dated title change restarts the estimate");
    auto undated = window(10, 7, netease, "OrpheusBrowserHost", "Fourth - Artist");
    d.desktop->set({undated});
    check(!d.read().value.track->position, "An undated title change (nothing observed it) has no position");
}

// The player's own controls (its accessibility tree, the Mac
// NowPlayingAccessibility read): elapsed and total time, play state and
// transport controls, resynchronized on every change.
void controls() {
    Direct d;
    const auto at = [&] { return d.now; };
    n::NowPlayingPlayerReading playing{83., 296., true, true, true, true, true};
    d.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "Song - Artist")});
    d.desktop->setReading(7, playing, at);
    auto first = d.read();
    check(d.desktop->refreshes() == 1 && first.value.track->position == 83. && first.value.track->duration == 296. && first.value.track->isPlaying &&
              first.value.track->sampledAt == 5, "A player first seen is read: its own elapsed and total time and play state");
    d.now = 10;
    auto later = d.read();
    check(d.desktop->refreshes() == 1 && later.value.track->position == 88. && later.value.track->duration == 296.,
          "Without a change no new reading is asked; the time runs on from the player's clock");
    // A pause inside the player: its audio stops, which asks its controls again.
    d.desktop->setReading(7, n::NowPlayingPlayerReading{90., 296., false, true, true, true, true}, at);
    d.activity->update(std::vector<m::NowPlayingAudioRoute>{{"7:1", 7, netease, true, m::NowPlayingAudioRouteState::direct, {}, {}, false}});
    d.now = 12;
    auto paused = d.read();
    check(d.desktop->refreshes() == 2 && !paused.value.track->isPlaying && paused.value.track->position == 90., "A change of its audio activity resynchronizes the time");
    d.now = 20;
    check(d.read().value.track->position == 90., "Paused, the time stays");
    // The next track: its title change resynchronizes before anything else.
    d.desktop->setReading(7, n::NowPlayingPlayerReading{1., 200., true, true, true, true, true}, at);
    d.activity->update(std::vector<m::NowPlayingAudioRoute>{{"7:1", 7, netease, true, m::NowPlayingAudioRouteState::direct, {}, {}, true}});
    d.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "Next - Artist")});
    d.now = 25;
    auto next = d.read();
    check(d.desktop->refreshes() == 3 && next.value.track->title == "Next" && next.value.track->position == 1. && next.value.track->duration == 200. &&
              next.value.track->isPlaying, "A new title reads the new track's time");
    // A reading begun before the track changed describes the previous track.
    n::NowPlayingPlayerReading stale{50., 300., true, true, true, true, true, 24., 24.};
    d.desktop->setReading(7, stale);
    d.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "Third - Artist")});
    d.now = 30;
    auto third = d.read();
    check(third.value.track->title == "Third" && !third.value.track->position && !third.value.track->duration, "An older reading never dates a newer track");
    // A minimized or hidden player may stop updating its elapsed label.
    n::NowPlayingPlayerReading hidden{40., 180., true, true, true, true, false};
    d.desktop->setReading(7, hidden, at);
    d.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "Fourth - Artist")});
    d.now = 40;
    auto minimized = d.read();
    check(minimized.value.track->duration == 180. && !minimized.value.track->position, "A hidden window's elapsed label is not trusted (its total is)");

    // Transport through the player's own controls first.
    const auto token = minimized.value.session->token;
    const auto sent = d.desktop->commands().size();
    d.desktop->pressControls(true);
    check(d.perform(token, m::NowPlayingCommandKind::next) == F::none && d.desktop->presses().size() == 1 &&
              d.desktop->presses()[0].kind == m::NowPlayingCommandKind::next && d.desktop->presses()[0].pid == 7 && d.desktop->commands().size() == sent,
          "Next presses the player's own control: no key or window message");
    const auto refreshes = d.desktop->refreshes();
    d.read();
    check(d.desktop->refreshes() == refreshes + 1, "A delivered command resynchronizes the player's controls");
    d.desktop->pressControls(false);
    check(d.perform(token, m::NowPlayingCommandKind::previous) == F::none && d.desktop->commands().size() == sent + 1 &&
              d.desktop->commands().back().kind == m::NowPlayingCommandKind::previous, "Without its own control the media key or WM_APPCOMMAND carries it");
    check(d.perform(token, m::NowPlayingCommandKind::seek) == F::unsupported && d.desktop->presses().size() == 1, "Seek is never pressed");
}

void merge() {
    const auto fallback = [](bool playing, std::string title = "Window Song") {
        n::NowPlayingReadResult r;
        r.value.sessions = {{3, netease, netease, m::NowPlayingSessionOrigin::playerWindow}};
        r.value.session = r.value.sessions.front();
        m::NowPlayingTrack track;
        track.title = std::move(title);
        track.artist = "Window Artist";
        track.isPlaying = playing;
        track.supportsSeeking = false;
        r.value.track = track;
        r.value.capabilities.toggle = r.value.capabilities.previous = r.value.capabilities.next = true;
        r.value.metadataRevision = 4;
        return r;
    };
    const auto bit = n::nowPlayingWindowTokenBit;
    n::NowPlayingReadResult none;
    auto only = n::nowPlayingMergeReads(none, fallback(true));
    check(only.failure == F::none && only.value.session && only.value.session->token == (3 | bit) && only.value.sessions.size() == 1 &&
              only.value.sessions[0].token == (3 | bit) && only.value.track->title == "Window Song" && only.value.metadataRevision == 4,
          "Without a media session the window-title player is presented under a disjoint token");
    auto spotifyPlaying = n::nowPlayingMergeReads(media("Spotify.exe", true), fallback(true));
    check(spotifyPlaying.value.session->appUserModelID == "Spotify.exe" && spotifyPlaying.value.sessions.size() == 2 && spotifyPlaying.value.track->title == "Media Song",
          "A playing media session keeps precedence; the window player is still listed");
    auto spotifyPaused = n::nowPlayingMergeReads(media("Spotify.exe", false), fallback(true));
    check(spotifyPaused.value.session->token == (3 | bit) && spotifyPaused.value.track->title == "Window Song" && !spotifyPaused.value.timeline &&
              !spotifyPaused.value.capabilities.seek && spotifyPaused.value.metadataRevision == 4, "A playing window player beats a paused media session");
    auto bothPaused = n::nowPlayingMergeReads(media("Spotify.exe", false), fallback(false));
    check(bothPaused.value.session->appUserModelID == "Spotify.exe", "Equal ranks keep the media session");
    auto emptySession = n::nowPlayingMergeReads(media("Chrome", false, false), fallback(false));
    check(emptySession.value.session->token == (3 | bit), "A paused window track beats a media session without a track");
    auto listed = n::nowPlayingMergeReads(media("cloudmusic.exe", false), fallback(true));
    check(listed.value.sessions.size() == 1 && listed.value.session->appUserModelID == "cloudmusic.exe", "A player GSMTC already lists is never listed twice");
    auto store = n::nowPlayingMergeReads(media("1F8B0F94.122165AE053F_j2p0p5q0044a6!App", false, false), fallback(true));
    check(store.value.sessions.size() == 1 && !store.value.track, "The NetEase Store session also hides the Win32 window fallback");
    {
        auto foobar = fallback(true);
        foobar.value.sessions[0].appUserModelID = foobar.value.sessions[0].displayName = "C:\\Apps\\foobar2000\\foobar2000.exe";
        foobar.value.session = foobar.value.sessions[0];
        auto same = n::nowPlayingMergeReads(media("foobar2000.exe", false), foobar);
        check(same.value.sessions.size() == 1 && same.value.session->appUserModelID == "foobar2000.exe",
              "A player whose image GSMTC lists (foobar2000 with its media controls on) is never listed twice");
    }
    n::NowPlayingReadResult stale;
    stale.failure = F::stale;
    check(n::nowPlayingMergeReads(stale, fallback(true)).failure == F::stale, "A stale media read is re-read, never replaced by the fallback");
    n::NowPlayingReadResult unavailable;
    unavailable.failure = F::unavailable;
    unavailable.nativeError = -7;
    check(n::nowPlayingMergeReads(unavailable, fallback(true)).value.session->token == (3 | bit), "A failed media read still presents the window player");
    n::NowPlayingReadResult failedWindows;
    failedWindows.failure = F::unavailable;
    const auto both = n::nowPlayingMergeReads(unavailable, failedWindows);
    check(both.failure == F::unavailable && both.nativeError == -7, "Both failed: the media failure is reported");
    check(n::nowPlayingMergeReads(media("Spotify.exe", false), std::nullopt).value.session->appUserModelID == "Spotify.exe", "Media sessions alone pass through");
    n::NowPlayingReadResult many;
    for (std::uint64_t k = 1; k <= 128; ++k) many.value.sessions.push_back({k, "player" + std::to_string(k), "p"});
    many.value.session = many.value.sessions.back();
    many.value.track = media("player128", true).value.track;
    auto crowded = n::nowPlayingMergeReads(many, fallback(false));
    check(crowded.value.sessions.size() == 128 && crowded.value.session->token == 128 &&
              std::find(crowded.value.sessions.begin(), crowded.value.sessions.end(), *crowded.value.session) != crowded.value.sessions.end() &&
              crowded.value.sessions.back().token == (3 | bit), "The bounded list keeps the presented session and the window player");
}

// The composite through the production service and owner, with a manual
// GSMTC stand-in and the synthetic desktop.
struct Owner {
    Q q{[] {}};
    double now = 10;
    // Synthetic per-app audio rows (NowPlayingAppVolume) when requested.
    std::vector<m::NowPlayingAudioRoute> routes;
    std::uint64_t audioRevision{1};
    std::shared_ptr<Provider> gsmtc = std::make_shared<Provider>();
    std::shared_ptr<n::NowPlayingWindowFixture> desktop = std::make_shared<n::NowPlayingWindowFixture>();
    unsigned factories{};
    n::NativeNowPlayingService service{q,
        n::nowPlayingCompositeProvider([this] { ++factories; return gsmtc; }, n::nowPlayingWindowProvider([d = desktop] { return d; }, [this] { return now; })),
        [] {}, [this] { return now; }};
    std::vector<m::NowPlayingPlaybackEvent> events;
    std::unique_ptr<t::NowPlayingSession> owner;
    explicit Owner(bool audio = false) {
        t::NowPlayingSessionOptions o;
        if (audio) {
            auto v = std::make_shared<m::NowPlayingAppVolume>();
            v->setActive = [](bool) {};
            v->revision = [this] { return audioRevision; };
            v->routes = [this] { return routes; };
            v->setGain = [](const std::string&, double) { return false; };
            v->stop = [](const std::string&) { return false; };
            o.volume = std::move(v);
        }
        o.playback = [this](const m::NowPlayingPlaybackEvent& e) { events.push_back(e); };
        o.decode = [](std::span<const std::uint8_t>) { return n::NowPlayingImageResult{}; };
        owner = std::make_unique<t::NowPlayingSession>(service, q, m::NowPlayingAppearance{}, [] {}, std::move(o));
    }
    void flush() {
        for (unsigned k = 0; k < 4; ++k) {
            q.waitIdle();
            q.drain();
            owner->utilityCompleted(now);
        }
    }
    const m::NowPlayingViewInput& input() const { return owner->presentation().input(); }
};
void composite() {
    Owner o;
    o.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "晴天 - 周杰伦")});
    o.owner->setActive(true, o.now);
    o.flush();
    check(o.factories == 1 && o.gsmtc->starts == 1 && o.desktop->watchers() == 1, "One activation starts GSMTC and subscribes to player windows");
    check(o.input().track && o.input().track->title == "晴天" && o.service.snapshot().media.session->origin == m::NowPlayingSessionOrigin::playerWindow &&
              o.gsmtc->reads == 0, "The window-title player is presented while GSMTC is still starting");
    check(!o.owner->presentation().sliders(o.now)[0].enabled, "The seek rail is disabled for a window-title player");
    o.gsmtc->started({});
    o.flush();
    check(o.gsmtc->reads == 1, "GSMTC becoming ready joins with one re-read");
    o.gsmtc->reading(media("Spotify.exe", false));
    o.flush();
    check(o.input().track->title == "晴天" && o.service.snapshot().media.sessions.size() == 2, "A paused Spotify session is listed; the playing NetEase window stays presented");

    check(o.owner->dispatch({m::NowPlayingIntentKind::playPause, o.input().session}, o.now), "The HUD play/pause reaches the window-title player");
    o.flush();
    const auto sent = o.desktop->commands();
    check(sent.size() == 1 && sent[0].kind == m::NowPlayingCommandKind::playPause && o.gsmtc->commands == 0, "The command goes to the player window, not GSMTC");
    check(o.events.size() == 1 && o.events[0].action == "playPause" && o.events[0].source == m::NowPlayingSource::netease, "Event Log playbackAction records the NetEase source");
    check(o.gsmtc->reads == 2, "The completed command asks both sources for one read");
    o.gsmtc->reading(media("Spotify.exe", false));
    o.flush();
    check(o.input().track && !o.input().track->isPlaying && o.input().track->title == "晴天", "The delivered toggle shows NetEase paused");
    check(o.service.snapshot().media.session->appUserModelID == netease, "Paused against paused, the presented window player keeps the tie");

    o.gsmtc->changed();
    o.flush();
    check(o.gsmtc->reads == 3, "A GSMTC event reads both sources once");
    o.gsmtc->reading(media("Spotify.exe", true, true, 4));
    o.flush();
    check(o.input().track->title == "Media Song" && o.service.snapshot().media.session->appUserModelID == "Spotify.exe", "A playing GSMTC session takes precedence");
    check(o.owner->dispatch({m::NowPlayingIntentKind::next, o.input().session}, o.now), "Next is accepted for the media session");
    o.flush();
    check(o.gsmtc->commands == 1 && o.gsmtc->target == 4 && o.gsmtc->request.kind == m::NowPlayingCommandKind::next, "Media commands keep the GSMTC token");
    o.gsmtc->command({});
    o.flush();
    check(o.events.size() == 2 && o.events[1].source == m::NowPlayingSource::spotify, "The media command records its own source");

    o.owner->setActive(false, o.now);
    check(o.gsmtc->stops == 1 && o.desktop->watchers() == 0 && !o.service.snapshot().active, "Hiding stops GSMTC and removes the window subscription");
    o.desktop->notify();
    o.flush();
    check(o.factories == 1, "Hidden window events start nothing");
}
void compositeFailures() {
    // GSMTC fails to start: the window-title player still works.
    Owner o;
    o.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "Song - Artist")});
    o.owner->setActive(true, o.now);
    o.flush();
    o.gsmtc->started({F::accessDenied, -5});
    o.flush();
    check(o.input().track && o.input().track->title == "Song" && o.service.snapshot().failure == F::none, "A failed GSMTC start leaves the window-title player");
    o.owner->setActive(false, o.now);

    // No fallback player and GSMTC failed: the media failure is reported.
    Owner none;
    none.owner->setActive(true, none.now);
    none.flush();
    none.gsmtc->started({F::accessDenied, -5});
    none.flush();
    check(!none.input().track && none.service.snapshot().failure == F::accessDenied && none.service.snapshot().nativeError == -5,
          "Without a window-title player the GSMTC start failure is reported, as before");
    none.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "Song - Artist")});
    none.desktop->notify();
    none.flush();
    check(none.input().track && none.input().track->title == "Song" && none.service.snapshot().failure == F::none, "A player window appearing later is presented");
    none.owner->setActive(false, none.now);
}
// The window title does not report pausing; the player's process route
// (published by the shared audio worker) does, when it is captured.
void audioActivity() {
    Owner o(true);
    m::NowPlayingAudioRoute route;
    route.id = "7:100";
    route.pid = 7;
    route.executablePath = netease;
    route.available = true;
    route.rendering = false;
    o.routes = {route};
    o.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "Song - Artist")});
    o.owner->setActive(true, o.now);
    o.flush();
    check(o.service.snapshot().media.track && o.service.snapshot().media.track->isPlaying && o.input().track && !o.input().track->isPlaying &&
              o.input().volumeAvailable, "A silent player process shows its window-title track paused");
    o.routes[0].rendering = true;
    ++o.audioRevision;
    check(o.owner->audioChanged(o.now) && o.input().track->isPlaying, "Its audio becoming active shows it playing (audio publication, no poll)");
    o.routes[0].rendering.reset();
    ++o.audioRevision;
    o.desktop->set({window(10, 7, netease, "OrpheusBrowserHost", "网易云音乐")});
    o.desktop->notify();
    o.flush();
    check(o.input().track && !o.input().track->isPlaying && o.input().track->title == "Song", "Without captured activity the title inference stands");
    o.owner->setActive(false, o.now);

    // A media session reports its own playback: never overridden.
    Owner listed(true);
    m::NowPlayingAudioRoute spotify;
    spotify.id = "9:100";
    spotify.pid = 9;
    spotify.executablePath = "C:\\EndfieldHUD-Synthetic\\Spotify\\Spotify.exe";
    spotify.available = true;
    spotify.rendering = false;
    listed.routes = {spotify};
    listed.owner->setActive(true, listed.now);
    listed.flush();
    listed.gsmtc->started({});
    listed.flush();
    listed.gsmtc->reading(media("Spotify.exe", true));
    listed.flush();
    check(listed.input().track && listed.input().track->isPlaying && listed.input().volumeAvailable, "A GSMTC session keeps its own playing state");
    listed.owner->setActive(false, listed.now);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: now_playing_players_tests now-playing-players-source.json");
        oracle(argv[1]);
        titles();
        transport();
        timeline();
        activity();
        provider();
        estimate();
        controls();
        merge();
        composite();
        compositeFailures();
        audioActivity();
        std::cout << "Now Playing window-title players: " << checks << " checks passed (synthetic windows only)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Now Playing window-title players failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
