#include "app/areas/mac_import_module.hpp"
#include "app/application_host.hpp"
#include "app/areas/mac_import_card.hpp"
#include "app/areas/mac_import_flow.hpp"
#include "app/areas/mac_import_handoff.hpp"
#include "app/utility_executor.hpp"
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_files.hpp"
#include "core/migration/mac_import_native.hpp"
#include "core/migration/mac_import_outcome.hpp"
#include "core/migration/mac_import_work.hpp"
#include "core/migration/plist.hpp"
#include "core/source_desktop_chrome.hpp"
#include "modules/hud_design_page.hpp"
#include "native/hud_design_scene.hpp"
#include "native/layer_scene.hpp"
#include "native/mac_import_automation.hpp"
#include "native/renderer.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h> // VARIANT/BSTR helpers even under WIN32_LEAN_AND_MEAN
#include <uiautomation.h>
#include <wrl/client.h>

namespace endfield::areas {
namespace {
namespace fs = std::filesystem;
namespace g = native;
namespace m = modules;
namespace mig = ehud::migration;
using app::ApplicationAreaMode;
using Json = ehud::data::Json;
using Matrix = core::Matrix4;
using Rect = core::Rect;
using Entries = std::span<const g::LayerCompositionEntry>;

void need(bool value, const char* why) { if (!value) throw std::runtime_error(std::string("macOS import: ") + why); }
double systemUnixNow() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string utf8(const fs::path& path) { const auto t = path.u8string(); return std::string(reinterpret_cast<const char*>(t.data()), t.size()); }
fs::path utf8Path(std::string_view text) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())); }

// The card on the design library (app/areas/mac_import_card.hpp): one
// NativeDesignScene over the panel's layer tree, placed like the shared
// centre overlay on the content box of the centre plane (DesktopChromeLayout::
// boxWorld of the frame, without the module switch motion), so it follows
// the HUD tilt as the confirm bar does. Content events re-emit only changed
// surfaces; warm frames only re-pose (progress fill, focus rings and the
// dismissal fade are pose numbers). A new palette or language rebuilds the
// panel (Context::apply). Pointer input goes to the panel's buttons through
// the library router and highlight pass (hover lift, press scale).
class NativeImportCard final {
public:
    NativeImportCard(m::design::Context& context, g::LayerRasterizer& r, g::LayerRasterOptions o)
        : context_(context), scene_(r, std::move(o), "macImport.card") { build(); }
    std::function<void(int)> onActivate;
    void sync(const MacImportCardContent& content, const MacImportCardState& state, double now) {
        need(std::isfinite(now), "Invalid import card time");
        const auto& env = context_.env();
        if (palette_ != env.palette.generation() || language_ != env.language) build();
        panel_->setContent(content);
        if (showings_ != state.showings()) { showings_ = state.showings(); panel_->reveal({now, state.reduceMotion(), &context_.env().deadlines}); }
        panel_->pose(state, now);
        const auto revision = panel_->root().arena().revision();
        if (revision != arenaRevision_) { arenaRevision_ = revision; ++revision_; }
        const auto before = rasterWork();
        (void)scene_.syncContent(panel_->root(), env.text, revision_, std::clamp(context_.boxScale(), 1., 4.), now);
        rasterizations_ += rasterWork() - before;
    }
    void updatePose(const Matrix& center, const app::ClientMetrics& metrics, float opacity, double now) {
        metrics_ = metrics;
        const auto boxWorld = core::source::designBoxWorld(center);
        const auto camera = g::layerViewportProjection(metrics.pixelWidth, metrics.pixelHeight) * Matrix::scale(metrics.scale, metrics.scale);
        projection_ = core::Projection::viewport(camera * boxWorld, metrics.pixelWidth, metrics.pixelHeight);
        const auto before = rasterWork();
        scene_.updatePose({boxWorld, opacity, now, {}, {}});
        rasterizations_ += rasterWork() - before;
        posed_ = true;
    }
    bool requiresFrames(double now) const { return posed_ && scene_.requiresFrames(now); }
    // SystemHUDView.updateControlHighlights + the router, in box px; a press
    // during the dismissal fade reaches nothing.
    void pointer(const app::PointerEvent& e, bool inert) {
        const auto q = local({e.x, e.y});
        auto& root = panel_->root();
        auto& router = panel_->router();
        const bool left = e.button == app::PointerButton::left;
        switch (e.kind) {
        case app::PointerKind::down: case app::PointerKind::doubleClick:
            if (left && q && !inert) pressed_ = router.pointerDown(*q, root);
            break;
        case app::PointerKind::move:
            if (pressed_ && q) router.pointerDragged(*q, root);
            break;
        case app::PointerKind::up:
            if (left && pressed_) { pressed_ = false; router.pointerUp(q.value_or(core::Point{-1e6, -1e6}), root); }
            break;
        case app::PointerKind::leave: case app::PointerKind::captureLost:
            cancel();
            return;
        }
        m::design::updateHighlights(root, q, pressed_);
    }
    void cancel() {
        if (pressed_) { pressed_ = false; panel_->router().cancel(); }
        m::design::updateHighlights(panel_->root(), std::nullopt, false);
    }
    std::optional<core::Point> actionPoint(int button) const {
        const auto g = geometry();
        const auto r = button ? g.confirm : g.cancel;
        return point({r.x + r.width * .5, r.y + r.height * .5});
    }
    std::optional<core::Point> listPoint() const {
        const auto g = geometry();
        return point({g.list.x + g.list.width * .5, g.list.y + g.list.height * .5});
    }
    MacImportCardGeometry geometry() const { return panel_->geometry(); }
    bool posed() const noexcept { return posed_; }
    // Logical client bounds of a panel-local rect through the current pose
    // (the projection pointer input uses); none when it does not project.
    std::optional<Rect> clientRect(Rect r) const {
        if (!posed_) return {};
        const auto card = geometry().card;
        double left = std::numeric_limits<double>::infinity(), top = left, right = -left, bottom = -left;
        for (const auto corner : std::array<core::Point, 4>{{{r.x, r.y}, {r.x + r.width, r.y}, {r.x, r.y + r.height}, {r.x + r.width, r.y + r.height}}}) {
            const auto q = projection_.project({card.x + corner.x, card.y + corner.y});
            if (!q || !std::isfinite(q->x) || !std::isfinite(q->y)) return {};
            left = std::min(left, q->x); top = std::min(top, q->y);
            right = std::max(right, q->x); bottom = std::max(bottom, q->y);
        }
        const double s = metrics_.scale > 0 ? metrics_.scale : 1.0;
        return Rect{left / s, top / s, (right - left) / s, (bottom - top) / s};
    }
    void upload(g::Renderer& r) { need(posed_, "Place the import card before uploading"); (void)scene_.uploadAnimations(r); }
    Entries entries() const { return scene_.entries(); }
    bool collectRetired(g::Renderer& r) { return scene_.collectRetired(r); }
    bool releaseResources(g::Renderer& r) { return scene_.releaseResources(r); }
    // Surfaces rasterized so far (content events only).
    std::size_t rasterizations() const noexcept { return rasterizations_; }
private:
    void build() {
        auto& env = context_.env();
        panel_ = std::make_unique<MacImportPanel>(env);
        panel_->onActivate = [this](int button) { if (onActivate) onActivate(button); };
        palette_ = env.palette.generation(); language_ = env.language; pressed_ = false;
        arenaRevision_ = panel_->root().arena().revision(); ++revision_;
    }
    std::size_t rasterWork() const {
        const auto s = scene_.stats();
        return std::size_t(s.partsLoaded + s.surfacesUpdated + s.lazyPaints);
    }
    std::optional<core::Point> local(core::Point logical) const {
        if (!posed_) return std::nullopt;
        return projection_.unproject({logical.x * metrics_.scale, logical.y * metrics_.scale});
    }
    std::optional<core::Point> point(core::Point panelLocal) const {
        if (!posed_) return std::nullopt;
        const auto card = geometry().card;
        const auto q = projection_.project({card.x + panelLocal.x, card.y + panelLocal.y});
        if (!q) return std::nullopt;
        const double s = metrics_.scale > 0 ? metrics_.scale : 1.0;
        return core::Point{q->x / s, q->y / s};
    }
    m::design::Context& context_;
    std::unique_ptr<MacImportPanel> panel_;
    g::NativeDesignScene scene_;
    app::ClientMetrics metrics_;
    core::Projection projection_;
    std::uint64_t palette_{}, arenaRevision_{}, revision_{}, showings_{};
    core::Language language_{core::Language::english};
    std::size_t rasterizations_{};
    bool posed_{}, pressed_{};
};

// Hidden-coverage stand-in for this owner's group under the HUD window's UIA
// root (app/hud_automation.cpp): a stable fragment root that forwards child
// navigation and hit tests to the ModuleOwner hooks exactly as the host's
// group does. It is never returned for a window, so no UIA client reaches it.
class CoverageGroup final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot {
public:
    explicit CoverageGroup(const app::ModuleOwner& owner) noexcept : owner_(owner) {}
    CoverageGroup(const CoverageGroup&) = delete;
    CoverageGroup& operator=(const CoverageGroup&) = delete;
    IRawElementProviderFragmentRoot* root() noexcept { return this; }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot)) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override { const ULONG n = --refs_; if (!n) delete this; return n; }
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_POINTER;
        *out = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID, VARIANT* out) override { if (!out) return E_POINTER; VariantInit(out); return S_OK; }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto count = owner_.automationCount();
        if (!count || (direction != NavigateDirection_FirstChild && direction != NavigateDirection_LastChild)) return S_OK;
        return owner_.automationChild(direction == NavigateDirection_FirstChild ? 0 : count - 1, out);
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* out) override { if (!out) return E_POINTER; *out = {}; return S_OK; }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP SetFocus() override { return S_OK; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override { if (!out) return E_POINTER; AddRef(); *out = this; return S_OK; }
    IFACEMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return owner_.automationFromPoint(x, y, out);
    }
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
private:
    ~CoverageGroup() = default;
    const app::ModuleOwner& owner_;
    std::atomic<ULONG> refs_{1};
};
bool sameObject(IUnknown* a, IUnknown* b) {
    Microsoft::WRL::ComPtr<IUnknown> x, y;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get() == y.Get();
}

