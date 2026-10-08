#include "modules/notes_state.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
std::atomic<std::size_t> allocations{};
void* operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using State=endfield::modules::NotesState;
using ehud::data::Note;
using ehud::data::NoteKind;
namespace {
unsigned checks{};
void check(bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char* m){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,m);}
constexpr const char* a="00000000-0000-4000-8000-000000000001";
constexpr const char* b="00000000-0000-4000-8000-000000000002";
constexpr const char* c="00000000-0000-4000-8000-000000000003";
Note note(const char* id,NoteKind kind=NoteKind::text){return Note{.id=id,.kind=kind,.createdAt=123.25};}
struct Fake {
    std::map<std::string,Note,std::less<>> saved;
    unsigned writes{},deletes{};bool failWrite{},failDelete{};
    State::Persistence adapter(){return {[this](const Note& n){++writes;if(failWrite)throw std::runtime_error("fixture disk full");saved.insert_or_assign(n.id,n);},
        [this](std::string_view id){++deletes;if(failDelete)throw std::runtime_error("fixture delete denied");saved.erase(std::string(id));}};}
};
void createAndEdit(){
    Fake f;State s({},f.adapter());s.setWorkspaceBounds({100,50,1000,700});
    check(s.createText(a,345.5),"Text add is accepted in Notes");const auto* n=s.note(a);
    check(n&&n->x==505&&n->y==340&&n->width==210&&n->height==140&&n->zIndex==0&&n->createdAt==345.5,"Source create size, center offset, z and injected time");
    check(s.selection()==a&&s.editing()&&s.editing()->rect==State::Rect{514,369,192,82},"Source create selects and requests workspace editor with formatting space");
    check(f.writes==1&&f.saved.at(a).text.empty(),"Creation persists once before typing");
    check(s.finishEditing("中文 한국어 日本語 🌍")&&s.note(a)->text=="中文 한국어 日本語 🌍"&&!s.editing(),"Plain Unicode edit commits without flattening a formatted document");
    check(f.writes==2,"Edit completion is one persistence boundary");
    s.createText(b,346);check(s.note(b)->x==523&&s.note(b)->y==358,"Next creation uses source eighteen-point cascade");s.finishEditing("");
    check(s.select(a)&&s.note(a)->zIndex==2,"Selecting a lower note raises and persists source z order");const auto count=f.writes;
    check(!s.select(a)&&f.writes==count,"Already-top selection does not rewrite SQLite");
    s.beginEditing(a);rejects([&]{s.setWorkspaceBounds({0,0,800,600});},"Resize requires host to finish projected editing first");
    rejects([&]{s.finishEditing(std::string(1,char(0xff)));},"Invalid UTF-8 cannot corrupt pending editor or saved text");check(s.editing().has_value(),"Rejected edit remains available");s.detachEditor();
    check(s.note(a)->text=="中文 한국어 日本語 🌍","Explicit editor discard preserves committed note");
    auto rich=note(c);rich.richText="opaque styles preserved";Fake g;State formatted({rich},g.adapter());
    rejects([&]{formatted.beginEditing(c);},"Formatted note editing is explicitly deferred");check(formatted.note(c)->richText==rich.richText,"Unavailable plain editor does not erase rich payload");
}
void workspaceAndGesture(){
    auto first=note(a);first.x=20;first.y=30;first.width=210;first.height=140;
    auto second=note(b);second.x=400;second.zIndex=3;
    Fake f;State s({first,second},f.adapter());s.setWorkspaceBounds({0,0,600,400});
    check(s.beginGesture(a,{30,40},State::Gesture::move)&&s.note(a)->zIndex==4,"Gesture retains separately raised selection order");const auto writes=f.writes;
    check(!s.dragTo({30.5,40.5})&&f.writes==writes,"One-point Manhattan motion remains a click");
    check(s.dragTo({50,60})&&s.note(a)->x==40&&s.note(a)->y==50&&f.writes==writes,"Drag previews source geometry without persistence");
    s.dragTo({60,65});check(s.note(a)->x==50&&s.note(a)->y==55,"Repeated samples use gesture origin, not accumulated deltas");
    s.cancelInteraction();check(!s.dragging()&&f.writes==writes+1&&f.saved.at(a).x==50,"Cancellation commits the changed gesture instead of rollback");
    s.beginGesture(a,{0,0},State::Gesture::resize);s.dragTo({-999,-999});check(s.note(a)->width==110&&s.note(a)->height==70,"Source text minimum size retained");s.endGesture();
    s.beginGesture(a,{0,0},State::Gesture::move);s.dragTo({9999,9999});s.endGesture();check(s.note(a)->x==490&&s.note(a)->y==330,"Actual move is clamped to workspace");
    const auto persisted=*s.note(a);const auto before=f.writes;s.setWorkspaceBounds({10,20,50,40});
    check(*s.note(a)==persisted&&f.writes==before,"Workspace resize does not rewrite saved coordinates");
    check(s.card(a)->rect==State::Rect{10,20,50,40}&&s.card(a)->localRect==State::Rect{0,0,50,40},"Display clamp handles tiny workspace and nonzero origin exactly");
    const auto nan=std::numeric_limits<double>::quiet_NaN();check(!s.setWorkspaceBounds({0,0,nan,40})&&s.workspaceBounds()==State::Rect{10,20,50,40},"Invalid workspace leaves old geometry");
    check(s.topNote({10,20}).has_value()&&!s.topNote({60,20})&&!s.topNote({10,60}),"Notes hits match original CGRect minimum-inclusive maximum-exclusive edges");
    s.setWorkspaceBounds({0,0,12,30});const auto& tiny=s.beginEditing(a);
    check(tiny.rect.width==-6&&tiny.rect.height==1,"Tiny editor retains explicit source width-minus18 and max1 height; fitting is not invented");s.detachEditor();
    auto invalid=note(c,NoteKind::todo);invalid.width=nan;invalid.height=-1;invalid.x=nan;invalid.y=std::numeric_limits<double>::infinity();const auto bounded=State::constrained(invalid);
    check(bounded.x==24&&bounded.y==50&&bounded.width==180&&bounded.height==105,"Geometry fallback matches source TODO rules");
}
void pinDeleteAndClose(){
    auto first=note(a);first.x=10;first.y=10;first.zIndex=1;auto second=note(b);second.x=250;second.zIndex=2;
    Fake f;State s({first,second},f.adapter());s.setWorkspaceBounds({0,0,800,600});s.togglePin(a);s.select(b);s.requestDeletion(b);
    s.setPresentation(false);check(s.visible(a)&&!s.visible(b)&&!s.selection()&&!s.pendingDeletion(),"Other module keeps pinned cards, removes unpinned selection and confirmation");
    check(!s.createText(c,0)&&!s.requestDeletion(b),"Other module cannot add or interact with hidden notes");
    check(s.topNote({20,20})==a,"Pinned card remains hit-testable outside Notes");
    check(s.requestDeletion(a)&&s.deletionControls().has_value(),"Pinned card keeps delete confirmation on other tabs");
    const auto controls=*s.deletionControls();check(controls[0]==State::Rect{111,37,25,25}&&controls[1]==State::Rect{142,37,25,25},"Confirmation source geometry sits beneath header delete control");
    f.failDelete=true;check(!s.confirmDeletion(a)&&s.note(a)&&s.pendingDeletion()==a,"Failed delete preserves note, selection and confirmation");
    f.failDelete=false;check(s.confirmDeletion(a)&&!s.note(a)&&!s.pendingDeletion()&&!s.selection(),"Delete mutates UI only after persistence succeeds");
    s.setPresentation(true);s.select(b);s.beginGesture(b,{0,0},State::Gesture::move);s.dragTo({10,10});const auto count=f.writes;
    s.setPresentation(false);check(!s.dragging()&&f.writes==count+1,"Tab transition commits one active drag");
    s.setPresentation(true);s.togglePin(b);const auto content=s.revision(),placement=s.placementRevision();const auto saved=f.writes;
    // Host continues supplying its actual closing plane until global concealment.
    endfield::core::Projection closing;closing.values[2]=20;closing.values[5]=-30;
    check(s.setWorkspaceProjection(closing)&&s.placementRevision()==placement+1&&s.revision()==content&&f.writes==saved,"Closing tilt updates only retained workspace placement");
    check(s.visible(b)&&s.workspaceProjection().values==closing.values,"Closing placement does not discard pinned card state");
    const auto allocateBefore=allocations.load();for(unsigned i=0;i<1000;++i){closing.values[2]=double(i);s.setWorkspaceProjection(closing);(void)s.card(b);(void)s.topNote({1,1});}
    check(allocations.load()==allocateBefore,"Repeated placement, card and hit queries allocate no memory");
}
void largePayloadGesture(){
    auto item=note(a);item.text=std::string(1024*1024,'x');item.richText=std::string(256*1024,'r');item.media=std::string(256*1024,'m');
    const auto original=item;Fake f;State s({std::move(item)},f.adapter());s.setWorkspaceBounds({0,0,4000,3000});s.beginGesture(a,{0,0},State::Gesture::move);
    const auto* textBytes=s.note(a)->text.data();const auto* richBytes=s.note(a)->richText->data();const auto* mediaBytes=s.note(a)->media->data();
    const auto count=allocations.load();for(unsigned i=0;i<1000;++i)s.dragTo({double(i+2),double(i+3)});
    check(allocations.load()==count,"Large-payload changed drag samples allocate no heap or copied content");
    check(s.note(a)->text.data()==textBytes&&s.note(a)->richText->data()==richBytes&&s.note(a)->media->data()==mediaBytes&&s.note(a)->text==original.text&&s.note(a)->richText==original.richText&&s.note(a)->media==original.media,"Drag retains original text and opaque payload storage exactly");
    check(f.writes==0,"Large note gesture does not persist any intermediate sample");s.endGesture();check(f.writes==1,"Large note gesture persists once at finish");
}
void failuresAndOrder(){
    Fake f;f.failWrite=true;State s({},f.adapter());s.createText(a,0);
    check(s.note(a)&&s.editing()&&s.unsaved().contains(a)&&s.error(),"Failed creation retains editable unsaved draft");s.finishEditing("keep pending text");
    check(s.note(a)->text=="keep pending text"&&s.unsaved().contains(a),"Failed edit preserves user text in retained draft");f.failWrite=false;s.beginEditing(a);s.finishEditing("keep pending text");
    check(s.unsaved().empty()&&!s.error()&&f.saved.at(a).text=="keep pending text","Successful retry clears unsaved error");
    auto low=note(b);low.zIndex=9;auto high=note(c);high.zIndex=std::numeric_limits<std::int64_t>::max();Fake g;State old({high,low},g.adapter());old.select(b);
    check(old.note(c)->zIndex==1&&old.note(b)->zIndex==2&&g.writes==3,"Source z exhaustion compacts relative order then raises selection");
    rejects([&]{State bad({low,low},g.adapter());},"Duplicate initial note IDs reject");
    rejects([&]{s.createText("not-an-id",0);},"Invalid injected identity rejects before persistence");
}
void sqliteIntegration(){
    const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("Endfield-notes-state-"+ehud::data::makeUUID());
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}} cleanup{root};
    {
        ehud::data::NotesStore store(root);
        State s({}, {[&](const Note& n){(void)store.upsert(n);},[&](std::string_view id){(void)store.remove(id);}});
        s.setWorkspaceBounds({0,0,900,700});s.createText(a,987.125);s.finishEditing("temporary fixture");s.togglePin(a);
        const auto before=store.notes();s.beginGesture(a,{0,0},State::Gesture::move);s.dragTo({17,29});check(store.notes()==before,"SQLite remains unchanged while gesture is active");s.endGesture();
        check(store.notes().size()==1&&store.notes().front()==*s.note(a),"Completed gesture commits exact portable store row");
    }
    {ehud::data::NotesStore reopened(root);check(reopened.notes().size()==1&&reopened.notes().front().isPinned&&reopened.notes().front().text=="temporary fixture"&&reopened.notes().front().createdAt==987.125,"Temporary database roundtrip retains text, pin and Foundation timestamp");}
}
void sourceGeometry(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Missing explicit source fixture");
    const std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    const auto fixture=ehud::data::Json::parse(bytes);
    const auto number=[](const ehud::data::Json& v){if(v.isNumber())return v.number();if(v.string()=="nan")return std::numeric_limits<double>::quiet_NaN();return v.string()=="inf"?std::numeric_limits<double>::infinity():-std::numeric_limits<double>::infinity();};
    for(const auto& row:fixture["cases"].array()){
        auto n=note(a);const auto kind=row["kind"].string();n.kind=kind=="todo"?NoteKind::todo:kind=="image"?NoteKind::image:kind=="drawing"?NoteKind::drawing:NoteKind::text;
        const auto& in=row["input"].array();n.x=number(in[0]);n.y=number(in[1]);n.width=number(in[2]);n.height=number(in[3]);
        std::optional<State::Rect> bounds;if(!row["bounds"].isNull()){const auto& b=row["bounds"].array();bounds=State::Rect{b[0].number(),b[1].number(),b[2].number(),b[3].number()};}
        const auto result=State::constrained(n,bounds);const auto& expected=row["expected"].array();
        check(result.x==expected[0].number()&&result.y==expected[1].number()&&result.width==expected[2].number()&&result.height==expected[3].number(),"Exact original Swift NotesGeometry fixture");
    }
}
}
int main(int argc,char**argv){try{check(argc<=2,"Optional explicit source geometry fixture only");createAndEdit();workspaceAndGesture();pinDeleteAndClose();largePayloadGesture();failuresAndOrder();sqliteIntegration();if(argc==2)sourceGeometry(argv[1]);std::cout<<checks<<" isolated Notes state checks passed; no live app data or native APIs\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
