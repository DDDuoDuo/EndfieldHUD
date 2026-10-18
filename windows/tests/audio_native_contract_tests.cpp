#include "native/audio_service.hpp"
#include "native/audio_default_device.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <windows.h>
#include <tlhelp32.h>

// Windows-only contracts that need no audio device: nothing here creates the
// native backends, opens COM audio interfaces, reads or changes any endpoint,
// session level or default device.
namespace {
namespace n=endfield::native;using Json=ehud::data::Json;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
unsigned threads(){
    // Counts this process's threads without touching audio: the service must
    // not start its worker before a vote or command.
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return 0;
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);unsigned count{};
    if(Thread32First(snapshot,&entry))do{if(entry.th32OwnerProcessID==GetCurrentProcessId())++count;}while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);return count;
}
}
int main(int argc,char**argv){
    try{
        check(argc==2,"Usage: audio_native_contract_tests <audio-endpoint-source.json>");
        std::ifstream file(argv[1],std::ios::binary);check(bool(file),"Mac oracle fixture exists");std::stringstream text;text<<file.rdbuf();const auto root=Json::parse(text.str());
        // Production ordering: CompareStringEx (user locale, linguistic case-insensitive, digits as numbers).
        std::vector<std::wstring>expected;for(const auto&name:root["ordering"].array())expected.push_back(*n::audioWide(name.string()));
        const auto order=n::nativeAudioNameOrder();std::mt19937 random(3);
        for(unsigned round=0;round<6;++round){
            auto names=expected;std::shuffle(names.begin(),names.end(),random);
            std::vector<n::AudioEndpoint>devices;for(std::size_t i=0;i<names.size();++i)devices.push_back({L"id"+std::to_wstring(i),names[i]});
            n::sortAudioEndpoints(devices,order);
            for(std::size_t i=0;i<expected.size();++i)check(devices[i].name==expected[i],"CompareStringEx ordering equals Mac localizedStandardCompare for the oracle names");
        }
        check(order(L"Speakers 2",L"speakers 10")<0&&order(L"Buds",L"buds")==0,"Digits compare as numbers; case is ignored");
        // Factories are inert until the worker constructs them.
        const auto before=threads();
        {
            auto options=n::nativeAudioServiceOptions(std::nullopt);
            check(options.endpoints&&options.sessions&&options.order&&!options.journal,"Native options carry factories and no journal without a data root");
            n::AudioService service(std::move(options),{});
            check(!service.stats().workerStarted&&threads()==before,"Constructing the native audio service starts no thread and opens no audio interface");
            check(service.snapshot().paused&&!service.snapshot().topologyActive,"Unvoted native service exposes nothing");
        }
        const auto rooted=n::nativeAudioServiceOptions(std::filesystem::path(L"C:\\Synthetic\\EndfieldHUD\\v1"));
        check(rooted.journal&&*rooted.journal==std::filesystem::path(L"C:\\Synthetic\\EndfieldHUD\\v1\\Audio\\RouteJournal.json"),"The journal lives under the supplied versioned data root");
        // Platform gap: no undocumented default-device ABI in the build.
        n::AudioDefaultDeviceSelector selector(n::nativeAudioDefaultDeviceAccess());const auto capability=selector.capability();
        check(!capability.supported&&capability.status==static_cast<std::int32_t>(0x80004001u),"Default-device switching reports E_NOTIMPL (no public API; undocumented PolicyConfig excluded)");
        const auto result=selector.select(n::AudioDefaultFlow::output,L"{0.0.0.00000000}.{synthetic}");
        check(!result.succeeded()&&result.writes==0,"No default-device write is ever attempted");
        std::cout<<"PASS "<<checks<<" native audio contracts (no audio interface opened)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