// Hidden-coverage stand-in for the host's app::DataRootRestart. The host
// publishes the real service in production only (app/application.cpp
// requestRestart/declineRestart/finishQuit, the restart loop of app/main.cpp)
// and leaves the key free elsewhere; in hidden coverage this area publishes
// this object under the same key, so its production path (find, then
// restartWith) runs unchanged. The driver plays the host's answers with the
// host's contract: one pending request at a time; a failed save or a session
// end destroys the work unrun and then runs `declined` on the UI thread; an
// accepted quit tears the owner down first and runs the work exactly once.
class CoverageRestart final : public app::DataRootRestart {
public:
    bool restartWith(std::function<void()> work, std::function<void()> declined) override {
        if (pending_ || !work) { ++refused_; return false; }
        work_ = std::move(work);
        declined_ = std::move(declined);
        pending_ = true;
        ++requests_;
        return true;
    }
    bool pending() const noexcept { return pending_; }
    unsigned requests() const noexcept { return requests_; }
    unsigned refused() const noexcept { return refused_; }
    void decline() {
        pending_ = false;
        work_ = {}; // destroyed unrun
        if (auto declined = std::exchange(declined_, {})) declined();
    }
    std::function<void()> accept() {
        pending_ = false;
        declined_ = {};
        return std::exchange(work_, {});
    }
private:
    std::function<void()> work_, declined_;
    bool pending_{};
    unsigned requests_{}, refused_{};
};

// A recorded outcome of the previous run and the unresolved relink entries.
struct PendingOutcome { mig::MacImportOutcome outcome; std::vector<mig::MacRelinkItem> relink; };

class MacImportArea final : public app::ModuleOwner, public app::MacImportEntry {
public:
    MacImportArea(const app::ApplicationModuleContext& c, fs::path destination, fs::path goldenFixture)
        : host_(*c.host), raster_(c.rasterizer), utility_(*c.utility), mode_(c.mode), window_(c.window), destination_(std::move(destination)),
          coverageRoot_(c.coverageRoot), goldenFixture_(std::move(goldenFixture)), metrics_(c.metrics), alive_(std::make_shared<bool>(true)) {
        outcomeRoute_ = utility_.makeRoute();
        const auto& a = host_.appearance();
        language_ = a.language;
        // The card is a design-library panel on the HUD's one design Context
        // (palette, language, text services, render scale); none: no card.
        context_ = host_.designContext();
        flow_ = makeFlow();
        host_.publish<app::MacImportEntry>(app::macImportEntryService, *this);
        if (coverageMode() && !host_.find<app::DataRootRestart>(app::dataRootRestartService)) {
            coverageRestart_ = std::make_unique<CoverageRestart>();
            host_.publish<app::DataRootRestart>(app::dataRootRestartService, *coverageRestart_);
        }
        // Folders an earlier run left behind, then that run's recorded
        // outcome (and relink list), both on the shared worker.
        startSweep();
        loadOutcome();
    }
    ~MacImportArea() override {
        *alive_ = false;
        host_.withdraw(app::macImportEntryService);
        if (coverageRestart_) host_.withdraw(app::dataRootRestartService);
        // A queued note removal or sweep step still runs; no completion
        // reaches this owner.
        utility_.invalidate(outcomeRoute_, false);
        automation_.reset();
        if (flow_) flow_->shutdown();
    }
    MacImportArea(const MacImportArea&) = delete;
    MacImportArea& operator=(const MacImportArea&) = delete;

    // --- MacImportEntry ----------------------------------------------------------
    bool present(double time) override {
        time_ = std::max(time_, time);
        flow_->open();
        flowChanged();
        return true;
    }
    std::string title(core::Language language) const override { return macImportEntryTitle(language); }

    // --- ModuleOwner -------------------------------------------------------------
    std::string_view name() const noexcept override { return "macImport"; }
    app::ModuleRouting routing() const noexcept override {
        // A modal card: captures before every main chain, after the built-in
        // modal captures (0...6), like the red power button confirmation.
        app::ModuleRouting r; r.pointerCapture = 20; r.wheelCapture = 20; r.keyCapture = 20; r.pointerPolicy = {false, false, false}; return r;
    }
    void resize(const app::ClientMetrics& metrics) override { metrics_ = metrics; }
    void setAppearance(const app::ModuleAppearance& a, double time) override {
        time_ = std::max(time_, time);
        language_ = a.language;
        flow_->setLanguage(a.language);
        state_.setReduceMotion(a.reduceMotion, time_);
    }
    void setOverlayVisible(bool visible, double time) override {
        time_ = std::max(time_, time);
        overlayVisible_ = visible;
        if (!visible) { cancelInteraction(time_); return; }
        if (!presentPendingOutcome()) syncCard();
    }
    // The card fades with the closing canvas and hides at concealment.
    void overlayClosing(double time) override { cancelInteraction(time); overlayVisible_ = false; }
    void update(const app::ModuleFrame& f) override {
        time_ = std::max(time_, f.time);
        state_.refresh(time_);
        if (!state_.presented() || !metrics_.pixelWidth || !metrics_.pixelHeight || !context_) return;
        if (!card_) {
            g::LayerRasterOptions options; options.paddingPoints = 1;
            card_ = std::make_unique<NativeImportCard>(*context_, raster_, options);
            // A button press: its keyboard focus moves there, then it answers.
            card_->onActivate = [this](int button) {
                if (!state_.focus(button, time_)) return;
                answer(button ? MacImportCardAction::right : MacImportCardAction::left);
            };
            automationPose_ = AutomationPose::unposed;
        }
        card_->sync(flow_->content(), state_, time_);
        card_->updatePose(f.center, metrics_, f.opacity, time_);
        // The card's UIA parts need its pose: the dialog (and its focused
        // button's focus event) appears on the card's first frame, and its
        // bounds are re-read once the reveal settles. Two content events per
        // presentation; warm frames do nothing here.
        if (automationPose_ != AutomationPose::settled) {
            const bool settled = !card_->requiresFrames(time_);
            if (automationPose_ == AutomationPose::unposed || settled) automationActed();
            automationPose_ = settled ? AutomationPose::settled : AutomationPose::revealing;
        }
    }
    bool requiresFrames(double time) const override {
        const auto t = std::max(time, time_);
        return state_.requiresFrames(t) || (card_ && state_.presented() && card_->requiresFrames(t));
    }
    void upload(g::Renderer& r) override { if (card_ && state_.presented() && card_->posed()) card_->upload(r); }
    Entries modalEntries() override {
        if (!card_ || !state_.presented() || !card_->posed()) return {};
        return card_->entries();
    }
    void collected(g::Renderer& r) override {
        if (!card_) return;
        need(card_->collectRetired(r), "Import card artwork still published");
        if (!state_.presented()) { need(card_->releaseResources(r), "Import card still published"); card_.reset(); }
    }
    void release(g::Renderer& r) override { if (card_) { need(card_->releaseResources(r), "Import card still published at teardown"); card_.reset(); } }
    bool covers(core::Point) const override { return state_.presented(); }
    bool capturesPointer() const override { return state_.presented(); }
    bool capturesWheel() const override { return state_.presented(); }
    bool capturesKeys() const override { return state_.presented(); }
    bool suppressesKeyFilters() const override { return state_.presented(); }
    bool pointer(const app::PointerEvent& e, double time) override {
        if (!state_.presented()) return false;
        time_ = std::max(time_, time);
        if (card_) card_->pointer(e, state_.dismissing());
        host_.requestFrame();
        return true;
    }
    bool wheel(const app::WheelEvent& e, double time) override {
        if (!state_.presented()) return false;
        time_ = std::max(time_, time);
        if (!e.horizontal && std::isfinite(e.steps)) {
            // One list row per detent; fractions accumulate (precision wheels).
            wheelRemainder_ += e.steps;
            const auto rows = static_cast<std::ptrdiff_t>(std::trunc(wheelRemainder_));
            wheelRemainder_ -= static_cast<double>(rows);
            if (rows) (void)flow_->scroll(-rows);
        }
        return true;
    }
    bool key(const app::KeyEvent& e, double time) override {
        if (!state_.presented()) return false;
        time_ = std::max(time_, time);
        if (e.kind == app::KeyKind::down) answer(state_.key(e.value, e.previouslyDown, time_));
        host_.requestFrame();
        return true;
    }
    void focus(bool value, double time) override { if (!value) cancelInteraction(time); }
    void cancelInteraction(double time) override {
        time_ = std::max(time_, time);
        if (card_) card_->cancel();
    }
    bool hostEvent(const app::HostEvent& e, double time) override {
        using K = app::HostEvent::Kind;
        time_ = std::max(time_, time);
        switch (e.kind) {
        case K::utilityCompleted:
            // Step completions already ran in the executor's drain (each one
            // reported through flowChanged, which also refreshes UIA).
            flow_->utilityCompleted();
            if (outcomeQueued_) loadOutcome(); // the shared queue was full
            if (sweepQueued_) sweepNext();
            return false;
        case K::overlayConcealed:
            // The HUD is hidden: the card goes with it; a running import keeps
            // going and its card returns when the HUD opens again.
            state_.hideImmediately(time_);
            return false;
        default: return false;
        }
    }
    void coverage(app::ModuleCoverage& step) override;

