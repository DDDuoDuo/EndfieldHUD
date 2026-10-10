#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "app/utility_executor.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "tools/account_preview.hpp"
#include "tools/account_sanity_gauge_preview.hpp"
#include <filesystem>
#include <memory>

// Production owner bundle for Account Linking: the app-wide account services
// (WinHTTP transport, signed community client, DPAPI vault, owner-only
// Account\profile-cache.json, shared-utility-queue adapter, avatar loader and
// the one HypergryphAccountController), the official login presenter
// (WebView2 when the build has the SDK, otherwise an explicit "unavailable"
// result), the Account module face and the header sanity / Work Mode wallet.
// Method names follow app::ModuleOwner so the Application wraps it with a
// trivial adapter. Everything runs on the UI thread; it creates no timer,
// thread or polling loop: WinHTTP completions wake the owner with one private
// message (serviceMessage) whose handler drains them, vault/cache work runs on
// the borrowed UtilityExecutor, and refresh/recovery/request deadlines are
// folded into the host's single nextWakeTime()/deadline() scheduler.
namespace endfield::tools {
struct AccountLinkingOwnerOptions {
    void* window{};                                   // HUD HWND (borrowed): private wake + login owner
    std::uint32_t serviceMessage{0x8000+243};         // WM_APP+243 (owner-private, WM_APP...0xBFFF)
    std::filesystem::path dataRoot;                   // explicit absolute app root; Account\ below it
    std::filesystem::path gaugeResources;             // staged windows/resources/account (pinned)
    native::LayerRasterOptions raster;
    std::function<double()> clock;                    // shared monotonic host clock, seconds
    std::function<void()> invalidate;                 // request one coalesced frame
    std::function<void(std::string_view)> recordEvent;// EventKind::accountAction {"action": linked/unlinked/synced/settings}
    // ProfileStoreAccountSink over the shared ProfileStore (owned by the
    // Personal Profile owner, which also supplies the managed avatar import).
    modules::hypergryph::AccountProfileSink* profile{};
    std::optional<std::string> initialAvatarFilename;
    // Work Mode snapshot for the wallet when unlinked / in Work Mode header mode.
    std::function<modules::hypergryph::WorkModeGaugeInput()> workMode;
    std::function<bool()> workModeRunning;
    core::Language language{core::Language::english};
    modules::hypergryph::CanvasStyle style;
    bool reduceMotion{};
    // false: Connect reports "Sign-in could not finish" and no browser is
    // ever created (hidden verification / builds without WebView2).
    bool officialLogin{true};
    // Wall clock in Foundation reference seconds (Mac Date()); tests pin it.
    std::function<modules::hypergryph::Time()> wallClock;
};
class AccountLinkingOwner final {
public:
    AccountLinkingOwner(app::UtilityExecutor&,native::LayerRasterizer&,AccountLinkingOwnerOptions);
    ~AccountLinkingOwner();
    AccountLinkingOwner(const AccountLinkingOwner&)=delete;
    AccountLinkingOwner& operator=(const AccountLinkingOwner&)=delete;

    void resize(const app::ClientMetrics&);             // zero-size (hidden) metrics are ignored
    void setAppearance(core::Language,const modules::hypergryph::CanvasStyle&,bool reduceMotion,double time);
    void setOverlayVisible(bool,double time);
    void overlayClosing(double time);
    // The header wallet uses the same projected centre plane; headerOpacity is
    // the shell chrome opacity (the module face uses `opacity`).
    void update(const core::Matrix4& center,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,float headerOpacity,double time);
    bool requiresFrames(double time) const;
    std::optional<double> nextWakeTime(double now) const;
    bool deadline(double time);                         // true: visible content changed
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> moduleEntries();  // Account centre content
    std::span<const native::LayerCompositionEntry> headerEntries();  // wallet + recovery popover (above modules)
    void collected(native::Renderer&);
    void release(native::Renderer&);

    bool covers(core::Point) const;
    bool capturesPointer() const;                       // open account menu or recovery popover
    bool capturesKeys() const;
    bool pointer(const app::PointerEvent&,double time);
    bool wheel(const app::WheelEvent&,double time);
    bool key(const app::KeyEvent&,bool modified,double time);
    bool message(const app::NativeMessage&,double time); // serviceMessage: drain completions
    void cancelInteraction(double time);
    bool flush(double time);                            // shutdown barrier: the latest Account cache bytes are written

    // Personal Profile observer: an avatar change outside game sync turns
    // avatar sync off (Mac profile observer); Work Mode timer state changes.
    void profileAvatarChanged(const std::optional<std::string>& filename);
    void workModeChanged(double time);

    modules::hypergryph::HypergryphAccountController& controller() noexcept;
    const AccountPreview& module() const noexcept;
    const AccountSanityGaugePreview& gauge() const noexcept;
    std::size_t activeRequests() const;                 // in-flight WinHTTP requests (diagnostics/tests)
    bool officialLoginAvailable() const noexcept;       // WebView2 build and runtime present
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
}
#endif
