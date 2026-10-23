#include "native/storage_probe.hpp"
#include "core/data/json.hpp"
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
void valid(const modules::StorageCapacity&v){if(v.totalBytes<=0||v.availableBytes<0||v.availableBytes>v.totalBytes||!std::isfinite(v.updatedAt)||v.volumeName.size()>4096||!ehud::data::Json::validUtf8(v.volumeName))throw std::invalid_argument("Invalid capacity provider result");}
}
struct StorageCapacityProbe::Impl {
    modules::StorageController&controller;app::UtilityExecutor&executor;Options options;
    app::UtilityExecutor::Route route;std::optional<modules::StorageCapacityRequest>pending;Stats counts;
    Impl(modules::StorageController&c,app::UtilityExecutor&e,Options o):controller(c),executor(e),options(std::move(o)),route(e.makeRoute()){}
    ~Impl(){executor.invalidate(route);}
};
StorageCapacityProbe::StorageCapacityProbe(modules::StorageController&c,app::UtilityExecutor&e,Options o){if(!o.read||!o.clock)throw std::invalid_argument("Storage requires an explicit independently owned capacity provider and owner clock");impl_=std::make_unique<Impl>(c,e,std::move(o));}
StorageCapacityProbe::~StorageCapacityProbe()=default;
bool StorageCapacityProbe::submitPending(){auto&i=*impl_;if(!i.pending)i.pending=i.controller.takeCapacityRequest();if(!i.pending)return false;
    auto result=std::make_shared<modules::StorageCapacityResult>();const auto token=i.pending->token;std::string name=i.options.startupName?i.options.startupName():std::string("Startup disk");
    const bool submitted=i.executor.submit(i.route,[reader=i.options.read,result,name=std::move(name)]{
        *result=reader(name);for(const auto&v:result->volumes)valid(v.capacity);if(result->startup)valid(*result->startup);
    },[owner=&i,result,token](std::exception_ptr error){
        // This callback runs only on the owner thread. Route invalidation in
        // ~Impl removes it before either borrowed object can be destroyed.
        owner->counts.inFlight=false;++owner->counts.completed;if(error)++owner->counts.failed;
        owner->controller.completeCapacity(token,error?modules::StorageCapacityResult{}:std::move(*result),owner->options.clock());
    });
    if(!submitted){++i.counts.backpressure;return false;}i.pending.reset();i.counts.inFlight=true;++i.counts.submitted;return true;
}
StorageCapacityProbe::Stats StorageCapacityProbe::stats()const noexcept{auto value=impl_->counts;value.waiting=impl_->pending.has_value();return value;}
}
