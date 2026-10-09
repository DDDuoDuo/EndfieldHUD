#ifdef _WIN32
#include "native/reader_import.hpp"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace n=endfield::native;namespace m=endfield::modules;namespace a=endfield::app;
namespace {
std::atomic<unsigned>checks{};void check(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
struct Gate {
    std::mutex mutex;std::condition_variable cv;bool entered{},released{};
    void work(){std::unique_lock lock(mutex);entered=true;cv.notify_all();if(!cv.wait_for(lock,std::chrono::seconds(5),[&]{return released;}))throw std::runtime_error("Synthetic import gate expired");}
    void wait(){std::unique_lock lock(mutex);check(cv.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"Owned import reaches borrowed worker");}
    void release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
    ~Gate(){release();}
};
m::ReaderBook book(unsigned n){return m::windowsReaderReference(ehud::data::makeUUID(),"C:\\owned-fixture\\book"+std::to_string(n)+".txt","Book "+std::to_string(n));}
void drain(a::UtilityExecutor&executor){for(unsigned n=0;n<4;++n){executor.waitIdle();executor.drain();}}
void latestAndLifetime(){
    const auto owner=std::this_thread::get_id();std::atomic<unsigned>validations{};a::UtilityExecutor executor([]{});Gate gate;
    std::vector<std::uint64_t>delivered;std::weak_ptr<int>firstLease,pendingLease,lastLease;
    n::ReaderImportQueue queue(executor,[&](auto generation,auto result,std::exception_ptr error){check(std::this_thread::get_id()==owner,"Import publishes only inside owner drain");check(!error&&result&&result->title=="Validated","Only fully validated result reaches callback");delivered.push_back(generation);},
        [&](m::ReaderBook candidate,const auto&,const m::ReaderCancel&cancel){check(std::this_thread::get_id()!=owner,"Validation stays off the owner UI thread");if(++validations==1)gate.work();m::checkReaderCancelled(cancel);candidate.title="Validated";return candidate;});
    check(!executor.stats().started&&!queue.busy(),"Constructing unused Reader import starts no worker");
    auto lease=std::make_shared<int>(1);firstLease=lease;queue.begin(1,book(1),{},lease);lease.reset();gate.wait();
    lease=std::make_shared<int>(2);pendingLease=lease;queue.begin(2,book(2),{},lease);lease.reset();
    lease=std::make_shared<int>(3);lastLease=lease;queue.begin(3,book(3),{},lease);lease.reset();
    check(!firstLease.expired()&&pendingLease.expired()&&!lastLease.expired(),"In-flight and latest shelf leases remain bounded; replaced pending lease is released");
    gate.release();drain(executor);
    check(validations==2&&delivered==std::vector<std::uint64_t>{3},"Rapid selection cancels old validation and imports only the latest choice");
    check(!queue.busy()&&firstLease.expired()&&lastLease.expired(),"All completed request leases release without persisting a file copy");
}
void capacityAndDestruction(){
    a::UtilityExecutor executor([]{},1);Gate gate;const auto other=executor.makeRoute();
    check(executor.submit(other,[&]{gate.work();},[](auto){}),"Occupy one existing utility slot");gate.wait();
    unsigned callbacks{};std::atomic<unsigned>validations{};
    auto queue=std::make_unique<n::ReaderImportQueue>(executor,[&](auto,auto,auto){++callbacks;},[&](auto value,const auto&,const auto&){++validations;return value;});
    queue->begin(1,book(1),{});check(queue->busy()&&validations==0,"Full executor preserves latest validation without another thread or timer");
    queue->cancel();gate.release();drain(executor);queue->queueCapacityAvailable();drain(executor);
    check(callbacks==0&&validations==0&&!queue->busy(),"Cancel before capacity returns never opens a document");
    Gate running;queue=std::make_unique<n::ReaderImportQueue>(executor,[&](auto,auto,auto){++callbacks;},[&](auto value,const auto&,const m::ReaderCancel&cancel){running.work();m::checkReaderCancelled(cancel);return value;});
    queue->begin(2,book(2),{});running.wait();queue.reset();running.release();drain(executor);
    check(callbacks==0,"Destroying the owner invalidates in-flight completion without touching UI");
}
void failuresAndReentry(){
    a::UtilityExecutor executor([]{});unsigned callbacks{},failures{};std::unique_ptr<n::ReaderImportQueue>queue;
    queue=std::make_unique<n::ReaderImportQueue>(executor,[&](auto generation,auto result,std::exception_ptr error){++callbacks;if(error){++failures;check(!result,"Failed validation never publishes an unvalidated reference");queue->begin(2,book(2),{});}else{check(generation==2&&result.has_value(),"Completion reentry accepts exactly one follow-up");queue.reset();}},
        [](auto value,const auto&,const auto&){if(value.title=="Book 1")throw m::ReaderError(m::ReaderErrorCode::invalidBook);return value;});
    queue->begin(1,book(1),{});drain(executor);check(callbacks==2&&failures==1&&!queue,"Completion may replace request or destroy owner safely");
}
}
int main(){try{latestAndLifetime();capacityAndDestruction();failuresAndReentry();std::cout<<"PASS "<<checks<<" Reader import shared-worker checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
