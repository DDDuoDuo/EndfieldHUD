#include "render/native_renderer.h"
#include <roapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using ehud::render::NativeRenderer;
using ehud::scene::Frame;
using ehud::scene::Graphic;
using endfield::platform::FrozenDesktopSnapshot;
using endfield::platform::FrozenSnapshot;
unsigned checks{};
void check(bool condition,const char* reason) {
    ++checks;if(!condition)throw std::runtime_error(reason);
}
void checked(HRESULT status,const char* operation) {
    ++checks;
    if(FAILED(status)) {
        std::ostringstream message;message<<operation<<" HRESULT=0x"<<std::hex<<static_cast<unsigned long>(status);
        throw std::runtime_error(message.str());
    }
}
class Window final {
public:
    Window() {
        instance_=GetModuleHandleW(nullptr);
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=instance_;
        type.lpszClassName=L"EndfieldHUD.Isolated.NativeRendererLifecycle";
        check(RegisterClassW(&type)!=0,"Register isolated hidden window class");
        window_=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,type.lpszClassName,
            L"Synthetic renderer lifecycle",WS_POPUP,0,0,96,72,nullptr,nullptr,instance_,nullptr);
        check(window_!=nullptr,"Create one hidden window for all swapchain generations");
    }
    ~Window() {
        if(window_)DestroyWindow(window_);
        UnregisterClassW(L"EndfieldHUD.Isolated.NativeRendererLifecycle",instance_);
    }
    HWND get() const {return window_;}
