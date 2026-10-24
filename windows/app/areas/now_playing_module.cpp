#include "app/areas/now_playing_module.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "modules/event_log.hpp"
#include "modules/now_playing_canvas.hpp"
#include "modules/now_playing_lyrics_sources.hpp"
#include "native/audio_runtime.hpp"
#include "native/now_playing_cover.hpp"
#include "native/now_playing_fixture.hpp"
#include "native/now_playing_players.hpp"
#include "native/now_playing_volume.hpp"
#include "native/now_playing_web.hpp"
#include "native/now_playing_web_cache.hpp"
#include "native/screen_reader_peers.hpp"
#include "tools/hud_design_owner.hpp"
#include "tools/now_playing_session.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
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
namespace d = modules::design;

// Reads NetEase Cloud Music's own queue state file so the catalog can resolve
// the current track's exact length and song id (its player bar exposes no
// time labels and no accessible slider value, read live on 3.1.41). Returns
// the raw bytes, cached and re-read only when the file's size or write time
// changes (NetEase rewrites it on a track change), so the owner-thread caller
// pays a stat on each distinct track and a bounded read only on a real
// change; the parse itself runs on the shared decode worker. Nothing is kept
// beyond the latest bytes, and nothing is written. Null when the file is
// absent, unreadable or larger than the bound (no NetEase install, or a
// portable build with a different data directory).
modules::NowPlayingPlayingListReader neteasePlayingListReader() {
    struct Cache {
        std::wstring path;
        bool resolved{};
        FILETIME written{};
        unsigned long long size{};
        modules::NowPlayingBody bytes;
    };
    auto cache = std::make_shared<Cache>();
    return [cache]() -> modules::NowPlayingBody {
        constexpr unsigned long long maximumBytes = 16ull * 1024 * 1024;
        if (!cache->resolved) {
            cache->resolved = true;
            wchar_t local[MAX_PATH]{};
            const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
            if (length > 0 && length < std::size(local))
                cache->path = std::wstring(local, length) + L"\\NetEase\\CloudMusic\\webdata\\file\\playingList";
        }
        if (cache->path.empty()) return nullptr;
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!GetFileAttributesExW(cache->path.c_str(), GetFileExInfoStandard, &info)) { cache->bytes.reset(); return nullptr; }
        const unsigned long long size = (static_cast<unsigned long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        if (cache->bytes && size == cache->size && CompareFileTime(&info.ftLastWriteTime, &cache->written) == 0) return cache->bytes;
        cache->bytes.reset();
        cache->size = size;
        cache->written = info.ftLastWriteTime;
        if (!size || size > maximumBytes) return nullptr;
        const HANDLE file = CreateFileW(cache->path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return nullptr;
        auto data = std::make_shared<std::vector<std::uint8_t>>();
        try {
            data->resize(static_cast<std::size_t>(size));
        } catch (...) {
            CloseHandle(file);
            return nullptr;
        }
        std::size_t read = 0;
        while (read < data->size()) {
            DWORD got = 0;
            const DWORD want = static_cast<DWORD>(std::min<unsigned long long>(1u << 20, data->size() - read));
            if (!ReadFile(file, data->data() + read, want, &got, nullptr) || got == 0) break;
            read += got;
        }
        CloseHandle(file);
        if (read != data->size()) return nullptr; // the file changed under us; a later call retries
        cache->bytes = std::move(data);
        return cache->bytes;
    };
}

bool hasPixels(const app::ClientMetrics& m) { return m.pixelWidth && m.pixelHeight && std::isfinite(m.scale) && m.scale > 0; }
core::Point center(core::Rect r) { return {r.x + r.width * .5, r.y + r.height * .5}; }
// The middle lyric row the display shows at `now`.
std::string lyricRow(const modules::NowPlayingViewInput& i, double now) {
    return i.lyrics ? std::string(i.lyrics->window(modules::nowPlayingElapsed(i, now).value_or(0))[1]) : std::string();
}

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
        : host_(*c.host), utility_(*c.utility), window_(static_cast<HWND>(c.window)), mode_(c.mode), record_(c.recordEvent), invalidate_(c.invalidate) {
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
        // Production: GSMTC beside the window-title fallback for Win32 players
        // that publish no media session (NetEase Cloud Music, QQ Music,
        // Kugou); its WinEvent hooks run on this (UI) thread's message loop
        // while Now Playing is presented. Hidden coverage composes the
        // fixture player with a synthetic desktop the same way.
        // The window system lives as long as this owner: while Now Playing is
        // hidden it keeps one title hook per known player process, so the next
        // opening knows when the current track began. The players' audio
        // activity (which process renders audio) comes from the shared per-app
        // audio publication and ranks and pauses the window-title players.
        gpu::NativeNowPlayingService::Factory provider;
        activity_ = std::make_shared<gpu::NowPlayingProcessActivity>();
        if (mode_ == ApplicationAreaMode::production) {
            windowSystem_ = gpu::windowsNowPlayingWindowSystem(c.clock);
            provider = gpu::windowsNowPlayingPlayers(c.clock, windowSystem_, activity_);
        } else if (mode_ == ApplicationAreaMode::developmentPreview) provider = gpu::nowPlayingFixtureProvider(c.clock);
        else {
            fixtureClock_ = std::make_shared<std::atomic<double>>(c.clock());
            const auto mirror = [clock = fixtureClock_] { return clock->load(std::memory_order_acquire); };
            coverageWindows_ = std::make_shared<gpu::NowPlayingWindowFixture>();
            provider = gpu::nowPlayingCompositeProvider(gpu::nowPlayingFixtureProvider(mirror),
                gpu::nowPlayingWindowProvider([desktop = coverageWindows_] { return desktop; }, mirror, activity_));
        }
        // Each activation (selection of a visible Now Playing) creates one provider.
        provider = [inner = std::move(provider), starts = starts_] { ++*starts; return inner(); };
        service_ = std::make_unique<gpu::NativeNowPlayingService>(utility_, std::move(provider), wake, c.clock);

        tools::NowPlayingSessionOptions options;
        // Covers are decoded (WIC Fant, scaled down only) to the size the
        // canvas draws its 480 px slot at (B11: setArtworkPixels from the
        // live render scale) and drawn 1:1 by the cover carrier.
        options.decodeAt = [](std::span<const std::uint8_t> bytes, unsigned pixels) { return gpu::decodeNowPlayingCover(bytes, pixels); };
        if (mode_ == ApplicationAreaMode::production) {
            // Visible-only LRCLIB/NetEase lookups and catalog covers over one
            // kept-alive WinHTTP session (retained while Now Playing is
            // presented), answered from a bounded disk cache when a track
            // returns. Covers are requested at the size the slot shows.
            http_ = std::make_shared<gpu::NowPlayingHttp>();
            if (!c.dataRoot.empty()) {
                gpu::NowPlayingWebCache::Options cache{c.dataRoot / "Caches" / "NowPlaying"};
                // Only answers that carry lyrics, songs or a cover are kept:
                // a refusal or an empty answer is asked again next time.
                cache.keep = [](const gpu::NowPlayingWeb::Request& request, std::span<const std::uint8_t> body) {
                    return request.kind != gpu::NowPlayingWeb::Kind::lyrics || modules::nowPlayingLyricsBodyCacheable(request.url, body);
                };
                webCache_ = std::make_shared<gpu::NowPlayingWebCache>(utility_, std::move(cache));
            }
            web_ = std::make_shared<gpu::NowPlayingWeb>(http_->transport(), wake, c.clock, 10, webCache_);
            options.web = web_;
            // NetEase's own queue file supplies the exact length and song id
            // for its window-title player (no time labels, no slider value).
            options.neteasePlayingList = neteasePlayingListReader();
            // The playing app's mixer route through the ONE shared audio worker;
            // without it the pop-up stays unavailable.
            if (auto* audio = host_.audio()) options.volume = gpu::nowPlayingAppVolume(*audio);
        } else {
            audioFixture_ = std::make_unique<gpu::NowPlayingAudioFixture>(wake);
            options.volume = gpu::nowPlayingAppVolume(*audioFixture_);
        }
        volume_ = options.volume;
        // Successful transport commands only; never the title (source onEvent).
        options.playback = [this](const modules::NowPlayingPlaybackEvent& event) {
            auto metadata = modules::nowPlayingPlaybackMetadata(event);
            ++playbackEvents_;
            lastPlayback_ = metadata;
            if (record_) record_(modules::EventKind::playbackAction, std::move(metadata));
        };
        session_ = std::make_unique<tools::NowPlayingSession>(*service_, utility_, [invalidate = invalidate_] { invalidate(); }, std::move(options));

        // The canvas on the shared design library (modules/now_playing_canvas.hpp)
        // in a design owner core this area wraps (its own services, the cover
        // carrier and the coverage stay here).
        tools::DesignOwnerOptions design;
        if (!host_.designOwnerOptions(design, "nowPlaying", routing())) throw std::runtime_error("Now Playing needs the shared design overlay");
        core_ = std::make_unique<tools::DesignOwnerCore>(c.rasterizer, *host_.designContext(), std::move(design));
        modules::NowPlayingCanvasServices services;
        services.input = [this]() -> const modules::NowPlayingViewInput& { return session_->input(); };
        services.artwork = [this] { return session_->artwork(); };
        services.setPresented = [this](bool value) { session_->setActive(value, time_); followPresentation(); };
        services.perform = [this](const modules::NowPlayingIntent& intent) { const bool done = session_->dispatch(intent, time_); synchronize(time_); return done; };
        services.setArtworkPixels = [this](unsigned pixels) { session_->setArtworkPixels(pixels); };
        auto canvas = std::make_unique<modules::NowPlayingCanvas>(*host_.designContext(), std::move(services));
        canvas_ = canvas.get();
        core_->add(std::move(canvas));
        cover_ = std::make_unique<gpu::NativeNowPlayingCover>(c.rasterizer);
        // Not a DesignModuleOwner: this wrapper marks its module itself (HOST-HOOKS R5).
        host_.setDesignScreen(core::Module::nowPlaying);
        resize(c.metrics);
    }
    ~NowPlayingArea() override {
        wake_->alive.store(false, std::memory_order_release);
        // UIA fragments call into the canvas: disconnect them first.
        if (core_) core_->automationDisconnect();
        // The canvas withdraws the audio vote and stops the provider; then
        // the downloads and the service go, all before the shared executor,
        // the AudioRuntime and the HWND.
        core_.reset();
        session_.reset();
        audioFixture_.reset();
        web_.reset();
        http_.reset();
        webCache_.reset();
        service_.reset();
        // Last: the hidden-time title hooks belong to this (UI) thread.
        if (windowSystem_) windowSystem_->shutdown();
    }
    NowPlayingArea(const NowPlayingArea&) = delete;
    NowPlayingArea& operator=(const NowPlayingArea&) = delete;

    std::string_view name() const noexcept override { return "nowPlaying"; }
    app::ModuleRouting routing() const noexcept override {
        // The open volume pop-up captures like the other module menus; ordinary
        // input follows the built-in owners (0...19).
        app::ModuleRouting r;
        r.pointerCapture = 30; r.pointer = 30; r.key = 30; r.keyCapture = 30; r.message = 30;
        return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::nowPlaying; }

    void resize(const app::ClientMetrics& m) override { if (hasPixels(m)) core_->resize(m); }
    void setAppearance(const app::ModuleAppearance& a, double t) override { clock(t); core_->setAppearance(a, t); }
    void setOverlayVisible(bool visible, double t) override {
        clock(t);
        overlayVisible_ = visible;
        if (!visible) canvas_->setPresented(false, time_);
        core_->setOverlayVisible(visible, t);
        followPresentation();
    }
    void overlayClosing(double t) override { clock(t); canvas_->setPresented(false, time_); core_->overlayClosing(t); overlayVisible_ = false; followPresentation(); }
    // setPresented follows the presentation exactly as v1.2.0's owner did:
    // the module is the current or incoming surface, requested, in a visible
    // HUD (Mac: from the start of the reveal, before input is accepted).
    void update(const app::ModuleFrame& f) override {
        clock(f.time);
        const auto& sample = f.presentation;
        const bool shown = sample.current.module == core::Module::nowPlaying || (sample.incoming && sample.incoming->module == core::Module::nowPlaying);
        canvas_->setPresented(shown && sample.requested == core::Module::nowPlaying && overlayVisible_, time_);
        synchronize(time_);
        core_->update(f);
        placeCover();
        followPresentation();
    }
    void prewarm(const app::ModuleFrame& f) override { clock(f.time); core_->prewarm(f); }
    bool requiresFrames(double t) const override { return core_->requiresFrames(t); }
    std::optional<double> nextWakeTime(double t) const override {
        auto a = session_->nextWakeTime(t);
        const auto b = core_->nextWakeTime(t);
        if (b && (!a || *b < *a)) a = b;
        return a;
    }
    bool deadline(double t) override {
        clock(t);
        const bool changed = session_->deadline(time_);
        synchronize(time_);
        const bool woke = core_->deadline(t);
        return changed || woke;
    }

    void upload(gpu::Renderer& r) override { core_->upload(r); if (insertion_) cover_->upload(r); }
    std::span<const gpu::LayerCompositionEntry> entries(core::Module m) override { return core_->entries(m); }
    void collected(gpu::Renderer& r) override { core_->collected(r); cover_->collected(r); }
    void release(gpu::Renderer& r) override {
        if (insertion_) { core_->setInsertion(core::Module::nowPlaying, nullptr, std::nullopt); insertion_ = false; insertedAfter_ = nullptr; }
        (void)cover_->release(r);
        core_->release(r);
    }

    // Source containsControl: only control faces (or the open volume pop-up)
    // claim the hit test; transparent canvas space (the cover, the gaps of
    // the button row over the shell) still exposes the shell beneath.
    bool covers(core::Point p) const override {
        if (core_->capturesPointer()) return true;
        const auto q = core_->boxPoint(p);
        return q && canvas_->containsControl(*q);
    }
    bool pointerLocked() const override { return core_->pointerLocked(); }
    bool capturesPointer() const override { return core_->capturesPointer(); }
    bool capturesWheel() const override { return core_->capturesWheel(); }
    bool capturesKeys() const override { return core_->capturesKeys(); }
    bool pointer(const app::PointerEvent& e, double t) override { clock(t); return core_->pointer(e, t); }
    bool wheel(const app::WheelEvent& e, double t) override { clock(t); return core_->wheel(e, t); }
    bool key(const app::KeyEvent& e, double t) override { clock(t); return core_->key(e, t); }
    bool filterKey(const app::NativeMessage& m) override { return core_->filterKey(m); }
    bool message(const app::NativeMessage& m, double t) override {
        clock(t);
        if (m.message != message_) return core_->message(m, t);
        (void)deliver(time_);
        return true;
    }
    // Mac SystemHUDView.draggingUpdated offers no drag operation while Now
    // Playing is selected (only the Shelf entry, App shortcuts, File Shelf,
    // documents and Notes take files), so a file drag shows "no drop" here
    // instead of the host's default copy feedback; drop() keeps refusing.
    bool dropHover(std::span<const std::string>, std::optional<core::Point>, double) override { return false; }
    void focus(bool focused, double t) override { clock(t); core_->focus(focused, t); if (!focused) cancelInteraction(t); }
    void cancelInteraction(double t) override { clock(t); core_->cancelInteraction(t); }
    std::optional<app::ModuleCursor> cursorAt(core::Point p) const override { return core_->cursorAt(p); }

    bool hostEvent(const app::HostEvent& e, double t) override {
        clock(t);
        switch (e.kind) {
        case app::HostEvent::Kind::utilityCompleted:
            // Service results, downloads, decoded artwork/lyrics, queue capacity.
            return deliver(time_);
        case app::HostEvent::Kind::overlayConcealed: {
            followPresentation();
            (void)core_->hostEvent(e, t);   // canvases not shown keep no artwork
            // Closed on another module: keep no cover texture and only the
            // presented cover decoded (the module shown at the close keeps
            // its artwork for the warm reopen).
            if (host_.selectedModule() != core::Module::nowPlaying) {
                (void)session_->park();          // false when nothing was left to trim
                if (insertion_) { core_->setInsertion(core::Module::nowPlaying, nullptr, std::nullopt); insertion_ = false; insertedAfter_ = nullptr; }
                (void)cover_->release(host_.renderer());
            }
            return false;
        }
        case app::HostEvent::Kind::audioChanged:
            if (!(e.flags & app::HostEvent::audioApplications)) return false;
            followActivity();
            return synchronize(time_, session_->audioChanged(time_));
        default:
            return core_->hostEvent(e, t);
        }
    }
    void coverage(app::ModuleCoverage& step) override {
        if (step.module) presentedCoverage(step);
        else idleCoverage(step);
    }

    // UI Automation: the canvas's tree through the design owner core (the
    // shared NativeDesignAutomation, LIBRARY-DESIGN.md section 9).
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return core_->automationRefresh(root); }
    std::size_t automationCount() const override { return core_->automationCount(); }
    long automationChild(std::size_t index, IRawElementProviderFragment** out) const override { return core_->automationChild(index, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return core_->automationFromPoint(x, y, out); }
    void automationDisconnect() noexcept override { core_->automationDisconnect(); }
    std::optional<core::Module> automationModule() const noexcept override { return core_->automationModule(); }

private:
    void clock(double t) {
        if (!std::isfinite(t)) return;
        time_ = std::max(time_, t);
        observe(time_);
    }
    // The controller published a new snapshot: the canvas follows it (its observers).
    bool synchronize(double t, bool changed = false) {
        if (session_->inputRevision() != seenRevision_) {
            seenRevision_ = session_->inputRevision();
            canvas_->refresh(t);
            changed = true;
        }
        return changed;
    }
    // The cover textures right after the cover plate's part, posed as the canvas's art layers.
    void placeCover() {
        const auto* plate = canvas_->coverLayer();
        const auto pose = core_->pose(core::Module::nowPlaying);
        if (plate && pose) {
            const auto views = canvas_->coverViews(time_);
            cover_->updatePose(*pose, canvas_->root(), views);
            if (!insertion_ || insertedAfter_ != plate) { core_->setInsertion(core::Module::nowPlaying, plate, cover_->entry()); insertion_ = true; insertedAfter_ = plate; }
        } else if (insertion_) {
            core_->setInsertion(core::Module::nowPlaying, nullptr, std::nullopt);
            insertion_ = false; insertedAfter_ = nullptr;
        }
    }
    // A logical client rect of a box rect (UIA and coverage geometry).
    std::optional<core::Rect> screenRect(core::Rect box) const {
        return gpu::screenReaderClientToScreen(window_, host_.metrics().scale, core_->clientRect(box));
    }
    void observe(double t) {
        if (!fixtureClock_ || !std::isfinite(t)) return;
        if (t > fixtureClock_->load(std::memory_order_relaxed)) fixtureClock_->store(t, std::memory_order_release);
    }
    // Drains the service, the downloads, decoded artwork/lyrics and (previews
    // and coverage) the synthetic audio publication; true when anything changed.
    bool deliver(double t) {
        wake_->pending.store(false, std::memory_order_release);
        bool changed = session_->utilityCompleted(t);
        if (audioFixture_ && audioFixture_->publish()) {
            followActivity();
            changed = session_->audioChanged(t) || changed;
        }
        return synchronize(t, changed);
    }
    // The WinHTTP session is retained exactly while Now Playing is presented;
    // released, it closes with its connections once no download is in flight.
    void followPresentation() {
        if (!http_) return;
        const bool active = service_->snapshot().active;
        if (active == httpRetained_) return;
        httpRetained_ = active;
        http_->setRetained(active);
    }
    // The per-app audio publication names the processes that render audio. A
    // change for any process is folded into the window-title players' state
    // with one provider read (event driven: only on such a publication while
    // Now Playing is presented; the vote is withdrawn while it is hidden).
    void followActivity() {
        if (!activity_ || !volume_ || !volume_->routes || !service_->snapshot().active) return;
        if (activity_->update(volume_->routes())) service_->refresh();
    }
    std::optional<d::AccessibilityItem> item(std::string_view id) const {
        std::vector<d::AccessibilityItem> items;
        canvas_->accessibilityItems(items);
        for (auto& i : items) if (i.id == id) return i;
        return std::nullopt;
    }
    std::size_t itemCount() const { std::vector<d::AccessibilityItem> items; canvas_->accessibilityItems(items); return items.size(); }
    std::optional<core::Point> controlPoint(std::string_view id) const {
        const auto i = item(id);
        if (!i) return std::nullopt;
        const auto r = core_->clientRect(i->rect);
        return r ? std::optional(center(*r)) : std::nullopt;
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
    bool recorded(unsigned count, std::string_view action, std::string_view source = "system") const {
        const auto value = [&](std::string_view key) {
            const auto found = lastPlayback_.find(key);
            return found == lastPlayback_.end() ? std::string_view{} : std::string_view(found->second);
        };
        return playbackEvents_ == count && lastPlayback_.size() == 2 && value("action") == action && value("source") == source;
    }
    void presentedCoverage(app::ModuleCoverage& step) {
        step.check(*step.module == core::Module::nowPlaying && fixtureClock_ && audioFixture_ && coverageWindows_ && !web_,
                   "Hidden coverage presents Now Playing with the fixture player, a synthetic audio route, a synthetic desktop and no network");
        double t = step.time;
        const auto present = [&](double seconds) { t += seconds; observe(t); step.present(t); };
        const auto& in = session_->input();
        const auto title = [&] { return in.track ? in.track->title : std::string(); };
        const auto position = [&] { return in.track && in.track->position ? *in.track->position : -1.; };
        const auto playing = [&] { return in.track && in.track->isPlaying; };

        if (step.cycle == 0) startsBefore_ = *starts_ ? *starts_ - 1 : 0;
        settle(step);
        present(.5); // the cover cross-fade and the page entrance settle
        step.check(*starts_ == startsBefore_ + step.cycle + 1 && service_->snapshot().active && canvas_->presented(),
                   "Each presentation of Now Playing starts exactly one provider");
        step.check(title() == "EndfieldHUD Fixture 1" && in.track->artist == "Local verification" && in.track->album == "Now Playing" && !in.failed && in.session != 0,
                   "Fixture metadata reaches the canvas");
        step.check(std::abs(position() - 42) < 1e-9 && !playing() && modules::nowPlayingTime(in.track->duration) == "03:30", "The fixture starts paused at 42 of 210 seconds");
        step.check(in.coverAvailable && session_->artworkStats().decoded > 0 && canvas_->coverViews(t)[1].image, "The fixture cover decodes on the shared executor and shows");
        step.check(canvas_->showsLyrics() && lyricRow(in, t) == "Current", "Fixture timed lyrics show the current line");
        step.check(in.volumeAvailable && in.volume && std::abs(*in.volume - 1) < 1e-9 && audioFixture_->nowPlayingVisible() && audioFixture_->applications().size() == 1,
                   "The synthetic process route resolves through the per-app audio bridge");

        // Projection, hit testing and the UIA items.
        const auto playPoint = controlPoint("playPause");
        step.check(playPoint && item("playPause")->enabled, "The play control projects through the Now Playing plane");
        const auto transparent = core_->clientRect({295, 200, 0, 0});
        step.check(covers(*playPoint) && transparent && !covers({transparent->x, transparent->y}), "Only control faces claim the hit test over navigation");
        {
            // Paths are only compared, never opened.
            const std::array<std::string, 1> dragged{"coverage-track.mp3"};
            step.check(!dropHover(dragged, *playPoint, t) && !dropHover(dragged, std::nullopt, t) && !drop(dragged, *playPoint, t),
                       "A file drag over Now Playing is refused, as the source offers no drag operation there");
        }
        {
            const auto track = item("track");
            step.check(track && track->role == d::AccessibilityRole::text && track->value == "EndfieldHUD Fixture 1 / Local verification / Now Playing" && itemCount() == 7
                && item("seek") && item("seek")->role == d::AccessibilityRole::slider && !item("appVolume"),
                "The track readout (title / artist / album), the seek slider and the five buttons");
        }
        // UI Automation through the ModuleOwner hooks, as the HUD window's root
        // drives them: one stable group, refreshed on content events.
        auto* group = coverageGroup();
        (void)automationRefresh(group->root());
        step.check(automationCount() == 7 && !automationRefresh(group->root()), "The UIA hooks list the canvas's seven items under the owner's group");

        // Settled paused frames: no rasterization, provider work or deadline.
        {
            const auto scene = core_->sceneStats(core::Module::nowPlaying);
            const auto decodes = session_->artworkStats().submitted;
            const auto revision = service_->snapshot().revision;
            const auto starts = *starts_;
            const auto uploads = cover_->stats().uploads;
            for (unsigned frame = 0; frame < 12; ++frame) present(1. / 60);
            const auto after = core_->sceneStats(core::Module::nowPlaying);
            step.check(scene && after && after->generations == scene->generations && after->surfacesUpdated == scene->surfacesUpdated && after->partsLoaded == scene->partsLoaded
                && cover_->stats().uploads == uploads && session_->artworkStats().submitted == decodes && service_->snapshot().revision == revision && *starts_ == starts
                && !wake_->pending.load() && !nextWakeTime(t) && !requiresFrames(t), "Paused warm frames re-rasterize, decode, read, schedule and wake nothing");
        }

        if (step.cycle == 0) {
            // Transport commands record Event Log playbackAction once each (only
            // in the first cycle, so the Event Log stays identical afterwards).
            const auto events = playbackEvents_;
            step.check(pointer({app::PointerKind::down, app::PointerButton::left, playPoint->x, playPoint->y}, t) &&
                       pointer({app::PointerKind::up, app::PointerButton::left, playPoint->x, playPoint->y}, t), "Projected play click is consumed");
            settle(step);
            step.check(playing() && recorded(events + 1, "playPause"), "Play reaches the fixture and records playbackAction");
            step.check(nextWakeTime(t).has_value() && canvas_->clockActive(), "Playing: the display clock folds its next second or cue into the shared deadline");
            present(.4);
            step.check(core_->key({app::KeyKind::down, VK_RIGHT}, 0, t), "Right arrow seeks forward");
            settle(step);
            step.check(position() > 47 - 1e-6 && recorded(events + 2, "seek"), "Keyboard seek moves five seconds and records seek");
            step.check(core_->key({app::KeyKind::down, VK_SPACE}, 0, t), "Space toggles playback");
            settle(step);
            step.check(!playing() && recorded(events + 3, "playPause"), "Space pauses the fixture");
            step.check(!core_->key({app::KeyKind::down, VK_SPACE}, 2, t) && !core_->key({app::KeyKind::down, VK_SPACE}, 4, t), "Modified transport keys are left to the HUD");

            // Seek drag on the projected bar: drag lock, then one command at release.
            const auto from = core_->clientRect({55 + .2 * 480, 473, 0, 0}), to = core_->clientRect({55 + .4 * 480, 473, 0, 0});
            step.check(from && to && pointer({app::PointerKind::down, app::PointerButton::left, from->x, from->y}, t) && pointerLocked(),
                       "The seek bar starts a drag that locks the HUD pointer");
            present(1. / 60);
            step.check(pointer({app::PointerKind::move, app::PointerButton::none, to->x, to->y}, t) && playbackEvents_ == events + 3,
                       "Seek movement previews locally without a command");
            step.check(pointer({app::PointerKind::up, app::PointerButton::left, to->x, to->y}, t) && !pointerLocked(), "Release ends the drag");
            settle(step);
            step.check(std::abs(position() - 84) < 1 && recorded(events + 4, "seek"), "Seek release emits one successful command");

            // UI Automation over the real owner through its hooks: navigation
            // and hit tests enter through the group like a UIA client's.
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> first, parent, sibling, last;
            Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> fragmentRoot;
            step.check(SUCCEEDED(group->Navigate(NavigateDirection_FirstChild, &first)) && first && automationID(first.Get()) == L"track" &&
                       SUCCEEDED(first->Navigate(NavigateDirection_Parent, &parent)) && sameObject(parent.Get(), group->root()) &&
                       SUCCEEDED(first->get_FragmentRoot(&fragmentRoot)) && sameObject(fragmentRoot.Get(), group->root()) &&
                       SUCCEEDED(first->Navigate(NavigateDirection_NextSibling, &sibling)) && automationID(sibling.Get()) == L"seek",
                       "UIA fragments navigate in the canvas's order under the owner's group, their parent and fragment root");
            step.check(SUCCEEDED(group->Navigate(NavigateDirection_LastChild, &last)) && automationID(last.Get()) == L"lyrics", "The lyrics toggle is the group's last child");
            const auto nextScreen = screenRect(item("next")->rect);
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> nextElement;
            step.check(nextScreen && SUCCEEDED(group->ElementProviderFromPoint(nextScreen->x + nextScreen->width / 2, nextScreen->y + nextScreen->height / 2, &nextElement)) &&
                       automationID(nextElement.Get()) == L"next", "UIA hit testing finds Next at its projected screen position");
            Microsoft::WRL::ComPtr<IInvokeProvider> invokeNext;
            step.check(SUCCEEDED(nextElement.As(&invokeNext)) && SUCCEEDED(invokeNext->Invoke()), "UIA Invoke performs Next");
            settle(step);
            present(.3);
            step.check(title() == "EndfieldHUD Fixture 2" && std::abs(position()) < 1e-9 && recorded(events + 5, "next"), "Next advances the fixture track");
            step.check(canvas_->perform("previous", t), "Previous is available");
            settle(step);
            present(.3);
            step.check(title() == "EndfieldHUD Fixture 1" && recorded(events + 6, "previous"), "Previous within 3 s goes to the previous fixture track");
            // The seek slider keeps its identity across the track changes' content events.
            (void)automationRefresh(group->root());
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> seekElement;
            Microsoft::WRL::ComPtr<IRangeValueProvider> seekRange;
            double step5{};
            step.check(SUCCEEDED(automationChild(1, &seekElement)) && automationID(seekElement.Get()) == L"seek" && SUCCEEDED(seekElement.As(&seekRange))
                && SUCCEEDED(seekRange->get_SmallChange(&step5)) && step5 == 5, "The UIA seek slider has the Mac's 5 s step");
            step.check(SUCCEEDED(seekRange->SetValue(42)), "UIA RangeValue seeks to an exact position");
            settle(step);
            step.check(std::abs(position() - 42) < 1e-9 && recorded(events + 7, "seek"), "Accessible seek restores 42 seconds");

            // The lyrics toggle is local canvas state (not persisted).
            step.check(canvas_->perform("lyrics", t) && !canvas_->showsLyrics(), "Lyrics toggle hides the lyric rows");
            present(.3);
            step.check(canvas_->perform("lyrics", t) && canvas_->showsLyrics(), "Lyrics toggle restores the lyric rows");
            present(.3);

            // Playing-app volume: the per-app route through the audio bridge,
            // opened and set through UI Automation.
            const auto accepted = audioFixture_->accepted();
            const auto revision = audioFixture_->applicationsRevision();
            (void)automationRefresh(group->root());
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> volumeButton, appVolume;
            Microsoft::WRL::ComPtr<IInvokeProvider> openVolume;
            Microsoft::WRL::ComPtr<IRangeValueProvider> appVolumeRange;
            step.check(SUCCEEDED(automationChild(5, &volumeButton)) && automationID(volumeButton.Get()) == L"volume" &&
                       SUCCEEDED(volumeButton.As(&openVolume)) && SUCCEEDED(openVolume->Invoke()) && capturesPointer() && canvas_->volumeOpen() && covers({1, 1}),
                       "UIA Invoke opens the volume pop-up, which captures the pointer");
            (void)automationRefresh(group->root());
            step.check(automationCount() == 8 && SUCCEEDED(group->Navigate(NavigateDirection_LastChild, &appVolume)) && automationID(appVolume.Get()) == L"appVolume" &&
                       SUCCEEDED(appVolume.As(&appVolumeRange)), "The App volume slider joins the UIA tree as soon as the pop-up opens");
            {
                double shown{};
                const auto a = item("appVolume");
                step.check(a && a->enabled && a->number == 100 && a->value == "100%" && SUCCEEDED(appVolumeRange->get_Value(&shown)) && std::abs(shown - 100) < 1e-9,
                           "The accessible app-volume slider reports the route at 100% (0...100, step 2)");
            }
            present(.2);
            step.check(SUCCEEDED(appVolumeRange->SetValue(40)) && audioFixture_->accepted() == accepted + 1 && audioFixture_->pending(),
                       "A UIA volume change becomes one asynchronous route write");
            settle(step);
            const auto& route = audioFixture_->process();
            step.check(audioFixture_->applicationsRevision() > revision && route.state == gpu::AudioApplicationRouteState::active && route.gain &&
                       std::abs(*route.gain - .4f) < 1e-6f && in.volume && std::abs(*in.volume - .4) < 1e-6 && item("appVolume")->value == "40%",
                       "The confirmed route gain returns through the audio publication");
            step.check(canvas_->adjustAccessibilitySlider("appVolume", 100, t), "Volume returns to 100%");
            settle(step);
            step.check(route.gain && std::abs(*route.gain - 1) < 1e-6f && in.volume && std::abs(*in.volume - 1) < 1e-6 && playbackEvents_ == events + 7,
                       "Volume changes are not playback actions");
            step.check(core_->key({app::KeyKind::down, VK_ESCAPE}, 0, t) && !capturesPointer() && !canvas_->volumeOpen() && !covers({1, 1}), "Escape closes the volume pop-up");
            {
                double shown{};
                step.check(automationRefresh(group->root()) && automationCount() == 7 && appVolumeRange->get_Value(&shown) == UIA_E_ELEMENTNOTAVAILABLE,
                           "Closing the pop-up disconnects the App volume slider");
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
            step.check(automationRefresh(group->root()) && automationCount() == 7 && SUCCEEDED(automationChild(4, &reconnected)) &&
                       automationID(reconnected.Get()) == L"next" && !sameObject(reconnected.Get(), nextElement.Get()), "The next refresh connects fresh fragments");
            windowPlayerCoverage(step, t, present);
        }

        // Every cycle ends in the same presented state (bounded, repeatable caches).
        step.check(title() == "EndfieldHUD Fixture 1" && std::abs(position() - 42) < 1e-9 && !playing() && in.coverAvailable &&
                   in.volume && std::abs(*in.volume - 1) < 1e-6 && !capturesPointer() && !pointerLocked() &&
                   (step.cycle == 0 || playbackEvents_ == eventsAfterFirstCycle_), "Now Playing returns to the same paused fixture state");
        if (step.cycle == 0) eventsAfterFirstCycle_ = playbackEvents_;
    }
    // The window-title fallback (a NetEase Cloud Music window that publishes
    // no media session) through the production composite provider, on a
    // synthetic desktop: title and artist from the window title, inferred
    // playback, WM_APPCOMMAND transport and Event Log source netease, GSMTC
    // precedence, and the fixture session again once the player is gone.
    template <class Present>
    void windowPlayerCoverage(app::ModuleCoverage& step, double& t, const Present& present) {
        const auto& in = session_->input();
        const auto title = [&] { return in.track ? in.track->title : std::string(); };
        const auto playing = [&] { return in.track && in.track->isPlaying; };
        const auto origin = [&] {
            const auto& session = service_->snapshot().media.session;
            return session ? session->origin : modules::NowPlayingSessionOrigin::mediaSession;
        };
        const std::string player = "EndfieldHUD-Coverage\\cloudmusic.exe";
        const auto desktop = [&](std::string caption, std::uintptr_t window = 0x5150) {
            coverageWindows_->set({{window, 5150, player, "OrpheusBrowserHost", std::move(caption), true, false}});
            coverageWindows_->notify();
            settle(step);
            present(.3);
        };
        const auto events = playbackEvents_;
        const auto sent = coverageWindows_->commands().size();
        step.check(coverageWindows_->watchers() == 1, "The presented provider follows the synthetic desktop's window events");
        desktop("Coverage Song - Coverage Artist");
        step.check(title() == "Coverage Song" && in.track->artist == "Coverage Artist" && in.track->album.empty() && playing() &&
                       origin() == modules::NowPlayingSessionOrigin::playerWindow && service_->snapshot().media.sessions.size() == 2,
                   "A NetEase window title is presented over the paused fixture session");
        step.check(!in.track->position && !in.track->duration && !modules::nowPlayingControls(in).seek && !in.capabilities.seek &&
                       !in.volumeAvailable && !in.lyrics && modules::nowPlayingTime(in.track->duration) == "\xe2\x80\x94:\xe2\x80\x94" && !item("seek")->enabled,
                   "No timeline, seek, lyrics offline or single audio route for the window-title player");
        step.check(canvas_->perform("playPause", t), "Play/pause is enabled for the window-title player");
        settle(step);
        present(.3);
        auto commands = coverageWindows_->commands();
        step.check(commands.size() == sent + 1 && commands.back().window == 0x5150 && commands.back().pid == 5150 &&
                       commands.back().kind == modules::NowPlayingCommandKind::playPause && commands.back().onlyPlayer && !playing() &&
                       title() == "Coverage Song" && recorded(events + 1, "playPause", "netease"),
                   "Play/pause goes to the only player (its media-key hotkey or window), shown paused and logged with source netease");
        desktop("\xe7\xbd\x91\xe6\x98\x93\xe4\xba\x91\xe9\x9f\xb3\xe4\xb9\x90");
        step.check(title() == "Coverage Song" && !playing() && origin() == modules::NowPlayingSessionOrigin::playerWindow,
                   "The idle caption keeps the paused track; it keeps the tie with the paused fixture");
        desktop("Coverage Song 2 - Coverage Artist", 0x5151);
        step.check(title() == "Coverage Song 2" && playing(), "A new title in another main window is the next track, playing");
        step.check(canvas_->perform("next", t), "Next is enabled for the window-title player");
        settle(step);
        commands = coverageWindows_->commands();
        step.check(commands.size() == sent + 2 && commands.back().window == 0x5151 && commands.back().kind == modules::NowPlayingCommandKind::next &&
                       recorded(events + 2, "next", "netease"), "Next is posted to the current main window and logged with source netease");
        // The player's own controls (its accessibility tree): the elapsed and
        // total labels and its own next control, through the same provider.
        coverageWindows_->setReading(5150, gpu::NowPlayingPlayerReading{61., 200., true, true, true, true, true},
                                     [clock = fixtureClock_] { return clock->load(std::memory_order_acquire); });
        coverageWindows_->pressControls(true);
        desktop("Coverage Song 3 - Coverage Artist", 0x5152);
        step.check(title() == "Coverage Song 3" && in.track->duration == 200. && in.track->position &&
                       std::abs(*in.track->position - 61) < 1e-6 && modules::nowPlayingTime(in.track->duration) == "03:20" && modules::nowPlayingElapsed(in, t) &&
                       !in.capabilities.seek, "The player's own elapsed and total labels give the window-title track a timeline (no seek)");
        const auto pressed = coverageWindows_->presses().size();
        step.check(canvas_->perform("next", t), "Next is enabled with the player's own controls");
        settle(step);
        step.check(coverageWindows_->presses().size() == pressed + 1 && coverageWindows_->presses().back().kind == modules::NowPlayingCommandKind::next &&
                       coverageWindows_->commands().size() == commands.size() && recorded(events + 3, "next", "netease"),
                   "Next presses the player's own control (no key or window message) and is logged with source netease");
        coverageWindows_->pressControls(false);
        coverageWindows_->setReading(5150, std::nullopt);
        coverageWindows_->set({});
        coverageWindows_->notify();
        settle(step);
        present(.5);
        step.check(title() == "EndfieldHUD Fixture 1" && origin() == modules::NowPlayingSessionOrigin::mediaSession && !playing() &&
                       service_->snapshot().media.sessions.size() == 1, "When the player exits the fixture media session is presented again");
    }
    void idleCoverage(app::ModuleCoverage& step) {
        settle(step); // e.g. the withdrawn audio vote's publication
        step.present(step.time + .05);
        observe(step.time + .05);
        if (host_.selectedModule() == core::Module::nowPlaying && host_.overlayVisible()) {
            // Now Playing is still the last presented module of this cycle.
            step.check(service_->snapshot().active && canvas_->presented(), "The presented Now Playing keeps its single provider");
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
        step.check(!service_->snapshot().active && !canvas_->presented() && !nextWakeTime(step.time + .1) && !pointerLocked() && !capturesPointer(),
                   "Unpresented Now Playing keeps no provider, deadline or input");
        step.check(!audioFixture_->nowPlayingVisible() && audioFixture_->applications().empty() && !wake_->pending.load() && *starts_ == starts,
                   "Unpresented Now Playing withdraws its audio vote and starts nothing");
        step.check(!hostEvent({app::HostEvent::Kind::audioChanged, app::HostEvent::audioApplications}, step.time + .1),
                   "Audio publications are ignored while Now Playing is hidden");
        if (host_.selectedModule() != core::Module::nowPlaying) {
            // Memory. Once the HUD has closed on another module (the host's
            // overlayConcealed broadcast, delivered here as the host does)
            // the scene keeps no rasters and the cover no texture, and the
            // session only its presented cover; the next cycle's module step
            // shows the same artwork rebuilt.
            const auto held = core_->sceneStats(core::Module::nowPlaying);
            const auto memory = session_->memory();
            const auto entries = host_.rasterizer().stats().entries;
            (void)hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, step.time + .1);
            const auto parked = core_->sceneStats(core::Module::nowPlaying);
            const auto kept = session_->memory();
            if (held && held->generations)
                step.check(parked && parked->residentPixelBytes == 0 && cover_->stats().residentTextures == 0 &&
                               host_.rasterizer().stats().entries <= entries && kept.recentCovers == 0 && kept.artworkBytes <= memory.artworkBytes,
                           "Closed on another module, Now Playing releases its scene (" + std::to_string(held->residentPixelBytes) + " raster bytes, " +
                               std::to_string(entries - host_.rasterizer().stats().entries) + " shared raster entries) and its cover texture, and keeps " +
                               std::to_string(kept.artworkBytes) + " decoded cover bytes for its warm reopen");
        }
    }
    CoverageGroup* coverageGroup() {
        if (!coverageGroup_) coverageGroup_.Attach(new CoverageGroup(*this));
        return coverageGroup_.Get();
    }

    app::ApplicationHost& host_;
    app::UtilityExecutor& utility_;
    HWND window_{};
    const ApplicationAreaMode mode_;
    std::function<void(modules::EventKind, modules::EventMetadata)> record_;
    std::function<void()> invalidate_;
    std::uint32_t message_{};
    std::shared_ptr<Wake> wake_;
    std::shared_ptr<std::atomic<double>> fixtureClock_;
    std::shared_ptr<std::uint64_t> starts_ = std::make_shared<std::uint64_t>(0);
    std::shared_ptr<gpu::NowPlayingWindowFixture> coverageWindows_; // hidden coverage only
    std::shared_ptr<gpu::NowPlayingWindowSystem> windowSystem_;    // production only
    std::shared_ptr<gpu::NowPlayingProcessActivity> activity_;
    std::shared_ptr<const modules::NowPlayingAppVolume> volume_;
    // Declaration order is destruction order in reverse: the canvas first.
    std::unique_ptr<gpu::NativeNowPlayingService> service_;
    std::shared_ptr<gpu::NowPlayingWebCache> webCache_; // production with a data root
    std::shared_ptr<gpu::NowPlayingHttp> http_;          // production only
    std::shared_ptr<gpu::NowPlayingWeb> web_;
    std::unique_ptr<gpu::NowPlayingAudioFixture> audioFixture_;
    std::unique_ptr<tools::NowPlayingSession> session_;
    std::unique_ptr<gpu::NativeNowPlayingCover> cover_;
    std::unique_ptr<tools::DesignOwnerCore> core_;
    modules::NowPlayingCanvas* canvas_{};
    Microsoft::WRL::ComPtr<CoverageGroup> coverageGroup_; // hidden coverage only
    const d::Layer* insertedAfter_{};
    bool insertion_{}, httpRetained_{}, overlayVisible_{true};
    double time_{};
    std::uint64_t seenRevision_{};
    // Coverage evidence (never user content: action and source only).
    unsigned playbackEvents_{}, eventsAfterFirstCycle_{};
    std::uint64_t startsBefore_{};
    modules::EventMetadata lastPlayback_;
};
} // namespace

std::unique_ptr<app::ModuleOwner> nowPlayingModule(const app::ApplicationModuleContext& context) {
    // The shared executor, HUD window, clock and the design overlay are the
    // area's only services; without them the module stays absent rather than
    // inventing its own.
    if (!context.host || !context.utility || !context.window || !context.clock || !context.invalidate || !context.host->designContext()) return nullptr;
    return std::make_unique<NowPlayingArea>(context);
}
} // namespace endfield::areas
