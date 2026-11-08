#include "modules/app_shortcut_tasks.hpp"
#include "core/data/file_io.hpp"
#include "core/migration/relink_ledger.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>

namespace m=endfield::modules;namespace n=endfield::native;namespace a=endfield::app;namespace mig=ehud::migration;
using J=m::ShortcutJson;
namespace {
unsigned checks{};
void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
template<class F>void rejects(F f){bool yes{};try{f();}catch(const std::exception&){yes=true;}check(yes,"Invalid request rejected before dispatch");}
struct Temp {
    std::filesystem::path path=std::filesystem::canonical(std::filesystem::temp_directory_path())/("ehud-shortcut-tasks-"+ehud::data::makeUUID());
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
auto rules(){return m::ShortcutTextRules{[](std::string_view s){return !s.empty()&&s.size()<=128&&J::validUtf8(s)&&s.find_first_of("\r\n")==s.npos;},[](std::string_view s){return std::string(s);}};}
struct OS {
    unsigned inspected{},launched{};bool fail{};std::thread::id worker;
    n::AppShortcutSelection selection{n::AppShortcutSelectionKind::file,"C:\\Apps\\Owned.exe"};
    n::AppShortcutOS ops(){return {[this](const auto&s){++inspected;worker=std::this_thread::get_id();if(fail)throw m::ShortcutError(m::ShortcutErrorCode::unavailable);n::AppShortcutResolved r;r.selection=s;r.name="Owned application";r.applicationKey="path:c:\\apps\\owned.exe";r.selectedFileKey="file:owned";r.executablePath=s.value;return r;},[this](const auto&,auto){++launched;return n::AppShortcutLaunchReceipt{42};}};}
};
void drain(a::UtilityExecutor&q,m::AppShortcutTasks&t){for(unsigned n=0;n<5&&t.busy();++n){q.waitIdle();q.drain();t.queueCapacityAvailable();}q.waitIdle();q.drain();check(!t.busy(),"One shared completion flushes bounded work");}
void run(){
    Temp temp;OS os;std::atomic<unsigned>notices{};a::UtilityExecutor q([&]{++notices;},1);
    const auto owner=std::this_thread::get_id();std::vector<m::ShortcutTaskResult>results;
    m::AppShortcutTasks tasks(q,temp.path,rules(),os.ops(),[&](auto r){check(std::this_thread::get_id()==owner,"Only shared owner drain updates UI");results.push_back(std::move(r));});
    check(!std::filesystem::exists(temp.path)&&!q.stats().started,"Construction does not read disk or start another worker");
    m::ShortcutTask load;load.token=1;load.kind=m::ShortcutTaskKind::load;check(tasks.submit(load),"Accept explicit initial load");check(!tasks.submit(load),"Only one owned request outstanding");drain(q,tasks);
    check(results.size()==1&&results.back().file&&results.back().file->items.empty()&&!results.back().error,"Empty isolated store loads without production data");
    rejects([&]{tasks.submit(load);});check(!tasks.cancel(0),"Zero cannot cancel an idle request");
    m::ShortcutTask inspect;inspect.token=2;inspect.kind=m::ShortcutTaskKind::inspect;inspect.selection=os.selection;tasks.submit(inspect);drain(q,tasks);
    check(os.worker!=owner&&os.inspected==1&&!os.launched&&results.back().candidate,"Inspection runs on shared worker and never launches");
    m::ShortcutTask save;save.token=3;save.kind=m::ShortcutTaskKind::save;save.candidate=results.back().candidate;save.name="中文 shortcut";save.icon="globe";save.newID=ehud::data::makeUUID();save.createdAt=123;
    tasks.submit(save);check(!tasks.cancel(3),"Accepted save cannot be lost by concealing its module");drain(q,tasks);
    check(results.back().file&&results.back().file->items.size()==1&&os.inspected==2&&!results.back().error,"Save reinspects and atomically commits exactly once");
    auto record=results.back().file->items[0];const auto initial=ehud::data::detail::readFile(temp.path/"shortcuts.json",m::shortcutMaximumBytes);
    save.token=4;save.editingID=record.id;save.name="rejected";os.fail=true;tasks.submit(save);drain(q,tasks);os.fail=false;
    check(results.back().error&&!results.back().file&&ehud::data::detail::readFile(temp.path/"shortcuts.json",m::shortcutMaximumBytes)==initial,"Failed reinspection leaves saved file and UI snapshot untouched");
    const auto before=results.size();inspect.token=5;tasks.submit(inspect);check(tasks.cancel(5),"Conceal invalidates obsolete inspection delivery");drain(q,tasks);check(results.size()==before,"Cancelled completion never reopens editor");
    m::ShortcutTask launch;launch.token=6;launch.kind=m::ShortcutTaskKind::launch;launch.record=record;tasks.submit(launch);check(!tasks.cancel(6),"Explicit close-first launch remains accepted");drain(q,tasks);check(os.launched==1&&results.back().launched->processID==42,"Only explicit launch reaches OS handoff");
    const auto blocker=q.makeRoute();q.submit(blocker,[]{},[](auto){});q.waitIdle();save.token=7;save.name="after capacity";tasks.submit(save);check(tasks.busy(),"Full shared queue retains one immutable save");q.drain();tasks.queueCapacityAvailable();drain(q,tasks);check(results.back().file->items[0].name==save.name,"Completion event retries capacity without timer");
    q.submit(blocker,[]{},[](auto){});q.waitIdle();inspect.token=8;tasks.submit(inspect);check(tasks.cancel(8)&&!tasks.busy(),"Unsubmitted disposable request cancels immediately");q.drain();q.invalidate(blocker);
    m::ShortcutTask remove;remove.token=9;remove.kind=m::ShortcutTaskKind::remove;remove.record=record;tasks.submit(remove);drain(q,tasks);check(results.back().file->items.empty(),"Remove publishes only committed list");
    const auto stable=q.stats().accepted;for(unsigned n=0;n<10000;++n)tasks.queueCapacityAvailable();check(q.stats().accepted==stable,"Idle creates no work or polling requests");
}
void destroyedSave(){
    Temp temp;OS os;a::UtilityExecutor q([]{});unsigned callbacks{};
    const auto candidate=n::NativeAppShortcutService(os.ops()).inspect(os.selection);
    {m::AppShortcutTasks tasks(q,temp.path,rules(),os.ops(),[&](auto){++callbacks;});m::ShortcutTask t;t.token=1;t.kind=m::ShortcutTaskKind::save;t.candidate=candidate;t.name="persist on shutdown";t.icon="grid";t.newID=ehud::data::makeUUID();tasks.submit(t);}
    q.waitIdle();q.drain();check(callbacks==0,"Destroyed owner receives no callback");
    const auto bytes=ehud::data::detail::readFile(temp.path/"shortcuts.json",m::shortcutMaximumBytes);check(bytes&&m::decodeShortcuts(J::parse(*bytes),rules()).items.size()==1,"Accepted queued save survives owner destruction");
}
}
// The Mac import ledger follows the committed file on the same worker: the
// load reports the listed row, relinking the imported record (edit, choose a
// Windows application, save) resolves it, and a broken ledger never fails a
// committed file.
void relinkLedger(){
    // The repository reads record ids in Foundation's upper case; the ledger
    // row keeps the export's lower case (rows match regardless of case).
    const std::string imported="ABCDEF00-0000-4000-8000-000000000001";
    const std::string archive=R"({"version":1,"items":[{"bookmark":"AQIDBA==","createdAt":812345678.125,"iconPreset":"star","id":"abcdef00-0000-4000-8000-000000000001","lastKnownPath":"/Synthetic/Mac.app","name":"Mac \u5e94\u7528","originalName":"Mac \u5e94\u7528","securityScoped":true}]})";
    mig::MacRelinkItem item;item.id=ehud::data::makeUUID();item.store=std::string(mig::relinkAppShortcut);item.recordID="abcdef00-0000-4000-8000-000000000001";item.displayName="Mac";
    item.lastKnownPath="/Synthetic/Mac.app";item.kind="app";item.macReference=J::Object{{"bookmark","AQIDBA=="},{"securityScoped",true},{"lastKnownPath","/Synthetic/Mac.app"}};
    const auto seed=[&](const std::filesystem::path&root,const std::string&ledger){
        ehud::data::detail::replaceFile(root/"AppShortcuts"/"shortcuts.json",std::nullopt,archive,m::shortcutMaximumBytes);
        ehud::data::detail::replaceFile(root/"Migration"/"relink.json",std::nullopt,ledger,mig::MacRelinkLedger::maximumBytes);};
    rejects([&]{OS os;a::UtilityExecutor q([]{});m::AppShortcutTasks bad(q,"C:\\Unused",rules(),os.ops(),[](auto){},std::filesystem::path("relative"));});
    {
        Temp temp;OS os;a::UtilityExecutor q([]{},1);std::vector<m::ShortcutTaskResult>results;seed(temp.path,mig::MacRelinkLedger::encode({item}));
        m::AppShortcutTasks tasks(q,temp.path/"AppShortcuts",rules(),os.ops(),[&](auto r){results.push_back(std::move(r));},temp.path);
        m::ShortcutTask load;load.token=1;load.kind=m::ShortcutTaskKind::load;tasks.submit(load);drain(q,tasks);
        check(results.back().file&&results.back().file->items.size()==1&&!results.back().file->items[0].native()&&results.back().relink==m::ShortcutRelinkSync{0,0,1}&&!results.back().relinkFailed,
              "The load reports the imported shortcut's listed ledger row");
        m::ShortcutTask inspect;inspect.token=2;inspect.kind=m::ShortcutTaskKind::inspect;inspect.selection=os.selection;tasks.submit(inspect);drain(q,tasks);
        check(results.back().candidate&&!results.back().relink,"Inspection does no ledger work");
        m::ShortcutTask save;save.token=3;save.kind=m::ShortcutTaskKind::save;save.candidate=results.back().candidate;save.editingID=imported;save.name="Mac \xe5\xba\x94\xe7\x94\xa8";save.icon="star";
        save.newID=ehud::data::makeUUID();save.createdAt=5;tasks.submit(save);drain(q,tasks);
        const auto&relinked=results.back().file->items.at(0);
        check(relinked.id==imported&&relinked.native()&&relinked.iconPreset=="star"&&relinked.createdAt==812345678.125&&results.back().relink==m::ShortcutRelinkSync{1,0,0},
              "Relinking keeps the record's identity and resolves its ledger row");
        {const mig::MacRelinkLedger ledger(temp.path);const auto*row=ledger.find(item.id);
         check(row&&row->state==mig::RelinkState::resolved&&row->windowsPath==std::optional<std::string>("C:\\Apps\\Owned.exe"),"The ledger row names the chosen Windows application");}
        save.token=4;save.name="Renamed";tasks.submit(save);drain(q,tasks);
        check(results.back().file->items.at(0).name=="Renamed"&&!results.back().relink,"Once no shortcut row is listed, later writes do no ledger work");
    }
    {
        Temp temp;OS os;a::UtilityExecutor q([]{},1);std::vector<m::ShortcutTaskResult>results;seed(temp.path,"{\"version\":1,\"items\":[");
        m::AppShortcutTasks tasks(q,temp.path/"AppShortcuts",rules(),os.ops(),[&](auto r){results.push_back(std::move(r));},temp.path);
        m::ShortcutTask load;load.token=1;load.kind=m::ShortcutTaskKind::load;tasks.submit(load);drain(q,tasks);
        check(results.back().file&&!results.back().error&&results.back().relinkFailed&&!results.back().relink,"A broken ledger never fails the load");
        m::ShortcutTask remove;remove.token=2;remove.kind=m::ShortcutTaskKind::remove;remove.record=results.back().file->items.at(0);tasks.submit(remove);drain(q,tasks);
        check(results.back().file&&results.back().file->items.empty()&&results.back().relinkFailed&&
              ehud::data::detail::readFile(temp.path/"Migration"/"relink.json",mig::MacRelinkLedger::maximumBytes)==std::optional<std::string>("{\"version\":1,\"items\":["),
              "A committed remove stands and the broken ledger is kept as it is");
    }
}
// AppShortcutStore.move(id:to:) on the one worker (Mac eebdf4c): one write
// with a clamped index; none for the same slot or an unknown record; a file
// changed on disk is refused and kept.
void moves(){
    const auto row=[](unsigned n){char id[40];std::snprintf(id,sizeof(id),"ABCDEF00-0000-4000-8000-%012u",n);
        return std::string(R"({"bookmark":"AQIDBA==","createdAt":1,"iconPreset":"star","id":")")+id+R"(","lastKnownPath":"/Synthetic/A)"+std::to_string(n)+R"(.app","name":"A)"+std::to_string(n)+R"(","originalName":"A)"+std::to_string(n)+R"(","securityScoped":true})";};
    const std::string archive="{\"version\":1,\"items\":["+row(1)+","+row(2)+","+row(3)+"]}";
    Temp temp;OS os;a::UtilityExecutor q([]{},1);std::vector<m::ShortcutTaskResult>results;
    const auto path=temp.path/"shortcuts.json";
    ehud::data::detail::replaceFile(path,std::nullopt,archive,m::shortcutMaximumBytes);
    m::AppShortcutTasks tasks(q,temp.path,rules(),os.ops(),[&](auto r){results.push_back(std::move(r));});
    m::ShortcutTask load;load.token=1;load.kind=m::ShortcutTaskKind::load;tasks.submit(load);drain(q,tasks);
    const auto items=results.back().file->items;check(items.size()==3,"Three synthetic shortcuts load");
    const auto names=[&]{std::string out;for(const auto&v:results.back().file->items)out+=v.name+" ";return out;};
    m::ShortcutTask move;move.kind=m::ShortcutTaskKind::move;
    rejects([&]{m::ShortcutTask bare;bare.token=2;bare.kind=m::ShortcutTaskKind::move;tasks.submit(bare);});
    move.token=3;move.record=items[1];move.index=1;tasks.submit(move);drain(q,tasks);
    check(results.back().file&&!results.back().error&&names()=="A1 A2 A3 "&&ehud::data::detail::readFile(path,m::shortcutMaximumBytes)==std::optional<std::string>(archive),
          "The same slot answers the list and writes nothing");
    move.token=4;move.record=items[0];move.index=9;tasks.submit(move);drain(q,tasks);
    check(names()=="A2 A3 A1 "&&!results.back().relink,"A move past the end clamps to the last slot");
    const auto written=ehud::data::detail::readFile(path,m::shortcutMaximumBytes);
    check(written&&m::decodeShortcuts(J::parse(*written),rules()).items.back().name=="A1","The move is one committed write in the right-hand order");
    auto unknown=items[0];unknown.id="ABCDEF00-0000-4000-8000-000000000099";
    move.token=5;move.record=unknown;move.index=0;tasks.submit(move);drain(q,tasks);
    check(names()=="A2 A3 A1 "&&ehud::data::detail::readFile(path,m::shortcutMaximumBytes)==written,"An unknown shortcut moves nothing");
    std::filesystem::remove(path);ehud::data::detail::replaceFile(path,std::nullopt,archive,m::shortcutMaximumBytes);
    move.token=6;move.record=items[2];move.index=0;tasks.submit(move);drain(q,tasks);
    check(results.back().error&&results.back().code==m::ShortcutErrorCode::changedOnDisk&&!results.back().file&&
          ehud::data::detail::readFile(path,m::shortcutMaximumBytes)==std::optional<std::string>(archive),"A file changed on disk refuses the move and stays as it is");
}
int main(){try{run();destroyedSave();relinkLedger();moves();std::cout<<"App shortcut transactions: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
