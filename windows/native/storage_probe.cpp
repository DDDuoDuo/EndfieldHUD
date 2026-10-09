#include "native/storage_probe.hpp"
#include "core/data/json.hpp"
#include <cmath>
#include <stdexcept>

namespace endfield::native {
struct StorageCapacityProbe::Impl {
    modules::StorageController&controller;app::UtilityExecutor&executor;Reader reader;
    app::UtilityExecutor::Route route;std::optional<modules::StorageCapacityRequest>pending;Stats counts;
    Impl(modules::StorageController&c,app::UtilityExecutor&e,Reader r):controller(c),executor(e),reader(std::move(r)),route(e.makeRoute()){}
    ~Impl(){executor.invalidate(route);}
};
StorageCapacityProbe::StorageCapacityProbe(modules::StorageController&c,app::UtilityExecutor&e,Reader r){if(!r)throw std::invalid_argument("Storage requires an explicit independently owned capacity provider");impl_=std::make_unique<Impl>(c,e,std::move(r));}
StorageCapacityProbe::~StorageCapacityProbe()=default;
bool StorageCapacityProbe::submitPending(){auto&i=*impl_;if(!i.pending)i.pending=i.controller.takeCapacityRequest();if(!i.pending)return false;
    struct Result {std::optional<modules::StorageCapacity>capacity;};auto result=std::make_shared<Result>();const auto token=i.pending->token;
    const bool submitted=i.executor.submit(i.route,[reader=i.reader,result]{
        result->capacity=reader();if(result->capacity){const auto&v=*result->capacity;
            if(v.totalBytes<=0||v.availableBytes<0||v.availableBytes>v.totalBytes||!std::isfinite(v.updatedAt)||v.volumeName.size()>4096||!ehud::data::Json::validUtf8(v.volumeName))throw std::invalid_argument("Invalid capacity provider result");}
    },[owner=&i,result,token](std::exception_ptr error){
        // This callback runs only on the owner thread. Route invalidation in
        // ~Impl removes it before either borrowed object can be destroyed.
        owner->counts.inFlight=false;++owner->counts.completed;if(error)++owner->counts.failed;
        owner->controller.completeCapacity(token,error?std::nullopt:std::move(result->capacity));
    });
    if(!submitted){++i.counts.backpressure;return false;}i.pending.reset();i.counts.inFlight=true;++i.counts.submitted;return true;
}
StorageCapacityProbe::Stats StorageCapacityProbe::stats()const noexcept{auto value=impl_->counts;value.waiting=impl_->pending.has_value();return value;}
}
