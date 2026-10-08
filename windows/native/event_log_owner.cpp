#include "native/event_log_owner.hpp"
#include "native/event_log_names.hpp"
#include <stdexcept>
#include <thread>

namespace endfield::native {
namespace {void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}}
struct EventLogOwner::Impl {
    std::thread::id thread=std::this_thread::get_id();
    NativeEventNameCompactor names; // must outlive store's borrowed compactor
    ehud::data::EventLogStore store;
    EventLogOwnerCallbacks hooks;
    bool alive{true};
    Impl(std::optional<std::filesystem::path>root,EventLogOwnerCallbacks callbacks,std::size_t capacity)
        :store(std::move(root),capacity,[this](std::string_view value){return names.compact(value);}),hooks(std::move(callbacks)){
        need(bool(hooks.now),"Event Log owner requires its caller's monotonic clock");
    }
    void requireThread()const{need(thread==std::this_thread::get_id(),"Event Log owner belongs to its creating UI thread");}
    double now(){requireThread();auto clock=hooks.now;return clock();}
    void changed(){if(!alive)return;auto notify=hooks.changed;if(notify)notify();}
    modules::EventLogSnapshot snapshot()const{
        requireThread();if(!alive)return {};modules::EventLogSnapshot out;
        out.events.assign(store.events().begin(),store.events().end());if(const auto status=store.statusMessage())out.status=*status;return out;
    }
    void clear(){requireThread();if(!alive)return;const auto time=now();if(!alive)return;store.clear(time);changed();}
};
EventLogOwner::EventLogOwner(std::optional<std::filesystem::path>root,EventLogOwnerCallbacks hooks,std::size_t capacity)
    :impl_(std::make_shared<Impl>(std::move(root),std::move(hooks),capacity)){}
EventLogOwner::~EventLogOwner(){if(impl_){impl_->alive=false;impl_->hooks={};}}
void EventLogOwner::record(modules::SystemEvent event){auto i=impl_;i->requireThread();const auto time=i->now();if(!i->alive)return;i->store.record(std::move(event),time);i->changed();}
void EventLogOwner::clear(){auto i=impl_;i->clear();}
modules::EventLogSnapshot EventLogOwner::snapshot()const{return impl_->snapshot();}
modules::EventLogCallbacks EventLogOwner::callbacks(std::function<std::string(double)>timestamp)const{
    impl_->requireThread();need(bool(timestamp),"Event Log owner requires a caller timestamp formatter");
    std::weak_ptr<Impl>route=impl_;
    return {[route]{if(const auto i=route.lock())return i->snapshot();return modules::EventLogSnapshot{};},
        [route]{if(const auto i=route.lock())i->clear();},std::move(timestamp)};
}
modules::EventNameCompactor EventLogOwner::nameCompactor()const{
    impl_->requireThread();std::weak_ptr<Impl>route=impl_;
    return [route](std::string_view value){if(const auto i=route.lock()){i->requireThread();if(i->alive)return i->names.compact(value);}return std::string{};};
}
std::uint64_t EventLogOwner::revision()const{impl_->requireThread();return impl_->store.revision();}
std::optional<double>EventLogOwner::saveDeadline()const{impl_->requireThread();return impl_->store.saveDeadline();}
std::optional<ehud::data::EventLogWrite>EventLogOwner::takeSave(double time,bool force){impl_->requireThread();return impl_->store.takeSave(time,force);}
bool EventLogOwner::complete(ehud::data::EventLogSaveResult result){auto i=impl_;i->requireThread();const bool changed=i->store.complete(result);if(changed)i->changed();return changed;}
}
