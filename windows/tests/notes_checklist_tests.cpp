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
    check(actions[2].localRect==Rect{165.5,30,16,16}&&actions[3].localRect==Rect{183.5,30,16,16}&&actions[4].localRect==Rect{201.5,30,16,16},"DOCS-210: up/down/remove hit rects are the drawn 16 x 16 buttons (row x w - 63 / 45 / 27 - 4.5, y 3)");
    check(actions.back().localRect==Rect{5,80,204,20},"+ Add item is the plate at (5, h - 25, w - 24, 20)");
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
    NotesCardPresentation presentation(noteID);presentation.updateContent(state,input);const auto controlOf=[&](std::string_view verb)->const NotesControl&{for(const auto&c:presentation.controls())if(c.id.ends_with(":"+std::string(verb)))return c;throw std::runtime_error("Missing Notes control");};
    check(presentation.title().text=="TODO"&&controlOf("add").frame==Rect{5,154-25,228-24,20}&&controlOf("add").text=="+ Add item"&&controlOf("add").kind==NotesControlKind::formatToggle,"The TODO caption and the + Add item plate (5, h - 25, w - 24, 20)");
    check(controlOf(std::string("check:")+one).on&&!controlOf(std::string("check:")+two).on&&controlOf(std::string("check:")+one).glyph==NotesControlGlyph::check,"The checked row's check plate is on (the theme colour with a tick)");
    check(!layer(presentation,std::string("todo/")+one+"/line/0").text.strikethrough,"Empty checked placeholder is muted but never struck through");
    const auto edit=layout->beginEditing(0,0);const auto request=state.beginEditingItem(noteID,two,edit.localRect,edit.editorScrollOffset);check(!request.multiline&&request.fontSize==11&&request.itemID==two&&!request.richText,"TODO handoff remains plain font11 with row identity");
    presentation.updateContent(state,input);check(presentation.editor()&&!presentation.editor()->multiline&&presentation.editor()->localRect==edit.localRect,"Source row external editor hides only its own text slot");
    check(layer(presentation,std::string("todo/")+one+"/line/0").text.text=="New item…","Sibling text remains while another item is editing");
    rejects([&]{state.finishEditing("A",std::string("{}"));},"Checklist commit cannot introduce rich formatting");check(state.editing().has_value(),"Rejected rich row commit preserves active item");
    check(state.finishEditing("中文😀\nnext")&&state.note(noteID)->items[0].text=="中文😀\nnext"&&state.note(noteID)->items[1].text.empty(),"Plain TODO commit preserves wrapped imported Unicode/newline and sibling text");
    auto actual=measured("中文😀\nnext");inputs[0].text=actual;inputs[0].display=actual;input.checklist=std::make_shared<NotesChecklistLayout>(noteID,228,154,inputs);presentation.updateContent(state,input);
    const auto verb=std::string("check:")+two;check(std::any_of(presentation.actions().begin(),presentation.actions().end(),[&](const auto&a){const auto&f=controlOf(verb).frame;return a.verb==verb&&a.localRect.x+3.5==f.x&&a.localRect.y+4==f.y&&a.localRect.width==21&&a.localRect.height==22;}),"The row's check plate (14 x 14) is centred in its 21 x 22 hit rect");
    const auto count=allocations.load();counting=true;for(unsigned i=0;i<120;++i)presentation.updatePlacement(state);counting=false;check(allocations==count,"Unchanged placements reuse all artwork");
    state.mutateChecklistItem(noteID,two,NotesState::ChecklistAction::remove);check(state.note(noteID)->items.size()==1&&state.note(noteID)->items[0].id==one&&saved.items==state.note(noteID)->items,"Delete removes only selected row with original single-store save");
    const auto rev=state.revision();check(!state.mutateChecklistItem(noteID,two,NotesState::ChecklistAction::toggle)&&state.revision()==rev,"Stale deleted row action cannot mutate another item");
    rejects([&]{state.addChecklistItem(noteID,one);},"Duplicate row identity rejects before mutation");
}
}
int main(int argc,char**argv){try{(void)argc;(void)argv;run();connected();std::cout<<checks<<" source TODO layout checks passed\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<e.what()<<'\n';return 1;}}
