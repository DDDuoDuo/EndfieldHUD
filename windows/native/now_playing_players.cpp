#include "native/now_playing_players.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace endfield::native {
namespace {
namespace m = endfield::modules;
using Failure = NowPlayingServiceFailure;

// ---------------------------------------------------------------------------
// Window-title provider
// ---------------------------------------------------------------------------
struct Player {
    std::uint64_t token{};
    std::uintptr_t window{};
    std::uint32_t pid{};
    std::string identity;
    const m::NowPlayingWindowPlayer* rules{};
    std::optional<m::NowPlayingTitleTrack> last; // the last track its title showed
    // Title-inferred playing state (used while the process's audio activity
    // is unknown) and the idle caption.
    bool inferred{}, idle{};
    bool playing{};                     // presented state of the last read
    m::NowPlayingWindowTimeline timeline; // estimated elapsed time of `last`
    double trackSeen{};                 // when this provider saw `last` appear
    std::optional<bool> rendered;       // the process's audio activity last seen
    double renderedAt{};                // when it changed
    bool resync{true};                  // ask the player's controls again
    std::optional<NowPlayingPlayerReading> reading; // of the current track
    std::optional<double> anchored;     // the reading the estimate was anchored on
    // The progress bar of the reading the time is anchored on: its played
    // fraction, when it was read and the playing time since (pauses
    // counted); provisional while only a minimized window's bar was seen,
    // frozen once such a bar stood still while playing. The bar fraction
    // last read, and the cover the bar showed.
    std::optional<double> fraction, fractionAt, fractionSeen, fractionSeenAt;
    bool provisional{}, frozen{};
    m::NowPlayingWindowTimeline sinceFraction;
    std::optional<std::string> artwork;
    std::uint64_t revision{1}, seen{};
};
struct WindowShared {
    std::mutex mutex;
    NowPlayingProvider::Changed changed;
    std::function<void()> unwatch;
    bool stopped{};
    std::vector<Player> players;
    std::uint64_t nextToken{1}, generation{}, selected{};
    void event() {
        NowPlayingProvider::Changed callback;
        {
            std::lock_guard lock(mutex);
            if (stopped) return;
            callback = changed;
        }
        if (callback) try { callback(); } catch (...) {}
    }
};
int rank(const Player& p) noexcept { return p.last && !p.idle ? (p.playing ? 3 : 2) : p.last ? 2 : 1; }
// A recorded title change is usable when it is finite and not in the future
// of this read (clocks are the same host monotonic clock).
std::optional<double> changeTime(const std::optional<double>& value, double now) noexcept {
    if (!value || !std::isfinite(*value) || !std::isfinite(now) || *value > now + 1e-6) return std::nullopt;
    return value;
}

class WindowProvider final : public NowPlayingProvider {
public:
    WindowProvider(std::shared_ptr<NowPlayingWindowSystem> system, std::function<double()> now, std::shared_ptr<const NowPlayingProcessActivity> activity)
        : system_(std::move(system)), now_(std::move(now)), activity_(std::move(activity)), shared_(std::make_shared<WindowShared>()) {
        if (!system_ || !now_) throw std::invalid_argument("The window-title fallback needs a window system and the host clock");
        const std::weak_ptr<WindowShared> weak = shared_;
        auto unwatch = system_->watch([weak] { if (const auto s = weak.lock()) s->event(); });
        std::lock_guard lock(shared_->mutex);
        shared_->unwatch = std::move(unwatch);
    }
    ~WindowProvider() override { stop(); }
    void start(Changed changed, Ready ready) override {
        {
            std::lock_guard lock(shared_->mutex);
            if (shared_->stopped) return;
            shared_->changed = std::move(changed);
        }
        ready({});
    }
    void read(Read completion) override {
        NowPlayingReadResult result;
        std::vector<m::NowPlayingPlayerWindow> windows;
        try {
            windows = system_->windows();
        } catch (...) {
            {
                std::lock_guard lock(shared_->mutex);
                if (shared_->stopped) return;
            }
            result.failure = Failure::unavailable;
            completion(std::move(result));
            return;
        }
        if (windows.size() > nowPlayingMaximumPlayerWindows) windows.resize(nowPlayingMaximumPlayerWindows);
        // Group by process (enumeration order), then pick each main window.
        struct Found { std::size_t index; const m::NowPlayingWindowPlayer* rules; };
        std::vector<Found> found;
        std::vector<std::uint32_t> pids;
        for (const auto& w : windows) {
            if (!w.pid || std::find(pids.begin(), pids.end(), w.pid) != pids.end()) continue;
            const auto* rules = m::nowPlayingWindowPlayer(w.executable);
            if (!rules || w.executable.empty() || !ehud::data::Json::validUtf8(w.executable) || w.executable.size() > 4096) continue;
            pids.push_back(w.pid);
            if (found.size() >= nowPlayingMaximumPlayerProcesses) continue;
            std::vector<m::NowPlayingPlayerWindow> process;
            std::vector<std::size_t> indices;
            for (std::size_t k = 0; k < windows.size(); ++k)
                if (windows[k].pid == w.pid && windows[k].executable == w.executable) {
                    process.push_back(windows[k]);
                    indices.push_back(k);
                }
            if (const auto main = m::nowPlayingMainWindow(*rules, process)) found.push_back({indices[*main], rules});
        }
        const double time = now_();
        {
            std::lock_guard lock(shared_->mutex);
            if (shared_->stopped) return;
            auto& s = *shared_;
            ++s.generation;
            for (const auto& f : found) {
                const auto& w = windows[f.index];
                // The same process keeps its session (and inferred state) when
                // its main window changes, e.g. between full and mini mode.
                auto existing = std::find_if(s.players.begin(), s.players.end(), [&](const Player& p) { return p.pid == w.pid && p.identity == w.executable; });
                if (existing == s.players.end()) {
                    if (s.nextToken >= nowPlayingWindowTokenBit) throw std::runtime_error("Window-title session tokens exhausted");
                    Player next;
                    next.token = s.nextToken++;
                    next.pid = w.pid;
                    next.identity = w.executable;
                    next.rules = f.rules;
                    s.players.push_back(std::move(next));
                    existing = s.players.end() - 1;
                }
                auto& p = *existing;
                if (p.window != w.window) p.resync = true;
                p.window = w.window;
                p.seen = s.generation;
                if (auto track = m::nowPlayingWindowTitleTrack(*f.rules, w.title)) {
                    if (!p.last || *p.last != *track) {
                        p.last = std::move(track);
                        p.inferred = true;
                        // The track began when its title appeared, if that was observed.
                        p.timeline.restart(changeTime(w.titleChangedAt, time));
                        p.trackSeen = time;
                        p.resync = true;
                        p.fraction.reset();
                        p.fractionAt.reset();
                        p.fractionSeen.reset();
                        p.fractionSeenAt.reset();
                        p.provisional = p.frozen = false;
                        p.artwork.reset();
                        ++p.revision;
                    } else if (p.idle) p.inferred = true;
                    p.idle = false;
                } else if (p.last && !p.idle) {
                    p.idle = true;
                    p.inferred = false;
                }
                // The process's audio activity, when published, is the playing
                // state; a change asks the player's controls again.
                std::optional<bool> rendering;
                if (activity_) rendering = activity_->rendering(p.pid);
                if (rendering != p.rendered) {
                    p.rendered = rendering;
                    p.renderedAt = time;
                    p.resync = true;
                }
                p.playing = p.last && !p.idle && rendering.value_or(p.inferred);
                // The player's own controls (accessibility): a reading begun
                // after this track appeared states its elapsed and total time
                // and, when newer than the audio activity, its play state.
                p.reading.reset();
                if (p.last && !p.idle) {
                    std::optional<NowPlayingPlayerReading> reading;
                    try {
                        reading = system_->reading(p.window, p.pid, p.resync);
                    } catch (...) {
                        reading.reset();
                    }
                    p.resync = false;
                    if (reading && reading->startedAt + 1e-9 >= p.trackSeen && std::isfinite(reading->sampledAt) && reading->sampledAt <= time + 1e-6) {
                        p.reading = reading;
                        if (reading->playing && (reading->startedAt + 1e-9 >= p.renderedAt || !rendering)) p.playing = *reading->playing;
                    }
                }
                if (p.last) {
                    if (p.reading && p.reading->position && p.reading->visible && p.anchored != p.reading->sampledAt) {
                        // Anchor the estimate on the player's own clock once per
                        // reading; pauses seen afterwards still count.
                        p.anchored = p.reading->sampledAt;
                        p.timeline.restart(p.reading->sampledAt - *p.reading->position);
                        p.timeline.observe(p.reading->playing.value_or(p.playing), p.reading->sampledAt);
                    }
                    // The bar describes this track only while the window shows
                    // the title it was read under.
                    const bool shown = p.reading && p.reading->title && *p.reading->title == w.title;
                    if (shown && p.reading->fraction && std::isfinite(*p.reading->fraction) && p.fractionSeenAt != p.reading->sampledAt) {
                        // Every reading of a shown window resynchronizes. A
                        // minimized Chromium page may stop drawing its bar: its
                        // reading counts once the bar moved since the last one;
                        // the first one is shown provisionally and withdrawn
                        // when the bar stands still for 1.5 s while playing.
                        const double value = std::clamp(*p.reading->fraction, 0., 1.);
                        const bool moved = p.fractionSeen && *p.fractionSeen != value;
                        const bool playing = p.reading->playing.value_or(p.playing);
                        const auto anchor = [&](bool provisional) {
                            p.fraction = value;
                            p.fractionAt = p.reading->sampledAt;
                            p.provisional = provisional;
                            if (!provisional) p.frozen = false;
                            p.sinceFraction.restart(p.reading->sampledAt);
                            p.sinceFraction.observe(playing, p.reading->sampledAt);
                        };
                        p.fractionSeenAt = p.reading->sampledAt;
                        if (p.reading->visible || moved) anchor(false);
                        else if (!p.fraction && !p.frozen) anchor(true);
                        else if (p.provisional && playing && p.fractionAt && p.reading->sampledAt - *p.fractionAt >= 1.5) {
                            p.fraction.reset();
                            p.fractionAt.reset();
                            p.provisional = false;
                            p.frozen = true;
                        }
                        p.fractionSeen = value;
                    }
                    if (shown && p.reading->artwork) p.artwork = p.reading->artwork;
                    p.timeline.observe(p.playing, time);
                    if (p.fraction) p.sinceFraction.observe(p.playing, time);
                }
            }
            std::erase_if(s.players, [&](const Player& p) { return p.seen != s.generation; });
            // Current: the highest rank; the previous current wins a tie, then
            // enumeration (z) order.
            const Player* current{};
            for (const auto& f : found) {
                const auto& w = windows[f.index];
                const auto& p = *std::find_if(s.players.begin(), s.players.end(), [&](const Player& x) { return x.pid == w.pid && x.identity == w.executable; });
                if (!current || rank(p) > rank(*current) || (rank(p) == rank(*current) && p.token == s.selected && current->token != s.selected)) current = &p;
            }
            for (const auto& f : found) {
                const auto& w = windows[f.index];
                const auto& p = *std::find_if(s.players.begin(), s.players.end(), [&](const Player& x) { return x.pid == w.pid && x.identity == w.executable; });
                result.value.sessions.push_back({p.token, p.identity, p.identity, m::NowPlayingSessionOrigin::playerWindow});
            }
            s.selected = current ? current->token : 0;
            if (current) {
                result.value.session = m::NowPlayingSession{current->token, current->identity, current->identity, m::NowPlayingSessionOrigin::playerWindow};
                result.value.metadataRevision = current->revision;
                if (current->last) {
                    m::NowPlayingTrack track;
                    track.title = current->last->title;
                    track.artist = current->last->artist;
                    track.isPlaying = current->playing;
                    track.sampledAt = std::isfinite(time) ? time : 0;
                    track.position = current->timeline.position(time);
                    if (current->reading && current->reading->duration) track.duration = current->reading->duration;
                    track.supportsSeeking = false;
                    result.value.track = m::NowPlayingTrack::bounded(std::move(track));
                    result.value.capabilities.toggle = result.value.capabilities.previous = result.value.capabilities.next = true;
                    if (current->fraction)
                        result.value.progress = NowPlayingMediaSnapshot::Progress{*current->fraction, std::max(0., current->sinceFraction.position(time).value_or(0))};
                    if (current->artwork && current->artwork->size() <= 2048) result.value.artworkURL = current->artwork;
                    result.value.playerControls = current->reading.has_value();
                }
            }
        }
        completion(std::move(result));
    }
    void perform(std::uint64_t token, std::uint64_t, m::NowPlayingCommand command, Ready ready) override {
        std::uintptr_t window{};
        std::uint32_t pid{};
        bool onlyPlayer{};
        {
            std::lock_guard lock(shared_->mutex);
            if (shared_->stopped) return;
            const auto found = std::find_if(shared_->players.begin(), shared_->players.end(), [&](const Player& p) { return p.token == token; });
            if (found != shared_->players.end() && found->last) {
                window = found->window;
                pid = found->pid;
                // The held media key reaches whoever registered it: only the
                // one window-title player application may be its holder.
                onlyPlayer = std::all_of(shared_->players.begin(), shared_->players.end(), [&](const Player& p) { return p.rules == found->rules; });
            }
        }
        if (!window) {
            ready({Failure::stale, 0});
            return;
        }
        if (command.kind == m::NowPlayingCommandKind::seek) {
            ready({Failure::unsupported, 0});
            return;
        }
        // The player's own control first (its accessibility default action:
        // it reaches exactly this player), then its media-key hotkey or
        // WM_APPCOMMAND (modules::nowPlayingWindowTransport).
        const auto shared = shared_;
        const auto system = system_;
        const auto kind = command.kind;
        const auto finish = [shared, token, kind](Failure failure, const Ready& done) {
            {
                std::lock_guard lock(shared->mutex);
                if (shared->stopped) return;
                for (auto& p : shared->players)
                    if (p.token == token && failure == Failure::none) {
                        // The title does not report pausing: a delivered toggle flips
                        // the inferred state until the title, the audio activity or
                        // the player's controls say otherwise.
                        if (kind == m::NowPlayingCommandKind::playPause) p.inferred = !p.inferred;
                        p.resync = true;
                    }
            }
            done({failure, 0});
        };
        const auto fallback = [system, window, pid, kind, onlyPlayer]() {
            try {
                return system->command(window, pid, kind, onlyPlayer);
            } catch (...) {
                return Failure::unavailable;
            }
        };
        bool pressing{};
        try {
            pressing = system->press(window, pid, kind, [finish, fallback, ready](bool pressed) {
                finish(pressed ? Failure::none : fallback(), ready);
            });
        } catch (...) {
            pressing = false;
        }
        if (!pressing) finish(fallback(), ready);
    }
    void resynchronize() noexcept override {
        std::lock_guard lock(shared_->mutex);
        for (auto& p : shared_->players)
            if (p.reading) p.resync = true;
    }
    void stop() noexcept override {
        std::function<void()> unwatch;
        {
            std::lock_guard lock(shared_->mutex);
            shared_->stopped = true;
            shared_->changed = nullptr;
            unwatch = std::move(shared_->unwatch);
            shared_->unwatch = nullptr;
        }
        if (unwatch) try { unwatch(); } catch (...) {}
    }
private:
    std::shared_ptr<NowPlayingWindowSystem> system_;
    std::function<double()> now_;
    std::shared_ptr<const NowPlayingProcessActivity> activity_;
    std::shared_ptr<WindowShared> shared_;
};

// ---------------------------------------------------------------------------
// Composite provider
// ---------------------------------------------------------------------------
enum class Phase : std::uint8_t { starting, ready, failed };
struct CompositeShared {
    std::mutex mutex;
    NowPlayingProvider::Ready ready;
    NowPlayingProvider::Changed changed;
    std::array<Phase, 2> phase{Phase::starting, Phase::starting};
    std::array<NowPlayingCommandResult, 2> failure{};
    bool answered{}, stopped{};
    bool reading{}; // a read was issued after the composite became ready
    // The last merged snapshot presented the window-title player: it keeps
    // ties, so pausing it from the HUD does not hand over to a paused session.
    bool presentedWindows{};
    void started(std::size_t part, NowPlayingCommandResult result) {
        NowPlayingProvider::Ready answer;
        NowPlayingProvider::Changed refresh;
        NowPlayingCommandResult value{};
        {
            std::lock_guard lock(mutex);
            if (stopped || phase[part] != Phase::starting) return;
            phase[part] = result.failure == Failure::none ? Phase::ready : Phase::failed;
            failure[part] = result;
            if (!answered) {
                if (phase[part] == Phase::ready) {
                    answered = true;
                    answer = std::move(ready);
                } else if (phase[0] == Phase::failed && phase[1] == Phase::failed) {
                    answered = true;
                    answer = std::move(ready);
                    value = failure[0];
                }
            } else if (reading) refresh = changed; // a late source joins (or its failure shows) with one re-read
        }
        if (answer) answer(value);
        if (refresh) refresh();
    }
};
struct Join {
    std::mutex mutex;
    std::size_t remaining{};
    std::array<std::optional<NowPlayingReadResult>, 2> results;
    NowPlayingProvider::Read completion;
    std::shared_ptr<CompositeShared> shared;
    void arrive(std::size_t part, NowPlayingReadResult result) {
        NowPlayingProvider::Read done;
        std::array<std::optional<NowPlayingReadResult>, 2> all;
        {
            std::lock_guard lock(mutex);
            if (!remaining || results[part]) return; // at most once per part
            results[part] = std::move(result);
            if (--remaining) return;
            done = std::move(completion);
            all = std::move(results);
        }
        bool preferWindows{};
        {
            std::lock_guard lock(shared->mutex);
            if (shared->stopped) return;
            preferWindows = shared->presentedWindows;
        }
        NowPlayingReadResult merged;
        try {
            merged = nowPlayingMergeReads(std::move(all[0]), std::move(all[1]), preferWindows);
        } catch (...) {
            merged = {};
            merged.failure = Failure::unavailable;
        }
        if (merged.failure == Failure::none) {
            std::lock_guard lock(shared->mutex);
            shared->presentedWindows = merged.value.session && (merged.value.session->token & nowPlayingWindowTokenBit);
        }
        if (done) done(std::move(merged));
    }
};
class CompositeProvider final : public NowPlayingProvider {
public:
    CompositeProvider(std::shared_ptr<NowPlayingProvider> media, std::shared_ptr<NowPlayingProvider> windows)
        : parts_{std::move(media), std::move(windows)}, shared_(std::make_shared<CompositeShared>()) {}
    ~CompositeProvider() override { stop(); }
    void start(Changed changed, Ready ready) override {
        {
            std::lock_guard lock(shared_->mutex);
            if (shared_->stopped) return;
            shared_->ready = std::move(ready);
            shared_->changed = changed;
        }
        for (std::size_t part = 0; part < parts_.size(); ++part) {
            const auto shared = shared_;
            if (!parts_[part]) {
                shared->started(part, {Failure::unavailable, 0});
                continue;
            }
            try {
                parts_[part]->start(changed, [shared, part](NowPlayingCommandResult result) { shared->started(part, result); });
            } catch (...) {
                shared->started(part, {Failure::unavailable, 0});
            }
        }
    }
    void read(Read completion) override {
        std::array<bool, 2> ready{};
        std::optional<NowPlayingCommandResult> mediaFailure;
        {
            std::lock_guard lock(shared_->mutex);
            if (shared_->stopped) return;
            shared_->reading = true;
            for (std::size_t part = 0; part < 2; ++part) ready[part] = shared_->phase[part] == Phase::ready;
            if (shared_->phase[0] == Phase::failed) mediaFailure = shared_->failure[0];
        }
        auto join = std::make_shared<Join>();
        join->remaining = static_cast<std::size_t>(ready[0]) + static_cast<std::size_t>(ready[1]);
        join->completion = std::move(completion);
        join->shared = shared_;
        // GSMTC that failed to start still reports its failure when no
        // window-title player is there to present instead.
        if (mediaFailure) {
            NowPlayingReadResult failed;
            failed.failure = mediaFailure->failure;
            failed.nativeError = mediaFailure->nativeError;
            join->results[0] = std::move(failed);
        }
        if (!join->remaining) {
            NowPlayingReadResult none;
            none.failure = Failure::unavailable;
            if (join->results[0]) none = std::move(*join->results[0]);
            auto done = std::move(join->completion);
            done(std::move(none));
            return;
        }
        for (std::size_t part = 0; part < 2; ++part) {
            if (!ready[part]) continue;
            try {
                parts_[part]->read([join, part](NowPlayingReadResult result) { join->arrive(part, std::move(result)); });
            } catch (...) {
                NowPlayingReadResult failed;
                failed.failure = Failure::unavailable;
                join->arrive(part, std::move(failed));
            }
        }
    }
    void perform(std::uint64_t token, std::uint64_t revision, m::NowPlayingCommand command, Ready ready) override {
        const std::size_t part = (token & nowPlayingWindowTokenBit) ? 1 : 0;
        bool usable{};
        {
            std::lock_guard lock(shared_->mutex);
            if (shared_->stopped) return;
            usable = shared_->phase[part] == Phase::ready;
        }
        if (!usable) {
            ready({Failure::stale, 0});
            return;
        }
        parts_[part]->perform(token & ~nowPlayingWindowTokenBit, revision, command, std::move(ready));
    }
    void resynchronize() noexcept override {
        for (auto& part : parts_)
            if (part) part->resynchronize();
    }
    void stop() noexcept override {
        {
            std::lock_guard lock(shared_->mutex);
            if (shared_->stopped) return;
            shared_->stopped = true;
            shared_->ready = nullptr;
            shared_->changed = nullptr;
        }
        for (auto& part : parts_)
            if (part) part->stop();
    }
private:
    std::array<std::shared_ptr<NowPlayingProvider>, 2> parts_;
    std::shared_ptr<CompositeShared> shared_;
};

int rank(const NowPlayingMediaSnapshot& v) noexcept {
    return v.session && v.track ? (v.track->isPlaying ? 3 : 2) : v.session ? 1 : 0;
}
bool usable(const std::optional<NowPlayingReadResult>& r) noexcept { return r && r->failure == Failure::none; }
} // namespace

