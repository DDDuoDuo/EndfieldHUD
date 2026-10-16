#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;namespace d=ehud::data;
namespace {
unsigned checks{};void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
constexpr const char*id="00000000-0000-4000-8000-000000000091";constexpr const char*pinnedID="00000000-0000-4000-8000-000000000092";
n::NativeNotesWorkspaceStyle style(){n::NativeNotesWorkspaceStyle s;s.palette=m::NotesPalette::source(true,{.98,.83,.12,1});s.editor={{.12,.12,.12,1},s.palette.accent};return s;}
n::NativeNotesWorkspacePose pose(double t){n::NativeNotesWorkspacePose p;p.screenToClip=n::layerViewportProjection(640,400);p.pixelWidth=640;p.pixelHeight=400;p.time=t;return p;}
void run(HWND hwnd,const std::filesystem::path&shader){n::Renderer renderer;renderer.initialize(hwnd,640,400,{n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});n::LayerRasterizer raster;n::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=1;options.raster.paddingPoints=1;
    d::Note a{.id=id,.kind=d::NoteKind::drawing,.x=30,.y=40,.width=300,.height=240,.createdAt=0};a.drawing=m::NotesDrawing{}.encode();auto b=a;b.id=pinnedID;b.x=370;b.width=220;b.isPinned=true;
    std::size_t saves{};m::NotesState state({a,b},{[&](const auto&){++saves;},[](auto){}});state.setWorkspaceBounds({0,0,640,400});n::NativeNotesWorkspace workspace(hwnd,state,raster,style(),options);workspace.updatePose(pose(0));workspace.select(id);
    check(workspace.card(id)->drawing()&&workspace.card(id)->contentViewport()==c::Rect{5,29,290,184}&&workspace.entries().size()==6,"Drawing cards connect original content viewport plus their ink and brush ring");
    const auto initial=*state.note(id)->drawing;const auto baseline=saves;check(workspace.beginDrawing(id,{80,110})&&workspace.drawingActive(),"Pointer in drawing viewport begins the source gesture");workspace.updateDrawing({150,130});check(*state.note(id)->drawing==initial&&saves==baseline,"Painting does not update persisted Notes until mouse-up");
    n::LayerComposition composition;workspace.uploadMedia(renderer);composition.setEntries(renderer,workspace.entries());const auto painted=raster.stats();const auto gpu=renderer.stats();const auto measured=workspace.measurementStats().measurements;allocations=0;counting=true;for(unsigned f=0;f<120;++f){auto p=pose(double(f)/60);p.workspaceToScreen.values[12]=double(f)*.01;p.workspaceToScreen.values[3]=double(f)*.0000001;workspace.updatePose(p);composition.present(renderer);}counting=false;
    check(allocations==0&&raster.stats().rasterizations==painted.rasterizations&&renderer.stats().textureUploads==gpu.textureUploads&&workspace.measurementStats().measurements==measured,"Drawing tilt retains the painted ink, layouts, textures and allocations");workspace.updatePose(pose(2));
    check(workspace.endDrawing()&&!workspace.drawingActive()&&saves==baseline+1,"Mouse-up saves exactly one completed source stroke");auto drawing=m::NotesDrawing(*state.note(id)->drawing);check(drawing.strokes().size()==1&&drawing.pointCount()==2&&drawing.strokes()[0].width==8,"Normalized source stroke and initial width persist");
    check(workspace.scrollAt({90,100},.5)&&workspace.drawingWidth()==8.125&&saves==baseline+1,"Fractional wheel changes source width by delta times .25 without saving");
    check(workspace.toggleDrawingEraser({80,110})&&workspace.drawingErasing(),"Right-click inside drawing viewport toggles erase");const auto saved=*state.note(id)->drawing;workspace.beginDrawing(id,{80,110});workspace.updateDrawing({81,110});check(*state.note(id)->drawing==saved,"Erase keeps the committed document until mouse-up");workspace.endDrawing();check(m::NotesDrawing(*state.note(id)->drawing).strokes().empty()&&saves==baseline+2,"Eraser removes the touched whole stroke with one save");
    workspace.toggleDrawingEraser({80,110});workspace.setDrawingColor({.1,.3,.7,1});workspace.beginDrawing(id,{90,115});workspace.updateDrawing({110,120});workspace.setPresentation(false,true);check(!workspace.drawingActive()&&m::NotesDrawing(*state.note(id)->drawing).strokes().size()==1&&saves==baseline+3,"Section close finishes the drawing before retaining outgoing artwork");check(!workspace.beginDrawing(id,{90,115})&&!workspace.hitTest({90,115}),"Outgoing drawing cannot receive input");check(workspace.card(pinnedID)->placement().visible,"Pinned drawing stays available across module changes");
    composition.setEntries(renderer,workspace.entries());workspace.updatePose(pose(3));composition.present(renderer);workspace.settleOutgoing();composition.setEntries(renderer,workspace.entries());check(workspace.entries().size()==3,"Settled unpinned artwork leaves only the pinned card and its surfaces");
    // A fast drag (RC: the pen lagged the cursor and cut corners): each move
    // carries the samples Windows coalesced since the previous one - six per
    // frame, a 1000 Hz mouse on a 165 Hz display. Every sample is kept;
    // planning them rasterizes nothing, and each frame paints only the
    // regions its samples reach, in place, never the whole ink.
    {const auto frame=[&]{workspace.uploadMedia(renderer);composition.setEntries(renderer,workspace.entries());composition.present(renderer);};frame();
     const auto start=c::Point{380,120};check(workspace.beginDrawing(pinnedID,start),"A drag begins on the pinned drawing");frame();
     std::vector<c::Point>samples;std::size_t sent{1};const auto ink=workspace.drawingStats(pinnedID);
     for(unsigned f=0;f<30;++f){samples.clear();for(unsigned k=1;k<=6;++k){const double t=double(f*6+k)/180.;samples.push_back({380+190*t,120+50*std::sin(t*6)});}
        const auto rasters=raster.stats().rasterizations;check(workspace.updateDrawing(samples),"One move carries its coalesced samples");sent+=samples.size();
        check(raster.stats().rasterizations==rasters,"Planning a move's samples rasterizes nothing");
        const auto before=workspace.drawingStats(pinnedID);const auto gpu=renderer.stats();frame();const auto after=workspace.drawingStats(pinnedID);
        check(after.wholeRasters==before.wholeRasters&&after.textureCreations==before.textureCreations&&renderer.stats().textureUploads==gpu.textureUploads,"No frame of the drag repaints or recreates the whole ink");
        check(after.regionRasters>before.regionRasters&&after.regionRasters-before.regionRasters<=2&&after.regionPixels-before.regionPixels<210u*184u/8,"Each frame paints its six samples once, in one small region per tile");}
     const auto drawn=workspace.drawingStats(pinnedID);check(drawn.wholeRasters==ink.wholeRasters&&drawn.paints<=ink.paints+31,"The whole drag painted once per frame at most");
     const auto beforeCommit=workspace.drawingStats(pinnedID);check(workspace.endDrawing(),"Mouse-up commits the fast stroke");frame();const auto committed=workspace.drawingStats(pinnedID);
     check(committed.plan.adoptions==beforeCommit.plan.adoptions+1&&committed.regionRasters==beforeCommit.regionRasters&&committed.wholeRasters==beforeCommit.wholeRasters,"The committed stroke is already painted: pen-up rasterizes nothing");
     const m::NotesDrawing saved(*state.note(pinnedID)->drawing);check(saved.strokes().size()==1&&saved.strokes()[0].points.size()==sent,"Every coalesced sample of the fast drag is kept");
     // The eraser over several samples of one move: one repaint of the
     // erased stroke's footprint at the next frame, no whole repaint.
     check(workspace.toggleDrawingEraser({480,200})&&workspace.drawingErasing(),"Right-click toggles the eraser on the pinned drawing");check(workspace.beginDrawing(pinnedID,{470,60+29+10}),"An eraser drag begins");
     const auto rasters=raster.stats().rasterizations;const std::array<c::Point,4>across{{{470,110},{470,125},{470,140},{470,155}}};workspace.updateDrawing(across);check(raster.stats().rasterizations==rasters,"Erasing plans without rasterizing");
     const auto beforeErase=workspace.drawingStats(pinnedID);frame();const auto afterErase=workspace.drawingStats(pinnedID);check(afterErase.wholeRasters==beforeErase.wholeRasters&&afterErase.regionRasters+afterErase.clearedRegions>beforeErase.regionRasters+beforeErase.clearedRegions,"The erased stroke's footprint is repainted in place");
     check(workspace.endDrawing()&&m::NotesDrawing(*state.note(pinnedID)->drawing).strokes().empty(),"The eraser drag removes the fast stroke");frame();workspace.toggleDrawingEraser({480,200});}
    composition.detach(renderer);check(workspace.releaseResources(renderer)&&renderer.stats().resourceBytes==0,"Drawing scenes stay alive until shared publication detaches");check(!IsWindowVisible(hwnd)&&renderer.stats().presents==0,"Only hidden owned window and injected synthetic persistence used");
}
}
int wmain(int argc,wchar_t**argv){const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 1;int status{};HWND hwnd{};try{check(argc==2,"Pass native/hud.hlsl");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,L"STATIC",L"Owned drawing coordinator",WS_POPUP,0,0,640,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(hwnd!=nullptr,"Create hidden drawing coordinator fixture");run(hwnd,argv[1]);std::cout<<"PASS "<<checks<<" native drawing workspace checks\n";}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';status=1;}if(hwnd)DestroyWindow(hwnd);CoUninitialize();return status;}
#else
int main(){return 0;}
#endif
