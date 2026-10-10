#include "app/areas/power_module.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "modules/power_policy.hpp"
#include "native/layer_scene.hpp"
#include "native/system_services.hpp"
#include "tools/charge_badge_preview.hpp"
#include "tools/power_owner.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <utility>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::power {
namespace {
namespace m = modules;
namespace gpu = native;
using Json = ehud::data::Json;
using Mode = app::ApplicationAreaMode;
using Kind = app::HostEvent::Kind;

// The alert animates while the HUD is concealed, when the shared frame clock
// is stopped: its finite sequences (about 1.5 s in, 0.64 s out) wake the one
// shared waitable timer at the display cadence, plus one settle wake that
// presents the exact final pose. Idle alerts wake only at their deadlines.
constexpr double alertFrameInterval = 1.0 / 60;
// SystemHUDView: HUDChargeBadge.center + chargeSourceOffset (source shell).
constexpr core::Point badgeDesignCenter{m::ChargeBadgeState::center.x, m::ChargeBadgeState::center.y + tools::ChargeBadgePreview::sourceOffset};
// Hidden coverage: one synthetic 1440 x 900 work area at 1 pixel per point.
constexpr std::array<m::ChargeWorkArea, 1> coverageScreens{m::ChargeWorkArea{11, {0, 0, 1440, 900}, 1}};

core::Language resolvedLanguage(core::Language language) noexcept {
    return language == core::Language::system ? core::Language::english : language;
}
// Synthetic provider for hidden coverage and development previews without the
// shared SystemServices. Never a real device and never used in production.
gpu::BatterySnapshot syntheticSnapshot(unsigned percent, bool ac, bool charging) noexcept {
    gpu::BatterySnapshot s;
    s.available = true;
    s.present = true;
    s.percent = percent;
    s.ac_connected = ac;
    s.charging = charging;
    s.fully_charged = ac && !charging && percent == 100;
    return s;
}
gpu::BatteryDeviceRead syntheticDevices() {
    gpu::BatteryDeviceRead read;
    gpu::BatteryDeviceReport report;
    report.capabilities = gpu::BatteryDeviceReport::systemBattery;
    report.fullChargedCapacity = 52'000;
    report.remainingCapacity = 32'760;
    read.reports.push_back(report);
    return read;
}
// The host broadcasts the first suspension reason and the end of the last
// one; the coordinator receives that one logical suspension.
m::PowerSuspension suspensionReason(std::uint32_t flags) noexcept {
    if (flags & app::HostEvent::suspendSystem) return m::PowerSuspension::system;
    if (flags & app::HostEvent::suspendDisplay) return m::PowerSuspension::display;
    return m::PowerSuspension::session;
}
std::uint64_t splitmix(std::uint64_t& state) noexcept {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
} // namespace

class PowerArea final : public app::ModuleOwner, public PowerAreaService {
public:
    PowerArea(const app::ApplicationModuleContext& context, std::filesystem::path shader)
        : host_(*context.host), mode_(context.mode), window_(static_cast<HWND>(context.window)), metrics_(context.metrics),
          shaderAvailable_(!shader.empty()) {
        // Battery source: the ONE shared SystemServices provider in production
        // (none when it could not start: no reading is ever invented there);
        // synthetic readings in hidden coverage, and in development previews
        // that run without the shared provider.
        source_ = mode_ != Mode::hiddenCoverage && host_.systemServices() ? Source::shared
                : mode_ == Mode::production                           ? Source::none
                                                                      : Source::synthetic;
        // HUDDeploymentFlicker draws UInt64.random per deployment; hidden
        // coverage keeps a deterministic sequence.
        seed_ = mode_ == Mode::hiddenCoverage ? 0x5eedc4a26eull : (std::uint64_t(std::random_device{}()) << 32) ^ std::random_device{}();
        badge_ = std::make_unique<tools::ChargeBadgePreview>(context.rasterizer, [this] { host_.selectModule(core::Module::power); });
        if (validMetrics(metrics_)) badge_->resize(metrics_);
        tools::PowerOwnerCallbacks callbacks;
        callbacks.recordEvent = [this](m::EventKind kind, const m::EventMetadata& metadata) { recordEvent(kind, metadata); };
        callbacks.readingChanged = [this](const m::BatteryReading& reading) { readingChanged(reading); };
        callbacks.savePosition = [this](const m::ChargePosition& position) { savePosition(position); };
        callbacks.positionEditFinished = [this] { positionEditFinished(); };
        gpu::PowerService::Reader reader; // shared provider: the battery-class devices
        if (source_ != Source::shared) reader = [] { return syntheticDevices(); };
        power_ = std::make_unique<tools::PowerOwner>(*context.utility, context.rasterizer,
            gpu::ChargePanelOptions{std::move(shader), mode_ == Mode::hiddenCoverage}, std::move(callbacks), std::move(reader));
        if (mode_ == Mode::hiddenCoverage) power_->panel().setScreens(coverageScreens, 0);
        if (source_ == Source::synthetic) synthetic_ = syntheticSnapshot(63, false, false);
        noticeMessage_ = host_.registerMessage();
        appearance_ = host_.appearance();
        const double now = host_.now();
        time_ = now;
        applySettings(now);
        // AppDelegate startup: the first reading is a baseline (always mode
        // presents persistently); the HUD is not open yet.
        (void)observe(now);
        host_.publish<PowerAreaService>(PowerAreaService::key, *this);
    }
    ~PowerArea() override {
        host_.withdraw(PowerAreaService::key);
        // Stops the provider route and closes the alert window before the
        // shared executor, the SystemServices and the HUD HWND go away.
        power_.reset();
    }
    PowerArea(const PowerArea&) = delete;
    PowerArea& operator=(const PowerArea&) = delete;

