#include "app/areas/media_assembly_module.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "core/localization.hpp"
#include "modules/media_assembly_model.hpp"
#include "native/layer_raster.hpp"
#include "native/media_assembly_codec.hpp"
#include "native/renderer.hpp"
#include "tools/media_assembly_preview.hpp"
#include <algorithm>
#include <array>
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
#include <wrl/client.h>

namespace endfield::areas {
namespace {
namespace fs = std::filesystem;
namespace gpu = native;
namespace mod = modules;
using Microsoft::WRL::ComPtr;

// Optional host services (see the header); exact types for the directory.
using ShelfChoices = std::function<std::vector<mod::NotesShelfChoice>()>;
using ShelfOpen = std::function<std::optional<std::pair<std::string, std::shared_ptr<void>>>(std::string_view)>;
constexpr std::string_view shelfChoicesKey = "fileShelf.mediaChoices", shelfOpenKey = "fileShelf.openReference",
    longWorkKey = "app.longWorkExecutor";

// MediaAssemblyCanvas colours: HUDRuntimeAppearance.accent is the configured
// accent (no light-theme blend) and the error status uses NSColor.systemOrange
// (AppKit sRGB values, as in the Notes canvas).
mod::MediaAssemblyAppearance appearanceFor(const app::ModuleAppearance& a) {
    mod::MediaAssemblyAppearance m;
    m.dark = a.dark; m.language = a.language;
    m.accent = {a.accentSRGB[0], a.accentSRGB[1], a.accentSRGB[2], 1};
    m.systemOrange = a.dark ? mod::MediaAssemblyColor{1, 159. / 255., 10. / 255., 1} : mod::MediaAssemblyColor{1, 149. / 255., 0, 1};
    return m;
}
bool down(int key) { return (GetKeyState(key) & 0x8000) != 0; }
// Mac Option is Alt; Control is Control; Command has no Windows equivalent in
// the source key routing beyond "any modifier", so the Windows keys stand in.
mod::MediaAssemblyModifiers keyboardModifiers(const app::KeyEvent& e) {
    return {down(VK_SHIFT), down(VK_CONTROL), e.alt || down(VK_MENU), down(VK_LWIN) || down(VK_RWIN)};
}

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

class MediaAssemblyModule final : public app::ModuleOwner {
public:
    MediaAssemblyModule(const app::ApplicationModuleContext& c, fs::path assets)
        : host_(*c.host), window_(static_cast<HWND>(c.window)), utility_(*c.utility), coverageMode_(c.mode == app::ApplicationAreaMode::hiddenCoverage),
          coverageRoot_(c.coverageRoot), recordEvent_(c.recordEvent) {
        if (coverageMode_ && c.fixture) movieFixture_ = c.fixture("mediaAssembly.movie");
        engine_ = std::make_shared<gpu::NativeMediaAssemblyEngine>(gpu::NativeMediaAssemblyEngine::Options{assets});
        tools::MediaAssemblyPreviewOptions o;
        o.raster.pixelsPerPoint = 2; o.raster.paddingPoints = 1; o.raster.assetRoot = assets;
        const auto& appearance = host_.appearance();
        o.appearance = appearanceFor(appearance); o.reduceMotion = appearance.reduceMotion;
        o.window = window_; o.dialogMessage = dialogMessage_ = host_.registerMessage();
        o.changed = c.invalidate;
        // OverlayController.onEvent: EventLog mediaAssemblyAction {action}.
        o.event = [this](std::string_view action) {
            if (action == "imported") ++events_.imported; else if (action == "edited") ++events_.edited; else if (action == "exported") ++events_.exported;
            if (recordEvent_) recordEvent_(mod::EventKind::mediaAssemblyAction, {{"action", std::string(action)}});
        };
        o.uuid = [] { return ehud::data::makeUUID(); };
        if (coverageMode_) {
            // Synthetic Temporary File Shelf: one owned still and one
            // unsupported row; the lease is released after the import.
            o.shelfChoices = [] { return std::vector<mod::NotesShelfChoice>{{"coverage-shelf-1", "Shelf still.png", "PNG", true, true}, {"coverage-shelf-2", "notes.txt", "TXT", false, true}}; };
            o.shelfAccess = [this](std::string_view id) -> std::optional<tools::MediaAssemblyShelfAccess> {
                if (id != "coverage-shelf-1") return std::nullopt;
                auto lease = std::make_shared<int>(1); shelfLease_ = lease;
                return tools::MediaAssemblyShelfAccess{gpu::mediaAssemblyUTF8(coverageRoot_ / L"Shelf still.png"), std::move(lease)};
            };
            // Injected dialog: answers queued by the coverage step, never UI.
            o.dialog = [this](HWND owner, const gpu::MediaAssemblyDialogRequest& request) -> std::optional<std::string> {
                if (owner != window_) throw std::logic_error("Media Assembly dialogs are modal to the HUD window");
                dialogs_.push_back(request);
                if (answers_.empty()) return std::nullopt;
                auto answer = std::move(answers_.front()); answers_.pop_front(); return answer;
            };
        } else {
            o.shelfChoices = [this] {
                const auto* service = host_.find<ShelfChoices>(shelfChoicesKey);
                return service && *service ? (*service)() : std::vector<mod::NotesShelfChoice>{};
            };
            o.shelfAccess = [this](std::string_view id) -> std::optional<tools::MediaAssemblyShelfAccess> {
                const auto* service = host_.find<ShelfOpen>(shelfOpenKey);
                if (!service || !*service) return std::nullopt;
                auto opened = (*service)(id);
                if (!opened) return std::nullopt;
                return tools::MediaAssemblyShelfAccess{std::move(opened->first), std::move(opened->second)};
            };
        }
        // The app's ONE shared media broker (AVPlayer equivalent, with audio).
        if ((broker_ = host_.mediaBroker())) client_ = broker_->attachClient();
        o.broker = broker_; o.client = client_;
        o.exportExecutor = host_.find<app::UtilityExecutor>(longWorkKey);
        try {
            preview_ = std::make_unique<tools::MediaAssemblyPreview>(host_.rasterizer(), utility_, engine_, std::move(o));
        } catch (...) { detachClient(); throw; }
    }
    ~MediaAssemblyModule() override {
        // The owner hides its player and cancels an explicit export first;
        // then this area's broker channels are retired and the client detached.
        preview_.reset();
        detachClient();
    }
    MediaAssemblyModule(const MediaAssemblyModule&) = delete;
    MediaAssemblyModule& operator=(const MediaAssemblyModule&) = delete;

