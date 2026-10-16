#include "native/activity_provider.hpp"
#include "native/activity_gpu.hpp"
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
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
 n::ActivityProbe::Readers readers;readers.system=[&]{const std::uint64_t k=++reads;const double now=clock;m::ActivityRawSample raw;raw.uptime=now;raw.timestamp=now;raw.cpu=m::ActivityCPU{k*10,k*10,k*80,0};
  m::ActivityGPUSample sample;sample.time=std::uint64_t(now*1e7);sample.usage.reserve(64);sample.usage.push_back({7,engine,std::uint64_t(now*2.5e6)});raw.gpu=std::move(sample);return raw;}; // 25% busy
 readers.apps=[&]{++appReads;return m::ActivityAppsRaw{};};
 n::ActivityProbe probe(state,plan,executor,readers);const auto recycled=n::windowsActivityGPUStats().recycled;
 const auto at=[&](double t){clock=t;(void)probe.update(t);executor.waitIdle();executor.drain();};
 plan.start(100);at(100);at(105);at(110);check(plan.nextWakeTime()==115,"Hidden from launch: one 5 s deadline for CPU and GPU");
 plan.setVisible(true,false,false,110);at(111);at(112);check(plan.nextWakeTime()==113,"Shown: the same deadline at 1 s");
 plan.setVisible(false,false,false,112);at(117);at(122);check(plan.nextWakeTime()==127,"Hidden or the HUD closed: 5 s again, GPU adding no deadline");
 std::size_t cpu{},gpu{};for(const auto&s:state.history()){cpu+=s.cpuPercent==20;gpu+=s.gpuPercent&&std::abs(*s.gpuPercent-25)<1e-9;}
 check(state.history().size()==7&&reads==7&&appReads==0&&cpu==6&&gpu==6,"Every system sample after the baseline records GPU with CPU: shown, hidden and closed alike");
#ifdef _WIN32
 check(n::windowsActivityGPUStats().recycled>recycled,"The worker recycles the superseded reading's GPU buffer");
#else
 (void)recycled;
#endif
 plan.stop();at(200);check(state.history().size()==7,"Suspended: neither CPU nor GPU is sampled");}
int main(){try{test();gpu();std::cout<<"PASS "<<checks<<" Activity shared-worker lifecycle checks (no live telemetry reads)\n";}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
