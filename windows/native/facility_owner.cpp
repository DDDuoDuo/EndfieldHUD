// The facility alerts and daily boot log area (see the header). Mac:
// Sources/SystemHUDView.swift :1467, :1519, :1582, :2940-3097 and
// Sources/OverlayController.swift :143-147, :420, :940, :1079-1088.
#include "native/facility_owner.hpp"
#ifdef _WIN32
// The design library first: <windows.h> (also through screen_reader_peers.hpp)
// defines `small` (rpcndr.h), a design enumerator.
#include "tools/hud_design_owner.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "core/source_desktop_chrome.hpp"
#include "modules/event_log.hpp"
#include "modules/work_mode.hpp"
#include "native/event_log_owner.hpp"
#include "native/hud_design_automation.hpp"
#include "native/hud_design_scene.hpp"
#include "native/id_card_presenter.hpp"
#include "native/layer_scene.hpp"
#include "native/screen_reader_peers.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbt.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::facility {
namespace {
namespace d = modules::design;
namespace f = modules::facility;
namespace b = modules::boot;
namespace gpu = native;
using Matrix = core::Matrix4;
using Kind = modules::FacilityAlertKind;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
// Mac FacilityAlertMonitor.pollInterval, plus the closed HUD's Activity
// sample interval (ActivitySamplingPlan: 5 s) before this owner's own wake.
constexpr double tickSlack = 5, memoryInterval = 4.5;
// The banner's top below the client top (layoutFacilityLayers: max(14, safe
// area + 10); Windows has no camera housing inset).
constexpr double bannerTop = 14;
std::optional<core::Rect> projected(const core::Projection& projection, double scale, core::Rect r) {
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (const core::Point c : {core::Point{r.x, r.y}, core::Point{r.x + r.width, r.y}, core::Point{r.x, r.y + r.height}, core::Point{r.x + r.width, r.y + r.height}}) {
        const auto q = projection.project(c);
        if (!q || !std::isfinite(q->x) || !std::isfinite(q->y)) return std::nullopt;
        x0 = std::min(x0, q->x); y0 = std::min(y0, q->y); x1 = std::max(x1, q->x); y1 = std::max(y1, q->y);
    }
    return core::Rect{x0 / scale, y0 / scale, (x1 - x0) / scale, (y1 - y0) / scale};
}
// One design tree on one plane (the Mac layer and its host plane).
struct Surface {
    gpu::NativeDesignScene scene;
    Matrix world;
    core::Projection projection;
    std::uint64_t synced{};
    bool posed{}, composed{};
    Surface(gpu::LayerRasterizer& r, const gpu::LayerRasterOptions& o, std::string prefix) : scene(r, o, std::move(prefix)) {}
};
}

FacilitySources FacilitySources::windows() {
    FacilitySources s;
    s.disk = [](std::string_view startupName) { return gpu::readWindowsFacilityDisk(startupName); };
    s.memory = [] { return gpu::readWindowsFacilityMemory(); };
    s.localDate = [] { return gpu::windowsLocalDate(); };
    s.uptime = [] { return gpu::windowsUptimeSeconds(); };
    s.localDay = [](double t) { return gpu::windowsLocalDay(t); };
    s.bootDisk = [] { return gpu::readWindowsBootLogDisk(); };
    return s;
}

struct FacilityOwner::Impl {
    app::ApplicationHost& host;
    d::Context& context;
    FacilityOwnerOptions options;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::shared_ptr<std::string> startupName = std::make_shared<std::string>();
    app::UtilityExecutor::Route route{};
    modules::FacilityAlertMonitor monitor;
    f::FacilityBanner banner;
    f::FacilityTagStack tags;
    b::BootLog bootLog;
    b::BootLogStage stage;
    Surface bannerSurface, tagSurface, bootSurface;
    d::Router bannerRouter, tagRouter;
    d::Router* tracking{};
    std::unique_ptr<gpu::AutomationTree> automation;
    app::ClientMetrics metrics;
    std::vector<gpu::LayerCompositionEntry> modal;
    double time{}, lastRead{-1e300}, lastMemory{-1e300};
    std::optional<core::Language> renderedLanguage;
    bool visible{}, interactive{}, playsBootLog{}, concealed{}, updating{}, pressed{};

