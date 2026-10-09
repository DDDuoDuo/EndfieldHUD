#include "native/activity_catalog.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
void need(bool b,const char*m){if(!b)throw std::invalid_argument(m);}
bool text(std::string_view s,std::size_t cap){return s.size()<=cap&&ehud::data::Json::validUtf8(s)&&s.find('\0')==s.npos;}
void validate(const ActivityCatalogProcess&p){need(p.pid&&p.startID&&p.pid!=p.parent&&!p.imageKey.empty()&&text(p.imageKey,4096)&&text(p.applicationID,4096)&&text(p.name,4096)&&text(p.iconKey,4096),"Invalid Activity process identity");}
std::string key(const ActivityCatalogProcess&p){return p.applicationID.empty()?"image:"+p.imageKey:"app:"+p.applicationID;}
bool childImage(std::string_view image,std::string_view parent){const auto slash=parent.find_last_of("/\\");return slash!=parent.npos&&slash>3&&image.size()>slash+1&&image.substr(0,slash+1)==parent.substr(0,slash+1);}
}
ActivityCatalogResult buildActivityCatalog(const ActivityCatalogInventory&input){
    need(input.processes.size()<=8192&&input.unreadablePIDs.size()<=8192-input.processes.size(),"Activity process inventory exceeds8192");ActivityCatalogResult out;out.complete=input.complete;std::map<std::uint32_t,const ActivityCatalogProcess*>all;
    for(const auto&p:input.processes){validate(p);need(all.emplace(p.pid,&p).second,"Duplicate Activity PID");}std::set<std::uint32_t>unreadable;for(auto pid:input.unreadablePIDs)need(pid&&!all.contains(pid)&&unreadable.insert(pid).second,"Invalid unreadable Activity PID");
    // Window/PID snapshots may race exits. A process born after the capture
    // began cannot inherit an older occupant's window or parent metadata.
    if(input.captureStartID)std::erase_if(all,[&](const auto&entry){return entry.second->startID>input.captureStartID;});
    std::map<std::string,modules::ActivityAppIdentity,std::less<>>groups;std::map<std::uint32_t,std::string>owner;
    for(const auto&[pid,p]:all)if(p->appWindow||!p->applicationID.empty()){
        const auto id=key(*p);auto[it,inserted]=groups.try_emplace(id,modules::ActivityAppIdentity{id,p->name.empty()?p->imageKey:p->name,p->iconKey,{}});(void)inserted;owner.emplace(pid,id);
    }
    need(groups.size()<=modules::ActivityState::maximumApps,"Activity app inventory exceeds256");
    // Resolve a sorted creation-time traversal, bounded once per five seconds.
    // A reused parent PID created after its child cannot establish ownership.
    std::vector<const ActivityCatalogProcess*>ordered;ordered.reserve(all.size());for(const auto&[_,p]:all)ordered.push_back(p);std::sort(ordered.begin(),ordered.end(),[](auto*a,auto*b){return a->startID<b->startID||(a->startID==b->startID&&a->pid<b->pid);});
    for(const auto*p:ordered){if(owner.contains(p->pid))continue;const auto direct=groups.find(key(*p));if(direct!=groups.end()){owner.emplace(p->pid,direct->first);continue;}
        if(!p->applicationID.empty())continue;const auto parent=all.find(p->parent);if(parent==all.end()||parent->second->startID>=p->startID)continue;const auto owned=owner.find(p->parent);if(owned==owner.end()||!owned->second.starts_with("image:"))continue;
        const auto root=owned->second.substr(6);if(childImage(p->imageKey,root))owner.emplace(p->pid,owned->second);
    }
    for(const auto&[pid,id]:owner){groups.at(id).processIDs.push_back(pid);out.identities.emplace(pid,*all.at(pid));}
    out.apps.reserve(groups.size());for(auto&[_,app]:groups)out.apps.push_back(std::move(app));return out;
}
ActivityCatalogSampler::ActivityCatalogSampler(Readers r):readers_(std::move(r)){need(bool(readers_.inventory)&&bool(readers_.process),"Activity catalog requires injected readers");}
void ActivityCatalogSampler::reset()noexcept{catalog_.apps.clear();catalog_.identities.clear();catalog_.complete=true;inventoryTime_.reset();lastTime_.reset();}
modules::ActivityAppsRaw ActivityCatalogSampler::sample(double stamp,double now){need(std::isfinite(stamp)&&std::isfinite(now)&&now>=0,"Invalid Activity catalog clock");const auto generation=generation_.load(std::memory_order_relaxed);if(workerGeneration_!=generation){reset();workerGeneration_=generation;}need(std::isfinite(stamp)&&std::isfinite(now)&&now>=0&&(!lastTime_||now>=*lastTime_),"Invalid Activity catalog clock");lastTime_=now;
    if(!inventoryTime_||now-*inventoryTime_>=5){inventoryTime_=now;try{auto inventory=readers_.inventory();(void)buildActivityCatalog(inventory);for(auto pid:inventory.unreadablePIDs){const auto old=catalog_.identities.find(pid);if(old!=catalog_.identities.end())inventory.processes.push_back(old->second);}inventory.unreadablePIDs.clear();auto next=buildActivityCatalog(inventory);if(next.complete)catalog_=std::move(next);else catalog_.complete=false;}catch(const std::exception&){catalog_.complete=false;}}
    modules::ActivityAppsRaw out;out.timestamp=stamp;out.uptime=now;out.secondsPerCPUTick=1e-7;out.apps=catalog_.apps;out.inventoryComplete=catalog_.complete;
    for(const auto&app:out.apps){auto&members=out.members[app.id];for(auto pid:app.processIDs){const auto&old=catalog_.identities.at(pid);auto current=readers_.process(pid);if(!current){members.push_back(pid);continue;}validate(current->identity);need(current->identity.pid==pid,"Activity reader returned another PID");
            // Reuse/path/app changes stay unknown until the next bounded
            // inventory; never charge a new process to a stale card.
            if(current->identity.startID!=old.startID||current->identity.imageKey!=old.imageKey||current->identity.applicationID!=old.applicationID){members.push_back(pid);continue;}
            members.push_back(pid);if(current->usage){const auto&v=*current->usage;need(v.pid==pid&&v.startID==old.startID&&std::isfinite(v.startUptime)&&v.startUptime>=0,"Activity identity/usage mismatch");out.processes.emplace(pid,v);}
        }}
    if(!out.inventoryComplete)out.statusNotes.push_back("App helper inventory unavailable");out.statusNotes.push_back("Per-app disk and network accounting unavailable");return out;
}
}
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <appmodel.h>
#include <psapi.h>
#include <array>
namespace endfield::native {
namespace {
struct Handle{HANDLE value{};~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
std::uint64_t ticks(FILETIME v){return (std::uint64_t(v.dwHighDateTime)<<32)|v.dwLowDateTime;}
std::string utf8(std::wstring_view w){if(w.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);if(n<=0||n>4096)return {};std::string out(std::size_t(n),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),out.data(),n,nullptr,nullptr)!=n)return {};return out;}
std::optional<std::vector<std::uint8_t>>userSID(HANDLE process){Handle token;if(!OpenProcessToken(process,TOKEN_QUERY,&token.value))return {};DWORD n{};GetTokenInformation(token.value,TokenUser,nullptr,0,&n);if(!n||n>65536)return {};std::vector<std::uint8_t>bytes(n);if(!GetTokenInformation(token.value,TokenUser,bytes.data(),n,&n))return {};const auto sid=reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid;if(!IsValidSid(sid))return {};const auto size=GetLengthSid(sid);std::vector<std::uint8_t>out(size);if(!CopySid(size,out.data(),sid))return {};return out;}
struct NativeCatalog {
    std::optional<std::vector<std::uint8_t>>sid;
    bool ensureSID(){if(!sid)sid=userSID(GetCurrentProcess());return bool(sid);}
    std::optional<ActivityCatalogReading>read(std::uint32_t pid,std::uint32_t parent,bool window,bool usage,bool*otherUser=nullptr){
        if(!ensureSID())return {};Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};if(!process.value)return {};auto user=userSID(process.value);if(!user)return {};if(!EqualSid(user->data(),sid->data())){if(otherUser)*otherUser=true;return {};}
        std::array<wchar_t,32768>path{};DWORD n=DWORD(path.size());FILETIME created{},exit{},kernel{},userTime{};if(!QueryFullProcessImageNameW(process.value,0,path.data(),&n)||!n||!GetProcessTimes(process.value,&created,&exit,&kernel,&userTime)||!ticks(created))return {};
        std::wstring original(path.data(),n),normalized(n,L'\0');if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,original.data(),int(n),normalized.data(),int(n),nullptr,nullptr,0))return {};
        ActivityCatalogReading out;auto&identity=out.identity;identity.pid=pid;identity.parent=parent;identity.startID=ticks(created);identity.imageKey=utf8(normalized);identity.iconKey=utf8(original);identity.appWindow=window;if(identity.imageKey.empty())return {};
        const auto slash=original.find_last_of(L"\\/");auto label=original.substr(slash==original.npos?0:slash+1);if(label.size()>4&&label.substr(label.size()-4)==L".exe")label.resize(label.size()-4);identity.name=utf8(label);
        UINT32 size{};const auto query=GetApplicationUserModelId(process.value,&size,nullptr);
        if(query!=APPMODEL_ERROR_NO_APPLICATION){if(query!=ERROR_INSUFFICIENT_BUFFER||size<=1||size>4096)return {};std::vector<wchar_t>id(size);if(GetApplicationUserModelId(process.value,&size,id.data())!=ERROR_SUCCESS||size<=1||size>id.size()||id[size-1]!=L'\0')return {};identity.applicationID=utf8(std::wstring_view(id.data(),size-1));if(identity.applicationID.empty())return {};}
        if(usage){PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);FILETIME wall{};GetSystemTimePreciseAsFileTime(&wall);if(GetProcessMemoryInfo(process.value,&memory,sizeof(memory))&&ticks(wall)>=identity.startID&&ticks(userTime)<=UINT64_MAX-ticks(kernel)){modules::ActivityProcess p;p.pid=pid;p.startID=identity.startID;p.cpuTicks=ticks(userTime)+ticks(kernel);p.memoryBytes=memory.WorkingSetSize;const double uptime=double(GetTickCount64())*.001;p.startUptime=std::max(0.,uptime-double(ticks(wall)-p.startID)*1e-7);out.usage=p;}}
        return out;
    }
    ActivityCatalogInventory inventory(){ActivityCatalogInventory out;FILETIME began{};GetSystemTimePreciseAsFileTime(&began);out.captureStartID=ticks(began);if(!ensureSID()){out.complete=false;return out;}struct Windows{std::set<DWORD>pids;bool bounded{true};std::exception_ptr error;unsigned count{};}windows;
        const auto callback=[](HWND hwnd,LPARAM value)->BOOL{auto&state=*reinterpret_cast<Windows*>(value);try{if(++state.count>8192){state.bounded=false;return FALSE;}if(GetWindow(hwnd,GW_OWNER)||GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)return TRUE;DWORD pid{};GetWindowThreadProcessId(hwnd,&pid);if(pid)state.pids.insert(pid);return TRUE;}catch(...){state.error=std::current_exception();return FALSE;}};
        if(!EnumWindows(callback,reinterpret_cast<LPARAM>(&windows))||!windows.bounded||windows.error){out.complete=false;return out;}Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};if(snapshot.value==INVALID_HANDLE_VALUE){out.complete=false;return out;}
        PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);if(!Process32FirstW(snapshot.value,&entry)){out.complete=GetLastError()==ERROR_NO_MORE_FILES;return out;}unsigned count{};do{if(++count>8192){out.complete=false;return out;}if(entry.th32ProcessID&&entry.th32ProcessID!=entry.th32ParentProcessID){bool other{};if(auto value=read(entry.th32ProcessID,entry.th32ParentProcessID,windows.pids.contains(entry.th32ProcessID),false,&other))out.processes.push_back(std::move(value->identity));else if(!other)out.unreadablePIDs.push_back(entry.th32ProcessID);}}while(Process32NextW(snapshot.value,&entry));out.complete=GetLastError()==ERROR_NO_MORE_FILES;return out;
    }
};
}
ActivityCatalogSampler::Readers windowsActivityCatalogReaders(){auto state=std::make_shared<NativeCatalog>();return {[state]{return state->inventory();},[state](auto pid){return state->read(pid,0,false,true);}};}
}
#else
namespace endfield::native {ActivityCatalogSampler::Readers windowsActivityCatalogReaders(){return {[]{return ActivityCatalogInventory{{},false,{}};},[](auto)->std::optional<ActivityCatalogReading>{return {};}};}}
#endif
