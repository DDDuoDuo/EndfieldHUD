#include "modules/storage_state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace endfield::modules {
namespace {
void need(bool b,const char*m){if(!b)throw std::invalid_argument(m);}void clock(double v){need(std::isfinite(v),"Storage time must be finite");}
void validate(const StorageCapacity&v){clock(v.updatedAt);need(v.totalBytes>0&&v.availableBytes>=0&&v.availableBytes<=v.totalBytes,"Invalid storage capacity");}
void validate(const StorageDetailsSnapshot&v){if(v.updatedAt)clock(*v.updatedAt);for(const auto&c:v.categories)need(!c.bytes||*c.bytes>=0,"Allocated category bytes cannot be negative");}
}
std::vector<StorageVolume>limitedStorageVolumes(std::vector<StorageVolume>volumes,const std::optional<std::string>&keeping){
    if(volumes.size()<=storageVolumeLimit)return volumes;
    std::vector<StorageVolume>result(volumes.begin(),volumes.begin()+std::ptrdiff_t(storageVolumeLimit));
    if(keeping&&std::none_of(result.begin(),result.end(),[&](const auto&v){return v.id==*keeping;}))
        if(const auto kept=std::find_if(volumes.begin(),volumes.end(),[&](const auto&v){return v.id==*keeping;});kept!=volumes.end())result.back()=*kept;
    return result;
}
StorageController::~StorageController(){if(cancellation_)cancellation_->cancel();}
std::uint64_t StorageController::token(){need(serial_!=std::numeric_limits<std::uint64_t>::max(),"Storage request generation exhausted");return ++serial_;}
// activate (:274-285): a read now and the 60 s timer.
void StorageController::activate(double now){clock(now);if(active_)return;active_=true;refreshCapacity(now);deadline_=now+capacityCacheDuration;}
void StorageController::deactivate(){active_=false;deadline_.reset();pendingDetails_=false;if(cancellation_)cancellation_->cancel();if(details_.isLoading){details_.isLoading=false;publish();}}
void StorageController::refresh(double now){refreshCapacity(now,true);}
void StorageController::setSelectedVolume(std::optional<std::string>id){if(id&&(id->empty()||id->size()>1024))id.reset();selected_=std::move(id);}
const StorageVolume*StorageController::currentVolume()const noexcept{for(const auto&v:volumes_)if(selected_&&v.id==*selected_)return &v;for(const auto&v:volumes_)if(v.startup)return &v;return nullptr;}
bool StorageController::selectVolume(const std::string&id,double now){clock(now);
    const auto listed=std::find_if(volumes_.begin(),volumes_.end(),[&](const auto&v){return v.id==id;});if(listed==volumes_.end())return false;
    const auto*current=currentVolume();const bool newChoice=selected_!=id,newDisk=!current||current->id!=id;
    if(newChoice){selected_=id;if(selectionChanged)selectionChanged(id);}
    if(newDisk){snapshot_.capacity=listed->capacity;snapshot_.error.reset();}
    if(newChoice||newDisk)publish();
    if(newDisk)refreshCapacity(now,true);
    return true;}
void StorageController::volumesChanged(double now){clock(now);requestedAt_.reset();if(!active_)return;if(capacityToken_)pendingVolumeRead_=true;else refreshCapacity(now,true);}
// refreshCapacity (:427-449): one read in flight; the 60 s cache unless forced.
void StorageController::refreshCapacity(double now,bool force){clock(now);if(!active_||capacityToken_)return;if(!force&&requestedAt_&&now-*requestedAt_>=0&&now-*requestedAt_<capacityCacheDuration)return;
    const auto next=token();requestedAt_=now;capacityToken_=next;pendingVolumeRead_=false;capacityRequest_=StorageCapacityRequest{next,now};snapshot_.isLoading=true;publish();if(requested)requested();}
