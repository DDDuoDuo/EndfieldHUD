#pragma once
#include "modules/activity_state.hpp"
#include <memory>
#include <atomic>

namespace endfield::native {
// Explicit current-user metadata. imageKey is a platform-normalized executable
// identity, never an inferred installation-directory/app identity.
struct ActivityCatalogProcess {
    std::uint32_t pid{},parent{};std::uint64_t startID{};
    std::string imageKey,applicationID,name,iconKey;
    bool appWindow{};
};
struct ActivityCatalogInventory {std::vector<ActivityCatalogProcess>processes;bool complete{true};std::vector<std::uint32_t>unreadablePIDs;std::uint64_t captureStartID{};};
struct ActivityCatalogReading {ActivityCatalogProcess identity;std::optional<modules::ActivityProcess>usage;};
struct ActivityCatalogResult {
    std::vector<modules::ActivityAppIdentity>apps;
    std::map<std::uint32_t,ActivityCatalogProcess>identities;
    bool complete{true};
};
// Packaged application IDs, otherwise exact executable identity. Different
// executable helpers join only through an older same-user parent and a path
// inside that desktop app's directory. No generic prefix/directory grouping.
ActivityCatalogResult buildActivityCatalog(const ActivityCatalogInventory&);
class ActivityCatalogSampler final {
public:
    struct Readers {
        std::function<ActivityCatalogInventory()>inventory;
        std::function<std::optional<ActivityCatalogReading>(std::uint32_t)>process;
    };
    explicit ActivityCatalogSampler(Readers);
    // Worker-only, serial calls from the existing ActivityProbe. Five-second
    // bounded catalog refresh; no thread/timer/event hook. reset on Apps close.
    modules::ActivityAppsRaw sample(double timestamp,double uptime);
    void reset()noexcept;
    // UI demand change may invalidate without touching worker state or OS.
    void invalidate()noexcept{generation_.fetch_add(1,std::memory_order_relaxed);}
private:Readers readers_;ActivityCatalogResult catalog_;std::optional<double>inventoryTime_,lastTime_;std::atomic<std::uint64_t>generation_{1};std::uint64_t workerGeneration_{};
};
// Construction is inert. Actual metadata enumeration occurs only when its
// inventory reader is called. No user-window titles/command lines/file contents,
// process memory reads, shell commands or icon decoding. Current-user only.
// Desktop apps without any top-level window and without an AUMID are not
// discoverable by this first provider. App names fall back to executable name.
ActivityCatalogSampler::Readers windowsActivityCatalogReaders();
}
