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
    bool playing{}, idle{};
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

class WindowProvider final : public NowPlayingProvider {
public:
    WindowProvider(std::shared_ptr<NowPlayingWindowSystem> system, std::function<double()> now)
        : system_(std::move(system)), now_(std::move(now)), shared_(std::make_shared<WindowShared>()) {
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
                p.window = w.window;
                p.seen = s.generation;
                if (auto track = m::nowPlayingWindowTitleTrack(*f.rules, w.title)) {
                    if (!p.last || *p.last != *track) {
                        p.last = std::move(track);
                        p.playing = true;
                        ++p.revision;
                    } else if (p.idle) p.playing = true;
                    p.idle = false;
                } else if (p.last && !p.idle) {
                    p.idle = true;
                    p.playing = false;
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
                    track.supportsSeeking = false;
                    result.value.track = m::NowPlayingTrack::bounded(std::move(track));
                    result.value.capabilities.toggle = result.value.capabilities.previous = result.value.capabilities.next = true;
                }
            }
        }
        completion(std::move(result));
    }
    void perform(std::uint64_t token, std::uint64_t, m::NowPlayingCommand command, Ready ready) override {
        std::uintptr_t window{};
        std::uint32_t pid{};
        {
            std::lock_guard lock(shared_->mutex);
            if (shared_->stopped) return;
            const auto found = std::find_if(shared_->players.begin(), shared_->players.end(), [&](const Player& p) { return p.token == token; });
            if (found == shared_->players.end() || !found->last) {
                window = 0;
            } else {
                window = found->window;
                pid = found->pid;
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
        Failure failure = Failure::unavailable;
        try {
            failure = system_->command(window, pid, command.kind);
        } catch (...) {
            failure = Failure::unavailable;
        }
        if (failure == Failure::none && command.kind == m::NowPlayingCommandKind::playPause) {
            std::lock_guard lock(shared_->mutex);
            if (shared_->stopped) return;
            // The title does not report pausing: a delivered toggle flips the
            // inferred state until the title says otherwise.
            for (auto& p : shared_->players)
                if (p.token == token) p.playing = !p.playing;
        }
        {
            std::lock_guard lock(shared_->mutex);
            if (shared_->stopped) return;
        }
        ready({failure, 0});
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
        // A player GSMTC lists keeps its media session (same source mapping).
        std::vector<m::NowPlayingSource> listed;
        if (usable(media))
            for (const auto& s : media->value.sessions)
                if (const auto source = m::nowPlayingSourceForAppUserModelID(s.appUserModelID); source != m::NowPlayingSource::system) listed.push_back(source);
        const auto duplicate = [&](const m::NowPlayingSession& s) {
            return std::find(listed.begin(), listed.end(), m::nowPlayingSourceForAppUserModelID(s.appUserModelID)) != listed.end();
        };
        std::erase_if(fallback.sessions, duplicate);
        if (fallback.sessions.size() > nowPlayingMaximumPlayerProcesses) fallback.sessions.resize(nowPlayingMaximumPlayerProcesses);
        if (fallback.session && std::find(fallback.sessions.begin(), fallback.sessions.end(), *fallback.session) == fallback.sessions.end()) {
            fallback.session.reset();
            fallback.track.reset();
            fallback.artwork.reset();
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
    } else if (v.session && std::find(v.sessions.begin(), v.sessions.end(), *v.session) == v.sessions.end()) {
        v.session.reset();
        v.track.reset();
        v.artwork.reset();
    }
    return out;
}

NativeNowPlayingService::Factory nowPlayingWindowProvider(std::function<std::shared_ptr<NowPlayingWindowSystem>()> system, std::function<double()> now) {
    if (!system || !now) throw std::invalid_argument("The window-title fallback needs a window system and the host clock");
    return [system = std::move(system), now = std::move(now)]() -> std::shared_ptr<NowPlayingProvider> {
        return std::make_shared<WindowProvider>(system(), now);
    };
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
NowPlayingServiceFailure NowPlayingWindowFixture::command(std::uintptr_t window, std::uint32_t pid, m::NowPlayingCommandKind kind) {
    std::lock_guard lock(mutex_);
    if (kind == m::NowPlayingCommandKind::seek) return Failure::unsupported;
    if (std::none_of(windows_.begin(), windows_.end(), [&](const auto& w) { return w.window == window && w.pid == pid; })) return Failure::stale;
    if (refuse_) return Failure::unavailable;
    if (commands_.size() >= 64) commands_.erase(commands_.begin());
    commands_.push_back({window, pid, kind});
    return Failure::none;
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
constexpr std::size_t maximumClassified = 256;
constexpr std::size_t maximumEnumerated = 4096;
// Shared between the provider's read (worker) and the hook filter (owner).
struct Desktop {
    std::mutex mutex;
    std::vector<HWND> followed;            // the windows the last read returned
    std::vector<DWORD> players;            // their processes
    struct Process { DWORD pid; bool player; std::string path; };
    std::vector<Process> classified;       // bounded, per provider activation
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
};
struct Watch {
    std::shared_ptr<Desktop> desktop;
    std::function<void()> changed;
    bool relevant(DWORD event, HWND window) {
        switch (event) {
        case EVENT_OBJECT_DESTROY:
        case EVENT_OBJECT_HIDE:
            return desktop->followedWindow(window);
        case EVENT_OBJECT_NAMECHANGE: {
            if (desktop->followedWindow(window)) return true;
            if (GetAncestor(window, GA_ROOT) != window) return false;
            DWORD pid{};
            return GetWindowThreadProcessId(window, &pid) && desktop->playerProcess(pid);
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
// Out-of-context WinEvents arrive on the installing thread's message loop.
thread_local std::vector<std::pair<HWINEVENTHOOK, std::weak_ptr<Watch>>> registry;
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
    if (object != OBJID_WINDOW || child != CHILDID_SELF || !window) return;
    std::shared_ptr<Watch> watch;
    for (const auto& [installed, entry] : registry)
        if (installed == hook) {
            watch = entry.lock();
            break;
        }
    if (!watch) return;
    try {
        if (watch->relevant(event, window) && watch->changed) watch->changed();
    } catch (...) {
    }
}

class Win32WindowSystem final : public NowPlayingWindowSystem {
public:
    std::function<void()> watch(std::function<void()> changed) override {
        drainRetired();
        auto entry = std::make_shared<Watch>();
        entry->desktop = desktop_;
        entry->changed = std::move(changed);
        constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        // Destroy, show and hide (0x8001-0x8003), and title changes (0x800C).
        const HWINEVENTHOOK lifetime = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, onEvent, 0, 0, flags);
        const HWINEVENTHOOK titles = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, onEvent, 0, 0, flags);
        for (const auto hook : {lifetime, titles})
            if (hook) registry.emplace_back(hook, entry);
        const DWORD thread = GetCurrentThreadId();
        return [lifetime, titles, thread, entry]() mutable noexcept {
            if (!entry) return;
            entry->changed = nullptr;
            if (GetCurrentThreadId() == thread) {
                unhook(lifetime);
                unhook(titles);
                drainRetired();
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
        std::vector<modules::NowPlayingPlayerWindow> out;
        std::vector<HWND> followed;
        std::vector<DWORD> players;
        const DWORD self = GetCurrentProcessId();
        for (const HWND window : all) {
            DWORD pid{};
            if (!GetWindowThreadProcessId(window, &pid) || !pid || pid == self) continue;
            const auto process = desktop_->classify(pid);
            if (!process || !process->player) continue;
            modules::NowPlayingPlayerWindow w;
            w.window = reinterpret_cast<std::uintptr_t>(window);
            w.pid = pid;
            w.executable = process->path;
            wchar_t name[256]{};
            w.className = utf8(name, GetClassNameW(window, name, static_cast<int>(std::size(name))));
            // Another process's window: the stored caption, never WM_GETTEXT.
            wchar_t title[1024]{};
            w.title = utf8(title, GetWindowTextW(window, title, static_cast<int>(std::size(title))));
            w.visible = IsWindowVisible(window) != FALSE;
            w.owned = GetWindow(window, GW_OWNER) != nullptr;
            out.push_back(std::move(w));
            followed.push_back(window);
            if (std::find(players.begin(), players.end(), pid) == players.end()) players.push_back(pid);
            if (out.size() >= nowPlayingMaximumPlayerWindows) break;
        }
        std::lock_guard lock(desktop_->mutex);
        desktop_->followed = std::move(followed);
        desktop_->players = std::move(players);
        return out;
    }
    NowPlayingServiceFailure command(std::uintptr_t value, std::uint32_t pid, modules::NowPlayingCommandKind kind) override {
        short code{};
        switch (kind) {
        case modules::NowPlayingCommandKind::playPause: code = APPCOMMAND_MEDIA_PLAY_PAUSE; break;
        case modules::NowPlayingCommandKind::previous: code = APPCOMMAND_MEDIA_PREVIOUSTRACK; break;
        case modules::NowPlayingCommandKind::next: code = APPCOMMAND_MEDIA_NEXTTRACK; break;
        case modules::NowPlayingCommandKind::seek: return NowPlayingServiceFailure::unsupported;
        }
        const HWND window = reinterpret_cast<HWND>(value);
        DWORD owner{};
        if (!IsWindow(window) || !GetWindowThreadProcessId(window, &owner) || owner != pid) return NowPlayingServiceFailure::stale;
        // As if the media key were pressed over the player's window.
        const LPARAM parameter = MAKELPARAM(0, static_cast<WORD>(code) | FAPPCOMMAND_KEY);
        return PostMessageW(window, WM_APPCOMMAND, reinterpret_cast<WPARAM>(window), parameter) ? NowPlayingServiceFailure::none
                                                                                                   : NowPlayingServiceFailure::unavailable;
    }
private:
    std::shared_ptr<Desktop> desktop_ = std::make_shared<Desktop>();
};
} // namespace

std::shared_ptr<NowPlayingWindowSystem> windowsNowPlayingWindowSystem() { return std::make_shared<Win32WindowSystem>(); }
NativeNowPlayingService::Factory windowsNowPlayingPlayers(std::function<double()> now) {
    if (!now) throw std::invalid_argument("Now Playing needs the host monotonic clock");
    return nowPlayingCompositeProvider(windowsNowPlayingProvider(now), nowPlayingWindowProvider([] { return windowsNowPlayingWindowSystem(); }, now));
}
} // namespace endfield::native
#endif
