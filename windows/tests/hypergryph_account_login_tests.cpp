// Official-site login policy and session lifecycle against the unchanged Mac
// HypergryphAccountLoginPolicy (synthetic URLs, nonces and message bodies). No
// browser, network, login page or credential is used.
#include "modules/hypergryph_account_login.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace h=endfield::modules::hypergryph;
namespace p=endfield::modules::hypergryph::login_policy;
using ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool value,const std::string& why) {++checks;if(!value) throw std::runtime_error(why);}
std::optional<h::ParsedURL> url(const Json& v) {if(v.isNull()) return std::nullopt;return h::parseURL(v.string());}
h::Region region(const Json& v) {return *h::regionNamed(v.string());}
// Mac WKScriptMessageHandler call -> chrome.webview {channel, body} envelope.
std::string toWebView2(std::string text) {
    const std::string prefix="window.webkit.messageHandlers.";
    for(auto at=text.find(prefix);at!=std::string::npos;at=text.find(prefix,at)) {
        const auto nameStart=at+prefix.size();const auto call=text.find(".postMessage(",nameStart);
        const auto name=text.substr(nameStart,call-nameStart);const auto open=call+13;
        int depth=0;std::size_t close=open;
        for(;close<text.size();++close) {if(text[close]=='{') ++depth;else if(text[close]=='}') {if(--depth==0) break;}}
        const auto payload=text.substr(open,close-open+1);
        check(text[close+1]==')',"Mac post call closes after its object payload");
        const auto replacement="window.chrome.webview.postMessage({channel:'"+name+"',body:"+payload+"})";
        text.replace(at,close+2-at,replacement);at+=replacement.size();
    }
    return text;
}
void policy(const Json& d) {
    for(const auto& row:d["origin"].array()) {
        check(h::parseURL(row["url"].isNull()?"":row["url"].string()).has_value()==row["parsed"].boolean()||row["url"].isNull()||!row["accepts"].boolean(),"URL parse agrees where it matters");
        check(p::acceptsCredentialOrigin(url(row["url"]),region(row["region"]))==row["accepts"].boolean(),"Credential origin matches Mac: "+row["url"].encode()+" "+row["region"].string());
    }
    for(const auto& row:d["navigation"].array())
        check(p::permitsNavigation(url(row["url"]),region(row["region"]),row["mainFrame"].boolean())==row["permits"].boolean(),
              "Navigation policy matches Mac: "+row["url"].encode()+" main="+(row["mainFrame"].boolean()?"1 ":"0 ")+row["region"].string());
    for(const auto& row:d["popup"].array())
        check(p::permitsPopup(url(row["url"]),url(row["source"]),region(row["region"]))==row["permits"].boolean(),"Popup policy matches Mac: "+row["url"].encode()+" from "+row["source"].encode());
    for(const auto& row:d["zoom"].array()) {
        const double width=std::strtod(row["width"].string().c_str(),nullptr),zoom=std::strtod(row["zoom"].string().c_str(),nullptr);
        check(p::pageZoom(width)==zoom,"Page zoom matches Mac for width "+row["width"].string());
    }
    for(const auto& row:d["accepts"].array()) check(p::acceptsCredential(row["value"].string())==row["accepts"].boolean(),"Credential character policy matches Mac");
    for(const auto& row:d["sessions"].array()) {
        const auto body=Json::parse(row["body"].string());
        const auto s=p::decodeSession(body,row["nonce"].string(),region(row["region"]),row["mainFrame"].boolean(),h::parseURL(row["frame"].string()),h::parseURL(row["page"].string()));
        const auto& e=row["session"];const auto label="Session message validation matches Mac: "+row["body"].string()+" "+row["frame"].string()+" "+row["page"].string();
        check(s.has_value()==!e.isNull(),label);
        if(s) check(s->cred==e["cred"].string()&&s->signingToken==std::optional<std::string>(e["signingToken"].string())&&s->deviceID==std::optional<std::string>(e["deviceID"].string())&&h::regionName(s->region)==e["region"].string(),label);
        const auto legacy=p::decodeCredential(body,row["nonce"].string(),region(row["region"]),row["mainFrame"].boolean(),h::parseURL(row["frame"].string()),h::parseURL(row["page"].string()));
        check(legacy==(row["legacy"].isNull()?std::nullopt:std::optional<std::string>(row["legacy"].string())),"Legacy credential decoder matches Mac");
    }
    const auto nonce=d["nonce"].string();
    for(const auto r:{h::Region::mainland,h::Region::global}) {
        const auto& s=d["scripts"][h::regionName(r)];
        check(p::siteURL(r)==s["site"].string(),"Official site URL");
        check(p::bridgeScript(r,nonce,p::ScriptHost::macOS)==s["bridge"].string(),"Bridge script is a byte-identical port");
        check(p::credentialReadScript(r,nonce,p::ScriptHost::macOS)==s["read"].string(),"Credential read script is a byte-identical port");
        check(p::directLoginScript(r,nonce,p::ScriptHost::macOS)==s["direct"].string(),"Direct login script is a byte-identical port");
        check(p::bridgeScript(r,nonce)==toWebView2(s["bridge"].string())&&p::credentialReadScript(r,nonce)==toWebView2(s["read"].string())&&
              p::directLoginScript(r,nonce)==toWebView2(s["direct"].string()),"WebView2 scripts differ from the Mac only in the message channel");
        check(p::bridgeScript(r,nonce).find("webkit")==std::string::npos,"WebView2 scripts never reference WebKit handlers");
    }
    const auto& tags=d["tags"].array();
    const h::LoginDiagnostic all[]{h::LoginDiagnostic::consented,h::LoginDiagnostic::pageStarted,h::LoginDiagnostic::pageLoaded,h::LoginDiagnostic::pageRetried,
        h::LoginDiagnostic::pageTimeout,h::LoginDiagnostic::pageFailed,h::LoginDiagnostic::webProcessTerminated,h::LoginDiagnostic::navigationBlocked,
        h::LoginDiagnostic::popupOpened,h::LoginDiagnostic::popupClosed,h::LoginDiagnostic::credentialAccepted,h::LoginDiagnostic::credentialRejected,
        h::LoginDiagnostic::loginFormRequested,h::LoginDiagnostic::loginFormUnavailable,h::LoginDiagnostic::credentialContextMissing,h::LoginDiagnostic::completed,
        h::LoginDiagnostic::cancelled,h::LoginDiagnostic::expired};
    for(std::size_t i=0;i<tags.size();++i) check(h::loginDiagnosticTag(all[i],-1001)==tags[i].string(),"Diagnostic tag matches Mac");
    const auto& keys=d["keys"].array();
    check(keys[0].string()==p::credentialKey&&keys[1].string()==p::signingTokenKey&&keys[2].string()==p::deviceIDKey&&keys[3].string()==p::messageName&&keys[4].string()==p::stateMessageName,"Scoped storage keys and channels");
}
void session() {
    const std::string nonce="5c0f2b0e-1d1d-4e4e-9a9a-000000000042";
    bool rejected=false;try {h::LoginSession bad(h::Region::mainland,"");} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"A session requires a nonce");
    h::LoginSession s(h::Region::mainland,nonce);
    check(s.presenting()&&!s.consented()&&!s.nextDeadline(),"Consent screen first: no browser, deadline or timer before explicit consent");
    check(!s.allowNavigation("https://www.skland.com/",true,false)&&!s.credentialMessage(Json::Object{},true,"https://www.skland.com","https://www.skland.com/"),"Nothing is accepted before consent");
    check(s.consent(100)&&!s.consent(101)&&s.nextDeadline()==100+h::LoginSession::sessionExpiry,"Consent arms only the 900 s session expiry");
    s.pageStarted(100);check(s.nextDeadline()==125&&s.statusText(endfield::core::Language::english)=="Loading…","Loading arms the 25 s page deadline");
    check(!s.deadline(124.9)&&s.status()=="loading","Early deadline wake changes nothing");
    check(!s.deadline(125)&&s.status()=="failed"&&s.statusText(endfield::core::Language::english)=="The sign-in page could not load. Retry to continue.","Page timeout shows the retry text");
    check(s.retry(130),"Retry reloads the official page");
    s.pageStarted(130);s.pageFinished(true,"www.skland.com");check(s.status()=="host"&&s.nextDeadline()==1000,"Loaded page clears its deadline and shows the host");
    check(s.allowNavigation("https://user.hypergryph.com/login",true,false)&&!s.allowNavigation("https://evil.example/",true,false)&&s.status()=="blocked","Navigation allowlist; blocked routes show the supported-route hint");
    check(s.allowNavigation("https://gcaptcha4.geetest.com/x",false,false)&&!s.allowNavigation("https://gcaptcha4.geetest.com/x",true,false),"Challenge hosts only in frames");
    check(s.allowNavigation("about:blank",true,true)&&!s.allowNavigation("about:blank",true,false),"Blank main navigation only inside an approved child");
    check(s.allowPopup("about:blank","https://www.skland.com/")&&s.allowPopup("https://user.hypergryph.com/x","https://user.hypergryph.com/")&&!s.allowPopup("about:blank","https://www.skland.com/")&&s.popupCount()==2,"At most two official popups");
    s.popupClosed();check(s.popupCount()==1&&!s.allowPopup("about:blank","https://evil.example/"),"Popups require a trusted official source");
    const Json good=Json::Object{{"nonce",nonce},{"cred","SYNTHETIC-CRED"},{"signingToken","synthetic-token"},{"deviceID","synthetic-device"}};
    check(!s.credentialMessage(good,true,"https://user.hypergryph.com","https://www.skland.com/"),"Credentials only from the community origin");
    check(!s.credentialMessage(good,false,"https://www.skland.com","https://www.skland.com/"),"Credentials only from the main frame");
    s.stateMessage(Json::Object{{"nonce",nonce},{"event","form-requested"}},true,"https://www.skland.com","https://www.skland.com/");
    s.stateMessage(Json::Object{{"nonce","other"},{"event","context-missing"}},true,"https://www.skland.com","https://www.skland.com/");
    const auto result=s.credentialMessage(good,true,"https://www.skland.com","https://www.skland.com/user");
    check(result&&result->cred=="SYNTHETIC-CRED"&&result->signingToken==std::optional<std::string>("synthetic-token")&&result->region==h::Region::mainland,"Valid nonce-bound session message completes");
    const auto& tags=s.diagnostics();
    check(std::find(tags.begin(),tags.end(),"login-form-requested")!=tags.end()&&std::find(tags.begin(),tags.end(),"credential-context-missing")==tags.end(),"State messages need the nonce");
    s.finish(h::LoginSession::Outcome::completed);s.finish(h::LoginSession::Outcome::cancelled);
    check(!s.presenting()&&s.nonce().empty()&&!s.nextDeadline()&&!s.credentialMessage(good,true,"https://www.skland.com","https://www.skland.com/"),"Finished session accepts nothing and keeps no deadline");
    check(s.diagnostics().back()=="completed"&&std::count(s.diagnostics().begin(),s.diagnostics().end(),"cancelled")==0,"The Mac outcome tag is recorded once");
    h::LoginSession expiring(h::Region::global,nonce);expiring.consent(0);
    check(expiring.deadline(899.9)==std::nullopt&&expiring.deadline(900)==h::LoginFailure::expired,"Session expires after 900 s");
    expiring.finish(h::LoginSession::Outcome::expired);check(expiring.diagnostics().back()=="expired","Expiry records the expired tag");
    const auto zh=h::LoginSession::strings(h::Region::mainland,endfield::core::Language::simplifiedChinese);
    check(zh.title=="EndfieldHUD · 森空岛"&&zh.consentTitle=="通过官方网站关联"&&zh.continueLabel=="继续前往官方登录"&&h::LoginSession::strings(h::Region::global,endfield::core::Language::english).title=="EndfieldHUD · SKPORT","Login window strings");
    check(p::documentEndWrapper("x();").find("DOMContentLoaded")!=std::string::npos,"Document-end injection waits for the parsed DOM");
}
// Windows names its own credential store; the Mac Keychain text stays available
// byte-for-byte for oracle replays and every other string is the Mac catalog text.
void wording() {
    using L=endfield::core::Language;
    for(const auto l:{L::english,L::simplifiedChinese,L::traditionalChinese,L::japanese,L::korean}) {
        const auto windows=h::LoginSession::strings(h::Region::mainland,l).consentDetail;
        const auto mac=h::LoginSession::strings(h::Region::mainland,l,h::AccountWording::mac).consentDetail;
        check(mac==endfield::core::localized(h::macConsentDetail,h::macConsentDetailChinese,l),"Mac wording keeps the shared catalog text");
        check(windows!=mac&&windows.find("Windows")!=std::string::npos,"Windows consent names the current Windows account store");
        const auto vault=h::accountText(h::macVaultFailure,h::macVaultFailureChinese,l,h::AccountWording::windows);
        check(!vault.empty()&&vault!=endfield::core::localized(h::macVaultFailure,h::macVaultFailureChinese,l),"Vault failure has Windows wording");
        for(const std::string_view word:{"Keychain","Mac","钥匙串","鑰匙圈","キーチェーン","키체인"})
            check(windows.find(word)==std::string::npos&&vault.find(word)==std::string::npos,"Windows wording never mentions the Mac Keychain");
        check(h::accountText("Retry","重试",l,h::AccountWording::windows)==endfield::core::localized("Retry","重试",l),"Other account strings are the Mac catalog text");
    }
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Pass tests/fixtures/hypergryph-account-source.json");
        const auto bytes=ehud::data::detail::readFile(argv[1],4*1024*1024);check(bytes.has_value(),"Read bounded account oracle");
        const auto fixture=Json::parse(*bytes,4*1024*1024);
        policy(fixture["login"]);session();wording();
        std::cout<<"PASS "<<checks<<" Hypergryph account login checks\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