    Impl(app::ApplicationHost& h, d::Context& c, gpu::LayerRasterizer& r, FacilityOwnerOptions o)
        : host(h), context(c), options(std::move(o)), monitor(sources()), banner(c.env()), tags(c.env()), bootLog(c.env()),
          bannerSurface(r, options.raster, "facility.banner"), tagSurface(r, options.raster, "facility.tags"), bootSurface(r, options.raster, "facility.boot") {
        if (options.executor) route = options.executor->makeRoute();
        time = std::max(0.0, host.now());
        lastRead = lastMemory = time;
        modal.reserve(64);
        banner.onClick = [this](Kind k) { openModule(k); };
        tags.onOpen = [this](Kind k) { openModule(k); };
        tags.onDismiss = [this](Kind k) { monitor.dismiss(k); };
        banner.onHidden = [this] { changed(); };
        bootLog.onFinish = [this] { finishBootLog(); };
        monitor.onChange = [this] { if (visible) facilityChanged(); };
        // OverlayController.update: facilityAlerts.isEnabled = configuration.facilityAlerts && !isDiagnosticRun.
        monitor.setEnabled(host.settings().facilityAlerts() && !options.diagnostic);
    }
    ~Impl() {
        *alive = false;
        if (options.executor) options.executor->invalidate(route);
    }
    void clock(double t) {
        need(std::isfinite(t), "Facility owner needs finite time");
        time = std::max(time, t);
        context.env().now = std::max(context.env().now, time);
    }
    void changed() const { if (options.changed) options.changed(); }
    // SystemHUDView.allowsModuleInput: interaction enabled (the opening
    // finished, not closing).
    bool allowsInput() const { return visible && interactive; }

    // --- The monitor's sources (FacilityAlertSources.system on Windows) --------
    modules::FacilityAlertMonitor::Sources sources() {
        modules::FacilityAlertMonitor::Sources s;
        // The name of an unnamed startup disk, set on the owner thread before
        // each read (one read in flight at a time). The Mac names it in the
        // system language; this is the HUD language (equal while Language is
        // System, the default).
        s.readDisk = [disk = options.sources.disk, name = startupName]() -> std::optional<modules::FacilityDiskReading> {
            return disk ? disk(*name) : std::nullopt;
        };
        s.readMemoryPressure = [memory = options.sources.memory]() -> std::optional<modules::FacilityMemoryPressure> {
            const auto m = memory ? memory() : std::nullopt;
            return m ? modules::facilityMemoryPressure(m->used, m->total, m->lowMemory) : std::nullopt;
        };
        s.readMemoryUsage = [memory = options.sources.memory]() -> std::optional<modules::FacilityMemoryUsage> {
            const auto m = memory ? memory() : std::nullopt;
            return m ? modules::FacilityMemoryUsage::make(m->used, m->total) : std::nullopt;
        };
        // No thermal source on Windows (D4).
        s.runWorker = [this](std::function<modules::FacilityReadings()> read, std::function<void(modules::FacilityReadings)> apply) {
            *startupName = core::localized("Startup disk", "启动磁盘", context.env().language);
            if (!options.executor) { apply(read()); return; }
            auto result = std::make_shared<modules::FacilityReadings>();
            const bool accepted = options.executor->submit(route, [read = std::move(read), result] { *result = read(); },
                [alive = std::weak_ptr<bool>(alive), apply, result](std::exception_ptr error) {
                    const auto live = alive.lock();
                    if (!live || !*live) return;
                    apply(error ? modules::FacilityReadings{} : *result);
                });
            // A full queue reads nothing this time: the next cadence tick retries.
            if (!accepted) apply({});
        };
        s.clock = [this] { return time; };
        return s;
    }
    // A full read (disk + memory) on the worker.
    void readAll() {
        lastRead = lastMemory = time;
        monitor.checkNow();
    }
    // The memory pressure of the Activity cadence (owner thread).
    void readMemory() {
        lastMemory = time;
        const auto m = options.sources.memory ? options.sources.memory() : std::nullopt;
        if (!m) return;
        const auto pressure = modules::facilityMemoryPressure(m->used, m->total, m->lowMemory);
        if (!pressure) return;
        modules::FacilityReadings r;
        r.memory = modules::FacilityMemoryReading{*pressure, *pressure != modules::FacilityMemoryPressure::normal || monitor.wantsMemoryUsage()
            ? modules::FacilityMemoryUsage::make(m->used, m->total) : std::nullopt};
        monitor.apply(r);
    }

