#pragma once
#include "modules/activity_state.hpp"
#include <memory>
#include <atomic>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace endfield::native {
// Explicit current-user metadata. imageKey is a platform-normalized executable
// identity, never an inferred installation-directory/app identity. iconKey is
// the executable path, or "appsfolder:<AUMID>" for a packaged app
// (native/activity_icons.hpp).
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
        // Optional GPU engine readings of the catalog's processes (sorted
        // PIDs), Windows: native::readWindowsActivityGPU.
        std::function<std::optional<modules::ActivityGPUSample>(std::span<const std::uint32_t>)>gpu;
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
// inventory reader is called. No user-window titles/command lines/document
// contents, process memory reads, shell commands or icon decoding. Current-user
// only. Desktop apps without any top-level window and without an AUMID are not
// discoverable by this first provider.
// App names (NSRunningApplication.localizedName): a packaged app's Start
// display name, else the executable's localized version-resource
// FileDescription (what Task Manager shows), else the executable name. Only
// app-card candidates (a top-level window or an AUMID) are resolved, once per
// identity and five-second inventory, on the sampling worker; the name cache
// holds only identities seen in the latest inventory (at most 512).
ActivityCatalogSampler::Readers windowsActivityCatalogReaders();

// Display-name hygiene shared by every platform reader: control characters
// and surrounding/duplicate whitespace removed, at most 160 UTF-8 bytes on a
// code point boundary; empty input or invalid UTF-8 yields the fallback.
std::string activityDisplayName(std::string_view candidate,std::string_view fallback);
// Version-resource translation choice: the user's UI language, then the same
// primary language, then language-neutral (0), then the first entry.
// `translations` holds (LANGID, codepage) pairs in resource order.
std::optional<std::size_t>activityTranslation(std::span<const std::pair<std::uint16_t,std::uint16_t>>translations,std::uint16_t userLanguage);
#ifdef _WIN32
// Worker-only readers (exposed for synthetic tests). Empty when unavailable.
std::string windowsActivityFileDescription(const std::wstring&executablePath);
std::string windowsActivityPackagedName(const std::wstring&applicationUserModelID);
#endif
}