NowPlayingReadResult nowPlayingMergeReads(std::optional<NowPlayingReadResult> media, std::optional<NowPlayingReadResult> windows, bool preferWindows) {
    // The service re-reads after a race; never present the fallback meanwhile.
    if (media && (media->failure == Failure::stale || media->failure == Failure::cancelled)) return std::move(*media);
    if (!usable(media) && !usable(windows)) {
        if (media) return std::move(*media);
        if (windows) return std::move(*windows);
        NowPlayingReadResult none;
        none.failure = Failure::unavailable;
        return none;
    }
    NowPlayingMediaSnapshot fallback;
    if (usable(windows)) {
        fallback = std::move(windows->value);
        // A player GSMTC lists keeps its media session (same known source
        // mapping, or a Win32 session identity naming the same image).
        std::vector<m::NowPlayingSource> listed;
        if (usable(media))
            for (const auto& s : media->value.sessions)
                if (const auto source = m::nowPlayingSourceForAppUserModelID(s.appUserModelID); source != m::NowPlayingSource::system) listed.push_back(source);
        const auto duplicate = [&](const m::NowPlayingSession& s) {
            if (std::find(listed.begin(), listed.end(), m::nowPlayingSourceForAppUserModelID(s.appUserModelID)) != listed.end()) return true;
            return usable(media) && std::any_of(media->value.sessions.begin(), media->value.sessions.end(),
                [&](const m::NowPlayingSession& session) { return m::nowPlayingSameExecutable(session.appUserModelID, s.appUserModelID); });
        };
        std::erase_if(fallback.sessions, duplicate);
        if (fallback.sessions.size() > nowPlayingMaximumPlayerProcesses) fallback.sessions.resize(nowPlayingMaximumPlayerProcesses);
        if (fallback.session && std::find(fallback.sessions.begin(), fallback.sessions.end(), *fallback.session) == fallback.sessions.end()) {
            fallback.session.reset();
            fallback.track.reset();
            fallback.artwork.reset();
            fallback.progress.reset();
            fallback.artworkURL.reset();
            fallback.playerControls = false;
        }
        for (auto& s : fallback.sessions) s.token |= nowPlayingWindowTokenBit;
        if (fallback.session) fallback.session->token |= nowPlayingWindowTokenBit;
    }
    if (!usable(media)) {
        if (media && !fallback.session) return std::move(*media);
        NowPlayingReadResult out;
        out.value = std::move(fallback);
        return out;
    }
    NowPlayingReadResult out = std::move(*media);
    auto& v = out.value;
    const bool presentFallback = rank(fallback) > rank(v) || (preferWindows && rank(fallback) >= 2 && rank(fallback) == rank(v));
    const std::size_t room = 128 - std::min<std::size_t>(128, fallback.sessions.size());
    if (v.sessions.size() > room) {
        // Keep the presented media session listed.
        std::vector<m::NowPlayingSession> kept;
        if (v.session && !presentFallback) kept.push_back(*v.session);
        for (const auto& s : v.sessions)
            if (kept.size() < room && (!v.session || presentFallback || !(s == *v.session))) kept.push_back(s);
        v.sessions = std::move(kept);
    }
    v.sessions.insert(v.sessions.end(), fallback.sessions.begin(), fallback.sessions.end());
    if (presentFallback) {
        v.session = fallback.session;
        v.track = std::move(fallback.track);
        v.capabilities = fallback.capabilities;
        v.timeline.reset();
        v.metadataRevision = fallback.metadataRevision;
        v.artwork = std::move(fallback.artwork);
        v.progress = fallback.progress;
        v.artworkURL = std::move(fallback.artworkURL);
        v.playerControls = fallback.playerControls;
    } else if (v.session && std::find(v.sessions.begin(), v.sessions.end(), *v.session) == v.sessions.end()) {
        v.session.reset();
        v.track.reset();
        v.artwork.reset();
        v.progress.reset();
        v.artworkURL.reset();
        v.playerControls = false;
    }
    return out;
}