    // --- SystemHUDView glue -------------------------------------------------------
    // facilityChanged (:3014-3029), re-entrancy guarded.
    void facilityChanged() {
        if (updating) return;
        updating = true;
        struct Done { bool& v; ~Done() { v = false; } } done{updating};
        const auto& alerts = monitor.alerts();
        for (const auto kind : modules::facilityAlertKinds)
            if (std::none_of(alerts.begin(), alerts.end(), [kind](const auto& a) { return a.kind() == kind; })) banner.withdraw(kind);
        if (allowsInput() && !bootLog.isPlaying())
            for (const auto& alert : monitor.newAlerts()) { banner.show(alert); monitor.markPresented(alert.kind()); }
        tags.setActive(visible);
        tags.update(monitor.alerts(), allowsInput());
        bannerRouter.set({&banner.control()});
        tagRouter.set(tags.controls());
        renderedLanguage = context.env().language;
        changed();
    }
    // openFacilityModule (:3031-3034): opens the module even when it is hidden in System > Modules.
    void openModule(Kind kind) {
        if (!allowsInput()) return;
        host.selectModule(modules::facilityAlertModule(kind));
    }
    void conceal(bool on) {
        concealed = on;
        if (stage.concealCentre) stage.concealCentre(on);
    }
    // OverlayController.consumeDailyBootLog (:1079-1088): off, Reduce Motion
    // or a diagnostic run -> false; otherwise today's first opening consumes
    // the local day (even if the log is then skipped).
    bool consumeDailyBootLog() {
        if (options.diagnostic || !host.settings().dailyBootLog() || context.env().reduceMotion || !options.sources.localDate) return false;
        const auto today = b::bootLogDay(options.sources.localDate());
        bool played{};
        if (!host.updateSettings([&](ehud::data::Settings& s) { played = s.consumeDailyBootLog(today); })) return false;
        return played;
    }
    // hudDidBecomeInteractive (:3037-3048).
    void becameInteractive() {
        tags.setActive(true);
        if (playsBootLog && !context.env().reduceMotion) {
            playsBootLog = false;
            conceal(true);
            bootLog.play(lines(), options.version);
            bannerRouter.cancel(); tagRouter.cancel(); tracking = nullptr;
            changed();
        } else {
            if (playsBootLog) { playsBootLog = false; conceal(false); }
            facilityChanged();
        }
    }
    // finishBootLog (:3051-3057): the centre enters from below, then the checks run.
    void finishBootLog() {
        conceal(false);
        if (stage.enterCentre) stage.enterCentre(core::ShellPosition::subpageOffset(0, 1), time);
        if (monitor.isEnabled()) readAll();
        facilityChanged();
    }
    // bootLogLines (:3060-3097).
    std::vector<b::BootLogLine> lines() {
        const auto date = options.sources.localDate ? options.sources.localDate() : b::BootLogDate{};
        const auto* sources = host.find<b::BootLogSources>(b::bootLogSourcesKey);
        std::optional<b::BootLogBattery> battery;
        std::vector<std::string> events;
        std::optional<b::BootLogSanity> sanity;
        if (sources) {
            if (sources->battery) battery = sources->battery();
            if (sources->eventsToday) events = sources->eventsToday(time);
            if (sources->sanity) sanity = sources->sanity(time);
        }
        // The streak counts consecutive local days with Work Mode sessions, ending today or yesterday.
        std::vector<std::int64_t> days;
        if (const auto* log = host.find<gpu::EventLogOwner>(gpu::eventLogOwnerServiceKey); log && options.sources.localDay)
            for (const auto& event : log->snapshot().events)
                if (event.kind == modules::EventKind::workStarted || event.kind == modules::EventKind::workCompleted)
                    if (const auto day = options.sources.localDay(event.createdAt)) days.push_back(*day);
        const auto streak = b::bootLogWorkStreak(days, b::bootLogDayNumber(date.year, date.month, date.day));
        const double hours = host.workMode() ? host.workMode()->trackedWorkSeconds(time) : 0;
        std::optional<b::BootLogDisk> disk;
        if (const auto& reading = monitor.readings().disk)
            disk = b::BootLogDisk{reading->freePercent(), reading->freeFraction() < modules::FacilityAlertMonitor::diskRaiseFreeFraction};
        else if (options.sources.bootDisk) disk = options.sources.bootDisk();
        std::string name;
        if (const auto* feed = host.find<gpu::IdCardFeed>(gpu::IdCardFeed::serviceKey); feed && feed->card() && !feed->card()->captions().empty())
            name = feed->card()->captions()[0].text;
        return b::bootLogLines(date, options.sources.uptime ? options.sources.uptime() : 0, battery, events, sanity,
            b::bootLogWork(streak, hours), disk, name, context.env().text.uppercase);
    }

