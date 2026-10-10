#include "modules/app_shortcut_tasks.hpp"
#include "core/data/file_io.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>

namespace m=endfield::modules;namespace n=endfield::native;namespace a=endfield::app;
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
int main(){try{run();destroyedSave();std::cout<<"App shortcut transactions: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
