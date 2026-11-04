#include "modules/activity_state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::modules {
namespace {
using U=std::uint64_t;
bool add(U&a,U b)noexcept{if(b>std::numeric_limits<U>::max()-a)return false;a+=b;return true;}
std::optional<U>product(U a,U b)noexcept{if(b&&a>std::numeric_limits<U>::max()/b)return {};return a*b;}
void time(double n){if(!std::isfinite(n)||n<0)throw std::invalid_argument("Invalid Activity clock");}
bool validInterval(double elapsed,double maximum){return std::isfinite(elapsed)&&elapsed>0&&elapsed<=maximum;}
std::optional<double>cpu(const std::optional<ActivityCPU>&a,const std::optional<ActivityCPU>&b){if(!a||!b)return {};const std::array<U,4>x{a->user,a->system,a->idle,a->nice},y{b->user,b->system,b->idle,b->nice};U total{};for(unsigned k=0;k<4;++k)if(x[k]<y[k]||!add(total,x[k]-y[k]))return {};if(!total)return {};return std::clamp(double(total-(a->idle-b->idle))/double(total)*100,0.,100.);}
std::optional<std::array<double,2>>rates(const std::optional<ActivityCounters>&a,const std::optional<ActivityCounters>&b,double elapsed){if(!a||!b||a->size()!=b->size()||!validInterval(elapsed,std::numeric_limits<double>::max()))return {};U received{},sent{};auto old=b->begin();for(const auto&[key,value]:*a){if(key!=old->first||value.received<old->second.received||value.sent<old->second.sent||!add(received,value.received-old->second.received)||!add(sent,value.sent-old->second.sent))return {};++old;}const std::array<double,2>out{double(received)/elapsed,double(sent)/elapsed};if(!std::isfinite(out[0])||!std::isfinite(out[1]))return {};return out;}
std::vector<std::uint32_t>pids(std::vector<std::uint32_t>p){std::sort(p.begin(),p.end());p.erase(std::unique(p.begin(),p.end()),p.end());return p;}
std::optional<double>combined(std::optional<double>a,std::optional<double>b){if(!a||!b||!std::isfinite(*a)||!std::isfinite(*b)||*a<0||*b<0||!std::isfinite(*a+*b))return {};return *a+*b;}
void increment(U&v){if(v==std::numeric_limits<U>::max())throw std::overflow_error("Activity generation exhausted");++v;}
void metric(std::optional<double>value){if(value&&(!std::isfinite(*value)||*value<0))throw std::invalid_argument("Invalid Activity metric");}
void validateSnapshot(const ActivitySnapshot&v){time(v.uptime);if(!std::isfinite(v.timestamp))throw std::invalid_argument("Invalid Activity timestamp");metric(v.cpuPercent);metric(v.gpuPercent);metric(v.upload);metric(v.download);metric(v.diskRead);metric(v.diskWrite);if(v.memory&&(!v.memory->total||v.memory->used>v.memory->total||(v.memory->compressed&&*v.memory->compressed>v.memory->total)))throw std::invalid_argument("Invalid Activity memory");}
}
std::uint32_t activityGPUEngineType(std::string_view label)noexcept{std::uint32_t hash=2166136261u;for(const char c:label){hash^=static_cast<unsigned char>(c);hash*=16777619u;}return hash;}
std::optional<double>activityGPUPercent(const ActivityGPUSample&current,const ActivityGPUSample&previous,std::span<const std::uint32_t>pids){
    const auto ordered=[](const ActivityGPUSample&v){if(v.usage.size()>ActivityGPUSample::maximumUsage)return false;for(std::size_t n=1;n<v.usage.size();++n){const auto&a=v.usage[n-1];const auto&b=v.usage[n];if(!(a.engine<b.engine||(a.engine==b.engine&&a.pid<b.pid)))return false;}return true;};
    if(current.time<=previous.time||!ordered(current)||!ordered(previous)||!std::is_sorted(pids.begin(),pids.end()))return {};
    const double elapsed=double(current.time-previous.time);const auto selected=[&](std::uint32_t pid){return pids.empty()||std::binary_search(pids.begin(),pids.end(),pid);};
    // One pass over the (adapter, type, engine)-ordered collection: an
    // engine closes into its type's busiest engine, a type into its
    // adapter's sum, an adapter into the busier adapter's load.
    double load{},adapter{},type{},running{};std::optional<ActivityGPUEngine>engine;
    const auto close=[&](const ActivityGPUEngine*next){if(!engine)return;type=std::max(type,std::min(100.,running/elapsed*100));running=0;
        const bool otherAdapter=!next||next->luid!=engine->luid||next->physical!=engine->physical;
        if(otherAdapter||next->type!=engine->type){adapter+=type;type=0;}
        if(otherAdapter){load=std::max(load,std::min(100.,adapter));adapter=0;}
        engine.reset();};
    // Merge join over (engine, pid); a growth only counts within one engine.
    auto old=previous.usage.begin();
    for(const auto&value:current.usage){
        if(engine&&value.engine!=*engine)close(&value.engine);
        if(!engine)engine=value.engine;
        while(old!=previous.usage.end()&&(old->engine<value.engine||(old->engine==value.engine&&old->pid<value.pid)))++old;
        if(old==previous.usage.end()||old->engine!=value.engine||old->pid!=value.pid||!selected(value.pid)||value.runningTime<old->runningTime)continue;
        running+=double(value.runningTime-old->runningTime);
    }
    close(nullptr);if(!std::isfinite(load))return {};return std::clamp(load,0.,100.);
}
std::optional<ActivityMemory>activityMacMemory(U physical,U pageSize,U internal,U purgeable,U wired,U compressed)noexcept{if(!physical||!pageSize)return {};U pages=internal-std::min(internal,purgeable);if(!add(pages,wired)||!add(pages,compressed))return {};const auto used=product(pages,pageSize),compression=product(compressed,pageSize);if(!used||!compression)return {};return ActivityMemory{std::min(physical,*used),physical,std::min(physical,*compression)};}
ActivitySnapshot deriveActivity(const ActivityRawSample&value,const ActivityRawSample*previous){ActivitySnapshot out;out.timestamp=value.timestamp;out.uptime=value.uptime;out.memory=value.memory;const double elapsed=previous?value.uptime-previous->uptime:0;std::optional<std::array<double,2>>network,disk;if(validInterval(elapsed,15)){out.cpuPercent=cpu(value.cpu,previous->cpu);network=rates(value.network,previous->network,elapsed);disk=rates(value.disk,previous->disk,elapsed);if(value.gpu&&previous->gpu)out.gpuPercent=activityGPUPercent(*value.gpu,*previous->gpu);}if(network){out.download=(*network)[0];out.upload=(*network)[1];}if(disk){out.diskRead=(*disk)[0];out.diskWrite=(*disk)[1];}if(!out.cpuPercent)out.statusNotes.push_back(value.cpu?"Collecting CPU baseline":"CPU unavailable");if(!out.memory)out.statusNotes.push_back("RAM unavailable");if(!network)out.statusNotes.push_back(value.network?"Collecting network baseline":"Network unavailable");if(!disk)out.statusNotes.push_back(value.disk?"Collecting disk baseline":"Disk activity unavailable");return out;}
ActivityAppsSnapshot deriveActivityApps(const ActivityAppsRaw&value,const ActivityAppsRaw*previous){if(value.apps.size()>ActivityState::maximumApps)throw std::invalid_argument("Activity catalog exceeds source256 limit");ActivityAppsSnapshot out;out.timestamp=value.timestamp;out.uptime=value.uptime;out.statusNotes=value.statusNotes;out.items.reserve(value.apps.size());const double elapsed=previous?value.uptime-previous->uptime:0;
    for(const auto&identity:value.apps){ActivityApp item;item.identity=identity;const auto member=value.members.find(identity.id);item.identity.processIDs=pids(member==value.members.end()?identity.processIDs:member->second);const auto&ids=item.identity.processIDs;
        bool complete=value.inventoryComplete&&!ids.empty();for(auto id:ids)if(!value.processes.contains(id))complete=false;
        bool same=false;if(previous){const auto prior=previous->members.find(identity.id);same=prior!=previous->members.end()&&pids(prior->second)==ids;}
        if(complete){U memory{};bool validMemory=true;for(auto id:ids)validMemory=add(memory,value.processes.at(id).memoryBytes)&&validMemory;if(validMemory)item.memoryBytes=memory;
            // Threads (:99-104): every member's current count, else none.
            U threads{};bool counted=true;for(auto id:ids){const auto&v=value.processes.at(id).threads;counted=counted&&v&&add(threads,*v);}if(counted)item.threads=threads;
            if(validInterval(elapsed,5)&&same&&std::isfinite(value.secondsPerCPUTick)&&value.secondsPerCPUTick>0){U ticks{},reads{},writes{};bool valid=true,validTicks=true,hasDisk=true,validReads=true,validWrites=true;
                for(auto id:ids){const auto&v=value.processes.at(id);const auto old=previous->processes.find(id);if(old==previous->processes.end()||old->second.startID!=v.startID||v.cpuTicks<old->second.cpuTicks){valid=false;break;}const auto&o=old->second;if(v.diskRead&&v.diskWrite&&o.diskRead&&o.diskWrite){if(*v.diskRead<*o.diskRead||*v.diskWrite<*o.diskWrite){valid=false;break;}validReads=add(reads,*v.diskRead-*o.diskRead)&&validReads;validWrites=add(writes,*v.diskWrite-*o.diskWrite)&&validWrites;}else hasDisk=false;validTicks=add(ticks,v.cpuTicks-o.cpuTicks)&&validTicks;}
                if(valid){if(validTicks){const double percent=double(ticks)*value.secondsPerCPUTick/elapsed*100;if(std::isfinite(percent))item.cpuPercent=std::max(0.,percent);}if(hasDisk&&validReads)item.diskRead=double(reads)/elapsed;if(hasDisk&&validWrites)item.diskWrite=double(writes)/elapsed;}}
            if(value.network&&value.uptime-value.network->uptime>=0&&value.uptime-value.network->uptime<=4){const auto&net=*value.network;bool valid=true;double received{},sent{};for(auto id:ids){if(!(value.processes.at(id).startUptime<=net.intervalStart)||net.unknownPIDs.contains(id)){valid=false;break;}const auto found=net.rates.find(id);if(found!=net.rates.end()){received+=found->second[0];sent+=found->second[1];}}if(valid&&std::isfinite(received)&&std::isfinite(sent)){item.download=received;item.upload=sent;}}}
        // GPU (:124-130): a complete app only (Mac 1.3.0; the v1.2.0 Windows
        // reading also covered apps with protected processes), and, like CPU,
        // a member whose start time differs from the previous reading (a
        // reused PID) leaves it unknown for one interval.
        if(complete&&validInterval(elapsed,5)&&same&&value.gpu&&previous->gpu){bool reused{};for(auto id:ids){const auto old=previous->processes.find(id);if(old==previous->processes.end()||value.processes.at(id).startID!=old->second.startID)reused=true;}if(!reused)item.gpuPercent=activityGPUPercent(*value.gpu,*previous->gpu,ids);}
        if(!complete)item.statusNotes.push_back("Some app processes are unavailable or protected");else if(!item.cpuPercent)item.statusNotes.push_back("Collecting app baseline");if(!item.memoryBytes)item.statusNotes.push_back("RAM unavailable");if(!item.download)item.statusNotes.push_back("Network unavailable or collecting baseline");out.items.push_back(std::move(item));}
    return out;
}
std::string_view activitySortKey(ActivitySort key)noexcept{static constexpr std::array<std::string_view,11>keys{"name","cpu","memory","network","disk","upload","download","diskRead","diskWrite","gpu","threads"};const auto i=static_cast<unsigned>(key);return i<keys.size()?keys[i]:std::string_view{};}
void sortActivityApps(std::vector<ActivityApp>&items,ActivitySort key,bool descending,const ActivityNameCompare&compare){if(!compare||activitySortKey(key).empty())throw std::invalid_argument("Activity requires source-compatible name comparison");auto number=[key](const ActivityApp&i)->std::optional<double>{switch(key){case ActivitySort::name:return {};case ActivitySort::cpu:return i.cpuPercent;case ActivitySort::memory:return i.memoryBytes?std::optional<double>(double(*i.memoryBytes)):std::nullopt;case ActivitySort::network:return combined(i.upload,i.download);case ActivitySort::disk:return combined(i.diskRead,i.diskWrite);case ActivitySort::upload:return i.upload;case ActivitySort::download:return i.download;case ActivitySort::diskRead:return i.diskRead;case ActivitySort::diskWrite:return i.diskWrite;case ActivitySort::gpu:return i.gpuPercent;case ActivitySort::threads:return i.threads?std::optional<double>(double(*i.threads)):std::nullopt;}return {};};
    std::sort(items.begin(),items.end(),[&](const auto&a,const auto&b){if(key!=ActivitySort::name){const auto x=number(a),y=number(b);if(x&&y&&*x!=*y)return descending?*x>*y:*x<*y;if(bool(x)!=bool(y))return bool(x);}const int names=compare(a.identity.name,b.identity.name);if(names)return key==ActivitySort::name&&descending?names>0:names<0;return a.identity.id<b.identity.id;});}