    std::string_view name() const noexcept override { return "mediaAssembly"; }
    // Areas route after the built-in owners. Open secondary menus and drags
    // capture input before the floating Notes layer (source: menus consume
    // input before the controls behind them).
    app::ModuleRouting routing() const noexcept override {
        app::ModuleRouting r;
        r.pointerCapture = 20; r.pointer = 20; r.wheelCapture = 20; r.wheel = 20; r.keyCapture = 20; r.key = 20; r.message = 20;
        return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::mediaAssembly; }

    void resize(const app::ClientMetrics& m) override {
        if (!m.pixelWidth || !m.pixelHeight) return; // hidden window: keep the last real size
        preview_->resize(m); sized_ = true;
    }
    void setAppearance(const app::ModuleAppearance& a, double t) override {
        preview_->setLanguage(a.language, t); preview_->setAppearance(appearanceFor(a), t); preview_->setReduceMotion(a.reduceMotion, t);
    }
    void setOverlayVisible(bool v, double t) override { preview_->setOverlayVisible(v, t); }
    void overlayClosing(double t) override { preview_->cancelInteraction(t); preview_->setOverlayVisible(false, t); }
    void update(const app::ModuleFrame& f) override {
        if (!sized_) return;
        // The shared broker was sampled/accepted before this frame: re-read the
        // retained player record (no work without a requested player).
        (void)preview_->refreshSharedMedia(f.time);
        preview_->update(f.center, f.chrome, f.presentation, f.opacity, f.time);
    }
    bool requiresFrames(double t) const override { return preview_->requiresFrames(t); }
    std::optional<double> nextWakeTime(double) const override { return preview_->nextWakeTime(); }
    bool deadline(double t) override { return preview_->deadline(t); }

