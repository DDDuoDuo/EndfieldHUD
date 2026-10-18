// Cadence and cost of Now Playing's NetEase Cloud Music path, measured
// without NetEase (no real player, window, media session, network request or
// user file is touched; nothing is shown and no input is injected).
//
// Part 1, synthetic clock (deterministic): the production owner
// (tools::NowPlayingSession over the NativeNowPlayingService and the
// production composite and window-title providers) against a simulated
// NetEase Cloud Music 3: a media session like its own SMTC (title, artist,
// transport, all-zero timeline), a main window whose title names the track,
// a player bar read asynchronously like the MSAA reader (a reading completes
// a little later and wakes the provider), its queue file (exact length and
// song id) and the song's catalog lyrics. Counted per second while Now
// Playing is presented and playing, paused, closed and reopened: window
// enumerations, media-session reads, player-bar readings, owner wakes,
// publications and content events; and the presented time and lyric line
// against the simulated song (pause, seek and track change inside the
// player included). A well-behaved media session (a usable timeline) is the
// reference.
//
// Part 2, real clock (reported, bounded only loosely): a hidden stand-in
// process (this executable with --stand-in) exposes NetEase 3's player-bar
// accessibility tree in a hidden window (the shape of
// tests/fixtures/now-playing-netease-tree.json); this process reads it
// across processes with the production NowPlayingControlReader and measures
// the CPU of one bar reading in both processes, the UI-thread cost of one
// out-of-context WinEvent from that process, the cost of one top-level
// window enumeration, and the production owner presented and closed on the
// real clock against the stand-in (UI thread, whole process and stand-in).
#include "tools/now_playing_session.hpp"
#include "native/now_playing_players.hpp"
#include "native/now_playing_player_controls.hpp"
#include "native/now_playing_scene.hpp"
#include "native/layer_raster.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oleacc.h>
#include <servprov.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace n = endfield::native;
namespace m = endfield::modules;
namespace t = endfield::tools;
using Q = endfield::app::UtilityExecutor;

namespace {
unsigned checks{};
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
double readingHere{}, readingThere{}, readingCalls{}, hookEach{}, enumerationEach{}, pipelineOpen{};
std::string fixed(double value, int digits = 2) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", digits, value);
    return buffer;
}

// ---------------------------------------------------------------------------
// Simulated NetEase Cloud Music 3
// ---------------------------------------------------------------------------
struct Song {
    std::string title, artist;
    double length{};
    std::int64_t id{};
};
const std::array<Song, 2> songs{{{"Synthetic Song", "Example Artist", 233.4, 2001}, {"Second Song", "Example Artist", 200.0, 2002}}};
constexpr double lyricStep = 7, lyricOffset = .5;
const std::string executable = "C:\\EndfieldHUD-Synthetic\\CloudMusic\\cloudmusic.exe";
constexpr std::uintptr_t neteaseWindow = 0x5150;
constexpr std::uint32_t neteasePid = 5150;

struct SimPlayer {
    std::size_t song{};
    double anchor{}, anchorTime{};
    bool playing{true};
    double length() const { return songs[song].length; }
    double position(double time) const { return std::clamp(playing ? anchor + (time - anchorTime) : anchor, 0., length()); }
    void pause(double time) { anchor = position(time); anchorTime = time; playing = false; }
    void play(double time) { anchor = position(time); anchorTime = time; playing = true; }
    void seek(double time, double value) { anchor = value; anchorTime = time; }
    void next(double time) { song = (song + 1) % songs.size(); anchor = 0; anchorTime = time; playing = true; }
    // The bar as the player draws it: redrawn every quarter second, in whole
    // pixels of a 1000-pixel bar (a narrow window: 0.23 s per pixel).
    double fraction(double time) const {
        const double drawn = std::max(anchorTime, std::floor(time / .25) * .25);
        return std::floor(position(drawn) / length() * 1000) / 1000;
    }
    std::string caption() const { return songs[song].title + " - " + songs[song].artist; }
    // The lyric line current at a position (lines every 7 s from 0.5 s).
    static std::string line(double position) {
        if (position < lyricOffset) return {};
        return "Line " + std::to_string(static_cast<int>(std::floor((position - lyricOffset) / lyricStep)));
    }
    static bool nearBoundary(double position, double margin) {
        const double phase = std::fmod(position - lyricOffset + 100 * lyricStep, lyricStep);
        return phase < margin || lyricStep - phase < margin;
    }
};
std::string lyricsDocument(double length) {
    std::string lrc;
    for (int k = 0; lyricOffset + k * lyricStep < length; ++k) {
        const double at = lyricOffset + k * lyricStep;
        const int minutes = static_cast<int>(at / 60);
        const double seconds = at - minutes * 60;
        char stamp[32];
        std::snprintf(stamp, sizeof stamp, "[%02d:%05.2f]", minutes, seconds);
        lrc += stamp;
        lrc += "Line " + std::to_string(k) + "\\n";
    }
    return R"({"code":200,"lrc":{"lyric":")" + lrc + R"("}})";
}
m::NowPlayingBody body(const std::string& text) { return std::make_shared<const std::vector<std::uint8_t>>(text.begin(), text.end()); }
m::NowPlayingBody queueFile() {
    std::string list = R"({"list":[)";
    for (std::size_t k = 0; k < songs.size(); ++k) {
        const auto& s = songs[k];
        if (k) list += ",";
        list += R"({"id":")" + std::to_string(s.id) + R"(","track":{"id":)" + std::to_string(s.id) + R"(,"name":")" + s.title +
                R"(","artists":[{"name":")" + s.artist + R"("}],"album":{"name":"Fixture Album","picUrl":"http://p3.music.126.net/Queue==/)" +
                std::to_string(s.id) + R"(.jpg"},"duration":)" + std::to_string(static_cast<long long>(std::llround(s.length * 1000))) + "}}";
    }
    return body(list + "]}");
}

// NetEase's own media session (3.1.41, live): identity, metadata and
// transport, an all-zero timeline without LastUpdatedTime or rate. Or, as
// the reference, a well-behaved session whose timeline drives the time.
struct SmtcState {
    std::mutex mutex;
    n::NowPlayingProvider::Changed changed;
    const SimPlayer* player{};
    const double* now{};
    bool enabled{true}, usable{};
    std::atomic<unsigned> reads{};
    std::uint64_t revision{1};
    void fire() {
        n::NowPlayingProvider::Changed callback;
        {
            std::lock_guard lock(mutex);
            callback = changed;
        }
        if (callback) callback();
    }
};
class Smtc final : public n::NowPlayingProvider {
public:
    explicit Smtc(std::shared_ptr<SmtcState> s) : s_(std::move(s)) {}
    void start(Changed changed, Ready ready) override {
        {
            std::lock_guard lock(s_->mutex);
            s_->changed = std::move(changed);
        }
        ready({});
    }
    void read(Read done) override {
        ++s_->reads;
        auto out = snapshot();
        done(std::move(out));
    }
    void perform(std::uint64_t, std::uint64_t, m::NowPlayingCommand, Ready ready) override { ready({n::NowPlayingServiceFailure::unsupported, 0}); }
    void stop() noexcept override {
        std::lock_guard lock(s_->mutex);
        s_->changed = nullptr;
    }
private:
    n::NowPlayingReadResult snapshot() const {
        n::NowPlayingReadResult out;
        std::lock_guard lock(s_->mutex);
        if (!s_->enabled) return out;
        const auto& p = *s_->player;
        const double now = *s_->now;
        const std::string aumid = s_->usable ? "Spotify.exe" : "cloudmusic.exe";
        out.value.sessions = {{1, aumid, aumid}};
        out.value.session = out.value.sessions.front();
        m::NowPlayingTrack track;
        track.title = songs[p.song].title;
        track.artist = songs[p.song].artist;
        track.isPlaying = p.playing;
        track.sampledAt = now;
        track.supportsSeeking = false;
        n::NowPlayingTimeline timeline;
        timeline.playing = p.playing;
        timeline.sampledAt = now;
        if (s_->usable) {
            timeline.end = static_cast<std::int64_t>(p.length() * 1e7);
            timeline.position = static_cast<std::int64_t>(p.position(now) * 1e7);
            timeline.maximumSeek = timeline.end;
            timeline.rate = 1;
            track.duration = p.length();
            track.position = p.position(now);
        } else {
            timeline.rate = std::numeric_limits<double>::quiet_NaN();
            timeline.lastUpdated = false;
        }
        out.value.track = track;
        out.value.timeline = timeline;
        out.value.capabilities.play = out.value.capabilities.toggle = out.value.capabilities.next = out.value.capabilities.previous = true;
        out.value.capabilities.pause = p.playing;
        out.value.metadataRevision = s_->revision;
        return out;
    }
    std::shared_ptr<SmtcState> s_;
};

