#include "app/event_log_save_queue.hpp"
#include "core/data/file_io.hpp"
#include <iostream>
#include <stdexcept>

namespace app=endfield::app;namespace native=endfield::native;namespace data=ehud::data;namespace mod=endfield::modules;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
struct Temporary {
    std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("Endfield-event-save-"+data::makeUUID());
    Temporary(){check(std::filesystem::create_directory(root),"Create fresh synthetic store");}
    ~Temporary(){std::error_code error;std::filesystem::remove_all(root,error);}
};
mod::SystemEvent event(unsigned n){return {data::makeUUID(),mod::EventKind::clipboardCopied,800000000.+n,{{"kind","text"},{"body","PRIVATE"}}};}
void run(){
    Temporary temp;double now=1;unsigned changes{},callbacks{};app::UtilityExecutor executor([]{},1);
    native::EventLogOwner owner(temp.root,{[&]{return now;},[&]{++changes;}});app::EventLogSaveQueue saves(owner,executor);
    const auto other=executor.makeRoute();check(executor.submit(other,[]{},[&](auto){++callbacks;}),"Other module fills the one shared slot");
    owner.record(event(1));now=1.4;saves.capture(now);check(saves.pending()&&!owner.saveDeadline(),"Saturated queue retains the due immutable snapshot without a retry timer");
    now=2;owner.record(event(2));now=2.4;saves.capture(now);check(saves.pending(),"Later event replaces the unaccepted snapshot");
    now=3;saves.flush(now);check(!saves.pending()&&callbacks==1,"Shutdown flush waits once for shared capacity and admits latest save");
    check(changes==2&&!owner.snapshot().status,"Successful file completion causes no duplicate UI refresh");
    data::EventLogStore loaded(temp.root);check(loaded.events().size()==2,"Latest accepted snapshot stores both events");
    const auto bytes=data::detail::readFile(temp.root/"EventLog"/"events.json",data::EventLogStore::maximumArchiveBytes);check(bytes&&bytes->find("PRIVATE")==std::string::npos,"Worker snapshot contains sanitized metadata only");
    Temporary late;{
        auto transient=std::make_unique<native::EventLogOwner>(late.root,native::EventLogOwnerCallbacks{[&]{return now;},[]{}});
        {app::EventLogSaveQueue queue(*transient,executor);transient->record(event(3));queue.capture(now,true);}
        transient.reset();
    }
    executor.waitIdle();executor.drain();data::EventLogStore reopened(late.root);check(reopened.events().size()==1,"Accepted immutable write outlives its destroyed UI binding and owner");
}
// Application::Impl::onDeadline runs the owners' deadline() with the wake time
// (Media Assembly's debounced "edited", Work Mode completion, ...: they record
// at the owner clock, a moment later), then eventSaves->capture(wake). The RC
// ended there with exit 1 ("Event persistence requires finite monotonic owner
// time"). Two runs share the data root, like the user's relaunch.
void hostDeadlineOrder(){
    Temporary temp;app::UtilityExecutor executor([]{});
    for(unsigned run=0;run<2;++run){
        double now=run?2:5000;
        native::EventLogOwner owner(temp.root,{[&]{return now;},[]{}});app::EventLogSaveQueue saves(owner,executor);
        owner.record(event(10*run+1));now+=.4;saves.capture(now);executor.waitIdle();executor.drain();
        const double wake=now+=1;now+=1e-6;owner.record(event(10*run+2));saves.capture(wake);
        check(!saves.pending()&&owner.saveDeadline(),"A capture with a wake time sampled before an in-callback record neither throws nor saves early");
        now+=.5;saves.capture(now);executor.waitIdle();executor.drain();
        check(!saves.pending()&&!owner.snapshot().status&&data::EventLogStore(temp.root).events().size()==2*(run+1),"The next wake saves the in-callback event, in both runs on the same root");
    }
}
}
int main(){try{run();hostDeadlineOrder();std::cout<<"Event Log save binding: "<<checks<<" checks passed; temporary stores only\n";return 0;}catch(const std::exception&e){std::cerr<<"Event Log save binding failed: "<<e.what()<<'\n';return 1;}}