void StorageController::requestDetails(double now,bool refresh){clock(now);if(!active_)return;refreshCapacity(now);if(cancellation_){if(cancellation_->cancelled())pendingDetails_=true;return;}
    if(!refresh&&details_.updatedAt&&now-*details_.updatedAt>=0&&now-*details_.updatedAt<detailsCacheDuration)return;
    auto cancellation=std::make_shared<StorageScanCancellation>();const auto next=token();cancellation_=std::move(cancellation);detailToken_=next;detailRequest_=StorageDetailsRequest{next,cancellation_};details_.isLoading=true;details_.error.reset();publish();if(requested)requested();}
void StorageController::wake(double now){clock(now);if(!active_||!deadline_||now<*deadline_)return;const auto periods=std::floor((now-*deadline_)/capacityCacheDuration)+1;deadline_=*deadline_+periods*capacityCacheDuration;refreshCapacity(now);}
std::optional<StorageCapacityRequest>StorageController::takeCapacityRequest()noexcept{return std::exchange(capacityRequest_,{});}
std::optional<StorageDetailsRequest>StorageController::takeDetailsRequest()noexcept{return std::exchange(detailRequest_,{});}
bool StorageController::completeCapacity(std::uint64_t token,StorageCapacityResult result,double now){clock(now);if(!token||token!=capacityToken_)return false;
    for(const auto&v:result.volumes){validate(v.capacity);need(!v.id.empty(),"Invalid storage volume");}if(result.startup)validate(*result.startup);
    capacityToken_=0;capacityRequest_.reset();snapshot_.isLoading=false;
    volumes_=limitedStorageVolumes(std::move(result.volumes),selected_);
    if(const auto*current=currentVolume()){snapshot_.capacity=current->capacity;snapshot_.error.reset();}
    else if(result.startup){snapshot_.capacity=std::move(result.startup);snapshot_.error.reset();}
    else snapshot_.error="Storage capacity is unavailable.";
    publish();if(pendingVolumeRead_&&active_)refreshCapacity(now,true);return true;}
bool StorageController::completeDetails(std::uint64_t token,StorageDetailsSnapshot value,double now){clock(now);if(!token||token!=detailToken_||!cancellation_)return false;const bool cancelled=cancellation_->cancelled();if(!cancelled)validate(value);detailToken_=0;cancellation_.reset();detailRequest_.reset();if(!cancelled){details_=std::move(value);details_.isLoading=false;publish();}if(pendingDetails_&&active_){pendingDetails_=false;requestDetails(now,true);}return true;}
StorageScanBudget::StorageScanBudget(double start,StorageScanLimits limits):limits_(limits),start_(start),scopeStart_(start){clock(start);clock(limits.maximumSeconds);clock(limits.secondsPerFolder);}
void StorageScanBudget::beginScope(double now){clock(now);scopeStart_=now;entries_=bytes_=0;}
bool StorageScanBudget::limitReached(double now,bool cancelled)const{clock(now);return cancelled||total_>=std::max<std::int64_t>(0,limits_.maximumEntries)||entries_>=std::max<std::int64_t>(0,limits_.entriesPerFolder)||now-start_>=std::max(0.,limits_.maximumSeconds)||now-scopeStart_>=std::max(0.,limits_.secondsPerFolder);}
void StorageScanBudget::examinedEntry(){need(total_<std::numeric_limits<std::int64_t>::max()&&entries_<std::numeric_limits<std::int64_t>::max(),"Storage entry accounting overflow");++total_;++entries_;}
bool StorageScanBudget::mayDescend(int depth)const noexcept{return depth<std::max(0,limits_.maximumDepth);}
bool StorageScanBudget::addAllocated(std::uint64_t volume,std::uint64_t file,std::int64_t bytes){if(!seen_.emplace(volume,file).second)return true;bytes=std::max<std::int64_t>(0,bytes);if(bytes>std::numeric_limits<std::int64_t>::max()-bytes_)return false;bytes_+=bytes;return true;}
bool StorageScanBudget::addSourceBlocks(std::uint64_t volume,std::uint64_t file,std::int64_t blocks){if(seen_.contains({volume,file}))return true;blocks=std::max<std::int64_t>(0,blocks);if(blocks>std::numeric_limits<std::int64_t>::max()/512){seen_.emplace(volume,file);return false;}return addAllocated(volume,file,blocks*512);}
}