    // --- ModuleOwner ----------------------------------------------------------
    std::string_view name() const noexcept override { return "powerCharge"; }
    app::ModuleRouting routing() const noexcept override {
        app::ModuleRouting r;
        // SystemHUDView.mouseDown routes the charge badge after the Notes
        // workspace and ahead of every center module interaction: equal
        // priority with Notes (registered first) and before modules (2...19).
        r.pointer = 1;
        r.pointerPolicy = {false, true, false};
        // Private notices plus the shared provider's coalesced service message
        // (observed after SystemServices refreshed it; never consumed).
        r.message = 20;
        return r;
    }
    void resize(const app::ClientMetrics& metrics) override {
        metrics_ = metrics;
        if (validMetrics(metrics)) badge_->resize(metrics);
    }
    void setAppearance(const app::ModuleAppearance& a, double time) override {
        clock(time);
        appearance_ = a;
        (void)applySettings(time_);
    }
    void setOverlayVisible(bool visible, double time) override {
        if (!visible) return; // closing arrives through overlayClosing/overlayConcealed
        clock(time);
        // A reopen after an accepted quit means a document failure cancelled it.
        terminating_ = false;
        pendingEdit_ = closeRequested_ = false;
        hudVisible_ = true;
        // OverlayController.toggleSystemOverlay: the alert yields to the HUD.
        power_->systemOverlayOpening(time_);
        alertChanged();
        // SystemHUDView.animateSourceEntrance (Reduce Motion: showStable).
        if (appearance_.reduceMotion) badge_->setStable(true, time_);
        else badge_->animateEntrance(time_, splitmix(seed_));
    }
    void overlayClosing(double time) override {
        clock(time);
        cancelInteraction(time_);
        // animateSourceExit retracts the badge with the shell (0.64 s).
        if (appearance_.reduceMotion) badge_->setStable(false, time_);
        else badge_->animateExit(time_, splitmix(seed_));
    }
    void update(const app::ModuleFrame& frame) override {
        clock(frame.time);
        lastCenter_ = frame.center;
        (void)badge_->update(frame.center, frame.opacity, time_);
    }
    bool requiresFrames(double time) const override { return badge_->requiresFrames(time); }
    std::optional<double> nextWakeTime(double now) const override {
        std::optional<double> next;
        const auto include = [&](std::optional<double> value) { if (value && (!next || *value < *next)) next = value; };
        if (hudVisible_) include(badge_->nextWakeTime());
        if (alertCadence_) include(std::max(now, time_) + alertFrameInterval);
        include(power_->nextWakeTime());
        return next;
    }
    bool deadline(double time) override {
        clock(time);
        alertChanged();
        std::optional<double> due;
        if (hudVisible_) due = badge_->nextWakeTime();
        return due && *due <= time_ + 1e-6; // the badge advances on the next HUD frame
    }
    void upload(gpu::Renderer& renderer) override { ++uploads_; badge_->upload(renderer); }
    // The charge plane: above the center modules and the shell (see
    // host_requests for the slot below the floating Notes workspace).
    std::span<const gpu::LayerCompositionEntry> floatingEntries() override { return badge_->entries(); }
    void release(gpu::Renderer& renderer) override { badge_->release(renderer); }
    bool covers(core::Point p) const override { return hudVisible_ && badge_->covers(p); }
    bool pointer(const app::PointerEvent& e, double time) override {
        clock(time);
        if (!hudVisible_) return false;
        return badge_->pointer(e, time_);
    }
    void cancelInteraction(double time) override {
        clock(time);
        (void)badge_->pointer({app::PointerKind::leave, app::PointerButton::none, 0, 0, 0}, time_);
    }
    bool message(const app::NativeMessage& notice, double time) override {
        clock(time);
        if (notice.message == noticeMessage_) {
            noticePosted_ = false;
            runNotice();
            return true;
        }
        if (observe(time_)) reschedule();
        return false;
    }
    bool hostEvent(const app::HostEvent& e, double time) override {
        clock(time);
        bool changed{};
        switch (e.kind) {
        case Kind::utilityCompleted:
            // The battery-class pair completed inside the drain.
            changed = std::exchange(readingApplied_, false);
            break;
        case Kind::overlayConcealed:
            hudVisible_ = false;
            badge_->setStable(false, time_);
            // OverlayController.tearDownSystemPresentation: onSystemClosed
            // (presentLatestSnapshot) unless quitting, then afterSystemClose.
            if (!terminating_) power_->systemOverlayClosed(time_);
            if (pendingEdit_) beginEdit();
            changed = true;
            break;
        case Kind::suspended:
            if (!suspended_) {
                suspended_ = true;
                suspension_ = suspensionReason(e.flags);
                pendingEdit_ = closeRequested_ = openDisplayPending_ = false;
                power_->suspend(suspension_, time_);
            }
            changed = true;
            break;
        case Kind::resumed:
            if (suspended_) {
                suspended_ = false;
                // The host refreshed the shared provider before this broadcast:
                // read while still suspended, then present exactly once.
                const auto fresh = currentSnapshot();
                if (fresh) last_ = fresh;
                power_->resume(suspension_, fresh, time_);
            }
            changed = true;
            break;
        case Kind::displaysChanged:
        case Kind::systemSettingsChanged:
            // Monitor topology, DPI or work area (the alert window also
            // follows them itself once it exists).
            power_->panel().refreshScreens();
            changed = true;
            break;
        case Kind::settingsChanged:
            changed = applySettings(time_);
            break;
        case Kind::quitting:
            // AppDelegate.onQuitAccepted: terminating; no alert, edit or
            // reopen until the owner ends (or a failed save reopens the HUD).
            terminating_ = true;
            pendingEdit_ = closeRequested_ = openDisplayPending_ = false;
            power_->systemOverlayOpening(time_);
            changed = true;
            break;
        default:
            break;
        }
        changed = observe(time_) || changed;
        alertChanged();
        return changed;
    }
    void coverage(app::ModuleCoverage&) override;