    // --- Planes -------------------------------------------------------------------
    void place(Surface& s, const Matrix& world, float opacity, const d::Layer& root, std::uint64_t revision, bool show) {
        if (!metrics.pixelWidth || !metrics.pixelHeight) { s.composed = false; return; }
        s.world = world;
        const auto camera = gpu::layerViewportProjection(metrics.pixelWidth, metrics.pixelHeight) * Matrix::scale(metrics.scale, metrics.scale);
        s.projection = core::Projection::viewport(camera * world, metrics.pixelWidth, metrics.pixelHeight);
        if (!show) { s.composed = false; return; }
        s.synced = revision;
        s.scene.syncContent(root, context.env().text, revision, std::clamp(context.boxScale(), 1., 4.), time);
        s.scene.updatePose({world, opacity, time, {}, {}});
        s.posed = s.composed = true;
    }
    bool bannerShows() const { return visible && banner.isShowing(); }
    bool tagsShow() const { return visible && !tags.empty(); }
    bool bootShows() const { return visible && bootLog.isPlaying(); }
    std::optional<core::Point> local(const Surface& s, core::Point logical) const {
        if (!s.posed) return std::nullopt;
        return s.projection.unproject({logical.x * metrics.scale, logical.y * metrics.scale});
    }
    d::Control* hit(core::Point logical) const {
        if (!allowsInput()) return nullptr;
        if (banner.isShowing()) if (const auto p = local(bannerSurface, logical)) if (auto* c = bannerRouter.control(*p, banner.layer())) return c;
        if (const auto p = local(tagSurface, logical)) return tagRouter.control(*p, tags.layer());
        return nullptr;
    }
    void hover(std::optional<core::Point> logical) {
        const auto at = [&](const Surface& s) { return logical ? local(s, *logical) : std::nullopt; };
        d::updateHighlights(banner.layer(), allowsInput() && banner.isShowing() ? at(bannerSurface) : std::nullopt, pressed);
        d::updateHighlights(tags.layer(), allowsInput() ? at(tagSurface) : std::nullopt, pressed);
    }
    void cancelPointer() {
        bannerRouter.cancel(); tagRouter.cancel(); tracking = nullptr; pressed = false;
        d::updateHighlights(banner.layer(), std::nullopt, false);
        d::updateHighlights(tags.layer(), std::nullopt, false);
    }
    // interactionEnabled = false (:150-153): the banner hides at once and drops its queue.
    void deactivate() {
        interactive = false;
        cancelPointer();
        banner.hide(false);
    }
    std::vector<d::Control*> automationControls() {
        std::vector<d::Control*> out;
        if (!allowsInput()) return out;
        if (banner.isShowing()) out.push_back(&banner.control());
        for (auto* c : tags.controls()) out.push_back(c);
        return out;
    }
};