    // UI Automation (app/module_owner.hpp): the card as one dialog under this
    // owner's group. Fragments are created on the host's first refresh, i.e.
    // once a UIA client asked the window; a new group replaces them.
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override {
        if (!root) return false;
        bool replaced = false;
        if (automation_ && automationRoot_ != root) {
            replaced = automation_->size() > 0;
            automation_.reset(); // disconnects its fragments and releases the old group
            automationRoot_ = nullptr;
        }
        if (!automation_) {
            automation_ = std::make_unique<g::NativeMacImportAutomation>(automationHost(root));
            automationRoot_ = root;
        }
        return automation_->refresh() || replaced;
    }
    std::size_t automationCount() const override { return automation_ ? automation_->size() : 0; }
    long automationChild(std::size_t index, IRawElementProviderFragment** out) const override {
        if (!automation_) { if (out) *out = nullptr; return E_INVALIDARG; }
        return automation_->child(index, out);
    }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override {
        if (!automation_) { if (out) *out = nullptr; return S_OK; }
        return automation_->fromPoint(x, y, out);
    }
    void automationDisconnect() noexcept override { if (automation_) automation_->disconnect(); }

private:
    bool coverageMode() const noexcept { return mode_ == ApplicationAreaMode::hiddenCoverage; }
    double now() const { return host_.now(); }
    double unixNow() {
        // Hidden coverage keeps report contents deterministic across cycles.
        if (coverageMode()) return 1760000000.0 + double(++coverageClock_);
        return systemUnixNow();
    }
    std::unique_ptr<MacImportFlow> makeFlow() {
        MacImportFlow::Host h;
        std::weak_ptr<bool> alive = alive_;
        h.chooseFolder = [this, alive](const std::string& caption, const std::string& accept) {
            if (coverageMode()) { ++coverageChooser_; chooserPending_ = true; return true; }
            return host_.chooseFiles(app::ApplicationFileRequest{caption, accept, accept}, [this, alive](app::ApplicationFileResult result) {
                if (const auto token = alive.lock(); !token || !*token) return;
                flow_->folderChosen(result.cancelled, result.failed, std::move(result.paths));
                flowChanged();
            });
        };
        h.cancelChoice = [this] {
            if (coverageMode()) { chooserCancelled_ = true; return; } // the driver delivers the cancelled completion
            host_.cancelFileChoice();
        };
        h.inProcess = [this] { return coverageMode() && coverageInProcess_; };
        h.restart = [this](std::shared_ptr<mig::MacImportSession> session) { return requestRestart(std::move(session)); };
        h.restartAvailable = [this] { return host_.find<app::DataRootRestart>(app::dataRootRestartService) != nullptr; };
        h.forgetOutcome = [this] { forgetOutcome(); };
        h.changed = [this] { flowChanged(); };
        h.unixNow = [this] { return unixNow(); };
        const bool coverage = coverageMode();
        return std::make_unique<MacImportFlow>(utility_, destination_, [coverage] {
            auto platform = mig::nativeMacImportPlatform();
            if (coverage) platform.unixNow = [] { return 1760000000.0; };
            return platform;
        }, std::move(h));
    }
    // The host's restart handoff closes every store first (production; the
    // coverage stand-in in hidden coverage). The work captures only the
    // session and the destination: it runs after this owner was destroyed.
    bool requestRestart(std::shared_ptr<mig::MacImportSession> session) {
        auto* restart = host_.find<app::DataRootRestart>(app::dataRootRestartService);
        if (!restart) return false;
        const auto destination = destination_;
        const bool coverage = coverageMode();
        auto work = [session = std::move(session), destination, coverage] {
            (void)mig::commitMacImportSession(*session, destination, coverage ? 1760000000.0 : systemUnixNow());
        };
        std::weak_ptr<bool> alive = alive_;
        auto declined = [this, alive] {
            if (const auto token = alive.lock(); !token || !*token) return;
            flow_->restartDeclined();
            flowChanged();
        };
        return restart->restartWith(std::move(work), std::move(declined));
    }
    // Bounded sweep of the importer's stale private folders next to the root.
    void startSweep() {
        sweep_ = std::make_shared<mig::MacImportWorkSweep>(destination_);
        sweepNext();
    }
    void sweepNext() {
        if (!sweep_) return;
        auto sweep = sweep_;
        auto more = std::make_shared<bool>(false);
        std::weak_ptr<bool> alive = alive_;
        sweepQueued_ = false;
        const bool queued = utility_.submit(outcomeRoute_, [sweep, more] { *more = sweep->next(); }, [this, alive, sweep, more](std::exception_ptr) {
            if (const auto token = alive.lock(); !token || !*token) return;
            if (*more) { sweepNext(); return; }
            swept_ = sweep->stats();
            if (sweep_ == sweep) sweep_.reset();
        });
        sweepQueued_ = !queued; // a full queue resumes from utilityCompleted
    }
    void loadOutcome() {
        auto result = std::make_shared<std::optional<PendingOutcome>>();
        std::weak_ptr<bool> alive = alive_;
        const auto destination = destination_;
        outcomeQueued_ = false;
        outcomeLoading_ = utility_.submit(outcomeRoute_, [destination, result] {
            try {
                auto outcome = mig::readMacImportOutcome(destination);
                if (!outcome) return;
                std::vector<mig::MacRelinkItem> relink;
                if (outcome->state == mig::MacImportOutcome::State::completed) {
                    try { relink = mig::MacRelinkLedger(destination).unresolved(); } catch (const std::exception&) {}
                }
                *result = PendingOutcome{std::move(*outcome), std::move(relink)};
            } catch (const std::exception&) {
                // An unreadable note cannot be reported: drop it (the import's
                // own Migration/import.json and backup are untouched).
                (void)mig::removeMacImportOutcome(destination);
            }
        }, [this, alive, result](std::exception_ptr) {
            if (const auto token = alive.lock(); !token || !*token) return;
            outcomeLoading_ = false;
            if (*result) pending_ = std::move(**result);
            presentPendingOutcome();
        });
        outcomeQueued_ = !outcomeLoading_;
    }
    void forgetOutcome() {
        const auto destination = destination_;
        if (!utility_.submit(outcomeRoute_, [destination] { (void)mig::removeMacImportOutcome(destination); }, [](std::exception_ptr) {}))
            (void)mig::removeMacImportOutcome(destination); // full queue: one small file, removed here
    }
    bool presentPendingOutcome() {
        if (!pending_ || !overlayVisible_ || flow_->presented()) return false;
        auto outcome = std::move(*pending_);
        pending_.reset();
        flow_->showOutcome(std::move(outcome.outcome), std::move(outcome.relink));
        flowChanged();
        return true;
    }
    void flowChanged() {
        const auto t = std::max(time_, now());
        time_ = t;
        state_.setEnabled(flow_->content().enabled, t);
        if (flow_->step() != step_) {
            const bool fromHidden = step_ == MacImportStep::hidden;
            step_ = flow_->step();
            if (!fromHidden) state_.resetFocus(t);
        }
        syncCard();
        // A recorded outcome that arrived while another card was open is
        // shown once that card closes.
        if (pending_ && !flow_->presented()) (void)presentPendingOutcome();
        host_.requestFrame();
        // A content event: connected UIA parts follow (clients only).
        automationActed();
    }
    void syncCard() {
        const auto t = std::max(time_, now());
        if (!flow_->presented()) { if (state_.presented() && !state_.dismissing()) state_.dismiss(true, t); }
        else if (overlayVisible_ && (!state_.presented() || state_.dismissing())) state_.show(t);
    }
    void answer(std::optional<MacImportCardAction> action) {
        if (!action) return;
        switch (*action) {
        case MacImportCardAction::left: flow_->activate(0); break;
        case MacImportCardAction::right: flow_->activate(1); break;
        case MacImportCardAction::escape: flow_->escape(); break;
        case MacImportCardAction::scrollUp: (void)flow_->scroll(-1); break;
        case MacImportCardAction::scrollDown: (void)flow_->scroll(1); break;
        case MacImportCardAction::pageUp: (void)flow_->scroll(-std::ptrdiff_t(macImportCardRows)); break;
        case MacImportCardAction::pageDown: (void)flow_->scroll(std::ptrdiff_t(macImportCardRows)); break;
        case MacImportCardAction::none: break;
        }
    }

