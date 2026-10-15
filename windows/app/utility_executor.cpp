#include "app/utility_executor.hpp"
#include <algorithm>
#include <condition_variable>
#include <list>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace endfield::app {
namespace {
// Best effort: names the worker for the memory trace and debuggers.
void describeWorker(const std::string& name)noexcept{
#ifdef _WIN32
    if(name.empty())return;
    using Describe=HRESULT(WINAPI*)(HANDLE,PCWSTR);
    Describe describe{};
    for(const auto*module:{L"kernelbase.dll",L"kernel32.dll"})if(const auto handle=GetModuleHandleW(module))
        if((describe=reinterpret_cast<Describe>(reinterpret_cast<void*>(GetProcAddress(handle,"SetThreadDescription")))))break;
    if(!describe)return;
    wchar_t text[64]{};std::size_t n=0;for(;n<name.size()&&n+1<std::size(text);++n)text[n]=static_cast<unsigned char>(name[n])<0x80?wchar_t(name[n]):L'?';
    (void)describe(GetCurrentThread(),text);
#else
    (void)name;
#endif
}
}
struct UtilityExecutor::Impl {
    struct Task { Route route;Work work;Completion completion;std::exception_ptr error;Attribution tag{}; };
    const std::thread::id owner=std::this_thread::get_id();
    const std::size_t capacity;
    const std::chrono::milliseconds idleExit;
    const std::string threadName;
    std::function<void()> notify;
    std::function<Attribution()> current;std::function<void(Attribution,const std::function<void()>&)> attributed;
    mutable std::mutex mutex;std::condition_variable ready,idle;
    std::list<Task> pending,completed;
    std::unordered_set<Route> routes;
    std::thread worker;
    Route nextRoute{1};Stats statistics;
    bool stopping{},notified{};
    bool exited{}; // the idle worker returned; the next submit joins it
    Impl(std::function<void()> n,std::size_t c,std::chrono::milliseconds idle,std::string name):capacity(c),idleExit(idle),threadName(std::move(name)),notify(std::move(n)) {
        if(!notify||c==0||c>256||idle.count()<0)throw std::invalid_argument("Invalid utility queue configuration");
    }
    void checkOwner()const {if(std::this_thread::get_id()!=owner)throw std::logic_error("Utility queue owner-thread operation");}
    void run()noexcept {
        describeWorker(threadName);
        for(;;){
            std::list<Task> current;{
                std::unique_lock lock(mutex);const auto work=[&]{return stopping||!pending.empty();};
                // An idle worker ends after idleExit without work: one bounded
                // wait, not a periodic wake. The thread stays joinable.
                if(idleExit.count()>0){if(!ready.wait_for(lock,idleExit,work)){exited=true;statistics.workerAlive=false;return;}}
                else ready.wait(lock,work);
                if(pending.empty()){statistics.workerAlive=false;return;}
                current.splice(current.end(),pending,pending.begin());statistics.pending=pending.size();statistics.running=1;
            }
            auto&task=current.front();try{task.work();}catch(...){task.error=std::current_exception();}
            task.work={};bool wake{};{
                std::lock_guard lock(mutex);statistics.running=0;++statistics.executed;
                if(!stopping&&routes.contains(task.route)){
                    completed.splice(completed.end(),current);statistics.completed=completed.size();wake=!notified;notified=true;
                }else ++statistics.discarded;
                if(pending.empty())idle.notify_all();
            }
            if(wake){try{notify();}catch(...){
                // Preserve queued results; a caller may drain explicitly even
                // after a failed OS message post. Let the next result retry.
                std::lock_guard lock(mutex);notified=false;
            }}
        }
    }
};
UtilityExecutor::UtilityExecutor(std::function<void()> notify,std::size_t capacity,std::chrono::milliseconds idleExit,std::string threadName)
    :impl_(std::make_shared<Impl>(std::move(notify),capacity,idleExit,std::move(threadName))){}
