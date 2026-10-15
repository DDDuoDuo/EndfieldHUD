#include "core/migration/mac_import_service.hpp"
#include <atomic>

namespace ehud::migration {
namespace {
// Shared between one worker job at a time and the owner thread. The executor's
// queue hand-off orders every write before the completion that reads it.
struct Job {
    std::shared_ptr<MacImportSession> session;
    // Shared with the session's cancellation probe, which outlives the job
    // when a reviewed session is handed off (handOff()).
    std::shared_ptr<std::atomic<bool>> cancelled{std::make_shared<std::atomic<bool>>(false)};
    bool more{true}, committable{};
    MacImportProgress progress;
    std::optional<MacImportSummary> summary;
    std::optional<MacImportCommitResult> result;
    std::string error;
    std::optional<MacImportErrorCode> code;
};
enum class Pending { none, stage, commit, discard };
}
struct MacImportService::Impl {
    endfield::app::UtilityExecutor& executor;
    endfield::app::UtilityExecutor::Route route;
    std::function<void()> changed;
    State state{State::idle};
    std::shared_ptr<Job> job;
    Pending pending{Pending::none};
    bool running{};
    std::size_t steps{};
    MacImportProgress progress;
    std::optional<MacImportSummary> summary;
    std::optional<MacImportCommitResult> result;
    std::string error;
    std::optional<MacImportErrorCode> code;
    bool committable{};
    Impl(endfield::app::UtilityExecutor& e, std::function<void()> c) : executor(e), route(e.makeRoute()), changed(std::move(c)) {}
    void notify() { if (changed) changed(); }
    // Back to idle without anything of the last import (strings and lists
    // are replaced, not cleared, so their storage is returned too).
    void clear() {
        state = State::idle;
        steps = 0;
        progress = {};
        summary.reset();
        result.reset();
        error = std::string();
        code.reset();
        committable = false;
    }
    void finish(State next, std::string message = {}) {
        state = next;
        error = std::move(message);
        pending = Pending::none;
        notify();
    }
    void submit(Pending what) {
        pending = what;
        if (running) return;
        auto current = job;
        const std::weak_ptr<Job> weakJob = current;
        const auto kind = what;
        // A step that ends the import (a finished commit, a discard or any
        // failure) destroys the session here on the worker: its manifest,
        // staged bookkeeping and private-folder lock never outlive the import
        // on the owner thread. Only its results travel in the job.
        auto work = [current = std::move(current), kind] {
            try {
                if (kind == Pending::stage) {
                    current->more = current->session->stageNext();
                    if (!current->more) { current->summary = current->session->summary(); current->committable = current->session->committable(); }
                    current->progress = current->session->progress();
                } else if (kind == Pending::commit) {
                    current->more = current->session->commitNext();
                    current->progress = current->session->progress();
                    if (!current->more) { current->summary = current->session->summary(); current->result = current->session->commitResult(); current->session.reset(); }
                } else {
                    current->session->discard();
                    current->progress = current->session->progress();
                    current->session.reset();
                }
            } catch (const MacImportError& e) {
                current->progress = current->session->progress();
                current->error = e.what();
                current->code = e.code();
                current->session.reset();
                throw;
            } catch (const std::exception& e) {
                current->progress = current->session->progress();
                current->error = e.what();
                current->session.reset();
                throw;
            }
        };
        // Only the work functor owns the job: the executor destroys it on the
        // worker before reporting idle, so an abandoned session (owner gone)
        // removes its private folder deterministically, before waitIdle returns.
        auto completion = [this, weak = weakJob, kind](std::exception_ptr failure) {
            running = false;
            const auto current = weak.lock();
            if (!current || current != job) return; // superseded by a newer import
            progress = current->progress;
            if (failure) {
                const bool cancelledByUser = current->cancelled->load();
                code = cancelledByUser ? std::optional<MacImportErrorCode>(MacImportErrorCode::cancelled) : current->code;
                job.reset(); // the worker already destroyed its session
                finish(cancelledByUser ? State::cancelled : State::failed, std::move(current->error));
                return;
            }
            if (kind == Pending::discard) { job.reset(); finish(State::cancelled); return; }
            ++steps;
            if (current->more) {
                // Staging stops at once; a commit learns about cancellation in
                // its next step (honoured until the new root is active).
                if (kind == Pending::stage && current->cancelled->load()) { submit(Pending::discard); return; }
                submit(kind);
                notify();
                return;
            }
            summary = std::move(current->summary);
            current->summary.reset();
            if (kind == Pending::commit) { result = std::move(current->result); job.reset(); finish(State::completed); return; }
            committable = current->committable;
            finish(State::ready);
        };
        if (executor.submit(route, std::move(work), std::move(completion))) { running = true; pending = Pending::none; }
    }
};
MacImportService::MacImportService(endfield::app::UtilityExecutor& executor, std::function<void()> changed)
    : impl_(std::make_shared<Impl>(executor, std::move(changed))) {}
MacImportService::~MacImportService() {
    auto& i = *impl_;
    if (i.job) i.job->cancelled->store(true);
    // A running step finishes; queued steps (including a not-yet-started
    // commit) are dropped, so nothing commits after its owner is gone. No
    // completion reaches this destroyed owner.
    const bool inFlight = i.running;
    i.executor.invalidate(i.route, true);
    // An idle reviewed session still owns its private folder: remove it on the
    // worker through a detached one-shot route rather than on this thread.
    if (i.job && i.job->session && !inFlight) {
        try {
            const auto route = i.executor.makeRoute();
            auto job = std::move(i.job);
            const bool queued = i.executor.submit(route, [job] { job->session->discard(); }, [](std::exception_ptr) {});
            i.executor.invalidate(route, false);
            if (!queued) i.job = std::move(job); // queue full: the session destructor cleans up here
        } catch (...) {}
    }
}
bool MacImportService::begin(std::filesystem::path exportRoot, std::filesystem::path destination, MacImportPlatform platform, MacImportOptions options) {
    auto& i = *impl_;
    // A reviewed (ready) import must be committed or cancelled first, so its
    // private folder is always removed on the worker, never on this thread.
    if (i.state == State::staging || i.state == State::ready || i.state == State::committing || i.running) return false;
    auto job = std::make_shared<Job>();
    auto flag = job->cancelled;
    auto user = platform.cancelled;
    platform.cancelled = [flag, user] { return flag->load() || (user && user()); };
    job->session = std::make_shared<MacImportSession>(std::move(exportRoot), std::move(destination), std::move(platform), std::move(options));
    i.job = std::move(job);
    i.clear();
    i.state = State::staging;
    i.submit(Pending::stage);
    i.notify();
    return true;
}
bool MacImportService::commit() {
    auto& i = *impl_;
    if (i.state != State::ready || !i.committable || i.running) return false;
    i.state = State::committing;
    i.submit(Pending::commit);
    i.notify();
    return true;
}
void MacImportService::cancel() {
    auto& i = *impl_;
    if (!i.job || (i.state != State::staging && i.state != State::ready && i.state != State::committing)) return;
    i.job->cancelled->store(true);
    if (i.state == State::ready && !i.running) i.submit(Pending::discard);
}
bool MacImportService::acceptRejected(std::span<const MacImportStore> stores) {
    auto& i = *impl_;
    if (i.state != State::ready || i.running || i.pending != Pending::none || !i.job || !i.job->session) return false;
    // No step is queued or running, so the session is owned by this thread
    // until the next submit (the executor hand-off orders the accesses).
    i.job->session->acceptRejected(stores);
    i.committable = i.job->session->committable();
    i.notify();
    return true;
}
std::shared_ptr<MacImportSession> MacImportService::handOff() {
    auto& i = *impl_;
    if (i.state != State::ready || !i.committable || i.running || i.pending != Pending::none || !i.job || !i.job->session) return {};
    auto session = std::move(i.job->session);
    i.job.reset();
    // The holder reads the review from the session itself; nothing of this
    // import stays with the service.
    i.clear();
    i.notify();
    return session;
}
bool MacImportService::release() {
    auto& i = *impl_;
    if (i.running || i.pending != Pending::none || i.state == State::staging || i.state == State::ready || i.state == State::committing) return false;
    i.job.reset();
    i.clear();
    return true;
}
bool MacImportService::holdsSession() const noexcept {
    // While a step runs the worker owns the session (and may destroy it):
    // count it as held without reading the job's pointer.
    const auto& i = *impl_;
    return i.job && (i.running || i.job->session);
}
bool MacImportService::holdsResults() const noexcept {
    const auto& i = *impl_;
    return i.summary || i.result || !i.error.empty() || i.error.capacity() > std::string().capacity();
}
void MacImportService::queueCapacityAvailable() {
    auto& i = *impl_;
    if (!i.running && i.pending != Pending::none) i.submit(i.pending);
}
MacImportService::State MacImportService::state() const noexcept { return impl_->state; }
bool MacImportService::committable() const noexcept { return impl_->state == State::ready && impl_->committable; }
std::size_t MacImportService::completedSteps() const noexcept { return impl_->steps; }
MacImportProgress MacImportService::progress() const noexcept { return impl_->progress; }
const MacImportSummary* MacImportService::summary() const noexcept { return impl_->summary ? &*impl_->summary : nullptr; }
const std::optional<MacImportCommitResult>& MacImportService::result() const noexcept { return impl_->result; }
const std::string& MacImportService::error() const noexcept { return impl_->error; }
std::optional<MacImportErrorCode> MacImportService::errorCode() const noexcept { return impl_->code; }
bool MacImportService::busy() const noexcept { return impl_->running || impl_->pending != Pending::none; }
std::string_view macImportServiceStateName(MacImportService::State state) noexcept {
    switch (state) {
    case MacImportService::State::idle: return "idle";
    case MacImportService::State::staging: return "staging";
    case MacImportService::State::ready: return "ready";
    case MacImportService::State::committing: return "committing";
    case MacImportService::State::completed: return "completed";
    case MacImportService::State::failed: return "failed";
    case MacImportService::State::cancelled: return "cancelled";
    }
    return "unknown";
}
}