FacilityOwner::FacilityOwner(app::ApplicationHost& host, d::Context& context, gpu::LayerRasterizer& raster, FacilityOwnerOptions options)
    : impl_(std::make_unique<Impl>(host, context, raster, std::move(options))) {
    host.publish<b::BootLogStage>(b::bootLogStageKey, impl_->stage);
}
FacilityOwner::~FacilityOwner() {
    impl_->automation.reset();
    impl_->host.withdraw(b::bootLogStageKey);
}
b::BootLogStage& FacilityOwner::stage() noexcept { return impl_->stage; }
const modules::FacilityAlertMonitor& FacilityOwner::monitor() const noexcept { return impl_->monitor; }
const f::FacilityBanner& FacilityOwner::banner() const noexcept { return impl_->banner; }
const f::FacilityTagStack& FacilityOwner::tags() const noexcept { return impl_->tags; }
const b::BootLog& FacilityOwner::bootLog() const noexcept { return impl_->bootLog; }
bool FacilityOwner::playsBootLogOnOpen() const noexcept { return impl_->playsBootLog; }

// The boot log takes every press and key while it plays (SHELL-3, SHELL-4:
// before every other branch, the confirm bar's included); the banner and tag
// routers come before the module chains.
app::ModuleRouting FacilityOwner::routing() const noexcept {
    app::ModuleRouting r;
    r.pointerCapture = 0; r.keyCapture = 0; r.pointer = 0;
    r.message = 90; // WM_DEVICECHANGE volume events (never consumed)
    return r;
}
void FacilityOwner::resize(const app::ClientMetrics& m) {
    need(m.pixelWidth && m.pixelHeight && std::isfinite(m.scale) && m.scale > 0, "Invalid facility viewport");
    impl_->metrics = m;
}
// Opening (the Mac builds a new SystemHUDView): the layers install
// (facilityChanged: tags at once, no banner yet), then today's boot log is
// decided before the entrance (OverlayController.swift:940).
void FacilityOwner::setOverlayVisible(bool v, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!v) { overlayClosing(t); return; }
    if (i.visible) return;
    i.visible = true; i.interactive = false;
    i.facilityChanged();
    i.playsBootLog = i.consumeDailyBootLog();
    if (i.stage.deferEntrance) i.stage.deferEntrance(i.playsBootLog);
    if (i.playsBootLog) i.conceal(true);
}
// Closing: interaction off (the banner hides at once); a log still playing
// stops with the view. The centre stays concealed until the close finished.
void FacilityOwner::overlayClosing(double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.visible) return;
    i.deactivate();
    i.bootLog.stop();
    i.playsBootLog = false;
    i.visible = false;
    i.tags.setActive(false);
    i.changed();
}
void FacilityOwner::update(const app::ModuleFrame& frame) {
    auto& i = *impl_;
    i.clock(frame.time);
    // The opening finished: interactionEnabled -> hudDidBecomeInteractive.
    const bool interactive = i.visible && i.host.overlayPhase() == core::SystemOverlayPhase::open;
    if (interactive && !i.interactive) { i.interactive = true; i.becameInteractive(); }
    else if (!interactive && i.interactive) i.deactivate();
    // A language change re-renders the banner and the tags (SHELL-18).
    if (i.visible && i.renderedLanguage && *i.renderedLanguage != i.context.env().language) i.facilityChanged();
    const double unit = d::geometry::baseUnit;
    // Banner: flat in shell space (layoutFacilityLayers): anchor (.5, 0) at (500, top).
    const auto layout = core::source::DesktopChromeLayout::make(frame.chrome, frame.center, Matrix{});
    const double y = (bannerTop - layout.designOrigin.y) / std::max(.01, layout.designScale);
    const auto bannerWorld = layout.designToScreen * Matrix::translation(500, y) * Matrix::scale(unit, unit) * Matrix::translation(-f::FacilityBanner::size.width / 2, 0);
    // Tags: on the core plane, anchor (1, 0) at (header.minX - 8, header.minY + 50).
    const auto tagWorld = frame.center * Matrix::translation(layout.header.x - 8, layout.header.y + 50) * Matrix::scale(unit, unit) * Matrix::translation(-f::FacilityTagStack::width, 0);
    // Boot log: the overlay's sublayer at (10, 30) on the content box.
    const auto bootWorld = core::source::designBoxWorld(frame.center) * Matrix::translation(10, 30);
    i.place(i.bannerSurface, bannerWorld, frame.opacity, i.banner.layer(), i.banner.contentRevision(), i.bannerShows());
    i.place(i.tagSurface, tagWorld, frame.opacity, i.tags.layer(), i.tags.contentRevision(), i.tagsShow());
    i.place(i.bootSurface, bootWorld, frame.opacity, i.bootLog.layer(), i.bootLog.contentRevision(), i.bootShows());
}
bool FacilityOwner::requiresFrames(double t) const {
    const auto& i = *impl_;
    if (!i.visible) return false;
    const auto now = std::max(t, i.time);
    const auto wants = [&](const Surface& s, bool show, std::uint64_t revision) {
        if (show != s.composed || (show && s.synced != revision)) return true;
        return s.composed && s.scene.requiresFrames(now);
    };
    return wants(i.bannerSurface, i.bannerShows(), i.banner.contentRevision()) || wants(i.tagSurface, i.tagsShow(), i.tags.contentRevision())
        || wants(i.bootSurface, i.bootShows(), i.bootLog.contentRevision());
}
// The fallback read when no executor drain came for a minute (the banner,
// tag and boot-log timers are the shared Context's deadlines).
std::optional<double> FacilityOwner::nextWakeTime(double) const {
    const auto& i = *impl_;
    if (!i.monitor.isEnabled()) return std::nullopt;
    return i.lastRead + modules::FacilityAlertMonitor::pollInterval + tickSlack;
}
bool FacilityOwner::deadline(double t) {
    auto& i = *impl_;
    i.clock(t);
    if (i.monitor.isEnabled() && i.time >= i.lastRead + modules::FacilityAlertMonitor::pollInterval + tickSlack) i.readAll();
    return false;
}
void FacilityOwner::upload(gpu::Renderer& r) {
    auto& i = *impl_;
    for (auto* s : {&i.bannerSurface, &i.tagSurface, &i.bootSurface}) if (s->composed) s->scene.uploadAnimations(r);
}
std::span<const gpu::LayerCompositionEntry> FacilityOwner::planeEntries() {
    auto& i = *impl_;
    if (!i.tagSurface.composed) return {};
    return i.tagSurface.scene.entries();
}
std::span<const gpu::LayerCompositionEntry> FacilityOwner::modalEntries() {
    auto& i = *impl_;
    i.modal.clear();
    // The boot log (in the overlay, core plane) below the banner (canvas z 1000).
    for (auto* s : {&i.bootSurface, &i.bannerSurface})
        if (s->composed) for (const auto& e : s->scene.entries()) i.modal.push_back(e);
    return i.modal;
}
void FacilityOwner::collected(gpu::Renderer& r) {
    auto& i = *impl_;
    for (auto* s : {&i.bannerSurface, &i.tagSurface, &i.bootSurface}) {
        need(s->scene.collectRetired(r), "Facility artwork still published");
        // Nothing shown keeps no artwork (alerts and the log are occasional).
        if (!s->composed && s->scene.park(r)) s->posed = false;
    }
}
void FacilityOwner::release(gpu::Renderer& r) {
    auto& i = *impl_;
    for (auto* s : {&i.bannerSurface, &i.tagSurface, &i.bootSurface}) need(s->scene.releaseResources(r), "Facility artwork still published at teardown");
}
bool FacilityOwner::covers(core::Point p) const {
    const auto& i = *impl_;
    if (i.bootShows()) return true;
    return i.hit(p) != nullptr;
}
bool FacilityOwner::capturesPointer() const { return impl_->bootShows(); }
bool FacilityOwner::capturesKeys() const { return impl_->bootShows(); }
bool FacilityOwner::suppressesKeyFilters() const { return impl_->bootShows(); }
// mouseDown (:1511-1524): the boot log skips on any press; then the banner
// router, then the tag router (with their own hover pass, :1441-1449).
bool FacilityOwner::pointer(const app::PointerEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    const bool left = e.button == app::PointerButton::left;
    const bool press = (e.kind == app::PointerKind::down || e.kind == app::PointerKind::doubleClick) && left;
    if (i.bootShows()) {
        if (press) i.bootLog.skip();
        return press || (e.kind == app::PointerKind::up && left);
    }
    if (!i.allowsInput()) { i.cancelPointer(); return false; }
    const core::Point p{e.x, e.y};
    switch (e.kind) {
    case app::PointerKind::move:
        if (i.tracking) {
            const auto& surface = i.tracking == &i.bannerRouter ? i.bannerSurface : i.tagSurface;
            const auto& root = i.tracking == &i.bannerRouter ? i.banner.layer() : i.tags.layer();
            if (const auto q = i.local(surface, p)) i.tracking->pointerDragged(*q, root);
        }
        i.hover(p);
        return i.tracking != nullptr;
    case app::PointerKind::down: case app::PointerKind::doubleClick: {
        if (!left) return false;
        if (i.banner.isShowing()) if (const auto q = i.local(i.bannerSurface, p); q && i.bannerRouter.pointerDown(*q, i.banner.layer())) {
            i.tracking = &i.bannerRouter; i.pressed = true; i.hover(p); return true;
        }
        if (const auto q = i.local(i.tagSurface, p); q && i.tagRouter.pointerDown(*q, i.tags.layer())) {
            i.tracking = &i.tagRouter; i.pressed = true; i.hover(p); return true;
        }
        return false;
    }
    case app::PointerKind::up: {
        if (!left || !i.tracking) return false;
        auto* router = i.tracking;
        const auto& surface = router == &i.bannerRouter ? i.bannerSurface : i.tagSurface;
        const auto& root = router == &i.bannerRouter ? i.banner.layer() : i.tags.layer();
        i.tracking = nullptr; i.pressed = false;
        router->pointerUp(i.local(surface, p).value_or(core::Point{-1e6, -1e6}), root);
        if (i.allowsInput()) i.hover(p);
        return true;
    }
    case app::PointerKind::leave: case app::PointerKind::captureLost:
        i.cancelPointer();
        return false;
    }
    return false;
}
// keyDown (:1581-1582): any key skips the playing log (repeats included).
bool FacilityOwner::key(const app::KeyEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.bootShows()) return false;
    if (e.kind == app::KeyKind::down) i.bootLog.skip();
    return true;
}
// NSWorkspace didMount / didUnmount -> FacilityAlertEvent.volumesChanged: a
// volume arrived or was removed; the disk is read at once. Not consumed.
bool FacilityOwner::message(const app::NativeMessage& m, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (m.message != WM_DEVICECHANGE || (m.wParam != DBT_DEVICEARRIVAL && m.wParam != DBT_DEVICEREMOVECOMPLETE) || !m.lParam) return false;
    if (reinterpret_cast<const DEV_BROADCAST_HDR*>(m.lParam)->dbch_devicetype != DBT_DEVTYP_VOLUME) return false;
    if (i.monitor.isEnabled()) i.readAll();
    return false;
}
void FacilityOwner::cancelInteraction(double t) { impl_->clock(t); impl_->cancelPointer(); }
bool FacilityOwner::hostEvent(const app::HostEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    using K = app::HostEvent::Kind;
    switch (e.kind) {
    case K::settingsChanged: {
        const bool enabled = i.host.settings().facilityAlerts() && !i.options.diagnostic;
        if (enabled == i.monitor.isEnabled()) return false;
        if (enabled) i.lastRead = i.lastMemory = i.time; // start() samples at once
        i.monitor.setEnabled(enabled);
        if (i.visible) i.facilityChanged();
        return i.visible;
    }
    case K::resumed:      // NSWorkspace.didWakeNotification
        if (i.monitor.isEnabled()) i.readAll();
        return false;
    case K::utilityCompleted:
        // The Activity system samples' cadence (and any other drain).
        if (!i.monitor.isEnabled()) return false;
        if (i.time >= i.lastRead + modules::FacilityAlertMonitor::pollInterval) i.readAll();
        else if (i.time >= i.lastMemory + memoryInterval && !i.monitor.sampling()) i.readMemory();
        return false;
    case K::overlayConcealed:
        // The close finished: the next opening decides its own centre.
        if (i.concealed) i.conceal(false);
        return false;
    default:
        return false;
    }
}
bool FacilityOwner::automationRefresh(IRawElementProviderFragmentRoot* root) {
    auto& i = *impl_;
    if (!i.automation) {
        gpu::AutomationTreeHost host;
        host.root = root;
        // Stand-ins for the banner and the tags (SHELL-8, SHELL-21): space 0 the banner, 1 the tags.
        host.nodes = [&i] {
            std::vector<d::AccessibilityItem> banner, tags;
            if (i.allowsInput() && i.banner.isShowing()) i.bannerRouter.accessibilityItems(i.banner.layer(), banner);
            if (i.allowsInput()) i.tagRouter.accessibilityItems(i.tags.layer(), tags);
            auto nodes = gpu::automationNodes(banner);
            for (auto& node : gpu::automationNodes(tags)) { node.space = 1; nodes.push_back(std::move(node)); }
            return nodes;
        };
        host.toScreen = [&i](core::Rect r, std::uint8_t space) -> std::optional<core::Rect> {
            const auto& s = space ? i.tagSurface : i.bannerSurface;
            if (!s.posed) return std::nullopt;
            return gpu::screenReaderClientToScreen(i.options.window, i.metrics.scale, projected(s.projection, i.metrics.scale, r));
        };
        // A press exactly as its click (HUDAccessibilityStandIns.onPerform -> activate).
        host.invoke = [&i](std::string_view id) {
            if (!i.allowsInput()) return false;
            for (auto* c : i.automationControls())
                if (c->id() == id && c->enabled()) { c->activate(); return true; }
            return false;
        };
        host.changed = [&i] { i.changed(); };
        i.automation = std::make_unique<gpu::AutomationTree>(std::move(host));
    }
    return i.automation->refresh();
}
std::size_t FacilityOwner::automationCount() const { return impl_->automation ? impl_->automation->size() : 0; }
long FacilityOwner::automationChild(std::size_t n, IRawElementProviderFragment** out) const {
    if (!impl_->automation) { if (out) *out = nullptr; return 0x80070057L; } // E_INVALIDARG
    return impl_->automation->child(n, out);
}
long FacilityOwner::automationFromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!impl_->automation) { if (out) *out = nullptr; return 0; }
    return impl_->automation->fromPoint(x, y, out);
}
void FacilityOwner::automationDisconnect() noexcept { if (impl_->automation) impl_->automation->disconnect(); }

