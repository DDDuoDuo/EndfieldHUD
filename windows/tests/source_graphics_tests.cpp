#include "native/renderer.hpp"
#include "native/source_graphics.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <new>
#include <cmath>
#include <iostream>
#include <span>
namespace { std::atomic<bool> countAllocations{}; std::atomic<std::size_t> allocations{}; }
void* operator new(std::size_t size) {
    if (countAllocations.load(std::memory_order_relaxed)) allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* value=std::malloc(size?size:1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
#if defined(__cpp_sized_deallocation)
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }
#endif

using namespace endfield::native;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks{};
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f, const char* message) { bool rejected{}; try { f(); } catch(const std::exception&) { rejected=true; } check(rejected,message); }
template<class T, std::size_t N> std::span<const std::uint8_t> bytes(const std::array<T,N>& values) {
    return {reinterpret_cast<const std::uint8_t*>(values.data()),sizeof(T)*N};
}
ComPtr<ID3DBlob> compile(const char* entry, const char* target) {
    constexpr char text[]=R"(
struct Vertex { float4 position : TEXCOORD0; float4 color : TEXCOORD1; };
struct Interpolated { float4 position : SV_Position; float4 color : TEXCOORD0; };
cbuffer Fixture : register(b0) { float4 tint; };
Interpolated vs(Vertex v) { Interpolated o; o.position=v.position; o.color=v.color; return o; }
float4 ps(Interpolated i) : SV_Target { return i.color*tint; }
)";
    ComPtr<ID3DBlob> code,error;
    const auto result=D3DCompile(text,sizeof(text)-1,nullptr,nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
    if(FAILED(result)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Synthetic source shader compilation failed");
    return code;
}
void run(const std::filesystem::path& shader) {
    struct Window {
        HWND value{CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"Source graphics fixture",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
        ~Window(){ if(value) DestroyWindow(value); }
    } window;
    check(window.value!=nullptr && !IsWindowVisible(window.value),"Owned fixture stays hidden");
    Renderer renderer;
    renderer.initialize(window.value,32,32,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});
    auto& source=renderer.sourceGraphics();
    auto vs=compile("vs","vs_5_0"),ps=compile("ps","ps_5_0");
    SourcePipeline pipeline;
    pipeline.vertexBytecode={static_cast<const std::uint8_t*>(vs->GetBufferPointer()),vs->GetBufferSize()};
    pipeline.fragmentBytecode={static_cast<const std::uint8_t*>(ps->GetBufferPointer()),ps->GetBufferSize()};
    pipeline.attributes={{0,4,0},{1,4,16}};
    source.setPipeline("fixture",pipeline);
    const std::array<float,32> quad{-1,1,.5f,1,1,0,0,1, 1,1,.5f,1,1,0,0,1,
                                    1,-1,.5f,1,1,0,0,1, -1,-1,.5f,1,1,0,0,1};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const std::array<float,4> tint{1,1,1,1};
    const std::string meshID="original-watch-geometry-with-a-realistic-long-identity-retained-across-animation-frames";
    const std::string uniformID="original-watch-material-constant-with-a-realistic-long-identity-retained-across-animation-frames";
    source.setMesh(meshID,1,32,bytes(quad),indices);
    source.setUniform(uniformID,bytes(tint));
    SourceDraw draw{meshID,"fixture",0,6,0,{{SourceStage::fragment,0,uniformID}}, {}};
    source.setDraws(std::span(&draw,1));
    renderer.draw(false);
    auto image=renderer.readback();
    check(image.pixels[(16*32+16)*4+2]==255 && image.pixels[(16*32+16)*4+3]==255,"Original source pipeline renders retained vertex colors");
    const auto beforeGate=source.stats();const auto nativeBeforeGate=renderer.stats();
    check(renderer.sourcePassEnabled()&&renderer.stats().sourcePassEnabled,"Existing HUD source pass starts enabled");
    allocations=0;countAllocations=true;
    bool gateChanged{},gateEqual{};
    try{gateChanged=renderer.setSourcePassEnabled(false);gateEqual=renderer.setSourcePassEnabled(false);}
    catch(...){countAllocations=false;throw;}countAllocations=false;
    check(gateChanged&&!gateEqual&&allocations==0&&!renderer.stats().sourcePassEnabled,"Source gate changes once and equality allocates nothing");
    renderer.draw(false);image=renderer.readback();
    check(image.pixels[(16*32+16)*4+3]==0&&source.stats().frames==beforeGate.frames,"Disabled source-only frame clears old pixels without executing source passes");
    check(source.active()&&source.stats().draws==beforeGate.draws&&source.stats().payloadBytes==beforeGate.payloadBytes,"Suspension retains original ordered source draws and resource bytes");
    check(renderer.setSourcePassEnabled(true)&&!renderer.setSourcePassEnabled(true),"Restoration is an equal-safe presentation event");
    renderer.draw(false);image=renderer.readback();
    check(image.pixels[(16*32+16)*4+2]==255&&image.pixels[(16*32+16)*4+3]==255&&source.stats().frames==beforeGate.frames+1,"Restoring the gate draws the same retained source again");
    check(source.stats().geometryUploads==beforeGate.geometryUploads&&source.stats().textureUploads==beforeGate.textureUploads&&source.stats().uniformUploads==beforeGate.uniformUploads&&source.stats().uniformAllocations==beforeGate.uniformAllocations&&source.stats().pipelines==beforeGate.pipelines&&renderer.stats().meshUploads==nativeBeforeGate.meshUploads&&renderer.stats().textureUploads==nativeBeforeGate.textureUploads,"Toggling the source pass uploads no geometry, textures, constants or pipelines");
    const auto before=source.stats();
    rejects([&] {source.setMesh(meshID,2,32,bytes(quad),std::span(indices).first(3));},"A shorter replacement cannot invalidate an active draw");
    rejects([&] {source.setMesh(meshID,2,16,bytes(quad),indices);},"A smaller stride cannot invalidate the active vertex layout");
    check(source.stats().geometryUploads==before.geometryUploads && source.stats().payloadBytes==before.payloadBytes,"Rejected replacements preserve resources and accounting");
    source.setMesh(meshID,1,32,bytes(quad),indices);
    source.setUniform(uniformID,bytes(tint));
    for(unsigned i=0;i<20;++i) renderer.draw(false);
    check(source.stats().geometryUploads==before.geometryUploads && source.stats().uniformUploads==before.uniformUploads && source.stats().textureUploads==before.textureUploads,"Unchanged frames do not recreate or upload GPU data");
    const std::array<float,4> dim{.25f,1,1,1};
    source.setUniform(uniformID,bytes(dim));
    renderer.draw(false); image=renderer.readback();
    check(std::abs(int(image.pixels[(16*32+16)*4+2])-137)<=1,"Original sRGB attachment encodes linear shader results exactly once");
    check(source.stats().uniformAllocations==before.uniformAllocations && source.stats().uniformUploads==before.uniformUploads+1,"Changed shader constants reuse their native allocation");
    auto moved=quad;moved[4]=.5f;moved[12]=.5f;moved[20]=.5f;moved[28]=.5f;const auto geometryBefore=source.stats();
    auto animatedTint=dim;
    allocations=0; countAllocations=true;
    try {
        for(unsigned revision=2;revision<22;++revision) {
            animatedTint[0]=revision%2==0?.5f:.25f;
            source.setMesh(meshID,revision,32,bytes(moved),indices);
            source.setUniform(uniformID,bytes(animatedTint));
            renderer.draw(false);
        }
    } catch(...) { countAllocations=false; throw; }
    countAllocations=false;
    check(allocations==0,"Animated original geometry and constants retain long resource identities without C++ heap allocation");
    check(source.stats().uniformAllocations==geometryBefore.uniformAllocations &&
          source.stats().uniformUploads==geometryBefore.uniformUploads+20,
          "Changing constants during geometry animation reuse GPU capacity");
    check(source.stats().meshBufferAllocations==geometryBefore.meshBufferAllocations&&source.stats().indexUploads==geometryBefore.indexUploads&&
          source.stats().vertexUploads==geometryBefore.vertexUploads+20,"Repeated source vertex updates retain GPU capacity and do not reupload unchanged indices");
    renderer.draw(false);image=renderer.readback();check(std::abs(int(image.pixels[(16*32+16)*4+2])-99)<=1,"Retained dynamic vertices preserve original color math and sRGB encoding");
    source.setMesh(meshID,22,32,bytes(quad),indices);
    source.setDraws({});const auto beforeShrink=source.stats();source.setMesh(meshID,23,32,bytes(quad),std::span(indices).first(3));
    check(source.stats().meshBufferAllocations==beforeShrink.meshBufferAllocations&&source.stats().indexUploads==beforeShrink.indexUploads+1,"Smaller triangle count reuses retained index capacity after explicit draw transaction");
    auto triangleDraw=draw;triangleDraw.indexCount=3;source.setDraws(std::span(&triangleDraw,1));
    source.setDraws({});source.setMesh(meshID,24,32,bytes(quad),indices);source.setDraws(std::span(&draw,1));
    const std::array<Vertex,4> nativeQuad{{
        {{-1,1,.5f},{0,0},{1,1,1,1}},{{1,1,.5f},{1,0},{1,1,1,1}},
        {{1,-1,.5f},{1,1},{1,1,1,1}},{{-1,-1,.5f},{0,1},{1,1,1,1}}}};
    renderer.setMesh("native-caption",1,{nativeQuad,indices});
    DrawObject overlay;overlay.sourceID="caption";overlay.meshID="native-caption";
    overlay.linearTint={0,0,1,1};overlay.opacity=.5f;
    overlay.masks={{endfield::core::Matrix4{}, {-1,-1,1,2}}};
    renderer.setDrawList(std::span(&overlay,1));renderer.draw(false);image=renderer.readback();
    auto pixel=[&](unsigned x,unsigned channel){return int(image.pixels[(16*32+x)*4+channel]);};
    check(std::abs(pixel(8,0)-128)<=1 && std::abs(pixel(8,2)-68)<=1 && pixel(8,3)==255,
          "Native caption surface composites over original shader output in encoded space");
    check(pixel(24,0)==0 && std::abs(pixel(24,2)-137)<=1 && pixel(24,3)==255,
          "Transparent native surface preserves original source pixels");
    const auto mixedGate=source.stats();renderer.setSourcePassEnabled(false);renderer.draw(false);image=renderer.readback();
    check(std::abs(pixel(8,0)-128)<=1&&pixel(8,2)==0&&std::abs(pixel(8,3)-128)<=1&&pixel(24,3)==0,"Disabled source leaves native content and its original alpha intact");
    check(source.stats().frames==mixedGate.frames,"Native-only draws do not increment original source frame statistics");
    renderer.setSourcePassEnabled(true);renderer.draw(false);image=renderer.readback();
    check(pixel(24,3)==255&&std::abs(pixel(24,2)-137)<=1,"Source resumes underneath existing native content without republishing either list");
    renderer.clearResources(); renderer.draw(false); image=renderer.readback();
    check(source.stats().payloadBytes==0 && !source.active(),"Scene teardown releases all original resources");
    check(image.pixels[(16*32+16)*4+3]==0,"Source teardown does not leave old content visible");
    renderer.setSourcePassEnabled(false);renderer.reset();check(renderer.sourcePassEnabled()&&renderer.stats().sourcePassEnabled,"Reset restores the default source gate");
    renderer.initialize(window.value,32,32,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});check(renderer.sourcePassEnabled(),"Reinitialization preserves default HUD source behavior");
}
}
int wmain(int argc,wchar_t**argv) {
    try {check(argc==2,"Pass native/hud.hlsl");run(argv[1]);std::cout<<"Passed "<<checks<<" original graphics contracts\n";return 0;}
    catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
