#include "native/audio_service.hpp"
#include "native/audio_default_device.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <windows.h>
#include <tlhelp32.h>

// Windows-only contracts that change nothing: nothing here creates the native
// backends or changes any endpoint, session level or default device. The one
// native call is read-only: the IPolicyConfig capability (Windows build and
// CoCreateInstance) and the current defaults, on a thread of its own as the
// audio worker does. No default device is ever selected.
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
            check(options.endpoints&&options.sessions&&options.order&&options.defaults&&!options.journal,"Native options carry factories (the default-device selector included) and no journal without a data root");
            n::AudioService service(std::move(options),{});
            check(!service.stats().workerStarted&&threads()==before,"Constructing the native audio service starts no thread and opens no audio interface");
            check(service.snapshot().paused&&!service.snapshot().topologyActive,"Unvoted native service exposes nothing");
        }
        const auto rooted=n::nativeAudioServiceOptions(std::filesystem::path(L"C:\\Synthetic\\EndfieldHUD\\v1"));
        check(rooted.journal&&*rooted.journal==std::filesystem::path(L"C:\\Synthetic\\EndfieldHUD\\v1\\Audio\\RouteJournal.json"),"The journal lives under the supplied versioned data root");
        // IPolicyConfig (default-device switching): capability and current
        // defaults only, read on a fresh thread like the audio worker. Never
        // validate or select: no default device changes here.
        std::exception_ptr failure;
        std::thread([&failure]{try{
            auto access=n::nativeAudioDefaultDeviceAccess();
            check(access.capability&&access.validateEndpoint&&access.current&&access.select,"The native access is complete");
            const auto capability=access.capability();
            check(capability.supported?(capability.windowsMajor==10&&capability.windowsBuild>=14393&&capability.status>=0):capability.status<0,"The capability reports the Windows build and the interface honestly");
            std::cout<<"IPolicyConfig capability: supported="<<capability.supported<<" Windows="<<capability.windowsMajor<<'.'<<capability.windowsMinor<<" build="<<capability.windowsBuild
                <<" HRESULT=0x"<<std::hex<<static_cast<std::uint32_t>(capability.status)<<std::dec<<'\n';
            const auto again=access.capability();check(again.supported==capability.supported&&again.status==capability.status,"The capability is read once and kept");
            std::int32_t wrong{};std::thread([&]{std::wstring id;wrong=access.current(n::AudioDefaultFlow::output,n::AudioDefaultRole::console,id);}).join();
            check(wrong==static_cast<std::int32_t>(0x8001010Eu),"The access answers only on the thread that created it (RPC_E_WRONG_THREAD elsewhere)");
            if(!capability.supported)return;
            for(const auto flow:{n::AudioDefaultFlow::output,n::AudioDefaultFlow::input})
                for(const auto role:{n::AudioDefaultRole::console,n::AudioDefaultRole::multimedia,n::AudioDefaultRole::communications}){
                    std::wstring id;const auto hr=access.current(flow,role,id);
                    check(hr<0||!id.empty(),"A current default that reads has an identity");
                    std::cout<<"  current "<<(flow==n::AudioDefaultFlow::input?"input":"output")<<" role "<<static_cast<int>(role)<<": HRESULT=0x"<<std::hex<<static_cast<std::uint32_t>(hr)<<std::dec<<(id.empty()?" (none)":" (an endpoint)")<<'\n';
                }
        }catch(...){failure=std::current_exception();}}).join();
        if(failure)std::rethrow_exception(failure);
        std::cout<<"PASS "<<checks<<" native audio contracts (nothing changed)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
