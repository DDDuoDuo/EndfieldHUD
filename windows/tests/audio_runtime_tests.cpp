#include "native/audio_runtime.hpp"
#include "native/volume_strings.hpp"
#include "modules/volume_canvas.hpp"
#ifdef EHUD_AUDIO_RUNTIME_REGISTRY
#include "app/application_audio.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
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
    // The owner message: adopt the publication, then republish the Volume
    // binding (the application does this under Volume's isolation:
    // app/application_audio.hpp drainAudio).
    std::function<n::AudioRuntimeChanges()>message=[this]{auto changes=runtime->drain();if(changes.snapshot)changes.volume|=runtime->volume().receive();return changes;};
    explicit Harness(c::Language language=c::Language::english){
        n::AudioServiceOptions o;o.endpoints=[w=world]{return std::make_unique<Endpoints>(w);};o.sessions=[w=world]{return std::make_unique<Sessions>(w);};o.topologySettleSeconds=.02;
        runtime=std::make_unique<n::AudioRuntime>(std::move(o),language,[this]{{std::lock_guard l(nm);++notices;}ncv.notify_all();});
    }
    // Owner message loop: drain once per notice until the predicate holds.
    template<class P>void until(P&&predicate,const char*what){
        const auto deadline=std::chrono::steady_clock::now()+10s;
        for(;;){
            unsigned count;{std::lock_guard l(nm);count=notices;}
            const auto changes=message();seen.snapshot|=changes.snapshot;seen.volume|=changes.volume;seen.applications|=changes.applications;seen.devices|=changes.devices;
            if(predicate())return;
            std::unique_lock l(nm);if(!ncv.wait_until(l,deadline,[&]{return notices!=count;})){l.unlock();(void)message();if(predicate())return;throw std::runtime_error(what);}
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
    unsigned received{};n::VolumeSnapshot delivered=h.runtime->volume().snapshot();const auto callbacks=h.runtime->volume().callbacks();
    h.runtime->volume().setReceiver([&](const n::VolumeSnapshot&s){++received;delivered=s;});
    h.runtime->volume().setVisible(true);callbacks.setActive(true);
    h.until([&]{return delivered.canSetVolume&&delivered.outputs.size()==2;},"Volume content arrives through the binding");
    check(h.seen.volume&&received>0,"The owner message reports and delivers Volume content");
    check(delivered.outputs[1].headphones&&!delivered.canSetDefaultOutput&&!delivered.canSetDefaultInput,"Headset traits reach the UI; default-device switching stays unavailable");
    check(callbacks.setVolume(delivered.outputID,.25),"Slider write accepted");
    h.until([&]{return delivered.volume&&std::abs(*delivered.volume-.25)<1e-6;},"Readback reaches the Volume snapshot");
    {std::lock_guard l(h.world->m);h.world->defaultOutput=usb;}
    h.wake(&World::topologyWake);
    h.until([&]{return delivered.outputID=="{0.0.0.00000000}.{usb}";},"Default output change reaches the Volume UI without polling");
    // Localized failure text follows the language without a new provider.
    check(h.runtime->setLanguage(c::Language::simplifiedChinese)&&!h.runtime->setLanguage(c::Language::simplifiedChinese),"Language changes once");
    check(!h.runtime->volume().callbacks().setVolume("{0.0.0.00000000}.{speaker}",.4),"A gesture bound to the previous output is refused");
    check(delivered.status=="音频设备已更改，请重试。","The refusal shows the source localized device-changed message");
    {std::lock_guard l(h.world->m);check(std::abs(h.world->master-.25f)<1e-6f,"The refused gesture never reached a device");}
    check(h.runtime->language()==c::Language::simplifiedChinese,"Language is retained");
    bool threw{};try{h.runtime->setLanguage(c::Language::system);}catch(const std::invalid_argument&){threw=true;}check(threw,"System must be resolved by the caller");
    h.runtime->volume().setVisible(false);
    h.until([&]{return h.runtime->snapshot().paused;},"Concealing releases the endpoint controls");
    h.runtime->stop();h.runtime->stop();
    check(h.runtime->drain()==n::AudioRuntimeChanges{}&&!h.runtime->setApplicationGain("x",.5)&&!h.runtime->powerSuspend(),"A stopped runtime accepts nothing and stays idempotent");
}
// Leaving Volume and coming back, or closing and reopening the HUD on it (the
// Mac's AudioDeviceController.stop()/start() keeps its snapshot; only the
// setters check isRunning): the page's content stays as it was while the vote
// is released and until the worker has read again, and an unchanged read
// publishes nothing. A snapshot that dropped applicationActivitySupported
// (VolumeCanvas's page structure) made the canvas rebuild its page as it left
// and again under the entrance of every switch back to Volume.
void volumeRevisit(){
    Harness h;
    {std::lock_guard l(h.world->m);h.world->outputs={{speaker,L"Speakers",L"HDAUDIO",1u}};h.world->inputs={{mic,L"Microphone",L"HDAUDIO",4u}};h.world->defaultOutput=speaker;h.world->defaultInput=mic;
        h.world->sessions={{"{player}","4242:100",L"Player",4242,true,true,.8f,0}};}
    auto&binding=h.runtime->volume();const auto callbacks=binding.callbacks();
    std::vector<n::VolumeSnapshot>deliveries;n::VolumeSnapshot delivered=binding.snapshot();
    binding.setReceiver([&](const n::VolumeSnapshot&s){deliveries.push_back(s);delivered=s;});
    binding.setVisible(true);callbacks.setActive(true);
    h.until([&]{return delivered.canSetVolume&&delivered.applicationActivitySupported&&delivered.applications.size()==1;},"Volume shows the devices and the audio app");
    const auto shown=delivered;
    for(const bool conceal:{false,true}){
        deliveries.clear();
        if(conceal)binding.setVisible(false);else callbacks.setActive(false);
        check(deliveries.empty()&&binding.snapshot()==shown,"Releasing the vote keeps the shown snapshot (nothing is delivered to the leaving page)");
        h.until([&]{return h.runtime->snapshot().paused;},"The worker releases the endpoint controls");
        check(deliveries.empty()&&binding.snapshot()==shown,"The paused publication changes nothing Volume shows");
        check(!callbacks.setVolume(shown.outputID,.3),"Writes stay refused while Volume is away");
        if(conceal)binding.setVisible(true);else callbacks.setActive(true);
        check(deliveries.empty()&&binding.snapshot()==shown,"Coming back shows the retained content before the worker read");
        h.until([&]{return !h.runtime->snapshot().paused;},"The worker reads again");
        check(deliveries.empty()&&binding.snapshot()==shown,"An unchanged read publishes nothing: the page is not rebuilt under its entrance");
    }
    // A change made while Volume was away arrives as one ordinary update.
    deliveries.clear();callbacks.setActive(false);
    h.until([&]{return h.runtime->snapshot().paused;},"Paused again");
    {std::lock_guard l(h.world->m);h.world->master=.3f;}
    callbacks.setActive(true);
    h.until([&]{return delivered.volume&&std::abs(*delivered.volume-.3)<1e-6;},"The new level arrives with the first read");
    check(deliveries.size()==1&&deliveries[0].applicationActivitySupported&&deliveries[0].canSetVolume,"The read updates the values in place; the page structure stays");
    h.runtime->stop();
}
// An app session whose name is ill-formed UTF-16 (an app-set display name or
// an executable label with an unpaired surrogate): the UI's audio message
// (AudioRuntime::drain, outside module isolation) converted every per-app
// name to UTF-8 for the Volume page and threw, which stopped the HUD's frames
// and ended the run. The row keeps the backend's PID fallback label instead,
// a well-formed surrogate pair keeps its own name. A name with an embedded
// NUL (which made routes.update drop the whole app list) keeps the same
// label, and the other apps stay listed. So does a name within the session
// limit (4096 UTF-16 units) but past the Volume rows' 4096 UTF-8 bytes, which
// failed the Volume canvas's snapshot check and disabled the module.
void malformedAppName(){
    Harness h;
    {std::lock_guard l(h.world->m);h.world->outputs={{speaker,L"Speakers",L"HDAUDIO",1u}};h.world->inputs={{mic,L"Microphone",L"HDAUDIO",4u}};h.world->defaultOutput=speaker;h.world->defaultInput=mic;
        h.world->sessions={{"{high}","4242:100",std::wstring(L"Player")+wchar_t(0xD800),4242,true,true,.8f,0},
            {"{low}","4343:100",std::wstring(1,wchar_t(0xDC00))+L"Tail",4343,true,true,.6f,0},
            {"{pair}","4444:100",std::wstring(L"Music \U0001F3B5"),4444,true,true,.4f,0},
            {"{nul}","4545:100",std::wstring(L"Nul\0Name",8),4545,true,true,.5f,0},
            {"{long}","4646:100",std::wstring(2000,L'\u6587'),4646,true,true,.5f,0}};}
    auto&binding=h.runtime->volume();const auto callbacks=binding.callbacks();n::VolumeSnapshot delivered=binding.snapshot();
    binding.setReceiver([&](const n::VolumeSnapshot&s){delivered=s;});
    bool threw{};
    try{binding.setVisible(true);callbacks.setActive(true);
        h.until([&]{return delivered.applicationActivitySupported&&delivered.applications.size()==5;},"Volume lists every audio app");}
    catch(const std::invalid_argument&){threw=true;}
    check(!threw,"An ill-formed UTF-16 app name never throws out of AudioRuntime::drain (the UI audio message)");
    const auto named=[&](std::string_view id)->std::string{for(const auto&a:delivered.applications)if(a.id==id)return a.name;return "missing";};
    check(named("4242:100")=="PID 4242"&&named("4343:100")=="PID 4343","An ill-formed name shows the existing PID fallback label");
    check(named("4444:100")=="Music \xF0\x9F\x8E\xB5","A well-formed surrogate pair keeps its name");
    check(named("4545:100")=="PID 4545","A name with an embedded NUL shows the PID label and the list stays");
    check(named("4646:100")=="PID 4646","A name past 4096 UTF-8 bytes shows the PID label");
    threw=false;try{n::validateVolumeSnapshot(delivered);}catch(const std::invalid_argument&){threw=true;}
    check(!threw,"The published snapshot passes the Volume canvas's check");
    // Later publications with the relabelled rows listed: an endpoint volume
    // change and one app's gain.
    threw=false;
    try{{std::lock_guard l(h.world->m);h.world->master=.3f;}h.wake(&World::volumeWake);
        h.until([&]{return delivered.volume&&std::abs(*delivered.volume-.3)<1e-6;},"An endpoint volume change still drains");
        check(callbacks.setAppGain("4242:100",.5),"The app's slider is accepted");
        h.until([&]{for(const auto&a:delivered.applications)if(a.id=="4242:100")return a.state==n::VolumeAppState::active;return false;},"The app's gain reaches its row");}
    catch(const std::invalid_argument&){threw=true;}
    check(!threw,"Volume and per-app gain changes with the ill-formed app listed never throw");
    h.runtime->stop();
}
// An endpoint whose name is within the worker's 32767 units but past a Volume
// row's 4096 UTF-8 bytes (2000 CJK characters, 6000 bytes): it passed the
// worker, then failed the Volume canvas's snapshot check, which disabled the
// module. The Volume row shows the endpoint ID instead, as for an empty name,
// and so does the Event Log (endpointLabelsInEventLog).
void longEndpointName(){
    Harness h;const std::wstring longName(2000,L'\u6587');
    {std::lock_guard l(h.world->m);h.world->outputs={{speaker,longName,L"HDAUDIO",1u},{usb,L"USB Headset",L"USB",5u}};h.world->inputs={{mic,L"Microphone",L"HDAUDIO",4u}};h.world->defaultOutput=speaker;h.world->defaultInput=mic;}
    auto&binding=h.runtime->volume();const auto callbacks=binding.callbacks();n::VolumeSnapshot delivered=binding.snapshot();
    // The canvas checks every snapshot it receives (VolumeCanvas::receive).
    binding.setReceiver([&](const n::VolumeSnapshot&s){n::validateVolumeSnapshot(s);delivered=s;});
    check(h.runtime->setDeviceEvents(true)==0,"Event Log device list vote");
    bool threw{};
    try{binding.setVisible(true);callbacks.setActive(true);
        h.until([&]{return delivered.canSetVolume&&delivered.outputs.size()==2;},"Volume lists both outputs");}
    catch(const std::invalid_argument&){threw=true;}
    check(!threw,"An endpoint name past 4096 UTF-8 bytes never fails the Volume canvas's snapshot check");
    const auto named=[&](std::string_view id)->std::string{for(const auto&d:delivered.outputs)if(d.id==id)return d.name;return "missing";};
    check(named("{0.0.0.00000000}.{speaker}")=="{0.0.0.00000000}.{speaker}","The Volume row shows the endpoint ID for the long name");
    check(named("{0.0.0.00000000}.{usb}")=="USB Headset"&&delivered.outputID=="{0.0.0.00000000}.{speaker}","Other endpoints keep their names and the default stays selected");
    h.until([&]{return h.runtime->deviceLabels().has_value();},"The Event Log device list arrives");
    const auto labels=h.runtime->deviceLabels();
    check(labels->size()==3&&labels->at("{0.0.0.00000000}.{speaker}")=="{0.0.0.00000000}.{speaker}","The Event Log labels the long name with the endpoint ID, as Volume does");
    // A name with an unpaired surrogate (valid at both wchar_t widths) made
    // the whole endpoint list unavailable; the row shows the ID instead.
    {std::lock_guard l(h.world->m);h.world->outputs[1].name=L"USB "+std::wstring(1,static_cast<wchar_t>(0xD800));}
    threw=false;
    try{h.wake(&World::topologyWake);h.until([&]{return named("{0.0.0.00000000}.{usb}")=="{0.0.0.00000000}.{usb}";},"The ill-formed endpoint name reaches Volume");}
    catch(const std::exception&){threw=true;}
    check(!threw&&delivered.outputs.size()==2&&named("{0.0.0.00000000}.{speaker}")=="{0.0.0.00000000}.{speaker}"&&delivered.outputID=="{0.0.0.00000000}.{speaker}"&&delivered.canSetVolume,"An endpoint name that is not well-formed UTF-16 shows the endpoint ID, and the list stays available");
    // A name past 4096 UTF-8 bytes only with its trailing spaces: Volume
    // checks the raw name and shows the ID, and the Event Log, which lists
    // trimmed names, chose from the trimmed one and kept the name. Both now
    // choose on the raw name. The microphone leaves so the list changes.
    {std::lock_guard l(h.world->m);h.world->outputs[1].name=std::wstring(4095,L'a')+L"  ";h.world->inputs.clear();}
    h.seen={};h.wake(&World::topologyWake);
    h.until([&]{return h.seen.devices&&h.runtime->deviceLabels()&&h.runtime->deviceLabels()->size()==2;},"The microphone's removal reaches the Event Log");
    const auto padded=h.runtime->deviceLabels();
    check(named("{0.0.0.00000000}.{usb}")=="{0.0.0.00000000}.{usb}"&&padded->at("{0.0.0.00000000}.{usb}")=="{0.0.0.00000000}.{usb}","A name past 4096 UTF-8 bytes with its trailing spaces is the endpoint ID in both Volume and the Event Log");
    h.runtime->stop();
}
// The Event Log's device list labels each endpoint as its Volume row does
// (audioEndpointLabel). An endpoint name that is not well-formed UTF-16 made
// deviceLabels() withhold every list while it existed, so no device change was
// recorded; a name past 4096 UTF-8 bytes was handed over raw, and the Event
// Log drops a device name that long. Both now give the endpoint ID, and a
// device arriving and leaving beside them is still reported.
void endpointLabelsInEventLog(){
    Harness h;const std::wstring longName(2000,L'\u6587'),illFormed=L"USB "+std::wstring(1,static_cast<wchar_t>(0xD800));
    {std::lock_guard l(h.world->m);h.world->outputs={{speaker,longName,L"HDAUDIO",1u},{usb,illFormed,L"USB",5u}};}
    check(h.runtime->setDeviceEvents(true)==0,"Event Log device list vote");
    h.until([&]{return h.seen.devices;},"The first device list settles");
    const auto labelled=[](const std::map<std::string,std::string>&labels){
        return labels.at("{0.0.0.00000000}.{speaker}")=="{0.0.0.00000000}.{speaker}"&&labels.at("{0.0.0.00000000}.{usb}")=="{0.0.0.00000000}.{usb}";};
    auto labels=h.runtime->deviceLabels();
    check(labels&&labels->size()==2&&labelled(*labels),"The over-long and ill-formed names are listed with their endpoint IDs");
    {std::lock_guard l(h.world->m);h.world->inputs={{mic,L"Microphone",L"HDAUDIO",4u}};}
    h.seen={};h.wake(&World::topologyWake);
    h.until([&]{return h.seen.devices;},"The microphone's arrival settles");
    labels=h.runtime->deviceLabels();
    check(labels&&labels->size()==3&&labels->at("{0.0.1.00000000}.{mic}")=="Microphone"&&labelled(*labels),"A device arriving beside them reaches the Event Log");
    {std::lock_guard l(h.world->m);h.world->inputs.clear();}
    h.seen={};h.wake(&World::topologyWake);
    h.until([&]{return h.seen.devices;},"The microphone's removal settles");
    labels=h.runtime->deviceLabels();
    check(labels&&labels->size()==2&&!labels->contains("{0.0.1.00000000}.{mic}")&&labelled(*labels),"A device leaving beside them reaches the Event Log");
    h.runtime->stop();
}
#ifdef EHUD_AUDIO_RUNTIME_REGISTRY
// A Volume publish that throws (here the receiver; in 1.3.1 an ill-formed
// per-app name reaching the throwing UTF-8 conversion) escaped the UI's audio
// message, which drained outside module isolation: the overlay host stopped
// its frames, then the run ended. The application's audio message
// (drainAudio) and its other binding calls (volumeBindingCall) run Volume
// under the Volume owner's isolation: only Volume is disabled, the message
// keeps draining for Now Playing and the Event Log, and the binding closes,
// so the dead Volume's vote no longer reads endpoints and sessions when the
// HUD opens again.
struct VolumeOwner final:endfield::app::ModuleOwner {std::string_view name()const noexcept override{return "volume";}};
void volumeFailure(){
    namespace app=endfield::app;
    Harness h;
    {std::lock_guard l(h.world->m);h.world->outputs={{speaker,L"Speakers",L"HDAUDIO",1u}};h.world->inputs={{mic,L"Microphone",L"HDAUDIO",4u}};h.world->defaultOutput=speaker;h.world->defaultInput=mic;
        h.world->sessions={{"{player}","4242:100",L"Player",4242,true,true,.8f,0}};}
    VolumeOwner owner;app::ModuleRegistry registry(true);registry.add(owner); // the owner outlives its registration
    h.message=[&]{return app::drainAudio(registry,&owner,*h.runtime);};
    auto&binding=h.runtime->volume();const auto callbacks=binding.callbacks();
    n::VolumeSnapshot delivered=binding.snapshot();bool failing{};
    binding.setReceiver([&](const n::VolumeSnapshot&s){if(failing)throw std::invalid_argument("synthetic Volume publish failure");delivered=s;});
    const auto visible=[&](bool value,const char*operation){(void)app::volumeBindingCall(registry,&owner,*h.runtime,operation,[&]{(void)binding.setVisible(value);});};
    check(h.runtime->setDeviceEvents(true)==0&&h.runtime->setNowPlayingVisible(true)==0,"Event Log and Now Playing votes");
    visible(true,"open");callbacks.setActive(true);
    h.until([&]{return delivered.canSetVolume&&delivered.applications.size()==1&&h.runtime->deviceLabels()&&route(h.runtime->applications(),"4242:100");},"Volume, Now Playing and the Event Log are served");
    failing=true;
    bool escaped{};
    try{{std::lock_guard l(h.world->m);h.world->master=.3f;}h.wake(&World::volumeWake);
        h.until([&]{return !registry.enabled(owner);},"The throwing Volume publish is reached");}
    catch(const std::invalid_argument&){escaped=true;}
    check(!escaped,"A throwing Volume publish never escapes the audio message to the host");
    check(registry.failures().size()==1&&registry.failures()[0].owner=="volume"&&registry.failures()[0].operation=="audio","Only Volume is disabled, by the audio message");
    check(!binding.active(),"The failed Volume's binding is closed and its vote withdrawn");
    // The message keeps draining for every other audio user.
    {std::lock_guard l(h.world->m);h.world->sessions.push_back({"{second}","5151:100",L"Second",5151,true,true,.7f,0});}
    h.seen={};h.wake(&World::sessionWake);
    h.until([&]{return h.seen.applications&&route(h.runtime->applications(),"5151:100");},"Now Playing's app list keeps updating");
    check(h.runtime->setApplicationGain("4242:100",.5),"Now Playing's app volume is accepted");
    h.until([&]{const auto*r=route(h.runtime->applications(),"4242:100");return r&&r->state==n::AudioApplicationRouteState::active&&r->gain&&*r->gain==.5f;},"Now Playing's app volume keeps updating");
    {std::lock_guard l(h.world->m);h.world->outputs.push_back({usb,L"USB Headset",L"USB",5u});}
    h.seen={};h.wake(&World::topologyWake);
    h.until([&]{const auto labels=h.runtime->deviceLabels();return h.seen.devices&&labels&&labels->count("{0.0.0.00000000}.{usb}");},"The Event Log device list keeps updating");
    check(!h.seen.volume,"The closed binding publishes nothing");
    // Close the HUD (Now Playing leaves too), then open it again: the dead
    // Volume reads nothing. A device change the Event Log hears proves the
    // worker is awake.
    check(h.runtime->stopApplication("4242:100"),"Restore the route");
    check(h.runtime->setNowPlayingVisible(false)==0,"Now Playing leaves");visible(false,"close");
    h.until([&]{return h.runtime->snapshot().applicationsPaused&&h.runtime->snapshot().paused;},"Nothing but the device list is read while closed");
    const auto before=h.runtime->stats();
    visible(true,"open");callbacks.setActive(true);
    {std::lock_guard l(h.world->m);h.world->outputs.pop_back();}
    h.seen={};h.wake(&World::topologyWake);
    h.until([&]{const auto labels=h.runtime->deviceLabels();return h.seen.devices&&labels&&!labels->count("{0.0.0.00000000}.{usb}");},"The Event Log hears the next device change");
    const auto after=h.runtime->stats();
    {std::lock_guard l(h.world->m);check(after.sessionReads==before.sessionReads&&after.volumeReads==before.volumeReads&&h.world->bound.empty(),"Opening again reads no endpoint volume and no session for the failed Volume");}
    check(registry.failures().size()==1,"No further failure reaches the registry");
    h.runtime->stop();
}
// The production notice (app::audioNotice) dropped PostMessageW's result: one
// post that failed (here the window's posted-message quota is full) left the
// coalescer's pending flag set, so no later publication posted the owner
// message and audio went silent for the run. The failed post now throws, the
// coalescer re-arms, and the next publication is delivered.
void noticeRetry(){
    namespace app=endfield::app;
    constexpr UINT audioMessage=WM_APP+233,filler=WM_APP+1;constexpr WPARAM generation=7;
    const HWND window=CreateWindowExW(0,L"STATIC",L"Audio notice fixture",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(window&&!IsWindowVisible(window),"The notice posts to an owned message-only window");
    const auto world=std::make_shared<World>();{std::lock_guard l(world->m);world->outputs={{speaker,L"Speakers",L"HDAUDIO",1u}};}
    n::AudioServiceOptions o;o.endpoints=[w=world]{return std::make_unique<Endpoints>(w);};o.sessions=[w=world]{return std::make_unique<Sessions>(w);};o.topologySettleSeconds=.02;
    // Counts the worker's notice calls around the production notice itself.
    std::atomic<unsigned>attempts{},failures{};
    auto runtime=std::make_unique<n::AudioRuntime>(std::move(o),c::Language::english,[notice=app::audioNotice(window,audioMessage,generation),&attempts,&failures]{
        ++attempts;try{notice();}catch(...){++failures;throw;}});
    const auto posted=[&]{MSG m{};unsigned count{};while(PeekMessageW(&m,window,audioMessage,audioMessage,PM_REMOVE)){check(m.wParam==generation,"The owner message carries the window's generation");++count;}return count;};
    const auto attempted=[&](unsigned after){const auto deadline=std::chrono::steady_clock::now()+10s;while(attempts.load()<=after&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);return attempts.load()>after;};
    unsigned filled{};while(filled<1000000&&PostMessageW(window,filler,0,0))++filled;
    check(filled>0&&filled<1000000,"The window's posted-message quota is full");
    check(runtime->setDeviceEvents(true)==0,"Event Log device list vote");
    check(attempted(0),"The first publication calls the notice");
    // Wait for the worker to go quiet (the device list settles after .02 s):
    // the counters read together only once no post is in flight, so a late
    // publication cannot fall between them.
    const auto quiet=[&]{
        const auto deadline=std::chrono::steady_clock::now()+10s;auto since=std::chrono::steady_clock::now();
        unsigned a=attempts.load(),f=failures.load();
        while(std::chrono::steady_clock::now()<deadline){
            std::this_thread::sleep_for(1ms);const unsigned na=attempts.load(),nf=failures.load();
            if(na!=a||nf!=f){a=na;f=nf;since=std::chrono::steady_clock::now();}
            else if(a==f&&std::chrono::steady_clock::now()-since>=100ms)return true;
        }
        return false;};
    check(quiet(),"Every post into the full queue failed and threw, and the worker is quiet");
    const unsigned failed=failures.load(),before=attempts.load();
    {MSG m{};while(PeekMessageW(&m,window,filler,filler,PM_REMOVE)){}}
    check(posted()==0,"Nothing was posted while the queue was full");
    // No drain() ran: only the coalescer's re-arm lets the next publication post.
    {std::lock_guard l(world->m);world->outputs.push_back({usb,L"USB Headset",L"USB",5u});}
    {n::AudioEndpointBackend::Wake w;{std::lock_guard l(world->m);w=world->topologyWake;}check(bool(w),"Native notification is registered");w();}
    check(attempted(before),"A later publication calls the notice again after a failed post");
    const auto deadline=std::chrono::steady_clock::now()+10s;unsigned delivered{};
    while(!(delivered=posted())&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
    check(delivered==1,"The later publication's owner message is delivered");
    check(failed>0&&failed==before,"Every post into the full queue failed and threw");
    bool adopted{};
    for(const auto until=std::chrono::steady_clock::now()+10s;!adopted&&std::chrono::steady_clock::now()<until;){
        (void)posted();(void)runtime->drain();const auto labels=runtime->deviceLabels();adopted=labels&&labels->count("{0.0.0.00000000}.{usb}");
        if(!adopted)std::this_thread::sleep_for(1ms);}
    check(adopted,"The owner adopts the later publication");
    runtime->stop();DestroyWindow(window);
}
// The application's own wiring (app/, read as text: the hidden owner
// coverage never constructs AudioRuntime). drain() no longer republishes
// Volume, so a caller that drains directly would silently freeze the Volume
// page, and a binding call outside volumeBindingCall would publish outside
// Volume's isolation. Every AudioRuntime drain goes through drainAudio and
// every publishing binding call through volumeBindingCall. Reading the text is
// the only way to pin this wiring, so a failure names itself as a text match:
// reformatting these lines fails it too, and then the expected text is updated.
void appWiring(const std::filesystem::path&app){
    check(std::filesystem::is_directory(app),"The application sources are passed");
    const auto wired=[](bool value,const std::string&what){++checks;if(!value)throw std::runtime_error("app/ wiring text check: "+what+" (reformatting these lines? update the expected text in appWiring)");};
    const std::regex call(R"(audio(->|\.)(drain\(|volume\(\)\.(receive|setVisible|setErrorText)\(|setLanguage\())");
    unsigned guarded{},drains{};
    for(const auto&entry:std::filesystem::recursive_directory_iterator(app)){
        const auto file=entry.path();if(!entry.is_regular_file()||(file.extension()!=".cpp"&&file.extension()!=".hpp")||file.filename()=="application_audio.hpp")continue;
        std::ifstream in(file,std::ios::binary);check(bool(in),"Read an application source");std::string line;
        while(std::getline(in,line)){
            if(line.find("drainAudio(registry,volumeModuleOwner.get(),*audio)")!=line.npos)++drains;
            for(auto it=std::sregex_iterator(line.begin(),line.end(),call);it!=std::sregex_iterator();++it){
                wired((*it)[2].str()!="drain(","AudioRuntime::drain() is called outside drainAudio in "+file.filename().string());
                wired(line.find("volumeBindingCall(registry,volumeModuleOwner.get(),")!=line.npos,"A Volume binding call publishes outside volumeBindingCall in "+file.filename().string()+": "+(*it)[0].str());
                ++guarded;
            }
        }
    }
    wired(drains==1,"The audio message drains through drainAudio under the Volume owner");
    wired(guarded>=4,"Open, close, force close and the language run the binding under the Volume owner");
}
#endif
// The same through the Volume canvas, as DesignOwnerCore drives a module
// switch (show, setActive, playEntrance; prepareToLeave, setActive(false)):
// the page Volume shows when it enters keeps its staggered entrance through
// the worker's first read, and the leaving page is not rebuilt, from the
// first switch after launch on.
void volumePage(){
    namespace d=endfield::modules::design;
    Harness h;
    {std::lock_guard l(h.world->m);h.world->outputs={{speaker,L"Speakers",L"HDAUDIO",1u}};h.world->inputs={{mic,L"Microphone",L"HDAUDIO",4u}};h.world->defaultOutput=speaker;h.world->defaultInput=mic;
        h.world->sessions={{"{player}","4242:100",L"Player",4242,true,true,.8f,0}};}
    // Deterministic text widths (.55 em per byte; the layout is not under test).
    struct Measurer final:d::TextMeasurer {
        double width(std::span<const d::TextRun>runs,d::TextRegion)override{double w{};for(const auto&r:runs)w+=double(r.text.size())*r.style.size*.55;return w;}
        d::LineMetrics metrics(const d::TextStyle&s,d::TextRegion,double)override{return {s.size*s.lineHeight,s.size*.8,s.size*.2};}
    }measurer;
    d::Context context(measurer,d::asciiUppercase);double now=100;
    context.update(d::geometry::baseUnit,2/d::geometry::baseUnit,now);
    d::Context::Appearance appearance;appearance.reduceMotion=false;context.apply(appearance,now);
    auto&binding=h.runtime->volume();binding.setVisible(true);
    endfield::modules::VolumeCanvas canvas(context,binding.snapshot(),binding.callbacks());
    unsigned rebuilt{};const d::Page*page{};
    binding.setReceiver([&](const n::VolumeSnapshot&s){canvas.receive(s);if(page&&&canvas.page()!=page)++rebuilt;page=&canvas.page();});
    const auto enter=[&]{now+=1;canvas.show(now);canvas.setActive(true,now);canvas.playEntrance({0,24},now);page=&canvas.page();};
    const auto leave=[&]{now+=1;canvas.prepareToLeave(now);canvas.setActive(false,now);};
    // The first switch after launch: the page is built before anything was
    // read (no devices, no level, no per-app support); the worker's first read
    // fills it in place, so the page keeps its entrance.
    rebuilt=0;enter();
    {
        const std::vector<d::Layer*>rows(canvas.page().staggered().begin(),canvas.page().staggered().end());
        check(!binding.snapshot().applicationActivitySupported&&!binding.snapshot().canSetVolume&&canvas.header().counterText()=="00","The first page is built before the first read");
        h.until([&]{return binding.snapshot().applicationActivitySupported&&binding.snapshot().canSetVolume;},"The first visit reads the devices");
        check(rebuilt==0&&&canvas.page()==page&&rows.size()==5,"The first switch to Volume after launch keeps the page it entered with");
        check(std::all_of(rows.begin(),rows.end(),[&](const d::Layer*row){return row->animating(now+.02);}),"Its tab bar and rows keep their staggered entrance");
        check(canvas.header().counterText()=="50"&&canvas.snapshot().outputs.size()==1,"The read fills the level and the devices in place");
    }
    leave();h.until([&]{return h.runtime->snapshot().paused;},"Volume left");
    for(unsigned visit=0;visit<3;++visit){
        rebuilt=0;
        enter();
        const std::vector<d::Layer*>rows(canvas.page().staggered().begin(),canvas.page().staggered().end());
        h.until([&]{return !h.runtime->snapshot().paused;},"The worker reads again");
        check(rebuilt==0&&&canvas.page()==page&&rows.size()==5,"Switching back to Volume keeps the page it entered with");
        check(std::all_of(rows.begin(),rows.end(),[&](const d::Layer*row){return row->animating(now+.02);}),"Its tab bar and rows keep their staggered entrance");
        leave();
        check(rebuilt==0&&&canvas.page()==page,"The leaving page is not rebuilt");
        h.until([&]{return h.runtime->snapshot().paused;},"Volume left again");
        check(rebuilt==0,"The worker's pause leaves the page alone");
    }
    h.runtime->stop();
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
int main(int argc,char**argv){
    try{construction();coalescing();devices();volume();volumeRevisit();malformedAppName();longEndpointName();endpointLabelsInEventLog();
#ifdef EHUD_AUDIO_RUNTIME_REGISTRY
        check(argc==2,"Pass the application source directory (app/)");
        volumeFailure();noticeRetry();appWiring(argv[1]);
#else
        (void)argc;(void)argv;
#endif
        volumePage();nowPlaying();std::cout<<"PASS "<<checks<<" injected audio runtime checks (no native audio accessed)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
