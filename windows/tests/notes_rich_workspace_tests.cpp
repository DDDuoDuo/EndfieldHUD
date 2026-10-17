#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace native=endfield::native;namespace core=endfield::core;namespace mod=endfield::modules;namespace data=ehud::data;
namespace {
std::size_t checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action,const char*why){bool rejected{};try{action();}catch(const std::exception&){rejected=true;}check(rejected,why);}
constexpr const char*id="00000000-0000-4000-8000-000000000041";
struct Window{HWND value{CreateWindowExW(0,L"STATIC",L"Owned rich Notes fixture",WS_POPUP,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};Window(){check(value!=nullptr,"Hidden owned rich Notes HWND");}~Window(){DestroyWindow(value);}};
native::NativeNotesWorkspaceStyle style(){native::NativeNotesWorkspaceStyle s;s.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});s.editor={{.12,.12,.12,1},s.palette.accent};return s;}
native::NativeNotesWorkspacePose pose(double time=0){native::NativeNotesWorkspacePose p;p.screenToClip=native::layerViewportProjection(640,360);p.pixelWidth=640;p.pixelHeight=360;p.time=time;return p;}
core::notes::RichText formatting(){core::notes::TextStyle large;large.fontSize=30;large.bold=true;large.color=core::notes::RGBA{1,0,0,1};large.extra.emplace("futureStyle",true);core::notes::RichText rich;rich.extra.emplace("futureRoot","preserved");rich.runs={{0,4,large,{{"futureRun",42}}}};return rich;}
void measurement(){
    native::LayerRasterizer raster;native::NativeNotesTextMeasurer measurer(raster);const auto rich=formatting();const std::string value="A😀中\r\nsmall\n";
    const auto raw=core::notes::encodeRichText(rich,u"A😀中\r\nsmall\n").encode();const auto result=measurer.measure("rich-metric",1,value,190,12,{},raw);
    check(result->measured.richText&&result->measured.sourceRichPayload==raw,"Measured rich source identity and complete styles retained");
    check(result->measured.lines.size()==3&&result->measured.lines[0].height>result->measured.lines[1].height,"Rich lines use their actual selected font metrics; final newline keeps blank line");
    const auto&line=result->measured.lines.front();check(line.begin==0&&line.visibleTextEnd==8&&line.end==10&&line.utf16Begin==0&&line.utf16VisibleEnd==4,"CRLF and supplementary scalar ranges retain both UTF8 and UTF16 coordinates");
    const auto saved=raster.stats();check(measurer.measure("rich-metric",1,value,190,12,{},raw)==result,"Unchanged rich measurement returns same owned index");
    check(raster.stats().rasterizations==0&&raster.stats().textAnalysisFormatsCreated==saved.textAnalysisFormatsCreated,"Rich measurement cache creates no glyph bitmap or repeated format");
    const std::string large="中"+std::string(70000,'x');
    core::notes::RichText sparse;core::notes::TextStyle bold;bold.bold=true;sparse.runs={{0,1,bold,{}}};
    const auto longRaw=core::notes::encodeRichText(sparse,std::u16string(70001,u'x')).encode();
    const auto largeIndex=measurer.measure("large-rich",1,large,190,12,{},longRaw);check(largeIndex->measured.text==large&&largeIndex->measured.lines.size()>1000,"Stored long rich note indexes complete content beyond editor leaf limit without a document bitmap");
    const auto stats=raster.stats();check(stats.rasterizations==0&&stats.resourceBytes==0,"Long rich measurement retains only line metadata");
    rejects([&]{measurer.measure("rich-metric",2,"short",190,12,{},std::string("{}"));},"Malformed rich index cannot replace valid cached measurement");
    check(measurer.measure("rich-metric",1,value,190,12,{},raw)==result,"Rejected formatting leaves prior immutable measurement alive");
}
void workspace(HWND window){
    native::LayerRasterizer raster;native::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=1;
    data::Note note{.id=id,.kind=data::NoteKind::text,.text="A😀中\nsmall\nthird\nfourth\nfifth\nsixth",.x=20,.y=20,.width=260,.height=140,.createdAt=0};
    const auto original=formatting();note.richText=" \n"+core::notes::encodeRichText(original,u"A😀中\nsmall\nthird\nfourth\nfifth\nsixth").encode()+"\n ";
    std::size_t saves{};data::Note committed=note;mod::NotesState state({note},{[&](const auto&saved){++saves;committed=saved;},[](auto){}});state.setWorkspaceBounds({0,0,640,360});
    native::NativeNotesWorkspace owner(window,state,raster,style(),options);owner.updatePose(pose());
    const auto*card=owner.card(id);check(card&&card->measuredText()->richText==original,"Imported rich card is connected to the source workspace");
    const auto findLine=[&](std::size_t index)->const mod::NotesLayer&{const auto suffix="/content/line/"+std::to_string(index);for(const auto&l:owner.card(id)->layers())if(l.id.ends_with(suffix))return l;throw std::runtime_error("Missing visible rich line");};
    check(findLine(0).text.runs.size()==1&&findLine(0).text.runs[0].style.fontSize==30,"Actual settled local descriptor carries source attributed run");
    owner.beginEditing(id);owner.updatePose(pose(1));check(owner.editor()->richLayoutEnabled()&&owner.editorDocument()->text()==u"A😀中\nsmall\nthird\nfourth\nfifth\nsixth","Typed rich draft preserves full source Unicode before editing");
    check(owner.finishEditing().finished&&state.note(id)->richText==note.richText&&committed.richText==note.richText,"Unchanged entry/finish preserves raw formatting bytes including additive fields");
    owner.beginEditing(id);owner.updatePose(pose(2));owner.editor()->command(native::ProjectedEditorCommand::documentStart);owner.syncEditor();owner.editor()->command(native::ProjectedEditorCommand::right,true);owner.syncEditor();
    core::notes::FormatChange size;size.kind=core::notes::FormatKind::size;size.fontSize=42;check(owner.applyFormat(size).changed&&owner.selectionStyle()->fontSize==42,"Workspace formatting reaches selected rich draft and menu selection style");
    check(state.note(id)->richText==note.richText,"Formatting does not persist the draft before finish");
    check(owner.undo().changed&&owner.selectionStyle()->fontSize==30,"Workspace undo restores selected character style");check(owner.redo().changed&&owner.selectionStyle()->fontSize==42,"Workspace redo restores selected formatting");
    const auto glyph=owner.editor()->layout().painted();const auto before=raster.stats();const auto metrics=owner.measurementStats();
    for(unsigned frame=0;frame<120;++frame){auto p=pose(3+double(frame)/60);p.workspaceToScreen.values[3]=double(frame)*.0000006;p.workspaceToScreen.values[12]=double(frame)*.025;owner.updatePose(p);}
    check(owner.editor()->layout().painted()==glyph&&raster.stats().rasterizations==before.rasterizations&&raster.stats().textLayoutsCreated==before.textLayoutsCreated&&owner.measurementStats().measurements==metrics.measurements,"Rich workspace tilt retains exact editor layout and all settled artwork");
    owner.editor()->setScrollOffset(10.25);const auto scrolled=owner.editor()->layout().painted();check(scrolled==glyph,"Manual rich scrolling does not reshape document");
    check(owner.finishEditing().finished,"Rich draft finish accepted");const auto saved=mod::decodeNotesRichText(state.note(id)->text,state.note(id)->richText);check(saved&&saved->extra==original.extra&&saved->runs.front().style.fontSize==42&&saved->runs.front().style.extra==original.runs.front().style.extra,"Finish saves changed style with root and style extensions preserved");
    check(owner.card(id)->measuredText()->richText==saved,"Settled card remeasures the actual saved style revision");
    owner.beginEditing(id);owner.updatePose(pose(6));owner.editor()->command(native::ProjectedEditorCommand::documentEnd);owner.syncEditor();owner.editor()->character(0x4e2d,true);owner.syncEditor();check(owner.finishEditing(false).finished&&state.note(id)->text==note.text,"Explicit discard preserves committed rich text and payload");
    check(saves>0&&!IsWindowVisible(window),"Only injected saves occurred; no visible app or real data");
}
}
int main(){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 1;int status{};try{Window window;measurement();workspace(window.value);std::cout<<checks<<" native rich Notes workspace checks passed\n";}catch(const std::exception&e){std::cerr<<"Rich Notes failed after "<<checks<<" checks: "<<e.what()<<'\n';status=1;}CoUninitialize();return status;}
#endif
