#include "app/latest_file_save.hpp"
#include "core/data/file_io.hpp"
#include "core/data/map_store.hpp"
#include "core/data/notes_centre_store.hpp"
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

namespace app=endfield::app;namespace data=ehud::data;namespace m=endfield::modules;
namespace {
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Temporary {
    std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("Endfield-latest-save-"+data::makeUUID());
    Temporary(){std::filesystem::create_directories(root);}
    ~Temporary(){std::error_code e;std::filesystem::remove_all(root,e);}
    std::filesystem::path file()const{return root/"Notes"/"centre.json";}
};
std::optional<std::string> read(const std::filesystem::path&p){return data::detail::readFile(p,1<<20);}
void write(const std::filesystem::path&p,std::string_view s){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(s.data(),static_cast<std::streamsize>(s.size()));check(bool(f),"Write only owned temporary fixture");}
std::string reason(std::exception_ptr e){try{std::rethrow_exception(e);}catch(const std::exception&x){return x.what();}}
// Holds the executor's one worker until released (the saves queue behind it).
struct Hold {
    std::promise<void> gate;std::shared_future<void> open{gate.get_future().share()};
    Hold(app::UtilityExecutor&e,app::UtilityExecutor::Route r){check(e.submit(r,[f=open]{f.wait();},[](auto){}),"The holding task is accepted");}
    void release(){gate.set_value();}
};

// The last write wins in order: one save runs, the latest waits (earlier waiting ones are superseded).
void orderAndCoalesce(){
    Temporary t;app::UtilityExecutor executor([]{},8);const auto other=executor.makeRoute();unsigned failures{};
    app::LatestFileSave saves(executor,t.file(),1<<20,std::nullopt,[&](auto){++failures;});
    Hold hold(executor,other);
    saves.save("first");saves.save("second");saves.save("third");
    check(!saves.idle()&&saves.stats().queued==1&&!read(t.file()),"One save runs at a time, none on this thread");
    hold.release();executor.waitIdle();check(read(t.file())==std::optional<std::string>("first"),"The running save writes the bytes it took");
    executor.drain();executor.waitIdle();executor.drain();
    check(read(t.file())==std::optional<std::string>("third")&&saves.onDisk()==std::optional<std::string>("third")&&saves.idle(),"The latest bytes are written next; the superseded ones never are");
    check(saves.stats().queued==2&&saves.stats().written==2&&saves.stats().atOnce==0&&failures==0,"Two saves for three changes, all on the worker");
    saves.save("fourth");saves.flush();check(saves.idle()&&read(t.file())==std::optional<std::string>("fourth"),"flush returns once the file holds the latest bytes");
}
// A full executor saves at once, in order (nothing of this file runs then).
void fullQueue(){
    Temporary t;app::UtilityExecutor executor([]{},1);const auto other=executor.makeRoute();
    app::LatestFileSave saves(executor,t.file(),1<<20,std::nullopt);
    check(executor.submit(other,[]{},[](auto){}),"Another module takes the only slot");executor.waitIdle();
    saves.save("at once");check(read(t.file())==std::optional<std::string>("at once")&&saves.stats().atOnce==1&&saves.stats().queued==0&&saves.idle(),"A save the executor cannot take is written here");
    executor.drain();saves.save("queued");check(saves.stats().queued==1,"A free slot takes the next save");saves.flush();
    check(read(t.file())==std::optional<std::string>("queued"),"It expects the bytes the save at once wrote");
}
// A failed save reports the store's reason on the owner thread; the next one
// again expects the bytes last known on disk (or nothing waits: discardWaiting).
void failure(){
    Temporary t;app::UtilityExecutor executor([]{},8);const auto owner=std::this_thread::get_id();std::vector<std::string> reasons;bool discard{};
    std::unique_ptr<app::LatestFileSave> saves;
    saves=std::make_unique<app::LatestFileSave>(executor,t.file(),1<<20,std::nullopt,[&](std::exception_ptr e){check(std::this_thread::get_id()==owner,"Failures report on the owner thread");reasons.push_back(reason(e));if(discard)saves->discardWaiting();});
    saves->save("ours");saves->flush();
    write(t.file(),"another instance");saves->save("lost");saves->flush();
    check(reasons.size()==1&&reasons[0]=="Saved data changed in another instance","The synchronous save's reason, unchanged");
    check(read(t.file())==std::optional<std::string>("another instance")&&saves->onDisk()==std::optional<std::string>("ours")&&saves->stats().failed==1,"The file and the bytes known on disk stay");
    write(t.file(),"ours");saves->save("again");saves->flush();check(read(t.file())==std::optional<std::string>("again")&&reasons.size()==1,"The next save expects the bytes last known on disk");
    // A waiting save after a failure: written (Notes keeps its change), or dropped (the Map returns to the disk).
    for(const bool drop:{false,true}){
        discard=drop;write(t.file(),"another instance");reasons.clear();
        {const auto other=executor.makeRoute();Hold hold(executor,other);saves->save("fails");saves->save("waits");hold.release();}
        executor.waitIdle();executor.drain();executor.waitIdle();executor.drain();
        if(!drop)check(reasons.size()==2&&read(t.file())==std::optional<std::string>("another instance"),"The waiting save expects the same bytes and fails as a synchronous one would");
        else check(reasons.size()==1&&saves->idle()&&saves->stats().queued>0,"A handler that returned its store to the disk drops what waits");
        write(t.file(),"again");saves->flush();
    }
}
// Destruction waits for the running save and writes the waiting bytes here, without callbacks;
// a completion that arrives after the owner is gone runs nothing.
void destruction(){
    Temporary t;app::UtilityExecutor executor([]{},8);const auto other=executor.makeRoute();unsigned callbacks{};
    std::promise<void> gate;auto open=gate.get_future().share();std::thread opener;
    {
        app::LatestFileSave saves(executor,t.file(),1<<20,std::nullopt,[&](auto){++callbacks;});
        check(executor.submit(other,[open]{open.wait();},[](auto){}),"The holding task is accepted");
        saves.save("running");saves.save("waiting");
        // ~LatestFileSave waits for the running save (released meanwhile), then writes the waiting bytes at once.
        opener=std::thread([&]{std::this_thread::sleep_for(std::chrono::milliseconds(30));gate.set_value();});
    }
    opener.join();
    check(read(t.file())==std::optional<std::string>("waiting"),"Nothing handed is lost when the owner goes");
    executor.drain();check(callbacks==0,"No callback reaches a destroyed owner");
    {
        app::LatestFileSave saves(executor,t.file(),1<<20,std::optional<std::string>("stale"),[&](auto){++callbacks;});
        saves.save("fails");executor.waitIdle();
    }
    executor.drain();check(callbacks==0&&read(t.file())==std::optional<std::string>("waiting"),"A failure after the owner is gone reports nothing and writes nothing");
}
// A completion the host withholds (its module failed) still lets the barrier end, on the worker's word.
void withheld(){
    Temporary t;app::UtilityExecutor executor([]{},8);int tag{};unsigned callbacks{};
    executor.attribute([&]{return static_cast<app::UtilityExecutor::Attribution>(&tag);},[](auto,const std::function<void()>&){});
    app::LatestFileSave saves(executor,t.file(),1<<20,std::nullopt,[&](auto){++callbacks;});
    saves.save("one");saves.save("two");saves.flush();
    check(saves.idle()&&read(t.file())==std::optional<std::string>("two")&&saves.stats().written==2,"flush settles withheld completions and writes what waits");
}
// The stores hand their encoded files over and publish at once; the Map goes back to the disk on a failure.
void stores(){
    Temporary t;app::UtilityExecutor executor([]{},8);
    {
        data::NotesCentreStore store(t.root);std::vector<std::string> reasons;
        app::LatestFileSave saves(executor,*store.path(),data::notesCentreMaximumBytes,store.persisted(),[&](auto e){reasons.push_back(reason(e));});
        store.setWriter([&](std::string bytes){saves.save(std::move(bytes));});
        check(store.update([](auto&d){d.tasks.push_back({data::makeUUID(),"A",false});})&&store.document().tasks.size()==1,"An edit changes the document at once");
        check(store.writes().handed==1&&store.writes().atOnce==0&&!read(*store.path()),"and hands the file to the writer");
        check(store.update([](auto&d){d.isFolded=true;}),"A second edit");saves.flush();
        check(read(*store.path())==std::optional<std::string>(data::encodeNotesCentre(store.document())),"The file holds the latest document");
        check(data::NotesCentreStore(t.root).document()==store.document(),"A fresh read sees the write");
        write(*store.path(),"{}");check(store.update([](auto&d){d.tab=data::NotesCentreDocument::Tab::text;}),"An edit whose write fails");saves.flush();
        check(reasons.size()==1&&reasons[0]=="Saved data changed in another instance"&&store.document().tab==data::NotesCentreDocument::Tab::text,"keeps its change and reports the store's reason");
    }
    {
        data::MapStore store(t.root);m::MapState state(store.value(),{[&](const m::MapSnapshot&s){store.replace(s);},[]{return data::makeUUID();},[]{return 800000000.;}});
        std::unique_ptr<app::LatestFileSave> saves;std::vector<std::string> reasons;
        saves=std::make_unique<app::LatestFileSave>(executor,store.path(),data::MapStore::maximumArchiveBytes,store.persisted(),[&](std::exception_ptr e){
            saves->discardWaiting();store.restore(saves->onDisk());const auto change=state.restore(store.value());check(change.pinsChanged,"The pins follow the disk");reasons.push_back(reason(e));});
        store.setWriter([&](std::string bytes){saves->save(std::move(bytes));});
        auto change=state.addPin({430,430});check(change.pinsChanged&&!change.error&&state.pins().size()==1&&state.selection(),"A pin shows at once");
        check(store.writes().handed==1&&store.writes().atOnce==0,"Its archive goes to the writer");saves->flush();
        check(data::MapStore(t.root).value()==state.persisted(),"A fresh read sees the pin");
        const auto saved=*read(store.path());write(store.path(),"{\"external\":true}");
        change=state.addPin({300,300});check(state.pins().size()==2,"A second pin shows at once");saves->flush();
        check(reasons.size()==1&&reasons[0]=="Saved data changed in another instance","Its failed save reports the store's reason");
        check(state.pins().size()==1&&store.value()==state.persisted()&&!state.selection(),"and the map returns to the archive on disk (the pin and its selection go)");
        write(store.path(),saved);change=state.addPin({300,300});saves->flush();check(reasons.size()==1&&data::MapStore(t.root).value().pins.size()==2,"A later pin saves against the archive on disk");
    }
}
}
int main(){try{orderAndCoalesce();fullQueue();failure();destruction();withheld();stores();std::cout<<"PASS "<<checks<<" latest-file-save checks; temporary fixtures only\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