    void upload(gpu::Renderer& r) override { preview_->upload(r); }
    std::span<const gpu::LayerCompositionEntry> entries(core::Module m) override {
        return m == core::Module::mediaAssembly ? preview_->entries() : std::span<const gpu::LayerCompositionEntry>{};
    }
    void collected(gpu::Renderer& r) override { preview_->collected(r); }
    void release(gpu::Renderer& r) override { preview_->release(r); }

    bool covers(core::Point p) const override { return preview_->covers(p); }
    bool pointerLocked() const override { return preview_->pointerLocked(); }
    bool capturesPointer() const override { return preview_->capturesPointer(); }
    bool capturesWheel() const override { return preview_->menu().has_value(); }
    bool capturesKeys() const override { return preview_->menu().has_value(); }
    bool pointer(const app::PointerEvent& e, double t) override { return preview_->pointer(e, t); }
    bool wheel(const app::WheelEvent& e, double t) override { return preview_->wheel(e, t); }
    bool key(const app::KeyEvent& e, double t) override { return preview_->key(e, keyboardModifiers(e), t); }
    bool message(const app::NativeMessage& m, double t) override { return preview_->message(m, t); }
    void focus(bool v, double t) override { preview_->focus(v, t); if (!v) preview_->cancelInteraction(t); }
    void cancelInteraction(double t) override { preview_->cancelInteraction(t); }
    bool hostEvent(const app::HostEvent& e, double t) override {
        // A concealed HUD released its player: retire the broker records now.
        if (e.kind == app::HostEvent::Kind::overlayConcealed) (void)preview_->refreshSharedMedia(t);
        return false;
    }
    // Source HUDMediaAssemblyInteraction.importFiles (first file; ignored while
    // exporting). Signature-compatible with a host drop route for module
    // areas (requested): it binds as soon as ModuleOwner declares it.
    bool drop(std::span<const std::string> paths, core::Point, double t) { return preview_->importFiles(paths, t); }

    void coverage(app::ModuleCoverage& step) override {
        if (!coverageMode_) return;
        if (step.module) coverModule(step); else coverIdle(step);
    }

private:
    struct Events { unsigned imported{}, edited{}, exported{}; };
    app::ApplicationHost& host_;
    HWND window_{};
    app::UtilityExecutor& utility_;
    bool coverageMode_{}, sized_{};
    fs::path coverageRoot_, movieFixture_;
    std::function<void(mod::EventKind, mod::EventMetadata)> recordEvent_;
    std::uint32_t dialogMessage_{};
    gpu::NativeMediaRequestBroker* broker_{};
    core::MediaClient client_{};
    std::shared_ptr<gpu::NativeMediaAssemblyEngine> engine_;
    Events events_;
    std::deque<std::optional<std::string>> answers_;
    std::vector<gpu::MediaAssemblyDialogRequest> dialogs_;
    std::weak_ptr<int> shelfLease_;
    unsigned coveredCycles_{};
    std::unique_ptr<tools::MediaAssemblyPreview> preview_; // destroyed first (declared last)

    void detachClient() noexcept {
        if (!broker_ || !client_) return;
        // The owner already hid its player channel; a client still holding a
        // retiring record stays attached until the broker itself is destroyed.
        try { (void)broker_->collectRetired(); (void)broker_->detachClient(client_); } catch (...) {}
        client_ = {};
    }

