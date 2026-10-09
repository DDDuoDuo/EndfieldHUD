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
StorageController::~StorageController(){if(cancellation_)cancellation_->cancel();}
std::uint64_t StorageController::token(){need(serial_!=std::numeric_limits<std::uint64_t>::max(),"Storage request generation exhausted");return ++serial_;}
void StorageController::activate(double now){clock(now);if(active_)return;active_=true;refreshCapacity(now);deadline_=now+capacityCacheDuration;}
void StorageController::deactivate(){active_=false;deadline_.reset();pendingDetails_=false;if(cancellation_)cancellation_->cancel();if(details_.isLoading){details_.isLoading=false;publish();}}
void StorageController::refresh(double now){refreshCapacity(now,true);}
void StorageController::refreshCapacity(double now,bool force){clock(now);if(!active_||capacityToken_)return;if(!force&&requestedAt_&&now-*requestedAt_>=0&&now-*requestedAt_<capacityCacheDuration)return;
    const auto next=token();requestedAt_=now;capacityToken_=next;capacityRequest_=StorageCapacityRequest{next,now};snapshot_.isLoading=true;publish();}
void StorageController::requestDetails(double now,bool refresh){clock(now);if(!active_)return;refreshCapacity(now);if(cancellation_){if(cancellation_->cancelled())pendingDetails_=true;return;}
    if(!refresh&&details_.updatedAt&&now-*details_.updatedAt>=0&&now-*details_.updatedAt<detailsCacheDuration)return;
    auto cancellation=std::make_shared<StorageScanCancellation>();const auto next=token();cancellation_=std::move(cancellation);detailToken_=next;detailRequest_=StorageDetailsRequest{next,cancellation_};details_.isLoading=true;details_.error.reset();publish();}
void StorageController::wake(double now){clock(now);if(!active_||!deadline_||now<*deadline_)return;const auto periods=std::floor((now-*deadline_)/capacityCacheDuration)+1;deadline_=*deadline_+periods*capacityCacheDuration;refreshCapacity(now);}
std::optional<StorageCapacityRequest>StorageController::takeCapacityRequest()noexcept{return std::exchange(capacityRequest_,{});}
std::optional<StorageDetailsRequest>StorageController::takeDetailsRequest()noexcept{return std::exchange(detailRequest_,{});}
bool StorageController::completeCapacity(std::uint64_t token,std::optional<StorageCapacity>value){if(!token||token!=capacityToken_)return false;if(value)validate(*value);capacityToken_=0;capacityRequest_.reset();snapshot_.isLoading=false;if(value){snapshot_.capacity=std::move(value);snapshot_.error.reset();}else snapshot_.error="Storage capacity is unavailable.";publish();return true;}
bool StorageController::completeDetails(std::uint64_t token,StorageDetailsSnapshot value,double now){clock(now);if(!token||token!=detailToken_||!cancellation_)return false;const bool cancelled=cancellation_->cancelled();if(!cancelled)validate(value);detailToken_=0;cancellation_.reset();detailRequest_.reset();if(!cancelled){details_=std::move(value);details_.isLoading=false;publish();}if(pendingDetails_&&active_){pendingDetails_=false;requestDetails(now,true);}return true;}
StorageScanBudget::StorageScanBudget(double start,StorageScanLimits limits):limits_(limits),start_(start),scopeStart_(start){clock(start);clock(limits.maximumSeconds);clock(limits.secondsPerFolder);}
void StorageScanBudget::beginScope(double now){clock(now);scopeStart_=now;entries_=bytes_=0;}
bool StorageScanBudget::limitReached(double now,bool cancelled)const{clock(now);return cancelled||total_>=std::max<std::int64_t>(0,limits_.maximumEntries)||entries_>=std::max<std::int64_t>(0,limits_.entriesPerFolder)||now-start_>=std::max(0.,limits_.maximumSeconds)||now-scopeStart_>=std::max(0.,limits_.secondsPerFolder);}
void StorageScanBudget::examinedEntry(){need(total_<std::numeric_limits<std::int64_t>::max()&&entries_<std::numeric_limits<std::int64_t>::max(),"Storage entry accounting overflow");++total_;++entries_;}
bool StorageScanBudget::mayDescend(int depth)const noexcept{return depth<std::max(0,limits_.maximumDepth);}
bool StorageScanBudget::addAllocated(std::uint64_t volume,std::uint64_t file,std::int64_t bytes){if(!seen_.emplace(volume,file).second)return true;bytes=std::max<std::int64_t>(0,bytes);if(bytes>std::numeric_limits<std::int64_t>::max()-bytes_)return false;bytes_+=bytes;return true;}
bool StorageScanBudget::addSourceBlocks(std::uint64_t volume,std::uint64_t file,std::int64_t blocks){if(seen_.contains({volume,file}))return true;blocks=std::max<std::int64_t>(0,blocks);if(blocks>std::numeric_limits<std::int64_t>::max()/512){seen_.emplace(volume,file);return false;}return addAllocated(volume,file,blocks*512);}
}
