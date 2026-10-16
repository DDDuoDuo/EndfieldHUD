#include "native/activity_provider.hpp"
#include "native/activity_gpu.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace endfield::native {
namespace {void clocks(double stamp,double uptime){if(!std::isfinite(stamp)||!std::isfinite(uptime)||uptime<0)throw std::invalid_argument("Invalid Activity sample clock");}
// The previous system sample's GPU buffer and network counters carry the
// next readings, so neither allocates in the steady state.
void recycle(std::optional<modules::ActivityRawSample>&previous)noexcept{if(!previous)return;if(previous->gpu){recycleWindowsActivityGPU(std::move(*previous->gpu));previous->gpu.reset();}
    if(previous->network){recycleWindowsActivityNetwork(std::move(*previous->network));previous->network.reset();}}
// MIB_IF_ROW2 Type values (ipifcons.h).
constexpr std::uint32_t ethernet=6,ppp=23,wifi=71;}
bool activityCountedInterface(const ActivityInterface&row)noexcept{return row.up&&((row.type==ethernet||row.type==wifi)?row.hardware:row.type==ppp);}
std::optional<modules::ActivityCounters>activityNetworkCounters(std::span<const ActivityInterface>table){
    if(table.size()>4096)return {};modules::ActivityCounters out;for(const auto&row:table)if(activityCountedInterface(row))out.emplace(std::to_string(row.luid),modules::ActivityBytes{row.received,row.sent});return out;}
ActivityNetworkReader::ActivityNetworkReader(Source source):source_(std::move(source)){if(!source_.table||!source_.entry)throw std::invalid_argument("Activity network reading requires a table and an entry source");}
void ActivityNetworkReader::invalidate()noexcept{invalidations_.fetch_add(1,std::memory_order_relaxed);stale_.store(true,std::memory_order_seq_cst);}
bool ActivityNetworkReader::notify(std::uint64_t luid)noexcept{
    if(luid)for(const auto&kept:watched_)if(kept.load(std::memory_order_seq_cst)==luid){skipped_.fetch_add(1,std::memory_order_relaxed);return false;}
    invalidate();return true;}
