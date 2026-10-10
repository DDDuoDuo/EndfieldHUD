// Live acceptance probe for the Windows Volume / per-app audio service. It is a
// console tool for the USER to run on their own machine; automated tests only
// run the inert default invocation.
//
//   audio_probe                           usage only; touches nothing
//   audio_probe --live [--seconds N]      READ-ONLY: active output/input
//                                         endpoints with their Bluetooth and
//                                         headphone traits, the default devices,
//                                         the default output's volume/mute/
//                                         balance readback and capabilities,
//                                         per-app sessions (name, PID, packaged
//                                         identity) and Event Log device
//                                         connect/disconnect events. Prints
//                                         every event-driven change (JSON lines).
//                                         Never writes a level, mute, balance,
//                                         default device or journal.
//   audio_probe --endpoints               READ-ONLY one-shot: every active
//                                         render/capture endpoint with the raw
//                                         documented properties behind its
//                                         traits (PKEY_Device_EnumeratorName,
//                                         PKEY_AudioEndpoint_FormFactor) and the
//                                         resulting Bluetooth/headphone flags.
//                                         Opens no volume control or session.
//   audio_probe --live --app-gain ID=G    additionally attenuates exactly one
//                                         listed application (ID from the
//                                         output, G in [0,1]) and restores it
//                                         when the probe exits (journaled in
//                                         %TEMP%\EndfieldHUD-audio-probe).
//
// Suggested checks: plug/unplug headphones, connect a Bluetooth headset, switch
// the default device in Windows Settings > System > Sound, change the volume
// with the keyboard, start/stop a player. Each must appear without polling.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "native/audio_service.hpp"
#include "core/data/json.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>

