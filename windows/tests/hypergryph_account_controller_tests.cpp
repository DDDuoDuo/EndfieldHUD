// Account controller replay of the unchanged Mac HypergryphAccountController
// trace (scripted service, in-memory vault and cache, temporary profile). No
// network, Keychain, real credential, account ID or personal data is used.
#include "modules/hypergryph_account_controller.hpp"
#include "modules/hypergryph_account_identity_sync.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <deque>
#include <iostream>

namespace h=endfield::modules::hypergryph;
using ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool value,const std::string& why) {++checks;if(!value) throw std::runtime_error(why);}
double num(const Json& v) {if(v.isString()) return std::strtod(v.string().c_str(),nullptr);return v.number();}
std::optional<std::string> optText(const Json& v) {if(v.isNull()) return std::nullopt;return v.string();}
Json optJson(const std::optional<std::string>& v) {return v?Json(*v):Json();}

struct FakeRequest:h::AccountRequest {int* cancels;explicit FakeRequest(int* c):cancels(c) {}void cancel() override {++*cancels;}};
struct FakeAPI:h::AccountService {
    struct Pending {std::string kind;std::function<void(const Json&)> resolve;};
    std::deque<Pending> pending;Json::Array calls;int cancels{};std::function<double()> now;
    static Json credentials(const h::Credentials& c) {return Json::Object{{"cred",c.cred},{"signingToken",c.signingToken},{"deviceID",optJson(c.deviceID)}};}
    static std::optional<h::Error> error(const Json& v) {
        if(!v.contains("error")) return std::nullopt;const auto name=v["error"].string();const auto code=v.contains("code")?v["code"].integer():0;
        for(const auto k:{h::ErrorKind::invalidCredentials,h::ErrorKind::invalidRole,h::ErrorKind::authenticationExpired,h::ErrorKind::service,h::ErrorKind::http,
            h::ErrorKind::transport,h::ErrorKind::cancelled,h::ErrorKind::responseTooLarge,h::ErrorKind::invalidResponse,h::ErrorKind::unsafeRedirect})
            if(h::errorName(k)==name) return h::Error{k,code};
        return h::Error{h::ErrorKind::invalidResponse};
    }
    static h::Role role(const Json& v) {
        h::Role r;r.region=*h::regionNamed(v["region"].string());r.game=*h::gameNamed(v["game"].string());r.bindingUID=v["bindingUID"].string();r.roleID=v["roleID"].string();
        r.serverID=optText(v["serverID"]);r.name=optText(v["name"]);r.serverName=optText(v["serverName"]);
        r.isDefault=v.contains("isDefault")&&v["isDefault"].boolean();r.isAvailable=!v.contains("isAvailable")||v["isAvailable"].boolean();return r;
    }
    h::RequestHandle handle() {return std::make_shared<FakeRequest>(&cancels);}
    h::RequestHandle refreshCredentials(const std::string& cred,h::Region region,std::function<void(h::Outcome<h::Credentials>)> done) override {
        calls.push_back(Json::Object{{"kind","refreshCred"},{"cred",cred},{"region",std::string(h::regionName(region))}});
        pending.push_back({"refreshCred",[done](const Json& v){if(auto e=error(v)) {done(*e);return;}const auto& c=v["ok"];done(h::Credentials{c["cred"].string(),c["signingToken"].string(),optText(c["deviceID"])});}});
        return handle();
    }
    h::RequestHandle refreshCredentials(const h::Credentials& creds,h::Region region,std::function<void(h::Outcome<h::Credentials>)> done) override {
        auto call=credentials(creds);call["kind"]="refresh";call["region"]=std::string(h::regionName(region));calls.push_back(call);
        pending.push_back({"refresh",[done](const Json& v){if(auto e=error(v)) {done(*e);return;}const auto& c=v["ok"];done(h::Credentials{c["cred"].string(),c["signingToken"].string(),optText(c["deviceID"])});}});
        return handle();
    }
    h::RequestHandle bindings(const h::Credentials& creds,h::Region region,std::function<void(h::Outcome<std::vector<h::Role>>)> done) override {
        auto call=credentials(creds);call["kind"]="bindings";call["region"]=std::string(h::regionName(region));calls.push_back(call);
        pending.push_back({"bindings",[done](const Json& v){if(auto e=error(v)) {done(*e);return;}std::vector<h::Role> roles;for(const auto& r:v["ok"].array()) roles.push_back(role(r));done(roles);}});
        return handle();
    }
    h::RequestHandle profile(const h::Role& r,const h::Credentials& creds,std::function<void(h::Outcome<h::Snapshot>)> done) override {
        auto call=credentials(creds);call["kind"]="profile";call["roleID"]=r.roleID;call["game"]=std::string(h::gameName(r.game));call["region"]=std::string(h::regionName(r.region));calls.push_back(call);
        auto clock=now;
        pending.push_back({"profile",[done,r,clock](const Json& v){
            if(auto e=error(v)) {done(*e);return;}const auto& s=v["ok"];
            h::Snapshot snapshot;snapshot.role=r;if(s.contains("mismatch")) snapshot.role.roleID="mismatch";snapshot.observedAt=clock();
            snapshot.name=optText(s["name"]);snapshot.avatarURL=optText(s["avatarURL"]);
            const auto i=[](const Json& x){return x.isNull()?std::optional<std::int64_t>{}:std::optional<std::int64_t>(x.integer());};
            snapshot.level=i(s["level"]);snapshot.worldLevel=i(s["worldLevel"]);snapshot.experience=i(s["experience"]);
            if(!s["createdAt"].isNull()) snapshot.createdAt=h::fromUnix(s["createdAt"].number());
            snapshot.operatorCount=i(s["operatorCount"]);snapshot.weaponCount=i(s["weaponCount"]);snapshot.documentCount=i(s["documentCount"]);
            if(s["stamina"].isObject()) {const auto& st=s["stamina"];h::Stamina value{static_cast<std::int64_t>(st["current"].number()),static_cast<std::int64_t>(st["maximum"].number()),{},{}};
                if(!st["fullRecoveryAt"].isNull()) value.fullRecoveryAt=h::fromUnix(st["fullRecoveryAt"].number());
                if(!st["serverObservedAt"].isNull()) value.serverObservedAt=h::fromUnix(st["serverObservedAt"].number());snapshot.stamina=value;}
            done(snapshot);}});
        return handle();
    }
};
struct FakeVault:h::AccountCredentialVault {
    std::map<h::Region,h::Credentials> values;bool failSave{},failRemove{},failLoad{};
    std::optional<h::Credentials> load(h::Region r) override {if(failLoad) throw std::runtime_error("load");const auto f=values.find(r);if(f==values.end()) return std::nullopt;return f->second;}
    void save(const h::Credentials& c,h::Region r) override {if(failSave) throw std::runtime_error("save");values[r]=c;}
    void remove(h::Region r) override {if(failRemove) throw std::runtime_error("remove");values.erase(r);}
    Json stored() const {Json::Object o;for(const auto& [r,c]:values) o[std::string(h::regionName(r))]=FakeAPI::credentials(c);return o;}
};
struct ManualQueue:h::AccountWorkQueue {
    std::deque<std::pair<std::function<void()>,std::function<void(std::exception_ptr)>>> items;
    void run(std::function<void()> work,std::function<void(std::exception_ptr)> done) override {items.emplace_back(std::move(work),std::move(done));}
    void pump() {while(!items.empty()) {auto item=std::move(items.front());items.pop_front();std::exception_ptr error;try {item.first();} catch(...) {error=std::current_exception();}item.second(error);}}
};
struct MemoryFile:h::AccountCacheFile {
    std::optional<std::string> bytes;bool fail{};
    std::optional<std::string> read() override {return bytes;}
    bool exists() override {return bytes.has_value();}
    void write(const std::string& value) override {if(fail) throw std::runtime_error("write");bytes=value;}
};
struct ProfileSink:h::AccountProfileSink {
    ehud::data::Profile profile;bool locked{};int lockChanges{};
    void applyGameProfile(const h::GameProfileUpdate& u) override {h::applyGameProfileUpdate(profile,u);}
    void setProfileSyncLocked(bool v) override {locked=v;++lockChanges;}
    std::optional<std::string> importGameAvatar(const std::vector<std::uint8_t>&) override {throw std::runtime_error("no avatar in trace");}
    Json fields() const {
        return Json::Object{{"name",profile.name},{"tag",profile.tag},{"gamePlayerID",optJson(profile.gamePlayerID)},{"playerIDOverride",optJson(profile.playerIDOverride)},
            {"hasManualAwakeningDate",profile.hasManualAwakeningDate},{"permissionLevel",profile.permissionLevel},{"explorationLevel",profile.explorationLevel},
            {"operatorsCount",profile.operatorsCount},{"weaponsCount",profile.weaponsCount},{"archivesCount",profile.archivesCount}};
    }
};
h::LocalDateFields utc(double reference) {
    const auto seconds=static_cast<std::time_t>(std::floor(h::toUnix(reference)));std::tm t{};
#ifdef _WIN32
    gmtime_s(&t,&seconds);
#else
    gmtime_r(&seconds,&t);
#endif
    return {t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec};
}
// Structural equality with exact numeric values (JSON number tokens may differ).
bool sameJson(const Json& a,const Json& b) {
    if(a.isNumber()&&b.isNumber()) return a.number()==b.number();
    if(a.isArray()!=b.isArray()||a.isObject()!=b.isObject()) return false;
    if(a.isArray()) {if(a.array().size()!=b.array().size()) return false;for(std::size_t i=0;i<a.array().size();++i) if(!sameJson(a.array()[i],b.array()[i])) return false;return true;}
    if(a.isObject()) {if(a.object().size()!=b.object().size()) return false;for(const auto& [k,v]:a.object()) {if(!b.contains(k)||!sameJson(v,b[k])) return false;}return true;}
    return a==b;
}
void replay(const Json& oracle) {
    const auto& trace=oracle["trace"].array();double now=num(oracle["start"]);
    FakeVault vault;ManualQueue queue;MemoryFile file;ProfileSink sink;
    const auto& initial=oracle["initialProfile"];
    sink.profile.name=initial["name"].string();sink.profile.tag=initial["tag"].string();sink.profile.playerIDOverride=optText(initial["playerIDOverride"]);
    sink.profile.gamePlayerID=optText(initial["gamePlayerID"]);sink.profile.hasManualAwakeningDate=initial["hasManualAwakeningDate"].boolean();
    sink.profile.permissionLevel=static_cast<int>(initial["permissionLevel"].integer());sink.profile.explorationLevel=static_cast<int>(initial["explorationLevel"].integer());
    sink.profile.operatorsCount=initial["operatorsCount"].integer();sink.profile.weaponsCount=initial["weaponsCount"].integer();sink.profile.archivesCount=initial["archivesCount"].integer();
    sink.profile.awakeningDate=num(initial["awakeningDate"]);
    std::unique_ptr<FakeAPI> api;std::unique_ptr<h::HypergryphAccountController> controller;std::vector<std::string> events;
    auto language=endfield::core::Language::english;
    std::vector<h::WorkModeGaugeInput> work;
    for(const auto& w:oracle["work"].array()) work.push_back({w["kind"].string()=="countdown",num(w["duration"]),num(w["elapsed"])});
    std::size_t index{};
    for(const auto& entry:trace) {
        const auto& step=entry["step"];const auto op=step["op"].string();const auto label="step "+std::to_string(index++)+" "+op+" "+step.encode().substr(0,120);
        if(op=="new") {
            controller.reset();api=std::make_unique<FakeAPI>();api->now=[&now]{return now;};
            h::AccountControllerOptions options;options.now=[&now]{return now;};options.localDate=utc;options.language=language;options.wording=h::AccountWording::mac;
            options.onEvent=[&](std::string_view e){events.emplace_back(e);};
            controller=std::make_unique<h::HypergryphAccountController>(*api,vault,queue,&file,&sink,nullptr,nullptr,options);
        } else if(op=="visible") controller->setVisible(step["value"].boolean(),step["module"].boolean());
        else if(op=="tick") controller->tick();
        else if(op=="advance") now+=num(step["seconds"]);
        else if(op=="refresh") controller->refresh(step["manual"].boolean());
        else if(op=="disconnect") controller->disconnect();
        else if(op=="accept") controller->acceptLogin(step["cred"].string(),*h::regionNamed(step["region"].string()),optText(step["token"]),optText(step["device"]));
        else if(op=="resolve") {
            check(!api->pending.empty(),label+": a request is pending");auto call=std::move(api->pending.front());api->pending.pop_front();
            check(call.kind==step["kind"].string(),label+": resolves the same request kind ("+call.kind+")");call.resolve(step["result"]);
        } else if(op=="resolve-none") check(static_cast<std::int64_t>(api->pending.size())==step["pending"].integer(),label+": pending count");
        else if(op=="vault") {if(step.contains("failSave")) vault.failSave=step["failSave"].boolean();if(step.contains("failRemove")) vault.failRemove=step["failRemove"].boolean();if(step.contains("failLoad")) vault.failLoad=step["failLoad"].boolean();}
        else if(op=="language") {language=step["value"].string()=="simplifiedChinese"?endfield::core::Language::simplifiedChinese:endfield::core::Language::english;controller->setLanguage(language);}
        else if(op=="perform") {
            const auto action=step["action"].string();h::AccountAction a;
            if(action=="selectHeaderMode") {a.kind=h::AccountAction::Kind::selectHeaderMode;a.headerMode=*h::headerModeNamed(step["value"].string());}
            else if(action=="selectRegion") {a.kind=h::AccountAction::Kind::selectRegion;a.region=step["value"].string()=="china"?h::AccountPresentation::RegionChoice::china:h::AccountPresentation::RegionChoice::global;}
            else if(action=="selectRole") {a.kind=h::AccountAction::Kind::selectRole;a.roleID=step["roleID"].string();}
            else if(action=="setSyncProfile") {a.kind=h::AccountAction::Kind::setSyncProfile;a.enabled=step["value"].boolean();}
            else if(action=="setSyncAvatar") {a.kind=h::AccountAction::Kind::setSyncAvatar;a.enabled=step["value"].boolean();}
            controller->perform(a);
        } else check(false,"unknown oracle op "+op);
        queue.pump();
        const auto& after=entry["after"];const auto p=controller->presentation();
        Json::Array roles;for(const auto& r:p.roles) roles.push_back(Json::Object{{"id",r.id},{"title",r.title},{"subtitle",r.subtitle}});
        const Json presentation=Json::Object{{"region",std::string(h::regionChoiceName(p.region))},{"status",std::string(h::statusName(p.status))},{"statusMessage",p.statusMessage},
            {"accountName",p.accountName},{"roles",roles},{"selectedRoleID",optJson(p.selectedRoleID)},{"headerMode",std::string(h::headerModeName(p.headerMode))},
            {"syncProfile",p.syncProfile},{"syncAvatar",p.syncAvatar},{"lastSync",p.lastSync},{"isLinked",p.isLinked}};
        check(sameJson(presentation,after["presentation"]),label+": presentation "+presentation.encode()+" vs "+after["presentation"].encode());
        for(std::size_t n=0;n<work.size();++n) {
            const auto g=controller->gaugeValue(work[n]);const auto& e=after["gauge"].array()[n].array();
            check(g.value==e[0].string()&&g.accessibilityLabel==e[1].string()&&g.visible==e[2].boolean(),label+": header gauge "+g.value+" | "+g.accessibilityLabel);
        }
        const auto s=controller->sanityPresentation();const auto& es=after["sanity"];
        check(s.has_value()==!es.isNull(),label+": sanity presence");
        if(s) {
            const auto same=[](std::optional<double> a,const Json& b){return a.has_value()==!b.isNull()&&(!a||*a==num(b));};
            check(s->current==es["current"].integer()&&s->maximum==es["maximum"].integer()&&s->observedAt==num(es["observedAt"])&&same(s->nextRecoveryAt,es["next"])&&
                  same(s->fullRecoveryAt,es["full"])&&s->isRefreshing==es["refreshing"].boolean()&&s->refreshAvailable==es["available"].boolean()&&h::gameName(s->game)==es["game"].string(),label+": sanity");
        }
        check(controller->hasActiveRequest()==after["active"].boolean()&&controller->isPresentingAccountPanel()==after["panel"].boolean()&&controller->gameSyncActive()==after["gameSync"].boolean(),label+": request/panel/sync state");
        check(sink.locked==controller->gameSyncActive(),label+": profile sync lock mirrors gameSyncActive");
        check(sameJson(Json(api->calls),after["calls"]),label+": service calls "+Json(api->calls).encode()+" vs "+after["calls"].encode());
        check(api->cancels==after["cancels"].integer(),label+": cancellations");
        Json::Array eventList;for(const auto& e:events) eventList.emplace_back(e);
        check(Json(eventList)==after["events"],label+": events "+Json(eventList).encode());
        check(static_cast<std::int64_t>(api->pending.size())==after["pending"].integer(),label+": pending requests");
        Json expectedProfile=after["profile"];expectedProfile.erase("awakeningDate");
        check(sameJson(sink.fields(),expectedProfile)&&sink.profile.awakeningDate==num(after["profile"]["awakeningDate"]),label+": personal profile "+sink.fields().encode());
        const Json cache=file.bytes?Json::parse(*file.bytes):Json();
        check(sameJson(cache,after["cache"]),label+": cache file "+cache.encode().substr(0,400)+" vs "+after["cache"].encode().substr(0,400));
        check(sameJson(vault.stored(),after["vault"]),label+": vault contents");
        api->calls.clear();api->cancels=0;events.clear();
    }
    check(index>=100,"Controller oracle trace is long");
}
// Behaviour outside the oracle trace, from the Mac source semantics.
void lifecycle() {
    double now=800000000;FakeAPI api;api.now=[&]{return now;};FakeVault vault;ManualQueue queue;MemoryFile file;ProfileSink sink;
    // A rejected/newer existing cache is preserved: in memory only.
    file.bytes=std::string("{\"version\":2}");
    {
        h::AccountControllerOptions o;o.now=[&]{return now;};o.localDate=utc;
        h::HypergryphAccountController c(api,vault,queue,&file,&sink,nullptr,nullptr,o);
        check(!c.cacheWritesAllowed()&&c.cache()==h::AccountCache{},"Unreadable/newer cache stays untouched and the app continues in memory");
        h::AccountAction a;a.kind=h::AccountAction::Kind::selectHeaderMode;a.headerMode=h::AccountPresentation::HeaderMode::hidden;c.perform(a);
        check(file.bytes==std::optional<std::string>("{\"version\":2}"),"Original bytes are never replaced");
        check(c.gaugeValue({true,1500,0}).visible==false,"Hidden header mode removes the gauge");
    }
    // No polling: nothing is scheduled while hidden or unlinked.
    file.bytes.reset();
    h::AccountControllerOptions o;o.now=[&]{return now;};o.localDate=utc;int changes{};o.changed=[&]{++changes;};
    h::HypergryphAccountController c(api,vault,queue,&file,&sink,nullptr,nullptr,o);
    check(!c.nextWakeTime(),"Unlinked hidden controller schedules nothing");
    c.setVisible(true,true);check(!c.nextWakeTime(),"Unlinked visible controller schedules nothing");
    c.acceptLogin("SYNTHETIC-L",h::Region::mainland,"synthetic-t","synthetic-d");queue.pump();
    check(api.pending.size()==1&&!c.nextWakeTime(),"Busy controller schedules nothing while a request is in flight");
    h::Role role;role.region=h::Region::mainland;role.game=h::Game::endfield;role.bindingUID="1";role.roleID="2";role.serverID="3";
    auto bindings=std::move(api.pending.front());api.pending.pop_front();
    bindings.resolve(Json::Object{{"ok",Json::Array{Json::Object{{"region","mainland"},{"game","endfield"},{"bindingUID","1"},{"roleID","2"},{"serverID","3"},{"isDefault",true}}}}});
    auto profile=std::move(api.pending.front());api.pending.pop_front();
    profile.resolve(Json::Object{{"ok",Json::Object{{"stamina",Json::Object{{"current",100},{"maximum",360},{"fullRecoveryAt",h::toUnix(now)+432.0*260-10}}}}}});
    queue.pump();
    const auto start=now;auto wake=c.nextWakeTime();
    check(wake&&*wake==start+5&&!c.sanityPresentation()->refreshAvailable,"First wake enables the 5 s manual-refresh guard, not a per-second poll");
    now=*wake;check(c.sanityPresentation()->refreshAvailable,"Refresh becomes available exactly at the scheduled wake");
    wake=c.nextWakeTime();
    check(wake&&*wake==start+422,"Next wake is the next local recovery point ("+std::to_string(wake?*wake-start:-1)+")");
    now=*wake;check(c.sanityPresentation()->current==101,"Local projection advances exactly at the scheduled wake");
    now+=600;check(c.nextWakeTime()&&*c.nextWakeTime()<=now,"Refresh becomes due after 600 s");
    c.tick();check(api.pending.size()==1,"Due tick issues exactly one refresh request");
    c.setVisible(false,false);check(api.cancels==1&&!c.nextWakeTime(),"Hiding cancels in-flight work and stops scheduling");
    // Sync lock and identity: disabling sync unlocks without restoring old values.
    c.setVisible(true,true);
    h::AccountAction sync;sync.kind=h::AccountAction::Kind::setSyncProfile;sync.enabled=true;c.perform(sync);
    check(sink.locked&&sink.profile.gamePlayerID==std::optional<std::string>("2")&&!sink.profile.playerIDOverride,"Enabling sync locks fields, sets game UID and clears the override");
    sync.enabled=false;c.perform(sync);
    check(!sink.locked&&sink.profile.gamePlayerID==std::optional<std::string>("2"),"Disabling sync unlocks and keeps the imported values");
    // Login presenter path.
    struct Presenter:h::AccountLoginPresenter {
        bool presenting{};std::function<void(std::optional<h::LoginResult>,h::LoginFailure)> done;int cancels{};
        void present(h::Region,std::function<void(std::optional<h::LoginResult>,h::LoginFailure)> c) override {presenting=true;done=std::move(c);}
        void cancel() override {++cancels;if(presenting) {presenting=false;auto d=std::move(done);d(std::nullopt,h::LoginFailure::cancelled);}}
        bool isPresenting() const override {return presenting;}
    } presenter;
    h::HypergryphAccountController withLogin(api,vault,queue,nullptr,nullptr,&presenter,nullptr,o);
    withLogin.setVisible(true,true);
    h::AccountAction connect;connect.kind=h::AccountAction::Kind::connect;connect.region=h::AccountPresentation::RegionChoice::global;withLogin.perform(connect);
    check(presenter.presenting&&withLogin.isPresentingLogin()&&withLogin.presentation().status==h::AccountPresentation::Status::connecting&&withLogin.region()==h::Region::global,"Connect opens the official login for the chosen region");
    withLogin.perform(connect);check(presenter.presenting,"A second Connect while presenting is ignored");
    presenter.presenting=false;auto done=std::move(presenter.done);done(std::nullopt,h::LoginFailure::pageUnavailable);
    check(withLogin.presentation().statusMessage=="Sign-in could not finish. Please try again.","Failed login reports the Mac message");
    withLogin.perform(connect);presenter.presenting=false;done=std::move(presenter.done);
    done(h::LoginResult{h::Region::global,"SYNTHETIC-G","synthetic-gt","synthetic-gd"},h::LoginFailure::cancelled);queue.pump();
    check(withLogin.record().linked&&vault.values.count(h::Region::global),"Successful login commits the scoped credential to the vault and links the region");
    withLogin.perform(connect);check(!presenter.presenting,"Connect is ignored while the first sync is busy (Mac guard)");
    check(!api.pending.empty()&&api.pending.back().kind=="bindings","Login starts the first binding read");
    auto first=std::move(api.pending.back());api.pending.pop_back();first.resolve(Json::Object{{"ok",Json::Array{}}});queue.pump();
    check(withLogin.presentation().statusMessage=="No linked game account found.","Empty binding list reports the Mac message");
    withLogin.perform(connect);check(presenter.presenting,"Reconnect presents again");
    withLogin.setVisible(false,false);check(!presenter.presenting&&presenter.cancels>=1,"Closing the HUD cancels and disposes the login");
}
// Avatar sync (HypergryphAccountController.applyAvatar + profile observer), Mac semantics.
void avatarSync() {
    double now=800000000;FakeAPI api;api.now=[&]{return now;};FakeVault vault;ManualQueue queue;
    struct Loader:h::AccountAvatarLoader {
        std::vector<std::string> urls;std::function<void(std::optional<std::vector<std::uint8_t>>)> pending;int cancels{};
        struct Handle:h::AccountRequest {int* c;explicit Handle(int* v):c(v) {}void cancel() override {++*c;}};
        h::RequestHandle load(const std::string& url,std::function<void(std::optional<std::vector<std::uint8_t>>)> done) override {urls.push_back(url);pending=std::move(done);return std::make_shared<Handle>(&cancels);}
    } loader;
    struct Sink:ProfileSink {int imports{};bool fail{};
        std::optional<std::string> importGameAvatar(const std::vector<std::uint8_t>& png) override {if(fail) throw std::runtime_error("import");++imports;return "avatar-"+std::to_string(imports)+"-"+std::to_string(png.size())+".image";}} sink;
    sink.profile.uid="1234567890";
    h::AccountControllerOptions o;o.now=[&]{return now;};o.localDate=utc;
    h::HypergryphAccountController c(api,vault,queue,nullptr,&sink,nullptr,&loader,o);
    const Json role=Json::Object{{"region","mainland"},{"game","endfield"},{"bindingUID","1"},{"roleID","2"},{"serverID","3"},{"isDefault",true}};
    const std::string url="https://assets.skland.com/avatar/synthetic.png";
    const auto answer=[&](const std::string& avatar){
        while(!api.pending.empty()) {
            auto call=std::move(api.pending.front());api.pending.pop_front();
            if(call.kind=="bindings") call.resolve(Json::Object{{"ok",Json::Array{role}}});
            else if(call.kind=="profile") call.resolve(Json::Object{{"ok",Json::Object{{"name","Endmin#1"},{"avatarURL",avatar}}}});
            else call.resolve(Json::Object{{"ok",Json::Object{{"cred","SYNTHETIC-L"},{"signingToken","synthetic-t2"},{"deviceID","synthetic-d"}}}});
            queue.pump();
        }
    };
    c.setVisible(true,true);c.acceptLogin("SYNTHETIC-L",h::Region::mainland,"synthetic-t","synthetic-d");queue.pump();answer(url);
    check(loader.urls.empty(),"Avatar sync is off by default");
    h::AccountAction avatar;avatar.kind=h::AccountAction::Kind::setSyncAvatar;avatar.enabled=true;c.perform(avatar);
    check(loader.urls==std::vector<std::string>{url},"Enabling avatar sync loads the selected Endfield avatar once");
    auto done=std::move(loader.pending);done(std::vector<std::uint8_t>(10,1));
    check(sink.imports==1&&c.cache().syncAvatar,"Downloaded avatar is imported as the personal avatar");
    c.profileAvatarChanged(std::string("avatar-1-10.image"));check(c.cache().syncAvatar,"The game's own avatar import is not mistaken for a manual edit");
    now+=700;c.tick();answer(url);check(loader.urls.size()==1,"An unchanged avatar URL is not downloaded again");
    now+=700;c.tick();answer("https://assets.skland.com/avatar/next.png");check(loader.urls.size()==2,"A new avatar URL downloads once");
    sink.fail=true;done=std::move(loader.pending);done(std::vector<std::uint8_t>(5,1));
    check(c.presentation().statusMessage=="Game avatar is unavailable.","Import failure reports the Mac message");
    sink.fail=false;now+=700;c.tick();answer("https://assets.skland.com/avatar/third.png");
    avatar.enabled=false;const int before=loader.cancels;c.perform(avatar);
    check(loader.cancels==before+1&&!c.cache().syncAvatar,"Disabling avatar sync cancels the in-flight download");
    done=std::move(loader.pending);done(std::vector<std::uint8_t>(3,1));check(sink.imports==1,"A late avatar after disabling sync is ignored");
    avatar.enabled=true;c.perform(avatar);check(loader.urls.size()==4,"Re-enabling loads the current avatar again");
    c.profileAvatarChanged(std::string("manual.image"));
    check(!c.cache().syncAvatar,"A manual avatar edit disables game avatar sync");
    c.setVisible(false,false);avatar.enabled=true;c.perform(avatar);check(loader.urls.size()==4,"Hidden HUD never starts an avatar download");
}
// A due refresh blocked by the 5 s attempt guard wakes once when the guard
// opens; it never reports a past/now deadline that tick() cannot act on.
void guardWake() {
    double now=800000000;FakeAPI api;api.now=[&]{return now;};FakeVault vault;ManualQueue queue;
    h::AccountControllerOptions o;o.now=[&]{return now;};o.localDate=utc;
    h::HypergryphAccountController c(api,vault,queue,nullptr,nullptr,nullptr,nullptr,o);
    c.setVisible(true,true);c.acceptLogin("SYNTHETIC-L",h::Region::mainland,"synthetic-t","synthetic-d");queue.pump();
    const double start=now;
    auto bindings=std::move(api.pending.front());api.pending.pop_front();
    bindings.resolve(Json::Object{{"ok",Json::Array{Json::Object{{"region","mainland"},{"game","endfield"},{"bindingUID","1"},{"roleID","2"},{"serverID","3"},{"isDefault",true}}}}});
    auto profile=std::move(api.pending.front());api.pending.pop_front();
    profile.resolve(Json::Object{{"ok",Json::Object{{"name","Endmin#1"}}}});queue.pump();
    check(api.pending.empty()&&!c.hasActiveRequest(),"First sync completed");
    now+=1;h::AccountAction header;header.kind=h::AccountAction::Kind::selectHeaderMode;header.headerMode=h::AccountPresentation::HeaderMode::arknights;c.perform(header);
    check(api.pending.empty(),"A header change inside the 5 s guard starts no request");
    auto wake=c.nextWakeTime();
    check(wake&&*wake==start+5,"The blocked refresh wakes exactly when the guard opens (no spin until then)");
    now=*wake;c.tick();
    wake=c.nextWakeTime();
    check(api.pending.empty()&&wake&&*wake==start+600,"After the guard the fresh snapshot defers to the 600 s cadence");
    now=*wake;c.tick();check(api.pending.size()==1&&api.pending.front().kind=="bindings","The 600 s wake issues the next sync (bindings older than 300 s are re-read)");
}
// The vault failure names the Windows credential store (Mac: Keychain).
void vaultWording() {
    double now=800000000;FakeAPI api;api.now=[&]{return now;};FakeVault vault;ManualQueue queue;
    h::AccountControllerOptions o;o.now=[&]{return now;};o.localDate=utc;
    h::HypergryphAccountController c(api,vault,queue,nullptr,nullptr,nullptr,nullptr,o);
    c.setVisible(true,true);vault.failSave=true;c.acceptLogin("SYNTHETIC-L",h::Region::mainland,"synthetic-t","synthetic-d");queue.pump();
    check(c.presentation().statusMessage=="The saved sign-in could not be updated. Try again."&&!c.record().linked&&vault.values.empty(),
          "A failed DPAPI save reports the Windows store, keeps the region unlinked and stores nothing");
    vault.failSave=false;o.language=endfield::core::Language::simplifiedChinese;o.wording=h::AccountWording::mac;
    h::HypergryphAccountController mac(api,vault,queue,nullptr,nullptr,nullptr,nullptr,o);
    mac.setVisible(true,true);vault.failRemove=true;mac.disconnect();queue.pump();
    check(mac.presentation().statusMessage=="无法更新钥匙串，请重试。","Mac wording keeps the Keychain text for oracle replays");
}
void avatarPolicy(const Json& rows) {
    for(const auto& row:rows.array()) check(h::avatarURLAllowed(row["url"].string())==row["allowed"].boolean(),"Avatar host allowlist matches Mac: "+row["url"].string());
}
void identitySync() {
    ehud::data::Profile p;p.uid="1234567890";p.name="Local";p.tag="4321";p.playerIDOverride="MANUAL";p.hasManualAwakeningDate=true;
    h::GameProfileUpdate u;u.gamePlayerID="9007199254740993";u.name="  Very long display name exceeding twenty  ";u.tag="0042";u.awakeningDate=-100;
    u.permissionLevel=99;u.explorationLevel=0;u.operatorsCount=3;
    h::applyGameProfileUpdate(p,u);
    check(p.uid=="1234567890"&&p.gamePlayerID==std::optional<std::string>("9007199254740993")&&!p.playerIDOverride,"Original local UID preserved; decimal game UID never rounded; override cleared");
    check(p.name=="Very long display na"&&p.tag=="0042"&&p.permissionLevel==60&&p.explorationLevel==1&&p.awakeningDate==-100&&!p.hasManualAwakeningDate,"Mac normalization of synced fields");
    check(h::prefixCharacters("e\xCC\x81\xCC\x81xyz",2)=="e\xCC\x81\xCC\x81x"&&h::characterCount("\r\n")==1,"Character prefix keeps combining clusters together");
    h::GameProfileUpdate bad;bad.gamePlayerID="1";bad.tag="12 3";bool rejected{};try {h::applyGameProfileUpdate(p,bad);} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected&&p.tag=="0042","Invalid synced values are rejected without partial changes");
}
void importCache() {
    h::AccountCache c;c.syncProfile=true;h::RegionRecord linked;linked.linked=true;linked.selectedRoleID="x";c.records["mainland"]=linked;
    h::RegionRecord unlinked;c.records["global"]=unlinked;
    auto bytes=h::encodeCacheBytes(c);
    const auto imported=h::importMacAccountCache(bytes);
    check(imported&&imported->records.at("mainland").requiresReconnect&&imported->records.at("mainland").linked&&!imported->records.at("global").requiresReconnect,
          "Mac cache import keeps cached roles but requires a fresh Windows sign-in");
    check(!h::importMacAccountCache("{\"version\":1}")&&!h::importMacAccountCache(std::string(h::maximumCacheBytes+1,' ')),"Incomplete or oversized caches are rejected");
    auto json=h::encodeCache(c);json["futureField"]=Json::Object{{"kept",true}};json["records"]["mainland"]["extra"]=7;
    const auto decoded=h::decodeCache(json);check(decoded&&h::encodeCache(*decoded)==json,"Unknown additive cache fields survive a rewrite");
    check(bytes.find("cred")==std::string::npos&&bytes.find("signingToken")==std::string::npos,"Cache never contains credentials");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Pass tests/fixtures/hypergryph-account-source.json");
        const auto bytes=ehud::data::detail::readFile(argv[1],4*1024*1024);check(bytes.has_value(),"Read bounded account oracle");
        const auto fixture=Json::parse(*bytes,4*1024*1024);
        replay(fixture["controller"]);lifecycle();guardWake();vaultWording();avatarSync();avatarPolicy(fixture["avatar"]);identitySync();importCache();
        std::cout<<"PASS "<<checks<<" Hypergryph account controller checks\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
