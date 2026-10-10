#include "modules/hypergryph_account_controller.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>

namespace endfield::modules::hypergryph {
namespace {
using HeaderMode=AccountPresentation::HeaderMode;
void need(bool value,const char* why) {if(!value) throw std::invalid_argument(why);}
std::string text(core::Language language,std::string_view english,std::string_view chinese) {return core::localized(english,chinese,language);}
std::string key(Region r) {return std::string(regionName(r));}
std::optional<Game> headerGame(HeaderMode mode) {
    if(mode==HeaderMode::endfield) return Game::endfield;
    if(mode==HeaderMode::arknights) return Game::arknights;
    return std::nullopt;
}
bool acceptsCredential(std::string_view value) {return validSecret(value);} // HypergryphAccountLoginPolicy.acceptsCredential
}

std::string_view headerModeName(HeaderMode m) noexcept {
    switch(m) {case HeaderMode::workMode: return "workMode";case HeaderMode::endfield: return "endfield";case HeaderMode::arknights: return "arknights";case HeaderMode::hidden: return "hidden";}
    return "workMode";
}
std::optional<HeaderMode> headerModeNamed(std::string_view s) noexcept {
    for(const auto m:{HeaderMode::workMode,HeaderMode::endfield,HeaderMode::arknights,HeaderMode::hidden}) if(headerModeName(m)==s) return m;
    return std::nullopt;
}
std::string headerModeTitle(HeaderMode m,core::Language l) {
    switch(m) {
        case HeaderMode::workMode: return text(l,"Work Mode","工作模式");
        case HeaderMode::endfield: return text(l,"Arknights: Endfield","明日方舟：终末地");
        case HeaderMode::arknights: return text(l,"Arknights","明日方舟");
        case HeaderMode::hidden: return text(l,"Hidden","隐藏");
    }
    return {};
}
std::string_view regionChoiceName(AccountPresentation::RegionChoice r) noexcept {return r==AccountPresentation::RegionChoice::china?"china":"global";}
std::string regionChoiceTitle(AccountPresentation::RegionChoice r,core::Language l) {
    return r==AccountPresentation::RegionChoice::china?text(l,"China","国服"):text(l,"Global","国际服");
}
std::string_view statusName(AccountPresentation::Status s) noexcept {
    switch(s) {using S=AccountPresentation::Status;case S::disconnected: return "disconnected";case S::connecting: return "connecting";
        case S::connected: return "connected";case S::refreshing: return "refreshing";case S::failed: return "failed";}
    return "disconnected";
}
std::optional<Credentials> MemoryCredentialVault::load(Region r) {const auto f=values_.find(r);if(f==values_.end()) return std::nullopt;return f->second;}
void MemoryCredentialVault::save(const Credentials& c,Region r) {values_[r]=c;}
void MemoryCredentialVault::remove(Region r) {values_.erase(r);}
bool avatarURLAllowed(std::string_view url) {
    // HypergryphAvatarLoader.isAllowedURL: scheme compared case-insensitively,
    // no user info, port nil/443 and an exact allowlisted host (lowercased).
    std::string lower(url.substr(0,std::min<std::size_t>(url.size(),8)));
    for(auto& c:lower) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');
    if(lower!="https://") return false;
    const auto normalized=httpsURL("https://"+std::string(url.substr(8)));if(!normalized) return false;
    const auto rest=std::string_view(*normalized).substr(8);
    const auto authority=rest.substr(0,std::min(rest.find_first_of("/?#"),rest.size()));
    std::string host(authority.substr(0,authority.find(':')));
    for(auto& c:host) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');
    for(const std::string_view allowed:{"bbs.hycdn.cn","assets.skland.com","assets.skport.com","static.skport.com","web-static.hg-cdn.com"}) if(host==allowed) return true;
    return false;
}

struct HypergryphAccountController::Impl:std::enable_shared_from_this<Impl> {
    AccountService& api;AccountCredentialVault& vault;AccountWorkQueue& queue;AccountCacheFile* file;
    AccountProfileSink* profile;AccountLoginPresenter* login;AccountAvatarLoader* avatarLoader;AccountControllerOptions options;
    AccountCache cache;bool writesAllowed{};
    std::map<Region,Credentials> credentials;std::map<Region,Time> credentialDates;
    RequestHandle request;int generation{};
    std::map<Region,int> accountRevisions,pendingDisconnects;int pendingCredentialCommits{};
    bool busy{},visible{},moduleVisible{},manualRefreshPending{},loginActive{};
    std::map<Region,Time> nextRefreshDates,lastAttemptDates;std::map<Region,int> failureCounts;
    std::string statusMessage;
    RequestHandle avatar;std::optional<std::string> avatarURL,knownAvatarFilename;bool applyingGameAvatar{};
    std::optional<bool> lastLock;

