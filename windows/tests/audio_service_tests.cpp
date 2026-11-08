#include "native/audio_service.hpp"
#include "native/audio_route_journal.hpp"
#include "native/volume_audio_access.hpp"
#include "core/data/data_store.hpp"
#include "native/volume_snapshot.hpp"
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

// Injected endpoint/session backends only: no COM, audio device, mixer,
// process or user file is touched. Journals live in temporary directories.
namespace {
namespace n=endfield::native;namespace fs=std::filesystem;using namespace std::chrono_literals;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
constexpr std::int32_t deviceChanged=static_cast<std::int32_t>(0x8007048Fu),notImplemented=static_cast<std::int32_t>(0x80004001u),corrupt=static_cast<std::int32_t>(0x80070570u);
const std::wstring speaker=L"{0.0.0.00000000}.{speaker}",buds=L"{0.0.0.00000000}.{buds}",usb=L"{0.0.0.00000000}.{usb}",mic=L"{0.0.1.00000000}.{mic}";
const std::string persistent="{0.0.0.00000000}.{speaker}|\\Device\\HarddiskVolume3\\Synthetic\\player.exe%b{00000000-0000-0000-0000-000000000000}";

struct World {
    std::mutex m;std::condition_variable cv;std::thread::id worker;bool violation{};
    std::vector<n::AudioEndpointRecord>outputs{{speaker,L"Speakers 10",L"HDAUDIO",1u},{buds,L"Buds",L"BTHENUM",3u},{usb,L"Speakers 2",L"USB",3u}},inputs{{mic,L"Microphone",L"HDAUDIO",4u}};
    std::wstring defaultOutput{speaker},defaultInput{mic},bound;
    float master{.55f},left{.55f},right{.55f};bool muted{};std::int32_t writeStatus{};
    n::AudioEndpointBackend::Wake topologyWake,volumeWake;bool endpointOpen{};
    unsigned endpointsCreated{},endpointOpens{},endpointCloses{},binds{},volumeWrites{},muteWrites{},channelWrites{};std::vector<float>writtenVolumes;
    bool holdRead{},inRead{};
    std::map<std::wstring,std::vector<n::AudioSessionRecord>>sessions;
    n::AudioSessionBackend::Wake sessionWake;unsigned sessionsCreated{},sessionOpens{},sessionResumes{},sessionPauses{},sessionCloses{},sessionReads{},sessionWrites{};
    std::function<void(std::string_view,float)>beforeSessionWrite;
    // NativeBackend contract: a terminated process's sessions are reported
    // once with `exited`, then released by the next read.
    std::set<std::string>exitReported;bool failSessionWrites{};
    // Default-device selector fake (the IPolicyConfig access): the console
    // role is the default the endpoint backend reports; multimedia and
    // communications are kept beside it. No real default device is touched.
    std::wstring multimediaOutput{speaker},communicationsOutput{speaker},multimediaInput{mic},communicationsInput{mic};
    struct DefaultWrite{std::wstring id;n::AudioDefaultRole role;};std::vector<DefaultWrite>defaultWrites;
    unsigned selectorsCreated{},failDefaultWriteAt{};bool defaultsSupported{true},throwDefaults{},nullDefaults{},failDefaultReads{};
    // Another mixer (or Windows) takes the default over right after a write.
    std::wstring raceDefault;
    std::wstring&role(bool input,n::AudioDefaultRole r){
        if(r==n::AudioDefaultRole::console)return input?defaultInput:defaultOutput;
        if(r==n::AudioDefaultRole::multimedia)return input?multimediaInput:multimediaOutput;
        return input?communicationsInput:communicationsOutput;
    }
    bool listed(bool input,std::wstring_view id){const auto&list=input?inputs:outputs;return std::any_of(list.begin(),list.end(),[&](const auto&r){return r.id==id;});}
    void thread(){const auto id=std::this_thread::get_id();if(worker==std::thread::id{})worker=id;else if(worker!=id)violation=true;}
    World(){
        n::AudioSessionRecord app{"{session-player}","4242:100",L"Player",4242,true,true,.8f,0};app.persistentID=persistent;app.appUserModelID="Synthetic.Player_8wekyb3d8bbwe!App";
        n::AudioSessionRecord other{"{session-other}","77:100",L"Browser",77,true,true,1.f,0};other.persistentID="{other}";
        sessions[speaker]={app,other};
        auto moved=app;moved.id="{session-player-usb}";sessions[usb]={moved};
    }
};
class FakeEndpoints final:public n::AudioEndpointBackend {
    std::shared_ptr<World>w;
public:
    explicit FakeEndpoints(std::shared_ptr<World>world):w(std::move(world)){std::lock_guard l(w->m);w->thread();++w->endpointsCreated;}
    ~FakeEndpoints()override{std::lock_guard l(w->m);w->thread();}
    std::int32_t open(Wake wake)override{std::lock_guard l(w->m);w->thread();w->topologyWake=std::move(wake);w->endpointOpen=true;++w->endpointOpens;return 0;}
    void close()noexcept override{std::lock_guard l(w->m);w->thread();w->topologyWake={};w->volumeWake={};w->bound.clear();if(w->endpointOpen)++w->endpointCloses;w->endpointOpen=false;}
    std::int32_t enumerate(n::AudioFlow flow,std::vector<n::AudioEndpointRecord>&out)override{std::lock_guard l(w->m);w->thread();out=flow==n::AudioFlow::output?w->outputs:w->inputs;return 0;}
    std::int32_t defaultEndpoint(n::AudioFlow flow,std::wstring&out)override{std::lock_guard l(w->m);w->thread();out=flow==n::AudioFlow::output?w->defaultOutput:w->defaultInput;return out.empty()?static_cast<std::int32_t>(0x80070490u):0;}
    std::int32_t bind(std::wstring_view id,Wake wake)override{std::lock_guard l(w->m);w->thread();++w->binds;w->bound=id;w->volumeWake=id.empty()?Wake{}:std::move(wake);return 0;}
    std::int32_t read(n::AudioEndpointVolumeState&v)override{
        std::unique_lock l(w->m);w->thread();
        if(w->holdRead){w->inRead=true;w->cv.notify_all();w->cv.wait(l,[&]{return !w->holdRead;});w->inRead=false;}
        v={};v.scalar=w->master;v.muted=w->muted;v.channels=2;v.left=w->left;v.right=w->right;v.minimumDecibels=-65.25f;v.maximumDecibels=0;v.incrementDecibels=.03125f;v.steps=100;return 0;
    }
    std::int32_t setVolume(float value)override{std::lock_guard l(w->m);w->thread();++w->volumeWrites;if(w->writeStatus<0)return w->writeStatus;w->writtenVolumes.push_back(value);const auto peak=std::max(w->left,w->right);if(peak>0){w->left=w->left/peak*value;w->right=w->right/peak*value;}else w->left=w->right=value;w->master=value;return 0;}
    std::int32_t setMute(bool value)override{std::lock_guard l(w->m);w->thread();++w->muteWrites;w->muted=value;return 0;}
    std::int32_t readChannel(unsigned c,float&v)override{std::lock_guard l(w->m);w->thread();if(c>1)return -1;v=c?w->right:w->left;return 0;}
    std::int32_t writeChannel(unsigned c,float v)override{std::lock_guard l(w->m);w->thread();if(c>1)return -1;++w->channelWrites;(c?w->right:w->left)=v;w->master=std::max(w->left,w->right);return 0;}
};
class FakeSessions final:public n::AudioSessionBackend {
    std::shared_ptr<World>w;std::wstring endpoint;bool open_{};
    n::AudioSessionRecord*find(std::string_view id){auto&list=w->sessions[endpoint];for(auto&r:list)if(r.id==id)return &r;return nullptr;}
public:
    explicit FakeSessions(std::shared_ptr<World>world):w(std::move(world)){std::lock_guard l(w->m);w->thread();++w->sessionsCreated;}
    ~FakeSessions()override{std::lock_guard l(w->m);w->thread();}
    std::int32_t open(std::wstring_view id,Wake wake)override{std::lock_guard l(w->m);w->thread();endpoint=id;open_=true;w->sessionWake=std::move(wake);++w->sessionOpens;return 0;}
    std::int32_t resume(Wake wake)override{std::lock_guard l(w->m);w->thread();w->sessionWake=std::move(wake);++w->sessionResumes;return 0;}
    void pause()noexcept override{std::lock_guard l(w->m);w->thread();w->sessionWake={};++w->sessionPauses;}
    std::int32_t read(std::vector<n::AudioSessionRecord>&out)override{std::lock_guard l(w->m);w->thread();++w->sessionReads;auto&list=w->sessions[endpoint];
        std::erase_if(list,[&](const auto&r){return r.exited&&w->exitReported.contains(r.id);});
        for(const auto&r:list)if(r.exited)w->exitReported.insert(r.id);
        out=list;return 0;}
    std::int32_t readVolume(std::string_view id,float&v)override{std::lock_guard l(w->m);w->thread();const auto*r=find(id);if(!r||!r->volume)return -98;v=*r->volume;return 0;}
    std::int32_t writeVolume(std::string_view id,float v)override{
        std::function<void(std::string_view,float)>hook;{std::lock_guard l(w->m);hook=w->beforeSessionWrite;}
        if(hook)hook(id,v);
        std::lock_guard l(w->m);w->thread();++w->sessionWrites;auto*r=find(id);if(!r)return -98;if(w->failSessionWrites)return -97;r->volume=v;return 0;
    }
    void close()noexcept override{std::lock_guard l(w->m);w->thread();if(open_)++w->sessionCloses;open_=false;w->sessionWake={};}
};

// Every call records the worker thread, like the backends.
n::AudioDefaultDeviceAccess fakeDefaults(std::shared_ptr<World>w){
    n::AudioDefaultDeviceAccess a;
    a.capability=[w]{std::lock_guard l(w->m);w->thread();return n::AudioDefaultDeviceCapability{w->defaultsSupported,10,0,26100,w->defaultsSupported?0:notImplemented};};
    a.validateEndpoint=[w](std::wstring_view id,n::AudioDefaultFlow flow){std::lock_guard l(w->m);w->thread();return w->listed(flow==n::AudioDefaultFlow::input,id)?0:deviceChanged;};
    a.current=[w](n::AudioDefaultFlow flow,n::AudioDefaultRole r,std::wstring&out){std::lock_guard l(w->m);w->thread();if(w->failDefaultReads)return static_cast<std::int32_t>(0x80004005u); // E_FAIL
        out=w->role(flow==n::AudioDefaultFlow::input,r);return 0;};
    a.select=[w](std::wstring_view id,n::AudioDefaultRole r){std::lock_guard l(w->m);w->thread();w->defaultWrites.push_back({std::wstring(id),r});
        if(w->defaultWrites.size()==w->failDefaultWriteAt)return static_cast<std::int32_t>(0x80004005u); // E_FAIL
        w->role(w->listed(true,id),r)=w->raceDefault.empty()?std::wstring(id):w->raceDefault;return 0;};
    return a;
}
std::function<std::unique_ptr<n::AudioDefaultDeviceSelector>()>defaultsFactory(std::shared_ptr<World>w){
    return [w]()->std::unique_ptr<n::AudioDefaultDeviceSelector>{
        bool raise{},none{};{std::lock_guard l(w->m);w->thread();++w->selectorsCreated;raise=w->throwDefaults;none=w->nullDefaults;}
        if(raise)throw std::runtime_error("Synthetic CoCreateInstance failure");if(none)return nullptr;
        return std::make_unique<n::AudioDefaultDeviceSelector>(fakeDefaults(w));};
}
struct Harness {
    std::shared_ptr<World>world=std::make_shared<World>();
    std::mutex nm;std::condition_variable ncv;unsigned notices{};std::thread::id noticeThread;
    std::unique_ptr<n::AudioService>service;
    explicit Harness(std::optional<fs::path>journal={},bool defaults=false){
        n::AudioServiceOptions o;o.endpoints=[w=world]{return std::make_unique<FakeEndpoints>(w);};o.sessions=[w=world]{return std::make_unique<FakeSessions>(w);};
        if(defaults)o.defaults=defaultsFactory(world);
        o.journal=std::move(journal);o.journalFlushSeconds=.05;o.topologySettleSeconds=.05;
        service=std::make_unique<n::AudioService>(std::move(o),[this]{{std::lock_guard l(nm);++notices;noticeThread=std::this_thread::get_id();}ncv.notify_all();});
    }
    template<class P>void until(P&&predicate,const char*what){
        const auto deadline=std::chrono::steady_clock::now()+10s;
        for(;;){
            unsigned seen;{std::lock_guard l(nm);seen=notices;}
            service->drain();if(predicate(service->snapshot()))return;
            std::unique_lock l(nm);
            if(!ncv.wait_until(l,deadline,[&]{return notices!=seen;})){l.unlock();service->drain();if(predicate(service->snapshot()))return;throw std::runtime_error(what);}
        }
    }
    template<class P>void world_until(P&&predicate,const char*what){ // test-only observation of the fake
        const auto deadline=std::chrono::steady_clock::now()+10s;
        for(;;){{std::lock_guard l(world->m);if(predicate(*world))return;}if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error(what);std::this_thread::sleep_for(1ms);}
    }
    void wake(n::AudioEndpointBackend::Wake World::*member){n::AudioEndpointBackend::Wake w;{std::lock_guard l(world->m);w=(*world).*member;}check(bool(w),"Native notification is registered");w();}
};
const n::AudioApplicationRoute*app(const n::AudioEndpointSnapshot&s,std::string_view key){for(const auto&a:s.applications)if(a.id==key)return &a;return nullptr;}
float sessionVolume(World&w,const std::wstring&endpoint,std::string_view id){std::lock_guard l(w.m);for(const auto&r:w.sessions[endpoint])if(r.id==id)return *r.volume;throw std::runtime_error("Missing synthetic session");}

void lifecycle(){
    Harness h;auto&s=*h.service;
    check(!s.stats().workerStarted&&h.world->endpointsCreated==0&&h.world->sessionsCreated==0,"Construction starts no worker and touches no backend");
    check(!s.drain()&&s.snapshot().paused&&!s.snapshot().topologyActive,"Initial snapshot is paused and inert");
    check(s.vote(n::AudioVoter::eventLog,true)==0&&s.stats().workerStarted,"Event Log vote starts the one worker lazily");
    h.until([](const auto&x){return x.topologyActive&&x.topology.size()==4;},"Device-list tier publishes the topology");
    {
        const auto&x=s.snapshot();std::lock_guard l(h.world->m);
        check(h.world->endpointOpens==1&&h.world->binds==0&&h.world->sessionsCreated==0,"Topology tier registers device notifications only (no volume control, no sessions)");
        check(x.paused&&x.applicationsPaused&&!x.available,"Closed-HUD topology watch exposes no controls");
        check(x.outputs.size()==3&&x.outputs[0].name==L"Buds"&&x.outputs[1].name==L"Speakers 2"&&x.outputs[2].name==L"Speakers 10","Outputs follow the source name ordering");
        check(x.outputs[0].headphones&&x.outputs[1].headphones&&!x.outputs[2].headphones,"Headphones from the form factor, whatever the bus");
        check(x.defaultOutputID==speaker&&x.defaultInputID==mic&&x.inputs.size()==1,"Defaults and inputs are read");
    }
    check(h.noticeThread!=std::this_thread::get_id(),"Notice runs on the worker; the owner drains");
    const auto generation=s.snapshot().topologyGeneration;
    {std::lock_guard l(h.world->m);h.world->inputs.push_back({L"{0.0.1.00000000}.{usb-mic}",L"USB Microphone",L"USB",4u});}
    h.wake(&World::topologyWake);
    h.until([&](const auto&x){return x.topologyGeneration==generation+1&&x.topology.size()==5;},"Device add notification refreshes the list without polling");
    n::AudioTopologyRecorder recorder;(void)recorder.receive(s.snapshot().topology);
    {std::lock_guard l(h.world->m);h.world->inputs.back().name.clear();}
    const auto reads=s.stats().topologyReads;h.wake(&World::topologyWake);
    h.world_until([&](World&){return h.service->stats().topologyReads>reads;},"Incomplete read is processed");
    std::this_thread::sleep_for(150ms); // past the settle window
    s.drain();check(s.snapshot().topology.size()==5&&s.snapshot().topologyGeneration==generation+1,"An incomplete read never reports a disconnection");
    {std::lock_guard l(h.world->m);h.world->inputs.pop_back();}
    h.wake(&World::topologyWake);
    h.until([&](const auto&x){return x.topology.size()==4;},"Device removal is observed");
    const auto events=recorder.receive(s.snapshot().topology);
    check(events.size()==1&&!events[0].connected&&events[0].device=="USB Microphone","The Event Log records the removed device label");
    // Source AudioTopologyWatcher coalescing: a device that appears and
    // vanishes within the settle window logs nothing, while the Volume lists
    // follow immediately.
    {
        const auto settled=s.snapshot().topologyGeneration;
        {std::lock_guard l(h.world->m);h.world->inputs.push_back({L"{0.0.1.00000000}.{flap}",L"Flapping Mic",L"USB",4u});}
        h.wake(&World::topologyWake);
        h.until([](const auto&x){return x.inputs.size()==2;},"Volume input list refreshes immediately");
        check(s.snapshot().topologyGeneration==settled,"The Event Log list waits for the burst to settle");
        {std::lock_guard l(h.world->m);h.world->inputs.pop_back();}
        h.wake(&World::topologyWake);
        h.until([](const auto&x){return x.inputs.size()==1;},"Removal reaches the Volume list");
        std::this_thread::sleep_for(150ms);s.drain();
        check(s.snapshot().topologyGeneration==settled&&recorder.receive(s.snapshot().topology).empty(),"A connect/disconnect burst inside the settle window logs no event");
    }

    check(s.vote(n::AudioVoter::volume,true)==0,"Volume vote accepted");
    h.until([](const auto&x){return !x.paused&&x.available&&!x.applicationsPaused&&x.applicationsSupported&&x.applications.size()==2;},"Volume tier binds controls and per-app sessions");
    {
        const auto&x=s.snapshot();
        check(x.volume&&std::abs(*x.volume-.55f)<1e-6f&&x.canSetVolume&&x.muted==false&&x.canSetMute&&x.balance&&std::abs(*x.balance)<1e-6f&&x.canSetBalance,"Endpoint readback becomes the published controls");
        check(x.applications[0].name==L"Browser"&&x.applications[1].name==L"Player"&&x.applications[1].appUserModelID=="Synthetic.Player_8wekyb3d8bbwe!App","Applications are name-ordered and keep the packaged identity");
        std::lock_guard l(h.world->m);check(h.world->bound==speaker&&h.world->sessionOpens==1&&h.world->endpointOpens==1,"One endpoint backend, bound to the default output; sessions opened on it");
    }
    check(!s.setVolume(L"",.5f)&&!s.setVolume(speaker,1.5f)&&!s.setBalance(speaker,-2)&&!s.setApplicationGain(speaker,"",.5f)&&!s.setApplicationGain(L"","4242:100",.5f),"Invalid commands are rejected before the queue");
    check(s.setVolume(buds,.2f),"A stale-endpoint command is accepted for worker validation");
    h.until([](const auto&x){return x.commandError==deviceChanged;},"Stale endpoint reported");
    {std::lock_guard l(h.world->m);check(h.world->volumeWrites==0,"A gesture is never retargeted to a different output");}
    check(s.setVolume(speaker,.3f),"Volume accepted");
    h.until([](const auto&x){return x.volume&&std::abs(*x.volume-.3f)<1e-6f&&x.commandError==0;},"Volume readback published");
    {std::unique_lock l(h.world->m);h.world->holdRead=true;}
    h.wake(&World::volumeWake);
    {std::unique_lock l(h.world->m);h.world->cv.wait(l,[&]{return h.world->inRead;});}
    unsigned writes{};{std::lock_guard l(h.world->m);writes=h.world->volumeWrites;}
    for(const float v:{.1f,.2f,.4f,.7f})check(s.setVolume(speaker,v),"Drag sample queued while the worker is busy");
    {std::lock_guard l(h.world->m);h.world->holdRead=false;}h.world->cv.notify_all();
    h.until([](const auto&x){return x.volume&&std::abs(*x.volume-.7f)<1e-6f;},"Latest drag sample wins");
    {std::lock_guard l(h.world->m);check(h.world->volumeWrites==writes+1&&h.world->writtenVolumes.back()==.7f,"Queued same-kind samples coalesce to one native write (source mailbox)");}
    check(s.setMute(speaker,true)&&s.setBalance(speaker,.5f),"Mute and balance accepted");
    h.until([](const auto&x){return x.muted==true&&x.balance&&std::abs(*x.balance-.5f)<1e-5f;},"Mute and balance readback");
    {std::lock_guard l(h.world->m);check(h.world->channelWrites>=1&&std::abs(h.world->right-.7f)<1e-6f&&std::abs(h.world->left-.35f)<1e-6f,"Balance writes peak-preserving stereo channel levels");}

    const auto player=std::string("4242:100");
    check(s.setApplicationGain(speaker,player,.5f),"Per-app gain accepted");
    h.until([&](const auto&x){const auto*a=app(x,player);return a&&a->state==n::AudioApplicationRouteState::active&&a->gain&&*a->gain==.5f;},"Per-app route becomes active");
    check(std::abs(sessionVolume(*h.world,speaker,"{session-player}")-.4f)<1e-6f,"Session attenuated relative to its original level");
    {std::lock_guard l(h.world->m);h.world->defaultOutput=usb;}
    h.wake(&World::topologyWake);
    h.until([&](const auto&x){return x.defaultOutputID==usb&&!app(x,player)->gain;},"Output change stops owned routes");
    check(!s.snapshot().applicationError,"The routes end quietly, like an explicit stop (source 730f4bb)");
    check(std::abs(sessionVolume(*h.world,speaker,"{session-player}")-.8f)<1e-6f,"The previous output's session is restored to its original level");
    {std::lock_guard l(h.world->m);check(h.world->bound==usb&&h.world->sessionCloses==1&&h.world->sessionOpens==2,"Controls and sessions follow the new default output");}
    // A gain set on the old output races the change: refused like a stale
    // endpoint write (source "The output device changed. Try again").
    check(s.setApplicationGain(speaker,player,.3f),"A gain set on the old output is accepted for worker validation");
    h.until([](const auto&x){return x.commandError==deviceChanged;},"The stale gain reports the device change");
    check(!app(s.snapshot(),player)->gain&&std::abs(sessionVolume(*h.world,usb,"{session-player-usb}")-.8f)<1e-6f,"It never lands on the new output's session");
    check(s.setApplicationGain(usb,player,.25f),"Gain on the new output");
    h.until([&](const auto&x){const auto*a=app(x,player);return a&&a->state==n::AudioApplicationRouteState::active;},"A route starts on the new output");

    check(s.vote(n::AudioVoter::volume,false)==0,"Volume leaves");
    h.until([](const auto&x){return x.paused&&x.applicationsPaused&&!x.available&&x.topologyActive;},"Hidden Volume pauses controls and sessions; topology stays for the Event Log");
    {std::lock_guard l(h.world->m);check(h.world->bound.empty()&&h.world->sessionPauses>=1,"Volume control unbound and session notifications paused");}
    check(std::abs(sessionVolume(*h.world,usb,"{session-player-usb}")-.2f)<1e-6f,"Owned attenuation survives hiding Volume (source)");
    check(s.vote(n::AudioVoter::nowPlaying,true)==0,"Now Playing vote");
    h.until([](const auto&x){return x.paused&&!x.applicationsPaused&&x.applicationsSupported;},"Now Playing activates per-app routes without endpoint controls");
    {std::lock_guard l(h.world->m);check(h.world->bound.empty(),"Now Playing never binds the endpoint volume control");}
    check(s.powerSuspend(),"Suspend accepted");
    h.until([&](const auto&x){const auto*a=app(x,player);return a&&a->state==n::AudioApplicationRouteState::direct;},"System sleep restores owned routes (source)");
    check(std::abs(sessionVolume(*h.world,usb,"{session-player-usb}")-.8f)<1e-6f,"Sleep restored the original level");

    // Idle: once the one-shot deadlines (journal flush, device-list settle)
    // have elapsed, the worker does nothing without an event.
    std::this_thread::sleep_for(200ms);s.drain();const auto idle=s.stats();std::this_thread::sleep_for(150ms);const auto after=s.stats();
    check(after.wakes==idle.wakes&&after.topologyReads==idle.topologyReads&&after.sessionReads==idle.sessionReads&&after.volumeReads==idle.volumeReads,"No polling: the worker sleeps until a notification or command");
    check(s.vote(n::AudioVoter::nowPlaying,true)==1&&s.vote(n::AudioVoter::eventLog,true)==1,"Repeated votes are no-ops");
    check(s.setApplicationGain(usb,player,.5f),"Route before shutdown");
    h.until([&](const auto&x){const auto*a=app(x,player);return a&&a->state==n::AudioApplicationRouteState::active;},"Route active before shutdown");
    s.stop();
    check(std::abs(sessionVolume(*h.world,usb,"{session-player-usb}")-.8f)<1e-6f,"Quit restores every owned session");
    {std::lock_guard l(h.world->m);check(!h.world->endpointOpen&&!h.world->violation,"Shutdown unregisters notifications; every backend call ran on the one worker");}
    check(!s.setVolume(usb,.5f)&&s.vote(n::AudioVoter::volume,true)<0,"A stopped service accepts nothing");
    s.stop();
}
void fixedOutput(){
    Harness h;auto&s=*h.service;s.vote(n::AudioVoter::volume,true);
    h.until([](const auto&x){return x.available&&x.canSetVolume;},"Writable output");
    {std::lock_guard l(h.world->m);h.world->writeStatus=notImplemented;}
    check(s.setVolume(speaker,.2f),"Write queued");
    h.until([](const auto&x){return x.commandError==notImplemented&&!x.canSetVolume&&x.volume;},"Refused write marks a fixed output; value stays visible");
    check(s.setVolume(speaker,.3f),"Second write queued");
    {unsigned writes{};{std::lock_guard l(h.world->m);writes=h.world->volumeWrites;}h.until([](const auto&x){return x.commandError==static_cast<std::int32_t>(0x80070032u);},"Fixed output rejects later writes");std::lock_guard l(h.world->m);check(h.world->volumeWrites==writes,"No further software volume writes reach a fixed output");}
    {std::lock_guard l(h.world->m);h.world->outputs.erase(h.world->outputs.begin());h.world->defaultOutput=usb;}
    h.wake(&World::topologyWake);h.until([](const auto&x){return x.defaultOutputID==usb&&x.canSetVolume;},"Another output is writable again");
    s.stop();
}
struct Temp {fs::path root;Temp(){root=fs::temp_directory_path()/("endfield-audio-service-"+ehud::data::makeUUID());fs::create_directories(root);root=fs::canonical(root);}~Temp(){std::error_code e;fs::remove_all(root,e);}};
std::string read(const fs::path&p){std::ifstream f(p,std::ios::binary);std::stringstream s;s<<f.rdbuf();return s.str();}
void journal(){
    Temp temp;const auto path=temp.root/"Audio"/"RouteJournal.json";
    {
        Harness h(path);auto&s=*h.service;bool durable{},checked{};
        h.world->beforeSessionWrite=[&](std::string_view id,float value){
            if(id!="{session-player}"||value>=.8f||checked)return;checked=true;
            const auto disk=n::AudioRouteJournal::decode(read(path));
            durable=disk&&disk->size()==1&&(*disk)[0].session=="{session-player}"&&(*disk)[0].original==.8f&&(*disk)[0].written==value&&(*disk)[0].persistent==persistent&&(*disk)[0].endpoint==speaker;
        };
        s.vote(n::AudioVoter::nowPlaying,true);
        h.until([](const auto&x){return x.applicationsSupported&&x.applications.size()==2;},"Sessions listed");
        check(s.setApplicationGain(speaker,"4242:100",.5f),"Gain accepted");
        h.until([](const auto&x){const auto*a=app(x,"4242:100");return a&&a->state==n::AudioApplicationRouteState::active;},"Route active");
        check(checked&&durable,"The recovery entry is durable on disk BEFORE the first attenuating mixer write");
        for(const float v:{.4f,.3f,.2f})s.setApplicationGain(speaker,"4242:100",v);
        h.until([](const auto&x){const auto*a=app(x,"4242:100");return a&&a->gain&&*a->gain==.2f;},"Drag settles");
        h.world_until([&](World&){const auto d=n::AudioRouteJournal::decode(read(path));return d&&d->size()==1&&std::abs((*d)[0].written-.16f)<1e-6f;},"Coalesced journal flush records the latest written level");
        check(s.stopApplication("4242:100"),"Restore accepted");
        h.until([](const auto&x){const auto*a=app(x,"4242:100");return a&&a->state==n::AudioApplicationRouteState::direct;},"Route restored");
        h.world_until([&](World&){const auto d=n::AudioRouteJournal::decode(read(path));return d&&d->empty();},"Restoring releases the journal entry");
        s.stop();
    }
    {
        // A previous run crashed with two attenuated sessions and one the user
        // later changed. Only an unchanged level is restored.
        std::vector<n::AudioRouteJournalEntry>crashed{{speaker,"{old-instance}",persistent,"4242:1",.8f,.25f},{speaker,"{old-other}","{other}","77:1",.9f,.45f},{usb,"{old-usb}","{usb-only}","9:1",.7f,.35f}};
        std::error_code e;fs::remove(path,e);fs::create_directories(path.parent_path());{std::ofstream f(path,std::ios::binary);f<<n::AudioRouteJournal::encode(crashed);}
        Harness h(path);auto&s=*h.service;
        {std::lock_guard l(h.world->m);h.world->sessions[speaker][0].volume=.25f;h.world->sessions[speaker][1].volume=.6f;
            n::AudioSessionRecord usbApp{"{usb-session}","9:2",L"Usb App",9,true,true,.35f,0};usbApp.persistentID="{usb-only}";h.world->sessions[usb].push_back(usbApp);}
        check(s.recover(),"Startup recovery accepted");
        h.world_until([&](World&){const auto d=n::AudioRouteJournal::decode(read(path));return d&&d->empty();},"Recovered entries are settled");
        check(std::abs(sessionVolume(*h.world,speaker,"{session-player}")-.8f)<1e-6f,"A crash-left attenuation is restored (persistent session identity, unchanged level)");
        check(std::abs(sessionVolume(*h.world,speaker,"{session-other}")-.6f)<1e-6f,"A level the user changed after the crash is never overwritten");
        check(std::abs(sessionVolume(*h.world,usb,"{usb-session}")-.7f)<1e-6f,"Recovery visits a journaled non-default endpoint once");
        {std::lock_guard l(h.world->m);check(h.world->endpointsCreated==0&&!h.world->sessionWake,"Recovery opens only the journaled session managers, then closes them");}
        s.stop();
    }
    {
        std::error_code e;fs::remove(path,e);{std::ofstream f(path,std::ios::binary);f<<"{corrupt";}
        Harness h(path);auto&s=*h.service;s.vote(n::AudioVoter::nowPlaying,true);
        h.until([](const auto&x){return x.applicationsSupported&&x.applicationError==corrupt;},"An unreadable journal is surfaced");
        check(s.setApplicationGain(speaker,"4242:100",.5f),"Gain accepted for validation");
        h.until([](const auto&x){const auto*a=app(x,"4242:100");return a&&a->state==n::AudioApplicationRouteState::failed;},"Attenuation without a durable record fails");
        check(std::abs(sessionVolume(*h.world,speaker,"{session-player}")-.8f)<1e-6f&&read(path)=="{corrupt","The mixer and the damaged journal are both left untouched");
        s.stop();
    }
}
void processExit(){
    Temp temp;const auto path=temp.root/"Audio"/"RouteJournal.json";Harness h(path);auto&s=*h.service;
    const auto journalEmpty=[&]{const auto d=n::AudioRouteJournal::decode(read(path));return d&&d->empty();};
    s.vote(n::AudioVoter::nowPlaying,true);
    h.until([](const auto&x){return x.applicationsSupported&&x.applications.size()==2;},"Sessions listed");
    check(s.setApplicationGain(speaker,"4242:100",.5f),"Gain accepted");
    h.until([](const auto&x){const auto*a=app(x,"4242:100");return a&&a->state==n::AudioApplicationRouteState::active;},"Route active");
    {std::lock_guard l(h.world->m);h.world->sessions[speaker][0].exited=true;h.world->sessions[speaker][0].active=false;}
    h.wake(&World::sessionWake);
    h.until([](const auto&x){return !app(x,"4242:100")&&x.applications.size()==1;},"A terminated player leaves the list");
    check(std::abs(sessionVolume(*h.world,speaker,"{session-other}")-1.f)<1e-6f,"Other apps are untouched");
    {std::lock_guard l(h.world->m);check(h.world->sessions[speaker].size()==1&&h.world->exitReported.contains("{session-player}"),"The terminated session was reported once, then released");}
    h.world_until([&](World&){return journalEmpty();},"The restored attenuation leaves no recovery entry");
    // A failed restore at exit: the app's next session in this run carries the
    // level Windows persisted for it and is restored once it appears.
    {std::lock_guard l(h.world->m);n::AudioSessionRecord player{"{session-player-2}","4243:200",L"Player",4243,true,true,.8f,0};player.persistentID=persistent;h.world->sessions[speaker].push_back(player);}
    h.wake(&World::sessionWake);
    h.until([](const auto&x){return app(x,"4243:200")!=nullptr;},"The player starts again");
    check(s.setApplicationGain(speaker,"4243:200",.5f),"Gain accepted");
    h.until([](const auto&x){const auto*a=app(x,"4243:200");return a&&a->state==n::AudioApplicationRouteState::active;},"Route active");
    {std::lock_guard l(h.world->m);h.world->failSessionWrites=true;for(auto&r:h.world->sessions[speaker])if(r.id=="{session-player-2}"){r.exited=true;r.active=false;}}
    h.wake(&World::sessionWake);
    h.until([](const auto&x){return !app(x,"4243:200");},"The process leaves even when its restore fails");
    h.world_until([&](World&){const auto d=n::AudioRouteJournal::decode(read(path));return d&&d->size()==1&&std::abs((*d)[0].written-.4f)<1e-6f;},"The unrestored attenuation is kept for recovery");
    {std::lock_guard l(h.world->m);h.world->failSessionWrites=false;n::AudioSessionRecord player{"{session-player-3}","4244:300",L"Player",4244,true,true,.4f,0};player.persistentID=persistent;h.world->sessions[speaker].push_back(player);}
    h.wake(&World::sessionWake);
    h.world_until([&](World&){return journalEmpty();},"The restarted app's persisted attenuation is restored");
    check(std::abs(sessionVolume(*h.world,speaker,"{session-player-3}")-.8f)<1e-6f,"The app plays at its original level again (source: the tap ended with the process)");
    h.until([](const auto&x){const auto*a=app(x,"4244:300");return a&&a->state==n::AudioApplicationRouteState::direct&&a->available;},"The restarted app is listed as direct");
    s.stop();
}
// Owned attenuation keeps the device listener while nobody shows per-app
// audio: an output change then restores the routes on the old endpoint, and
// the listener and sessions close with the last route.
void hiddenOutputChange(){
    Harness h;auto&s=*h.service;const auto player=std::string("4242:100");
    check(s.vote(n::AudioVoter::volume,true)==0,"Volume shown");
    h.until([](const auto&x){return x.applicationsSupported&&x.applications.size()==2;},"Sessions listed");
    check(s.setApplicationGain(speaker,player,.5f),"Owned route");
    h.until([&](const auto&x){const auto*a=app(x,player);return a&&a->state==n::AudioApplicationRouteState::active;},"Route active");
    check(s.vote(n::AudioVoter::volume,false)==0,"Volume hidden with the route owned");
    h.until([](const auto&x){return x.applicationsPaused&&x.topologyActive;},"Only the device listener stays");
    {std::lock_guard l(h.world->m);h.world->defaultOutput=usb;}
    h.wake(&World::topologyWake);
    h.until([](const auto&x){return !x.topologyActive;},"The listener closes with the last route");
    check(std::abs(sessionVolume(*h.world,speaker,"{session-player}")-.8f)<1e-6f,"The speaker session is restored on the old endpoint");
    check(s.snapshot().applications.empty()&&!s.snapshot().applicationError,"The route is gone, quietly");
    {std::lock_guard l(h.world->m);check(h.world->sessionOpens==1&&!h.world->sessionWake&&!h.world->violation,"No session manager opened on the new output while hidden");}
    s.stop();
}
// A restore that fails when another output takes over is reported as the
// per-app status line (source PerAppAudioController.stopAll: errors.first)
// and survives the switch to the new output; the next per-app command that
// succeeds clears it.
void failedRestore(){
    Harness h;auto&s=*h.service;
    n::VolumeProviderBinding volume(n::volumeProviderAccess(s));auto callbacks=volume.callbacks();n::VolumeSnapshot delivered=volume.snapshot();
    volume.setReceiver([&](const auto&v){delivered=v;});
    const auto pump=[&](auto&&p,const char*what){h.until([&](const auto&){return volume.receive(),p();},what);};
    const auto active=[&](double gain){const auto&a=delivered.applications;return std::any_of(a.begin(),a.end(),[&](const auto&x){return x.id=="4242:100"&&x.state==n::VolumeAppState::active&&x.gain==gain;});};
    volume.setVisible(true);callbacks.setActive(true);
    pump([&]{return delivered.applicationActivitySupported&&delivered.applications.size()==2;},"App rows");
    check(callbacks.setAppGain(delivered.outputID,"4242:100",.5),"An app route");
    pump([&]{return active(.5)&&!delivered.perAppStatus;},"The route is active with no status line");
    {std::lock_guard l(h.world->m);h.world->failSessionWrites=true;h.world->defaultOutput=usb;}
    h.wake(&World::topologyWake);
    pump([&]{return delivered.outputID=="{0.0.0.00000000}.{usb}"&&delivered.applicationActivitySupported&&delivered.perAppStatus;},"The new output is listed with the per-app status");
    check(s.snapshot().routeError==-97&&!s.snapshot().applicationError&&s.snapshot().applicationsSupported,"The failed restore outlives the new output's session open");
    check(delivered.perAppStatus->find("(-97)")!=std::string::npos&&!delivered.status&&!delivered.applicationMessage,"It is shown as the status line only");
    {std::lock_guard l(h.world->m);h.world->failSessionWrites=false;}
    check(callbacks.setAppGain(delivered.outputID,"4242:100",.25),"A route on the new output");
    pump([&]{return active(.25)&&!delivered.perAppStatus;},"A per-app command that succeeds clears the status line");
    check(std::abs(sessionVolume(*h.world,usb,"{session-player-usb}")-.2f)<1e-6f,"The new output's session follows the route");
    volume.close();s.stop();
}
// Now Playing reads AudioApplicationRoute::rendering for window-title
// players (NetEase keeps the song in its title while paused). A session
// state event republishes it without repainting Volume; hiding per-app audio
// with nothing owned releases the session manager and every session object.
void renderingAndRelease(){
    Harness h;auto&s=*h.service;const auto player=std::string("4242:100");
    check(s.vote(n::AudioVoter::volume,true)==0,"Volume shown");
    h.until([](const auto&x){return x.applicationsSupported&&!x.applicationsPaused&&x.applications.size()==2;},"Sessions listed");
    check(app(s.snapshot(),player)->rendering&&app(s.snapshot(),"77:100")->rendering,"Active sessions publish their process as rendering");
    const auto volumeBefore=n::volumeSnapshotFromEndpoints(s.snapshot());
    {std::lock_guard l(h.world->m);h.world->sessions[speaker][0].active=false;}h.wake(&World::sessionWake);
    h.until([&](const auto&x){const auto*a=app(x,player);return a&&!a->rendering&&a->available;},"A paused player's state event republishes its route as not rendering (still listed and adjustable)");
    check(n::volumeSnapshotFromEndpoints(s.snapshot())==volumeBefore,"A play/pause changes nothing Volume draws, so Volume does not repaint");
    {std::lock_guard l(h.world->m);h.world->sessions[speaker][0].active=true;}h.wake(&World::sessionWake);
    h.until([&](const auto&x){const auto*a=app(x,player);return a&&a->rendering;},"Playing again republishes rendering");
    unsigned opens{},closes{},pauses{};{std::lock_guard l(h.world->m);opens=h.world->sessionOpens;closes=h.world->sessionCloses;pauses=h.world->sessionPauses;}
    check(s.vote(n::AudioVoter::volume,false)==0,"Volume hidden with no owned route");
    h.until([](const auto&x){return x.applicationsPaused&&x.applications.empty();},"Hidden per-app audio publishes no stale routes");
    {std::lock_guard l(h.world->m);check(h.world->sessionCloses==closes+1&&!h.world->sessionWake&&s.stats().sessionReleases==1,"With nothing owned, hiding releases the session manager and every session object");}
    check(s.vote(n::AudioVoter::volume,true)==0,"Volume shown again");
    h.until([](const auto&x){return !x.applicationsPaused&&x.applicationsSupported&&x.applications.size()==2;},"The sessions are listed again on the next showing");
    {std::lock_guard l(h.world->m);check(h.world->sessionOpens==opens+1,"Showing per-app audio again opens the sessions afresh");}
    check(s.setApplicationGain(speaker,player,.5f),"Owned route");
    h.until([&](const auto&x){const auto*a=app(x,player);return a&&a->state==n::AudioApplicationRouteState::active;},"Route active");
    check(s.vote(n::AudioVoter::volume,false)==0,"Volume hidden with an owned route");
    h.until([](const auto&x){return x.applicationsPaused;},"Paused");
    {std::lock_guard l(h.world->m);check(h.world->sessionCloses==closes+1&&h.world->sessionPauses>pauses,"An owned route keeps its paused sessions so it can still be restored");}
    check(s.stopApplication(player),"Restore while hidden");
    h.world_until([&](World&w){return w.sessionCloses==closes+2;},"Restoring the last owned route while hidden releases the sessions too");
    check(std::abs(sessionVolume(*h.world,speaker,"{session-player}")-.8f)<1e-6f&&s.stats().sessionReleases==2,"The original level is restored before the release");
    s.stop();
}
// The Volume Output and Input menus (IPolicyConfig on Windows, injected
// here): the selector is created once, on the worker, when Volume first
// shows; a switch writes every role, the device list is read again and the
// controls and sessions follow. No real default device is touched.
void defaultDevices(){
    const std::wstring headset=L"{0.0.1.00000000}.{headset-mic}",gone=L"{0.0.0.00000000}.{gone}";
    {
        Harness h({},true);auto&s=*h.service;
        {std::lock_guard l(h.world->m);h.world->inputs.push_back({headset,L"Headset Microphone",L"USB",5u});}
        check(s.vote(n::AudioVoter::eventLog,true)==0,"Device list vote");
        h.until([](const auto&x){return x.topologyActive&&x.topologyListed;},"Device list published");
        {std::lock_guard l(h.world->m);check(h.world->selectorsCreated==0&&!s.snapshot().canSetDefaultOutput,"The device list alone never creates the selector");}
        check(s.vote(n::AudioVoter::volume,true)==0,"Volume shows");
        h.until([](const auto&x){return x.available&&x.canSetDefaultOutput&&x.canSetDefaultInput;},"A supported selector enables both menus");
        {std::lock_guard l(h.world->m);check(h.world->selectorsCreated==1&&h.world->defaultWrites.empty(),"The selector is created once, on the worker, and writes nothing");}
        check(!s.setDefaultEndpoint(n::AudioFlow::output,L""),"An empty identity is refused before the queue");
        check(s.setDefaultEndpoint(n::AudioFlow::output,usb),"Output switch accepted");
        h.until([](const auto&x){return x.defaultOutputID==usb&&x.commandError==0;},"The published default output follows the switch");
        {std::lock_guard l(h.world->m);const auto&w=*h.world;
            check(w.defaultWrites.size()==3&&w.defaultOutput==usb&&w.multimediaOutput==usb&&w.communicationsOutput==usb,"Console, multimedia and communications are all set");
            check(w.defaultInput==mic&&w.multimediaInput==mic&&w.communicationsInput==mic,"An output switch leaves the input defaults");}
        h.world_until([](World&w){return w.bound==usb&&w.sessionOpens==2;},"The volume control and per-app sessions follow the new output");
        check(s.snapshot().canSetDefaultOutput&&!s.snapshot().applicationError,"Switching stays available and quiet");
        check(s.setDefaultEndpoint(n::AudioFlow::input,headset),"Input switch accepted");
        h.until([&](const auto&x){return x.defaultInputID==headset;},"The published default input follows the switch");
        {std::lock_guard l(h.world->m);const auto&w=*h.world;check(w.defaultWrites.size()==6&&w.defaultInput==headset&&w.multimediaInput==headset&&w.communicationsInput==headset&&w.defaultOutput==usb,"Every input role is set; the output stays");}
        // Listed in the snapshot but gone before the switch: nothing is
        // written, the device change is reported and the list is read again.
        {std::lock_guard l(h.world->m);std::erase_if(h.world->outputs,[](const auto&r){return r.id==buds;});}
        check(s.setDefaultEndpoint(n::AudioFlow::output,buds),"Accepted for worker validation");
        h.until([](const auto&x){return x.commandError==deviceChanged&&x.outputs.size()==2;},"A device that left reports the change and the list is read again");
        check(s.setDefaultEndpoint(n::AudioFlow::output,gone)&&s.setDefaultEndpoint(n::AudioFlow::input,usb),"Unlisted and wrong-direction choices are accepted for validation");
        check(s.setVolume(usb,.33f),"A later command marks the queue");
        h.until([](const auto&x){return x.volume&&std::abs(*x.volume-.33f)<1e-6f&&x.commandError==0;},"Commands run in order");
        {std::lock_guard l(h.world->m);check(h.world->defaultWrites.size()==6&&h.world->defaultOutput==usb&&h.world->defaultInput==headset&&!h.world->violation,"No write reached a device that is not listed; every call ran on the one worker");}
        check(s.snapshot().canSetDefaultOutput&&s.snapshot().canSetDefaultInput,"A device that left does not turn switching off");
        s.stop();
    }
    {
        // A write fails (a future Windows changed the interface): the written
        // role is rolled back and switching turns off quietly for the run.
        Harness h({},true);auto&s=*h.service;
        n::VolumeProviderBinding volume(n::volumeProviderAccess(s));auto callbacks=volume.callbacks();n::VolumeSnapshot delivered=volume.snapshot();
        volume.setReceiver([&](const auto&v){delivered=v;});
        const auto pump=[&](auto&&p,const char*what){h.until([&](const auto&){return volume.receive(),p();},what);};
        volume.setVisible(true);callbacks.setActive(true);
        pump([&]{return delivered.canSetDefaultOutput&&delivered.canSetDefaultInput;},"Both menus enabled");
        {std::lock_guard l(h.world->m);h.world->failDefaultWriteAt=2;}
        check(callbacks.setDefaultOutput("{0.0.0.00000000}.{usb}"),"The Output menu's choice is accepted");
        pump([&]{return !delivered.canSetDefaultOutput&&!delivered.canSetDefaultInput;},"A failed write disables both menus");
        {std::lock_guard l(h.world->m);const auto&w=*h.world;
            check(w.defaultWrites.size()==3&&w.defaultWrites[2].id==speaker&&w.defaultWrites[2].role==n::AudioDefaultRole::console,"The console role written before the failure is rolled back");
            check(w.defaultOutput==speaker&&w.multimediaOutput==speaker&&w.communicationsOutput==speaker,"Every role is back on the previous output");}
        check(!delivered.status&&!volume.lastFailure()&&delivered.outputID=="{0.0.0.00000000}.{speaker}","Quiet: no status line or toast, the previous output stays");
        const auto reads=s.stats().topologyReads;h.wake(&World::topologyWake);
        h.world_until([&](World&){return h.service->stats().topologyReads>reads;},"A later device event is processed");
        pump([&]{return true;},"Republished");
        check(!delivered.status&&!delivered.canSetDefaultOutput,"Later snapshots stay quiet and keep the menus off (no toast loop)");
        const auto wakes=s.stats().wakes;
        check(s.setDefaultEndpoint(n::AudioFlow::input,mic),"A direct command is still accepted by the queue");
        h.world_until([&](World&){return h.service->stats().wakes>wakes;},"Processed");
        {std::lock_guard l(h.world->m);check(h.world->defaultWrites.size()==3&&h.world->selectorsCreated==1,"Once off, nothing is written and no selector is created again");}
        volume.close();s.stop();
    }
    {
        // A read fails (GetDefaultAudioEndpoint through the interface): nothing
        // is written and switching turns off quietly, like a failed write.
        Harness h({},true);auto&s=*h.service;
        check(s.vote(n::AudioVoter::volume,true)==0,"Volume shows");
        h.until([](const auto&x){return x.available&&x.canSetDefaultOutput&&x.canSetDefaultInput;},"Both menus enabled");
        {std::lock_guard l(h.world->m);h.world->failDefaultReads=true;}
        check(s.setDefaultEndpoint(n::AudioFlow::output,usb),"Output switch accepted");
        h.until([](const auto&x){return !x.canSetDefaultOutput&&!x.canSetDefaultInput;},"A failed read disables both menus");
        check(s.snapshot().commandError==0&&s.snapshot().defaultOutputID==speaker,"Quiet: no status, the previous output stays");
        {std::lock_guard l(h.world->m);h.world->failDefaultReads=false;}
        const auto wakes=s.stats().wakes;
        check(s.setDefaultEndpoint(n::AudioFlow::input,mic),"A later command is accepted by the queue");
        h.world_until([&](World&){return h.service->stats().wakes>wakes;},"Processed");
        s.drain();
        {std::lock_guard l(h.world->m);check(h.world->defaultWrites.empty()&&h.world->selectorsCreated==1&&s.snapshot().commandError==0,"Once off, nothing is written, reported or created again");}
        s.stop();
    }
    {
        // Another mixer takes the default over between the write and its
        // readback: every call succeeded, so the change is reported once and
        // switching stays on. The rollback leaves the other mixer's choice.
        Harness h({},true);auto&s=*h.service;
        check(s.vote(n::AudioVoter::volume,true)==0,"Volume shows");
        h.until([](const auto&x){return x.available&&x.canSetDefaultOutput;},"Both menus enabled");
        {std::lock_guard l(h.world->m);h.world->raceDefault=buds;}
        check(s.setDefaultEndpoint(n::AudioFlow::output,usb),"Output switch accepted");
        h.until([](const auto&x){return x.commandError==deviceChanged&&x.defaultOutputID==buds;},"The concurrent change is reported and the real default published");
        check(s.snapshot().canSetDefaultOutput&&s.snapshot().canSetDefaultInput,"A concurrent change does not turn switching off");
        {std::lock_guard l(h.world->m);const auto&w=*h.world;check(w.defaultWrites.size()==1&&w.multimediaOutput==speaker&&w.communicationsOutput==speaker,"The switch stops at the mismatch and overwrites nothing");}
        {std::lock_guard l(h.world->m);h.world->raceDefault.clear();}
        check(s.setDefaultEndpoint(n::AudioFlow::output,usb),"The same choice again");
        h.until([](const auto&x){return x.commandError==0&&x.defaultOutputID==usb;},"The next switch succeeds");
        {std::lock_guard l(h.world->m);const auto&w=*h.world;check(w.defaultWrites.size()==4&&w.multimediaOutput==usb&&w.communicationsOutput==usb&&!w.violation,"Every role is set on the worker");}
        s.stop();
    }
    // The interface is missing (E_NOTIMPL, a failed CoCreateInstance) or no
    // selector could be made: both menus stay disabled, nothing is written.
    for(unsigned kind=0;kind<3;++kind){
        Harness h({},true);auto&s=*h.service;
        {std::lock_guard l(h.world->m);if(kind==0)h.world->defaultsSupported=false;else if(kind==1)h.world->throwDefaults=true;else h.world->nullDefaults=true;}
        check(s.vote(n::AudioVoter::volume,true)==0,"Volume shows");
        h.until([](const auto&x){return x.available&&x.applicationsSupported;},"Volume controls");
        check(!s.snapshot().canSetDefaultOutput&&!s.snapshot().canSetDefaultInput&&s.snapshot().commandError==0,"An unavailable selector leaves both menus disabled, quietly");
        const auto wakes=s.stats().wakes;
        check(s.setDefaultEndpoint(n::AudioFlow::output,usb),"Accepted by the queue");
        h.world_until([&](World&){return h.service->stats().wakes>wakes;},"Processed");
        s.drain();check(s.snapshot().defaultOutputID==speaker&&s.snapshot().commandError==0,"Nothing switches and nothing is reported");
        check(s.vote(n::AudioVoter::volume,false)==0&&s.vote(n::AudioVoter::volume,true)==0,"Volume shows again");
        h.until([](const auto&x){return x.available;},"Volume controls again");
        {std::lock_guard l(h.world->m);check(h.world->defaultWrites.empty()&&h.world->selectorsCreated==1&&!h.world->violation,"The selector is tried once per run, on the worker");}
        s.stop();
    }
    {
        Harness h;auto&s=*h.service;s.vote(n::AudioVoter::volume,true);
        h.until([](const auto&x){return x.available;},"Volume controls");
        check(!s.snapshot().canSetDefaultOutput&&!s.snapshot().canSetDefaultInput,"Without a selector both menus stay disabled");
        s.stop();
    }
}
#ifdef _WIN32
// This process's thread IDs (Toolhelp; read-only). Compared as sets, so a
// loader or pool thread that exits meanwhile cannot disturb the count.
std::set<DWORD>threads(){std::set<DWORD>ids;const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return ids;THREADENTRY32 e{};e.dwSize=sizeof(e);const auto pid=GetCurrentProcessId();
    if(Thread32First(snapshot,&e))do if(e.th32OwnerProcessID==pid)ids.insert(e.th32ThreadID);while(Thread32Next(snapshot,&e));CloseHandle(snapshot);return ids;}
std::size_t added(const std::set<DWORD>&before){std::size_t n{};for(const auto id:threads())n+=!before.contains(id);return n;}
void oneWorker(){
    const auto before=threads();Harness h;auto&s=*h.service;check(added(before)==0,"Constructing the audio service starts no thread");
    s.vote(n::AudioVoter::eventLog,true);s.vote(n::AudioVoter::volume,true);s.vote(n::AudioVoter::nowPlaying,true);
    h.until([](const auto&x){return x.applicationsSupported&&x.available;},"Every tier active");
    const auto running=added(before);std::cout<<"Audio service threads: "<<before.size()<<" before, +"<<running<<" with every tier active (injected backends)\n";
    check(running==1,"Every audio tier (devices, endpoint control, per-app sessions) shares one worker thread");
    s.stop();check(added(before)==0,"Stopping the service joins its one worker");
}
#endif
void binding(){
    Harness h({},true);auto&s=*h.service;
    n::VolumeProviderBinding volume(n::volumeProviderAccess(s));auto callbacks=volume.callbacks();n::VolumeSnapshot delivered=volume.snapshot();
    volume.setReceiver([&](const auto&v){delivered=v;});
    const auto pump=[&](auto&&p,const char*what){h.until([&](const auto&){return volume.receive(),p();},what);};
    check(!volume.active()&&!s.stats().workerStarted,"Binding construction votes nothing");
    volume.setVisible(true);callbacks.setActive(true);check(s.votes()==static_cast<unsigned>(n::AudioVoter::volume),"Visible selected Volume casts exactly the volume vote");
    pump([&]{return delivered.canSetVolume&&delivered.outputs.size()==3;},"Binding receives the worker snapshot");
    check(delivered.outputs[0].headphones&&delivered.outputID=="{0.0.0.00000000}.{speaker}"&&delivered.canSetDefaultOutput&&delivered.canSetDefaultInput,"Headphones reach the UI; a supported selector enables both device menus");
    check(callbacks.setVolume(delivered.outputID,.25),"A volume write is accepted asynchronously");
    pump([&]{return delivered.volume&&std::abs(*delivered.volume-.25)<1e-6;},"Readback reaches the Volume snapshot");
    check(callbacks.setMute(delivered.outputID,true),"Mute reaches the binding");
    pump([&]{return delivered.muted==true;},"Mute readback");
    // The Output menu (VolumeCanvas.openDeviceMenu): stop the routes quietly,
    // then switch (730f4bb: no status line of its own).
    pump([&]{return delivered.applicationActivitySupported&&delivered.applications.size()==2;},"App rows before the switch");
    check(callbacks.setAppGain(delivered.outputID,"4242:100",.5),"An app route before the switch");
    pump([&]{const auto&a=delivered.applications;return std::any_of(a.begin(),a.end(),[](const auto&x){return x.state==n::VolumeAppState::active&&x.gain==.5;});},"The route is active before the switch");
    check(std::abs(sessionVolume(*h.world,speaker,"{session-player}")-.4f)<1e-6f,"The old output's session is attenuated");
    callbacks.stopAllApps();check(callbacks.setDefaultOutput("{0.0.0.00000000}.{usb}"),"The Output menu's choice is accepted");
    pump([&]{return delivered.outputID=="{0.0.0.00000000}.{usb}"&&delivered.canSetVolume;},"The new default output reaches the Volume UI with its controls");
    check(!delivered.status&&!delivered.applicationMessage&&!delivered.perAppStatus,"Switching to another output is quiet: no toast, app message or status line");
    check(std::abs(sessionVolume(*h.world,speaker,"{session-player}")-.8f)<1e-6f,"The old output's session is back at its original level");
    callbacks.stopAllApps();check(callbacks.setDefaultOutput("{0.0.0.00000000}.{speaker}"),"Switching back");
    pump([&]{return delivered.outputID=="{0.0.0.00000000}.{speaker}";},"The previous output is the default again");
    {std::lock_guard l(h.world->m);check(h.world->defaultWrites.size()==6&&h.world->communicationsOutput==speaker&&!h.world->violation,"Each switch wrote the three roles on the worker");}
    const auto appID=std::string("app:4242:100");
    pump([&]{return delivered.applicationActivitySupported&&delivered.applications.size()==2;},"App rows");
    check(callbacks.setAppGain(delivered.outputID,appID.substr(4),.5),"App slider");
    pump([&]{const auto&a=delivered.applications;return std::any_of(a.begin(),a.end(),[](const auto&x){return x.state==n::VolumeAppState::active&&x.gain==.5;});},"App route active in UI");
    volume.setVisible(false);check(s.votes()==0,"Concealing releases the vote");
    h.until([](const auto&x){return x.paused&&x.applicationsPaused&&x.topologyActive;},"Owned routes keep only the device listener after conceal");
    {std::lock_guard l(h.world->m);check(h.world->endpointOpen&&h.world->bound.empty()&&!h.world->sessionWake,"Hidden with an owned route: one device listener, no volume control, no session notifications");}
    check(s.stopApplication("4242:100"),"Restore while hidden");
    h.until([](const auto&x){return !x.topologyActive;},"The listener kept for the route closes with the last route");
    {std::lock_guard l(h.world->m);check(!h.world->endpointOpen&&std::abs(h.world->sessions[speaker][0].volume.value_or(0)-.8f)<1e-6f,"No audio notification remains registered; the level is restored");}
    volume.close();s.stop();
}
}
int main(){
    try{lifecycle();fixedOutput();journal();processExit();hiddenOutputChange();failedRestore();renderingAndRelease();
#ifdef _WIN32
        oneWorker();
#endif
        binding();defaultDevices();std::cout<<"PASS "<<checks<<" injected audio service checks (one sleeping worker; no native audio accessed)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
