#include "native/layer_group.hpp"
#include "native/layer_text_layout.hpp"
#include "native/notes_controls_scene.hpp"
#include "modules/notes_motion.hpp"
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
namespace {std::atomic<bool> counting{};std::atomic<std::size_t> allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace gpu=endfield::native;namespace core=endfield::core;using ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
Json leaf(const char* id,double x,double y,double width,double height,double red,double green,double blue){
    return Json::Object{{"id",id},{"class","CALayer"},{"kind","layer"},{"bounds",Json::Array{0,0,width,height}},
        {"anchorPoint",Json::Array{0,0}},{"position",Json::Array{x,y}},{"backgroundColor",Json::Object{{"sRGB",Json::Array{red,green,blue,1}}}},{"children",Json::Array{}}};
}
Json root(){return Json::Object{{"bounds",Json::Array{0,0,32,20}},{"children",Json::Array{leaf("red",0,0,20,20,1,0,0),leaf("blue",8,0,20,20,0,0,1)}}};}
void pixel(const gpu::Readback& frame,unsigned x,unsigned y,std::array<int,4> rgba,const char* message){
    const auto offset=std::size_t(y)*frame.rowBytes+x*4;const std::array<int,4> order{2,1,0,3};
    for(unsigned i=0;i<4;++i)check(std::abs(int(frame.pixels[offset+order[i]])-rgba[i])<=2,message);
}
void run(HWND hwnd,const wchar_t* shader){
    gpu::Renderer renderer;renderer.initialize(hwnd,96,64,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(96,64));
    gpu::LayerRasterizer raster;gpu::LayerRasterOptions options;options.pixelsPerPoint=1;
    gpu::LayerScene local(raster),sibling(raster);local.load(root(),options);sibling.load(leaf("sibling",64,8,20,20,0,1,0),options);
    gpu::NativeLayerGroup group(local,"synthetic-menu",1);
    check(group.localCoverageBounds()==core::Rect{-1,-1,30,22},"Group extent includes actual retained raster padding");
    rejects([&]{group.entry();},"Unuploaded group cannot enter main composition");
    rejects([&]{group.uploadResources(renderer,core::Rect{0,0,28,20});},"Nominal menu rectangle cannot crop source raster ink");
    check(renderer.stats().textures==0,"Rejected crop performs no GPU upload");
    group.uploadResources(renderer);group.setPose(core::Matrix4::translation(8,8),.5f);
    gpu::LayerComposition composition;const std::array order{group.entry(),gpu::LayerCompositionEntry{&sibling,{}}};composition.setEntries(renderer,order);composition.present(renderer);renderer.draw(false);
    pixel(renderer.readback(),12,12,{128,0,0,128},"Group half fade applies once to lone red input");
    pixel(renderer.readback(),20,12,{0,0,128,128},"Overlapping child paints composite before whole-menu fade");
    pixel(renderer.readback(),70,12,{0,255,0,255},"Sibling composition remains opaque and correctly ordered");
    {
        gpu::LayerScene nextMenu(raster);nextMenu.load(leaf("next",0,0,10,10,0,1,0),options);
        gpu::NativeLayerGroup sameKind(nextMenu,"synthetic-menu",1);sameKind.uploadResources(renderer);
        check(sameKind.draws()[0].textureID!=group.draws()[0].textureID&&renderer.stats().nativeGroups==2,"Incoming same-kind menu cannot overwrite the outgoing retained target");
        composition.present(renderer);renderer.draw(false);pixel(renderer.readback(),20,12,{0,0,128,128},"Preparing another same-kind menu preserves the published outgoing pixels");
        check(sameKind.releaseResources(renderer),"Unpublished incoming menu retires independently");
    }
    rejects([&]{local.upload(renderer);},"Grouped source cannot publish another draw list");
    rejects([&]{local.releaseResources(renderer);},"Grouped source cannot bypass retained group ownership");
    rejects([&]{composition.setScenes(renderer,std::array{&local});},"Local group inputs cannot also enter main composition");
    check(!group.releaseResources(renderer),"Published carrier keeps target and local assets alive");
    check(!renderer.removeTexture(local.draws()[0].textureID)&&!renderer.removeMesh(local.draws()[0].meshID),"Local group inputs cannot retire while target references them");
    const auto before=renderer.stats();const auto rasterBefore=raster.stats();allocations=0;counting=true;
    try{for(unsigned frame=0;frame<120;++frame){core::Matrix4 tilt=core::Matrix4::translation(8+frame*.01,8);tilt.values[3]=frame*.000001;
        group.setPose(tilt,float(.3+frame*.002));composition.present(renderer);renderer.draw(false);
    }}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"Whole-menu tilt/fade allocates no C++ frame storage");
    const auto after=renderer.stats();check(after.nativeGroupRenders==before.nativeGroupRenders&&after.nativeGroupTargetAllocations==before.nativeGroupTargetAllocations&&after.meshUploads==before.meshUploads&&after.textureUploads==before.textureUploads,"Root animation never rebuilds or rerenders cached local menu");
    check(raster.stats().rasterizations==rasterBefore.rasterizations,"Root animation never rerasterizes menu artwork");
    check(!group.updateLocal(renderer),"Unchanged local feedback retains the group target");
    const auto blue=*local.surfaceIndex("blue");const gpu::LayerPlacement feedback{blue,core::Matrix4::translation(8,0),.5f,{}};local.setPlacements(std::span{&feedback,1});
    check(group.updateLocal(renderer),"Changed local feedback dirties the retained GPU target");group.setPose(core::Matrix4::translation(8,8),1);composition.present(renderer);renderer.draw(false);
    check(renderer.stats().nativeGroupRenders==before.nativeGroupRenders+1,"Local feedback causes exactly one target repaint");
    // Content replacement keeps its old group-referenced resources until the
    // complete new candidate has uploaded, then retires only those old assets.
    auto replacement=root();replacement["children"]=Json::Array{leaf("new",-4,-3,40,26,1,1,0)};local.load(replacement,options);
    rejects([&]{group.updateLocal(renderer);},"New content cannot use the old retained local bindings");
    group.uploadResources(renderer);composition.upload(renderer);composition.present(renderer);renderer.draw(false);
    pixel(renderer.readback(),6,7,{255,255,0,255},"Resized group retains negative local source coverage");
    check(renderer.stats().nativeGroups==1,"Group content replacement does not accumulate targets");
    const auto siblingOnly=std::array{&sibling};composition.setScenes(renderer,siblingOnly);check(group.releaseResources(renderer),"Detach main group entry before retiring local resources");
    check(renderer.stats().nativeGroups==0&&renderer.stats().textures==1&&renderer.stats().meshes==1,"Group teardown preserves exactly the sibling assets");composition.present(renderer);renderer.draw(false);pixel(renderer.readback(),70,12,{0,255,0,255},"Sibling remains visible after group teardown");
    composition.detach(renderer);check(renderer.stats().resourceBytes==0,"Owned group and sibling resources fully retire");
    // Exercise the actual ported Notes secondary menu, including its shadow,
    // overlapping plates, labels and last-painted border. A source root fade
    // must multiply the completed group, not all these leaves individually.
    renderer.resize(256,128);renderer.setCamera(gpu::layerViewportProjection(256,128));
    endfield::modules::NotesControls controls;endfield::modules::NotesControlsInput input;input.kind=endfield::modules::NotesControlsKind::mediaSource;controls.update(input);
    gpu::NativeNotesControlsScene menu(controls,raster,options);menu.syncContent();menu.updatePose({},1,0);
    gpu::NativeLayerGroup menuGroup(menu.scene(),"notes-media-menu",1);menuGroup.uploadResources(renderer);menuGroup.setPose(core::Matrix4::translation(16,16),1);
    composition.setEntries(renderer,std::array{menuGroup.entry()});composition.present(renderer);renderer.draw(false);const auto full=renderer.readback();
    check(menuGroup.localCoverageBounds().x<0&&menuGroup.localCoverageBounds().height>controls.bounds().height,"Actual Notes shadow and border extend beyond the nominal menu rectangle");
    for(float alpha:{.25f,.5f,.75f}){menuGroup.setPose(core::Matrix4::translation(16,16),alpha);composition.present(renderer);renderer.draw(false);const auto faded=renderer.readback();
        for(std::size_t byte=0;byte<full.pixels.size();++byte)check(std::abs(int(faded.pixels[byte])-int(std::lround(full.pixels[byte]*alpha)))<=2,"Actual overlapping Notes menu fades as one retained image");
    }
    const auto menuBefore=renderer.stats();const auto menuRaster=raster.stats();allocations=0;counting=true;
    try{for(unsigned frame=0;frame<120;++frame){const auto t=frame/600.;const auto fade=endfield::modules::notesMenuMotion(true,t);core::Matrix4 tilt=core::Matrix4::translation(16,16+fade.y);tilt.values[3]=frame*.0000001;
        menuGroup.setPose(tilt,float(fade.opacity));composition.present(renderer);renderer.draw(false);
    }}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&renderer.stats().nativeGroupRenders==menuBefore.nativeGroupRenders&&raster.stats().rasterizations==menuRaster.rasterizations,"Source Notes menu entrance and pointer tilt retain one cached target without C++ allocation or artwork repaint");
    composition.detach(renderer);check(menuGroup.releaseResources(renderer),"Actual Notes menu retires after its borrowed output detaches");check(renderer.stats().resourceBytes==0,"Actual menu leaves no retained GPU resources");renderer.reset();
}
}
int wmain(int argc,wchar_t** argv){try{check(argc==2,"Pass native HUD shader path");check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Fixture COM apartment initializes");
    WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldLayerGroupFixture";check(RegisterClassW(&c)!=0,"Own hidden fixture class registers");
    const auto hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned native group fixture",WS_POPUP,0,0,96,64,nullptr,nullptr,c.hInstance,nullptr);check(hwnd!=nullptr,"Own hidden fixture window creates");
    run(hwnd,argv[1]);check(!IsWindowVisible(hwnd),"Group test never presents a user-visible window");DestroyWindow(hwnd);UnregisterClassW(c.lpszClassName,c.hInstance);CoUninitialize();
    std::cout<<"Native layer group contracts: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){counting=false;std::cerr<<"Native layer group failure after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#endif
