#include "native/audio_endpoint_model.hpp"
#include "native/system_services.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>

// Source: bash windows/tools/audio_reference.sh (unchanged Mac sources at
// ca04f142; SystemEventRecorder, AudioVolumeMath, localizedStandardCompare).
namespace {
namespace n=endfield::native;using Json=ehud::data::Json;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
Json fixture(const std::filesystem::path&path){
    std::ifstream file(path,std::ios::binary);check(bool(file),"Explicit Mac audio oracle fixture exists");
    std::stringstream text;text<<file.rdbuf();return Json::parse(text.str());
}
std::wstring wide(const std::string&value){const auto w=n::audioWide(value);check(w.has_value(),"Fixture text is valid UTF-8");return *w;}

void classification(){
    using F=n::AudioEndpointFormFactor;
    check(n::audioEndpointHeadphones(static_cast<std::uint32_t>(F::headphones))&&n::audioEndpointHeadphones(static_cast<std::uint32_t>(F::headset)),"Headphones and headsets are headphone endpoints");
    for(const auto f:{F::remoteNetworkDevice,F::speakers,F::lineLevel,F::microphone,F::handset,F::unknownDigitalPassthrough,F::spdif,F::digitalAudioDisplayDevice,F::unknownFormFactor})
        check(!n::audioEndpointHeadphones(static_cast<std::uint32_t>(f)),"Every other documented form factor is not a headphone");
    check(!n::audioEndpointHeadphones(std::nullopt)&&!n::audioEndpointHeadphones(99u),"Missing or unknown form factor is never guessed");
    for(const auto bus:{L"BTHENUM",L"bthenum",L"BTHHFENUM",L"BthHfEnum",L"BTHLEDEVICE",L"BTHLE"})check(n::audioEndpointBluetooth(bus),"Documented Bluetooth Classic/LE enumerators are Bluetooth");
    for(const auto bus:{L"USB",L"HDAUDIO",L"SWD",L"",L"BTH",L"BTHENUMX",L"Bluetooth Speaker",L"MMDEVAPI"})check(!n::audioEndpointBluetooth(bus),"Other buses and device names are never guessed as Bluetooth");
}
void controls(const Json&root){
    n::AudioEndpointVolumeState v;v.scalar=.55f;v.muted=true;v.channels=2;v.left=.4f;v.right=.8f;v.minimumDecibels=-65.25f;v.maximumDecibels=0;v.incrementDecibels=.03125f;v.steps=100;
    auto c=n::audioEndpointControls(v);
    check(c.available&&c.volume==.55f&&c.canSetVolume&&c.muted==true&&c.canSetMute&&c.canSetBalance&&c.error==0,"Readable software-volume endpoint exposes every supported control");
    check(c.balance&&std::abs(*c.balance-.5f)<1e-6f,"Stereo balance uses the source peak-preserving math");
    auto fixed=v;fixed.maximumDecibels=fixed.minimumDecibels;check(!n::audioEndpointControls(fixed).canSetVolume&&n::audioEndpointControls(fixed).volume,"Degenerate dB range is a fixed output: value shown, slider disabled");
    fixed=v;fixed.steps=1;check(!n::audioEndpointControls(fixed).canSetVolume,"Single-step endpoint is fixed");
    fixed=v;fixed.rangeStatus=-1;check(!n::audioEndpointControls(fixed).canSetVolume,"Unreadable range never claims a writable volume");
    check(!n::audioEndpointControls(v,true).canSetVolume&&n::audioEndpointControls(v,true).canSetMute,"An endpoint that refused a write stays fixed without affecting mute");
    check(n::audioEndpointWriteRefused(static_cast<std::int32_t>(0x80004001u))&&n::audioEndpointWriteRefused(static_cast<std::int32_t>(0x80070032u))&&!n::audioEndpointWriteRefused(static_cast<std::int32_t>(0x88890004u))&&!n::audioEndpointWriteRefused(0),"Only E_NOTIMPL/ERROR_NOT_SUPPORTED mark a fixed output");
    auto broken=v;broken.status=-7;c=n::audioEndpointControls(broken);check(!c.available&&!c.volume&&!c.canSetVolume&&!c.canSetMute&&c.error==-7,"Failed master read exposes no control");
    broken=v;broken.scalar=1.5f;check(!n::audioEndpointControls(broken).available&&n::audioEndpointControls(broken).error<0,"Out-of-range scalar is rejected");
    broken=v;broken.muteStatus=-3;check(!n::audioEndpointControls(broken).muted&&!n::audioEndpointControls(broken).canSetMute,"Unreadable mute is unavailable");
    auto surround=v;surround.channels=6;check(!n::audioEndpointControls(surround).balance&&!n::audioEndpointControls(surround).canSetBalance,"Multichannel endpoints keep no fake stereo balance");
    auto silent=v;silent.left=silent.right=0;check(!n::audioEndpointControls(silent).balance,"All-zero channels have no balance (the source computes balance only for a nonzero peak)");
    for(const auto&row:root["balance"].array()){
        const auto left=static_cast<float>(row["left"].number()),right=static_cast<float>(row["right"].number());
        const auto native=n::audio_stereo_balance(left,right);
        if(std::max(left,right)<=0){check(!native,"Zero peak: source refresh() does not publish a balance");continue;}
        check(native&&std::abs(*native-row["balance"].number())<1e-6,"Balance equals Mac AudioVolumeMath.balance");
    }
    for(const auto&row:root["stereo"].array()){
        const auto levels=n::audio_stereo_levels(static_cast<float>(row["volume"].number()),static_cast<float>(row["balance"].number()));
        check(std::abs(levels[0]-row["levels"].array()[0].number())<1e-6&&std::abs(levels[1]-row["levels"].array()[1].number())<1e-6,"Stereo channel levels equal Mac AudioVolumeMath.stereo");
    }
}
void ordering(const Json&root){
    std::vector<std::wstring>expected;for(const auto&name:root["ordering"].array())expected.push_back(wide(name.string()));
    std::mt19937 random(7);
    for(unsigned round=0;round<8;++round){
        auto shuffled=expected;std::shuffle(shuffled.begin(),shuffled.end(),random);
        std::vector<n::AudioEndpoint>devices;for(std::size_t i=0;i<shuffled.size();++i)devices.push_back({L"id"+std::to_wstring(i),shuffled[i]});
        n::sortAudioEndpoints(devices);
        for(std::size_t i=0;i<expected.size();++i)check(devices[i].name==expected[i],"Portable endpoint order equals Mac localizedStandardCompare");
    }
    std::vector<n::AudioEndpoint>ties{{L"b",L"Same"},{L"a",L"same"},{L"c",L"Other"}};n::sortAudioEndpoints(ties);
    check(ties[0].id==L"c"&&ties[1].id==L"b"&&ties[2].id==L"a","Equal names keep enumeration order (stable)");
    std::vector<n::AudioApplicationRoute>apps(3);apps[0].id="9:1";apps[0].name=L"Player";apps[1].id="2:1";apps[1].name=L"player";apps[2].id="5:1";apps[2].name=L"Browser";
    n::sortAudioApplications(apps);check(apps[0].id=="5:1"&&apps[1].id=="2:1"&&apps[2].id=="9:1","Applications order by name, then identity");
    std::vector<n::AudioEndpoint>custom{{L"x",L"a"},{L"y",L"b"}};n::sortAudioEndpoints(custom,[](std::wstring_view a,std::wstring_view b){return a<b?1:a>b?-1:0;});
    check(custom[0].id==L"y","An injected native comparator governs ordering");
}
void topology(const Json&root){
    n::AudioTopologyRecorder recorder;std::size_t steps{};
    for(const auto&step:root["topology"].array()){
        std::vector<n::AudioTopologyDevice>devices;for(const auto&d:step["devices"].array())devices.push_back({wide(d["id"].string()),wide(d["name"].string())});
        std::reverse(devices.begin(),devices.end()); // Input order must not matter.
        const auto events=recorder.receive(devices);const auto&expected=step["events"].array();
        check(events.size()==expected.size(),"Topology step logs exactly the source events");
        for(std::size_t i=0;i<events.size();++i)check(events[i].connected==(expected[i]["kind"].string()=="connected")&&events[i].device==expected[i]["device"].string(),"Event kind, label and order equal SystemEventRecorder.recordTopology");
        ++steps;
    }
    check(steps==6&&recorder.hasBaseline(),"Every source topology step was replayed");
    std::vector<n::AudioEndpoint>outputs{{L"{0.0.0}.{b}",L" Speakers "},{L"{0.0.0}.{a}",L"Headphones"}},inputs{{L"{0.0.1}.{a}",L"Mic"}};
    auto list=n::audioTopology(outputs,inputs);
    check(list&&list->size()==3&&(*list)[0].id==L"{0.0.0}.{a}"&&(*list)[1].name==L"Speakers"&&(*list)[2].id==L"{0.0.1}.{a}","Topology unions render and capture endpoints, trimmed, by identity");
    auto incomplete=outputs;incomplete[0].name=L"  ";check(!n::audioTopology(incomplete,inputs),"An empty label makes the whole read incomplete (no false disconnect)");
    auto duplicate=inputs;duplicate[0].id=outputs[0].id;check(!n::audioTopology(outputs,duplicate),"A repeated identity makes the read incomplete");
    n::AudioTopologyRecorder fresh;check(fresh.receive(*list).empty()&&fresh.receive(*list).empty(),"Baseline and unchanged lists log nothing");
    fresh.reset();check(!fresh.hasBaseline()&&fresh.receive({}).empty(),"Reset restores the baseline rule");
}
void text(){
    const std::wstring sample=L"Speakers 扬声器 \U0001F3A7";const auto utf8=n::audioUtf8(sample);
    check(utf8&&*utf8=="Speakers \xE6\x89\xAC\xE5\xA3\xB0\xE5\x99\xA8 \xF0\x9F\x8E\xA7"&&n::audioWide(*utf8)==sample,"Endpoint labels round-trip UTF-8 exactly");
    check(!n::audioUtf8(std::wstring(1,static_cast<wchar_t>(0xD800)))&&!n::audioUtf8(std::wstring(1,L'\0')),"Unpaired surrogates and NUL are rejected");
    check(!n::audioWide("\xC0\x80")&&!n::audioWide("\xED\xA0\x80")&&!n::audioWide("\xFF"),"Overlong, surrogate and invalid UTF-8 are rejected");
}
void bounds(){
    n::AudioEndpointSnapshot s;s.outputs={{L"a",L"A"}};s.defaultOutputID=L"a";s.volume=.5f;s.balance=-1;
    check(n::validAudioEndpointSnapshot(s),"Valid snapshot accepted");
    auto bad=s;bad.outputs.push_back({L"a",L"Again"});check(!n::validAudioEndpointSnapshot(bad),"Repeated endpoint identity rejected");
    bad=s;bad.volume=std::nanf("");check(!n::validAudioEndpointSnapshot(bad),"Nonfinite volume rejected");
    bad=s;bad.outputs.resize(n::maximumAudioEndpoints+1);check(!n::validAudioEndpointSnapshot(bad),"Endpoint count is bounded");
}
}
int main(int argc,char**argv){
    try{
        check(argc==2,"Usage: audio_endpoint_model_tests <audio-endpoint-source.json>");
        const auto root=fixture(argv[1]);check(root["schemaVersion"].integer()==1&&root["sourceAuthority"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Pinned source oracle");
        classification();controls(root);ordering(root);topology(root);text();bounds();
        std::cout<<"PASS "<<checks<<" audio endpoint model checks against the Mac oracle (no native audio accessed)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