UtilityExecutor::~UtilityExecutor(){shutdown();}
UtilityExecutor::Route UtilityExecutor::makeRoute(){auto&i=*impl_;i.checkOwner();std::lock_guard lock(i.mutex);if(i.stopping||i.nextRoute==std::numeric_limits<Route>::max())throw std::logic_error("Utility queue is stopped or exhausted");const auto route=i.nextRoute++;i.routes.insert(route);return route;}
void UtilityExecutor::invalidate(Route route,bool discardPending){auto state=impl_;auto&i=*state;i.checkOwner();std::list<Impl::Task>retired;{
    std::lock_guard lock(i.mutex);i.routes.erase(route);auto erase=[&](auto&queue){for(auto it=queue.begin();it!=queue.end();)if(it->route==route){auto remove=it++;retired.splice(retired.end(),queue,remove);++i.statistics.discarded;}else ++it;};
    if(discardPending)erase(i.pending);erase(i.completed);i.statistics.pending=i.pending.size();i.statistics.completed=i.completed.size();if(i.completed.empty())i.notified=false;if(i.pending.empty()&&!i.statistics.running)i.idle.notify_all();
    } // Captured destructors can reenter owner cleanup only after unlocking.
}
void UtilityExecutor::attribute(std::function<Attribution()> current,std::function<void(Attribution,const std::function<void()>&)> run){auto&i=*impl_;i.checkOwner();i.current=std::move(current);i.attributed=std::move(run);}
bool UtilityExecutor::submit(Route route,Work work,Completion completion){auto&i=*impl_;i.checkOwner();if(!work||!completion)throw std::invalid_argument("Utility task requires work and completion");
    const Attribution tag=i.current?i.current():nullptr;
    std::lock_guard lock(i.mutex);if(i.stopping||!i.routes.contains(route))throw std::logic_error("Utility task route is inactive");if(i.pending.size()+i.completed.size()+i.statistics.running>=i.capacity)return false;
    i.pending.push_back({route,std::move(work),std::move(completion),{},tag});
    // A worker that ended idle released the lock before returning: joining it
    // here only waits for its thread exit.
    if(i.exited){i.worker.join();i.exited=false;}
    if(!i.worker.joinable())try{i.worker=std::thread([&i]{i.run();});i.statistics.started=i.statistics.workerAlive=true;++i.statistics.workersStarted;}catch(...){i.pending.pop_back();throw;}
    ++i.statistics.accepted;i.statistics.pending=i.pending.size();i.ready.notify_one();return true;
}
std::size_t UtilityExecutor::drain(){auto state=impl_;auto&i=*state;i.checkOwner();std::size_t count{},available{};{std::lock_guard lock(i.mutex);available=i.completed.size();i.notified=false;}
    // Bound this drain to results already present. Reentrant submissions and
    // concurrently finishing tasks are left for the next coalesced message.
    try{
        while(count<available){Impl::Task task;{std::lock_guard lock(i.mutex);if(i.stopping||i.completed.empty())break;task=std::move(i.completed.front());i.completed.pop_front();i.statistics.completed=i.completed.size();if(!i.routes.contains(task.route)){++i.statistics.discarded;continue;}}++count;
            if(task.tag&&i.attributed){const std::function<void()> call=[&task]{task.completion(task.error);};i.attributed(task.tag,call);}else task.completion(task.error);}
    }catch(...){
        // Preserve the original callback failure, but leave a wake for results
        // it prevented us from visiting. Otherwise an idle queue can strand a
        // save completion forever without any subsequent worker submission.
        bool wake{};{std::lock_guard lock(i.mutex);wake=!i.stopping&&!i.completed.empty()&&!i.notified;if(wake)i.notified=true;}
        if(wake)try{i.notify();}catch(...){std::lock_guard lock(i.mutex);i.notified=false;}
        throw;
    }
    return count;
}
UtilityExecutor::Stats UtilityExecutor::stats()const{const auto&i=*impl_;std::lock_guard lock(i.mutex);return i.statistics;}
void UtilityExecutor::waitIdle(){auto&i=*impl_;i.checkOwner();std::unique_lock lock(i.mutex);i.idle.wait(lock,[&]{return i.pending.empty()&&!i.statistics.running;});}
void UtilityExecutor::shutdown(){auto state=impl_;auto&i=*state;i.checkOwner();std::list<Impl::Task>retired;{
    std::lock_guard lock(i.mutex);i.stopping=true;i.routes.clear();i.statistics.discarded+=i.completed.size();retired.splice(retired.end(),i.completed);i.statistics.completed=0;i.notified=false;i.ready.notify_one();
    }if(i.worker.joinable())i.worker.join();
}
}
