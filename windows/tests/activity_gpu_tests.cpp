// Activity Monitor GPU usage (a Windows addition): the "GPU Engine" counter
// instance names (narrow and PDH's UTF-16), one collection's ordering and
// bounds, the recording policy and, on Windows, the live collector over this
// test process only. GPU is recorded like CPU and RAM: every system sample
// reads it, through one query kept open and buffers reused (no allocation per
// sample), so its history fills while Activity is hidden or the HUD is
// closed, on the sampling plan's one deadline. The live part reads the
// system's GPU engine performance counters (no user data) and asserts only
// about this process; its optional D3D11 load is this process's own work on
// a hardware device and is skipped where none can be created. No window is
// shown and no input is sent.
#include "native/activity_gpu.hpp"
#include "native/activity_disk.hpp"
#include "native/activity_provider.hpp"
#include "app/utility_executor.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#include <d3d11.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <chrono>
#include <map>
#include <thread>
#endif

// Allocations made through this executable's operator new on the calling
// thread (the collector and the probe are linked in statically).
static thread_local std::size_t allocations{};
void*operator new(std::size_t size){++allocations;if(auto*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void*operator new[](std::size_t size){return ::operator new(size);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
namespace n=endfield::native;namespace m=endfield::modules;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
void parsing(){
    const auto a=n::activityGPUInstance("pid_1234_luid_0x00000000_0x0000C2E1_phys_0_eng_3_engtype_3D");
    check(a&&a->pid==1234&&a->engine.luid==0xC2E1&&a->engine.physical==0&&a->engine.engine==3&&a->engine.type==m::activityGPUEngineType("3D"),"Instance name: process, adapter LUID, node, engine and engine type");
    const auto b=n::activityGPUInstance("pid_7_luid_0x00000001_0xFFFFFFFF_phys_1_eng_12_engtype_VideoDecode");
    check(b&&b->engine.luid==0x1FFFFFFFFULL&&b->engine.physical==1&&b->engine.engine==12&&b->engine.type==m::activityGPUEngineType("VideoDecode"),"High and low LUID halves");
    check(a->engine.type!=b->engine.type&&n::activityGPUInstance("pid_9_luid_0x0_0x2_phys_0_eng_7_engtype_3D")->engine.type==a->engine.type,"One engine type label, one type, on any adapter and engine");
    const auto codec=n::activityGPUInstance("pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_Video Codec");
    check(n::activityGPUInstance("pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_")&&codec&&codec->engine.type==m::activityGPUEngineType("Video Codec"),"Short hex and any engine type label");
    for(const auto*bad:{"pid_0_luid_0x0_0x1_phys_0_eng_0_engtype_3D","pid_1234_luid_0x00000000_0x0000C2E1_phys_0_eng_3_engtype_3D#1","_Total","pid_x_luid_0x0_0x1_phys_0_eng_0_engtype_3D",
        "pid_99999999999_luid_0x0_0x1_phys_0_eng_0_engtype_3D","pid_1_luid_0x123456789_0x1_phys_0_eng_0_engtype_3D","pid_1_luid_0x0_0xG_phys_0_eng_0_engtype_3D","pid_1_luid_0x0_0x1_phys_0_eng_0","pid_1_luid_0x0_0x1_phys__eng_0_engtype_3D",""})
        check(!n::activityGPUInstance(bad),"Malformed, duplicate-suffixed or zero-PID instances are not engines");
    check(!n::activityGPUInstance(std::string(300,'p')),"Over-long names are rejected");
    // PDH's own UTF-16 names parse in place, exactly like their ASCII form.
    for(const auto*name:{"pid_1234_luid_0x00000000_0x0000C2E1_phys_0_eng_3_engtype_3D","pid_7_luid_0x00000001_0xFFFFFFFF_phys_1_eng_12_engtype_VideoDecode","pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_Video Codec",
        "pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_","pid_0_luid_0x0_0x1_phys_0_eng_0_engtype_3D","pid_1234_luid_0x00000000_0x0000C2E1_phys_0_eng_3_engtype_3D#1","_Total","pid_1_luid_0x123456789_0x1_phys_0_eng_0_engtype_3D","pid_1_luid_0x0_0xg_phys_0_eng_0_engtype_3D"}){
        const std::string narrow(name);const std::wstring wide(narrow.begin(),narrow.end());check(n::activityGPUInstance(wide)==n::activityGPUInstance(narrow),"A UTF-16 instance name parses like its ASCII form");}
    check(n::activityGPUInstance(L"pid_9_luid_0x0_0x2_phys_0_eng_0_engtype_3D")->engine.type==m::activityGPUEngineType("3D")&&n::activityGPUInstance(L"pid_9_luid_0x0_0xa_phys_0_eng_0_engtype_3D")->engine.luid==0xA,"UTF-16 engine type key and lower-case hex");
    check(!n::activityGPUInstance(L"pid_9_luid_0x0_0x2_phys_0_eng_0_engtype_3D\u00e9")&&!n::activityGPUInstance(std::wstring(300,L'p')),"A non-ASCII or over-long UTF-16 name is not an engine");
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
    // Derivation end to end: two collections one second apart. Adapter 1:
    // 3D 50% + Copy 10%; adapter 2: 3D 25%. The total load is adapter 1's.
    std::vector<n::ActivityGPUCounter>later=raw;later[0].runningTime=50+2'500'000;later[3].runningTime=20+5'000'000;later[1].runningTime=40+1'000'000;
    const auto next=n::activityGPUSample(later,1234+10'000'000);const auto percent=m::activityGPUPercent(*next,*all);check(percent&&std::abs(*percent-60)<1e-9,"Total load 60%: the busier adapter's 3D and Copy summed");
}
#ifdef _WIN32
unsigned threads(){const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return 0;THREADENTRY32 entry{};entry.dwSize=sizeof(entry);unsigned count{};const auto self=GetCurrentProcessId();
    if(Thread32First(snapshot,&entry))do{if(entry.th32OwnerProcessID==self)++count;}while(Thread32Next(snapshot,&entry));CloseHandle(snapshot);return count;}
// This process's own work on a hardware device: full copies between two
// large textures (a clear alone can be a metadata-only fast clear that keeps
// no engine busy). The device is created (and used once) before the baseline
// collection, since a process that first appears in the newer collection has
// no baseline and adds nothing.
struct Load {
    Microsoft::WRL::ComPtr<ID3D11Device>device;Microsoft::WRL::ComPtr<ID3D11DeviceContext>context;Microsoft::WRL::ComPtr<ID3D11Texture2D>source,target;Microsoft::WRL::ComPtr<ID3D11RenderTargetView>view;Microsoft::WRL::ComPtr<ID3D11Query>done;bool ready{};
    Load(){if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=2048;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&source))||FAILED(device->CreateTexture2D(&desc,nullptr,&target))||FAILED(device->CreateRenderTargetView(source.Get(),nullptr,&view)))return;
        D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};if(FAILED(device->CreateQuery(&q,&done)))return;ready=true;run(.05);}
    void run(double seconds){if(!ready)return;const auto end=std::chrono::steady_clock::now()+std::chrono::duration<double>(seconds);float color[4]{.25f,.5f,.75f,1};
        while(std::chrono::steady_clock::now()<end){context->ClearRenderTargetView(view.Get(),color);for(unsigned k=0;k<32;++k){context->CopyResource(target.Get(),source.Get());context->CopyResource(source.Get(),target.Get());}
            context->End(done.Get());context->Flush();while(context->GetData(done.Get(),nullptr,0,0)==S_FALSE)std::this_thread::yield();}}
};
// Each physical adapter's own total load (its readings alone), logged; the
// busiest of them.
double adapters(const m::ActivityGPUSample&now,const m::ActivityGPUSample&before){double busiest{};
    std::vector<std::pair<std::uint64_t,std::uint32_t>>seen;for(const auto&v:now.usage)if(std::find(seen.begin(),seen.end(),std::pair{v.engine.luid,v.engine.physical})==seen.end())seen.push_back({v.engine.luid,v.engine.physical});
    for(const auto&adapter:seen){const auto luid=adapter.first;const auto physical=adapter.second;const auto only=[&](const m::ActivityGPUSample&s){m::ActivityGPUSample out;out.time=s.time;for(const auto&v:s.usage)if(v.engine.luid==luid&&v.engine.physical==physical)out.usage.push_back(v);return out;};
        std::vector<std::uint32_t>types;for(const auto&v:now.usage)if(v.engine.luid==luid&&v.engine.physical==physical&&std::find(types.begin(),types.end(),v.engine.type)==types.end())types.push_back(v.engine.type);
        const auto load=m::activityGPUPercent(only(now),only(before));check(load&&*load>=0&&*load<=100,"Each adapter's total load within 0-100%");
        busiest=std::max(busiest,*load);std::cout<<"adapter luid 0x"<<std::hex<<luid<<std::dec<<" phys "<<physical<<": "<<types.size()<<" engine types, total load "<<*load<<"%\n";}
    return busiest;
}
double own(const m::ActivityGPUSample&now,const m::ActivityGPUSample&before){const std::array<std::uint32_t,1>self{GetCurrentProcessId()};return m::activityGPUPercent(now,before,self).value_or(-1);}
void live(){
    const auto baseline=threads();
    // No visibility gate: the first system reading opens the one query.
    const auto first=n::readWindowsActivityGPU();auto stats=n::windowsActivityGPUStats();
    if(!first){std::cout<<"GPU Engine counters unavailable on this machine (no WDDM GPU counters); the GPU row shows —\n";check(!stats.open,"Unavailable counters keep no query");
        const auto attempts=stats.collections;(void)n::readWindowsActivityGPU();check(!n::windowsActivityGPUStats().open&&n::windowsActivityGPUStats().collections==attempts+1,"Every sample asks again, still without a query");return;}
    check(stats.open&&stats.opens==1&&stats.collections==1,"The first system sample opens one query and collects");
    for(std::size_t k=1;k<first->usage.size();++k){const auto&a=first->usage[k-1],&b=first->usage[k];check(a.engine<b.engine||(a.engine==b.engine&&a.pid<b.pid),"Collection sorted by engine then process");}
    std::optional<m::ActivityGPUSample>previous=n::readWindowsActivityGPU();stats=n::windowsActivityGPUStats();check(previous&&previous->time==first->time&&previous->usage==first->usage&&stats.collections==1&&stats.reuses==1,"The system and Apps readers of one sampling job share a collection");
    const std::array<std::uint32_t,1>self{GetCurrentProcessId()};const auto mine=n::readWindowsActivityGPU(self);check(mine&&mine->usage.size()<=first->usage.size(),"Filtered to this process");for(const auto&v:mine->usage)check(v.pid==self[0],"Only this process");
    std::cout<<"GPU Engine collection: "<<first->usage.size()<<" engine instances, opened and collected in "<<stats.lastSeconds*1000<<" ms\n";
    // Later samples (every 1 s while shown, every 5 s while Activity is
    // hidden or the HUD is closed; at least 250 ms apart either way) collect
    // afresh through the same query. With the previous reading recycled, as
    // ActivityProbe does, a sample allocates nothing once the kept buffers
    // hold the instances (one sample may grow them if processes start using
    // the GPU meanwhile).
    std::this_thread::sleep_for(std::chrono::milliseconds(260));{auto next=n::readWindowsActivityGPU();n::recycleWindowsActivityGPU(std::move(*previous));previous=std::move(next);}
    unsigned quiet{};std::string counts;const auto opened=n::windowsActivityGPUStats();
    for(unsigned k=0;k<6;++k){std::this_thread::sleep_for(std::chrono::milliseconds(260));const auto before=allocations;
        auto next=n::readWindowsActivityGPU();check(next&&previous&&next->time>previous->time,"Each sample >= 250 ms later collects afresh");n::recycleWindowsActivityGPU(std::move(*previous));previous=std::move(next);
        const auto used=allocations-before;quiet+=used==0;counts+=(k?",":"")+std::to_string(used);}
    stats=n::windowsActivityGPUStats();
    std::cout<<"steady samples: allocations per sample "<<counts<<"; last collection "<<stats.lastSeconds*1000<<" ms, "<<stats.instances<<" instances; "<<stats.retainedBytes/1024<<" KiB kept between samples\n";
    check(quiet>=5,"A GPU sample allocates nothing in the steady state (raw array, parse buffer and reading reused)");
    check(stats.open&&stats.opens==opened.opens&&stats.closes==opened.closes&&stats.collections==opened.collections+6&&stats.recycled>opened.recycled,"One query kept open across samples; superseded readings are recycled");
    const auto collecting=threads();
    std::cout<<"threads (before any D3D device): before "<<baseline<<", with the query open "<<collecting<<'\n';
    check(collecting<=baseline,"The GPU reading adds no thread of its own");
    Load work;std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto before=n::readWindowsActivityGPU();const bool loaded=work.ready;work.run(1.0);const auto after=n::readWindowsActivityGPU();stats=n::windowsActivityGPUStats();
    check(before&&after&&after->time>before->time,"Each sampling tick collects afresh");
    const auto total=m::activityGPUPercent(*after,*before);check(total&&*total>=0&&*total<=100,"System total load within 0-100%");
    const auto ownPercent=own(*after,*before);check(*total>=ownPercent-1e-9,"The whole system's total load is at least this process's");
    std::cout<<"System GPU total load "<<*total<<"%, this process "<<ownPercent<<"% (hardware load "<<(loaded?"ran":"unavailable")<<"), collect max "<<stats.maximumSeconds*1000<<" ms\n";
    check(std::abs(adapters(*after,*before)-*total)<1e-9,"The system's total load is its busier adapter's");
    // Units: one second of this process's own copies must register on its
    // engines (100 ns running time over the 100 ns time base, not ms or ticks).
    if(loaded)check(ownPercent>1&&ownPercent<=100,"This process's own hardware work registers as a plausible percentage");
    std::cout<<"threads with this process's own D3D11 device: "<<threads()<<'\n';
    n::recycleWindowsActivityGPU(std::move(*previous));
    // Teardown only: release closes the query and drops every buffer; the next reading reopens it.
    n::releaseWindowsActivityGPU();stats=n::windowsActivityGPUStats();check(!stats.open&&stats.retainedBytes==0&&stats.closes==opened.closes+1,"Release closes the query and keeps no buffer");
    check(n::readWindowsActivityGPU().has_value()&&n::windowsActivityGPUStats().opens==opened.opens+1,"The next sample opens it again");
}
// GPU history accumulates exactly like CPU while Activity is hidden and the
// HUD is closed: the production system reader on the shared executor, driven
// only by the sampling plan's deadline (ActivityProbe::update), as the host
// does. Plan time is simulated (5 s hidden, 1 s shown); the reader runs on
// the real clock, its samples at least 250 ms apart.
void history(){
    namespace a=endfield::app;a::UtilityExecutor executor([]{},4);m::ActivityState state([](std::string_view x,std::string_view y){return x.compare(y);});m::ActivitySamplingPlan plan;
    n::ActivityProbe::Readers readers;std::atomic<unsigned>systemReads{},appReads{};
    readers.system=[&]{++systemReads;const auto uptime=double(GetTickCount64())*.001;return n::readWindowsActivity(uptime,uptime);};
    readers.apps=[&]{++appReads;return m::ActivityAppsRaw{};};
    n::ActivityProbe probe(state,plan,executor,readers);
    const bool available=n::readWindowsActivityGPU().has_value();std::this_thread::sleep_for(std::chrono::milliseconds(260));
    const auto start=n::windowsActivityGPUStats();double t=1000;
    const auto tick=[&](double next){std::this_thread::sleep_for(std::chrono::milliseconds(260));t=next;(void)probe.update(t);executor.waitIdle();executor.drain();};
    // Launch: the 5 s session history, Activity never shown.
    plan.start(t);(void)probe.update(t);executor.waitIdle();executor.drain();check(plan.nextWakeTime()==t+5,"Background history every 5 s from launch");
    tick(1005);tick(1010);
    // Activity shown (1 s), then hidden, then the HUD closed: hidden either way.
    plan.setVisible(true,false,false,t);tick(t+1);tick(t+1);
    plan.setVisible(false,false,false,t);check(plan.nextWakeTime()==t+5,"Hidden again: back to the 5 s deadline, GPU adding none");tick(t+5);tick(t+5);
    const auto quiet=n::windowsActivityGPUStats().collections;std::this_thread::sleep_for(std::chrono::milliseconds(600));(void)probe.update(t+4.9);executor.waitIdle();executor.drain();
    check(n::windowsActivityGPUStats().collections==quiet,"Between deadlines nothing collects: no timer or thread of its own");
    const auto stats=n::windowsActivityGPUStats();const auto history=state.history();std::size_t cpu{},gpu{};for(const auto&s:history){cpu+=s.cpuPercent.has_value();gpu+=s.gpuPercent.has_value();}
    std::cout<<"history: "<<history.size()<<" system samples, CPU in "<<cpu<<", GPU in "<<gpu<<"; GPU collections "<<stats.collections-start.collections<<", query opens "<<stats.opens-start.opens<<'\n';
    check(history.size()==7&&systemReads==7&&appReads==0,"Seven system samples on the plan's deadlines, no Apps reading while the Apps page is hidden");
    check(stats.collections-start.collections==7&&stats.opens==start.opens&&stats.closes==start.closes,"One GPU collection per system sample through the one kept query");
    check(cpu==6&&gpu==(available?6u:0u),"GPU history fills like CPU from launch, shown, hidden and with the HUD closed");
    check(executor.stats().workersStarted==1,"All on the shared worker");
}
// Measurement (not a ctest): activity_gpu_tests --closed-idle <seconds>.
// The closed HUD's Activity history exactly as the host runs it: the
// production system reader (CPU, RAM, network, disk and the GPU reading of
// native/activity_gpu.hpp) on one shared utility worker at the 5 s
// background cadence of ActivitySamplingPlan (Activity hidden, no charge
// alert), driven by the plan's own deadline and the executor's completion
// notice as the host's message loop is. After a warm-up sample (the first
// opens the counter queries) it reports this process's wake-ups (context
// switches of every thread) and CPU time per minute, and how many history
// samples carry CPU and GPU readings.
struct ThreadUse {std::uint32_t switches{};std::int64_t cpu{};};
std::map<std::uint32_t,ThreadUse>threadUse(){
    static_assert(sizeof(void*)==8,"SYSTEM_PROCESS_INFORMATION offsets are the x64 layout");
    using Query=LONG(WINAPI*)(ULONG,PVOID,ULONG,PULONG);
    static const auto query=reinterpret_cast<Query>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQuerySystemInformation")));
    std::map<std::uint32_t,ThreadUse>out;if(!query)return out;std::vector<std::uint8_t>buffer(std::size_t(4)<<20);
    const auto field=[&](std::size_t at,auto value){std::memcpy(&value,buffer.data()+at,sizeof(value));return value;};
    for(unsigned attempt=0;attempt<6;++attempt){ULONG needed{};const auto status=query(5,buffer.data(),ULONG(buffer.size()),&needed);
        if(status==LONG(0xC0000004L)){buffer.resize(std::max<std::size_t>(needed+(1<<20),buffer.size()*2));continue;}if(status<0)return out;
        for(std::size_t at=0;at+0x100<=buffer.size();){
            if(DWORD(field(at+0x50,std::uintptr_t{}))==GetCurrentProcessId()){const auto count=field(at+4,ULONG{});
                for(ULONG k=0;k<count;++k){const auto entry=at+0x100+std::size_t(k)*0x50;if(entry+0x50>buffer.size())break;
                    out[std::uint32_t(field(entry+0x30,std::uintptr_t{}))]={field(entry+0x40,ULONG{}),field(entry,std::int64_t{})+field(entry+8,std::int64_t{})};}
                return out;}
            const auto next=field(at,ULONG{});if(!next)return out;at+=next;}
        return out;}
    return out;
}
double processCPU(){FILETIME creation{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user);const auto ticks=[](FILETIME f){return double((std::uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime)*1e-7;};return ticks(kernel)+ticks(user);}
// Thread CPU times advance by whole clock ticks (15.6 ms), too coarse for a
// sample costing about a millisecond; the process's CPU cycles are exact.
// They count at the time-stamp counter's rate, measured here against QPC.
std::uint64_t processCycles(){ULONG64 cycles{};QueryProcessCycleTime(GetCurrentProcess(),&cycles);return cycles;}
double cycleRate(){LARGE_INTEGER frequency{},a{},b{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&a);const auto t0=__rdtsc();Sleep(200);const auto t1=__rdtsc();QueryPerformanceCounter(&b);return double(t1-t0)*double(frequency.QuadPart)/double(b.QuadPart-a.QuadPart);}
int closedIdle(double seconds){
    namespace a=endfield::app;struct Event {HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(value)CloseHandle(value);}}event;const HANDLE completed=event.value;check(completed!=nullptr,"Completion event");
    a::UtilityExecutor executor([completed]{SetEvent(completed);},32,std::chrono::milliseconds::zero(),"activity closed-idle");
    m::ActivityState state([](std::string_view x,std::string_view y){return x.compare(y);});m::ActivitySamplingPlan plan;auto disk=std::make_shared<n::WindowsActivityDisk>();
    const auto clock=[]{return double(GetTickCount64())*.001;};
    auto worker=std::make_shared<std::atomic<std::uint32_t>>(0u);
    // The reader's own time per sample (QPC), all of it and the GPU part.
    struct Timing {std::atomic<std::uint64_t>samples{},ticks{},gpuTicks{};};auto timing=std::make_shared<Timing>();
    n::ActivityProbe::Readers readers;readers.system=[disk,clock,worker,timing]{worker->store(GetCurrentThreadId());LARGE_INTEGER a{},b{};QueryPerformanceCounter(&a);const auto gpu0=n::windowsActivityGPUStats().lastSeconds;const auto collections0=n::windowsActivityGPUStats().collections;
        const auto uptime=clock();auto value=n::readWindowsActivity(uptime,uptime);value.disk=disk->sample();QueryPerformanceCounter(&b);
        const auto gpu=n::windowsActivityGPUStats();LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);++timing->samples;timing->ticks+=std::uint64_t(b.QuadPart-a.QuadPart);
        if(gpu.collections!=collections0||gpu.lastSeconds!=gpu0)timing->gpuTicks+=std::uint64_t(gpu.lastSeconds*double(frequency.QuadPart));return value;};
    readers.apps=[]{return m::ActivityAppsRaw{};}; // a hidden Activity has no Apps demand
    n::ActivityProbe probe(state,plan,executor,readers);
    // One host turn: the deadline or the completion notice, whichever comes first.
    const auto turn=[&]{const auto wake=plan.nextWakeTime();const auto delay=wake?std::max(0.,*wake-clock()):1.;
        if(WaitForSingleObject(completed,DWORD(std::ceil(delay*1000)))==WAIT_OBJECT_0){executor.drain();(void)probe.submitPending();}else (void)probe.update(clock());};
    plan.start(clock());(void)probe.update(clock());while(state.history().size()<1)turn(); // warm-up: the first sample opens the queries
    const auto firstTicks=timing->ticks.load(),firstGPU=timing->gpuTicks.load();
    const auto gpuBefore=n::windowsActivityGPUStats();const auto threads0=threadUse();const auto cpu0=processCPU();const auto cycles0=processCycles();const auto timed0=timing->samples.load();const auto ticks0=timing->ticks.load(),gpuTicks0=timing->gpuTicks.load();const auto start=clock();const auto samples0=state.history().size();
    while(clock()-start<seconds)turn();
    const auto threads1=threadUse();const auto cpu1=processCPU();const auto cycles1=processCycles();const auto elapsed=clock()-start;const auto gpuAfter=n::windowsActivityGPUStats();
    const auto rate=cycleRate();LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);const auto timed=double(timing->samples.load()-timed0);
    // Per role: this main thread (the host's message loop), the sampling
    // worker, and every other thread (system pools, PDH, the network stack).
    std::array<std::uint64_t,3>roleWakes{};std::array<double,3>roleCPU{};std::array<unsigned,3>roleThreads{};
    for(const auto&[id,use]:threads1){const auto old=threads0.find(id);const unsigned role=id==GetCurrentThreadId()?0:id==worker->load()?1:2;
        roleWakes[role]+=old==threads0.end()?use.switches:use.switches-std::min(use.switches,old->second.switches);roleCPU[role]+=double(use.cpu-(old==threads0.end()?0:old->second.cpu))*1e-7;++roleThreads[role];}
    const auto wakes=roleWakes[0]+roleWakes[1]+roleWakes[2];const auto threadCPU=roleCPU[0]+roleCPU[1]+roleCPU[2];
    std::size_t cpuSamples{},gpuSamples{};for(const auto&s:state.history()){cpuSamples+=s.cpuPercent.has_value();gpuSamples+=s.gpuPercent.has_value();}
    const auto perMinute=60/elapsed;
    std::cout<<"closed-idle "<<elapsed<<" s: "<<state.history().size()-samples0<<" system samples at the "<<plan.interval()<<" s cadence; history "<<state.history().size()<<" samples, CPU in "<<cpuSamples<<", GPU in "<<gpuSamples<<'\n'
             <<"  process wake-ups "<<wakes<<" ("<<double(wakes)*perMinute<<"/min) over "<<threads1.size()<<" threads; CPU "<<(cpu1-cpu0)*1000<<" ms ("<<(cpu1-cpu0)*1000*perMinute<<" ms/min, threads "<<threadCPU*1000*perMinute<<" ms/min)\n"
             <<"  CPU cycles "<<double(cycles1-cycles0)*1e-6<<" M = "<<double(cycles1-cycles0)/rate*1000*perMinute<<" ms/min at the "<<rate*1e-9<<" GHz time-stamp rate; each system sample takes "
             <<(timed?double(timing->ticks.load()-ticks0)/double(frequency.QuadPart)*1000/timed:0)<<" ms on the worker, of which GPU "<<(timed?double(timing->gpuTicks.load()-gpuTicks0)/double(frequency.QuadPart)*1000/timed:0)<<" ms\n"
             <<"  the first sample (opening the queries) took "<<double(firstTicks)/double(frequency.QuadPart)*1000<<" ms, of which GPU "<<double(firstGPU)/double(frequency.QuadPart)*1000<<" ms\n"
             <<"  per minute: main "<<double(roleWakes[0])*perMinute<<" wake-ups "<<roleCPU[0]*1000*perMinute<<" ms; sampling worker "<<double(roleWakes[1])*perMinute<<" wake-ups "<<roleCPU[1]*1000*perMinute<<" ms; "
             <<roleThreads[2]<<" other threads "<<double(roleWakes[2])*perMinute<<" wake-ups "<<roleCPU[2]*1000*perMinute<<" ms\n"
             <<"  GPU collector: "<<gpuAfter.collections-gpuBefore.collections<<" collections, "<<gpuAfter.opens-gpuBefore.opens<<" opens, "<<gpuAfter.instances<<" engine instances, last "<<gpuAfter.lastSeconds*1000<<" ms, max "<<gpuAfter.maximumSeconds*1000<<" ms; GPU now "<<(state.snapshot().gpuPercent?std::to_string(*state.snapshot().gpuPercent)+"%":"unknown")<<'\n';
    return 0;
}
#endif
}
int main(int argc,char**argv){try{
#ifdef _WIN32
    if(argc==3&&std::string_view(argv[1])=="--closed-idle")return closedIdle(std::stod(argv[2]));
#endif
    (void)argc;(void)argv;parsing();samples();
#ifdef _WIN32
    live();history();
#endif
    std::cout<<"PASS "<<checks<<" Activity GPU checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