ActivityNetworkReader::Stats ActivityNetworkReader::stats()const noexcept{auto out=stats_;out.invalidations=invalidations_.load(std::memory_order_relaxed);out.skipped=skipped_.load(std::memory_order_relaxed);out.counted=kept_.size();return out;}
bool ActivityNetworkReader::read(modules::ActivityCounters&out){
    ++stats_.reads;
    // A change notified meanwhile (even during this read) leaves the flag
    // set for the next read.
    if(!stale_.exchange(false,std::memory_order_seq_cst)){if(steady(out))return true;++stats_.fallbacks;}
    return list(out);
}
bool ActivityNetworkReader::steady(modules::ActivityCounters&out){
    // `out` takes the kept interfaces' names unless it already has them (the
    // recycled previous reading): only then does a reading allocate.
    bool same=out.size()==kept_.size();for(auto at=out.begin();same&&at!=out.end();++at)same=at->first==kept_[std::size_t(at-out.begin())].key;
    if(!same){out.clear();for(const auto&kept:kept_)out.emplace(kept.key,{});}
    auto at=out.begin();for(const auto&kept:kept_){ActivityInterface row;++stats_.entries;if(!source_.entry(kept.luid,row)||row.luid!=kept.luid||!activityCountedInterface(row))return false;at->second={row.received,row.sent};++at;}
    return true;
}
bool ActivityNetworkReader::list(modules::ActivityCounters&out){
    // Until this listing's interfaces are published every notification
    // invalidates: one skipped for a previously kept interface was sent
    // before this point, so the table read below already shows its change.
    for(auto&kept:watched_)kept.store(0,std::memory_order_seq_cst);
    ++stats_.listings;table_.clear();kept_.clear();out.clear();
    if(!source_.table(table_)||table_.size()>4096){table_.clear();stale_.store(true,std::memory_order_seq_cst);return false;}
    // activityNetworkCounters, also keeping each counted name's LUID.
    for(const auto&row:table_)if(activityCountedInterface(row)){auto key=std::to_string(row.luid);if(out.emplace(key,{row.received,row.sent}).second)kept_.push_back({row.luid,std::move(key)});}
    std::sort(kept_.begin(),kept_.end(),[](const Kept&a,const Kept&b){return a.key<b.key;});
    if(kept_.size()<=watched_.size())for(std::size_t k=0;k<kept_.size();++k)watched_[k].store(kept_[k].luid,std::memory_order_seq_cst);
    return true;
}
struct ActivityProbe::Impl:std::enable_shared_from_this<Impl>{
    struct Worker {std::optional<modules::ActivityRawSample>system;std::optional<modules::ActivityAppsRaw>apps;std::uint64_t generation{},appGeneration{};};
    modules::ActivityState&state;modules::ActivitySamplingPlan&plan;app::UtilityExecutor&executor;Readers readers;Changed changed;app::UtilityExecutor::Route route;std::thread::id owner=std::this_thread::get_id();std::shared_ptr<Worker>worker=std::make_shared<Worker>();std::shared_ptr<std::atomic<bool>>alive=std::make_shared<std::atomic<bool>>(true);std::optional<modules::ActivityDemand>pending;Stats counts;bool appsRetained{};
    // AppActivityMonitor.deactivate (state.stop: previous = nil): once the
    // Apps detail is hidden the worker drops its retained Apps reading now
    // instead of keeping it until the next Apps sample.
    void releaseApps(){if(!appsRetained||plan.appsActive())return;if(executor.submit(route,[retained=worker]{retained->apps.reset();retained->appGeneration=0;},[](std::exception_ptr){})){appsRetained=false;++counts.appReleases;}}
    Impl(modules::ActivityState&s,modules::ActivitySamplingPlan&p,app::UtilityExecutor&e,Readers r,Changed c):state(s),plan(p),executor(e),readers(std::move(r)),changed(std::move(c)),route(e.makeRoute()){}
    void thread()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Activity probe owner-thread operation");}
    bool submit(){thread();if(counts.inFlight||!pending)return false;auto demand=*pending;if(!plan.running()||demand.generation!=plan.generation()){pending.reset();return false;}if(!plan.appsActive()||demand.appGeneration!=plan.appGeneration())demand.apps=false;if(!demand.system&&!demand.apps){pending.reset();return false;}
        struct Result {std::optional<modules::ActivitySnapshot>system;std::optional<modules::ActivityAppsSnapshot>apps;};auto result=std::make_shared<Result>();const auto weak=weak_from_this();
        if(!executor.submit(route,[reader=readers,retained=worker,live=alive,demand,result]{if(!live->load(std::memory_order_acquire))return;
            if(demand.system){if(retained->generation!=demand.generation||demand.resetSystem){recycle(retained->system);retained->system.reset();retained->generation=demand.generation;}auto current=reader.system();clocks(current.timestamp,current.uptime);result->system=modules::deriveActivity(current,retained->system?&*retained->system:nullptr);recycle(retained->system);retained->system=std::move(current);}
            if(demand.apps&&live->load(std::memory_order_acquire)){if(retained->appGeneration!=demand.appGeneration||demand.resetApps){retained->apps.reset();retained->appGeneration=demand.appGeneration;}auto current=reader.apps();clocks(current.timestamp,current.uptime);result->apps=modules::deriveActivityApps(current,retained->apps?&*retained->apps:nullptr);retained->apps=std::move(current);}},
            [weak,result,demand](std::exception_ptr error){if(auto self=weak.lock()){self->counts.inFlight=false;++self->counts.completed;if(error)++self->counts.failures;bool system{},apps{};if(self->plan.running()&&self->plan.generation()==demand.generation){if(result->system){self->state.receive(std::move(*result->system));system=true;}if(result->apps&&self->plan.appsActive()&&self->plan.appGeneration()==demand.appGeneration){self->state.receiveApps(std::move(*result->apps));apps=true;}}if(!system&&!apps)++self->counts.discarded;auto callback=self->changed;if(callback&&(system||apps))callback(system,apps);if(self->alive->load(std::memory_order_acquire))self->submit();}})){++counts.backpressure;return false;}
        if(demand.apps)appsRetained=true;pending.reset();counts.inFlight=true;++counts.submitted;return true;
    }
};
ActivityProbe::ActivityProbe(modules::ActivityState&s,modules::ActivitySamplingPlan&p,app::UtilityExecutor&e,Readers r,Changed c){if(!r.system||!r.apps)throw std::invalid_argument("Activity requires explicit system and app readers");impl_=std::make_shared<Impl>(s,p,e,std::move(r),std::move(c));}
ActivityProbe::~ActivityProbe(){impl_->alive->store(false,std::memory_order_release);impl_->executor.invalidate(impl_->route);}
bool ActivityProbe::update(double now){auto&i=*impl_;i.thread();i.releaseApps();const auto due=i.plan.takeDue(now);if((!i.counts.inFlight||due.resetSystem||due.resetApps)&&(due.system||due.apps)){if(!i.pending||i.pending->generation!=due.generation)i.pending=due;else{i.pending->system|=due.system;i.pending->apps|=due.apps;i.pending->resetSystem|=due.resetSystem;i.pending->resetApps|=due.resetApps;i.pending->appGeneration=due.appGeneration;}}return i.submit();}
bool ActivityProbe::submitPending(){impl_->releaseApps();return impl_->submit();}
ActivityProbe::Stats ActivityProbe::stats()const{impl_->thread();auto out=impl_->counts;out.waiting=impl_->pending.has_value();return out;}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
// The SDK gates MIB_IF_TABLE2 and GetIfTable2 on _WS2IPDEF_.
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <psapi.h>
#include <mutex>
namespace endfield::native {
namespace {
std::uint64_t ticks(FILETIME f){return (std::uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
struct Table {MIB_IF_TABLE2*value{};~Table(){if(value)FreeMibTable(value);}};
ActivityInterface activityInterface(const MIB_IF_ROW2&r){return {r.InterfaceLuid.Value,std::uint32_t(r.Type),r.InterfaceAndOperStatusFlags.HardwareInterface!=0,r.OperStatus==IfOperStatusUp,r.InOctets,r.OutOctets};}
// The production network reading: one reader for the process. Its kept
// interfaces are listed again after an IP interface change notification
// naming another interface (added, removed, connected, disconnected,
// changed; both families), which arrives on a system thread and only sets
// the reader's flag; there is no timer and nothing runs between samples
// otherwise.
struct Network {
    std::mutex mutex;ActivityNetworkReader reader{windowsActivityNetworkSource()};HANDLE notification{};
    modules::ActivityCounters spare;std::atomic<std::uint64_t>notifications{};std::uint64_t recycled{};
    ~Network(){cancel();}
    void cancel()noexcept{if(notification)CancelMibChangeNotify2(notification);notification=nullptr;}
    // Registered before the first listing, so no change after it is missed.
    // Without a registration every reading lists the table, as before.
    void watch()noexcept{if(notification)return;HANDLE handle{};if(NotifyIpInterfaceChange(AF_UNSPEC,&changed,this,FALSE,&handle)==NO_ERROR&&handle)notification=handle;else reader.invalidate();}
    static VOID NETIOAPI_API_ changed(PVOID context,PMIB_IPINTERFACE_ROW row,MIB_NOTIFICATION_TYPE type){if(type==MibInitialNotification)return;auto&self=*static_cast<Network*>(context);self.notifications.fetch_add(1,std::memory_order_relaxed);(void)self.reader.notify(row?row->InterfaceLuid.Value:0);}
};
Network&network(){static Network value;return value;}
struct Process {HANDLE value{};~Process(){if(value)CloseHandle(value);}};
std::wstring wide(std::string_view value){if(value.size()>4096)throw std::invalid_argument("Activity name too long");if(value.empty())return {};const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(!length)throw std::invalid_argument("Invalid Activity name UTF8");std::wstring out(static_cast<std::size_t>(length),0);if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),length))throw std::runtime_error("Activity name conversion failed");return out;}
}
// PROCESS_MEMORY_COUNTERS_EX2 layout (psapi.h, Windows build 20348+), kept
// local so an older SDK target macro cannot hide PrivateWorkingSetSize.
struct ActivityMemoryCounters2 {DWORD cb;DWORD PageFaultCount;SIZE_T PeakWorkingSetSize,WorkingSetSize,QuotaPeakPagedPoolUsage,QuotaPagedPoolUsage,QuotaPeakNonPagedPoolUsage,QuotaNonPagedPoolUsage,PagefileUsage,PeakPagefileUsage,PrivateUsage,PrivateWorkingSetSize;ULONG64 SharedCommitUsage;};
std::optional<std::uint64_t>windowsActivityProcessMemory(void*process)noexcept{
    ActivityMemoryCounters2 extended{};extended.cb=sizeof(extended);
    if(GetProcessMemoryInfo(static_cast<HANDLE>(process),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&extended),sizeof(extended))&&extended.PrivateWorkingSetSize<=extended.WorkingSetSize)return std::uint64_t(extended.PrivateWorkingSetSize);
    PROCESS_MEMORY_COUNTERS basic{};basic.cb=sizeof(basic);if(GetProcessMemoryInfo(static_cast<HANDLE>(process),&basic,sizeof(basic)))return std::uint64_t(basic.WorkingSetSize);
    return {};
}
ActivityNetworkReader::Source windowsActivityNetworkSource(){
    return {[](std::vector<ActivityInterface>&rows){Table table;if(GetIfTable2(&table.value)!=NO_ERROR||!table.value||table.value->NumEntries>4096)return false;rows.reserve(table.value->NumEntries);for(ULONG n=0;n<table.value->NumEntries;++n)rows.push_back(activityInterface(table.value->Table[n]));return true;},
        [](std::uint64_t luid,ActivityInterface&row){MIB_IF_ROW2 value{};value.InterfaceLuid.Value=luid;if(GetIfEntry2(&value)!=NO_ERROR)return false;row=activityInterface(value);return true;}};
}
std::optional<modules::ActivityCounters>readWindowsActivityNetwork(){
    auto&n=network();std::lock_guard lock(n.mutex);n.watch();
    std::optional<modules::ActivityCounters>out;out.emplace();out->swap(n.spare);
    if(!n.reader.read(*out)){n.spare.swap(*out);return {};}
    return out;
}
void recycleWindowsActivityNetwork(modules::ActivityCounters&&value)noexcept{auto&n=network();std::lock_guard lock(n.mutex);n.spare.swap(value);++n.recycled;}
WindowsActivityNetworkStats windowsActivityNetworkStats()noexcept{auto&n=network();std::lock_guard lock(n.mutex);return {n.reader.stats(),n.notifications.load(std::memory_order_relaxed),n.recycled,n.notification!=nullptr};}
void releaseWindowsActivityNetwork()noexcept{auto&n=network();std::lock_guard lock(n.mutex);n.cancel();n.reader.invalidate();modules::ActivityCounters().swap(n.spare);}
modules::ActivityRawSample readWindowsActivity(double stamp,double uptime){clocks(stamp,uptime);modules::ActivityRawSample out;out.timestamp=stamp;out.uptime=uptime;FILETIME idle{},kernel{},user{};
    if(GetActiveProcessorGroupCount()==1&&GetSystemTimes(&idle,&kernel,&user)&&ticks(kernel)>=ticks(idle))out.cpu=modules::ActivityCPU{ticks(user),ticks(kernel)-ticks(idle),ticks(idle),0};
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);if(GlobalMemoryStatusEx(&memory)&&memory.ullTotalPhys>0&&memory.ullAvailPhys<=memory.ullTotalPhys)out.memory=modules::ActivityMemory{memory.ullTotalPhys-memory.ullAvailPhys,memory.ullTotalPhys,{}};
    // The counted interfaces only (ActivityNetworkReader): the table
    // reading's counters without listing every interface each sample.
    out.network=readWindowsActivityNetwork();
    // GPU engines with every system sample, like CPU, RAM and network
    // (native/activity_gpu.hpp): shown, hidden or with the HUD closed.
    out.gpu=readWindowsActivityGPU();return out;
}
modules::ActivityAppsRaw readWindowsActivityApps(std::span<const modules::ActivityAppIdentity>catalog,double stamp,double uptime){clocks(stamp,uptime);if(catalog.size()>256)throw std::invalid_argument("Activity catalog exceeds256");modules::ActivityAppsRaw out;out.timestamp=stamp;out.uptime=uptime;out.secondsPerCPUTick=1e-7;out.apps.assign(catalog.begin(),catalog.end());std::set<std::uint32_t>ids;std::set<std::string>names;
    for(const auto&app:catalog){if(app.id.empty()||app.id.size()>4096||app.name.size()>4096||!names.insert(app.id).second||app.processIDs.size()>8192)throw std::invalid_argument("Invalid Activity app catalog");for(auto id:app.processIDs){if(!id)throw std::invalid_argument("Invalid Activity PID");ids.insert(id);}out.members.emplace(app.id,app.processIDs);}if(ids.size()>8192)throw std::invalid_argument("Activity PID inventory exceeds8192");FILETIME now{};GetSystemTimePreciseAsFileTime(&now);const auto wall=ticks(now);
    for(auto id:ids){Process process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,id)};if(!process.value)continue;FILETIME creation{},exit{},kernel{},user{};if(!GetProcessTimes(process.value,&creation,&exit,&kernel,&user)||ticks(creation)>wall||ticks(user)>std::numeric_limits<std::uint64_t>::max()-ticks(kernel))continue;const auto memory=windowsActivityProcessMemory(process.value);if(!memory)continue;modules::ActivityProcess value;value.pid=id;value.startID=ticks(creation);value.startUptime=std::max(0.,uptime-double(wall-value.startID)*1e-7);value.cpuTicks=ticks(kernel)+ticks(user);value.memoryBytes=*memory;out.processes.emplace(id,value);}return out;
}
int compareWindowsActivityNames(std::string_view a,std::string_view b){const auto x=wide(a),y=wide(b);const int result=CompareStringEx(LOCALE_NAME_USER_DEFAULT,NORM_IGNORECASE|SORT_DIGITSASNUMBERS,x.data(),static_cast<int>(x.size()),y.data(),static_cast<int>(y.size()),nullptr,nullptr,0);if(!result)throw std::runtime_error("Activity locale comparison failed");return result-CSTR_EQUAL;}
}
#else
namespace endfield::native {
std::optional<modules::ActivityCounters>readWindowsActivityNetwork(){return {};}
void recycleWindowsActivityNetwork(modules::ActivityCounters&&)noexcept{}
WindowsActivityNetworkStats windowsActivityNetworkStats()noexcept{return {};}
void releaseWindowsActivityNetwork()noexcept{}
modules::ActivityRawSample readWindowsActivity(double stamp,double uptime){clocks(stamp,uptime);modules::ActivityRawSample out;out.timestamp=stamp;out.uptime=uptime;return out;}
modules::ActivityAppsRaw readWindowsActivityApps(std::span<const modules::ActivityAppIdentity>catalog,double stamp,double uptime){clocks(stamp,uptime);modules::ActivityAppsRaw out;out.timestamp=stamp;out.uptime=uptime;out.apps.assign(catalog.begin(),catalog.end());out.inventoryComplete=false;return out;}
int compareWindowsActivityNames(std::string_view,std::string_view){throw std::runtime_error("Windows Activity collation unavailable; inject source fixture ordering");}
}
#endif
