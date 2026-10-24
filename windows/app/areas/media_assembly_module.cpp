#include "app/areas/media_assembly_module.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "core/localization.hpp"
#include "modules/notes_shelf_choice.hpp"
#include "native/media_assembly_codec.hpp"
#include "native/renderer.hpp"
#include "tools/media_assembly_owner.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <uiautomation.h>
#include <wrl/client.h>

namespace endfield::areas {
namespace {
namespace fs = std::filesystem;
namespace gpu = native;
namespace mod = modules;
namespace d = modules::design;
using Microsoft::WRL::ComPtr;

// Optional host services (see the header); exact types for the directory.
using ShelfChoices = std::function<std::vector<mod::NotesShelfChoice>()>;
using ShelfOpen = std::function<std::optional<std::pair<std::string, std::shared_ptr<void>>>(std::string_view)>;
constexpr std::string_view shelfChoicesKey = "fileShelf.mediaChoices", shelfOpenKey = "fileShelf.openReference",
    longWorkKey = "app.longWorkExecutor";

// Hidden coverage only: a deterministic synthetic still in the area's owned
// coverage root (WIC PNG encoder; never user media).
void writeSyntheticPNG(const fs::path& path, unsigned w, unsigned h, unsigned seed) {
    ComPtr<IWICImagingFactory> wic;
    const auto ok = [](HRESULT hr, const char* why) { if (FAILED(hr)) throw std::runtime_error(why); };
    ok(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)), "Media Assembly coverage needs WIC");
    ComPtr<IWICStream> stream; ok(wic->CreateStream(&stream), "WIC stream");
    ok(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "Owned synthetic still");
    ComPtr<IWICBitmapEncoder> encoder; ok(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "PNG encoder");
    ok(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "PNG encoder stream");
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> bag; ok(encoder->CreateNewFrame(&frame, &bag), "PNG frame");
    ok(frame->Initialize(bag.Get()), "PNG frame options"); ok(frame->SetSize(w, h), "PNG size");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA; ok(frame->SetPixelFormat(&format), "PNG format");
    std::vector<BYTE> pixels(std::size_t(w) * h * 4);
    for (unsigned i = 0; i < w * h; ++i) {
        pixels[i * 4] = BYTE((i + seed) % 251); pixels[i * 4 + 1] = BYTE((i / w) * 255 / h); pixels[i * 4 + 2] = BYTE((i % w) * 255 / w); pixels[i * 4 + 3] = 255;
    }
    ok(frame->WritePixels(h, w * 4, UINT(pixels.size()), pixels.data()), "PNG pixels");
    ok(frame->Commit(), "PNG frame commit"); ok(encoder->Commit(), "PNG commit"); ok(stream->Commit(STGC_DEFAULT), "PNG stream commit");
}

// Hidden coverage only: stands in for the owner's group under the HUD
// window's UI Automation root (app/hud_automation.cpp). The host passes that
// group to automationRefresh() as the fragments' parent and fragment root and
// routes hit tests and first/last child navigation to the owner hooks; this
// stand-in does the same, so coverage drives exactly the hooks the root calls.
class CoverageAutomationGroup final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot {
public:
    explicit CoverageAutomationGroup(const app::ModuleOwner& owner) : owner_(owner) {}
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot)) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override { const ULONG n = --refs_; if (!n) delete this; return n; }
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_POINTER;
        *out = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading); return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID, VARIANT* out) override { if (!out) return E_POINTER; VariantInit(out); return S_OK; }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const auto n = owner_.automationCount();
        if (!n || (direction != NavigateDirection_FirstChild && direction != NavigateDirection_LastChild)) return S_OK;
        const long hr = owner_.automationChild(direction == NavigateDirection_FirstChild ? 0 : n - 1, out);
        if (hr < 0 && *out) { (*out)->Release(); *out = nullptr; }
        return S_OK;
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        int ids[2]{UiaAppendRuntimeId, 0x4d410001};
        SAFEARRAY* a = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!a) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 2; ++i) if (FAILED(SafeArrayPutElement(a, &i, &ids[i]))) { SafeArrayDestroy(a); return E_FAIL; }
        *out = a; return S_OK;
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* out) override { if (!out) return E_POINTER; *out = {}; return S_OK; }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    IFACEMETHODIMP SetFocus() override { return S_OK; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = this; AddRef(); return S_OK;
    }
    IFACEMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const long hr = owner_.automationFromPoint(x, y, out);
        if (hr < 0 && *out) { (*out)->Release(); *out = nullptr; }
        return hr < 0 ? HRESULT(hr) : S_OK;
    }
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
private:
    ~CoverageAutomationGroup() = default;
    const app::ModuleOwner& owner_;
    std::atomic<ULONG> refs_{1};
};
// COM identity: both pointers answer the same IUnknown.
bool sameObject(IUnknown* a, IUnknown* b) {
    if (!a || !b) return false;
    ComPtr<IUnknown> x, y;
    return SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get() == y.Get();
}
std::wstring automationText(IRawElementProviderFragment* fragment, PROPERTYID id) {
    ComPtr<IRawElementProviderSimple> simple; VARIANT v; VariantInit(&v); std::wstring out;
    if (fragment && SUCCEEDED(fragment->QueryInterface(IID_PPV_ARGS(&simple))) && SUCCEEDED(simple->GetPropertyValue(id, &v)) && v.vt == VT_BSTR && v.bstrVal)
        out.assign(v.bstrVal, SysStringLen(v.bstrVal));
    VariantClear(&v); return out;
}