    // --- PowerAreaService -------------------------------------------------------
    void preview(double time) override {
        clock(time);
        power_->preview(time_);
        alertChanged();
        reschedule();
    }
    bool editPosition(double time) override {
        clock(time);
        if (terminating_ || suspended_) return false;
        pendingEdit_ = true;
        closeRequested_ = false;
        reschedule();
        return true;
    }
    bool editingPosition() const override { return power_->panel().state().editingPosition(); }
    void setProjectionActive(bool active, double time) override {
        clock(time);
        if (active) {
            pendingEdit_ = closeRequested_ = false;
            power_->panel().state().discardPosition(time_);
            power_->panel().state().hide(false, time_);
        }
        power_->setProjectionActive(active, time_);
        alertChanged();
        reschedule();
    }
    const m::BatteryReading& reading() const override { return power_->reading(); }
    std::string trayStatus(core::Language language) const override { return power_->trayStatus(resolvedLanguage(language)); }
    std::string trayTooltip(core::Language language) const override { return power_->trayTooltip(resolvedLanguage(language)); }
    std::string previewMenuTitle(core::Language language) const override { return m::powerPreviewMenuTitle(resolvedLanguage(language)); }
    std::string powerAccessibilityLabel() const override { return badge_->accessibilityLabel(); }
    bool telemetryDemand() const override { return power_->telemetryDemand() || badge_->telemetryDemand(); }
    void setTelemetry(const std::optional<m::ChargeTelemetry>& telemetry, double time) override {
        clock(time);
        if (telemetry == telemetry_) return;
        telemetry_ = telemetry;
        power_->setTelemetry(telemetry_);
        badgeContent();
        alertChanged();
        reschedule();
        if (hudVisible_) host_.requestFrame();
    }
    void setEditingObserver(std::function<void(bool)> observer) override {
        editingObserver_ = std::move(observer);
        if (editingObserver_) editingObserver_(editing_);
    }

private:
    enum class Source : std::uint8_t { shared, synthetic, none };
    struct Applied {
        m::ChargeAlertSettings settings;
        bool dark{};
        core::Language language{};
        bool reduceMotion{};
        bool operator==(const Applied&) const = default;
    };
    app::ApplicationHost& host_;
    Mode mode_;
    HWND window_{};
    app::ClientMetrics metrics_;
    bool shaderAvailable_{};
    Source source_{Source::none};
    std::unique_ptr<tools::ChargeBadgePreview> badge_;
    std::unique_ptr<tools::PowerOwner> power_;
    app::ModuleAppearance appearance_;
    std::optional<Applied> applied_;
    std::optional<gpu::BatterySnapshot> last_, synthetic_;
    std::optional<m::ChargeTelemetry> telemetry_;
    std::optional<core::Matrix4> lastCenter_;
    std::function<void(bool)> editingObserver_;
    std::uint64_t seed_{};
    std::uint32_t noticeMessage_{};
    double time_{};
    m::PowerSuspension suspension_{m::PowerSuspension::system};
    bool hudVisible_{}, terminating_{}, suspended_{};
    bool pendingEdit_{}, closeRequested_{}, openDisplayPending_{}, noticePosted_{}, editing_{};
    bool alertAnimating_{}, alertCadence_{}, readingApplied_{};
    unsigned uploads_{}, recordedEvents_{};

