#pragma once
#include "core/scene.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace endfield::modules {
struct ActivityCPU {std::uint64_t user{},system{},idle{},nice{};bool operator==(const ActivityCPU&)const=default;};
struct ActivityBytes {std::uint64_t received{},sent{};bool operator==(const ActivityBytes&)const=default;};
using ActivityCounters=std::map<std::string,ActivityBytes,std::less<>>;
struct ActivityMemory {std::uint64_t used{},total{};std::optional<std::uint64_t>compressed;bool operator==(const ActivityMemory&)const=default;};
// Exact Mac physical-footprint formula, also available to the source oracle.
std::optional<ActivityMemory>activityMacMemory(std::uint64_t physical,std::uint64_t pageSize,std::uint64_t internal,std::uint64_t purgeable,std::uint64_t wired,std::uint64_t compressed)noexcept;
struct ActivityRawSample {double timestamp{},uptime{};std::optional<ActivityCPU>cpu;std::optional<ActivityMemory>memory;std::optional<ActivityCounters>network,disk;};
struct ActivitySnapshot {
    double timestamp{},uptime{};std::optional<double>cpuPercent;std::optional<ActivityMemory>memory;
    std::optional<double>upload,download,diskRead,diskWrite;std::vector<std::string>statusNotes;
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
};
struct ActivityApp {ActivityAppIdentity identity;std::optional<double>cpuPercent,upload,download,diskRead,diskWrite;std::optional<std::uint64_t>memoryBytes;std::vector<std::string>statusNotes;bool operator==(const ActivityApp&)const=default;};
struct ActivityAppsSnapshot {double timestamp{},uptime{};std::vector<ActivityApp>items;std::vector<std::string>statusNotes;};
ActivityAppsSnapshot deriveActivityApps(const ActivityAppsRaw&,const ActivityAppsRaw*previous=nullptr);
enum class ActivitySort {name,cpu,memory,network,disk,upload,download,diskRead,diskWrite};
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
    ActivityDemand takeDue(double now);std::optional<double>nextWakeTime()const noexcept;
    bool running()const noexcept{return running_;}bool appsActive()const noexcept{return running_&&visible_&&apps_;}
    double interval()const noexcept{return visible_||alert_?1:5;}std::uint64_t generation()const noexcept{return generation_;}std::uint64_t appGeneration()const noexcept{return appGeneration_;}
private:bool running_{},visible_{},alert_{},apps_{},resetSystem_{},resetApps_{};std::uint64_t generation_{},appGeneration_{};std::optional<double>systemDue_,appsDue_;
};
} // namespace endfield::modules
