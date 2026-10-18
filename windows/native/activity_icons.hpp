#pragma once
// Activity Monitor app icons (ActivityMonitorCanvas.requestVisibleIcons):
// requested only for the visible app rows of the Apps tab, at most eight
// pending, loaded off the UI thread on the shared UtilityExecutor and kept in
// a cache pruned to the running apps of the latest sample. A completed load
// only advances revision(); the owner rebinds the rows on its next content
// event. Missing artwork uses the generic Windows application icon, as
// NSWorkspace.icon(forFile:) falls back to a generic one on the Mac.
#include "app/utility_executor.hpp"
#include "modules/activity_state.hpp"
#include "native/activity_scene.hpp"
#include "native/layer_image_source.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
struct ActivityIconPixels {unsigned width{},height{};std::vector<std::uint8_t>straightRGBA;};
// Worker-only, value-owned loader for one ActivityAppIdentity::iconKey (a
// desktop executable path, or "appsfolder:<AUMID>" for a packaged app) as a
// square straight-RGBA image of `pixels` x `pixels`. Empty when unavailable.
using ActivityIconLoader=std::function<std::optional<ActivityIconPixels>(const std::string& iconKey,unsigned pixels)>;
#ifdef _WIN32
// Executable icon resources (SHDefExtractIcon: no shell extension handlers),
// a packaged app's AppsFolder icon, else the stock application icon.
ActivityIconLoader windowsActivityIconLoader();
#endif
struct ActivityIconStats {std::uint64_t requests{},jobs{},loaded{},unavailable{},publications{},backpressure{};std::size_t cached{},pending{};bool inFlight{};};

class ActivityIconProvider final {
public:
    static constexpr std::size_t maximumPending=8,maximumCached=64,maximumVisible=8;
    // Executor outlives this owner-thread object (the provider invalidates its
    // route on destruction; completions never run afterwards).
    ActivityIconProvider(app::UtilityExecutor&,ActivityIconLoader);
    ~ActivityIconProvider();
    ActivityIconProvider(const ActivityIconProvider&)=delete;
    ActivityIconProvider&operator=(const ActivityIconProvider&)=delete;
    // Content event: the visible rows' icon keys (at most eight). Publishes the
    // cached ones into images() (most recently used, so they stay resident for
    // the raster that follows) and returns exactly their bindings; requests
    // the missing ones. Square `pixels` (18 pt at the row's render scale).
    ActivityIcons bind(std::span<const std::string_view> visible,unsigned pixels);
    // Keeps cached icons only for these running apps (iconCache filter).
    void retain(std::span<const modules::ActivityApp> live);
    // Advances when a load completes, so the owner knows to rebind.
    std::uint64_t revision()const noexcept;
    // Executor backpressure left a request unsent: rebind on the next event.
    bool retryPending()const noexcept;
    LayerImageSource&images()noexcept;
    ActivityIconStats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
} // namespace endfield::native