NativeNowPlayingService::Factory nowPlayingWindowProvider(std::function<std::shared_ptr<NowPlayingWindowSystem>()> system, std::function<double()> now,
    std::shared_ptr<const NowPlayingProcessActivity> activity) {
    if (!system || !now) throw std::invalid_argument("The window-title fallback needs a window system and the host clock");
    return [system = std::move(system), now = std::move(now), activity = std::move(activity)]() -> std::shared_ptr<NowPlayingProvider> {
        return std::make_shared<WindowProvider>(system(), now, activity);
    };
}

bool NowPlayingProcessActivity::update(std::span<const modules::NowPlayingAudioRoute> routes) {
    std::vector<std::pair<std::uint32_t, bool>> next;
    next.reserve(std::min(routes.size(), maximumProcesses));
    for (const auto& route : routes) {
        if (!route.pid || !route.rendering || next.size() >= maximumProcesses) continue;
        const auto found = std::find_if(next.begin(), next.end(), [&](const auto& entry) { return entry.first == route.pid; });
        if (found == next.end()) next.emplace_back(route.pid, *route.rendering);
        else found->second = found->second || *route.rendering;
    }
    std::sort(next.begin(), next.end());
    std::lock_guard lock(mutex_);
    if (next == processes_) return false;
    processes_ = std::move(next);
    ++revision_;
    return true;
}
std::optional<bool> NowPlayingProcessActivity::rendering(std::uint32_t pid) const {
    std::lock_guard lock(mutex_);
    const auto found = std::lower_bound(processes_.begin(), processes_.end(), std::pair<std::uint32_t, bool>{pid, false});
    if (found == processes_.end() || found->first != pid) return std::nullopt;
    return found->second;
}
std::uint64_t NowPlayingProcessActivity::revision() const {
    std::lock_guard lock(mutex_);
    return revision_;
}
NativeNowPlayingService::Factory nowPlayingCompositeProvider(NativeNowPlayingService::Factory media, NativeNowPlayingService::Factory windows) {
    if (!media || !windows) throw std::invalid_argument("The composite Now Playing provider needs both sources");
    return [media = std::move(media), windows = std::move(windows)]() -> std::shared_ptr<NowPlayingProvider> {
        auto first = media();
        std::shared_ptr<NowPlayingProvider> second;
        try {
            second = windows();
        } catch (...) {
            // The window-title fallback is additive: GSMTC keeps working.
            second = nullptr;
        }
        return std::make_shared<CompositeProvider>(std::move(first), std::move(second));
    };
}

// ---------------------------------------------------------------------------
// Synthetic desktop
// ---------------------------------------------------------------------------
std::function<void()> NowPlayingWindowFixture::watch(std::function<void()> changed) {
    std::lock_guard lock(mutex_);
    if (stopped_) return [] {};
    const auto id = ++next_;
    watchers_.emplace_back(id, std::move(changed));
    return [this, id] {
        std::lock_guard lock(mutex_);
        std::erase_if(watchers_, [id](const auto& w) { return w.first == id; });
    };
}
std::vector<m::NowPlayingPlayerWindow> NowPlayingWindowFixture::windows() {
    std::lock_guard lock(mutex_);
    ++reads_;
    auto out = windows_;
    if (out.size() > nowPlayingMaximumPlayerWindows) out.resize(nowPlayingMaximumPlayerWindows);
    return out;
}
NowPlayingServiceFailure NowPlayingWindowFixture::command(std::uintptr_t window, std::uint32_t pid, m::NowPlayingCommandKind kind, bool onlyPlayer) {
    std::lock_guard lock(mutex_);
    if (kind == m::NowPlayingCommandKind::seek) return Failure::unsupported;
    if (std::none_of(windows_.begin(), windows_.end(), [&](const auto& w) { return w.window == window && w.pid == pid; })) return Failure::stale;
    if (refuse_) return Failure::unavailable;
    if (commands_.size() >= 64) commands_.erase(commands_.begin());
    commands_.push_back({window, pid, kind, onlyPlayer});
    return Failure::none;
}
std::optional<NowPlayingPlayerReading> NowPlayingWindowFixture::reading(std::uintptr_t window, std::uint32_t pid, bool refresh) {
    std::lock_guard lock(mutex_);
    if (std::none_of(windows_.begin(), windows_.end(), [&](const auto& w) { return w.window == window && w.pid == pid; })) return std::nullopt;
    const auto found = std::find_if(readers_.begin(), readers_.end(), [&](const Reader& r) { return r.pid == pid; });
    if (found == readers_.end()) return std::nullopt;
    if (refresh) {
        ++refreshes_;
        found->delivered = found->value;
        if (found->delivered && found->clock) found->delivered->startedAt = found->delivered->sampledAt = found->clock();
    }
    return found->delivered;
}
bool NowPlayingWindowFixture::press(std::uintptr_t window, std::uint32_t pid, m::NowPlayingCommandKind kind, std::function<void(bool)> done) {
    bool pressed{};
    {
        std::lock_guard lock(mutex_);
        if (!controls_ || kind == m::NowPlayingCommandKind::seek) return false;
        pressed = std::any_of(windows_.begin(), windows_.end(), [&](const auto& w) { return w.window == window && w.pid == pid; });
        if (pressed) {
            if (presses_.size() >= 64) presses_.erase(presses_.begin());
            presses_.push_back({pid, kind});
        }
    }
    if (done) done(pressed);
    return true;
}
void NowPlayingWindowFixture::setReading(std::uint32_t pid, std::optional<NowPlayingPlayerReading> value, std::function<double()> clock) {
    std::lock_guard lock(mutex_);
    auto found = std::find_if(readers_.begin(), readers_.end(), [&](const Reader& r) { return r.pid == pid; });
    if (found == readers_.end()) {
        readers_.push_back({pid});
        found = readers_.end() - 1;
    }
    found->value = std::move(value);
    found->clock = std::move(clock);
}
void NowPlayingWindowFixture::pressControls(bool value) {
    std::lock_guard lock(mutex_);
    controls_ = value;
}
std::vector<NowPlayingWindowFixture::Press> NowPlayingWindowFixture::presses() const {
    std::lock_guard lock(mutex_);
    return presses_;
}
std::size_t NowPlayingWindowFixture::refreshes() const {
    std::lock_guard lock(mutex_);
    return refreshes_;
}
void NowPlayingWindowFixture::shutdown() noexcept {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    watchers_.clear();
}
bool NowPlayingWindowFixture::stopped() const {
    std::lock_guard lock(mutex_);
    return stopped_;
}
void NowPlayingWindowFixture::set(std::vector<m::NowPlayingPlayerWindow> next) {
    std::lock_guard lock(mutex_);
    windows_ = std::move(next);
}
void NowPlayingWindowFixture::notify() {
    std::vector<std::function<void()>> callbacks;
    {
        std::lock_guard lock(mutex_);
        for (const auto& w : watchers_) callbacks.push_back(w.second);
    }
    for (auto& callback : callbacks)
        if (callback) callback();
}
void NowPlayingWindowFixture::refuseCommands(bool value) {
    std::lock_guard lock(mutex_);
    refuse_ = value;
}
std::vector<NowPlayingWindowFixture::Command> NowPlayingWindowFixture::commands() const {
    std::lock_guard lock(mutex_);
    return commands_;
}
std::size_t NowPlayingWindowFixture::watchers() const {
    std::lock_guard lock(mutex_);
    return watchers_.size();
}
std::size_t NowPlayingWindowFixture::reads() const {
    std::lock_guard lock(mutex_);
    return reads_;
}
} // namespace endfield::native

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "native/now_playing_player_controls.hpp"
#include <windows.h>