    // --- UI Automation ---------------------------------------------------------
    g::MacImportAutomationHost automationHost(IRawElementProviderFragmentRoot* root) {
        g::MacImportAutomationHost h;
        h.root = root;
        h.nodes = [this] { return automationNodes(); };
        h.toScreen = [this](Rect r) { return screenRect(r); };
        h.invoke = [this](std::string_view id) {
            const int button = id == "cancel" ? 0 : id == "confirm" ? 1 : -1;
            if (button < 0 || !flow_ || !state_.presented() || state_.dismissing() || !state_.enabled()[static_cast<std::size_t>(button)]) return false;
            time_ = std::max(time_, now());
            // The click's action (Mac accessibilityPerformPress); the flow
            // reports the change, which refreshes these parts.
            answer(button ? MacImportCardAction::right : MacImportCardAction::left);
            host_.requestFrame();
            return true;
        };
        h.focus = [this](std::string_view id) {
            const int button = id == "cancel" ? 0 : id == "confirm" ? 1 : -1;
            if (button < 0 || !flow_ || !flow_->presented()) return false;
            time_ = std::max(time_, now());
            // The keyboard focus moves exactly as Tab/Left/Right move it; the
            // parts follow at once (HasKeyboardFocus and the focus event).
            if (!state_.focus(button, time_)) return false;
            host_.requestFrame();
            automationActed();
            return true;
        };
        return h;
    }
    // The presented card in pre-order (native/mac_import_automation.hpp).
    std::vector<g::MacImportAutomationNode> automationNodes() const {
        using N = g::MacImportAutomationNode;
        using R = N::Role;
        std::vector<N> out;
        if (!flow_ || !flow_->presented() || !state_.presented() || state_.dismissing() || !card_ || !card_->posed()) return out;
        const auto geometry = std::optional(card_->geometry());
        const auto& c = flow_->content();
        const auto part = [&](std::string id, R role, std::string name, std::string help, Rect local, std::size_t parent) {
            N n;
            n.id = std::move(id); n.role = role; n.name = std::move(name); n.help = std::move(help);
            n.rect = card_->clientRect(local); n.parent = parent;
            out.push_back(std::move(n));
            return out.size() - 1;
        };
        const auto dialog = part("dialog", R::dialog, c.title, c.message, {0, 0, geometry->card.width, geometry->card.height}, N::topLevel);
        part("title", R::text, c.title, {}, geometry->title, dialog);
        if (!c.message.empty()) part("message", R::text, c.message, {}, geometry->message, dialog);
        if (c.progress) {
            const auto bar = part("progress", R::progress, c.title, c.message, geometry->bar, dialog);
            out[bar].value = std::clamp(*c.progress, 0.0, 1.0) * 100;
        }
        if (!c.detail.empty()) part("detail", R::text, c.detail, {}, geometry->detail, dialog);
        else if (!flow_->list().empty()) {
            const auto list = part("list", R::list, c.title, c.position, geometry->list, dialog);
            const bool fullWidth = language_ == core::Language::simplifiedChinese || language_ == core::Language::traditionalChinese || language_ == core::Language::japanese;
            const auto& rows = flow_->list();
            const auto first = flow_->firstRow();
            out.reserve(out.size() + rows.size() + 2);
            for (std::size_t n = 0; n < rows.size(); ++n) {
                N row;
                row.id = "row." + std::to_string(n);
                row.role = R::item;
                row.name = rows[n].value.empty() ? rows[n].label : rows[n].label + (fullWidth ? "\xEF\xBC\x9A" : ": ") + rows[n].value;
                // Rows scrolled out of the card are offscreen (no bounds).
                if (n >= first && n < first + macImportCardRows) row.rect = card_->clientRect(geometry->row(n - first));
                row.parent = list;
                out.push_back(std::move(row));
            }
        }
        for (unsigned b = 0; b < 2; ++b) {
            const auto index = part(b ? "confirm" : "cancel", R::button, c.buttons[b], {}, b ? geometry->confirm : geometry->cancel, dialog);
            out[index].enabled = state_.enabled()[b];
            out[index].focused = state_.focused() == int(b);
        }
        return out;
    }
    // Logical client rect -> physical screen pixels.
    std::optional<Rect> screenRect(Rect r) const {
        POINT origin{};
        if (!window_ || !ClientToScreen(static_cast<HWND>(window_), &origin)) return std::nullopt;
        const double scale = metrics_.scale > 0 ? metrics_.scale : 1.0;
        return Rect{origin.x + r.x * scale, origin.y + r.y * scale, r.width * scale, r.height * scale};
    }
    // A content event no host call reports (a dialog completion, a declined
    // restart, an executor step, a UIA Invoke): re-list at once and, like the
    // host after its own refresh, tell listening clients the group changed.
    void automationActed() {
        if (!automation_ || !automation_->refresh() || !automationRoot_ || !UiaClientsAreListening()) return;
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> group;
        if (SUCCEEDED(automationRoot_->QueryInterface(IID_PPV_ARGS(&group))))
            (void)UiaRaiseStructureChangedEvent(group.Get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
    }

    struct Driver;
    void relinkOwnerCalls(app::ModuleCoverage&);
    app::ApplicationHost& host_;
    g::LayerRasterizer& raster_;
    app::UtilityExecutor& utility_;
    ApplicationAreaMode mode_{};
    void* window_{};
    fs::path destination_, coverageRoot_, goldenFixture_;
    app::ClientMetrics metrics_;
    std::shared_ptr<bool> alive_;
    app::UtilityExecutor::Route outcomeRoute_{};
    core::Language language_{core::Language::english};
    MacImportCardState state_;
    std::unique_ptr<MacImportFlow> flow_;
    m::design::Context* context_{};
    std::unique_ptr<NativeImportCard> card_;
    std::optional<PendingOutcome> pending_;
    MacImportStep step_{MacImportStep::hidden};
    double time_{}, wheelRemainder_{};
    bool overlayVisible_{}, outcomeLoading_{}, outcomeQueued_{}, sweepQueued_{};
    std::shared_ptr<mig::MacImportWorkSweep> sweep_;
    std::optional<mig::MacImportWorkSweep::Stats> swept_; // the last finished sweep
    std::unique_ptr<g::NativeMacImportAutomation> automation_;
    IRawElementProviderFragmentRoot* automationRoot_{}; // held by automation_
    enum class AutomationPose : std::uint8_t { unposed, revealing, settled };
    AutomationPose automationPose_{AutomationPose::unposed};
    // Hidden coverage only.
    std::unique_ptr<CoverageRestart> coverageRestart_;
    Microsoft::WRL::ComPtr<CoverageGroup> coverageGroup_;
    bool coverageInProcess_{true}, chooserPending_{}, chooserCancelled_{};
    unsigned coverageChooser_{};
    std::uint64_t coverageClock_{};
};

// One module-less hidden-coverage step per combined cycle, on the card through
// projected pointer input, keys and its UI Automation parts:
//   cycle 0  in-process import of the golden export: intro, Escape, folder,
//            bounded staging with progress, per data set review, commit with
//            progress, final report, relink list, Done; the card's UIA tree
//            (dialog, parts, focus and its focus events, SetFocus, offscreen
//            rows, hit test, Invoke); the relink ledger's owner API over the
//            imported root as the module owners call it;
//   cycle 1  the same destination again through the production restart path
//            against the stand-in host service: replace prompt, review, a
//            refused second request, a failed save (declined), the accepted
//            quit (owner teardown first, then the work once), the next run's
//            startup sweep of planted stale folders and its recorded report;
//            the host's pre-open recovery of an interrupted activation;
//   cycle 2  an invalid folder (failure card), cancelling a running staging,
//            the keyboard path with a cancelled dialog, and a session end
//            while a restart is pending (the work never runs).
// Each cycle ends hidden with the card's raster entries released.
struct MacImportArea::Driver {
    using Fragment = Microsoft::WRL::ComPtr<IRawElementProviderFragment>;
    MacImportArea& a;
    app::ModuleCoverage& step;
    double t;
    const MacImportCardContent& content() const { return a.flow_->content(); }
    void frame(double dt = 1. / 60) { t += dt; step.present(t); }
    void frames(double seconds) { const double end = t + seconds; while (t < end) frame(); }
    bool busy() const { return a.flow_->busy() || a.outcomeLoading_ || a.sweep_ != nullptr; }
    void settle() {
        for (unsigned n = 0; n < 800 && busy(); ++n) { step.drain(); frame(); }
        step.check(!busy(), "The import settles through the shared executor");
        frames(.05);
    }
    void click(int button) {
        frames(.25); // the reveal and any feedback finished
        step.check(a.state_.presented() && a.card_ != nullptr, "The import card is presented");
        const auto p = a.card_->actionPoint(button);
        step.check(p.has_value(), "The card button projects onto the client");
        step.check(a.covers(*p) && a.capturesPointer() && a.capturesKeys() && a.capturesWheel() && a.suppressesKeyFilters(), "The card is modal");
        step.check(a.state_.enabled()[button], "The clicked button is enabled");
        a.pointer({app::PointerKind::move, app::PointerButton::none, p->x, p->y, 0}, t);
        a.pointer({app::PointerKind::down, app::PointerButton::left, p->x, p->y, 0}, t);
        a.pointer({app::PointerKind::up, app::PointerButton::left, p->x, p->y, 0}, t + .02);
        frame();
    }
    void key(unsigned vk) { a.key({app::KeyKind::down, vk}, t); frame(); }
    void open() {
        step.check(a.host_.find<app::MacImportEntry>(app::macImportEntryService) == &a, "The Settings entry service resolves to the area");
        step.check(a.present(t), "The Settings entry presents the card");
        frames(.3);
        step.check(a.flow_->step() == MacImportStep::intro && a.state_.presented() && !a.modalEntries().empty(), "The intro card is above the HUD");
        step.check(content().enabled == std::array<bool, 2>{true, true} && !content().rows[0].label.empty(), "The intro lists the data sets");
    }
    void hidden(const char* why) {
        frames(.3);
        step.check(!a.flow_->presented() && !a.state_.presented() && a.modalEntries().empty() && !a.card_ && !a.capturesPointer(), why);
        // Residency (WINDOWS-MIGRATION.md section 9): the hidden card keeps
        // nothing of the import (no session, results, outcome, rows or text)
        // and no UIA part.
        step.check(a.flow_->residency() == MacImportFlowResidency{} && (!a.automation_ || a.automation_->parts() == 0),
                   "The hidden card holds nothing of the import");
    }
    // Choose Folder through the projected button; the dialog is synthetic.
    void choose(const fs::path& folder) {
        const auto before = a.coverageChooser_;
        click(1);
        step.check(a.coverageChooser_ == before + 1 && a.chooserPending_ && a.flow_->step() == MacImportStep::choosing && !a.state_.enabled()[1],
                   "Choose Folder asks the one shared dialog and waits");
        a.chooserPending_ = false;
        a.flow_->folderChosen(false, false, {utf8(folder)});
        a.flowChanged();
        frame();
    }
    void stage(const fs::path& folder, bool automation = false) {
        choose(folder);
        step.check(a.flow_->step() == MacImportStep::staging && content().progress && !a.state_.enabled()[1], "Staging shows progress with Import disabled");
        double last = 0, lastPercent = 0;
        bool sawProgress = false;
        for (unsigned n = 0; n < 800 && a.flow_->step() == MacImportStep::staging; ++n) {
            step.drain(); frame();
            if (a.flow_->step() != MacImportStep::staging) break;
            step.check(content().progress && *content().progress >= last, "Staging progress only moves forward");
            last = *content().progress;
            if (automation) {
                // The progress bar part follows each executor step.
                (void)a.automationRefresh(group());
                const auto bar = part(L"macImport.progress");
                step.check(bar.Get() != nullptr, "Staging lists a progress bar part");
                Microsoft::WRL::ComPtr<IRangeValueProvider> range;
                double percent{}, maximum{};
                BOOL readOnly{};
                step.check(SUCCEEDED(pattern(bar.Get(), UIA_RangeValuePatternId, range)) && SUCCEEDED(range->get_Value(&percent)) &&
                           SUCCEEDED(range->get_Maximum(&maximum)) && SUCCEEDED(range->get_IsReadOnly(&readOnly)) && readOnly && maximum == 100 &&
                           std::abs(percent - last * 100) < 1e-9 && percent >= lastPercent, "The progress bar reports the card's progress, read-only");
                lastPercent = percent;
                sawProgress = true;
                step.check(invoke(L"macImport.confirm") == UIA_E_ELEMENTNOTENABLED, "Disabled buttons refuse UIA Invoke");
                const auto confirm = part(L"macImport.confirm");
                step.check(confirm && confirm->SetFocus() == UIA_E_ELEMENTNOTENABLED && integer(confirm.Get(), UIA_IsKeyboardFocusablePropertyId) == 0 &&
                           integer(confirm.Get(), UIA_HasKeyboardFocusPropertyId) == 0 && focusedParts() == std::vector<std::wstring>{L"macImport.cancel"},
                           "A disabled button refuses SetFocus; Cancel keeps the keyboard focus");
            }
        }
        step.check(!automation || sawProgress, "Staging progress reached UIA");
        settle();
    }
    void review(const Json& expected) {
        step.check(a.flow_->step() == MacImportStep::review && content().enabled[1], "Staging ends in the per data set review");
        const auto* summary = a.flow_->service().summary();
        step.check(summary != nullptr, "The review has a summary");
        for (const auto store : mig::macImportStores) {
            const auto name = std::string(mig::macImportStoreName(store));
            step.check(std::string(mig::macImportStatusName(summary->store(store).status)) == expected["stores"][name].string(),
                       "The Windows validators accept the golden export exactly like the Mac oracle");
        }
        step.check(summary->relink.size() == std::size_t(expected["relinkItems"].integer()) && summary->rejected().empty(), "Every Mac file reference is listed for relinking");
        for (std::size_t n = 0; n < mig::macImportStores.size(); ++n)
            step.check(content().rows[n].label == macImportStoreTitle(mig::macImportStores[n], a.language_) && !content().rows[n].value.empty(), "One report row per data set");
    }
    // An accepted quit (application.cpp finishQuit, then ~Application): the
    // owner's flow is destroyed with its reference to a handed-off session.
    void endRun() {
        a.flow_->shutdown();
    }
    // The next run over the same root (main.cpp): a new flow, the startup
    // sweep and the recorded outcome, exactly as the constructor starts them.
    void startRun() {
        a.flow_ = a.makeFlow();
        a.step_ = MacImportStep::hidden;
        a.flowChanged();
        a.startSweep();
        a.loadOutcome();
        settle();
    }

