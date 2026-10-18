#include "native/activity_provider.hpp"
#include "native/activity_gpu.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
// Allocations made through this executable's operator new on the calling
// thread (the network reader is linked in statically).
static thread_local std::size_t allocations{};
void*operator new(std::size_t size){++allocations;if(auto*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void*operator new[](std::size_t size){return ::operator new(size);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
namespace n=endfield::native;namespace m=endfield::modules;namespace a=endfield::app;
namespace {
unsigned checks{};void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
int names(std::string_view x,std::string_view y){return x.compare(y);}
struct Block {std::mutex mutex;std::condition_variable cv;bool entered{},released{};void wait(){std::unique_lock lock(mutex);entered=true;cv.notify_all();cv.wait(lock,[&]{return released;});}void await(){std::unique_lock lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}))throw std::runtime_error("Fixture reader did not start");}void release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}};
struct Release {std::shared_ptr<Block>b;~Release(){b->release();}};
void test(){std::atomic<unsigned>notifications{},systemReads{},appReads{};std::atomic<double>tick{100};const auto owner=std::this_thread::get_id();bool workerOnly=true,ownerOnly=true;a::UtilityExecutor executor([&]{++notifications;},1);m::ActivityState state(names);m::ActivitySamplingPlan plan;
 n::ActivityProbe::Readers readers;readers.system=[&]{workerOnly=workerOnly&&std::this_thread::get_id()!=owner;const auto num=++systemReads;m::ActivityRawSample raw;raw.uptime=tick;raw.timestamp=123;raw.cpu=m::ActivityCPU{num*10,num*10,num*80,0};raw.network=m::ActivityCounters{};raw.disk=m::ActivityCounters{};return raw;};readers.apps=[&]{workerOnly=workerOnly&&std::this_thread::get_id()!=owner;++appReads;m::ActivityAppsRaw raw;raw.uptime=tick;return raw;};
 unsigned systemPublications{},appPublications{};n::ActivityProbe probe(state,plan,executor,readers,[&](bool sys,bool apps){ownerOnly=ownerOnly&&std::this_thread::get_id()==owner;systemPublications+=sys;appPublications+=apps;});
 check(!probe.update(0)&&!executor.stats().started,"Construction/hidden idle starts no worker/provider");plan.start(100);check(probe.update(100),"Session start submits one baseline");executor.waitIdle();check(state.history().empty(),"Background worker cannot mutate owner model");executor.drain();check(state.history().size()==1&&!state.snapshot().cpuPercent,"First snapshot publishes unknown CPU baseline");const auto before=systemReads.load();for(unsigned k=0;k<1000;++k)check(!probe.update(100.5),"Idle animation frames never sample");check(systemReads==before&&notifications==1,"No idle polls/notifications");tick=105;probe.update(105);executor.waitIdle();executor.drain();check(state.snapshot().cpuPercent==20&&systemReads==2,"Five-second background sample derives complete counter delta");plan.setVisible(true,false,true,105.1);probe.update(105.1);executor.waitIdle();executor.drain();check(appReads==1&&systemReads==2,"Entering Apps immediately requests only its independent baseline");tick=106.1;probe.update(106.1);executor.waitIdle();executor.drain();check(systemReads==3&&appReads==2&&systemPublications==3&&appPublications==2,"Visible shared1Hz deadline coalesces system and app work");check(workerOnly&&ownerOnly,"Reader and publication thread boundaries preserved");
 auto unrelated=std::make_shared<Block>();Release free{unrelated};const auto route=executor.makeRoute();executor.submit(route,[unrelated]{unrelated->wait();},[](auto){});unrelated->await();tick=107.1;check(!probe.update(107.1)&&probe.stats().waiting,"Shared-queue backpressure retains one due request");unrelated->release();executor.waitIdle();executor.drain();check(probe.submitPending(),"Drain event retries without a new timer");executor.waitIdle();executor.drain();check(!probe.stats().waiting,"Completion clears retained due request");executor.invalidate(route);
 plan.setVisible(false,false,true,108);check(!probe.update(108)&&!plan.appsActive()&&plan.nextWakeTime()==113,"Hiding stops apps and retains only background system deadline");check(probe.stats().appReleases==1,"Hiding the Apps detail queues the release of the worker's retained Apps reading");executor.waitIdle();executor.drain();check(!probe.update(108.5)&&probe.stats().appReleases==1,"Released once");const auto priorApps=appReads.load();tick=113;probe.update(113);executor.waitIdle();executor.drain();check(appReads==priorApps,"Background never enumerates or samples apps");plan.stop();check(!probe.update(200)&&!plan.nextWakeTime(),"Shutdown stops all future sample work");
 auto blocked=std::make_shared<Block>();Release freeBlock{blocked};m::ActivityState isolated(names);m::ActivitySamplingPlan isolatedPlan;auto heldReaders=readers;heldReaders.system=[&,blocked]{blocked->wait();return readers.system();};unsigned late{};auto disposable=std::make_unique<n::ActivityProbe>(isolated,isolatedPlan,executor,heldReaders,[&](bool,bool){++late;});isolatedPlan.start(300);disposable->update(300);blocked->await();disposable.reset();blocked->release();executor.waitIdle();executor.drain();check(late==0&&isolated.history().empty(),"Destroyed probe suppresses late completion without waiting");
 auto restartBlock=std::make_shared<Block>();Release freeRestart{restartBlock};unsigned attempts{};heldReaders.system=[&,restartBlock]{if(++attempts==1)restartBlock->wait();return readers.system();};m::ActivitySamplingPlan restartPlan;m::ActivityState restartState(names);n::ActivityProbe restart(restartState,restartPlan,executor,heldReaders);restartPlan.start(400);restart.update(400);restartBlock->await();restartPlan.stop();restartPlan.start(401);check(!restart.update(401)&&restart.stats().waiting,"Restart while running retains exactly one fresh baseline request");restartBlock->release();executor.waitIdle();executor.drain();executor.waitIdle();executor.drain();check(restartState.history().size()==1&&!restartState.snapshot().cpuPercent&&restart.stats().discarded==1,"Stale generation is discarded and new baseline publishes once");
 m::ActivitySamplingPlan selfPlan;m::ActivityState selfState(names);std::unique_ptr<n::ActivityProbe>self;self=std::make_unique<n::ActivityProbe>(selfState,selfPlan,executor,readers,[&](bool,bool){self.reset();});selfPlan.start(500);self->update(500);executor.waitIdle();executor.drain();check(!self,"Callback can destroy its own probe without resubmission");
 #ifdef _WIN32
 {// Private working set of this test process only (no other process is opened).
  std::vector<std::uint8_t>touched(8*1024*1024,1);const auto memory=n::windowsActivityProcessMemory(GetCurrentProcess());PROCESS_MEMORY_COUNTERS basic{};basic.cb=sizeof(basic);check(GetProcessMemoryInfo(GetCurrentProcess(),&basic,sizeof(basic))!=0,"Own working set readable");
  check(memory&&*memory>=touched.size()/2&&*memory<=basic.WorkingSetSize+touched.size(),"App memory is the private working set (resident private pages, bounded by the working set)");check(!n::windowsActivityProcessMemory(nullptr),"An invalid handle has no memory reading");}
 check(n::compareWindowsActivityNames("Example2","Example10")<0,"Native natural comparator sorts digit runs numerically");check(n::compareWindowsActivityNames("활동","활동")==0,"Native comparator supports Korean identity");
 #endif
}
}
// GPU rides the system sample: the same deadline and worker job as CPU, so
// its history fills whether Activity is shown, hidden or the HUD is closed
// (visibility only sets the cadence); the worker hands each superseded
// reading's GPU buffer back for the next reading (allocation-free steady state).
void gpu(){a::UtilityExecutor executor([]{},1);m::ActivityState state(names);m::ActivitySamplingPlan plan;std::atomic<unsigned>reads{},appReads{};std::atomic<double>clock{100};
 const m::ActivityGPUEngine engine{1,0,m::activityGPUEngineType("3D"),0};
 n::ActivityProbe::Readers readers;readers.system=[&]{const std::uint64_t k=++reads;const double now=clock;m::ActivityRawSample raw;raw.uptime=now;raw.timestamp=now;raw.cpu=m::ActivityCPU{k*10,k*10,k*80,0};raw.network=m::ActivityCounters{{"7",{k*1000,k*100}}};
  m::ActivityGPUSample sample;sample.time=std::uint64_t(now*1e7);sample.usage.reserve(64);sample.usage.push_back({7,engine,std::uint64_t(now*2.5e6)});raw.gpu=std::move(sample);return raw;}; // 25% busy
 readers.apps=[&]{++appReads;return m::ActivityAppsRaw{};};
 n::ActivityProbe probe(state,plan,executor,readers);const auto recycled=n::windowsActivityGPUStats().recycled;const auto recycledNetwork=n::windowsActivityNetworkStats().recycled;
 const auto at=[&](double t){clock=t;(void)probe.update(t);executor.waitIdle();executor.drain();};
 plan.start(100);at(100);at(105);at(110);check(plan.nextWakeTime()==115,"Hidden from launch: one 5 s deadline for CPU and GPU");
 plan.setVisible(true,false,false,110);at(111);at(112);check(plan.nextWakeTime()==113,"Shown: the same deadline at 1 s");
 plan.setVisible(false,false,false,112);at(117);at(122);check(plan.nextWakeTime()==127,"Hidden or the HUD closed: 5 s again, GPU adding no deadline");
 std::size_t cpu{},gpu{};for(const auto&s:state.history()){cpu+=s.cpuPercent==20;gpu+=s.gpuPercent&&std::abs(*s.gpuPercent-25)<1e-9;}
 check(state.history().size()==7&&reads==7&&appReads==0&&cpu==6&&gpu==6,"Every system sample after the baseline records GPU with CPU: shown, hidden and closed alike");
#ifdef _WIN32
 check(n::windowsActivityGPUStats().recycled>recycled,"The worker recycles the superseded reading's GPU buffer");
 check(n::windowsActivityNetworkStats().recycled>=recycledNetwork+6,"The worker recycles the superseded reading's network counters");
#else
 (void)recycled;(void)recycledNetwork;
#endif
 plan.stop();at(200);check(state.history().size()==7,"Suspended: neither CPU nor GPU is sampled");}
