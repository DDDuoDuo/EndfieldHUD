#include "native/notes_drawing_scene.hpp"
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;
namespace {
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Window{HWND h{};ATOM a{};Window(){WNDCLASSW cl{};cl.lpfnWndProc=DefWindowProcW;cl.hInstance=GetModuleHandleW(nullptr);cl.lpszClassName=L"OwnedNotesDrawingFixture";a=RegisterClassW(&cl);check(a!=0,"Register own drawing fixture");h=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,cl.lpszClassName,L"Hidden drawing fixture",WS_POPUP,0,0,320,260,nullptr,nullptr,cl.hInstance,nullptr);check(h&&!IsWindowVisible(h),"Drawing fixture stays hidden");}~Window(){if(h)DestroyWindow(h);if(a)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(a)),GetModuleHandleW(nullptr));}};
void run(const std::filesystem::path&shader){Window w;n::Renderer renderer;renderer.initialize(w.h,320,260,{n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});n::LayerRasterizer raster;n::LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;n::NativeNotesDrawingScene scene(raster,options);m::NotesDrawing document;
    check(document.append({{{.2,.5},{.8,.5}},8,{1,0,0,1}}),"Synthetic source-normalized stroke accepted");check(scene.setCompleted(document,1,{300,240})&&!scene.setCompleted(document,1,{300,240}),"Equal document/size keeps the completed raster");
    scene.updatePose(c::Matrix4::translation(10,10),1);n::LayerComposition composition;composition.setEntries(renderer,scene.entries());composition.present(renderer);renderer.setCamera(n::layerViewportProjection(320,260));renderer.draw(false);auto image=renderer.readback();const auto at=std::size_t(131)*image.rowBytes+160*4;check(image.pixels[at+2]>240&&image.pixels[at+3]>240,"Completed strokes paint in the exact source viewport");
    check(scene.entries().size()==3&&scene.entries()[0].scene->report().unsupported.empty()&&scene.entries()[1].scene->report().unsupported.empty(),"Completed/live/brush surfaces retain original paths without unsupported artwork");
    const auto before=scene.stats();m::DrawingStroke live{{{.4,.25}},12,{0,0,1,1}};scene.setLive(&live,1);composition.setEntries(renderer,scene.entries());check(scene.stats().completedRasters==before.completedRasters&&scene.stats().liveRasters==before.liveRasters+1,"Painting never rerasterizes completed strokes");
    const auto stableRaster=raster.stats().rasterizations;scene.setBrush(c::Point{100,90},8,{1,1,1,1});composition.setEntries(renderer,scene.entries());const auto stable=scene.stats();const auto gpu=renderer.stats();allocations=0;counting=true;for(unsigned i=0;i<120;++i){scene.setBrush(c::Point{100+double(i)*.1,90},8,{1,1,1,1});scene.updatePose(c::Matrix4::translation(10+double(i)*.01,10),1);composition.present(renderer);}counting=false;
    check(allocations==0&&scene.stats().completedRasters==stable.completedRasters&&scene.stats().liveRasters==stable.liveRasters&&scene.stats().brushRasters==stable.brushRasters,"120 brush/tilt samples allocate and rasterize nothing");check(renderer.stats().textureUploads==gpu.textureUploads&&raster.stats().rasterizations==stableRaster+1,"Brush movement uses the existing small ring texture");
    const auto draws=scene.entries()[2].scene->draws();check(draws.size()==1&&draws[0].masks.size()==1&&draws[0].masks[0].bounds==c::Rect{0,0,300,240}&&draws[0].masks[0].cornerRadius==3,"Brush clips to the source rounded card, not the drawing viewport");
    scene.setLive(nullptr,2);scene.setBrush({},8,{1,1,1,1});composition.present(renderer);check(scene.entries()[1].scene->draws()[0].opacity==0&&scene.entries()[2].scene->draws()[0].opacity==0,"Ending gesture retains resources but hides transient artwork");
    const auto resize=scene.stats();scene.setCompleted(document,1,{310,250});check(scene.stats().completedRasters==resize.completedRasters+1,"Same document revision at resized viewport rescales original normalized strokes");composition.setEntries(renderer,scene.entries());composition.detach(renderer);check(scene.releaseResources(renderer)&&renderer.stats().resourceBytes==0,"All three drawing surfaces retire only after composition detaches");check(!IsWindowVisible(w.h)&&renderer.stats().presents==0,"No desktop capture, visible window or application data");
}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 1;int result{};try{check(argc==2,"Pass native/hud.hlsl");run(argv[1]);std::cout<<"PASS "<<checks<<" retained Notes drawing checks\n";}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#else
int main(){return 0;}
#endif
