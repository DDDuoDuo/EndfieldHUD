#pragma once
#include "app/utility_executor.hpp"
#include "modules/activity_state.hpp"
#include <memory>

namespace endfield::native {
// Explicit worker-only entry points. These do no file-content/directory reads,
// process inventory, clipboard access, service creation or sampling scheduling.
// CPU: total machine only on one processor group; >64 CPUs reports unavailable.
// RAM: Windows physical total-minus-available (standby excluded), not private
// commit. Compression is unavailable. Network: up physical Ethernet/Wi-Fi/PPP.
// Disk and per-app network/disk await an exact provider; no fake zero values.
modules::ActivityRawSample readWindowsActivity(double timestamp,double uptime);
// Caller supplies an explicit <=256 app catalog with <=8192 unique PIDs.
// Opens one limited-query handle at a time; protected/reused PIDs stay unknown.
// Windows working set is resident shared+private pages, not Mac phys_footprint.
modules::ActivityAppsRaw readWindowsActivityApps(std::span<const modules::ActivityAppIdentity>,double timestamp,double uptime);
// Locale-aware natural Windows ordering; not a bit-identical Foundation
// collation claim. Portable tests inject an explicit deterministic comparator.
int compareWindowsActivityNames(std::string_view,std::string_view);

class ActivityProbe final {
public:
    struct Readers {std::function<modules::ActivityRawSample()>system;std::function<modules::ActivityAppsRaw()>apps;};
    using Changed=std::function<void(bool system,bool apps)>;
    struct Stats {std::uint64_t submitted{},completed{},discarded{},failures{},backpressure{};bool inFlight{},waiting{};};
    // State, plan and executor outlive this owner-thread object. Readers must
    // own their worker inputs; never capture HWND/UI/controller references.
    ActivityProbe(modules::ActivityState&,modules::ActivitySamplingPlan&,app::UtilityExecutor&,Readers,Changed={});
    ~ActivityProbe();ActivityProbe(const ActivityProbe&)=delete;ActivityProbe&operator=(const ActivityProbe&)=delete;
    // Call only on shared deadline, demand change, or executor drain. Busy
    // sample ticks are coalesced, not replayed. No independent clock/thread.
    bool update(double now);bool submitPending();Stats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
