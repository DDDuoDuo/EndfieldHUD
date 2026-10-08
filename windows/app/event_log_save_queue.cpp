#include "app/event_log_save_queue.hpp"

namespace endfield::app {
struct EventLogSaveQueue::Impl:std::enable_shared_from_this<Impl> {
    native::EventLogOwner&owner;UtilityExecutor&executor;UtilityExecutor::Route route;
    std::optional<ehud::data::EventLogWrite>pending;bool alive{true};
    Impl(native::EventLogOwner&o,UtilityExecutor&e):owner(o),executor(e),route(e.makeRoute()){}
    void retry(){
        if(!alive||!pending)return;
        const auto write=*pending;auto result=std::make_shared<ehud::data::EventLogSaveResult>(ehud::data::EventLogSaveResult{write.revision(),false});
        const std::weak_ptr<Impl>weak=shared_from_this();
        if(executor.submit(route,[write,result]{*result=write.execute();},[weak,result](std::exception_ptr error){
            if(const auto i=weak.lock();i&&i->alive){auto value=*result;if(error)value.success=false;i->owner.complete(value);if(i->alive)i->retry();}
        }))pending.reset();
    }
};
EventLogSaveQueue::EventLogSaveQueue(native::EventLogOwner&o,UtilityExecutor&e):impl_(std::make_shared<Impl>(o,e)){}
EventLogSaveQueue::~EventLogSaveQueue(){auto i=impl_;i->alive=false;i->executor.invalidate(i->route,false);}
void EventLogSaveQueue::capture(double time,bool force){auto i=impl_;if(auto next=i->owner.takeSave(time,force))i->pending=std::move(next);i->retry();}
void EventLogSaveQueue::retry(){auto i=impl_;i->retry();}
bool EventLogSaveQueue::pending()const{return impl_->pending.has_value();}
void EventLogSaveQueue::flush(double time){auto i=impl_;capture(time,true);if(!i->alive)return;
    while(i->pending){i->executor.waitIdle();i->executor.drain();if(!i->alive)return;i->retry();}
    i->executor.waitIdle();i->executor.drain();
}
}