namespace endfield::native {
namespace {
std::string utf8(const wchar_t* text, int length) {
    if (length <= 0) return {};
    // Unpaired surrogates in a title become U+FFFD instead of failing.
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), bytes, nullptr, nullptr);
    return out;
}
// Another process's window: the stored caption, never WM_GETTEXT.
std::string windowTitle(HWND window) {
    wchar_t title[1024]{};
    return utf8(title, GetWindowTextW(window, title, static_cast<int>(std::size(title))));
}
constexpr std::size_t maximumClassified = 256;
constexpr std::size_t maximumEnumerated = 4096;
constexpr std::size_t maximumTitles = nowPlayingMaximumPlayerWindows;
// A hidden-time title hook that delivers more than this many events within
// one second (e.g. a Chromium player whose accessibility tree is on) is
// removed: its wake-ups would outweigh the start times it records.
constexpr unsigned maximumBackgroundEventsPerSecond = 20;

// Shared between the provider's read (worker) and the hook callbacks (owner).
struct Desktop {
    explicit Desktop(std::function<double()> clock) : now(std::move(clock)) {}
    std::mutex mutex;
    const std::function<double()> now;
    std::vector<HWND> followed;            // the windows the last read returned
    std::vector<DWORD> players;            // their processes
    struct Process { DWORD pid; bool player; std::string path; };
    std::vector<Process> classified;       // bounded, per presentation
    // Last observed title of each player window and when it changed to it.
    struct Title { HWND window{}; DWORD pid{}; std::string text; std::optional<double> changedAt; };
    std::vector<Title> titles;
    std::vector<DWORD> observed;           // processes with a live hidden-time title hook
    bool watching{};                       // the presented hooks are installed
    double time() const {
        const double value = now();
        return std::isfinite(value) ? value : 0;
    }
    std::optional<Process> classify(DWORD pid) {
        {
            std::lock_guard lock(mutex);
            for (const auto& p : classified)
                if (p.pid == pid) return p;
        }
        Process next{pid, false, {}};
        if (const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            wchar_t buffer[1024]{};
            DWORD length = static_cast<DWORD>(std::size(buffer));
            if (QueryFullProcessImageNameW(process, 0, buffer, &length) && length) {
                next.path = utf8(buffer, static_cast<int>(length));
                next.player = modules::nowPlayingWindowPlayer(next.path) != nullptr;
            }
            CloseHandle(process);
        }
        // An inaccessible process is remembered as "not a player" too.
        std::lock_guard lock(mutex);
        if (classified.size() >= maximumClassified) classified.erase(classified.begin());
        classified.push_back(next);
        return next;
    }
    bool followedWindow(HWND window) {
        std::lock_guard lock(mutex);
        return std::find(followed.begin(), followed.end(), window) != followed.end();
    }
    bool playerProcess(DWORD pid) {
        std::lock_guard lock(mutex);
        return std::find(players.begin(), players.end(), pid) != players.end();
    }
    // Owner thread, from a NAMECHANGE of a player's top-level window: its
    // title may have changed now.
    void named(HWND window, DWORD pid) {
        auto text = windowTitle(window);
        const double at = time();
        std::lock_guard lock(mutex);
        const auto found = std::find_if(titles.begin(), titles.end(), [&](const Title& t) { return t.window == window; });
        if (found == titles.end()) {
            if (titles.size() >= maximumTitles) titles.erase(titles.begin());
            titles.push_back({window, pid, std::move(text), at});
        } else if (found->pid != pid || found->text != text) {
            *found = {window, pid, std::move(text), at};
        }
    }
    // Any thread, from a read: the title a read saw, and when it changed to
    // it. A change no hook reported is dated now only while the window's
    // process was being observed (an event would have been at most moments
    // earlier); otherwise its time is unknown.
    std::optional<double> reconcile(HWND window, DWORD pid, const std::string& text, double at) {
        std::lock_guard lock(mutex);
        const auto found = std::find_if(titles.begin(), titles.end(), [&](const Title& t) { return t.window == window; });
        if (found == titles.end() || found->pid != pid) {
            if (found != titles.end()) titles.erase(found);
            if (titles.size() >= maximumTitles) titles.erase(titles.begin());
            titles.push_back({window, pid, text, std::nullopt});
            return std::nullopt;
        }
        if (found->text != text) {
            const bool live = watching || std::find(observed.begin(), observed.end(), pid) != observed.end();
            found->text = text;
            found->changedAt = live ? std::optional<double>(at) : std::nullopt;
        }
        return found->changedAt;
    }
};

