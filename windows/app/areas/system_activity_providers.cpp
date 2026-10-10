#include "app/areas/system_activity_providers.hpp"
#include "app/application_host.hpp"
#include "app/event_log_save_queue.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "modules/activity_state.hpp"
#include "native/activity_icons.hpp"
#include "native/clipboard_provider.hpp"
#include "native/event_log_owner.hpp"
#include "native/event_log_time.hpp"
#include "native/storage_files.hpp"
#include <array>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::providers {
namespace {
namespace fs = std::filesystem;
namespace gpu = native;
bool timestampShape(const std::string& text) {
    if (text.size() != 14) return false;
    for (std::size_t n = 0; n < text.size(); ++n) {
        const char c = text[n];
        if (n == 2 ? c != '-' : n == 5 ? c != ' ' : n == 8 || n == 11 ? c != ':' : (c < '0' || c > '9')) return false;
    }
    return true;
}
std::optional<gpu::ActivityIconPixels> syntheticIcon(const std::string& key, unsigned pixels) {
    if (key != "activity/coverage") return {}; // unavailable artwork stays icon-free
    gpu::ActivityIconPixels out;
    out.width = out.height = pixels;
    out.straightRGBA.assign(std::size_t(pixels) * pixels * 4, 0x80);
    return out;
}

class SystemProvidersCoverage final : public app::ModuleOwner {
public:
    explicit SystemProvidersCoverage(const app::ApplicationModuleContext& c)
        : host_(*c.host), utility_(*c.utility), root_(c.coverageRoot), clock_(c.clock ? c.clock() : 0),
          events_(root_, gpu::EventLogOwnerCallbacks{[this] { return clock_; }, [this] { ++eventChanges_; }}),
          saves_(events_, utility_), icons_(utility_, syntheticIcon) {
        gpu::ClipboardPayload link;
        link.kind = gpu::ClipboardKind::url;
        link.text = u"https://example.com/endfieldhud-coverage";
        if (history_.ingest(std::move(link)) != gpu::ClipboardInsertResult::inserted) throw std::runtime_error("Synthetic clipboard history rejected its link");
        linkID_ = history_.items().front().id;
        gpu::ClipboardProviderSource source;
        source.history = [this]() -> const gpu::ClipboardHistory& { return history_; };
        source.copy = [this](std::uint64_t) { ++copies_; return true; }; // synthetic copy-back: the OS clipboard is never written
        source.copied = [this](gpu::ClipboardKind kind) {
            events_.record({ehud::data::makeUUID(), modules::EventKind::clipboardCopied, reference_, {{"kind", std::string(gpu::clipboardEventKind(kind))}}});
        };
        clipboard_.emplace(std::move(source));
    }
    ~SystemProvidersCoverage() override { if (clipboard_) clipboard_->close(); }
    SystemProvidersCoverage(const SystemProvidersCoverage&) = delete;
    SystemProvidersCoverage& operator=(const SystemProvidersCoverage&) = delete;

