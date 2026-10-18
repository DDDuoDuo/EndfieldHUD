#include "native/storage_details_probe.hpp"
#include "core/data/json.hpp"
#include <cmath>
#include <stdexcept>
namespace endfield::native {
namespace {void valid(const modules::StorageDetailsSnapshot&v){if(v.categories.size()>64||(v.updatedAt&&!std::isfinite(*v.updatedAt)))throw std::invalid_argument("Invalid Storage details result");const auto label=[](const std::string&s){return s.size()<=4096&&s.find('\0')==s.npos&&ehud::data::Json::validUtf8(s);};for(const auto&c:v.categories)if(c.id.empty()||!label(c.id)||!label(c.title)||(c.bytes&&*c.bytes<0))throw std::invalid_argument("Invalid Storage category metadata");if(v.error&&!label(*v.error))throw std::invalid_argument("Invalid Storage details error");}}
struct StorageDetailsProbe::Impl {modules::StorageController&controller;app::UtilityExecutor&executor;Scanner scanner;Clock clock;app::UtilityExecutor::Route route;std::optional<modules::StorageDetailsRequest>pending;std::shared_ptr<modules::StorageScanCancellation>active;Stats counts;
    Impl(modules::StorageController&c,app::UtilityExecutor&e,Scanner s,Clock t):controller(c),executor(e),scanner(std::move(s)),clock(std::move(t)),route(e.makeRoute()){}
    ~Impl(){if(pending)pending->cancellation->cancel();if(active)active->cancel();executor.invalidate(route);}
};
StorageDetailsProbe::StorageDetailsProbe(modules::StorageController&c,app::UtilityExecutor&e,Scanner scanner,Clock clock){if(!scanner||!clock)throw std::invalid_argument("Storage details require explicit scanner and owner clock");impl_=std::make_unique<Impl>(c,e,std::move(scanner),std::move(clock));}
StorageDetailsProbe::~StorageDetailsProbe()=default;
bool StorageDetailsProbe::submitPending(){auto&i=*impl_;if(!i.pending)i.pending=i.controller.takeDetailsRequest();if(!i.pending)return false;
    struct Result{modules::StorageDetailsSnapshot value;};auto result=std::make_shared<Result>();const auto request=*i.pending;
    const bool submitted=i.executor.submit(i.route,[scanner=i.scanner,cancel=request.cancellation,result]{if(cancel->cancelled())return;result->value=scanner(*cancel);if(!cancel->cancelled())valid(result->value);},[owner=&i,result,token=request.token](std::exception_ptr error){const auto now=owner->clock();if(!std::isfinite(now))throw std::invalid_argument("Invalid Storage completion clock");owner->counts.inFlight=false;owner->active.reset();++owner->counts.completed;if(error){++owner->counts.failed;result->value={{},false,now,true,"Folder sizes are unavailable or the scan was interrupted."};}owner->controller.completeDetails(token,std::move(result->value),now);});
    if(!submitted){++i.counts.backpressure;return false;}i.active=request.cancellation;i.pending.reset();i.counts.inFlight=true;++i.counts.submitted;return true;
}
StorageDetailsProbe::Stats StorageDetailsProbe::stats()const noexcept{auto s=impl_->counts;s.waiting=impl_->pending.has_value();return s;}
}
