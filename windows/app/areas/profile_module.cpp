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
#include "native/profile_pickers.hpp"
#include "tools/profile_owner.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
// the current record as an account write, the lock mirrors gameSyncActive and
// the game avatar becomes the managed avatar synchronously.
class ProfileAccountSink final : public modules::hypergryph::AccountProfileSink {
public:
    ProfileAccountSink(tools::ProfileOwner& owner, app::ApplicationHost& host) : owner_(owner), host_(host) {}
    void applyGameProfile(const modules::hypergryph::GameProfileUpdate& update) override {
        owner_.acceptFromGame([update](m::PersonalProfile& p) { modules::hypergryph::applyGameProfileUpdate(p, update); }, host_.now());
    }
    void setProfileSyncLocked(bool locked) override { owner_.setSyncLocked(locked, host_.now()); }
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
        if (hiddenCoverage()) seedCoverageProfile(root);
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
        auto* manager = static_cast<ITfThreadMgr*>(c.textManager);
        owner_ = std::make_unique<tools::ProfileOwner>(hwnd_, *c.utility, c.rasterizer, images_, *work_, std::move(o), manager,
                                                       manager ? static_cast<TfClientId>(c.textClient) : TF_CLIENTID_NULL);
        // Work Mode lifetime: every absolute checkpoint of the shared controller.
        listener_ = work_->listenCheckpoints([this](double total) { if (owner_) owner_->workModeCheckpoint(total); });
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
    void setOverlayVisible(bool visible, double t) override { owner_->setOverlayVisible(visible, t); }
    void overlayClosing(double t) override { owner_->overlayClosing(t); }
    void update(const app::ModuleFrame& f) override { owner_->update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
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
        case K::timeChanged:
        case K::systemSettingsChanged: owner_->setTimeZone(zone_, t); return true;
        default: return false;
        }
    }
    void coverage(app::ModuleCoverage& step) override;

private:
    app::ApplicationHost& host_;
    app::ApplicationAreaMode mode_;
    HWND hwnd_{};
    fs::path coverageRoot_;
    std::function<fs::path(std::string_view)> fixture_;
    m::WorkModeController idleWork_;           // only when the host runs no Work Mode owner
    const m::WorkModeController* work_{};
    std::uint64_t listener_{};
    app::ModuleAppearance appearance_;
    std::u16string zone_;
    UINT pickerMessage_{};
    gpu::ProfilePickerSession colorSession_;
    bool fileChoiceOpen_{};
    gpu::IdCardFeed feed_;
#ifdef EHUD_PROFILE_ACCOUNT_LINK
    app::areas::AccountProfileLink* account_{};
