// Development snapshot tool: renders the header sanity wallet/popover flat (no
// HUD tilt) with the shared renderer on a hidden WARP target and writes a PNG
// for visual review against the Mac snapshot (HUD_ACCOUNT_PREVIEW_DIR). The
// Account page itself is on the design library and compared node by node
// with the Mac oracle (tests/account_canvas_tests.cpp). Synthetic values
// only; no account, network, vault or visible window is used.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "native/hypergryph_account_sanity_gauge_scene.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
namespace gpu=endfield::native;namespace h=endfield::modules::hypergryph;namespace core=endfield::core;
using Microsoft::WRL::ComPtr;
void need(bool v,const char* s) {if(!v) throw std::runtime_error(s);}
void writePNG(const gpu::Readback& r,const std::filesystem::path& file,unsigned x0,unsigned y0,unsigned w,unsigned hgt) {
    ComPtr<IWICImagingFactory> wic;need(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.GetAddressOf()))),"WIC");
    std::vector<std::uint8_t> pixels(std::size_t(w)*hgt*4);
    for(unsigned y=0;y<hgt;++y) for(unsigned x=0;x<w;++x) {
        const auto* s=&r.pixels[std::size_t(y0+y)*r.rowBytes+std::size_t(x0+x)*4];auto* d=&pixels[(std::size_t(y)*w+x)*4];
        // Composite the premultiplied output over the Mac snapshot background (white 0.08).
        const unsigned a=s[3],bg=20;for(int c=0;c<3;++c) d[c]=static_cast<std::uint8_t>(std::min(255u,s[c]+bg*(255-a)/255));d[3]=255;
    }
    ComPtr<IWICStream> stream;wic->CreateStream(stream.GetAddressOf());need(SUCCEEDED(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE)),"Snapshot file");
    ComPtr<IWICBitmapEncoder> encoder;wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf());encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;encoder->CreateNewFrame(frame.GetAddressOf(),nullptr);frame->Initialize(nullptr);frame->SetSize(w,hgt);
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;frame->SetPixelFormat(&format);
    need(SUCCEEDED(frame->WritePixels(hgt,w*4,static_cast<UINT>(pixels.size()),pixels.data())),"Snapshot pixels");frame->Commit();encoder->Commit();
}
struct Window {ATOM atom{};HWND hwnd{};
    Window() {WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldAccountSnapshot";atom=RegisterClassW(&c);
        hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"",WS_POPUP,0,0,900,760,nullptr,nullptr,c.hInstance,nullptr);need(hwnd&&!IsWindowVisible(hwnd),"Hidden snapshot target");}
    ~Window() {if(hwnd) DestroyWindow(hwnd);if(atom) UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}};
}
int wmain(int argc,wchar_t** argv) {
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        need(argc==4,"account_snapshot <hud.hlsl> <resources/account> <new output directory>");
        const std::filesystem::path out=argv[3];need(!std::filesystem::exists(out),"Refusing to reuse an output directory");std::filesystem::create_directories(out);
        Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,900,760,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});
        renderer.setCamera(gpu::layerViewportProjection(900,760));gpu::LayerRasterizer raster;gpu::LayerRasterOptions options;options.pixelsPerPoint=2;
        constexpr double scale=2;const auto place=core::Matrix4::translation(20,20)*core::Matrix4::scale(scale,scale);
        gpu::LayerComposition composition;double t=0;
        // Header wallet + recovery popover (flat).
        auto assets=std::make_shared<const h::GaugeAssets>(h::GaugeAssets::load(std::filesystem::absolute(argv[2]).lexically_normal()));
        gpu::NativeSanityGaugeScene gauge(assets,raster,options);h::SanityGaugeModel g;const double now=781000000;
        h::SanityPresentation s{h::Game::endfield,42,360,now,now+49,now+38*3600+193,false,true};
        g.update("42 / 360","Sanity",true,scale,s,now,core::Language::english);g.perform("toggle");gauge.sync(g,core::Language::english,t,true);
        const auto gaugePlace=core::Matrix4::translation(200,40)*core::Matrix4::scale(scale,scale);
        gauge.updatePose(gaugePlace,1,t);gauge.upload(renderer);composition.setEntries(renderer,gauge.entries());composition.present(renderer);renderer.draw(false);
        writePNG(renderer.readback(),out/"account-sanity-recovery.png",80,20,480,260);std::wcout<<L"wrote account-sanity-recovery\n";
        composition.setEntries(renderer,{});gauge.collected(renderer);gauge.release(renderer);composition.detach(renderer);renderer.reset();
        if(SUCCEEDED(hr)) CoUninitialize();return 0;
    } catch(const std::exception& e) {if(SUCCEEDED(hr)) CoUninitialize();std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
#endif