// NetEase's main window and its player bar read like the MSAA reader: a
// refresh starts one background reading (one more is queued while one runs),
// which completes `latency` later and then wakes the watchers.
class Desktop final : public n::NowPlayingWindowSystem {
public:
    Desktop(const SimPlayer& player, const double& now) : player_(player), now_(now) {}
    std::function<void()> watch(std::function<void()> changed) override {
        std::lock_guard lock(mutex_);
        const auto id = ++nextWatcher_;
        watchers_.emplace_back(id, std::move(changed));
        return [this, id] {
            std::lock_guard lock(mutex_);
            std::erase_if(watchers_, [id](const auto& w) { return w.first == id; });
        };
    }
    std::vector<m::NowPlayingPlayerWindow> windows() override {
        std::lock_guard lock(mutex_);
        ++enumerations;
        if (!running_) return {};
        return {{neteaseWindow, neteasePid, executable, "OrpheusBrowserHost", caption_, true, false, captionAt_}};
    }
    n::NowPlayingServiceFailure command(std::uintptr_t, std::uint32_t, m::NowPlayingCommandKind, bool) override { return n::NowPlayingServiceFailure::none; }
    bool notifiesReadings() const noexcept override { return true; }
    std::optional<n::NowPlayingPlayerReading> reading(std::uintptr_t window, std::uint32_t pid, bool refresh) override {
        std::lock_guard lock(mutex_);
        if (window != neteaseWindow || pid != neteasePid || !running_) return std::nullopt;
        if (refresh) {
            ++refreshRequests;
            if (due_) queued_ = true;
            else start(now_);
        }
        return last_;
    }
    // Completes a reading that is due (the reader's job on the pool) and
    // wakes the watchers, as the reader's notification does.
    void complete(double time) {
        std::vector<std::function<void()>> calls;
        {
            std::lock_guard lock(mutex_);
            if (!due_ || *due_ > time) return;
            n::NowPlayingPlayerReading r;
            r.startedAt = started_;
            r.sampledAt = *due_;
            r.playing = player_.playing;
            r.playPause = r.previous = r.next = true;
            r.visible = true;
            r.fraction = player_.fraction(*due_);
            r.artwork = "https://p3.music.126.net/Own==/" + std::to_string(songs[player_.song].id) + ".jpg?param=600y600";
            r.title = caption_;
            last_ = r;
            ++jobs;
            due_.reset();
            if (queued_) {
                queued_ = false;
                start(time);
            }
            for (const auto& w : watchers_) calls.push_back(w.second);
        }
        for (auto& call : calls) call();
    }
    std::optional<double> nextDue() const {
        std::lock_guard lock(mutex_);
        return due_;
    }
    // The player's title changed (its NAMECHANGE wakes the watchers).
    void retitle(double time) {
        std::vector<std::function<void()>> calls;
        {
            std::lock_guard lock(mutex_);
            caption_ = player_.caption();
            captionAt_ = time;
            for (const auto& w : watchers_) calls.push_back(w.second);
        }
        for (auto& call : calls) call();
    }
    std::size_t watchers() const {
        std::lock_guard lock(mutex_);
        return watchers_.size();
    }
    std::atomic<unsigned> enumerations{}, jobs{}, refreshRequests{};
    double latency{.03};
private:
    void start(double time) {
        started_ = time;
        due_ = time + latency;
    }
    const SimPlayer& player_;
    const double& now_;
    mutable std::mutex mutex_;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> watchers_;
    std::uint64_t nextWatcher_{};
    std::optional<n::NowPlayingPlayerReading> last_;
    std::optional<double> due_;
    double started_{};
    bool queued_{}, running_{true};
    std::string caption_{songs[0].title + " - " + songs[0].artist};
    std::optional<double> captionAt_;
};

// Answers the catalog: the queued song's lyric document; covers fail (no
// decode is needed for the cadence).
struct WebTask final : n::NowPlayingWeb::Task {
    void cancel() noexcept override {}
};
struct Catalog {
    std::vector<std::pair<n::NowPlayingWeb::Request, n::NowPlayingWeb::Done>> pending;
    std::mutex mutex;
    unsigned lyrics{}, covers{};
    n::NowPlayingWeb::Transport bind() {
        return [this](const n::NowPlayingWeb::Request& r, n::NowPlayingWeb::Done d) -> std::shared_ptr<n::NowPlayingWeb::Task> {
            std::lock_guard lock(mutex);
            pending.emplace_back(r, std::move(d));
            return std::make_shared<WebTask>();
        };
    }
    bool answer() {
        std::vector<std::pair<n::NowPlayingWeb::Request, n::NowPlayingWeb::Done>> now;
        {
            std::lock_guard lock(mutex);
            now.swap(pending);
        }
        for (auto& [request, done] : now) {
            m::NowPlayingBody reply;
            if (request.kind == n::NowPlayingWeb::Kind::lyrics) {
                ++lyrics;
                for (const auto& s : songs)
                    if (request.url == m::nowPlayingCatalogLyricsURL(s.id)) reply = body(lyricsDocument(s.length));
            } else {
                ++covers;
            }
            done(std::move(reply));
        }
        return !now.empty();
    }
    bool waiting() {
        std::lock_guard lock(mutex);
        return !pending.empty();
    }
};

struct Counts {
    unsigned enumerations{}, mediaReads{}, jobs{}, wakes{}, publishes{}, contentEvents{}, deadlines{};
    Counts operator-(const Counts& o) const {
        return {enumerations - o.enumerations, mediaReads - o.mediaReads, jobs - o.jobs, wakes - o.wakes, publishes - o.publishes,
                contentEvents - o.contentEvents, deadlines - o.deadlines};
    }
    std::string perSecond(double seconds) const {
        const auto r = [&](unsigned v) { return fixed(v / seconds); };
        return "enumerations " + r(enumerations) + "/s, media reads " + r(mediaReads) + "/s, bar readings " + r(jobs) + "/s, owner wakes " + r(wakes) +
               "/s, publications " + r(publishes) + "/s, content events " + r(contentEvents) + "/s, display deadlines " + r(deadlines) + "/s";
    }
};