#endif
    // Hidden coverage choosers.
    tools::ProfileOwnerOptions::ImageCompletion coverageChooser_;
    std::optional<std::array<double, 3>> coverageColor_;
    unsigned coverageImageAsks_{}, coverageColorAsks_{}, cropEvents_{};
    std::string lastCrop_;
    // Declaration order: the owner borrows the image source and is destroyed first.
    gpu::LayerImageSource images_;
    std::unique_ptr<tools::ProfileOwner> owner_;
    std::unique_ptr<ProfileAccountSink> sink_; // borrows the owner; detached and destroyed before it

    static tools::ProfileOwnerAppearance profileAppearance(const app::ModuleAppearance& a) {
        return {resolved(a.language), a.dark, a.accentSRGB, 2, a.reduceMotion};
    }
    bool hiddenCoverage() const noexcept { return mode_ == app::ApplicationAreaMode::hiddenCoverage; }
    // HUDPersonalProfileInteraction.chooseImage: NSOpenPanel(image types) as
    // the host's shared file dialog; the first chosen file is imported (the
    // import picks the codec from the content and reports non-images).
    bool chooseImage(m::ProfileImageKind kind, tools::ProfileOwnerOptions::ImageCompletion done) {
        if (hiddenCoverage()) {
            if (coverageChooser_) return false;
            ++coverageImageAsks_; coverageChooser_ = std::move(done);
            return true;
        }
        if (fileChoiceOpen_) return false;
        const auto language = resolved(appearance_.language);
        const bool avatar = kind == m::ProfileImageKind::avatar;
        app::ApplicationFileRequest request;
        request.title = avatar ? core::localized("Choose profile picture", "选择头像", language) : core::localized("Choose profile background", "选择名片背景", language);
        request.acceptLabel = core::localized("Choose", "选择", language);
        request.selectionLabel = request.acceptLabel;
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
        auto done = std::exchange(coverageChooser_, {});
        done(photo);
        drainUntil(step, [&] { return owner_->state()->profile().avatarFilename.has_value() && owner_->cardTexture(tools::IdCardTextureKind::avatar); });
        const auto avatar = owner_->state()->profile().avatarFilename;
        step.check(avatar && avatar->ends_with(".image") && fs::exists(coverageRoot_ / "Profile" / "Images" / *avatar) && !owner_->pickerOpen(),
                   "The chosen picture becomes the managed avatar in the profile root");
        const auto* texture = feed_.texture(gpu::IdCardTextureSlot::avatar);
        step.check(texture && texture->width == 368 && texture->height == 368 && texture->revision == owner_->card()->avatarRevision(),
                   "The ID card avatar is the 136 pt crop the presenter binds to desktop.profile.avatar");
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
        // Crop sliders: a committed thumbnail zoom records profileCropChanged
        // {thumbnail} (SystemEventRecorder) and regenerates the card background.
        const auto backgroundBefore = owner_->card()->backgroundRevision();
        step.check(owner_->perform("profile:backgroundMenu", t) && owner_->setSlider(m::ProfileField::thumbnailZoom, 2, t), "The thumbnail zoom slider commits");
        step.check(cropEvents_ == 1 && lastCrop_ == "thumbnail" && owner_->card()->backgroundRevision() > backgroundBefore, "The crop is recorded once and moves the card background");
        drainUntil(step, [&] { const auto* b = feed_.texture(gpu::IdCardTextureSlot::background); return b && b->revision == owner_->card()->backgroundRevision(); });
        step.check(feed_.texture(gpu::IdCardTextureSlot::background) && feed_.texture(gpu::IdCardTextureSlot::background)->revision == owner_->card()->backgroundRevision(),
                   "The regenerated card background is published");
        step.check(owner_->perform("profile:popoverClose", t), "The card theme popover closes");
        // 3. Official sync: the card shows the synced UID, never a # tag.
        sink_->setProfileSyncLocked(true);
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
    for (unsigned n = 0; n < 30; ++n) step.present(t += 1. / 60);
    // Settled frames (tilt, ambient motion, Work Mode ticks) do no card work.
    const auto feed = feed_.revision();
    const auto textures = std::array{owner_->card()->avatarRevision(), owner_->card()->backgroundRevision(), owner_->card()->hoverRevision()};
    for (unsigned n = 0; n < 12; ++n) step.present(t += 1. / 60);
    step.check(feed_.revision() == feed && textures == std::array{owner_->card()->avatarRevision(), owner_->card()->backgroundRevision(), owner_->card()->hoverRevision()},
               "Settled page frames publish no card content and regenerate no card texture");
}
// Module-less step: the published card drives the presenter the host owns,
// here over the exported caption leaves (coverage fixture) and a private
// rasterizer, with the SourceScene replaced by a recording target.
void ProfileArea::coverageCard(app::ModuleCoverage& step) {
    step.check(host_.find<gpu::IdCardFeed>(gpu::IdCardFeed::serviceKey) == &feed_, "The ID card feed is published by exact type");
    drainUntil(step, [&] { return owner_->loaded() && owner_->card(); });
    step.check(owner_->card() && feed_.card() == owner_->card(), "The loaded card is published");
    if (step.cycle != 0) return;
    // Quit/session-end flush: the live Work Mode total lands once above the
    // stored lifetime and profile.json is drained; a flush never blocks quitting.
    const double now = step.time;
    step.check(flush(now), "The profile flush never blocks quitting");
    const auto lifetime = 3600 + work_->trackedWorkSeconds(now);
    step.check(std::abs(ehud::data::ProfileStore(coverageRoot_).value().accumulatedWorkSeconds - lifetime) < 1e-6 && owner_->serviceStatus() && !owner_->serviceStatus()->dirty,
               "The flushed lifetime is the stored hours plus the live Work Mode total");
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
