#include "native/activity_gpu.hpp"
#include <algorithm>
#include <limits>
#include <type_traits>
#include <vector>

namespace endfield::native {
namespace {
// ASCII-only reading of narrow or UTF-16 names: no locale, no copy.
template<class Char>std::uint32_t code(Char c){return static_cast<std::uint32_t>(static_cast<std::make_unsigned_t<Char>>(c));}
int digit(std::uint32_t c,int base){if(c>='0'&&c<='9')return int(c-'0');if(base==16){if(c>='a'&&c<='f')return int(c-'a')+10;if(c>='A'&&c<='F')return int(c-'A')+10;}return -1;}
template<class Char>bool prefixed(std::basic_string_view<Char>&text,std::string_view prefix){
    if(text.size()<prefix.size())return false;for(std::size_t k=0;k<prefix.size();++k)if(code(text[k])!=static_cast<unsigned char>(prefix[k]))return false;text.remove_prefix(prefix.size());return true;
}
// At most `maximum` digits (a longer run is out of range; at most 11 digits
// are accumulated, which a 64-bit value holds).
template<class Char>bool digits(std::basic_string_view<Char>&text,std::string_view prefix,std::uint64_t&value,int base,std::size_t maximum){
    if(!prefixed(text,prefix))return false;std::size_t length{};std::uint64_t parsed{};
    while(length<text.size()&&length<=maximum){const int d=digit(code(text[length]),base);if(d<0)break;parsed=parsed*unsigned(base)+unsigned(d);++length;}
    if(!length||length>maximum)return false;value=parsed;text.remove_prefix(length);return true;
}
template<class Char>std::optional<ActivityGPUInstance>instance(std::basic_string_view<Char>text){
    if(text.size()>256)return {};std::uint64_t pid{},high{},low{},physical{},engine{};
    if(!digits(text,"pid_",pid,10,10)||!digits(text,"_luid_0x",high,16,8)||!digits(text,"_0x",low,16,8)||!digits(text,"_phys_",physical,10,5)||!digits(text,"_eng_",engine,10,5)||!prefixed(text,"_engtype_"))return {};
    // The rest is the engine type label; its key is the label's 32-bit
    // FNV-1a (modules::activityGPUEngineType), hashed here in place.
    std::uint32_t type=2166136261u;
    for(const Char c:text){const auto value=code(c);if(value=='#'||value=='\\'||value=='/'||value=='('||value==')')return {};
        if constexpr(sizeof(Char)>1){if(value>0x7F)return {};}type^=value;type*=16777619u;}
    if(!pid||pid>std::numeric_limits<std::uint32_t>::max())return {};
    return ActivityGPUInstance{std::uint32_t(pid),{(high<<32)|low,std::uint32_t(physical),type,std::uint32_t(engine)}};
}
// One parsed reading and its position in the collection.
struct Reading {modules::ActivityGPUUsage usage;std::uint32_t order{};};
bool before(const modules::ActivityGPUUsage&a,const modules::ActivityGPUUsage&b){return a.engine<b.engine||(a.engine==b.engine&&a.pid<b.pid);}
// Sorted by (engine, pid); of a repeated (engine, pid) its first reading.
// std::sort over the reading's position: no temporary buffer.
void order(std::vector<Reading>&readings,std::vector<modules::ActivityGPUUsage>&out){
    std::sort(readings.begin(),readings.end(),[](const Reading&a,const Reading&b){return before(a.usage,b.usage)||(!before(b.usage,a.usage)&&a.order<b.order);});
    out.clear();for(const auto&r:readings)if(out.empty()||before(out.back(),r.usage))out.push_back(r.usage);
}
}
std::optional<ActivityGPUInstance>activityGPUInstance(std::string_view text){return instance(text);}
std::optional<ActivityGPUInstance>activityGPUInstance(std::wstring_view text){return instance(text);}
std::optional<modules::ActivityGPUSample>activityGPUSample(std::span<const ActivityGPUCounter>counters,std::uint64_t time,std::span<const std::uint32_t>pids){
    if(counters.size()>4*modules::ActivityGPUSample::maximumUsage||!std::is_sorted(pids.begin(),pids.end()))return {};
    std::vector<Reading>readings;readings.reserve(std::min(counters.size(),modules::ActivityGPUSample::maximumUsage));
    for(std::size_t n=0;n<counters.size();++n){const auto&counter=counters[n];if(!counter.valid)continue;const auto parsed=activityGPUInstance(counter.instance);if(!parsed)continue;
        if(!pids.empty()&&!std::binary_search(pids.begin(),pids.end(),parsed->pid))continue;
        if(readings.size()==modules::ActivityGPUSample::maximumUsage)return {};readings.push_back({{parsed->pid,parsed->engine,counter.runningTime},std::uint32_t(n)});}
    modules::ActivityGPUSample out;out.time=time;out.usage.reserve(readings.size());order(readings,out.usage);return out;
}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <winperf.h>
#include <chrono>
#include <cstddef>
#include <mutex>
namespace endfield::native {
namespace {
constexpr ULONGLONG reuseMilliseconds=250,retryMilliseconds=60'000;
// Grows a kept buffer with headroom (engine instances come and go with
// processes), never shrinks it.
template<class T>void room(std::vector<T>&value,std::size_t size){if(value.capacity()<size)value.reserve(size+size/4+16);}
// One process-wide collector: the system reading of every Activity sample
// and the Apps reading (while shown) run on the shared UtilityExecutor and
// share its latest collection.
struct Collector {
    std::mutex mutex;PDH_HQUERY query{};PDH_HCOUNTER counter{};
    modules::ActivityGPUSample last;bool fresh{},openFailed{};ULONGLONG lastTick{},failedTick{};
    // Kept between samples: the raw counter array, the parse buffer and a
    // recycled system reading (recycleWindowsActivityGPU).
    std::vector<std::max_align_t>storage;std::vector<Reading>readings;modules::ActivityGPUSample spare;WindowsActivityGPUStats stats;
    ~Collector(){close();}
    void close()noexcept{if(query){PdhCloseQuery(query);++stats.closes;}query=nullptr;counter=nullptr;fresh=false;stats.open=false;stats.instances=0;}
    void release()noexcept{close();openFailed=false;std::vector<std::max_align_t>().swap(storage);std::vector<Reading>().swap(readings);
        std::vector<modules::ActivityGPUUsage>().swap(last.usage);std::vector<modules::ActivityGPUUsage>().swap(spare.usage);}
    bool open(){if(query)return true;const auto now=GetTickCount64();if(openFailed&&now-failedTick<retryMilliseconds)return false;
        const auto failed=[&](PDH_HQUERY candidate){if(candidate)PdhCloseQuery(candidate);openFailed=true;failedTick=now;return false;};
        PDH_HQUERY candidate{};if(PdhOpenQueryW(nullptr,0,&candidate)!=ERROR_SUCCESS)return failed(nullptr);PDH_HCOUNTER c{};
        if(PdhAddEnglishCounterW(candidate,L"\\GPU Engine(*)\\Running Time",0,&c)!=ERROR_SUCCESS)return failed(candidate);
        DWORD infoBytes{};if(PdhGetCounterInfoW(c,FALSE,&infoBytes,nullptr)!=PDH_MORE_DATA||infoBytes<sizeof(PDH_COUNTER_INFO_W)||infoBytes>64*1024)return failed(candidate);
        std::vector<std::max_align_t>info((infoBytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));const DWORD capacity=infoBytes;auto*value=reinterpret_cast<PDH_COUNTER_INFO_W*>(info.data());
        // A cumulative 100 ns count (the documented raw "Running Time").
        if(PdhGetCounterInfoW(c,FALSE,&infoBytes,value)!=ERROR_SUCCESS||infoBytes>capacity||(value->dwType!=PERF_COUNTER_LARGE_RAWCOUNT&&value->dwType!=PERF_COUNTER_BULK_COUNT))return failed(candidate);
        query=candidate;counter=c;openFailed=false;++stats.opens;stats.open=true;return true;}
    PDH_RAW_COUNTER_ITEM_W*items(){return reinterpret_cast<PDH_RAW_COUNTER_ITEM_W*>(storage.data());}
    // At most 80 MiB (a probed 64 MiB array and its headroom).
    DWORD capacity()const{return DWORD(storage.size()*sizeof(std::max_align_t));}
    // One collection into `last`; true when it holds a valid reading.
    bool collect(){
        if(!open())return false;if(PdhCollectQueryData(query)!=ERROR_SUCCESS){++stats.failures;close();return false;}
        // The raw array of every instance, into the kept buffer. A larger
        // array: probe its size afresh (the size a short buffer reports is
        // not reliable), grow with headroom and read again; two bounded tries.
        DWORD bytes=capacity(),count{};auto status=bytes?PdhGetRawCounterArrayW(counter,&bytes,&count,items()):static_cast<PDH_STATUS>(PDH_MORE_DATA);
        for(unsigned attempt=0;status==PDH_MORE_DATA&&attempt<2;++attempt){bytes=0;count=0;const auto probe=PdhGetRawCounterArrayW(counter,&bytes,&count,nullptr);
            if(probe==ERROR_SUCCESS&&!count){status=ERROR_SUCCESS;break;}
            if(probe!=PDH_MORE_DATA||bytes<sizeof(PDH_RAW_COUNTER_ITEM_W)||bytes>64*1024*1024||count>4*modules::ActivityGPUSample::maximumUsage)return false;
            const std::size_t wanted=std::size_t(bytes)+bytes/4;storage.resize((wanted+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));
            bytes=capacity();count=0;status=PdhGetRawCounterArrayW(counter,&bytes,&count,items());}
        if(status!=ERROR_SUCCESS||bytes>capacity()||std::size_t(count)>bytes/sizeof(PDH_RAW_COUNTER_ITEM_W)||count>4*modules::ActivityGPUSample::maximumUsage)return false;
        // Names are parsed where PDH wrote them (no converted copy).
        const auto begin=reinterpret_cast<std::uintptr_t>(storage.data()),end=begin+bytes;const auto*array=items();room(readings,count);readings.clear();std::uint64_t time{};
        for(DWORD n=0;n<count;++n){const auto address=reinterpret_cast<std::uintptr_t>(array[n].szName);if(address<begin||address>=end||address%alignof(wchar_t))return false;
            const auto maximum=std::min<std::size_t>((end-address)/sizeof(wchar_t),257);std::size_t length{};while(length<maximum&&array[n].szName[length])++length;if(!length||length==maximum)continue;
            const auto&raw=array[n].RawValue;if((raw.CStatus!=PDH_CSTATUS_VALID_DATA&&raw.CStatus!=PDH_CSTATUS_NEW_DATA)||raw.FirstValue<0)continue;
            if(!time)time=(std::uint64_t(raw.TimeStamp.dwHighDateTime)<<32)|raw.TimeStamp.dwLowDateTime;
            const auto parsed=activityGPUInstance(std::wstring_view(array[n].szName,length));if(!parsed)continue;
            if(readings.size()==modules::ActivityGPUSample::maximumUsage)return false;readings.push_back({{parsed->pid,parsed->engine,std::uint64_t(raw.FirstValue)},std::uint32_t(n)});}
        room(last.usage,readings.size());order(readings,last.usage);last.time=time?time:now();stats.instances=last.usage.size();return true;}
    static std::uint64_t now(){FILETIME value{};GetSystemTimePreciseAsFileTime(&value);return (std::uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
};
Collector&collector(){static Collector value;return value;}
}
WindowsActivityGPUStats windowsActivityGPUStats()noexcept{auto&c=collector();std::lock_guard lock(c.mutex);auto out=c.stats;
    out.retainedBytes=c.storage.capacity()*sizeof(std::max_align_t)+c.readings.capacity()*sizeof(Reading)+(c.last.usage.capacity()+c.spare.usage.capacity())*sizeof(modules::ActivityGPUUsage);return out;}
std::optional<modules::ActivityGPUSample>readWindowsActivityGPU(std::span<const std::uint32_t>pids){
    auto&c=collector();std::lock_guard lock(c.mutex);
    // The reuse window runs from the end of the last collection, so the
    // system and the Apps readers of one sampling job share a collection even
    // when it is slow (opening the counter set on a busy machine can take
    // longer than 250 ms).
    if(!c.fresh||GetTickCount64()-c.lastTick>=reuseMilliseconds){const auto began=std::chrono::steady_clock::now();c.fresh=c.collect();const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
        c.lastTick=GetTickCount64();++c.stats.collections;c.stats.lastSeconds=seconds;c.stats.maximumSeconds=std::max(c.stats.maximumSeconds,seconds);if(!c.fresh){++c.stats.failures;c.stats.instances=0;}}
    else ++c.stats.reuses;
    if(!c.fresh)return {};
    if(pids.empty()){
        // The system reading: the latest collection, copied into the
        // recycled buffer (no allocation once it is large enough).
        std::optional<modules::ActivityGPUSample>out;out.emplace();std::swap(out->usage,c.spare.usage);
        room(out->usage,c.last.usage.size());out->time=c.last.time;out->usage.assign(c.last.usage.begin(),c.last.usage.end());return out;}
    if(!std::is_sorted(pids.begin(),pids.end()))return {};
    modules::ActivityGPUSample out;out.time=c.last.time;for(const auto&value:c.last.usage)if(std::binary_search(pids.begin(),pids.end(),value.pid))out.usage.push_back(value);return out;
}
void recycleWindowsActivityGPU(modules::ActivityGPUSample&&value)noexcept{
    auto&c=collector();std::lock_guard lock(c.mutex);if(value.usage.capacity()<=c.spare.usage.capacity())return;
    // The caller's sample takes the smaller spare (released with it).
    std::swap(c.spare.usage,value.usage);++c.stats.recycled;
}
void releaseWindowsActivityGPU()noexcept{auto&c=collector();std::lock_guard lock(c.mutex);c.release();}
}
#else
namespace endfield::native {
WindowsActivityGPUStats windowsActivityGPUStats()noexcept{return {};}
std::optional<modules::ActivityGPUSample>readWindowsActivityGPU(std::span<const std::uint32_t>){return {};}
void recycleWindowsActivityGPU(modules::ActivityGPUSample&&)noexcept{}
void releaseWindowsActivityGPU()noexcept{}
}
#endif