struct Inputs {
    std::shared_ptr<gpu::NativeMediaAssemblyEngine> engine;
    gpu::NativeMediaRequestBroker* broker{};
    core::MediaClient client{};
    UINT dialogMessage{};
};
struct Events { unsigned imported{}, edited{}, exported{}; };
// What the owner's services record or answer (hidden coverage reads it).
struct Coverage {
    Events events;
    std::deque<std::optional<std::string>> answers;
    std::vector<gpu::MediaAssemblyDialogRequest> dialogs;
    std::weak_ptr<int> shelfLease;
};

class MediaAssemblyModule final : public tools::MediaAssemblyOwner {
public:
    MediaAssemblyModule(const app::ApplicationModuleContext& c, d::Context& context, fs::path assets, Inputs inputs, tools::MediaAssemblyOwnerOptions options,
        std::shared_ptr<Coverage> state)
        : MediaAssemblyOwner(c.rasterizer, *c.utility, context, inputs.engine, prepare(std::move(options), c, assets, inputs, state)),
          host_(*c.host), context_(context), window_(static_cast<HWND>(c.window)), utility_(*c.utility), engine_(inputs.engine),
          coverageMode_(c.mode == app::ApplicationAreaMode::hiddenCoverage), coverageRoot_(c.coverageRoot), dialogMessage_(inputs.dialogMessage),
          coverageState_(std::move(state)) {
        if (coverageMode_ && c.fixture) movieFixture_ = c.fixture("mediaAssembly.movie");
    }
    MediaAssemblyModule(const MediaAssemblyModule&) = delete;
    MediaAssemblyModule& operator=(const MediaAssemblyModule&) = delete;

    // The shared broker was sampled/accepted before this frame: re-read the
    // retained player record (no work without a requested player).
    void update(const app::ModuleFrame& f) override { (void)refreshSharedMedia(f.time); MediaAssemblyOwner::update(f); }
    bool hostEvent(const app::HostEvent& e, double t) override {
        // A concealed HUD released its player: retire the broker records now.
        if (e.kind == app::HostEvent::Kind::overlayConcealed) (void)refreshSharedMedia(t);
        return MediaAssemblyOwner::hostEvent(e, t);
    }
    void coverage(app::ModuleCoverage& step) override {
        if (!coverageMode_) return;
        if (step.module) coverModule(step); else coverIdle(step);
    }

private:
    app::ApplicationHost& host_;
    d::Context& context_;
    HWND window_{};
    app::UtilityExecutor& utility_;
    std::shared_ptr<gpu::NativeMediaAssemblyEngine> engine_;
    bool coverageMode_{};
    fs::path coverageRoot_, movieFixture_;
    UINT dialogMessage_{};
    std::shared_ptr<Coverage> coverageState_;
    unsigned coveredCycles_{};