    // --- UI Automation helpers (the host's group stand-in) -------------------------
    IRawElementProviderFragmentRoot* group() {
        if (!a.coverageGroup_) a.coverageGroup_.Attach(new CoverageGroup(a));
        return a.coverageGroup_->root();
    }
    static std::wstring text(IRawElementProviderFragment* f, PROPERTYID id) {
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
        VARIANT v;
        VariantInit(&v);
        if (!f || FAILED(f->QueryInterface(IID_PPV_ARGS(&simple))) || FAILED(simple->GetPropertyValue(id, &v))) return {};
        std::wstring out = v.vt == VT_BSTR && v.bstrVal ? std::wstring(v.bstrVal, SysStringLen(v.bstrVal)) : std::wstring{};
        VariantClear(&v);
        return out;
    }
    static std::optional<long> integer(IRawElementProviderFragment* f, PROPERTYID id) {
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
        VARIANT v;
        VariantInit(&v);
        if (!f || FAILED(f->QueryInterface(IID_PPV_ARGS(&simple))) || FAILED(simple->GetPropertyValue(id, &v))) return {};
        std::optional<long> out;
        if (v.vt == VT_I4) out = v.lVal;
        else if (v.vt == VT_BOOL) out = v.boolVal == VARIANT_TRUE ? 1 : 0;
        VariantClear(&v);
        return out;
    }
    static Fragment navigate(IRawElementProviderFragment* f, NavigateDirection direction) {
        Fragment out;
        if (f) (void)f->Navigate(direction, &out);
        return out;
    }
    template <class T> static HRESULT pattern(IRawElementProviderFragment* f, PATTERNID id, Microsoft::WRL::ComPtr<T>& out) {
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
        Microsoft::WRL::ComPtr<IUnknown> unknown;
        if (!f || FAILED(f->QueryInterface(IID_PPV_ARGS(&simple)))) return E_FAIL;
        const auto hr = simple->GetPatternProvider(id, &unknown);
        if (FAILED(hr) || !unknown) return FAILED(hr) ? hr : E_NOINTERFACE;
        return unknown.As(&out);
    }
    // Depth-first walk of the owner's tree through UIA navigation only.
    void walk(IRawElementProviderFragment* f, std::vector<Fragment>& out) {
        for (auto child = navigate(f, NavigateDirection_FirstChild); child.Get(); child = navigate(child.Get(), NavigateDirection_NextSibling)) {
            out.push_back(child);
            walk(child.Get(), out);
            step.check(out.size() < 4096, "The UIA tree is finite");
        }
    }
    std::vector<Fragment> parts() {
        std::vector<Fragment> out;
        Fragment dialog;
        if (a.automationCount() && SUCCEEDED(a.automationChild(0, &dialog)) && dialog.Get()) { out.push_back(dialog); walk(dialog.Get(), out); }
        return out;
    }
    Fragment part(std::wstring_view id) {
        for (auto& f : parts()) if (text(f.Get(), UIA_AutomationIdPropertyId) == id) return f;
        return {};
    }
    // The parts reporting UIA keyboard focus, found as the host's GetFocus
    // finds them (HasKeyboardFocus over the owner's fragments).
    std::vector<std::wstring> focusedParts() {
        std::vector<std::wstring> out;
        for (auto& f : parts()) if (integer(f.Get(), UIA_HasKeyboardFocusPropertyId) == 1) out.push_back(text(f.Get(), UIA_AutomationIdPropertyId));
        return out;
    }
    std::uint64_t focusMoves() const { return a.automation_ ? a.automation_->stats().focusMoves : 0; }
    HRESULT invoke(std::wstring_view id) {
        Microsoft::WRL::ComPtr<IInvokeProvider> button;
        const auto f = part(id);
        if (!f.Get()) return E_FAIL;
        const auto hr = pattern(f.Get(), UIA_InvokePatternId, button);
        return FAILED(hr) ? hr : button->Invoke();
    }
    static std::wstring wide(std::string_view s) {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<std::size_t>(std::max(n, 0)), L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }
    // The card's UIA tree on the intro (cycle 0): one dialog with its parts,
    // focus, hit test on the projected button, identity across refreshes.
    void automationIntro() {
        auto* root = group();
        step.check(a.automationRefresh(root) && a.automationCount() == 1 && !a.automationRefresh(root), "The card is one UIA dialog under the owner's group");
        const auto all = parts();
        const auto rows = a.flow_->list().size();
        step.check(all.size() == 6 + rows && rows == mig::macImportStores.size(), "Dialog, title, message, list with every data set, two buttons");
        const auto dialog = all.front();
        step.check(text(dialog.Get(), UIA_NamePropertyId) == wide(content().title) && text(dialog.Get(), UIA_HelpTextPropertyId) == wide(content().message) &&
                   integer(dialog.Get(), UIA_ControlTypePropertyId) == UIA_PaneControlTypeId && integer(dialog.Get(), 30174) == 1,
                   "The dialog is named by the card title, helped by its message");
        Fragment parent;
        step.check(SUCCEEDED(dialog->Navigate(NavigateDirection_Parent, &parent)) && sameObject(parent.Get(), root), "The dialog's parent is the owner's group");
        const auto title = navigate(dialog.Get(), NavigateDirection_FirstChild), confirm = navigate(dialog.Get(), NavigateDirection_LastChild);
        step.check(text(title.Get(), UIA_AutomationIdPropertyId) == L"macImport.title" && text(confirm.Get(), UIA_AutomationIdPropertyId) == L"macImport.confirm" &&
                   text(confirm.Get(), UIA_NamePropertyId) == wide(content().buttons[1]) && integer(confirm.Get(), UIA_ControlTypePropertyId) == UIA_ButtonControlTypeId,
                   "Title first, the confirm button last");
        const auto list = part(L"macImport.list");
        const auto firstRow = navigate(list.Get(), NavigateDirection_FirstChild);
        step.check(integer(list.Get(), UIA_ControlTypePropertyId) == UIA_ListControlTypeId && integer(firstRow.Get(), UIA_ControlTypePropertyId) == UIA_ListItemControlTypeId &&
                   text(firstRow.Get(), UIA_NamePropertyId) == wide(a.flow_->list()[0].label) && sameObject(navigate(firstRow.Get(), NavigateDirection_Parent).Get(), list.Get()),
                   "The list holds one item per data set");
        const auto cancel = part(L"macImport.cancel");
        step.check(integer(cancel.Get(), UIA_HasKeyboardFocusPropertyId) == 1 && integer(confirm.Get(), UIA_HasKeyboardFocusPropertyId) == 0 &&
                   integer(confirm.Get(), UIA_IsEnabledPropertyId) == 1, "Cancel holds the card's keyboard focus first");
        // Focus events (AutomationFocusChanged, raised for listening clients
        // only; counted always): the card appearing focuses Cancel once.
        Fragment focused;
        step.check(focusMoves() == 1 && SUCCEEDED(a.automation_->focused(&focused)) && sameObject(focused.Get(), cancel.Get()) &&
                   focusedParts() == std::vector<std::wstring>{L"macImport.cancel"}, "The card's appearance moves the UIA focus to Cancel once");
        step.check(!a.automationRefresh(root) && focusMoves() == 1, "An unchanged refresh raises no focus event");
        key(VK_TAB);
        step.check(!a.automationRefresh(root) && integer(confirm.Get(), UIA_HasKeyboardFocusPropertyId) == 1 && integer(cancel.Get(), UIA_HasKeyboardFocusPropertyId) == 0,
                   "Tab moves the focus property without restructuring");
        step.check(focusMoves() == 2 && focusedParts() == std::vector<std::wstring>{L"macImport.confirm"}, "Tab raises one focus event on the newly focused button");
        // UIA SetFocus moves the card's own focus like Tab, with its event.
        const auto requests = a.automation_->stats().focusRequests;
        step.check(SUCCEEDED(cancel->SetFocus()) && a.state_.focused() == 0 && a.automation_->stats().focusRequests == requests + 1 && focusMoves() == 3 &&
                   integer(cancel.Get(), UIA_HasKeyboardFocusPropertyId) == 1 && integer(confirm.Get(), UIA_HasKeyboardFocusPropertyId) == 0,
                   "SetFocus on a button moves the card's keyboard focus and announces it");
        step.check(SUCCEEDED(cancel->SetFocus()) && focusMoves() == 3, "SetFocus on the focused button announces nothing again");
        step.check(navigate(cancel.Get(), NavigateDirection_Parent)->SetFocus() == S_OK && title->SetFocus() == UIA_E_INVALIDOPERATION &&
                   integer(title.Get(), UIA_IsKeyboardFocusablePropertyId) == 0 && a.state_.focused() == 0 && focusMoves() == 3,
                   "The dialog keeps its button's focus; text parts are not keyboard focusable");
        step.check(SUCCEEDED(confirm->SetFocus()) && a.state_.focused() == 1 && focusMoves() == 4, "SetFocus reaches the confirm button");
        key(VK_TAB);
        step.check(!a.automationRefresh(root) && a.state_.focused() == 0 && focusMoves() == 5, "Tab returns to Cancel with its focus event");
        // The hit test uses the projection pointer input uses.
        const auto p = a.card_->actionPoint(1);
        POINT origin{};
        step.check(p && ClientToScreen(static_cast<HWND>(a.window_), &origin), "The confirm button projects");
        const double scale = a.metrics_.scale > 0 ? a.metrics_.scale : 1.0;
        Fragment hit;
        step.check(SUCCEEDED(a.automationFromPoint(origin.x + p->x * scale, origin.y + p->y * scale, &hit)) && sameObject(hit.Get(), confirm.Get()),
                   "A screen hit test on the projected button finds its part");
        UiaRect bounds{};
        step.check(SUCCEEDED(confirm->get_BoundingRectangle(&bounds)) && bounds.width > 0 && bounds.height > 0 && integer(confirm.Get(), UIA_IsOffscreenPropertyId) == 0,
                   "Buttons have screen bounds");
    }
};

// The relink ledger's owner API (core/migration/mac_import_relink.hpp) as the
// module owners call it after their own relinks, removals and loads - by
// store and record id - over the imported synthetic root's ledger. The
// targets are explicit synthetic Windows paths below the coverage root
// (never opened) and one packaged application id.
void MacImportArea::relinkOwnerCalls(app::ModuleCoverage& step) {
    const auto ledgerPath = destination_ / "Migration" / "relink.json";
    const auto bytes = [&] { return ehud::data::detail::readFile(ledgerPath, mig::MacRelinkLedger::maximumBytes); };
    const auto rows = mig::MacRelinkLedger(destination_).unresolved();
    const auto first = [&](std::string_view store) -> const mig::MacRelinkItem* {
        for (const auto& row : rows) if (row.store == store) return &row;
        return nullptr;
    };
    const auto target = [&](std::string_view name) { return utf8(coverageRoot_ / "Relinked" / utf8Path(name)); };
    std::size_t changed{};
    // Without a ledger (a root that never imported) nothing is written.
    const auto fresh = coverageRoot_ / "NoImport";
    fs::create_directories(fresh);
    step.check(mig::resolveMacRelink(fresh, {mig::relinkNotesMedia, ehud::data::makeUUID()}, target("a.png")) == mig::MacRelinkChange{} &&
               !fs::exists(fresh / "Migration"), "Owner calls without an import write nothing");
    fs::remove_all(fresh);
    if (const auto* note = first(mig::relinkNotesMedia)) {
        // Notes: the footer relink saved the note; its id names the row.
        const auto c = mig::resolveMacRelink(destination_, {mig::relinkNotesMedia, note->recordID}, target("Note picture.png"));
        step.check(c.resolved >= 1, "A Notes relink resolves the note's row");
        changed += c.resolved;
        const auto settled = bytes();
        step.check(mig::resolveMacRelink(destination_, {mig::relinkNotesMedia, note->recordID}, target("Other.png")).resolved == 0 && bytes() == settled,
                   "Repeating the call writes nothing");
    }
    if (const auto* attachment = first(mig::relinkArchiveMedia)) {
        // Archive: the attachment is named by its Mac object, not its position.
        const auto c = mig::resolveMacRelink(destination_, {mig::relinkArchiveMedia, attachment->recordID, &attachment->macReference}, target("Attachment.png"));
        step.check(c.resolved >= 1, "An Archive relink resolves the attachment's row");
        changed += c.resolved;
    }
    if (const auto* shortcut = first(mig::relinkAppShortcut)) {
        const auto c = mig::resolveMacRelink(destination_, {mig::relinkAppShortcut, shortcut->recordID}, "shell:AppsFolder\\Synthetic.Coverage_8wekyb3d8bbwe!App");
        step.check(c.resolved == 1, "A shortcut relinked to a packaged application resolves its row");
        changed += c.resolved;
    }
    if (const auto* item = first(mig::relinkShelfItem)) {
        const auto c = mig::dismissMacRelink(destination_, {mig::relinkShelfItem, item->recordID});
        step.check(c.dismissed == 1, "A removed shelf item dismisses its row");
        changed += c.dismissed;
    }
    if (first(mig::relinkReaderBook)) {
        // Reader on load: books still holding their Mac locator stay listed
        // (nothing written); a book the owner now names by a Windows file
        // resolves with it.
        const auto settled = bytes();
        std::size_t asked{};
        const auto unchanged = mig::reconcileMacRelinks(destination_, mig::relinkReaderBook, [&](const mig::MacRelinkItem&) { ++asked; return mig::RelinkRecordCheck{}; });
        step.check(asked >= 1 && unchanged.resolved == 0 && unchanged.dismissed == 0 && unchanged.open == asked && bytes() == settled,
                   "A reconcile that finds every record unchanged writes nothing");
        const auto c = mig::reconcileMacRelinks(destination_, mig::relinkReaderBook, [&](const mig::MacRelinkItem&) {
            return mig::RelinkRecordCheck{mig::RelinkRecordState::relinked, target("Book.epub")};
        });
        step.check(c.resolved == asked && c.open == 0, "A reconcile resolves books the Reader already relinked");
        changed += c.resolved;
    }
    const mig::MacRelinkLedger after(destination_);
    step.check(changed >= 1 && after.unresolved().size() == rows.size() - changed && after.items().size() >= rows.size(),
               "The owner calls changed exactly their rows; the card's relink list shrinks with them");
}

void MacImportArea::coverage(app::ModuleCoverage& step) {
    if (step.module) return; // presents no HUD module
    Driver d{*this, step, step.time};
    step.check(coverageMode() && host_.mode() == ApplicationAreaMode::hiddenCoverage && !coverageRoot_.empty() && destination_.parent_path() == coverageRoot_ / "Imported",
               "Hidden coverage imports into the area's own synthetic destination");
    step.check(coverageRestart_ && host_.find<app::DataRootRestart>(app::dataRootRestartService) == coverageRestart_.get(),
               "The host leaves the restart service to the coverage stand-in outside production");
    step.check(!flow_->presented() && !state_.presented() && !flow_->busy(), "The import card is idle between cycles");
    const auto exportRoot = coverageRoot_ / "Export";
    d.settle(); // the construction-time sweep and outcome read
    step.check(swept_ && swept_->failed == 0 && (step.cycle > 0 || (swept_->candidates == 0 && swept_->removed == 0)), "The startup sweep ran on the shared executor");
    // The Mac-generated golden export, materialized once (synthetic records
    // written by the unchanged Mac stores and the exporter script).
    const auto bytes = ehud::data::detail::readFile(goldenFixture_, 64 * 1024 * 1024);
    step.check(bytes.has_value(), "The golden export fixture is declared");
    const auto golden = Json::parse(*bytes, 64 * 1024 * 1024);
    if (!fs::exists(exportRoot)) {
        for (const auto& [path, encoded] : golden["files"].object()) {
            const auto decoded = mig::base64Decode(encoded.string());
            step.check(decoded && (mig::validExportPath(path) || path == "manifest.json"), "Golden export entries are valid");
            mig::files::writeNewFile(exportRoot / utf8Path(path), *decoded);
        }
        fs::create_directories(coverageRoot_ / "NotAnExport");
    }
    const auto& expected = golden["expected"];
    const auto rootName = utf8(destination_.filename());
    const auto clean = [&] {
        for (const auto& entry : fs::directory_iterator(destination_.parent_path()))
            if (utf8(entry.path().filename()).find(".import-") != std::string::npos) return false;
        return true;
    };
    const auto backups = [&] {
        std::size_t n{};
        for (const auto& entry : fs::directory_iterator(destination_.parent_path()))
            if (utf8(entry.path().filename()).starts_with(rootName + ".backup-")) ++n;
        return n;
    };
    const auto stateOf = [&](const fs::path& folder) {
        for (const auto& f : mig::inspectMacImportWork(destination_)) if (f.folder == folder) return f.state;
        return mig::MacImportWorkState::unrecognized;
    };
    // Review, then the restart handoff through the stand-in host service.
    const auto restartFromReview = [&] {
        const auto requests = coverageRestart_->requests();
        d.click(1);
        step.check(flow_->step() == MacImportStep::restarting && coverageRestart_->requests() == requests + 1 && coverageRestart_->pending() &&
                   !state_.enabled()[0] && !state_.enabled()[1], "Import hands the reviewed session to the host's restart service");
    };

    if (step.cycle == 0) {
        coverageInProcess_ = true;
        d.open();
        d.automationIntro();
        d.key(VK_ESCAPE);
        d.hidden("Escape closes the intro and releases the card");
        (void)automationRefresh(d.group());
        step.check(automationCount() == 0, "A hidden card lists no UIA parts");
        const auto moves = d.focusMoves();
        d.open();
        // With a client connected, the card's first frame lists the dialog
        // and announces its focused button without another content event.
        step.check(automationCount() == 1 && d.focusMoves() == moves + 1 && d.focusedParts() == std::vector<std::wstring>{L"macImport.cancel"},
                   "A reopened card appears in UIA on its first frame with Cancel focused");
        (void)automationRefresh(d.group());
        const auto dialog = d.part(L"macImport.dialog");
        d.stage(exportRoot, true);
        d.review(expected);
        step.check(flow_->residency().session && mig::macImportSessionsAlive() == 1, "The review holds the one staged session for the commit");
        (void)automationRefresh(d.group());
        step.check(sameObject(d.part(L"macImport.dialog").Get(), dialog.Get()) && d.text(dialog.Get(), UIA_NamePropertyId) == Driver::wide(d.content().title),
                   "The dialog keeps its UIA identity across steps and follows the title");
        {
            const auto row = d.text(d.part(L"macImport.row.0").Get(), UIA_NamePropertyId);
            const auto label = Driver::wide(flow_->list()[0].label), value = Driver::wide(flow_->list()[0].value);
            step.check(!value.empty() && row.size() > label.size() + value.size() && row.starts_with(label) && row.ends_with(value), "Review rows read label and value");
        }
        const auto rasterized = card_ ? card_->rasterizations() : 0;
        d.frames(.2);
        step.check(card_ && card_->rasterizations() == rasterized, "Warm frames re-pose the card without rasterizing");
        // Import through UIA Invoke: exactly the click's action.
        step.check(d.invoke(L"macImport.confirm") == S_OK, "UIA Invoke presses Import");
        step.check(flow_->step() == MacImportStep::committing || flow_->step() == MacImportStep::report, "Import starts the in-process commit");
        d.settle();
        step.check(flow_->step() == MacImportStep::report && fs::exists(destination_ / "Migration" / "import.json") && clean() && backups() == 0,
                   "The commit activated the imported root and removed its private folder");
        step.check(!flow_->residency().session && !flow_->residency().results && flow_->residency().outcome && mig::macImportSessionsAlive() == 0,
                   "The finished commit's session is gone; the report holds its outcome only");
        step.check(d.content().enabled[0] && d.content().buttons[0].find(std::to_string(expected["relinkItems"].integer())) != std::string::npos,
                   "The final report offers the relink list with its count");
        mig::MacRelinkLedger ledger(destination_);
        step.check(ledger.unresolved().size() == std::size_t(expected["relinkItems"].integer()), "Migration/relink.json lists every reference");
        d.click(0);
        step.check(flow_->step() == MacImportStep::relink && flow_->listSize() == ledger.unresolved().size() && !d.content().rows[0].label.empty(),
                   "The relink list names each reference and its module");
        d.key(VK_ESCAPE);
        step.check(flow_->step() == MacImportStep::report, "Escape returns from the relink list to the report");
        // The report list scrolls with the wheel when it outgrows the card;
        // rows outside the card are offscreen UIA list items.
        if (flow_->listSize() > macImportCardRows) {
            (void)automationRefresh(d.group());
            const auto last = d.part(L"macImport.row." + std::to_wstring(flow_->listSize() - 1));
            UiaRect none{};
            step.check(d.integer(last.Get(), UIA_IsOffscreenPropertyId) == 1 && SUCCEEDED(last->get_BoundingRectangle(&none)) && none.width == 0,
                       "Rows below the card are offscreen");
            const auto p = card_->listPoint();
            step.check(p && wheel({p->x, p->y, -3, false, 0, 3}, d.t) && flow_->firstRow() > 0, "The wheel scrolls the report");
            (void)automationRefresh(d.group());
            step.check(d.integer(d.part(L"macImport.row.0").Get(), UIA_IsOffscreenPropertyId) == 1, "Scrolled-out rows become offscreen");
            d.key(VK_PRIOR);
            step.check(flow_->firstRow() == 0, "Page Up returns to the top");
        }
        (void)automationRefresh(d.group());
        const auto done = d.part(L"macImport.confirm");
        d.click(1);
        d.hidden("Done closes the report");
        UiaRect gone{};
        step.check(automationCount() == 0 && done->get_BoundingRectangle(&gone) == UIA_E_ELEMENTNOTAVAILABLE,
                   "A closed card disconnects its UIA parts");
        relinkOwnerCalls(step);
    } else if (step.cycle == 1) {
        coverageInProcess_ = false; // the production restart path
        step.check(fs::exists(destination_ / "Migration" / "import.json"), "Cycle 1 starts from the earlier import");
        d.open();
        d.stage(exportRoot);
        step.check(flow_->step() == MacImportStep::replace && d.content().enabled[1], "An earlier import needs explicit replacement");
        d.click(1);
        d.settle();
        d.review(expected);
        restartFromReview();
        step.check(!coverageRestart_->restartWith([] {}, [] {}) && coverageRestart_->refused() > 0, "A second restart cannot be requested while one is pending");
        // The host's save failed (reopenAfterSaveFailure): the quit was
        // cancelled, the work destroyed unrun, then `declined` ran.
        coverageRestart_->decline();
        d.frame();
        step.check(flow_->step() == MacImportStep::review && d.content().enabled[1] && !clean() && !coverageRestart_->pending(), "A declined restart keeps the reviewed import");
        restartFromReview();
        // The accepted quit: editors finish, `quitting` is broadcast, the
        // flush chain runs, then every owner is destroyed before the work.
        step.check(finishEditing(d.t) && !hostEvent({app::HostEvent::Kind::quitting, 0}, d.t) && flush(d.t), "The card never blocks the accepted quit");
        auto work = coverageRestart_->accept();
        d.endRun();
        const auto held = std::find_if(fs::directory_iterator(destination_.parent_path()), fs::directory_iterator{}, [&](const auto& entry) {
            return utf8(entry.path().filename()).starts_with(rootName + ".import-");
        });
        step.check(work && held != fs::directory_iterator{} && stateOf(held->path()) == mig::MacImportWorkState::live && mig::macImportSessionsAlive() == 1,
                   "After the teardown only the host's work holds the reviewed import, still live");
        work(); // main.cpp: once, while no owner has the root open
        work = {};
        step.check(mig::macImportSessionsAlive() == 0, "The finished restart work released its session");
        const auto outcome = mig::readMacImportOutcome(destination_);
        step.check(outcome && outcome->state == mig::MacImportOutcome::State::completed && !outcome->backup.empty() && backups() == 1 && clean(),
                   "The post-shutdown commit kept a backup, switched roots and recorded its outcome");
        // Leftovers of earlier runs that ended while they owned a folder
        // (synthetic): the next run's startup sweep removes only the
        // importer's own stale folders.
        const auto token = [](unsigned n) { return "C0VE4A6E-0000-4000-8000-00000000000" + std::to_string(n); };
        const auto abandoned = mig::MacImportWorkLock::create(destination_, token(1), 1760000000.0).folder(); // released at once
        mig::files::writeNewFile(abandoned / "source" / "EndfieldCharge" / "Notes" / "notes.sqlite3", "private copy");
        mig::files::writeNewFile(abandoned / "root" / "settings.json", "{}");
        const auto activated = mig::MacImportWorkLock::create(destination_, token(2), 1760000000.0).folder();
        mig::files::writeNewFile(activated / utf8Path(mig::macImportActivationJournal), mig::encodeMacImportActivation(rootName, utf8(utf8Path(outcome->backup).filename())));
        mig::files::writeNewFile(activated / "source" / "copy.bin", "private copy");
        const auto empty = mig::macImportWorkPath(destination_, token(3));
        fs::create_directories(empty);
        const auto unmarked = mig::macImportWorkPath(destination_, token(4));
        mig::files::writeNewFile(unmarked / "user.txt", "not the importer's");
        auto live = mig::MacImportWorkLock::create(destination_, token(5), 1760000000.0);
        d.startRun();
        step.check(swept_ && swept_->candidates == 5 && swept_->removed == 3 && swept_->live == 1 && swept_->unrecognized == 1 && swept_->interrupted == 0 && swept_->failed == 0,
                   "The next run sweeps abandoned, activated and empty work folders only");
        step.check(!fs::exists(abandoned) && !fs::exists(activated) && !fs::exists(empty) && fs::exists(unmarked / "user.txt") && live.held() && fs::exists(live.folder()) &&
                   backups() == 1 && mig::readMacImportOutcome(destination_) == outcome, "Unmarked and live folders, backups and the outcome note are untouched");
        fs::remove_all(unmarked); // the user's own folder, removed by the driver only
        step.check(live.remove(), "A live import removes its own folder");
        step.check(flow_->step() == MacImportStep::report && flow_->outcomeRecorded() && state_.presented(), "The next run shows the recorded final report once");
        step.check(!d.content().rows[0].value.empty() && d.content().rows[0].value == utf8(utf8Path(outcome->backup).filename()), "The report names the backup folder");
        d.click(0);
        step.check(flow_->step() == MacImportStep::relink && flow_->listSize() == std::size_t(expected["relinkItems"].integer()), "The relink list is read from the imported root");
        d.click(1);
        d.hidden("Done closes the recorded report");
        step.drain(); // the note removal runs on the shared executor
        step.check(!fs::exists(mig::macImportOutcomePath(destination_)) && clean(), "Done forgets the recorded outcome");

        // The host's pre-open recovery (recoverMacImportWork, before any
        // owner opens the root) over a second synthetic root: a process that
        // ended between the two renames of an activation.
        const auto recovery = coverageRoot_ / "Recovery";
        const auto root = recovery / "EndfieldHUD";
        const auto backup = recovery / "EndfieldHUD.backup-20260101T000000Z-C0VE4A6E";
        mig::files::writeNewFile(backup / "settings.json", "{\"previous\":true}");
        const auto interrupted = mig::MacImportWorkLock::create(root, token(6), 1760000000.0).folder();
        mig::files::writeNewFile(interrupted / utf8Path(mig::macImportActivationJournal), mig::encodeMacImportActivation("EndfieldHUD", utf8(backup.filename())));
        mig::files::writeNewFile(interrupted / "root" / "Migration" / "import.json", "{}");
        const auto listed = mig::inspectMacImportWork(root);
        step.check(listed.size() == 1 && listed.front().state == mig::MacImportWorkState::interrupted && listed.front().backup == backup, "An interrupted activation is recognized");
        {
            mig::MacImportWorkSweep sweep(root);
            sweep.run();
            step.check(sweep.stats().interrupted == 1 && fs::exists(interrupted / "root") && fs::exists(backup), "The sweep never removes an interrupted activation");
        }
        const auto restored = mig::recoverMacImportWork(root, 1760000000.0);
        const auto note = mig::readMacImportOutcome(root);
        step.check(restored.restored == 1 && !fs::exists(backup) && mig::files::readFile(root / "settings.json", 64) == "{\"previous\":true}" && note &&
                   note->state == mig::MacImportOutcome::State::failed, "Recovery moves the previous data back and records why");
        mig::MacImportWorkSweep sweep(root);
        sweep.run();
        step.check(sweep.stats().removed == 1 && !fs::exists(interrupted), "The rolled-back folder is then swept");
        fs::remove_all(recovery);
    } else {
        coverageInProcess_ = true;
        const auto before = backups();
        d.open();
        d.stage(coverageRoot_ / "NotAnExport");
        step.check(flow_->step() == MacImportStep::failed && !d.content().detail.empty() && d.content().enabled[0], "An invalid folder fails with the importer's reason");
        (void)automationRefresh(d.group());
        step.check(d.part(L"macImport.detail").Get() && !d.part(L"macImport.list").Get(), "The failure's detail text replaces the list in UIA");
        d.click(0);
        d.hidden("Close dismisses the failure");
        d.open();
        d.choose(exportRoot);
        step.check(flow_->step() == MacImportStep::staging, "A second staging starts");
        d.click(0);
        step.check(flow_->step() == MacImportStep::cancelling || flow_->step() == MacImportStep::hidden, "Cancel stops the staging");
        d.settle();
        d.hidden("A cancelled staging closes the card");
        step.check(clean() && backups() == before, "A cancelled staging leaves no private folder and no backup");
        // Keyboard: Tab to Choose Folder, Return opens the dialog, Escape
        // cancels it (its cancelled completion returns to the intro).
        d.open();
        d.key(VK_TAB);
        d.key(VK_RETURN);
        step.check(flow_->step() == MacImportStep::choosing && chooserPending_, "Tab and Return choose a folder");
        d.key(VK_ESCAPE);
        step.check(chooserCancelled_, "Escape cancels the dialog");
        chooserPending_ = chooserCancelled_ = false;
        flow_->folderChosen(true, false, {});
        flowChanged();
        d.frame();
        step.check(flow_->step() == MacImportStep::intro, "A cancelled dialog returns to the intro");
        d.key(VK_ESCAPE);
        d.hidden("Escape closes the intro");
        // A session end while the restart is pending (sessionEnding:
        // declineRestart, runRestartDeclined, then the run stops): the work is
        // destroyed unrun and the owner's teardown discards the import.
        coverageInProcess_ = false;
        d.open();
        d.stage(exportRoot);
        step.check(flow_->step() == MacImportStep::replace, "The earlier import asks for replacement");
        d.click(1);
        d.settle();
        restartFromReview();
        coverageRestart_->decline();
        d.frame();
        step.check(flow_->step() == MacImportStep::review && !clean(), "The session end declines the restart");
        d.endRun();
        step.check(clean() && backups() == before && !fs::exists(mig::macImportOutcomePath(destination_)) && mig::macImportCompleted(destination_),
                   "A session end never runs the work: no backup, no outcome, the root unchanged");
        d.startRun();
        d.hidden("The next run has nothing to report");
        coverageInProcess_ = true;
    }
    step.check(!flow_->busy() && !pending_ && !sweep_ && !coverageRestart_->pending(), "Nothing is left running after the cycle");
    step.check(!flow_->presented() && !card_ && flow_->residency() == MacImportFlowResidency{} && mig::macImportSessionsAlive() == 0,
               "Nothing of the import stays resident after the cycle");
}
} // namespace

std::unique_ptr<app::ModuleOwner> macImportModule(const app::ApplicationModuleContext& context) {
    need(context.host && context.utility, "the host and the shared executor are required");
    fs::path destination;
    if (context.mode == ApplicationAreaMode::hiddenCoverage) {
        // Synthetic destination owned by this area; never the app data root.
        need(!context.coverageRoot.empty(), "hidden coverage needs the area's coverage root");
        const auto fixture = context.fixture ? context.fixture("macImport.goldenExport") : fs::path{};
        if (fixture.empty()) return nullptr;
        const auto imported = context.coverageRoot / "Imported";
        fs::create_directories(imported);
        destination = (imported / "EndfieldHUD").lexically_normal();
        return std::make_unique<MacImportArea>(context, destination, fixture);
    }
    if (context.dataRoot.empty() || !context.dataRoot.is_absolute()) return nullptr;
    destination = context.dataRoot.lexically_normal();
    if (destination.filename().empty()) destination = destination.parent_path();
    return std::make_unique<MacImportArea>(context, destination, fs::path{});
}
} // namespace endfield::areas
