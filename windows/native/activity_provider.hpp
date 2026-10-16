#pragma once
#include "app/utility_executor.hpp"
#include "modules/activity_state.hpp"
#include <array>
#include <atomic>
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
// Network: the byte counters of the counted interfaces, keyed by interface
// LUID in decimal. One interface as GetIfTable2/GetIfEntry2 report it
// (MIB_IF_ROW2), with only the fields the reading uses.
struct ActivityInterface {std::uint64_t luid{};std::uint32_t type{};bool hardware{},up{};std::uint64_t received{},sent{};};
// The counting rule (the Mac counts its up, running, non-loopback en* and
// ppp* interfaces): an up interface that is hardware Ethernet or Wi-Fi, or
// PPP. Loopback, tunnels, virtual adapters and filter interfaces would count
// the same packets twice.
bool activityCountedInterface(const ActivityInterface&)noexcept;
// The table reading: every counted interface of one interface table (a
// repeated LUID keeps its first row); unavailable above 4096 interfaces.
std::optional<modules::ActivityCounters>activityNetworkCounters(std::span<const ActivityInterface>table);
// The network reading of every system sample, equal to the table reading of
// the moment. Listing every interface (GetIfTable2) cost 22-28 context
// switches and 1.4-1.7 ms per sample on the laptop (40 interfaces); reading
// only the counted one (GetIfEntry2) about 2 and 0.16 ms. So the counted
// interfaces are taken from the table once and kept, each sample reads only
// them, and the sample lists the table again (and is the table reading
// itself) when
//  - an interface change notification named an interface it does not keep
//    (one added, connected or changed; the IP interface change
//    notification, never a timer), or invalidate() was called;
//  - a kept interface cannot be read (removed) or no longer counts (down);
//  - the last listing failed.
// An interface that starts to count is found through its notification; one
// bound to neither IPv4 nor IPv6 sends none and is counted from the next
// listing. Notifications naming a kept interface (the counted Wi-Fi
// adapter's parameter changes, about one a minute on the laptop) list
// nothing: its own reading each sample shows it removed or disconnected.
class ActivityNetworkReader final {
public:
    struct Source {
        // Every interface; false when the table cannot be read.
        std::function<bool(std::vector<ActivityInterface>&)>table;
        // One interface by LUID; false when it cannot be read.
        std::function<bool(std::uint64_t luid,ActivityInterface&)>entry;
    };
    struct Stats {std::uint64_t reads{},listings{},entries{},fallbacks{},invalidations{},skipped{};std::size_t counted{};};
    explicit ActivityNetworkReader(Source);
    // Any thread: the next read lists the table.
    void invalidate()noexcept;
    // Any thread (the change notification, naming an interface by LUID; 0
    // for none): invalidate() unless it names a kept interface. While a
    // listing runs (and above `watchable` kept interfaces) every
    // notification invalidates. True when it invalidated.
    bool notify(std::uint64_t luid)noexcept;
    static constexpr std::size_t watchable=16;
    // One reading into `out` (one caller at a time). With the previous
    // reading's counters in `out` and no interface change it reads each kept
    // interface once and allocates nothing. False (and `out` empty) when the
    // table reading is unavailable.
    bool read(modules::ActivityCounters&out);
    Stats stats()const noexcept;
private:
    bool steady(modules::ActivityCounters&);bool list(modules::ActivityCounters&);
    struct Kept {std::uint64_t luid{};std::string key;};
    Source source_;std::vector<ActivityInterface>table_;std::vector<Kept>kept_;std::atomic<bool>stale_{true};std::atomic<std::uint64_t>invalidations_{},skipped_{};Stats stats_;
    // The kept LUIDs as notify() sees them (0: none).
    std::array<std::atomic<std::uint64_t>,watchable>watched_{};
};
#ifdef _WIN32
// GetIfTable2 and GetIfEntry2 (the live test compares them).
ActivityNetworkReader::Source windowsActivityNetworkSource();
#endif
// The production network reading (readWindowsActivity's): one process-wide
// reader, its interface change notification (NotifyIpInterfaceChange, both
// families) registered by the first reading and kept. Without a
// registration every reading lists the table. The returned counters reuse
// the buffer recycleWindowsActivityNetwork returned. Empty off Windows.
std::optional<modules::ActivityCounters>readWindowsActivityNetwork();
// Gives back a reading the sampling worker no longer needs (ActivityProbe:
// the previous sample's), so the next reading refills it in place.
void recycleWindowsActivityNetwork(modules::ActivityCounters&&)noexcept;
struct WindowsActivityNetworkStats {ActivityNetworkReader::Stats reader;std::uint64_t notifications{},recycled{};bool watching{};};
WindowsActivityNetworkStats windowsActivityNetworkStats()noexcept;
// Cancels the notification and forgets the kept interfaces (tests; the next
// reading registers and lists again).
void releaseWindowsActivityNetwork()noexcept;
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
    // The worker hands each superseded system sample's GPU buffer and
    // network counters back (native::recycleWindowsActivityGPU,
    // recycleWindowsActivityNetwork) for the next reading.
    bool update(double now);bool submitPending();Stats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
