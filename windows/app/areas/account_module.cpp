#include "app/areas/account_module.hpp"
#include "app/application_host.hpp"
#include "app/areas/account_coverage.hpp"
#include "app/areas/account_profile_link.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include "modules/boot_log.hpp"
#include "modules/hypergryph_account_identity_sync.hpp"
#include "modules/hypergryph_account_profile_relay.hpp"
#include "modules/work_mode.hpp"
#include "native/hud_design_automation.hpp"
#include "native/hud_design_text_metrics.hpp"
#include "native/hypergryph_account_vault.hpp"
#include "tools/account_linking_owner.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <stdexcept>
#include <string>

namespace endfield::app::areas {
namespace {
namespace fs = std::filesystem;
namespace h = modules::hypergryph;
using Entries = std::span<const native::LayerCompositionEntry>;
using ehud::data::Json;
constexpr std::string_view areaName = "account";
constexpr std::string_view gaugeResource = "account.gauge";
constexpr std::string_view coverageFixture = "account.coverage";
constexpr int route = 20; // after every built-in owner (0...19)

// The modifier bits of DesignOwnerCore::key (1 Alt, 2 Ctrl, 4 Shift, 8 Win).
// This is the HUD window's key dispatch: Win32 synchronizes the thread's
// GetKeyState with the key message being dispatched. Off that dispatch
// (hidden coverage's synthetic keys) a thread's GetKeyState follows the live
// keyboard of the interactive session, so those callers pass the modifier
// state explicitly.
std::uint32_t modifiers(const KeyEvent& e) {
    std::uint32_t bits{};
    if (e.alt || GetKeyState(VK_MENU) < 0) bits |= 1;
    if (GetKeyState(VK_CONTROL) < 0) bits |= 2;
    if (GetKeyState(VK_SHIFT) < 0) bits |= 4;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) bits |= 8;
    return bits;
}
h::WorkModeGaugeInput workModeInput(const modules::WorkModeController* work, double now) {
    const auto snapshot = work ? work->snapshot(now) : modules::WorkModeSnapshot{};
    return {snapshot.kind == modules::WorkModeKind::countdown, snapshot.duration, snapshot.elapsed};
}
// Coverage "Updated MM/dd HH:mm:ss": UTC, so the text never depends on the
// test machine's time zone.
h::LocalDateFields utcFields(h::Time time) {
    const double unix = h::toUnix(time);
    if (!std::isfinite(unix)) return {};
    const auto seconds = static_cast<std::time_t>(std::floor(unix));
    std::tm value{};
    if (gmtime_s(&value, &seconds) != 0) return {};
    return {value.tm_mon + 1, value.tm_mday, value.tm_hour, value.tm_min, value.tm_sec};
}

// Hidden coverage only: the host's per-owner UI Automation group
// (app/hud_automation.cpp) as the hooks see it: the fragments' parent and
// fragment root, whose children and hit test come from the owner hooks. The
// host's own root over these hooks is exercised by the area unit test.
class CoverageAutomationGroup final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot {
public:
    explicit CoverageAutomationGroup(const ModuleOwner& owner) : owner_(owner) {}
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
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        if (id == UIA_ControlTypePropertyId) { out->vt = VT_I4; out->lVal = UIA_GroupControlTypeId; }
        return S_OK;
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto n = owner_.automationCount();
        if (!n || (direction != NavigateDirection_FirstChild && direction != NavigateDirection_LastChild)) return S_OK;
        return owner_.automationChild(direction == NavigateDirection_FirstChild ? 0 : n - 1, out);
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* out) override { if (!out) return E_POINTER; *out = {}; return S_OK; }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP SetFocus() override { return S_OK; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = this;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        return owner_.automationFromPoint(x, y, out);
    }
    // As the host's group: the owner fragment reporting HasKeyboardFocus.
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        for (std::size_t i = 0; i < owner_.automationCount(); ++i) {
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> child;
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
            if (owner_.automationChild(i, &child) < 0 || !child || FAILED(child.As(&simple))) continue;
            VARIANT focused;
            VariantInit(&focused);
            const bool found = SUCCEEDED(simple->GetPropertyValue(UIA_HasKeyboardFocusPropertyId, &focused)) && focused.vt == VT_BOOL && focused.boolVal == VARIANT_TRUE;
            VariantClear(&focused);
            if (found) { *out = child.Detach(); return S_OK; }
        }
        return S_OK;
    }

private:
    ~CoverageAutomationGroup() = default;
    const ModuleOwner& owner_;
    std::atomic<ULONG> refs_{1};
};
// Same COM object (IUnknown identity).
bool sameObject(IUnknown* a, IUnknown* b) {
    Microsoft::WRL::ComPtr<IUnknown> x, y;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get() == y.Get();
}

// Hidden coverage only: synthetic providers, a pinned wall clock and a
// private profile store in the area's own coverage root.
struct CoverageWorld {
    AccountCoverageFixture fixture;
    SyntheticCommunityHost host;
    SyntheticAvatarLoader avatar;
    SyntheticLoginHost login;
    h::MemoryCredentialVault vault;
    h::Time wall;
    ehud::data::ProfileStore profileStore;
    h::ProfileStoreAccountSink profileSink;
    fs::path root;                                // the area's NEW coverage root (Account\ and the private profile below it)
    std::vector<std::string> events;              // accountAction metadata, in order
    h::AccountProfileSink* externalSink{};        // a Personal Profile area attaching in coverage (observed only)
    unsigned externalAttaches{};
    unsigned workModeOpens{};                     // presses on the wallet's Work Mode link (Mac 1.3.0)
    // The host's UIA group for this owner (one per registration, like the host's).
    Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> automationGroup;
    // Staged WebView2 SDK licence and notices (empty without the loader).
    fs::path webview2License, webview2Notice;
    // Coverage progress (module cycles repeat the same interactions after the first).
    bool linked{}, finished{};
    // The launch prewarm as the owner saw it: installed while concealed, then
    // the gauge work (rasters, uploads, redraws) of the first opening frame.
    struct Prewarm { bool prewarmed{}, installed{}, opened{}; std::uint64_t installedWork{}, openingWork{}; } prewarm;
    h::AccountPresentation reference;
    std::string referenceGauge;
    std::size_t referenceRequests{};
    CoverageWorld(AccountCoverageFixture value, const fs::path& directory)
        : fixture(std::move(value)), host(fixture.responses), avatar(host), login(host, fixture.nonce), wall(fixture.now),
          profileStore(directory), profileSink(profileStore, native::nativeCharacterRules()), root(directory) {}
};