    static bool validMetrics(const app::ClientMetrics& metrics) noexcept {
        return metrics.pixelWidth && metrics.pixelHeight && std::isfinite(metrics.scale) && metrics.scale > 0;
    }
    void clock(double time) {
        if (!std::isfinite(time)) throw std::invalid_argument("Invalid Power area time");
        time_ = std::max(time_, time);
    }
    std::optional<gpu::BatterySnapshot> currentSnapshot() const {
        if (source_ == Source::shared) {
            if (const auto* services = host_.systemServices()) return services->battery();
            return std::nullopt;
        }
        return source_ == Source::synthetic ? synthetic_ : std::nullopt;
    }
    // One provider change (ServiceChange::battery): identical snapshots are
    // ignored, so routing every notice through here costs one comparison.
    bool observe(double time) {
        const auto snapshot = currentSnapshot();
        if (!snapshot || (last_ && *last_ == *snapshot)) return false;
        last_ = snapshot;
        power_->receive(*snapshot, time);
        alertChanged();
        return true;
    }
    // Presents the alert at once after any state change, then keeps the
    // shared timer at the display cadence while it animates (plus one settle).
    void alertChanged() {
        power_->advance(time_);
        const bool animating = power_->requiresFrames(time_);
        alertCadence_ = animating || alertAnimating_;
        alertAnimating_ = animating;
        editingChanged();
    }
    void editingChanged() {
        const bool editing = power_->panel().state().editingPosition();
        if (editing == editing_) return;
        editing_ = editing;
        if (editingObserver_) editingObserver_(editing_);
    }
    // A change outside a host refresh: one coalesced private notice makes the
    // host re-read nextWakeTime (and runs deferred host actions). Hidden
    // coverage never posts to the HUD HWND; its steps drive the clock.
    void reschedule() {
        if (mode_ == Mode::hiddenCoverage || noticePosted_ || !window_) return;
        noticePosted_ = PostMessageW(window_, noticeMessage_, 0, 0) != FALSE;
    }
    void runNotice() {
        if (pendingEdit_ && !terminating_ && !suspended_) {
            // onEditBatteryPosition: afterSystemClose = editPosition; close.
            if (!host_.overlayVisible()) beginEdit();
            else if (!closeRequested_) { closeRequested_ = true; host_.closeOverlay(); }
        }
        if (std::exchange(openDisplayPending_, false) && !terminating_ && !suspended_ && !host_.overlayVisible())
            host_.openModule(core::Module::display); // onPositionEditFinished: openSettingsModule(.display)
        alertChanged();
    }
    void beginEdit() {
        pendingEdit_ = closeRequested_ = false;
        if (terminating_ || suspended_) return;
        (void)power_->editPosition(time_);
        alertChanged();
    }
    bool applySettings(double time) {
        const auto& fields = host_.settings().fields;
        Applied next{m::ChargeAlertSettings::fromSettings(fields), appearance_.dark, resolvedLanguage(appearance_.language), appearance_.reduceMotion};
        if (applied_ && *applied_ == next) return false;
        applied_ = std::move(next);
        // ModuleAppearance.dark is the resolved theme, so it is also the
        // system appearance whenever the theme is System.
        power_->setSettings(fields, applied_->dark, applied_->language, applied_->reduceMotion, time);
        badge_->setReduceMotion(applied_->reduceMotion, time);
        badgeContent();
        alertChanged();
        return true;
    }
    void badgeContent() {
        if (!applied_) return;
        const auto& s = applied_->settings;
        const bool dark = s.theme == m::ChargeTheme::dark || (s.theme == m::ChargeTheme::system && applied_->dark);
        badge_->setContent(power_->reading(), {dark, s.accent(), applied_->language}, s.metric, telemetry_);
    }
    void readingChanged(const m::BatteryReading& reading) {
        readingApplied_ = true;
        badgeContent();
        if (const auto* sink = host_.find<PowerPageSink>(powerPageSinkKey); sink && *sink && (*sink)(reading)) host_.requestFrame();
        if (hudVisible_) host_.requestFrame();
    }
    void recordEvent(m::EventKind, const m::EventMetadata&) {
        // The host's ApplicationEventRecorder already records the same
        // SystemEventRecorder battery section from the same provider; the
        // area counts its transitions (coverage) instead of duplicating them.
        ++recordedEvents_;
    }
    void savePosition(const m::ChargePosition& position) {
        // onPositionEditFinished: placement = custom + customPosition through
        // the committed preference record (normalized, persisted, applied).
        (void)host_.updateSettings([&](ehud::data::Settings& s) {
            s.set("placement", Json("custom"));
            s.set("customScreenID", position.screenID ? Json(static_cast<std::int64_t>(*position.screenID)) : Json(nullptr));
            s.set("customPositionX", Json(position.x));
            s.set("customPositionY", Json(position.y));
        });
    }
    void positionEditFinished() {
        // Runs inside the editor window's input: report the editor's end and
        // defer the HUD reopen out of that window procedure.
        editingChanged();
        if (suspended_ || terminating_) return;
        openDisplayPending_ = true;
        reschedule();
    }
    std::optional<core::Point> badgeClientPoint(core::Point design) const {
        if (!lastCenter_ || !validMetrics(metrics_)) return std::nullopt;
        const auto camera = gpu::layerViewportProjection(metrics_.pixelWidth, metrics_.pixelHeight) * core::Matrix4::scale(metrics_.scale, metrics_.scale);
        const auto q = core::Projection::viewport(camera * *lastCenter_, metrics_.pixelWidth, metrics_.pixelHeight).project(design);
        if (!q) return std::nullopt;
        return core::Point{q->x / metrics_.scale, q->y / metrics_.scale};
    }
};

// Hidden coverage (module-less step, identical in every cycle so the shared
// rasterizer's usage repeats): the badge through the owner's pointer route on
// the real core plane, the synthetic provider through the shared message
// chain and the UtilityExecutor, then the alert on a hidden WARP window while
// the area's overlay events model a concealed HUD, Settings round trips
// through the committed preference record, position editing, sleep/wake and
// the restoration of every input before the next cycle.
void PowerArea::coverage(app::ModuleCoverage& step) {
    if (step.module) return; // presents no HUD module (Power belongs to the built-in page)
    const auto& check = step.check;
    double t = std::max(step.time, time_);
    const auto present = [&](double at) { t = std::max(t, at); step.present(t); };
    const auto hudFrames = [&](double seconds, unsigned count) {
        const double start = t;
        for (unsigned n = 1; n <= count; ++n) present(start + seconds * n / count);
    };
    // The alert's own clock while the HUD is concealed: the shared timer
    // wakes deadline() at nextWakeTime() (display cadence or a deadline).
    const auto alertUntil = [&](double end) {
        for (unsigned guard = 0; guard < 4096; ++guard) {
            const auto wake = nextWakeTime(t);
            if (!wake || *wake > end) break;
            t = std::max(t, *wake);
            (void)deadline(t);
        }
        t = std::max(t, end);
        (void)deadline(t);
    };
    // Any notice routed through the owners' message chain observes the
    // provider, exactly as the shared SystemServices message does.
    const auto notice = [&] { (void)step.message(WM_APP + 0x0FFF, 0, 0); };
    const auto& alert = power_->panel().state();
    const auto rendered = [&] {
        // The alert renders on its own hidden WARP device given the packaged
        // shader; without the package it records the failure and keeps state.
        const auto window = static_cast<HWND>(power_->panel().window());
        return shaderAvailable_ ? power_->panel().lastFailure().empty() && window && !IsWindowVisible(window)
                                : !power_->panel().lastFailure().empty() && !window;
    };

    check(mode_ == Mode::hiddenCoverage && host_.mode() == mode_ && source_ == Source::synthetic && !host_.systemServices(),
          "Power area: hidden coverage reads the synthetic provider, never the system battery");
    check(host_.overlayVisible() && hudVisible_ && !terminating_ && !suspended_ && !alert.windowVisible(),
          "Power area: the HUD is open during the module cycles and holds the alert");
    step.drain();
    // --- Badge on the core plane (entrance, compact hold, circle) --------------
    check(badge_->state().phase() == m::ChargeBadgeState::Phase::presented && badge_->state().stage() == m::ChargeStage::circle &&
          !floatingEntries().empty() && uploads_ > 0 && lastCenter_.has_value(), "Power area: the badge entered and settled to its circle on the HUD frames");
    check(reading().present && reading().percentage == 63u && reading().capacity == m::BatteryReading::Capacity{32'760, 52'000, "mWh"},
          "Power area: the baseline reading carries the battery-class pair");
    check(powerAccessibilityLabel().find("63%") != std::string::npos, "Power area: the Power entry help carries the live reading");
    const auto circle = badgeClientPoint(badgeDesignCenter);
    check(circle && covers(*circle), "Power area: the circle hit region projects through the core plane");
    const auto previous = host_.selectedModule();
    check(pointer({app::PointerKind::move, app::PointerButton::none, circle->x, circle->y, 0}, t) && badge_->hovered(), "Power area: hover acquires the badge");
    hudFrames(.25, 8);
    check(badge_->state().stage() == m::ChargeStage::compact, "Power area: hover expands the compact capsule");
    const auto edge = badgeClientPoint({badgeDesignCenter.x + 100, badgeDesignCenter.y});
    check(edge && covers(*edge), "Power area: the expanded capsule keeps its compact target");
    // A press is captured; releasing it would select Power through the host
    // (power_area_tests covers that action). Here the press is cancelled so
    // another area's presented module is never switched mid-cycle.
    check(pointer({app::PointerKind::down, app::PointerButton::left, edge->x, edge->y, 0}, t) && host_.selectedModule() == previous,
          "Power area: a press on the capsule is captured");
    cancelInteraction(t);
    check(!badge_->hovered() && !pointer({app::PointerKind::up, app::PointerButton::left, edge->x, edge->y, 0}, t) &&
          host_.selectedModule() == previous, "Power area: a cancelled press selects nothing and leaving clears the hover");
    hudFrames(.25, 8);
    check(badge_->state().stage() == m::ChargeStage::circle, "Power area: the unhovered badge returns to its circle");

    // --- Provider while the HUD is open: live data, no alert --------------------
    const auto events = recordedEvents_;
    synthetic_ = syntheticSnapshot(64, true, true);
    notice();
    check(reading().pluggedIn && reading().charging && recordedEvents_ == events + 2 && !alert.windowVisible(),
          "Power area: a plug-in observed through the message chain is recorded and stays silent under the HUD");
    step.drain();
    check(reading().capacity == m::BatteryReading::Capacity{32'760, 52'000, "mWh"}, "Power area: the capacity pair returns through the shared executor");
    present(t + .02);
    check(powerAccessibilityLabel().find("64%") != std::string::npos, "Power area: the badge follows the live reading");

    // --- The HUD is concealed: the alert belongs to the desktop ----------------
    (void)hostEvent({Kind::overlayConcealed, 0}, t);
    check(!hudVisible_ && badge_->entries().empty() && !alert.windowVisible(),
          "Power area: concealment hides the badge; the plug-in seen under the HUD is the baseline");
    synthetic_ = syntheticSnapshot(64, false, false);
    notice();
    check(alert.windowVisible() && recordedEvents_ == events + 4 && rendered(), "Power area: unplugging presents the alert on its hidden coverage window");
    const auto cadence = nextWakeTime(t);
    check(cadence && std::abs(*cadence - (std::max(t, time_) + alertFrameInterval)) < 1e-9,
          "Power area: the animating alert wakes the shared timer at the display cadence");
    alertUntil(t + 1.7);
    const auto dismissal = power_->nextWakeTime();
    check(alert.stage() == m::ChargeStage::compact && !power_->requiresFrames(t) && dismissal && *dismissal > t + 1 && nextWakeTime(t) == dismissal,
          "Power area: the settled alert wakes only at its dismissal deadline");
    alertUntil(*dismissal + .8);
    check(!alert.windowVisible() && !nextWakeTime(t) && !power_->requiresFrames(t), "Power area: the dismissed alert releases every wake");
    // Tray "Preview charging effect".
    preview(t);
    check(alert.windowVisible() && alert.previewContent() && rendered(), "Power area: the tray preview presents with the preview label");
    alertUntil(t + 1.7);
    const auto previewEnd = power_->nextWakeTime();
    check(previewEnd.has_value(), "Power area: the preview owns its dismissal deadline");
    alertUntil(*previewEnd + .8);
    check(!alert.windowVisible() && !nextWakeTime(t), "Power area: the preview dismisses itself");

    // --- Settings: always mode through the committed preference record --------
    const auto original = host_.settings();
    const std::array<const char*, 5> keys{"displayMode", "placement", "customScreenID", "customPositionX", "customPositionY"};
    const auto restore = [&] {
        check(host_.updateSettings([&](ehud::data::Settings& s) { for (const auto* k : keys) s.set(k, original.fields[k]); }),
              "Power area: the preference record restores");
        step.drain();
        present(t + .02);
    };
    // Without the Settings owner there is no committed preference record.
    const bool settingsOwner = host_.updateSettings([](ehud::data::Settings& s) { s.set("displayMode", Json("always")); });
    if (settingsOwner) {
        step.drain();
        present(t + .02);
        alertUntil(t + 1.7);
        check(applied_ && applied_->settings.alwaysMode && alert.windowVisible() && alert.persistent() && !power_->nextWakeTime(),
              "Power area: always mode applies from the settings broadcast and presents persistently");
        restore();
        alertUntil(t + .8);
        check(applied_ && !applied_->settings.alwaysMode && !alert.windowVisible() && !nextWakeTime(t),
              "Power area: leaving always mode re-baselines silently");
    }

    // --- Position editing (Settings > Edit position, after the HUD closed) ------
    check(editPosition(t) && pendingEdit_ && !editingPosition(), "Power area: an edit request waits for the HUD to close");
    (void)hostEvent({Kind::overlayConcealed, 0}, t);
    check(editingPosition() && editing_ && alert.windowVisible() && rendered(), "Power area: concealment begins the queued position edit");
    const auto from = power_->panel().windowRect();
    const core::Point grab{from.x + from.width / 2, from.y + from.height * .3};
    check(power_->panel().pointer(gpu::ChargeIndicatorPanel::PointerKind::down, grab, t) &&
          power_->panel().pointer(gpu::ChargeIndicatorPanel::PointerKind::move, {grab.x, grab.y + 400}, t) &&
          power_->panel().pointer(gpu::ChargeIndicatorPanel::PointerKind::up, {grab.x, grab.y + 400}, t), "Power area: the editor drags the alert");
    check(power_->panel().key(VK_RETURN, t) && !editingPosition() && openDisplayPending_ && !alert.windowVisible(),
          "Power area: Return confirms and queues the Display reopen");
    alertChanged();
    check(!editing_, "Power area: the editing observer state follows the editor");
    if (settingsOwner) {
        step.drain();
        present(t + .02);
        const auto& saved = host_.settings();
        check(saved.string("placement") == "custom" && saved.fields["customScreenID"].isNumber() && saved.fields["customScreenID"].integer() == 11 &&
              applied_ && applied_->settings.customPlacement && applied_->settings.customScreenID == 11u &&
              std::abs(applied_->settings.customY - power_->coordinator().settings().customY) < 1e-9,
              "Power area: the confirmed position persists through the preference record");
    }
    // The private notice runs the deferred reopen (the coverage HUD is open).
    check(step.message(noticeMessage_, 0, 0) && !openDisplayPending_, "Power area: the private notice runs the deferred Display reopen");
    if (settingsOwner) restore();

    // --- Sleep with a plug change: shown exactly once on wake --------------------
    (void)hostEvent({Kind::suspended, app::HostEvent::suspendSystem}, t);
    check(suspended_ && !editPosition(t) && !alert.windowVisible(), "Power area: sleep hides the alert and refuses edits");
    synthetic_ = syntheticSnapshot(64, true, true);
    t += .1;
    (void)hostEvent({Kind::resumed, app::HostEvent::suspendSystem}, t);
    check(!suspended_ && alert.windowVisible() && reading().charging && recordedEvents_ == events + 6,
          "Power area: a plug change during sleep presents once on wake");
    alertUntil(t + .5);

    // --- Restore: the HUD reopens, the baseline reading returns ------------------
    setOverlayVisible(true, t);
    check(hudVisible_ && !alert.windowVisible() && !power_->requiresFrames(t) && nextWakeTime(t).has_value(),
          "Power area: reopening the HUD hides the alert at once and schedules the badge entrance");
    synthetic_ = syntheticSnapshot(63, false, false);
    notice();
    step.drain();
    check(reading().percentage == 63u && !reading().pluggedIn && reading().capacity == m::BatteryReading::Capacity{32'760, 52'000, "mWh"} &&
          recordedEvents_ == events + 8 && !alert.windowVisible(), "Power area: the baseline reading returns silently under the HUD");
    hudFrames(.8, 8);
    check(!badge_->entries().empty() && badge_->state().phase() != m::ChargeBadgeState::Phase::hidden, "Power area: the badge redeploys with the HUD");
}

std::unique_ptr<app::ModuleOwner> powerModule(const app::ApplicationModuleContext& context) {
    if (!context.host || !context.utility || !context.window) throw std::invalid_argument("The Power area needs the shared host, executor and HUD window");
    // The alert window renders with the packaged shader (SHA-256 verified in
    // production, the staged package in hidden coverage). Without it the
    // badge and provider still run; the alert records its window failure.
    auto shader = context.resource ? context.resource("shell.shader") : std::filesystem::path{};
    return std::make_unique<PowerArea>(context, std::move(shader));
}
} // namespace endfield::power
