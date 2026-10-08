#pragma once
#include "app/utility_executor.hpp"
#include "native/event_log_owner.hpp"

namespace endfield::app {
// App-lifetime binding, not a second Event Log model or worker. Owner/executor
// outlive this binding. Root combines owner.saveDeadline() with media deadlines
// on its one waitable timer, then calls capture(). After any utility drain,
// retry() admits a retained save if a different module had filled the queue.
class EventLogSaveQueue final {
public:
    EventLogSaveQueue(native::EventLogOwner&,UtilityExecutor&);
    ~EventLogSaveQueue();
    EventLogSaveQueue(const EventLogSaveQueue&)=delete;
    EventLogSaveQueue&operator=(const EventLogSaveQueue&)=delete;
    void capture(double monotonicTime,bool force=false);
    void retry();
    bool pending()const;
    // App shutdown only: accept the latest snapshot, wait for bounded file
    // work and deliver its result while owner still exists. No retry polling.
    void flush(double monotonicTime);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
