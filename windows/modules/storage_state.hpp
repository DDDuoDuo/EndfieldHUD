#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace endfield::modules {
struct StorageCapacity {
    std::string volumeName;std::int64_t totalBytes{},availableBytes{};double updatedAt{};
    std::int64_t usedBytes()const noexcept{return totalBytes-availableBytes;}
    bool operator==(const StorageCapacity&)const=default;
};
struct StorageSnapshot {std::optional<StorageCapacity>capacity;bool isLoading{};std::optional<std::string>error;bool operator==(const StorageSnapshot&)const=default;};
struct StorageCategoryUsage {std::string id,title;std::optional<std::int64_t>bytes;bool isPartial{};bool operator==(const StorageCategoryUsage&)const=default;};
struct StorageDetailsSnapshot {std::vector<StorageCategoryUsage>categories;bool isLoading{};std::optional<double>updatedAt;bool isPartial{};std::optional<std::string>error;bool operator==(const StorageDetailsSnapshot&)const=default;};
class StorageScanCancellation final {
public:bool cancelled()const noexcept{return cancelled_.load(std::memory_order_relaxed);}void cancel()noexcept{cancelled_.store(true,std::memory_order_relaxed);}
private:std::atomic<bool>cancelled_{};
};
struct StorageCapacityRequest {std::uint64_t token{};double requestedAt{};};
struct StorageDetailsRequest {std::uint64_t token{};std::shared_ptr<StorageScanCancellation>cancellation;};
// Caller-thread state matching original StorageController. The caller consumes
// requests into its existing serial utility executor and feeds completions on
// this same thread. No service, timer, callback, file walk or private clock.
// Capacity completion remains valid while hidden; details are cancelled and
// replacement waits for the cancelled serial walk to return, exactly as Mac.
class StorageController final {
public:
    static constexpr double capacityCacheDuration=60,detailsCacheDuration=900;
    StorageController()=default;~StorageController();
    StorageController(const StorageController&)=delete;StorageController&operator=(const StorageController&)=delete;
    void activate(double now);void deactivate();void refresh(double now);
    void requestDetails(double now,bool refresh=false);void wake(double now);
    std::optional<double>nextWakeTime()const noexcept{return deadline_;}
    std::optional<StorageCapacityRequest>takeCapacityRequest()noexcept;
    std::optional<StorageDetailsRequest>takeDetailsRequest()noexcept;
    bool completeCapacity(std::uint64_t,std::optional<StorageCapacity>);
    bool completeDetails(std::uint64_t,StorageDetailsSnapshot,double now);
    bool active()const noexcept{return active_;}
    const StorageSnapshot&snapshot()const noexcept{return snapshot_;}
    const StorageDetailsSnapshot&details()const noexcept{return details_;}
    std::uint64_t revision()const noexcept{return revision_;}
private:
    StorageSnapshot snapshot_;StorageDetailsSnapshot details_;
    bool active_{},pendingDetails_{};std::uint64_t serial_{},capacityToken_{},detailToken_{},revision_{};
    std::optional<double>requestedAt_,deadline_;
    std::shared_ptr<StorageScanCancellation>cancellation_;
    std::optional<StorageCapacityRequest>capacityRequest_;std::optional<StorageDetailsRequest>detailRequest_;
    void refreshCapacity(double,bool force=false);std::uint64_t token();void publish()noexcept{++revision_;}
};
struct StorageScanLimits {std::int64_t maximumEntries{40000},entriesPerFolder{8000};double maximumSeconds{10},secondsPerFolder{2};int maximumDepth{32};};
// Shared pure accounting for a future handle-relative metadata-only Windows
// walker. The platform must itself reject symlinks/reparse points, other volumes
// and offline/dataless placeholders BEFORE calling addAllocated; no data is read
// here. A single instance deduplicates file identities across all source scopes.
class StorageScanBudget final {
public:
    explicit StorageScanBudget(double start,StorageScanLimits={});
    void beginScope(double now);bool limitReached(double now,bool cancelled=false)const;
    void examinedEntry();bool mayDescend(int depth)const noexcept;
    bool addAllocated(std::uint64_t volume,std::uint64_t file,std::int64_t bytes);
    bool addSourceBlocks(std::uint64_t volume,std::uint64_t file,std::int64_t blocks);
    std::int64_t scopeBytes()const noexcept{return bytes_;}
    std::int64_t totalEntries()const noexcept{return total_;}
    std::int64_t scopeEntries()const noexcept{return entries_;}
private:
    StorageScanLimits limits_;double start_,scopeStart_;std::int64_t total_{},entries_{},bytes_{};
    std::set<std::pair<std::uint64_t,std::uint64_t>>seen_;
};
}
