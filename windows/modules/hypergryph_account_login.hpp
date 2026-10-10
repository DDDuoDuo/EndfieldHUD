#pragma once
#include "modules/hypergryph_account_controller.hpp"

// Port of HypergryphAccountLogin.swift's policy and lifecycle, independent of
// the browser. The WebView2 adapter (native/hypergryph_account_webview.*) feeds
// browser events in and applies the returned decisions; the official website
// owns every authentication step. Nothing here fills a form, solves a
// challenge, reads cookies/passport tokens or keeps a browser profile.
namespace endfield::modules::hypergryph {
struct ParsedURL {
    std::string absolute,scheme;
    std::optional<std::string> user,password,host;
    std::optional<int> port;
};
std::optional<ParsedURL> parseURL(std::string_view); // Foundation URL(string:) subset used by the policy

namespace login_policy {
inline constexpr std::string_view credentialKey="SK_OAUTH_CRED_KEY";
inline constexpr std::string_view signingTokenKey="SK_TOKEN_CACHE_KEY";
inline constexpr std::string_view deviceIDKey="SK_SHUMEI_DEVICE_ID_KEY";
inline constexpr std::string_view messageName="endfieldCommunityCredential";
inline constexpr std::string_view stateMessageName="endfieldCommunityState";
std::string siteURL(Region);
bool acceptsCredentialOrigin(const std::optional<ParsedURL>&,Region);
bool permitsNavigation(const std::optional<ParsedURL>&,Region,bool mainFrame);
bool permitsPopup(const std::optional<ParsedURL>& url,const std::optional<ParsedURL>& source,Region);
double pageZoom(double width);
bool acceptsCredential(std::string_view);
std::optional<std::string> decodeCredential(const Json& body,std::string_view nonce,Region,bool mainFrame,
                                            const std::optional<ParsedURL>& frameOrigin,const std::optional<ParsedURL>& pageURL);
std::optional<LoginResult> decodeSession(const Json& body,std::string_view nonce,Region,bool mainFrame,
                                         const std::optional<ParsedURL>& frameOrigin,const std::optional<ParsedURL>& pageURL);
// Script flavours: macOS (WKScriptMessageHandler) reproduces the Mac text
// byte-for-byte; webView2 posts {channel,body} through chrome.webview.
enum class ScriptHost {macOS,webView2};
std::string bridgeScript(Region,std::string_view nonce,ScriptHost=ScriptHost::webView2);
std::string credentialReadScript(Region,std::string_view nonce,ScriptHost=ScriptHost::webView2);
std::string directLoginScript(Region,std::string_view nonce,ScriptHost=ScriptHost::webView2);
// WebView2 has no document-end injection: run the Mac atDocumentEnd script
// once the DOM is parsed, from a document-created script.
std::string documentEndWrapper(std::string_view script);
}

enum class LoginDiagnostic {consented,pageStarted,pageLoaded,pageRetried,pageTimeout,pageFailed,webProcessTerminated,navigationBlocked,
    popupOpened,popupClosed,credentialAccepted,credentialRejected,loginFormRequested,loginFormUnavailable,credentialContextMissing,completed,cancelled,expired};
std::string loginDiagnosticTag(LoginDiagnostic,int code=0);

// Browser-independent session: nonce, consent, page/popup policy, message
// validation, retry, 25 s load deadline and 900 s expiry (as deadlines, no timer).
class LoginSession final {
public:
    struct Strings {std::string title,consentTitle,consentDetail,site,continueLabel,cancelLabel,retryLabel;};
    static Strings strings(Region,core::Language,AccountWording=AccountWording::windows);
    static constexpr double loadTimeout=25,sessionExpiry=900;
    static constexpr int maximumPopups=2;
    LoginSession(Region,std::string nonce);
    Region region() const noexcept;
    const std::string& nonce() const noexcept;
    bool presenting() const noexcept;
    bool consented() const noexcept;
    // Explicit consent: the caller then creates the private browser, adds the
    // bridge (document-created) and direct-login (document-end) scripts and
    // navigates to siteURL.
    bool consent(double now);
    void pageStarted(double now);            // main page provisional navigation
    void pageFinished(bool mainView,const std::string& host);
    void pageFailed(int code,bool cancelled);
    void processFailed();
    bool retry(double now);                  // caller closes popups and reloads siteURL
    bool allowNavigation(const std::string& uri,bool mainFrame,bool inPopup);
    bool allowPopup(const std::string& uri,const std::string& sourceURI);
    void popupClosed();
    int popupCount() const noexcept;
    // Script messages. Returns the session result when a valid credential
    // message arrives (the caller then finishes with success).
    std::optional<LoginResult> credentialMessage(const Json& body,bool mainFrame,const std::string& frameURI,const std::string& pageURI);
    void stateMessage(const Json& body,bool mainFrame,const std::string& frameURI,const std::string& pageURI);
    // Deadlines: load timeout shows the failure text; expiry finishes the session.
    std::optional<double> nextDeadline() const;
    std::optional<LoginFailure> deadline(double now);
    // Success/cancel/expiry teardown: records the Mac outcome tag (completed,
    // cancelled or expired) once, clears the nonce and accepts nothing more.
    enum class Outcome {completed,cancelled,expired};
    void finish(Outcome=Outcome::cancelled);
    const std::string& status() const noexcept; // localized status line text key (English); see statusText
    std::string statusText(core::Language) const;
    const std::vector<std::string>& diagnostics() const noexcept;
private:
    Region region_;std::string nonce_;bool presenting_{true},consented_{},loading_{};
    int popups_{};std::optional<double> loadDeadline_,expiry_;std::string status_,host_;
    std::vector<std::string> diagnostics_;
    void diagnostic(LoginDiagnostic,int code=0);
};
}
