#include "modules/hypergryph_account_login.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace endfield::modules::hypergryph {
namespace {
std::string lower(std::string_view s) {std::string out(s);for(auto& c:out) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');return out;}
bool schemeChar(char c,bool first) {return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(!first&&((c>='0'&&c<='9')||c=='+'||c=='-'||c=='.'));}
bool isHTTPS(const ParsedURL& u) {return lower(u.scheme)=="https"&&!u.user&&!u.password&&(!u.port||*u.port==443);}
std::string siteHost(Region r) {return r==Region::mainland?"www.skland.com":"www.skport.com";}
std::string origin(Region r) {return r==Region::mainland?"https://www.skland.com":"https://www.skport.com";}
std::string poster(std::string_view name,std::string_view payload,login_policy::ScriptHost host) {
    if(host==login_policy::ScriptHost::macOS) return "window.webkit.messageHandlers."+std::string(name)+".postMessage("+std::string(payload)+")";
    return "window.chrome.webview.postMessage({channel:'"+std::string(name)+"',body:"+std::string(payload)+"})";
}
std::string sessionReader() {
    using namespace login_policy;
    return "const valid = value => typeof value === 'string' && /^[\\x21-\\x7e]{1,4096}$/.test(value);\n"
        "const readSession = () => {\n"
        "  try {\n"
        "    const cred = window.localStorage.getItem('"+std::string(credentialKey)+"');\n"
        "    const signingToken = window.localStorage.getItem('"+std::string(signingTokenKey)+"');\n"
        "    const raw = window.localStorage.getItem('"+std::string(deviceIDKey)+"');\n"
        "    let deviceID = null;\n"
        "    if (typeof raw === 'string' && raw.length <= 16384) {\n"
        "      try { const value = JSON.parse(raw); if (value && !Array.isArray(value) && typeof value === 'object') deviceID = value.id; } catch (_) {}\n"
        "    }\n"
        "    return {cred, signingToken, deviceID};\n"
        "  } catch (_) { return {}; }\n"
        "};";
}
bool validNonce(std::string_view nonce) {
    return !nonce.empty()&&nonce.size()<=64&&std::all_of(nonce.begin(),nonce.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-';});
}
}

std::optional<ParsedURL> parseURL(std::string_view text) {
    if(text.empty()) return std::nullopt;
    for(const char c:text) {const auto b=static_cast<unsigned char>(c);if(b<=0x20||b==0x7f) return std::nullopt;}
    ParsedURL u;u.absolute=std::string(text);
    const auto colon=text.find(':');
    if(colon==std::string_view::npos||colon==0) return std::nullopt;
    for(std::size_t i=0;i<colon;++i) if(!schemeChar(text[i],i==0)) return std::nullopt;
    u.scheme=std::string(text.substr(0,colon));
    const auto rest=text.substr(colon+1);
    if(rest.substr(0,2)!="//") return u; // opaque (about:, data:, javascript:)
    const auto after=rest.substr(2);const auto end=std::min(after.find_first_of("/?#"),after.size());
    auto authority=after.substr(0,end);
    if(const auto at=authority.rfind('@');at!=std::string_view::npos) {
        const auto info=authority.substr(0,at);const auto sep=info.find(':');
        u.user=std::string(info.substr(0,sep));if(sep!=std::string_view::npos) u.password=std::string(info.substr(sep+1));
        authority=authority.substr(at+1);
    }
    std::string_view host,port;
    if(!authority.empty()&&authority.front()=='[') {
        const auto close=authority.find(']');if(close==std::string_view::npos) return std::nullopt;
        host=authority.substr(1,close-1);const auto tail=authority.substr(close+1);
        if(!tail.empty()) {if(tail.front()!=':') return std::nullopt;port=tail.substr(1);}
    } else {
        const auto sep=authority.find(':');host=authority.substr(0,sep);if(sep!=std::string_view::npos) port=authority.substr(sep+1);
    }
    if(!host.empty()) u.host=std::string(host);
    if(!port.empty()) {
        if(!std::all_of(port.begin(),port.end(),[](char c){return c>='0'&&c<='9';})||port.size()>5) return std::nullopt;
        const int value=std::stoi(std::string(port));if(value>65535) return std::nullopt;u.port=value;
    }
    return u;
}

namespace login_policy {
std::string siteURL(Region r) {return origin(r)+"/";}
bool acceptsCredentialOrigin(const std::optional<ParsedURL>& url,Region r) {
    return url&&isHTTPS(*url)&&url->host&&lower(*url->host)==siteHost(r);
}
bool permitsNavigation(const std::optional<ParsedURL>& url,Region r,bool mainFrame) {
    if(!url) return false;
    if(!mainFrame&&url->absolute=="about:blank") return true;
    if(!isHTTPS(*url)||!url->host) return false;
    const auto host=lower(*url->host);
    // Document navigations only; the official page fetches its own resources.
    static const std::set<std::string> mainland{"www.skland.com","web-api.skland.com","user.hypergryph.com","as.hypergryph.com","web-api.hypergryph.com","assets.skland.com"};
    static const std::set<std::string> global{"www.skport.com","web-api.skport.com","user.gryphline.com","as.gryphline.com","web-api.gryphline.com","web-api.gryphline.net","assets.skport.com"};
    // The official account SDK's Geetest challenge frames.
    static const std::set<std::string> challenge{"gcaptcha4.geetest.com","gcaptcha4.geevisit.com","gcaptcha4.gsensebot.com","static.geetest.com","static.geevisit.com","dn-staticdown.qbox.me"};
    if(!mainFrame&&challenge.count(host)) return true;
    return (r==Region::mainland?mainland:global).count(host)>0;
}
bool permitsPopup(const std::optional<ParsedURL>& url,const std::optional<ParsedURL>& source,Region r) {
    if(!permitsNavigation(source,r,true)) return false;
    return (url&&url->absolute=="about:blank")||permitsNavigation(url,r,true);
}
double pageZoom(double width) {
    if(!std::isfinite(width)) return 1;
    // The community desktop header has a fixed 1200 px layout: give it 1280 CSS px.
    return std::max(0.5,std::min(1.0,width/1280));
}
bool acceptsCredential(std::string_view value) {return validSecret(value);}
std::optional<std::string> decodeCredential(const Json& body,std::string_view nonce,Region r,bool mainFrame,const std::optional<ParsedURL>& frame,const std::optional<ParsedURL>& page) {
    if(nonce.empty()||!mainFrame||!acceptsCredentialOrigin(frame,r)||!acceptsCredentialOrigin(page,r)||!body.isObject()||body.object().size()!=2) return std::nullopt;
    if(!body["nonce"].isString()||body["nonce"].string()!=nonce||!body["cred"].isString()||!acceptsCredential(body["cred"].string())) return std::nullopt;
    return body["cred"].string();
}
std::optional<LoginResult> decodeSession(const Json& body,std::string_view nonce,Region r,bool mainFrame,const std::optional<ParsedURL>& frame,const std::optional<ParsedURL>& page) {
    if(nonce.empty()||!mainFrame||!acceptsCredentialOrigin(frame,r)||!acceptsCredentialOrigin(page,r)||!body.isObject()) return std::nullopt;
    const auto& o=body.object();
    if(o.size()!=4||!body.contains("nonce")||!body.contains("cred")||!body.contains("signingToken")||!body.contains("deviceID")) return std::nullopt;
    if(!body["nonce"].isString()||body["nonce"].string()!=nonce) return std::nullopt;
    for(const char* key:{"cred","signingToken","deviceID"}) if(!body[key].isString()||!acceptsCredential(body[key].string())) return std::nullopt;
    return LoginResult{r,body["cred"].string(),body["signingToken"].string(),body["deviceID"].string()};
}
std::string bridgeScript(Region r,std::string_view nonce,ScriptHost host) {
    const std::string n(nonce);
    return "(() => {\n"
        "  if (window.top !== window || location.origin !== '"+origin(r)+"') return;\n"
        "  "+sessionReader()+"\n"
        "  const keys = ['"+std::string(credentialKey)+"', '"+std::string(signingTokenKey)+"', '"+std::string(deviceIDKey)+"'];\n"
        "  let last = null, missingReported = false;\n"
        "  const read = () => {\n"
        "    const value = readSession();\n"
        "    if (!valid(value.cred)) return;\n"
        "    if (!valid(value.signingToken) || !valid(value.deviceID)) {\n"
        "      if (!missingReported) {\n"
        "        missingReported = true;\n"
        "        "+poster(stateMessageName,"{nonce:'"+n+"',event:'context-missing'}",host)+";\n"
        "      }\n"
        "      return;\n"
        "    }\n"
        "    const signature = JSON.stringify([value.cred, value.signingToken, value.deviceID]);\n"
        "    if (last === signature) return;\n"
        "    last = signature;\n"
        "    "+poster(messageName,"{nonce:'"+n+"',cred:value.cred,signingToken:value.signingToken,deviceID:value.deviceID}",host)+";\n"
        "  };\n"
        "  const original = Storage.prototype.setItem;\n"
        "  Storage.prototype.setItem = function(name, value) {\n"
        "    const result = Reflect.apply(original, this, arguments);\n"
        "    try { if (this === window.localStorage && keys.includes(name)) read(); } catch (_) {}\n"
        "    return result;\n"
        "  };\n"
        "  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', read, {once:true});\n"
        "  else read();\n"
        "  window.addEventListener('storage', event => {\n"
        "    if (keys.includes(event.key) && event.storageArea === window.localStorage) read();\n"
        "  });\n"
        "  window.addEventListener('pageshow', read);\n"
        "  window.addEventListener('focus', read);\n"
        "  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible') read(); });\n"
        "})();";
}
std::string credentialReadScript(Region r,std::string_view nonce,ScriptHost host) {
    const std::string n(nonce);
    return "(() => {\n"
        "  if (window.top !== window || location.origin !== '"+origin(r)+"') return;\n"
        "  "+sessionReader()+"\n"
        "  const value = readSession();\n"
        "  if (valid(value.cred) && valid(value.signingToken) && valid(value.deviceID))\n"
        "    "+poster(messageName,"{nonce:'"+n+"',cred:value.cred,signingToken:value.signingToken,deviceID:value.deviceID}",host)+";\n"
        "})();";
}
std::string directLoginScript(Region r,std::string_view nonce,ScriptHost host) {
    const std::string n(nonce);
    return "(() => {\n"
        "  if (window.top !== window || location.origin !== '"+origin(r)+"') return;\n"
        "  let ended = false, stage = 0, observer = null, deadline = null;\n"
        "  const stop = event => {\n"
        "    if (ended) return; ended = true;\n"
        "    if (observer) observer.disconnect();\n"
        "    if (deadline !== null) clearTimeout(deadline);\n"
        "    if (event) "+poster(stateMessageName,"{nonce:'"+n+"',event}",host)+";\n"
        "  };\n"
        "  const visible = element => !!element && element.getBoundingClientRect().width > 0 && element.getBoundingClientRect().height > 0;\n"
        "  const attempt = () => {\n"
        "    if (ended) return;\n"
        "    try { if (window.localStorage.getItem('"+std::string(credentialKey)+"')) { stop(null); return; } } catch (_) {}\n"
        "    if ("+std::string(r==Region::mainland?"true":"false")+") {\n"
        "      const button = document.querySelector('header .header-right .header-button');\n"
        "      if (visible(button) && button.textContent.trim() === '登录') {\n"
        "        stop('form-requested'); button.click();\n"
        "      }\n"
        "    } else if (stage === 0) {\n"
        "      const avatar = document.querySelector('[class*=\"Header__AvatarWrapper\"]');\n"
        "      if (visible(avatar)) { stage = 1; avatar.click(); Promise.resolve().then(attempt); }\n"
        "    } else {\n"
        "      const menu = document.querySelector('[class*=\"HAvatar__PopoverWrapper\"]');\n"
        "      const items = menu ? Array.from(menu.querySelectorAll('.hover-item')) : [];\n"
        "      const button = items[items.length - 1];\n"
        "      if (visible(button) && button.textContent.trim().length > 0) { stop('form-requested'); button.click(); }\n"
        "    }\n"
        "  };\n"
        "  observer = new MutationObserver(attempt);\n"
        "  observer.observe(document.documentElement, {childList:true,subtree:true});\n"
        "  deadline = setTimeout(() => stop('form-unavailable'), 20000);\n"
        "  window.addEventListener('pagehide', () => stop(null), {once:true});\n"
        "  attempt();\n"
        "})();";
}
std::string documentEndWrapper(std::string_view script) {
    return "(() => {\n"
        "  const run = () => {\n"+std::string(script)+"\n};\n"
        "  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', run, {once:true});\n"
        "  else run();\n"
        "})();";
}
}

std::string loginDiagnosticTag(LoginDiagnostic d,int code) {
    switch(d) {
        case LoginDiagnostic::consented: return "consented";case LoginDiagnostic::pageStarted: return "page-started";
        case LoginDiagnostic::pageLoaded: return "page-loaded";case LoginDiagnostic::pageRetried: return "page-retried";
        case LoginDiagnostic::pageTimeout: return "page-timeout";case LoginDiagnostic::pageFailed: return "page-error:"+std::to_string(code);
        case LoginDiagnostic::webProcessTerminated: return "web-process-terminated";case LoginDiagnostic::navigationBlocked: return "navigation-blocked";
        case LoginDiagnostic::popupOpened: return "popup-opened";case LoginDiagnostic::popupClosed: return "popup-closed";
        case LoginDiagnostic::credentialAccepted: return "credential-accepted";case LoginDiagnostic::credentialRejected: return "credential-rejected";
        case LoginDiagnostic::loginFormRequested: return "login-form-requested";case LoginDiagnostic::loginFormUnavailable: return "login-form-unavailable";
        case LoginDiagnostic::credentialContextMissing: return "credential-context-missing";case LoginDiagnostic::completed: return "completed";
        case LoginDiagnostic::cancelled: return "cancelled";case LoginDiagnostic::expired: return "expired";
    }
    return {};
}

LoginSession::Strings LoginSession::strings(Region r,core::Language l,AccountWording wording) {
    const auto t=[&](std::string_view e,std::string_view z){return accountText(e,z,l,wording);};
    return {r==Region::mainland?"EndfieldHUD · 森空岛":"EndfieldHUD · SKPORT",t("Connect through the official website","通过官方网站关联"),
        t(macConsentDetail,macConsentDetailChinese),
        login_policy::siteURL(r),t("Continue to official sign-in","继续前往官方登录"),t("Cancel","取消"),t("Retry","重试")};
}
LoginSession::LoginSession(Region r,std::string nonce):region_(r),nonce_(std::move(nonce)) {
    if(!validNonce(nonce_)) throw std::invalid_argument("Login sessions need a fresh random nonce");
}
Region LoginSession::region() const noexcept {return region_;}
const std::string& LoginSession::nonce() const noexcept {return nonce_;}
bool LoginSession::presenting() const noexcept {return presenting_;}
bool LoginSession::consented() const noexcept {return consented_;}
void LoginSession::diagnostic(LoginDiagnostic d,int code) {if(diagnostics_.size()<256) diagnostics_.push_back(loginDiagnosticTag(d,code));}
bool LoginSession::consent(double now) {
    if(!presenting_||consented_) return false;
    consented_=true;diagnostic(LoginDiagnostic::consented);status_="site";expiry_=now+sessionExpiry;return true;
}
void LoginSession::pageStarted(double now) {
    if(!presenting_||!consented_) return;
    diagnostic(LoginDiagnostic::pageStarted);status_="loading";loading_=true;loadDeadline_=now+loadTimeout;
}
void LoginSession::pageFinished(bool mainView,const std::string& host) {
    if(!presenting_||!consented_) return;
    diagnostic(LoginDiagnostic::pageLoaded);
    if(mainView) {loading_=false;loadDeadline_.reset();status_="host";host_=host.empty()?siteHost(region_):host;}
}
void LoginSession::pageFailed(int code,bool cancelled) {
    if(!presenting_||!consented_||cancelled) return;
    diagnostic(LoginDiagnostic::pageFailed,code);loading_=false;loadDeadline_.reset();status_="failed";
}
void LoginSession::processFailed() {
    if(!presenting_||!consented_) return;
    diagnostic(LoginDiagnostic::webProcessTerminated);loading_=false;loadDeadline_.reset();status_="failed";
}
bool LoginSession::retry(double now) {
    if(!presenting_||!consented_) return false;
    diagnostic(LoginDiagnostic::pageRetried);popups_=0;(void)now;return true;
}
bool LoginSession::allowNavigation(const std::string& uri,bool mainFrame,bool inPopup) {
    if(!presenting_||!consented_) return false;
    bool allowed;
    if(inPopup&&uri=="about:blank") allowed=true; // child already approved from a trusted official frame
    else allowed=login_policy::permitsNavigation(parseURL(uri),region_,mainFrame);
    if(!allowed) {diagnostic(LoginDiagnostic::navigationBlocked);status_="blocked";}
    return allowed;
}
bool LoginSession::allowPopup(const std::string& uri,const std::string& source) {
    if(!presenting_||!consented_||popups_>=maximumPopups||!login_policy::permitsPopup(parseURL(uri),parseURL(source),region_)) {
        diagnostic(LoginDiagnostic::navigationBlocked);status_="blocked";return false;
    }
    ++popups_;diagnostic(LoginDiagnostic::popupOpened);return true;
}
void LoginSession::popupClosed() {if(popups_>0) {--popups_;diagnostic(LoginDiagnostic::popupClosed);}}
int LoginSession::popupCount() const noexcept {return popups_;}
std::optional<LoginResult> LoginSession::credentialMessage(const Json& body,bool mainFrame,const std::string& frame,const std::string& page) {
    if(!presenting_||!consented_) return std::nullopt;
    auto session=login_policy::decodeSession(body,nonce_,region_,mainFrame,parseURL(frame),parseURL(page));
    if(!session) {diagnostic(LoginDiagnostic::credentialRejected);return std::nullopt;}
    diagnostic(LoginDiagnostic::credentialAccepted);return session;
}
void LoginSession::stateMessage(const Json& body,bool mainFrame,const std::string& frame,const std::string& page) {
    if(!presenting_||!consented_||!mainFrame||!login_policy::acceptsCredentialOrigin(parseURL(frame),region_)||
       !login_policy::acceptsCredentialOrigin(parseURL(page),region_)||!body.isObject()||body.object().size()!=2||
       !body["nonce"].isString()||body["nonce"].string()!=nonce_||!body["event"].isString()) return;
    const auto event=body["event"].string();
    if(event=="form-requested") diagnostic(LoginDiagnostic::loginFormRequested);
    else if(event=="form-unavailable") diagnostic(LoginDiagnostic::loginFormUnavailable);
    else if(event=="context-missing") diagnostic(LoginDiagnostic::credentialContextMissing);
}
std::optional<double> LoginSession::nextDeadline() const {
    if(!presenting_) return std::nullopt;
    std::optional<double> next=expiry_;
    if(loadDeadline_&&loading_) next=next?std::min(*next,*loadDeadline_):*loadDeadline_;
    return next;
}
std::optional<LoginFailure> LoginSession::deadline(double now) {
    if(!presenting_) return std::nullopt;
    if(expiry_&&now>=*expiry_) return LoginFailure::expired;
    if(loadDeadline_&&now>=*loadDeadline_) {
        loadDeadline_.reset();
        if(loading_) {diagnostic(LoginDiagnostic::pageTimeout);loading_=false;status_="failed";}
    }
    return std::nullopt;
}
void LoginSession::finish(Outcome outcome) {
    if(!presenting_) return;
    diagnostic(outcome==Outcome::completed?LoginDiagnostic::completed:outcome==Outcome::expired?LoginDiagnostic::expired:LoginDiagnostic::cancelled);
    presenting_=false;consented_=false;nonce_.clear();loadDeadline_.reset();expiry_.reset();popups_=0;
}
const std::string& LoginSession::status() const noexcept {return status_;}
std::string LoginSession::statusText(core::Language l) const {
    const auto t=[&](std::string_view e,std::string_view z){return core::localized(e,z,l);};
    if(status_=="site") return login_policy::siteURL(region_);
    if(status_=="loading") return t("Loading…","正在载入…");
    if(status_=="host") return host_;
    if(status_=="failed") return t("The sign-in page could not load. Retry to continue.","登录页面未能载入，请重试。");
    if(status_=="blocked") return t("This sign-in route is not supported here. Use the official page’s email or phone sign-in, or cancel.","此登录方式暂不支持，请使用官方页面的邮箱或手机号登录，或取消。");
    return {};
}
const std::vector<std::string>& LoginSession::diagnostics() const noexcept {return diagnostics_;}
}
