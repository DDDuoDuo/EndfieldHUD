#include "native/renderer.hpp"
#include "native/source_graphics.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
namespace{std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace gpu=endfield::native;namespace core=endfield::core;
namespace{
std::size_t checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char*why){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
struct Window{ATOM atom{};HWND handle{};
    Window(){WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"EndfieldNativeGroupOwnedTest";
        atom=RegisterClassW(&type);check(atom!=0,"Owned group test class registered");handle=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,type.lpszClassName,L"Owned GPU group test",WS_POPUP,0,0,64,64,nullptr,nullptr,type.hInstance,nullptr);check(handle!=nullptr,"Owned hidden group window created");}
    ~Window(){if(handle)DestroyWindow(handle);if(atom)UnregisterClassW(MAKEINTATOM(atom),GetModuleHandleW(nullptr));}
};
core::Matrix4 camera(){core::Matrix4 m;m.values={2./64,0,0,0,0,-2./64,0,0,0,0,0,0,-1,1,0,1};return m;}
constexpr std::array<std::uint32_t,6>indices{0,1,2,0,2,3};
constexpr std::array<gpu::Vertex,4>quad{{{{0,0,0},{0,0},{1,1,1,1}},{{16,0,0},{1,0},{1,1,1,1}},{{16,16,0},{1,1},{1,1,1,1}},{{0,16,0},{0,1},{1,1,1,1}}}};
void pixel(const gpu::Readback&image,unsigned x,unsigned y,std::array<int,4>expected,int tolerance,const char*why){
    const auto at=std::size_t(y)*image.rowBytes+x*4;for(std::size_t c=0;c<4;++c)check(std::abs(int(image.pixels.at(at+c))-expected[c])<=tolerance,why);
}
Microsoft::WRL::ComPtr<ID3DBlob>shader(const char*entry,const char*profile){
    constexpr char code[]="float4 VS(float4 p:TEXCOORD0):SV_Position{return p;} float4 PS():SV_Target{return float4(.2,.4,.6,.7);}";
    Microsoft::WRL::ComPtr<ID3DBlob>result,error;check(SUCCEEDED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&result,&error)),"Synthetic source sentinel shader compiles");return result;
}
void sourcePreservation(gpu::Renderer&renderer){
    renderer.clearResources();auto&source=renderer.sourceGraphics();auto vs=shader("VS","vs_5_0"),ps=shader("PS","ps_5_0");
    gpu::SourcePipeline pipeline;pipeline.vertexBytecode={static_cast<const std::uint8_t*>(vs->GetBufferPointer()),vs->GetBufferSize()};pipeline.fragmentBytecode={static_cast<const std::uint8_t*>(ps->GetBufferPointer()),ps->GetBufferSize()};pipeline.attributes={{0,4,0}};pipeline.blendEnabled=false;
    source.setPipeline("owned-original-sentinel",pipeline);
    constexpr std::array<std::array<float,4>,4>vertices{{{-1,1,.5f,1},{1,1,.5f,1},{1,-1,.5f,1},{-1,-1,.5f,1}}};
    source.setMesh("owned-original-mesh",1,16,{reinterpret_cast<const std::uint8_t*>(vertices.data()),sizeof(vertices)},indices);
    gpu::SourceDraw sourceDraw;sourceDraw.mesh="owned-original-mesh";sourceDraw.pipeline="owned-original-sentinel";sourceDraw.indexCount=6;source.setDraws(std::span(&sourceDraw,1));
    renderer.draw(false);const auto original=renderer.readback().pixels;const auto originalStats=source.stats();
    renderer.configureNativeGroup("transparent",{{0,0,64,64},1},{});auto output=renderer.nativeGroupOutput("transparent");renderer.setCamera(camera());renderer.setDrawList(std::span(&output,1));renderer.draw(false);
    check(renderer.readback().pixels==original,"Transparent native group preserves every source attachment byte");
    check(source.stats().draws==originalStats.draws&&source.stats().pipelines==originalStats.pipelines&&source.stats().uniformUploads==originalStats.uniformUploads&&source.stats().geometryUploads==originalStats.geometryUploads,"Native group never mutates source pipeline/resources/constants");
    renderer.clearDrawList();renderer.draw(false);check(renderer.readback().pixels==original,"No native overlay preserves original source output after group pass");
    check(renderer.removeNativeGroup("transparent"),"Unpublished transparent group releases independently of source");source.clear();
}
void run(gpu::Renderer&renderer){
    renderer.setCamera(camera());renderer.setMesh("local-quad",1,{quad,indices});
    const std::array<std::uint8_t,4>white{255,255,255,255};renderer.setTexture("local-white",1,{1,1,white});
    std::array<gpu::DrawObject,3>children;
    for(std::size_t n=0;n<children.size();++n){children[n].sourceID="local/"+std::to_string(n);children[n].meshID="local-quad";children[n].textureID="local-white";}
    children[0].world=core::Matrix4::translation(-3,4)*core::Matrix4::scale(1.5,1);children[0].linearTint={0,0,0,.3f};
    children[1].linearTint={1,0,0,1};children[2].world=core::Matrix4::translation(8,0);children[2].linearTint={0,0,1,1};
    const gpu::NativeGroupTarget target{{-3,0,27,20},1};check(renderer.configureNativeGroup("menu",target,children),"Atomic initial group installs local target and children");
    const auto*stable=&renderer.nativeGroupOutput("menu");auto output=*stable;output.world=core::Matrix4::translation(20,16);output.opacity=.5f;renderer.setDrawList(std::span(&output,1));renderer.draw(false);
    const auto image=renderer.readback();pixel(image,24,24,{0,0,128,128},1,"Group fades opaque red once");pixel(image,36,24,{128,0,0,128},1,"Overlapping opaque blue wins before group fade");pixel(image,18,34,{0,0,0,38},1,"Negative local shadow extent survives target composition");
    check(renderer.stats().nativeGroupRenders==1,"First visible group renders once");check(!renderer.configureNativeGroup("menu",target,children),"Identical group configuration/children is a no-op");check(!renderer.setNativeGroupDraws("menu",children),"Unchanged child pose does not invalidate target");
    check(!renderer.removeNativeGroup("menu")&&!renderer.removeMesh(output.meshID)&&!renderer.removeTexture(output.textureID),"Published output and generated assets cannot be retired independently");
    check(!renderer.removeMesh("local-quad")&&!renderer.removeTexture("local-white"),"Local group dependencies stay resident");
    rejects([&]{renderer.setTexture(output.textureID,2,{1,1,white});},"Caller cannot overwrite generated group texture");rejects([&]{renderer.setMesh(output.meshID,2,{quad,indices});},"Caller cannot overwrite generated group quad");
    const auto saved=renderer.readback().pixels;const auto stats=renderer.stats();auto invalid=children;invalid[2].meshID="missing";
    rejects([&]{renderer.configureNativeGroup("menu",{{-5,-4,40,40},2},invalid);},"Invalid child rejects combined resize before target mutation");
    auto nonfinite=children;nonfinite[2].opacity=std::numeric_limits<float>::quiet_NaN();rejects([&]{renderer.setNativeGroupDraws("menu",nonfinite);},"Complete numeric child validation precedes constants mutation");
    rejects([&]{renderer.setNativeGroupDraws("menu",std::span(&output,1));},"Nested/self group output is explicitly unsupported");
    renderer.configureNativeGroup("other",{{0,0,2,2},1},{});auto other=renderer.nativeGroupOutput("other");rejects([&]{renderer.setNativeGroupDraws("menu",std::span(&other,1));},"Cross-group target dependency is explicitly unsupported");renderer.removeNativeGroup("other");
    renderer.draw(false);check(renderer.readback().pixels==saved&&renderer.stats().nativeGroupTargetAllocations==stats.nativeGroupTargetAllocations+1,"Rejected resize/children preserve prior target and pixels");
    rejects([&]{renderer.configureNativeGroup("invalid",{{0,0,4096,4096},1},{});},"Oversized target rejects before GPU allocation");rejects([&]{renderer.configureNativeGroup("invalid",{{0,0,1,1},0},{});},"Zero target density rejects");
    rejects([&]{renderer.configureNativeGroup("invalid",{{std::numeric_limits<double>::infinity(),0,1,1},1},{});},"Nonfinite target coordinate rejects");
    renderer.configureNativeGroup("budget",{{0,0,4096,1024},1},{});const auto budgetStats=renderer.stats();rejects([&]{renderer.configureNativeGroup("budget2",{{0,0,4096,1024},1},{});},"Aggregate group byte bound rejects before allocating second target");
    check(renderer.stats().nativeGroupTargetAllocations==budgetStats.nativeGroupTargetAllocations,"Rejected aggregate budget does not allocate target");renderer.removeNativeGroup("budget");
    const auto warm=renderer.stats();allocations=0;counting=true;
    try{for(unsigned frame=0;frame<120;++frame){output.opacity=.1f+frame*.005f;output.world.values[3]=frame*.00001;renderer.setDrawList(std::span(&output,1));renderer.draw(false);}}catch(...){counting=false;throw;}counting=false;
    const auto animated=renderer.stats();check(allocations==0&&animated.nativeGroupRenders==warm.nativeGroupRenders&&animated.nativeGroupTargetAllocations==warm.nativeGroupTargetAllocations&&animated.textureUploads==warm.textureUploads&&animated.objectBufferAllocations==warm.objectBufferAllocations,"120 root tilt/fade frames allocate/repaint/upload no group resources");
    children[2].opacity=.75f;check(renderer.setNativeGroupDraws("menu",children),"Local feedback change invalidates cached target");renderer.draw(false);check(renderer.stats().nativeGroupRenders==animated.nativeGroupRenders+1,"Local feedback renders target exactly once");renderer.draw(false);check(renderer.stats().nativeGroupRenders==animated.nativeGroupRenders+1,"Settled feedback adds no repeated local work");
    const std::array<std::uint8_t,4>black{0,0,0,255};const auto beforeReplacement=renderer.stats().nativeGroupRenders;renderer.setTexture("local-white",2,{1,1,black});renderer.draw(false);check(renderer.stats().nativeGroupRenders==beforeReplacement+1,"Referenced texture replacement invalidates local group even without child-constant changes");
    auto moved=quad;for(auto&vertex:moved)vertex.position[0]+=1;renderer.setMesh("local-quad",2,{moved,indices});renderer.draw(false);check(renderer.stats().nativeGroupRenders==beforeReplacement+2,"Referenced geometry replacement invalidates local group");
    check(renderer.configureNativeGroup("menu",{{-4.25,-1.1,30.5,24.2},1.5},children),"Negative fractional coverage/DPI reconfigures atomically");check(&renderer.nativeGroupOutput("menu")==stable&&renderer.nativeGroupOutput("menu").textureID==output.textureID,"Reconfiguration keeps output handle/identity stable");renderer.draw(false);
    renderer.setNativeGroupDraws("menu",{});check(renderer.removeMesh("local-quad")&&renderer.removeTexture("local-white"),"Removing group children releases their local references without clearing sibling publication");
    renderer.draw(false);const auto empty=renderer.readback();check(std::all_of(empty.pixels.begin(),empty.pixels.end(),[](auto value){return value==0;}),"Empty visible group target clears previous cached pixels");
    renderer.clearDrawList();check(renderer.removeNativeGroup("menu"),"Group releases after publication removes output reference");check(renderer.stats().nativeGroups==0&&renderer.stats().nativeGroupBytes==0&&renderer.stats().resourceBytes==0,"Group target/camera/quad teardown has no retained resource leak");
    sourcePreservation(renderer);renderer.clearResources();check(renderer.stats().resourceBytes==0&&renderer.stats().nativeGroups==0,"Full resource reset removes groups and source/native content");
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==2,"Pass original native HUD shader path");check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Owned COM fixture initializes");
    {Window window;gpu::Renderer renderer;renderer.initialize(window.handle,64,64,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});run(renderer);check(!IsWindowVisible(window.handle),"Group fixture never opens a visible UI");renderer.reset();}
    CoUninitialize();std::cout<<"Native GPU group contracts: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception&e){counting=false;std::cerr<<"Native GPU group contract failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#endif
