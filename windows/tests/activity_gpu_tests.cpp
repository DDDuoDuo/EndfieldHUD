// Activity Monitor GPU usage (a Windows addition): the "GPU Engine" counter
// instance names, one collection's ordering and bounds, the visibility gate
// and, on Windows, the live collector over this test process only. The live
// part reads the system's GPU engine performance counters (no user data) and
// asserts only about this process; its optional D3D11 load is this process's
// own work on a hardware device and is skipped where none can be created.
#include "native/activity_gpu.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <chrono>
#include <thread>
#endif

namespace n=endfield::native;namespace m=endfield::modules;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
void parsing(){
    const auto a=n::activityGPUInstance("pid_1234_luid_0x00000000_0x0000C2E1_phys_0_eng_3_engtype_3D");
    check(a&&a->pid==1234&&a->engine.luid==0xC2E1&&a->engine.physical==0&&a->engine.engine==3,"Instance name: process, adapter LUID, node and engine");
    const auto b=n::activityGPUInstance("pid_7_luid_0x00000001_0xFFFFFFFF_phys_1_eng_12_engtype_VideoDecode");
    check(b&&b->engine.luid==0x1FFFFFFFFULL&&b->engine.physical==1&&b->engine.engine==12,"High and low LUID halves");
    check(n::activityGPUInstance("pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_")&&n::activityGPUInstance("pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_Video Codec"),"Short hex and any engine type label");
    for(const auto*bad:{"pid_0_luid_0x0_0x1_phys_0_eng_0_engtype_3D","pid_1234_luid_0x00000000_0x0000C2E1_phys_0_eng_3_engtype_3D#1","_Total","pid_x_luid_0x0_0x1_phys_0_eng_0_engtype_3D",
        "pid_99999999999_luid_0x0_0x1_phys_0_eng_0_engtype_3D","pid_1_luid_0x123456789_0x1_phys_0_eng_0_engtype_3D","pid_1_luid_0x0_0xG_phys_0_eng_0_engtype_3D","pid_1_luid_0x0_0x1_phys_0_eng_0","pid_1_luid_0x0_0x1_phys__eng_0_engtype_3D",""})
        check(!n::activityGPUInstance(bad),"Malformed, duplicate-suffixed or zero-PID instances are not engines");
    check(!n::activityGPUInstance(std::string(300,'p')),"Over-long names are rejected");
}
void samples(){
    std::vector<n::ActivityGPUCounter>raw{{"pid_9_luid_0x0_0x2_phys_0_eng_0_engtype_3D",50,true},{"pid_8_luid_0x0_0x1_phys_0_eng_1_engtype_Copy",40,true},{"pid_8_luid_0x0_0x1_phys_0_eng_0_engtype_3D",30,true},
        {"pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_3D",20,true},{"pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_3D",999,true},{"_Total",1,true},{"pid_6_luid_0x0_0x1_phys_0_eng_0_engtype_3D",5,false}};
    const auto all=n::activityGPUSample(raw,1234);check(all&&all->time==1234&&all->usage.size()==4,"Valid engine readings only, one per (engine, process)");
    const auto&u=all->usage;check(u[0].pid==7&&u[0].runningTime==20&&u[1].pid==8&&u[1].engine.engine==0&&u[2].pid==8&&u[2].engine.engine==1&&u[3].pid==9,"Sorted by engine then process; a repeated instance keeps its first reading");
    const std::array<std::uint32_t,2>pids{8,9};const auto some=n::activityGPUSample(raw,1234,pids);check(some&&some->usage.size()==3&&some->usage[0].pid==8,"Filtered to the given processes");
    const std::array<std::uint32_t,2>unsorted{9,8};check(!n::activityGPUSample(raw,1234,unsorted),"Process filter must be sorted");
    std::vector<n::ActivityGPUCounter>many(m::ActivityGPUSample::maximumUsage+1);for(std::size_t k=0;k<many.size();++k)many[k]={"pid_"+std::to_string(k+1)+"_luid_0x0_0x1_phys_0_eng_0_engtype_3D",1,true};
    check(!n::activityGPUSample(many,1),"More readings than the bound are unavailable");many.pop_back();check(n::activityGPUSample(many,1)->usage.size()==m::ActivityGPUSample::maximumUsage,"Exactly the bound is accepted");
    // Derivation end to end: two collections one second apart.
    std::vector<n::ActivityGPUCounter>later=raw;later[0].runningTime=50+2'500'000;later[3].runningTime=20+5'000'000;
    const auto next=n::activityGPUSample(later,1234+10'000'000);const auto percent=m::activityGPUPercent(*next,*all);check(percent&&std::abs(*percent-50)<1e-9,"Busiest engine 50%");
}
#ifdef _WIN32
unsigned threads(){const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return 0;THREADENTRY32 entry{};entry.dwSize=sizeof(entry);unsigned count{};const auto self=GetCurrentProcessId();
    if(Thread32First(snapshot,&entry))do{if(entry.th32OwnerProcessID==self)++count;}while(Thread32Next(snapshot,&entry));CloseHandle(snapshot);return count;}
