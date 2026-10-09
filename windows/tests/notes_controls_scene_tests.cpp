#include "native/notes_controls_scene.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#endif
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace native=endfield::native;namespace modules=endfield::modules;namespace core=endfield::core;using Json=ehud::data::Json;
namespace {
unsigned checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char*m){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,m);}
std::size_t surface(const native::NotesControlsScenePlan&p,std::string_view id){for(std::size_t n=0;n<p.surfaces.size();++n)if(p.surfaces[n].id==id)return n;throw std::runtime_error("Missing source control surface");}
#ifdef _WIN32
std::size_t surface(const native::LayerScene&s,std::string_view id){auto n=s.surfaceIndex(id);if(!n)throw std::runtime_error("Missing native control surface");return *n;}
#endif
modules::NotesControlsInput fallback(){modules::NotesControlsInput i;i.manualIconAvailable=false;i.missionIconAvailable=false;return i;}
void portable(){
    modules::NotesControls source;rejects([&]{native::prepareNotesControlsScene(source);},"Uninitialized source explicitly rejects");source.update({});const auto original=source.artwork();
    rejects([&]{native::prepareNotesControlsScene(source);},"Required actual icon data never becomes silently blank");
    std::vector<native::NativeNotesControlsImage>images;for(const auto&d:source.images())images.push_back({d,Json::Object{{"asset",d.layerID=="tool:text/icon"?"manual.png":"mission.png"},{"sha256",std::string(64,'a')}}});
    const auto resolved=native::prepareNotesControlsScene(source,images);check(source.artwork()==original,"Preparing pinned image metadata never mutates source artwork");
    for(const auto&i:images){const auto n=surface(resolved,i.dependency.layerID);check(resolved.layers["children"].array()[n]["contents"]==i.contents,"Exact owner image metadata reaches its source leaf");}
    auto wrong=images;wrong[0].dependency.tint[0]=0;rejects([&]{native::prepareNotesControlsScene(source,wrong);},"Untinted/different source image dependency rejects before native load");
    wrong=images;wrong[0].sourceInTint=modules::NotesColor{.11,.11,.11,1};rejects([&]{native::prepareNotesControlsScene(source,wrong);},"Explicit image recoloring cannot bypass the exact requested source tint");
    wrong=images;wrong[0].contents["sha256"]="missing";rejects([&]{native::prepareNotesControlsScene(source,wrong);},"Unpinned image binding rejects");
    auto input=fallback();source.update(input);const auto plan=native::prepareNotesControlsScene(source);check(!plan.requiresGroupOpacity,"Source center controls permit independent retained surfaces");
    constexpr std::array<std::string_view,4>toolIDs{"tool:text","tool:todo","tool:image","tool:drawing"};
    for(std::size_t n=0;n<4;++n){const auto id=std::string(toolIDs[n]);const auto plate=surface(plan,id),tint=surface(plan,id+"/highlight/tint"),rim=surface(plan,id+"/highlight/rim"),icon=surface(plan,id+"/icon"),label=surface(plan,id+"/label");
        check(plate<tint&&tint<rim&&rim<icon&&icon<label,"Source toolbar highlight is below the exact icon and caption");
        for(auto index:{plate,tint,rim,icon,label})check(plan.surfaces[index].toolbar==n,"Every toolbar descendant follows its whole-button press");
        check(plan.surfaces[plate].local.values[12]==7+double(n)*98&&plan.surfaces[plate].local.values[13]==294,"Actual toolbar placement is source-authored");
        check(plan.surfaces[tint].feedback==n&&!plan.surfaces[tint].rim&&plan.surfaces[rim].rim,"Retained feedback binds exact source identities");
    }
    input.notesSelected=false;source.update(input);check(source.actions().empty()&&native::prepareNotesControlsScene(source).surfaces.size()==plan.surfaces.size(),"Hidden module input does not erase independently animated toolbar artwork");
    for(auto kind:{modules::NotesControlsKind::mediaSource,modules::NotesControlsKind::font,modules::NotesControlsKind::size,modules::NotesControlsKind::special,modules::NotesControlsKind::shelfMedia}){
        input=fallback();input.kind=kind;input.values={"12","16","20"};source.update(input);const auto menu=native::prepareNotesControlsScene(source);
        check(menu.requiresGroupOpacity&&menu.surfaces.front().id=="notes.menu/back"&&menu.surfaces.back().id=="notes.menu/face/native-border","Menu shadow first and face border last preserve source paint order");
        check(menu.surfaces.front().local.values[12]==-3&&menu.surfaces.front().local.values[13]==4&&menu.surfaces.front().local.values[14]==0,"Overflowing shadow remains local; sibling z-order never becomes huge geometric depth");
        check(menu.layers["masksToBounds"].isNull(),"Menu receives no invented center/workspace clip");
    }
    input.kind=modules::NotesControlsKind::color;source.update(input);images.clear();for(const auto&d:source.images())images.push_back({d,Json::Object{{"asset","wheel.png"},{"sha256",std::string(64,'a')}}});
    const auto wheel=native::prepareNotesControlsScene(source,images);const auto marker=surface(wheel,"notes.menu/wheelMarker");check(wheel.layers["children"].array()[marker]["bounds"]==Json(Json::Array{0,0,0,0})&&!wheel.layers["children"].array()[marker]["shape"]["path"].array().empty(),"Source zero-bounds wheel marker keeps real absolute path ink for native measurement");
}
#ifdef _WIN32
struct Window{HWND hwnd{};ATOM atom{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldNotesControlsSynthetic";atom=RegisterClassW(&c);if(!atom)throw std::runtime_error("Register hidden control fixture");hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOREDIRECTIONBITMAP,c.lpszClassName,L"Hidden controls",WS_POPUP,0,0,512,384,nullptr,nullptr,c.hInstance,nullptr);if(!hwnd)throw std::runtime_error("Create hidden control fixture");}~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}};
void gpu(const wchar_t*shader){
    Window window;native::Renderer renderer;renderer.initialize(window.hwnd,512,384,{native::Driver::warpForTests,shader,native::RenderTarget::offscreenForTests});
    native::LayerRasterizer raster;native::LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;
    modules::NotesControls source;source.update({});native::NativeNotesControlsScene adapter(source,raster,options);
    rejects([&]{adapter.syncContent();},"Missing artwork fails before replacing native content");check(adapter.scene().contentRevision()==0,"Rejected icon resolution leaves empty native scene untouched");
    auto input=fallback();source.update(input);check(adapter.syncContent()&&!adapter.syncContent(),"Controls raster only on content events");
    const auto tint=surface(adapter.scene(),"tool:text/highlight/tint"),rim=surface(adapter.scene(),"tool:text/highlight/rim"),icon=surface(adapter.scene(),"tool:text/icon"),caption=surface(adapter.scene(),"tool:text/label");
    check(adapter.scene().draws()[tint].opacity==0&&adapter.scene().draws()[rim].opacity==0,"Initial hidden feedback retains resources without a visible flash");
    adapter.updatePose({},1,0);native::LayerComposition composition;const std::array scenes{&adapter.scene()};composition.setScenes(renderer,scenes);renderer.setCamera(native::layerViewportProjection(512,384));composition.present(renderer);renderer.draw(false);
    const auto beforeRaster=raster.stats();const auto beforeGPU=renderer.stats();
    adapter.setFeedback("tool:text",false,false,1);adapter.updatePose({},1,1.07);composition.present(renderer);const auto half=adapter.scene().draws()[tint].opacity;
    check(half>0&&half<.62f&&adapter.requiresFrames(1.07),"Source hover uses retained finite ease-out feedback");
    adapter.updatePose({},1,1.15,{2,0,0,0});composition.present(renderer);
    check(adapter.scene().draws()[tint].opacity==.62f&&adapter.scene().draws()[icon].world.values[13]==301&&adapter.scene().draws()[caption].world.values[13]==304,"Toolbar press translates source icon/caption/highlights together");
    adapter.setFeedback({},false,true,1.2);adapter.updatePose({},1,1.2);adapter.setFeedback("tool:text",false,false,1.3);adapter.updatePose({},1,1.33);composition.present(renderer);
    check(adapter.scene().draws()[rim].opacity>0&&adapter.scene().draws()[rim].opacity<1,"Interrupted hover starts from an actual in-flight rim");
    adapter.setFeedback("tool:text",true,false,1.33);adapter.updatePose({},1,1.391);composition.present(renderer);
    check(adapter.scene().draws()[rim].opacity==1&&adapter.scene().draws()[tint].opacity==1,"Press accelerates both tint and unchanged-target rim using the source duration");
    const auto stable=raster.stats();const auto uploads=renderer.stats();allocations=0;counting=true;
    try{for(unsigned n=0;n<120;++n){const auto world=core::Matrix4::translation(double(n%5),double(n%3));adapter.updatePose(world,1,2+n*.001,{double(n%3),0,0,0});composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==stable.rasterizations&&raster.stats().textLayoutsCreated==stable.textLayoutsCreated&&renderer.stats().textureUploads==uploads.textureUploads,"Pointer/tool translation frames allocate, rasterize and upload no textures");
    check(stable.rasterizations==beforeRaster.rasterizations&&uploads.textureUploads==beforeGPU.textureUploads,"Feedback preserves all local raster resources");
    // A later unrelated control transition must not prematurely finish the old
    // control's independently running release fade.
    adapter.setFeedback("tool:todo",false,false,3);adapter.updatePose({},1,3.03);composition.present(renderer);const auto fading=adapter.scene().draws()[tint].opacity;
    adapter.setFeedback("tool:todo",true,false,3.03);adapter.updatePose({},1,3.04);composition.present(renderer);check(adapter.scene().draws()[tint].opacity>0&&adapter.scene().draws()[tint].opacity<fading,"Unchanged feedback targets retain their interrupted-release timing");
    input.kind=modules::NotesControlsKind::mediaSource;source.update(input);adapter.syncContent();adapter.updatePose(core::Matrix4::translation(10,10),1,4);composition.upload(renderer);composition.present(renderer);renderer.draw(false);
    check(adapter.requiresGroupOpacity()&&adapter.scene().draws().back().sourceID.ends_with("notes.menu/face/native-border"),"Menu keeps exact final border over its children");
    const auto oldWorld=adapter.scene().draws()[0].world;rejects([&]{adapter.updatePose(core::Matrix4::translation(90,90),.5f,4.1);},"Partial grouped menu fade rejects rather than changing source compositing");composition.present(renderer);check(adapter.scene().draws()[0].world==oldWorld,"Rejected partial opacity preserves prior scene pose");
    const auto frame=renderer.readback();check(frame.pixels[std::size_t(20)*frame.rowBytes+8*4+3]>0,"Source menu shadow remains visible outside nominal left bounds");
    composition.detach(renderer);check(renderer.stats().meshes==0&&renderer.stats().textures==0&&!IsWindowVisible(window.hwnd),"Owner detaches all controls without visible windows or retained GPU leaks");renderer.reset();
}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass shader to isolated Windows controls fixture");portable();gpu(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Notes controls scene checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{portable();std::cout<<"PASS "<<checks<<" Notes controls scene preparation checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
