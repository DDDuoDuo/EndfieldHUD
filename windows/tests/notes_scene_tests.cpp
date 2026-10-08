#include "native/notes_scene.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace gpu=endfield::native;
namespace mod=endfield::modules;
namespace core=endfield::core;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&&f,const char*message){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
constexpr const char*id1="00000000-0000-4000-8000-000000000001";
constexpr const char*id2="00000000-0000-4000-8000-000000000002";
// Fixed injected short-fixture lines only; this is NOT a native wrapping engine.
std::shared_ptr<const mod::NotesMeasuredText> measured(std::string text,double width){
    auto m=std::make_shared<mod::NotesMeasuredText>();m->text=std::move(text);m->width=width;std::size_t begin{};
    while(begin<m->text.size()){auto end=m->text.find('\n',begin);const auto visible=end==std::string::npos?m->text.size():end;end=end==std::string::npos?m->text.size():end+1;m->lines.push_back({begin,end,visible,m->height,16});m->height+=16;begin=end;}
    if(m->lines.empty()||m->text.back()=='\n'){m->lines.push_back({begin,begin,begin,m->height,16});m->height+=16;}return m;
}
mod::NotesPresentationInput input(std::string text,double width){return {mod::NotesPalette::source(true,{.98,.83,.12,1}),{},measured(std::move(text),width),0,{}};}
std::size_t feedbackSurface(const gpu::NativeNotesCardScene&s,std::string_view verb,bool rim){
    const auto suffix="/highlight/"+std::string(verb)+(rim?"/rim":"/tint");
    const auto draws=s.scene().draws();for(std::size_t i=0;i<draws.size();++i)if(draws[i].sourceID.ends_with(suffix))return i;
    throw std::runtime_error("Missing retained feedback surface");
}
class Window final {
public:
    Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldNotesSceneOwnedFixture";
        atom=RegisterClassW(&c);if(!atom)throw std::runtime_error("Cannot register owned Notes fixture");
        hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned isolated Notes fixture",WS_POPUP,0,0,512,256,nullptr,nullptr,c.hInstance,nullptr);
        if(!hwnd)throw std::runtime_error("Cannot create owned Notes fixture");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
    HWND hwnd{};ATOM atom{};
};
void contracts(gpu::Renderer&renderer,gpu::LayerRasterizer&raster){
    std::vector<ehud::data::Note>notes{{.id=id1,.kind=ehud::data::NoteKind::text,.text="One\nTwo",.x=20,.y=40,.width=210,.height=140,.zIndex=0,.createdAt=123},
        {.id=id2,.kind=ehud::data::NoteKind::text,.text="Sibling",.x=280,.y=40,.width=210,.height=140,.zIndex=1,.createdAt=123}};
    mod::NotesState state(std::move(notes),{[](const auto&){},[](auto){}});state.setWorkspaceBounds({0,0,512,256});
    mod::NotesCardPresentation p1(id1),p2(id2);auto in1=input("One\nTwo",192),in2=input("Sibling",192);
    p1.updateContent(state,in1);p2.updateContent(state,in2);gpu::LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;
    gpu::NativeNotesCardScene a(p1,raster,options),b(p2,raster,options);
    check(a.syncContent()&&b.syncContent()&&!a.syncContent(),"Explicit content events build each source card once");
    check(a.scene().report().unsupported.empty()&&a.scene().draws().size()==6&&a.scene().draws().back().sourceID.ends_with("/resizeGrip"),"Card raster, tiny source feedback and final grip preserve clipping and paint order");
    check(a.updatePose({},1,0)&&b.updatePose({},1,0),"Source workspace placement initializes retained cards");
    gpu::LayerComposition composition;std::array order{&a.scene(),&b.scene()};composition.setScenes(renderer,order);composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(512,256));renderer.draw(false);
    const auto frame=renderer.readback();const auto body=std::size_t(160)*frame.rowBytes+30*4;
    check(frame.pixels[body+3]==255&&frame.pixels[body+0]>0&&frame.pixels[body+0]<80,"Source card has opaque dark body outside its text/control regions");
    const auto outside=std::size_t(20)*frame.rowBytes+10*4;check(frame.pixels[outside+3]==0,"Workspace remains transparent outside cards");
    const auto pinTint=feedbackSurface(a,"pin",false),pinRim=feedbackSurface(a,"pin",true),deleteTint=feedbackSurface(a,"delete",false);
    check(a.scene().draws()[pinTint].opacity==0&&a.scene().draws()[pinRim].opacity==0,"Unhovered controls do not bake a bright highlight into card");
    const auto rasterBefore=raster.stats();const auto gpuBefore=renderer.stats();
    check(a.setFeedback("pin",false,false,1)&&a.requiresFrames(1),"Hover requests only the finite source feedback track");
    a.updatePose({},1,1);composition.present(renderer);check(a.scene().draws()[pinTint].opacity==0,"Hover begins at current presented opacity");
    a.updatePose({},1,1.07);composition.present(renderer);const auto half=a.scene().draws()[pinTint].opacity;
    check(half>0&&half<.62f,"Hover midpoint interpolates the exact easeOut target");
    a.updatePose({},1,1.140001);composition.present(renderer);check(std::abs(a.scene().draws()[pinTint].opacity-.62f)<1e-6f&&!a.requiresFrames(1.140001),"Hover settles at .62 in .14 seconds without continued demand");
    check(a.setFeedback("pin",true,false,2),"Source press starts from retained hover opacity");a.updatePose({},1,2.06);composition.present(renderer);
    check(a.scene().draws()[pinTint].opacity==1,"Pressed tint settles in .06 seconds");
    a.setFeedback("delete",true,false,3);a.updatePose({},1,3.06);composition.present(renderer);
    check(a.scene().draws()[deleteTint].opacity==1&&a.scene().draws()[pinTint].opacity>0,"New press is fast while old control releases with .14 duration");
    a.updatePose({},1,3.14);composition.present(renderer);check(a.scene().draws()[pinTint].opacity==0,"Old feedback retires fully at its own source duration");
    a.setFeedback({},false,true,4);a.updatePose({},1,4);composition.present(renderer);check(a.scene().draws()[deleteTint].opacity==0&&!a.requiresFrames(4),"Reduced motion settles feedback immediately");
    check(raster.stats().rasterizations==rasterBefore.rasterizations&&raster.stats().textLayoutsCreated==rasterBefore.textLayoutsCreated&&renderer.stats().textureUploads==gpuBefore.textureUploads,"Hover/press changes no raster, text layout or texture upload");
    const auto stable=raster.stats();const auto uploads=renderer.stats();allocations=0;counting=true;
    try{for(unsigned i=0;i<120;++i){const auto t=5+i/60.;core::Matrix4 world;world.values[3]=i*.000001;world.values[7]=-i*.0000005;
        a.updatePose(world,.75f,t);b.updatePose(world,.75f,t);composition.present(renderer);
    }}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"120 shared pointer/closing matrix frames allocate no CPU storage");
    check(raster.stats().rasterizations==stable.rasterizations&&raster.stats().textLayoutsCreated==stable.textLayoutsCreated&&renderer.stats().textureUploads==uploads.textureUploads&&renderer.stats().meshUploads==uploads.meshUploads,"Shared pointer frames retain sibling rasters and GPU geometry");
    const auto previous=a.scene().draws()[0].world;core::Matrix4 bad;bad.values.fill(0);
    rejects([&]{a.updatePose(bad,1,8);},"Singular workspace rejects before mutating card pose");composition.present(renderer);check(a.scene().draws()[0].world==previous,"Invalid pose leaves previously published card transforms intact");
    rejects([&]{a.updatePose({},std::numeric_limits<float>::infinity(),8);},"Invalid workspace opacity cannot reach native GPU constants");
    const auto siblingID=b.scene().draws()[0].textureID;const auto changedBefore=renderer.stats();state.togglePin(id1);p1.updateContent(state,in1);a.syncContent();a.updatePose({},1,8);composition.upload(renderer);composition.present(renderer);
    check(b.scene().draws()[0].textureID==siblingID&&renderer.stats().textureUploads-changedBefore.textureUploads==6,"One pinned card content event keeps every sibling texture resident");
    state.beginGesture(id1,{20,40},mod::NotesState::Gesture::move);state.dragTo({27,49});p1.updatePlacement(state);const auto moveRaster=raster.stats();a.updatePose({},1,9);composition.present(renderer);
    check(a.scene().draws()[0].world.values[12]==27&&a.scene().draws()[0].world.values[13]==49&&raster.stats().rasterizations==moveRaster.rasterizations,"Source screen-point dragging only changes retained placement");state.endGesture();
    state.setPresentation(false);p1.updatePlacement(state);p2.updatePlacement(state);a.updatePose({},1,10);b.updatePose({},1,10);composition.present(renderer);
    check(a.scene().draws()[0].opacity==1&&b.scene().draws()[0].opacity==0,"Pinned card remains visible while unpinned sibling hides on another tab");
    state.setPresentation(true);state.beginEditing(id1);p1.updateContent(state,in1);rejects([&]{a.syncContent();},"Missing real projected editor is explicit rather than flattening note data");state.detachEditor();p1.updateContent(state,in1);a.syncContent();a.updatePose({},1,11);composition.upload(renderer);
    const auto retainedRevision=a.scene().contentRevision(),retainedTextures=renderer.stats().textures;
    state.setWorkspaceBounds({0,0,2000,1200});state.beginGesture(id1,{0,0},mod::NotesState::Gesture::resize);state.dragTo({2000,1200});state.endGesture();
    const auto oversized=input("One\nTwo",state.card(id1)->rect.width-18);p1.updateContent(state,oversized);
    rejects([&]{a.syncContent();},"Unsupported oversized rounded group rejects before replacing retained scene");
    composition.present(renderer);check(a.scene().contentRevision()==retainedRevision&&renderer.stats().textures==retainedTextures,"Failed oversized update leaves published references and retained source bindings valid");
    state.beginGesture(id1,{0,0},mod::NotesState::Gesture::resize);state.dragTo({210-state.card(id1)->rect.width,140-state.card(id1)->rect.height});state.endGesture();p1.updateContent(state,in1);a.syncContent();a.updatePose({},1,12);composition.upload(renderer);
    const std::array onlyA{&a.scene()};composition.setScenes(renderer,onlyA);check(renderer.stats().textures==6&&renderer.stats().meshes==6,"Removing a sibling retires its assets after the combined list replacement");
    composition.detach(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0,"One owner detach releases all Notes assets before adapter destruction");
    // Source permits screen-clamped cards shorter than their header. Controls
    // and grip then overlap; their rounded alpha clipping and order still hold.
    ehud::data::Note tiny{.id=id1,.kind=ehud::data::NoteKind::text,.text="One",.width=100,.height=70,.createdAt=123};
    mod::NotesState tinyState({tiny},{[](const auto&){},[](auto){}});tinyState.setWorkspaceBounds({0,0,100,12});mod::NotesCardPresentation tinyPresentation(id1);tinyPresentation.updateContent(tinyState,input("One",82));
    gpu::NativeNotesCardScene tinyScene(tinyPresentation,raster,options);tinyScene.syncContent();tinyScene.updatePose({},1,0);tinyScene.setFeedback("delete",true,true,1);tinyScene.updatePose({},1,1);
    const std::array tinyOnly{&tinyScene.scene()};composition.setScenes(renderer,tinyOnly);composition.present(renderer);renderer.draw(false);
    check(tinyScene.scene().draws().back().sourceID.ends_with("/resizeGrip")&&tinyScene.scene().report().unsupported.empty(),"Overlapping tiny-card feedback retains exact rounded ancestor clip and grip-last order");
    const auto tinyImage=renderer.readback();const auto below=std::size_t(13)*tinyImage.rowBytes+95*4;check(tinyImage.pixels[below+3]==0,"Tiny feedback cannot leak below its source card clip");composition.detach(renderer);
}
}
int wmain(int argc,wchar_t**argv){try{
    check(argc==2,"Pass original native HUD shader path");const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);check(SUCCEEDED(hr),"Owned fixture COM initializes");
    {Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,512,256,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});gpu::LayerRasterizer raster;
        contracts(renderer,raster);check(!IsWindowVisible(window.hwnd),"Synthetic Notes test never shows its owned window");check(raster.stats().entries==0,"Adapter teardown releases local cache entries");renderer.reset();}
    CoUninitialize();std::cout<<"Native Notes scene contracts: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception&e){counting=false;std::cerr<<"Native Notes scene contract failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
