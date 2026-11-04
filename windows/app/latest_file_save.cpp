#include "app/latest_file_save.hpp"
#include "core/data/file_io.hpp"
#include <atomic>
#include <utility>
#include <vector>

namespace endfield::app {
struct LatestFileSave::Impl:std::enable_shared_from_this<Impl> {
    UtilityExecutor&executor;UtilityExecutor::Route route;std::filesystem::path path;std::size_t maximum;Failed failed;
    std::optional<std::string>onDisk,waiting;Stats stats;bool alive{true};
    // The running save: its bytes, and whether the worker replaced the file
    // (read by the destructor after waitIdle, when no completion runs).
    std::shared_ptr<const std::string>running;std::shared_ptr<std::atomic<bool>>replaced;
    // whenWritten callbacks of the running and the waiting bytes (a superseded save's go on with the bytes that carry its change).
    std::vector<Written>runningWritten,waitingWritten;
    Impl(UtilityExecutor&e,std::filesystem::path p,std::size_t m,std::optional<std::string>disk,Failed f)
        :executor(e),route(e.makeRoute()),path(std::move(p)),maximum(m),failed(std::move(f)),onDisk(std::move(disk)){}
    void finished(const std::string&bytes,std::exception_ptr error,std::vector<Written>written){
        if(error)++stats.failed;else{onDisk=bytes;++stats.written;}
        // The handler may drop what waits (built on the change that failed) before it goes.
        if(error){if(failed)failed(error);}else for(auto&w:written)if(w)w();
        if(alive)pump();
    }
    void pump(){
        if(!alive||running||!waiting)return;
        auto bytes=std::make_shared<std::string>(std::move(*waiting));waiting.reset();
        auto done=std::make_shared<std::atomic<bool>>(false);const std::weak_ptr<Impl>weak=shared_from_this();
        if(executor.submit(route,[path=path,maximum=maximum,expected=onDisk,bytes,done]{ehud::data::detail::replaceFile(path,expected,*bytes,maximum);*done=true;},
            [weak,bytes](std::exception_ptr error){const auto i=weak.lock();if(!i||!i->alive)return;i->running.reset();i->replaced.reset();i->finished(*bytes,error,std::exchange(i->runningWritten,{}));})){
            running=std::move(bytes);replaced=std::move(done);runningWritten=std::exchange(waitingWritten,{});++stats.queued;return;}
        // A full executor: the bytes wait for a completion (queueCapacityAvailable), as every other save does.
        waiting=std::move(*bytes);
    }
    // flush() and teardown only: nothing of this file runs, so the save is in order here.
    void writeAtOnce(){
        const auto bytes=std::move(*waiting);waiting.reset();auto written=std::exchange(waitingWritten,{});
        ++stats.atOnce;std::exception_ptr error;
        try{ehud::data::detail::replaceFile(path,onDisk,bytes,maximum);}catch(...){error=std::current_exception();}
        finished(bytes,error,std::move(written));
    }
};
LatestFileSave::LatestFileSave(UtilityExecutor&executor,std::filesystem::path path,std::size_t maximum,std::optional<std::string>onDisk,Failed failed)
    :impl_(std::make_shared<Impl>(executor,std::move(path),maximum,std::move(onDisk),std::move(failed))){}
LatestFileSave::~LatestFileSave(){
    auto&i=*impl_;i.alive=false;i.executor.invalidate(i.route,false);
    if(i.running){i.executor.waitIdle();if(*i.replaced)i.onDisk=*i.running;}
    if(i.waiting)try{ehud::data::detail::replaceFile(i.path,i.onDisk,*i.waiting,i.maximum);}catch(...){}
}
void LatestFileSave::save(std::string bytes){auto&i=*impl_;i.waiting=std::move(bytes);i.pump();}
void LatestFileSave::whenWritten(Written written){
    auto&i=*impl_;if(i.waiting)i.waitingWritten.push_back(std::move(written));else if(i.running)i.runningWritten.push_back(std::move(written));else if(written)written();
}
void LatestFileSave::queueCapacityAvailable(){impl_->pump();}
void LatestFileSave::discardWaiting()noexcept{impl_->waiting.reset();impl_->waitingWritten.clear();}
void LatestFileSave::flush(){
    const auto i=impl_;
    while(i->alive&&(i->running||i->waiting)){
        if(!i->running){
            i->pump();if(i->running)continue;
            // Other modules' work holds every slot: let it finish, then write here if it still does.
            i->executor.waitIdle();i->executor.drain();if(i->alive&&!i->running&&i->waiting){i->pump();if(!i->running)i->writeAtOnce();}
            continue;
        }
        const auto before=i->running;i->executor.waitIdle();i->executor.drain();
        // A completion the host withheld (its module failed): the worker's own word.
        if(i->alive&&i->running==before){const auto bytes=std::exchange(i->running,{});const bool replaced=*std::exchange(i->replaced,{});auto written=std::exchange(i->runningWritten,{});
            if(replaced){i->onDisk=*bytes;++i->stats.written;for(auto&w:written)if(w)w();}else ++i->stats.failed;}
    }
}
bool LatestFileSave::idle()const noexcept{return !impl_->running&&!impl_->waiting;}
const std::optional<std::string>&LatestFileSave::onDisk()const noexcept{return impl_->onDisk;}
LatestFileSave::Stats LatestFileSave::stats()const noexcept{return impl_->stats;}
}
