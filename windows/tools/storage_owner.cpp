#include "tools/storage_owner.hpp"
#ifdef _WIN32
#include "core/data/json.hpp"
#include <stdexcept>

namespace endfield::tools {
namespace {
void need(bool b, const char* m) { if (!b) throw std::invalid_argument(m); }
std::optional<std::string> remembered(const std::function<std::string()>& load) {
    if (!load) return std::nullopt;
    auto value = load();
    if (value.empty() || value.size() > 1024 || !ehud::data::Json::validUtf8(value)) return std::nullopt;
    return value;
}
}
struct StorageService::Impl {
    modules::StorageController controller;
    native::StorageCapacityProbe capacity;
    std::unique_ptr<native::StorageDetailsProbe> details;
    std::function<void()> openSettings;
    std::function<std::string()> loadVolume;
    modules::StorageCanvas* canvas{};
    std::uint64_t seen{};
    Impl(StorageServiceOptions& o)
        : capacity(controller, *o.utility, {std::move(o.read), std::move(o.startupName), std::move(o.clock)}),
          openSettings(std::move(o.openSettings)), loadVolume(std::move(o.loadVolume)) {
        need(bool(o.scanDetails) == bool(o.completionClock), "Storage details require both scanner and owner completion clock");
        if (o.scanDetails) details = std::make_unique<native::StorageDetailsProbe>(controller, *o.utility, std::move(o.scanDetails), std::move(o.completionClock));
        controller.setSelectedVolume(remembered(loadVolume));
        if (o.saveVolume) controller.selectionChanged = std::move(o.saveVolume);
        // The Mac's runWorker: every request goes to the shared executor at once.
        controller.requested = [this] { capacity.submitPending(); if (details) details->submitPending(); };
    }
    // A publication the canvas has not seen yet (a completion, a mount event):
    // readings in place and the result toasts.
    bool notify(double now) {
        if (controller.revision() == seen) return false;
        seen = controller.revision();
        if (canvas) canvas->controllerChanged(now);
        return canvas && canvas->isActive();
    }
};
StorageService::StorageService(StorageServiceOptions o) {
    need(o.utility && bool(o.read) && bool(o.clock), "Storage requires the shared executor, a capacity reader and the owner clock");
    impl_ = std::make_unique<Impl>(o);
}
StorageService::~StorageService() = default;
app::ModuleRouting StorageService::routing() noexcept {
    app::ModuleRouting r; r.pointerCapture = 7; r.pointer = 11; r.wheelCapture = 7; r.wheel = 11; r.keyCapture = 7; r.key = 11; return r;
}
const modules::StorageController& StorageService::controller() const noexcept { return impl_->controller; }
modules::StorageCanvas* StorageService::canvas() const noexcept { return impl_->canvas; }
native::StorageCapacityProbe::Stats StorageService::probeStats() const noexcept { return impl_->capacity.stats(); }
bool StorageService::volumesChanged(double now) { impl_->controller.volumesChanged(now); return impl_->notify(now); }
bool StorageService::requestDetails(bool refresh, double now) {
    auto& i = *impl_;
    if (!i.controller.active() || !i.details) return false;
    i.controller.requestDetails(now, refresh);
    i.notify(now);
    return true;
}
std::unique_ptr<DesignModuleOwner> StorageService::makeOwner(native::LayerRasterizer& raster, modules::design::Context& context, DesignOwnerOptions options) {
    auto& i = *impl_;
    need(!i.canvas, "Storage has one owner");
    auto canvas = std::make_unique<modules::StorageCanvas>(context, i.controller);
    canvas->onOpenSystemStorage = [this] { if (impl_->openSettings) impl_->openSettings(); };
    i.canvas = canvas.get();
    auto previous = std::move(options.hostEvent);
    options.hostEvent = [this, previous = std::move(previous)](const app::HostEvent& e, double t) {
        bool changed = previous && previous(e, t);
        auto& s = *impl_;
        if (e.kind == app::HostEvent::Kind::utilityCompleted) {
            // Retry what executor backpressure left, then publish completions.
            s.capacity.submitPending();
            if (s.details) s.details->submitPending();
            changed = s.notify(t) || changed;
        } else if (e.kind == app::HostEvent::Kind::settingsChanged) {
            // The remembered disk may have loaded or been restored to defaults.
            const auto value = remembered(s.loadVolume);
            if (value != s.controller.selectedVolume()) {
                const auto* before = s.controller.currentVolume();
                const auto shown = before ? std::optional(before->id) : std::nullopt;
                s.controller.setSelectedVolume(value);
                const auto* after = s.controller.currentVolume();
                if ((after ? std::optional(after->id) : std::nullopt) != shown && s.controller.active()) s.controller.refresh(t);
                changed = s.notify(t) || changed;
            }
        }
        return changed;
    };
    auto core = std::make_unique<DesignOwnerCore>(raster, context, std::move(options));
    core->add(std::move(canvas));
    return std::make_unique<DesignModuleOwner>(std::move(core));
}
} // namespace endfield::tools
#endif
