#pragma once
#include "core/data/event_log_store.hpp"
#include <functional>
#include <memory>

namespace endfield::native {
struct EventLogOwnerCallbacks {
    // Same monotonic epoch as the root OverlayHost deadline. Invoked only for
    // record/clear, never snapshot, paint, hover or selection.
    std::function<double()> now;
    // UI-owner notification: schedule a coalesced active-preview refresh,
    // recompute the root's minimum external deadline, invalidate if visible.
    // Avoid reentering a clear-action dispatch. No file I/O here.
    std::function<void()> changed;
};
// One source Event Log store + Unicode adapter, on the caller's UI thread.
// No observer of system/user activity, timer, worker, window or publisher.
// The root owns execution: takeSave -> bounded utility executor -> complete.
// Retain an unaccepted request (or replace it with a newer revision); queue
// saturation must not discard the last snapshot. At shutdown detach completion
// routing and flush/drain on that same shared executor before app termination.
class EventLogOwner final {
public:
    EventLogOwner(std::optional<std::filesystem::path> appRoot,EventLogOwnerCallbacks,
        std::size_t capacity=500);
    ~EventLogOwner();
    EventLogOwner(const EventLogOwner&)=delete;
    EventLogOwner&operator=(const EventLogOwner&)=delete;
    void record(modules::SystemEvent); // UUID and Foundation-epoch timestamp are caller-supplied
    void clear();
    modules::EventLogSnapshot snapshot()const;
    // Closures borrow a lifetime route. After owner destruction they are inert
    // and expose no stale events; accepted save requests remain self-contained.
    modules::EventLogCallbacks callbacks(std::function<std::string(double)> timestamp)const;
    modules::EventNameCompactor nameCompactor()const;
    std::uint64_t revision()const;
    std::optional<double>saveDeadline()const;
    std::optional<ehud::data::EventLogWrite>takeSave(double monotonicTime,bool force=false);
    bool complete(ehud::data::EventLogSaveResult); // UI thread; not a worker callback
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