// This process's own work on a hardware device: large clears. The device is
// created (and used once) before the baseline collection, since a process
// that first appears in the newer collection has no baseline and adds nothing.
struct Load {
    Microsoft::WRL::ComPtr<ID3D11Device>device;Microsoft::WRL::ComPtr<ID3D11DeviceContext>context;Microsoft::WRL::ComPtr<ID3D11Texture2D>texture;Microsoft::WRL::ComPtr<ID3D11RenderTargetView>target;Microsoft::WRL::ComPtr<ID3D11Query>done;bool ready{};
    Load(){if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=4096;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&texture))||FAILED(device->CreateRenderTargetView(texture.Get(),nullptr,&target)))return;
        D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};if(FAILED(device->CreateQuery(&q,&done)))return;ready=true;run(.05);}
    void run(double seconds){if(!ready)return;const auto end=std::chrono::steady_clock::now()+std::chrono::duration<double>(seconds);float color[4]{};
        while(std::chrono::steady_clock::now()<end){for(unsigned k=0;k<64;++k){color[0]=float(k)/64;context->ClearRenderTargetView(target.Get(),color);}context->End(done.Get());context->Flush();while(context->GetData(done.Get(),nullptr,0,0)==S_FALSE)std::this_thread::yield();}}
};
double own(const m::ActivityGPUSample&now,const m::ActivityGPUSample&before){const std::array<std::uint32_t,1>self{GetCurrentProcessId()};return m::activityGPUPercent(now,before,self).value_or(-1);}
void live(){
    const auto baseline=threads();
    n::setWindowsActivityGPUDemand(false);check(!n::readWindowsActivityGPU()&&!n::windowsActivityGPUStats().open,"Hidden Activity reads no GPU counters and keeps no query");
    n::setWindowsActivityGPUDemand(true);const auto first=n::readWindowsActivityGPU();auto stats=n::windowsActivityGPUStats();
    if(!first){std::cout<<"GPU Engine counters unavailable on this machine (no WDDM GPU counters); the GPU row shows —\n";check(!stats.open,"Unavailable counters keep no query");n::setWindowsActivityGPUDemand(false);return;}
    check(stats.open&&stats.opens==1&&stats.collections==1,"Visible Activity opens one query and collects");
    for(std::size_t k=1;k<first->usage.size();++k){const auto&a=first->usage[k-1],&b=first->usage[k];check(a.engine<b.engine||(a.engine==b.engine&&a.pid<b.pid),"Collection sorted by engine then process");}
    const auto shared=n::readWindowsActivityGPU();stats=n::windowsActivityGPUStats();check(shared&&shared->time==first->time&&stats.collections==1&&stats.reuses==1,"The system and Apps readers of one sampling job share a collection");
    const std::array<std::uint32_t,1>self{GetCurrentProcessId()};const auto mine=n::readWindowsActivityGPU(self);check(mine&&mine->usage.size()<=first->usage.size(),"Filtered to this process");for(const auto&v:mine->usage)check(v.pid==self[0],"Only this process");
    std::cout<<"GPU Engine collection: "<<first->usage.size()<<" engine instances in "<<stats.lastSeconds*1000<<" ms\n";
    const auto collecting=threads();n::setWindowsActivityGPUDemand(false);std::this_thread::sleep_for(std::chrono::milliseconds(200));const auto closed=threads();n::setWindowsActivityGPUDemand(true);(void)n::readWindowsActivityGPU();
    std::cout<<"threads (before any D3D device): before "<<baseline<<", with the open query "<<collecting<<", after hiding "<<closed<<'\n';stats=n::windowsActivityGPUStats();check(stats.opens==2&&stats.closes==1,"Hide and show again reopen the query");
    Load work;std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto before=n::readWindowsActivityGPU();const auto firstSeconds=n::windowsActivityGPUStats().lastSeconds;const bool loaded=work.ready;work.run(1.0);const auto after=n::readWindowsActivityGPU();stats=n::windowsActivityGPUStats();
    check(before&&after&&after->time>before->time&&stats.collections==4,"Each sampling tick (>=250 ms apart) collects afresh");
    std::cout<<"steady collections: "<<firstSeconds*1000<<" ms and "<<stats.lastSeconds*1000<<" ms\n";
    const auto busiest=m::activityGPUPercent(*after,*before);check(busiest&&*busiest>=0&&*busiest<=100,"System busiest engine within 0-100%");
    const auto ownPercent=own(*after,*before);
    std::cout<<"System GPU "<<*busiest<<"%, this process "<<ownPercent<<"% (hardware load "<<(loaded?"ran":"unavailable")<<"), collect max "<<stats.maximumSeconds*1000<<" ms\n";
    // Units: one second of this process's own clears must register on its
    // engine (100 ns running time over the 100 ns time base, not ms or ticks).
    if(loaded)check(ownPercent>1&&ownPercent<=100,"This process's own hardware work registers as a plausible percentage");
    const auto during=threads();
    n::setWindowsActivityGPUDemand(false);stats=n::windowsActivityGPUStats();check(!stats.open&&stats.closes==2,"Hiding Activity closes the query at once");
    check(!n::readWindowsActivityGPU(),"No reading while hidden");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));const auto after2=threads();
    std::cout<<"threads with this process's own D3D11 device: while collecting "<<during<<", after hiding "<<after2<<'\n';
    n::setWindowsActivityGPUDemand(true);check(n::readWindowsActivityGPU().has_value()&&n::windowsActivityGPUStats().opens==3,"Showing Activity again reopens it");n::setWindowsActivityGPUDemand(false);
}
#endif
}
int main(){try{parsing();samples();
#ifdef _WIN32
    live();
#endif
    std::cout<<"PASS "<<checks<<" Activity GPU checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