ActivityState::ActivityState(ActivityNameCompare compare):compare_(std::move(compare)){if(!compare_)throw std::invalid_argument("Activity requires name ordering");}
void ActivityState::receive(ActivitySnapshot value){validateSnapshot(value);latest_=value;if(count_==historyLimit){std::move(history_.begin()+1,history_.end(),history_.begin());history_.back()=std::move(value);}else history_[count_++]=std::move(value);++revision_;++overviewRevision_;if(onSystem)onSystem();}
void ActivityState::receiveApps(ActivityAppsSnapshot value){time(value.uptime);if(!std::isfinite(value.timestamp)||value.items.size()>maximumApps)throw std::invalid_argument("Invalid Activity app snapshot");std::set<std::string>identities;for(const auto&i:value.items){if(i.identity.id.empty()||i.identity.id.size()>4096||i.identity.name.size()>4096||i.identity.iconKey.size()>4096||!identities.insert(i.identity.id).second)throw std::invalid_argument("Invalid Activity app identity");metric(i.cpuPercent);metric(i.upload);metric(i.download);metric(i.diskRead);metric(i.diskWrite);metric(i.gpuPercent);}sortActivityApps(value.items,sort_,descending_,compare_);appSnapshot_=std::move(value);++revision_;++appsRevision_;if(onApps)onApps();}
void ActivityState::setSort(ActivitySort key,bool descending){if(activitySortKey(key).empty())throw std::invalid_argument("Invalid Activity sort");sortActivityApps(appSnapshot_.items,key,descending,compare_);sort_=key;descending_=descending;++revision_;++appsRevision_;}
void ActivitySamplingPlan::start(double now){time(now);if(running_)return;increment(generation_);running_=true;resetSystem_=true;systemDue_=now;if(visible_&&apps_){increment(appGeneration_);resetApps_=true;appsDue_=now;}}
void ActivitySamplingPlan::stop(){if(!running_)return;increment(generation_);increment(appGeneration_);running_=visible_=alert_=apps_=false;systemDue_.reset();appsDue_.reset();resetSystem_=resetApps_=false;}
void ActivitySamplingPlan::setVisible(bool visible,bool alert,bool apps,double now){time(now);const auto before=interval();const bool wasApps=appsActive();visible_=visible;alert_=alert;apps_=apps;if(!running_&&(visible||alert)){start(now);return;}if(!running_)return;if(before!=interval())systemDue_=now+interval();if(wasApps!=appsActive()){increment(appGeneration_);resetApps_=appsActive();if(appsActive())appsDue_=now;else appsDue_.reset();}}
void ActivitySamplingPlan::setAlert(bool alert,double now){time(now);if(running_&&alert_==alert)return;if(!running_&&!alert)return;setVisible(running_&&visible_,alert,running_&&apps_,now);}
ActivityTelemetryLink::ActivityTelemetryLink(ActivitySamplingPlan&plan,const ActivityState&state):plan_(plan),state_(state){}
bool ActivityTelemetryLink::synchronize(bool demand,double now,const Deliver&deliver){
    const auto before=plan_.nextWakeTime();
    if(demand!=attached_){attached_=demand;plan_.setAlert(demand,now);delivered_=state_.overviewRevision();if(deliver)deliver(demand?&state_.snapshot():nullptr);return before!=plan_.nextWakeTime();}
    if(!attached_)return false;
    // A stop/start cycle (sleep, lock) cleared the alert: attach again.
    if(!plan_.alertActive())plan_.setAlert(true,now);
    if(delivered_!=state_.overviewRevision()){delivered_=state_.overviewRevision();if(deliver)deliver(&state_.snapshot());}
    return before!=plan_.nextWakeTime();
}
ActivityDemand ActivitySamplingPlan::takeDue(double now){time(now);ActivityDemand out;out.generation=generation_;out.appGeneration=appGeneration_;if(!running_)return out;if(systemDue_&&now>=*systemDue_){out.system=true;out.resetSystem=resetSystem_;resetSystem_=false;systemDue_=now+interval();}if(appsDue_&&now>=*appsDue_){out.apps=true;out.resetApps=resetApps_;resetApps_=false;appsDue_=now+1;}return out;}
std::optional<double>ActivitySamplingPlan::nextWakeTime()const noexcept{if(systemDue_&&appsDue_)return std::min(*systemDue_,*appsDue_);return systemDue_?systemDue_:appsDue_;}
} // namespace endfield::modules
