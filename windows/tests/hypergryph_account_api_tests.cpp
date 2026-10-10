// Account Linking: model, signing, request canonicalization, decoding and API
// state-machine contracts against the unchanged Mac sources (synthetic fixture
// from windows/tools/hypergryph_account_reference.sh). No network, real token,
// account identifier or personal data is used.
#include "modules/hypergryph_account_api.hpp"
#include "modules/hypergryph_account_crypto.hpp"
#include "modules/hypergryph_account_unicode.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>

namespace h=endfield::modules::hypergryph;
using ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool value,const std::string& why) {++checks;if(!value) throw std::runtime_error(why);}
double num(const Json& v) {if(v.isString()) return std::strtod(v.string().c_str(),nullptr);return v.number();}
std::optional<double> optNum(const Json& v) {if(v.isNull()) return std::nullopt;return num(v);}
std::optional<std::string> optText(const Json& v) {if(v.isNull()) return std::nullopt;return v.string();}
bool same(std::optional<double> a,std::optional<double> b) {return a.has_value()==b.has_value()&&(!a||*a==*b||(std::isnan(*a)&&std::isnan(*b)));}
std::string hex(const auto& digest) {return h::lowercaseHex(std::span<const std::uint8_t>(digest.data(),digest.size()));}

void crypto() {
    check(hex(h::sha256(""))=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA-256 empty vector");
    check(hex(h::sha256("abc"))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 abc vector");
    check(hex(h::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1","SHA-256 two-block vector");
    check(hex(h::hmacSha256(std::string(20,'\x0b'),"Hi There"))=="b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7","RFC 4231 case 1");
    check(hex(h::hmacSha256("Jefe","what do ya want for nothing?"))=="5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843","RFC 4231 case 2");
    check(hex(h::hmacSha256(std::string(131,'\xaa'),"Test Using Larger Than Block-Size Key - Hash Key First"))=="60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54","RFC 4231 case 6 (long key)");
    check(hex(h::md5(""))=="d41d8cd98f00b204e9800998ecf8427e"&&hex(h::md5("abc"))=="900150983cd24fb0d6963f7d28e17f72","RFC 1321 vectors");
    check(hex(h::md5("12345678901234567890123456789012345678901234567890123456789012345678901234567890"))=="57edf4a22be3c955ac49da2e2107b67a","RFC 1321 multi-block vector");
}
void unicode(const Json& sets) {
    for(const auto& [name,set]:std::initializer_list<std::pair<const char*,h::UnicodeSet>>{{"alphanumerics",h::UnicodeSet::alphanumerics},{"whitespaces",h::UnicodeSet::whitespaces},
        {"whitespacesAndNewlines",h::UnicodeSet::whitespacesAndNewlines},{"controlCharacters",h::UnicodeSet::controlCharacters},{"format",h::UnicodeSet::format},
        {"icuDigit",h::UnicodeSet::icuDigit},{"icuSpace",h::UnicodeSet::icuSpace},{"prependBeforeHash",h::UnicodeSet::prependBeforeHash},{"joinsAfterBase",h::UnicodeSet::joinsAfterBase},
        {"digitEquivalent0",h::UnicodeSet::digitEquivalent0},{"digitEquivalent1",h::UnicodeSet::digitEquivalent1},{"digitEquivalent2",h::UnicodeSet::digitEquivalent2},
        {"digitEquivalent3",h::UnicodeSet::digitEquivalent3},{"digitEquivalent4",h::UnicodeSet::digitEquivalent4},{"digitEquivalent5",h::UnicodeSet::digitEquivalent5},
        {"digitEquivalent6",h::UnicodeSet::digitEquivalent6},{"digitEquivalent7",h::UnicodeSet::digitEquivalent7},{"digitEquivalent8",h::UnicodeSet::digitEquivalent8},
        {"digitEquivalent9",h::UnicodeSet::digitEquivalent9}}) {
        const auto& expected=sets[name].array();const auto actual=h::unicodeRanges(set);
        check(expected.size()==actual.size(),std::string("Generated Unicode table size matches Mac runtime: ")+name);
        for(std::size_t i=0;i<actual.size();++i) check(expected[i].array()[0].integer()==actual[i].first&&expected[i].array()[1].integer()==actual[i].last,std::string("Unicode range matches: ")+name);
        for(const auto& r:expected) {check(h::unicodeContains(set,char32_t(r.array()[0].integer()))&&h::unicodeContains(set,char32_t(r.array()[1].integer())),"Range lookup includes bounds");
            if(r.array()[1].integer()<0x10ffff) check(!h::unicodeContains(set,char32_t(r.array()[1].integer()+1))||&r!=&expected.back(),"Range lookup excludes the next scalar");}
    }
}
void identity(const Json& rows) {
    for(const auto& row:rows.array()) {
        h::Snapshot s;s.role.name=optText(row["binding"]);s.name=optText(row["card"]);
        const auto id=s.personalIdentity();
        check(id.name==optText(row["name"])&&id.tag==optText(row["tag"]),"personalProfileIdentity matches Mac for "+row["card"].encode()+" / "+row["binding"].encode());
    }
}
void sanity(const Json& rows) {
    std::size_t n{};
    for(const auto& row:rows.array()) {
        h::Snapshot s;s.role.game=*h::gameNamed(row["game"].string());s.observedAt=num(row["observedAt"]);
        s.stamina=h::Stamina{row["current"].integer(),row["maximum"].integer(),optNum(row["fullRecoveryAt"]),optNum(row["serverObservedAt"])};
        const auto out=s.sanityPresentation(num(row["date"]),row["refreshing"].boolean(),row["available"].boolean());
        const auto& expected=row["out"];
        check(out.has_value()==!expected.isNull(),"Sanity presentation availability "+std::to_string(n));
        if(out) {
            check(out->current==expected["current"].integer()&&out->maximum==expected["maximum"].integer(),"Projected sanity value never lowers reported value "+std::to_string(n));
            check(out->observedAt==num(expected["observedAt"])&&same(out->nextRecoveryAt,optNum(expected["next"]))&&same(out->fullRecoveryAt,optNum(expected["full"])),"Projected recovery deadlines bit-identical "+std::to_string(n));
            check(out->isRefreshing==expected["refreshing"].boolean()&&out->refreshAvailable==expected["available"].boolean()&&h::gameName(out->game)==expected["game"].string(),"Sanity flags preserved");
        }
        ++n;
    }
    check(n>=100,"Sanity oracle has broad coverage");
}
void signatures(const Json& rows) {
    for(const auto& row:rows.array())
        check(h::signature(row["path"].string(),row["query"].string(),row["timestamp"].string(),row["token"].string(),optText(row["device"]))==row["sign"].string(),
              "Byte-identical HMAC-SHA256+MD5 signature for "+row["path"].string()+" "+row["timestamp"].string());
}
Json roleJson(const h::Role& r) {
    Json::Object o{{"region",std::string(h::regionName(r.region))},{"game",std::string(h::gameName(r.game))},{"bindingUID",r.bindingUID},{"roleID",r.roleID},
        {"isDefault",r.isDefault},{"isAvailable",r.isAvailable},{"id",r.id()}};
    if(r.serverID) o["serverID"]=*r.serverID;if(r.name) o["name"]=*r.name;if(r.serverName) o["serverName"]=*r.serverName;if(r.communityUserID) o["communityUserID"]=*r.communityUserID;
    return o;
}
h::Role role(const Json& v) {
    h::Role r;r.region=*h::regionNamed(v["region"].string());r.game=*h::gameNamed(v["game"].string());r.bindingUID=v["bindingUID"].string();r.roleID=v["roleID"].string();
    r.serverID=optText(v["serverID"]);r.name=optText(v["name"]);r.serverName=optText(v["serverName"]);r.isDefault=v["isDefault"].boolean();r.isAvailable=v["isAvailable"].boolean();
    r.communityUserID=optText(v["communityUserID"]);if(v.contains("id")) check(r.id()==v["id"].string(),"Length-prefixed composite role identity matches Mac");return r;
}
void sameSnapshot(const h::Snapshot& s,const Json& e,const std::string& why) {
    check(roleJson(s.role)==roleJson(role(e["role"])),why+": role");
    check(s.observedAt==num(e["observedAt"]),why+": observedAt");
    check(s.name==optText(e["name"])&&s.avatarURL==optText(e["avatarURL"]),why+": name/avatar");
    const auto i=[](const Json& v){return v.isNull()?std::optional<std::int64_t>{}:std::optional<std::int64_t>(v.integer());};
    check(s.level==i(e["level"])&&s.worldLevel==i(e["worldLevel"])&&s.experience==i(e["experience"]),why+": levels");
    check(same(s.createdAt,optNum(e["createdAt"])),why+": createdAt");
    check(s.operatorCount==i(e["operatorCount"])&&s.weaponCount==i(e["weaponCount"])&&s.documentCount==i(e["documentCount"]),why+": counts");
    check(s.stamina.has_value()==e.contains("stamina"),why+": stamina presence");
    if(s.stamina) {const auto& st=e["stamina"];check(s.stamina->current==st["current"].integer()&&s.stamina->maximum==st["maximum"].integer()&&
        same(s.stamina->fullRecoveryAt,optNum(st["fullRecoveryAt"]))&&same(s.stamina->serverObservedAt,optNum(st["serverObservedAt"])),why+": stamina");}
}
void sameError(const h::Error& e,const Json& row,const std::string& why) {
    check(h::errorName(e.kind)==row["error"].string(),why+": error kind "+std::string(h::errorName(e.kind))+" vs "+row["error"].string());
    if(row.contains("code")) check(e.code==row["code"].integer(),why+": error code");
}
void decode(const Json& d) {
    for(const auto& row:d["payload"].array()) {
        const auto r=h::payload(Json::parse(row["envelope"].string()));
        if(row.contains("ok")) {check(r.ok(),"payload accepts "+row["envelope"].string());Json::Array keys;for(const auto& [k,v]:r.value().object()) keys.emplace_back(k);check(Json(keys)==row["ok"],"payload data");}
        else {check(!r.ok(),"payload rejects "+row["envelope"].string());sameError(r.error(),row,"payload "+row["envelope"].string());}
    }
    for(const auto& row:d["bindings"].array()) {
        const auto r=h::decodeBindings(Json::parse(row["data"].string()),*h::regionNamed(row["region"].string()));
        const auto label="bindings "+row["data"].string().substr(0,160);
        if(row.contains("ok")) {
            check(r.ok(),label+" decodes");const auto& expected=row["ok"].array();check(expected.size()==r.value().size(),label+" role count");
            for(std::size_t i=0;i<expected.size();++i) check(roleJson(r.value()[i])==roleJson(role(expected[i])),label+" role "+std::to_string(i)+" "+roleJson(r.value()[i]).encode());
        } else {check(!r.ok(),label+" rejected");sameError(r.error(),row,label);}
    }
    for(const auto& row:d["profiles"].array()) {
        const auto r=h::decodeProfile(Json::parse(row["data"].string()),role(row["role"]),781000000.5);
        const auto label="profile "+row["data"].string().substr(0,160);
        if(row.contains("ok")) {check(r.ok(),label+" decodes");sameSnapshot(r.value(),row["ok"],label);}
        else {check(!r.ok(),label+" rejected");sameError(r.error(),row,label);}
    }
}

// Scripted transport replaying the oracle's in-process URLProtocol responses.
struct FakeRequest:h::AccountRequest {bool cancelled{};void cancel() override {cancelled=true;}};
struct FakeTransport:h::HttpTransport {
    std::deque<Json> script;std::vector<h::HttpRequest> captured;std::deque<std::function<void()>> queue;
    h::RequestHandle start(h::HttpRequest request,std::function<void(h::HttpOutcome)> completion) override {
        captured.push_back(request);auto handle=std::make_shared<FakeRequest>();
        h::HttpOutcome outcome;
        if(script.empty()) outcome.failure=h::Error{h::ErrorKind::transport};
        else {
            const auto stub=script.front();script.pop_front();
            if(stub.contains("failure")) outcome.failure=h::Error{stub["failure"].integer()==-999?h::ErrorKind::cancelled:h::ErrorKind::transport};
            else if(stub.contains("redirect")) outcome.failure=h::Error{h::ErrorKind::unsafeRedirect};
            else {
                outcome.status=static_cast<int>(stub["status"].integer());outcome.body=stub["body"].string();
                const auto& length=stub["headers"]["Content-Length"];
                if(!(outcome.status>=200&&outcome.status<=299)&&outcome.status!=401&&outcome.status!=403) outcome.failure=h::Error{h::ErrorKind::http,outcome.status};
                else if(length.isString()&&std::stoll(length.string())>static_cast<long long>(h::maximumResponseBytes)) outcome.failure=h::Error{h::ErrorKind::responseTooLarge};
            }
        }
        queue.push_back([handle,outcome,completion]{if(!handle->cancelled) completion(outcome);});
        return handle;
    }
    void post(std::function<void()> f) override {queue.push_back(std::move(f));}
    void drain() {while(!queue.empty()) {auto f=std::move(queue.front());queue.pop_front();f();}}
};
std::optional<h::Credentials> credentials(const Json& v) {
    if(!v.contains("signingToken")) return std::nullopt;
    return h::Credentials{v["cred"].string(),v["signingToken"].string(),optText(v["deviceID"])};
}
void api(const Json& scenarios) {
    std::size_t requests{};
    for(const auto& scenario:scenarios.array()) {
        FakeTransport transport;double unix=1791000000.75;
        const auto routeName=scenario["route"].string();
        const auto route=routeName=="web"?h::EndfieldCardRoute::web:routeName=="app"?h::EndfieldCardRoute::app:h::EndfieldCardRoute::authenticatedSelf;
        h::HypergryphAccountClient client(transport,[&]{return h::fromUnix(unix);},route);
        std::size_t index{};
        for(const auto& step:scenario["steps"].array()) {
            const auto label=scenario["name"].string()+" step "+std::to_string(index++);
            const auto op=step["op"].string();
            if(op=="advance") {unix+=num(step["seconds"]);continue;}
            check(h::toUnix(h::fromUnix(unix))==h::toUnix(num(step["now"])),label+": oracle clock");
            transport.captured.clear();transport.script.clear();for(const auto& r:step["responses"].array()) transport.script.push_back(r);
            std::optional<Json> result;
            const auto& in=step["input"];const auto region=in.contains("region")?*h::regionNamed(in["region"].string()):h::Region::mainland;
            const auto onCredentials=[&](h::Outcome<h::Credentials> r){Json v;if(r.ok()) v=Json::Object{{"ok",Json::Object{{"cred",r.value().cred},{"signingToken",r.value().signingToken},{"deviceID",r.value().deviceID?Json(*r.value().deviceID):Json()}}}};
                else {v=Json::Object{{"error",std::string(h::errorName(r.error().kind))}};if(r.error().kind==h::ErrorKind::service||r.error().kind==h::ErrorKind::http) v["code"]=r.error().code;}result=v;};
            if(op=="refreshCred") client.refreshCredentials(in["cred"].string(),region,onCredentials);
            else if(op=="refresh") client.refreshCredentials(*credentials(in),region,onCredentials);
            else if(op=="bindings") client.bindings(*credentials(in),region,[&](h::Outcome<std::vector<h::Role>> r){
                if(r.ok()) {Json::Array roles;for(const auto& x:r.value()) roles.push_back(roleJson(x));result=Json::Object{{"ok",roles}};}
                else {Json v=Json::Object{{"error",std::string(h::errorName(r.error().kind))}};if(r.error().kind==h::ErrorKind::service||r.error().kind==h::ErrorKind::http) v["code"]=r.error().code;result=v;}});
            else if(op=="profile") client.profile(role(in["role"]),*credentials(in),[&](h::Outcome<h::Snapshot> r){
                if(r.ok()) {Json snapshot=Json::Object{{"snapshot",true}};result=Json::Object{{"ok",snapshot}};
                    sameSnapshot(r.value(),step["result"]["ok"],label+" snapshot");}
                else {Json v=Json::Object{{"error",std::string(h::errorName(r.error().kind))}};if(r.error().kind==h::ErrorKind::service||r.error().kind==h::ErrorKind::http) v["code"]=r.error().code;result=v;}});
            check(!result,label+": completion is never synchronous");
            transport.drain();
            check(result.has_value(),label+": completion delivered on owner drain");
            const auto& expected=step["result"];
            if(op=="profile"&&expected.contains("ok")) check(result->contains("ok"),label+": profile succeeds");
            else check(*result==expected,label+": result "+result->encode()+" vs "+expected.encode());
            const auto& captured=step["requests"].array();
            check(captured.size()==transport.captured.size(),label+": request count "+std::to_string(transport.captured.size())+" vs "+std::to_string(captured.size()));
            for(std::size_t i=0;i<captured.size();++i) {
                const auto& mine=transport.captured[i];const auto& theirs=captured[i];
                check(mine.url()==theirs["url"].string()&&theirs["method"].string()=="GET",label+": URL "+mine.url()+" vs "+theirs["url"].string());
                Json::Object headers;for(const auto& [k,v]:mine.headers) headers[k]=v;
                check(Json(headers)==theirs["headers"],label+": header set and values byte-identical "+Json(headers).encode()+" vs "+theirs["headers"].encode());
                ++requests;
            }
            check(static_cast<std::int64_t>(transport.script.size())==step["unused"].integer(),label+": scripted responses consumed identically");
        }
    }
    check(requests>=40,"API oracle exercised many signed requests");
}
void cancellation() {
    FakeTransport transport;h::HypergryphAccountClient client(transport,[]{return 800000000.0;});
    const h::Credentials c{"SYNTHETIC-C","synthetic-t","synthetic-d"};
    std::vector<std::string> seen;
    auto request=client.bindings(c,h::Region::mainland,[&](h::Outcome<std::vector<h::Role>> r){seen.push_back(r.ok()?"ok":std::string(h::errorName(r.error().kind)));});
    transport.script.push_back(Json::Object{{"status",200},{"headers",Json::Object{}},{"body","{\"code\":0,\"data\":{\"list\":[]}}"}});
    check(request!=nullptr&&transport.captured.size()==1,"Bindings starts one request");
    request->cancel();request->cancel();transport.drain();
    check(seen==std::vector<std::string>{"cancelled"},"Cancelled chain delivers exactly one cancelled completion and no stale result");
    seen.clear();
    auto none=client.bindings(h::Credentials{"bad cred","t",std::nullopt},h::Region::global,[&](h::Outcome<std::vector<h::Role>> r){seen.push_back(std::string(h::errorName(r.error().kind)));});
    check(!none&&seen.empty(),"Invalid credentials fail without a request or synchronous callback");
    transport.drain();check(seen==std::vector<std::string>{"invalidCredentials"},"Invalid credentials delivered on owner drain");
    // Recovery chain cancelled during its refresh hop never retries.
    seen.clear();transport.captured.clear();
    transport.script={Json::Object{{"status",200},{"headers",Json::Object{}},{"body","{\"code\":10000}"}},Json::Object{{"status",200},{"headers",Json::Object{}},{"body","{\"code\":0,\"data\":{\"token\":\"x\"}}"}}};
    auto recovering=client.bindings(c,h::Region::mainland,[&](h::Outcome<std::vector<h::Role>> r){seen.push_back(r.ok()?"ok":std::string(h::errorName(r.error().kind)));});
    auto first=std::move(transport.queue.front());transport.queue.pop_front();first();
    check(transport.captured.size()==2&&seen.empty(),"10000 starts exactly one signing refresh");
    recovering->cancel();transport.drain();
    check(transport.captured.size()==2&&seen==std::vector<std::string>{"cancelled"},"Cancelling during recovery drops the refresh and never retries");
    // Unsupported path is a programming error, never a request.
    bool rejected=false;try {(void)h::makeRequest(h::Region::mainland,"/api/v1/other",{},std::string("c"),nullptr,0,0);} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"Arbitrary endpoint paths are rejected");
    const auto unsigned_=h::makeRequest(h::Region::mainland,"/web/v1/auth/refresh",{},std::string("SYNTHETIC"),nullptr,0,0);
    check(!unsigned_.header("sign")&&!unsigned_.header("timestamp")&&!unsigned_.header("dId")&&unsigned_.header("CRED")==std::optional<std::string>("SYNTHETIC"),"Cred-only refresh is unsigned; header lookup is case-insensitive");
}
void redaction(const Json& d) {
    h::HttpRequest request;request.host="zonai.skland.com";request.path="/api/v1/game/endfield/card/detail";request.query="roleId=4000500060&serverId=1";
    request.queryItems={{"roleId","4000500060"},{"serverId","1"}};
    for(const auto& [k,v]:d["request"]["headers"].object()) request.headers.emplace_back(k,v.string());
    check(request.url()==d["request"]["url"].string(),"Redaction request URL");
    for(const auto& row:d["redacted"].array()) {
        std::vector<std::string> secrets;for(const auto& s:row["secrets"].array()) secrets.push_back(s.string());
        const auto out=h::redactedServiceMessage(row["message"],request,secrets);
        check(out==row["out"].string(),"Redacted service message matches Mac: "+row["message"].encode().substr(0,80)+" => "+out.substr(0,120)+" vs "+row["out"].string().substr(0,120));
    }
    for(const auto& row:d["classify"].array())
        check(h::diagnosticName(h::classifyServiceMessage(row["message"]))==row["reason"].string(),"Diagnostic reason matches Mac: "+row["message"].encode().substr(0,60));
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Pass tests/fixtures/hypergryph-account-source.json");
        const auto bytes=ehud::data::detail::readFile(argv[1],4*1024*1024);check(bytes.has_value(),"Read bounded account oracle");
        const auto fixture=Json::parse(*bytes,4*1024*1024);
        check(fixture["provenance"]["modifications"].array().empty()&&fixture["provenance"]["sourceAuthority"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Oracle executed unchanged pinned Mac sources");
        crypto();unicode(fixture["unicode"]);identity(fixture["identity"]);sanity(fixture["sanity"]);signatures(fixture["signature"]);
        decode(fixture["decode"]);api(fixture["api"]);cancellation();redaction(fixture["redaction"]);
        std::cout<<"PASS "<<checks<<" Hypergryph account API checks\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
