#pragma once
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include "app/utility_executor.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "modules/account_canvas.hpp"
#include "native/hud_design_automation.hpp"
#include "native/hypergryph_account_services.hpp"
#include "tools/account_sanity_gauge_preview.hpp"
#include "tools/hud_design_owner.hpp"
#include <filesystem>
#include <memory>

// Production owner bundle for Account Linking: the app-wide account services
// (WinHTTP transport, signed community client, DPAPI vault, owner-only
// Account\profile-cache.json, shared-utility-queue adapter, avatar loader and
// the one HypergryphAccountController), the official login presenter
// (WebView2 when the build has the SDK, otherwise an explicit "unavailable"
// result), the Account page on the design library (modules::AccountCanvas in
// a DesignOwnerCore on the HUD's one design Context: the Mac 1.3.0
// HUDAccountCanvas) and the header sanity / Work Mode wallet. Method names
// follow app::ModuleOwner so the area wraps it with a thin ModuleOwner.
// Everything runs on the UI thread; it creates no timer, thread or polling
// loop: WinHTTP completions wake the owner with one private message
// (serviceMessage) whose handler drains them, vault/cache work runs on the
// borrowed UtilityExecutor, and refresh/recovery/request deadlines and the
// page's 1 s recovery readout are folded into the host's single
// nextWakeTime()/deadline() scheduler.
namespace endfield::tools {
struct AccountLinkingOwnerOptions {
    void* window{};                                   // HUD HWND (borrowed): private wake + login owner
    std::uint32_t serviceMessage{0x8000+243};         // WM_APP+243 (owner-private, WM_APP...0xBFFF)
    std::filesystem::path dataRoot;                   // explicit absolute app root; Account\ below it
    std::filesystem::path gaugeResources;             // staged windows/resources/account (pinned)
    native::LayerRasterOptions raster;                // the header wallet
    // The HUD's one design Context (ApplicationHost::designContext; tests: a
    // DesignOverlayOwner's) and the page owner's options (name "account",
    // routing, raster, clock, frame demand, text-field services: the host's
    // ApplicationHost::designOwnerOptions). Required.
    modules::design::Context* design{};
    DesignOwnerOptions page;
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
    // The wallet without sanity data is a link to Work Mode (Mac 1.3.0).
    std::function<void()> openWorkMode;
    core::Language language{core::Language::english};
    // The configured HUD accent (sRGB): the login window's auth buttons
    // (HUDRuntimeAppearance.accent).
    std::array<double,3> accent{250./255,212./255,31./255};
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
// UI Automation spaces of accessibleNodes(): the page's controls in
// content-box px, the wallet's in logical client points.
inline constexpr std::uint8_t accountPageSpace=0,accountWalletSpace=1;
class AccountLinkingOwner final {
public:
    AccountLinkingOwner(app::UtilityExecutor&,native::LayerRasterizer&,AccountLinkingOwnerOptions);
    ~AccountLinkingOwner();
    AccountLinkingOwner(const AccountLinkingOwner&)=delete;
    AccountLinkingOwner& operator=(const AccountLinkingOwner&)=delete;

    void resize(const app::ClientMetrics&);             // zero-size (hidden) metrics are ignored
    // The HUD's appearance (the Context already took it: the overlay owner is
    // registered first): the controller's and the login's language and accent.
    void setAppearance(const app::ModuleAppearance&,double time);
    void setOverlayVisible(bool,double time);
    void overlayClosing(double time);
    // The header wallet uses the same projected centre plane; headerOpacity is
    // the shell chrome opacity (the page uses the frame's).
    void update(const app::ModuleFrame&,float headerOpacity);
    // Launch prewarm (app::ModuleOwner::prewarm), while the HUD is still
    // closed: poses the hidden header wallet at opacity 0 (so the host's
    // following upload installs its artwork, numerals and group targets) and
    // builds and rasterizes the page when the first opening shows Account.
    // Nothing becomes visible, no request starts and no provider opens.
    void prewarm(const app::ModuleFrame&);
    // The HUD finished closing (HostEvent::overlayConcealed): a page that is
    // not shown keeps no artwork and a shut recovery popover releases its
    // card (rebuilt when next shown); the wallet stays.
    void concealed(double time);
    bool requiresFrames(double time) const;
    std::optional<double> nextWakeTime(double now) const;
    bool deadline(double time);                         // true: visible content changed
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> moduleEntries();  // Account page
    std::span<const native::LayerCompositionEntry> headerEntries();  // wallet + recovery popover (above modules)
    void collected(native::Renderer&);
    void release(native::Renderer&);

    bool covers(core::Point) const;
    bool pointerLocked() const;
    bool capturesPointer() const;                       // open recovery popover, open page menu, a tracked press
    bool capturesWheel() const;
    bool capturesKeys() const;
    bool pointer(const app::PointerEvent&,double time);
    bool wheel(const app::WheelEvent&,double time);
    // Keys: an open recovery popover first (unmodified keys only), then the
    // page (modifier bits 1 Alt, 2 Ctrl, 4 Shift, 8 Win: DesignOwnerCore::key).
    bool key(const app::KeyEvent&,std::uint32_t modifiers,double time);
    bool message(const app::NativeMessage&,double time); // serviceMessage: drain completions
    void cancelInteraction(double time);
    std::optional<app::ModuleCursor> cursorAt(core::Point) const;
    // SystemHUDView.presentQuitConfirmation: an open recovery popover closes
    // instead of the quit being asked; true when one was open.
    bool dismissWalletPopover(double time);
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
    // UI Automation (the Mac's HUDDesignCanvas.accessibilityItems of the
    // page while it takes input, then the wallet's stand-ins
    // "wallet:<action id>" with the open popover's countdowns as the toggle's
    // value): one node list for the area's AutomationTree; rects in
    // accountPageSpace (box px) or accountWalletSpace (logical client).
    std::vector<native::AutomationNode> accessibleNodes() const;
    std::optional<core::Rect> accessibleClientRect(core::Rect,std::uint8_t space) const; // logical client
    bool performAccessible(std::string_view id,double time);
    bool setAccessibleValue(std::string_view id,double value,double time);
    bool expandAccessible(std::string_view id,bool expand,double time);

    modules::hypergryph::HypergryphAccountController& controller() noexcept;
    modules::AccountCanvas& canvas() noexcept;
    DesignOwnerCore& page() noexcept;
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