    // The owner's services: production host services or hidden-coverage stand-ins.
    static tools::MediaAssemblyOwnerOptions prepare(tools::MediaAssemblyOwnerOptions o, const app::ApplicationModuleContext& c, const fs::path& assets, const Inputs& inputs,
        const std::shared_ptr<Coverage>& state) {
        auto& host = *c.host;
        const bool coverage = c.mode == app::ApplicationAreaMode::hiddenCoverage;
        const auto window = static_cast<HWND>(c.window);
        o.assetRoot = assets;
        o.window = window; o.dialogMessage = inputs.dialogMessage;
        // OverlayController.onEvent: EventLog mediaAssemblyAction {action}.
        o.event = [state, record = c.recordEvent](std::string_view action) {
            if (action == "imported") ++state->events.imported; else if (action == "edited") ++state->events.edited; else if (action == "exported") ++state->events.exported;
            if (record) record(mod::EventKind::mediaAssemblyAction, {{"action", std::string(action)}});
        };
        o.uuid = [] { return ehud::data::makeUUID(); };
        if (coverage) {
            // Synthetic Temporary File Shelf: one owned still and one
            // unsupported row; the lease is released after the import.
            const auto root = c.coverageRoot;
            o.shelfChoices = [] { return std::vector<mod::MediaAssemblyShelfChoice>{{"coverage-shelf-1", "Shelf still.png", true, true}, {"coverage-shelf-2", "notes.txt", false, true}}; };
            o.shelfAccess = [state, root](std::string_view id) -> std::optional<mod::MediaAssemblyShelfFile> {
                if (id != "coverage-shelf-1") throw mod::MediaAssemblyFailure(mod::MediaAssemblyError::unavailable);
                auto lease = std::make_shared<int>(1); state->shelfLease = lease;
                return mod::MediaAssemblyShelfFile{gpu::mediaAssemblyUTF8(root / L"Shelf still.png"), std::move(lease)};
            };
            // Injected dialog: answers queued by the coverage step, never UI.
            o.dialog = [state, window](HWND owner, const gpu::MediaAssemblyDialogRequest& request) -> std::optional<std::string> {
                if (owner != window) throw std::logic_error("Media Assembly dialogs are modal to the HUD window");
                state->dialogs.push_back(request);
                if (state->answers.empty()) return std::nullopt;
                auto answer = std::move(state->answers.front()); state->answers.pop_front(); return answer;
            };
        } else {
            // Modal IFileOpenDialog/IFileSaveDialog owned by the HUD (same UI
            // thread, so no application deactivation closes the HUD). Like the
            // host's shared picker, the HUD gives up pointer capture first.
            o.dialog = [](HWND owner, const gpu::MediaAssemblyDialogRequest& request) {
                if (GetCapture() == owner) (void)ReleaseCapture();
                return gpu::showMediaAssemblyDialog(owner, request);
            };
            auto* services = &host;
            o.shelfChoices = [services] {
                std::vector<mod::MediaAssemblyShelfChoice> out;
                const auto* service = services->find<ShelfChoices>(shelfChoicesKey);
                if (service && *service) for (auto& choice : (*service)()) out.push_back({std::move(choice.id), std::move(choice.title), choice.supported, choice.available});
                return out;
            };
            o.shelfAccess = [services](std::string_view id) -> std::optional<mod::MediaAssemblyShelfFile> {
                const auto* service = services->find<ShelfOpen>(shelfOpenKey);
                if (!service || !*service) return std::nullopt;
                auto opened = (*service)(id);
                // The row's file could not be opened (the source ShelfFileAccess throws).
                if (!opened) throw mod::MediaAssemblyFailure(mod::MediaAssemblyError::unavailable);
                return mod::MediaAssemblyShelfFile{std::move(opened->first), std::move(opened->second)};
            };
        }
        o.broker = inputs.broker; o.client = inputs.client;
        o.exportExecutor = host.find<app::UtilityExecutor>(longWorkKey);
        return o;
    }

