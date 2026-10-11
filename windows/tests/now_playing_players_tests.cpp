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
    double now = 5;
    unsigned changes{};
    std::shared_ptr<n::NowPlayingProvider> provider;
    Direct() {
        provider = n::nowPlayingWindowProvider([d = desktop] { return d; }, [this] { return now; })();
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
    check(sent.size() == 1 && sent[0].window == 10 && sent[0].pid == 7 && sent[0].kind == m::NowPlayingCommandKind::playPause, "WM_APPCOMMAND targets that window and process");
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
        provider();
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
