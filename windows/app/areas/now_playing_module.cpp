#include "app/areas/now_playing_module.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "modules/event_log.hpp"
#include "modules/now_playing_accessibility.hpp"
#include "modules/now_playing_lyrics_sources.hpp"
#include "native/audio_runtime.hpp"
#include "native/now_playing_automation.hpp"
#include "native/now_playing_fixture.hpp"
#include "native/now_playing_volume.hpp"
#include "native/now_playing_web.hpp"
#include "tools/now_playing_preview.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>

namespace endfield::areas {
namespace {
namespace gpu = native;
using app::ApplicationAreaMode;
using modules::NowPlayingAction;
using modules::NowPlayingSliderKind;

bool hasPixels(const app::ClientMetrics& m) { return m.pixelWidth && m.pixelHeight && std::isfinite(m.scale) && m.scale > 0; }
bool modifierDown() { return (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000); }
modules::NowPlayingAppearance appearanceFor(const app::ModuleAppearance& a) {
    // HUDRuntimeAppearance.accent: the effective accent (ModuleAppearance::accent).
    return {a.dark, a.reduceMotion, a.language, a.accent};
}
core::Point center(core::Rect r) { return {r.x + r.width * .5, r.y + r.height * .5}; }

// One coalesced owner wake shared by the media-session service, the public
// downloads and the synthetic per-app audio runtime. Worker, WinRT and HTTP
// threads only set the flag and post the area's registered WM_APP message;
// message() (or the shared utilityCompleted broadcast) drains everything on
// the UI thread. Hidden coverage never posts: its steps route the same message
// through the owners' message chain on the synthetic clock.
struct Wake {
    HWND window{};
    UINT message{};
    bool post{};
    std::atomic<bool> pending{}, alive{true};
    void operator()() {
        if (!alive.load(std::memory_order_acquire) || pending.exchange(true, std::memory_order_acq_rel) || !post) return;
        if (!PostMessageW(window, message, 0, 0)) {
            // The caller (service/web) re-arms its own notice and retries on
            // the next result; the next broadcast drains meanwhile.
            pending.store(false, std::memory_order_release);
            throw std::runtime_error("Now Playing could not post its owner wake");
        }
    }
};

// Hidden-coverage stand-in for this owner's group under the HUD window's UIA
// root (app/hud_automation.cpp): it hands the ModuleOwner hooks a stable
// fragment root and forwards child navigation and hit tests to them exactly
// as the host's group does. It is never returned for a window, so no UIA
// client can reach it.
class CoverageGroup final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot {
public:
    explicit CoverageGroup(const app::ModuleOwner& owner) noexcept : owner_(owner) {}
    CoverageGroup(const CoverageGroup&) = delete;
    CoverageGroup& operator=(const CoverageGroup&) = delete;
    IRawElementProviderFragmentRoot* root() noexcept { return this; }
    ULONG references() const noexcept { return refs_.load(); }
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
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        AddRef();
        *out = this;
        return S_OK;
    }
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
// COM identity (the same object behind two interface pointers).
bool sameObject(IUnknown* a, IUnknown* b) {
    Microsoft::WRL::ComPtr<IUnknown> x, y;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get() == y.Get();
}
std::wstring automationID(IRawElementProviderFragment* fragment) {
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
    VARIANT value;
    VariantInit(&value);
    std::wstring id;
    if (fragment && SUCCEEDED(fragment->QueryInterface(IID_PPV_ARGS(&simple))) && SUCCEEDED(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &value)) &&
        value.vt == VT_BSTR && value.bstrVal)
        id.assign(value.bstrVal, SysStringLen(value.bstrVal));
    VariantClear(&value);
    return id;
}

class NowPlayingArea final : public app::ModuleOwner {
public:
    explicit NowPlayingArea(const app::ApplicationModuleContext& c)
        : host_(*c.host), utility_(*c.utility), window_(static_cast<HWND>(c.window)), mode_(c.mode), record_(c.recordEvent) {
        message_ = host_.registerMessage();
        wake_ = std::make_shared<Wake>();
        wake_->window = window_;
        wake_->message = message_;
        wake_->post = mode_ != ApplicationAreaMode::hiddenCoverage;
        const std::function<void()> wake = [state = wake_] { (*state)(); };

        // The service's clock runs on this thread only. The real provider
        // reads it from WinRT threads (the production clock is the thread-safe
        // QueryPerformanceCounter clock). The coverage fixture reads a mirror
        // of the synthetic clock instead, so no worker races the harness.
        gpu::NativeNowPlayingService::Factory provider;
        if (mode_ == ApplicationAreaMode::production) provider = gpu::windowsNowPlayingProvider(c.clock);
        else if (mode_ == ApplicationAreaMode::developmentPreview) provider = gpu::nowPlayingFixtureProvider(c.clock);
        else {
            fixtureClock_ = std::make_shared<std::atomic<double>>(c.clock());
            provider = gpu::nowPlayingFixtureProvider([clock = fixtureClock_] { return clock->load(std::memory_order_acquire); });
        }
        // Each activation (selection of a visible Now Playing) creates one provider.
        provider = [inner = std::move(provider), starts = starts_] { ++*starts; return inner(); };
        service_ = std::make_unique<gpu::NativeNowPlayingService>(utility_, std::move(provider), wake, c.clock);

        tools::NowPlayingPreviewOptions options;
        options.raster.pixelsPerPoint = 2;
        options.raster.paddingPoints = 1;
        options.appearance = appearanceFor(host_.appearance());
        options.changed = c.invalidate;
        if (mode_ == ApplicationAreaMode::production) {
            // Visible-only LRCLIB/NetEase lookups and catalog covers.
            web_ = std::make_shared<gpu::NowPlayingWeb>(gpu::windowsNowPlayingTransport(), wake, c.clock);
            options.web = web_;
            // The playing app's mixer route through the ONE shared audio worker;
            // without it the source "unavailable" submenu remains.
            if (auto* audio = host_.audio()) options.volume = gpu::nowPlayingAppVolume(*audio);
        } else {
            audioFixture_ = std::make_unique<gpu::NowPlayingAudioFixture>(wake);
            options.volume = gpu::nowPlayingAppVolume(*audioFixture_);
        }
        // Successful transport commands only; never the title (source onEvent).
        options.playback = [this](const modules::NowPlayingPlaybackEvent& event) {
            auto metadata = modules::nowPlayingPlaybackMetadata(event);
            ++playbackEvents_;
            lastPlayback_ = metadata;
            if (record_) record_(modules::EventKind::playbackAction, std::move(metadata));
        };
        preview_ = std::make_unique<tools::NowPlayingPreview>(*service_, c.rasterizer, utility_, std::move(options));
        resize(c.metrics);
    }
    ~NowPlayingArea() override {
        wake_->alive.store(false, std::memory_order_release);
        // UIA fragments call into the preview: disconnect and drop them first
        // (clients still holding one get UIA_E_ELEMENTNOTAVAILABLE).
        automation_.reset();
        automationRoot_ = nullptr;
        // The preview withdraws the audio vote and stops the provider; then
        // the downloads and the service go, all before the shared executor,
        // the AudioRuntime and the HWND.
        preview_.reset();
        audioFixture_.reset();
        web_.reset();
        service_.reset();
    }
    NowPlayingArea(const NowPlayingArea&) = delete;
    NowPlayingArea& operator=(const NowPlayingArea&) = delete;