    std::string_view name() const noexcept override { return "systemProviders"; }
    bool hostEvent(const app::HostEvent& e, double) override {
        if (e.kind == app::HostEvent::Kind::utilityCompleted) saves_.retry();
        return false;
    }
    void coverage(app::ModuleCoverage& step) override {
        if (step.module) return;
        clock_ = step.time;
        const auto cycle = step.cycle;
        step.check(host_.mode() == app::ApplicationAreaMode::hiddenCoverage && host_.utility() == &utility_, "System providers run on the shared executor in hidden coverage");

        // Clipboard copy-back -> Event Log clipboardCopied {kind}, persisted
        // on the shared executor and read back by a second owner.
        reference_ = 812000000. + cycle;
        const auto actions = clipboard_->actions();
        step.check(actions.copy(linkID_) && copies_ == cycle + 1, "Clipboard copy-back reaches its source once");
        step.check(!actions.copy(linkID_ + 1000) && copies_ == cycle + 1, "An unknown clipboard item copies nothing");
        saves_.capture(clock_, true);
        step.drain();
        const gpu::EventLogOwner reloaded(root_, gpu::EventLogOwnerCallbacks{[] { return 0.; }, {}});
        const auto snapshot = reloaded.snapshot();
        step.check(snapshot.events.size() == cycle + 1 && snapshot.events.front().kind == modules::EventKind::clipboardCopied &&
                   snapshot.events.front().metadata == modules::EventMetadata{{"kind", "url"}} && !snapshot.status,
                   "A copy records clipboardCopied {kind} only, saved through the shared executor");
        step.check(fs::is_regular_file(root_ / "EventLog" / "events.json"), "The Event Log file lives in the area's own coverage root");

        // EventLogCanvas timestamps: current Windows zone, MM-dd HH:mm:ss.
        const auto local = gpu::localEventLogTimestamp(reference_);
        DYNAMIC_TIME_ZONE_INFORMATION zone{};
        const bool known = GetDynamicTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID;
        step.check(timestampShape(local) && (!known || zone.DynamicDaylightTimeDisabled || gpu::eventLogTimestampInZone(reference_, zone.TimeZoneKeyName) == local),
                   "Event Log rows format in the current time zone");
        step.check(gpu::localEventLogTimestamp(std::numeric_limits<double>::quiet_NaN()) == "--", "An unrepresentable date has no invented time");

        // Activity icons: visible keys load on the shared executor, once.
        const std::array<std::string_view, 2> keys{"activity/coverage", "activity/unavailable"};
        const auto jobs = icons_.stats().jobs;
        auto bound = icons_.bind(keys, 36);
        if (cycle == 0) {
            step.check(bound.empty() && icons_.stats().jobs == jobs + 1, "Missing icons are requested, not drawn as placeholders");
            step.drain();
            bound = icons_.bind(keys, 36);
        } else {
            step.check(icons_.stats().jobs == jobs, "Cached icons never reload in later cycles");
        }
        const auto icon = bound.find("activity/coverage");
        step.check(bound.size() == 1 && icon != bound.end() && icons_.images().acquire(icon->second.memoryKey, icon->second.revision),
                   "A loaded icon is bound and resident; unavailable artwork stays icon-free");

        // Charge-metric telemetry over the shared sampling plan.
        modules::ActivityState state([](std::string_view a, std::string_view b) { return a.compare(b); });
        modules::ActivitySamplingPlan plan;
        modules::ActivityTelemetryLink telemetry(plan, state);
        unsigned delivered{}, cleared{};
        const auto deliver = [&](const modules::ActivitySnapshot* value) { value ? ++delivered : ++cleared; };
        plan.start(step.time);
        (void)plan.takeDue(step.time);
        step.check(telemetry.synchronize(true, step.time, deliver) && plan.alertActive() && plan.interval() == 1 && delivered == 1,
                   "A non-battery charge metric attaches 1 Hz sampling and gets the current sample");
        step.check(telemetry.synchronize(false, step.time, deliver) && !plan.alertActive() && plan.interval() == 5 && cleared == 1,
                   "Detaching returns to the 5 s session history");

        // Storage title and Clipboard status vocabulary.
        step.check(gpu::storageVolumeTitle("", "Local Disk (C:)", "C:\\") == "Local Disk" && gpu::storageVolumeTitle("", "", "C:\\") == "Startup disk",
                   "An unlabelled volume takes Explorer's name");
        step.check(!gpu::clipboardStatusText(gpu::clipboard_status::pending, core::Language::english) &&
                   gpu::clipboardStatusText(gpu::clipboard_status::tooManyFiles, core::Language::english) == "This file selection is too large to cache.",
                   "Clipboard capture statuses use the source messages");
        ++steps_;
    }

private:
    app::ApplicationHost& host_;
    app::UtilityExecutor& utility_;
    fs::path root_;
    double clock_{}, reference_{812000000.};
    gpu::EventLogOwner events_;
    app::EventLogSaveQueue saves_;
    gpu::ActivityIconProvider icons_;
    gpu::ClipboardHistory history_{8};
    std::optional<gpu::NativeClipboardProvider> clipboard_;
    std::uint64_t linkID_{};
    unsigned copies_{}, eventChanges_{}, steps_{};
};
}

std::unique_ptr<app::ModuleOwner> systemProvidersCoverage(const app::ApplicationModuleContext& context) {
    // Coverage-only registration; it owns nothing in any other mode.
    if (context.mode != app::ApplicationAreaMode::hiddenCoverage || !context.host || !context.utility || context.coverageRoot.empty()) return nullptr;
    return std::make_unique<SystemProvidersCoverage>(context);
}
} // namespace endfield::providers
