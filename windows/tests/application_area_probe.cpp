#include "tests/application_area_probe.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::tests {
namespace {
namespace fs = std::filesystem;
void require(bool value, const char* why) { if (!value) throw std::runtime_error(std::string("Area probe: ") + why); }

// Shared with the publisher through the host's area service directory.
struct ProbeService { unsigned lookups{}; };

class AreaProbe final : public app::ModuleOwner {
public:
    explicit AreaProbe(const app::ApplicationModuleContext& c)
        : host_(*c.host), window_(static_cast<HWND>(c.window)), utility_(*c.utility), route_(c.utility->makeRoute()), root_(c.coverageRoot) {
        message_ = host_.registerMessage();
        require(message_ >= WM_APP + 0x1000 && message_ < WM_APP + 0x4000 && host_.registerMessage() == message_ + 1, "registered messages are private WM_APP ids");
        host_.publish("areaProbe.service", service_);
        // The coverage root is NEW, empty and owned: write a synthetic input.
        std::ofstream(root_ / "synthetic.txt", std::ios::binary) << "area probe";
    }
    ~AreaProbe() override { host_.withdraw("areaProbe.service"); utility_.invalidate(route_); }
    AreaProbe(const AreaProbe&) = delete;
    AreaProbe& operator=(const AreaProbe&) = delete;

    std::string_view name() const noexcept override { return "areaProbe"; }
    app::ModuleRouting routing() const noexcept override {
        app::ModuleRouting r; r.message = 50; r.finishEditing = 50; r.flush = 50; return r;
    }
    bool message(const app::NativeMessage& m, double) override {
        if (m.message != message_) return false;
        ++messages_; return true;
    }
    bool hostEvent(const app::HostEvent& e, double) override {
        if (e.kind == app::HostEvent::Kind::utilityCompleted) ++drains_;
        return false;
    }
    void coverage(app::ModuleCoverage& step) override {
        step.check(!step.module && !step.project(core::Point{1, 1}, 10, 10), "The probe presents no module and gets the module-less step");
        step.check(host_.mode() == app::ApplicationAreaMode::hiddenCoverage && host_.utility() == &utility_,
                   "The host reports hidden coverage and the shared executor");
        // UtilityExecutor round trip delivered by the coverage drain, which
        // also broadcasts utilityCompleted.
        const auto completedBefore = completed_, drainsBefore = drains_;
        step.check(utility_.submit(route_, [] {}, [this](std::exception_ptr error) { if (!error) ++completed_; }), "Work reaches the shared UtilityExecutor");
        step.drain();
        step.check(completed_ == completedBefore + 1 && drains_ > drainsBefore, "Its completion and the utilityCompleted broadcast arrive in the drain");
        // A registered WM_APP message reaches message() through the owners'
        // message chain (the HUD HWND route, on the coverage clock); an
        // unregistered one is not consumed by this owner.
        const auto messagesBefore = messages_;
        step.check(step.message(message_, 7, 0) && messages_ == messagesBefore + 1, "The registered message reaches the owner's message()");
        (void)step.message(WM_APP + 0x0FFF, 0, 0);
        step.check(messages_ == messagesBefore + 1 && IsWindow(window_) && !IsWindowVisible(window_), "Other ids pass the probe; the HUD HWND stays hidden");
        // Area-to-area service directory: exact type, non-owning.
        step.check(host_.find<ProbeService>("areaProbe.service") == &service_ && !host_.find<int>("areaProbe.service") && !host_.find<ProbeService>("absent"),
                   "Published services resolve by exact type");
        ++service_.lookups;
        // The coverage root keeps what this owner wrote.
        const auto bytes = ehud::data::detail::readFile(root_ / "synthetic.txt", 64);
        step.check(bytes && *bytes == "area probe", "The coverage root holds the area's synthetic input");
        step.present(step.time + .05);
        ++steps_;
    }
    bool flush(double) override { return true; }

private:
    app::ApplicationHost& host_;
    HWND window_{};
    app::UtilityExecutor& utility_;
    app::UtilityExecutor::Route route_{};
    fs::path root_;
    std::uint32_t message_{};
    ProbeService service_;
    unsigned messages_{}, drains_{}, completed_{}, steps_{};
};
}

std::unique_ptr<app::ModuleOwner> applicationAreaProbe(const app::ApplicationModuleContext& context) {
    // Coverage-only registration: any other mode is a registration bug.
    require(context.mode == app::ApplicationAreaMode::hiddenCoverage && context.host && context.host->mode() == context.mode, "runs in hidden coverage only");
    require(context.name == "areaProbe" && context.utility && context.window && context.host->utility() == context.utility, "receives its name and the shared executor");
    require(&context.host->rasterizer() == &context.rasterizer && context.host->metrics() == context.metrics, "receives the shared rasterizer and metrics");
    require(fs::is_directory(context.coverageRoot) && fs::is_empty(context.coverageRoot) && context.coverageRoot.parent_path().filename() == "Coverage" &&
            context.coverageRoot.parent_path().parent_path() == context.dataRoot, "owns a NEW coverage root inside the temporary data root");
    // The declared fixture resolves; unknown ids resolve to nothing.
    const auto fixture = context.fixture ? context.fixture("applicationAreaProbe.fixture") : fs::path{};
    require(!fixture.empty() && context.fixture("applicationAreaProbe.absent").empty(), "resolves its declared fixture only");
    const auto bytes = ehud::data::detail::readFile(fixture, 64 * 1024);
    require(bytes && ehud::data::Json::parse(*bytes)["probe"].string() == "application area", "reads its synthetic fixture");
    // Package resources are verified paths or nothing (no --package).
    const auto shader = context.resource ? context.resource("shell.shader") : fs::path{};
    require(shader.empty() || fs::is_regular_file(shader), "package resources resolve to verified files or nothing");
    require(context.clock && context.invalidate && context.recordEvent, "receives the shared clock, invalidation and Event Log");
    return std::make_unique<AreaProbe>(context);
}
} // namespace endfield::tests