    std::string_view name() const noexcept override { return "nowPlaying"; }
    app::ModuleRouting routing() const noexcept override {
        // The open volume menu captures like the other module menus; ordinary
        // input follows the built-in owners (0...19).
        app::ModuleRouting r;
        r.pointerCapture = 30; r.pointer = 30; r.key = 30; r.message = 30;
        return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::nowPlaying; }

    void resize(const app::ClientMetrics& m) override {
        if (!hasPixels(m)) return;
        preview_->resize(m);
        sized_ = true;
    }
    void setAppearance(const app::ModuleAppearance& a, double t) override { observe(t); preview_->setAppearance(appearanceFor(a), t); }
    void setOverlayVisible(bool visible, double t) override { observe(t); preview_->setOverlayVisible(visible, t); }
    void update(const app::ModuleFrame& f) override {
        observe(f.time);
        if (sized_) preview_->update(f.center, f.chrome, f.presentation, f.opacity, f.time);
    }
    bool requiresFrames(double t) const override { return preview_->requiresFrames(t); }
    std::optional<double> nextWakeTime(double) const override { return preview_->nextWakeTime(); }
    bool deadline(double t) override { observe(t); return preview_->deadline(t); }

    void upload(gpu::Renderer& r) override { preview_->upload(r); }
    std::span<const gpu::LayerCompositionEntry> entries(core::Module m) override {
        if (m != core::Module::nowPlaying) return {};
        return preview_->entries();
    }
    void release(gpu::Renderer& r) override { preview_->release(r); }