// The production owner over the simulated player on a synthetic clock. Every
// wake (service, downloads, the shared executor) is drained before the clock
// moves on, as the host's message loop does at once.
class Harness {
public:
    explicit Harness(bool smtc = true, bool usable = false) {
        state = std::make_shared<SmtcState>();
        state->player = &player;
        state->now = &now;
        state->enabled = smtc;
        state->usable = usable;
        desktop = std::make_shared<Desktop>(player, now);
        activity = std::make_shared<n::NowPlayingProcessActivity>();
        setRendering(true);
        auto clock = [this] { return now; };
        auto media = [s = state]() -> std::shared_ptr<n::NowPlayingProvider> { return std::make_shared<Smtc>(s); };
        n::NativeNowPlayingService::Factory windows = [] { return std::shared_ptr<n::NowPlayingProvider>(); };
        if (!usable) windows = n::nowPlayingWindowProvider([d = desktop] { return d; }, clock, activity);
        service = std::make_unique<n::NativeNowPlayingService>(q, n::nowPlayingCompositeProvider(media, windows), [this] {
            ++wakes;
            pending = true;
        }, clock);
        web = std::make_shared<n::NowPlayingWeb>(catalog.bind(), [this] { pending = true; }, clock);
        t::NowPlayingSessionOptions o;
        o.web = web;
        o.neteasePlayingList = [] { return queueFile(); };
        o.decode = [](std::span<const std::uint8_t>) { return n::NowPlayingImageResult{}; };
        owner = std::make_unique<t::NowPlayingSession>(*service, q, m::NowPlayingAppearance{}, [] {}, std::move(o));
    }
    ~Harness() {
        owner.reset();
        service.reset();
        q.waitIdle();
        q.drain();
        q.shutdown();
    }
    void setRendering(bool rendering) {
        std::vector<m::NowPlayingAudioRoute> routes{{"5150:1", neteasePid, executable, true, m::NowPlayingAudioRouteState::direct, {}, {}, rendering}};
        activity->update(routes);
    }
    // The area's followActivity: a change of the process's audio activity
    // asks the provider for one read.
    void audio(bool rendering) {
        setRendering(rendering);
        service->refresh();
        settle();
    }
    Counts counts() const {
        return {desktop->enumerations.load(), state->reads.load(), desktop->jobs.load(), wakes.load(), publishes, contentEvents, deadlines};
    }
    void open() {
        owner->setActive(true, now);
        observe();
        settle();
    }
    void close() {
        owner->setActive(false, now);
        observe();
        settle();
    }
    // Runs the clock to `until`, delivering wakes, readings and deadlines in
    // time order; `sample` sees the presentation every 50 ms.
    template <class Sample> void run(double until, Sample sample) {
        nextSample = std::max(nextSample, now);
        for (unsigned guard = 0;; ++guard) {
            check(guard < 400000, "The synthetic clock advances");
            if (pending.load()) settle();
            if (const auto d = desktop->nextDue(); d && *d <= now) {
                desktop->complete(now);
                settle();
                continue;
            }
            if (const auto w = owner->nextWakeTime(now); w && *w <= now) {
                ++deadlines;
                if (owner->deadline(now)) ++publishes;
                observe();
                settle();
                continue;
            }
            if (now >= nextSample) {
                sample();
                nextSample = now + .05;
            }
            if (now >= until) break;
            double next = std::min(until, nextSample);
            if (const auto w = owner->nextWakeTime(now)) next = std::min(next, *w);
            if (const auto d = desktop->nextDue()) next = std::min(next, *d);
            now = std::max(now, next);
        }
    }
    void run(double until) { run(until, [] {}); }
    void settle() {
        for (unsigned round = 0; round < 32; ++round) {
            pending = false;
            q.waitIdle();
            q.drain();
            if (owner->utilityCompleted(now)) ++publishes;
            observe();
            catalog.answer();
            q.waitIdle();
            if (!pending.load() && !q.stats().completed && !catalog.waiting()) return;
        }
        check(false, "Synthetic Now Playing work settles");
    }
    double presented() const {
        const auto value = owner->presentation().sliders(now)[0].value;
        return value ? *value : -1;
    }
    bool presentedTimeKnown() const {
        const auto& in = owner->presentation().input();
        return in.track && in.track->duration && in.track->position;
    }
    std::string row() const { return std::string(owner->presentation().lyricRows()[1]); }

    double now{10};
    SimPlayer player;
    std::shared_ptr<SmtcState> state;
    std::shared_ptr<Desktop> desktop;
    std::shared_ptr<n::NowPlayingProcessActivity> activity;
    Catalog catalog;
    std::atomic<bool> pending{};
    std::atomic<unsigned> wakes{};
    Q q{[this] { pending = true; }};
    std::unique_ptr<n::NativeNowPlayingService> service;
    std::shared_ptr<n::NowPlayingWeb> web;
    std::unique_ptr<t::NowPlayingSession> owner;
    unsigned publishes{}, contentEvents{}, deadlines{};
private:
    void observe() {
        const auto revision = owner->presentation().contentRevision();
        if (revision != revision_) {
            contentEvents += static_cast<unsigned>(revision - revision_);
            revision_ = revision;
        }
    }
    std::uint64_t revision_{};
    double nextSample{};
};

struct Accuracy {
    double worst{}, sum{};
    unsigned samples{}, rows{}, rowMisses{}, animating{};
    void add(double error) {
        worst = std::max(worst, std::abs(error));
        sum += std::abs(error);
        ++samples;
    }
    std::string text() const {
        return "time error max " + fixed(worst * 1000, 0) + " ms, mean " + fixed(samples ? sum / samples * 1000 : 0, 0) + " ms over " + std::to_string(samples) +
               " samples; lyric line " + std::to_string(rows - rowMisses) + "/" + std::to_string(rows) + "; animating (frames drawn) " +
               fixed(samples ? 100. * animating / samples : 0, 0) + " % of the time";
    }
};

