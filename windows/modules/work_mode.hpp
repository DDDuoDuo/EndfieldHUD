#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::modules {
enum class WorkModeKind {countdown,stopwatch};
enum class WorkModePhase {idle,running,paused,stopped,completed};
std::string_view workModeKindKey(WorkModeKind);std::string_view workModePhaseKey(WorkModePhase);
struct WorkModeSnapshot {
    WorkModeKind kind{WorkModeKind::countdown};WorkModePhase phase{WorkModePhase::idle};
    double duration{1800},elapsed{};
    double remaining()const;double progress()const;bool active()const noexcept;
    std::int64_t displayedSeconds()const;std::string timeText()const;
    bool operator==(const WorkModeSnapshot&)const=default;
};
std::optional<double>parseWorkModeDuration(std::string_view);
std::string workModeDurationEditText(double);
struct WorkModeHooks {
    // Both callbacks are synchronous on the caller's UI owner. They may read
    // the completed state, but must not destroy/reenter this controller.
    std::function<void()>changed;
    std::function<void(double)>trackedWorkSecondsChanged; // absolute total, never a delta
};
// Source WorkModeController semantics with explicit continuous-clock input.
// The caller's clock MUST include sleep. A suspended controller keeps elapsed
// time but schedules no work; waking reconciles a completed countdown.
// Root aggregates nextDeadline() into its existing timer, then calls wake().
// No timer/thread, file, system Focus control, OS clock or service is created.
class WorkModeController final {
public:
    explicit WorkModeController(WorkModeHooks={});
    WorkModeSnapshot snapshot(double now)const;
    double trackedWorkSeconds(double now)const;
    bool restoreTrackedWorkSeconds(double);
    bool chooseCountdown(double seconds,double now);void chooseStopwatch(double now);
    void start(double now);void pause(double now);void resume(double now);void stop(double now);void reset(double now);
    void setVisible(bool,double now);void setSuspended(bool,double now);
    void refresh(double now);void wake(double now);void shutdown(double now);
    bool visible()const noexcept{return visible_;}bool suspended()const noexcept{return suspended_;}
    std::uint64_t revision()const noexcept{return revision_;}
    std::uint64_t notificationRevision()const noexcept{return notificationRevision_;}
    std::optional<double>nextDeadline()const noexcept;
    std::optional<double>displayDeadline()const noexcept{return display_;}
    std::optional<double>completionDeadline()const noexcept{return completion_;}
private:
    WorkModeHooks hooks_;WorkModeKind kind_{WorkModeKind::countdown};WorkModePhase phase_{WorkModePhase::idle};
    double duration_{1800},accumulated_{},trackedTotal_{},trackedSessionElapsed_{};
    std::optional<double>started_,display_,completion_;std::optional<std::int64_t>publishedSecond_;
    bool visible_{},suspended_{},restored_{};std::uint64_t revision_{},notificationRevision_{};
    void changed(double);void publish(double,bool);void reconcile(double);void checkpoint(double);
};
}
