#include "native/audio_session_worker.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace {
using namespace endfield::native;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Fake final:AudioSessionBackend {
    std::vector<AudioSessionRecord>records{{"a","42:100",L"Synthetic app",42,true,true,.4f,0},{"b","42:100",L"Synthetic app",42,true,true,.8f,0}};
    std::vector<std::string>calls;std::int32_t failWrite{};unsigned writes{};Wake wake;
    std::int32_t open(std::wstring_view,Wake w)override{wake=std::move(w);calls.push_back("open");return 0;}
    std::int32_t resume(Wake w)override{wake=std::move(w);calls.push_back("resume");return 0;}
    void pause()noexcept override{wake={};}
    std::int32_t read(std::vector<AudioSessionRecord>&out)override{calls.push_back("read");out=records;return 0;}
    std::int32_t readVolume(std::string_view id,float&v)override{calls.push_back("get:"+std::string(id));for(const auto&r:records)if(r.id==id){if(!r.volume)return -99;v=*r.volume;return 0;}return -98;}
    std::int32_t writeVolume(std::string_view id,float v)override{calls.push_back("set:"+std::string(id));for(auto&r:records)if(r.id==id){r.volume=v;return failWrite&&++writes==static_cast<unsigned>(failWrite)?-77:0;}return -98;}
    void close()noexcept override{wake={};}
};
void transactions(){Fake f;AudioSessionRoutes routes;routes.update(f.records,f);check(routes.applications().size()==1&&routes.applications()[0].state==AudioApplicationRouteState::direct&&!routes.applications()[0].gain,"Direct100% leaves existing native mixer levels untouched");
    check(routes.setGain("42:100",1,f)==0&&f.calls.empty(),"Unity direct gain creates no native route/writes");
    check(routes.setGain("42:100",.5f,f)==0&&f.records[0].volume==.2f&&f.records[1].volume==.4f,"One confirmed process group attenuates every original session relatively");
    check(f.calls.size()>=2&&f.calls[0]=="get:a"&&f.calls[1]=="get:b","Both identity-bound originals read before the first write");
    check(routes.applications()[0].state==AudioApplicationRouteState::active&&routes.applications()[0].gain==.5f,"UI reports owned relative gain rather than guessed native absolute average");
    const auto previous=f.calls.size();check(routes.setGain("42:100",.5f,f)==0&&f.calls.size()==previous,"Unchanged owned gain is no native request");
    f.records.push_back({"c","42:100",L"Synthetic app",42,true,true,.6f,0});routes.update(f.records,f);check(f.records[2].volume==.3f,"New same-process session adopts explicitly owned attenuation");
    f.records[0].active=f.records[1].active=f.records[2].active=false;routes.update(f.records,f);check(routes.applications().size()==1&&routes.applications()[0].state==AudioApplicationRouteState::active,"Owned source route stays visible while app audio is silent");
    check(routes.stop("42:100",f)==0&&f.records[0].volume==.4f&&f.records[1].volume==.8f&&f.records[2].volume==.6f,"Explicit stop restores all remembered independent native mixer originals");
    f.records[0].active=f.records[1].active=f.records[2].active=true;routes.update(f.records,f);routes.setGain("42:100",.5f,f);f.records[0].volume=.9f;routes.update(f.records,f);check(routes.applications()[0].state==AudioApplicationRouteState::failed,"External mixer mutation is detected rather than overwritten");
    check(routes.stopAll(f)==0&&f.records[0].volume==.9f&&f.records[1].volume==.8f,"Cleanup preserves external gain while restoring unchanged owned sessions");
    Fake bad;AudioSessionRoutes rollback;rollback.update(bad.records,bad);bad.failWrite=2;check(rollback.setGain("42:100",.25f,bad)==-77&&bad.records[0].volume==.4f&&bad.records[1].volume==.8f,"Partial native failure rolls attempted sessions back in reverse order");
    check(bad.calls==std::vector<std::string>{"get:a","get:b","set:a","get:a","set:b","set:b","get:b","set:a","get:a"},"Rollback ordering includes the possibly mutating failed native write");
    check(rollback.applications()[0].state==AudioApplicationRouteState::failed&&rollback.applications()[0].gain==.25f,"Failed transaction retains the source requested gain explicitly and has a cleanup path");
    check(rollback.stopAll(bad)==0,"Failed route can safely relinquish restored state");
    auto invalid=bad.records;invalid[1].id=invalid[0].id;bool rejected{};try{rollback.update(invalid,bad);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Duplicate session identities reject before retained metadata mutation");
    check(rollback.setGain("missing",.5f,bad)<0&&rollback.setGain("42:100",2,bad)<0,"Unknown process identity and invalid scalar do not reach native writes");
    invalid=bad.records;invalid[1].pid=43;rejected=false;try{rollback.update(invalid,bad);}catch(...){rejected=true;}check(rejected,"A process key cannot bind two PIDs");
    invalid=bad.records;invalid[0].processKey="99:200";invalid[0].pid=99;rejected=false;try{rollback.update(invalid,bad);}catch(...){rejected=true;}check(rejected&&rollback.applications()[0].id=="42:100","An existing leased session cannot be reassigned to another process before mutation");
}
void pausedPlayer(){Fake f;for(auto&r:f.records)r.active=false;AudioSessionRoutes routes;routes.update(f.records,f);
    check(routes.applications().size()==1&&routes.applications()[0].available&&routes.applications()[0].state==AudioApplicationRouteState::direct,"A paused player (inactive, unexpired sessions) stays listed and adjustable, like the source's remembered output process");
    check(routes.setGain("42:100",.5f,f)==0&&f.records[0].volume==.2f&&f.records[1].volume==.4f,"Paused-player volume applies relatively to every session immediately");
    f.records[1].controllable=false;f.records[1].volume.reset();routes.update(f.records,f);
    check(routes.applications()[0].state==AudioApplicationRouteState::failed,"A session that becomes uncontrollable fails the owned route instead of guessing");
    check(routes.stop("42:100",f)<0&&f.records[0].volume==.4f,"Cleanup restores every readable session and reports the unreadable one");
    AudioSessionRoutes fresh;Fake g;for(auto&r:g.records)r.active=false;g.records[1].controllable=false;g.records[1].volume.reset();fresh.update(g.records,g);
    check(fresh.applications().empty(),"A process with any uncontrollable session is never offered (no partial control)");
}
void exitedProcess(){Fake f;AudioSessionRoutes routes;routes.update(f.records,f);check(routes.setGain("42:100",.5f,f)==0&&f.records[0].volume==.2f,"Owned attenuation before the player quits");
    for(auto&r:f.records){r.exited=true;r.active=false;}routes.update(f.records,f);
    check(f.records[0].volume==.4f&&f.records[1].volume==.8f,"A terminated owned process is restored on its still-held sessions (Windows persists the level; the source tap ended)");
    check(routes.applications().empty(),"A terminated process is never listed");
    f.records.clear();routes.update(f.records,f);check(routes.applications().empty(),"Released sessions leave nothing owned");
    Fake g;AudioSessionRoutes failing;failing.update(g.records,g);failing.setGain("42:100",.5f,g);g.failWrite=1;g.writes=0;for(auto&r:g.records)r.exited=true;failing.update(g.records,g);
    check(failing.applications().size()==1&&failing.applications()[0].state==AudioApplicationRouteState::failed&&!failing.applications()[0].available,"A failed restore of a terminated process is reported, never offered for new attenuation");
    check(failing.setGain("42:100",.3f,g)<0,"A terminated process cannot take a new gain");
    g.records.clear();failing.update(g.records,g);check(failing.applications().empty(),"Its route ends when the backend releases the sessions");
    Fake h;h.records[1].exited=true;AudioSessionRoutes fresh;fresh.update(h.records,h);check(fresh.applications().empty(),"A process with a terminated session is not offered");
}
void appearanceMetadata(){Fake f;AudioSessionRoutes routes;AudioApplicationExecutable e;e.path="C:\\explicit-synthetic\\app.exe";e.identity.objectID[0]=1;e.identity.volumeSerial=9;f.records[0].executable=e;routes.update(f.records,f);
    check(routes.applications()[0].executable==e&&f.calls.empty(),"Captured regular-file appearance metadata flows through existing process grouping without audio writes");
    f.records[1].executable=e;routes.update(f.records,f);check(routes.applications()[0].executable==e,"Matching process sessions share one exact icon source");
    f.records[1].executable->identity.objectID[0]=2;routes.update(f.records,f);check(!routes.applications()[0].executable&&routes.applications()[0].available,"Conflicting process metadata removes icon without disabling valid audio control");
    f.records[1].executable=e;routes.update(f.records,f);const auto previous=routes.applications();auto broken=f.records;broken.back().executable->path="relative.exe";bool rejected{};try{routes.update(broken,f);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&routes.applications()==previous,"Malformed late executable metadata rejects before route mutation");
    broken=f.records;broken[0].processKey.clear();broken[0].pid=0;rejected=false;try{routes.update(broken,f);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Unowned/system session cannot attach guessed executable artwork");
}
struct WorkerState{std::mutex mutex;std::vector<AudioSessionRecord>records{{"a","42:100",L"Fixture",42,true,true,.8f,0}};AudioSessionBackend::Wake wake;std::thread::id backendThread;unsigned reads{},opens{},closes{},pauses{},writes{};bool violation{};};
class ThreadFake final:public AudioSessionBackend {
    std::shared_ptr<WorkerState>s;
    void thread(){if(s->backendThread!=std::this_thread::get_id())s->violation=true;}
public:
    explicit ThreadFake(std::shared_ptr<WorkerState>state):s(std::move(state)){std::lock_guard lock(s->mutex);s->backendThread=std::this_thread::get_id();}
    ~ThreadFake(){std::lock_guard lock(s->mutex);thread();}
    std::int32_t open(std::wstring_view,Wake wake)override{std::lock_guard lock(s->mutex);thread();s->wake=std::move(wake);++s->opens;return 0;}
    std::int32_t resume(Wake wake)override{std::lock_guard lock(s->mutex);thread();s->wake=std::move(wake);return 0;}
    void pause()noexcept override{std::lock_guard lock(s->mutex);thread();s->wake={};++s->pauses;}
    std::int32_t read(std::vector<AudioSessionRecord>&out)override{std::lock_guard lock(s->mutex);thread();out=s->records;++s->reads;return 0;}
    std::int32_t readVolume(std::string_view id,float&out)override{std::lock_guard lock(s->mutex);thread();for(auto&r:s->records)if(r.id==id){out=*r.volume;return 0;}return -1;}
    std::int32_t writeVolume(std::string_view id,float value)override{std::lock_guard lock(s->mutex);thread();for(auto&r:s->records)if(r.id==id){r.volume=value;++s->writes;return 0;}return -1;}
    void close()noexcept override{std::lock_guard lock(s->mutex);thread();s->wake={};++s->closes;}
};
void lifecycle(){auto state=std::make_shared<WorkerState>();std::mutex noticeMutex;std::condition_variable noticed;unsigned notices{};AudioSessionWorker worker([state]{return std::make_unique<ThreadFake>(state);},[&]{std::lock_guard lock(noticeMutex);++notices;noticed.notify_one();});AudioSessionSnapshot snapshot;
    const auto wait=[&](const auto&predicate){std::unique_lock lock(noticeMutex);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);return noticed.wait_until(lock,deadline,[&]{worker.takeSnapshot(snapshot);return predicate(snapshot);});};
    check(worker.activate(L"fixture endpoint")&&wait([](const auto&s){return s.supported&&!s.paused&&s.applications.size()==1;}),"One worker opens injected endpoint and posts first complete typed snapshot");
    {std::lock_guard lock(state->mutex);check(state->backendThread!=std::this_thread::get_id()&&state->opens==1,"Backend construction and all native work stay off the UI thread");}
    check(worker.setGain("42:100",.5f)&&wait([](const auto&s){return s.applications[0].gain==.5f;}),"Explicit app gain is processed by the same retained worker");
    AudioSessionBackend::Wake stale;unsigned reads{};{std::lock_guard lock(state->mutex);stale=state->wake;reads=state->reads;}
    check(worker.pause()&&wait([](const auto&s){return s.paused;}),"Inactive HUD unregisters audio callbacks without resetting user's live attenuation");
    stale();{std::lock_guard lock(state->mutex);check(state->records[0].volume==.4f,"Pause preserves opted-in gain without a hidden mixer write");}
    check(worker.activate(L"fixture endpoint")&&wait([](const auto&s){return !s.paused&&s.applications[0].gain==.5f;}),"Resume performs a fresh event-driven read and preserves explicit route ownership");
    {std::lock_guard lock(state->mutex);check(state->reads>=reads+1,"Resume reads current native state rather than a stale cached scalar");}
    worker.stop();stale();{std::lock_guard lock(state->mutex);check(!state->violation&&state->closes>=1&&state->records[0].volume==.8f,"Terminal stop restores untouched owned original and releases backend on the worker");}
    check(!worker.activate(L"fixture endpoint")&&!worker.setGain("42:100",.5f),"Closed worker rejects stale queued UI commands");
}
}
int main(){try{transactions();pausedPlayer();exitedProcess();appearanceMetadata();lifecycle();std::cout<<"PASS "<<checks<<" synthetic audio-session worker checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
