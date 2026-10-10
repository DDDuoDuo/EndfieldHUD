#include "modules/hypergryph_account_api.hpp"
#include "modules/hypergryph_account_crypto.hpp"
#include "modules/hypergryph_account_unicode.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace endfield::modules::hypergryph {
namespace {
void need(bool value,const char* why) {if(!value) throw std::invalid_argument(why);}
Error fail(ErrorKind kind,std::int64_t code=0) {return Error{kind,code};}
const Json& member(const Json& object,std::string_view key) {return object[key];}
bool isObject(const Json& value) {return value.isObject();}
// Swift `dictionary[a] ?? dictionary[b]`: a present key (even JSON null) wins.
const Json& either(const Json& object,std::string_view first,std::string_view second) {
    return object.contains(first)?object[first]:object[second];
}
// `as? [[String: Any]]` succeeds only when every element is a dictionary.
const Json::Array* dictionaries(const Json& value) {
    if(!value.isArray()) return nullptr;
    for(const auto& item:value.array()) if(!item.isObject()) return nullptr;
    return &value.array();
}
std::string lower(std::string_view s) {std::string out(s);for(auto& c:out) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');return out;}
}

std::string HttpRequest::url() const {return "https://"+host+path+(query.empty()?"":"?"+query);}
std::optional<std::string> HttpRequest::header(std::string_view name) const {
    const auto key=lower(name);
    for(const auto& [k,v]:headers) if(lower(k)==key) return v;
    return std::nullopt;
}
std::string_view endfieldCardPath(EndfieldCardRoute r) noexcept {return r==EndfieldCardRoute::web?"/web/v1/game/endfield/card/detail":"/api/v1/game/endfield/card/detail";}
bool requiresCommunityUser(EndfieldCardRoute r) noexcept {return r!=EndfieldCardRoute::authenticatedSelf;}
bool allowedPath(std::string_view path) noexcept {
    for(const std::string_view p:{"/web/v1/auth/refresh","/api/v1/game/player/binding","/web/v1/user",
        "/web/v1/game/endfield/card/detail","/api/v1/game/endfield/card/detail","/api/v1/game/player/info"}) if(p==path) return true;
    return false;
}
std::string queryEscape(std::string_view value) {
    static constexpr char digits[]="0123456789ABCDEF";std::string out;out.reserve(value.size());
    for(const char ch:value) {
        const auto c=static_cast<unsigned char>(ch);
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.'||c=='_'||c=='~') out.push_back(ch);
        else {out.push_back('%');out.push_back(digits[c>>4]);out.push_back(digits[c&15]);}
    }
    return out;
}
std::string signature(std::string_view path,std::string_view query,std::string_view timestamp,std::string_view token,const std::optional<std::string>& device) {
    std::string escaped;
    if(device) for(const char c:*device) {if(c=='\\') escaped+="\\\\";else if(c=='"') escaped+="\\\"";else escaped.push_back(c);}
    std::string message;message.reserve(path.size()+query.size()+timestamp.size()*2+escaped.size()+64);
    message.append(path).append(query).append(timestamp);
    message+="{\"platform\":\"3\",\"timestamp\":\"";message.append(timestamp);message+="\",\"dId\":\"";message+=escaped;message+="\",\"vName\":\"1.0.0\"}";
    auto result=communitySignatureDigest(token,message);wipe(message);wipe(escaped);return result;
}
HttpRequest makeRequest(Region region,std::string_view path,const std::vector<std::pair<std::string,std::string>>& query,
                        const std::optional<std::string>& cred,const Credentials* credentials,Time now,double offset) {
    need(allowedPath(path),"Only the fixed read-only community endpoints are reachable");
    HttpRequest r;r.host=std::string(apiHost(region));r.path=std::string(path);r.queryItems=query;
    for(std::size_t i=0;i<query.size();++i) {if(i) r.query.push_back('&');r.query+=queryEscape(query[i].first)+"="+queryEscape(query[i].second);}
    auto set=[&](std::string name,std::string value){r.headers.emplace_back(std::move(name),std::move(value));};
    set("Accept","application/json");set("Content-Type","application/json");
    if(cred) set("cred",*cred);else if(credentials) set("cred",credentials->cred);
    set("platform","3");set("vName","1.0.0");set("Origin",std::string(gameOrigin(region)));set("Referer",std::string(gameOrigin(region))+"/");
    // Mainland game tools omit the language; SKPORT's client sends en.
    if(region==Region::global) set("sk-language","en");
    if(credentials) {
        const double seconds=toUnix(now)+offset;
        need(std::isfinite(seconds)&&seconds>-9223372036854775808.0&&seconds<9223372036854775808.0,"Request clock is not representable");
        const auto timestamp=std::to_string(static_cast<std::int64_t>(seconds));
        set("timestamp",timestamp);
        if(credentials->deviceID) set("dId",*credentials->deviceID);
        set("sign",signature(path,r.query,timestamp,credentials->signingToken,credentials->deviceID));
    }
    return r;
}
EnvelopeOutcome interpretResponse(const HttpOutcome& outcome) {
    if(outcome.failure) return *outcome.failure;
    const auto status=outcome.status;const bool authentication=status==401||status==403;
    if(!(status>=200&&status<=299)&&!authentication) return fail(ErrorKind::http,status);
    if(outcome.body.size()>maximumResponseBytes) return fail(ErrorKind::responseTooLarge);
    Json value;
    try {value=Json::parse(outcome.body,maximumResponseBytes);} catch(const std::exception&) {return authentication?fail(ErrorKind::http,status):fail(ErrorKind::invalidResponse);}
    if(!value.isObject()) return authentication?fail(ErrorKind::http,status):fail(ErrorKind::invalidResponse);
    if(authentication) {const auto code=jsonInteger(value["code"]);if(!code||*code==0) return fail(ErrorKind::http,status);}
    return value;
}
EnvelopeOutcome payload(const Json& envelope) {
    const auto code=jsonInteger(envelope["code"]);
    if(!code) return fail(ErrorKind::invalidResponse);
    if(*code==10002) return fail(ErrorKind::authenticationExpired);
    if(*code!=0) return fail(ErrorKind::service,*code);
    const auto& data=envelope["data"];if(!data.isObject()) return fail(ErrorKind::invalidResponse);
    return data;
}
std::optional<bool> jsonBool(const Json& value) {
    if(value.isBool()) return value.boolean();
    if(value.isNumber()) {const double d=value.number();if(d==0) return false;if(d==1) return true;}
    return std::nullopt;
}
Outcome<std::vector<Role>> decodeBindings(const Json& data,Region region) {
    const auto* apps=dictionaries(data["list"]);
    if(!apps||apps->size()>64) return fail(ErrorKind::invalidResponse);
    std::vector<Role> roles;std::set<std::string,std::less<>> seen;
    for(const auto& app:*apps) {
        const auto& codeValue=app["appCode"];if(!codeValue.isString()) continue;
        const auto game=gameNamed(codeValue.string());if(!game) continue;
        const auto* bindings=dictionaries(app["bindingList"]);
        if(!bindings||bindings->size()>256) return fail(ErrorKind::invalidResponse);
        for(const auto& binding:*bindings) {
            const auto uid=jsonIdentifier(binding["uid"]);if(!uid) continue;
            const bool available=!jsonBool(binding["isDelete"]).value_or(false);
            const bool isDefault=jsonBool(binding["isDefault"]).value_or(false)||jsonIdentifier(app["defaultUid"])==uid;
            if(*game==Game::arknights) {
                Role role{region,*game,*uid,*uid,jsonIdentifier(binding["channelMasterId"]),jsonText(either(binding,"nickName","nickname")),
                          jsonText(binding["channelName"]),isDefault,available};
                if(seen.insert(role.id()).second) roles.push_back(std::move(role));
            } else {
                Json::Array items;
                if(const auto* list=dictionaries(binding["roles"])) items=*list;
                if(items.empty()&&binding["defaultRole"].isObject()) items={binding["defaultRole"]};
                if(items.size()>256) return fail(ErrorKind::invalidResponse);
                const auto defaultID=binding["defaultRole"].isObject()?jsonIdentifier(binding["defaultRole"]["roleId"]):std::nullopt;
                for(const auto& item:items) {
                    const auto id=jsonIdentifier(item["roleId"]),server=jsonIdentifier(item["serverId"]);if(!id||!server) continue;
                    Role role{region,*game,*uid,*id,server,jsonText(either(item,"nickname","nickName")),jsonText(item["serverName"]),
                              jsonBool(item["isDefault"]).value_or(false)||id==defaultID||(isDefault&&items.size()==1),
                              available&&!jsonBool(item["isBanned"]).value_or(false)};
                    if(seen.insert(role.id()).second) roles.push_back(std::move(role));
                }
            }
            if(roles.size()>512) return fail(ErrorKind::invalidResponse);
        }
    }
    return roles;
}
std::int64_t arknightsAP(std::int64_t current,std::int64_t maximum,std::optional<Time> fullRecovery,std::optional<Time> lastRecovery,Time reference) {
    if(current>=maximum||!fullRecovery||!std::isfinite(toUnix(reference))) return current;
    if(*fullRecovery<=reference) return maximum;
    const auto capacity=maximum-current;
    if(lastRecovery) {
        const double recovered=std::floor(std::max(0.0,reference-*lastRecovery)/arknightsSecondsPerPoint);
        if(!std::isfinite(recovered)) return current;
        return recovered>=static_cast<double>(capacity)?maximum:current+static_cast<std::int64_t>(recovered);
    }
    const double missing=std::ceil((*fullRecovery-reference)/arknightsSecondsPerPoint);
    if(!std::isfinite(missing)) return current;
    const std::int64_t estimated=missing>=static_cast<double>(maximum)?0:maximum-static_cast<std::int64_t>(missing);
    return std::max(current,std::min(maximum,estimated));
}
Outcome<Snapshot> decodeProfile(const Json& data,const Role& role,Time observedAt) {
    Snapshot s;s.role=role;s.observedAt=observedAt;
    if(role.game==Game::endfield) {
        const auto& detail=data["detail"];if(!detail.isObject()) return fail(ErrorKind::invalidResponse);
        const auto& base=detail["base"];if(!base.isObject()) return fail(ErrorKind::invalidResponse);
        if(const auto id=jsonIdentifier(base["roleId"]);id&&*id!=role.roleID) return fail(ErrorKind::invalidResponse);
        s.name=jsonText(base["name"]);s.avatarURL=jsonImageURL(base["avatarUrl"]);
        s.level=jsonNonnegative(base["level"]);s.worldLevel=jsonNonnegative(base["worldLevel"]);
        s.experience=jsonNonnegative(base["exp"]);s.createdAt=jsonTimestamp(base["createTime"]);
        s.operatorCount=jsonNonnegative(base["charNum"]);s.weaponCount=jsonNonnegative(base["weaponNum"]);
        s.documentCount=jsonNonnegative(base["docNum"]);
        const auto& dungeon=detail["dungeon"];
        if(dungeon.isObject()) {
            const auto current=jsonNonnegative(dungeon["curStamina"]),maximum=jsonNonnegative(dungeon["maxStamina"]);
            if(current&&maximum&&*maximum>0)
                s.stamina=Stamina{*current,*maximum,jsonTimestamp(dungeon["maxTs"]),jsonTimestamp(detail.contains("currentTs")?detail["currentTs"]:data["currentTs"])};
        }
    } else {
        const auto& status=data["status"];if(!status.isObject()) return fail(ErrorKind::invalidResponse);
        if(const auto id=jsonIdentifier(status["uid"]);id&&*id!=role.roleID) return fail(ErrorKind::invalidResponse);
        s.name=jsonText(status["name"]);
        if(status["avatar"].isObject()) s.avatarURL=jsonImageURL(status["avatar"]["url"]);
        s.level=jsonNonnegative(status["level"]);
        if(status["exp"].isObject()) s.experience=jsonNonnegative(status["exp"]["current"]);
        s.createdAt=jsonTimestamp(status["registerTs"]);s.operatorCount=jsonNonnegative(status["charCnt"]);
        const auto& ap=status["ap"];
        if(ap.isObject()) {
            const auto current=jsonNonnegative(ap["current"]),maximum=jsonNonnegative(ap["max"]);
            if(current&&maximum&&*maximum>0) {
                const auto full=jsonTimestamp(ap["completeRecoveryTime"]),server=jsonTimestamp(data["currentTs"]);
                const auto normalized=arknightsAP(*current,*maximum,full,jsonTimestamp(ap["lastApAddTime"]),server.value_or(observedAt));
                s.stamina=Stamina{normalized,*maximum,full,server};
            }
        }
    }
    return s;
}

namespace {
bool isDigitScalar(char32_t c) {return unicodeContains(UnicodeSet::icuDigit,c);}
bool isSpaceScalar(char32_t c) {return unicodeContains(UnicodeSet::icuSpace,c);}
struct Scalar {char32_t value;std::size_t offset,length;};
std::vector<Scalar> scalars(std::string_view text) {
    std::vector<Scalar> out;
    for(std::size_t at=0;at<text.size();) {const auto s=decodeUTF8(text,at);if(!s) {out.push_back({0xfffd,at,1});++at;continue;}out.push_back({s->value,at,s->length});at+=s->length;}
    return out;
}
std::string replaceMatches(std::string_view text,const std::function<std::size_t(const std::vector<Scalar>&,std::size_t)>& match) {
    // match returns the number of scalars matched at index (0 = no match).
    const auto list=scalars(text);std::string out;std::size_t i=0;
    while(i<list.size()) {
        const auto n=match(list,i);
        if(n) {out+="[redacted]";i+=n;}
        else {out.append(text.substr(list[i].offset,list[i].length));++i;}
    }
    return out;
}
bool asciiIn(char32_t c,std::string_view set) {return c<128&&set.find(static_cast<char>(c))!=std::string_view::npos;}
bool alnumASCII(char32_t c) {return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}
bool letterASCII(char32_t c) {return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
std::size_t urlMatch(const std::vector<Scalar>& s,std::size_t i) {
    const auto lowerAt=[&](std::size_t k){const auto c=s[k].value;return c<128?static_cast<char32_t>(c>='A'&&c<='Z'?c-'A'+'a':c):c;};
    const auto prefix=[&](std::string_view p){if(i+p.size()>s.size()) return false;for(std::size_t k=0;k<p.size();++k) if(lowerAt(i+k)!=static_cast<char32_t>(p[k])) return false;return true;};
    std::size_t start;
    if(prefix("https://")) start=i+8;else if(prefix("http://")) start=i+7;else return 0;
    std::size_t end=start;
    while(end<s.size()&&!isSpaceScalar(s[end].value)&&!asciiIn(s[end].value,"<>\"'")) ++end;
    return end==start?0:end-i;
}
std::size_t emailMatch(const std::vector<Scalar>& s,std::size_t i) {
    const auto local=[](char32_t c){return alnumASCII(c)||asciiIn(c,"._%+-");};
    const auto domain=[](char32_t c){return alnumASCII(c)||asciiIn(c,".-");};
    std::size_t at=i;while(at<s.size()&&local(s[at].value)) ++at;
    if(at==i||at>=s.size()||s[at].value!='@') return 0;
    const std::size_t d0=at+1;std::size_t d1=d0;while(d1<s.size()&&domain(s[d1].value)) ++d1;
    for(std::size_t k=d1;k>d0+1;) {
        --k; // candidate '.' at k with non-empty prefix [d0,k)
        if(s[k].value!='.') continue;
        std::size_t letters=k+1;while(letters<d1&&letterASCII(s[letters].value)) ++letters;
        if(letters-(k+1)>=2) return letters-i;
    }
    return 0;
}
std::size_t opaqueMatch(const std::vector<Scalar>& s,std::size_t i) {
    const auto cls=[](char32_t c){return alnumASCII(c)||asciiIn(c,"_+/=.-");};
    std::size_t at=i;while(at<s.size()&&cls(s[at].value)) ++at;
    return at-i>=20?at-i:0;
}
std::size_t digitsMatch(const std::vector<Scalar>& s,std::size_t i) {
    std::size_t at=i;while(at<s.size()&&isDigitScalar(s[at].value)) ++at;
    return at-i>=3?at-i:0;
}
std::size_t markupMatch(const std::vector<Scalar>& s,std::size_t i) {
    if(s[i].value!='<') return 0;
    for(std::size_t at=i+1;at<s.size();++at) if(s[at].value=='>') return at-i+1;
    return 0;
}
}
std::string redactedServiceMessage(const Json& value,const HttpRequest& request,const std::vector<std::string>& secrets) {
    if(!value.isString()||value.string().empty()) return "No service reason was supplied.";
    auto text=value.string();
    if(text.size()>4096) return "The service reason was too long to display safely.";
    std::vector<std::string> values=secrets;
    for(const char* name:{"cred","dId","sign","sk-game-role"}) if(auto v=request.header(name)) values.push_back(*v);
    for(const auto& item:request.queryItems) values.push_back(item.second);
    std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());
    values.erase(std::remove_if(values.begin(),values.end(),[](const std::string& v){return v.empty();}),values.end());
    const auto characters=[](const std::string& v){std::size_t n{};for(std::size_t at=0;at<v.size();) {const auto s=decodeUTF8(v,at);at+=s?s->length:1;++n;}return n;};
    std::stable_sort(values.begin(),values.end(),[&](const std::string& a,const std::string& b){return characters(a)>characters(b);});
    // String.replacingOccurrences(of:with:) compares Characters: a match must
    // start and end on grapheme boundaries, and an ASCII digit in the needle
    // matches any digit with the same numeric value (tables from the Mac).
    for(const auto& secret:values) {
        std::vector<char32_t> needle;for(const auto& s:scalars(secret)) needle.push_back(s.value);
        const auto list=scalars(text);std::string out;std::size_t i=0;
        const auto matches=[&](std::size_t at){
            if(at+needle.size()>list.size()) return false;
            for(std::size_t k=0;k<needle.size();++k) {
                const auto a=list[at+k].value,b=needle[k];if(a==b) continue;
                if(b>='0'&&b<='9'&&unicodeContains(static_cast<UnicodeSet>(static_cast<int>(UnicodeSet::digitEquivalent0)+static_cast<int>(b-'0')),a)) continue;
                return false;
            }
            if(at>0&&(unicodeContains(UnicodeSet::joinsAfterBase,list[at].value)||unicodeContains(UnicodeSet::prependBeforeHash,list[at-1].value))) return false;
            if(at+needle.size()<list.size()&&unicodeContains(UnicodeSet::joinsAfterBase,list[at+needle.size()].value)) return false;
            return true;
        };
        while(i<list.size()) {
            if(!needle.empty()&&matches(i)) {out+="[redacted]";i+=needle.size();}
            else {out.append(text,list[i].offset,list[i].length);++i;}
        }
        text=std::move(out);
    }
    text=replaceMatches(text,urlMatch);
    text=replaceMatches(text,emailMatch);
    text=replaceMatches(text,opaqueMatch);
    text=replaceMatches(text,digitsMatch);
    text=replaceMatches(text,markupMatch);
    // components(separatedBy: .whitespacesAndNewlines) without empty parts.
    std::string joined;std::string part;
    const auto list=scalars(text);
    for(const auto& s:list) {
        if(unicodeContains(UnicodeSet::whitespacesAndNewlines,s.value)) {if(!part.empty()) {if(!joined.empty()) joined.push_back(' ');joined+=part;part.clear();}}
        else part.append(text,s.offset,s.length);
    }
    if(!part.empty()) {if(!joined.empty()) joined.push_back(' ');joined+=part;}
    std::string filtered;
    for(const auto& s:scalars(joined)) if(!unicodeContains(UnicodeSet::controlCharacters,s.value)&&!unicodeContains(UnicodeSet::format,s.value)) filtered.append(joined,s.offset,s.length);
    if(filtered.empty()) return "No readable service reason was supplied.";
    // String.prefix(512) counts grapheme clusters; joining scalars (Extend,
    // SpacingMark, ZWJ) stay with their base. Emoji ZWJ sequences are counted
    // per pictograph here (diagnostic-only text).
    std::size_t clusters=0,end=0;
    for(const auto& s:scalars(filtered)) {
        const bool joins=clusters>0&&unicodeContains(UnicodeSet::joinsAfterBase,s.value);
        if(!joins) {if(clusters==512) break;++clusters;}
        end=s.offset+s.length;
    }
    return filtered.substr(0,end);
}

RequestHandle AccountService::refreshCredentials(const Credentials& c,Region region,std::function<void(Outcome<Credentials>)> completion) {
    return refreshCredentials(c.cred,region,std::move(completion));
}

namespace {
class Chain final:public AccountRequest {
public:
    Chain(HttpTransport& transport,std::function<void()> onCancel):transport_(&transport),onCancel_(std::move(onCancel)) {}
    bool cancelled() const noexcept {return cancelled_;}
    void replace(RequestHandle request) {if(cancelled_) {if(request) request->cancel();} else request_=std::move(request);}
    template<class F> void finish(F&& action) {const bool deliver=!cancelled_&&!completed_;completed_=true;request_.reset();if(deliver) action();}
    void cancel() override {
        const bool deliver=!cancelled_&&!completed_;cancelled_=true;auto request=std::move(request_);request_.reset();
        if(request) request->cancel();
        if(deliver&&onCancel_) {auto callback=std::move(onCancel_);onCancel_={};transport_->post(std::move(callback));}
    }
private:
    HttpTransport* transport_;std::function<void()> onCancel_;RequestHandle request_;bool cancelled_{},completed_{};
};
using ChainPtr=std::shared_ptr<Chain>;
}

struct HypergryphAccountClient::Impl:std::enable_shared_from_this<Impl> {
    struct SigningState {Sha256Digest identity{};std::string supplied,active;};
    HttpTransport& transport;std::function<Time()> now;EndfieldCardRoute route;
    std::map<Region,double> clockOffsets;std::map<Region,SigningState> signing;
    std::function<void(const Diagnostic&)> onDiagnostic;
    std::function<void(DiagnosticEndpoint,std::int64_t,const std::string&)> onServiceMessage;
    Impl(HttpTransport& t,std::function<Time()> n,EndfieldCardRoute r):transport(t),now(std::move(n)),route(r) {}
    static Sha256Digest identity(const std::string& cred,const std::optional<std::string>& device) {
        std::string material=cred;material.push_back('\0');if(device) material+=*device;
        const auto digest=sha256(material);wipe(material);return digest;
    }
    static bool valid(const Credentials& c) {return validSecret(c.cred)&&validSecret(c.signingToken)&&(!c.deviceID||validSecret(*c.deviceID));}
    template<class T> void deliver(std::function<void(Outcome<T>)> completion,Outcome<T> result) {
        transport.post([completion=std::move(completion),result=std::move(result)]() mutable {completion(std::move(result));});
    }
    void prepareIdentity(const std::string& cred,Region region) {
        const auto id=identity(cred,std::nullopt);auto found=signing.find(region);
        if(found==signing.end()||found->second.identity!=id) signing[region]=SigningState{id,"",""};
    }
    Credentials effective(const Credentials& supplied,Region region) {
        const auto id=identity(supplied.cred,supplied.deviceID);auto found=signing.find(region);
        if(found!=signing.end()&&found->second.identity==id&&(supplied.signingToken==found->second.supplied||supplied.signingToken==found->second.active))
            return Credentials{supplied.cred,found->second.active,supplied.deviceID};
        signing[region]=SigningState{id,supplied.signingToken,supplied.signingToken};
        return supplied;
    }
    bool calibrate(const Json& envelope,Region region) {
        const auto timestamp=jsonTimestamp(envelope["timestamp"]);if(!timestamp) return false;
        clockOffsets[region]=*timestamp-now();return true;
    }
    double offset(Region region) const {const auto found=clockOffsets.find(region);return found==clockOffsets.end()?0:found->second;}
    RequestHandle fetch(HttpRequest request,std::vector<std::string> secrets,std::function<void(EnvelopeOutcome)> completion) {
        std::optional<DiagnosticEndpoint> endpoint;
        if(request.path=="/web/v1/auth/refresh") endpoint=DiagnosticEndpoint::refresh;
        else if(request.path=="/api/v1/game/player/binding") endpoint=DiagnosticEndpoint::bindings;
        else if(request.path=="/web/v1/user") endpoint=DiagnosticEndpoint::communityUser;
        else if(request.path=="/web/v1/game/endfield/card/detail"||request.path=="/api/v1/game/endfield/card/detail") endpoint=DiagnosticEndpoint::endfieldProfile;
        else if(request.path=="/api/v1/game/player/info") endpoint=DiagnosticEndpoint::arknightsProfile;
        auto diagnostic=onDiagnostic;auto service=onServiceMessage;
        if(!service) {for(auto& s:secrets) wipe(s);secrets.clear();}
        auto copy=service?std::optional<HttpRequest>(request):std::nullopt;
        return transport.start(std::move(request),[endpoint,diagnostic,service,copy,secrets,completion=std::move(completion)](HttpOutcome raw) mutable {
            auto result=interpretResponse(raw);
            if(endpoint&&result.ok()) {
                const auto code=jsonInteger(result.value()["code"]);
                if(code&&*code!=0) {
                    const auto& message=result.value().contains("message")?result.value()["message"]:result.value()["msg"];
                    if(diagnostic) diagnostic(Diagnostic{*endpoint,*code,classifyServiceMessage(message)});
                    if(service&&copy) service(*endpoint,*code,redactedServiceMessage(message,*copy,secrets));
                }
            }
            completion(std::move(result));
        });
    }
    RequestHandle refreshSigning(std::string cred,std::optional<Credentials> credentials,Region region,std::function<void(Outcome<Credentials>)> completion) {
        auto chain=std::make_shared<Chain>(transport,[completion]{completion(fail(ErrorKind::cancelled));});
        performRefresh(std::move(cred),std::move(credentials),region,true,chain,std::move(completion));
        return chain;
    }
    void performRefresh(std::string cred,std::optional<Credentials> credentials,Region region,bool mayCorrectClock,ChainPtr chain,std::function<void(Outcome<Credentials>)> completion) {
        if(chain->cancelled()) return;
        auto request=makeRequest(region,"/web/v1/auth/refresh",{},cred,credentials?&*credentials:nullptr,now(),offset(region));
        std::vector<std::string> secrets{cred,credentials?credentials->signingToken:"",credentials&&credentials->deviceID?*credentials->deviceID:""};
        auto self=shared_from_this();
        chain->replace(fetch(std::move(request),std::move(secrets),[self,cred,credentials,region,mayCorrectClock,chain,completion](EnvelopeOutcome result) {
            if(chain->cancelled()) return;
            if(mayCorrectClock&&credentials&&result.ok()&&jsonInteger(result.value()["code"])==std::optional<std::int64_t>(10003)&&self->calibrate(result.value(),region)) {
                self->performRefresh(cred,credentials,region,false,chain,completion);return;
            }
            if(!result.ok()) {chain->finish([&]{completion(result.error());});return;}
            const auto data=payload(result.value());
            if(!data.ok()) {chain->finish([&]{completion(data.error());});return;}
            const auto& token=data.value()["token"];
            if(!token.isString()||!validSecret(token.string())) {chain->finish([&]{completion(fail(ErrorKind::invalidResponse));});return;}
            self->calibrate(result.value(),region);
            Credentials value{cred,token.string(),credentials?credentials->deviceID:std::nullopt};
            chain->finish([&]{completion(std::move(value));});
        }));
    }
    RequestHandle fetchSigned(Region region,std::string path,std::vector<std::pair<std::string,std::string>> query,const Credentials& credentials,std::function<void(EnvelopeOutcome)> completion) {
        auto chain=std::make_shared<Chain>(transport,[completion]{completion(fail(ErrorKind::cancelled));});
        performSigned(region,std::move(path),std::move(query),effective(credentials,region),true,chain,std::move(completion));
        return chain;
    }
    void performSigned(Region region,std::string path,std::vector<std::pair<std::string,std::string>> query,Credentials credentials,bool mayRecover,ChainPtr chain,std::function<void(EnvelopeOutcome)> completion) {
        if(chain->cancelled()) return;
        auto request=makeRequest(region,path,query,std::nullopt,&credentials,now(),offset(region));
        std::vector<std::string> secrets{credentials.cred,credentials.signingToken,credentials.deviceID.value_or("")};
        auto self=shared_from_this();
        chain->replace(fetch(std::move(request),std::move(secrets),[self,region,path,query,credentials,mayRecover,chain,completion](EnvelopeOutcome result) {
            if(chain->cancelled()) return;
            const auto code=result.ok()?jsonInteger(result.value()["code"]):std::nullopt;
            if(!mayRecover||!code||(*code!=10000&&*code!=10003)) {chain->finish([&]{completion(std::move(result));});return;}
            // Stale signing token (10000) and clock skew (10003) are distinct
            // from an expired credential (10002). Exactly one retry follows.
            const bool clockUpdated=self->calibrate(result.value(),region);
            if(*code==10003) {
                if(!clockUpdated) {chain->finish([&]{completion(std::move(result));});return;}
                self->performSigned(region,path,query,credentials,false,chain,completion);return;
            }
            // The official client clears only its stale signing key, still signs
            // the refresh and retains its issued dId.
            Credentials context{credentials.cred,"",credentials.deviceID};
            auto refresh=self->refreshSigning(credentials.cred,context,region,[self,region,path,query,credentials,chain,completion](Outcome<Credentials> refreshed) {
                if(chain->cancelled()) return;
                if(!refreshed.ok()) {chain->finish([&]{completion(refreshed.error());});return;}
                const auto& value=refreshed.value();
                // A late reply for an earlier identity never replaces the current one.
                auto found=self->signing.find(region);
                if(found!=self->signing.end()&&found->second.identity==identity(value.cred,value.deviceID)&&found->second.active==credentials.signingToken)
                    found->second.active=value.signingToken;
                self->performSigned(region,path,query,value,false,chain,completion);
            });
            chain->replace(std::move(refresh));
        }));
    }
};

HypergryphAccountClient::HypergryphAccountClient(HttpTransport& transport,std::function<Time()> now,EndfieldCardRoute route)
    :impl_(std::make_shared<Impl>(transport,std::move(now),route)) {need(bool(impl_->now),"Account client requires a clock");}
HypergryphAccountClient::~HypergryphAccountClient()=default;
void HypergryphAccountClient::setDiagnostics(std::function<void(const Diagnostic&)> d,std::function<void(DiagnosticEndpoint,std::int64_t,const std::string&)> s) {
    impl_->onDiagnostic=std::move(d);impl_->onServiceMessage=std::move(s);
}
RequestHandle HypergryphAccountClient::refreshCredentials(const std::string& cred,Region region,std::function<void(Outcome<Credentials>)> completion) {
    if(!validSecret(cred)) {impl_->deliver<Credentials>(std::move(completion),fail(ErrorKind::invalidCredentials));return nullptr;}
    impl_->prepareIdentity(cred,region);
    return impl_->refreshSigning(cred,std::nullopt,region,std::move(completion));
}
RequestHandle HypergryphAccountClient::refreshCredentials(const Credentials& credentials,Region region,std::function<void(Outcome<Credentials>)> completion) {
    if(!Impl::valid(credentials)) {impl_->deliver<Credentials>(std::move(completion),fail(ErrorKind::invalidCredentials));return nullptr;}
    auto value=impl_->effective(credentials,region);
    return impl_->refreshSigning(value.cred,value,region,std::move(completion));
}
RequestHandle HypergryphAccountClient::bindings(const Credentials& credentials,Region region,std::function<void(Outcome<std::vector<Role>>)> completion) {
    if(!Impl::valid(credentials)) {impl_->deliver<std::vector<Role>>(std::move(completion),fail(ErrorKind::invalidCredentials));return nullptr;}
    auto self=impl_;
    auto chain=std::make_shared<Chain>(self->transport,[completion]{completion(fail(ErrorKind::cancelled));});
    chain->replace(self->fetchSigned(region,"/api/v1/game/player/binding",{},credentials,[self,chain,credentials,region,completion](EnvelopeOutcome result) {
        if(chain->cancelled()) return;
        if(!result.ok()) {chain->finish([&]{completion(result.error());});return;}
        const auto data=payload(result.value());if(!data.ok()) {chain->finish([&]{completion(data.error());});return;}
        auto roles=decodeBindings(data.value(),region);if(!roles.ok()) {chain->finish([&]{completion(roles.error());});return;}
        const bool endfield=std::any_of(roles.value().begin(),roles.value().end(),[](const Role& r){return r.game==Game::endfield;});
        if(!requiresCommunityUser(self->route)||!endfield) {chain->finish([&]{completion(std::move(roles.value()));});return;}
        auto list=std::move(roles.value());
        chain->replace(self->fetchSigned(region,"/web/v1/user",{},credentials,[chain,list,completion](EnvelopeOutcome result) {
            if(chain->cancelled()) return;
            if(!result.ok()) {chain->finish([&]{completion(result.error());});return;}
            const auto data=payload(result.value());if(!data.ok()) {chain->finish([&]{completion(data.error());});return;}
            const auto& user=data.value()["user"];
            const auto id=user.isObject()?jsonIdentifier(user.contains("id")?user["id"]:user["userId"]):std::nullopt;
            if(!id) {chain->finish([&]{completion(fail(ErrorKind::invalidResponse));});return;}
            auto roles=list;for(auto& r:roles) r.communityUserID=*id;
            chain->finish([&]{completion(std::move(roles));});
        }));
    }));
    return chain;
}
RequestHandle HypergryphAccountClient::profile(const Role& role,const Credentials& credentials,std::function<void(Outcome<Snapshot>)> completion) {
    if(!Impl::valid(credentials)) {impl_->deliver<Snapshot>(std::move(completion),fail(ErrorKind::invalidCredentials));return nullptr;}
    const auto ident=[](const std::optional<std::string>& v){return v&&validIdentifier(*v);};
    const bool community=requiresCommunityUser(impl_->route);
    if(!role.isAvailable||!validIdentifier(role.roleID)||
       !(role.game!=Game::arknights||!role.serverID||ident(role.serverID))||
       !(role.game!=Game::endfield||(ident(role.serverID)&&(!community||ident(role.communityUserID))))) {
        impl_->deliver<Snapshot>(std::move(completion),fail(ErrorKind::invalidRole));return nullptr;
    }
    std::string path;std::vector<std::pair<std::string,std::string>> query;
    if(role.game==Game::endfield) {
        // The official card client carries its identity in the query; the
        // shared game client's sk-game-role header is not configured there.
        path=std::string(endfieldCardPath(impl_->route));
        query={{"roleId",role.roleID},{"serverId",*role.serverID}};
        if(community) query.emplace_back("userId",*role.communityUserID);
    } else {
        path="/api/v1/game/player/info";query={{"uid",role.roleID}};
        if(role.serverID) query.emplace_back("channelMasterId",*role.serverID);
    }
    auto now=impl_->now;
    return impl_->fetchSigned(role.region,std::move(path),std::move(query),credentials,[role,now,completion](EnvelopeOutcome result) {
        if(!result.ok()) {completion(result.error());return;}
        const auto data=payload(result.value());if(!data.ok()) {completion(data.error());return;}
        completion(decodeProfile(data.value(),role,now()));
    });
}
}
