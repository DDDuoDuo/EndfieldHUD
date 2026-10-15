#pragma once
#include "core/scene.hpp"
#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::modules {
struct ActivityCPU {std::uint64_t user{},system{},idle{},nice{};bool operator==(const ActivityCPU&)const=default;};
struct ActivityBytes {std::uint64_t received{},sent{};bool operator==(const ActivityBytes&)const=default;};
using ActivityCounters=std::map<std::string,ActivityBytes,std::less<>>;
struct ActivityMemory {std::uint64_t used{},total{};std::optional<std::uint64_t>compressed;bool operator==(const ActivityMemory&)const=default;};
// Exact Mac physical-footprint formula, also available to the source oracle.
std::optional<ActivityMemory>activityMacMemory(std::uint64_t physical,std::uint64_t pageSize,std::uint64_t internal,std::uint64_t purgeable,std::uint64_t wired,std::uint64_t compressed)noexcept;
// GPU (a Windows addition the Mac Activity Monitor does not show). One GPU
// engine: the adapter LUID and its physical node (together one physical
// adapter), the engine type and the engine index, exactly as the
// "GPU Engine" performance counter instance names them
// (pid_<pid>_luid_0x<high>_0x<low>_phys_<n>_eng_<n>_engtype_<type>); `type`
// is activityGPUEngineType of the engtype_ label. Ordered by adapter, then
// type, then index, so one adapter's engines of one type are adjacent.
struct ActivityGPUEngine {
    std::uint64_t luid{};std::uint32_t physical{},type{},engine{};
    auto operator<=>(const ActivityGPUEngine&)const=default;
};
// The type key of an engtype_ label ("3D", "Copy", "VideoDecode",
// "Compute_0", ...; driver-defined): 32-bit FNV-1a over its bytes, so the
// engines of one label share a type without the label being stored. Two
// labels colliding would only be counted as one type.
std::uint32_t activityGPUEngineType(std::string_view label)noexcept;
// A process's cumulative running time on one engine, in 100 ns units.
struct ActivityGPUUsage {std::uint32_t pid{};ActivityGPUEngine engine;std::uint64_t runningTime{};bool operator==(const ActivityGPUUsage&)const=default;};
// One collection of every engine instance: `time` is the collection's own
// 100 ns time base; `usage` is sorted by (engine, pid) without duplicates.
// At most maximumUsage entries (a larger collection is unavailable).
struct ActivityGPUSample {
    static constexpr std::size_t maximumUsage=16384;
    std::uint64_t time{};std::vector<ActivityGPUUsage>usage;
    bool operator==(const ActivityGPUSample&)const=default;
};
// Total GPU load between two collections (the GPU row's "TOTAL LOAD", the
// Apps GPU column), over the selected processes (all when `pids` is empty):
//  - an engine's utilization is their summed running-time growth on it over
//    the elapsed time base, at most 100%;
//  - an engine type's utilization on one physical adapter is its busiest
//    engine of that type (two Copy engines each half busy are 50% Copy, not
//    100%);
//  - an adapter's total load is the sum of its engine types' utilizations
//    (3D + Copy + Video Decode + Video Encode + Compute + ...), at most 100%;
//  - the figure is the busier adapter's total load: two GPUs (a discrete and
//    an integrated one) are neither added (one 0-100% figure) nor averaged
//    (an idle adapter would dilute the busy one).
// A process whose counter went backwards (an instance restarted) contributes
// nothing; one present in only one collection contributes nothing.
// Unavailable without both collections, a positive elapsed time base or a
// valid usage list. Allocation-free.
std::optional<double>activityGPUPercent(const ActivityGPUSample&current,const ActivityGPUSample&previous,std::span<const std::uint32_t>pids={});
struct ActivityRawSample {double timestamp{},uptime{};std::optional<ActivityCPU>cpu;std::optional<ActivityMemory>memory;std::optional<ActivityCounters>network,disk;std::optional<ActivityGPUSample>gpu;};
struct ActivitySnapshot {
    double timestamp{},uptime{};std::optional<double>cpuPercent;std::optional<ActivityMemory>memory;
    std::optional<double>upload,download,diskRead,diskWrite;std::vector<std::string>statusNotes;
    std::optional<double>gpuPercent; // Windows addition; adds no status note (the Mac notes stay exact)
    bool operator==(const ActivitySnapshot&)const=default;
};
ActivitySnapshot deriveActivity(const ActivityRawSample&,const ActivityRawSample*previous=nullptr);
struct ActivityAppIdentity {std::string id,name,iconKey;std::vector<std::uint32_t>processIDs;bool operator==(const ActivityAppIdentity&)const=default;};
struct ActivityProcess {
    std::uint32_t pid{};std::uint64_t startID{},cpuTicks{},memoryBytes{};double startUptime{};
    // Absent Windows disk accounting is not fabricated from all-process I/O.
    std::optional<std::uint64_t>diskRead,diskWrite;
};
struct ActivityAppNetwork {double uptime{},intervalStart{};std::map<std::uint32_t,std::array<double,2>>rates;std::set<std::uint32_t>unknownPIDs;};
struct ActivityAppsRaw {
    double timestamp{},uptime{},secondsPerCPUTick{};std::vector<ActivityAppIdentity>apps;
    std::map<std::string,std::vector<std::uint32_t>,std::less<>>members;std::map<std::uint32_t,ActivityProcess>processes;
    std::optional<ActivityAppNetwork>network;bool inventoryComplete{true};std::vector<std::string>statusNotes;
    std::optional<ActivityGPUSample>gpu; // the catalog's processes only (Windows addition)
};
// gpuPercent: the total GPU load of the app's member processes (activityGPUPercent).
struct ActivityApp {ActivityAppIdentity identity;std::optional<double>cpuPercent,upload,download,diskRead,diskWrite;std::optional<std::uint64_t>memoryBytes;std::vector<std::string>statusNotes;std::optional<double>gpuPercent;bool operator==(const ActivityApp&)const=default;};
struct ActivityAppsSnapshot {double timestamp{},uptime{};std::vector<ActivityApp>items;std::vector<std::string>statusNotes;};
ActivityAppsSnapshot deriveActivityApps(const ActivityAppsRaw&,const ActivityAppsRaw*previous=nullptr);
enum class ActivitySort {name,cpu,memory,network,disk,upload,download,diskRead,diskWrite,gpu};
std::string_view activitySortKey(ActivitySort)noexcept;
// Source localizedStandardCompare is supplied by the platform, never replaced
// with byte ordering for non-English names. Return negative/equal/positive.
using ActivityNameCompare=std::function<int(std::string_view,std::string_view)>;
void sortActivityApps(std::vector<ActivityApp>&,ActivitySort,bool descending,const ActivityNameCompare&);

