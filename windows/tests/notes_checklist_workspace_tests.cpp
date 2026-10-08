#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace native=endfield::native;namespace core=endfield::core;namespace mod=endfield::modules;namespace data=ehud::data;
namespace {
unsigned checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char*m){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,m);}
constexpr const char*noteID="00000000-0000-4000-8000-000000000081";
constexpr const char*first="00000000-0000-4000-8000-000000000082";
constexpr const char*second="00000000-0000-4000-8000-000000000083";
constexpr const char*added="00000000-0000-4000-8000-000000000084";
struct Window{HWND value{CreateWindowExW(0,L"STATIC",L"Owned checklist fixture",WS_POPUP,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};Window(){check(value!=nullptr,"Hidden owned checklist HWND");}~Window(){DestroyWindow(value);}};
native::NativeNotesWorkspaceStyle style(){native::NativeNotesWorkspaceStyle s;s.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});s.editor={{.12,.12,.12,1},s.palette.accent};return s;}
native::NativeNotesWorkspacePose pose(double t){native::NativeNotesWorkspacePose p;p.screenToClip=native::layerViewportProjection(640,360);p.pixelWidth=640;p.pixelHeight=360;p.time=t;return p;}
void run(HWND hwnd){native::LayerRasterizer raster;native::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=1;
    data::Note note{.id=noteID,.kind=data::NoteKind::todo,.x=30,.y=35,.width=260,.height=140,.createdAt=0};
    note.items={{first,"第一项😀",true,data::Json::Object{{"future","preserved"}}},{second,"one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten",false,data::Json::Object{}}};
    std::size_t saves{};data::Note committed=note;mod::NotesState state({note},{[&](const auto&n){++saves;committed=n;},[](auto){}});state.setWorkspaceBounds({0,0,640,360});native::NativeNotesWorkspace workspace(hwnd,state,raster,style(),options);workspace.updatePose(pose(0));
    check(workspace.card(noteID)&&workspace.card(noteID)->contentViewport()==core::Rect{5,27,250,85},"Actual TODO source card is connected to native retained scene");
    check(workspace.entries().size()==1&&!workspace.editor(),"Settled checklist owns one card scene only");
    const auto measured=workspace.measurementStats().measurements;const auto painted=raster.stats().rasterizations;
    workspace.setFeedback(noteID,std::string("check:")+first,false,false,0);for(unsigned frame=0;frame<120;++frame){auto p=pose(double(frame)/60);p.workspaceToScreen.values[12]=double(frame)*.02;p.workspaceToScreen.values[3]=double(frame)*.0000001;workspace.updatePose(p);}
    check(workspace.measurementStats().measurements==measured&&raster.stats().rasterizations==painted,"TODO tilt/hover retains source artwork and every row measurement");
    workspace.updatePose(pose(2));const auto beforeScroll=workspace.measurementStats().measurements;check(workspace.scrollAt({60,85},20.25)&&workspace.card(noteID)->scrollOffset()==20.25,"Fractional pointer wheel scrolls source row viewport");
    check(workspace.measurementStats().measurements==beforeScroll,"Row scrolling never reshapes text");
    check(workspace.beginEditingItem(noteID,first)&&workspace.editingItemID()==first&&workspace.editorDocument()->text()==u"第一项😀","Source row editor opens exact full Unicode item");workspace.updatePose(pose(3));
    check(!workspace.editor()->richLayoutEnabled()&&!workspace.selectionStyle()&&!workspace.card(noteID)->editor()->multiline&&workspace.card(noteID)->editor()->fontSize==11,"TODO is plain font11 with no rich format state");core::notes::FormatChange bold;bold.kind=core::notes::FormatKind::bold;check(!workspace.applyFormat(bold).handled,"Rich toolbar cannot format a checklist item");
    check(workspace.entries().size()==2&&!workspace.entries()[1].after.empty(),"One item editor composes after its card and before its retained final border");
    const auto glyph=workspace.editor()->layout().painted();const auto count=raster.stats().rasterizations;for(unsigned frame=0;frame<120;++frame){auto p=pose(4+double(frame)/60);p.workspaceToScreen.values[12]=double(frame)*.04;workspace.updatePose(p);}
    check(workspace.editor()->layout().painted()==glyph&&raster.stats().rasterizations==count,"Active row editor perspective shares retained glyph/selection/IME geometry");
    workspace.editor()->character(0x4e2d,true);workspace.syncEditor();const auto beforeHistory=saves;
    check(workspace.undo().changed&&workspace.editorDocument()->text()==u"第一项😀","TODO owner exposes Unicode undo on its existing editor");
    check(workspace.redo().changed&&workspace.editorDocument()->text()==u"第一项😀中"&&saves==beforeHistory&&!workspace.selectionStyle()&&!workspace.editor()->richLayoutEnabled(),"TODO redo preserves plain font/layout and does not persist an intermediate draft");
    check(workspace.finishEditing().finished&&state.note(noteID)->items[0].text=="第一项😀中"&&state.note(noteID)->items[0].originalFields==note.items[0].originalFields&&!state.note(noteID)->richText,"Plain row finish persists target text and unknown item fields only");
    check(workspace.beginEditingItem(noteID,second),"Oversized wrapped item opens its clipped source viewport");workspace.updatePose(pose(7));
    check(workspace.editor()->maximumScrollOffset()>0&&workspace.editor()->setScrollOffset(11.25),"Long row uses bounded scrollable document editor");const auto initial=state.editing()->scrollOffset;
    const auto finishScroll=workspace.editor()->scrollOffset();check(workspace.finishEditing().finished,"Long row completion returns its session scroll");
    check(initial>0||finishScroll>0,"Row finish exercised original scroll transfer condition");check(workspace.card(noteID)->scrollOffset()>0,"Completed item scroll maps to its row origin plus source3pt inset");
    const auto unchanged=workspace.measurementStats().measurements;check(workspace.mutateChecklistItem(noteID,first,mod::NotesState::ChecklistAction::toggle)&&!state.note(noteID)->items[0].isChecked,"Checkbox mutation uses existing NotesState persistence");
    check(workspace.measurementStats().measurements==unchanged,"Check/uncheck changes artwork but retains all text measurements");workspace.mutateChecklistItem(noteID,second,mod::NotesState::ChecklistAction::up);check(state.note(noteID)->items[0].id==second,"Reorder retains complete row identity");
    check(workspace.addChecklistItem(noteID,added)&&workspace.editingItemID()==added&&workspace.editorDocument()->text().empty(),"Add scrolls bottom and begins the new empty source item");workspace.updatePose(pose(8));workspace.finishEditing(false);workspace.mutateChecklistItem(noteID,added,mod::NotesState::ChecklistAction::remove);check(state.note(noteID)->items.size()==2&&!workspace.mutateChecklistItem(noteID,added,mod::NotesState::ChecklistAction::toggle),"Removed item action cannot touch surviving row");
    workspace.togglePin(noteID);workspace.setPresentation(false);workspace.updatePose(pose(9));check(workspace.entries().size()==1&&workspace.card(noteID)->placement().visible,"Pinned TODO remains outside the center module clip");
    check(saves>0&&committed.items.size()==2&&!IsWindowVisible(hwnd),"Only injected synthetic saves and hidden owned HWND used");
}
}
int main(){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 1;int status{};try{Window window;run(window.value);std::cout<<checks<<" native checklist workspace checks passed\n";}catch(const std::exception&e){std::cerr<<"Checklist workspace failed after "<<checks<<" checks: "<<e.what()<<'\n';status=1;}CoUninitialize();return status;}
#endif