    // ---- Hidden coverage (synthetic inputs in the owned coverage root) ----
    struct Run {
        MediaAssemblyModule& self;
        app::ModuleCoverage& step;
        double t;
        tools::MediaAssemblyPreview& owner() { return *self.preview_; }
        const mod::MediaAssemblySession& session() { return owner().controller().session(); }
        void present(double dt = 1. / 60) { t += dt; step.present(t); }
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
        std::optional<core::Point> action(std::string_view id) {
            const auto view = session().view();
            for (const auto& a : owner().presentation().actions(view, self.host_.appearance().language))
                if (a.id == id) return core::Point{a.rect.x + a.rect.width * .5, a.rect.y + a.rect.height * .5};
            return std::nullopt;
        }
        core::Point projected(core::Point local) {
            const auto p = step.project(local, 440, 440);
            step.check(p.has_value(), "Media Assembly input projects through its 440-point module plane");
            return *p;
        }
        bool click(core::Point local) {
            const auto p = projected(local);
            const bool consumed = self.pointer({app::PointerKind::down, app::PointerButton::left, p.x, p.y, 0}, t);
            (void)self.pointer({app::PointerKind::up, app::PointerButton::left, p.x, p.y, 0}, t);
            return consumed;
        }
        bool clickAction(std::string_view id) { const auto at = action(id); step.check(at.has_value(), "The source control is laid out"); return click(*at); }
        bool key(std::uint32_t vk, mod::MediaAssemblyModifiers m = {}) { return owner().key({app::KeyKind::down, vk}, m, t); }
        // The owner posted its dialog notice to the HUD HWND: take exactly that
        // private message and route it through the owners' message chain.
        bool routeDialog() {
            MSG msg{}; bool routed{};
            while (PeekMessageW(&msg, self.window_, self.dialogMessage_, self.dialogMessage_, PM_REMOVE | PM_NOYIELD))
                routed = step.message(msg.message, msg.wParam, msg.lParam) || routed;
            return routed;
        }
    };

