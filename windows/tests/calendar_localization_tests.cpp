// Calendar display-time localization against the unchanged Mac sources
// (tools/calendar_localization_reference.py -> calendar-localization-source.json).
// Portable; synthetic in-memory repository only, no file, notification or OS API.
#include "modules/calendar_localization.hpp"
#include "modules/calendar_state.hpp"
#include <array>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
namespace {
using namespace endfield;namespace mod=modules;using J=mod::CalendarJson;using L=core::Language;unsigned checks{};
void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
constexpr std::array<std::pair<L,const char*>,5>languages{{{L::english,"english"},{L::simplifiedChinese,"simplifiedChinese"},{L::traditionalChinese,"traditionalChinese"},{L::japanese,"japanese"},{L::korean,"korean"}}};
std::string replaced(std::string text,std::string_view from,std::string_view to){const auto at=text.find(from);check(at!=std::string::npos,"Mac permission wording names the settings location once");text.replace(at,from.size(),to);check(text.find(from)==std::string::npos,"Settings location appears exactly once");return text;}
void source(const J&root){
    check(root["authority"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1"&&root["errors"].array().size()==7,"Fixture holds every HUDCalendarError case of the pinned Mac source");
    for(std::size_t n=0;n<7;++n){const auto&row=root["errors"].array()[n];const auto code=static_cast<mod::CalendarErrorCode>(n);const auto pair=mod::calendarErrorText(code);
        check(pair.english==row["english"].string()&&pair.simplified==row["simplifiedChinese"].string(),"CalendarErrorCode order and source pair match HUDCalendarError.errorDescription");
        check(mod::CalendarError(code).what()==row["english"].string(),"CalendarError keeps the English source text in what()");
        for(const auto&[language,key]:languages)check(mod::calendarErrorMessage(code,language)==row[key].string(),"Typed Calendar errors render the Mac catalog row in all five languages");
        check(mod::calendarErrorMessage(code,L::system)==row["english"].string(),"Unresolved System renders the English source like core::localized");}
    const auto&unavailable=root["permission"]["unavailable"];const auto&denied=root["permission"]["denied"];
    // Only the settings location changes (Chinese spaces the Latin product
    // name); the rest of each Mac sentence is kept.
    const std::array<std::array<std::string_view,2>,5>location{{{"System Settings","Windows Settings"},{"系统设置"," Windows 设置"},{"系統設定"," Windows 設定"},{"システム設定","Windows の設定"},{"시스템 설정","Windows 설정"}}};
    for(std::size_t n=0;n<languages.size();++n){const auto&[language,key]=languages[n];const auto strings=mod::calendarStrings(language);
        check(strings.unavailable==unavailable[key].string(),"Unavailable notifications keep the Mac caption in all five languages");
        check(strings.denied==mod::calendarNotificationsOff(language)&&strings.denied!=denied[key].string(),"Denied notifications use Windows wording, never macOS System Settings");
        check(strings.denied==replaced(denied[key].string(),location[n][0],location[n][1]),"Windows denied wording is the Mac sentence with only the settings location replaced");}
    auto english=mod::calendarStrings(L::english);english.denied=mod::CalendarStrings{}.denied;check(english==mod::CalendarStrings{},"English captions other than the Windows permission wording stay the Mac defaults");
    auto chinese=mod::calendarStrings(L::simplifiedChinese);chinese.denied=mod::CalendarStrings::simplifiedChinese().denied;check(chinese==mod::CalendarStrings::simplifiedChinese(),"Simplified Chinese captions stay the Mac source pairs");
    const auto japanese=mod::calendarStrings(L::japanese);check(japanese.today!="Today"&&japanese.refreshReminders!="Refresh reminders"&&japanese.deleteQuestion!="Delete this event?","Japanese captions resolve through the shared catalog rather than English fallback");
}
void accessibility(const J&root){
    const auto&ax=root["accessibility"];
    for(const auto&[language,key]:languages){const auto strings=mod::calendarStrings(language);
        mod::CalendarCanvasInput canvas;canvas.strings=strings;canvas.today=canvas.selected={2026,10,4};canvas.month={2026,10,1};canvas.monthHeading="October 2026";
        mod::CalendarEvent event;event.id="A0000000-0000-4000-8000-000000000001";event.title="Owned synthetic title";event.day={2026,10,4};canvas.events={event};
        const auto art=mod::prepareCalendarCanvas(canvas);const auto items=mod::calendarCanvasAccessibility(art.actions,strings);check(items.size()==art.actions.size(),"One accessible button per source canvas action");
        unsigned days{},named{},events{};
        for(std::size_t n=0;n<items.size();++n){const auto&a=art.actions[n];const auto&e=items[n];check(e.id==a.id&&e.rect==a.rect&&e.enabled==a.enabled&&e.role==mod::CalendarAccessibleRole::button,"Accessible canvas buttons keep the source id, rectangle and enabled state");
            if(!ax["canvas"][a.id].isNull()){++named;check(e.label==ax["canvas"][a.id][key].string(),"Named canvas controls read the Mac AX label in all five languages");}
            else if(a.id.starts_with("day:")){++days;check(e.label==a.id.substr(4)&&e.label.size()==10,"Day cells read their YYYY-MM-DD date like the Mac AX button");}
            else{events+=a.id.starts_with("event:");check(e.label==a.label,"Other canvas controls read their own source label");}}
        check(named==4&&days==31&&events==1,"Month arrows, add, refresh, 31 October days and the event row are exposed");
        for(bool deleting:{false,true})for(bool editing:{false,true}){if(deleting&&!editing)continue;mod::CalendarMenuInput menu;menu.strings=strings;menu.editing=editing;menu.deleting=deleting;const auto face=mod::prepareCalendarMenu(menu);const auto list=mod::calendarMenuAccessibility(face.actions,strings,language);
            check(list.size()==face.actions.size()+3,"Menu buttons plus the three source text fields");
            for(std::size_t n=0;n<face.actions.size();++n){const auto&a=face.actions[n];check(list[n].id==a.id&&list[n].rect==a.rect&&list[n].enabled==a.enabled&&!ax["menu"][a.id].isNull()&&list[n].label==ax["menu"][a.id][key].string(),"Menu buttons read the Mac CalendarEventMenu/NotesRetainedMenu AX labels");}
            const std::array<const char*,3>fields{"title","date","details"};for(std::size_t n=0;n<3;++n){const auto&f=list[face.actions.size()+n];check(f.role==mod::CalendarAccessibleRole::textField&&f.label==ax["fields"][fields[n]][key].string()&&f.enabled,"Editor fields read the Mac placeholder labels");}
            check(list[face.actions.size()].rect==core::Rect{14,49,312,30}&&list[face.actions.size()+2].rect==core::Rect{14,149,312,86},"Editor field rectangles are the source menu-local frames");}
    }
}
struct Repository final:mod::CalendarRepository {mod::CalendarFile value;std::exception_ptr failure;bool exists()override{return true;}mod::CalendarFile load()override{if(failure)std::rethrow_exception(failure);return value;}void save(const mod::CalendarFile&v)override{value=v;}};
struct Queue {struct Job{std::function<void()>work;std::function<void(std::exception_ptr)>done;};std::deque<Job>jobs;mod::CalendarExecutor executor(){return{[this](auto work,auto done){jobs.push_back({std::move(work),std::move(done)});return true;}};}void drain(){for(unsigned n=0;!jobs.empty();++n){check(n<64,"Synthetic FIFO settles");auto j=std::move(jobs.front());jobs.pop_front();std::exception_ptr e;try{j.work();}catch(...){e=std::current_exception();}j.done(e);}}};
void state(){
    Queue queue;auto repo=std::make_shared<Repository>();unsigned ids{};mod::CalendarStateOptions o;
    o.text.characters=[](std::string_view s){return s.size();};o.text.trimmed=[](std::string_view s){const auto b=s.find_first_not_of(" \n");return b==s.npos?std::string{}:std::string(s.substr(b,s.find_last_not_of(" \n")-b+1));};o.text.prefix=[](std::string_view s,std::size_t n){return std::string(s.substr(0,n));};
    const double now=*mod::calendarUTC().timestamp({2026,10,4},10,0);o.now=[now]{return now;};o.zone=[]{return mod::calendarUTC();};o.newID=[&]{return std::string("E0000000-0000-4000-8000-00000000000")+char('0'+ ++ids);};
    o.scheduling.authorization=[](bool,auto done){done(mod::CalendarPermission::denied);};o.scheduling.reconcile=[](auto,auto,auto done){done({});};o.scheduling.cancelObsolete=[](auto,auto done){done();};
    mod::CalendarState s(repo,queue.executor(),o);s.setActive(true);queue.drain();check(s.loaded()&&!s.error()&&!s.errorCode(),"Synthetic Calendar loads without an error");
    check(!s.save("Owned","",{2026,2,30})&&s.errorCode()==mod::CalendarErrorCode::invalidDate&&s.error()==std::optional<std::string>(mod::CalendarError(mod::CalendarErrorCode::invalidDate).what()),"Invalid source date keeps its typed code next to the English text");
    check(!s.save("Owned","",{2026,10,5},std::string("E0000000-0000-4000-8000-000000000099"))&&s.errorCode()==mod::CalendarErrorCode::missing,"Missing edit target keeps its typed code");
    s.report("Owned free-form failure");check(!s.errorCode()&&s.error()==std::optional<std::string>("Owned free-form failure"),"Free text clears any previous code and is shown as reported");
    check(s.save("Owned","",{2026,10,5}),"Valid save is accepted");check(!s.error()&&!s.errorCode(),"A new transaction clears the previous error and code");queue.drain();check(s.events().size()==1,"Synthetic save settles");
    repo->failure=std::make_exception_ptr(mod::CalendarError(mod::CalendarErrorCode::changedOnDisk));check(s.save("Second","",{2026,10,6}),"Conflicting save is submitted");queue.drain();check(s.errorCode()==mod::CalendarErrorCode::changedOnDisk&&s.hasPersistenceFailure(),"Repository CalendarError reaches the owner as its typed code");
    repo->failure=std::make_exception_ptr(std::runtime_error("Owned synthetic disk failure"));check(s.save("Third","",{2026,10,6}),"Failing save is submitted");queue.drain();check(!s.errorCode()&&s.error()==std::optional<std::string>("Owned synthetic disk failure"),"Non-source failures keep their own text without a code, like Mac localizedDescription");
    repo->failure=std::make_exception_ptr(42);check(s.save("Fourth","",{2026,10,6}),"Unknown failure save is submitted");queue.drain();check(s.errorCode()==mod::CalendarErrorCode::invalidData,"Unknown failures report the source invalid-data error");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass calendar-localization-source.json");std::ifstream file(argv[1],std::ios::binary);check(bool(file),"Mac Calendar localization fixture opens");const std::string bytes(std::istreambuf_iterator<char>(file),{});const auto root=J::parse(bytes,1024*1024);source(root);accessibility(root);state();std::cout<<"PASS "<<checks<<" Calendar localization checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