struct ActivityGraphTrace {
    std::array<core::Point,60>points{};
    // Source clip rectangles exclude every absent interval, including an
    // isolated valid sample; do not draw a fabricated line through gaps.
    std::array<core::Rect,59>coverage{};std::size_t coverageCount{};bool markerVisible{};core::Point marker{};
};
ActivityGraphTrace activityGraph(std::span<const std::optional<double>>values,double ceiling,std::span<const double>timestamps={},double width=165,double height=36);
struct ActivityRowPlacement {std::size_t index{};core::Rect rect;};
struct ActivityRows {std::array<ActivityRowPlacement,6>rows{};std::size_t count{};std::optional<core::Rect>scrollbar;};
class ActivityState final {
public:
    static constexpr std::size_t historyLimit=60,maximumApps=256;
    static constexpr core::Rect appsViewport(){return {12,84,376,226};}
    explicit ActivityState(ActivityNameCompare);
    void receive(ActivitySnapshot);void receiveApps(ActivityAppsSnapshot);
    bool setApps(bool);bool sort(ActivitySort);bool scroll(core::Point,double delta);
    bool showingApps()const noexcept{return apps_;}double scrollOffset()const noexcept{return offset_;}
    double maximumOffset()const noexcept;ActivitySort sortKey()const noexcept{return sort_;}bool descending()const noexcept{return descending_;}
    const ActivitySnapshot&snapshot()const noexcept{return latest_;}
    std::span<const ActivitySnapshot>history()const noexcept{return {history_.data(),count_};}
    const ActivityAppsSnapshot&appSnapshot()const noexcept{return appSnapshot_;}
    ActivityRows visibleRows()const noexcept;std::uint64_t revision()const noexcept{return revision_;}
    std::uint64_t overviewRevision()const noexcept{return overviewRevision_;}
    std::uint64_t appsContentRevision()const noexcept{return appsRevision_;}
    std::uint64_t placementRevision()const noexcept{return placementRevision_;}
private:ActivityNameCompare compare_;ActivitySnapshot latest_;std::array<ActivitySnapshot,60>history_;std::size_t count_{};ActivityAppsSnapshot appSnapshot_;bool apps_{},descending_{true};ActivitySort sort_{ActivitySort::cpu};double offset_{};std::uint64_t revision_{},overviewRevision_{},appsRevision_{},placementRevision_{};
};