// Network: the counted interfaces kept between samples (ActivityNetworkReader)
// against the table reading of every sample (activityNetworkCounters), on a
// synthetic interface table: same names, same counters, same derived rates,
// through counter growth and resets, interfaces added, removed, connected and
// disconnected, table failures and notifications from another thread; one
// entry read per counted interface and no allocation per steady sample.
constexpr std::uint32_t ethernet=6,ppp=23,loopback=24,wifi=71,tunnel=131;
std::uint64_t luid(std::uint32_t type,std::uint64_t index){return (std::uint64_t(type)<<48)|(index<<24);}
struct Interfaces {
    std::vector<n::ActivityInterface>rows;bool failing{};unsigned tables{},entries{};std::function<void()>listing; // runs while the table is read
    n::ActivityNetworkReader::Source source(){return {[this](std::vector<n::ActivityInterface>&out){++tables;if(listing)listing();if(failing)return false;out.assign(rows.begin(),rows.end());return true;},
        [this](std::uint64_t id,n::ActivityInterface&row){++entries;for(const auto&r:rows)if(r.luid==id){row=r;return true;}return false;}};}
    n::ActivityInterface*find(std::uint64_t id){for(auto&r:rows)if(r.luid==id)return &r;return nullptr;}
    void grow(std::uint64_t received,std::uint64_t sent){for(auto&r:rows){r.received+=received;r.sent+=sent;received+=7;sent+=3;}}
    std::optional<m::ActivityCounters>table()const{if(failing)return {};return n::activityNetworkCounters(rows);}
};
m::ActivitySnapshot derive(const std::optional<m::ActivityCounters>&now,const std::optional<m::ActivityCounters>&before){m::ActivityRawSample a,b;a.uptime=12;b.uptime=10;a.network=now;b.network=before;return m::deriveActivity(a,&b);}
void network(){
    // The counting rule: up hardware Ethernet and Wi-Fi, and up PPP only.
    const std::vector<n::ActivityInterface>kinds{{luid(ethernet,1),ethernet,true,true,100,10},{luid(wifi,1),wifi,true,true,200,20},{luid(ethernet,2),ethernet,false,true,300,30},{luid(wifi,2),wifi,true,false,400,40},
        {luid(loopback,1),loopback,false,true,500,50},{luid(tunnel,1),tunnel,false,true,600,60},{luid(ppp,1),ppp,false,true,700,70},{luid(wifi,3),wifi,false,true,800,80},{luid(ppp,2),ppp,false,false,900,90}};
    const std::array<bool,9>counted{true,true,false,false,false,false,true,false,false};
    for(std::size_t k=0;k<kinds.size();++k)check(n::activityCountedInterface(kinds[k])==counted[k],"Counted: up hardware Ethernet/Wi-Fi and up PPP; not virtual adapters, loopback, tunnels or down interfaces");
    const auto table=n::activityNetworkCounters(kinds);
    check(table&&table->size()==3&&table->at(std::to_string(luid(ethernet,1)))==m::ActivityBytes{100,10}&&table->at(std::to_string(luid(wifi,1)))==m::ActivityBytes{200,20}&&table->at(std::to_string(luid(ppp,1)))==m::ActivityBytes{700,70},"Table reading: the counted interfaces by LUID");
    auto twice=kinds;twice.push_back({luid(ethernet,1),ethernet,true,true,1,1});check(n::activityNetworkCounters(twice)==table,"A repeated LUID keeps its first row");
    std::vector<n::ActivityInterface>many(4097);check(!n::activityNetworkCounters(many)&&(many.pop_back(),n::activityNetworkCounters(many)->empty()),"More than 4096 interfaces are unavailable; 4096 are read");
    // First reading lists the table; steady readings read only the counted
    // interfaces, into the recycled previous reading, allocating nothing.
    Interfaces system;system.rows=kinds;n::ActivityNetworkReader reader(system.source());
    std::optional<m::ActivityCounters>previous;previous.emplace();check(reader.read(*previous)&&previous==system.table()&&system.tables==1&&system.entries==0&&reader.stats().counted==3,"The first reading is the table reading");
    std::optional<m::ActivityCounters>spare;spare.emplace();std::size_t steadyAllocations{};
    // One sample as ActivityProbe and readWindowsActivityNetwork run it.
    const auto sample=[&]{std::optional<m::ActivityCounters>current;current.emplace();current->swap(*spare);const auto before=allocations;const bool ok=reader.read(*current);steadyAllocations=allocations-before;if(!ok)current.reset();
        const auto expected=system.table();check(current==expected,"Each reading equals the table reading of the moment");check(derive(current,previous)==derive(expected,previous),"Same derived upload and download");
        if(previous)spare->swap(*previous);previous=std::move(current);};
    for(unsigned k=0;k<4;++k){const auto entries=system.entries;system.grow(1000,500);sample();check(system.tables==1&&system.entries==entries+3,"A steady sample reads each counted interface once and lists nothing");if(k)check(steadyAllocations==0,"A steady sample allocates nothing");}
    // Rates: counter deltas over the elapsed time, exactly as from the table.
    {auto before=previous;system.find(luid(ethernet,1))->received+=4000;system.find(luid(wifi,1))->sent+=1000;system.find(luid(ppp,1))->received+=2;sample();const auto rates=derive(previous,before);
        check(rates.download==2001.&&rates.upload==500.,"Upload and download in bytes per second from the counter deltas");
        system.find(luid(wifi,1))->received=5;before=previous;sample();check(!derive(previous,before).download&&system.tables==1,"A counter that went back (wrapped or reset) leaves the interval unavailable, as from the table");}
    // Added: an interface that starts to count is found through the change
    // notification, never by polling the table.
    system.rows.push_back({luid(ethernet,7),ethernet,true,true,50,5});{m::ActivityCounters quiet;check(reader.read(quiet)&&quiet.size()==3&&system.tables==1,"Without a notification nothing lists the table");}
    check(reader.notify(luid(ethernet,7)),"A notification naming an interface not kept invalidates");sample();check(system.tables==2&&previous->size()==4&&reader.stats().invalidations==1,"An interface added: its notification lists the table at the next sample");
    sample();sample();check(system.tables==2&&steadyAllocations==0,"Steady again with the added interface");
    // Connected: a down Wi-Fi adapter comes up (the notification).
    system.find(luid(wifi,2))->up=true;check(reader.notify(luid(wifi,2)),"Connected: notified");sample();check(system.tables==3&&previous->size()==5,"An interface connected: counted from the next sample");
    // A kept interface's own notifications (the laptop's Wi-Fi parameter
    // changes) list nothing.
    for(unsigned k=0;k<3;++k){check(!reader.notify(luid(wifi,1)),"A notification naming a kept interface does not invalidate");system.grow(10,10);sample();}
    check(system.tables==3&&reader.stats().skipped==3&&steadyAllocations==0,"A kept interface's parameter changes: steady samples");
    // Disconnected and removed: the kept interface's own reading shows it,
    // with or without a notification; that sample is the table reading.
    system.find(luid(ethernet,1))->up=false;sample();check(system.tables==4&&previous->size()==4&&reader.stats().fallbacks==1,"An interface disconnected: the table is listed at once, without a notification");
    std::erase_if(system.rows,[](const n::ActivityInterface&r){return r.luid==luid(ppp,1);});sample();check(system.tables==5&&previous->size()==3&&reader.stats().fallbacks==2,"An interface removed: the table is listed at once, without a notification");
    system.find(luid(ethernet,7))->up=false;check(!reader.notify(luid(ethernet,7)),"A kept interface's notification is skipped");sample();check(system.tables==6&&previous->size()==2&&reader.stats().fallbacks==3,"Disconnected with its notification: its own reading lists the table once");
    sample();sample();check(system.tables==6&&steadyAllocations==0,"Steady after the changes");
    // The table cannot be read: unavailable, as before, and every sample
    // lists it again until it can.
    system.failing=true;reader.invalidate();sample();sample();check(system.tables==8&&!previous,"An unreadable table is unavailable and tried again each sample");
    system.failing=false;sample();sample();check(system.tables==9&&previous&&previous->size()==2,"Readable again: listed once, then steady");
    // A notification arrives on another thread.
    std::thread([&]{(void)reader.notify(luid(loopback,1));}).join();sample();check(system.tables==10,"A notification from another thread lists the table at the next sample");
    // A notification arriving while the table is listed invalidates even
    // when it names a kept interface: the listing may predate its change.
    system.listing=[&]{check(reader.notify(luid(wifi,1)),"During a listing every notification invalidates");};reader.invalidate();sample();system.listing=nullptr;sample();
    check(system.tables==12&&!reader.notify(luid(wifi,1)),"The notification during a listing lists again; afterwards a kept interface's is skipped");
    // Above `watchable` kept interfaces every notification invalidates.
    {Interfaces wide;for(std::uint64_t k=1;k<=n::ActivityNetworkReader::watchable+1;++k)wide.rows.push_back({luid(ethernet,100+k),ethernet,true,true,k,k});n::ActivityNetworkReader many(wide.source());m::ActivityCounters reading;
        check(many.read(reading)&&reading.size()==n::ActivityNetworkReader::watchable+1&&many.notify(luid(ethernet,101)),"Above the watched bound a kept interface's notification invalidates");
        wide.rows.pop_back();many.invalidate();check(many.read(reading)&&reading.size()==n::ActivityNetworkReader::watchable&&!many.notify(luid(ethernet,101)),"At the bound it is skipped");}
    // No counted interface (all disconnected): available and empty, nothing read.
    for(auto&r:system.rows)r.up=false;reader.invalidate();sample();const auto entries=system.entries;sample();sample();
    check(previous&&previous->empty()&&system.entries==entries&&system.tables==13&&steadyAllocations==0,"No counted interface: an empty reading that reads nothing");
    // Random histories: interfaces grow, reset, come and go, connect and
    // disconnect (each addition and connection notified with its LUID, as
    // Windows does; removals and disconnections notified or not). Every
    // sample equals the table reading and derives the same rates.
    std::mt19937 random(132);std::uint64_t next=20;unsigned listings{};
    for(unsigned step=0;step<4000;++step){const auto pick=random()%100;auto&rows=system.rows;system.failing=false;
        if(pick<70||rows.empty())system.grow(random()%5000,random()%5000);
        else if(pick<78){const std::uint32_t types[]{ethernet,wifi,ppp,loopback,tunnel};const auto type=types[random()%5];rows.push_back({luid(type,++next),type,random()%2==0,random()%3!=0,random()%100000,random()%100000});(void)reader.notify(rows.back().luid);}
        else if(pick<84){const auto at=rows.begin()+std::ptrdiff_t(random()%rows.size());const auto id=at->luid;rows.erase(at);if(random()%2)(void)reader.notify(id);}
        else if(pick<92){auto&row=rows[random()%rows.size()];row.up=!row.up;if(row.up||random()%2)(void)reader.notify(row.luid);}
        else if(pick<95){(void)reader.notify(rows[random()%rows.size()].luid);} // a parameter change
        else if(pick<98){auto&row=rows[random()%rows.size()];row.received=random()%100;}
        else{system.failing=true;reader.invalidate();}
        const auto tables=system.tables;sample();listings+=system.tables!=tables;}
    std::cout<<"network: 4000 random samples equal to the table reading, "<<listings<<" of them listing the table ("<<reader.stats().fallbacks<<" for an unreadable or disconnected kept interface, "<<reader.stats().skipped<<" notifications naming a kept interface skipped)\n";
    check(listings<4000/2,"Most random samples read only the kept interfaces");
}
int main(){try{test();gpu();network();std::cout<<"PASS "<<checks<<" Activity shared-worker lifecycle and network reading checks (no live telemetry reads)\n";}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
