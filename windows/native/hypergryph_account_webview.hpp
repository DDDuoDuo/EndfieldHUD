#pragma once
#include "modules/hypergryph_account_login.hpp"
#if defined(_WIN32)&&defined(EHUD_HAS_WEBVIEW2)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// Official-site login window over Microsoft Edge WebView2 (Evergreen runtime),
// port of HypergryphAccountLogin. Created only after explicit Connect; the
// consent page is native, then a private, temporary WebView2 profile opens
// the official site directly. Navigation/popup/credential-origin policy, the
// nonce-bound bridge and every lifecycle decision come from LoginSession. On
// success/cancel/expiry the controller, environment and its temporary user
// data folder are released (the folder is removed once the browser exits).
// Requires the WebView2 SDK headers/static loader (EHUD_WEBVIEW2_SDK_DIR);
// without them the app uses UnavailableLoginPresenter instead.
//
// The Evergreen runtime is not bundled. When Connect finds none installed,
// the same native window shows a localized explanation page instead of the
// consent page (webView2RuntimeStrings): Microsoft's official download page
// (webView2DownloadURL) opens in the user's browser on request, Check Again
// asks the loader again and continues to the consent page once a runtime is
// installed (no restart), and closing the page reports runtimeUnavailable.
// The HUD hiding closes the page like any login (the controller cancels it).
// No profile, browser process or download is started by EndfieldHUD itself.
//
// Keyboard: the window is no dialog, so its pages do what the dialog manager
// would. Tab / Shift+Tab walk the page's controls in visual order (after
// consent: Retry, Cancel, then the official page, whose MoveFocusRequested
// returns to the toolbar), the focused push button draws as the default one,
// Return presses the focused button (else the page's default: Continue, the
// download page), Escape is Cancel from any control (Mac key equivalent), and
// reactivation restores the focused control or the page. Per-monitor DPI:
// WM_DPICHANGED takes the suggested frame and re-creates the page fonts at
// the same point sizes.
namespace endfield::native {
struct WebViewLoginOptions {
    HWND owner{};                              // HUD window: the login stays above it
    core::Language language{core::Language::english};
    std::function<double()> now;               // host clock (seconds)
    std::function<void()> scheduleChanged;     // host re-reads nextDeadline()
    std::function<void(std::string_view)> diagnostic; // explicit diagnostics only (fixed tags)
    // Parent of the temporary private browser profiles (empty: %TEMP%). Only
    // folders named EndfieldHUD-Login-* below it are created or swept.
    std::filesystem::path profileRoot;
    bool showWindows{true};                    // false only for hidden verification
    // Opens Microsoft's official WebView2 download page in the user's default
    // browser (default: ShellExecuteW "open"); tests record the request.
    std::function<bool(std::string_view url)> openLink;
};
class WebViewLoginPresenter final:public modules::hypergryph::AccountLoginPresenter {
public:
    explicit WebViewLoginPresenter(WebViewLoginOptions);
    ~WebViewLoginPresenter() override;
    // The installed WebView2 runtime the loader would use (official loader
    // query; no browser process is started). Checked at every Connect, so a
    // runtime installed while EndfieldHUD runs is used without a restart.
    static std::optional<std::wstring> runtimeVersion();
    // Without a runtime the window shows the explanation page (no profile or
    // browser); however it closes (including cancel()), the completion
    // reports runtimeUnavailable.
    void present(modules::hypergryph::Region,std::function<void(std::optional<modules::hypergryph::LoginResult>,modules::hypergryph::LoginFailure)>) override;
    void cancel() override;
    bool isPresenting() const override;
    void setLanguage(core::Language); // next login window / status text
    std::optional<double> nextDeadline() const;  // 25 s page load / 900 s session expiry
    void deadline(double now);
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
}
#endif
