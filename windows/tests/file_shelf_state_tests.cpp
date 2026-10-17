#include "modules/file_shelf_state.hpp"
#include "core/data/json.hpp"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <algorithm>
std::atomic<std::size_t> allocations{};
void* operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void* operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using State=endfield::modules::FileShelfState;using Item=endfield::modules::FileShelfItem;using Json=ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}
std::string id(std::size_t n){char buffer[64];std::snprintf(buffer,sizeof(buffer),"00000000-0000-4000-8000-%012zu",n);return buffer;}
Item item(std::size_t n){return {.id=id(n),.name="file "+std::to_string(n),.lastKnownPath="/synthetic/"+std::to_string(n)+".png",.typeDescription="Source type"};}
std::vector<Item> items(std::size_t count){std::vector<Item> out;for(std::size_t i=0;i<count;++i)out.push_back(item(i));return out;}
struct Fake {
    std::vector<Item> saved;unsigned snapshots{},refreshes{},adds{},removes{},clears{};bool failAdd{},failClear{},failRefresh{};std::optional<std::string> failRemove;
    State::Store adapter(){return {[this]{++snapshots;return saved;},[this]{++refreshes;if(failRefresh)throw std::runtime_error("refresh denied");},[this](std::span<const std::string> tokens){++adds;if(failAdd)throw std::runtime_error("batch denied");const auto before=saved.size();for(const auto& token:tokens){const auto value=std::stoull(token);if(std::none_of(saved.begin(),saved.end(),[&](const auto& n){return n.id==id(value);}))saved.push_back(item(value));}return saved.size()-before;},[this](std::string_view key){++removes;if(failRemove&&*failRemove==key)throw std::runtime_error("remove denied");std::erase_if(saved,[&](const auto& n){return n.id==key;});},[this]{++clears;if(failClear)throw std::runtime_error("clear denied");saved.clear();}};}
};
std::string action(std::size_t i,const char* verb){return "shelf:"+id(i)+":"+verb;}
void geometryAndHits(){
    auto data=items(18);data[0].availabilityError="missing";State s(data);
    check(s.visibleRange()==State::VisibleRange{0,8}&&s.maximumOffset()==472,"Source two-column, 80-point rows and 248-point viewport");
    auto c=s.card(id(0));check(c&&c->full==State::Rect{12,44,184,74}&&c->clipped==c->full&&!c->available&&c->iconOpacity==.45,"Source unavailable card geometry/icon alpha");
    check(s.card(id(1))->full==State::Rect{204,44,184,74}&&s.card(id(6))->clipped==State::Rect{12,284,184,4},"Column spacing and bottom partial card are exact");
    check(!s.itemAt({20,50})&&s.itemAt({220,50})==id(1),"Unavailable reference never initiates outgoing drag");
    check(!s.itemAt({340,100}),"Whole inline control strip excludes native drag");
    check(!s.mouseDown({400,100})&&!s.mouseDown({10,334})&&!s.mouseDown({std::numeric_limits<double>::quiet_NaN(),0}),"Bounds exclude maximum edges and nonfinite input");
    check(s.scroll({10,40},77)&&s.scrollOffset()==77&&!s.card(id(0)),"Source excludes clipped card fragments below two points");
    s.scrollBy(-1);check(s.card(id(0))->clipped==State::Rect{12,40,184,2},"Exactly two-point fragment stays accessible");
    check(!s.scroll({391,50},10)&&!s.scroll({30,50},std::numeric_limits<double>::infinity()),"Scroll hit excludes right boundary and nonfinite delta");
}
void selectionAndDrag(){
    auto data=items(12);data[2].availabilityError="offline";State s(data);
    s.selectItem(id(1),{});s.selectItem(id(5),{true,false});check(s.selectedIDs().size()==5&&s.selectedID()==id(5),"Shift selection uses stable anchor and inclusive range");
    s.selectItem(id(3),{false,true});check(!s.selectedIDs().contains(id(3))&&s.selectedID()==id(1),"Toggle primary falls back to first shelf order");
    s.selectItem(id(5),{},true);check(s.selectedIDs().size()==4&&s.selectedID()==id(5),"Mouse-down on group delays single selection");
    const auto drag=s.dragSelection(id(5));check(drag==std::vector<std::string>{id(1),id(4),id(5)},"Drag preserves shelf order and omits unavailable reference");
    s.beginSelectionDrag();s.finishPointerSelection();check(s.selectedIDs().size()==4,"Native drag consumes pending single click");
    s.selectItem(id(4),{},true);s.finishPointerSelection();check(s.selectedIDs()==std::set<std::string,std::less<>>{id(4)},"Mouse-up without drag collapses group");
    s.selectNext(2,true);check(s.selectedIDs().size()==3&&s.selectedID()==id(6),"Down key extends by two cards");
    s.selectItem({},{});s.selectNext(-2);check(s.selectedID()==id(11)&&s.scrollOffset()==232,"Negative movement without selection starts at last and reveals row");
    s.selectNext(std::numeric_limits<int>::min());check(s.selectedID()==id(0)&&s.scrollOffset()==0,"Extreme negative movement clamps without overflow");
    s.selectNext(std::numeric_limits<int>::max());check(s.selectedID()==id(11),"Extreme positive movement clamps without overflow");
    s.perform("shelf:clear");s.scrollBy(0);check(s.confirmingClear(),"Unchanged scroll does not cancel confirmation");s.scrollBy(-1);check(!s.confirmingClear(),"Changed scroll clears confirmation");
    s.setDropTarget(true);const auto selected=s.selectedIDs();s.deactivate();check(!s.dropTarget()&&!s.confirmingClear()&&s.selectedIDs()==selected,"Deactivate preserves selection/scroll but clears transient state");
}
void importsAndFailure(){
    Fake f;f.saved=items(10);State s(f.saved,f.adapter());check(!f.snapshots&&!f.refreshes,"Constructor never invokes storage or resolves files");s.activate();check(f.refreshes==1&&f.snapshots==1,"Activation refresh is explicit once");
    std::vector<std::string> batch{"10","11","12"};check(s.importFiles(batch)&&s.items().size()==13&&s.selectedIDs().size()==3&&s.selectedID()==id(12)&&s.scrollOffset()==312,"Import selects added group, primary last and reveals destination");
    const auto selection=s.selectedIDs();const auto offset=s.scrollOffset();auto events=s.takeEvents();check(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind==State::EventKind::itemsAdded;})==1,"One added-name event per committed batch");
    check(s.importFiles(batch)&&s.selectedIDs()==selection&&s.scrollOffset()==offset,"All-duplicate import is accepted without destroying selection");events=s.takeEvents();check(events.empty(),"Duplicate import has no collection reveal or add event");
    f.failAdd=true;s.showError("old error");check(!s.importFiles(batch)&&s.items().size()==13&&s.selectedIDs()==selection&&s.error()=="batch denied","Failed atomic batch retains committed references and selection");
    s.setDropTarget(true);check(s.statusText()=="Release to keep file references","Drop hint takes priority over error");s.setDropTarget(false);check(s.statusText()=="batch denied","Error returns after drop leaves");
    s.selectItem(id(0),{});s.selectItem(id(3),{true,false});f.failRemove=id(1);s.deleteSelection();check(!s.item(id(0))&&s.item(id(1))&&s.items().size()==12&&s.error()=="remove denied","Sequential remove preserves earlier commit when a later reference fails");
    events=s.takeEvents();check(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind==State::EventKind::itemRemoved&&e.values==std::vector<std::string>{"file 0"};})==1,"Only successfully removed reference emits its name");
    s.perform("shelf:confirmClear");check(f.clears==0,"Clear requires confirmation");s.perform("shelf:clear");f.failClear=true;s.perform("shelf:confirmClear");check(s.items().size()==12&&s.confirmingClear()&&s.error()=="clear denied","Failed clear leaves cards and confirmation intact");
    f.failClear=false;s.perform("shelf:confirmClear");check(s.items().empty()&&s.selectedIDs().empty()&&!s.selectedID()&&!s.confirmingClear()&&!s.error(),"Successful clear publishes empty committed shelf");events=s.takeEvents();check(events.back().kind==State::EventKind::shelfCleared&&events.back().count==12,"Clear announcement uses actual pre-clear committed count");
    f.failRefresh=true;s.activate();check(s.error()=="refresh denied"&&s.items().empty(),"Refresh failure remains status without fabricated rows");
    State missing({}, {}, {},endfield::modules::FileShelfStrings::simplifiedChinese());check(!missing.importFiles(batch)&&missing.error()=="文件暂存架存储不可用。","Missing storage uses original Chinese error");
}
void anchorsActionsAndStatus(){
    Fake f;f.saved=items(30);unsigned chooser{},previews{},reveals{};std::string requested;
    State::PlatformActions platform{[&]{++chooser;},[&](std::string_view key){++previews;requested=key;},[&](std::string_view key){++reveals;requested=key;},[](std::int64_t bytes){return std::to_string(bytes)+" source-formatted";}};
    State s(f.saved,f.adapter(),platform);s.scrollBy(165);f.saved.erase(f.saved.begin(),f.saved.begin()+2);s.refreshFromStore();check(s.scrollOffset()==85&&s.card(id(4))->full.y==39,"Refresh retains first visible row identity and sub-row remainder");
    s.selectItem(id(4),{});s.perform(action(4,"preview"));check(previews==1&&requested==id(4),"Preview calls injected source request");s.revealSelection();check(reveals==1,"Reveal calls injected owner action");s.perform("shelf:add");check(chooser==1,"Add opens only injected chooser");
    f.saved[2].availabilityError="gone";s.refreshFromStore();s.previewSelection();s.revealSelection();check(previews==1&&reveals==1,"Unavailable selection suppresses preview/reveal");
    const auto actions=s.cardActions(id(4),false);check(actions.size()==1&&actions.front().id==action(4,"remove"),"Unavailable control slots keep only remove action");
    s.scrollBy(-9999);s.mouseDown({220,50},2);check(previews==2&&requested==id(3),"Double-click requests source preview for available body");
    s.mouseDown({399,30});check(s.selectedID()==id(3),"Click outside collection preserves selection");s.mouseDown({10,280});check(!s.selectedID(),"Blank collection click clears selection");
    auto n=item(90);n.byteCount=123;check(s.typeLabel(n)=="Image · PNG"&&s.sizeLabel(n)=="123 source-formatted","Type is source extension mapping; byte formatting belongs to injected owner");n.isDirectory=true;n.lastKnownPath="/synthetic/Test.pages/";check(s.typeLabel(n)=="Source type"&&s.sizeLabel(n)=="—","Directory-backed source document preserves type description");
    n.isDirectory=false;n.lastKnownPath="C:\\synthetic\\.png";check(s.typeLabel(n)=="Source type","Leading-dot filename has no path extension");
    const auto toolbar=s.toolbarActions();check(toolbar[0].rect==State::Rect{12,299,84,27}&&toolbar[1].rect==State::Rect{104,299,74,27},"Source toolbar geometry");
    s.perform("shelf:clear");check(s.toolbarActions()[0].rect==State::Rect{218,299,67,27}&&s.toolbarActions()[1].rect==State::Rect{294,299,94,27},"Source confirmation control geometry");
    auto zh=endfield::modules::FileShelfStrings::simplifiedChinese();State chinese(items(3),{}, {},zh);chinese.selectItem(id(0),{});chinese.selectItem(id(2),{true,false});check(chinese.statusText()=="已选 3 项 · 拖出以复制","Authoritative Chinese count status retained");
    State noFormatter({n});rejects([&]{(void)noFormatter.sizeLabel(n);},"Missing platform formatter is explicit instead of an invented label");
}
void guardAndPerformance(){
    Fake f;f.saved=items(1000);f.saved[0].typeDescription=std::string(1024*1024,'x');State s(f.saved,f.adapter());s.scrollBy(1000);(void)s.takeEvents();
    const auto before=allocations.load();for(unsigned i=0;i<10000;++i){(void)s.visibleRange();(void)s.card(f.saved[i%1000].id);(void)s.scrollIndicator();(void)s.itemAt({double(i%400),double(i%334)});}
    check(allocations.load()==before,"Steady geometry/hit queries never copy payloads or allocate");check(f.snapshots==0&&f.refreshes==0,"Pose/hit queries never access reference provider");
    rejects([&]{State bad({item(0),item(0)});},"Duplicate snapshot IDs reject");auto bad=item(0);bad.byteCount=-1;rejects([&]{State invalid({bad});},"Negative byte metadata rejects");
    const auto old=s.items().size();f.saved.push_back(f.saved[0]);rejects([&]{s.refreshFromStore();},"Invalid snapshot rejects before publication");check(s.items().size()==old,"Rejected snapshot preserves previous geometry/content");
    State* owner=nullptr;State reentrant({}, {},{[&]{owner->deactivate();},{},{},{}});owner=&reentrant; // No store means choose callback is deliberately unavailable.
    Fake store;State guarded({},store.adapter(),{[&]{owner->deactivate();},{},{},{}});owner=&guarded;guarded.activate();guarded.perform("shelf:add");check(guarded.active()&&guarded.error()=="Reentrant FileShelfState mutation","Platform callbacks cannot mutate or deactivate in-flight state");
    auto sized=item(7);sized.byteCount=7;State* formatterOwner=nullptr;
    State formatting({sized},{},{ {},{},{},[&](std::int64_t){formatterOwner->deactivate();return std::string("invalid callback");} });formatterOwner=&formatting;
    rejects([&]{(void)formatting.accessibleActions();},"Format callback cannot invalidate metadata during accessible traversal");
    const auto dark=State::style(true),light=State::style(false);check(dark.cardWhite==.76&&light.cardWhite==.89&&dark.cardInk==.14&&dark.muted==.68&&light.primary==.11,"Original source card palette metadata");
    const auto polygon=State::cutCorner({0,0,184,74},7);check(polygon[0]==State::Point{7,0}&&polygon[2]==State::Point{184,67}&&polygon[5]==State::Point{0,7},"Exact six-vertex card cut corners");
}
State::Rect rect(const Json& value){const auto& a=value.array();return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
void sourceOracle(const char* path){
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Missing explicit shelf source oracle");const std::string bytes{std::istreambuf_iterator<char>(input),{}};const auto root=Json::parse(bytes,8*1024*1024);
    auto data=items(100);for(std::size_t i=0;i<data.size();++i)if(i%11==0)data[i].availabilityError="unavailable";State s(std::move(data));
    for(const auto& row:root["cases"].array()){
        const auto op=row["op"].integer();const auto index=std::size_t(row["index"].integer());const auto key=id(index);
        switch(op){case 0:s.selectItem(key,{});break;case 1:s.selectItem(key,{true,false});break;case 2:s.selectItem(key,{false,true});break;case 3:s.selectItem(key,{},true);break;case 4:s.finishPointerSelection();break;case 5:s.beginSelectionDrag();break;case 6:s.scrollBy(row["amount"].number());break;case 7:s.selectNext(int(row["direction"].integer()),row["extend"].boolean());break;default:s.selectItem({},{});break;}
        check(s.scrollOffset()==row["scroll"].number(),"Original Swift scroll value");check(row["selected"].isNull()?!s.selectedID():s.selectedID()==id(std::size_t(row["selected"].integer())),"Original Swift primary selection");
        std::set<std::string,std::less<>> selected;for(const auto& value:row["selectedIDs"].array())selected.insert(id(std::size_t(value.integer())));check(s.selectedIDs()==selected,"Original Swift range/toggle/delayed selection");
        const auto range=s.visibleRange();check(range.end-range.begin==row["visible"].array().size(),"Original Swift visible range count");
        for(const auto& card:row["visible"].array()){const auto c=s.card(id(std::size_t(card["index"].integer())));if(card["clipped"].isNull())check(!c,"Original Swift tiny clipped row omission");else check(c&&c->full==rect(card["full"])&&c->clipped==rect(card["clipped"]),"Original Swift full/clipped rectangle");}
        std::vector<std::string> drag;for(const auto& value:row["drag"].array())drag.push_back(id(std::size_t(value.integer())));check(s.dragSelection(key)==drag,"Original Swift ordered available drag references");(void)s.takeEvents();
    }
}
}
int main(int argc,char** argv){try{geometryAndHits();selectionAndDrag();importsAndFailure();anchorsActionsAndStatus();guardAndPerformance();if(argc>1)sourceOracle(argv[1]);std::cout<<"File shelf state: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"File shelf state failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
