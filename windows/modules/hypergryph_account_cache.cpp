#include "modules/hypergryph_account_cache.hpp"
#include <charconv>
#include <cmath>
#include <limits>

namespace endfield::modules::hypergryph {
namespace {
// JSONDecoder: Bool only from true/false; Int only from integral numbers
// in range; Double from any number; String only from strings.
std::optional<bool> boolean(const Json& v) {if(!v.isBool()) return std::nullopt;return v.boolean();}
std::optional<std::int64_t> integer(const Json& v) {
    if(!v.isNumber()) return std::nullopt;
    const auto token=v.encode(256);std::int64_t value{};
    const auto r=std::from_chars(token.data(),token.data()+token.size(),value);
    if(r.ec==std::errc{}&&r.ptr==token.data()+token.size()) return value;
    const double d=v.number();
    if(!std::isfinite(d)||std::trunc(d)!=d||d< -9223372036854775808.0||d>=9223372036854775808.0) return std::nullopt;
    return static_cast<std::int64_t>(d);
}
std::optional<double> real(const Json& v) {if(!v.isNumber()) return std::nullopt;return v.number();}
std::optional<std::string> text(const Json& v) {if(!v.isString()) return std::nullopt;return v.string();}
// decodeIfPresent: absent or null -> nil; a present value must decode.
template<class T,class F> bool optionalField(const Json& o,std::string_view key,std::optional<T>& out,F decode) {
    if(!o.contains(key)||o[key].isNull()) {out.reset();return true;}
    auto v=decode(o[key]);if(!v) return false;out=std::move(*v);return true;
}
Json number(double v) {return Json(v);}
}
Json encodeRole(const Role& r) {
    Json::Object o{{"region",std::string(regionName(r.region))},{"game",std::string(gameName(r.game))},{"bindingUID",r.bindingUID},
        {"roleID",r.roleID},{"isDefault",r.isDefault},{"isAvailable",r.isAvailable}};
    if(r.serverID) o["serverID"]=*r.serverID;if(r.name) o["name"]=*r.name;if(r.serverName) o["serverName"]=*r.serverName;
    if(r.communityUserID) o["communityUserID"]=*r.communityUserID;
    return o;
}
std::optional<Role> decodeRole(const Json& o) {
    if(!o.isObject()) return std::nullopt;
    const auto region=text(o["region"]),game=text(o["game"]),uid=text(o["bindingUID"]),id=text(o["roleID"]);
    const auto isDefault=boolean(o["isDefault"]),isAvailable=boolean(o["isAvailable"]);
    if(!region||!game||!uid||!id||!isDefault||!isAvailable) return std::nullopt;
    Role r;const auto rg=regionNamed(*region);const auto gm=gameNamed(*game);if(!rg||!gm) return std::nullopt;
    r.region=*rg;r.game=*gm;r.bindingUID=*uid;r.roleID=*id;r.isDefault=*isDefault;r.isAvailable=*isAvailable;
    if(!optionalField(o,"serverID",r.serverID,text)||!optionalField(o,"name",r.name,text)||!optionalField(o,"serverName",r.serverName,text)||
       !optionalField(o,"communityUserID",r.communityUserID,text)) return std::nullopt;
    return r;
}
Json encodeSnapshot(const Snapshot& s) {
    Json::Object o{{"role",encodeRole(s.role)},{"observedAt",number(s.observedAt)}};
    if(s.name) o["name"]=*s.name;if(s.avatarURL) o["avatarURL"]=*s.avatarURL;
    if(s.level) o["level"]=*s.level;if(s.worldLevel) o["worldLevel"]=*s.worldLevel;if(s.experience) o["experience"]=*s.experience;
    if(s.createdAt) o["createdAt"]=number(*s.createdAt);
    if(s.operatorCount) o["operatorCount"]=*s.operatorCount;if(s.weaponCount) o["weaponCount"]=*s.weaponCount;if(s.documentCount) o["documentCount"]=*s.documentCount;
    if(s.stamina) {
        Json::Object st{{"current",s.stamina->current},{"maximum",s.stamina->maximum}};
        if(s.stamina->fullRecoveryAt) st["fullRecoveryAt"]=number(*s.stamina->fullRecoveryAt);
        if(s.stamina->serverObservedAt) st["serverObservedAt"]=number(*s.stamina->serverObservedAt);
        o["stamina"]=std::move(st);
    }
    return o;
}
std::optional<Snapshot> decodeSnapshot(const Json& o) {
    if(!o.isObject()) return std::nullopt;
    const auto role=decodeRole(o["role"]);const auto observed=real(o["observedAt"]);if(!role||!observed) return std::nullopt;
    Snapshot s;s.role=*role;s.observedAt=*observed;
    const auto url=[](const Json& v)->std::optional<std::string>{if(!v.isString()||v.string().empty()) return std::nullopt;return v.string();};
    if(!optionalField(o,"name",s.name,text)||!optionalField(o,"avatarURL",s.avatarURL,url)||!optionalField(o,"level",s.level,integer)||
       !optionalField(o,"worldLevel",s.worldLevel,integer)||!optionalField(o,"experience",s.experience,integer)||!optionalField(o,"createdAt",s.createdAt,real)||
       !optionalField(o,"operatorCount",s.operatorCount,integer)||!optionalField(o,"weaponCount",s.weaponCount,integer)||
       !optionalField(o,"documentCount",s.documentCount,integer)) return std::nullopt;
    if(o.contains("stamina")&&!o["stamina"].isNull()) {
        const auto& st=o["stamina"];if(!st.isObject()) return std::nullopt;
        const auto current=integer(st["current"]),maximum=integer(st["maximum"]);if(!current||!maximum) return std::nullopt;
        Stamina value{*current,*maximum,{},{}};
        if(!optionalField(st,"fullRecoveryAt",value.fullRecoveryAt,real)||!optionalField(st,"serverObservedAt",value.serverObservedAt,real)) return std::nullopt;
        s.stamina=value;
    }
    return s;
}
namespace {
Json encodeRecord(const RegionRecord& r) {
    Json o=r.extra.isObject()?r.extra:Json(Json::Object{});
    o["linked"]=r.linked;o["requiresReconnect"]=r.requiresReconnect;
    Json::Array roles;for(const auto& role:r.roles) roles.push_back(encodeRole(role));o["roles"]=std::move(roles);
    if(r.selectedRoleID) o["selectedRoleID"]=*r.selectedRoleID;else o.erase("selectedRoleID");
    Json::Object snapshots;for(const auto& [id,s]:r.snapshots) snapshots[id]=encodeSnapshot(s);o["snapshots"]=std::move(snapshots);
    if(r.bindingsAt) o["bindingsAt"]=number(*r.bindingsAt);else o.erase("bindingsAt");
    return o;
}
std::optional<RegionRecord> decodeRecord(const Json& o) {
    if(!o.isObject()) return std::nullopt;
    const auto linked=boolean(o["linked"]),reconnect=boolean(o["requiresReconnect"]);
    if(!linked||!reconnect||!o["roles"].isArray()||!o["snapshots"].isObject()) return std::nullopt;
    RegionRecord r;r.linked=*linked;r.requiresReconnect=*reconnect;
    for(const auto& item:o["roles"].array()) {auto role=decodeRole(item);if(!role) return std::nullopt;r.roles.push_back(std::move(*role));}
    if(!optionalField(o,"selectedRoleID",r.selectedRoleID,text)) return std::nullopt;
    for(const auto& [id,item]:o["snapshots"].object()) {auto s=decodeSnapshot(item);if(!s) return std::nullopt;r.snapshots.emplace(id,std::move(*s));}
    if(!optionalField(o,"bindingsAt",r.bindingsAt,real)) return std::nullopt;
    Json extra=Json::Object{};
    for(const auto& [k,v]:o.object()) if(k!="linked"&&k!="requiresReconnect"&&k!="roles"&&k!="selectedRoleID"&&k!="snapshots"&&k!="bindingsAt") extra[k]=v;
    r.extra=std::move(extra);
    return r;
}
}
Json encodeCache(const AccountCache& c) {
    Json o=c.extra.isObject()?c.extra:Json(Json::Object{});
    o["version"]=c.version;o["region"]=std::string(regionName(c.region));o["header"]=c.header;o["syncProfile"]=c.syncProfile;o["syncAvatar"]=c.syncAvatar;
    Json::Object records;for(const auto& [k,r]:c.records) records[k]=encodeRecord(r);o["records"]=std::move(records);
    return o;
}
std::optional<AccountCache> decodeCache(const Json& o) {
    if(!o.isObject()) return std::nullopt;
    const auto version=integer(o["version"]);const auto region=text(o["region"]);const auto header=text(o["header"]);
    const auto syncProfile=boolean(o["syncProfile"]),syncAvatar=boolean(o["syncAvatar"]);
    if(!version||!region||!header||!syncProfile||!syncAvatar||!o["records"].isObject()) return std::nullopt;
    const auto rg=regionNamed(*region);if(!rg||*version!=1) return std::nullopt;
    AccountCache c;c.version=1;c.region=*rg;c.header=*header;c.syncProfile=*syncProfile;c.syncAvatar=*syncAvatar;
    for(const auto& [k,item]:o["records"].object()) {auto r=decodeRecord(item);if(!r) return std::nullopt;c.records.emplace(k,std::move(*r));}
    Json extra=Json::Object{};
    for(const auto& [k,v]:o.object()) if(k!="version"&&k!="region"&&k!="header"&&k!="syncProfile"&&k!="syncAvatar"&&k!="records") extra[k]=v;
    c.extra=std::move(extra);
    return c;
}
std::optional<AccountCache> decodeCacheBytes(std::string_view bytes) {
    if(bytes.size()>maximumCacheBytes) return std::nullopt;
    try {return decodeCache(Json::parse(bytes,maximumCacheBytes));} catch(const std::exception&) {return std::nullopt;}
}
std::string encodeCacheBytes(const AccountCache& c) {return encodeCache(c).encode(maximumCacheBytes);}
std::optional<AccountCache> importMacAccountCache(std::string_view bytes) {
    auto cache=decodeCacheBytes(bytes);if(!cache) return std::nullopt;
    for(auto& [key,record]:cache->records) if(record.linked) record.requiresReconnect=true;
    return cache;
}
}
