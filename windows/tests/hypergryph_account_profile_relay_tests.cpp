// AccountProfileRelay: the account controller's profile sink while the
// Personal Profile owner lives in a later module area. A controller over an
// imported (requiresReconnect) cache applies its Endfield snapshot before any
// profile exists; the relay keeps the latest update and lock and replays them
// on attach, forwards afterwards and never touches a detached sink. Synthetic
// cache, in-memory vault and fake service only: no network, credential or
// personal data.
#include "modules/hypergryph_account_profile_relay.hpp"
#include "modules/hypergryph_account_identity_sync.hpp"
#include <deque>
#include <iostream>
#include <stdexcept>

namespace h=endfield::modules::hypergryph;
namespace {
std::size_t checks{};
void check(bool value,const char* why) {++checks;if(!value) throw std::runtime_error(why);}

struct Sink:h::AccountProfileSink {
    ehud::data::Profile profile;std::optional<bool> locked;int applies{},locks{},imports{};bool failApply{};std::vector<std::string> order;
    void applyGameProfile(const h::GameProfileUpdate& u) override {
        order.push_back("apply");if(failApply) throw std::runtime_error("profile unavailable");h::applyGameProfileUpdate(profile,u);++applies;
    }
    void setProfileSyncLocked(bool v) override {order.push_back("lock");locked=v;++locks;}
    std::optional<std::string> importGameAvatar(const std::vector<std::uint8_t>& png) override {++imports;return "game-"+std::to_string(png.size())+".png";}
};
struct NoService:h::AccountService {
    int calls{};
    h::RequestHandle refreshCredentials(const std::string&,h::Region,std::function<void(h::Outcome<h::Credentials>)>) override {++calls;return {};}
    h::RequestHandle bindings(const h::Credentials&,h::Region,std::function<void(h::Outcome<std::vector<h::Role>>)>) override {++calls;return {};}
    h::RequestHandle profile(const h::Role&,const h::Credentials&,std::function<void(h::Outcome<h::Snapshot>)>) override {++calls;return {};}
};
struct Queue:h::AccountWorkQueue {
    std::deque<std::pair<std::function<void()>,std::function<void(std::exception_ptr)>>> jobs;
    void run(std::function<void()> work,std::function<void(std::exception_ptr)> done) override {jobs.emplace_back(std::move(work),std::move(done));}
};
struct File:h::AccountCacheFile {
    std::optional<std::string> bytes;
    std::optional<std::string> read() override {return bytes;}
    bool exists() override {return bytes.has_value();}
    void write(const std::string& value) override {bytes=value;}
};
// The offline Mac import's Account/profile-cache.json: linked Endfield role,
// cached snapshot, sync on, sign-in required (no credentials).
std::string importedCache(double now) {
    h::Role role;role.region=h::Region::mainland;role.game=h::Game::endfield;role.bindingUID="9";role.roleID="4000500060";role.serverID="1";
    role.name="Endmin#1234";role.isDefault=true;
    h::Snapshot s;s.role=role;s.observedAt=now-100;s.name="Endmin#1234";s.level=41;s.worldLevel=3;s.createdAt=now-86400.0*30;
    s.operatorCount=12;s.weaponCount=20;s.documentCount=100;
    h::RegionRecord record;record.linked=true;record.requiresReconnect=true;record.roles={role};record.selectedRoleID=role.id();record.snapshots[role.id()]=s;
    h::AccountCache cache;cache.region=h::Region::mainland;cache.header="endfield";cache.syncProfile=true;cache.records["mainland"]=record;
    return h::encodeCacheBytes(cache);
}

void relayAlone() {
    h::AccountProfileRelay relay;Sink first,second;
    h::GameProfileUpdate update;update.gamePlayerID="4000500060";update.name="Endmin";update.tag="1234";update.permissionLevel=41;
    relay.applyGameProfile(update);relay.setProfileSyncLocked(true);
    check(relay.pending()==update&&relay.locked()==true&&!relay.attached(),"Before a profile exists the latest update and lock wait");
    bool threw=false;try {(void)relay.importGameAvatar({1,2,3});} catch(const std::runtime_error&) {threw=true;}
    check(threw,"An avatar import without the personal profile fails like an unavailable profile");
    relay.attach(&first);
    check(first.order==std::vector<std::string>{"apply","lock"}&&first.profile.gamePlayerID==std::optional<std::string>("4000500060")&&first.profile.name=="Endmin"&&
          first.locked==true&&!relay.pending(),"Attaching replays the update, then the lock (controller construction order)");
    update.permissionLevel=42;relay.applyGameProfile(update);relay.setProfileSyncLocked(false);
    check(first.applies==2&&first.profile.permissionLevel==42&&first.locked==false&&!relay.pending(),"An attached relay forwards immediately");
    check(relay.importGameAvatar({1,2,3,4})==std::optional<std::string>("game-4.png")&&first.imports==1,"Avatar imports reach the attached profile");
    first.failApply=true;threw=false;try {relay.applyGameProfile(update);} catch(const std::runtime_error&) {threw=true;}
    check(threw,"A failed save propagates to the controller (its Mac status text)");
    relay.attach(nullptr);update.permissionLevel=43;relay.applyGameProfile(update);relay.setProfileSyncLocked(true);
    check(first.applies==2&&first.locks==2&&relay.pending()==update,"A detached relay never touches the old sink");
    second.failApply=true;relay.attach(&second);
    check(relay.replayFailures()==1&&relay.pending()==update&&second.locked==true,"A failed replay keeps the update pending and still applies the lock");
    second.failApply=false;relay.attach(&second);
    check(second.applies==1&&second.profile.permissionLevel==43&&!relay.pending(),"A later attach replays the kept update");
    threw=false;try {relay.attach(&relay);} catch(const std::invalid_argument&) {threw=true;}
    check(threw,"A relay never attaches to itself");
}

void controllerBeforeProfile() {
    const double now=812692800.75;NoService service;h::MemoryCredentialVault vault;Queue queue;File file;file.bytes=importedCache(now);
    h::AccountProfileRelay relay;
    h::AccountControllerOptions o;o.now=[now]{return now;};
    h::HypergryphAccountController controller(service,vault,queue,&file,&relay,nullptr,nullptr,o);
    check(controller.gameSyncActive()&&relay.pending()&&relay.locked()==true,"The controller applies its cached snapshot before the profile area exists");
    // The Personal Profile area attaches later (sorted area construction).
    Sink profile;profile.profile.playerIDOverride="MANUAL";profile.profile.name="Local";relay.attach(&profile);
    check(profile.profile.gamePlayerID==std::optional<std::string>("4000500060")&&!profile.profile.playerIDOverride&&profile.profile.name=="Endmin"&&
          profile.profile.tag=="1234"&&profile.profile.permissionLevel==41&&profile.locked==true,
          "Attach replays the imported snapshot and keeps the profile locked while reconnecting");
    // Turning sync off unlocks without restoring old values.
    h::AccountAction off;off.kind=h::AccountAction::Kind::setSyncProfile;off.enabled=false;controller.perform(off);
    check(profile.locked==false&&profile.profile.name=="Endmin"&&service.calls==0,"Disabling sync only unlocks; no request is sent");
}
}

int main() {
    try {relayAlone();controllerBeforeProfile();std::cout<<"PASS "<<checks<<" account profile relay checks\n";return 0;}
    catch(const std::exception& e) {std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
