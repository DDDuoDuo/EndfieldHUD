#include "native/activity_disk.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <stdexcept>
namespace endfield::native {
std::optional<modules::ActivityCounters>activityDiskCounters(std::span<const ActivityDiskCounter>reads,std::span<const ActivityDiskCounter>writes){
    if(reads.size()>1025||writes.size()>1025)return {};modules::ActivityCounters out;std::set<std::string,std::less<>>seenRead,seenWrite;
    const auto valid=[](const ActivityDiskCounter&v){return !v.instance.empty()&&v.instance.size()<=4096&&ehud::data::Json::validUtf8(v.instance)&&v.instance.find('\0')==v.instance.npos;};
    for(const auto&r:reads){if(!valid(r)||!seenRead.insert(r.instance).second)return {};if(r.instance=="_Total")continue;if(!r.valid)return {};out.emplace(r.instance,modules::ActivityBytes{r.bytes,0});}
    for(const auto&w:writes){if(!valid(w)||!seenWrite.insert(w.instance).second)return {};if(w.instance=="_Total")continue;if(!w.valid)return {};const auto found=out.find(w.instance);if(found==out.end())return {};found->second.sent=w.bytes;}
    seenRead.erase("_Total");seenWrite.erase("_Total");if(out.empty()||out.size()>1024||seenRead!=seenWrite)return {};return out;
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
#include <cstddef>
namespace endfield::native {
// PDH raw arrays: learn.microsoft.com/windows/win32/api/pdh/nf-pdh-pdhgetrawcounterarrayw
// PERF_COUNTER_BULK_COUNT FirstValue is the cumulative byte numerator.
struct WindowsActivityDisk::Impl {
    PDH_HQUERY query{};PDH_HCOUNTER reads{},writes{};
    ~Impl(){if(query)PdhCloseQuery(query);}
    void close()noexcept{if(query)PdhCloseQuery(query);query=nullptr;reads=nullptr;writes=nullptr;}
    bool open(){if(query)return true;PDH_HQUERY candidate{};if(PdhOpenQueryW(nullptr,0,&candidate)!=ERROR_SUCCESS)return false;PDH_HCOUNTER r{},w{};
        if(PdhAddEnglishCounterW(candidate,L"\\PhysicalDisk(*)\\Disk Read Bytes/sec",0,&r)!=ERROR_SUCCESS||PdhAddEnglishCounterW(candidate,L"\\PhysicalDisk(*)\\Disk Write Bytes/sec",0,&w)!=ERROR_SUCCESS){PdhCloseQuery(candidate);return false;}
        query=candidate;reads=r;writes=w;return true;
    }
    static std::optional<std::vector<ActivityDiskCounter>>values(PDH_HCOUNTER counter){
        DWORD infoBytes{};if(PdhGetCounterInfoW(counter,FALSE,&infoBytes,nullptr)!=PDH_MORE_DATA||infoBytes<sizeof(PDH_COUNTER_INFO_W)||infoBytes>1024*1024)return {};
        std::vector<std::max_align_t>infoStorage((infoBytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));auto*info=reinterpret_cast<PDH_COUNTER_INFO_W*>(infoStorage.data());const DWORD infoCapacity=infoBytes;
        if(PdhGetCounterInfoW(counter,FALSE,&infoBytes,info)!=ERROR_SUCCESS||infoBytes>infoCapacity||info->dwType!=PERF_COUNTER_BULK_COUNT)return {};
        // Probe afresh if hotplug grows the array; at most two bounded tries.
        for(unsigned attempt=0;attempt<2;++attempt){DWORD bytes{},count{};if(PdhGetRawCounterArrayW(counter,&bytes,&count,nullptr)!=PDH_MORE_DATA||bytes<sizeof(PDH_RAW_COUNTER_ITEM_W)||bytes>1024*1024||count>1025)return {};
            std::vector<std::max_align_t>storage((bytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));const DWORD capacity=bytes;auto*array=reinterpret_cast<PDH_RAW_COUNTER_ITEM_W*>(storage.data());const auto hr=PdhGetRawCounterArrayW(counter,&bytes,&count,array);if(hr==PDH_MORE_DATA)continue;if(hr!=ERROR_SUCCESS||bytes>capacity||count>1025||std::size_t(count)>bytes/sizeof(*array))return {};
            const auto begin=reinterpret_cast<std::uintptr_t>(storage.data()),end=begin+bytes;std::vector<ActivityDiskCounter>result;result.reserve(count);
            for(DWORD n=0;n<count;++n){const auto address=reinterpret_cast<std::uintptr_t>(array[n].szName);if(address<begin||address>=end||address%alignof(wchar_t))return {};const auto maximum=std::min<std::size_t>((end-address)/sizeof(wchar_t),4097);std::size_t length{};while(length<maximum&&array[n].szName[length])++length;if(!length||length==maximum||length>4096)return {};
                const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,array[n].szName,int(length),nullptr,0,nullptr,nullptr);if(size<=0||size>4096)return {};std::string name(std::size_t(size),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,array[n].szName,int(length),name.data(),size,nullptr,nullptr)!=size)return {};
                const auto&raw=array[n].RawValue;const bool good=(raw.CStatus==PDH_CSTATUS_VALID_DATA||raw.CStatus==PDH_CSTATUS_NEW_DATA)&&raw.FirstValue>=0;result.push_back({std::move(name),good?std::uint64_t(raw.FirstValue):0,good});
            }return result;
        }return {};
    }
};
WindowsActivityDisk::WindowsActivityDisk():impl_(std::make_unique<Impl>()){}WindowsActivityDisk::~WindowsActivityDisk()=default;
std::optional<modules::ActivityCounters>WindowsActivityDisk::sample(){auto&i=*impl_;if(!i.open())return {};if(PdhCollectQueryData(i.query)!=ERROR_SUCCESS){i.close();return {};}const auto reads=Impl::values(i.reads),writes=Impl::values(i.writes);if(!reads||!writes)return {};return activityDiskCounters(*reads,*writes);}
}
#else
namespace endfield::native {struct WindowsActivityDisk::Impl{};WindowsActivityDisk::WindowsActivityDisk():impl_(std::make_unique<Impl>()){}WindowsActivityDisk::~WindowsActivityDisk()=default;std::optional<modules::ActivityCounters>WindowsActivityDisk::sample(){return {};}}
#endif