std::unique_ptr<app::ModuleOwner> facilityModule(const app::ApplicationModuleContext& ctx) {
    auto* host = ctx.host;
    auto* context = host ? host->designContext() : nullptr;
    if (!context) return nullptr;
    tools::DesignOwnerOptions design;
    if (!host->designOwnerOptions(design, std::string(FacilityOwner::areaName), {})) return nullptr;
    FacilityOwnerOptions options;
    options.raster = design.raster;
    options.window = ctx.window;
    options.changed = design.changed;
    options.executor = ctx.utility;
#ifdef EHUD_FACILITY_VERSION
    options.version = EHUD_FACILITY_VERSION;
#endif
    // OverlayController.isDiagnosticRun: verification runs never read live
    // facility state or write the boot-log day; the section 9 gate's
    // isolated --data-root runs count as diagnostic runs too.
    options.diagnostic = ctx.mode != app::ApplicationAreaMode::production || host->isolatedDataRoot();
    if (ctx.mode == app::ApplicationAreaMode::production) options.sources = FacilitySources::windows();
    else {
        // Nominal synthetic readings (diagnostic runs keep the monitor off anyway).
        options.sources.disk = [](std::string_view) { return modules::FacilityDiskReading::make("Local Disk", 1'000'000'000'000, 500'000'000'000); };
        options.sources.memory = [] { return std::optional(native::WindowsFacilityMemory{8'000'000'000, 16'000'000'000, false}); };
        options.sources.localDate = [] { return b::BootLogDate{2026, 1, 1, 9, 0}; };
        options.sources.uptime = [] { return 3600.0; };
    }
    return std::make_unique<FacilityOwner>(*host, *context, ctx.rasterizer, std::move(options));
}
} // namespace endfield::facility
#endif
