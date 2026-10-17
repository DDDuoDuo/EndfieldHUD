#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "app/utility_executor.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "native/account_automation.hpp"
#include "native/hypergryph_account_services.hpp"
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
    // The login window's link to Microsoft's WebView2 runtime download page
    // (WebViewLoginOptions::openLink). The host closes the HUD (and with it
    // the login) first and opens the page once it is hidden, like every
    // hand-off to another application (ApplicationHost::openLinkAfterClose);
    // empty: the login opens it itself.
    std::function<bool(std::string_view url)> openLink;
    // Wall clock in Foundation reference seconds (Mac Date()); tests pin it.
    std::function<modules::hypergryph::Time()> wallClock;
    // "Updated MM/dd HH:mm:ss" fields (default: the user's local time zone).
    std::function<modules::hypergryph::LocalDateFields(modules::hypergryph::Time)> localDate;
    // Thread-safe owner wake for WinHTTP completions; default posts
    // serviceMessage to `window`. Hidden coverage passes a no-op and routes
    // serviceMessage through the owners' message chain itself.
    std::function<void()> wake;
    // Hidden coverage only: synthetic transport/vault/avatar providers and a
    // synthetic login host (borrowed, outliving the owner). No WinHTTP
    // session, DPAPI blob, browser or network endpoint is then created.
    std::optional<native::HypergryphAccountServicesOptions::Synthetic> synthetic;
    modules::hypergryph::AccountLoginPresenter* login{};
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
    // Launch prewarm (app::ModuleOwner::prewarm), while the HUD is still
    // closed: poses the hidden header wallet (and the face, when the first
    // opening shows Account) at opacity 0, so the host's following upload
    // installs their artwork, numerals, popover and group targets and the
    // first opening frame only re-poses them. Nothing becomes visible, no
    // request starts and no provider opens.
    void prewarm(const core::Matrix4& center,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,double time);
    // The HUD finished closing (HostEvent::overlayConcealed): an Account face
    // that is not shown and a shut recovery popover release their GPU
    // targets and rasters (rebuilt when next shown); the wallet stays.
    void concealed(double time);
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
    // The profile's loaded avatar (its first committed record): the known
    // baseline, never mistaken for a manual edit.
    void profileAvatarBaseline(const std::optional<std::string>& filename);
    void workModeChanged(double time);
    // Local wall-clock presentation changed (time zone / clock change).
    void localTimeChanged(double time);
    // Accessibility peers (Mac transparent AX buttons): the active Account
    // face ("account:<control id>"), then the header wallet and its open
    // recovery popover ("wallet:<action id>"), with projected logical client
    // rects; a press performs exactly the click's action. Windows additions:
    // an open menu's rows are focusable and its keyboard selection focused,
    // the sync toggles carry their state, and Connect carries the face's
    // status texts (help) and status message (announced when it changes).
    std::vector<native::AccountAutomationControl> accessibleControls() const;
    bool performAccessible(std::string_view id,double time);

    modules::hypergryph::HypergryphAccountController& controller() noexcept;
    const AccountPreview& module() const noexcept;
    const AccountSanityGaugePreview& gauge() const noexcept;
    std::size_t activeRequests() const;                 // in-flight WinHTTP requests (diagnostics/tests)
    // The WinHTTP session (none in hidden coverage): opened on the shared
    // queue by the first request, closed once the HUD is hidden and idle.
    std::optional<native::WinHttpAccountTransport::SessionStats> networkSession() const;
    bool officialLoginAvailable() const noexcept;       // WebView2 login built in (the runtime is checked at each Connect)
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
}
#endif
