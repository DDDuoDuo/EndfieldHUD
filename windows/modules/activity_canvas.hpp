#pragma once
// Activity Monitor on the design library: the port of
// Sources/ActivityMonitorCanvas.swift (ActivityMonitorCanvas, ActivityGraph,
// ActivityRow, SortColumn, AppTable, ActivityAppIcons; MODS-400..421). It
// replaces the v1.2.0 Windows page (modules/activity_presentation,
// native/activity_scene, tools/activity_preview: 400 x 334 tab plates, the
// 60-vertex time-based graph with its .32 s morph, coverage masks and latest
// marker, the Network/Disk app columns, the RAM "Compressed" line).
//
// Overview: GPU, CPU, RAM, Network and Disk I/O rows (GPU first: the Windows
// addition the Mac adopted) with the last 44 samples. Apps: a sortable,
// virtualised table (App, GPU, CPU, RAM, Threads) of pooled rows over a
// scroll area. The canvas asks for 1 Hz system samples, and app readings on
// Apps, only while Activity is the active module (`demand`, the host's
// ActivitySamplingPlan), and observes modules::ActivityState meanwhile; every
// value updates in place. No telemetry-owned loop remains (animationCount 0).
//
// Esc: the canvas has no back, so Esc on Apps is not handled here and the
// host closes the HUD (Mac 1.3.0 behaviour, MODS-402, owner decision Q1).
#include "modules/activity_state.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_page.hpp"
#include <map>
#include <memory>

namespace endfield::modules {
// ActivityGraph.paths (:240-260): the newest sample at the right edge (fewer
// than 44 leave the left empty), y = h - clamp(v, 0, 100) x .42 x h / 44; a
// missing or non-finite value ends the run (no zero, no line across the
// gap); runs of one point are not drawn. Values 0-100, oldest first.
struct ActivityGraphPaths { design::PathBuilder area, line; };
inline constexpr std::size_t activityGraphSamples = 44;
inline constexpr design::Size activityGraphSize{296, 44};
ActivityGraphPaths activityGraphPaths(std::span<const std::optional<double>>, design::Size = activityGraphSize);

// ActivityAppIcons (:566-620): app icons rasterised once at the row's 20 px
// (x the box render scale) and kept by app id across visits and sorts; a miss
// is queued and loaded later, `batch` per pass; an app without an icon key or
// an icon gets the generic tile; on overflow (capacity) the whole cache is
// cleared; a render-scale change empties it. Windows loads a batch on the
// shared executor (the Mac on later main-queue passes); `Loader::release`
// gives back evicted images. Without a loader every app shows the tile.
class ActivityAppIcons final {
public:
    struct Icon {
        enum class Kind : std::uint8_t { image, generic, pending } kind{Kind::pending};
        design::ImageSource image;
        bool operator==(const Icon&) const = default;
    };
    struct Request { std::string id, key; unsigned pixels{}; };
    // The loaded images, per request in order (nullopt: no icon). False: not
    // taken (the cache was emptied or destroyed meanwhile): the loader
    // releases them itself.
    using Done = std::function<bool(std::vector<std::optional<design::ImageSource>>)>;
    struct Loader {
        std::function<bool(std::vector<Request>, Done)> load; // false: not accepted now (retried on the next lookup)
        std::function<void(const design::ImageSource&)> release;
    };
    static constexpr double side = 20;
    static constexpr std::size_t batch = 4, capacity = 256;
    explicit ActivityAppIcons(Loader = {});
    ~ActivityAppIcons();
    ActivityAppIcons(const ActivityAppIcons&) = delete;
    ActivityAppIcons& operator=(const ActivityAppIcons&) = delete;
    void setScale(double);
    unsigned pixels() const noexcept;
    // A lookup: a miss is queued (pending) and loads on a later pass.
    Icon icon(const ActivityApp&);
    // The end of the caller's pass (the Mac's DispatchQueue.main.async):
    // starts the next batch of up to `batch` queued icons unless one loads.
    void schedule();
    std::function<void(const std::vector<std::string>&)> onLoad; // ids that just loaded
    std::uint64_t loadCount() const noexcept { return loads_; }
    std::size_t cached() const noexcept { return images_.size(); }
    std::size_t queued() const noexcept { return queue_.size(); }
private:
    struct State;
    Loader loader_;
    std::shared_ptr<State> state_;          // completions after destruction are dropped
    std::map<std::string, std::optional<design::ImageSource>> images_;
    std::vector<Request> queue_;
    double scale_{1};
    bool inFlight_{};
    std::uint64_t generation_{}, loads_{};
    bool loaded(std::uint64_t generation, const std::vector<Request>&, std::vector<std::optional<design::ImageSource>>);
    void clear();
};

class ActivityCanvas final : public design::Canvas {
public:
    // The host's sampling demand (SystemActivityMonitor.activate/deactivate,
    // AppActivityMonitor.activate/deactivate): system 1 Hz while active, app
    // readings while the Apps tab shows.
    using Demand = std::function<void(bool system, bool apps, double now)>;
    ActivityCanvas(design::Context&, ActivityState&, Demand = {}, ActivityAppIcons::Loader = {});
    ~ActivityCanvas() override;
    bool isShowingApps() const noexcept { return showingApps_; }
    ActivitySort sortKey() const noexcept { return sortKey_; }
    bool sortDescending() const noexcept { return sortDescending_; }
    ActivityAppIcons& icons() noexcept { return icons_; }
    // The redesigned canvas has no telemetry-owned loops (host diagnostics).
    static constexpr int animationCount = 0;
    // `activity:overview`, `activity:apps` and `activity:sort:<key>`.
    bool perform(std::string_view actionID, double now);

    void buildPage(design::Page&) override;
    void didActivate() override;
    void didDeactivate() override { stopReading(); }
    void willLeave() override { stopReading(); }
    bool handleScroll(design::Point, design::Point delta) override;
private:
    class Row;
    class SortColumn;
    class AppTable;
    ActivityState* state_;
    Demand demand_;
    ActivityAppIcons icons_;
    bool showingApps_{}, observing_{}, reading_{};
    ActivitySort sortKey_{ActivitySort::cpu};
    bool sortDescending_{true};
    std::vector<Row*> rows_;
    std::unique_ptr<AppTable> table_;
    std::optional<int> leavingTab_;
    void stopReading();
    void showApps(bool, double direction);
    void sort(ActivitySort);
    void buildOverview(design::Page&);
    void buildApps(design::Page&);
    void updateOverview();
    void updateApps();
};
} // namespace endfield::modules