class AccountModule final : public ModuleOwner, public AccountProfileLink {
public:
    AccountModule(const ApplicationModuleContext& c, fs::path resources, std::unique_ptr<CoverageWorld> world)
        : host_(*c.host), window_(c.window), clock_(c.clock), world_(std::move(world)) {
        message_ = host_.registerMessage();
        tools::AccountLinkingOwnerOptions o;
        o.window = c.window;
        o.serviceMessage = message_;
        // Production: Account\ below the versioned data root (the offline Mac
        // import writes Account\profile-cache.json there). Coverage: the
        // area's own NEW coverage root.
        o.dataRoot = world_ ? c.coverageRoot : c.dataRoot;
        o.gaugeResources = std::move(resources);
        o.clock = c.clock;
        o.invalidate = c.invalidate;
        auto record = c.recordEvent;
        CoverageWorld* w = world_.get();
        o.recordEvent = [record, w](std::string_view action) {
            if (w) w->events.emplace_back(action);
            if (record) record(modules::EventKind::accountAction, {{"action", std::string(action)}});
        };
        o.profile = &relay_;
        ApplicationHost* host = c.host;
        // The runtime download page opens once the HUD (and the login above
        // it) closed, never behind the HUD's topmost band.
        o.openLink = [host](std::string_view url) { host->openLinkAfterClose(std::string(url)); return true; };
        auto clock = c.clock;
        o.workMode = [host, clock] { return workModeInput(host->workMode(), clock()); };
        o.workModeRunning = [host, clock] {
            const auto* work = host->workMode();
            return work && work->snapshot(clock()).phase == modules::WorkModePhase::running;
        };
        // SystemHUDView: accountGauge.onOpenWorkMode = activateNavigationTarget(.module(.workMode)),
        // also when Work Mode is hidden in System > Modules (hidden means no list entry only).
        // Hidden coverage counts the request instead of leaving its Account step.
        o.openWorkMode = w ? std::function<void()>([w] { ++w->workModeOpens; }) : std::function<void()>([host] { host->selectModule(core::Module::workMode); });
        const auto& appearance = host_.appearance();
        o.language = appearance.language;
        o.accent = appearance.accentSRGB;
        o.reduceMotion = appearance.reduceMotion;
        // The page on the HUD's one design Context (the factory checked it).
        o.design = c.host->designContext();
        (void)c.host->designOwnerOptions(o.page, std::string(areaName), routing());
        if (w) {
            o.officialLogin = false;
            o.wallClock = [w] { return w->wall; };
            o.localDate = utcFields;
            o.wake = [] {}; // coverage routes the registered message itself
            o.synthetic = native::HypergryphAccountServicesOptions::Synthetic{&w->host, &w->vault, &w->avatar};
            o.login = &w->login;
        }
        owner_ = std::make_unique<tools::AccountLinkingOwner>(*c.utility, c.rasterizer, std::move(o));
        if (const auto* work = host_.workMode()) workRevision_ = work->revision();
        host_.publish<AccountProfileLink>(AccountProfileLink::key, *this);
        // The quit asks only once an open recovery popover is closed (Mac 1.3.0).
        quitPreemption_ = [this](double time) { return owner_ && owner_->dismissWalletPopover(time); };
        host_.publish<QuitPreemption>(quitPreemptionService, quitPreemption_);
        // The daily boot log's SANITY line (SystemHUDView.bootLogLines :3073-3075):
        // accountController.sanityPresentation() and the seconds from now to
        // its next point, both on the controller's wall clock.
        if (auto* sources = host_.find<modules::boot::BootLogSources>(modules::boot::bootLogSourcesKey))
            sources->sanity = [this](double) -> std::optional<modules::boot::BootLogSanity> {
                if (!owner_) return std::nullopt;
                const auto& controller = owner_->controller();
                const auto value = controller.sanityPresentation();
                if (!value) return std::nullopt;
                modules::boot::BootLogSanity line{int(value->current), int(value->maximum), std::nullopt};
                if (value->nextRecoveryAt) line.nextIn = *value->nextRecoveryAt - controller.now();
                return line;
            };
    }
    ~AccountModule() override {
        if (auto* sources = host_.find<modules::boot::BootLogSources>(modules::boot::bootLogSourcesKey)) sources->sanity = {};
        host_.withdraw(AccountProfileLink::key);
        host_.withdraw(quitPreemptionService);
        relay_.attach(nullptr);
        // The UIA peers borrow the owner; they are disconnected first.
        automation_.reset();
        if (world_) world_->automationGroup.Reset();
        owner_.reset();
    }
    AccountModule(const AccountModule&) = delete;
    AccountModule& operator=(const AccountModule&) = delete;

