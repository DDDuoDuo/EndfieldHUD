#include "modules/notes_checklist.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
using namespace endfield::modules;using endfield::core::Rect;
namespace {
std::size_t checks{};
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char*m){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,m);}
constexpr const char*noteID="00000000-0000-4000-8000-000000000011";
constexpr const char*one="00000000-0000-4000-8000-000000000012";
constexpr const char*two="00000000-0000-4000-8000-000000000013";
constexpr const char*three="00000000-0000-4000-8000-000000000014";
std::shared_ptr<const NotesMeasuredText> measured(std::string value,double width=139,double lineHeight=15){auto result=std::make_shared<NotesMeasuredText>();result->text=std::move(value);result->width=width;result->fontSize=11;std::size_t at{};
    do{const auto newline=result->text.find('\n',at),visible=newline==std::string::npos?result->text.size():newline,end=newline==std::string::npos?visible:visible+1;result->lines.push_back({at,end,visible,result->height,lineHeight});result->height+=lineHeight;if(newline==std::string::npos)break;at=end;}while(at<=result->text.size());return result;}
void run(){
    const auto first=measured("First 中文"),second=measured("Line one\nLine two\nLine three"),empty=measured(""),placeholder=measured("New\nitem\n…");
    const std::array<NotesChecklistMeasurement,3> inputs{{{one,first,first,false},{two,second,second,true},{three,empty,placeholder,false}}};
    NotesChecklistLayout layout(noteID,228,105,inputs);
    check(layout.viewport()==Rect{5,27,218,50}&&layout.textWidth()==139,"Original TODO viewport uses header24, insets5/3 and footer31; text width is w-89");
    check(layout.rows().size()==3&&layout.rows()[0].height==25&&layout.rows()[1].origin==25&&layout.rows()[1].height==53&&layout.rows()[2].origin==78&&layout.rows()[2].height==25,"Original rows max25 versus measured height+8, without fixed row approximation");
    check(layout.contentHeight()==103&&layout.maximumScrollOffset()==53,"Source content extent and overflow");
    check(layout.rows()[2].display==placeholder&&layout.rows()[2].height!=placeholder->height+8,"Empty item's placeholder does not inflate source row geometry");
    check(layout.visibleRows(0)==std::pair<std::size_t,std::size_t>{0,2}&&layout.visibleRows(25)==std::pair<std::size_t,std::size_t>{1,2},"Visible rows use strict end>minimum and origin<maximum");
    check(layout.textRect(1,25)==Rect{28,30,139,47},"Item text rect shares row offset and exact source3-point inset");
    const auto edit=layout.beginEditing(1,0);check(edit.noteScrollOffset==25&&edit.localRect==Rect{28,30,139,47}&&edit.editorScrollOffset==0&&!edit.multiline&&edit.fontSize==11,"Oversized row reveals its top and opens clipped plain font11 editor");
    const auto tail=layout.beginEditing(2,0);check(tail.noteScrollOffset==53&&tail.localRect==Rect{28,55,139,19},"Last row reveals bottom while retaining source edit height");
    check(layout.finishEditingOffset(1,12.5,0,0)==12.5,"Unscrolled item editor preserves original note scroll exactly");
    check(layout.finishEditingOffset(1,12.5,0,7.25)==35.25&&layout.finishEditingOffset(1,12.5,9,0)==28,"Source finish offset maps row origin+3+editor scroll only when requested");
    const auto actions=layout.actions(25);check(actions.size()==6,"Only visible row contributes five actions plus source add action");
    check(actions[0].id==std::string("note:")+noteID+":check:"+two&&actions[0].label=="Uncheck "+second->text&&actions[0].localRect==Rect{5,27,21,22},"Check identity/checked label and local action geometry");
    check(actions[2].localRect==Rect{170,27,17,22}&&actions[3].localRect==Rect{188,27,17,22}&&actions[4].localRect==Rect{206,27,17,22},"Source up/down/remove trailing offsets remain17x22");
    check(actions.back().localRect==Rect{7,79,199,21},"Add item is fixed below viewport");
    const auto clipped=layout.actions(50);check(clipped[0].verb.starts_with("editItem"),"Fully clipped check/control height is omitted while taller text remains actionable");
    const auto count=allocations.load();counting=true;for(unsigned n=0;n<120;++n){const auto y=double(n)*.1;(void)layout.visibleRows(y);(void)layout.textRect(1,y);(void)layout.beginEditing(1,y);(void)layout.finishEditingOffset(1,y,0,0);}counting=false;check(allocations==count,"Repeated scroll/hit/editor geometry allocates no storage or layouts");
    NotesChecklistLayout none(noteID,228,154,{});check(none.visibleRows(0)==std::pair<std::size_t,std::size_t>{0,0}&&none.actions(0).size()==1&&none.maximumScrollOffset()==0,"Empty checklist retains only its add action");
    auto duplicate=inputs;duplicate[1].itemID=one;rejects([&]{NotesChecklistLayout bad(noteID,228,105,duplicate);},"Duplicate item identity rejects without mutating old index");
    auto wrong=inputs;wrong[0].text=measured("wrong",140);rejects([&]{NotesChecklistLayout bad(noteID,228,105,wrong);},"Wrong measured width cannot split paint and editor geometry");
    rejects([&]{layout.beginEditing(3,0);},"Removed/stale row index cannot open an editor");rejects([&]{layout.clampOffset(std::numeric_limits<double>::quiet_NaN());},"Nonfinite scroll rejected");
    check(layout.rows()[0].text==first&&layout.rows()[1].text==second,"Failed calls preserve exact caller-owned measurement handles");
}
const NotesLayer&layer(const NotesCardPresentation&p,std::string_view suffix){for(const auto&v:p.layers())if(v.id.ends_with(suffix))return v;throw std::runtime_error("Missing checklist artwork");}
void connected(){
    std::size_t writes{};ehud::data::Note saved{.id=noteID,.createdAt=0};NotesState state({}, {[&](const auto&n){++writes;saved=n;},[](auto){}});state.setWorkspaceBounds({0,0,800,600});
    check(state.createChecklist(noteID,one,12)&&state.selection()==noteID&&!state.editing(),"Checklist creation uses one model/save and awaits actual measured item viewport");
    const auto*n=state.note(noteID);check(n&&n->kind==ehud::data::NoteKind::todo&&n->width==228&&n->height==154&&n->items.size()==1&&n->items[0].id==one&&n->items[0].text.empty(),"Source228x154 checklist begins with caller-owned blank item identity");
    check(state.addChecklistItem(noteID,two)&&state.note(noteID)->items.size()==2,"Add preserves first item and inserts one empty item");
    const auto before=writes;check(state.mutateChecklistItem(noteID,one,NotesState::ChecklistAction::up)&&writes==before+1,"Source up-at-top still performs accepted save/render event");
    check(state.mutateChecklistItem(noteID,one,NotesState::ChecklistAction::toggle)&&state.note(noteID)->items[0].isChecked,"Toggle mutates only target checked state");
    state.mutateChecklistItem(noteID,two,NotesState::ChecklistAction::up);check(state.note(noteID)->items[0].id==two&&state.note(noteID)->items[1].id==one&&state.note(noteID)->items[1].isChecked,"Reorder moves full item identity and state together");
    const auto empty=measured(""),placeholder=measured("New item…");std::array<NotesChecklistMeasurement,2> inputs{{{two,empty,placeholder,false},{one,empty,placeholder,true}}};
    auto layout=std::make_shared<NotesChecklistLayout>(noteID,228,154,inputs);NotesPresentationInput input;input.palette=NotesPalette::source(true,{.98,.83,.12,1});input.checklist=layout;
    NotesCardPresentation presentation(noteID);presentation.updateContent(state,input);check(layer(presentation,"/header/title").text.text=="⠿  TODO"&&layer(presentation,"/footer").text.medium&&layer(presentation,"/footer").text.fontSize==10.5,"Source TODO header and medium footer are connected artwork");
    const auto checkID=std::string("todo/")+one+"/check";check(layer(presentation,checkID).shape.fill==input.palette.accent&&layer(presentation,std::string("todo/")+one+"/tick").shape.lineWidth==1.4,"Checked source square/tick use original accent and stroke");
    check(!layer(presentation,std::string("todo/")+one+"/line/0").text.strikethrough,"Empty checked placeholder is muted but never struck through");
    const auto edit=layout->beginEditing(0,0);const auto request=state.beginEditingItem(noteID,two,edit.localRect,edit.editorScrollOffset);check(!request.multiline&&request.fontSize==11&&request.itemID==two&&!request.richText,"TODO handoff remains plain font11 with row identity");
    presentation.updateContent(state,input);check(presentation.editor()&&!presentation.editor()->multiline&&presentation.editor()->localRect==edit.localRect,"Source row external editor hides only its own text slot");
    check(layer(presentation,std::string("todo/")+one+"/line/0").text.text=="New item…","Sibling text remains while another item is editing");
    rejects([&]{state.finishEditing("A",std::string("{}"));},"Checklist commit cannot introduce rich formatting");check(state.editing().has_value(),"Rejected rich row commit preserves active item");
    check(state.finishEditing("中文😀\nnext")&&state.note(noteID)->items[0].text=="中文😀\nnext"&&state.note(noteID)->items[1].text.empty(),"Plain TODO commit preserves wrapped imported Unicode/newline and sibling text");
    auto actual=measured("中文😀\nnext");inputs[0].text=actual;inputs[0].display=actual;input.checklist=std::make_shared<NotesChecklistLayout>(noteID,228,154,inputs);presentation.updateContent(state,input);
    const auto verb=std::string("check:")+two;check(presentation.setFeedback(verb,false,false),"Full colon-qualified item action binds correct row highlight");
    const auto count=allocations.load();counting=true;for(unsigned i=0;i<120;++i){presentation.setFeedback(i%2?std::optional<std::string_view>(verb):std::nullopt,false,false);presentation.updatePlacement(state);}counting=false;check(allocations==count,"Checklist hover and unchanged placements reuse all artwork");
    state.mutateChecklistItem(noteID,two,NotesState::ChecklistAction::remove);check(state.note(noteID)->items.size()==1&&state.note(noteID)->items[0].id==one&&saved.items==state.note(noteID)->items,"Delete removes only selected row with original single-store save");
    const auto rev=state.revision();check(!state.mutateChecklistItem(noteID,two,NotesState::ChecklistAction::toggle)&&state.revision()==rev,"Stale deleted row action cannot mutate another item");
    rejects([&]{state.addChecklistItem(noteID,one);},"Duplicate row identity rejects before mutation");
}
const ehud::data::Json*find(const ehud::data::Json&j,std::string_view name){if(j.isObject()){if(j["name"].isString()&&j["name"].string()==name)return &j;for(const auto&[key,value]:j.object())if(const auto*p=find(value,name))return p;}else if(j.isArray())for(const auto&v:j.array())if(const auto*p=find(v,name))return p;return nullptr;}
Rect rectangle(const ehud::data::Json&j){const auto&a=j.array();return{a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
void original(const char*path){std::ifstream file(path);check(bool(file),"Explicit original NotesCanvas export exists");const std::string bytes{std::istreambuf_iterator<char>(file),{}};const auto j=ehud::data::Json::parse(bytes,16*1024*1024);
    const std::string note="00000000-0000-4000-8000-000000000002",a="00000000-0000-4000-8000-000000000003",b="00000000-0000-4000-8000-000000000004";
    const auto*rowA=find(j,"notes.todo.row."+a),*rowB=find(j,"notes.todo.row."+b);check(rowA&&rowB,"Both actual detached source TODO rows exist");
    const auto*lineA=find(*rowA,"notes.content.line.0"),*lineB=find(*rowB,"notes.content.line.0");check(lineA&&lineB,"Original rows share measured visible-line source tree");
    const auto first=measured("Compare geometry",171,rectangle((*lineA)["frame"]).height),second=measured("Compare actions",171,rectangle((*lineB)["frame"]).height);
    const std::array<NotesChecklistMeasurement,2> items{{{a,first,first,true},{b,second,second,false}}};NotesChecklistLayout layout(note,260,160,items);
    check(rectangle((*rowA)["frame"])==Rect{0,layout.rows()[0].origin,250,layout.rows()[0].height}&&rectangle((*rowB)["frame"])==Rect{0,layout.rows()[1].origin,250,layout.rows()[1].height},"Retained row frames equal original source export");
    ehud::data::Note record{.id=note,.kind=ehud::data::NoteKind::todo,.x=340,.y=90,.width=260,.height=160,.createdAt=0};record.items={{a,"Compare geometry",true,ehud::data::Json::Object{}},{b,"Compare actions",false,ehud::data::Json::Object{}}};NotesState state({record},{[](const auto&){},[](auto){}});state.setWorkspaceBounds({0,0,1920,1080});NotesPresentationInput input;input.palette=NotesPalette::source(true,{.98,.83,.12,1});input.checklist=std::make_shared<NotesChecklistLayout>(layout);NotesCardPresentation presentation(note);presentation.updateContent(state,input);
    const auto base="todo/"+a+"/";const std::array<std::string,3>names{base+"up",base+"down",base+"remove"};for(std::size_t k=0;k<names.size();++k){const auto&actual=layer(presentation,names[k]);const auto&expected=(*rowA)["children"].array()[k+1];check(actual.frame==rectangle(expected["frame"]),"Checklist control frame matches original source raster tree");const auto&path=expected["shape"]["path"].array();check(actual.shape.points.size()==path.size(),"Checklist stroke topology equals original source");for(std::size_t v=0;v<path.size();++v){const auto&point=path[v]["points"].array()[0].array();check(actual.shape.points[v].move==(path[v]["op"].string()=="move")&&actual.shape.points[v].point==endfield::core::Point{point[0].number(),point[1].number()},"Every arrow/delete stroke point equals original source");}}
    check(layer(presentation,base+"line/0").text.strikethrough&&layer(presentation,base+"line/0").frame==rectangle((*lineA)["frame"]),"Checked text frame and strike flag follow original source");
    for(const auto&action:layout.actions(0)){const ehud::data::Json*match{};for(const auto&candidate:j["actions"].array())if(candidate["id"].string()==action.id){match=&candidate;break;}check(match!=nullptr,"Source TODO action identity exists");auto r=action.localRect;r.x+=340;r.y+=90;check(r==rectangle((*match)["rect"])&&action.label==(*match)["label"].string(),"Every checklist action rectangle and label equals original detached NotesCanvas");}
}

}
int main(int argc,char**argv){try{check(argc<=2,"Only an optional original-source export path is accepted");run();connected();if(argc==2)original(argv[1]);std::cout<<checks<<" source TODO layout checks passed\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<e.what()<<'\n';return 1;}}
