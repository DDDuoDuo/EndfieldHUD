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
// GPU: the engine running times of native/activity_gpu.hpp, read with every
// system sample like CPU and RAM (empty only where the counters are
// unavailable), so its history fills while Activity is hidden or the HUD is
// closed too.
modules::ActivityRawSample readWindowsActivity(double timestamp,double uptime);
// Caller supplies an explicit <=256 app catalog with <=8192 unique PIDs.
// Opens one limited-query handle at a time; protected/reused PIDs stay unknown.
// App memory is the process's private working set (Task Manager's "Memory"
// column), the closest public counterpart of the Mac phys_footprint: resident
// pages no other process shares. Before Windows exposes it (pre-20348 builds)
// the whole working set is used, which also counts shared pages.
modules::ActivityAppsRaw readWindowsActivityApps(std::span<const modules::ActivityAppIdentity>,double timestamp,double uptime);
#ifdef _WIN32
// The memory figure above for one open process handle (HANDLE); empty when
// the counters cannot be read.
std::optional<std::uint64_t>windowsActivityProcessMemory(void*process)noexcept;
#endif
// Locale-aware natural Windows ordering; not a bit-identical Foundation
// collation claim. Portable tests inject an explicit deterministic comparator.
int compareWindowsActivityNames(std::string_view,std::string_view);

class ActivityProbe final {
public:
    struct Readers {std::function<modules::ActivityRawSample()>system;std::function<modules::ActivityAppsRaw()>apps;};
    using Changed=std::function<void(bool system,bool apps)>;
    struct Stats {std::uint64_t submitted{},completed{},discarded{},failures{},backpressure{},appReleases{};bool inFlight{},waiting{};};
    // State, plan and executor outlive this owner-thread object. Readers must
    // own their worker inputs; never capture HWND/UI/controller references.
    ActivityProbe(modules::ActivityState&,modules::ActivitySamplingPlan&,app::UtilityExecutor&,Readers,Changed={});
    ~ActivityProbe();ActivityProbe(const ActivityProbe&)=delete;ActivityProbe&operator=(const ActivityProbe&)=delete;
    // Call only on shared deadline, demand change, or executor drain. Busy
    // sample ticks are coalesced, not replayed. No independent clock/thread.
    // When the Apps demand has ended either call also queues the release of
    // the worker's retained Apps reading (the Mac drops it on deactivate).
    // The worker hands each superseded system sample's GPU buffer back to
    // native::recycleWindowsActivityGPU for the next reading.
    bool update(double now);bool submitPending();Stats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