    // Source containsControl: only control faces (or the open volume menu)
    // claim the hit test; transparent canvas space still exposes navigation.
    bool covers(core::Point p) const override { return preview_->capturesPointer() || preview_->containsControl(p, 0); }
    bool pointerLocked() const override { return preview_->pointerLocked(); }
    bool capturesPointer() const override { return preview_->capturesPointer(); }
    bool pointer(const app::PointerEvent& e, double t) override { observe(t); return preview_->pointer(e, t); }
    bool key(const app::KeyEvent& e, double t) override { observe(t); return preview_->key(e, modifierDown(), t); }
    bool message(const app::NativeMessage& m, double t) override {
        if (m.message != message_) return false;
        observe(t);
        (void)deliver(t);
        return true;
    }
    void focus(bool focused, double t) override { if (!focused) cancelInteraction(t); }
    void cancelInteraction(double t) override { observe(t); preview_->cancelInteraction(t); }

    bool hostEvent(const app::HostEvent& e, double t) override {
        observe(t);
        switch (e.kind) {
        case app::HostEvent::Kind::utilityCompleted:
            // Service results, downloads, decoded artwork/lyrics, queue capacity.
            return deliver(t);
        case app::HostEvent::Kind::audioChanged:
            return (e.flags & app::HostEvent::audioApplications) && preview_->audioChanged(t);
        default:
            return false;
        }
    }
    void coverage(app::ModuleCoverage& step) override {
        if (step.module) presentedCoverage(step);
        else idleCoverage(step);
    }

