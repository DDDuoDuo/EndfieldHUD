#include "app/areas/account_module.hpp"
#include "app/application_host.hpp"
#include "app/areas/account_coverage.hpp"
#include "app/areas/account_profile_link.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include "modules/hypergryph_account_identity_sync.hpp"
#include "modules/hypergryph_account_profile_relay.hpp"
#include "modules/work_mode.hpp"
#include "native/account_automation.hpp"
#include "native/hypergryph_account_vault.hpp"
#include "tools/account_linking_owner.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
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

h::CanvasStyle canvasStyle(const ModuleAppearance& a) {
    h::CanvasStyle style;
    style.dark = a.dark;
    style.accent = {a.accent[0], a.accent[1], a.accent[2], 1};
    return style;
}
// HUDAccountInteraction / SystemHUDView handle unmodified keys only.
bool modified(const KeyEvent& e) {
    return e.alt || GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0 || GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0;
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
    // Coverage progress (module cycles repeat the same interactions after the first).
    bool linked{}, finished{};
    h::AccountPresentation reference;
    std::string referenceGauge;
    std::size_t referenceRequests{};
    CoverageWorld(AccountCoverageFixture value, const fs::path& directory)
        : fixture(std::move(value)), host(fixture.responses), avatar(host), login(host, fixture.nonce), wall(fixture.now),
          profileStore(directory), profileSink(profileStore), root(directory) {}
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
        auto clock = c.clock;
        o.workMode = [host, clock] { return workModeInput(host->workMode(), clock()); };
        o.workModeRunning = [host, clock] {
            const auto* work = host->workMode();
            return work && work->snapshot(clock()).phase == modules::WorkModePhase::running;
        };
        const auto& appearance = host_.appearance();
        o.language = appearance.language;
        o.style = canvasStyle(appearance);
        o.reduceMotion = appearance.reduceMotion;
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
    }
    ~AccountModule() override {
        host_.withdraw(AccountProfileLink::key);
        relay_.attach(nullptr);
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
    void setAppearance(const ModuleAppearance& a, double t) override { owner_->setAppearance(a.language, canvasStyle(a), a.reduceMotion, t); }
    void setOverlayVisible(bool v, double t) override { owner_->setOverlayVisible(v, t); }
    void overlayClosing(double t) override { owner_->overlayClosing(t); }
    void update(const ModuleFrame& f) override {
        syncWorkMode(f.time);
        // The wallet follows the header chrome, which fades with the canvas.
        owner_->update(f.center, f.chrome, f.presentation, f.opacity, f.opacity, f.time);
    }
    bool requiresFrames(double t) const override { return owner_->requiresFrames(t); }
    std::optional<double> nextWakeTime(double now) const override { return owner_->nextWakeTime(now); }
    bool deadline(double t) override { const bool work = syncWorkMode(t); return owner_->deadline(t) || work; }
    void upload(native::Renderer& r) override { owner_->upload(r); }
    Entries entries(core::Module m) override { return m == core::Module::account ? owner_->moduleEntries() : Entries{}; }
    Entries floatingEntries() override { return owner_->headerEntries(); }
    void collected(native::Renderer& r) override { owner_->collected(r); }
    void release(native::Renderer& r) override { owner_->release(r); }
    bool covers(core::Point p) const override { return owner_->covers(p); }
    bool capturesPointer() const override { return owner_->capturesPointer(); }
    bool capturesWheel() const override { return owner_->capturesPointer(); }
    bool capturesKeys() const override { return owner_->capturesKeys(); }
    bool pointer(const PointerEvent& e, double t) override { return owner_->pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return owner_->wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return owner_->key(e, modified(e), t); }
    bool message(const NativeMessage& m, double t) override { return owner_->message(m, t); }
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
        default: return syncWorkMode(t);
        }
    }
    void coverage(ModuleCoverage& step) override {
        if (!world_) return;
        if (step.module) moduleStep(step); else areaStep(step);
    }

    // The host's UI Automation view of this owner (native/account_automation.hpp):
    // the Mac's transparent AX buttons over the projected face and the header
    // wallet. The HUD window's UIA root provider (host) lists the fragments.
    native::AccountAutomationHost automationHost(IRawElementProviderFragmentRoot* root) {
        native::AccountAutomationHost view;
        view.root = root;
        view.controls = [this] { return owner_->accessibleControls(); };
        view.toScreen = [this](core::Rect r) -> std::optional<core::Rect> {
            // Logical client -> physical client pixels -> screen pixels.
            const double scale = host_.metrics().scale;
            POINT origin{0, 0};
            if (!(scale > 0) || !ClientToScreen(static_cast<HWND>(window_), &origin)) return std::nullopt;
            return core::Rect{origin.x + r.x * scale, origin.y + r.y * scale, r.width * scale, r.height * scale};
        };
        view.invoke = [this](std::string_view id) { return owner_->performAccessible(id, host_.now()); };
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
    std::uint64_t workRevision_{};

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
    struct Driver {
        AccountModule& self;
        ModuleCoverage& step;
        CoverageWorld& w;
        double t;
        void frame(double dt = .05) { t += dt; step.present(t); }
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
        const h::AccountCanvasModel& canvas() const { return self.owner_->module().canvas(); }
        std::optional<core::Rect> control(std::string_view id) const {
            for (const auto& c : canvas().accessibleActions()) if (c.id == id) return c.enabled ? std::optional(c.rect) : std::nullopt;
            return std::nullopt;
        }
        core::Point canvasPoint(core::Point local) {
            const auto p = step.project(local, h::AccountCanvasModel::bounds.width, h::AccountCanvasModel::bounds.height);
            step.check(p.has_value(), "Account canvas points project through the module plane");
            return *p;
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
        bool keyDown(std::uint32_t vk) {
            t += .05;
            const bool handled = self.key({KeyKind::down, vk}, t);
            (void)self.key({KeyKind::up, vk}, t);
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
        // UI Automation over the real owner: the peer under a control's
        // projected screen centre, its AutomationId and Invoke.
        Microsoft::WRL::ComPtr<IRawElementProviderFragment> peer(native::NativeAccountAutomation& automation, std::string_view id) {
            std::optional<core::Rect> client;
            for (const auto& c : self.owner_->accessibleControls()) if (c.id == id) client = c.rect;
            step.check(client.has_value(), "The control has an accessibility peer");
            const auto screen = self.automationHost(nullptr).toScreen(*client);
            step.check(screen.has_value(), "The peer projects to screen pixels");
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> element;
            step.check(SUCCEEDED(automation.fromPoint(screen->x + screen->width / 2, screen->y + screen->height / 2, &element)) && element,
                       "UIA hit testing finds the projected peer");
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
            VARIANT automationId;
            VariantInit(&automationId);
            step.check(SUCCEEDED(element.As(&simple)) && SUCCEEDED(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &automationId)) &&
                       automationId.vt == VT_BSTR && std::wstring_view(automationId.bstrVal) == std::wstring(id.begin(), id.end()),
                       "The hit peer carries the control's automation id");
            VariantClear(&automationId);
            return element;
        }
        HRESULT invoke(const Microsoft::WRL::ComPtr<IRawElementProviderFragment>& element) {
            Microsoft::WRL::ComPtr<IInvokeProvider> invoker;
            step.check(SUCCEEDED(element.As(&invoker)), "Account peers are buttons with Invoke");
            t += .05;
            const HRESULT hr = invoker->Invoke();
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
                         std::abs(s.observedAt - w.fixture.now) < 1e-6, "Snapshot stamina matches the Mac decode at the pinned clock");
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
            {
                native::NativeAccountAutomation automation(automationHost(nullptr));
                step.check(automation.refresh() && automation.size() == d.canvas().accessibleActions().size(),
                           "UIA lists the face's peers; the unlinked wallet has none");
                step.check(d.invoke(d.peer(automation, "account:refresh")) == UIA_E_ELEMENTNOTENABLED, "A disabled peer refuses Invoke");
            }
            step.check(d.click("region") && d.canvas().menu() == h::AccountCanvasModel::Menu::region, "Region opens its shaped menu");
            step.check(d.keyDown(VK_ESCAPE) && !d.canvas().isPopoverOpen(), "Escape closes only the menu");
            step.check(d.click("header") && d.canvas().menu() == h::AccountCanvasModel::Menu::header && capturesPointer() && capturesKeys(),
                       "The header-mode menu captures input");
            step.check(d.click("header:arknights") && controller().headerMode() == h::AccountPresentation::HeaderMode::arknights &&
                       !w.events.empty() && w.events.back() == "settings", "Choosing a header mode records accountAction settings");
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
            step.check(w.host.requests().size() == 3 && !w.host.rejected(), "Bindings plus one card per header game; nothing else is requested");
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
            step.check(d.click("syncProfile") && controller().cache().syncProfile && controller().gameSyncActive() && relay_.pending() && relay_.locked() == true,
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
        step.check(d.click("role") && d.canvas().menu() == h::AccountCanvasModel::Menu::role, "The role menu opens");
        const auto list = d.canvas().listRect();
        const auto inside = d.canvasPoint({list.x + list.width / 2, list.y + list.height / 2});
        step.check(wheel({inside.x, inside.y, -1, false, 0, 3}, d.t += .05), "An open menu consumes the wheel");
        step.check(d.keyDown(VK_DOWN) && d.canvas().isPopoverOpen(), "Arrow keys move the menu selection");
        step.check(d.press(d.canvasPoint({20, 300})) && !d.canvas().isPopoverOpen() && controller().presentation() == w.reference,
                   "An outside click dismisses the menu without choosing");
        const auto toggle = d.gaugePoint("toggle");
        step.check(toggle && d.press(*toggle) && owner_->gauge().model().popoverOpen(), "The recovery popover opens");
        step.check(d.keyDown(VK_ESCAPE) && !owner_->gauge().model().popoverOpen(), "Escape closes it");
        {
            // The same controls through UI Automation (Mac AX press).
            native::NativeAccountAutomation automation(automationHost(nullptr));
            step.check(automation.refresh() && automation.size() == d.canvas().accessibleActions().size() + 1, "UIA lists the face and the wallet");
            const auto role = d.peer(automation, "account:role");
            step.check(d.invoke(role) == S_OK && d.canvas().menu() == h::AccountCanvasModel::Menu::role && automation.refresh(),
                       "Invoking the role peer opens its menu and restructures the tree");
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> stale;
            VARIANT name;
            VariantInit(&name);
            step.check(SUCCEEDED(role.As(&stale)) && stale->GetPropertyValue(UIA_NamePropertyId, &name) == UIA_E_ELEMENTNOTAVAILABLE,
                       "Peers hidden by the menu are disconnected");
            step.check(d.keyDown(VK_ESCAPE) && !d.canvas().isPopoverOpen() && automation.refresh(), "Escape closes the menu; the face peers return");
            step.check(d.invoke(d.peer(automation, "wallet:toggle")) == S_OK && owner_->gauge().model().popoverOpen() && automation.refresh() &&
                       automation.size() == d.canvas().accessibleActions().size() + 2, "The wallet peer opens the popover and exposes its Refresh");
            step.check(d.keyDown(VK_ESCAPE) && !owner_->gauge().model().popoverOpen(), "Escape closes the popover");
            step.check(automation.stats().invokes == 2 && controller().presentation() == w.reference, "UIA presses change nothing else");
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
    std::unique_ptr<CoverageWorld> world;
    if (c.mode == ApplicationAreaMode::developmentPreview) return nullptr; // never a real account in a development preview
    if (c.mode == ApplicationAreaMode::hiddenCoverage) {
        const auto fixture = c.fixture ? c.fixture(coverageFixture) : fs::path{};
        if (fixture.empty() || c.coverageRoot.empty()) throw std::runtime_error("Account coverage needs its declared fixture and coverage root");
        world = std::make_unique<CoverageWorld>(loadAccountCoverageFixture(fixture), c.coverageRoot);
    }
    return std::make_unique<AccountModule>(c, resources, std::move(world));
}
} // namespace
} // namespace endfield::app::areas

namespace endfield::areas {
std::unique_ptr<app::ModuleOwner> accountModule(const app::ApplicationModuleContext& context) { return app::areas::buildAccountModule(context); }
}