    Impl(AccountService& a,AccountCredentialVault& v,AccountWorkQueue& q,AccountCacheFile* f,AccountProfileSink* p,AccountLoginPresenter* l,AccountAvatarLoader* av,AccountControllerOptions o)
        :api(a),vault(v),queue(q),file(f),profile(p),login(l),avatarLoader(av),options(std::move(o)) {}
    Time now() const {return options.now();}
    std::string t(std::string_view english,std::string_view chinese) const {return accountText(english,chinese,options.language,options.wording);}
    Region region() const {return cache.region;}
    const RegionRecord& record() const {static const RegionRecord empty;const auto f=cache.records.find(key(region()));return f==cache.records.end()?empty:f->second;}
    RegionRecord& mutableRecord(Region r) {return cache.records[key(r)];}
    HeaderMode headerMode() const {return headerModeNamed(cache.header).value_or(HeaderMode::workMode);}
    Time nextRefresh() const {const auto f=nextRefreshDates.find(region());return f==nextRefreshDates.end()?distantPast:f->second;}
    Time lastAttempt() const {const auto f=lastAttemptDates.find(region());return f==lastAttemptDates.end()?distantPast:f->second;}
    int failures() const {const auto f=failureCounts.find(region());return f==failureCounts.end()?0:f->second;}
    const Role* selectedRole() const {
        const auto& r=record();if(!r.selectedRoleID) return nullptr;
        for(const auto& role:r.roles) if(role.id()==*r.selectedRoleID) return &role;
        return nullptr;
    }
    const Snapshot* selectedSnapshot() const {
        const auto& r=record();if(!r.selectedRoleID) return nullptr;
        const auto f=r.snapshots.find(*r.selectedRoleID);return f==r.snapshots.end()?nullptr:&f->second;
    }
    bool isPresentingLogin() const {return loginActive&&login&&login->isPresenting();}
    bool hasActiveRequest() const {return busy||pendingDisconnects.count(region());}
    bool gameSyncActive() const {const auto* role=selectedRole();return cache.syncProfile&&record().linked&&role&&role->game==Game::endfield;}
    int advanceRevision(Region r) {const int value=accountRevisions[r]+1;accountRevisions[r]=value;return value;}
    void changed(bool save=false) {
        if(save&&writesAllowed&&file) {
            try {file->write(encodeCacheBytes(cache));}
            catch(const std::exception&) {statusMessage=t("Profile cache could not be saved.","无法保存账户缓存。");}
        }
        const bool lock=gameSyncActive();
        if(profile&&lastLock!=lock) {lastLock=lock;profile->setProfileSyncLocked(lock);}
        if(options.changed) {auto callback=options.changed;callback();}
    }
    void event(std::string_view name) {if(options.onEvent) {auto callback=options.onEvent;callback(name);}}
    void cancelRequests() {
        manualRefreshPending=false;++generation;
        if(auto r=std::move(request)) r->cancel();request.reset();
        if(auto a=std::move(avatar)) a->cancel();avatar.reset();busy=false;
    }
    void fail(const Error& error) {
        manualRefreshPending=false;busy=false;request.reset();
        auto& count=failureCounts[region()];count=std::min(5,count+1);
        nextRefreshDates[region()]=now()+std::min(900.0,60*std::pow(2.0,static_cast<double>(count-1)));
        if(error.kind==ErrorKind::authenticationExpired||error.kind==ErrorKind::invalidCredentials) {
            mutableRecord(region()).requiresReconnect=true;statusMessage=t("Sign in again to continue syncing.","请重新登录以继续同步。");
        } else statusMessage=t("Sync unavailable. Saved data is preserved.","暂时无法同步，已保留上次数据。");
        changed(true);
    }
    std::vector<Role> rolesNeeded() const {
        std::vector<Role> roles;if(const auto* s=selectedRole()) roles.push_back(*s);
        if(const auto game=headerGame(headerMode());game&&std::none_of(roles.begin(),roles.end(),[&](const Role& r){return r.game==*game;})) {
            for(const auto& r:record().roles) if(r.game==*game) {roles.push_back(r);break;}
        }
        return roles;
    }
    void applyProfile() {
        const auto* snapshot=selectedSnapshot();
        if(!cache.syncProfile||!record().linked||!snapshot||snapshot->role.game!=Game::endfield||!profile) return;
        const auto identity=snapshot->personalIdentity();
        GameProfileUpdate update;update.gamePlayerID=snapshot->role.roleID;update.name=identity.name;update.tag=identity.tag;
        update.awakeningDate=snapshot->createdAt;update.permissionLevel=snapshot->level;update.explorationLevel=snapshot->worldLevel;
        update.operatorsCount=snapshot->operatorCount;update.weaponsCount=snapshot->weaponCount;update.archivesCount=snapshot->documentCount;
        try {profile->applyGameProfile(update);} catch(const std::exception&) {statusMessage=t("The personal profile could not be saved.","无法保存个人名片。");}
    }
    void applyAvatar() {
        const auto* snapshot=selectedSnapshot();
        if(!visible||!cache.syncAvatar||!record().linked||!snapshot||snapshot->role.game!=Game::endfield||!snapshot->avatarURL||
           snapshot->avatarURL==avatarURL||!profile||!avatarLoader) return;
        if(auto a=std::move(avatar)) a->cancel();
        const auto url=*snapshot->avatarURL;const int token=generation;std::weak_ptr<Impl> weak=shared_from_this();
        avatar=avatarLoader->load(url,[weak,token,url](std::optional<std::vector<std::uint8_t>> png) {
            auto self=weak.lock();if(!self||self->generation!=token||!self->cache.syncAvatar) return;
            self->avatar.reset();
            if(png&&self->profile) {
                self->applyingGameAvatar=true;
                try {self->knownAvatarFilename=self->profile->importGameAvatar(*png);self->avatarURL=url;}
                catch(const std::exception&) {self->statusMessage=self->t("Game avatar is unavailable.","游戏头像暂不可用。");self->changed();}
                self->applyingGameAvatar=false;
            }
        });
    }
    template<class F> void vaultWork(F work,std::function<void(std::exception_ptr)> done) {queue.run(std::move(work),std::move(done));}
    void commitLoginCredentials(const Credentials& value,Region r,int token,int mutation) {
        // An explicit sign-in may represent another person: old community IDs
        // are never retried with this credential, even when saving fails.
        credentials.erase(r);credentialDates.erase(r);cache.records[key(r)]=RegionRecord{};
        changed(true);
        ++pendingCredentialCommits;
        std::weak_ptr<Impl> weak=shared_from_this();AccountCredentialVault* v=&vault;
        vaultWork([v,value,r]{v->save(value,r);},[weak,value,r,token,mutation](std::exception_ptr error) {
            auto self=weak.lock();if(!self) return;
            self->pendingCredentialCommits=std::max(0,self->pendingCredentialCommits-1);
            if(self->accountRevisions[r]!=mutation) return;
            if(!error) {
                self->credentials[r]=value;self->credentialDates[r]=self->now();
                self->mutableRecord(r).linked=true;self->nextRefreshDates[r]=distantPast;
                self->event("linked");self->changed(true);
                if(self->generation==token&&self->region()==r&&self->visible) self->fetchBindings(value,token);
                else if(self->region()==r) self->busy=false;
            } else if(self->region()==r&&self->generation==token) {
                self->busy=false;self->statusMessage=self->t("Keychain could not be updated. Try again.","无法更新钥匙串，请重试。");self->changed();
            }
        });
    }
    void refreshSigningIfNeeded(const Credentials& c,int token) {
        const auto f=credentialDates.find(region());
        if(f!=credentialDates.end()&&now()-f->second<1200) {fetchBindingsIfNeeded(c,token);return;}
        std::weak_ptr<Impl> weak=shared_from_this();
        request=api.refreshCredentials(c,region(),[weak,token](Outcome<Credentials> result) {
            auto self=weak.lock();if(!self||self->generation!=token) return;
            if(!result.ok()) {self->fail(result.error());return;}
            const auto value=result.value();
            self->saveCredentials(value,self->region(),token,[weak,value,token]{if(auto s=weak.lock()) s->fetchBindingsIfNeeded(value,token);});
        });
    }
    void saveCredentials(const Credentials& value,Region r,int token,std::function<void()> completion) {
        ++pendingCredentialCommits;
        std::weak_ptr<Impl> weak=shared_from_this();AccountCredentialVault* v=&vault;
        vaultWork([v,value,r]{v->save(value,r);},[weak,value,r,token,completion](std::exception_ptr error) {
            auto self=weak.lock();if(!self) return;
            self->pendingCredentialCommits=std::max(0,self->pendingCredentialCommits-1);
            if(self->generation!=token) return;
            if(error) {self->busy=false;self->statusMessage=self->t("Keychain could not be updated. Try again.","无法更新钥匙串，请重试。");self->changed();return;}
            self->credentials[r]=value;self->credentialDates[r]=self->now();completion();
        });
    }
    void fetchBindingsIfNeeded(const Credentials& c,int token) {
        const auto& r=record();
        if(r.bindingsAt&&now()-*r.bindingsAt<300&&!r.roles.empty()) {fetchProfiles(c,token);return;}
        fetchBindings(c,token);
    }
    void fetchBindings(const Credentials& c,int token) {
        std::weak_ptr<Impl> weak=shared_from_this();
        request=api.bindings(c,region(),[weak,c,token](Outcome<std::vector<Role>> result) {
            auto self=weak.lock();if(!self||self->generation!=token) return;
            if(!result.ok()) {self->fail(result.error());return;}
            auto record=self->record();
            record.roles.clear();
            for(const auto& role:result.value()) if(role.region==self->region()&&role.isAvailable&&record.roles.size()<64) record.roles.push_back(role);
            record.bindingsAt=self->now();
            const auto selected=[&](){for(const auto& r:record.roles) if(record.selectedRoleID&&r.id()==*record.selectedRoleID) return true;return false;};
            if(!selected()) {
                std::optional<std::string> choice;
                for(const auto& r:record.roles) if(r.game==Game::endfield&&r.isDefault) {choice=r.id();break;}
                if(!choice) for(const auto& r:record.roles) if(r.game==Game::endfield) {choice=r.id();break;}
                if(!choice&&!record.roles.empty()) choice=record.roles.front().id();
                record.selectedRoleID=choice;
            }
            std::set<std::string> valid;for(const auto& r:record.roles) valid.insert(r.id());
            for(auto it=record.snapshots.begin();it!=record.snapshots.end();) it=valid.count(it->first)?std::next(it):record.snapshots.erase(it);
            self->cache.records[key(self->region())]=std::move(record);self->changed(true);
            self->fetchProfiles(c,token);
        });
    }
    void fetchProfiles(const Credentials& c,int token) {fetchNext(rolesNeeded(),0,c,token);}
    void fetchNext(std::vector<Role> roles,std::size_t index,const Credentials& c,int token) {
        if(index>=roles.size()) {
            busy=false;request.reset();failureCounts[region()]=0;nextRefreshDates[region()]=now()+automaticRefreshInterval;
            if(record().roles.empty()) statusMessage=t("No linked game account found.","未找到已绑定的游戏账户。");
            applyProfile();applyAvatar();
            if(manualRefreshPending&&selectedSnapshot()&&statusMessage.empty()) event("synced");
            manualRefreshPending=false;
            changed(true);return;
        }
        const auto role=roles[index];std::weak_ptr<Impl> weak=shared_from_this();
        request=api.profile(role,c,[weak,roles,index,role,c,token](Outcome<Snapshot> result) {
            auto self=weak.lock();if(!self||self->generation!=token) return;
            if(!result.ok()) {self->fail(result.error());return;}
            if(result.value().role.id()!=role.id()) {self->fail(Error{ErrorKind::invalidResponse});return;}
            self->mutableRecord(self->region()).snapshots[role.id()]=result.value();
            self->fetchNext(roles,index+1,c,token);
        });
    }
    void tick() {
        const auto mode=headerMode();
        if(!visible||!record().linked||record().requiresReconnect||busy||!(moduleVisible||mode==HeaderMode::endfield||mode==HeaderMode::arknights)||now()<nextRefresh()) return;
        refresh(false);
    }
    void refresh(bool manual) {
        if(!visible||!record().linked||busy||record().requiresReconnect||pendingDisconnects.count(region())||now()-lastAttempt()<5) return;
        if(!manual) {
            const auto roles=rolesNeeded();std::vector<Time> dates;
            for(const auto& role:roles) {const auto f=record().snapshots.find(role.id());if(f!=record().snapshots.end()) dates.push_back(f->second.observedAt);}
            if(!dates.empty()&&dates.size()==roles.size()) {
                const auto oldest=*std::min_element(dates.begin(),dates.end());
                if(now()-oldest>=0&&now()-oldest<automaticRefreshInterval) {nextRefreshDates[region()]=oldest+automaticRefreshInterval;return;}
            }
        }
        manualRefreshPending=manual;
        lastAttemptDates[region()]=now();busy=true;statusMessage.clear();const int token=generation;const auto r=region();
        changed();
        if(const auto f=credentials.find(r);f!=credentials.end()) {refreshSigningIfNeeded(f->second,token);return;}
        std::weak_ptr<Impl> weak=shared_from_this();AccountCredentialVault* v=&vault;
        auto loaded=std::make_shared<std::optional<Credentials>>();
        vaultWork([v,r,loaded]{*loaded=v->load(r);},[weak,token,r,loaded](std::exception_ptr error) {
            auto self=weak.lock();if(!self||self->generation!=token) return;
            if(error||!*loaded) {self->fail(Error{ErrorKind::authenticationExpired});return;}
            self->credentials[r]=**loaded;self->refreshSigningIfNeeded(**loaded,token);
        });
    }
    void acceptLogin(const std::string& cred,Region r,std::optional<std::string> signingToken,std::optional<std::string> deviceID) {
        cancelRequests();cache.region=r;
        const int mutation=advanceRevision(r);
        if(pendingDisconnects.erase(r)) {credentials.erase(r);credentialDates.erase(r);cache.records.erase(key(r));}
        lastAttemptDates[region()]=now();busy=true;statusMessage.clear();changed();
        const int token=generation;std::weak_ptr<Impl> weak=shared_from_this();
        if(signingToken&&deviceID) {
            if(!acceptsCredential(cred)||!acceptsCredential(*signingToken)||!acceptsCredential(*deviceID)) {fail(Error{ErrorKind::invalidCredentials});return;}
            // Official login already issued this signing pair; it is used as-is.
            commitLoginCredentials(Credentials{cred,*signingToken,*deviceID},r,token,mutation);
        } else if(!signingToken&&!deviceID) {
            request=api.refreshCredentials(cred,r,[weak,r,token,mutation](Outcome<Credentials> result) {
                auto self=weak.lock();if(!self||self->generation!=token||self->accountRevisions[r]!=mutation) return;
                if(!result.ok()) {self->fail(result.error());return;}
                self->commitLoginCredentials(result.value(),r,token,mutation);
            });
        } else fail(Error{ErrorKind::invalidCredentials});
    }
    void disconnect() {
        const auto r=region();
        if(pendingDisconnects.count(r)) return;
        cancelRequests();
        if(loginActive&&login) {loginActive=false;login->cancel();}
        const int mutation=advanceRevision(r);pendingDisconnects[r]=mutation;
        busy=true;changed();
        std::weak_ptr<Impl> weak=shared_from_this();AccountCredentialVault* v=&vault;
        vaultWork([v,r]{v->remove(r);},[weak,r,mutation](std::exception_ptr error) {
            auto self=weak.lock();if(!self||self->accountRevisions[r]!=mutation) return;
            self->pendingDisconnects.erase(r);
            if(self->region()==r) self->busy=false;
            if(!error) {
                self->credentials.erase(r);self->credentialDates.erase(r);self->cache.records.erase(key(r));
                self->nextRefreshDates.erase(r);self->lastAttemptDates.erase(r);self->failureCounts.erase(r);
                if(self->region()==r) {self->avatarURL.reset();self->statusMessage.clear();}
                self->event("unlinked");self->changed(true);
            } else if(self->region()==r) {
                self->statusMessage=self->t("Keychain could not be updated. Try again.","无法更新钥匙串，请重试。");self->changed();
            }
        });
    }
    void connect(Region r) {
        if(busy||isPresentingLogin()||!login) return;
        cancelRequests();cache.region=r;statusMessage.clear();
        loginActive=true;std::weak_ptr<Impl> weak=shared_from_this();
        login->present(r,[weak](std::optional<LoginResult> result,LoginFailure failure) {
            auto self=weak.lock();if(!self) return;self->loginActive=false;
            if(result) self->acceptLogin(result->cred,result->region,result->signingToken,result->deviceID);
            else {
                if(failure!=LoginFailure::cancelled) self->statusMessage=self->t("Sign-in could not finish. Please try again.","登录未完成，请重试。");
                self->changed();
            }
        });
        changed();
    }
    void perform(const AccountAction& a) {
        using K=AccountAction::Kind;const auto toRegion=[](AccountPresentation::RegionChoice c){return c==AccountPresentation::RegionChoice::china?Region::mainland:Region::global;};
        switch(a.kind) {
            case K::connect: connect(toRegion(a.region));break;
            case K::refresh: refresh(true);break;
            case K::disconnect: disconnect();break;
            case K::selectRegion:
                if(busy) return;cancelRequests();cache.region=toRegion(a.region);
                statusMessage.clear();nextRefreshDates[region()]=distantPast;avatarURL.reset();applyProfile();event("settings");changed(true);tick();break;
            case K::selectRole: {
                const auto& roles=record().roles;
                if(busy||std::none_of(roles.begin(),roles.end(),[&](const Role& r){return r.id()==a.roleID;})) return;
                mutableRecord(region()).selectedRoleID=a.roleID;avatarURL.reset();applyProfile();event("settings");changed(true);
                nextRefreshDates[region()]=distantPast;tick();break;
            }
            case K::selectHeaderMode: cache.header=std::string(headerModeName(a.headerMode));event("settings");changed(true);nextRefreshDates[region()]=distantPast;tick();break;
            case K::setSyncProfile: cache.syncProfile=a.enabled;if(a.enabled) applyProfile();event("settings");changed(true);break;
            case K::setSyncAvatar:
                cache.syncAvatar=a.enabled;avatarURL.reset();
                if(a.enabled) applyAvatar();else {if(auto av=std::move(avatar)) av->cancel();avatar.reset();}
                event("settings");changed(true);break;
        }
    }
};

HypergryphAccountController::HypergryphAccountController(AccountService& api,AccountCredentialVault& vault,AccountWorkQueue& queue,AccountCacheFile* file,
        AccountProfileSink* profile,AccountLoginPresenter* login,AccountAvatarLoader* avatar,AccountControllerOptions options)
    :impl_(std::make_shared<Impl>(api,vault,queue,file,profile,login,avatar,std::move(options))) {
    auto& i=*impl_;need(bool(i.options.now),"Account controller requires the shared HUD clock");
    bool loaded=false;
    if(file) {
        try {
            if(const auto bytes=file->read();bytes&&bytes->size()<=maximumCacheBytes) if(auto saved=decodeCacheBytes(*bytes)) {i.cache=std::move(*saved);loaded=true;}
        } catch(const std::exception&) {}
        if(loaded) i.writesAllowed=true;
        else {try {i.writesAllowed=!file->exists();} catch(const std::exception&) {i.writesAllowed=false;}}
    }
    i.knownAvatarFilename=i.options.initialAvatarFilename;
    // A previously enabled sync stays authoritative after an update: reuse the
    // cached snapshot without a startup network or credential read.
    i.applyProfile();
    const bool lock=i.gameSyncActive();
    if(profile) {i.lastLock=lock;profile->setProfileSyncLocked(lock);}
}
HypergryphAccountController::~HypergryphAccountController() {
    auto& i=*impl_;
    if(auto r=std::move(i.request)) r->cancel();
    if(auto a=std::move(i.avatar)) a->cancel();
    if(i.loginActive&&i.login) {i.loginActive=false;i.login->cancel();}
    i.options.changed={};i.options.onEvent={};
}
void HypergryphAccountController::setLanguage(core::Language l) {impl_->options.language=l;}
void HypergryphAccountController::setVisible(bool value,bool accountModule) {
    auto& i=*impl_;i.moduleVisible=accountModule;
    if(i.visible==value) return;i.visible=value;
    if(!value) {
        ++i.generation;if(auto r=std::move(i.request)) r->cancel();i.request.reset();
        if(auto a=std::move(i.avatar)) a->cancel();i.avatar.reset();i.busy=false;
        if(i.loginActive&&i.login) {i.loginActive=false;i.login->cancel();}
    }
}
void HypergryphAccountController::tick() {impl_->tick();}
std::optional<Time> HypergryphAccountController::nextWakeTime() const {
    const auto& i=*impl_;std::optional<Time> wake;
    const auto mode=i.headerMode();
    // tick() can act only once refresh() accepts it: after nextRefresh and after
    // the 5 s attempt guard (the Mac re-ticks every header-clock second until
    // then; one wake at the guard's end is the event-driven equivalent).
    if(i.visible&&i.record().linked&&!i.record().requiresReconnect&&!i.busy&&(i.moduleVisible||mode==HeaderMode::endfield||mode==HeaderMode::arknights))
        wake=std::max({i.nextRefresh(),i.lastAttempt()+5,i.now()});
    if(i.visible) {
        if(const auto s=sanityPresentation()) {
            if(s->nextRecoveryAt) wake=wake?std::min(*wake,*s->nextRecoveryAt):*s->nextRecoveryAt;
            const auto enable=i.lastAttempt()+5;if(!s->refreshAvailable&&enable>i.now()) wake=wake?std::min(*wake,enable):enable;
        }
    }
    return wake;
}
void HypergryphAccountController::perform(const AccountAction& a) {impl_->perform(a);}
void HypergryphAccountController::acceptLogin(const std::string& cred,Region r,std::optional<std::string> token,std::optional<std::string> device) {impl_->acceptLogin(cred,r,std::move(token),std::move(device));}
void HypergryphAccountController::disconnect() {impl_->disconnect();}
void HypergryphAccountController::refresh(bool manual) {impl_->refresh(manual);}
void HypergryphAccountController::profileAvatarChanged(const std::optional<std::string>& filename) {
    auto& i=*impl_;if(filename==i.knownAvatarFilename) return;i.knownAvatarFilename=filename;
    if(!i.applyingGameAvatar&&i.cache.syncAvatar) {
        i.cache.syncAvatar=false;if(auto a=std::move(i.avatar)) a->cancel();i.avatar.reset();i.avatarURL.reset();i.changed(true);
    }
}
void HypergryphAccountController::cacheWriteFailed() {
    auto& i=*impl_;i.statusMessage=i.t("Profile cache could not be saved.","无法保存账户缓存。");i.changed();
}
AccountPresentation HypergryphAccountController::presentation() const {
    const auto& i=*impl_;AccountPresentation p;
    p.region=i.region()==Region::mainland?AccountPresentation::RegionChoice::china:AccountPresentation::RegionChoice::global;
    p.isLinked=i.record().linked;
    p.statusMessage=i.record().requiresReconnect?i.t("Sign in again to continue syncing.","请重新登录以继续同步。"):i.statusMessage;
    using S=AccountPresentation::Status;
    p.status=i.isPresentingLogin()?S::connecting:i.hasActiveRequest()?S::refreshing:!p.statusMessage.empty()?S::failed:p.isLinked?S::connected:S::disconnected;
    for(const auto& r:i.record().roles) p.roles.push_back({r.id(),std::string(r.game==Game::endfield?"Endfield":"Arknights")+" · "+r.name.value_or(r.roleID),r.serverName.value_or("")});
    p.selectedRoleID=i.record().selectedRoleID;p.headerMode=i.headerMode();p.syncProfile=i.cache.syncProfile;p.syncAvatar=i.cache.syncAvatar;
    const auto* snapshot=i.selectedSnapshot();const auto* role=i.selectedRole();
    p.accountName=snapshot&&snapshot->name?*snapshot->name:role&&role->name?*role->name:"";
    if(snapshot&&i.options.localDate) {
        const auto f=i.options.localDate(snapshot->observedAt);char buffer[32];
        std::snprintf(buffer,sizeof buffer,"%02d/%02d %02d:%02d:%02d",f.month,f.day,f.hour,f.minute,f.second);
        p.lastSync=i.t("Updated ","更新于 ")+buffer;
    }
    return p;
}
std::optional<SanityPresentation> HypergryphAccountController::sanityPresentation(std::optional<Time> at) const {
    const auto& i=*impl_;if(!i.record().linked) return std::nullopt;
    const auto game=headerGame(i.headerMode());if(!game) return std::nullopt;
    const Role* role=nullptr;
    if(const auto* s=i.selectedRole();s&&s->game==*game) role=s;
    else for(const auto& r:i.record().roles) if(r.game==*game&&r.isAvailable) {role=&r;break;}
    if(!role) return std::nullopt;
    const auto f=i.record().snapshots.find(role->id());if(f==i.record().snapshots.end()) return std::nullopt;
    const Time date=at.value_or(i.now());
    return f->second.sanityPresentation(date,i.hasActiveRequest(),
        i.visible&&!i.busy&&!i.record().requiresReconnect&&!i.pendingDisconnects.count(i.region())&&date-i.lastAttempt()>=5);
}
GaugeValue HypergryphAccountController::gaugeValue(const WorkModeGaugeInput& work) const {
    const auto& i=*impl_;const auto mode=i.headerMode();
    if(mode==HeaderMode::hidden) return {};
    if(i.record().linked&&headerGame(mode)) {
        const auto s=sanityPresentation();
        const auto value=s?std::to_string(s->current)+" / "+std::to_string(s->maximum):std::string("— / —");
        return {value,headerModeTitle(mode,i.options.language)+" · "+i.t("Sanity","理智")+" "+value,true};
    }
    const auto clampInt=[](double v)->std::int64_t{if(!(v>0)) return 0;if(v>=9.2e18) return 9200000000000000000;return static_cast<std::int64_t>(v);};
    const double remaining=work.countdown?std::max(0.0,work.duration-work.elapsed):0;
    const auto shown=work.countdown?clampInt(std::ceil(remaining/60)):clampInt(work.elapsed/60);
    const auto total=clampInt(std::ceil(work.duration/60));
    const auto value=std::to_string(shown)+" / "+std::to_string(total);
    return {value,i.t("Work Mode minutes","工作模式分钟")+" "+value,true};
}
const AccountCache& HypergryphAccountController::cache() const noexcept {return impl_->cache;}
Region HypergryphAccountController::region() const noexcept {return impl_->cache.region;}
const RegionRecord& HypergryphAccountController::record() const {return impl_->record();}
AccountPresentation::HeaderMode HypergryphAccountController::headerMode() const noexcept {return impl_->headerMode();}
bool HypergryphAccountController::gameSyncActive() const {return impl_->gameSyncActive();}
bool HypergryphAccountController::hasActiveRequest() const {return impl_->hasActiveRequest();}
bool HypergryphAccountController::isPresentingLogin() const {return impl_->isPresentingLogin();}
bool HypergryphAccountController::isPresentingAccountPanel() const {return impl_->isPresentingLogin()||impl_->pendingCredentialCommits>0;}
bool HypergryphAccountController::cacheWritesAllowed() const noexcept {return impl_->writesAllowed;}
Time HypergryphAccountController::now() const {return impl_->now();}
}