Counts openCounts, referenceCounts;
void cadence() {
    Harness h; // the simulated song has played since 0 s
    h.open();
    check(h.desktop->watchers() == 1, "The presented provider follows the player's window events");
    const auto measure = [&](Accuracy& a) {
        if (!h.presentedTimeKnown()) return;
        const double truth = h.player.position(h.now);
        a.add(h.presented() - truth);
        if (h.owner->presentation().requiresFrames(h.now)) ++a.animating;
        if (!SimPlayer::nearBoundary(truth, .35)) {
            ++a.rows;
            if (h.row() != SimPlayer::line(truth)) ++a.rowMisses;
        }
    };
    // Lock-in: the first readings, the queue's length and the lyrics.
    h.run(20);
    check(h.presentedTimeKnown() && h.owner->presentation().input().track->duration == songs[0].length,
          "The queue file's exact length turns NetEase's bar into time");
    check(h.owner->presentation().input().lyrics && h.row() == SimPlayer::line(h.player.position(h.now)), "The song's own lyrics follow its bar");
    // Steady playing, presented.
    Accuracy steady;
    const auto before = h.counts();
    h.run(80, [&] { measure(steady); });
    const auto open = h.counts() - before;
    std::cout << "open, playing (60 s): " << open.perSecond(60) << "\n  " << steady.text() << "\n";
    check(steady.worst < .30, "The presented time stays within 0.3 s of the song while it plays (" + fixed(steady.worst, 3) + " s)");
    check(steady.rowMisses == 0, "The lyric line follows the song away from line changes");
    check(open.jobs >= 54 && open.jobs <= 66, "The bar is read about once a second while it plays (" + std::to_string(open.jobs) + " in 60 s)");
    check(open.enumerations <= open.jobs + 2 && open.mediaReads <= open.jobs + 2,
          "One provider read per bar reading: the windows are enumerated and the media session read once, after the new reading (" +
              std::to_string(open.enumerations) + " enumerations, " + std::to_string(open.mediaReads) + " media reads)");
    check(open.wakes <= 2 * open.jobs + 2 && open.contentEvents <= open.jobs + open.deadlines + 2,
          "Each re-anchor wakes the owner twice (the reading's completion, the read's) and changes the content once (" + std::to_string(open.wakes) +
              " wakes, " + std::to_string(open.contentEvents) + " content events)");
    openCounts = open;

    // A pause inside NetEase: its media session and its audio say so. The
    // bar is drawn in steps, so the paused time is the bar's (within one
    // step of 0.25 s and one pixel).
    h.player.pause(h.now);
    h.state->fire();
    h.audio(false);
    const auto pausedAt = h.counts();
    Accuracy paused;
    h.run(96, [&] {
        if (h.now > h.player.anchorTime + 1.5) measure(paused);
    });
    const auto pause = h.counts() - pausedAt;
    std::cout << "open, paused (16 s): " << pause.perSecond(16) << "\n  " << paused.text() << "\n";
    check(paused.worst < .5 && !h.owner->presentation().input().track->isPlaying, "Paused, the presented time stands at the bar's position");
    check(pause.jobs <= 2 && pause.deadlines == 0, "Paused: no display tick and no periodic bar reading");

    // Playing again, then seeks inside NetEase (no event: only its bar shows them).
    h.player.play(h.now);
    h.state->fire();
    h.audio(true);
    h.run(106);
    for (const double target : {150., 60.}) {
        h.player.seek(h.now, target);
        const double seekAt = h.now;
        std::optional<double> converged;
        h.run(seekAt + 6, [&] {
            if (!converged && std::abs(h.presented() - h.player.position(h.now)) < .3) converged = h.now - seekAt;
        });
        std::cout << "seek inside the player to " << fixed(target, 0) << " s: presented within 0.3 s after "
                  << (converged ? fixed(*converged, 2) + " s" : std::string("never")) << "\n";
        check(converged && *converged <= 1.6, "A seek inside the player is followed within about a second");
    }

    // The next track: its window title changes (and its media session).
    h.player.next(h.now);
    const double changeAt = h.now;
    h.desktop->retitle(h.now);
    h.state->revision++;
    h.state->fire();
    std::optional<double> titled, timed;
    h.run(changeAt + 6, [&] {
        const auto& in = h.owner->presentation().input();
        if (!titled && in.track && in.track->title == songs[1].title) titled = h.now - changeAt;
        if (titled && !timed && h.presentedTimeKnown() && in.track->duration == songs[1].length && std::abs(h.presented() - h.player.position(h.now)) < .3)
            timed = h.now - changeAt;
    });
    std::cout << "track change: title after " << (titled ? fixed(*titled, 2) + " s" : std::string("never")) << ", time after "
              << (timed ? fixed(*timed, 2) + " s" : std::string("never")) << "\n";
    check(titled && *titled < .1 && timed && *timed <= 1.6, "A track change shows the new title at once and its time within about a second");
    Accuracy second;
    h.run(140, [&] { measure(second); });
    check(second.worst < .30 && second.rowMisses == 0, "The new song's time and lyric line follow it (" + second.text() + ")");

    // Closed: nothing periodic, nothing read, even when the player changes.
    h.close();
    const auto closedAt = h.counts();
    const auto refreshesAt = h.desktop->refreshRequests.load();
    h.run(170);
    h.player.next(h.now);
    h.desktop->retitle(h.now);
    h.state->fire();
    h.run(200);
    const auto closed = h.counts() - closedAt;
    std::cout << "closed (60 s): " << closed.perSecond(60) << "\n";
    check(closed.enumerations == 0 && closed.mediaReads == 0 && closed.jobs == 0 && h.desktop->refreshRequests.load() == refreshesAt && closed.wakes == 0 &&
              closed.deadlines == 0 && !h.owner->nextWakeTime(h.now) && h.desktop->watchers() == 0,
          "Closed: no window enumeration, media read, bar reading, wake or deadline (a track change inside the player included)");

    // Reopened: the current song and its time at once.
    h.open();
    std::optional<double> back;
    const double openedAt = h.now;
    h.run(206, [&] {
        if (!back && h.presentedTimeKnown() && h.owner->presentation().input().track->title == songs[h.player.song].title &&
            std::abs(h.presented() - h.player.position(h.now)) < .3)
            back = h.now - openedAt;
    });
    std::cout << "reopened: presented within 0.3 s after " << (back ? fixed(*back, 2) + " s" : std::string("never")) << "\n";
    check(back && *back <= 1.2, "Reopened, the current song's time is back within about a second");
    h.close();
}