// Shared host clock/deadline policy. No OS timer, sampling, allocation, thread,
// callback or window. System history continues at5s once started; visibility
// OR charge alert makes it1s. Apps requests exist only on the visible Apps tab.
struct ActivityDemand {bool system{},apps{},resetSystem{},resetApps{};std::uint64_t generation{},appGeneration{};};
class ActivitySamplingPlan final {
public:
    void start(double now);void stop();void setVisible(bool activity,bool alert,bool apps,double now);
    // SystemActivityMonitor.setAlertActive: the charge alert/badge's own 1 Hz
    // demand, independent of Activity's visibility (which it keeps). Starts
    // a stopped session like the source; an unchanged value is a no-op.
    void setAlert(bool alert,double now);
    bool alertActive()const noexcept{return running_&&alert_;}bool visibleActive()const noexcept{return running_&&visible_;}
    ActivityDemand takeDue(double now);std::optional<double>nextWakeTime()const noexcept;
    bool running()const noexcept{return running_;}bool appsActive()const noexcept{return running_&&visible_&&apps_;}
    double interval()const noexcept{return visible_||alert_?1:5;}std::uint64_t generation()const noexcept{return generation_;}std::uint64_t appGeneration()const noexcept{return appGeneration_;}
private:bool running_{},visible_{},alert_{},apps_{},resetSystem_{},resetApps_{};std::uint64_t generation_{},appGeneration_{};std::optional<double>systemDue_,appsDue_;
};

// OverlayController.reconcileChargeMetricTelemetry: the charge alert/badge
// attach to the shared system samples only while they display a non-battery
// metric. Attaching sets the plan's alert demand (1 Hz) and delivers the
// current snapshot at once; every later system sample is delivered once;
// detaching clears the demand and delivers "no telemetry" (nullptr) once.
// The owner calls synchronize() from its existing events (deadline, executor
// drain, refresh); no timer, observer list or allocation is involved.
class ActivityTelemetryLink final {
public:
    using Deliver=std::function<void(const ActivitySnapshot*)>;
    ActivityTelemetryLink(ActivitySamplingPlan&,const ActivityState&);
    // True when the plan's next wake changed (the caller reschedules).
    bool synchronize(bool demand,double now,const Deliver&);
    bool attached()const noexcept{return attached_;}
private:ActivitySamplingPlan&plan_;const ActivityState&state_;bool attached_{};std::uint64_t delivered_{};
};
} // namespace endfield::modules
