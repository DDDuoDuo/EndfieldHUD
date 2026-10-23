#include "tools/activity_owner.hpp"
#ifdef _WIN32
#include <stdexcept>

namespace endfield::tools {
namespace {
void need(bool b, const char* m) { if (!b) throw std::invalid_argument(m); }
}
struct ActivityService::Impl {
    modules::ActivityState state;
    std::function<void(bool, bool, double)> demand;
    std::unique_ptr<native::ActivityIconLoads> icons;
    modules::ActivityCanvas* canvas{};
    std::uint64_t seenRevision{}, seenLoads{};
    explicit Impl(ActivityServiceOptions& o) : state(std::move(o.compareNames)), demand(std::move(o.demandChanged)) {
        if (o.iconExecutor) {
            if (!o.iconLoader) o.iconLoader = native::windowsActivityIconLoader();
            icons = std::make_unique<native::ActivityIconLoads>(*o.iconExecutor, std::move(o.iconLoader));
        }
        // Synthetic readings only: production history starts empty (the Mac's).
        if (o.initial.timestamp > 0) state.receive(std::move(o.initial));
        if (o.apps.timestamp > 0 || !o.apps.items.empty()) state.receiveApps(std::move(o.apps));
    }
};
ActivityService::ActivityService(ActivityServiceOptions o) : impl_(std::make_unique<Impl>(o)) {}
ActivityService::~ActivityService() = default;
modules::ActivityState& ActivityService::providerState() noexcept { return impl_->state; }
const modules::ActivityState& ActivityService::state() const noexcept { return impl_->state; }
modules::ActivityCanvas* ActivityService::canvas() const noexcept { return impl_->canvas; }
std::optional<native::ActivityIconStats> ActivityService::iconStats() const { if (!impl_->icons) return std::nullopt; return impl_->icons->stats(); }
app::ModuleRouting ActivityService::routing() noexcept { app::ModuleRouting r; r.pointer = 12; r.wheel = 8; r.key = 9; return r; }
std::unique_ptr<DesignModuleOwner> ActivityService::makeOwner(native::LayerRasterizer& raster, modules::design::Context& context, DesignOwnerOptions options) {
    auto& i = *impl_;
    need(!i.canvas, "Activity Monitor has one owner");
    // App rows draw the published icons as memory images.
    if (i.icons) options.raster.memoryImages = &i.icons->images();
    auto canvas = std::make_unique<modules::ActivityCanvas>(context, i.state,
        [this](bool system, bool apps, double time) { if (impl_->demand) impl_->demand(system, apps, time); },
        i.icons ? i.icons->loader() : modules::ActivityAppIcons::Loader{});
    i.canvas = canvas.get();
    // Readings and icons arrive in the shared executor's drain (the probe,
    // the icon loads): that utility completion presents the changed page.
    auto previous = std::move(options.hostEvent);
    options.hostEvent = [this, previous = std::move(previous)](const app::HostEvent& e, double t) {
        bool changed = previous && previous(e, t);
        if (e.kind != app::HostEvent::Kind::utilityCompleted || !impl_->canvas) return changed;
        auto& s = *impl_;
        const auto loads = s.canvas->icons().loadCount();
        if (s.canvas->isActive() && (s.state.revision() != s.seenRevision || loads != s.seenLoads)) changed = true;
        s.seenRevision = s.state.revision(); s.seenLoads = loads;
        return changed;
    };
    auto core = std::make_unique<DesignOwnerCore>(raster, context, std::move(options));
    core->add(std::move(canvas));
    return std::make_unique<DesignModuleOwner>(std::move(core));
}
} // namespace endfield::tools
#endif
