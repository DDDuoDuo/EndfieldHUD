// The host's shell SourceScene over the packaged catalog (it carries the
// bottom-left ID card textures the profile area binds): once upload() created
// the immutable GPU textures, the scene keeps no CPU copy of an installed
// texture except the desktop originals a runtime replacement can restore, the
// frame stays pixel-identical, replace/restore still works, and a fresh upload
// reads the released mips from the same verified file again. Hidden HWND,
// WARP and the packaged (synthetic, Mac-exported) catalog only.
#include "native/renderer.hpp"
#include "native/source_scene.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
namespace {
namespace gpu=endfield::native;namespace fs=std::filesystem;
unsigned checks{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>bool rejects(F f){try{f();}catch(const std::exception&){return true;}return false;}
struct Window{HWND hwnd{};Window(unsigned w,unsigned h){hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"Owned hidden shell scene fixture",WS_POPUP,0,0,int(w),int(h),nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(hwnd&&!IsWindowVisible(hwnd),"The fixture window stays hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);}};
void run(const fs::path&shader,const fs::path&catalog){
    gpu::SourceScene scene(gpu::CompiledSourceScene{catalog,{}});
    const auto loaded=scene.stats();check(loaded.textures>0&&loaded.textureCPUBytes>0,"The compiled catalog installs its textures with their mips");
    const auto&p=scene.parameters();Window w(p.width,p.height);gpu::Renderer renderer;
    renderer.initialize(w.hwnd,p.width,p.height,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setDrawList({});
    scene.upload(renderer.sourceGraphics());
    const auto uploaded=scene.stats();const auto graphics=renderer.sourceGraphics().stats();
    check(graphics.textures==loaded.textures,"Every installed texture is on the GPU");
    check(uploaded.textureCPUBytes<loaded.textureCPUBytes&&uploaded.retainedCPUBytes+(loaded.textureCPUBytes-uploaded.textureCPUBytes)==loaded.retainedCPUBytes,
          "Once uploaded, the scene releases the CPU mips its immutable GPU textures hold");
    std::cout<<"textures "<<loaded.textures<<", CPU texture bytes "<<loaded.textureCPUBytes<<" -> "<<uploaded.textureCPUBytes<<'\n';
    renderer.draw(false);const auto first=renderer.readback();
    check(std::any_of(first.pixels.begin(),first.pixels.end(),[](auto v){return v!=0;}),"The uploaded shell frame paints");
    // The ID card avatar original is kept: a runtime replacement and its
    // restore upload through flush() and the frame returns exactly.
    if(scene.textureInfo("desktop.profile.avatar")){
        std::vector<std::uint8_t>pixels(8*8*4,255);
        check(scene.replaceTexture("desktop.profile.avatar",8,8,pixels,1),"The ID card avatar takes a runtime replacement");
        scene.flush(renderer.sourceGraphics());renderer.draw(false);
        check(scene.restoreTexture("desktop.profile.avatar"),"The packet avatar is restored");
        scene.flush(renderer.sourceGraphics());renderer.draw(false);const auto restored=renderer.readback();
        check(restored.pixels==first.pixels,"The restored original uploads from its kept pixels and draws exactly as before");
    }
    check(rejects([&]{scene.writeCompiled(fs::temp_directory_path()/"EndfieldHUD-never-written.ehscene");}),"An uploaded compiled scene is never written back");
    // A fresh graphics target: the released mips are read from the same file.
    scene.upload(renderer.sourceGraphics());
    check(renderer.sourceGraphics().stats().textures==loaded.textures&&renderer.sourceGraphics().stats().textureUploads>=graphics.textureUploads+loaded.textures&&
          scene.stats().textureCPUBytes==uploaded.textureCPUBytes,"A fresh upload reads the released mips from the compiled file again and releases them once uploaded");
    renderer.draw(false);const auto again=renderer.readback();
    check(again.pixels==first.pixels,"The re-uploaded frame is pixel-identical");
    renderer.reset();
}
}
int wmain(int argc,wchar_t**argv){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{check(SUCCEEDED(hr)&&argc==3,"Pass the shared shader and the packaged shell catalog");run(argv[1],argv[2]);CoUninitialize();std::cout<<"PASS "<<checks<<" shell scene upload checks\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