    // UI Automation (app/module_owner.hpp): the projected source controls
    // (HUDNowPlayingInteraction.layoutAccessibility) under this owner's group
    // of the HUD window's root. The fragments are created on the host's first
    // refresh, i.e. once a UIA client asked the window; a new group (the host
    // rebuilt its tree) replaces them, so no fragment keeps a dead parent.
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override {
        if (!root) return false;
        bool replaced = false;
        if (automation_ && automationRoot_ != root) {
            replaced = automation_->size() > 0;
            automation_.reset(); // disconnects its fragments and releases the old group
            automationRoot_ = nullptr;
        }
        if (!automation_) {
            automation_ = std::make_unique<gpu::NativeNowPlayingAutomation>(automationHost(root));
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
    // The owner's UIA view (native/now_playing_automation.hpp): projected
    // source buttons and sliders while Now Playing accepts input.
    gpu::NowPlayingAutomationHost automationHost(IRawElementProviderFragmentRoot* root) {
        gpu::NowPlayingAutomationHost h;
        h.root = root;
        h.elements = [this] { return preview_->project({220, 220}) ? preview_->accessibility(host_.now()) : modules::NowPlayingAccessibility{}; };
        h.toScreen = [this](core::Rect r) { return screenRect(r); };
        h.invoke = [this](std::string_view id) {
            for (const auto action : {NowPlayingAction::lyrics, NowPlayingAction::previous, NowPlayingAction::playPause, NowPlayingAction::next, NowPlayingAction::volume})
                if (modules::nowPlayingActionID(action) == id) {
                    const double t = host_.now();
                    observe(t);
                    const bool performed = preview_->perform(action, t);
                    if (performed) automationActed();
                    return performed;
                }
            return false;
        };
        h.setValue = [this](std::string_view id, double value) {
            const double t = host_.now();
            observe(t);
            bool performed = false;
            if (id == "seek") performed = preview_->setSlider(NowPlayingSliderKind::seek, value, t);
            else if (id == "appVolume") performed = preview_->setSlider(NowPlayingSliderKind::volume, value, t);
            if (performed) automationActed();
            return performed;
        };
        return h;
    }
    // A UIA action is a content event no host call reports (App volume opens
    // or closes the menu, which adds or removes its slider): re-list
    // at once and, like the host after its own refresh, tell listening
    // clients that this group's children changed. Runs inside the element's
    // Invoke/SetValue, which keeps that element alive across the refresh.
    void automationActed() {
        if (!automation_ || !automation_->refresh() || !automationRoot_ || !UiaClientsAreListening()) return;
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> group;
        if (SUCCEEDED(automationRoot_->QueryInterface(IID_PPV_ARGS(&group))))
            (void)UiaRaiseStructureChangedEvent(group.Get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
    }

    // A 440-point canvas rect -> physical screen pixels through the current
    // module plane (bounds of its projected corners); none while not presented.
    std::optional<core::Rect> screenRect(core::Rect r) const {
        double left = std::numeric_limits<double>::infinity(), top = left, right = -left, bottom = -left;
        const std::array<core::Point, 4> corners{{{r.x, r.y}, {r.x + r.width, r.y}, {r.x, r.y + r.height}, {r.x + r.width, r.y + r.height}}};
        for (const auto corner : corners) {
            const auto p = preview_->project(corner);
            if (!p) return std::nullopt;
            left = std::min(left, p->x); top = std::min(top, p->y);
            right = std::max(right, p->x); bottom = std::max(bottom, p->y);
        }
        POINT origin{};
        if (!ClientToScreen(window_, &origin)) return std::nullopt;
        const double scale = host_.metrics().scale;
        return core::Rect{origin.x + left * scale, origin.y + top * scale, (right - left) * scale, (bottom - top) * scale};
    }
    void observe(double t) {
        if (!fixtureClock_ || !std::isfinite(t)) return;
        if (t > fixtureClock_->load(std::memory_order_relaxed)) fixtureClock_->store(t, std::memory_order_release);
    }
    // Drains the service, the downloads, decoded artwork/lyrics and (previews
    // and coverage) the synthetic audio publication; true when artwork changed.
    bool deliver(double t) {
        wake_->pending.store(false, std::memory_order_release);
        bool changed = preview_->utilityCompleted(t);
        if (audioFixture_ && audioFixture_->publish()) changed = preview_->audioChanged(t) || changed;
        return changed;
    }

    // --- Hidden coverage (synthetic fixture player and audio route only) -----
    // Delivers queued work on the coverage clock: owner wakes through the
    // registered message (the WM_APP route), executor completions through the
    // shared drain (the utilityCompleted broadcast).
    void settle(app::ModuleCoverage& step) {
        for (unsigned round = 0; round < 32; ++round) {
            utility_.waitIdle();
            if (wake_->pending.load(std::memory_order_acquire)) {
                step.check(step.message(message_, 0, 0), "The Now Playing wake reaches its owner through the registered message");
                continue;
            }
            if (!utility_.stats().completed) return;
            step.drain();
        }
        step.check(false, "Synthetic Now Playing work settles within bounded deliveries");
    }
    bool recorded(unsigned count, std::string_view action) const {
        const auto value = [&](std::string_view key) {
            const auto found = lastPlayback_.find(key);
            return found == lastPlayback_.end() ? std::string_view{} : std::string_view(found->second);
        };
        return playbackEvents_ == count && lastPlayback_.size() == 2 && value("action") == action && value("source") == "system";
    }
    void presentedCoverage(app::ModuleCoverage& step) {
        step.check(*step.module == core::Module::nowPlaying && fixtureClock_ && audioFixture_ && !web_,
                   "Hidden coverage presents Now Playing with the fixture player, a synthetic audio route and no network");
        double t = step.time;
        const auto present = [&](double seconds) { t += seconds; observe(t); step.present(t); };
        const auto& view = presentation();
        const auto title = [&] { return view.input().track ? view.input().track->title : std::string(); };
        const auto position = [&] { return view.input().track && view.input().track->position ? *view.input().track->position : -1.; };
        const auto playing = [&] { return view.input().track && view.input().track->isPlaying; };

        if (step.cycle == 0) startsBefore_ = *starts_ ? *starts_ - 1 : 0;
        settle(step);
        present(.5); // cover/caption crossfades settle
        step.check(*starts_ == startsBefore_ + step.cycle + 1 && service_->snapshot().active && view.active(),
                   "Each presentation of Now Playing starts exactly one provider");
        step.check(title() == "EndfieldHUD Fixture 1" && view.input().track->artist == "Local verification" && view.input().track->album == "Now Playing" &&
                   !view.input().failed && view.input().session != 0, "Fixture metadata reaches the presentation");
        step.check(std::abs(position() - 42) < 1e-9 && !playing() && view.durationText() == "3:30", "The fixture starts paused at 42 of 210 seconds");
        step.check(view.input().coverAvailable && preview_->artworkStats().decoded > 0, "The fixture cover decodes on the shared executor");
        step.check(view.lyricsSelected() && view.lyricRows()[1] == "Current", "Fixture timed lyrics show the current line");
        step.check(view.input().volumeAvailable && view.input().volume && std::abs(*view.input().volume - 1) < 1e-9 &&
                   audioFixture_->nowPlayingVisible() && audioFixture_->applications().size() == 1,
                   "The synthetic process route resolves through the per-app audio bridge");

        // Projection, hit testing and the UIA descriptors.
        const auto& play = view.actions()[static_cast<std::size_t>(NowPlayingAction::playPause)];
        const auto playPoint = step.project(center(play.rect), 440, 440);
        step.check(playPoint.has_value() && play.enabled, "The play control projects through the Now Playing plane");
        const auto uia = preview_->project(center(play.rect));
        step.check(uia && std::abs(uia->x - playPoint->x) < .05 && std::abs(uia->y - playPoint->y) < .05,
                   "UIA rectangles use the same projection as pointer input");
        const auto transparent = step.project({20, 200}, 440, 440);
        step.check(covers(*playPoint) && transparent && !covers(*transparent), "Only control faces claim the hit test over navigation");
        {
            const auto ax = preview_->accessibility(t);
            step.check(ax.status == "EndfieldHUD Fixture 1 \xc2\xb7 Local verification \xc2\xb7 Now Playing" && ax.buttons.size() == 5 &&
                       ax.sliders.size() == 1 && ax.sliders.front().id == "seek", "Accessible status, five buttons and the seek slider");
        }
        // UI Automation through the ModuleOwner hooks, as the HUD window's root
        // drives them: one stable group, refreshed on content events.
        auto* group = coverageGroup();
        (void)automationRefresh(group->root());
        step.check(automationCount() == 6 && !automationRefresh(group->root()),
                   "The UIA hooks list the five projected buttons and the seek slider under the owner's group");

        // Settled paused frames: no rasterization, provider work or deadline.
        {
            const auto scene = preview_->sceneStats();
            const auto decodes = preview_->artworkStats().submitted;
            const auto revision = service_->snapshot().revision;
            const auto starts = *starts_;
            for (unsigned frame = 0; frame < 12; ++frame) present(1. / 60);
            const auto after = preview_->sceneStats();
            step.check(after.localRasterUpdates == scene.localRasterUpdates && after.coverUploads == scene.coverUploads &&
                       preview_->artworkStats().submitted == decodes && service_->snapshot().revision == revision && *starts_ == starts &&
                       !wake_->pending.load() && !preview_->nextWakeTime() && !requiresFrames(t),
                       "Paused warm frames re-rasterize, decode, read, schedule and wake nothing");
        }

        if (step.cycle == 0) {
            // Transport commands record Event Log playbackAction once each (only
            // in the first cycle, so the Event Log stays identical afterwards).
            const auto events = playbackEvents_;
            step.check(pointer({app::PointerKind::down, app::PointerButton::left, playPoint->x, playPoint->y}, t) &&
                       pointer({app::PointerKind::up, app::PointerButton::left, playPoint->x, playPoint->y}, t), "Projected play click is consumed");
            settle(step);
            step.check(playing() && recorded(events + 1, "playPause"), "Play reaches the fixture and records playbackAction");
            step.check(preview_->nextWakeTime().has_value(), "Playing lyrics fold their next cue into the shared deadline");
            present(.4);
            step.check(preview_->key({app::KeyKind::down, VK_RIGHT}, false, t), "Right arrow seeks forward");
            settle(step);
            step.check(position() > 47 - 1e-6 && recorded(events + 2, "seek"), "Keyboard seek moves five seconds and records seek");
            step.check(preview_->key({app::KeyKind::down, VK_SPACE}, false, t), "Space toggles playback");
            settle(step);
            step.check(!playing() && recorded(events + 3, "playPause"), "Space pauses the fixture");
            step.check(!preview_->key({app::KeyKind::down, VK_SPACE}, true, t), "Modified transport keys are left to the HUD");

            // Seek drag on the projected rail: drag lock, then one command at release.
            const auto from = step.project({121, 388}, 440, 440), to = step.project({187, 388}, 440, 440);
            step.check(from && to && pointer({app::PointerKind::down, app::PointerButton::left, from->x, from->y}, t) && pointerLocked(),
                       "The seek rail starts a drag that locks the HUD pointer");
            present(1. / 60);
            step.check(pointer({app::PointerKind::move, app::PointerButton::none, to->x, to->y}, t) && playbackEvents_ == events + 3,
                       "Seek movement previews locally without a command");
            step.check(pointer({app::PointerKind::up, app::PointerButton::left, to->x, to->y}, t) && !pointerLocked(), "Release ends the drag");
            settle(step);
            step.check(std::abs(position() - 84) < 1 && recorded(events + 4, "seek"), "Seek release emits one successful command");

            // UI Automation over the real owner through its hooks: navigation
            // and hit tests enter through the group like a UIA client's.
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> first, parent, sibling, seekElement;
            Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> fragmentRoot;
            step.check(SUCCEEDED(group->Navigate(NavigateDirection_FirstChild, &first)) && first && automationID(first.Get()) == L"lyrics" &&
                       SUCCEEDED(first->Navigate(NavigateDirection_Parent, &parent)) && sameObject(parent.Get(), group->root()) &&
                       SUCCEEDED(first->get_FragmentRoot(&fragmentRoot)) && sameObject(fragmentRoot.Get(), group->root()) &&
                       SUCCEEDED(first->Navigate(NavigateDirection_NextSibling, &sibling)) && automationID(sibling.Get()) == L"previous",
                       "UIA fragments navigate in source order under the owner's group, their parent and fragment root");
            step.check(SUCCEEDED(group->Navigate(NavigateDirection_LastChild, &seekElement)) && automationID(seekElement.Get()) == L"seek",
                       "The seek slider is the group's last child");
            const auto nextScreen = screenRect(view.actions()[static_cast<std::size_t>(NowPlayingAction::next)].rect);
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> nextElement;
            step.check(nextScreen && SUCCEEDED(group->ElementProviderFromPoint(nextScreen->x + nextScreen->width / 2, nextScreen->y + nextScreen->height / 2, &nextElement)) &&
                       automationID(nextElement.Get()) == L"next", "UIA hit testing finds Next at its projected screen position");
            UiaRect bounds{};
            step.check(SUCCEEDED(nextElement->get_BoundingRectangle(&bounds)) && bounds.left == nextScreen->x && bounds.top == nextScreen->y &&
                       bounds.width == nextScreen->width && bounds.height == nextScreen->height, "UIA bounds are the projected control in screen pixels");
            Microsoft::WRL::ComPtr<IInvokeProvider> invokeNext;
            step.check(SUCCEEDED(nextElement.As(&invokeNext)) && SUCCEEDED(invokeNext->Invoke()), "UIA Invoke performs Next");
            settle(step);
            present(.3);
            step.check(title() == "EndfieldHUD Fixture 2" && std::abs(position()) < 1e-9 && recorded(events + 5, "next"), "Next advances the fixture track");
            step.check(preview_->perform(NowPlayingAction::previous, t), "Previous is available");
            settle(step);
            present(.3);
            step.check(title() == "EndfieldHUD Fixture 1" && recorded(events + 6, "previous"), "Previous returns to the first fixture track");
            // The consumed owner wakes were content events: the host refreshes.
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> seekAfter;
            Microsoft::WRL::ComPtr<IRangeValueProvider> seekRange;
            double step5{};
            step.check(!automationRefresh(group->root()) && SUCCEEDED(automationChild(automationCount() - 1, &seekAfter)) && sameObject(seekAfter.Get(), seekElement.Get()) &&
                       SUCCEEDED(seekAfter.As(&seekRange)) && SUCCEEDED(seekRange->get_SmallChange(&step5)) && step5 == 5,
                       "The UIA seek slider keeps its identity and the source 5 s increment");
            step.check(SUCCEEDED(seekRange->SetValue(42)), "UIA RangeValue seeks to an exact position");
            settle(step);
            step.check(std::abs(position() - 42) < 1e-9 && recorded(events + 7, "seek"), "Accessible seek restores 42 seconds");

            // The lyrics toggle (source lyrics action) is local presentation state.
            step.check(preview_->perform(NowPlayingAction::lyrics, t) && !view.lyricsSelected(), "Lyrics toggle hides the lyric rows");
            present(.3);
            step.check(preview_->perform(NowPlayingAction::lyrics, t) && view.lyricsSelected(), "Lyrics toggle restores the lyric rows");
            present(.3);

            // Playing-app volume: the per-app route through the audio bridge,
            // opened and set through UI Automation.
            const auto accepted = audioFixture_->accepted();
            const auto revision = audioFixture_->applicationsRevision();
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> volumeButton, appVolume;
            Microsoft::WRL::ComPtr<IInvokeProvider> openVolume;
            Microsoft::WRL::ComPtr<IRangeValueProvider> appVolumeRange;
            step.check(SUCCEEDED(automationChild(static_cast<std::size_t>(NowPlayingAction::volume), &volumeButton)) && automationID(volumeButton.Get()) == L"volume" &&
                       SUCCEEDED(volumeButton.As(&openVolume)) && SUCCEEDED(openVolume->Invoke()) && capturesPointer() && view.sliderCount() == 2 && covers({1, 1}),
                       "UIA Invoke opens the volume menu, which captures the pointer");
            step.check(automationCount() == 7 && SUCCEEDED(group->Navigate(NavigateDirection_LastChild, &appVolume)) && automationID(appVolume.Get()) == L"appVolume" &&
                       SUCCEEDED(appVolume.As(&appVolumeRange)), "The App volume slider joins the UIA tree as soon as the menu opens");
            {
                const auto ax = preview_->accessibility(t);
                double shown{};
                step.check(ax.sliders.size() == 2 && ax.sliders[1].enabled && ax.sliders[1].value && std::abs(*ax.sliders[1].value - 1) < 1e-9 &&
                           SUCCEEDED(appVolumeRange->get_Value(&shown)) && std::abs(shown - 1) < 1e-9,
                           "The accessible app-volume slider reports the route at 100%");
            }
            present(.2);
            step.check(SUCCEEDED(appVolumeRange->SetValue(.4)) && audioFixture_->accepted() == accepted + 1 && audioFixture_->pending(),
                       "A UIA volume change becomes one asynchronous route write");
            settle(step);
            const auto& route = audioFixture_->process();
            step.check(audioFixture_->applicationsRevision() > revision && route.state == gpu::AudioApplicationRouteState::active && route.gain &&
                       std::abs(*route.gain - .4f) < 1e-6f && view.input().volume && std::abs(*view.input().volume - .4) < 1e-6,
                       "The confirmed route gain returns through the audio publication");
            {
                double shown{};
                step.check(!automationRefresh(group->root()) && SUCCEEDED(appVolumeRange->get_Value(&shown)) && std::abs(shown - .4) < 1e-6,
                           "UIA reports the confirmed app volume on the same slider");
            }
            step.check(preview_->setSlider(NowPlayingSliderKind::volume, 1, t), "Volume returns to 100%");
            settle(step);
            step.check(route.gain && std::abs(*route.gain - 1) < 1e-6f && view.input().volume && std::abs(*view.input().volume - 1) < 1e-6 &&
                       playbackEvents_ == events + 7, "Volume changes are not playback actions");
            step.check(preview_->key({app::KeyKind::down, VK_ESCAPE}, false, t) && !capturesPointer() && !covers({1, 1}), "Escape dismisses the volume menu");
            {
                // Escape is a consumed key (a content event): the host refreshes.
                double shown{};
                step.check(automationRefresh(group->root()) && automationCount() == 6 && appVolumeRange->get_Value(&shown) == UIA_E_ELEMENTNOTAVAILABLE,
                           "Closing the menu disconnects the App volume slider");
            }
            present(.5);

            // Concealing disconnects every fragment; the next refresh connects
            // fresh ones. A new host group re-parents them and frees the old.
            automationDisconnect();
            UiaRect gone{};
            step.check(automationCount() == 0 && nextElement->get_BoundingRectangle(&gone) == UIA_E_ELEMENTNOTAVAILABLE &&
                       invokeNext->Invoke() == UIA_E_ELEMENTNOTAVAILABLE && title() == "EndfieldHUD Fixture 1",
                       "Disconnected UIA fragments answer ElementNotAvailable and perform nothing");
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> reconnected;
            step.check(automationRefresh(group->root()) && automationCount() == 6 && SUCCEEDED(automationChild(3, &reconnected)) &&
                       automationID(reconnected.Get()) == L"next" && !sameObject(reconnected.Get(), nextElement.Get()), "The next refresh connects fresh fragments");
            {
                Microsoft::WRL::ComPtr<CoverageGroup> other;
                other.Attach(new CoverageGroup(*this));
                const auto held = group->references();
                Microsoft::WRL::ComPtr<IRawElementProviderFragment> moved, movedParent;
                step.check(automationRefresh(other->root()) && automationCount() == 6 && group->references() < held && other->references() > 1 &&
                           SUCCEEDED(automationChild(0, &moved)) && SUCCEEDED(moved->Navigate(NavigateDirection_Parent, &movedParent)) &&
                           sameObject(movedParent.Get(), other->root()), "A new host group re-parents the fragments and releases the previous group");
                movedParent.Reset();
                moved.Reset();
                step.check(automationRefresh(group->root()) && automationCount() == 6 && other->references() == 1, "Returning to the owner's group releases the other one");
            }
        }

        // Every cycle ends in the same presented state (bounded, repeatable caches).
        step.check(title() == "EndfieldHUD Fixture 1" && std::abs(position() - 42) < 1e-9 && !playing() && view.input().coverAvailable &&
                   view.input().volume && std::abs(*view.input().volume - 1) < 1e-6 && !capturesPointer() && !pointerLocked() &&
                   (step.cycle == 0 || playbackEvents_ == eventsAfterFirstCycle_), "Now Playing returns to the same paused fixture state");
        if (step.cycle == 0) eventsAfterFirstCycle_ = playbackEvents_;
    }
    void idleCoverage(app::ModuleCoverage& step) {
        settle(step); // e.g. the withdrawn audio vote's publication
        step.present(step.time + .05);
        observe(step.time + .05);
        if (host_.selectedModule() == core::Module::nowPlaying && host_.overlayVisible()) {
            // Now Playing is still the last presented module of this cycle.
            step.check(service_->snapshot().active && presentation().active(), "The presented Now Playing keeps its single provider");
            return;
        }
        const auto starts = *starts_;
        step.present(step.time + .1);
        {
            // The host refreshes on selection and closing: nothing stays listed.
            auto* group = coverageGroup();
            (void)automationRefresh(group->root());
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> child;
            step.check(automationCount() == 0 && !automationRefresh(group->root()) && SUCCEEDED(group->Navigate(NavigateDirection_FirstChild, &child)) && !child,
                       "Unpresented Now Playing exposes no UIA fragments");
        }
        step.check(!service_->snapshot().active && !presentation().active() && !preview_->nextWakeTime() && !pointerLocked() && !capturesPointer(),
                   "Unpresented Now Playing keeps no provider, deadline or input");
        step.check(!audioFixture_->nowPlayingVisible() && audioFixture_->applications().empty() && !wake_->pending.load() && *starts_ == starts,
                   "Unpresented Now Playing withdraws its audio vote and starts nothing");
        step.check(!hostEvent({app::HostEvent::Kind::audioChanged, app::HostEvent::audioApplications}, step.time + .1),
                   "Audio publications are ignored while Now Playing is hidden");
    }
    const modules::NowPlayingPresentation& presentation() const { return preview_->presentation(); }
    CoverageGroup* coverageGroup() {
        if (!coverageGroup_) coverageGroup_.Attach(new CoverageGroup(*this));
        return coverageGroup_.Get();
    }

    app::ApplicationHost& host_;
    app::UtilityExecutor& utility_;
    HWND window_{};
    const ApplicationAreaMode mode_;
    std::function<void(modules::EventKind, modules::EventMetadata)> record_;
    std::uint32_t message_{};
    std::shared_ptr<Wake> wake_;
    std::shared_ptr<std::atomic<double>> fixtureClock_;
    std::shared_ptr<std::uint64_t> starts_ = std::make_shared<std::uint64_t>(0);
    // Declaration order is destruction order in reverse: the preview first.
    std::unique_ptr<gpu::NativeNowPlayingService> service_;
    std::shared_ptr<gpu::NowPlayingWeb> web_;
    std::unique_ptr<gpu::NowPlayingAudioFixture> audioFixture_;
    std::unique_ptr<tools::NowPlayingPreview> preview_;
    // UIA fragments (after the preview they call into; reset first) and the
    // host group they hang under (owned by automation_ while it exists).
    std::unique_ptr<gpu::NativeNowPlayingAutomation> automation_;
    IRawElementProviderFragmentRoot* automationRoot_{};
    Microsoft::WRL::ComPtr<CoverageGroup> coverageGroup_; // hidden coverage only
    bool sized_{};
    // Coverage evidence (never user content: action and source only).
    unsigned playbackEvents_{}, eventsAfterFirstCycle_{};
    std::uint64_t startsBefore_{};
    modules::EventMetadata lastPlayback_;
};
} // namespace

std::unique_ptr<app::ModuleOwner> nowPlayingModule(const app::ApplicationModuleContext& context) {
    // The shared executor, HUD window and clock are the area's only services;
    // without them the module stays absent rather than inventing its own.
    if (!context.host || !context.utility || !context.window || !context.clock || !context.invalidate) return nullptr;
    return std::make_unique<NowPlayingArea>(context);
}
} // namespace endfield::areas
