#pragma once
#include <atomic>
#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace endfield::modules {
// StorageCapacity (Sources/StorageController.swift:6-23): usedFraction drives
// the USED percentage and the filled cells; less than a tenth free is the
// low-space (red) presentation (MODS-437).
struct StorageCapacity {
    std::string volumeName;std::int64_t totalBytes{},availableBytes{};double updatedAt{};
    std::int64_t usedBytes()const noexcept{return totalBytes-availableBytes;}
    double usedFraction()const noexcept{return double(usedBytes())/double(totalBytes);}
    bool isLowOnSpace()const noexcept{return double(availableBytes)<double(totalBytes)*lowSpaceFraction;}
    static constexpr double lowSpaceFraction=.1;
    bool operator==(const StorageCapacity&)const=default;
};
struct StorageSnapshot {std::optional<StorageCapacity>capacity;bool isLoading{};std::optional<std::string>error;bool operator==(const StorageSnapshot&)const=default;};
// One mounted disk Storage can show (StorageVolume, :27-40; the disk choice
// is checklist A5, a Windows addition the Mac adopted): a fixed, removable or
// RAM-disk volume with media mounted at a drive path, with the numbers of the
// read that listed it. Its name is capacity.volumeName.
struct StorageVolume {
    std::string id;    // stable identity: the volume GUID path (\\?\Volume{...}\)
    std::string root;  // its first mount path ("D:\\")
    bool removable{},startup{};
    StorageCapacity capacity;
    const std::string&name()const noexcept{return capacity.volumeName;}
    bool operator==(const StorageVolume&)const=default;
};
// StorageVolumeCatalog.limit (:70): the volume menu lists at most this many
// disks, the startup disk first.
inline constexpr std::size_t storageVolumeLimit=7;
// StorageVolumeCatalog.limited (:116-123): at most storageVolumeLimit entries;
// a chosen disk beyond the limit takes the last place, so the menu always
// shows the current choice.
std::vector<StorageVolume>limitedStorageVolumes(std::vector<StorageVolume>,const std::optional<std::string>&keeping);
// One capacity read (the worker's result): every listed disk with its
// numbers, and the startup reader's capacity when the list lacks the startup
// disk (refreshCapacity, :427-449).
struct StorageCapacityResult {std::vector<StorageVolume>volumes;std::optional<StorageCapacity>startup;bool operator==(const StorageCapacityResult&)const=default;};
struct StorageCategoryUsage {std::string id,title;std::optional<std::int64_t>bytes;bool isPartial{};bool operator==(const StorageCategoryUsage&)const=default;};
struct StorageDetailsSnapshot {std::vector<StorageCategoryUsage>categories;bool isLoading{};std::optional<double>updatedAt;bool isPartial{};std::optional<std::string>error;bool operator==(const StorageDetailsSnapshot&)const=default;};
class StorageScanCancellation final {
public:bool cancelled()const noexcept{return cancelled_.load(std::memory_order_relaxed);}void cancel()noexcept{cancelled_.store(true,std::memory_order_relaxed);}
private:std::atomic<bool>cancelled_{};
};
struct StorageCapacityRequest {std::uint64_t token{};double requestedAt{};};
struct StorageDetailsRequest {std::uint64_t token{};std::shared_ptr<StorageScanCancellation>cancellation;};
// Caller-thread state matching the original StorageController (:228-460).
// The caller consumes requests into its existing serial utility executor and
// feeds completions on this same thread. No service, timer, callback, file
// walk or private clock. Each capacity read lists every mounted disk with its
// numbers (on the worker); snapshot().capacity is the current disk's. The
// choice is remembered by the owner (selectionChanged); while that disk is
// not mounted the startup disk is shown and the choice is kept, so it
// returns with the disk. Capacity completion remains valid while hidden;
// details are cancelled and replacement waits for the cancelled serial walk
// to return, exactly as Mac.
class StorageController final {
public:
    static constexpr double capacityCacheDuration=60,detailsCacheDuration=900;
    StorageController()=default;~StorageController();
    StorageController(const StorageController&)=delete;StorageController&operator=(const StorageController&)=delete;
    void activate(double now);void deactivate();
    // Explicit refresh: inexpensive, re-lists the disks with their numbers;
    // calls during a read coalesce into it.
    void refresh(double now);
    void requestDetails(double now,bool refresh=false);void wake(double now);
    // The remembered choice (storageVolumeIdentifier; Windows: the
    // preference record's "storageVolume"): empty or over 1024 UTF-8 bytes is
    // no choice. Setting it publishes nothing (the owner loads it).
    void setSelectedVolume(std::optional<std::string>);
    const std::optional<std::string>&selectedVolume()const noexcept{return selected_;}
    // selectVolume(identifier:) (:403-417): only a listed disk; remembers it
    // (selectionChanged), shows its listed numbers at once, publishes, then
    // reads fresh ones. False for an unlisted disk.
    bool selectVolume(const std::string&id,double now);
    std::function<void(const std::string&)>selectionChanged;
    // volumesDidChange (:420-425): a disk was mounted, unmounted or renamed.
    // A visible panel re-reads now (after a read in flight); a hidden one on
    // its next activation.
    void volumesChanged(double now);
    // Disks from the latest read, startup first (at most storageVolumeLimit).
    const std::vector<StorageVolume>&volumes()const noexcept{return volumes_;}
    // The disk snapshot() describes: the remembered choice while it is
    // listed, otherwise the startup disk.
    const StorageVolume*currentVolume()const noexcept;
    std::optional<double>nextWakeTime()const noexcept{return deadline_;}
    // A capacity or details request was queued (the Mac's runWorker): the
    // owner hands it to its worker probe at once.
    std::function<void()>requested;
    std::optional<StorageCapacityRequest>takeCapacityRequest()noexcept;
    std::optional<StorageDetailsRequest>takeDetailsRequest()noexcept;
    // `now`: the owner clock at the drain (a pending re-list starts then).
    bool completeCapacity(std::uint64_t,StorageCapacityResult,double now);
    bool completeDetails(std::uint64_t,StorageDetailsSnapshot,double now);
    bool active()const noexcept{return active_;}
    const StorageSnapshot&snapshot()const noexcept{return snapshot_;}
    const StorageDetailsSnapshot&details()const noexcept{return details_;}
    // Bumps on every publication (observers poll it after their events).
    std::uint64_t revision()const noexcept{return revision_;}
private:
    StorageSnapshot snapshot_;StorageDetailsSnapshot details_;std::vector<StorageVolume>volumes_;std::optional<std::string>selected_;
    bool active_{},pendingDetails_{},pendingVolumeRead_{};std::uint64_t serial_{},capacityToken_{},detailToken_{},revision_{};
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
