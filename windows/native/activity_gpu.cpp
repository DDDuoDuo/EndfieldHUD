#include "native/activity_gpu.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <limits>

namespace endfield::native {
namespace {
bool digits(std::string_view&text,std::string_view prefix,std::uint64_t&value,int base,std::size_t maximum){
    if(!text.starts_with(prefix))return false;text.remove_prefix(prefix.size());std::size_t length{};
    while(length<text.size()&&length<=maximum&&std::isxdigit(static_cast<unsigned char>(text[length]))&&(base==16||(text[length]>='0'&&text[length]<='9')))++length;
    if(!length||length>maximum)return false;const auto parsed=std::from_chars(text.data(),text.data()+length,value,base);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+length)return false;text.remove_prefix(length);return true;
}
std::atomic<bool>demand{};
}
std::optional<ActivityGPUInstance>activityGPUInstance(std::string_view text){
    if(text.size()>256)return {};std::uint64_t pid{},high{},low{},physical{},engine{};
    if(!digits(text,"pid_",pid,10,10)||!digits(text,"_luid_0x",high,16,8)||!digits(text,"_0x",low,16,8)||!digits(text,"_phys_",physical,10,5)||!digits(text,"_eng_",engine,10,5)||!text.starts_with("_engtype_"))return {};
    text.remove_prefix(9);if(text.find_first_of("#\\/()")!=std::string_view::npos)return {};
    if(!pid||pid>std::numeric_limits<std::uint32_t>::max())return {};
    return ActivityGPUInstance{std::uint32_t(pid),{(high<<32)|low,std::uint32_t(physical),std::uint32_t(engine)}};
}
std::optional<modules::ActivityGPUSample>activityGPUSample(std::span<const ActivityGPUCounter>counters,std::uint64_t time,std::span<const std::uint32_t>pids){
    if(counters.size()>4*modules::ActivityGPUSample::maximumUsage||!std::is_sorted(pids.begin(),pids.end()))return {};
    modules::ActivityGPUSample out;out.time=time;out.usage.reserve(std::min(counters.size(),modules::ActivityGPUSample::maximumUsage));
    for(const auto&counter:counters){if(!counter.valid)continue;const auto instance=activityGPUInstance(counter.instance);if(!instance)continue;
        if(!pids.empty()&&!std::binary_search(pids.begin(),pids.end(),instance->pid))continue;
        if(out.usage.size()==modules::ActivityGPUSample::maximumUsage)return {};out.usage.push_back({instance->pid,instance->engine,counter.runningTime});}
    // Stable: a repeated (engine, pid) keeps its first reading.
    std::stable_sort(out.usage.begin(),out.usage.end(),[](const auto&a,const auto&b){return a.engine<b.engine||(a.engine==b.engine&&a.pid<b.pid);});
    out.usage.erase(std::unique(out.usage.begin(),out.usage.end(),[](const auto&a,const auto&b){return a.engine==b.engine&&a.pid==b.pid;}),out.usage.end());
    return out;
}
bool windowsActivityGPUDemand()noexcept{return demand.load(std::memory_order_acquire);}
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
#include <vector>
namespace endfield::native {
namespace {
// One process-wide collector: both Activity sampling readers (system and
// Apps) run on the shared UtilityExecutor and share its latest collection.
struct Collector {
    std::mutex mutex;PDH_HQUERY query{};PDH_HCOUNTER counter{};std::optional<modules::ActivityGPUSample>last;ULONGLONG lastTick{};
    std::vector<std::max_align_t>storage;std::vector<ActivityGPUCounter>counters;WindowsActivityGPUStats stats;
    ~Collector(){close();}
    void close()noexcept{if(query){PdhCloseQuery(query);++stats.closes;}query=nullptr;counter=nullptr;last.reset();stats.open=false;stats.instances=0;
        // Hidden Activity keeps nothing: the buffers are released, not kept warm.
        std::vector<std::max_align_t>().swap(storage);std::vector<ActivityGPUCounter>().swap(counters);}
    bool open(){if(query)return true;PDH_HQUERY candidate{};if(PdhOpenQueryW(nullptr,0,&candidate)!=ERROR_SUCCESS)return false;PDH_HCOUNTER c{};
        if(PdhAddEnglishCounterW(candidate,L"\\GPU Engine(*)\\Running Time",0,&c)!=ERROR_SUCCESS){PdhCloseQuery(candidate);return false;}
        DWORD infoBytes{};std::vector<std::max_align_t>info;
        if(PdhGetCounterInfoW(c,FALSE,&infoBytes,nullptr)!=PDH_MORE_DATA||infoBytes<sizeof(PDH_COUNTER_INFO_W)||infoBytes>64*1024){PdhCloseQuery(candidate);return false;}
        info.resize((infoBytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));const DWORD capacity=infoBytes;auto*value=reinterpret_cast<PDH_COUNTER_INFO_W*>(info.data());
        // A cumulative 100 ns count (the documented raw "Running Time").
        if(PdhGetCounterInfoW(c,FALSE,&infoBytes,value)!=ERROR_SUCCESS||infoBytes>capacity||(value->dwType!=PERF_COUNTER_LARGE_RAWCOUNT&&value->dwType!=PERF_COUNTER_BULK_COUNT)){PdhCloseQuery(candidate);return false;}
        query=candidate;counter=c;++stats.opens;stats.open=true;return true;}
    // The raw array of every instance after one collection.
    std::optional<modules::ActivityGPUSample>collect(){
        if(!open())return {};if(PdhCollectQueryData(query)!=ERROR_SUCCESS){++stats.failures;close();return {};}
        for(unsigned attempt=0;attempt<2;++attempt){DWORD bytes{},count{};const auto probe=PdhGetRawCounterArrayW(counter,&bytes,&count,nullptr);
            if(probe==ERROR_SUCCESS&&!count)return modules::ActivityGPUSample{now(),{}};
            if(probe!=PDH_MORE_DATA||bytes<sizeof(PDH_RAW_COUNTER_ITEM_W)||bytes>64*1024*1024||count>4*modules::ActivityGPUSample::maximumUsage)return {};
            storage.resize((bytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));const DWORD capacity=bytes;auto*array=reinterpret_cast<PDH_RAW_COUNTER_ITEM_W*>(storage.data());
            const auto hr=PdhGetRawCounterArrayW(counter,&bytes,&count,array);if(hr==PDH_MORE_DATA)continue;
            if(hr!=ERROR_SUCCESS||bytes>capacity||std::size_t(count)>bytes/sizeof(*array))return {};
            const auto begin=reinterpret_cast<std::uintptr_t>(storage.data()),end=begin+bytes;counters.resize(count);std::uint64_t time{};
            for(DWORD n=0;n<count;++n){auto&out=counters[n];out.valid=false;out.instance.clear();const auto address=reinterpret_cast<std::uintptr_t>(array[n].szName);if(address<begin||address>=end||address%alignof(wchar_t))return {};
                const auto maximum=std::min<std::size_t>((end-address)/sizeof(wchar_t),257);std::size_t length{};while(length<maximum&&array[n].szName[length])++length;if(!length||length==maximum)continue;
                // Instance names are ASCII; anything else is not a GPU engine.
                bool ascii=true;for(std::size_t k=0;k<length;++k)ascii=ascii&&array[n].szName[k]<0x80;if(!ascii)continue;out.instance.assign(length,'\0');for(std::size_t k=0;k<length;++k)out.instance[k]=char(array[n].szName[k]);
                const auto&raw=array[n].RawValue;out.valid=(raw.CStatus==PDH_CSTATUS_VALID_DATA||raw.CStatus==PDH_CSTATUS_NEW_DATA)&&raw.FirstValue>=0;out.runningTime=out.valid?std::uint64_t(raw.FirstValue):0;
                if(out.valid&&!time)time=(std::uint64_t(raw.TimeStamp.dwHighDateTime)<<32)|raw.TimeStamp.dwLowDateTime;}
            return activityGPUSample(counters,time?time:now());}
        return {};}
    static std::uint64_t now(){FILETIME value{};GetSystemTimePreciseAsFileTime(&value);return (std::uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
};
Collector&collector(){static Collector value;return value;}
}
void setWindowsActivityGPUDemand(bool visible)noexcept{demand.store(visible,std::memory_order_release);if(visible)return;
    // Release now unless the worker is collecting (it then closes on its next call).
    auto&c=collector();std::unique_lock lock(c.mutex,std::try_to_lock);if(lock.owns_lock()&&!demand.load(std::memory_order_acquire))c.close();}
WindowsActivityGPUStats windowsActivityGPUStats()noexcept{auto&c=collector();std::lock_guard lock(c.mutex);return c.stats;}
std::optional<modules::ActivityGPUSample>readWindowsActivityGPU(std::span<const std::uint32_t>pids){
    auto&c=collector();std::lock_guard lock(c.mutex);if(!demand.load(std::memory_order_acquire)){c.close();return {};}
    const auto tick=GetTickCount64();
    if(!c.last||tick-c.lastTick>=250){const auto began=std::chrono::steady_clock::now();c.last=c.collect();const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
        c.lastTick=tick;++c.stats.collections;c.stats.lastSeconds=seconds;c.stats.maximumSeconds=std::max(c.stats.maximumSeconds,seconds);c.stats.instances=c.last?c.last->usage.size():0;if(!c.last)++c.stats.failures;}
    else ++c.stats.reuses;
    if(!c.last||pids.empty())return c.last;if(!std::is_sorted(pids.begin(),pids.end()))return {};
    modules::ActivityGPUSample out;out.time=c.last->time;for(const auto&value:c.last->usage)if(std::binary_search(pids.begin(),pids.end(),value.pid))out.usage.push_back(value);return out;
}
}
#else
namespace endfield::native {
void setWindowsActivityGPUDemand(bool visible)noexcept{demand.store(visible,std::memory_order_release);}
WindowsActivityGPUStats windowsActivityGPUStats()noexcept{return {};}
std::optional<modules::ActivityGPUSample>readWindowsActivityGPU(std::span<const std::uint32_t>){return {};}
}
#endif