    // ---- Hidden coverage (synthetic inputs in the owned coverage root) ----
    struct Run {
        MediaAssemblyModule& self;
        app::ModuleCoverage& step;
        double t;
        mod::MediaAssemblyCanvas& canvas() { return self.canvas(); }
        const mod::MediaAssemblySession& session() { return self.controller().session(); }
        // A host frame on the coverage clock, with the shared design deadlines
        // the overlay owner runs in the real loop (swaps, timers, the confirm
        // callback, the export ticker).
        void present(double dt = 1. / 60) { t += dt; (void)self.context_.advance(t); (void)self.deadline(t); step.present(t); }
        void frames(double seconds) { const auto end = t + seconds; while (t < end) present(); }
        // Completions run in the coverage drain; each pass presents one frame
        // so the owner uploads what completed and pumps the next request.
        void settle() {
            for (unsigned n = 0; n < 24; ++n) {
                step.drain(); present();
                const auto& s = session();
                if (!s.busy() && !s.working() && !s.pendingCount() && !s.exporting()) break;
            }
            const auto& s = session();
            step.check(!s.busy() && !s.working() && !s.pendingCount() && !s.exporting(), "Media Assembly work settles through the shared executor");
        }
        const d::Control* control(std::string_view id) {
            for (const auto* c : canvas().header().controls()) if (c->id() == id) return c;
            for (const auto* c : canvas().page().controls()) if (c->id() == id) return c;
            return nullptr;
        }
        std::optional<core::Point> client(core::Point box) {
            const auto r = self.core().clientRect({box.x, box.y, 0, 0});
            return r ? std::optional(core::Point{r->x, r->y}) : std::nullopt;
        }
        bool click(core::Point box) {
            const auto p = client(box);
            step.check(p.has_value(), "Media Assembly input projects through its content box");
            const bool consumed = self.pointer({app::PointerKind::down, app::PointerButton::left, p->x, p->y, 0}, t);
            (void)self.pointer({app::PointerKind::up, app::PointerButton::left, p->x, p->y, 0}, t);
            return consumed;
        }
        bool clickControl(std::string_view id) {
            const auto* c = control(id);
            step.check(c != nullptr, "The design control is laid out");
            const auto r = c->rect(canvas().root());
            return click({r.x + r.width / 2, r.y + r.height / 2});
        }
        bool perform(std::string_view id) { return canvas().perform(id, t); }
        bool key(std::uint32_t vk, std::uint32_t modifiers = 0) { return self.key({app::KeyKind::down, vk}, modifiers, t); }
        // The owner posted its dialog notice to the HUD HWND: take exactly that
        // private message and route it through the owners' message chain.
        bool routeDialog() {
            MSG msg{}; bool routed{};
            while (PeekMessageW(&msg, self.window_, self.dialogMessage_, self.dialogMessage_, PM_REMOVE | PM_NOYIELD))
                routed = step.message(msg.message, msg.wParam, msg.lParam) || routed;
            return routed;
        }
        // The confirm bar's decision (Return / the pill), its 120 ms callback included.
        bool confirm(std::string_view id) {
            auto& overlay = self.context_.overlay();
            if (!overlay.isConfirming() || overlay.confirmState().request()->id != id) return false;
            (void)overlay.decide(d::ConfirmState::Decision::confirm);
            frames(.3);
            return true;
        }
    };
    void coverModule(app::ModuleCoverage& step) {
        step.check(*step.module == core::Module::mediaAssembly && !coverageRoot_.empty(), "Media Assembly coverage owns its selected module and coverage root");
        Run run{*this, step, step.time};
        auto& canvas = this->canvas();
        auto& state = *coverageState_;
        const auto cycle = step.cycle;
        run.present();
        step.check(!entries(core::Module::mediaAssembly).empty() && !run.session().document() && canvas.stage().isEmpty(),
                   "Selected Media Assembly publishes its empty stage ([ NO SOURCE ])");
        const auto* closeButton = run.control("close"); const auto* exportButton = run.control("export");
        step.check(closeButton && !closeButton->enabled() && exportButton && !exportButton->enabled(), "With no source Close and Export are disabled");
        // GPU previews: build-time programs (or, without fxc, the HLSL compile
        // queued on the shared worker when the editor was first shown); the
        // processor exists once they are ready.
        bool mediaCapable{};
        try { mediaCapable = host_.renderer().mediaDevice() != nullptr; } catch (const std::exception&) {}
        step.check(mediaCapable, "The application renderer offers same-device media (RendererOptions.mediaVideo)");
        run.settle(); run.present();
        step.check(stats().gpuActive == mediaCapable && controller().deferredPreviews() == mediaCapable,
                   "Previews are edited on the renderer's media device when it has one");

        // Synthetic inputs, rewritten every cycle (identical bytes, new identity).
        const auto photo = coverageRoot_ / L"Owned Photo.png", shelfStill = coverageRoot_ / L"Shelf still.png";
        writeSyntheticPNG(photo, 320, 200, 0); writeSyntheticPNG(shelfStill, 64, 48, 7);
        const auto before = state.events;

        // Open -> source menu -> File Explorer (posted, modal, injected).
        step.check(run.clickControl("open") && canvas.menuOpen(), "Projected Open shows the source menu");
        run.frames(.1);
        step.check(capturesPointer() && capturesKeys() && capturesWheel(), "An open menu captures input before the controls behind it");
        step.check(run.key(VK_ESCAPE) && !canvas.menuOpen(), "Escape closes the menu first");
        run.frames(.25);
        step.check(run.clickControl("open") && canvas.menuOpen(), "The menu opens again");
        state.answers.push_back(gpu::mediaAssemblyUTF8(photo));
        const auto dialogsBefore = state.dialogs.size();
        step.check(run.perform("open:finder") && !canvas.menuOpen() && canvas.presentingPanel(), "Choose in File Explorer closes the menu and presents the panel");
        step.check(run.routeDialog() && state.dialogs.size() == dialogsBefore + 1 && state.dialogs.back().kind == gpu::MediaAssemblyDialogRequest::Kind::open &&
                   !state.dialogs.back().extensions.empty() && !canvas.presentingPanel(), "The open dialog runs from the posted notice through the message chain");
        run.settle(); run.frames(.5);
        const auto& session = controller().session();
        step.check(session.document() && session.document()->name == "Owned Photo.png" && !session.document()->video && state.events.imported == before.imported + 1,
                   "The chosen still opens and records one imported event");
        step.check(controller().previewImage() && controller().previewImage()->width == 320 && canvas.stage().image() == controller().previewImage() &&
                   stats().retainedTextures == 1 && stats().pictureDraws == 1, "One bounded preview is shown on the stage");
        step.check(canvas.counterText() == "IMG 01" && canvas.stage().chipTitles() == std::vector<std::string>{"Owned Photo.png"}, "The header counter and the source chip");
        {   // Warm frames reuse pixels, text and textures and enqueue nothing.
            const auto raster = host_.rasterizer().stats(); const auto uploads = stats().previewUploads;
            const auto textures = host_.renderer().stats().textureUploads; const auto accepted = utility_.stats().accepted;
            run.frames(.2);
            step.check(host_.rasterizer().stats().rasterizations == raster.rasterizations && stats().previewUploads == uploads &&
                       host_.renderer().stats().textureUploads == textures && utility_.stats().accepted == accepted, "Settled Media Assembly frames repaint and enqueue nothing");
        }

        // Rotate turns the picture (.25 s) and re-renders the bounded preview.
        step.check(run.perform("tool:rotate"), "Rotate is available"); run.settle(); run.frames(.35); run.settle();
        step.check(session.adjustments().rotationQuarterTurns == 1 && controller().previewImage()->width == 200 && canvas.stage().image() == controller().previewImage(),
                   "Rotate re-renders the bounded preview and the stage takes it after the transition");
        // Edited event debounced on the host deadline (0.4 s), recorded once.
        const auto wake = nextWakeTime(run.t);
        step.check(wake.has_value() && state.events.edited == before.edited, "The edited event waits on the shared deadline");
        run.t = std::max(run.t, *wake); step.check(deadline(run.t) && state.events.edited == before.edited + 1, "Edited recorded once when the deadline fires");
        run.present();
        // A tool sub-page; its sliders through UI Automation.
        step.check(run.perform("tool:adjust") && canvas.tool() == mod::MediaAssemblyTool::adjust && canvas.parameterRows().size() == 8, "Adjust opens its eight sliders");
        {   // UI Automation through the ModuleOwner hooks the HUD window's root
            // provider calls, under a stand-in for this owner's group.
            ComPtr<CoverageAutomationGroup> group; group.Attach(new CoverageAutomationGroup(*this));
            IRawElementProviderFragmentRoot* root = group.Get();
            step.check(automationRefresh(root) && automationCount() > 8, "One UIA fragment per design control under the owner's group");
            step.check(!automationRefresh(root), "An unchanged layout refreshes without a structure change");
            {   ComPtr<IRawElementProviderFragment> first, parent; ComPtr<IRawElementProviderFragmentRoot> fragmentRoot;
                step.check(SUCCEEDED(group->Navigate(NavigateDirection_FirstChild, &first)) && first, "The group lists the canvas's controls");
                step.check(SUCCEEDED(first->Navigate(NavigateDirection_Parent, &parent)) && sameObject(parent.Get(), root) &&
                           SUCCEEDED(first->get_FragmentRoot(&fragmentRoot)) && sameObject(fragmentRoot.Get(), root),
                           "Fragments report the owner's group as parent and fragment root"); }
            const auto element = [&](std::wstring_view id) {
                ComPtr<IRawElementProviderFragment> found;
                for (std::size_t n = 0; n < automationCount() && !found; ++n) {
                    ComPtr<IRawElementProviderFragment> fragment;
                    if (automationChild(n, &fragment) >= 0 && fragment && automationText(fragment.Get(), UIA_AutomationIdPropertyId) == id) found = fragment;
                }
                return found;
            };
            ComPtr<IRangeValueProvider> brightness;
            if (const auto fragment = element(L"parameter:brightness")) (void)fragment.As(&brightness);
            step.check(brightness && SUCCEEDED(brightness->SetValue(.25)) && std::abs(session.adjustments().brightness - .25) < 1e-9, "UIA RangeValue sets Brightness through its slider");
            ComPtr<IInvokeProvider> back;
            if (const auto fragment = element(L"toolBack")) (void)fragment.As(&back);
            step.check(back && SUCCEEDED(back->Invoke()) && !canvas.tool(), "UIA Invoke leaves the tool");
            canvas.settleSwap();
            step.check(automationRefresh(root) && !element(L"parameter:brightness") && element(L"tool:adjust"), "The fragments follow the tiles");
            ComPtr<IInvokeProvider> adjust;
            if (const auto fragment = element(L"tool:adjust")) (void)fragment.As(&adjust);
            step.check(adjust && SUCCEEDED(adjust->Invoke()) && canvas.tool() == mod::MediaAssemblyTool::adjust, "UIA Invoke opens Adjust again");
            canvas.settleSwap();
            (void)automationRefresh(root); // the reopened tool's sliders are listed again
            const auto held = element(L"parameter:brightness");
            automationDisconnect();
            ComPtr<IRawElementProviderSimple> simple; VARIANT stale; VariantInit(&stale);
            step.check(automationCount() == 0 && held && SUCCEEDED(held.As(&simple)) && simple->GetPropertyValue(UIA_NamePropertyId, &stale) == UIA_E_ELEMENTNOTAVAILABLE,
                       "Disconnect removes every fragment and a held one is no longer available");
            VariantClear(&stale);
            step.check(automationRefresh(root) && automationCount() > 0, "The next refresh connects the current fragments again");
            automationDisconnect();
        }
        run.settle();
        step.check(run.key(VK_ESCAPE) && !canvas.tool(), "Escape leaves the active tool first");
        run.frames(.4);
        // Zoom: a mouse wheel notch x1.1 about the pointer, Ctrl+wheel the
        // pinch, '-' and the zoom value back.
        {   const auto mid = run.client({mod::MediaAssemblyCanvas::stageOrigin.x + 224, mod::MediaAssemblyCanvas::stageOrigin.y + 206});
            step.check(mid.has_value(), "The stage centre projects");
            const auto zoom = canvas.viewport().zoom();
            step.check(wheel({mid->x, mid->y, 1, false, 0, 3}, false, run.t) && std::abs(canvas.viewport().zoom() - zoom * 1.1) < 1e-9, "A mouse wheel notch zooms x1.1");
            const auto notched = canvas.viewport().zoom();
            step.check(wheel({mid->x, mid->y, 1, false, MK_CONTROL, 3}, false, run.t) && canvas.viewport().zoom() > notched, "Ctrl+wheel (the pinch) zooms the preview");
            const auto zoomed = canvas.viewport().zoom();
            step.check(run.key(VK_OEM_MINUS) && canvas.viewport().zoom() < zoomed, "Minus zooms out");
            step.check(run.perform("zoomReset") && canvas.viewport().zoom() == 1, "The zoom value returns to the fitted preview"); }
        run.present();
        // Stickers: add, Shift+Right nudge, Alt+Right turn, Backspace delete.
        step.check(run.perform("mediaTab:2") && canvas.tab() == mod::MediaAssemblyCanvas::Tab::stickers, "Stickers tab");
        step.check(run.perform("sticker:sticker_bp_1") && session.adjustments().stickers.size() == 1 && !canvas.selectedSticker().empty() &&
                   canvas.tab() == mod::MediaAssemblyCanvas::Tab::stickers && std::abs(session.adjustments().stickers[0].size - .25) < 1e-12,
                   "A placed sticker gets a new identity at .25, is selected and the Stickers tab stays open");
        {   const auto x = session.adjustments().stickers[0].x;
            step.check(run.key(VK_RIGHT, 4) && std::abs(session.adjustments().stickers[0].x - x - .05) < 1e-9, "Shift+Right nudges the selected sticker by 0.05");
            step.check(run.key(VK_RIGHT, 1) && session.adjustments().stickers[0].rotation == 3, "Alt+Right turns it 3 degrees"); }
        run.present();
        step.check(stats().thumbnailBytes > 0 && stats().thumbnailBytes <= 30u * 96 * 96 * 4, "The drawer's sticker thumbnails are bounded 96 px decodes");
        step.check(run.key(VK_BACK) && session.adjustments().stickers.empty(), "Backspace deletes the selected sticker");
        run.settle();

        // Export -> Save As (posted dialog, injected answer) -> atomic commit.
        const auto exported = coverageRoot_ / (L"Coverage export " + std::to_wstring(cycle) + L".png");
        std::error_code ignored; fs::remove(exported, ignored);
        step.check(run.perform("export") && canvas.menuOpen(), "Export offers its menu");
        state.answers.push_back(gpu::mediaAssemblyUTF8(exported));
        step.check(run.perform("export:saveAs") && run.routeDialog() && state.dialogs.back().kind == gpu::MediaAssemblyDialogRequest::Kind::save &&
                   state.dialogs.back().filename == "Owned Photo-edited.png", "Save As proposes the source folder and suggested name");
        step.check(session.exporting() && canvas.isExporting() && nextWakeTime(run.t).has_value(), "Export progress uses the deadlines only while exporting");
        run.settle(); run.frames(1.5);
        step.check(fs::is_regular_file(exported) && state.events.exported == before.exported + 1 && !session.error() && !canvas.isExporting(),
                   "Save As exports atomically; the progress display finished");
        // Overwrite original: the shared confirm bar, then a silent re-import.
        const auto documentID = session.document()->id;
        step.check(run.perform("export") && run.perform("export:overwrite"), "Overwrite original is offered");
        {   const auto& request = context_.overlay().confirmState().request();
            step.check(request && request->id == "mediaOverwrite" && request->message == "Owned Photo.png" && request->danger, "Overwrite asks on the confirm bar"); }
        step.check(run.confirm("mediaOverwrite"), "Confirm overwrite");
        run.settle(); run.frames(1.5); run.settle();
        step.check(session.document() && session.document()->id != documentID && state.events.imported == before.imported + 1 && state.events.exported == before.exported + 2,
                   "The overwritten original reloads without a second imported event");
        {   bool temporary{};
            for (const auto& entry : fs::directory_iterator(coverageRoot_)) temporary = temporary || entry.path().filename().wstring().starts_with(L".endfield-export-");
            step.check(!temporary, "No temporary export file is left behind"); }

        // Source state changes: edited elsewhere -> changedOnDisk; removed ->
        // unavailable ("choose it again"); choosing it again restores it.
        step.check(run.perform("mediaTab:0") && canvas.tab() == mod::MediaAssemblyCanvas::Tab::adjust, "Back to the Adjust tiles");
        { std::ofstream(photo, std::ios::binary | std::ios::app) << 'x'; }
        step.check(run.perform("tool:mirror"), "Mirror edits the changed source"); run.settle();
        step.check(session.error() == mod::MediaAssemblyError::changedOnDisk, "A source changed on disk is reported, not silently reloaded");
        fs::remove(photo);
        step.check(run.perform("tool:mirror"), "Mirror edits the removed source"); run.settle();
        step.check(session.error() == mod::MediaAssemblyError::sourceUnavailable &&
                   mod::mediaAssemblyErrorMessage(*session.error(), core::Language::english) == "The original media file is unavailable. Choose it again to restore access.",
                   "A removed source asks to be chosen again");
        writeSyntheticPNG(photo, 320, 200, 0);
        {   const std::array<std::string, 2> dropped{gpu::mediaAssemblyUTF8(photo), gpu::mediaAssemblyUTF8(shelfStill)};
            step.check(drop(dropped, {}, run.t), "A drop imports its first file"); }
        run.settle(); run.frames(.5);
        step.check(session.document() && session.document()->name == "Owned Photo.png" && !session.error() && state.events.imported == before.imported + 2,
                   "Choosing the original again restores editing");

        // Temporary File Shelf source (synthetic provider), lease released.
        step.check(run.perform("open") && run.perform("open:shelf") && canvas.menuOpen(), "Choose from Shelf lists the Shelf");
        step.check(run.perform("shelf:coverage-shelf-1") && !canvas.menuOpen(), "A supported Shelf row imports");
        run.settle(); run.frames(.5);
        step.check(session.document() && session.document()->name == "Shelf still.png" && state.shelfLease.expired() && state.events.imported == before.imported + 3,
                   "The Shelf lease is released after the open");

        // Owned silent movie: poster frame, seek keys, trim; never played
        // (no audio device is opened in hidden coverage).
        if (!movieFixture_.empty()) {
            const auto movie = coverageRoot_ / L"Owned movie.mp4";
            if (!fs::exists(movie)) fs::copy_file(movieFixture_, movie);
            const std::array<std::string, 1> dropped{gpu::mediaAssemblyUTF8(movie)};
            step.check(drop(dropped, {}, run.t), "The owned movie is dropped"); run.settle(); run.frames(.5);
            step.check(session.document() && session.document()->video && session.document()->duration > .3 && controller().previewImage(),
                       "The movie opens with a bounded poster frame");
            const auto* play = run.control("play"); const auto* trim = run.control("tool:trim");
            step.check(play && play->enabled() && trim && canvas.counterText() == "VID 01", "Movies offer playback and Trim");
            step.check(run.perform("mediaTab:2") && run.control("sticker:sticker_bp_1") && !run.control("sticker:sticker_bp_1")->enabled(), "The Stickers grid is inert for movies");
            step.check(run.perform("mediaTab:0"), "Back to the tiles");
            const auto duration = session.document()->duration;
            step.check(run.key(VK_RIGHT) && session.currentTime() > 0 && session.currentTime() <= duration, "Right seeks forward within the movie");
            run.settle();
            step.check(run.key(VK_LEFT) && session.currentTime() == 0, "Left seeks back to the start");
            step.check(run.perform("tool:trim") && canvas.tool() == mod::MediaAssemblyTool::trim && canvas.parameterRows().size() == 2, "Trim: Start and End sliders");
            step.check(adjustAccessibilitySlider("parameter:trimStart", .1) && std::abs(session.adjustments().trimStart - .1) < 1e-9, "Trim start follows its slider");
            run.settle();
            step.check(run.key(VK_ESCAPE) && !canvas.tool(), "Leaving the trim tool");
            run.frames(.4);
            step.check(stats().playbackStarts == 0 && !stats().videoShown, "Hidden coverage never starts a player");
        }

        // Close: asks first (danger with edits), the picture fades (0.16 s) and its texture is released.
        step.check(run.perform("close"), "Close");
        {   const auto& request = context_.overlay().confirmState().request();
            step.check(request && request->id == "mediaClose" && request->danger == (session.adjustments() != mod::MediaAssemblyAdjustments{}), "Close asks on the confirm bar"); }
        step.check(run.confirm("mediaClose"), "Confirm close");
        run.frames(.4); run.settle();
        const auto released = [&] { return stats().retainedTextures == 0; };
        for (unsigned n = 0; n < 60 && !released(); ++n) run.present();
        step.check(!session.document() && canvas.stage().isEmpty() && released() && !canvas.menuOpen(),
                   "Close releases the preview after its fade (document " + std::to_string(bool(session.document())) + ", textures " + std::to_string(stats().retainedTextures) + ")");
        run.frames(.3);
        step.check(engine_->stats().cachedSourceBytes <= std::size_t(1024) * 1024 * 4, "The engine keeps at most one bounded decoded source");
        ++coveredCycles_;
    }
    bool adjustAccessibilitySlider(std::string_view id, double value) { return canvas().adjustAccessibilitySlider(id, value, host_.now()); }
    // Once per cycle: nothing open, scheduled or retained after the module step.
    void coverIdle(app::ModuleCoverage& step) {
        auto& state = *coverageState_;
        step.check(coveredCycles_ == step.cycle + 1, "The Media Assembly module step ran in this cycle");
        step.check(!controller().session().document() && !canvas().menuOpen() && !controller().session().exporting() &&
                   !controller().nextWakeTime() && stats().retainedTextures == 0, "Media Assembly parks with nothing open, scheduled or retained");
        // Per cycle: chooser, re-chosen original, Shelf and (when declared) the
        // movie import once each; the overwrite re-import records nothing.
        step.check(state.events.imported == coveredCycles_ * (movieFixture_.empty() ? 3u : 4u) && state.events.exported == 2 * coveredCycles_ &&
                   state.events.edited >= coveredCycles_, "Event Log actions follow the source rules");
        step.check(state.answers.empty() && stats().playbackStarts == 0, "Every injected dialog answer was consumed; no player started");
        // Production looks these up by exact type (coverage keeps its synthetic
        // Shelf provider): a type drift would silently empty "Choose from Shelf".
        step.check(host_.find<app::UtilityExecutor>(longWorkKey) && host_.find<ShelfChoices>(shelfChoicesKey) && host_.find<ShelfOpen>(shelfOpenKey),
                   "The host publishes the long-work executor and the Shelf services with the exact types the area looks up");
        step.check(stats().frameFilterHook && stats().frameFilterRoute,
                   "The shared frame server offers the played-frame filter hook and the media broker carries the filter to it");
        {   // Memory. Hidden inside the open HUD the editor keeps no decoded
            // source on the CPU or GPU (source setActive(false) clears the
            // engine caches); once the HUD has closed (the host's
            // overlayConcealed broadcast, delivered here as the host does) a
            // canvas that is not composed releases its artwork.
            step.drain();
            const auto hidden = stats();
            step.check(hidden.previewPixelBytes == 0 && hidden.gpuResidentBytes == 0 && hidden.playbackResidentBytes == 0 && hidden.retainedTextures == 0 && engine_->stats().cachedSourceBytes == 0,
                       "Hidden Media Assembly keeps no decoded source, preview pixels or GPU source textures (" + std::to_string(hidden.previewPixelBytes) + " CPU, " +
                       std::to_string(hidden.gpuResidentBytes + hidden.playbackResidentBytes) + " GPU bytes)");
            const bool composed = !entries(core::Module::mediaAssembly).empty();
            const auto entriesBefore = host_.rasterizer().stats().entries;
            (void)hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, step.time);
            const auto parked = stats();
            if (composed) step.check(parked.parks == hidden.parks, "A composed Media Assembly keeps its canvas until it is hidden");
            else step.check(hidden.sceneParts > 0 && parked.parks == hidden.parks + 1 && parked.sceneParts == 0 && parked.retainedTextures == 0 &&
                            host_.rasterizer().stats().entries < entriesBefore,
                            "Once the HUD is closed the hidden Media Assembly releases its canvas artwork (" + std::to_string(hidden.sceneParts) + " parts, " +
                            std::to_string(entriesBefore - host_.rasterizer().stats().entries) + " shared raster entries)");
        }
        {   // The host's refresh after the module step: the fragments follow the
            // owner (none while Media Assembly is not the accepting module).
            ComPtr<CoverageAutomationGroup> group; group.Attach(new CoverageAutomationGroup(*this));
            (void)automationRefresh(group.Get());
            const bool accepting = core().inputModule() == core::Module::mediaAssembly;
            step.check(accepting || automationCount() == 0, "UIA fragments follow the parked owner");
            automationDisconnect();
            step.check(automationCount() == 0, "Disconnected UIA fragments leave the owner's group empty");
        }
    }
};
}

