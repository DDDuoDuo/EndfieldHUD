#include "native/renderer.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
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
namespace n=endfield::native;namespace c=endfield::core;using J=ehud::data::Json;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
J fixture(const std::filesystem::path&path){std::ifstream input(path,std::ios::binary);check(bool(input),"Read explicit immutable source transition fixture");auto j=J::parse(std::string((std::istreambuf_iterator<char>(input)),{}),128*1024);check(j["sourceCommit"]==J("ca04f142185c7de40acd8523bdb563195d90a1d1")&&j["rows"].array().size()==28,"Original crossfade body and28measured detached cases");check(!j["usesAppOrWindow"].boolean(),"Oracle uses no application or visible window");return j;}
std::array<double,4>premultipliedBGRA(const J&rgba){const auto&a=rgba.array();const double alpha=a[3].number();return {a[2].number()*alpha,a[1].number()*alpha,a[0].number()*alpha,alpha};}
void compareMath(const J&j){for(const auto&row:j["rows"].array()){const auto old=premultipliedBGRA(row["oldRGBA"]),fresh=premultipliedBGRA(row["newRGBA"]),third=premultipliedBGRA(row["thirdRGBA"]);const double t=c::CubicTiming{.42,0,.58,1}.value(row["phase"].number());for(unsigned k=0;k<4;++k){check(std::abs(255*((1-t)*old[k]+t*fresh[k])-row["bgra"].array()[k].number())<=.50001,"Actual Core Animation uses encoded premultiplied weighted addition");check(std::abs(255*fresh[k]-row["interruptedStartBGRA"].array()[k].number())<=.50001,"Interrupted source transition starts from previous model content");check(std::abs(255*(fresh[k]+third[k])*.5-row["interruptedHalfBGRA"].array()[k].number())<=.50001,"Interrupted source transition remains bounded to two model surfaces");}}}
#ifdef _WIN32
template<class F>void rejects(F f,const char*why){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
struct Apartment {HRESULT result{CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)};Apartment(){check(SUCCEEDED(result),"Owned COM fixture initializes");}~Apartment(){if(SUCCEEDED(result))CoUninitialize();}};
struct Window {HWND handle{};Window(){WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"OwnedNowPlayingBlendFixture";check(RegisterClassW(&type)!=0,"Owned hidden fixture class");handle=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,type.lpszClassName,L"Synthetic source crossfade",WS_POPUP,0,0,8,8,nullptr,nullptr,type.hInstance,nullptr);check(handle&&!IsWindowVisible(handle),"Owned8px fixture remains hidden");}~Window(){if(handle)DestroyWindow(handle);UnregisterClassW(L"OwnedNowPlayingBlendFixture",GetModuleHandleW(nullptr));}};
constexpr std::array<std::uint32_t,6>indices{0,1,2,0,2,3};
constexpr std::array<n::Vertex,4>quad{{{{0,0,0},{0,0}},{{8,0,0},{1,0}},{{8,8,0},{1,1}},{{0,8,0},{0,1}}}};
std::array<std::uint8_t,4>pixelBytes(const J&rgba){std::array<std::uint8_t,4>result{};for(unsigned k=0;k<4;++k)result[k]=static_cast<std::uint8_t>(std::lround(rgba.array()[k].number()*255));return result;}
void sourcePixels(n::Renderer&r,const J&expected,unsigned&maximum){r.draw(false);const auto image=r.readback();const auto offset=4*image.rowBytes+16;for(unsigned k=0;k<4;++k){const auto delta=unsigned(std::abs(int(image.pixels[offset+k])-int(expected.array()[k].integer())));maximum=std::max(maximum,delta);check(delta<=2,"Native weighted group matches measured source BGRA within two byte levels");}}
void native(n::Renderer&r,const J&j){c::Matrix4 camera;camera.values={.25,0,0,0,0,-.25,0,0,0,0,0,0,-1,1,0,1};r.setCamera(camera);r.setMesh("quad",1,{quad,indices});
    std::array<n::DrawObject,2>draws;for(unsigned k=0;k<2;++k){draws[k].sourceID="weight"+std::to_string(k);draws[k].meshID="quad";draws[k].textureID=k?"new":"old";draws[k].blend=n::NativeBlend::weightedAdd;draws[k].opacity=.5f;}
    const n::NativeGroupTarget target{{0,0,8,8},1,n::NativeGroupColorSpace::encodedSRGB};std::uint64_t revision{};unsigned maximum{};
    for(auto space:{n::TextureColorSpace::encodedSRGB,n::TextureColorSpace::sRGB})for(const auto&row:j["rows"].array()){
        auto a=pixelBytes(row["oldRGBA"]),b=pixelBytes(row["newRGBA"]);r.setTexture("old",++revision,{1,1,a,space});r.setTexture("new",revision,{1,1,b,space});const double t=c::CubicTiming{.42,0,.58,1}.value(row["phase"].number());draws[0].opacity=float(1-t);draws[1].opacity=float(t);r.configureNativeGroup("fade",target,draws);auto output=r.nativeGroupOutput("fade");r.setDrawList(std::span(&output,1));sourcePixels(r,row["bgra"],maximum);
        a=b;b=pixelBytes(row["thirdRGBA"]);r.setTexture("old",++revision,{1,1,a,space});r.setTexture("new",revision,{1,1,b,space});draws[0].opacity=1;draws[1].opacity=0;r.setNativeGroupDraws("fade",draws);sourcePixels(r,row["interruptedStartBGRA"],maximum);draws[0].opacity=draws[1].opacity=.5f;r.setNativeGroupDraws("fade",draws);sourcePixels(r,row["interruptedHalfBGRA"],maximum);
    }
    const auto saved=r.readback().pixels;const auto before=r.stats();
    rejects([&]{r.setDrawList(draws);},"Weighted blending cannot enter root source-over publication");
    rejects([&]{r.configureNativeGroup("linear",{{0,0,8,8},1},draws);},"Weighted blending cannot enter a linear group");
    auto bad=draws;bad[1].blend=n::NativeBlend::sourceOver;rejects([&]{r.configureNativeGroup("fade",{{-1,0,9,8},1,n::NativeGroupColorSpace::encodedSRGB},bad);},"Mixed weighted/ordinary group rejects atomically before resize");
    bad=draws;bad[0].opacity=.6f;rejects([&]{r.setNativeGroupDraws("fade",bad);},"Total weights over one reject before any constants update");
    std::array<n::DrawObject,3>three{draws[0],draws[1],draws[0]};for(auto&d:three)d.opacity=.3f;rejects([&]{r.setNativeGroupDraws("fade",three);},"Crossfade is bounded to two regular leaves");
    bad=draws;bad[1].opacity=NAN;rejects([&]{r.setNativeGroupDraws("fade",bad);},"Nonfinite weighted input preserves prior group");
    auto output=r.nativeGroupOutput("fade");auto nested=output;nested.blend=n::NativeBlend::weightedAdd;rejects([&]{r.configureNativeGroup("nested",target,std::span(&nested,1));},"Weighted input cannot borrow a nested group output");
    r.draw(false);check(r.readback().pixels==saved&&r.stats().nativeGroupTargetAllocations==before.nativeGroupTargetAllocations&&r.stats().resourceBytes==before.resourceBytes,"All malformed weighted changes preserve old pixels and resources");
    allocations=0;counting=true;try{for(unsigned frame=0;frame<120;++frame){const float t=float(frame)/119;draws[0].opacity=1-t;draws[1].opacity=t;r.setNativeGroupDraws("fade",draws);r.draw(false);}}catch(...){counting=false;throw;}counting=false;const auto warm=r.stats();check(allocations==0&&warm.nativeGroupTargetAllocations==before.nativeGroupTargetAllocations&&warm.textureUploads==before.textureUploads&&warm.meshUploads==before.meshUploads&&warm.objectBufferAllocations==before.objectBufferAllocations,"120 weighted frames allocate/upload no image, target, geometry or object storage");
    for(unsigned frame=0;frame<10;++frame){check(!r.setNativeGroupDraws("fade",draws),"Equal weighted pose is a no-op");r.draw(false);}check(r.stats().nativeGroupRenders==warm.nativeGroupRenders,"Settled weighted group performs no repeated group paint");
    output.opacity=0;r.setDrawList(std::span(&output,1));draws[0].opacity=draws[1].opacity=.5f;r.setNativeGroupDraws("fade",draws);r.draw(false);check(r.stats().nativeGroupRenders==warm.nativeGroupRenders,"Hidden output suppresses local weighted render work");output.opacity=1;r.setDrawList(std::span(&output,1));r.draw(false);check(r.stats().nativeGroupRenders==warm.nativeGroupRenders+1,"Reveal flushes latest retained weights once");
    check(!r.removeNativeGroup("fade")&&!r.removeTexture("old")&&!r.removeMesh("quad"),"Published group and both borrowed inputs stay resident");
    const std::array<std::uint8_t,4>red{255,0,0,102},blue{0,0,255,102};r.setTexture("old",++revision,{1,1,red,n::TextureColorSpace::encodedSRGB});r.setTexture("new",revision,{1,1,blue,n::TextureColorSpace::encodedSRGB});for(auto&d:draws){d.opacity=1;d.blend=n::NativeBlend::sourceOver;}r.setNativeGroupDraws("fade",draws);sourcePixels(r,J::Array{102,0,61,163},maximum);draws[1].blend=n::NativeBlend::screen;r.setNativeGroupDraws("fade",draws);sourcePixels(r,J::Array{102,0,102,163},maximum);
    r.clearDrawList();check(r.removeNativeGroup("fade")&&r.removeTexture("old")&&r.removeTexture("new")&&r.removeMesh("quad"),"Detach then retire group and input resources");check(r.stats().resourceBytes==0&&r.stats().nativeGroups==0,"Weighted group leaves no resident resources");std::cout<<"Source crossfade native maximum residual "<<maximum<<"/255\n";
}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){try{check(argc==3,"Pass HLSL and immutable source transition fixture");const auto j=fixture(argv[2]);compareMath(j);Apartment apartment;{Window window;n::Renderer renderer;renderer.initialize(window.handle,8,8,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests});native(renderer,j);renderer.reset();check(!IsWindowVisible(window.handle),"No native test window was shown");}std::cout<<"Now Playing source blend: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"Now Playing source blend failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(int argc,char**argv){try{check(argc==2,"Pass immutable source transition fixture");const auto j=fixture(argv[1]);compareMath(j);std::cout<<"Now Playing source blend math: "<<checks<<" checks passed; native GPU execution pending\n";return 0;}catch(const std::exception&e){std::cerr<<"Now Playing source blend failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
