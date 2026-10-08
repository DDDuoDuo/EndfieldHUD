#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace gpu=endfield::native;namespace core=endfield::core;namespace mod=endfield::modules;namespace data=ehud::data;
namespace {
std::size_t checks{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char*why){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
constexpr const char*one="00000000-0000-4000-8000-000000000001";
constexpr const char*two="00000000-0000-4000-8000-000000000002";
constexpr const char*three="00000000-0000-4000-8000-000000000003";
struct Window {
    ATOM atom{};HWND hwnd{};
    Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldNotesWorkspaceOwnerFixture";atom=RegisterClassW(&c);check(atom!=0,"Owned workspace fixture class");
        hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Hidden synthetic Notes owner",WS_POPUP,0,0,640,360,nullptr,nullptr,c.hInstance,nullptr);check(hwnd!=nullptr,"Owned workspace fixture HWND");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
};
gpu::NativeNotesWorkspaceStyle style(){gpu::NativeNotesWorkspaceStyle s;s.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});s.editor={{.12,.12,.12,1},s.palette.accent};return s;}
gpu::NativeNotesWorkspaceOptions options(){gpu::NativeNotesWorkspaceOptions o;o.raster.pixelsPerPoint=1;o.raster.paddingPoints=1;return o;}
gpu::NativeNotesWorkspacePose pose(double time=0){gpu::NativeNotesWorkspacePose p;p.screenToClip=gpu::layerViewportProjection(640,360);p.pixelWidth=640;p.pixelHeight=360;p.time=time;return p;}
data::Note note(std::string id,std::string text,double x=20,double y=20,std::int64_t z=0){return {.id=std::move(id),.kind=data::NoteKind::text,.text=std::move(text),.x=x,.y=y,.width=210,.height=140,.zIndex=z,.createdAt=123};}
void run(HWND hwnd,gpu::Renderer&renderer){
    std::size_t saves{},removals{};std::string saved;bool failSave{},failRemove{};
    mod::NotesState state({note(one,"Actual text\n终末地 日本語 한국어 😀"),note(two,"",300,40,1)},
        {[&](const data::Note&n){if(failSave)throw std::runtime_error("Injected save failure");++saves;saved=n.text;},[&](std::string_view){if(failRemove)throw std::runtime_error("Injected delete failure");++removals;}});
    state.setWorkspaceBounds({0,0,640,360});gpu::LayerRasterizer raster;
    gpu::NativeNotesWorkspace workspace(hwnd,state,raster,style(),options());gpu::LayerComposition composition;
    auto publish=[&]{composition.setEntries(renderer,workspace.entries());composition.present(renderer);check(workspace.collectRetired(renderer),"Replacement permits explicit retired-scene collection");};
    workspace.updatePose(pose());publish();renderer.setCamera(gpu::layerViewportProjection(640,360));renderer.draw(false);
    check(workspace.entries().size()==2&&workspace.stats().cards==2&&workspace.measurementStats().measurements==2,"One retained card and measurement per visible plain record");
    check(saves==0&&removals==0&&!state.editing(),"Constructing retained workspace writes no data or editor state");
    const auto*initialOne=workspace.entries()[0].scene;const auto firstRevision=workspace.card(one)->contentRevision();
    const auto secondRevision=workspace.card(two)->contentRevision();const auto measured=workspace.measurementStats().measurements;
    workspace.select(std::string(two));check(workspace.card(one)->contentRevision()==firstRevision&&workspace.card(two)->contentRevision()>secondRevision,"Selecting one card rebuilds only its changed appearance");
    check(workspace.measurementStats().measurements==measured&&workspace.entries()[0].scene==initialOne,"Selection retains measurement and sibling scene identity");publish();
    const auto emptyMeasurement=workspace.card(two)->measuredText();check(!emptyMeasurement->text.empty(),"Settled empty card measures the source placeholder");
    workspace.beginEditing(two);workspace.updatePose(pose(1));publish();
    check(workspace.editorDocument()&&workspace.editorDocument()->text().empty()&&state.editing()->text.empty(),"Editing an empty card starts an empty draft, never its measured placeholder");
    check(workspace.entries().size()==3&&workspace.entries()[2].scene==&workspace.editor()->scene()&&workspace.entries()[2].after.size()==1,"Editor follows its own card with the final source border appended last");
    check(workspace.entries()[2].after[0].sourceID==composition.draws().back().sourceID,"Single shared composition preserves after-editor border order");
    const auto generation=workspace.editorGeneration();check(generation!=0&&workspace.takeEditorChanges(generation+1)==0,"Stale owner messages cannot access the active field");
    workspace.editor()->character(0x4e2d,true);workspace.editor()->character(0x1f600,true);workspace.syncEditor();publish();
    check(workspace.editorDocument()->text()==u"中😀"&&state.note(two)->text.empty(),"Projected field edits remain a draft until an explicit finish");
    const auto rasterBefore=raster.stats();const auto gpuBefore=renderer.stats();const auto measureBefore=workspace.measurementStats();const auto layout=workspace.editor()->layout().painted()->layoutIdentity();
    const auto visits=workspace.stats().poseCardVisits;allocations=0;counting=true;
    try{for(unsigned f=0;f<120;++f){auto p=pose(2+double(f)/60);p.workspaceToScreen.values[3]=double(f)*.0000006;p.workspaceToScreen.values[7]=-double(f)*.0000003;p.workspaceToScreen.values[12]=double(f)*.025;p.opacity=.75f;workspace.updatePose(p);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"120 editor/card pointer poses allocate no C++ storage");
    check(workspace.stats().poseCardVisits-visits==240,"Pose visits exactly the two cached visible cards");
    check(raster.stats().rasterizations==rasterBefore.rasterizations&&raster.stats().textLayoutsCreated==rasterBefore.textLayoutsCreated&&workspace.measurementStats().measurements==measureBefore.measurements&&renderer.stats().textureUploads==gpuBefore.textureUploads,"Pointer/closing poses perform no raster, layout, measure or texture work");
    check(workspace.editor()->layout().painted()->layoutIdentity()==layout,"Editor keeps the exact painted DWrite layout across tilt");
    const auto finish=workspace.finishEditing();check(finish.finished&&finish.saved&&state.note(two)->text=="中😀"&&saved=="中😀"&&!state.editing(),"Explicit finish commits exact UTF-8 through the one injected state owner");
    check(!workspace.editor()&&workspace.stats().retiredEditors==1,"Stopped editor remains alive until owner composition replacement");
    rejects([&]{workspace.collectRetired(renderer);},"Premature retirement rejects an editor still borrowed by the composition");
    check(workspace.stats().retiredEditors==1,"Rejected retirement preserves the borrowed editor scene");publish();
    check(workspace.stats().retiredEditors==0&&workspace.entries().size()==2,"One replacement retires the stopped editor and border reference");

    // No scene traversal/text comparison or allocation on the movement path.
    workspace.beginGesture(two,{310,45},mod::NotesState::Gesture::move);publish();const auto movedRaster=raster.stats();const auto movedMeasure=workspace.measurementStats();const auto movedRevision=workspace.card(two)->contentRevision();
    allocations=0;counting=true;try{for(unsigned f=0;f<120;++f){workspace.dragTo({312+double(f)*.2,46+double(f)*.1});composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&workspace.card(two)->contentRevision()==movedRevision,"120 source move samples retain card content and allocate no storage");
    check(raster.stats().rasterizations==movedRaster.rasterizations&&workspace.measurementStats().measurements==movedMeasure.measurements,"Moving a card never remeasures/rasterizes text");
    workspace.endGesture();publish();workspace.updatePose(pose(5));
    const auto hit=workspace.hitTest({state.card(two)->rect.x+10,state.card(two)->rect.y+10});check(hit&&hit->noteID==two,"Owner hit routing uses the same projected saved workspace origin");
    workspace.setFeedback(two,"pin",false,false,5);check(workspace.requiresFrames(5.01),"Existing source hover track requests caller frames");workspace.updatePose(pose(5.2));composition.present(renderer);check(!workspace.requiresFrames(5.2),"Settled workspace owns no perpetual frame demand");

    const auto beforeClose=workspace.card(two)->contentRevision();workspace.setPresentation(false,true);check(!state.selection()&&workspace.card(two)->contentRevision()>beforeClose,"Closing refreshes appearance when source clears selected unpinned note");
    auto closing=pose(6);closing.opacity=.6f;closing.workspaceToScreen=core::Matrix4::translation(7,3);workspace.updatePose(closing);publish();
    check(workspace.entries().size()==2&&workspace.entries()[1].scene->draws()[0].opacity>.59f,"Outgoing cards retain source visibility while target selection is hidden");
    check(!workspace.hitTest({40,40}),"Outgoing-only cards do not acquire new input");
    auto duringClose=style();duringClose.palette=mod::NotesPalette::source(false,{.1,.2,.8,1});duringClose.editor={{.96,.96,.96,1},duringClose.palette.accent};
    const auto*oldClosingScene=workspace.entries()[0].scene;workspace.setStyle(duringClose);
    check(workspace.entries().size()==2&&workspace.entries()[0].scene!=oldClosingScene&&workspace.stats().retiredCards==2,"Appearance event replaces artwork while retaining both outgoing card lifetimes");
    publish();
    check(!state.notesSelected()&&workspace.entries()[0].scene->draws()[0].opacity>.59f&&workspace.entries()[0].scene->draws()[0].world.values[12]>=7,"Outgoing appearance replacement preserves current opacity and workspace pose");
    check(workspace.stats().retiredCards==0&&!workspace.hitTest({40,40}),"Replacement safely retires old artwork without giving outgoing cards input");
    workspace.setStyle(style());publish();
    workspace.settleOutgoing();publish();check(workspace.entries().empty(),"Owner settles outgoing cards without a second clock");
    const auto hiddenVisits=workspace.stats().poseCardVisits;workspace.updatePose(pose(7));check(workspace.stats().poseCardVisits==hiddenVisits,"Hidden frames visit no cached or persisted notes");
    workspace.setPresentation(true);workspace.togglePin(two);workspace.setPresentation(false);workspace.updatePose(pose(8));publish();
    check(workspace.entries().size()==1&&workspace.card(two)->placement().visible,"Pinned Notes remains composed outside its selected module");
    workspace.setPresentation(true);workspace.requestDeletion(one);check(removals==0&&state.note(one),"Request deletion opens owner confirmation without removing data");publish();
    failRemove=true;check(!workspace.confirmDeletion(one)&&state.note(one),"Failed injected deletion retains record and card");failRemove=false;
    check(workspace.confirmDeletion(one)&&!state.note(one)&&workspace.stats().retiredCards==1,"Successful explicit confirmation moves removed card into retained retirement");
    rejects([&]{workspace.collectRetired(renderer);},"Removed card survives while prior composition still borrows it");publish();check(removals==1&&workspace.stats().cards==1,"Replacement retires only the removed card");
    const auto revision=state.revision(),size=state.notes().size();rejects([&]{workspace.setWorkspaceBounds({0,0,30,20});},"Unsupported narrow local artwork rejects before workspace mutation");
    check(state.revision()==revision&&state.notes().size()==size&&state.workspaceBounds()==core::Rect{0,0,640,360},"Rejected native bounds preserve state and records");
    workspace.createText(three,456);workspace.updatePose(pose(9));publish();check(state.note(three)&&state.note(three)->createdAt==456&&workspace.editorDocument()->text().empty(),"Creation delegates source identity/time/geometry and opens one empty field");
    workspace.editor()->character('x');workspace.syncEditor();failSave=true;const auto unsaved=workspace.finishEditing();check(unsaved.finished&&!unsaved.saved&&state.note(three)->text=="x"&&state.unsaved().contains(three),"Failed save preserves the actual draft in NotesState");failSave=false;publish();
    const auto newStyle=style();check(!workspace.setStyle(newStyle),"Equal appearance is a no-op");
    auto light=newStyle;light.palette=mod::NotesPalette::source(false,{.1,.2,.8,1});light.editor={{.96,.96,.96,1},light.palette.accent};workspace.setStyle(light);workspace.updatePose(pose(10));
    check(workspace.stats().retiredCards==2,"Appearance replacement retains former card scenes until republished");publish();
    composition.detach(renderer);check(workspace.releaseResources(renderer)&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Owner detach releases only workspace resources");
}
void boundaries(HWND hwnd){
    auto longNote=note(one,std::string(65537,'a'));mod::NotesState longState({longNote},{[](const auto&){},[](auto){}});longState.setWorkspaceBounds({0,0,640,360});gpu::LayerRasterizer raster;
    {gpu::NativeNotesWorkspace workspace(hwnd,longState,raster,style(),options());const auto before=longState.revision();rejects([&]{workspace.beginEditing(one);},"Short-editor capacity rejects complete long document before editing");
        check(!longState.editing()&&longState.revision()==before&&longState.note(one)->text.size()==65537,"Capacity failure never truncates or changes stored text");}
    auto rich=note(two,"Rich source");rich.richText="{\"runs\":[]}";mod::NotesState hidden({rich},{[](const auto&){},[](auto){}},false);
    {gpu::NativeNotesWorkspace workspace(hwnd,hidden,raster,style(),options());check(workspace.entries().empty(),"Hidden unsupported records are not rasterized");const auto before=hidden.revision();rejects([&]{workspace.setPresentation(true);},"Visible rich record rejects rather than flattening");check(hidden.revision()==before&&!hidden.notesSelected()&&hidden.note(two)->richText==rich.richText,"Rejected rich presentation leaves original payload and visibility untouched");}
    mod::NotesState tinyState({note(one,"")},{[](const auto&){},[](auto){}});tinyState.setWorkspaceBounds({0,0,100,50});
    {gpu::NativeNotesWorkspace workspace(hwnd,tinyState,raster,style(),options());const auto before=tinyState.revision();rejects([&]{workspace.beginEditing(one);},"Tiny field rejects source-editor ordering limitation before state changes");check(!tinyState.editing()&&tinyState.revision()==before,"Tiny editor rejection retains settled card/state");}
    mod::NotesState limited({note(one,"")},{[](const auto&){},[](auto){}});auto oneCard=options();oneCard.maximumRetainedCards=1;
    {gpu::NativeNotesWorkspace workspace(hwnd,limited,raster,style(),oneCard);const auto before=limited.revision();rejects([&]{workspace.createText(two,123);},"Explicit retained capacity rejects creation before persistence");check(limited.revision()==before&&limited.notes().size()==1,"Card capacity failure preserves original records");}
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==2,"Pass native hud.hlsl path");check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Owned COM test apartment");
    {Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,640,360,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});run(window.hwnd,renderer);boundaries(window.hwnd);check(!IsWindowVisible(window.hwnd),"Coordinator tests never show or activate a window");renderer.reset();}
    CoUninitialize();std::cout<<"Native Notes workspace owner contracts: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception&e){counting=false;std::cerr<<"Native Notes workspace owner failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#endif
