#pragma once
// Activity Monitor app icons on Windows: the pixel loader (an executable's
// icon resource, a packaged app's AppsFolder icon, else the stock application
// icon, as NSWorkspace.icon(forFile:) falls back to a generic one) and
// ActivityIconLoads, the worker side of the portable cache
// modules::ActivityAppIcons (modules/activity_canvas.hpp: the Mac's rules,
// 20 px x the render scale, 256 entries cleared on overflow, 4 per batch).
// A batch loads on the shared UtilityExecutor and is published into this
// object's LayerImageSource under fresh keys; the canvas's rows draw them as
// memory images (DesignOwnerOptions::raster.memoryImages). An image the cache
// drops (overflow, scale change, destruction) is retired here.
#include "app/utility_executor.hpp"
#include "modules/activity_canvas.hpp"
#include "native/layer_image_source.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
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
struct ActivityIconStats {std::uint64_t batches{},loaded{},unavailable{},backpressure{},retired{};std::size_t images{};bool inFlight{};};

class ActivityIconLoads final {
public:
    // The cache's capacity of published images (ActivityAppIcons::capacity).
    static LayerImageSourceLimits limits() noexcept;
    // Executor outlives this owner-thread object (its route is invalidated on
    // destruction; completions never run afterwards).
    ActivityIconLoads(app::UtilityExecutor&,ActivityIconLoader);
    ~ActivityIconLoads();
    ActivityIconLoads(const ActivityIconLoads&)=delete;
    ActivityIconLoads&operator=(const ActivityIconLoads&)=delete;
    // The two callbacks of modules::ActivityAppIcons::Loader, bound to this
    // object (it must outlive every cache using them). One batch at a time;
    // a full executor answers false (the cache retries on its next lookup).
    modules::ActivityAppIcons::Loader loader();
    LayerImageSource&images()noexcept;
    ActivityIconStats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
} // namespace endfield::native
