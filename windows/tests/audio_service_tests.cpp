#include "native/audio_service.hpp"
#include "native/audio_route_journal.hpp"
#include "native/volume_audio_access.hpp"
#include "core/data/data_store.hpp"
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

struct Harness {
    std::shared_ptr<World>world=std::make_shared<World>();
    std::mutex nm;std::condition_variable ncv;unsigned notices{};std::thread::id noticeThread;
    std::unique_ptr<n::AudioService>service;
    explicit Harness(std::optional<fs::path>journal={}){
        n::AudioServiceOptions o;o.endpoints=[w=world]{return std::make_unique<FakeEndpoints>(w);};o.sessions=[w=world]{return std::make_unique<FakeSessions>(w);};
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
        check(x.outputs[0].bluetooth&&x.outputs[0].headphones&&!x.outputs[1].bluetooth&&x.outputs[1].headphones&&!x.outputs[2].headphones,"Bluetooth from the BTHENUM enumerator; headphones from the form factor");
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
    check(!s.setVolume(L"",.5f)&&!s.setVolume(speaker,1.5f)&&!s.setBalance(speaker,-2)&&!s.setApplicationGain("",.5f),"Invalid commands are rejected before the queue");
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
    check(s.setApplicationGain(player,.5f),"Per-app gain accepted");
    h.until([&](const auto&x){const auto*a=app(x,player);return a&&a->state==n::AudioApplicationRouteState::active&&a->gain&&*a->gain==.5f;},"Per-app route becomes active");
    check(std::abs(sessionVolume(*h.world,speaker,"{session-player}")-.4f)<1e-6f,"Session attenuated relative to its original level");
    {std::lock_guard l(h.world->m);h.world->defaultOutput=usb;}
    h.wake(&World::topologyWake);
    h.until([&](const auto&x){return x.defaultOutputID==usb&&x.routesStoppedByDeviceChange&&!app(x,player)->gain;},"Output change stops owned routes with the source notice");
    check(std::abs(sessionVolume(*h.world,speaker,"{session-player}")-.8f)<1e-6f,"The previous output's session is restored to its original level");
    {std::lock_guard l(h.world->m);check(h.world->bound==usb&&h.world->sessionCloses==1&&h.world->sessionOpens==2,"Controls and sessions follow the new default output");}
    check(s.setApplicationGain(player,.25f),"Gain on the new output");
    h.until([&](const auto&x){const auto*a=app(x,player);return !x.routesStoppedByDeviceChange&&a&&a->state==n::AudioApplicationRouteState::active;},"Explicit per-app command clears the notice");

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
    check(s.setApplicationGain(player,.5f),"Route before shutdown");
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
        check(s.setApplicationGain("4242:100",.5f),"Gain accepted");
        h.until([](const auto&x){const auto*a=app(x,"4242:100");return a&&a->state==n::AudioApplicationRouteState::active;},"Route active");
        check(checked&&durable,"The recovery entry is durable on disk BEFORE the first attenuating mixer write");
        for(const float v:{.4f,.3f,.2f})s.setApplicationGain("4242:100",v);
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
        check(s.setApplicationGain("4242:100",.5f),"Gain accepted for validation");
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
    check(s.setApplicationGain("4242:100",.5f),"Gain accepted");
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
    check(s.setApplicationGain("4243:200",.5f),"Gain accepted");
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
void binding(){
    Harness h;auto&s=*h.service;
    n::VolumeProviderBinding volume(n::volumeProviderAccess(s));auto callbacks=volume.callbacks();n::VolumeController controller(volume.snapshot(),callbacks);
    volume.setReceiver([&](const auto&v){controller.receiveSnapshot(v);});
    const auto pump=[&](auto&&p,const char*what){h.until([&](const auto&){return volume.receive(),p();},what);};
    check(!volume.active()&&!s.stats().workerStarted,"Binding construction votes nothing");
    volume.setVisible(true);controller.setActive(true);check(s.votes()==static_cast<unsigned>(n::AudioVoter::volume),"Visible selected Volume casts exactly the volume vote");
    pump([&]{return controller.snapshot().canSetVolume&&controller.snapshot().outputs.size()==3;},"Binding receives the worker snapshot");
    check(controller.snapshot().outputs[0].bluetooth&&controller.snapshot().outputID=="{0.0.0.00000000}.{speaker}"&&!controller.snapshot().canSetDefaultOutput,"Bluetooth reaches the UI; default-device switching stays unavailable (platform gap)");
    check(controller.setSlider("volume",.25),"UI slider write accepted asynchronously");
    pump([&]{return controller.snapshot().volume&&std::abs(*controller.snapshot().volume-.25)<1e-6;},"Readback reaches the controller");
    check(controller.perform("audio:mute",1),"Mute through the source action");
    pump([&]{return controller.snapshot().muted==true;},"Mute readback");
    const auto appID=std::string("app:4242:100");
    pump([&]{return controller.snapshot().applicationActivitySupported&&controller.snapshot().applications.size()==2;},"App rows");
    check(controller.setSlider(appID,.5),"App slider");
    pump([&]{const auto&a=controller.snapshot().applications;return std::any_of(a.begin(),a.end(),[](const auto&x){return x.state==n::VolumeAppState::active&&x.gain==.5;});},"App route active in UI");
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
    try{lifecycle();fixedOutput();journal();processExit();binding();std::cout<<"PASS "<<checks<<" injected audio service checks (one sleeping worker; no native audio accessed)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