// Reference: a well-behaved media session (its timeline drives the time).
void reference() {
    Harness h(true, true);
    h.open();
    h.run(20);
    const auto before = h.counts();
    Accuracy a;
    h.run(80, [&] {
        if (!h.presentedTimeKnown()) return;
        a.add(h.presented() - h.player.position(h.now));
        if (h.owner->presentation().requiresFrames(h.now)) ++a.animating;
    });
    const auto open = h.counts() - before;
    std::cout << "reference media session, open, playing (60 s): " << open.perSecond(60) << "\n  " << a.text() << "\n";
    check(open.mediaReads == 0 && open.jobs == 0, "A usable timeline needs no read while it plays");
    referenceCounts = open;
    h.close();
}
// ---------------------------------------------------------------------------
// Part 2: the hidden stand-in process (real clock)
// ---------------------------------------------------------------------------
constexpr IID iidAccessible2{0xE89F726E, 0xC4F4, 0x4C19, {0xBB, 0x19, 0xB6, 0x47, 0xD7, 0xFA, 0x84, 0x78}};
std::wstring wide(std::string_view text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}
struct IA2LocaleValue {
    BSTR language;
    BSTR country;
    BSTR variant;
};
struct __declspec(uuid("E89F726E-C4F4-4C19-BB19-B647D7FA8478")) StandInAccessible2 : public IAccessible {
    virtual HRESULT STDMETHODCALLTYPE get_nRelations(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_relation(long, IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_relations(long, IUnknown**, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE role(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE scrollTo(long) = 0;
    virtual HRESULT STDMETHODCALLTYPE scrollToPoint(long, long, long) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_groupPosition(long*, long*, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_states(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_extendedRole(BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_localizedExtendedRole(BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_nExtendedStates(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_extendedStates(long, BSTR**, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_localizedExtendedStates(long, BSTR**, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_uniqueID(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_windowHandle(HWND*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_indexInParent(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_locale(IA2LocaleValue*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_attributes(BSTR*) = 0;
};
// A web page element of the stand-in (its own STA; calls arrive marshaled
// from the reading process). Counts the calls it answers.
std::atomic<unsigned long> answered{};
class WebNode final : public StandInAccessible2, public IServiceProvider {
public:
    WebNode(long role, std::string name, std::string description = {}, std::string attributes = {}, RECT bounds = {})
        : role_(role), name_(wide(name)), description_(wide(description)), attributes_(wide(attributes)), bounds_(bounds) {}
    std::vector<WebNode*> children;
    std::function<void()> onName;
    void place(RECT bounds) { bounds_ = bounds; }
    void rename(std::string name) { name_ = wide(name); }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) *out = static_cast<IAccessible*>(static_cast<StandInAccessible2*>(this));
        else if (riid == iidAccessible2) *out = static_cast<StandInAccessible2*>(this);
        else if (riid == IID_IServiceProvider) *out = static_cast<IServiceProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override { return --refs_; } // owned by the page
    IFACEMETHODIMP GetTypeInfoCount(UINT* count) override { if (count) *count = 0; return S_OK; }
    IFACEMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    IFACEMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    IFACEMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_accParent(IDispatch** out) override { if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accChildCount(long* out) override {
        ++answered;
        if (!out) return E_POINTER;
        *out = static_cast<long>(children.size());
        return S_OK;
    }
    IFACEMETHODIMP get_accChild(VARIANT child, IDispatch** out) override {
        ++answered;
        if (!out) return E_POINTER;
        *out = nullptr;
        if (child.vt != VT_I4 || child.lVal < 1 || static_cast<std::size_t>(child.lVal) > children.size()) return E_INVALIDARG;
        *out = static_cast<IAccessible*>(static_cast<StandInAccessible2*>(children[static_cast<std::size_t>(child.lVal - 1)]));
        (*out)->AddRef();
        return S_OK;
    }
    IFACEMETHODIMP get_accName(VARIANT child, BSTR* out) override {
        ++answered;
        if (!self(child)) return E_INVALIDARG;
        if (onName) onName();
        return string(name_, out);
    }
    IFACEMETHODIMP get_accValue(VARIANT, BSTR* out) override { ++answered; if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accDescription(VARIANT child, BSTR* out) override {
        ++answered;
        if (!self(child)) return E_INVALIDARG;
        return string(description_, out);
    }
    IFACEMETHODIMP get_accRole(VARIANT child, VARIANT* out) override {
        ++answered;
        if (!out || !self(child)) return E_INVALIDARG;
        VariantInit(out);
        out->vt = VT_I4;
        out->lVal = role_;
        return S_OK;
    }
    IFACEMETHODIMP get_accState(VARIANT, VARIANT* out) override { if (out) { VariantInit(out); out->vt = VT_I4; out->lVal = 0; } return S_OK; }
    IFACEMETHODIMP get_accHelp(VARIANT, BSTR* out) override { if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accHelpTopic(BSTR*, VARIANT, long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_accKeyboardShortcut(VARIANT, BSTR* out) override { if (out) *out = nullptr; return S_FALSE; }
    IFACEMETHODIMP get_accFocus(VARIANT* out) override { if (out) VariantInit(out); return S_FALSE; }
    IFACEMETHODIMP get_accSelection(VARIANT* out) override { if (out) VariantInit(out); return S_FALSE; }
    IFACEMETHODIMP get_accDefaultAction(VARIANT, BSTR* out) override { return string(L"press", out); }
    IFACEMETHODIMP accSelect(long, VARIANT) override { return E_NOTIMPL; }
    IFACEMETHODIMP accLocation(long* left, long* top, long* width, long* height, VARIANT child) override {
        ++answered;
        if (!left || !top || !width || !height || !self(child)) return E_INVALIDARG;
        *left = bounds_.left;
        *top = bounds_.top;
        *width = bounds_.right - bounds_.left;
        *height = bounds_.bottom - bounds_.top;
        return S_OK;
    }
    IFACEMETHODIMP accNavigate(long, VARIANT, VARIANT* out) override { if (out) VariantInit(out); return E_NOTIMPL; }
    IFACEMETHODIMP accHitTest(long, long, VARIANT* out) override { if (out) VariantInit(out); return S_FALSE; }
    IFACEMETHODIMP accDoDefaultAction(VARIANT child) override { return self(child) ? S_OK : E_INVALIDARG; }
    IFACEMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    IFACEMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_nRelations(long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_relation(long, IUnknown**) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_relations(long, IUnknown**, long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP role(long* out) override { if (!out) return E_POINTER; *out = role_; return S_OK; }
    IFACEMETHODIMP scrollTo(long) override { return E_NOTIMPL; }
    IFACEMETHODIMP scrollToPoint(long, long, long) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_groupPosition(long*, long*, long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_states(long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_extendedRole(BSTR*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_localizedExtendedRole(BSTR*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_nExtendedStates(long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_extendedStates(long, BSTR**, long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_localizedExtendedStates(long, BSTR**, long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_uniqueID(long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_windowHandle(HWND*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_indexInParent(long*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_locale(IA2LocaleValue*) override { return E_NOTIMPL; }
    IFACEMETHODIMP get_attributes(BSTR* out) override { ++answered; return string(attributes_, out); }
    IFACEMETHODIMP QueryService(REFGUID, REFIID riid, void** out) override {
        ++answered;
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid != iidAccessible2) return E_NOINTERFACE;
        *out = static_cast<StandInAccessible2*>(this);
        AddRef();
        return S_OK;
    }
private:
    static bool self(const VARIANT& child) { return child.vt == VT_I4 && child.lVal == CHILDID_SELF; }
    static HRESULT string(const std::wstring& value, BSTR* out) {
        if (!out) return E_POINTER;
        *out = value.empty() ? nullptr : SysAllocString(value.c_str());
        return value.empty() ? S_FALSE : S_OK;
    }
    long role_;
    std::wstring name_, description_, attributes_;
    RECT bounds_;
    std::atomic<ULONG> refs_{1};
};
// NetEase Cloud Music 3.1's page (fixture shape), built after Chromium's
// screen-reader probe and a name read, with a playlist above its bar.
struct NeteasePage {
    WebNode document{ROLE_SYSTEM_DOCUMENT, "Page"}, body{ROLE_SYSTEM_GROUPING, ""}, playlist{ROLE_SYSTEM_GROUPING, ""},
        listPlay{ROLE_SYSTEM_GRAPHIC, "play", "\xe6\x92\xad\xe6\x94\xbe"}, listLength{ROLE_SYSTEM_STATICTEXT, "03:33"},
        footer{ROLE_SYSTEM_GROUPING, "", "", "display:block;tag:footer;class:should-hide-under-vinyl-mode;", {0, 1462, 2561, 1601}},
        miniBar{ROLE_SYSTEM_GROUPING, "", "", "id:page_pc_mini_bar;", {0, 1462, 2561, 1601}},
        row{ROLE_SYSTEM_GROUPING, "", "", "class:default-bar-wrapper;", {0, 1481, 2561, 1601}},
        side{ROLE_SYSTEM_GROUPING, "", "", "class:side;", {45, 1481, 323, 1601}},
        cover{ROLE_SYSTEM_GRAPHIC, "", "", "display:block;tag:img;src:orpheus\\://cache/?http\\://p3.music.126.net/Test\\=\\=/1.jpg?imageView\\=&thumbnail\\=60y60&type\\=webp;", {48, 1499, 132, 1583}},
        title{ROLE_SYSTEM_STATICTEXT, "Synthetic Song"}, artist{ROLE_SYSTEM_STATICTEXT, "Example Artist"},
        pre{ROLE_SYSTEM_PUSHBUTTON, "pre"}, preIcon{ROLE_SYSTEM_GRAPHIC, "pre", "\xe4\xb8\x8a\xe4\xb8\x80\xe9\xa6\x96\xef\xbc\x88""Ctrl + Left\xef\xbc\x89"},
        play{ROLE_SYSTEM_BUTTONMENU, "pause"}, playIcon{ROLE_SYSTEM_GRAPHIC, "pause", "\xe6\x9a\x82\xe5\x81\x9c\xef\xbc\x88""Ctrl + P\xef\xbc\x89"},
        next{ROLE_SYSTEM_PUSHBUTTON, "next"}, nextIcon{ROLE_SYSTEM_GRAPHIC, "next", "\xe4\xb8\x8b\xe4\xb8\x80\xe9\xa6\x96\xef\xbc\x88""Ctrl + Right\xef\xbc\x89"},
        progress{ROLE_SYSTEM_GROUPING, "\xe6\x92\xad\xe6\x94\xbe\xe8\xbf\x9b\xe5\xba\xa6\xe8\xb0\x83\xe8\x8a\x82", "", "class:slider-default;", {0, 1476, 2561, 1481}},
        hotzone{ROLE_SYSTEM_GROUPING, "", "", "class:hotzone-overlay;", {0, 1461, 2561, 1496}},
        cache{ROLE_SYSTEM_GROUPING, "", "", "class:cache;", {0, 1476, 398, 1481}},
        track{ROLE_SYSTEM_GROUPING, "", "", "class:track;", {0, 1476, 80, 1481}};
    bool probed{}, named{}, built{};
    NeteasePage() {
        playlist.children = {&listPlay, &listLength};
        side.children = {&cover, &title, &artist};
        pre.children = {&preIcon};
        play.children = {&playIcon};
        next.children = {&nextIcon};
        row.children = {&side, &pre, &play, &next};
        progress.children = {&hotzone, &cache, &track};
        miniBar.children = {&progress, &row};
        footer.children = {&miniBar};
        body.children = {&playlist, &footer};
        document.onName = [this] { named = true; build(); };
    }
    void build() {
        if (probed && named && !built) {
            built = true;
            document.children = {&body};
        }
    }
};
constexpr UINT fireMessage = WM_APP + 1;  // wParam events, lParam WinEvent id
constexpr UINT answeredMessage = WM_APP + 2; // returns the calls its page answered
constexpr UINT standInLength = 233;       // its song's length (s)
NeteasePage* standInPage{};
LRESULT CALLBACK standInPageProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_GETOBJECT && standInPage) {
        if (static_cast<DWORD>(l) == static_cast<DWORD>(OBJID_CLIENT))
            return LresultFromObject(IID_IAccessible, w, static_cast<IAccessible*>(static_cast<StandInAccessible2*>(&standInPage->document)));
        if (l == 1) {
            standInPage->probed = true;
            standInPage->build();
            return 0;
        }
    }
    return DefWindowProcW(window, message, w, l);
}
HWND standInView{};
LRESULT CALLBACK standInHostProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == fireMessage) {
        for (WPARAM k = 0; k < w; ++k) NotifyWinEvent(static_cast<DWORD>(l), standInView, OBJID_CLIENT, -static_cast<LONG>(1 + k % 4096));
        return 0;
    }
    if (message == answeredMessage) return static_cast<LRESULT>(answered.load());
    return DefWindowProcW(window, message, w, l);
}
std::wstring standInClass(DWORD parent) { return L"EndfieldHUD.NeteaseStandIn." + std::to_wstring(parent); }

// The stand-in: a hidden player window titled "Synthetic Song - Example Artist"
// whose Chrome_RenderWidgetHostHWND child answers with NeteasePage, a bar
// that plays (its "track" part grows four times a second), `fillers` hidden
// top-level windows (a desktop's worth for the enumeration), and WinEvents
// fired on request. It ends with its parent.
int standIn(DWORD parentPid, HANDLE parent, HANDLE ready, unsigned fillers) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    NeteasePage page;
    standInPage = &page;
    const auto instance = GetModuleHandleW(nullptr);
    const auto hostClass = standInClass(parentPid);
    WNDCLASSW host{}, view{}, filler{};
    host.lpfnWndProc = standInHostProcedure;
    host.hInstance = view.hInstance = filler.hInstance = instance;
    host.lpszClassName = hostClass.c_str();
    view.lpfnWndProc = standInPageProcedure;
    view.lpszClassName = L"Chrome_RenderWidgetHostHWND";
    filler.lpfnWndProc = DefWindowProcW;
    filler.lpszClassName = L"EndfieldHUD.NeteaseStandInFiller";
    if (!RegisterClassW(&host) || !RegisterClassW(&view) || !RegisterClassW(&filler)) return 3;
    const HWND window = CreateWindowExW(0, host.lpszClassName, L"Synthetic Song - Example Artist", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr, nullptr, instance, nullptr);
    standInView = CreateWindowExW(0, view.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 200, 100, window, nullptr, instance, nullptr);
    if (!window || !standInView || IsWindowVisible(window)) return 4;
    std::vector<HWND> fill;
    for (unsigned k = 0; k < fillers; ++k)
        fill.push_back(CreateWindowExW(WS_EX_TOOLWINDOW, filler.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr));
    SetTimer(window, 1, 250, nullptr);
    const auto started = std::chrono::steady_clock::now();
    SetEvent(ready);
    for (;;) {
        const DWORD status = MsgWaitForMultipleObjectsEx(1, &parent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (status == WAIT_OBJECT_0) break;
        MSG message;
        bool quit{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) quit = true;
            if (message.message == WM_TIMER && message.hwnd == window) {
                const double played = std::fmod(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() + 60, standInLength);
                page.track.place({0, 1476, static_cast<LONG>(2561 * played / standInLength), 1481});
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (quit) break;
    }
    for (const HWND f : fill) DestroyWindow(f);
    DestroyWindow(standInView);
    DestroyWindow(window);
    standInPage = nullptr;
    CoUninitialize();
    return 0;
}

// The stand-in process as seen from here.
class StandIn {
public:
    explicit StandIn(unsigned fillers) {
        SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
        ready_ = CreateEventW(&inherit, TRUE, FALSE, nullptr);
        HANDLE self{};
        DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &self, SYNCHRONIZE, TRUE, 0);
        parent_ = self;
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring command = L"\"" + std::wstring(path) + L"\" --stand-in " + std::to_wstring(GetCurrentProcessId()) + L" " +
                               std::to_wstring(reinterpret_cast<std::uintptr_t>(parent_)) + L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(ready_)) +
                               L" " + std::to_wstring(fillers);
        STARTUPINFOW startup{sizeof startup};
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION info{};
        check(CreateProcessW(path, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info) != FALSE,
              "The hidden stand-in process starts");
        process_ = info.hProcess;
        CloseHandle(info.hThread);
        pid = info.dwProcessId;
        check(WaitForSingleObject(ready_, 20000) == WAIT_OBJECT_0, "The stand-in is ready");
        window = FindWindowW(standInClass(GetCurrentProcessId()).c_str(), nullptr);
        DWORD owner{};
        check(window && GetWindowThreadProcessId(window, &owner) && owner == pid && !IsWindowVisible(window), "The stand-in's player window is hidden");
    }
    ~StandIn() {
        if (window) PostMessageW(window, WM_QUIT, 0, 0);
        if (process_) {
            if (WaitForSingleObject(process_, 5000) != WAIT_OBJECT_0) TerminateProcess(process_, 1);
            CloseHandle(process_);
        }
        if (ready_) CloseHandle(ready_);
        if (parent_) CloseHandle(parent_);
    }
    StandIn(const StandIn&) = delete;
    StandIn& operator=(const StandIn&) = delete;
    std::uint64_t cycles() const {
        ULONG64 value{};
        QueryProcessCycleTime(process_, &value);
        return value;
    }
    void fire(unsigned events, DWORD event) const { PostMessageW(window, fireMessage, events, static_cast<LPARAM>(event)); }
    // The accessibility calls its page has answered so far.
    unsigned long answeredCalls() const {
        DWORD_PTR value{};
        SendMessageTimeoutW(window, answeredMessage, 0, 0, SMTO_ABORTIFHUNG, 2000, &value);
        return static_cast<unsigned long>(value);
    }
    HWND window{};
    DWORD pid{};
private:
    HANDLE process_{}, ready_{}, parent_{};
};

// CPU cycles to microseconds: this thread's cycle counter against the
// performance counter over a short busy wait.
double cyclesPerMicrosecond() {
    LARGE_INTEGER frequency, begin, end;
    QueryPerformanceFrequency(&frequency);
    ULONG64 c0{}, c1{};
    QueryThreadCycleTime(GetCurrentThread(), &c0);
    QueryPerformanceCounter(&begin);
    do QueryPerformanceCounter(&end);
    while (static_cast<double>(end.QuadPart - begin.QuadPart) / static_cast<double>(frequency.QuadPart) < .2);
    QueryThreadCycleTime(GetCurrentThread(), &c1);
    return static_cast<double>(c1 - c0) / (static_cast<double>(end.QuadPart - begin.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart));
}
double rate{1};
std::uint64_t threadCycles() {
    ULONG64 value{};
    QueryThreadCycleTime(GetCurrentThread(), &value);
    return value;
}
std::uint64_t processCycles() {
    ULONG64 value{};
    QueryProcessCycleTime(GetCurrentProcess(), &value);
    return value;
}
double micro(std::uint64_t cycles) { return static_cast<double>(cycles) / rate; }
double seconds() {
    LARGE_INTEGER frequency, value;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart) / static_cast<double>(frequency.QuadPart);
}
// Pumps this thread's messages until `done` (or the bound).
template <class Done> bool pump(Done done, double bound = 10) {
    const double end = seconds() + bound;
    while (!done()) {
        if (seconds() > end) return false;
        MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return true;
}

// One bar reading across processes with the production reader.
void readingCost(StandIn& standIn) {
    const auto handle = reinterpret_cast<std::uintptr_t>(standIn.window);
    const HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::atomic<unsigned> notified{};
    n::NowPlayingControlReader reader([] { return seconds(); });
    reader.setNotify([&] {
        ++notified;
        SetEvent(done);
    });
    const auto read = [&](const char* what) {
        const auto before = notified.load();
        reader.reading(handle, standIn.pid, true);
        while (notified.load() == before) check(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, what);
    };
    read("The stand-in's empty page is read (and probed)");
    if (reader.awaiting()) {
        const auto before = notified.load();
        check(reader.treeChanged(standIn.pid), "Its page's first event reads it again");
        while (notified.load() == before) check(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "The built page is read");
    }
    const auto first = reader.reading(handle, standIn.pid, false);
    check(first && first->fraction && first->playPause && first->previous && first->next && first->title, "The stand-in's bar reads like NetEase's");
    for (unsigned k = 0; k < 10; ++k) read("A warm-up reading completes");
    constexpr unsigned count = 200;
    const auto stats0 = reader.stats();
    const auto calls0 = standIn.answeredCalls();
    const auto own0 = processCycles();
    const auto other0 = standIn.cycles();
    const double wall0 = seconds();
    for (unsigned k = 0; k < count; ++k) read("A timed reading completes");
    const double wall = seconds() - wall0;
    const auto own = micro(processCycles() - own0) / count;
    const auto other = micro(standIn.cycles() - other0) / count;
    const auto calls = static_cast<double>(standIn.answeredCalls() - calls0) / count;
    const auto stats = reader.stats();
    const auto reading = reader.reading(handle, standIn.pid, false);
    check(reading && reading->fraction && *reading->fraction > 0 && reading->title, "Timed readings read the moving bar under the window's title");
    std::cout << "bar reading across processes: " << fixed(own, 0) << " us CPU here (reader on the OS pool), " << fixed(other, 0) << " us in the player, "
              << fixed(wall / count * 1e6, 0) << " us wall each; " << fixed(calls, 1) << " accessibility calls answered by the player, "
              << fixed(static_cast<double>(stats.rereads - stats0.rereads) / count, 2) << " re-reads (" << fixed(static_cast<double>(stats.quick - stats0.quick) / count, 2)
              << " of the bar's progress and play state only), " << fixed(static_cast<double>(stats.walks - stats0.walks) / count, 2) << " walks per reading\n";
    check(stats.walks == stats0.walks && calls <= 10, "A playing bar is re-read without a walk, with few accessibility calls (" + fixed(calls, 1) + " each)");
    readingHere = own;
    readingThere = other;
    readingCalls = calls;
    reader.shutdown();
    pump([] { return false; }, .3);
    CloseHandle(done);
}

// One out-of-context WinEvent from the player's process on the UI thread
// (the presented hooks' configuration, scoped to the stand-in here).
std::atomic<unsigned> delivered{};
void CALLBACK counted(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD) {
    // The production callback's non-window path is a registry lookup and an
    // atomic read; window objects are not fired here.
    if (!(object == OBJID_WINDOW && child == CHILDID_SELF && window)) ++delivered;
}
void hookCost(StandIn& standIn) {
    constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    const HWINEVENTHOOK lifetime = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, counted, standIn.pid, 0, flags);
    const HWINEVENTHOOK titles = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, counted, standIn.pid, 0, flags);
    check(lifetime && titles, "The presented hooks' configuration installs");
    pump([] { return false; }, .2);
    for (const DWORD event : {static_cast<DWORD>(EVENT_OBJECT_NAMECHANGE), static_cast<DWORD>(EVENT_OBJECT_SHOW)}) {
        constexpr unsigned batches = 20, batch = 500;
        delivered = 0;
        const auto ui0 = threadCycles();
        for (unsigned b = 0; b < batches; ++b) {
            standIn.fire(batch, event);
            if (!pump([&] { return delivered.load() >= (b + 1) * batch; }, 5)) break;
        }
        const auto ui = micro(threadCycles() - ui0);
        const auto got = delivered.load();
        check(got > 0, "WinEvents of the player's page reach the UI thread");
        std::cout << "WinEvent " << (event == EVENT_OBJECT_NAMECHANGE ? "NAMECHANGE" : "SHOW") << " of a player page object: " << fixed(ui / got, 1)
                  << " us of UI thread each (" << got << " delivered)\n";
        hookEach = std::max(hookEach, ui / got);
    }
    UnhookWinEvent(lifetime);
    UnhookWinEvent(titles);
    pump([] { return false; }, .2);
}

// One top-level window enumeration, as the window-title provider's read
// does it for every window (its process looked up in a bounded cache).
void enumerationCost() {
    struct Walk {
        std::vector<std::pair<DWORD, bool>> classified;
        unsigned windows{};
    };
    const auto once = [](Walk& w) {
        w.windows = 0;
        EnumWindows([](HWND window, LPARAM state) -> BOOL {
            auto& walk = *reinterpret_cast<Walk*>(state);
            ++walk.windows;
            DWORD pid{};
            if (!GetWindowThreadProcessId(window, &pid) || !pid) return TRUE;
            const auto found = std::find_if(walk.classified.begin(), walk.classified.end(), [&](const auto& e) { return e.first == pid; });
            if (found == walk.classified.end() && walk.classified.size() < 256) walk.classified.emplace_back(pid, false);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&w));
    };
    Walk walk;
    once(walk);
    constexpr unsigned count = 200;
    const auto c0 = threadCycles();
    for (unsigned k = 0; k < count; ++k) once(walk);
    const auto each = micro(threadCycles() - c0) / count;
    std::cout << "top-level window enumeration: " << fixed(each, 0) << " us each over " << walk.windows << " windows of " << walk.classified.size()
              << " processes\n";
    enumerationEach = each;
}

// The window-title system of the pipeline: the stand-in as NetEase's main
// window (found by a real enumeration of the desktop), read and pressed by
// the production reader, whose completions wake the provider.
class StandInDesktop final : public n::NowPlayingWindowSystem {
public:
    StandInDesktop(const StandIn& standIn, std::function<double()> clock) : standIn_(standIn), reader_(std::make_shared<n::NowPlayingControlReader>(std::move(clock))) {}
    ~StandInDesktop() override { reader_->shutdown(); }
    std::function<void()> watch(std::function<void()> changed) override {
        reader_->setNotify(changed);
        return [reader = reader_] { reader->setNotify({}); };
    }
    std::vector<m::NowPlayingPlayerWindow> windows() override {
        ++enumerations;
        struct Walk {
            HWND target{};
            DWORD pid{};
            bool found{};
        } walk{standIn_.window, standIn_.pid};
        EnumWindows([](HWND window, LPARAM state) -> BOOL {
            auto& w = *reinterpret_cast<Walk*>(state);
            DWORD pid{};
            GetWindowThreadProcessId(window, &pid);
            if (window == w.target && pid == w.pid) w.found = true;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&walk));
        if (!walk.found) return {};
        wchar_t title[256]{};
        const int length = GetWindowTextW(standIn_.window, title, 256);
        std::string text;
        for (int k = 0; k < length; ++k) text.push_back(static_cast<char>(title[k]));
        return {{reinterpret_cast<std::uintptr_t>(standIn_.window), standIn_.pid, executable, "OrpheusBrowserHost", text, true, false, std::nullopt}};
    }
    n::NowPlayingServiceFailure command(std::uintptr_t, std::uint32_t, m::NowPlayingCommandKind, bool) override { return n::NowPlayingServiceFailure::unsupported; }
    std::optional<n::NowPlayingPlayerReading> reading(std::uintptr_t window, std::uint32_t pid, bool refresh) override {
        if (refresh) ++refreshes;
        return reader_->reading(window, pid, refresh);
    }
    bool notifiesReadings() const noexcept override { return reader_->notifies(); }
    bool press(std::uintptr_t, std::uint32_t, m::NowPlayingCommandKind, std::function<void(bool)>) override { return false; }
    void shutdown() noexcept override { reader_->shutdown(); }
    n::NowPlayingControlReader& reader() { return *reader_; }
    std::atomic<unsigned> enumerations{}, refreshes{};
private:
    const StandIn& standIn_;
    std::shared_ptr<n::NowPlayingControlReader> reader_;
};

// The production owner presented, then closed, on the real clock against the
// stand-in (its own media session like NetEase's SMTC, the bar read across
// processes, the queue's length and the song's lyrics), with the retained
// scene's content synchronized on the UI thread as the preview does.
// `reference`: a well-behaved media session instead (its timeline drives the
// time; no window-title player), for what a playing track costs anyway.
void pipelineCost(StandIn& standIn, bool reference = false) {
    const DWORD ui = GetCurrentThreadId();
    constexpr UINT wakeMessage = WM_APP + 7;
    std::atomic<bool> flagged{};
    const auto wake = [ui, &flagged] {
        if (!flagged.exchange(true)) PostThreadMessageW(ui, wakeMessage, 0, 0);
    };
    const auto clock = [] { return seconds(); };
    SimPlayer player; // its song length matches the stand-in's bar
    player.anchorTime = seconds() - 60;
    auto state = std::make_shared<SmtcState>();
    double smtcNow = seconds();
    state->player = &player;
    state->now = &smtcNow;
    state->usable = reference;
    auto desktop = std::make_shared<StandInDesktop>(standIn, clock);
    Q q{wake};
    Catalog catalog;
    std::atomic<unsigned> wakes{};
    n::NativeNowPlayingService::Factory windows = [] { return std::shared_ptr<n::NowPlayingProvider>(); };
    if (!reference) windows = n::nowPlayingWindowProvider([desktop] { return desktop; }, clock);
    n::NativeNowPlayingService service(q,
        n::nowPlayingCompositeProvider([state]() -> std::shared_ptr<n::NowPlayingProvider> { return std::make_shared<Smtc>(state); }, windows),
        [&] { ++wakes; wake(); }, clock);
    auto web = std::make_shared<n::NowPlayingWeb>(catalog.bind(), wake, clock);
    t::NowPlayingSessionOptions o;
    o.web = web;
    o.neteasePlayingList = [] { return queueFile(); };
    o.decode = [](std::span<const std::uint8_t>) { return n::NowPlayingImageResult{}; };
    t::NowPlayingSession owner(service, q, m::NowPlayingAppearance{}, wake, std::move(o));
    std::unique_ptr<n::LayerRasterizer> raster;
    std::unique_ptr<n::NativeNowPlayingScene> scene;
    try {
        raster = std::make_unique<n::LayerRasterizer>();
        n::LayerRasterOptions options;
        options.pixelsPerPoint = 2;
        options.paddingPoints = 1;
        scene = std::make_unique<n::NativeNowPlayingScene>(owner.presentation(), *raster, options);
    } catch (const std::exception& e) {
        std::cout << "(retained scene not measured: " << e.what() << ")\n";
    }
    unsigned publishes{}, rasters{};
    const auto content = [&] {
        if (!scene) return;
        const auto before = scene->stats().localRasterUpdates;
        scene->syncContent(owner.artwork(), seconds());
        rasters += static_cast<unsigned>(scene->stats().localRasterUpdates - before);
    };
    struct Window {
        double ui{}, process{}, player{}, seconds{};
        unsigned loops{};
    };
    const auto loop = [&](double length, unsigned& loops) {
        const double end = seconds() + length;
        while (seconds() < end) {
            double now = seconds();
            double wait = end - now;
            if (const auto due = owner.nextWakeTime(now)) wait = std::min(wait, std::max(0., *due - now));
            MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(std::ceil(wait * 1000)), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            ++loops;
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            now = seconds();
            smtcNow = now;
            if (flagged.exchange(false)) {
                q.drain();
                if (owner.utilityCompleted(now)) {
                    ++publishes;
                    content();
                }
            }
            if (catalog.answer()) wake();
            if (const auto due = owner.nextWakeTime(now); due && *due <= now) {
                if (owner.deadline(now)) content();
            }
        }
    };
    const auto measure = [&](double length) {
        unsigned loops{};
        const auto ui0 = threadCycles();
        const auto all0 = processCycles();
        const auto other0 = standIn.cycles();
        const double w0 = seconds();
        loop(length, loops);
        const double w = seconds() - w0;
        return Window{micro(threadCycles() - ui0) / w, micro(processCycles() - all0) / w, micro(standIn.cycles() - other0) / w, w, loops};
    };
    owner.setActive(true, seconds());
    content();
    // Activation, the first walk (and probe), the queue's length and lyrics.
    unsigned warm{};
    loop(4, warm);
    check(owner.presentation().input().track && owner.presentation().input().track->duration && owner.presentation().input().track->position,
          "The presented stand-in has a time from its bar and the queue's length");
    const auto e0 = desktop->enumerations.load();
    const auto r0 = desktop->refreshes.load();
    const auto s0 = state->reads.load();
    const auto reader0 = desktop->reader().stats();
    const auto p0 = publishes;
    const auto w0 = wakes.load();
    const auto raster0 = rasters;
    const auto open = measure(10);
    const auto reader1 = desktop->reader().stats();
    const auto per = [&](double v) { return fixed(v / open.seconds); };
    const char* name = reference ? "reference media session pipeline" : "pipeline";
    std::cout << name << " open, playing (" << fixed(open.seconds, 1) << " s): UI thread " << fixed(open.ui / 1e4, 3) << " % of a core, process "
              << fixed(open.process / 1e4, 3) << " %, stand-in " << fixed(open.player / 1e4, 3) << " %; per second: enumerations "
              << per(desktop->enumerations.load() - e0) << ", media reads " << per(state->reads.load() - s0) << ", bar readings "
              << per(static_cast<double>(reader1.readings - reader0.readings)) << " (refresh requests " << per(desktop->refreshes.load() - r0)
              << "), owner wakes " << per(wakes.load() - w0) << ", publications " << per(publishes - p0) << ", text rasters " << per(rasters - raster0)
              << ", UI loop wakes " << per(open.loops) << "\n";
    check(reference || reader1.readings > reader0.readings, "The bar is read while Now Playing is presented");
    owner.setActive(false, seconds());
    const auto e1 = desktop->enumerations.load();
    const auto r1 = desktop->reader().stats();
    const auto s1 = state->reads.load();
    const auto closed = measure(8);
    std::cout << name << " closed (" << fixed(closed.seconds, 1) << " s): UI thread " << fixed(closed.ui / 1e4, 3) << " % of a core, process "
              << fixed(closed.process / 1e4, 3) << " %, stand-in " << fixed(closed.player / 1e4, 3) << " %; enumerations "
              << desktop->enumerations.load() - e1 << ", media reads " << state->reads.load() - s1 << ", bar readings "
              << desktop->reader().stats().readings - r1.readings << ", UI loop wakes " << closed.loops << "\n";
    check(desktop->enumerations.load() == e1 && state->reads.load() == s1 && desktop->reader().stats().readings == r1.readings,
          "Closed: no enumeration, media read or bar reading on the real clock");
    if (!reference) pipelineOpen = open.ui;
    service.setActive(false);
    q.waitIdle();
    q.drain();
    scene.reset();
    raster.reset();
}

void live() {
    rate = cyclesPerMicrosecond();
    std::cout << "cycle counter: " << fixed(rate, 0) << " per microsecond\n";
    StandIn standIn(400);
    readingCost(standIn);
    hookCost(standIn);
    enumerationCost();
    pipelineCost(standIn);
    pipelineCost(standIn, true);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 6 && std::wcscmp(argv[1], L"--stand-in") == 0) {
        const auto parent = static_cast<DWORD>(std::wcstoul(argv[2], nullptr, 10));
        const auto handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(argv[3], nullptr, 10)));
        const auto ready = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(argv[4], nullptr, 10)));
        return standIn(parent, handle, ready, static_cast<unsigned>(std::wcstoul(argv[5], nullptr, 10)));
    }
    try {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) throw std::runtime_error("COM");
        cadence();
        reference();
        live();
        CoUninitialize();
        std::cout << "Now Playing NetEase cadence and cost: " << checks << " checks passed (simulated player, hidden stand-in process)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Now Playing NetEase cadence and cost failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