struct Sink {
    virtual ~Sink() = default;
    virtual void event(HWINEVENTHOOK hook, DWORD event, HWND window, bool windowObject) = 0;
};
// Out-of-context WinEvents arrive on the installing thread's message loop.
thread_local std::vector<std::pair<HWINEVENTHOOK, std::weak_ptr<Sink>>> registry;
// Subscriptions ended off their thread, removed by that thread's next watch.
std::mutex retiredMutex;
std::vector<std::pair<DWORD, HWINEVENTHOOK>> retired;
void unhook(HWINEVENTHOOK hook) noexcept {
    if (!hook) return;
    UnhookWinEvent(hook);
    std::erase_if(registry, [hook](const auto& entry) { return entry.first == hook; });
}
void drainRetired() noexcept {
    std::vector<HWINEVENTHOOK> mine;
    {
        std::lock_guard lock(retiredMutex);
        const DWORD thread = GetCurrentThreadId();
        for (const auto& [owner, hook] : retired)
            if (owner == thread) mine.push_back(hook);
        std::erase_if(retired, [thread](const auto& entry) { return entry.first == thread; });
    }
    for (const auto hook : mine) unhook(hook);
}
void CALLBACK onEvent(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
    std::shared_ptr<Sink> sink;
    for (const auto& [installed, entry] : registry)
        if (installed == hook) {
            sink = entry.lock();
            break;
        }
    if (!sink) return;
    try {
        sink->event(hook, event, window, object == OBJID_WINDOW && child == CHILDID_SELF && window);
    } catch (...) {
    }
}

