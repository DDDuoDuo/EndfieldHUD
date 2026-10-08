#include "native/event_log_owner.hpp"
#include "core/data/data_store.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

namespace n=endfield::native;namespace m=endfield::modules;namespace d=ehud::data;
namespace {
std::size_t checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&&fn,const char*why){bool failed{};try{fn();}catch(const std::exception&){failed=true;}check(failed,why);}
struct Temporary {
    std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("Endfield-event-owner-"+d::makeUUID());
    Temporary(){check(std::filesystem::create_directory(root),"Create explicit synthetic owner root");}
    ~Temporary(){std::error_code error;std::filesystem::remove_all(root,error);}
    auto path()const{return root/"EventLog"/"events.json";}
};
std::string id(unsigned n){const auto tail=std::to_string(n);return "00000000-0000-4000-8000-"+std::string(12-tail.size(),'0')+tail;}
m::SystemEvent event(unsigned number){return {id(number),m::EventKind::audioDeviceConnected,812000000.+number,{{"device","  测试扬声器\n 设备 "},{"privateBody","NEVER_SERIALIZE"}}};}
struct Executor {
    std::optional<d::EventLogWrite>accepted;
    bool trySubmit(std::optional<d::EventLogWrite>&pending){if(accepted||!pending)return false;accepted=std::move(pending);pending.reset();return true;}
    d::EventLogSaveResult run(){check(bool(accepted),"Fake executor has one explicitly accepted request");d::EventLogSaveResult result;
        // One test-owned thread proves that only immutable sanitized bytes and
        // writer state cross the boundary. There is no persistent worker/loop.
        std::thread task([&]{result=accepted->execute();});task.join();accepted.reset();return result;}
};
void lifecycle(){
    Temporary temp;double time=10;unsigned notifications{},clockReads{};n::EventLogOwner owner(temp.root,{[&]{++clockReads;return time;},[&]{++notifications;}});
    auto callbacks=owner.callbacks([](double value){return value==812000001.?"10-08 12:34:56":"synthetic time";});
    m::EventLogState state(callbacks,{},owner.nameCompactor());check(owner.snapshot().events.empty()&&!owner.saveDeadline()&&!std::filesystem::exists(temp.path()),"Construction starts no event collection or write");
    owner.record(event(1));check(notifications==1&&clockReads==1&&std::abs(*owner.saveDeadline()-10.35)<1e-12,"Content change requests one original .35-second root deadline");
    check(state.itemCount()==0,"Inactive module is not read or rendered by store notifications");state.activate();check(state.itemCount()==1&&state.visibleRows().front().timestamp=="10-08 12:34:56","Existing EventLogState reads owner snapshot only on activation");
    const auto before=clockReads;for(unsigned k=0;k<100;++k){(void)owner.snapshot();(void)owner.saveDeadline();(void)owner.revision();}check(clockReads==before&&!std::filesystem::exists(temp.path()),"Snapshot/deadline queries perform no clock read or write");
    check(owner.snapshot().events.front().metadata==m::EventMetadata{{"device","测试扬声器 设备"}},"Native Unicode compactor is injected before storing private metadata");
    check(!owner.takeSave(10.2),"Shared root wake before deadline produces no job");time=10.35;auto pending=owner.takeSave(time);check(pending&&!owner.saveDeadline()&&!std::filesystem::exists(temp.path()),"Due owner request is immutable and performs no UI file write");
    Executor executor;check(executor.trySubmit(pending)&&!pending,"Root accepts the owned immutable task exactly once");const auto result=executor.run();check(result.success&&notifications==1,"Worker completion does not invoke UI callbacks");check(!owner.complete(result)&&notifications==1,"Successful current save causes no unnecessary repaint notification");
    std::ifstream in(temp.path(),std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(in)),{});in.close();check(bytes.find("NEVER_SERIALIZE")==std::string::npos&&bytes.find("测试扬声器")!=std::string::npos,"Only sanitized Unicode event metadata reaches the temporary archive");
    state.deactivate();time=11;owner.record(event(2));auto hidden=owner.takeSave(11.35);check(hidden.has_value(),"Hidden change reaches original save deadline");check(hidden->execute().success,"Hidden module persists after diagnostic archive reader releases its handle");check(state.itemCount()==1,"Hidden module persists without refreshing its UI snapshot");
    time=12;state.activate();state.perform("eventLog:clear");check(state.confirmingClear(),"Clear remains source confirmation before store mutation");state.perform("eventLog:confirmClear");check(owner.snapshot().events.empty()&&state.itemCount()==0&&notifications==3,"Confirmed clear persists through owner without logging itself");
    check(owner.revision()==3&&std::abs(*owner.saveDeadline()-12.35)<1e-12,"Clear uses current owner epoch and source revision");
    time=13;callbacks.clear();check(owner.revision()==4&&notifications==4,"Explicit empty clear is still a source change");
    time=std::numeric_limits<double>::quiet_NaN();rejects([&]{owner.record(event(3));},"Invalid injected clock rejects before model change");check(owner.revision()==4&&notifications==4,"Invalid event leaves owner and notifications unchanged");
}
void saturatedQueue(){
    Temporary temp;double time=1;unsigned notifications{};n::EventLogOwner owner(temp.root,{[&]{return time;},[&]{++notifications;}});Executor executor;
    owner.record(event(1));auto pending=owner.takeSave(time,true);check(executor.trySubmit(pending),"First save accepted by bounded root queue");
    time=2;owner.record(event(2));pending=owner.takeSave(time,true);check(!executor.trySubmit(pending)&&pending&&pending->revision()==2,"Saturated queue does not consume the latest unaccepted snapshot");
    time=3;owner.record(event(3));pending=owner.takeSave(time,true);check(pending&&pending->revision()==3,"Unaccepted save can coalesce to the latest original revision");
    const auto old=executor.run();check(!owner.complete(old)&&notifications==3,"Older successful completion does not notify latest UI state");
    check(executor.trySubmit(pending),"Pending latest save is accepted after queue capacity opens");check(!owner.complete(executor.run())&&notifications==3,"Latest save completes without extra model event");
    n::EventLogOwner reloaded(temp.root,{[]{return 0.;},{}});check(reloaded.snapshot().events.size()==3&&reloaded.snapshot().events.front().id==id(3),"Queue saturation/coalescing preserves all latest bounded events");
}
void statusAndRoutes(){
    Temporary temp;double time=1;unsigned notifications{};auto owner=std::make_unique<n::EventLogOwner>(temp.root,n::EventLogOwnerCallbacks{[&]{return time;},[&]{++notifications;}});
    auto callbacks=owner->callbacks([](double){return "time";});auto compact=owner->nameCompactor();owner->record(event(1));auto request=owner->takeSave(time,true);
    std::ofstream block(temp.root/"EventLog");block<<"synthetic blocker";block.close();const auto failed=request->execute();check(!failed.success&&owner->complete(failed)&&notifications==2&&owner->snapshot().status,"Owner routes current save failure into source status exactly once");
    check(!owner->complete(failed)&&notifications==2,"Repeated failure completion does not churn UI");std::filesystem::remove(temp.root/"EventLog");check(request->execute().success,"Immutable failed task may retry on root executor");check(owner->complete({owner->revision(),true})&&notifications==3&&!owner->snapshot().status,"Recovered current save clears status");
    time=2;owner->record(event(2));request=owner->takeSave(time,true);owner.reset();check(callbacks.snapshot().events.empty()&&compact("private").empty(),"Destroyed owner invalidates snapshot and compactor routes");callbacks.clear();check(notifications==4,"Destroyed clear route cannot call stale UI or clock");check(request->execute().success,"Accepted final write safely outlives store/UI owner");
    n::EventLogOwner restored(temp.root,{[]{return 0.;},{}});check(restored.snapshot().events.size()==2,"Detached final snapshot preserves newest events");
    bool rejected{};std::thread wrongThread([&]{try{(void)restored.snapshot();}catch(const std::invalid_argument&){rejected=true;}});wrongThread.join();check(rejected,"Owner forbids worker access to mutable ICU/store/UI state");
}
void reentrantTeardown(){
    unsigned notices{};std::unique_ptr<n::EventLogOwner>owner;
    owner=std::make_unique<n::EventLogOwner>(std::nullopt,n::EventLogOwnerCallbacks{[]{return 1.;},[&]{++notices;owner.reset();}});
    auto callbacks=owner->callbacks([](double){return "time";});owner->record(event(1));check(!owner&&notices==1&&callbacks.snapshot().events.empty(),"Notification may destroy owner without retaining a callable dangling route");
    rejects([]{n::EventLogOwner invalid(std::nullopt,{});},"Missing caller clock rejects at setup");
}
}
int main(){try{lifecycle();saturatedQueue();statusAndRoutes();reentrantTeardown();std::cout<<"Event Log owner: "<<checks<<" checks passed (temporary roots, injected events/executor)\n";return 0;}catch(const std::exception&e){std::cerr<<"Event Log owner failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