    void coverModule(app::ModuleCoverage& step) {
        step.check(*step.module == core::Module::mediaAssembly && !coverageRoot_.empty(), "Media Assembly coverage owns its selected module and coverage root");
        Run run{*this, step, step.time};
        auto& owner = *preview_;
        const auto cycle = step.cycle;
        run.present();
        step.check(!owner.entries().empty() && !owner.controller().session().document(), "Selected Media Assembly publishes its retained empty canvas");
        // GPU previews: the HLSL compile was queued on the shared worker when
        // the editor was first shown; the processor exists once it drained.
        bool mediaCapable{};
        try { mediaCapable = host_.renderer().mediaDevice() != nullptr; } catch (const std::exception&) {}
        run.settle(); run.present();
        step.check(owner.stats().gpuActive == mediaCapable && owner.controller().deferredPreviews() == mediaCapable,
                   "Previews are edited on the renderer's media device when it has one");

        // Synthetic inputs, rewritten every cycle (identical bytes, new identity).
        const auto photo = coverageRoot_ / L"Owned Photo.png", shelfStill = coverageRoot_ / L"Shelf still.png";
        writeSyntheticPNG(photo, 320, 200, 0); writeSyntheticPNG(shelfStill, 64, 48, 7);
        const auto before = events_;

        // Open -> source chooser -> File Explorer (posted, modal, injected).
        step.check(run.clickAction("open") && owner.menu() == mod::MediaAssemblyMenuKind::source, "Projected Open shows the source chooser");
        run.frames(.1);
        step.check(capturesPointer() && capturesKeys() && capturesWheel(), "An open menu captures input before the controls behind it");
        step.check(run.key(VK_ESCAPE) && !owner.menu(), "Escape closes the menu first");
        run.frames(.25);
        step.check(run.clickAction("open") && owner.menu() == mod::MediaAssemblyMenuKind::source, "The chooser opens again");
        answers_.push_back(gpu::mediaAssemblyUTF8(photo));
        const auto dialogsBefore = dialogs_.size();
        step.check(owner.menuAction("finder", run.t) && !owner.menu(), "Choose in File Explorer closes the chooser");
        step.check(run.routeDialog() && dialogs_.size() == dialogsBefore + 1 && dialogs_.back().kind == gpu::MediaAssemblyDialogRequest::Kind::open &&
                   !dialogs_.back().extensions.empty(), "The open dialog runs from the posted notice through the message chain");
        run.settle();
        const auto& session = owner.controller().session();
        step.check(session.document() && session.document()->name == "Owned Photo.png" && !session.document()->video && events_.imported == before.imported + 1,
                   "The chosen still opens and records one imported event");
        step.check(owner.controller().previewImage() && owner.controller().previewImage()->width == 320 && owner.stats().retainedTextures == 1,
                   "One bounded preview is retained for the document");
        run.frames(.3);
        {   // Warm frames reuse pixels, text and textures and enqueue nothing.
            const auto raster = host_.rasterizer().stats(); const auto uploads = owner.stats().previewUploads;
            const auto textures = host_.renderer().stats().textureUploads; const auto accepted = utility_.stats().accepted;
            run.frames(.2);
            step.check(host_.rasterizer().stats().rasterizations == raster.rasterizations && owner.stats().previewUploads == uploads &&
                       host_.renderer().stats().textureUploads == textures && utility_.stats().accepted == accepted, "Settled Media Assembly frames repaint and enqueue nothing");
        }

        // Tools drawer by projected click; rotate re-renders the bounded preview.
        step.check(run.clickAction("tools") && owner.presentation().drawer() == mod::MediaAssemblyDrawer::tools, "Projected Adjust opens the tools drawer");
        step.check(owner.perform("rotate", run.t), "Rotate is available"); run.settle();
        step.check(session.adjustments().rotationQuarterTurns == 1 && owner.controller().previewImage()->width == 200, "Rotate re-renders the bounded preview");
        // Edited event debounced on the host deadline (0.4 s), recorded once.
        const auto wake = nextWakeTime(run.t);
        step.check(wake.has_value() && events_.edited == before.edited, "The edited event waits on the shared deadline");
        run.t = std::max(run.t, *wake); step.check(deadline(run.t) && events_.edited == before.edited + 1 && !nextWakeTime(run.t), "Edited recorded once when the deadline fires");
        run.present();
        // Inline adjustment through the accessible slider route.
        step.check(owner.perform("adjust", run.t) && owner.presentation().tool() == mod::MediaAssemblyTool::adjust, "Adjust opens its inline parameters");
        step.check(owner.setAccessibleValue("exposure", .5, run.t) && std::abs(session.adjustments().exposure - .5) < 1e-9, "Exposure follows the accessible slider value");
        run.settle();
        {   const auto ax = owner.accessibility(); const auto close = core::localized("Close", "关闭", host_.appearance().language);
            step.check(std::any_of(ax.begin(), ax.end(), [&](const auto& e) { return e.id == "closeMedia" && e.name == close && e.rect.width > 0 && e.rect.height > 0; }),
                       "Accessible controls are projected into client coordinates"); }
        step.check(run.key(VK_ESCAPE) && !owner.presentation().tool(), "Escape leaves the active tool first");
        // Zoom: Ctrl+wheel (precision pinch) and '-' back.
        {   const auto mid = run.projected({220, 198}); const auto zoom = owner.presentation().viewport().zoom();
            step.check(wheel({mid.x, mid.y, 1, false, MK_CONTROL, 3}, run.t) && owner.presentation().viewport().zoom() > zoom, "Ctrl+wheel zooms the preview");
            const auto zoomed = owner.presentation().viewport().zoom();
            step.check(run.key(VK_OEM_MINUS) && owner.presentation().viewport().zoom() < zoomed, "Minus zooms out");
            step.check(owner.perform("zoomReset", run.t) && owner.presentation().viewport().zoom() == 1, "Reset returns to the fitted preview"); }
        run.present();
        // Stickers: add, Shift+Right nudge, Backspace delete.
        step.check(owner.perform("stickers", run.t) && owner.presentation().drawer() == mod::MediaAssemblyDrawer::stickers, "Stickers drawer");
        const auto sticker = "sticker:" + std::string(mod::mediaAssemblyStickers()[0].id);
        step.check(owner.perform(sticker, run.t) && session.adjustments().stickers.size() == 1 && !owner.presentation().selectedSticker().empty() &&
                   !owner.presentation().drawer(), "A placed sticker gets a new identity, is selected and closes the drawer");
        {   const auto x = session.adjustments().stickers[0].x;
            step.check(run.key(VK_RIGHT, {.shift = true}) && std::abs(session.adjustments().stickers[0].x - x - .05) < 1e-9, "Shift+Right nudges the selected sticker by 0.05"); }
        step.check(run.key(VK_BACK) && session.adjustments().stickers.empty(), "Backspace deletes the selected sticker");
        run.settle();

        // Export -> Save As (posted dialog, injected answer) -> atomic commit.
        const auto exported = coverageRoot_ / (L"Coverage export " + std::to_wstring(cycle) + L".png");
        std::error_code ignored; fs::remove(exported, ignored);
        step.check(owner.perform("export", run.t) && owner.menu() == mod::MediaAssemblyMenuKind::exportMedia, "Export offers its menu");
        answers_.push_back(gpu::mediaAssemblyUTF8(exported));
        step.check(owner.menuAction("saveAs", run.t) && run.routeDialog() && dialogs_.back().kind == gpu::MediaAssemblyDialogRequest::Kind::save &&
                   dialogs_.back().filename == "Owned Photo-edited.png", "Save As proposes the source folder and suggested name");
        step.check(session.exporting() && nextWakeTime(run.t).has_value(), "Export progress uses the host deadline only while exporting");
        run.settle();
        step.check(fs::is_regular_file(exported) && events_.exported == before.exported + 1 && !session.error(), "Save As exports atomically");
        // Overwrite original: confirmation, then a silent re-import.
        const auto documentID = session.document()->id;
        step.check(owner.perform("export", run.t) && owner.menuAction("overwrite", run.t) && owner.menu() == mod::MediaAssemblyMenuKind::confirmOverwrite,
                   "Overwrite asks for confirmation");
        step.check(owner.menuAction("confirm", run.t) && !owner.menu(), "Confirm overwrite");
        run.settle(); run.settle();
        step.check(session.document() && session.document()->id != documentID && events_.imported == before.imported + 1 && events_.exported == before.exported + 2,
                   "The overwritten original reloads without a second imported event");
        {   bool temporary{};
            for (const auto& entry : fs::directory_iterator(coverageRoot_)) temporary = temporary || entry.path().filename().wstring().starts_with(L".endfield-export-");
            step.check(!temporary, "No temporary export file is left behind"); }

        // Source state changes: edited elsewhere -> changedOnDisk; removed ->
        // unavailable ("choose it again"); choosing it again restores it.
        { std::ofstream(photo, std::ios::binary | std::ios::app) << 'x'; }
        step.check(owner.perform("tools", run.t) && owner.perform("mirror", run.t), "Mirror edits the changed source"); run.settle();
        step.check(session.error() == mod::MediaAssemblyError::changedOnDisk, "A source changed on disk is reported, not silently reloaded");
        fs::remove(photo);
        step.check(owner.perform("mirror", run.t), "Mirror edits the removed source"); run.settle();
        step.check(session.error() == mod::MediaAssemblyError::sourceUnavailable &&
                   mod::mediaAssemblyErrorMessage(*session.error(), core::Language::english) == "The original media file is unavailable. Choose it again to restore access.",
                   "A removed source asks to be chosen again");
        step.check(owner.perform("tools", run.t) && !owner.presentation().drawer(), "The tools drawer closes");
        writeSyntheticPNG(photo, 320, 200, 0);
        {   const std::array<std::string, 2> dropped{gpu::mediaAssemblyUTF8(photo), gpu::mediaAssemblyUTF8(shelfStill)};
            step.check(drop(dropped, {}, run.t), "A drop imports its first file"); }
        run.settle();
        step.check(session.document() && session.document()->name == "Owned Photo.png" && !session.error() && events_.imported == before.imported + 2,
                   "Choosing the original again restores editing");

        // Temporary File Shelf source (synthetic provider), lease released.
        step.check(owner.perform("open", run.t) && owner.menuAction("shelf", run.t) && owner.menu() == mod::MediaAssemblyMenuKind::shelf, "Choose from Shelf lists the Shelf");
        step.check(owner.menuAction("row:0", run.t) && !owner.menu(), "A supported Shelf row imports");
        run.settle();
        step.check(session.document() && session.document()->name == "Shelf still.png" && shelfLease_.expired() && events_.imported == before.imported + 3,
                   "The Shelf lease is released after the open");

        // Owned silent movie: poster frame, seek keys, trim; never played
        // (no audio device is opened in hidden coverage).
        if (!movieFixture_.empty()) {
            const auto movie = coverageRoot_ / L"Owned movie.mp4";
            if (!fs::exists(movie)) fs::copy_file(movieFixture_, movie);
            const std::array<std::string, 1> dropped{gpu::mediaAssemblyUTF8(movie)};
            step.check(drop(dropped, {}, run.t), "The owned movie is dropped"); run.settle();
            step.check(session.document() && session.document()->video && session.document()->duration > .3 && owner.controller().previewImage(),
                       "The movie opens with a bounded poster frame");
            const auto actions = owner.presentation().actions(session.view(), host_.appearance().language);
            const auto find = [&](std::string_view id) { return std::find_if(actions.begin(), actions.end(), [&](const auto& a) { return a.id == id; }); };
            step.check(find("play") != actions.end() && find("play")->enabled && find("stickers") != actions.end() && !find("stickers")->enabled,
                       "Movies offer playback and no stickers");
            const auto duration = session.document()->duration;
            step.check(run.key(VK_RIGHT) && session.currentTime() > 0 && session.currentTime() <= duration, "Right seeks forward within the movie");
            run.settle();
            step.check(run.key(VK_LEFT) && session.currentTime() == 0, "Left seeks back to the start");
            step.check(owner.perform("tools", run.t) && owner.perform("trim", run.t) && owner.presentation().tool() == mod::MediaAssemblyTool::trim, "Trim tool");
            step.check(owner.setAccessibleValue("trimStart", .1, run.t) && std::abs(session.adjustments().trimStart - .1) < 1e-9, "Trim start follows its slider");
            run.settle();
            step.check(run.key(VK_ESCAPE) && owner.perform("tools", run.t) && !owner.presentation().drawer(), "Leaving the trim tool and drawer");
            step.check(owner.stats().playbackStarts == 0 && !owner.stats().videoShown, "Hidden coverage never starts a player");
        }

        // Close: the poster fades (0.16 s) and its texture is released.
        step.check(run.clickAction("closeMedia"), "Projected close");
        run.frames(.4); run.settle();
        step.check(!session.document() && !owner.sceneClosing() && owner.stats().retainedTextures == 0 && !owner.menu(), "Close releases the poster after its fade");
        run.frames(.3);
        step.check(engine_->stats().cachedSourceBytes <= std::size_t(1024) * 1024 * 4, "The engine keeps at most one bounded decoded source");
        ++coveredCycles_;
    }
    // Once per cycle: nothing open, scheduled or retained after the module step.
    void coverIdle(app::ModuleCoverage& step) {
        const auto& owner = *preview_;
        step.check(coveredCycles_ == step.cycle + 1, "The Media Assembly module step ran in this cycle");
        step.check(!owner.controller().session().document() && !owner.menu() && !owner.controller().session().exporting() &&
                   !owner.nextWakeTime() && owner.stats().retainedTextures == 0, "Media Assembly parks with nothing open, scheduled or retained");
        // Per cycle: chooser, re-chosen original, Shelf and (when declared) the
        // movie import once each; the overwrite re-import records nothing.
        step.check(events_.imported == coveredCycles_ * (movieFixture_.empty() ? 3u : 4u) && events_.exported == 2 * coveredCycles_ &&
                   events_.edited >= coveredCycles_, "Event Log actions follow the source rules");
        step.check(answers_.empty() && owner.stats().playbackStarts == 0, "Every injected dialog answer was consumed; no player started");
    }
};
}

std::unique_ptr<app::ModuleOwner> mediaAssemblyModule(const app::ApplicationModuleContext& c) {
    if (!c.host || !c.utility || !c.window) return nullptr;
    // Production: the SHA-256 verified package only. Hidden coverage without
    // a staged package uses the declared source-tree fixture (still pinned by
    // the asset catalog); development previews have no package.
    auto assets = c.resource ? c.resource("mediaAssembly.assets") : fs::path{};
    if (assets.empty() && c.mode == app::ApplicationAreaMode::hiddenCoverage && c.fixture) assets = c.fixture("mediaAssembly.sourceAssets");
    if (assets.empty()) return nullptr;
    return std::make_unique<MediaAssemblyModule>(c, std::move(assets));
}
} // namespace endfield::areas
