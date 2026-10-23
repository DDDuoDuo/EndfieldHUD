#include "app/areas/profile_module.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include "modules/hypergryph_account_identity_sync.hpp"
#include "native/id_card_presenter.hpp"
#include "native/layer_scene.hpp"
#include "native/layer_text_layout.hpp"
#include "native/notes_clipboard.hpp"
#include "native/profile_automation.hpp"
#include "native/profile_pickers.hpp"
#include "tools/profile_owner.hpp"
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
// The page canvas (PersonalProfileCanvas 400 x 334 points).
constexpr double canvasWidth = 400, canvasHeight = 334;

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
Fragment uiaPeer(const gpu::NativeProfileAutomation& automation, std::string_view id) {
    const auto wanted = widen(id);
    for (std::size_t n = 0; n < automation.size(); ++n) {
        Fragment f;
        if (SUCCEEDED(automation.child(n, &f)) && f && uiaText(f.Get(), UIA_AutomationIdPropertyId) == wanted) return f;
    }
    return nullptr;
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

// Hidden coverage only: the page's Edit-menu clipboard, never the system one.
struct CoverageClipboard final : gpu::NotesClipboard {
    std::optional<gpu::NotesClipboardText> value;
    unsigned reads{}, writes{};
    std::optional<gpu::NotesClipboardText> readText(std::uint32_t maximum) override {
        ++reads;
        if (!value || value->text.size() > maximum) return std::nullopt;
        return value;
    }
    std::vector<std::string> readFiles() override { return {}; }
    bool writeText(const gpu::NotesClipboardText& text) override { ++writes; value = text; return true; }
};

// HypergryphAccountController's profile side over the single profile owner
// (never a second ProfileStore writer): official game fields are applied to
// the current record as an account write, the lock mirrors gameSyncedFields and
// the game avatar becomes the managed avatar synchronously.
class ProfileAccountSink final : public modules::hypergryph::AccountProfileSink {
public:
    ProfileAccountSink(tools::ProfileOwner& owner, app::ApplicationHost& host) : owner_(owner), host_(host) {}
    void applyGameProfile(const modules::hypergryph::GameProfileUpdate& update) override {
        owner_.acceptFromGame([update](m::PersonalProfile& p) { modules::hypergryph::applyGameProfileUpdate(p, update); }, host_.now());
    }
    void setProfileSyncLocked(ehud::data::ProfileSyncFields locked) override { owner_.setSyncLocked(locked, host_.now()); }
    std::optional<std::string> importGameAvatar(const std::vector<std::uint8_t>& png) override { return owner_.importGameAvatar(png, host_.now()); }
private:
    tools::ProfileOwner& owner_;
    app::ApplicationHost& host_;
};

class ProfileArea final : public app::ModuleOwner {
public:
    ProfileArea(const app::ApplicationModuleContext& c, fs::path resources)
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
        o.appRoot = root;
        o.resources = std::move(resources);
        o.timeZone = zone_;
        o.textMessage = host_.registerMessage();
        o.pickerMessage = host_.registerMessage();
        pickerMessage_ = o.pickerMessage;
        o.requestImage = [this](m::ProfileImageKind kind, tools::ProfileOwnerOptions::ImageCompletion done) { return chooseImage(kind, std::move(done)); };
        o.chooseColor = [this](std::array<double, 3> initial, const std::function<void(std::array<double, 3>)>& live) { return chooseColor(initial, live); };
        o.cancelPicker = [this] { cancelPickers(); };
        o.cropChanged = [this](std::string_view target) {
            ++cropEvents_; lastCrop_ = target;
            host_.recordEvent(modules::EventKind::profileCropChanged, {{"target", std::string(target)}});
        };
#ifdef EHUD_PROFILE_ACCOUNT_LINK
        o.loaded = [this](const m::PersonalProfile& p) { if (account_) account_->profileLoaded(p.avatarFilename); };
        o.avatarChanged = [this](const std::optional<std::string>& name) { if (account_) account_->profileAvatarChanged(name); };
#endif
        o.cardChanged = [this] { publishCard(); };
        o.changed = [this] { host_.requestFrame(); };
        // AppDelegate Edit menu (Ctrl+X/C/V) over the open field: the system
        // clipboard, touched only on those keys; a synthetic one in coverage.
        if (hiddenCoverage()) o.clipboard = coverageClipboard_ = std::make_shared<CoverageClipboard>();
        else o.clipboard = gpu::systemNotesClipboard();
        auto* manager = static_cast<ITfThreadMgr*>(c.textManager);
        owner_ = std::make_unique<tools::ProfileOwner>(hwnd_, *c.utility, c.rasterizer, images_, *work_, std::move(o), manager,
                                                       manager ? static_cast<TfClientId>(c.textClient) : TF_CLIENTID_NULL);
        // Work Mode lifetime: every absolute checkpoint of the shared controller.
        listener_ = work_->listenCheckpoints([this](double total) {
            ++checkpoints_; lastCheckpoint_ = total;
            if (owner_) owner_->workModeCheckpoint(total);
        });
        // The host delivers the resolved appearance with its next configuration
        // pass (configurationPending); until then the source defaults stand.
        owner_->resize(c.metrics);
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
        automation_.reset(); // its callbacks borrow the owner
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

    // --- ModuleOwner -----------------------------------------------------------
    std::string_view name() const noexcept override { return "profile"; }
    app::ModuleRouting routing() const noexcept override {
        app::ModuleRouting r;
        r.pointerCapture = 20; r.keyCapture = 20;          // open popover, picker or projected field
        r.pointer = 30; r.key = 30; r.filterKey = 20; r.message = 30;
        r.finishEditing = 30; r.flush = 30;
        return r;
    }
    bool presents(core::Module module) const noexcept override { return module == core::Module::profile; }
    void resize(const app::ClientMetrics& metrics) override { owner_->resize(metrics); }
    void setAppearance(const app::ModuleAppearance& a, double t) override { appearance_ = a; owner_->setAppearance(profileAppearance(a), t); }
    // HUDSourceWatchView.desktopUIDRedacted follows the applied record's Recording mode (B2b).
    tools::ProfileOwnerAppearance profileAppearance(const app::ModuleAppearance& a) const {
        auto value = profileAppearanceOf(a); value.recordingMode = host_.settings().recordingMode(); return value;
    }
    void setOverlayVisible(bool visible, double t) override { owner_->setOverlayVisible(visible, t); }
    void overlayClosing(double t) override { owner_->overlayClosing(t); }
    void update(const app::ModuleFrame& f) override { owner_->update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    // The first opening's page is built while the HUD is still closed.
    void prewarm(const app::ModuleFrame& f) override { owner_->prewarm(f.presentation); }
    bool requiresFrames(double t) const override { return owner_->requiresFrames(t); }
    std::optional<double> nextWakeTime(double now) const override { return owner_->nextWakeTime(now); }
    bool deadline(double t) override { return owner_->deadline(t); }
    void upload(gpu::Renderer& r) override { owner_->upload(r); }
    std::span<const gpu::LayerCompositionEntry> entries(core::Module module) override {
        return module == core::Module::profile ? owner_->entries() : std::span<const gpu::LayerCompositionEntry>{};
    }
    void collected(gpu::Renderer& r) override { owner_->collected(r); }
    void release(gpu::Renderer& r) override { owner_->release(r); }
    bool covers(core::Point p) const override { return owner_->covers(p); }
    bool pointerLocked() const override { return owner_->pointerLocked(); }
    bool capturesPointer() const override { return owner_->capturesPointer(); }
    bool capturesKeys() const override {
        const auto* page = owner_->page();
        return owner_->capturesPointer() || (page && page->editing());
    }
    bool pointer(const app::PointerEvent& e, double t) override { return owner_->pointer(e, t); }
    bool key(const app::KeyEvent& e, double t) override { return owner_->key(e, t); }
    bool filterKey(const app::NativeMessage& m) override { return owner_->filterKey(m); }
    bool message(const app::NativeMessage& m, double t) override { return owner_->message(m, t); }
    void focus(bool value, double t) override { owner_->focus(value, t); }
    void cancelInteraction(double t) override { owner_->cancelInteraction(t); }
    bool finishEditing(double t) override { return owner_->finishEditing(t); }
    bool flush(double t) override {
        // OverlayController: the final Work Mode total reaches the profile
        // before it is drained. The absolute live total equals the checkpoint
        // WorkModeController::shutdown delivers, whatever the host's order.
        if (std::isfinite(t)) owner_->workModeCheckpoint(work_->trackedWorkSeconds(std::max(t, 0.)));
        // A failed save stays on the page with the store's message, like the
        // Mac's synchronous UserProfileStore save; it never blocks quitting.
        (void)owner_->flush(t);
        return true;
    }
    bool hostEvent(const app::HostEvent& e, double t) override {
        using K = app::HostEvent::Kind;
        switch (e.kind) {
        case K::utilityCompleted: owner_->queueCapacityAvailable(); return false;
        // HUDModuleContent keeps only the shown screen: a Personal Profile
        // page that is not shown when the HUD has closed drops its artwork.
        case K::overlayConcealed: owner_->conceal(); return false;
        case K::timeChanged:
        case K::systemSettingsChanged: owner_->setTimeZone(zone_, t); return true;
        default: return false;
        }
    }
    void coverage(app::ModuleCoverage& step) override;
    // Pointer cursor (HUDSourceWatchView.refreshSourceCursor ->
    // HUDProjectedTextView.projectedCursor): the system I-beam over the open
    // projected profile field, no opinion elsewhere. No allocation or state.
    std::optional<app::ModuleCursor> cursorAt(core::Point p) const override {
        return owner_->textCursorAt(p) ? std::optional(app::ModuleCursor::text) : std::nullopt;
    }

    // UI Automation (HUDPersonalProfileInteraction.layoutAccessibility): one
    // fragment per projected action and slider under the profile group.
    // Created on the first refresh (a UIA client asked the HUD); a new group
    // root (another HudAutomation) rebuilds the fragments under it.
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override {
        bool rebuilt = false;
        if (!automation_ || automationRoot_ != root) {
            if (automation_) { automation_->disconnect(); rebuilt = true; }
            automation_ = std::make_unique<gpu::NativeProfileAutomation>(automationHost(root));
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
    // Hidden coverage choosers.
    tools::ProfileOwnerOptions::ImageCompletion coverageChooser_;
    std::optional<app::ApplicationFileRequest> coverageRequest_; // the request production would queue
    std::optional<std::array<double, 3>> coverageColor_;
    unsigned coverageImageAsks_{}, coverageColorAsks_{}, cropEvents_{};
    std::shared_ptr<CoverageClipboard> coverageClipboard_;
    std::string lastCrop_;
    // Declaration order: the owner borrows the image source and is destroyed first.
    gpu::LayerImageSource images_;
    std::unique_ptr<tools::ProfileOwner> owner_;
    std::unique_ptr<ProfileAccountSink> sink_; // borrows the owner; detached and destroyed before it
    std::unique_ptr<gpu::NativeProfileAutomation> automation_; // borrows the owner; destroyed first
    IRawElementProviderFragmentRoot* automationRoot_{};       // identity only (the automation holds the reference)

    // The host's UI Automation view of this owner (native/profile_automation.hpp).
    gpu::ProfileAutomationHost automationHost(IRawElementProviderFragmentRoot* root) {
        gpu::ProfileAutomationHost view;
        view.root = root;
        view.controls = [this] {
            auto* page = owner_->page();
            const auto* state = owner_->state();
            if (!page || !state || !page->active()) return std::vector<gpu::ProfileAutomationControl>{};
            std::optional<gpu::ProfileAutomationEditor> editor;
            if (auto view = page->editingView()) editor = gpu::ProfileAutomationEditor{view->field, view->rect, std::move(view->text)};
            return gpu::profileAutomationControls(*state, editor);
        };
        view.toScreen = [this](core::Rect canvas) -> std::optional<core::Rect> {
            // Canvas -> logical client -> physical client pixels -> screen pixels.
            const auto client = owner_->clientRect(canvas);
            const double scale = host_.metrics().scale;
            POINT origin{0, 0};
            if (!client || !(scale > 0) || !ClientToScreen(hwnd_, &origin)) return std::nullopt;
            return core::Rect{origin.x + client->x * scale, origin.y + client->y * scale, client->width * scale, client->height * scale};
        };
        // activateAction: the open field commits first (ProfileOwner::perform).
        view.invoke = [this](std::string_view id) {
            const bool used = owner_->perform(id, host_.now());
            if (used) automationContentChanged();
            return used;
        };
        // adjustSlider: refused while a chooser or the colour panel is open.
        view.setValue = [this](std::string_view id, double value) {
            const auto field = m::profileField(id);
            if (!field || owner_->pickerOpen()) return false;
            const bool used = owner_->setSlider(*field, value, host_.now());
            if (used) automationContentChanged();
            return used;
        };
        // The open field's Value: replaces its draft like typed text.
        view.setText = [this](std::string_view, std::string_view text) {
            const bool used = owner_->setEditingText(text, host_.now());
            if (used) automationContentChanged();
            return used;
        };
        return view;
    }
    // A UIA press changed the page outside the host's input routes: present
    // it and re-list the fragments now (an opened popover replaces them),
    // raising ChildrenInvalidated on this owner's group like the host.
    void automationContentChanged() {
        host_.requestFrame();
        if (!automation_ || !automation_->refresh() || !automationRoot_ || !UiaClientsAreListening()) return;
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> group;
        if (SUCCEEDED(automationRoot_->QueryInterface(IID_PPV_ARGS(&group))))
            UiaRaiseStructureChangedEvent(group.Get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
    }

    static tools::ProfileOwnerAppearance profileAppearanceOf(const app::ModuleAppearance& a) {
        return {resolved(a.language), a.dark, a.accentSRGB, 2, a.reduceMotion};
    }
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
    // HUDPersonalProfileInteraction.chooseImage: NSOpenPanel (allowedContentTypes
    // [.image], no multiple selection, no directories) as the host's shared
    // dialog in its single-file mode with the "Images" filter.
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
    std::optional<core::Point> actionPoint(app::ModuleCoverage& step, std::string_view id) {
        const auto* state = owner_->state();
        if (!state) return std::nullopt;
        for (const auto& action : state->actions())
            if (action.id == id) return step.project({action.rect.x + action.rect.width / 2, action.rect.y + action.rect.height / 2}, canvasWidth, canvasHeight);
        return std::nullopt;
    }
    void click(app::ModuleCoverage& step, core::Point p, double& t) {
        step.check(owner_->pointer({app::PointerKind::down, app::PointerButton::left, p.x, p.y}, t), "The projected profile control consumes the press");
        (void)owner_->pointer({app::PointerKind::up, app::PointerButton::left, p.x, p.y}, t + .001);
        t += .05; step.present(t);
    }
    void routePickerMessages(app::ModuleCoverage& step) {
        MSG msg;
        while (PeekMessageW(&msg, hwnd_, pickerMessage_, pickerMessage_, PM_REMOVE)) (void)step.message(msg.message, msg.wParam, msg.lParam);
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
    auto* page = owner_->page();
    step.check(page && page->active() && !owner_->entries().empty(), "Personal Profile is the selected, published module page");
    step.check(work_->checkpointListeners() >= 1 && owner_->nextWakeTime(t).has_value(), "Lifetime hours observe Work Mode and refresh on the shared clock");
    const auto& profile = owner_->state()->profile();
    step.check(profile.accumulatedWorkSeconds >= 3600, "The stored lifetime hours are kept");
    if (step.cycle == 0) {
        // 1. Image chooser: queued through the host route, imported on the worker.
        const auto photo = coverageRoot_ / "Synthetic portrait.png";
        writeCoveragePNG(photo, 300, 200);
        step.check(owner_->perform("profile:avatar", t) && coverageImageAsks_ == 1 && owner_->pickerOpen() && owner_->capturesPointer(),
                   "Change profile picture queues one chooser and captures input while it is open");
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
        step.check(!automationHost(nullptr).setValue(m::profileFieldID(m::ProfileField::thumbnailZoom), 2), "UIA slider values are refused while a chooser is open");
        auto done = std::exchange(coverageChooser_, {});
        done(photo);
        drainUntil(step, [&] { return owner_->state()->profile().avatarFilename.has_value() && owner_->cardTexture(tools::IdCardTextureKind::avatar); });
        const auto avatar = owner_->state()->profile().avatarFilename;
        step.check(avatar && avatar->ends_with(".image") && fs::exists(coverageRoot_ / "Profile" / "Images" / *avatar) && !owner_->pickerOpen(),
                   "The chosen picture becomes the managed avatar in the profile root");
        const auto* texture = feed_.texture(gpu::IdCardTextureSlot::avatar);
        step.check(texture && texture->width == 368 && texture->height == 368 && texture->revision == owner_->card()->avatarRevision(),
                   "The ID card avatar is the 136 pt crop the presenter binds to desktop.profile.avatar");
        // The page portrait (HUDPortraitArtwork at 55 pt) is cropped on the
        // shared worker like the card avatar: a zoom step queues one crop and
        // the owner thread only publishes the result.
        {
            drainUntil(step, [&] { const auto* p = owner_->page(); return p && !p->resources().cropPending && p->resources().cropPublications > 0; });
            const auto before = owner_->page()->resources();
            step.check(before.cropJobs >= 1 && before.cropPublications >= 1 && !before.cropPending && before.retainedCropBytes > 0 && before.retainedCropBytes <= 256 * 256 * 4,
                       "The page portrait is cropped on the shared worker and published");
            step.check(owner_->perform("profile:portraitMenu", t) && owner_->setSlider(m::ProfileField::avatarZoom, 2, t) &&
                       owner_->page()->resources().cropPending && owner_->page()->resources().cropJobs == before.cropJobs + 1 &&
                       owner_->page()->resources().cropPublications == before.cropPublications,
                       "A portrait zoom step queues one worker crop instead of cropping on the owner thread");
            drainUntil(step, [&] { return !owner_->page()->resources().cropPending; });
            step.check(owner_->page()->resources().cropPublications == before.cropPublications + 1, "The zoomed portrait is published from the worker");
            step.check(owner_->setSlider(m::ProfileField::avatarZoom, 1, t) && owner_->perform("profile:popoverClose", t), "The portrait zoom returns and Adjust portrait closes");
            drainUntil(step, [&] { return !owner_->page()->resources().cropPending; });
            t += .05; step.present(t);
        }
        // A cancelled chooser changes nothing.
        step.check(owner_->perform("profile:avatar", t) && coverageImageAsks_ == 2, "The chooser opens again");
        done = std::exchange(coverageChooser_, {});
        done(std::nullopt);
        step.check(owner_->state()->profile().avatarFilename == avatar && !owner_->pickerOpen(), "A cancelled chooser keeps the avatar");
        // 2. Card colour: posted request, live preview, committed result.
        coverageColor_ = std::array<double, 3>{0, .5, 1};
        step.check(owner_->perform("profile:themeMenu", t) && owner_->perform("profile:themeCustom", t), "Custom card colour is requested");
        routePickerMessages(step);
        drainUntil(step, [&] { return owner_->state()->profile().themeColorHex == std::optional<std::string>("0080FF"); });
        step.check(coverageColorAsks_ == 1 && owner_->state()->profile().themeColorHex == std::optional<std::string>("0080FF") &&
                   owner_->card()->captions()[4].color[2] == 1, "The chosen colour commits and recolours the card");
        step.check(owner_->perform("profile:popoverClose", t), "The colour popover closes");
        // Crop sliders through UI Automation (HUDProfileAccessibilitySlider): a
        // committed thumbnail zoom records profileCropChanged {thumbnail}
        // (SystemEventRecorder) and regenerates the card background.
        {
            const auto backgroundBefore = owner_->card()->backgroundRevision();
            gpu::NativeProfileAutomation automation(automationHost(nullptr));
            (void)automation.refresh();
            step.check(uiaInvoke(uiaPeer(automation, "profile:backgroundMenu")) == S_OK && owner_->state()->popover() == m::ProfilePopover::background && automation.refresh(),
                       "Change card theme opens through its UIA button and lists the crop sliders");
            const auto& sliders = owner_->state()->sliders();
            const auto slider = std::find_if(sliders.begin(), sliders.end(), [](const auto& v) { return v.field == m::ProfileField::thumbnailZoom; });
            const auto peer = uiaPeer(automation, m::profileFieldID(m::ProfileField::thumbnailZoom));
            Microsoft::WRL::ComPtr<IRangeValueProvider> zoom;
            double minimum{}, maximum{}, change{}, value{};
            step.check(slider != sliders.end() && peer && SUCCEEDED(peer.As(&zoom)) && SUCCEEDED(zoom->get_Minimum(&minimum)) && SUCCEEDED(zoom->get_Maximum(&maximum)) &&
                       SUCCEEDED(zoom->get_SmallChange(&change)) && SUCCEEDED(zoom->get_Value(&value)) && minimum == slider->minimum && maximum == slider->maximum &&
                       change == slider->step && value == slider->value && uiaControlType(peer.Get()) == UIA_SliderControlTypeId &&
                       uiaText(peer.Get(), UIA_NamePropertyId) == widen(slider->label) && uiaText(peer.Get(), UIA_HelpTextPropertyId) == widen(slider->valueDescription),
                       "The thumbnail zoom is a UIA slider over the source range and step, described by its value");
            step.check(zoom && zoom->SetValue(std::nan("")) == E_INVALIDARG && cropEvents_ == 0, "A non-finite slider value is refused");
            step.check(zoom && SUCCEEDED(zoom->SetValue(maximum + 10)) && owner_->state()->profile().thumbnailZoom == maximum,
                       "A value past the range is clamped like the source setAccessibilityValue");
            step.check(cropEvents_ == 1 && lastCrop_ == "thumbnail" && owner_->card()->backgroundRevision() > backgroundBefore, "The crop is recorded once and moves the card background");
            step.check(zoom && SUCCEEDED(zoom->SetValue(2)) && owner_->state()->profile().thumbnailZoom == 2 && cropEvents_ == 2, "The thumbnail zoom slider commits through RangeValue");
            automation.disconnect();
            step.check(zoom && zoom->get_Value(&value) == UIA_E_ELEMENTNOTAVAILABLE && !uiaAvailable(peer.Get()), "Disconnected fragments are no longer available");
        }
        drainUntil(step, [&] { const auto* b = feed_.texture(gpu::IdCardTextureSlot::background); return b && b->revision == owner_->card()->backgroundRevision(); });
        step.check(feed_.texture(gpu::IdCardTextureSlot::background) && feed_.texture(gpu::IdCardTextureSlot::background)->revision == owner_->card()->backgroundRevision(),
                   "The regenerated card background is published");
        step.check(owner_->perform("profile:popoverClose", t), "The card theme popover closes");
        // The open projected field is an Edit peer (the source NSTextView):
        // named by its field, holding its draft, written through Value.
        {
            step.check(owner_->perform("profile:menu", t) && owner_->perform("profile:name", t) && owner_->page()->editingField() == m::ProfileField::name,
                       "Edit name opens the projected name field");
            gpu::NativeProfileAutomation automation(automationHost(nullptr));
            (void)automation.refresh();
            const auto editor = uiaPeer(automation, "profile:editor:name");
            Microsoft::WRL::ComPtr<IValueProvider> value;
            BSTR draft{};
            step.check(editor && !uiaPeer(automation, "profile:name") && uiaControlType(editor.Get()) == UIA_EditControlTypeId &&
                       uiaText(editor.Get(), UIA_NamePropertyId) == widen(m::profileFieldTitle(m::ProfileField::name, resolved(appearance_.language))) &&
                       SUCCEEDED(editor.As(&value)) && SUCCEEDED(value->get_Value(&draft)) && draft && std::wstring_view(draft) == L"Endministrator",
                       "The open field is an Edit peer holding its draft in place of its button");
            SysFreeString(draft);
            draft = nullptr;
            const auto view = [&] { const auto* page = owner_->page(); return page ? page->editingView() : std::nullopt; };
            // The pointer cursor over the open field (projectedCursor): the
            // I-beam inside its projected rect, the source cursor beside it.
            const auto opened = view();
            const auto field = opened ? owner_->clientRect(opened->rect) : std::nullopt;
            const core::Point center = field ? core::Point{field->x + field->width / 2, field->y + field->height / 2} : core::Point{};
            const auto beside = opened ? owner_->clientRect({opened->rect.x + opened->rect.width / 2, opened->rect.y >= 12 ? opened->rect.y - 8 : opened->rect.y + opened->rect.height + 8, 0, 0}) : std::nullopt;
            step.check(field && field->width > 0 && cursorAt(center) == std::optional(app::ModuleCursor::text), "The open profile field answers the I-beam over its projected rect");
            step.check(beside && !cursorAt({beside->x, beside->y}), "Beside the open field the profile area keeps the source cursor");
            step.check(value && SUCCEEDED(value->SetValue(L"Endmin 管理员")) && view() && view()->text == "Endmin \xe7\xae\xa1\xe7\x90\x86\xe5\x91\x98",
                       "A Value write replaces the draft through the field's document");
            step.check(!automation.refresh() && SUCCEEDED(value->get_Value(&draft)) && draft && std::wstring_view(draft) == L"Endmin 管理员",
                       "The peer keeps its identity and reports the new draft");
            SysFreeString(draft);
            // AppDelegate Edit menu over the open field, through the synthetic
            // clipboard with explicitly stated modifiers.
            {
                auto* editingPage = owner_->page();
                const tools::ProfileKeyModifiers control{false, true, false};
                const auto writes = coverageClipboard_->writes, reads = coverageClipboard_->reads;
                step.check(editingPage && editingPage->key({app::KeyKind::down, 'A'}, control, t) && editingPage->key({app::KeyKind::down, 'C'}, control, t) &&
                           coverageClipboard_->writes == writes + 1 && coverageClipboard_->value && coverageClipboard_->value->text == u"Endmin 管理员",
                           "Ctrl+C copies the selected draft to the clipboard");
                coverageClipboard_->value = gpu::NotesClipboardText{u"Pasted", std::nullopt, std::nullopt};
                step.check(editingPage->key({app::KeyKind::down, 'V'}, control, t) && coverageClipboard_->reads == reads + 1 && view() && view()->text == "Pasted",
                           "Ctrl+V pastes over the selection like typing");
            }
            step.check(owner_->page()->finishEditing(false, t) && !owner_->page()->editing() && owner_->state()->profile().name == "Endministrator",
                       "Discarding the field keeps the stored name");
            step.check(!cursorAt(center), "Closing the field restores the source cursor");
            step.check(automation.refresh() && !uiaPeer(automation, "profile:editor:name") && value->SetValue(L"x") == UIA_E_ELEMENTNOTAVAILABLE,
                       "The closed field's peer is gone");
            if (owner_->state()->popover()) step.check(owner_->perform("profile:popoverClose", t), "The identity popover closes");
        }
        // 3. Official sync: the card shows the synced UID, never a # tag.
        sink_->setProfileSyncLocked(ehud::data::ProfileSyncFields::all());
        modules::hypergryph::GameProfileUpdate update;
        update.gamePlayerID = "1234567890123";
        update.permissionLevel = 42;
        sink_->applyGameProfile(update);
        const auto* card = owner_->card();
        step.check(card && card->captions()[1].text == "UID: 1234567890123" && card->captions()[2].text == "42" && card->captions()[4].text.empty(),
                   "Account sync updates the card at once");
        for (const auto& caption : card->captions()) step.check(caption.text.find('#') == std::string::npos, "No ID card caption shows the # tag");
        step.check(!owner_->state()->canEdit(m::ProfileField::playerID) && owner_->state()->profile().gamePlayerID == "1234567890123" &&
                   owner_->state()->profile().name == "Endministrator", "Synced identity fields are read-only; the game update keeps unsent fields");
        drainUntil(step, [&] { const auto* s = owner_->serviceStatus(); return s && !s->busy && !s->dirty; });
        step.check(ehud::data::ProfileStore(coverageRoot_).value().gamePlayerID == "1234567890123", "The single profile service persisted the account write");
    }
    // Projected input on every cycle: the identity menu opens and closes.
    t += .05; step.present(t);
    const auto menu = actionPoint(step, "profile:menu");
    step.check(menu.has_value(), "The profile menu projects through the module plane");
    click(step, *menu, t);
    step.check(owner_->state()->popover() == m::ProfilePopover::identity && owner_->capturesPointer() && capturesKeys(), "A click opens the identity popover, which captures input");
    const auto close = actionPoint(step, "profile:popoverClose");
    step.check(close.has_value(), "The popover close control projects");
    click(step, *close, t);
    step.check(!owner_->state()->popover(), "The close control dismisses the popover");
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
// UI Automation over the presented page (HUDPersonalProfileInteraction.
// layoutAccessibility): a private fragment set without a group root, then the
// owner's own hooks as the host's group refresh calls them.
void ProfileArea::coverageAutomation(app::ModuleCoverage& step, double& t) {
    {
        gpu::NativeProfileAutomation automation(automationHost(nullptr));
        const auto* state = owner_->state();
        step.check(automation.refresh() && !state->actions().empty() && automation.size() == state->actions().size() + state->sliders().size(),
                   "One UIA fragment per projected profile action and slider");
        const auto& actions = state->actions();
        const auto action = std::find_if(actions.begin(), actions.end(), [](const auto& a) { return a.id == "profile:menu"; });
        const auto menu = uiaPeer(automation, "profile:menu");
        step.check(action != actions.end() && menu && uiaText(menu.Get(), UIA_NamePropertyId) == widen(action->label) &&
                   uiaText(menu.Get(), UIA_HelpTextPropertyId) == widen(state->accessibilityStatus()) && uiaControlType(menu.Get()) == UIA_ButtonControlTypeId,
                   "A profile action is a button named by its label with the page status as help");
        UiaRect bounds{}, under{};
        Fragment hit;
        step.check(menu && SUCCEEDED(menu->get_BoundingRectangle(&bounds)) && bounds.width > 0 && bounds.height > 0, "The button covers its projected control on screen");
        const double x = bounds.left + bounds.width / 2, y = bounds.top + bounds.height / 2;
        step.check(SUCCEEDED(automation.fromPoint(x, y, &hit)) && hit && SUCCEEDED(hit->get_BoundingRectangle(&under)) &&
                   x >= under.left && y >= under.top && x < under.left + under.width && y < under.top + under.height,
                   "The screen hit test returns the projected control under the point");
        step.check(uiaInvoke(menu) == S_OK && owner_->state()->popover() == m::ProfilePopover::identity, "Invoke opens the identity popover like its click");
        step.check(automation.refresh() && !uiaPeer(automation, "profile:menu") && !uiaAvailable(menu.Get()), "The popover's controls replace the page's");
        const auto close = uiaPeer(automation, "profile:popoverClose");
        step.check(uiaInvoke(close) == S_OK && !owner_->state()->popover(), "Invoke on the close control dismisses the popover");
        step.check(automation.refresh() && uiaPeer(automation, "profile:menu"), "The page's controls return");
    }
    t += .05; step.present(t);
    // The owner's hooks (the host refreshes them with its group as the root).
    const auto listed = owner_->state()->actions().size() + owner_->state()->sliders().size();
    step.check(automationRefresh(nullptr) && automationCount() == listed, "The owner's UIA hooks list the page's controls");
    IRawElementProviderFragment* child{};
    step.check(automationChild(0, &child) == S_OK && child, "The first fragment is available");
    if (child) child->Release();
    child = nullptr;
    step.check(automationChild(listed, &child) == E_INVALIDARG && !child && !automationRefresh(nullptr), "Out-of-range children are refused; an unchanged page is no structure change");
    automationDisconnect();
    step.check(automationCount() == 0, "Disconnecting empties the profile group");
    automation_.reset();
    automationRoot_ = nullptr;
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
    owner_->setAppearance(profileAppearance(other), now);
    step.check(feed_.revision() > before && owner_->card()->captions()[3].text == core::localized("Authority", "权限等级", other.language), "A language change republishes the localized card");
    owner_->setAppearance(profileAppearance(appearance_), now);
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
        step.check(*step.module == core::Module::profile, "The profile area presents only Personal Profile");
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
    return std::make_unique<ProfileArea>(context, resources);
}
} // namespace endfield::areas