namespace n=endfield::native;using Json=ehud::data::Json;
namespace {
struct Wake {
    std::mutex mutex;std::condition_variable signal;bool pending{};
    void post(){{std::lock_guard lock(mutex);pending=true;}signal.notify_one();}
    void wait(double seconds){std::unique_lock lock(mutex);signal.wait_for(lock,std::chrono::duration<double>(std::max(0.,seconds)),[&]{return pending;});pending=false;}
};
const auto origin=std::chrono::steady_clock::now();
double monotonic(){return std::chrono::duration<double>(std::chrono::steady_clock::now()-origin).count();}
void emit(Json value){value["t"]=std::round(monotonic()*1000)/1000;std::cout<<value.encode()<<std::endl;}
std::string text(std::wstring_view value){return n::audioUtf8(value).value_or("<invalid UTF-16>");}
Json endpoints(const std::vector<n::AudioEndpoint>&list,const std::wstring&defaultID){
    Json::Array out;for(const auto&d:list)out.emplace_back(Json::Object{{"id",text(d.id)},{"name",text(d.name)},{"bluetooth",d.bluetooth},{"headphones",d.headphones},{"default",d.id==defaultID}});return out;
}
Json optional(const std::optional<float>&v){return v?Json(static_cast<double>(*v)):Json{};}
void report(const n::AudioEndpointSnapshot&s,n::AudioTopologyRecorder&recorder,std::uint64_t&generation){
    emit(Json::Object{{"event","endpoints"},{"outputs",endpoints(s.outputs,s.defaultOutputID)},{"inputs",endpoints(s.inputs,s.defaultInputID)},{"error",s.error},{"inputError",s.inputError}});
    emit(Json::Object{{"event","controls"},{"available",s.available},{"volume",optional(s.volume)},{"muted",s.muted?Json(*s.muted):Json{}},{"balance",optional(s.balance)},
        {"canSetVolume",s.canSetVolume},{"canSetMute",s.canSetMute},{"canSetBalance",s.canSetBalance},{"commandError",s.commandError}});
    Json::Array apps;for(const auto&a:s.applications)apps.emplace_back(Json::Object{{"id",a.id},{"name",text(a.name)},{"pid",static_cast<std::int64_t>(a.pid)},{"available",a.available},
        {"state",a.state==n::AudioApplicationRouteState::active?"active":a.state==n::AudioApplicationRouteState::failed?"failed":"direct"},{"gain",optional(a.gain)},{"appUserModelID",a.appUserModelID},{"error",a.error}});
    emit(Json::Object{{"event","applications"},{"supported",s.applicationsSupported},{"routesStoppedByDeviceChange",s.routesStoppedByDeviceChange},{"error",s.applicationError},{"applications",std::move(apps)}});
    if(s.topologyGeneration!=generation){
        generation=s.topologyGeneration;
        for(const auto&e:recorder.receive(s.topology))emit(Json::Object{{"event",e.connected?"audioDeviceConnected":"audioDeviceDisconnected"},{"device",e.device}});
    }
}
}
int endpointsOnly(){
    // The backend initializes this thread's MTA itself; nothing is written.
    auto backend=n::nativeAudioEndpointBackendFactory()();
    if(const auto hr=backend->open([]{});hr<0){emit(Json::Object{{"event","error"},{"status",hr}});return 1;}
    for(const auto flow:{n::AudioFlow::output,n::AudioFlow::input}){
        std::vector<n::AudioEndpointRecord>records;std::wstring defaultID;
        const auto hr=backend->enumerate(flow,records);(void)backend->defaultEndpoint(flow,defaultID);
        Json::Array list;
        for(const auto&r:records)list.emplace_back(Json::Object{{"id",text(r.id)},{"name",text(r.name)},{"enumeratorName",text(r.enumerator)},
            {"formFactor",r.formFactor?Json(static_cast<std::int64_t>(*r.formFactor)):Json{}},{"bluetooth",n::audioEndpointBluetooth(r.enumerator)},
            {"headphones",n::audioEndpointHeadphones(r.formFactor)},{"default",r.id==defaultID}});
        emit(Json::Object{{"event",flow==n::AudioFlow::output?"outputs":"inputs"},{"status",hr},{"endpoints",std::move(list)}});
    }
    backend->close();
    return 0;
}
int wmain(int argc,wchar_t**argv){
    bool live{};double seconds{30};std::optional<std::pair<std::string,float>>gain;
    if(argc==2&&std::wstring_view(argv[1])==L"--endpoints")return endpointsOnly();
    for(int i=1;i<argc;++i){
        const std::wstring_view a(argv[i]);
        if(a==L"--live")live=true;
        else if(a==L"--seconds"&&i+1<argc){seconds=std::wcstod(argv[++i],nullptr);if(!std::isfinite(seconds)||seconds<=0||seconds>3600){std::cerr<<"--seconds must be in (0,3600]\n";return 2;}}
        else if(a==L"--app-gain"&&i+1<argc){
            const auto value=text(argv[++i]);const auto equals=value.rfind('=');
            if(equals==std::string::npos||!equals){std::cerr<<"--app-gain needs ID=GAIN\n";return 2;}
            const auto g=std::strtod(value.c_str()+equals+1,nullptr);if(!std::isfinite(g)||g<0||g>1){std::cerr<<"GAIN must be in [0,1]\n";return 2;}
            gain=std::pair{value.substr(0,equals),static_cast<float>(g)};
        }else{std::cerr<<"Unknown argument\n";return 2;}
    }
    if(!live){
        std::cout<<"audio_probe: Run only on your own machine. Usage: audio_probe --endpoints | audio_probe --live [--seconds N] [--app-gain ID=GAIN]\n"
                   "Without --app-gain the probe is read-only; it never changes the default device.\n";
        return 0;
    }
    std::optional<std::filesystem::path>journal;
    if(gain){wchar_t temp[MAX_PATH+1]{};const auto length=GetTempPathW(MAX_PATH,temp);if(!length||length>MAX_PATH){std::cerr<<"No temporary directory\n";return 1;}journal=std::filesystem::path(temp)/L"EndfieldHUD-audio-probe";}
    auto options=n::nativeAudioServiceOptions(journal);
    Wake wake;n::AudioService service(std::move(options),[&wake]{wake.post();});
    if(journal)service.recover();
    service.vote(n::AudioVoter::eventLog,true);service.vote(n::AudioVoter::volume,true);
    n::AudioTopologyRecorder recorder;std::uint64_t generation{~0ull};bool applied{};
    emit(Json::Object{{"event","start"},{"readOnly",!gain.has_value()},{"seconds",seconds}});
    const auto end=monotonic()+seconds;
    while(monotonic()<end){
        wake.wait(end-monotonic());
        if(!service.drain())continue;
        report(service.snapshot(),recorder,generation);
        if(gain&&!applied){
            const auto&apps=service.snapshot().applications;
            if(std::any_of(apps.begin(),apps.end(),[&](const auto&a){return a.id==gain->first;})){applied=service.setApplicationGain(gain->first,gain->second);emit(Json::Object{{"event","appGainRequested"},{"id",gain->first},{"gain",static_cast<double>(gain->second)},{"accepted",applied}});}
        }
    }
    const auto stats=service.stats();
    service.stop(); // restores any owned attenuation and unregisters every notification
    emit(Json::Object{{"event","stop"},{"workerWakes",static_cast<std::int64_t>(stats.wakes)},{"topologyReads",static_cast<std::int64_t>(stats.topologyReads)},
        {"volumeReads",static_cast<std::int64_t>(stats.volumeReads)},{"sessionReads",static_cast<std::int64_t>(stats.sessionReads)},{"published",static_cast<std::int64_t>(stats.published)}});
    return 0;
}
#else
int main(){return 0;}
#endif