// The presented subscription: global lifetime and title hooks.
struct Watch final : Sink {
    std::shared_ptr<Desktop> desktop;
    std::shared_ptr<NowPlayingControlReader> reader;
    std::function<void()> changed;
    void event(HWINEVENTHOOK, DWORD event, HWND window, bool windowObject) override {
        if (!windowObject) {
            // The first event of a player's page after its tree was requested
            // (a label changing or an element shown): read its controls again.
            if ((event == EVENT_OBJECT_NAMECHANGE || event == EVENT_OBJECT_SHOW) && window && reader && reader->awaiting()) {
                DWORD pid{};
                if (GetWindowThreadProcessId(window, &pid) && pid && desktop->playerProcess(pid)) reader->treeChanged(pid);
            }
            return;
        }
        if (relevant(event, window) && changed) changed();
    }
    bool relevant(DWORD event, HWND window) {
        switch (event) {
        case EVENT_OBJECT_DESTROY:
        case EVENT_OBJECT_HIDE:
            return desktop->followedWindow(window);
        case EVENT_OBJECT_NAMECHANGE: {
            const bool followed = desktop->followedWindow(window);
            if (!followed && GetAncestor(window, GA_ROOT) != window) return false;
            DWORD pid{};
            if (!GetWindowThreadProcessId(window, &pid) || !pid) return followed;
            if (!followed && !desktop->playerProcess(pid)) return false;
            desktop->named(window, pid);
            return true;
        }
        case EVENT_OBJECT_SHOW: {
            if (GetAncestor(window, GA_ROOT) != window) return false;
            DWORD pid{};
            if (!GetWindowThreadProcessId(window, &pid) || !pid) return false;
            if (desktop->playerProcess(pid)) return true;
            const auto process = desktop->classify(pid);
            return process && process->player;
        }
        default:
            return false;
        }
    }
};
// One known player's process-scoped title hook while Now Playing is hidden.
struct Background final : Sink {
    std::shared_ptr<Desktop> desktop;
    DWORD pid{};
    ULONGLONG second{};
    unsigned count{};
    bool flooded{};
    void event(HWINEVENTHOOK hook, DWORD event, HWND window, bool windowObject) override {
        if (flooded) return;
        const auto tick = GetTickCount64();
        if (tick - second >= 1000) {
            second = tick;
            count = 0;
        }
        // A player whose page announces its own elements (an accessibility
        // tree that is on, e.g. after its controls were read) would wake the
        // hidden HUD with every label change: such a hook is removed at once.
        if (++count > maximumBackgroundEventsPerSecond || !windowObject) {
            flooded = true;
            unhook(hook);
            std::lock_guard lock(desktop->mutex);
            std::erase(desktop->observed, pid);
            return;
        }
        if (!windowObject || event != EVENT_OBJECT_NAMECHANGE || GetAncestor(window, GA_ROOT) != window) return;
        DWORD owner{};
        if (GetWindowThreadProcessId(window, &owner) && owner == pid) desktop->named(window, pid);
    }
};
// Owner-thread hook state of one window system.
struct Hooks {
    std::shared_ptr<Desktop> desktop;
    DWORD thread{};
    bool stopped{};
    struct Entry { DWORD pid; HWINEVENTHOOK hook; std::shared_ptr<Background> sink; };
    std::vector<Entry> background;
    std::vector<DWORD> flooded;               // listed players whose hook was flooded
    std::vector<HWINEVENTHOOK> presented;     // live presented hooks
    // Hidden-time title hooks follow the players the last read listed.
    void retain() noexcept {
        if (GetCurrentThreadId() != thread) return;
        std::vector<DWORD> players;
        {
            std::lock_guard lock(desktop->mutex);
            players = desktop->players;
        }
        const auto listed = [&](DWORD pid) { return std::find(players.begin(), players.end(), pid) != players.end(); };
        for (auto entry = background.begin(); entry != background.end();) {
            if (entry->sink->flooded) {
                if (std::find(flooded.begin(), flooded.end(), entry->pid) == flooded.end()) flooded.push_back(entry->pid);
                entry = background.erase(entry);
            } else if (stopped || !listed(entry->pid)) {
                unhook(entry->hook);
                entry = background.erase(entry);
            } else {
                ++entry;
            }
        }
        std::erase_if(flooded, [&](DWORD pid) { return !listed(pid); });
        if (!stopped)
            for (const DWORD pid : players) {
                if (background.size() >= nowPlayingMaximumPlayerProcesses) break;
                if (std::any_of(background.begin(), background.end(), [&](const Entry& e) { return e.pid == pid; }) ||
                    std::find(flooded.begin(), flooded.end(), pid) != flooded.end())
                    continue;
                auto sink = std::make_shared<Background>();
                sink->desktop = desktop;
                sink->pid = pid;
                const HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, onEvent, pid, 0, WINEVENT_OUTOFCONTEXT);
                if (!hook) continue;
                registry.emplace_back(hook, sink);
                background.push_back({pid, hook, std::move(sink)});
            }
        std::lock_guard lock(desktop->mutex);
        desktop->observed.clear();
        for (const auto& entry : background) desktop->observed.push_back(entry.pid);
    }
    void stop() noexcept {
        if (GetCurrentThreadId() != thread) {
            std::lock_guard lock(retiredMutex);
            for (const auto& entry : background) retired.emplace_back(thread, entry.hook);
            for (const auto hook : presented) retired.emplace_back(thread, hook);
        } else {
            for (const auto& entry : background) unhook(entry.hook);
            for (const auto hook : presented) unhook(hook);
            drainRetired();
        }
        stopped = true;
        background.clear();
        presented.clear();
        std::lock_guard lock(desktop->mutex);
        desktop->observed.clear();
        desktop->watching = false;
    }
};