private:
    HINSTANCE instance_{};HWND window_{};
};
class Assets final {
public:
    Assets() {
        const auto parent=std::filesystem::temp_directory_path();
        root=parent/(L"EndfieldHUD-native-lifecycle-fixture-"+std::to_wstring(GetCurrentProcessId())+
            L"-"+std::to_wstring(GetTickCount64()));
        check(std::filesystem::equivalent(root.parent_path(),parent),"Synthetic fixture remains a direct child of the temporary directory");
        check(std::filesystem::create_directory(root),"Create a new test-owned fixture directory");
        try {
            std::filesystem::create_directory(root/L"Scene");
            write(root/L"textures.json","[]");
            write(root/L"runtime-materials.json","{\"materials\":[]}");
            write(root/L"Scene"/L"materials.json","{\"materials\":[]}");
        } catch(...) {
            std::error_code ignored;std::filesystem::remove_all(root,ignored);throw;
        }
    }
    ~Assets() {std::error_code ignored;std::filesystem::remove_all(root,ignored);}
    std::filesystem::path root;
private:
    static void write(const std::filesystem::path& path,const char* value) {
        std::ofstream stream(path,std::ios::binary);stream<<value;stream.close();
        check(bool(stream),"Write minimal synthetic source catalog");
    }
};
FrozenSnapshot snapshot(unsigned width,unsigned height) {
    std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        const auto index=(std::size_t(y)*width+x)*4;
        pixels[index]=static_cast<std::uint8_t>(30+x%70);
        pixels[index+1]=static_cast<std::uint8_t>(80+y%70);
        pixels[index+2]=160;pixels[index+3]=255;
    }
    FrozenSnapshot result;
    checked(FrozenDesktopSnapshot::from_bgra({0,0,static_cast<std::int32_t>(width),
        static_cast<std::int32_t>(height)},std::move(pixels),result),"Validate generated opaque SDR pixels");
    return result;
}
Frame frame(unsigned width,unsigned height) {
    Frame result;result.camera.viewport={double(width),double(height)};result.backdropAlpha=.73;
    Graphic graphic;graphic.componentId="synthetic.quad";graphic.kind="UIImage";
    graphic.color={1,.5,.25,.4};graphic.vertexColorReady=true;
    graphic.quads.push_back({ehud::scene::Vec3{-.8,-.8,0},{-.8,.8,0},{.8,.8,0},{.8,-.8,0}});
    graphic.uvQuads.push_back({ehud::scene::Vec2{0,0},{0,1},{1,1},{1,0}});
    result.graphics.push_back(std::move(graphic));
    Graphic label;label.componentId="synthetic.label";label.kind="DesktopText";
    label.text="Synthetic lifecycle";label.fontSize=14;label.rect={{0,0},{128,24}};
    label.quads.push_back({ehud::scene::Vec3{-.9,-.2,0},{-.9,.2,0},{.9,.2,0},{.9,-.2,0}});
    result.graphics.push_back(std::move(label));return result;
}
void exercise(NativeRenderer& renderer,HWND window,const Assets& assets,unsigned width,unsigned height) {
    checked(renderer.initialize(window,width,height,D3D_DRIVER_TYPE_WARP),"Create explicit WARP renderer on the same hidden HWND");
    check(renderer.graphicsDevice()!=nullptr&&renderer.textFactory()!=nullptr,"Initialized renderer owns graphics and native text devices");
    check(!IsWindowVisible(window),"Lifecycle fixture never shows its composition window");
    checked(renderer.loadSourceAssets(assets.root),"Load isolated white texture and source shader catalog");
    check(renderer.textureCount()==1&&renderer.textCount()==0,"Each device starts with exactly the synthetic white texture");
    auto supplied=snapshot(width,height);const auto weak=std::weak_ptr(supplied);
    checked(renderer.setFrozenBackdrop(supplied),"Prepare generated background on this device");supplied.reset();
    check(!weak.expired()&&renderer.backdropTextureCount()==3&&renderer.backdropSnapshotBytes()==std::size_t(width)*height*4,
        "Prepared background retains one snapshot and three GPU textures");
    auto* originalDevice=renderer.graphicsDevice();
    for(auto unsupported:{D3D_DRIVER_TYPE_UNKNOWN,D3D_DRIVER_TYPE_REFERENCE,D3D_DRIVER_TYPE_NULL,D3D_DRIVER_TYPE_SOFTWARE}) {
        check(renderer.initialize(window,width,height,unsupported)==E_INVALIDARG,"Unsupported driver is rejected before altering renderer ownership");
        check(renderer.graphicsDevice()==originalDevice&&renderer.backdropTextureCount()==3&&!weak.expired(),
            "Invalid driver cannot discard a valid existing renderer or snapshot");
    }
    auto scene=frame(width,height);
    checked(renderer.draw(scene),"Present synthetic source, background and native text to the hidden swapchain");
    check(renderer.textCount()==1&&renderer.textureCount()==1,"Successful source draw owns one stable component text texture");
    checked(renderer.waitForDiagnosticGpu(),"Drain first hidden WARP presentation");
    renderer.clearFrozenBackdrop();
    check(weak.expired()&&renderer.backdropTextureCount()==0&&renderer.backdropSnapshotBytes()==0,
        "Close releases the only snapshot owner and all backdrop textures");
    checked(renderer.verifyDiagnosticAlpha(),"Read back source accumulation and final encoded half-alpha white");
    check(renderer.textCount()==0,"A later successful source frame prunes the unused native text cache");
    supplied=snapshot(width,height);const auto resizedWeak=std::weak_ptr(supplied);
    checked(renderer.setFrozenBackdrop(supplied),"Prepare a background before resize invalidation");supplied.reset();
    checked(renderer.draw(scene),"Populate swapchain and text views before resizing bound device state");
    const unsigned resizedWidth=width+16,resizedHeight=height+8;
    checked(renderer.resize(resizedWidth,resizedHeight),"Resize the retained flip swapchain after releasing all old buffer owners");
    check(resizedWeak.expired()&&renderer.backdropTextureCount()==0&&renderer.backdropSnapshotBytes()==0,
        "Resize invalidates old physical background pixels rather than stretching them");
    checked(renderer.draw(frame(resizedWidth,resizedHeight)),"Present resized source geometry with the tint fallback");
    checked(renderer.verifyDiagnosticAlpha(),"Resized source and presentation attachments preserve the alpha contract");
    checked(renderer.waitForDiagnosticGpu(),"Drain resized WARP rendering");
    supplied=snapshot(resizedWidth,resizedHeight);const auto resetWeak=std::weak_ptr(supplied);
    checked(renderer.setFrozenBackdrop(supplied),"Populate background owners before complete device teardown");supplied.reset();
    checked(renderer.draw(frame(resizedWidth,resizedHeight)),"Present before destroying this swapchain generation");
    renderer.reset();
    check(resetWeak.expired()&&renderer.backdropTextureCount()==0&&renderer.backdropSnapshotBytes()==0,
        "Renderer reset releases its snapshot and all captured GPU texture owners");
    check(renderer.graphicsDevice()==nullptr&&renderer.textFactory()==nullptr&&renderer.textureCount()==0&&renderer.textCount()==0,
        "Complete teardown invalidates graphics/text devices and all source texture caches");
    check(renderer.draw(scene)==E_UNEXPECTED,"Drawing cannot reuse destroyed attachments after reset");
    check(renderer.verifyDiagnosticAlpha()==E_UNEXPECTED,"Readback cannot reuse destroyed source views after reset");
    renderer.reset();check(renderer.graphicsDevice()==nullptr,"Repeated teardown is harmless");
}
} // namespace
int main() {
    const HRESULT apartment=RoInitialize(RO_INIT_SINGLETHREADED);
    if(FAILED(apartment)){std::cerr<<"FAIL: Initialize isolated STA\n";return 1;}
    int result=0;
    try {
        Window window;Assets assets;
        NativeRenderer invalid;
        check(invalid.initialize(window.get(),64,48,D3D_DRIVER_TYPE_UNKNOWN)==E_INVALIDARG&&invalid.graphicsDevice()==nullptr,
            "Invalid driver is rejected before any device initialization");
        check(invalid.initialize(nullptr,64,48,D3D_DRIVER_TYPE_WARP)==E_INVALIDARG,"Null composition owner is rejected");
        check(invalid.initialize(window.get(),0,48,D3D_DRIVER_TYPE_WARP)==E_INVALIDARG,"Zero target extent is rejected");
        for(unsigned generation=0;generation<12;++generation) {
            std::weak_ptr<const FrozenDesktopSnapshot> destructorSnapshot;
            {
                NativeRenderer renderer;
                exercise(renderer,window.get(),assets,64,48);
                // Reinitialize the same owner as well as destroying/recreating
                // owners. The HWND is deliberately never replaced.
                exercise(renderer,window.get(),assets,80,56);
                checked(renderer.initialize(window.get(),64,48,D3D_DRIVER_TYPE_WARP),"Initialize an active destructor teardown generation");
                checked(renderer.loadSourceAssets(assets.root),"Load source owners for destructor teardown");
                auto supplied=snapshot(64,48);destructorSnapshot=supplied;
                checked(renderer.setFrozenBackdrop(supplied),"Prepare a snapshot released only by renderer destruction");supplied.reset();
                checked(renderer.draw(frame(64,48)),"Leave actual source/text/background GPU submissions active before destruction");
                check(!destructorSnapshot.expired(),"The active renderer retains the supplied snapshot until destruction");
                // No explicit reset or diagnostic drain here: this mirrors
                // the host's renderingFailed unique_ptr destruction path.
            }
            check(destructorSnapshot.expired(),"Destructor releases active snapshot ownership before the next same-HWND renderer");
        }
        std::cout<<"PASS: "<<checks<<" hidden WARP renderer lifecycle contracts; 36 same-HWND device generations. "
            "Synthetic pixels only; hardware recovery, capture and visible pacing remain unverified.\n";
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';result=1;}
    RoUninitialize();return result;
}