    // --- ModuleOwner ----------------------------------------------------------
    std::string_view name() const noexcept override { return areaName; }
    ModuleRouting routing() const noexcept override {
        // Captures (open account menu / recovery popover) precede ordinary
        // module input; the header wallet takes clicks with any module shown.
        ModuleRouting r;
        r.pointerCapture = route; r.pointer = route; r.wheelCapture = route; r.wheel = route;
        r.keyCapture = route; r.key = route; r.message = route; r.flush = route;
        return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::account; }
    void resize(const ClientMetrics& m) override { owner_->resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override { owner_->setAppearance(a, t); }
    void setOverlayVisible(bool v, double t) override { visible_ = v; owner_->setOverlayVisible(v, t); }
    void overlayClosing(double t) override { visible_ = false; owner_->overlayClosing(t); }
    void update(const ModuleFrame& f) override {
        syncWorkMode(f.time);
        // The wallet follows the header chrome, which fades with the canvas.
        owner_->update(f, f.opacity);
    }
    // Launch prewarm: the first opening's header wallet (and the face when
    // that opening shows Account) is posed hidden, so the host's upload right
    // after this installs its artwork and the first opening frame does not.
    void prewarm(const ModuleFrame& f) override {
        (void)syncWorkMode(f.time);
        owner_->prewarm(f);
        if (world_) world_->prewarm.prewarmed = true;
    }
    bool requiresFrames(double t) const override { return owner_->requiresFrames(t); }
    std::optional<double> nextWakeTime(double now) const override { return owner_->nextWakeTime(now); }
    bool deadline(double t) override { const bool work = syncWorkMode(t); return owner_->deadline(t) || work; }
    void upload(native::Renderer& r) override {
        owner_->upload(r);
        if (world_) observePrewarm();
    }
    Entries entries(core::Module m) override { return m == core::Module::account ? owner_->moduleEntries() : Entries{}; }
    Entries floatingEntries() override { return owner_->headerEntries(); }
    void collected(native::Renderer& r) override { owner_->collected(r); }
    void release(native::Renderer& r) override { owner_->release(r); }
    bool covers(core::Point p) const override { return owner_->covers(p); }
    bool pointerLocked() const override { return owner_->pointerLocked(); }
    bool capturesPointer() const override { return owner_->capturesPointer(); }
    bool capturesWheel() const override { return owner_->capturesWheel(); }
    bool capturesKeys() const override { return owner_->capturesKeys(); }
    bool pointer(const PointerEvent& e, double t) override { return owner_->pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return owner_->wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return key(e, modifiers(e), t); }
    std::optional<ModuleCursor> cursorAt(core::Point p) const override { return owner_->cursorAt(p); }
    bool message(const NativeMessage& m, double t) override {
        if (m.message == message_ && (!m.window || m.window == window_)) automationWake_ = false;
        return owner_->message(m, t);
    }
    void focus(bool v, double t) override { if (!v) owner_->cancelInteraction(t); }
    void cancelInteraction(double t) override { owner_->cancelInteraction(t); }
    bool flush(double t) override { return owner_->flush(t); }
    bool hostEvent(const HostEvent& e, double t) override {
        using K = HostEvent::Kind;
        switch (e.kind) {
        case K::timeChanged: case K::systemSettingsChanged:
            // "Updated MM/dd HH:mm:ss" follows the local time zone.
            owner_->localTimeChanged(t); return e.kind == K::timeChanged;
        case K::resumed: {
            // The wall clock moved during sleep: recovery points and the
            // automatic refresh are re-evaluated once, on the shared clock.
            const bool work = syncWorkMode(t);
            return owner_->deadline(t) || work;
        }
        case K::overlayConcealed:
            // The HUD finished closing: a face that is not shown parks its
            // rasters (the next show rebuilds them); the wallet stays.
            owner_->concealed(t);
            return syncWorkMode(t);
        default: return syncWorkMode(t);
        }
    }
    void coverage(ModuleCoverage& step) override {
        if (!world_) return;
        if (step.module) moduleStep(step); else areaStep(step);
    }
    // Files dragged over the Account face: SystemHUDView.draggingUpdated has
    // no account target, so the drag shows no operation (drop() keeps the
    // default refusal) and the face changes nothing.
    bool dropHover(std::span<const std::string>, std::optional<core::Point>, double) override { return false; }

    // --- UI Automation (ModuleOwner hooks) -------------------------------------
    // One AutomationTree (native/hud_design_automation.hpp) under this
    // owner's group of the HUD window's root provider (app/hud_automation.hpp):
    // the page's items while it takes input, then the wallet's stand-ins.
    // Created on the first refresh, i.e. only once a UIA client asked the HUD;
    // refreshed on content events only.
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override {
        bool rebuilt = false;
        if (!automation_ || automationRoot_ != root) {
            rebuilt = automation_ != nullptr;
            automation_.reset();
            automation_ = std::make_unique<native::AutomationTree>(automationHost(root));
            automationRoot_ = root;
        }
        return automation_->refresh() || rebuilt;
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

    // Logical client rect -> physical client pixels -> screen pixels.
    std::optional<core::Rect> screenRect(core::Rect r) const {
        const double scale = host_.metrics().scale;
        POINT origin{0, 0};
        if (!(scale > 0) || !ClientToScreen(static_cast<HWND>(window_), &origin)) return std::nullopt;
        return core::Rect{origin.x + r.x * scale, origin.y + r.y * scale, r.width * scale, r.height * scale};
    }
    native::AutomationTreeHost automationHost(IRawElementProviderFragmentRoot* root) {
        native::AutomationTreeHost view;
        view.root = root;
        view.nodes = [this] { return owner_->accessibleNodes(); };
        view.toScreen = [this](core::Rect r, std::uint8_t space) -> std::optional<core::Rect> {
            const auto client = owner_->accessibleClientRect(r, space);
            return client ? screenRect(*client) : std::nullopt;
        };
        view.invoke = [this](std::string_view id) { return owner_->performAccessible(id, host_.now()); };
        view.focus = view.invoke;
        view.setValue = [this](std::string_view id, double value) { return owner_->setAccessibleValue(id, value, host_.now()); };
        view.expand = [this](std::string_view id, bool expand) { return owner_->expandAccessible(id, expand, host_.now()); };
        view.changed = [this] { wakeAfterAutomation(); };
        return view;
    }

    // --- AccountProfileLink ---------------------------------------------------
    void attachProfile(h::AccountProfileSink* sink) override {
        // Hidden coverage keeps account sync on its private store; another
        // area's coverage state is never written.
        if (world_) { world_->externalSink = sink; world_->externalAttaches += sink != nullptr; return; }
        relay_.attach(sink);
    }
    void profileLoaded(const std::optional<std::string>& avatar) override { if (!world_) owner_->profileAvatarBaseline(avatar); }
    void profileAvatarChanged(const std::optional<std::string>& avatar) override { if (!world_) owner_->profileAvatarChanged(avatar); }

private:
    ApplicationHost& host_;
    void* window_{};
    std::function<double()> clock_;
    std::uint32_t message_{};
    std::unique_ptr<CoverageWorld> world_;
    h::AccountProfileRelay relay_;
    std::unique_ptr<tools::AccountLinkingOwner> owner_;
    QuitPreemption quitPreemption_;
    std::uint64_t workRevision_{};
    bool visible_{}; // setOverlayVisible / overlayClosing (hidden coverage's prewarm probe)
    std::unique_ptr<native::AutomationTree> automation_;
    IRawElementProviderFragmentRoot* automationRoot_{}; // held (AddRef'd) by automation_
    bool automationWake_{};                             // a UIA press posted the registered message

    // A UIA press runs inside a COM call, outside the host's input routes. The
    // registered message brings the host's content refresh (frame demand,
    // deadlines, the UIA tree) exactly as a routed click does. One at a time;
    // hidden coverage routes it through ModuleCoverage::message itself.
    void wakeAfterAutomation() {
        if (automationWake_) return;
        if (world_) { automationWake_ = true; return; }
        automationWake_ = PostMessageW(static_cast<HWND>(window_), message_, 0, 0) != FALSE;
    }
    // The module's key route with the modifier state supplied: key() above
    // (the HUD dispatch) and hidden coverage's synthetic keys.
    bool key(const KeyEvent& e, std::uint32_t modifierBits, double t) { return owner_->key(e, modifierBits, t); }

    bool syncWorkMode(double t) {
        const auto* work = host_.workMode();
        const auto revision = work ? work->revision() : 0;
        if (revision == workRevision_) return false;
        workRevision_ = revision;
        owner_->workModeChanged(t);
        return true;
    }
    h::HypergryphAccountController& controller() { return owner_->controller(); }

    // --- Hidden coverage --------------------------------------------------------
    // Hidden coverage only: the header wallet's work at the prewarm's upload
    // (HUD concealed) and at the first opening frame's upload.
    std::uint64_t gaugeWork() const {
        const auto s = owner_->gauge().sceneStats();
        return s.artworkRasters + s.numberRasters + s.tooltipRasters + s.popoverLoads + s.groupUploads + s.groupRedraws;
    }
    void observePrewarm() {
        auto& p = world_->prewarm;
        if (!p.prewarmed || p.opened) return;
        if (!visible_) { p.installed = owner_->gauge().sceneStats().groupUploads > 0; p.installedWork = gaugeWork(); return; }
        p.opened = true;
        p.openingWork = gaugeWork() - p.installedWork;
    }
    struct Driver {
        AccountModule& self;
        ModuleCoverage& step;
        CoverageWorld& w;
        double t;
        // One hidden frame, then the design Context's deadlines (page swaps,
        // the confirm callback) on the coverage clock.
        void frame(double dt = .05) { t += dt; step.present(t); if (auto* c = self.host_.designContext()) (void)c->advance(t); }
        std::vector<std::string> pageIDs() const {
            std::vector<modules::design::AccessibilityItem> items;
            canvas().accessibilityItems(items);
            std::vector<std::string> out;
            for (const auto& i : items) out.push_back(i.id);
            return out;
        }
        std::size_t pageItems() const { return pageIDs().size(); }
        // Utility completions, then the registered wake through the owners'
        // message chain, until the controller is idle.
        void settle() {
            for (unsigned n = 0; n < 32; ++n) {
                step.drain();
                if (!w.host.queued()) { if (!self.controller().hasActiveRequest()) break; continue; }
                step.check(step.message(self.message_, 0, 0), "The account wake reaches its owner through the registered message");
            }
            step.drain();
            step.check(!w.host.queued() && !self.controller().hasActiveRequest() && !w.host.activeRequests(), "Account work settles on the coverage clock");
            frame();
        }
        modules::AccountCanvas& canvas() const { return self.owner_->canvas(); }
        // An enabled control (page, header action or open menu row) by its
        // accessibility item: its box rect.
        std::optional<core::Rect> control(std::string_view id) const {
            std::vector<modules::design::AccessibilityItem> items;
            canvas().accessibilityItems(items);
            for (const auto& c : items) if (c.id == id) return c.enabled ? std::optional(c.rect) : std::nullopt;
            return std::nullopt;
        }
        // A box point in logical client coordinates through the live plane.
        core::Point canvasPoint(core::Point local) {
            const auto r = self.owner_->page().clientRect({local.x, local.y, 0, 0});
            step.check(r.has_value(), "Account page points project through the module plane");
            return {r->x, r->y};
        }
        bool press(core::Point p) {
            t += .05;
            const bool down = self.pointer({PointerKind::down, PointerButton::left, p.x, p.y}, t);
            (void)self.pointer({PointerKind::up, PointerButton::left, p.x, p.y}, t);
            frame();
            return down;
        }
        bool click(std::string_view id) {
            const auto rect = control(id);
            step.check(rect.has_value(), "The account control is available");
            return press(canvasPoint({rect->x + rect->width / 2, rect->y + rect->height / 2}));
        }
        // Synthetic keys carry their modifier state: never the live keyboard
        // of the interactive session the coverage happens to run in.
        bool keyDown(std::uint32_t vk, std::uint32_t modifierBits = 0) {
            t += .05;
            const bool handled = self.key({KeyKind::down, vk}, modifierBits, t);
            (void)self.key({KeyKind::up, vk}, modifierBits, t);
            frame();
            return handled;
        }
        std::optional<core::Point> gaugePoint(std::string_view id) const {
            for (const auto& a : self.owner_->gauge().accessibleActions())
                if (a.id == id) return self.owner_->gauge().clientPoint({a.rect.x + a.rect.width / 2, a.rect.y + a.rect.height / 2});
            return std::nullopt;
        }
        std::string expectedGauge() const {
            const auto value = self.controller().gaugeValue(workModeInput(self.host_.workMode(), self.clock_()));
            return value.value;
        }
        void settleAnimations() { for (unsigned n = 0; n < 24; ++n) frame(1. / 60); }
        // UI Automation through the ModuleOwner hooks: the peer under a
        // control's projected screen centre (owner hook and the group's hit
        // test agree), its AutomationId and Invoke.
        IRawElementProviderFragmentRoot* group() {
            if (!w.automationGroup) w.automationGroup.Attach(static_cast<IRawElementProviderFragmentRoot*>(new CoverageAutomationGroup(self)));
            return w.automationGroup.Get();
        }
        Microsoft::WRL::ComPtr<IRawElementProviderFragment> peer(std::string_view id) {
            std::optional<core::Rect> client;
            for (const auto& c : self.owner_->accessibleNodes()) if (c.id == id) client = self.owner_->accessibleClientRect(c.rect, c.space);
            step.check(client.has_value(), "The control has an accessibility peer");
            const auto screen = self.screenRect(*client);
            step.check(screen.has_value(), "The peer projects to screen pixels");
            const double x = screen->x + screen->width / 2, y = screen->y + screen->height / 2;
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> element, viaGroup;
            step.check(self.automationFromPoint(x, y, &element) == S_OK && element, "UIA hit testing through the owner hook finds the projected peer");
            step.check(SUCCEEDED(group()->ElementProviderFromPoint(x, y, &viaGroup)) && viaGroup.Get() == element.Get(), "The group's hit test reaches the same peer");
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
            VARIANT automationId;
            VariantInit(&automationId);
            step.check(SUCCEEDED(element.As(&simple)) && SUCCEEDED(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &automationId)) &&
                       automationId.vt == VT_BSTR && std::wstring_view(automationId.bstrVal) == std::wstring(id.begin(), id.end()),
                       "The hit peer carries the control's automation id");
            VariantClear(&automationId);
            return element;
        }
        // The automation id of the peer the group's GetFocus answers ("" for none).
        std::string focusedPeer() {
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> focused;
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
            if (FAILED(group()->GetFocus(&focused)) || !focused || FAILED(focused.As(&simple))) return {};
            VARIANT id;
            VariantInit(&id);
            std::string out;
            if (SUCCEEDED(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &id)) && id.vt == VT_BSTR && id.bstrVal)
                for (const wchar_t* c = id.bstrVal; *c; ++c) out.push_back(static_cast<char>(*c)); // ids are ASCII
            VariantClear(&id);
            return out;
        }
        // The listed peer with this automation id (ids are ASCII).
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> listed(std::string_view id) {
            for (std::size_t i = 0; i < self.automationCount(); ++i) {
                Microsoft::WRL::ComPtr<IRawElementProviderFragment> child;
                Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
                VARIANT name;
                VariantInit(&name);
                if (self.automationChild(i, &child) < 0 || !child || FAILED(child.As(&simple)) || FAILED(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &name))) continue;
                const bool match = name.vt == VT_BSTR && name.bstrVal && std::wstring_view(name.bstrVal) == std::wstring(id.begin(), id.end());
                VariantClear(&name);
                if (match) return simple;
            }
            return nullptr;
        }
        // The Toggle state a peer reports (none: not a toggle).
        std::optional<bool> toggleState(std::string_view id) {
            Microsoft::WRL::ComPtr<IUnknown> provider;
            Microsoft::WRL::ComPtr<IToggleProvider> toggle;
            ToggleState state{};
            const auto simple = listed(id);
            if (!simple || FAILED(simple->GetPatternProvider(UIA_TogglePatternId, &provider)) || !provider || FAILED(provider.As(&toggle)) || FAILED(toggle->get_ToggleState(&state)))
                return std::nullopt;
            return state == ToggleState_On;
        }
        // A peer's read-only UIA Value as UTF-8 (none: no Value pattern now).
        std::optional<std::string> valueText(std::string_view id) {
            Microsoft::WRL::ComPtr<IUnknown> provider;
            Microsoft::WRL::ComPtr<IValueProvider> pattern;
            BSTR value{};
            const auto simple = listed(id);
            if (!simple || FAILED(simple->GetPatternProvider(UIA_ValuePatternId, &provider)) || !provider || FAILED(provider.As(&pattern)) ||
                FAILED(pattern->get_Value(&value)) || !value)
                return std::nullopt;
            std::string out;
            const int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) { out.resize(static_cast<std::size_t>(n - 1)); WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), n, nullptr, nullptr); }
            SysFreeString(value);
            return out;
        }
        // A peer's UIA help text as UTF-8.
        std::string helpText(std::string_view id) {
            const auto simple = listed(id);
            VARIANT value;
            VariantInit(&value);
            std::string out;
            if (simple && SUCCEEDED(simple->GetPropertyValue(UIA_HelpTextPropertyId, &value)) && value.vt == VT_BSTR && value.bstrVal) {
                const int n = WideCharToMultiByte(CP_UTF8, 0, value.bstrVal, -1, nullptr, 0, nullptr, nullptr);
                if (n > 1) { out.resize(static_cast<std::size_t>(n - 1)); WideCharToMultiByte(CP_UTF8, 0, value.bstrVal, -1, out.data(), n, nullptr, nullptr); }
            }
            VariantClear(&value);
            return out;
        }
        HRESULT invoke(const Microsoft::WRL::ComPtr<IRawElementProviderFragment>& element) {
            Microsoft::WRL::ComPtr<IInvokeProvider> invoker;
            step.check(SUCCEEDED(element.As(&invoker)), "Account peers are buttons with Invoke");
            t += .05;
            const HRESULT hr = invoker->Invoke();
            // A successful press posts the registered wake; route it as the
            // host would (never SendMessage in coverage).
            if (self.automationWake_)
                step.check(step.message(self.message_, 0, 0) && !self.automationWake_, "A UIA press wakes the host through the registered message");
            frame();
            return hr;
        }
    };

    void fakeOfficialSite(Driver& d) {
        auto& w = *world_;
        auto* session = w.login.session();
        d.step.check(session && w.login.presentations() == 1, "Connect presents the official login (fake site host, no browser)");
        d.step.check(!session->allowNavigation(w.fixture.site, true, false), "Nothing navigates before explicit consent");
        d.step.check(session->consent(d.t), "Consent opens the official site");
        const auto bridge = h::login_policy::bridgeScript(w.fixture.region, session->nonce());
        d.step.check(bridge.find(session->nonce()) != std::string::npos && bridge.find("chrome.webview.postMessage") != std::string::npos,
                     "The WebView2 bridge script is bound to the session nonce and posts through chrome.webview");
        session->pageStarted(d.t);
        for (const auto& n : w.fixture.navigation)
            d.step.check(session->allowNavigation(n.url, n.mainFrame, false) == n.permits, "Navigation policy matches the Mac verdict");
        for (const auto& p : w.fixture.popups) {
            const bool allowed = session->allowPopup(p.url, p.source);
            d.step.check(allowed == p.permits, "Popup policy matches the Mac verdict");
            if (allowed) session->popupClosed();
        }
        session->pageFinished(true, "www.skland.com");
        std::optional<h::LoginResult> accepted;
        for (const auto& m : w.fixture.messages) {
            auto result = session->credentialMessage(Json::parse(m.body), m.mainFrame, m.frame, m.page);
            d.step.check(result.has_value() == m.session.has_value(), "Bridge messages are accepted exactly as on the Mac");
            if (result) {
                d.step.check(result->cred == m.session->cred && result->signingToken == m.session->signingToken &&
                             result->deviceID == m.session->deviceID && result->region == w.fixture.region, "The accepted session carries the page's credentials");
                accepted = std::move(result);
            }
        }
        d.step.check(w.login.finish(std::move(accepted), h::LoginFailure::cancelled), "The fake site finishes the presented session");
        const auto& tags = w.login.diagnostics();
        for (const char* tag : {"consented", "navigation-blocked", "popup-opened", "credential-rejected", "credential-accepted", "completed"})
            d.step.check(std::find(tags.begin(), tags.end(), tag) != tags.end(), "The login lifecycle records its fixed diagnostic tags");
        d.step.check(!w.login.isPresenting() && !w.login.session(), "The login session is disposed after sign-in");
    }

    // The pinned wall clock's step between the link's fetch and the Arknights
    // header's refresh of both cards (the controller's 5 s retry guard).
    static constexpr double headerFetchDelay = 6;
    void checkSnapshots(Driver& d) {
        auto& w = *world_;
        const auto& record = controller().record();
        const auto& roles = w.fixture.expected["roles"].array();
        d.step.check(record.roles.size() == roles.size(), "Bindings decode the Mac role list");
        for (std::size_t n = 0; n < roles.size(); ++n) {
            const auto& r = record.roles[n];
            d.step.check(r.id() == roles[n]["id"].string() && r.isDefault == roles[n]["isDefault"].boolean() &&
                         r.name == std::optional(roles[n]["name"].string()) && r.serverName == std::optional(roles[n]["serverName"].string()),
                         "Each role matches the Mac decode (identity, default, names)");
        }
        for (const char* game : {"endfield", "arknights"}) {
            const auto& expected = w.fixture.expected[game];
            const auto found = record.snapshots.find(expected["role"]["id"].string());
            d.step.check(found != record.snapshots.end(), "Both header games have a snapshot");
            const auto& s = found->second;
            const auto& stamina = expected["stamina"];
            d.step.check(s.stamina && s.stamina->current == stamina["current"].integer() && s.stamina->maximum == stamina["maximum"].integer() &&
                         s.stamina->fullRecoveryAt && std::abs(*s.stamina->fullRecoveryAt - std::strtod(stamina["fullRecoveryAt"].string().c_str(), nullptr)) < 1e-6 &&
                         std::abs(s.observedAt - (w.fixture.now + headerFetchDelay)) < 1e-6,
                         "Snapshot stamina matches the Mac decode at the pinned clock");
            if (expected.contains("level")) d.step.check(s.level == expected["level"].integer() && s.name == std::optional(expected["name"].string()), "Endfield card fields match");
        }
    }

    void moduleStep(ModuleCoverage& step) {
        auto& w = *world_;
        Driver d{*this, step, w, step.time};
        step.check(*step.module == core::Module::account && host_.selectedModule() == core::Module::account, "Account is the presented module");
        step.check(!owner_->officialLoginAvailable(), "Hidden coverage never creates a browser");
        d.frame();
        if (!w.linked) {
            // ---- First cycle: unlinked -> header mode -> official login -> roles.
            const auto& p = d.canvas().presentation();
            step.check(!p.isLinked && p.status == h::AccountPresentation::Status::disconnected && p.roles.empty() && !relay_.attached(),
                       "A fresh data root starts unlinked");
            step.check(owner_->gauge().model().value() == d.expectedGauge() && !owner_->gauge().model().canOpen(),
                       "The unlinked header wallet shows Work Mode minutes");
            step.check(w.prewarm.prewarmed && w.prewarm.installed && w.prewarm.opened && w.prewarm.openingWork == 0,
                       "The launch prewarm installed the hidden header wallet; the first opening frame rasterized and uploaded none of it");
            {
                const auto gauge = owner_->gauge().sceneStats();
                step.check(!gauge.popoverLoaded && !gauge.popoverUploaded && gauge.popoverLoads == 0,
                           "The recovery popover's card is not built before it first opens (most HUD sessions never open it)");
            }
            {
                // Live RC 128: a press on the wallet closed the HUD. The wallet
                // (header design point 562,2 to 730,44) lies almost entirely
                // outside the displayed circle (radius 310 around 500,320), so
                // an unconsumed press there is the host's background close.
                // The shown wallet is a control whatever it shows: its press is
                // consumed; without sanity data it is a link to Work Mode
                // (Mac 1.3.0 HUDAccountGauge.opensWorkMode), never a popover.
                namespace g = h::gauge_geometry;
                const auto before = controller().presentation();
                for (const core::Point local : {core::Point{g::bounds.width / 2, g::bounds.height / 2}, core::Point{g::bounds.width - 4, g::bounds.height / 2}}) {
                    const auto client = owner_->gauge().clientPoint(local);const auto opens = w.workModeOpens;
                    step.check(client && covers(*client) && d.press(*client) && !owner_->gauge().model().popoverOpen() && !capturesPointer() &&
                               controller().presentation() == before && w.workModeOpens == opens + 1 && owner_->gauge().model().opensWorkMode(),
                               "A press on the unlinked wallet (Work Mode minutes) is consumed and opens Work Mode");
                }
            }
            {
                // Mac 1.3.0 SystemHUDView.layoutAccountGaugeControls: the bar without
                // sanity data keeps its toggle button (HUDAccountGauge.accessibleActions:
                // the Work Mode link), labelled with the bar's accessibility label, no value.
                step.check(automationRefresh(d.group()) && automationCount() == d.pageItems() + 1 && d.listed("connect") && d.listed("region:0") && d.listed("region:1"),
                           "UIA lists the page's items through the owner hooks and the unlinked wallet's Work Mode link");
                {
                    const auto link = d.listed("wallet:toggle");
                    VARIANT name;
                    VariantInit(&name);
                    const auto& label = owner_->gauge().model().accessibilityLabel();
                    std::wstring expected(label.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, label.data(), static_cast<int>(label.size()), nullptr, 0), L'\0');
                    if (!expected.empty()) MultiByteToWideChar(CP_UTF8, 0, label.data(), static_cast<int>(label.size()), expected.data(), static_cast<int>(expected.size()));
                    const bool named = link && SUCCEEDED(link->GetPropertyValue(UIA_NamePropertyId, &name)) && name.vt == VT_BSTR && name.bstrVal &&
                                       !expected.empty() && std::wstring_view(name.bstrVal) == expected;
                    VariantClear(&name);
                    step.check(named && !d.valueText("wallet:toggle"), "The Work Mode link peer carries the wallet's label and no value");
                    const auto opens = w.workModeOpens;
                    step.check(d.invoke(d.peer("wallet:toggle")) == S_OK && w.workModeOpens == opens + 1 && !owner_->gauge().model().popoverOpen(),
                               "Invoking the unlinked wallet's peer opens Work Mode, no popover");
                }
                automationDisconnect();
                step.check(automationCount() == 0, "Disconnecting removes every peer");
            }
            // Mac 1.3.0 (HUDAccountHUDVerification L43-45, L86-89): the unlinked page
            // offers only the region switch and Connect; Global while unlinked
            // stays local and unlinked, each region keeping its own page.
            step.check(d.pageIDs() == std::vector<std::string>{"region:0", "region:1", "connect"}, "The unlinked page lists exactly region:0, region:1 and connect");
            step.check(d.click("region:1") && controller().presentation().region == h::AccountPresentation::RegionChoice::global &&
                       !controller().presentation().isLinked && w.host.requests().empty(), "Selecting Global while unlinked stays unlinked and local");
            d.settleAnimations();
            step.check(d.click("region:0") && controller().presentation().region == h::AccountPresentation::RegionChoice::china && w.host.requests().empty(),
                       "Mainland China comes back without a request");
            d.settleAnimations();
            step.check(d.click("connect") && w.login.isPresenting() && controller().isPresentingLogin(), "Connect opens the login host");
            fakeOfficialSite(d);
            d.settle();
            const auto& linked = controller().record();
            step.check(linked.linked && !linked.requiresReconnect && controller().presentation().status == h::AccountPresentation::Status::connected,
                       "The official session links the region");
            const auto saved = w.vault.load(w.fixture.region);
            const auto& session = *w.fixture.messages.back().session;
            step.check(saved && saved->cred == session.cred && saved->signingToken == session.signingToken && saved->deviceID == session.deviceID,
                       "Credentials reach the vault through the shared utility queue");
            step.check(w.host.requests().size() == 2 && !w.host.rejected(), "Bindings plus the selected Endfield role's card; nothing else is requested");
            d.settleAnimations(); // the linked page slides in
            // The linked page (verification L46-60): Disconnect, the region switch,
            // the sanity plate, the rows and the field chips; no Connect.
            for (const char* id : {"disconnect", "region:1", "sanity", "role", "header", "refresh", "syncProfile", "field:name", "field:avatar"})
                step.check(d.control(id).has_value(), "The linked page has its control");
            step.check(!d.control("connect"), "The linked page has no Connect");
            // The header's automatic fetch honours the 5 s retry guard after the
            // link's own fetch (refresh: lastAttempt, on the wall clock).
            w.wall += headerFetchDelay;
            step.check(d.click("header") && d.canvas().menuOpen() && capturesPointer() && capturesKeys(), "The header-mode menu captures input");
            step.check(d.click("header:arknights") && controller().headerMode() == h::AccountPresentation::HeaderMode::arknights &&
                       std::find(w.events.begin(), w.events.end(), "settings") != w.events.end(), "Choosing a header mode records accountAction settings");
            d.settle();
            // refresh (HypergryphAccountController.swift:372-386) fetches every role the
            // presentation needs once the 5 s guard passed: the selected Endfield
            // card again and the Arknights header's player info.
            step.check(w.host.requests().size() == 4 && !w.host.rejected() && w.host.requests()[2].path == w.host.requests()[1].path &&
                       w.host.requests()[3].path != w.host.requests()[1].path, "The Arknights header refreshes the needed cards once (Endfield and Arknights)");
            for (const auto& r : w.host.requests()) {
                const auto sign = r.header("sign");
                step.check(r.host == h::apiHost(w.fixture.region) && r.header("cred") == std::optional(session.cred) && r.header("dId") == session.deviceID &&
                           sign && sign->size() == 32, "Every request is signed with the issued token and device id");
            }
            checkSnapshots(d);
            step.check(std::find(w.events.begin(), w.events.end(), "linked") != w.events.end(), "Linking records accountAction linked");
            d.frame();
            step.check(owner_->gauge().model().value() == d.expectedGauge() && owner_->gauge().model().canOpen() &&
                       d.expectedGauge().find(" / 135") != std::string::npos, "The header wallet shows Arknights sanity");
            // Profile sync before the profile exists, then attach (replay).
            d.settleAnimations();
            step.check(d.click("syncProfile") && controller().cache().syncProfile && controller().gameSyncActive() && relay_.pending() && relay_.locked() == ehud::data::ProfileSyncFields::all(),
                       "Enabling sync keeps the update until the personal profile attaches");
            relay_.attach(&w.profileSink);
            const auto& profile = w.profileStore.value();
            step.check(w.profileStore.profileSyncLocked() && profile.gamePlayerID == std::optional<std::string>("4000500060") && profile.name == "Endmin" &&
                       profile.tag == "1234" && profile.permissionLevel == 41 && !profile.hasManualAwakeningDate && !relay_.pending(),
                       "Attaching replays the Endfield snapshot into the profile and locks it");
            // Back to Endfield: both snapshots are fresh, so no request.
            const auto before = w.host.requests().size();
            step.check(d.click("header") && d.click("header:endfield") && controller().headerMode() == h::AccountPresentation::HeaderMode::endfield,
                       "The header returns to Endfield");
            d.settle();
            step.check(w.host.requests().size() == before && owner_->gauge().model().value() == d.expectedGauge() &&
                       d.expectedGauge().find(" / 360") != std::string::npos, "Fresh snapshots are reused; the wallet shows Endfield sanity");
            // Manual refresh from the recovery popover (5 s guard on the wall clock).
            w.wall += 6;
            owner_->deadline(d.t);
            const auto toggle = d.gaugePoint("toggle");
            step.check(toggle && covers(*toggle) && d.press(*toggle) && owner_->gauge().model().popoverOpen() && capturesPointer(),
                       "The wallet opens its recovery popover");
            step.check(owner_->gauge().sceneStats().popoverLoaded && owner_->gauge().sceneStats().popoverUploaded,
                       "Its first opening builds and uploads the card");
            const auto refresh = d.gaugePoint("refresh");
            step.check(refresh.has_value() && d.press(*refresh), "The popover refresh control takes the click");
            d.settle();
            step.check(w.host.requests().size() == before + 1 && w.host.requests().back().path == h::endfieldCardPath(h::EndfieldCardRoute::authenticatedSelf) &&
                       w.events.back() == "synced", "Manual refresh fetches the selected Endfield card and records synced");
            step.check(d.keyDown(VK_ESCAPE) && !owner_->gauge().model().popoverOpen() && !capturesPointer(), "Escape closes the recovery popover");
            d.settleAnimations();
            w.linked = true;
            w.reference = controller().presentation();
            w.referenceGauge = owner_->gauge().model().value();
            w.referenceRequests = w.host.requests().size();
        }
        // ---- Every cycle: menus and popover open and close without side effects.
        step.check(controller().presentation() == w.reference && owner_->gauge().model().value() == w.referenceGauge, "The linked account is unchanged between cycles");
        // Mac 1.3.0 verification L120-146: the role menu lists role:<id> with the
        // selection marked; the wheel and keys stay with it; the first press
        // outside only closes it (the Region segment behind is not pressed).
        step.check(d.click("role") && d.canvas().menuOpen(), "The role menu opens");
        const auto& roles = controller().presentation().roles;
        step.check(!roles.empty() && d.control("role:" + roles.front().id).has_value(), "The role menu lists role:<id> rows");
        const auto row = *d.control("role:" + roles.front().id);
        const auto inside = d.canvasPoint({row.x + row.width / 2, row.y + row.height / 2});
        step.check(wheel({inside.x, inside.y, -1, false, 0, 3}, d.t += .05), "An open menu consumes the wheel");
        step.check(d.keyDown(VK_DOWN) && d.canvas().menuOpen(), "Arrow keys move the menu selection");
        const auto segment = *d.control("region:1");
        step.check(d.press(d.canvasPoint({segment.x + segment.width / 2, segment.y + segment.height / 2})) && !d.canvas().menuOpen() &&
                   controller().presentation() == w.reference, "The first press outside only closes the menu (the segment behind is not pressed)");
        d.settleAnimations();
        step.check(d.click("header") && d.canvas().menuOpen() && d.control("header:hidden").has_value(), "The header menu offers every mode");
        step.check(d.keyDown(VK_ESCAPE) && !d.canvas().menuOpen() && controller().presentation() == w.reference, "Escape closes only the menu");
        // Disconnect asks through the shared confirm bar (Mac verification L160-170);
        // Esc cancels without an unlink.
        step.check(d.click("disconnect"), "Disconnect is pressed");
        const auto& confirmState = host_.designContext()->overlay().confirmState();
        step.check(confirmState.isOpen() && confirmState.request()->id == "disconnect" && confirmState.request()->danger &&
                   confirmState.request()->message == h::regionChoiceTitle(controller().presentation().region, host_.appearance().language),
                   "Disconnect opens the confirm bar with the region as its message, danger");
        (void)host_.designContext()->overlay().handleKey({VK_ESCAPE});
        d.settleAnimations();
        step.check(!confirmState.request() && controller().record().linked && controller().presentation() == w.reference, "Esc cancels without an unlink");
        const auto toggle = d.gaugePoint("toggle");
        step.check(toggle && d.press(*toggle) && owner_->gauge().model().popoverOpen(), "The recovery popover opens");
        step.check(d.keyDown(VK_ESCAPE) && !owner_->gauge().model().popoverOpen(), "Escape closes it");
        // The quit's preemption (SystemHUDView.presentQuitConfirmation): an
        // open popover is dismissed instead; a shut one lets the quit ask.
        if (const auto* preempt = host_.find<QuitPreemption>(quitPreemptionService)) {
            step.check(toggle && d.press(*toggle) && owner_->gauge().model().popoverOpen() && (*preempt)(d.t += .05) &&
                       !owner_->gauge().model().popoverOpen() && !(*preempt)(d.t += .05), "A quit closes the open recovery popover instead of asking");
        } else step.check(false, "The quit preemption is published");
        {
            // The same controls through the ModuleOwner UIA hooks (Mac AX
            // press), under this owner's group as the host lists them.
            auto* group = d.group();
            (void)automationRefresh(group);
            const auto faceControls = d.pageItems();
            step.check(automationCount() == faceControls + 1, "UIA lists the page and the wallet");
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> first, last, parent, fromGroup;
            Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> fragmentRoot;
            step.check(automationChild(0, &first) == S_OK && first && SUCCEEDED(first->Navigate(NavigateDirection_Parent, &parent)) && sameObject(parent.Get(), group) &&
                       SUCCEEDED(first->get_FragmentRoot(&fragmentRoot)) && sameObject(fragmentRoot.Get(), group),
                       "Fragments are children of the owner's group, which is also their fragment root");
            step.check(SUCCEEDED(group->QueryInterface(IID_PPV_ARGS(&parent))) && SUCCEEDED(parent->Navigate(NavigateDirection_FirstChild, &fromGroup)) &&
                       fromGroup.Get() == first.Get() && automationChild(automationCount(), &last) < 0 && !last, "The group's children are the owner's fragments in order");
            const auto invokes = automation_->stats().invokes, expansions = automation_->stats().expansions;
            const auto presented = controller().presentation();
            step.check(d.focusedPeer().empty() && d.toggleState("syncProfile") == std::optional(presented.syncProfile) &&
                       d.toggleState("field:avatar") == std::optional(presented.syncAvatar) &&
                       d.toggleState("field:levels") == std::optional(presented.syncFields.contains(ehud::data::ProfileSyncField::levels)) && !d.toggleState("disconnect"),
                       "The page holds no keyboard focus; the sync toggle and field chips report their state through Toggle");
            // The role dropdown is a pop-up button (ComboBox): ExpandCollapse presses it.
            const auto role = d.peer("role");
            Microsoft::WRL::ComPtr<IExpandCollapseProvider> expander;
            step.check(SUCCEEDED(role.As(&expander)) && expander->Expand() == S_OK && d.canvas().menuOpen(), "Expanding the role pop-up opens its menu");
            // (the expansion's input change may already have re-listed the tree)
            (void)automationRefresh(group);
            step.check(presented.selectedRoleID && d.listed("role:" + *presented.selectedRoleID), "The open role menu's rows are listed");
            step.check(d.keyDown(VK_ESCAPE) && !d.canvas().menuOpen() && ((void)automationRefresh(group), automationCount() == faceControls + 1),
                       "Escape closes the menu; its rows leave the tree");
            step.check(!d.valueText("wallet:toggle"), "The closed wallet's peer has no value");
            step.check(d.invoke(d.peer("wallet:toggle")) == S_OK && owner_->gauge().model().popoverOpen() && ((void)automationRefresh(group), true) &&
                       automationCount() == faceControls + 2, "The wallet peer opens the popover and exposes its Refresh");
            const auto countdowns = owner_->gauge().accessibilityValue();
            step.check(countdowns && d.valueText("wallet:toggle") == countdowns && (host_.appearance().language != core::Language::english || countdowns->rfind("Next recovery ", 0) == 0),
                       "The open wallet's peer reports the recovery countdowns as its value (Mac accessibilityValue)");
            step.check(d.keyDown(VK_ESCAPE) && !owner_->gauge().model().popoverOpen(), "Escape closes the popover");
            (void)automationRefresh(group);
            step.check(!d.valueText("wallet:toggle"), "Closing the popover clears the value");
            step.check(automation_->stats().invokes == invokes + 1 && automation_->stats().expansions == expansions + 1 && controller().presentation() == w.reference,
                       "UIA presses change nothing else");
            // HUD concealed: every fragment disconnects; the next refresh
            // (HUD shown again) lists fresh ones under the same group.
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> current;
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> listed;
            VARIANT label;
            VariantInit(&label);
            (void)automationRefresh(group);
            step.check(automationChild(0, &current) == S_OK && SUCCEEDED(current.As(&listed)) && listed->GetPropertyValue(UIA_NamePropertyId, &label) == S_OK &&
                       label.vt == VT_BSTR, "A listed peer answers with its label");
            VariantClear(&label);
            automationDisconnect();
            step.check(automationCount() == 0 && listed->GetPropertyValue(UIA_NamePropertyId, &label) == UIA_E_ELEMENTNOTAVAILABLE, "Disconnecting ends every peer");
            step.check(automationRefresh(group) && automationCount() == faceControls + 1, "The next refresh connects fresh peers");
        }
        {
            // A file drag over the face (synthetic paths; nothing is read).
            const std::string dragged[]{(w.root / "dragged.png").string()};
            const auto over = d.canvasPoint({modules::design::metric::boxWidth / 2, modules::design::metric::boxHeight / 2});
            step.check(!dropHover(dragged, over, d.t) && !dropHover(dragged, std::nullopt, d.t) && !drop(dragged, over, d.t) &&
                       controller().presentation() == w.reference && !d.canvas().menuOpen(),
                       "A file dragged over the Account page is refused, as on the Mac (no drag operation, no drop)");
        }
        (void)pointer({PointerKind::leave, PointerButton::none, 0, 0}, d.t);
        d.settleAnimations();
        step.check(w.host.requests().size() == w.referenceRequests && controller().presentation() == w.reference && !w.host.rejected(),
                   "Browsing the account face sends nothing");
    }

    void areaStep(ModuleCoverage& step) {
        auto& w = *world_;
        Driver d{*this, step, w, step.time};
        step.check(!step.module && host_.find<AccountProfileLink>(AccountProfileLink::key) == this, "The profile link is published for the Personal Profile area");
        step.check(!owner_->message({window_, WM_APP + 0x0FFF, 0, 0}, d.t) && owner_->message({window_, message_, 0, 0}, d.t),
                   "Only the registered account wake is consumed by this owner");
        step.check(!w.host.queued() && !w.host.activeRequests() && !owner_->activeRequests(), "No account work is pending between steps");
#ifdef EHUD_ACCOUNT_WEBVIEW2_NOTICES
        if (step.cycle == 0) {
            // The static WebView2 loader is in EndfieldHUD.exe: its licence
            // and notices are staged in the verified package.
            const auto starts = [](const fs::path& file, std::string_view text) {
                const auto bytes = file.empty() ? std::nullopt : ehud::data::detail::readFile(file, 64 * 1024);
                return bytes && bytes->rfind(text, 0) == 0;
            };
            step.check(starts(w.webview2License, "Copyright (C) Microsoft Corporation") && starts(w.webview2Notice, "NOTICES AND INFORMATION"),
                       "The WebView2 SDK licence and notices ship in the package");
        }
#endif
        {
            // The HUD finished closing on another module (the host's
            // overlayConcealed broadcast, delivered here as the host does): a
            // page shown earlier keeps no artwork (DesignOwnerCore's Clipboard
            // policy); the next cycle shows it again with the same artwork
            // (the cycles' raster usage repeats exactly).
            const auto before = owner_->page().sceneStats(core::Module::account);
            (void)hostEvent({HostEvent::Kind::overlayConcealed, 0}, d.t);
            const auto after = owner_->page().sceneStats(core::Module::account);
            step.check(before && after, "The Account page's scene reports its residency");
            if (host_.selectedModule() != core::Module::account)
                step.check(after->rasterEntries == 0 && after->residentPixelBytes == 0,
                           "Once the HUD is closed on another module the Account page keeps no raster");
            else
                step.check(after->parks == before->parks, "A page shown at the close stays resident");
            const auto gauge = owner_->gauge().sceneStats();
            step.check(!owner_->gauge().model().popoverOpen() && !gauge.popoverLoaded && !gauge.popoverUploaded,
                       "The shut recovery popover keeps no card once the HUD closed; the wallet stays");
        }
        d.frame();
        if (step.cycle < 2 || w.finished || !w.linked) return;
        // ---- Last cycle, after the retained raster comparison: failure paths.
        const bool english = host_.appearance().language == core::Language::english;
        const auto says = [english](const std::string& text, std::string_view expected) { return english ? text == expected : !text.empty(); };
        // A login the page cannot finish keeps the existing link.
        {
            h::AccountAction reconnect;
            reconnect.kind = h::AccountAction::Kind::connect;
            controller().perform(reconnect);
            step.check(w.login.isPresenting(), "Reconnect presents the login again");
            step.check(w.login.finish(std::nullopt, h::LoginFailure::pageUnavailable), "The fake site fails to load");
            d.settle();
            step.check(controller().record().linked && says(controller().presentation().statusMessage, "Sign-in could not finish. Please try again."),
                       "An unfinished sign-in reports the Mac failure and keeps the link");
            // Windows: no WebView2 Runtime, and the explanation page was closed.
            controller().perform(reconnect);
            step.check(w.login.isPresenting() && w.login.finish(std::nullopt, h::LoginFailure::runtimeUnavailable), "The runtime explanation page closes");
            d.settle();
            step.check(controller().record().linked && !controller().isPresentingLogin() &&
                       controller().presentation().statusMessage == h::webView2RuntimeStrings(host_.appearance().language).status,
                       "A missing WebView2 Runtime is named on the face and the link is kept");
        }
        // Expired community session: cached sanity stays, a new sign-in is required.
        w.wall += 6;
        w.host.script(std::string(h::endfieldCardPath(h::EndfieldCardRoute::authenticatedSelf)), w.fixture.expired);
        controller().refresh(true);
        d.settle();
        const auto requests = w.host.requests().size();
        step.check(controller().record().requiresReconnect && says(controller().presentation().statusMessage, "Sign in again to continue syncing.") &&
                   owner_->gauge().model().value() == d.expectedGauge() && d.expectedGauge().find(" / 360") != std::string::npos &&
                   w.profileStore.profileSyncLocked(), "An expired session keeps cached sanity and the profile lock and asks for a new sign-in");
        w.wall += 900;
        controller().tick();
        d.settle();
        step.check(w.host.requests().size() == requests, "Nothing is requested while a sign-in is required");
        // Disconnect removes the credential and the region; the profile unlocks.
        h::AccountAction disconnect;
        disconnect.kind = h::AccountAction::Kind::disconnect;
        controller().perform(disconnect);
        d.settle();
        step.check(!controller().record().linked && !w.vault.load(w.fixture.region) && w.events.back() == "unlinked" && !w.profileStore.profileSyncLocked() &&
                   w.profileStore.value().name == "Endmin", "Disconnect removes the vault entry, logs unlinked and unlocks without restoring");
        step.check(owner_->gauge().model().value() == d.expectedGauge() && !owner_->gauge().model().canOpen(), "The wallet returns to Work Mode minutes");
        step.check(flush(d.t), "The flush writes the latest account cache");
        const auto file = w.root / "Account" / "profile-cache.json";
        const auto bytes = native::readBoundedFile(file, h::maximumCacheBytes);
        const auto cache = bytes ? h::decodeCacheBytes(*bytes) : std::nullopt;
        step.check(cache && !cache->records.count("mainland") && cache->syncProfile && cache->header == "endfield" && native::ownerOnly(file),
                   "The owner-only cache keeps the settings and drops the unlinked region (no credentials on disk)");
        d.frame();
        w.finished = true;
    }
};
std::unique_ptr<ModuleOwner> buildAccountModule(const ApplicationModuleContext& c) {
    if (!c.host || !c.utility || !c.window || !c.clock) throw std::invalid_argument("The account area needs the shared host, executor, HWND and clock");
    // The header wallet's pinned sprites and numerals (resources/account).
    // Without the verified package (development previews) the area is absent.
    const auto resources = c.resource ? c.resource(gaugeResource) : fs::path{};
    if (resources.empty()) return nullptr;
    // The page is a design canvas: without the HUD's design overlay (a host
    // without Notes' chrome) the area is absent like other redesigned modules.
    if (!c.host->designContext()) return nullptr;
    std::unique_ptr<CoverageWorld> world;
    if (c.mode == ApplicationAreaMode::developmentPreview) return nullptr; // never a real account in a development preview
    if (c.mode == ApplicationAreaMode::hiddenCoverage) {
        const auto fixture = c.fixture ? c.fixture(coverageFixture) : fs::path{};
        if (fixture.empty() || c.coverageRoot.empty()) throw std::runtime_error("Account coverage needs its declared fixture and coverage root");
        world = std::make_unique<CoverageWorld>(loadAccountCoverageFixture(fixture), c.coverageRoot);
        world->webview2License = c.resource("account.webview2License");
        world->webview2Notice = c.resource("account.webview2Notice");
    }
    auto owner = std::make_unique<AccountModule>(c, resources, std::move(world));
    // Its own ModuleOwner (not DesignModuleOwner): mark the screen (HOST-HOOKS "Areas").
    c.host->setDesignScreen(core::Module::account);
    return owner;
}
} // namespace
} // namespace endfield::app::areas

namespace endfield::areas {
std::unique_ptr<app::ModuleOwner> accountModule(const app::ApplicationModuleContext& context) { return app::areas::buildAccountModule(context); }
}
