#include "native/activity_provider.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace endfield::native {
namespace {void clocks(double stamp,double uptime){if(!std::isfinite(stamp)||!std::isfinite(uptime)||uptime<0)throw std::invalid_argument("Invalid Activity sample clock");}}
struct ActivityProbe::Impl:std::enable_shared_from_this<Impl>{
    struct Worker {std::optional<modules::ActivityRawSample>system;std::optional<modules::ActivityAppsRaw>apps;std::uint64_t generation{},appGeneration{};};
    modules::ActivityState&state;modules::ActivitySamplingPlan&plan;app::UtilityExecutor&executor;Readers readers;Changed changed;app::UtilityExecutor::Route route;std::thread::id owner=std::this_thread::get_id();std::shared_ptr<Worker>worker=std::make_shared<Worker>();std::shared_ptr<std::atomic<bool>>alive=std::make_shared<std::atomic<bool>>(true);std::optional<modules::ActivityDemand>pending;Stats counts;
    Impl(modules::ActivityState&s,modules::ActivitySamplingPlan&p,app::UtilityExecutor&e,Readers r,Changed c):state(s),plan(p),executor(e),readers(std::move(r)),changed(std::move(c)),route(e.makeRoute()){}
    void thread()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Activity probe owner-thread operation");}
    bool submit(){thread();if(counts.inFlight||!pending)return false;auto demand=*pending;if(!plan.running()||demand.generation!=plan.generation()){pending.reset();return false;}if(!plan.appsActive()||demand.appGeneration!=plan.appGeneration())demand.apps=false;if(!demand.system&&!demand.apps){pending.reset();return false;}
        struct Result {std::optional<modules::ActivitySnapshot>system;std::optional<modules::ActivityAppsSnapshot>apps;};auto result=std::make_shared<Result>();const auto weak=weak_from_this();
        if(!executor.submit(route,[reader=readers,retained=worker,live=alive,demand,result]{if(!live->load(std::memory_order_acquire))return;
            if(demand.system){if(retained->generation!=demand.generation||demand.resetSystem){retained->system.reset();retained->generation=demand.generation;}auto current=reader.system();clocks(current.timestamp,current.uptime);result->system=modules::deriveActivity(current,retained->system?&*retained->system:nullptr);retained->system=std::move(current);}
            if(demand.apps&&live->load(std::memory_order_acquire)){if(retained->appGeneration!=demand.appGeneration||demand.resetApps){retained->apps.reset();retained->appGeneration=demand.appGeneration;}auto current=reader.apps();clocks(current.timestamp,current.uptime);result->apps=modules::deriveActivityApps(current,retained->apps?&*retained->apps:nullptr);retained->apps=std::move(current);}},
            [weak,result,demand](std::exception_ptr error){if(auto self=weak.lock()){self->counts.inFlight=false;++self->counts.completed;if(error)++self->counts.failures;bool system{},apps{};if(self->plan.running()&&self->plan.generation()==demand.generation){if(result->system){self->state.receive(std::move(*result->system));system=true;}if(result->apps&&self->plan.appsActive()&&self->plan.appGeneration()==demand.appGeneration){self->state.receiveApps(std::move(*result->apps));apps=true;}}if(!system&&!apps)++self->counts.discarded;auto callback=self->changed;if(callback&&(system||apps))callback(system,apps);if(self->alive->load(std::memory_order_acquire))self->submit();}})){++counts.backpressure;return false;}
        pending.reset();counts.inFlight=true;++counts.submitted;return true;
    }
};
ActivityProbe::ActivityProbe(modules::ActivityState&s,modules::ActivitySamplingPlan&p,app::UtilityExecutor&e,Readers r,Changed c){if(!r.system||!r.apps)throw std::invalid_argument("Activity requires explicit system and app readers");impl_=std::make_shared<Impl>(s,p,e,std::move(r),std::move(c));}
ActivityProbe::~ActivityProbe(){impl_->alive->store(false,std::memory_order_release);impl_->executor.invalidate(impl_->route);}
bool ActivityProbe::update(double now){auto&i=*impl_;i.thread();const auto due=i.plan.takeDue(now);if((!i.counts.inFlight||due.resetSystem||due.resetApps)&&(due.system||due.apps)){if(!i.pending||i.pending->generation!=due.generation)i.pending=due;else{i.pending->system|=due.system;i.pending->apps|=due.apps;i.pending->resetSystem|=due.resetSystem;i.pending->resetApps|=due.resetApps;i.pending->appGeneration=due.appGeneration;}}return i.submit();}
bool ActivityProbe::submitPending(){return impl_->submit();}
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
namespace endfield::native {
namespace {
std::uint64_t ticks(FILETIME f){return (std::uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
struct Table {MIB_IF_TABLE2*value{};~Table(){if(value)FreeMibTable(value);}};
struct Process {HANDLE value{};~Process(){if(value)CloseHandle(value);}};
std::wstring wide(std::string_view value){if(value.size()>4096)throw std::invalid_argument("Activity name too long");if(value.empty())return {};const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(!length)throw std::invalid_argument("Invalid Activity name UTF8");std::wstring out(static_cast<std::size_t>(length),0);if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),length))throw std::runtime_error("Activity name conversion failed");return out;}
}
modules::ActivityRawSample readWindowsActivity(double stamp,double uptime){clocks(stamp,uptime);modules::ActivityRawSample out;out.timestamp=stamp;out.uptime=uptime;FILETIME idle{},kernel{},user{};
    if(GetActiveProcessorGroupCount()==1&&GetSystemTimes(&idle,&kernel,&user)&&ticks(kernel)>=ticks(idle))out.cpu=modules::ActivityCPU{ticks(user),ticks(kernel)-ticks(idle),ticks(idle),0};
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);if(GlobalMemoryStatusEx(&memory)&&memory.ullTotalPhys>0&&memory.ullAvailPhys<=memory.ullTotalPhys)out.memory=modules::ActivityMemory{memory.ullTotalPhys-memory.ullAvailPhys,memory.ullTotalPhys,{}};
    Table table;if(GetIfTable2(&table.value)==NO_ERROR&&table.value&&table.value->NumEntries<=4096){modules::ActivityCounters values;for(ULONG n=0;n<table.value->NumEntries;++n){const auto&r=table.value->Table[n];const bool supported=(r.Type==IF_TYPE_ETHERNET_CSMACD||r.Type==IF_TYPE_IEEE80211)?r.InterfaceAndOperStatusFlags.HardwareInterface!=0:r.Type==IF_TYPE_PPP;if(r.OperStatus==IfOperStatusUp&&supported)values.emplace(std::to_string(r.InterfaceLuid.Value),modules::ActivityBytes{r.InOctets,r.OutOctets});}out.network=std::move(values);}return out;
}
modules::ActivityAppsRaw readWindowsActivityApps(std::span<const modules::ActivityAppIdentity>catalog,double stamp,double uptime){clocks(stamp,uptime);if(catalog.size()>256)throw std::invalid_argument("Activity catalog exceeds256");modules::ActivityAppsRaw out;out.timestamp=stamp;out.uptime=uptime;out.secondsPerCPUTick=1e-7;out.apps.assign(catalog.begin(),catalog.end());std::set<std::uint32_t>ids;std::set<std::string>names;
    for(const auto&app:catalog){if(app.id.empty()||app.id.size()>4096||app.name.size()>4096||!names.insert(app.id).second||app.processIDs.size()>8192)throw std::invalid_argument("Invalid Activity app catalog");for(auto id:app.processIDs){if(!id)throw std::invalid_argument("Invalid Activity PID");ids.insert(id);}out.members.emplace(app.id,app.processIDs);}if(ids.size()>8192)throw std::invalid_argument("Activity PID inventory exceeds8192");FILETIME now{};GetSystemTimePreciseAsFileTime(&now);const auto wall=ticks(now);
    for(auto id:ids){Process process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,id)};if(!process.value)continue;FILETIME creation{},exit{},kernel{},user{};PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);if(!GetProcessTimes(process.value,&creation,&exit,&kernel,&user)||!GetProcessMemoryInfo(process.value,&memory,sizeof(memory))||ticks(creation)>wall||ticks(user)>std::numeric_limits<std::uint64_t>::max()-ticks(kernel))continue;modules::ActivityProcess value;value.pid=id;value.startID=ticks(creation);value.startUptime=std::max(0.,uptime-double(wall-value.startID)*1e-7);value.cpuTicks=ticks(kernel)+ticks(user);value.memoryBytes=memory.WorkingSetSize;out.processes.emplace(id,value);}return out;
}
int compareWindowsActivityNames(std::string_view a,std::string_view b){const auto x=wide(a),y=wide(b);const int result=CompareStringEx(LOCALE_NAME_USER_DEFAULT,NORM_IGNORECASE|SORT_DIGITSASNUMBERS,x.data(),static_cast<int>(x.size()),y.data(),static_cast<int>(y.size()),nullptr,nullptr,0);if(!result)throw std::runtime_error("Activity locale comparison failed");return result-CSTR_EQUAL;}
}
#else
namespace endfield::native {
modules::ActivityRawSample readWindowsActivity(double stamp,double uptime){clocks(stamp,uptime);modules::ActivityRawSample out;out.timestamp=stamp;out.uptime=uptime;return out;}
modules::ActivityAppsRaw readWindowsActivityApps(std::span<const modules::ActivityAppIdentity>catalog,double stamp,double uptime){clocks(stamp,uptime);modules::ActivityAppsRaw out;out.timestamp=stamp;out.uptime=uptime;out.apps.assign(catalog.begin(),catalog.end());out.inventoryComplete=false;return out;}
int compareWindowsActivityNames(std::string_view,std::string_view){throw std::runtime_error("Windows Activity collation unavailable; inject source fixture ordering");}
}
#endif
