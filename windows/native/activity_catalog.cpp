#include "native/activity_catalog.hpp"
#include "native/activity_provider.hpp"
#include "native/activity_gpu.hpp"
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
std::string activityDisplayName(std::string_view candidate,std::string_view fallback){
    if(!ehud::data::Json::validUtf8(candidate))return std::string(fallback);
    std::string out;out.reserve(std::min<std::size_t>(candidate.size(),160));bool space{};
    for(std::size_t n=0;n<candidate.size();){
        const auto lead=static_cast<unsigned char>(candidate[n]);const std::size_t width=lead<0x80?1:lead<0xe0?2:lead<0xf0?3:4;
        std::uint32_t c=width==1?lead:lead&((1u<<(7-width))-1);for(std::size_t k=1;k<width;++k)c=(c<<6)|(static_cast<unsigned char>(candidate[n+k])&63u);
        const auto unit=candidate.substr(n,width);n+=width;
        // Format characters (zero-width, bidi embedding/isolates, BOM) never reach a card.
        if((c>=0x200b&&c<=0x200f)||(c>=0x202a&&c<=0x202e)||(c>=0x2060&&c<=0x2064)||(c>=0x2066&&c<=0x2069)||c==0xfeff)continue;
        const bool blank=c<0x20||c==0x7f||(c>=0x80&&c<0xa0)||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000||c==' ';
        if(blank){space=!out.empty();continue;}
        const std::size_t need=unit.size()+(space?1:0);if(out.size()+need>160)break;
        if(space)out.push_back(' ');space=false;out.append(unit);
    }
    if(out.empty())return std::string(fallback);return out;
}
std::optional<std::size_t>activityTranslation(std::span<const std::pair<std::uint16_t,std::uint16_t>>values,std::uint16_t user){
    if(values.empty())return {};
    for(std::size_t n=0;n<values.size();++n)if(values[n].first==user)return n;
    for(std::size_t n=0;n<values.size();++n)if((values[n].first&0x3ff)==(user&0x3ff)&&values[n].first)return n;
    for(std::size_t n=0;n<values.size();++n)if(!values[n].first)return n;
    return std::size_t{0};
}
ActivityCatalogSampler::ActivityCatalogSampler(Readers r):readers_(std::move(r)){need(bool(readers_.inventory)&&bool(readers_.process),"Activity catalog requires injected readers");}
void ActivityCatalogSampler::reset()noexcept{std::vector<modules::ActivityAppIdentity>().swap(catalog_.apps);catalog_.identities.clear();catalog_.complete=true;inventoryTime_.reset();lastTime_.reset();}
void ActivityCatalogSampler::invalidate()noexcept{generation_.fetch_add(1,std::memory_order_relaxed);std::unique_lock lock(mutex_,std::try_to_lock);if(lock.owns_lock()){reset();workerGeneration_=generation_.load(std::memory_order_relaxed);}}
std::size_t ActivityCatalogSampler::retainedIdentities()const noexcept{std::lock_guard lock(mutex_);return catalog_.identities.size()+catalog_.apps.size();}
modules::ActivityAppsRaw ActivityCatalogSampler::sample(double stamp,double now){std::lock_guard lock(mutex_);need(std::isfinite(stamp)&&std::isfinite(now)&&now>=0,"Invalid Activity catalog clock");const auto generation=generation_.load(std::memory_order_relaxed);if(workerGeneration_!=generation){reset();workerGeneration_=generation;}need(std::isfinite(stamp)&&std::isfinite(now)&&now>=0&&(!lastTime_||now>=*lastTime_),"Invalid Activity catalog clock");lastTime_=now;
    if(!inventoryTime_||now-*inventoryTime_>=5){inventoryTime_=now;try{auto inventory=readers_.inventory();(void)buildActivityCatalog(inventory);for(auto pid:inventory.unreadablePIDs){const auto old=catalog_.identities.find(pid);if(old!=catalog_.identities.end())inventory.processes.push_back(old->second);}inventory.unreadablePIDs.clear();auto next=buildActivityCatalog(inventory);if(next.complete)catalog_=std::move(next);else catalog_.complete=false;}catch(const std::exception&){catalog_.complete=false;}}
    modules::ActivityAppsRaw out;out.timestamp=stamp;out.uptime=now;out.secondsPerCPUTick=1e-7;out.apps=catalog_.apps;out.inventoryComplete=catalog_.complete;
    for(const auto&app:out.apps){auto&members=out.members[app.id];for(auto pid:app.processIDs){const auto&old=catalog_.identities.at(pid);auto current=readers_.process(pid);if(!current){members.push_back(pid);continue;}validate(current->identity);need(current->identity.pid==pid,"Activity reader returned another PID");
            // Reuse/path/app changes stay unknown until the next bounded
            // inventory; never charge a new process to a stale card.
            if(current->identity.startID!=old.startID||current->identity.imageKey!=old.imageKey||current->identity.applicationID!=old.applicationID){members.push_back(pid);continue;}
            members.push_back(pid);if(current->usage){const auto&v=*current->usage;need(v.pid==pid&&v.startID==old.startID&&std::isfinite(v.startUptime)&&v.startUptime>=0,"Activity identity/usage mismatch");out.processes.emplace(pid,v);}
        }}
    if(readers_.threads){const auto counts=readers_.threads();for(auto&[pid,process]:out.processes)if(const auto found=counts.find(pid);found!=counts.end())process.threads=found->second;}
    if(readers_.gpu){std::vector<std::uint32_t>pids;for(const auto&[id,members]:out.members)pids.insert(pids.end(),members.begin(),members.end());std::sort(pids.begin(),pids.end());pids.erase(std::unique(pids.begin(),pids.end()),pids.end());if(!pids.empty())out.gpu=readers_.gpu(pids);}
    if(!out.inventoryComplete)out.statusNotes.push_back("App helper inventory unavailable");out.statusNotes.push_back("Per-app disk and network accounting unavailable");
    // The Apps demand changed while this sample ran (invalidate() could not
    // take the lock): its result is discarded and the catalog released now
    // rather than kept until the next Apps showing.
    if(const auto latest=generation_.load(std::memory_order_relaxed);latest!=generation){reset();workerGeneration_=latest;}
    return out;
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
#include <shlobj_core.h>
#include <shobjidl_core.h>
#include <knownfolders.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <cwchar>
namespace endfield::native {
namespace {
struct Handle{HANDLE value{};~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
std::uint64_t ticks(FILETIME v){return (std::uint64_t(v.dwHighDateTime)<<32)|v.dwLowDateTime;}
std::string utf8(std::wstring_view w){if(w.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);if(n<=0||n>4096)return {};std::string out(std::size_t(n),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),out.data(),n,nullptr,nullptr)!=n)return {};return out;}
std::optional<std::vector<std::uint8_t>>userSID(HANDLE process){Handle token;if(!OpenProcessToken(process,TOKEN_QUERY,&token.value))return {};DWORD n{};GetTokenInformation(token.value,TokenUser,nullptr,0,&n);if(!n||n>65536)return {};std::vector<std::uint8_t>bytes(n);if(!GetTokenInformation(token.value,TokenUser,bytes.data(),n,&n))return {};const auto sid=reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid;if(!IsValidSid(sid))return {};const auto size=GetLengthSid(sid);std::vector<std::uint8_t>out(size);if(!CopySid(size,out.data(),sid))return {};return out;}
struct NativeCatalog {
    std::optional<std::vector<std::uint8_t>>sid;
    // Worker-owned display-name cache, pruned to the latest inventory.
    struct Name{std::string value;std::uint64_t seen{};};std::map<std::string,Name,std::less<>>names;std::uint64_t pass{};
    std::string displayName(const std::wstring&original,const ActivityCatalogProcess&identity){
        const auto key=identity.applicationID.empty()?"exe:"+identity.imageKey:"app:"+identity.applicationID;
        if(const auto found=names.find(key);found!=names.end()){found->second.seen=pass;return found->second.value;}
        std::string candidate;
        if(!identity.applicationID.empty()){const auto id=utf16(identity.applicationID);if(!id.empty())candidate=windowsActivityPackagedName(id);}
        if(candidate.empty())candidate=windowsActivityFileDescription(original);
        auto value=activityDisplayName(candidate,identity.name);
        if(names.size()<512)names.emplace(key,Name{value,pass});
        return value;
    }
    static std::wstring utf16(std::string_view value){if(value.empty()||value.size()>4096)return {};const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);if(n<=0)return {};std::wstring out(std::size_t(n),L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n)!=n)return {};return out;}
    bool ensureSID(){if(!sid)sid=userSID(GetCurrentProcess());return bool(sid);}
    std::optional<ActivityCatalogReading>read(std::uint32_t pid,std::uint32_t parent,bool window,bool usage,bool*otherUser=nullptr){
        if(!ensureSID())return {};Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};if(!process.value)return {};auto user=userSID(process.value);if(!user)return {};if(!EqualSid(user->data(),sid->data())){if(otherUser)*otherUser=true;return {};}
        std::array<wchar_t,32768>path{};DWORD n=DWORD(path.size());FILETIME created{},exit{},kernel{},userTime{};if(!QueryFullProcessImageNameW(process.value,0,path.data(),&n)||!n||!GetProcessTimes(process.value,&created,&exit,&kernel,&userTime)||!ticks(created))return {};
        std::wstring original(path.data(),n),normalized(n,L'\0');if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,original.data(),int(n),normalized.data(),int(n),nullptr,nullptr,0))return {};
        ActivityCatalogReading out;auto&identity=out.identity;identity.pid=pid;identity.parent=parent;identity.startID=ticks(created);identity.imageKey=utf8(normalized);identity.iconKey=utf8(original);identity.appWindow=window;if(identity.imageKey.empty())return {};
        const auto slash=original.find_last_of(L"\\/");auto label=original.substr(slash==original.npos?0:slash+1);if(label.size()>4&&label.substr(label.size()-4)==L".exe")label.resize(label.size()-4);identity.name=utf8(label);
        UINT32 size{};const auto query=GetApplicationUserModelId(process.value,&size,nullptr);
        if(query!=APPMODEL_ERROR_NO_APPLICATION){if(query!=ERROR_INSUFFICIENT_BUFFER||size<=1||size>4096)return {};std::vector<wchar_t>id(size);if(GetApplicationUserModelId(process.value,&size,id.data())!=ERROR_SUCCESS||size<=1||size>id.size()||id[size-1]!=L'\0')return {};identity.applicationID=utf8(std::wstring_view(id.data(),size-1));if(identity.applicationID.empty())return {};
            // A packaged app's icon is its Start (AppsFolder) icon, not its executable's.
            identity.iconKey="appsfolder:"+identity.applicationID;}
        // App-card candidates only (inventory pass, never the 1 Hz usage read).
        if(!usage&&(window||!identity.applicationID.empty()))identity.name=displayName(original,identity);
        if(usage){FILETIME wall{};GetSystemTimePreciseAsFileTime(&wall);const auto memory=windowsActivityProcessMemory(process.value);if(memory&&ticks(wall)>=identity.startID&&ticks(userTime)<=UINT64_MAX-ticks(kernel)){modules::ActivityProcess p;p.pid=pid;p.startID=identity.startID;p.cpuTicks=ticks(userTime)+ticks(kernel);p.memoryBytes=*memory;const double uptime=double(GetTickCount64())*.001;p.startUptime=std::max(0.,uptime-double(ticks(wall)-p.startID)*1e-7);out.usage=p;}}
        return out;
    }
    ActivityCatalogInventory inventory(){ActivityCatalogInventory out;++pass;
        // A complete inventory retires names of identities no longer running.
        struct Prune{NativeCatalog&c;const ActivityCatalogInventory&out;~Prune(){if(out.complete)std::erase_if(c.names,[&](const auto&e){return e.second.seen!=c.pass;});}}prune{*this,out};FILETIME began{};GetSystemTimePreciseAsFileTime(&began);out.captureStartID=ticks(began);if(!ensureSID()){out.complete=false;return out;}struct Windows{std::set<DWORD>pids;bool bounded{true};std::exception_ptr error;unsigned count{};}windows;
        const auto callback=[](HWND hwnd,LPARAM value)->BOOL{auto&state=*reinterpret_cast<Windows*>(value);try{if(++state.count>8192){state.bounded=false;return FALSE;}if(GetWindow(hwnd,GW_OWNER)||GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)return TRUE;DWORD pid{};GetWindowThreadProcessId(hwnd,&pid);if(pid)state.pids.insert(pid);return TRUE;}catch(...){state.error=std::current_exception();return FALSE;}};
        if(!EnumWindows(callback,reinterpret_cast<LPARAM>(&windows))||!windows.bounded||windows.error){out.complete=false;return out;}Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};if(snapshot.value==INVALID_HANDLE_VALUE){out.complete=false;return out;}
        PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);if(!Process32FirstW(snapshot.value,&entry)){out.complete=GetLastError()==ERROR_NO_MORE_FILES;return out;}unsigned count{};do{if(++count>8192){out.complete=false;return out;}if(entry.th32ProcessID&&entry.th32ProcessID!=entry.th32ParentProcessID){bool other{};if(auto value=read(entry.th32ProcessID,entry.th32ParentProcessID,windows.pids.contains(entry.th32ProcessID),false,&other))out.processes.push_back(std::move(value->identity));else if(!other)out.unreadablePIDs.push_back(entry.th32ProcessID);}}while(Process32NextW(snapshot.value,&entry));out.complete=GetLastError()==ERROR_NO_MORE_FILES;return out;
    }
};
}
std::string windowsActivityFileDescription(const std::wstring&path){
    if(path.empty()||path.size()>32767||path.find(L'\0')!=path.npos)return {};
    DWORD handle{};const DWORD size=GetFileVersionInfoSizeExW(FILE_VER_GET_LOCALISED,path.c_str(),&handle);if(!size||size>1024*1024)return {};
    std::vector<std::uint8_t>data(size);if(!GetFileVersionInfoExW(FILE_VER_GET_LOCALISED,path.c_str(),0,size,data.data()))return {};
    void*value{};UINT length{};std::vector<std::pair<std::uint16_t,std::uint16_t>>translations;
    if(VerQueryValueW(data.data(),L"\\VarFileInfo\\Translation",&value,&length)&&value)for(UINT at=0;at+4<=length&&translations.size()<64;at+=4){WORD pair[2]{};std::memcpy(pair,static_cast<const std::uint8_t*>(value)+at,4);translations.emplace_back(pair[0],pair[1]);}
    std::vector<std::pair<std::uint16_t,std::uint16_t>>order;if(const auto chosen=activityTranslation(translations,GetUserDefaultUILanguage()))order.push_back(translations[*chosen]);
    for(const auto&t:translations)if(order.size()<66&&std::find(order.begin(),order.end(),t)==order.end())order.push_back(t);
    for(const std::pair<std::uint16_t,std::uint16_t>t:{std::pair<std::uint16_t,std::uint16_t>{0x0409,0x04b0},{0x0409,0x04e4},{0x0000,0x04b0}})if(std::find(order.begin(),order.end(),t)==order.end())order.push_back(t);
    for(const auto&[language,codepage]:order){wchar_t key[64]{};std::swprintf(key,64,L"\\StringFileInfo\\%04x%04x\\FileDescription",unsigned(language),unsigned(codepage));
        if(!VerQueryValueW(data.data(),key,&value,&length)||!value||!length)continue;std::wstring_view text(static_cast<const wchar_t*>(value),std::min<std::size_t>(length,1024));if(const auto end=text.find(L'\0');end!=text.npos)text=text.substr(0,end);
        auto out=utf8(text);if(!out.empty())return out;}
    return {};
}
std::string windowsActivityPackagedName(const std::wstring&aumid){
    if(aumid.empty()||aumid.size()>4096||aumid.find(L'\0')!=aumid.npos)return {};
    const Apartment apartment;if(FAILED(apartment.hr)&&apartment.hr!=RPC_E_CHANGED_MODE)return {};
    Microsoft::WRL::ComPtr<IShellItem>item;if(FAILED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder,KF_FLAG_DONT_VERIFY,aumid.c_str(),IID_PPV_ARGS(&item)))||!item)return {};
    PWSTR name{};if(FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY,&name))||!name)return {};
    const std::wstring value(name);CoTaskMemFree(name);
    // An unknown AUMID echoes its own parsing name; that is not a display name.
    if(value==aumid)return {};
    return utf8(value);
}
std::map<std::uint32_t,std::uint32_t>readWindowsActivityThreads(){std::map<std::uint32_t,std::uint32_t>out;Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};if(snapshot.value==INVALID_HANDLE_VALUE)return out;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);if(!Process32FirstW(snapshot.value,&entry))return out;unsigned count{};do{if(++count>8192)break;if(entry.th32ProcessID)out.emplace(entry.th32ProcessID,entry.cntThreads);}while(Process32NextW(snapshot.value,&entry));return out;}
ActivityCatalogSampler::Readers windowsActivityCatalogReaders(){auto state=std::make_shared<NativeCatalog>();return {[state]{return state->inventory();},[state](auto pid){return state->read(pid,0,false,true);},[](std::span<const std::uint32_t>pids){return readWindowsActivityGPU(pids);},[]{return readWindowsActivityThreads();}};}
}
#else
namespace endfield::native {ActivityCatalogSampler::Readers windowsActivityCatalogReaders(){return {[]{return ActivityCatalogInventory{{},false,{}};},[](auto)->std::optional<ActivityCatalogReading>{return {};},{},{}};}}
#endif