std::unique_ptr<app::ModuleOwner> mediaAssemblyModule(const app::ApplicationModuleContext& c) {
    if (!c.host || !c.utility || !c.window) return nullptr;
    // The redesigned canvas needs the HUD's design Context (the overlay owner).
    auto* context = c.host->designContext();
    if (!context) return nullptr;
    // Production: the SHA-256 verified package only. Hidden coverage without
    // a staged package uses the declared source-tree fixture (still pinned by
    // the asset catalog); development previews have no package.
    auto assets = c.resource ? c.resource("mediaAssembly.assets") : fs::path{};
    if (assets.empty() && c.mode == app::ApplicationAreaMode::hiddenCoverage && c.fixture) assets = c.fixture("mediaAssembly.sourceAssets");
    if (assets.empty()) return nullptr;
    app::ModuleRouting routing;
    // Areas route after the built-in owners; the posted dialog notice needs the message chain.
    routing.pointerCapture = 20; routing.pointer = 20; routing.wheelCapture = 20; routing.wheel = 20; routing.keyCapture = 20; routing.key = 20; routing.message = 20;
    tools::MediaAssemblyOwnerOptions options;
    if (!c.host->designOwnerOptions(options.design, "mediaAssembly", routing)) return nullptr;
    Inputs inputs;
    inputs.engine = std::make_shared<gpu::NativeMediaAssemblyEngine>(gpu::NativeMediaAssemblyEngine::Options{assets});
    inputs.dialogMessage = c.host->registerMessage();
    // The app's ONE shared media broker (AVPlayer equivalent, with audio); the owner detaches the client.
    if ((inputs.broker = c.host->mediaBroker())) inputs.client = inputs.broker->attachClient();
    std::unique_ptr<app::ModuleOwner> owner;
    try { owner = std::make_unique<MediaAssemblyModule>(c, *context, std::move(assets), inputs, std::move(options), std::make_shared<Coverage>()); }
    catch (...) { if (inputs.broker && inputs.client) try { (void)inputs.broker->detachClient(inputs.client); } catch (...) {} throw; }
    // Not a tools::DesignModuleOwner (it wraps the core): marked here.
    c.host->setDesignScreen(core::Module::mediaAssembly);
    return owner;
}
} // namespace endfield::areas
