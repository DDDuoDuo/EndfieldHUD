#include "modules/hypergryph_account_model.hpp"
#include "modules/hypergryph_account_unicode.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <vector>

namespace endfield::modules::hypergryph {
namespace {
constexpr double two63=9223372036854775808.0;
bool asciiDigits(std::string_view s) noexcept {return !s.empty()&&std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';});}
std::string replaceAll(std::string text,char from,char to) {std::replace(text.begin(),text.end(),from,to);return text;}

// splitDisplayName in HypergryphAccountModels.swift. lastIndex(of: "#") works on
// grapheme clusters: a Prepend scalar joins the following "#"; such a "#" is
// never selected and any earlier "#" would leave a non-digit tag.
PersonalIdentity split(const std::optional<std::string>& raw) {
    if(!raw) return {};
    const auto name=trimScalars(replaceAll(replaceAll(*raw,'\n',' '),'\r',' '),UnicodeSet::whitespacesAndNewlines);
    if(name.empty()) return {};
    const auto separator=name.rfind('#');
    if(separator!=std::string::npos) {
        bool joined=false;
        if(separator>0) {
            std::size_t start=separator-1;while(start>0&&(static_cast<unsigned char>(name[start])&0xc0)==0x80) --start;
            const auto previous=decodeUTF8(name,start);joined=previous&&unicodeContains(UnicodeSet::prependBeforeHash,previous->value);
        }
        if(!joined) {
            const auto tag=std::string_view(name).substr(separator+1);
            const auto prefix=trimScalars(std::string_view(name).substr(0,separator),UnicodeSet::whitespaces);
            if(!prefix.empty()&&!tag.empty()&&tag.size()<=10&&asciiDigits(tag)) return {prefix,std::string(tag)};
        }
    }
    return {name,std::nullopt};
}

struct NumberToken {bool negative{},exponent{},fractionNonzero{};std::string integerDigits;};
std::optional<NumberToken> token(const Json& value) {
    if(!value.isNumber()) return std::nullopt;
    const auto text=value.encode(256);NumberToken t;std::size_t at=0;
    if(at<text.size()&&text[at]=='-') {t.negative=true;++at;}
    while(at<text.size()&&text[at]>='0'&&text[at]<='9') t.integerDigits.push_back(text[at++]);
    if(at<text.size()&&text[at]=='.') {++at;while(at<text.size()&&text[at]>='0'&&text[at]<='9') {if(text[at]!='0') t.fractionNonzero=true;++at;}}
    if(at<text.size()&&(text[at]=='e'||text[at]=='E')) t.exponent=true;
    const auto first=t.integerDigits.find_first_not_of('0');
    t.integerDigits=first==std::string::npos?"0":t.integerDigits.substr(first);
    return t;
}
double doubleValue(const Json& value) {return value.number();}
bool integralDouble(double d) noexcept {return std::isfinite(d)&&std::trunc(d)==d;}
// NSJSONSerialization integer-valued token beyond UInt64 becomes an
// NSDecimalNumber: its 128-bit mantissa keeps the leading digits that fit.
std::string decimalDigits(const std::string& digits) {
    std::array<std::uint32_t,5> limbs{}; // little-endian base 2^32, 160 bits for overflow detection
    std::string out;bool truncated=false;
    for(const char c:digits) {
        if(!truncated) {
            auto next=limbs;std::uint64_t carry=static_cast<std::uint64_t>(c-'0');
            for(auto& limb:next) {const auto v=static_cast<std::uint64_t>(limb)*10+carry;limb=static_cast<std::uint32_t>(v);carry=v>>32;}
            if(carry==0&&next[4]==0) {limbs=next;out.push_back(c);continue;}
            truncated=true;
        }
        out.push_back('0');
    }
    return out;
}
bool fitsInt64(const NumberToken& t,std::int64_t& result) noexcept {
    std::uint64_t magnitude{};const auto& d=t.integerDigits;
    const auto converted=std::from_chars(d.data(),d.data()+d.size(),magnitude);
    if(converted.ec!=std::errc{}||converted.ptr!=d.data()+d.size()) return false;
    if(!t.negative) {if(magnitude>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;result=static_cast<std::int64_t>(magnitude);return true;}
    if(magnitude>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())+1) return false;
    result=magnitude==0?0:static_cast<std::int64_t>(0-magnitude);return true;
}
bool hex(char c) noexcept {return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
bool unreserved(unsigned char c) noexcept {return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='.'||c=='_'||c=='~';}
bool subDelimiter(unsigned char c) noexcept {return c=='!'||c=='$'||c=='&'||c=='\''||c=='('||c==')'||c=='*'||c=='+'||c==','||c==';'||c=='=';}
// RFC 3492 Punycode for one label (Foundation's IDNA host conversion).
std::optional<std::string> punycode(std::string_view label) {
    std::vector<char32_t> input;
    for(std::size_t at=0;at<label.size();) {const auto s=decodeUTF8(label,at);if(!s) return std::nullopt;input.push_back(s->value);at+=s->length;}
    constexpr std::uint32_t base=36,tmin=1,tmax=26,skew=38,damp=700;
    const auto digit=[](std::uint32_t d){return static_cast<char>(d<26?'a'+d:'0'+(d-26));};
    const auto adapt=[&](std::uint32_t delta,std::uint32_t points,bool first){delta=first?delta/damp:delta/2;delta+=delta/points;std::uint32_t k=0;
        while(delta>((base-tmin)*tmax)/2) {delta/=base-tmin;k+=base;}return k+(base-tmin+1)*delta/(delta+skew);};
    std::string out;for(const auto c:input) if(c<0x80) out.push_back(static_cast<char>(c));
    const auto basic=static_cast<std::uint32_t>(out.size());std::uint32_t handled=basic;if(basic) out.push_back('-');
    std::uint32_t n=128,delta=0,bias=72;
    while(handled<input.size()) {
        std::uint32_t m=0x10ffff+1;for(const auto c:input) if(c>=n&&c<m) m=c;
        delta+=(m-n)*(handled+1);n=m;
        for(const auto c:input) {
            if(c<n) ++delta;
            if(c==n) {
                std::uint32_t q=delta;
                for(std::uint32_t k=base;;k+=base) {
                    const std::uint32_t t=k<=bias?tmin:k>=bias+tmax?tmax:k-bias;
                    if(q<t) break;
                    out.push_back(digit(t+(q-t)%(base-t)));q=(q-t)/(base-t);
                }
                out.push_back(digit(q));bias=adapt(delta,handled+1,handled==basic);delta=0;++handled;
            }
        }
        ++delta;++n;
    }
    return out;
}
void encodeComponent(std::string& out,std::string_view part,bool queryOrFragment) {
    static constexpr char digits[]="0123456789ABCDEF";
    for(std::size_t i=0;i<part.size();++i) {
        const auto c=static_cast<unsigned char>(part[i]);
        if(c=='%'&&i+2<part.size()&&hex(part[i+1])&&hex(part[i+2])) {out.append(part.substr(i,3));i+=2;continue;}
        if(unreserved(c)||subDelimiter(c)||c==':'||c=='@'||c=='/'||(queryOrFragment&&c=='?')) {out.push_back(static_cast<char>(c));continue;}
        out.push_back('%');out.push_back(digits[c>>4]);out.push_back(digits[c&15]);
    }
}
}

std::string_view regionName(Region r) noexcept {return r==Region::mainland?"mainland":"global";}
std::optional<Region> regionNamed(std::string_view s) noexcept {if(s=="mainland") return Region::mainland;if(s=="global") return Region::global;return std::nullopt;}
std::string_view gameName(Game g) noexcept {return g==Game::endfield?"endfield":"arknights";}
std::optional<Game> gameNamed(std::string_view s) noexcept {if(s=="endfield") return Game::endfield;if(s=="arknights") return Game::arknights;return std::nullopt;}
std::string_view apiHost(Region r) noexcept {return r==Region::mainland?"zonai.skland.com":"zonai.skport.com";}
std::string_view gameOrigin(Region r) noexcept {return r==Region::mainland?"https://game.skland.com":"https://game.skport.com";}
std::string_view communityURL(Region r) noexcept {return r==Region::mainland?"https://www.skland.com":"https://www.skport.com";}

std::string Role::id() const {
    std::string out;bool first=true;
    for(const std::string_view part:{regionName(region),gameName(game),std::string_view(bindingUID),std::string_view(roleID),serverID?std::string_view(*serverID):std::string_view()}) {
        if(!first) out.push_back('|');first=false;out+=std::to_string(part.size());out.push_back(':');out.append(part);
    }
    return out;
}
PersonalIdentity Snapshot::personalIdentity() const {
    const auto card=split(name),binding=split(role.name);
    return {card.name?card.name:binding.name,card.tag?card.tag:binding.tag};
}
std::optional<SanityPresentation> Snapshot::sanityPresentation(Time date,bool refreshing,bool available) const {
    if(!stamina||stamina->current<0||stamina->maximum<=0) return std::nullopt;
    auto current=stamina->current;std::optional<Time> nextLocal,fullLocal;
    const double interval=role.game==Game::endfield?endfieldSecondsPerPoint:arknightsSecondsPerPoint;
    const double elapsed=date-observedAt;
    const Time reference=stamina->serverObservedAt.value_or(observedAt);
    if(current<stamina->maximum&&std::isfinite(elapsed)&&elapsed>=0&&std::isfinite(toUnix(reference))&&stamina->fullRecoveryAt&&
       std::isfinite(toUnix(*stamina->fullRecoveryAt))&&toUnix(*stamina->fullRecoveryAt)>0) {
        const Time full=*stamina->fullRecoveryAt;
        const Time serverNow=reference+elapsed;
        const double remaining=full-serverNow;
        if(remaining<=0) current=stamina->maximum;
        else if(std::isfinite(remaining)) {
            const double missing=std::ceil(remaining/interval);
            const std::int64_t estimate=missing>=static_cast<double>(stamina->maximum)?0:stamina->maximum-static_cast<std::int64_t>(missing);
            current=std::max(current,estimate);
            fullLocal=observedAt+(full-reference);
            const double untilNext=remaining-static_cast<double>(stamina->maximum-current-1)*interval;
            if(std::isfinite(untilNext)&&untilNext>0) nextLocal=date+untilNext;
        }
    }
    return SanityPresentation{role.game,current,stamina->maximum,observedAt,nextLocal,fullLocal,refreshing,available};
}

std::string_view errorName(ErrorKind k) noexcept {
    switch(k) {
        case ErrorKind::invalidCredentials: return "invalidCredentials";case ErrorKind::invalidRole: return "invalidRole";
        case ErrorKind::authenticationExpired: return "authenticationExpired";case ErrorKind::service: return "service";
        case ErrorKind::http: return "http";case ErrorKind::transport: return "transport";case ErrorKind::cancelled: return "cancelled";
        case ErrorKind::responseTooLarge: return "responseTooLarge";case ErrorKind::invalidResponse: return "invalidResponse";
        case ErrorKind::unsafeRedirect: return "unsafeRedirect";
    }
    return "invalidResponse";
}
std::string_view diagnosticName(DiagnosticEndpoint e) noexcept {
    switch(e) {case DiagnosticEndpoint::refresh: return "refresh";case DiagnosticEndpoint::bindings: return "bindings";
        case DiagnosticEndpoint::communityUser: return "community_user";case DiagnosticEndpoint::endfieldProfile: return "endfield_profile";
        case DiagnosticEndpoint::arknightsProfile: return "arknights_profile";}
    return "refresh";
}
std::string_view diagnosticName(DiagnosticReason r) noexcept {
    switch(r) {case DiagnosticReason::device: return "device";case DiagnosticReason::parameters: return "parameters";case DiagnosticReason::role: return "role";
        case DiagnosticReason::permission: return "permission";case DiagnosticReason::identity: return "identity";
        case DiagnosticReason::authentication: return "authentication";case DiagnosticReason::signature: return "signature";case DiagnosticReason::other: return "other";}
    return "other";
}
DiagnosticReason classifyServiceMessage(const Json& value) {
    if(!value.isString()) return DiagnosticReason::other;
    auto text=value.string();if(text.size()>1024) return DiagnosticReason::other;
    for(auto& c:text) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');
    const auto any=[&](std::initializer_list<std::string_view> words){return std::any_of(words.begin(),words.end(),[&](std::string_view w){return text.find(w)!=std::string::npos;});};
    if(any({"设备","設備","device"})) return DiagnosticReason::device;
    if(any({"签名","簽名","signature","invalid sign"})) return DiagnosticReason::signature;
    if(any({"参数","參數","parameter"})) return DiagnosticReason::parameters;
    if(any({"实名","實名","real-name","real name"})) return DiagnosticReason::identity;
    if(any({"权限","權限","授权","授權","permission","forbidden","private","unauthorized"})) return DiagnosticReason::permission;
    if(any({"角色","role","绑定","綁定"})) return DiagnosticReason::role;
    if(any({"登录","登入","登錄","login","credential"})) return DiagnosticReason::authentication;
    return DiagnosticReason::other;
}

std::optional<std::int64_t> swiftInt(std::string_view s) noexcept {
    bool negative=false;if(!s.empty()&&(s.front()=='+'||s.front()=='-')) {negative=s.front()=='-';s.remove_prefix(1);}
    if(!asciiDigits(s)) return std::nullopt;
    std::uint64_t magnitude{};const auto r=std::from_chars(s.data(),s.data()+s.size(),magnitude);
    if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size()) return std::nullopt;
    constexpr auto max=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if(!negative) return magnitude>max?std::nullopt:std::optional<std::int64_t>(static_cast<std::int64_t>(magnitude));
    if(magnitude>max+1) return std::nullopt;
    return magnitude==0?0:static_cast<std::int64_t>(0-magnitude);
}
std::optional<std::int64_t> jsonInteger(const Json& value) {
    if(value.isString()) return swiftInt(value.string());
    const auto t=token(value);if(!t) return std::nullopt;
    if(!t->exponent&&!t->fractionNonzero) {std::int64_t exact{};if(fitsInt64(*t,exact)) return exact;}
    const double d=doubleValue(value);
    if(!integralDouble(d)||d<-two63||d>=two63) return std::nullopt;
    return static_cast<std::int64_t>(d);
}
std::optional<std::int64_t> jsonNonnegative(const Json& value) {const auto v=jsonInteger(value);if(!v||*v<0) return std::nullopt;return v;}
std::optional<Time> jsonTimestamp(const Json& value) {
    const auto v=jsonNonnegative(value);if(!v||*v<=0||*v>=32503680000) return std::nullopt;
    return fromUnix(static_cast<double>(*v));
}
std::optional<std::string> jsonText(const Json& value) {
    if(!value.isString()) return std::nullopt;auto s=value.string();if(s.empty()||s.size()>4096) return std::nullopt;return s;
}
bool validIdentifier(std::string_view s) noexcept {
    if(s.empty()||s.size()>256) return false;
    for(std::size_t at=0;at<s.size();) {
        const auto scalar=decodeUTF8(s,at);if(!scalar) return false;at+=scalar->length;
        const auto v=scalar->value;
        if(v=='-'||v=='_'||v=='.') continue;
        if(!unicodeContains(UnicodeSet::alphanumerics,v)) return false;
    }
    return true;
}
std::optional<std::string> jsonIdentifier(const Json& value) {
    std::string text;
    if(value.isString()) text=value.string();
    else if(const auto t=token(value)) {
        if(!t->exponent&&!t->fractionNonzero) {
            const bool zero=t->integerDigits=="0";
            if(t->negative&&!zero) return std::nullopt;
            {
                std::uint64_t magnitude{};const auto& d=t->integerDigits;
                const auto r=std::from_chars(d.data(),d.data()+d.size(),magnitude);
                text=(r.ec==std::errc{}&&r.ptr==d.data()+d.size())?d:decimalDigits(d);
            }
        } else {
            const double d=doubleValue(value);
            if(!integralDouble(d)||!(d>=0)||d>=two63) return std::nullopt;
            text=std::to_string(static_cast<std::int64_t>(d));
        }
    } else return std::nullopt;
    if(!validIdentifier(text)) return std::nullopt;
    return text;
}
std::optional<std::string> httpsURL(std::string_view s) {
    constexpr std::string_view scheme="https://";
    if(s.substr(0,scheme.size())!=scheme) return std::nullopt;
    const auto rest=s.substr(scheme.size());
    const auto authorityEnd=std::min(rest.find_first_of("/?#"),rest.size());
    const auto authority=rest.substr(0,authorityEnd);
    if(authority.find('@')!=std::string_view::npos) return std::nullopt;
    std::string_view host,port;bool hasPort=false;
    if(!authority.empty()&&authority.front()=='[') {
        const auto close=authority.find(']');if(close==std::string_view::npos) return std::nullopt;
        host=authority.substr(0,close+1);
        const auto inner=host.substr(1,host.size()-2);
        if(inner.empty()||!std::all_of(inner.begin(),inner.end(),[](char c){return hex(c)||c==':'||c=='.';})) return std::nullopt;
        const auto after=authority.substr(close+1);
        if(!after.empty()) {if(after.front()!=':') return std::nullopt;hasPort=true;port=after.substr(1);}
    } else {
        const auto colon=authority.find(':');
        host=authority.substr(0,colon);
        if(colon!=std::string_view::npos) {hasPort=true;port=authority.substr(colon+1);}
        for(std::size_t i=0;i<host.size();++i) {
            const auto c=static_cast<unsigned char>(host[i]);
            if(c>=0x80) continue;
            if(c=='%'&&i+2<host.size()&&hex(host[i+1])&&hex(host[i+2])) {i+=2;continue;}
            if(!(unreserved(c)||subDelimiter(c))) return std::nullopt;
        }
        if(!validUTF8(host)) return std::nullopt;
    }
    if(host.empty()) return std::nullopt;
    if(hasPort&&!port.empty()) {
        if(!asciiDigits(port)) return std::nullopt;
        const auto first=port.find_first_not_of('0');const auto digits=first==std::string_view::npos?std::string_view("0"):port.substr(first);
        if(digits.size()>5) return std::nullopt;
        unsigned value{};std::from_chars(digits.data(),digits.data()+digits.size(),value);
        if(value>65535||value!=443) return std::nullopt;
    } else if(hasPort&&port.empty()) {} // URL.port == nil
    std::string out(scheme);
    if(std::any_of(host.begin(),host.end(),[](char c){return static_cast<unsigned char>(c)>=0x80;})) {
        std::size_t start=0;
        while(true) {
            const auto dot=host.find('.',start);const auto label=host.substr(start,dot==std::string_view::npos?std::string_view::npos:dot-start);
            if(std::any_of(label.begin(),label.end(),[](char c){return static_cast<unsigned char>(c)>=0x80;})) {
                const auto encoded=punycode(label);if(!encoded) return std::nullopt;out+="xn--"+*encoded;
            } else out.append(label);
            if(dot==std::string_view::npos) break;
            out.push_back('.');start=dot+1;
        }
        out.append(authority.substr(host.size()));
    } else out.append(authority);
    const auto tail=rest.substr(authorityEnd);
    const auto queryAt=std::min(tail.find_first_of("?#"),tail.size());
    encodeComponent(out,tail.substr(0,queryAt),false);
    if(queryAt<tail.size()) {
        if(tail[queryAt]=='?') {
            const auto fragmentAt=std::min(tail.find('#',queryAt),tail.size());
            out.push_back('?');encodeComponent(out,tail.substr(queryAt+1,fragmentAt-queryAt-1),true);
            if(fragmentAt<tail.size()) {out.push_back('#');encodeComponent(out,tail.substr(fragmentAt+1),true);}
        } else {out.push_back('#');encodeComponent(out,tail.substr(queryAt+1),true);}
    }
    return out;
}
std::optional<std::string> jsonImageURL(const Json& value) {
    const auto text=jsonText(value);if(!text) return std::nullopt;return httpsURL(*text);
}
bool validSecret(std::string_view value) noexcept {
    return !value.empty()&&value.size()<=4096&&std::all_of(value.begin(),value.end(),[](char c){const auto b=static_cast<unsigned char>(c);return b>=33&&b<=126;});
}
}
