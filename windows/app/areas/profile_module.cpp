#include "app/areas/profile_module.hpp"
// The design headers first: rpcndr.h (through windows.h) defines `small`.
#include "tools/profile_owner.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include "modules/hypergryph_account_identity_sync.hpp"
#include "native/hud_design_text_metrics.hpp"
#include "native/id_card_presenter.hpp"
#include "native/layer_scene.hpp"
#include "native/layer_text_layout.hpp"
#include "native/profile_pickers.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomation.h>
#include <wincodec.h>
#include <wrl/client.h>
// Account Linking publishes its profile link (app/areas/account_profile_link.hpp,
// key "account.profileLink"); this area attaches the AccountProfileSink over
// the single profile owner when that area is part of the build.
#if __has_include("app/areas/account_profile_link.hpp")
#include "app/areas/account_profile_link.hpp"
#define EHUD_PROFILE_ACCOUNT_LINK 1
#endif

namespace endfield::areas {
namespace {
namespace fs = std::filesystem;
namespace m = modules;
namespace gpu = native;
using ehud::data::Json;

constexpr std::string_view resourceID = "profile";

void require(bool value, const char* why) { if (!value) throw std::runtime_error(std::string("Personal Profile area: ") + why); }
fs::path utf8Path(const std::string& text) { return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size())); }
std::string narrow(std::wstring_view text) {
    if (text.empty()) return {};
    require(text.size() < 0x7fffffff, "a bounded wide string");
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    require(n > 0, "valid UTF-16 text");
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    require(text.size() < 0x7fffffff, "a bounded UTF-8 string");
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    require(n > 0, "valid UTF-8 text");
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

// Hidden coverage UI Automation probes (no UIA client, no window shown).
using Fragment = Microsoft::WRL::ComPtr<IRawElementProviderFragment>;
HRESULT uiaProperty(IRawElementProviderFragment* f, PROPERTYID id, VARIANT& out) {
    VariantInit(&out);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
    if (!f || FAILED(f->QueryInterface(IID_PPV_ARGS(&simple)))) return E_NOINTERFACE;
    return simple->GetPropertyValue(id, &out);
}
std::wstring uiaText(IRawElementProviderFragment* f, PROPERTYID id) {
    VARIANT v;
    std::wstring out;
    if (SUCCEEDED(uiaProperty(f, id, v)) && v.vt == VT_BSTR && v.bstrVal) out.assign(v.bstrVal, SysStringLen(v.bstrVal));
    VariantClear(&v);
    return out;
}
long uiaControlType(IRawElementProviderFragment* f) {
    VARIANT v;
    long out = 0;
    if (SUCCEEDED(uiaProperty(f, UIA_ControlTypePropertyId, v)) && v.vt == VT_I4) out = v.lVal;
    VariantClear(&v);
    return out;
}
bool uiaAvailable(IRawElementProviderFragment* f) {
    VARIANT v;
    const auto hr = uiaProperty(f, UIA_NamePropertyId, v);
    VariantClear(&v);
    return hr != UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT uiaInvoke(const Fragment& f) {
    Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
    if (!f || FAILED(f.As(&invoke))) return E_NOINTERFACE;
    return invoke->Invoke();
}
core::Language resolved(core::Language language) { return language == core::Language::system ? core::Language::english : language; }

// Hidden coverage only: a synthetic record with a fixed identity in the
// area's own coverage root (never the real profile).
void seedCoverageProfile(const fs::path& root) {
    const auto path = ehud::data::ProfileStore(root).path();
    const auto bytes = ehud::data::detail::readFile(path, 1024 * 1024);
    require(bytes.has_value(), "the synthetic coverage record is readable");
    auto record = Json::parse(*bytes);
    record["profile"]["uid"] = "1000000000";
    record["profile"]["name"] = "Endministrator";
    record["profile"]["accumulatedWorkSeconds"] = 3600.;
    record["profile"]["awakeningDate"] = (1767225600. - 978307200.) - 3600; // 2025-12-31 23:00 UTC
    ehud::data::detail::replaceFile(path, *bytes, record.encode(), 1024 * 1024); // expected: the fresh record
    require(ehud::data::ProfileStore(root).value().uid == "1000000000", "the synthetic coverage record reads back");
}
// Hidden coverage only: a synthetic PNG written into the coverage root.
void writeCoveragePNG(const fs::path& path, UINT width, UINT height) {
    using Microsoft::WRL::ComPtr;
    const auto ok = [](HRESULT hr, const char* why) { require(SUCCEEDED(hr), why); };
    ComPtr<IWICImagingFactory> factory;
    ok(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "WIC factory");
    ComPtr<IWICStream> stream;
    ok(factory->CreateStream(&stream), "WIC stream");
    ok(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "synthetic image file");
    ComPtr<IWICBitmapEncoder> encoder;
    ok(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "PNG encoder");
    ok(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "PNG encoder stream");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> bag;
    ok(encoder->CreateNewFrame(&frame, &bag), "PNG frame");
    ok(frame->Initialize(bag.Get()), "PNG frame options");
    ok(frame->SetSize(width, height), "PNG size");
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    ok(frame->SetPixelFormat(&format), "PNG format");
    require(format == GUID_WICPixelFormat24bppBGR, "PNG keeps 24-bit BGR");
    const UINT stride = (width * 3 + 3) & ~3u;
    std::vector<BYTE> pixels(std::size_t(stride) * height);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            auto* p = &pixels[std::size_t(y) * stride + x * 3];
            p[0] = BYTE(x); p[1] = BYTE(y); p[2] = BYTE(x + y);
        }
    ok(frame->WritePixels(height, stride, UINT(pixels.size()), pixels.data()), "PNG pixels");
    ok(frame->Commit(), "PNG frame commit");
    ok(encoder->Commit(), "PNG commit");
}

// HypergryphAccountController's profile side over the single profile owner
// (never a second ProfileStore writer): official game fields are applied to
// the current record as an account write, the lock mirrors gameSyncedFields and
// the game avatar becomes the managed avatar synchronously.
class ProfileAccountSink final : public modules::hypergryph::AccountProfileSink {
public:
    ProfileAccountSink(tools::ProfileOwner& owner, app::ApplicationHost& host) : owner_(owner), host_(host) {}
    void applyGameProfile(const modules::hypergryph::GameProfileUpdate& update) override {
        owner_.acceptFromGame([update](m::PersonalProfile& p) { modules::hypergryph::applyGameProfileUpdate(p, update, gpu::nativeCharacterRules()); }, host_.now());
    }
    void setProfileSyncLocked(ehud::data::ProfileSyncFields locked) override { owner_.setSyncLocked(locked, host_.now()); }
    std::optional<std::string> importGameAvatar(const std::vector<std::uint8_t>& png) override { return owner_.importGameAvatar(png, host_.now()); }
private:
    tools::ProfileOwner& owner_;
    app::ApplicationHost& host_;
};

class ProfileArea final : public app::ModuleOwner {
public:
    ProfileArea(const app::ApplicationModuleContext& c, fs::path resources, modules::design::Context& context, tools::DesignOwnerOptions design)
        : host_(*c.host), mode_(c.mode), hwnd_(static_cast<HWND>(c.window)), coverageRoot_(c.coverageRoot), fixture_(c.fixture),
          work_(c.host->workMode() ? c.host->workMode() : &idleWork_) {
        // Hidden coverage keeps the record, zone and choosers synthetic.
        const auto root = hiddenCoverage() ? coverageRoot_ : c.dataRoot;
        require(!root.empty() && root.is_absolute(), "an explicit absolute data root");
        if (hiddenCoverage()) {
            seedCoverageProfile(root);
            // The host data root's profile.json as the run began: the profile
            // area is its single writer, Work Mode never writes it.
            if (!c.dataRoot.empty()) {
                hostProfilePath_ = c.dataRoot / "Profile" / "profile.json";
                hostProfileBefore_ = ehud::data::detail::readFile(hostProfilePath_, 1024 * 1024);
            }
        }
        zone_ = hiddenCoverage() ? u"UTC" : u"";
        appearance_ = host_.appearance();
        tools::ProfileOwnerOptions o;
        o.design = std::move(design);
        o.appRoot = root;
        o.resources = std::move(resources);
        o.timeZone = zone_;
        o.pickerMessage = host_.registerMessage();
        pickerMessage_ = o.pickerMessage;
        o.requestImage = [this](m::ProfileImageKind kind, tools::ProfileOwnerOptions::ImageCompletion done) { return chooseImage(kind, std::move(done)); };
        o.chooseColor = [this](std::array<double, 3> initial, const std::function<void(std::array<double, 3>)>& live) { return chooseColor(initial, live); };
        o.cancelPicker = [this] { cancelPickers(); };
        o.minigameBest = [this] { return hiddenCoverage() ? coverageBest_ : host_.minigameBest(); };
        o.cropChanged = [this](std::string_view target) {
            ++cropEvents_; lastCrop_ = target;
            host_.recordEvent(modules::EventKind::profileCropChanged, {{"target", std::string(target)}});
        };
#ifdef EHUD_PROFILE_ACCOUNT_LINK
        o.loaded = [this](const m::PersonalProfile& p) { if (account_) account_->profileLoaded(p.avatarFilename); };
        o.avatarChanged = [this](const std::optional<std::string>& name) { if (account_) account_->profileAvatarChanged(name); };
#endif
        o.cardChanged = [this] { publishCard(); };
        owner_ = std::make_unique<tools::ProfileOwner>(hwnd_, *c.utility, c.rasterizer, context, *work_, std::move(o));
        // Work Mode lifetime: every absolute checkpoint of the shared controller.
        listener_ = work_->listenCheckpoints([this](double total) {
            ++checkpoints_; lastCheckpoint_ = total;
            if (owner_) owner_->workModeCheckpoint(total);
        });
        owner_->core().resize(c.metrics);
        owner_->setRecordingMode(host_.settings().recordingMode(), host_.now());
        host_.publish<gpu::IdCardFeed>(gpu::IdCardFeed::serviceKey, feed_);
        sink_ = std::make_unique<ProfileAccountSink>(*owner_, host_);
#ifdef EHUD_PROFILE_ACCOUNT_LINK
        // Account Linking registers first and is destroyed after this area,
        // so its link stays valid for this owner's lifetime. Attaching replays
        // the account's latest lock and game profile update.
        account_ = host_.find<app::areas::AccountProfileLink>(app::areas::AccountProfileLink::key);
        if (account_) account_->attachProfile(sink_.get());
#endif
        owner_->start(host_.now());
    }
    ~ProfileArea() override {
#ifdef EHUD_PROFILE_ACCOUNT_LINK
        if (account_) account_->attachProfile(nullptr);
#endif
        host_.withdraw(gpu::IdCardFeed::serviceKey);
        work_->unlistenCheckpoints(listener_);
        if (fileChoiceOpen_) { fileChoiceOpen_ = false; host_.cancelFileChoice(); }
        owner_.reset();
    }
    ProfileArea(const ProfileArea&) = delete;
    ProfileArea& operator=(const ProfileArea&) = delete;
    static app::ModuleRouting profileRouting() noexcept {
        app::ModuleRouting r;
        r.pointerCapture = 20; r.keyCapture = 20;          // open menu, pop-over, picker or editing field
        r.pointer = 30; r.key = 30; r.filterKey = 20; r.message = 30;
        r.finishEditing = 30; r.flush = 30;
        return r;
    }

    // --- ModuleOwner: the design core, plus the art records, the pickers and the card ---
    std::string_view name() const noexcept override { return "profile"; }
    app::ModuleRouting routing() const noexcept override { return profileRouting(); }
    bool presents(core::Module module) const noexcept override { return module == core::Module::profile; }
    void resize(const app::ClientMetrics& metrics) override { core().resize(metrics); }
    void setAppearance(const app::ModuleAppearance& a, double t) override {
        appearance_ = a;
        // HUDSourceWatchView.desktopUIDRedacted follows the applied record's Recording mode (B2b).
        owner_->setRecordingMode(host_.settings().recordingMode(), t);
        core().setAppearance(a, t);
    }
    void setOverlayVisible(bool visible, double t) override { owner_->setOverlayVisible(visible, t); core().setOverlayVisible(visible, t); }
    void overlayClosing(double t) override { owner_->setOverlayVisible(false, t); core().overlayClosing(t); }
    void update(const app::ModuleFrame& f) override { core().update(f); }
    // The first opening's page is built while the HUD is still closed.
    void prewarm(const app::ModuleFrame& f) override { core().prewarm(f); }
    bool requiresFrames(double t) const override { return core().requiresFrames(t); }
    std::optional<double> nextWakeTime(double now) const override { return core().nextWakeTime(now); }
    bool deadline(double t) override { return core().deadline(t); }
    void upload(gpu::Renderer& r) override { owner_->upload(r); }
    std::span<const gpu::LayerCompositionEntry> entries(core::Module module) override { return core().entries(module); }
    void collected(gpu::Renderer& r) override { owner_->collected(r); }
    void release(gpu::Renderer& r) override { owner_->release(r); }
    bool covers(core::Point p) const override { return core().covers(p); }
    bool pointerLocked() const override { return owner_->pickerOpen() || core().pointerLocked(); }
    bool capturesPointer() const override { return owner_->pickerOpen() || core().capturesPointer(); }
    bool capturesWheel() const override { return core().capturesWheel(); }
    bool capturesKeys() const override { return core().capturesKeys(); }
    bool suppressesKeyFilters() const override { return core().suppressesKeyFilters(); }
    bool pointer(const app::PointerEvent& e, double t) override { return owner_->pickerOpen() || core().pointer(e, t); }
    bool wheel(const app::WheelEvent& e, double t) override { return core().wheel(e, t); }
    bool key(const app::KeyEvent& e, double t) override { return core().key(e, t); }
    bool filterKey(const app::NativeMessage& msg) override { return core().filterKey(msg); }
    bool message(const app::NativeMessage& msg, double t) override { return core().message(msg, t); }
    void focus(bool value, double t) override { core().focus(value, t); }
    void cancelInteraction(double t) override { core().cancelInteraction(t); }
    bool finishEditing(double t) override { return core().finishEditing(t); }
    bool flush(double t) override {
        // OverlayController: the final Work Mode total reaches the profile
        // before it is drained. The absolute live total equals the checkpoint
        // WorkModeController::shutdown delivers, whatever the host's order.
        if (std::isfinite(t)) owner_->workModeCheckpoint(work_->trackedWorkSeconds(std::max(t, 0.)));
        (void)core().finishEditing(t);
        // A failed save stays on the page as its toast, like the Mac's
        // synchronous UserProfileStore save; it never blocks quitting.
        (void)core().flush(t);
        return true;
    }
    bool hostEvent(const app::HostEvent& e, double t) override { return core().hostEvent(e, t); }
    void coverage(app::ModuleCoverage& step) override;
    std::optional<app::ModuleCursor> cursorAt(core::Point p) const override { return core().cursorAt(p); }
    // UI Automation: the library's tree of the canvas (LIBRARY-DESIGN.md section 9).
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return core().automationRefresh(root); }
    std::size_t automationCount() const override { return core().automationCount(); }
    long automationChild(std::size_t index, IRawElementProviderFragment** out) const override { return core().automationChild(index, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return core().automationFromPoint(x, y, out); }
    void automationDisconnect() noexcept override { core().automationDisconnect(); }
    std::optional<core::Module> automationModule() const noexcept override { return core().automationModule(); }
    app::ModuleResidency residency() const override { return core().residency(); }

private:
    app::ApplicationHost& host_;
    app::ApplicationAreaMode mode_;
    HWND hwnd_{};
    fs::path coverageRoot_;
    std::function<fs::path(std::string_view)> fixture_;
    m::WorkModeController idleWork_;           // only when the host runs no Work Mode owner
    const m::WorkModeController* work_{};
    std::uint64_t listener_{}, checkpoints_{};
    double lastCheckpoint_{};
    fs::path hostProfilePath_;                    // hidden coverage only
    std::optional<std::string> hostProfileBefore_;
    app::ModuleAppearance appearance_;
    std::u16string zone_;
    UINT pickerMessage_{};
    gpu::ProfilePickerSession colorSession_;
    bool fileChoiceOpen_{};
    std::optional<std::string> imageFilter_;
    gpu::IdCardFeed feed_;
#ifdef EHUD_PROFILE_ACCOUNT_LINK
    app::areas::AccountProfileLink* account_{};
#endif
    // Hidden coverage choosers and the synthetic minigame best.
    tools::ProfileOwnerOptions::ImageCompletion coverageChooser_;
    std::optional<app::ApplicationFileRequest> coverageRequest_; // the request production would queue
    std::optional<std::array<double, 3>> coverageColor_;
    unsigned coverageImageAsks_{}, coverageColorAsks_{}, cropEvents_{};
    std::int64_t coverageBest_{3262};
    std::string lastCrop_;
    std::unique_ptr<tools::ProfileOwner> owner_;
    std::unique_ptr<ProfileAccountSink> sink_; // borrows the owner; detached and destroyed before it

    tools::DesignOwnerCore& core() const { return owner_->core(); }
    bool hiddenCoverage() const noexcept { return mode_ == app::ApplicationAreaMode::hiddenCoverage; }
    // UTType.image as the shared dialog's filter: the installed WIC decoder
    // extensions, enumerated once (bounded like every picker label). Without
    // decoders or past the bound the dialog lists every file; the import
    // still picks the codec from the content and reports non-images.
    const std::string& imageFilterSpec() {
        if (!imageFilter_) {
            std::string spec;
            try { spec = narrow(gpu::profileImageFilterSpec()); } catch (const std::exception&) { spec.clear(); }
            if (spec.size() > 4096) spec.clear();
            imageFilter_ = std::move(spec);
        }
        return *imageFilter_;
    }
    // PersonalProfileCanvas.chooseImage: NSOpenPanel (title, prompt Choose,
    // images only, one file) as the host's shared dialog in its single-file
    // mode with the "Images" filter.
    app::ApplicationFileRequest imageRequest(m::ProfileImageKind kind) {
        const auto language = resolved(appearance_.language);
        const bool avatar = kind == m::ProfileImageKind::avatar;
        app::ApplicationFileRequest request;
        request.title = avatar ? core::localized("Choose profile picture", "选择头像", language) : core::localized("Choose profile background", "选择名片背景", language);
        request.acceptLabel = core::localized("Choose", "选择", language);
        request.selectionLabel = request.acceptLabel;
        request.singleFile = true;
        request.filterName = core::localized("Images", "图片", language);
        request.filterSpec = imageFilterSpec();
        return request;
    }
    bool chooseImage(m::ProfileImageKind kind, tools::ProfileOwnerOptions::ImageCompletion done) {
        if (hiddenCoverage()) {
            if (coverageChooser_) return false;
            ++coverageImageAsks_; coverageChooser_ = std::move(done);
            coverageRequest_ = imageRequest(kind);
            return true;
        }
        if (fileChoiceOpen_) return false;
        const auto request = imageRequest(kind);
        const bool queued = host_.chooseFiles(request, [this, done = std::move(done)](app::ApplicationFileResult result) {
            fileChoiceOpen_ = false;
            std::optional<fs::path> chosen;
            if (!result.cancelled && !result.failed)
                for (const auto& path : result.paths) {
                    std::error_code error;
                    const auto candidate = utf8Path(path);
                    if (fs::is_regular_file(candidate, error)) { chosen = candidate; break; }
                }
            done(std::move(chosen));
        });
        fileChoiceOpen_ = queued;
        return queued;
    }
    // NSColorPanel counterpart (platform gap: ChooseColorW is modal).
    std::optional<std::array<double, 3>> chooseColor(std::array<double, 3> initial, const std::function<void(std::array<double, 3>)>& live) {
        if (hiddenCoverage()) {
            ++coverageColorAsks_;
            if (coverageColor_) live(*coverageColor_);
            return coverageColor_;
        }
        return gpu::chooseProfileColor(hwnd_, initial, resolved(appearance_.language), live, &colorSession_);
    }
    void cancelPickers() {
        colorSession_.cancel();
        if (coverageChooser_) { auto done = std::exchange(coverageChooser_, {}); done(std::nullopt); }
        if (fileChoiceOpen_) { fileChoiceOpen_ = false; host_.cancelFileChoice(); }
    }
    std::array<std::optional<gpu::IdCardTextureView>, 3> cardTextures() const {
        std::array<std::optional<gpu::IdCardTextureView>, 3> textures;
        for (std::size_t k = 0; k < textures.size(); ++k)
            if (const auto* t = owner_->cardTexture(static_cast<tools::IdCardTextureKind>(k)); t && !t->pixels.premultiplied)
                textures[k] = gpu::IdCardTextureView{t->pixels.width, t->pixels.height, t->pixels.rgba, t->revision};
        return textures;
    }
    void publishCard() {
        feed_.publish(owner_->card(), cardTextures());
        host_.requestFrame();
    }
    // Hidden coverage helpers (synthetic clock, projected input).
    void drainUntil(app::ModuleCoverage& step, const std::function<bool()>& done) {
        for (unsigned n = 0; n < 16 && !done(); ++n) step.drain();
    }
    modules::ProfileCanvas& canvas() { return *owner_->canvas(); }
    // The centre of a control (page, menu or pop-over) in logical client points.
    std::optional<core::Point> controlPoint(std::string_view id) {
        std::vector<modules::design::AccessibilityItem> items; canvas().accessibilityItems(items);
        for (const auto& item : items)
            if (item.id == id)
                if (const auto r = core().clientRect(item.rect)) return core::Point{r->x + r->width / 2, r->y + r->height / 2};
        return std::nullopt;
    }
    void click(app::ModuleCoverage& step, std::string_view id, double& t) {
        const auto p = controlPoint(id);
        step.check(p.has_value(), "The profile control projects through the module plane");
        step.check(core().pointer({app::PointerKind::down, app::PointerButton::left, p->x, p->y}, t), "The projected profile control consumes the press");
        (void)core().pointer({app::PointerKind::up, app::PointerButton::left, p->x, p->y}, t + .001);
        t += .05; step.present(t);
    }
    void routePickerMessages(app::ModuleCoverage& step) {
        MSG msg;
        while (PeekMessageW(&msg, hwnd_, pickerMessage_, pickerMessage_, PM_REMOVE)) (void)step.message(msg.message, msg.wParam, msg.lParam);
    }
    Fragment peer(std::string_view id) const {
        const auto wanted = widen(id);
        for (std::size_t n = 0; n < core().automationCount(); ++n) {
            Fragment f;
            if (core().automationChild(n, &f) >= 0 && f && uiaText(f.Get(), UIA_AutomationIdPropertyId) == wanted) return f;
        }
        return nullptr;
    }
    void coverageModule(app::ModuleCoverage& step);
    void coverageAutomation(app::ModuleCoverage& step, double& t);
    void coverageCard(app::ModuleCoverage& step);
};

void ProfileArea::coverageModule(app::ModuleCoverage& step) {
    double t = step.time;
    drainUntil(step, [&] { return owner_->loaded(); });
    step.check(owner_->loaded() && owner_->state() && !owner_->loadError(), "The synthetic record loads through the shared executor");
    t += .05; step.present(t);
    step.check(canvas().isActive() && !core().entries(core::Module::profile).empty(), "Personal ID is the selected, published design page");
    step.check(work_->checkpointListeners() >= 1, "Lifetime hours observe Work Mode");
    // The canvas's 30 s timer is on the shared design Context's deadlines (the
    // overlay owner wakes for them), not this core's own wake.
    {const auto due = canvas().env().deadlines.next();
     step.check(due && *due <= t + 30 + 1e-6, "The Work Mode hours plate refreshes on the shared clock (30 s while active)");}
    step.check(canvas().bestText() == "3,262", "The OrbiPom! MERGE! best plate shows the grouped best score (OrbiPomSession.bestSoFar)");
    const auto& profile = owner_->state()->profile();
    step.check(profile.accumulatedWorkSeconds >= 3600, "The stored lifetime hours are kept");
    if (step.cycle == 0) {
        // 1. Image chooser from the ⋯ menu: queued through the host route, imported on the worker.
        const auto photo = coverageRoot_ / "Synthetic portrait.png";
        writeCoveragePNG(photo, 300, 200);
        click(step, "profile:menu", t);
        step.check(canvas().menuOpen() && capturesPointer(), "⋯ opens the profile menu, which captures input");
        step.check(canvas().perform("profile.more:picture", t) && coverageImageAsks_ == 1 && owner_->pickerOpen() && capturesPointer() && canvas().presentingPanel(),
                   "Change profile picture queues one chooser and holds the HUD while it is open");
        {
            const auto language = resolved(appearance_.language);
            const auto& r = coverageRequest_;
            const auto spec = r ? ";" + r->filterSpec + ";" : std::string{};
            step.check(r && r->singleFile && r->title == core::localized("Choose profile picture", "选择头像", language) &&
                       r->acceptLabel == core::localized("Choose", "选择", language) && r->filterName == core::localized("Images", "图片", language) &&
                       spec.find(";*.png;") != std::string::npos && spec.find(";*.jpg;") != std::string::npos && spec.find(";*.jpeg;") != std::string::npos &&
                       r->filterSpec.size() <= 4096,
                       "The chooser asks the shared dialog for one file through the localized Images filter of the installed decoders");
        }
        auto done = std::exchange(coverageChooser_, {});
        done(photo);
        drainUntil(step, [&] { return owner_->state()->profile().avatarFilename.has_value() && owner_->cardTexture(tools::IdCardTextureKind::avatar) && owner_->stats().portraitPublications > 0; });
        const auto avatar = owner_->state()->profile().avatarFilename;
        step.check(avatar && avatar->ends_with(".image") && fs::exists(coverageRoot_ / "Profile" / "Images" / *avatar) && !owner_->pickerOpen(),
                   "The chosen picture becomes the managed avatar in the profile root");
        const auto* texture = feed_.texture(gpu::IdCardTextureSlot::avatar);
        step.check(texture && texture->width == 368 && texture->height == 368 && texture->revision == owner_->card()->avatarRevision(),
                   "The ID card avatar is the 136 pt crop the presenter binds to desktop.profile.avatar");
        // The page portrait: one panning image per zoom step, made on the
        // shared worker (HUDPortraitArtwork.panningImage); a step change queues
        // one job, a zoom within the step only moves numbers.
        {
            t += .05; step.present(t);
            const auto before = owner_->stats();
            step.check(before.portraitJobs >= 1 && before.portraitPublications >= 1 && before.portraitBytes > 0 && before.portraitBytes <= 2048u * 2048 * 4,
                       "The page portrait is prepared on the shared worker and published");
            click(step, "profile:menu", t);
            step.check(canvas().perform("profile.more:portrait", t) && canvas().popoverOpen(), "Adjust portrait opens its pop-over below ⋯");
            step.check(canvas().adjustAccessibilitySlider("profile:avatarZoom", 1.5, t) && owner_->stats().portraitJobs == before.portraitJobs + 1,
                       "A zoom into the next step queues one worker image");
            drainUntil(step, [&] { return owner_->stats().portraitPublications == before.portraitPublications + 1; });
            step.check(owner_->stats().portraitPublications == before.portraitPublications + 1, "The step's panning image is published from the worker");
            const auto jobs = owner_->stats().portraitJobs;
            step.check(canvas().adjustAccessibilitySlider("profile:avatarZoom", 1.8, t) && owner_->stats().portraitJobs == jobs,
                       "A zoom inside the same step re-uses its image");
            step.check(canvas().adjustAccessibilitySlider("profile:avatarZoom", 1, t), "The portrait zoom returns");
            drainUntil(step, [&] { return owner_->stats().portraitJobs == owner_->stats().portraitPublications; });
            canvas().closePopover();
            t += .05; step.present(t);
        }
        // A cancelled chooser changes nothing.
        click(step, "profile:menu", t);
        step.check(canvas().perform("profile.more:picture", t) && coverageImageAsks_ == 2, "The chooser opens again");
        done = std::exchange(coverageChooser_, {});
        done(std::nullopt);
        step.check(owner_->state()->profile().avatarFilename == avatar && !owner_->pickerOpen() && !canvas().presentingPanel(), "A cancelled chooser keeps the avatar");
        // 2. Card colour: the posted dialog request, live preview, committed result.
        coverageColor_ = std::array<double, 3>{0, .5, 1};
        click(step, "profile:backgroundMenu", t);
        step.check(canvas().popoverOpen() && canvas().perform("theme:custom", t) && canvas().colorPanelOpen(), "Custom card colour asks for the colour dialog");
        routePickerMessages(step);
        step.check(coverageColorAsks_ == 1 && owner_->state()->profile().themeColorHex == std::optional<std::string>("0080FF") && !canvas().colorPanelOpen() &&
                   owner_->card()->captions()[4].color[2] == 1, "The chosen colour commits, the dialog closes and the card recolours");
        // Follow HUD back on, then off again restores the colour (lastThemeHex).
        step.check(canvas().perform("theme:follow", t) && !owner_->state()->profile().themeColorHex, "Follow HUD returns the card to the HUD accent");
        step.check(canvas().perform("theme:follow", t) && owner_->state()->profile().themeColorHex == std::optional<std::string>("0080FF"), "Follow HUD off restores the card's last colour");
        // Crop sliders through UI Automation: a committed thumbnail zoom
        // records profileCropChanged {thumbnail} and regenerates the card background.
        {
            const auto backgroundBefore = owner_->card()->backgroundRevision();
            (void)core().automationRefresh(nullptr);
            const auto zoomPeer = peer("profile:thumbnailZoom");
            Microsoft::WRL::ComPtr<IRangeValueProvider> zoom;
            double minimum{}, maximum{}, change{};
            step.check(zoomPeer && SUCCEEDED(zoomPeer.As(&zoom)) && SUCCEEDED(zoom->get_Minimum(&minimum)) && SUCCEEDED(zoom->get_Maximum(&maximum)) &&
                       SUCCEEDED(zoom->get_SmallChange(&change)) && minimum == 1 && maximum == 5 && change == .01 && uiaControlType(zoomPeer.Get()) == UIA_SliderControlTypeId,
                       "The thumbnail zoom is a UIA slider over 1...5 in .01 steps (Mac 1.3.0)");
            step.check(zoom && SUCCEEDED(zoom->SetValue(maximum + 10)) && owner_->state()->profile().thumbnailZoom == maximum,
                       "A value past the range is clamped");
            step.check(cropEvents_ == 1 && lastCrop_ == "thumbnail" && owner_->card()->backgroundRevision() > backgroundBefore, "The crop is recorded once and moves the card background");
            step.check(zoom && SUCCEEDED(zoom->SetValue(2)) && owner_->state()->profile().thumbnailZoom == 2 && cropEvents_ == 2, "The thumbnail zoom slider commits through RangeValue");
            core().automationDisconnect();
        }
        drainUntil(step, [&] { const auto* b = feed_.texture(gpu::IdCardTextureSlot::background); return b && b->revision == owner_->card()->backgroundRevision(); });
        step.check(feed_.texture(gpu::IdCardTextureSlot::background) && feed_.texture(gpu::IdCardTextureSlot::background)->revision == owner_->card()->backgroundRevision(),
                   "The regenerated card background is published");
        // 3. Background art: chosen from the pop-over, drawn by the owner over the centre disc.
        {
            const auto art = coverageRoot_ / "Synthetic background.png";
            writeCoveragePNG(art, 400, 300);
            step.check(canvas().perform("theme:background", t) && coverageImageAsks_ == 3, "Background asks the chooser");
            done = std::exchange(coverageChooser_, {});
            done(art);
            drainUntil(step, [&] { return owner_->state()->profile().backgroundFilename.has_value() && canvas().artBox(); });
            for (unsigned n = 0; n < 4; ++n) step.present(t += 1. / 60);
            step.check(canvas().artBox() && owner_->stats().artResident && owner_->stats().rampResident && owner_->stats().artDraws > 0,
                       "The art layers exist and the owner draws the photo through the shared ramp");
            const auto draws = owner_->stats().artDraws, uploads = owner_->stats().artUploads;
            step.check(canvas().adjustAccessibilitySlider("profile:backgroundZoom", 3, t), "Background zoom commits");
            for (unsigned n = 0; n < 4; ++n) step.present(t += 1. / 60);
            step.check(owner_->stats().artUploads == uploads && owner_->stats().artDraws > draws, "Zooming the art moves numbers, never re-uploads the photo");
            step.check(canvas().perform("theme:default", t) && !owner_->state()->profile().backgroundFilename && owner_->state()->profile().backgroundZoom == 1,
                       "Default removes the art and resets the background values");
            for (unsigned n = 0; n < 4; ++n) step.present(t += 1. / 60);
            step.check(!canvas().artBox(), "Without art the page has no art layers");
        }
        canvas().closePopover();
        // 4. The inline editor over the name: an Edit peer holding its draft.
        {
            click(step, "profile:name", t);
            step.check(canvas().editingField() == m::ProfileField::name && canvas().inputLocked(), "A press on the name opens its inline editor and holds the plane");
            (void)core().automationRefresh(nullptr);
            const auto editor = peer("edit:name");
            // The Value pattern is a pattern provider of the element (as UIA asks for it).
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
            Microsoft::WRL::ComPtr<IUnknown> pattern;
            Microsoft::WRL::ComPtr<IValueProvider> value;
            BSTR draft{};
            step.check(editor && uiaControlType(editor.Get()) == UIA_EditControlTypeId && SUCCEEDED(editor.As(&simple)) &&
                       SUCCEEDED(simple->GetPatternProvider(UIA_ValuePatternId, &pattern)) && pattern && SUCCEEDED(pattern.As(&value)) && SUCCEEDED(value->get_Value(&draft)) &&
                       draft && std::wstring_view(draft) == L"Endministrator", "The open editor is an Edit peer holding its draft");
            SysFreeString(draft);
            core().automationDisconnect();
            (void)core().key({app::KeyKind::down, VK_ESCAPE}, 0, t);
            step.check(!canvas().editingField() && owner_->state()->profile().name == "Endministrator", "Esc discards the edit and keeps the stored name");
        }
        // 5. Official sync: the card shows the synced UID, never a # tag; the synced fields lock.
        sink_->setProfileSyncLocked(ehud::data::ProfileSyncFields::all());
        modules::hypergryph::GameProfileUpdate update;
        update.gamePlayerID = "1234567890123";
        update.permissionLevel = 42;
        sink_->applyGameProfile(update);
        const auto* card = owner_->card();
        step.check(card && card->captions()[1].text == "UID: 1234567890123" && card->captions()[2].text == "42" && card->captions()[4].text.empty(),
                   "Account sync updates the card at once");
        for (const auto& caption : card->captions()) step.check(caption.text.find('#') == std::string::npos, "No ID card caption shows the # tag");
        step.check(!canvas().canEdit(m::ProfileField::playerID) && canvas().canEdit(m::ProfileField::introduction) && owner_->state()->profile().gamePlayerID == "1234567890123" &&
                   owner_->state()->profile().name == "Endministrator", "Synced identity fields are read-only; the game update keeps unsent fields");
        sink_->setProfileSyncLocked(ehud::data::ProfileSyncFields{}.with(ehud::data::ProfileSyncField::uid).with(ehud::data::ProfileSyncField::levels));
        step.check(!canvas().canEdit(m::ProfileField::playerID) && !canvas().canEdit(m::ProfileField::permissionLevel) && canvas().canEdit(m::ProfileField::name),
                   "Per-field locks follow the account's synced fields");
        drainUntil(step, [&] { const auto* s = owner_->serviceStatus(); return s && !s->busy && !s->dirty; });
        step.check(ehud::data::ProfileStore(coverageRoot_).value().gamePlayerID == "1234567890123", "The single profile service persisted the account write");
    }
    // Projected input on every cycle: the eye hides the card text and shows it again.
    t += .05; step.present(t);
    click(step, "profile:visibility", t);
    step.check(canvas().textHidden(), "The eye hides the card text");
    click(step, "profile:visibility", t);
    step.check(!canvas().textHidden(), "and shows it again");
    t += .05; step.present(t);
    coverageAutomation(step, t);
    for (unsigned n = 0; n < 30; ++n) step.present(t += 1. / 60);
    // Settled frames (tilt, ambient motion, Work Mode ticks) do no card work.
    const auto feed = feed_.revision();
    const auto textures = std::array{owner_->card()->avatarRevision(), owner_->card()->backgroundRevision(), owner_->card()->hoverRevision()};
    for (unsigned n = 0; n < 12; ++n) step.present(t += 1. / 60);
    step.check(feed_.revision() == feed && textures == std::array{owner_->card()->avatarRevision(), owner_->card()->backgroundRevision(), owner_->card()->hoverRevision()},
               "Settled page frames publish no card content and regenerate no card texture");
}
// UI Automation over the presented page: the library's tree (buttons for the
// card's controls, the Card theme pop-over's sliders), through the owner's hooks.
void ProfileArea::coverageAutomation(app::ModuleCoverage& step, double& t) {
    step.check(automationRefresh(nullptr) && automationCount() > 0, "The owner's UIA hooks list the page's controls");
    const auto menu = peer("profile:menu");
    UiaRect bounds{}, under{};
    Fragment hit;
    step.check(menu && uiaControlType(menu.Get()) == UIA_ButtonControlTypeId && uiaText(menu.Get(), UIA_NamePropertyId) == widen(core::localized("Edit personal profile", "编辑个人名片", resolved(appearance_.language))),
               "⋯ is a button named Edit personal profile");
    step.check(menu && SUCCEEDED(menu->get_BoundingRectangle(&bounds)) && bounds.width > 0 && bounds.height > 0, "The button covers its projected control on screen");
    const double x = bounds.left + bounds.width / 2, y = bounds.top + bounds.height / 2;
    step.check(SUCCEEDED(automationFromPoint(x, y, &hit)) && hit && SUCCEEDED(hit->get_BoundingRectangle(&under)) &&
               x >= under.left && y >= under.top && x < under.left + under.width && y < under.top + under.height,
               "The screen hit test returns the projected control under the point");
    step.check(uiaInvoke(menu) == S_OK && canvas().menuOpen(), "Invoke opens the ⋯ menu like its click");
    (void)automationRefresh(nullptr);
    step.check(peer("profile.more:portrait") != nullptr, "The menu's rows are listed while it is open");
    (void)core().key({app::KeyKind::down, VK_ESCAPE}, 0, t);
    step.check(!canvas().menuOpen(), "Esc closes only the menu");
    t += .05; step.present(t);
    IRawElementProviderFragment* child{};
    (void)automationRefresh(nullptr);
    const auto listed = automationCount();
    step.check(automationChild(0, &child) == S_OK && child, "The first fragment is available");
    if (child) child->Release();
    child = nullptr;
    step.check(automationChild(listed, &child) == E_INVALIDARG && !child, "Out-of-range children are refused");
    automationDisconnect();
}
// Module-less step: the published card drives the presenter the host owns,
// here over the exported caption leaves (coverage fixture) and a private
// rasterizer, with the SourceScene replaced by a recording target.
void ProfileArea::coverageCard(app::ModuleCoverage& step) {
    step.check(host_.find<gpu::IdCardFeed>(gpu::IdCardFeed::serviceKey) == &feed_, "The ID card feed is published by exact type");
    drainUntil(step, [&] { return owner_->loaded() && owner_->card(); });
    step.check(owner_->card() && feed_.card() == owner_->card(), "The loaded card is published");
    // Work Mode lifetime hours end to end, on every cycle: the host's single
    // controller -> this area's checkpoint listener -> WorkModeProfileHours ->
    // ProfileState (the page's REGION CONSTRUCTION hours) -> profile.json at
    // flush. The host restores nothing and never writes profile.json.
    {
        const double now = step.time;
        if (const auto* hostWork = host_.workMode()) {
            step.check(work_ == hostWork && hostWork->checkpointListeners() >= 1, "Lifetime hours observe the host's one Work Mode controller");
            const auto restored = hostWork->restoredTrackedWorkSeconds();
            step.check(!restored || *restored == 0, "The host no longer restores the profile lifetime into Work Mode");
        }
        const auto live = work_->trackedWorkSeconds(now);
        const auto lifetime = owner_->state()->profile().accumulatedWorkSeconds;
        step.check(checkpoints_ == 0 || (lastCheckpoint_ <= live + 1e-9 && lifetime >= 3600 + lastCheckpoint_ - 1e-6),
                   "Every delivered Work Mode checkpoint reached the profile lifetime");
        if (work_->snapshot(now).phase != m::WorkModePhase::running) {
            // Settled (idle, paused, stopped or completed): the controller's
            // total has been checkpointed and counted once above the stored hours.
            step.check(checkpoints_ == 0 ? live == 0 : std::abs(lastCheckpoint_ - live) < 1e-6, "A settled Work Mode total has reached the checkpoint listener");
            step.check(std::abs(lifetime - (3600 + live)) < 1e-6, "The profile lifetime is the stored hours plus the Work Mode total, counted once");
        }
        // The host runs a stopwatch through Work Mode's accessible controls in
        // cycle 0, before this per-cycle step (application_verification), so
        // the checks above and the flush below carry real tracked time.
        if (host_.workMode())
            step.check(live > 0 && checkpoints_ >= 1 && lastCheckpoint_ > 0 && lifetime > 3600 && std::abs(lastCheckpoint_ - live) < 1e-6,
                       "The host's Work Mode stopwatch reached the profile lifetime with nonzero tracked time");
        if (!hostProfilePath_.empty())
            step.check(ehud::data::detail::readFile(hostProfilePath_, 1024 * 1024) == hostProfileBefore_,
                       "Only the profile area writes profile.json: the host's Work Mode leaves the data root's record untouched");
    }
    if (step.cycle != 0) {
        // After the cycle-0 flush the stored record keeps the same Work Mode
        // total counted once: no checkpoint or later frame adds it again.
        drainUntil(step, [&] { const auto* s = owner_->serviceStatus(); return s && !s->busy && !s->dirty; });
        const auto stored = ehud::data::ProfileStore(coverageRoot_).value().accumulatedWorkSeconds;
        step.check(std::abs(stored - (3600 + work_->trackedWorkSeconds(step.time))) < 1e-6 && std::abs(stored - owner_->state()->profile().accumulatedWorkSeconds) < 1e-6,
                   "Later cycles keep the stored lifetime at the stored hours plus the Work Mode total, counted once");
        return;
    }
    // Quit/session-end flush: the live Work Mode total lands once above the
    // stored lifetime and profile.json is drained; a flush never blocks quitting.
    const double now = step.time;
    step.check(flush(now), "The profile flush never blocks quitting");
    const auto lifetime = 3600 + work_->trackedWorkSeconds(now);
    step.check(std::abs(ehud::data::ProfileStore(coverageRoot_).value().accumulatedWorkSeconds - lifetime) < 1e-6 && owner_->serviceStatus() && !owner_->serviceStatus()->dirty,
               "The flushed lifetime is the stored hours plus the live Work Mode total");
    // A language event relocalizes Authority/MAX on the card (content event
    // only); the host's own appearance is restored before the next frame.
    auto other = appearance_;
    other.language = resolved(appearance_.language) == core::Language::simplifiedChinese ? core::Language::english : core::Language::simplifiedChinese;
    const auto before = feed_.revision();
    const auto host = appearance_;
    setAppearance(other, now);
    step.check(feed_.revision() > before && owner_->card()->captions()[3].text == core::localized("Authority", "权限等级", other.language), "A language change republishes the localized card");
    setAppearance(host, now);
    step.check(owner_->card()->captions()[3].text == core::localized("Authority", "权限等级", resolved(appearance_.language)), "The host language returns");
    const auto fixture = fixture_ ? fixture_("profile.idCardCaptions") : fs::path{};
    step.check(!fixture.empty(), "The exported caption fixture is declared");
    const auto bytes = ehud::data::detail::readFile(fixture, 8 * 1024 * 1024);
    step.check(bytes.has_value(), "The exported caption fixture is readable");
    const auto exported = Json::parse(*bytes, 8 * 1024 * 1024);
    gpu::LayerRasterizer raster;
    gpu::LayerRasterOptions options;
    gpu::LayerScene labels(raster);
    labels.load(exported["nativeLayers"], options);
    std::vector<std::string> replaced, restored;
    gpu::IdCardTextureTarget target{
        [&](std::string_view id, unsigned w, unsigned h, std::span<const std::uint8_t> rgba, std::uint64_t) {
            replaced.push_back(std::string(id) + ":" + std::to_string(w) + "x" + std::to_string(h)); return rgba.size() == std::size_t(w) * h * 4; },
        [&](std::string_view id) { restored.emplace_back(id); return true; }};
    // The exported baseline binds the three textures to the card's nodes.
    const auto* card = owner_->card();
    const auto& source = card->source();
    core::source::SourceDesktopFrameSettings baseline;
    baseline.images[source.playerHead] = {"desktop.profile.avatar", {272, 272}, {}};
    baseline.images[source.background] = {"desktop.profile.background", {530, 204}, {}};
    baseline.images["profile-hover-plate"] = {"desktop.profile.hover", {530, 204}, {}};
    const auto leaves = gpu::idCardCaptionLeaves(exported["nativeLayers"]);
    gpu::IdCardPresenter presenter(labels, leaves, options, target, baseline);
    // A private mirror of the published content: the host's presenter keeps
    // the real feed's listener.
    unsigned requests{};
    gpu::IdCardFeed mirror;
    presenter.connect(mirror, [&] { ++requests; });
    mirror.publish(card, cardTextures());
    step.check(requests == 1 && presenter.highlightNode() == std::optional<std::string>("profile-hover-plate") && presenter.pending(),
               "The presenter hears the published card and finds the hover plate");
    const auto settings = presenter.apply();
    const auto painted = [&](std::size_t i) { const auto layout = labels.paintedTextLayout(leaves.surfaceIDs[i]); return layout ? std::u16string(layout->text()) : std::u16string{}; };
    step.check(painted(1) == u"UID: 1234567890123" && painted(2) == u"42" && painted(4).empty(), "The bottom-left card captions show the synced profile");
    step.check(painted(1).find(u'#') == std::u16string::npos, "The card UID never shows the # tag");
    step.check(replaced.size() == 3 && replaced[0] == "desktop.profile.avatar:368x368" && replaced[1] == "desktop.profile.background:530x204" &&
               replaced[2] == "desktop.profile.hover:530x204" && restored.empty(),
               "The photo crop, themed background and hover plate replace the packet textures");
    step.check(settings.has_value() && settings->images.at(source.playerHead).size == std::array<float, 2>{368, 368} &&
               settings->properties.at(source.levelSlider).at("m_FillAmount") == 42. / 60 &&
               settings->properties.at(source.headFrame).at("m_Color.r") == card->accent()[0],
               "Desktop settings carry the avatar size, the level fill and the card accent");
    step.check(!presenter.apply() && presenter.stats().unchanged == 1, "An unchanged feed is a no-op");
    mirror.withdraw();
    (void)presenter.apply();
    step.check((restored == std::vector<std::string>{"desktop.profile.avatar", "desktop.profile.background", "desktop.profile.hover"}) && presenter.settings() == baseline,
               "Without a card the packet textures and the baseline settings return");
    presenter.disconnect();
}
void ProfileArea::coverage(app::ModuleCoverage& step) {
    step.check(hiddenCoverage(), "Profile coverage runs only in hidden coverage");
    if (step.module) {
        step.check(*step.module == core::Module::profile, "The profile area presents only Personal ID");
        coverageModule(step);
    } else coverageCard(step);
}
} // namespace

std::unique_ptr<app::ModuleOwner> profileModule(const app::ApplicationModuleContext& context) {
    if (!context.host || !context.utility || !context.window) return nullptr;
    // The packaged card artwork and pinned card source; absent in development
    // previews (no package), where the module stays absent.
    const auto resources = context.resource ? context.resource(resourceID) : fs::path{};
    if (resources.empty()) return nullptr;
    // Personal ID is a design screen on the shared Context (HOST-HOOKS "Areas").
    auto* design = context.host->designContext();
    if (!design) return nullptr;
    tools::DesignOwnerOptions options;
    if (!context.host->designOwnerOptions(options, "profile", ProfileArea::profileRouting())) return nullptr;
    auto area = std::make_unique<ProfileArea>(context, resources, *design, std::move(options));
    context.host->setDesignScreen(core::Module::profile);
    return area;
}
} // namespace endfield::areas
