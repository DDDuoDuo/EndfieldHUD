#pragma once
#include "modules/hypergryph_account_login.hpp"
#if defined(_WIN32)&&defined(EHUD_HAS_WEBVIEW2)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>

// Official-site login window over Microsoft Edge WebView2 (Evergreen runtime),
// port of HypergryphAccountLogin. Created only after explicit Connect; the
// consent page is native, then a private, temporary WebView2 profile opens
// the official site directly. Navigation/popup/credential-origin policy, the
// nonce-bound bridge and every lifecycle decision come from LoginSession. On
// success/cancel/expiry the controller, environment and its temporary user
// data folder are released (the folder is removed once the browser exits).
// Requires the WebView2 SDK headers/static loader (EHUD_WEBVIEW2_SDK_DIR);
// without them the app uses UnavailableLoginPresenter instead.
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
};
class WebViewLoginPresenter final:public modules::hypergryph::AccountLoginPresenter {
public:
    explicit WebViewLoginPresenter(WebViewLoginOptions);
    ~WebViewLoginPresenter() override;
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
