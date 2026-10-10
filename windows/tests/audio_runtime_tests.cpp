#include "native/audio_runtime.hpp"
#include "native/volume_strings.hpp"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

// The production audio owner over injected endpoint/session backends: no COM,
// audio device, mixer level, process or user file is touched.
namespace {
namespace n=endfield::native;namespace c=endfield::core;using namespace std::chrono_literals;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
const std::wstring speaker=L"{0.0.0.00000000}.{speaker}",usb=L"{0.0.0.00000000}.{usb}",mic=L"{0.0.1.00000000}.{mic}";

struct World {
    std::mutex m;
    std::vector<n::AudioEndpointRecord>outputs,inputs;std::wstring defaultOutput,defaultInput;
    float master{.5f};bool muted{};
    n::AudioEndpointBackend::Wake topologyWake,volumeWake;bool endpointOpen{};std::wstring bound;
    std::vector<n::AudioSessionRecord>sessions;n::AudioSessionBackend::Wake sessionWake;
    unsigned endpointsCreated{},sessionsCreated{};
};
class Endpoints final:public n::AudioEndpointBackend {
    std::shared_ptr<World>w;
public:
    explicit Endpoints(std::shared_ptr<World>world):w(std::move(world)){std::lock_guard l(w->m);++w->endpointsCreated;}
    std::int32_t open(Wake wake)override{std::lock_guard l(w->m);w->topologyWake=std::move(wake);w->endpointOpen=true;return 0;}
    void close()noexcept override{std::lock_guard l(w->m);w->topologyWake={};w->volumeWake={};w->bound.clear();w->endpointOpen=false;}
    std::int32_t enumerate(n::AudioFlow flow,std::vector<n::AudioEndpointRecord>&out)override{std::lock_guard l(w->m);out=flow==n::AudioFlow::output?w->outputs:w->inputs;return 0;}
    std::int32_t defaultEndpoint(n::AudioFlow flow,std::wstring&out)override{std::lock_guard l(w->m);out=flow==n::AudioFlow::output?w->defaultOutput:w->defaultInput;return out.empty()?static_cast<std::int32_t>(0x80070490u):0;}
    std::int32_t bind(std::wstring_view id,Wake wake)override{std::lock_guard l(w->m);w->bound=id;w->volumeWake=id.empty()?Wake{}:std::move(wake);return 0;}
    std::int32_t read(n::AudioEndpointVolumeState&v)override{std::lock_guard l(w->m);v={};v.scalar=w->master;v.muted=w->muted;v.channels=1;v.minimumDecibels=-65;v.maximumDecibels=0;v.incrementDecibels=.5f;v.steps=100;return 0;}
    std::int32_t setVolume(float value)override{std::lock_guard l(w->m);w->master=value;return 0;}
    std::int32_t setMute(bool value)override{std::lock_guard l(w->m);w->muted=value;return 0;}
    std::int32_t readChannel(unsigned,float&)override{return -1;}
    std::int32_t writeChannel(unsigned,float)override{return -1;}
};
class Sessions final:public n::AudioSessionBackend {
    std::shared_ptr<World>w;
    n::AudioSessionRecord*find(std::string_view id){for(auto&r:w->sessions)if(r.id==id)return &r;return nullptr;}
public:
    explicit Sessions(std::shared_ptr<World>world):w(std::move(world)){std::lock_guard l(w->m);++w->sessionsCreated;}
    std::int32_t open(std::wstring_view,Wake wake)override{std::lock_guard l(w->m);w->sessionWake=std::move(wake);return 0;}
    std::int32_t resume(Wake wake)override{std::lock_guard l(w->m);w->sessionWake=std::move(wake);return 0;}
    void pause()noexcept override{std::lock_guard l(w->m);w->sessionWake={};}
    std::int32_t read(std::vector<n::AudioSessionRecord>&out)override{std::lock_guard l(w->m);out=w->sessions;return 0;}
    std::int32_t readVolume(std::string_view id,float&v)override{std::lock_guard l(w->m);const auto*r=find(id);if(!r||!r->volume)return -98;v=*r->volume;return 0;}
    std::int32_t writeVolume(std::string_view id,float v)override{std::lock_guard l(w->m);auto*r=find(id);if(!r)return -98;r->volume=v;return 0;}
    void close()noexcept override{std::lock_guard l(w->m);w->sessionWake={};}
};
struct Harness {
    std::shared_ptr<World>world=std::make_shared<World>();
    std::mutex nm;std::condition_variable ncv;unsigned notices{};
    std::unique_ptr<n::AudioRuntime>runtime;n::AudioRuntimeChanges seen;
    explicit Harness(c::Language language=c::Language::english){
        n::AudioServiceOptions o;o.endpoints=[w=world]{return std::make_unique<Endpoints>(w);};o.sessions=[w=world]{return std::make_unique<Sessions>(w);};o.topologySettleSeconds=.02;
        runtime=std::make_unique<n::AudioRuntime>(std::move(o),language,[this]{{std::lock_guard l(nm);++notices;}ncv.notify_all();});
    }
    // Owner message loop: drain once per notice until the predicate holds.
    template<class P>void until(P&&predicate,const char*what){
        const auto deadline=std::chrono::steady_clock::now()+10s;
        for(;;){
            unsigned count;{std::lock_guard l(nm);count=notices;}
            const auto changes=runtime->drain();seen.snapshot|=changes.snapshot;seen.volume|=changes.volume;seen.applications|=changes.applications;seen.devices|=changes.devices;
            if(predicate())return;
            std::unique_lock l(nm);if(!ncv.wait_until(l,deadline,[&]{return notices!=count;})){l.unlock();runtime->drain();if(predicate())return;throw std::runtime_error(what);}
        }
    }
    void wake(n::AudioEndpointBackend::Wake World::*member){n::AudioEndpointBackend::Wake w;{std::lock_guard l(world->m);w=(*world).*member;}check(bool(w),"Native notification is registered");w();}
};
const n::AudioApplicationRoute*route(std::span<const n::AudioApplicationRoute>apps,std::string_view id){for(const auto&a:apps)if(a.id==id)return &a;return nullptr;}

void construction(){
    bool rejected{};try{n::AudioRuntime bad({},c::Language::system,{});}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"System language must be resolved before construction");
    Harness h;
    check(!h.runtime->stats().workerStarted&&h.world->endpointsCreated==0&&h.world->sessionsCreated==0,"Construction starts no worker and opens no backend");
    check(h.runtime->drain()==n::AudioRuntimeChanges{}&&!h.runtime->deviceLabels()&&h.runtime->applications().empty(),"Nothing is published before a vote");
    check(!h.runtime->volume().active()&&h.runtime->volume().snapshot().outputs.empty(),"The Volume binding votes nothing until visible and selected");
    std::int32_t foreign{};std::thread([&]{foreign=h.runtime->setDeviceEvents(true);}).join();
    check(foreign==static_cast<std::int32_t>(0x8001010Eu)&&!h.runtime->stats().workerStarted,"Votes belong to the owner thread");
}
void coalescing(){
    Harness h;{std::lock_guard l(h.world->m);h.world->outputs={{speaker,L"Speakers",L"HDAUDIO",1u}};}
    check(h.runtime->setDeviceEvents(true)==0,"Vote");
    {std::unique_lock l(h.nm);check(h.ncv.wait_for(l,10s,[&]{return h.notices>=1;}),"First publication posts the owner message");}
    for(int n=0;n<3;++n){
        const auto before=h.runtime->stats().published;
        {std::lock_guard l(h.world->m);h.world->outputs[0].name=L"Speakers "+std::to_wstring(n);}h.wake(&World::topologyWake);
        const auto deadline=std::chrono::steady_clock::now()+10s;
        while(h.runtime->stats().published==before&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
        check(h.runtime->stats().published>before,"Each device change is published by the worker");
    }
    {std::lock_guard l(h.nm);check(h.notices==1,"Undrained publications never queue a second owner message");}
    check(h.runtime->drain().snapshot&&h.runtime->snapshot().outputs[0].name==L"Speakers 2","One drain adopts the latest publication");
    {std::lock_guard l(h.world->m);h.world->outputs[0].name=L"Speakers again";}h.wake(&World::topologyWake);
    {std::unique_lock l(h.nm);check(h.ncv.wait_for(l,10s,[&]{return h.notices==2;}),"Draining re-arms the owner message");}
}
void devices(){
    Harness h;
    // A machine that starts with no endpoint at all still gets a baseline.
    check(h.runtime->setDeviceEvents(true)==0,"Event Log device list vote");
    h.until([&]{return h.seen.devices;},"Empty baseline is reported");
    auto labels=h.runtime->deviceLabels();check(labels&&labels->empty(),"An empty device list is a valid baseline, never 'unknown'");
    {std::lock_guard l(h.world->m);h.world->outputs.push_back({speaker,L"  扬声器 (合成)  ",L"HDAUDIO",1u});h.world->inputs.push_back({mic,L"Microphone",L"HDAUDIO",4u});}
    h.seen={};h.wake(&World::topologyWake);
    h.until([&]{return h.seen.devices;},"Device arrival settles");
    labels=h.runtime->deviceLabels();
    check(labels&&labels->size()==2&&labels->at("{0.0.0.00000000}.{speaker}")=="扬声器 (合成)"&&labels->at("{0.0.1.00000000}.{mic}")=="Microphone","UTF-8 identity -> trimmed label map for ApplicationEventRecorder");
    check(!h.seen.volume&&!h.seen.applications&&h.runtime->volume().snapshot().outputs.empty(),"Device events alone publish no Volume or per-app content");
    // Hiding every user closes the listener; the stale list is withdrawn.
    check(h.runtime->setDeviceEvents(false)==0,"Vote withdrawn");
    h.until([&]{return !h.runtime->snapshot().topologyActive;},"Listener closes");
    {std::lock_guard l(h.world->m);check(!h.runtime->deviceLabels()&&!h.world->endpointOpen,"No list and no registration while nobody listens");}
    h.seen={};check(h.runtime->setDeviceEvents(true)==0,"Vote again");
    h.until([&]{return h.seen.devices;},"Reopening reports a fresh baseline");
    check(h.runtime->deviceLabels()->size()==2,"Baseline after reopening");
    h.runtime->stop();
}
void volume(){
    Harness h;
    {std::lock_guard l(h.world->m);h.world->outputs={{speaker,L"Speakers",L"HDAUDIO",1u},{usb,L"USB Headset",L"USB",5u}};h.world->inputs={{mic,L"Microphone",L"HDAUDIO",4u}};h.world->defaultOutput=speaker;h.world->defaultInput=mic;}
    unsigned received{};n::VolumeController controller(h.runtime->volume().snapshot(),h.runtime->volume().callbacks(),n::volumeStrings(c::Language::english));
    h.runtime->volume().setReceiver([&](const n::VolumeSnapshot&s){++received;controller.receiveSnapshot(s);});
    h.runtime->volume().setVisible(true);controller.setActive(true);
    h.until([&]{return controller.snapshot().canSetVolume&&controller.snapshot().outputs.size()==2;},"Volume content arrives through the binding");
    check(h.seen.volume&&received>0,"drain() reports and delivers Volume content");
    check(controller.snapshot().outputs[1].headphones&&!controller.snapshot().canSetDefaultOutput&&!controller.snapshot().canSetDefaultInput,"Headset traits reach the UI; default-device switching stays unavailable");
    check(controller.setSlider("volume",.25),"Slider write accepted");
    h.until([&]{return controller.snapshot().volume&&std::abs(*controller.snapshot().volume-.25)<1e-6;},"Readback reaches the controller");
    {std::lock_guard l(h.world->m);h.world->defaultOutput=usb;}
    h.wake(&World::topologyWake);
    h.until([&]{return controller.snapshot().outputID=="{0.0.0.00000000}.{usb}";},"Default output change reaches the Volume UI without polling");
    // Localized failure text follows the language without a new provider.
    check(h.runtime->setLanguage(c::Language::simplifiedChinese)&&!h.runtime->setLanguage(c::Language::simplifiedChinese),"Language changes once");
    check(!h.runtime->volume().callbacks().setVolume("{0.0.0.00000000}.{speaker}",.4),"A gesture bound to the previous output is refused");
    check(controller.snapshot().status=="音频设备已更改，请重试。","The refusal shows the source localized device-changed message");
    {std::lock_guard l(h.world->m);check(std::abs(h.world->master-.25f)<1e-6f,"The refused gesture never reached a device");}
    check(h.runtime->language()==c::Language::simplifiedChinese,"Language is retained");
    bool threw{};try{h.runtime->setLanguage(c::Language::system);}catch(const std::invalid_argument&){threw=true;}check(threw,"System must be resolved by the caller");
    h.runtime->volume().setVisible(false);
    h.until([&]{return h.runtime->snapshot().paused;},"Concealing releases the endpoint controls");
    h.runtime->stop();h.runtime->stop();
    check(h.runtime->drain()==n::AudioRuntimeChanges{}&&!h.runtime->setApplicationGain("x",.5)&&!h.runtime->powerSuspend(),"A stopped runtime accepts nothing and stays idempotent");
}
void nowPlaying(){
    Harness h;
    {std::lock_guard l(h.world->m);h.world->outputs={{speaker,L"Speakers",L"HDAUDIO",1u}};h.world->defaultOutput=speaker;
        n::AudioSessionRecord player{"{player}","4242:100",L"Player",4242,true,true,.8f,0};player.appUserModelID="Synthetic.Player_8wekyb3d8bbwe!App";h.world->sessions={player};}
    check(h.runtime->setNowPlayingVisible(true)==0,"Now Playing vote");
    h.until([&]{return h.seen.applications&&route(h.runtime->applications(),"4242:100");},"Per-app routes for Now Playing");
    check(h.runtime->applicationsRevision()==1&&route(h.runtime->applications(),"4242:100")->appUserModelID=="Synthetic.Player_8wekyb3d8bbwe!App","Routes carry the exact packaged identity for the GSMTC match");
    {std::lock_guard l(h.world->m);check(h.world->bound.empty(),"Now Playing never binds the endpoint volume control");}
    check(!h.runtime->setApplicationGain("4242:100",std::nan(""))&&!h.runtime->setApplicationGain("4242:100",1.5),"Invalid gains are refused before the queue");
    check(h.runtime->setApplicationGain("4242:100",.5),"Gain accepted");
    h.until([&]{const auto*r=route(h.runtime->applications(),"4242:100");return r&&r->state==n::AudioApplicationRouteState::active&&r->gain&&*r->gain==.5f;},"Route active");
    {std::lock_guard l(h.world->m);check(std::abs(*h.world->sessions[0].volume-.4f)<1e-6f,"Relative attenuation of the player's session");}
    const auto revision=h.runtime->applicationsRevision();check(revision>1,"Route changes advance the revision");
    check(h.runtime->powerSuspend(),"Suspend");
    h.until([&]{const auto*r=route(h.runtime->applications(),"4242:100");return r&&r->state==n::AudioApplicationRouteState::direct;},"Sleep restores owned routes");
    {std::lock_guard l(h.world->m);check(std::abs(*h.world->sessions[0].volume-.8f)<1e-6f,"Original level restored");}
    check(h.runtime->powerResume(),"Resume re-reads devices");
    check(h.runtime->setApplicationGain("4242:100",.25),"Gain again");
    h.until([&]{const auto*r=route(h.runtime->applications(),"4242:100");return r&&r->state==n::AudioApplicationRouteState::active;},"Route active again");
    check(h.runtime->stopApplication("4242:100"),"Stop accepted");
    h.until([&]{const auto*r=route(h.runtime->applications(),"4242:100");return r&&r->state==n::AudioApplicationRouteState::direct;},"Route restored");
    check(h.runtime->setApplicationGain("4242:100",.5),"Route before quit");
    h.until([&]{const auto*r=route(h.runtime->applications(),"4242:100");return r&&r->state==n::AudioApplicationRouteState::active;},"Active before quit");
    h.runtime.reset();
    {std::lock_guard l(h.world->m);check(std::abs(*h.world->sessions[0].volume-.8f)<1e-6f&&!h.world->endpointOpen&&!h.world->sessionWake,"Destruction restores every owned session and unregisters everything");}
}
}
int main(){
    try{construction();coalescing();devices();volume();nowPlaying();std::cout<<"PASS "<<checks<<" injected audio runtime checks (no native audio accessed)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
