#include "app/utility_executor.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using endfield::app::UtilityExecutor;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char*why){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,why);}
struct Signals {
    std::mutex mutex;std::condition_variable changed;unsigned value{};
    void post(){std::lock_guard lock(mutex);++value;changed.notify_all();}
    void wait(unsigned n){std::unique_lock lock(mutex);if(!changed.wait_for(lock,std::chrono::seconds(5),[&]{return value>=n;}))throw std::runtime_error("Timed out waiting for synthetic worker completion");}
};
void boundedAndOwner(){
    Signals signals;UtilityExecutor executor([&]{signals.post();},2);const auto owner=std::this_thread::get_id();
    check(!executor.stats().started,"Constructing idle executor creates no worker");const auto route=executor.makeRoute();
    std::promise<void> releaseFirst,releaseSecond;auto first=releaseFirst.get_future().share(),second=releaseSecond.get_future().share();
    std::atomic<unsigned> ran{};unsigned delivered{};
    auto complete=[&](std::exception_ptr error){check(!error&&std::this_thread::get_id()==owner,"Completion executes only on owner drain");++delivered;};
    check(executor.submit(route,[&]{first.wait();++ran;},complete),"First bounded task accepted");
    check(executor.submit(route,[&]{second.wait();++ran;},complete),"Second bounded task accepted");
    check(!executor.submit(route,[&]{ran+=100;},complete),"Full queue rejects work without starting it");
    releaseFirst.set_value();signals.wait(1);check(delivered==0&&ran==1,"Worker cannot invoke UI completion");
    check(!executor.submit(route,[]{},complete),"Completed but undrained result counts toward memory bound");
    check(executor.drain()==1&&delivered==1,"Explicit owner drain frees one slot");releaseSecond.set_value();signals.wait(2);check(executor.drain()==1&&delivered==2&&ran==2,"No rejected work executes later");
    check(executor.stats().pending==0&&executor.stats().running==0&&executor.stats().completed==0,"Settled worker sleeps with an empty queue");
    bool wrongThread{};std::thread other([&]{try{executor.makeRoute();}catch(const std::logic_error&){wrongThread=true;}});other.join();check(wrongThread,"Route mutation is owner-thread confined");
    executor.shutdown();rejects([&]{executor.submit(route,[]{},complete);},"Stopped queue rejects further work");
}
void cancellationAndFailure(){
    Signals signals,started;UtilityExecutor executor([&]{signals.post();},3);const auto cancelled=executor.makeRoute(),live=executor.makeRoute();
    std::promise<void> release;auto gate=release.get_future().share();std::atomic<unsigned> ran{};unsigned callbacks{};
    auto noCallback=[&](std::exception_ptr){++callbacks;};
    executor.submit(cancelled,[&]{started.post();gate.wait();++ran;},noCallback);started.wait(1);
    executor.submit(cancelled,[&]{ran+=100;},noCallback);
    executor.submit(live,[&]{++ran;throw std::runtime_error("synthetic disk failure");},[&](std::exception_ptr error){check(bool(error),"Worker exception delivered as value");try{std::rethrow_exception(error);}catch(const std::runtime_error&e){check(std::string(e.what())=="synthetic disk failure","Original exception is retained");}++callbacks;});
    executor.invalidate(cancelled);release.set_value();signals.wait(1);executor.drain();check(ran==2&&callbacks==1,"Cancelled pending work and in-flight completion cannot call destroyed owner");
    check(executor.stats().discarded==2,"Cancellation accounts for both pending and running routes");
    executor.invalidate(live);rejects([&]{executor.submit(live,[]{},noCallback);},"Invalid route cannot be reused");
}
void callbackLifetimes(){
    Signals signals;UtilityExecutor executor([&]{signals.post();},2);const auto route=executor.makeRoute();unsigned destroyed{};
    struct Capture {std::function<void()> cleanup;~Capture(){cleanup();}};
    auto held=std::make_shared<Capture>();held->cleanup=[&]{check(executor.stats().running==0,"Retired callback destructor can query queue without its mutex held");++destroyed;};
    executor.submit(route,[]{},[held](std::exception_ptr){});held.reset();signals.wait(1);executor.invalidate(route);check(destroyed==1,"Invalidation retires completion captures outside lock");
    auto self=std::make_unique<UtilityExecutor>([&]{signals.post();});const auto selfRoute=self->makeRoute();self->submit(selfRoute,[]{},[&](std::exception_ptr){self.reset();});signals.wait(2);
    auto*raw=self.get();check(raw->drain()==1&&!self,"Destruction from completion keeps the active drain state alive safely");
}
void throwingCompletion(){
    Signals signals;UtilityExecutor executor([&]{signals.post();},2);const auto route=executor.makeRoute();unsigned delivered{};
    executor.submit(route,[]{},[](std::exception_ptr){throw std::runtime_error("synthetic owner failure");});
    executor.submit(route,[]{},[&](std::exception_ptr){++delivered;});executor.waitIdle();signals.wait(1);
    rejects([&]{executor.drain();},"Owner callback failure remains observable");
    signals.wait(2);check(executor.stats().completed==1,"Undelivered result survives an earlier callback failure");
    check(executor.drain()==1&&delivered==1,"Reposted completion drains without another task or timer");
}
void teardownAndReentry(){
    std::atomic<unsigned> writes{};unsigned callbacks{};
    {UtilityExecutor executor([]{},4);const auto route=executor.makeRoute();for(unsigned n=0;n<4;++n)check(executor.submit(route,[&]{++writes;},[&](std::exception_ptr){++callbacks;}),"Shutdown fixture save accepted");executor.invalidate(route,false);executor.waitIdle();check(executor.stats().running==0&&executor.stats().pending==0,"Shutdown barrier waits only for accepted file work");executor.shutdown();check(writes==4&&callbacks==0,"Shutdown finishes accepted writes without dispatching stale UI callbacks");}
    Signals signals;UtilityExecutor executor([&]{signals.post();},2);const auto route=executor.makeRoute();
    executor.submit(route,[]{},[&](std::exception_ptr){++callbacks;check(executor.submit(route,[]{},[&](std::exception_ptr){++callbacks;}),"Completion may queue the latest save without lock recursion");});
    signals.wait(1);executor.drain();signals.wait(2);executor.drain();check(callbacks==2,"Reentrant next save completes on a later drain");
    rejects([]{UtilityExecutor bad([]{},0);},"Unbounded/zero-capacity configuration rejects");
}
}
int main(){try{boundedAndOwner();cancellationAndFailure();teardownAndReentry();callbackLifetimes();throwingCompletion();std::cout<<"Utility queue: "<<checks<<" checks passed; synthetic work only\n";return 0;}catch(const std::exception&e){std::cerr<<"Utility queue failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