bool mediaKeyHeld(UINT key) noexcept {
    // Thread hotkey ids are 0x0000-0xBFFF; this one is private to the call.
    constexpr int probe = 0x7E11;
    if (RegisterHotKey(nullptr, probe, MOD_NOREPEAT, key)) {
        UnregisterHotKey(nullptr, probe);
        MSG message;
        while (PeekMessageW(&message, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {}
        return false;
    }
    return GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED;
}
UINT mediaKey(modules::NowPlayingCommandKind kind) noexcept {
    switch (kind) {
    case modules::NowPlayingCommandKind::playPause: return VK_MEDIA_PLAY_PAUSE;
    case modules::NowPlayingCommandKind::previous: return VK_MEDIA_PREV_TRACK;
    case modules::NowPlayingCommandKind::next: return VK_MEDIA_NEXT_TRACK;
    case modules::NowPlayingCommandKind::seek: break;
    }
    return 0;
}
bool sendMediaKey(UINT key) noexcept {
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = static_cast<WORD>(key);
    inputs[0].ki.wScan = static_cast<WORD>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC));
    inputs[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

class Win32WindowSystem final : public NowPlayingWindowSystem {
public:
    explicit Win32WindowSystem(std::function<double()> now) : hooks_(std::make_shared<Hooks>()), reader_(std::make_shared<NowPlayingControlReader>(now)) {
        hooks_->desktop = std::make_shared<Desktop>(std::move(now));
        hooks_->thread = GetCurrentThreadId();
    }
    ~Win32WindowSystem() override { shutdown(); }
    std::function<void()> watch(std::function<void()> changed) override {
        drainRetired();
        if (hooks_->stopped || GetCurrentThreadId() != hooks_->thread) return [] {};
        const auto desktop = hooks_->desktop;
        {
            std::lock_guard lock(desktop->mutex);
            // A process id may be reused between presentations.
            desktop->classified.clear();
            desktop->watching = true;
        }
        auto entry = std::make_shared<Watch>();
        entry->desktop = desktop;
        entry->reader = reader_;
        // Completed readings of the players' controls wake the provider too.
        const auto owner = ++notifyOwner_;
        reader_->setNotify(changed);
        entry->changed = std::move(changed);
        constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        // Destroy, show and hide (0x8001-0x8003), and title changes (0x800C).
        const HWINEVENTHOOK lifetime = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, onEvent, 0, 0, flags);
        const HWINEVENTHOOK titles = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, onEvent, 0, 0, flags);
        for (const auto hook : {lifetime, titles})
            if (hook) {
                registry.emplace_back(hook, entry);
                hooks_->presented.push_back(hook);
            }
        hooks_->retain();
        const DWORD thread = GetCurrentThreadId();
        const std::weak_ptr<Hooks> weak = hooks_;
        return [this, owner, lifetime, titles, thread, entry, weak]() mutable noexcept {
            if (!entry) return;
            entry->changed = nullptr;
            const auto hooks = weak.lock();
            if (GetCurrentThreadId() == thread) {
                if (hooks && notifyOwner_ == owner) reader_->setNotify({});
                unhook(lifetime);
                unhook(titles);
                drainRetired();
                if (hooks) {
                    std::erase_if(hooks->presented, [&](HWINEVENTHOOK h) { return h == lifetime || h == titles; });
                    {
                        std::lock_guard lock(hooks->desktop->mutex);
                        hooks->desktop->watching = !hooks->presented.empty();
                    }
                    // Keep dating the players' title changes while hidden.
                    hooks->retain();
                }
            } else {
                std::lock_guard lock(retiredMutex);
                for (const auto hook : {lifetime, titles})
                    if (hook) retired.emplace_back(thread, hook);
            }
            entry.reset();
        };
    }
    std::vector<modules::NowPlayingPlayerWindow> windows() override {
        std::vector<HWND> all;
        all.reserve(512);
        EnumWindows([](HWND window, LPARAM list) -> BOOL {
            auto& out = *reinterpret_cast<std::vector<HWND>*>(list);
            if (out.size() >= maximumEnumerated) return FALSE;
            out.push_back(window);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&all));
        const auto& desktop = hooks_->desktop;
        std::vector<modules::NowPlayingPlayerWindow> out;
        std::vector<HWND> followed;
        std::vector<DWORD> players;
        const DWORD self = GetCurrentProcessId();
        const double at = desktop->time();
        for (const HWND window : all) {
            DWORD pid{};
            if (!GetWindowThreadProcessId(window, &pid) || !pid || pid == self) continue;
            const auto process = desktop->classify(pid);
            if (!process || !process->player) continue;
            modules::NowPlayingPlayerWindow w;
            w.window = reinterpret_cast<std::uintptr_t>(window);
            w.pid = pid;
            w.executable = process->path;
            wchar_t name[256]{};
            w.className = utf8(name, GetClassNameW(window, name, static_cast<int>(std::size(name))));
            w.title = windowTitle(window);
            w.visible = IsWindowVisible(window) != FALSE;
            w.owned = GetWindow(window, GW_OWNER) != nullptr;
            w.titleChangedAt = desktop->reconcile(window, pid, w.title, at);
            out.push_back(std::move(w));
            followed.push_back(window);
            if (std::find(players.begin(), players.end(), pid) == players.end()) players.push_back(pid);
            if (out.size() >= nowPlayingMaximumPlayerWindows) break;
        }
        // The control readings of players that left are released.
        {
            std::vector<std::uint32_t> pids(players.begin(), players.end());
            reader_->retain(pids);
        }
        std::lock_guard lock(desktop->mutex);
        // Titles of windows that are gone are forgotten.
        std::erase_if(desktop->titles, [&](const Desktop::Title& t) { return std::find(followed.begin(), followed.end(), t.window) == followed.end(); });
        desktop->followed = std::move(followed);
        desktop->players = std::move(players);
        return out;
    }
    std::optional<NowPlayingPlayerReading> reading(std::uintptr_t window, std::uint32_t pid, bool refresh) override {
        return reader_->reading(window, pid, refresh);
    }
    bool press(std::uintptr_t window, std::uint32_t pid, modules::NowPlayingCommandKind kind, std::function<void(bool)> done) override {
        return reader_->press(window, pid, kind, std::move(done));
    }
    NowPlayingServiceFailure command(std::uintptr_t value, std::uint32_t pid, modules::NowPlayingCommandKind kind, bool onlyPlayer) override {
        const UINT key = mediaKey(kind);
        if (!key) return NowPlayingServiceFailure::unsupported;
        const HWND window = reinterpret_cast<HWND>(value);
        DWORD owner{};
        if (!IsWindow(window) || !GetWindowThreadProcessId(window, &owner) || owner != pid) return NowPlayingServiceFailure::stale;
        switch (modules::nowPlayingWindowTransport(onlyPlayer && mediaKeyHeld(key), onlyPlayer)) {
        case modules::NowPlayingWindowTransport::mediaKey:
            // The holder of the registered hotkey receives WM_HOTKEY; the key
            // never reaches the foreground window.
            return sendMediaKey(key) ? NowPlayingServiceFailure::none : NowPlayingServiceFailure::unavailable;
        case modules::NowPlayingWindowTransport::appCommand:
            break;
        }
        short code{};
        switch (kind) {
        case modules::NowPlayingCommandKind::playPause: code = APPCOMMAND_MEDIA_PLAY_PAUSE; break;
        case modules::NowPlayingCommandKind::previous: code = APPCOMMAND_MEDIA_PREVIOUSTRACK; break;
        case modules::NowPlayingCommandKind::next: code = APPCOMMAND_MEDIA_NEXTTRACK; break;
        case modules::NowPlayingCommandKind::seek: return NowPlayingServiceFailure::unsupported;
        }
        // As if the media key were pressed over the player's window.
        const LPARAM parameter = MAKELPARAM(0, static_cast<WORD>(code) | FAPPCOMMAND_KEY);
        return PostMessageW(window, WM_APPCOMMAND, reinterpret_cast<WPARAM>(window), parameter) ? NowPlayingServiceFailure::none
                                                                                                   : NowPlayingServiceFailure::unavailable;
    }
    void shutdown() noexcept override {
        if (!hooks_->stopped) hooks_->stop();
        reader_->shutdown();
    }
private:
    std::shared_ptr<Hooks> hooks_;
    std::shared_ptr<NowPlayingControlReader> reader_;
    std::uint64_t notifyOwner_{}; // owner thread: the watch whose callback the reader uses
};
} // namespace

std::shared_ptr<NowPlayingWindowSystem> windowsNowPlayingWindowSystem(std::function<double()> now) {
    if (!now) throw std::invalid_argument("The window-title fallback needs the host monotonic clock");
    return std::make_shared<Win32WindowSystem>(std::move(now));
}
bool windowsNowPlayingMediaKeyHeld(modules::NowPlayingCommandKind kind) {
    const UINT key = mediaKey(kind);
    return key && mediaKeyHeld(key);
}
NativeNowPlayingService::Factory windowsNowPlayingPlayers(std::function<double()> now, std::shared_ptr<NowPlayingWindowSystem> system,
    std::shared_ptr<const NowPlayingProcessActivity> activity) {
    if (!now) throw std::invalid_argument("Now Playing needs the host monotonic clock");
    if (!system) system = windowsNowPlayingWindowSystem(now);
    return nowPlayingCompositeProvider(windowsNowPlayingProvider(now),
        nowPlayingWindowProvider([system] { return system; }, now, std::move(activity)));
}
} // namespace endfield::native
#endif
