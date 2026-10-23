#pragma once
// Activity Monitor's platform services around its design canvas
// (modules/activity_canvas.hpp): the session's readings (modules::
// ActivityState, fed by the host's native::ActivityProbe on the shared
// executor), the app-icon loads (native::ActivityIconLoads on the same
// executor) and the host's sampling demand. The module itself is a
// tools::DesignModuleOwner over one ActivityCanvas (makeOwner); this replaces
// tools/activity_preview and its app/application_modules.cpp adapter.
// No window, timer, thread or provider of its own.
#ifdef _WIN32
#include "modules/activity_canvas.hpp"
#include "native/activity_icons.hpp"
#include "tools/hud_design_owner.hpp"

namespace endfield::tools {
struct ActivityServiceOptions {
    // localizedStandardCompare (native::compareWindowsActivityNames in
    // production; tests a deterministic one).
    modules::ActivityNameCompare compareNames;
    // Synthetic readings (hidden coverage, previews); production starts empty.
    modules::ActivitySnapshot initial;
    modules::ActivityAppsSnapshot apps;
    // The host's ActivitySamplingPlan demand: 1 Hz system samples while
    // Activity is the active module, app readings while Apps shows.
    std::function<void(bool visible, bool apps, double time)> demandChanged;
    // Optional app icons on this shared executor (it must outlive the
    // service); the loader defaults to native::windowsActivityIconLoader.
    // Without an executor every app shows the generic tile.
    app::UtilityExecutor* iconExecutor{};
    native::ActivityIconLoader iconLoader;
};
class ActivityService final {
public:
    // The host publishes its built-in Activity Monitor under this key for
    // module areas (ApplicationHost::find), e.g. the hidden coverage checks.
    static constexpr std::string_view serviceKey = "activityMonitor";
    explicit ActivityService(ActivityServiceOptions);
    ~ActivityService();
    ActivityService(const ActivityService&) = delete;
    ActivityService& operator=(const ActivityService&) = delete;
    // The readings the host's ActivityProbe and charge telemetry use.
    modules::ActivityState& providerState() noexcept;
    const modules::ActivityState& state() const noexcept;
    // The module's owner (routing: pointer 12, wheel 8, key 9). Destroy it
    // before this service. A utility completion that delivered readings or
    // icons presents a frame (DesignOwnerOptions::hostEvent).
    std::unique_ptr<DesignModuleOwner> makeOwner(native::LayerRasterizer&, modules::design::Context&, DesignOwnerOptions);
    static app::ModuleRouting routing() noexcept;
    // The canvas while its owner lives (actions for the hidden coverage).
    modules::ActivityCanvas* canvas() const noexcept;
    std::optional<native::ActivityIconStats> iconStats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif
