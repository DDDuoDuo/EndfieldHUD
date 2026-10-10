#include "native/media_assembly_gpu.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;namespace m=endfield::modules;
[[noreturn]]void fail(const char*what,HRESULT hr){char code[16]{};std::snprintf(code,sizeof code,"%08lX",static_cast<unsigned long>(hr));throw std::runtime_error(std::string("Media Assembly GPU: ")+what+" (0x"+code+")");}
void check(HRESULT hr,const char*what){if(FAILED(hr))fail(what,hr);}
void need(bool value,const char*what){if(!value)throw std::invalid_argument(std::string("Media Assembly GPU: ")+what);}

// One fullscreen triangle; the processing pass reads source texels by exact
// integer address (no sampler), so results do not depend on filtering.
constexpr char shaderSource[]=R"hlsl(
Texture2D<float4> Source : register(t0);
Texture3D<float4> Cube : register(t1);
cbuffer Edit : register(b0) {
    int4 Map0;      // origin.xy, column.xy
    int4 Map1;      // row.xy, source size
    uint4 Mode;     // steps, flags, resample target size
    float4 Controls;// exposure gain, brightness, contrast, saturation
    float4 Levels;  // gain, bias, gamma power
    float4 Matrix0; float4 Matrix1; float4 Matrix2;
    float4 Shadow;  // shadow, highlight, no-op mix, highlight gain
    float4 Curve[4];// spline interval j: y, b, c, d
};
static const uint StepExposure=1u, StepControls=2u, StepTemperature=4u, StepHighlightShadow=8u, StepTone=16u, StepLevels=32u, StepGamma=64u, StepLookup=128u;
static const uint FlagSourcePremultiplied=1u, FlagLinear=2u, FlagOutputPremultiplied=4u;

float4 FullScreen(uint id : SV_VertexID) : SV_Position {
    float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}
float Encode(float x) { return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(max(x, 0.0031308), 1.0 / 2.4) - 0.055; }
float Decode(float x) { return x <= 0.04045 ? x / 12.92 : pow((max(x, 0.04045) + 0.055) / 1.055, 2.4); }
float SignedDecode(float v) { return v < 0 ? -Decode(-v) : Decode(v); }
float SignedEncode(float v) { return v < 0 ? -Encode(-v) : Encode(v); }
float3 ToLinear(float3 v) { return float3(SignedDecode(v.r), SignedDecode(v.g), SignedDecode(v.b)); }
float3 ToEncoded(float3 v) { return float3(SignedEncode(v.r), SignedEncode(v.g), SignedEncode(v.b)); }
float Power(float x, float e) {
    if (e == 1) return x;
    if (e == 0) return 1;
    if (x <= 0) return 0;
    return exp2(e * log2(x));
}
float Smooth(float e0, float e1, float x) { float t = saturate((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }
float Blend(float a, float b, float t) { return a * (1 - t) + b * t; }
float3 Blend3(float3 a, float3 b, float t) { return a * (1 - t) + b * t; }
float Luma(float3 v) { return v.r * 0.299 + v.g * 0.587 + v.b * 0.114; }
float MidWeight(float y) { return max(max(-2.6 * y * y - 2.6 * y + 0.98, -6.25 * y * y - 6.25 * y + 0.5965), 1); }
float SignOf(float v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

// CIHighlightShadowAdjust at radius 0 (see MediaAssemblyHighlightShadowKernel).
float3 HighlightShadow(float3 p) {
    float s = Shadow.x, h = Shadow.y, z = Shadow.z, g = Shadow.w;
    float peak = max(0, max(max(p.r, p.g), p.b));
    float root = sqrt(peak);
    float warmth = (max(p.r, 0) + 0.8 * max(p.g, 0) + 1.1 * max(p.b, 0)) / max(0.001, p.r + p.g + p.b);
    float lift = s == 0 ? 0 : s * Power(min(warmth, 1), 1 - s);
    float scale = 0.5 + 0.5 * Smooth(0.5, 1, s);
    bool linearShadow = z == 1;
    float3 e = float3(1, 1, 1);
    float3 shade = float3(0, 0, 0);
    [unroll] for (int c = 0; c < 3; ++c) {
        float base = max(p[c], 0) * scale;
        if (linearShadow) shade[c] = (1 + lift) * base * 2;
        else { e[c] = Blend(exp2(-lift - p[c]), 1, z); shade[c] = (1 + lift) * Power(base, e[c]) * 2; }
    }
    float y = p.r * 0.299 + p.g * 0.587 + p.b * 0.114;
    float i = p.r * 0.596 + p.g * -0.2755 + p.b * -0.321;
    float q = p.r * 0.212 + p.g * -0.523 + p.b * 0.311;
    float curved = SignOf(y) * (linearShadow ? abs(y) * scale : Power(abs(y) * scale, e.r)) * 2;
    float3 lumaOnly = float3(curved * 1.00048 + i * 0.955558 + q * 0.619549,
                             curved * 0.999864 + i * -0.271545 + q * -0.646786,
                             curved * 0.999446 + i * -1.10803 + q * 1.70542);
    float fade = Smooth(0, 0.1 + lift, sqrt(root));
    shade = Blend3(shade, lumaOnly, 0.35);
    shade = Blend3(p, shade, fade);
    shade = Blend3(shade, p, root);
    float3 high = float3(0, 0, 0);
    [unroll] for (int k = 0; k < 3; ++k) high[k] = SignOf(p[k]) * Power(abs(p[k]) * g, 2 - h);
    float pivotMix = 1 + (1 - min(1, h + 0.3)) * 0.4;
    float boost = min(MidWeight(Luma(high)), 30 * peak) * (1 - h);
    float highFade = root <= 0.2 ? 0 : (root >= 0.8 ? 1 : Smooth(0.2, 0.8, root));
    high = Blend3(high, Blend3(float3(0.25, 0.25, 0.25), high, pivotMix), boost);
    high = Blend3(p, high, highFade);
    high = Blend3(p, high, peak);
    float3 result = Blend3(shade, high, min(root, 1));
    float restore = min(MidWeight(Luma(result)), 30 * peak);
    float stretch = 1 + abs(lift) * 0.1 * (1 - z);
    result = Blend3(result, Blend3(float3(0.5, 0.5, 0.5), result, stretch), restore);
    return max(result, 0) + min(p, 0);
}
float Tone(float x) {
    float e = Encode(saturate(x));
    uint j = min(3u, (uint)floor(e * 4));
    float dx = e - (float)j * 0.25;
    float4 k = Curve[j];
    float y = k.x + k.y * dx + k.z * dx * dx + k.w * dx * dx * dx;
    return Decode(saturate(y));
}
float3 Lookup(float3 v) {
    float3 at = saturate(v) * 31;
    uint3 lo = (uint3)at;
    uint3 hi = min(uint3(31, 31, 31), lo + 1);
    float3 f = at - (float3)lo;
    float3 o = 0;
    [unroll] for (uint z = 0; z < 2; ++z) [unroll] for (uint y = 0; y < 2; ++y) [unroll] for (uint x = 0; x < 2; ++x) {
        uint3 c = uint3(x ? hi.x : lo.x, y ? hi.y : lo.y, z ? hi.z : lo.z);
        float w = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y) * (z ? f.z : 1 - f.z);
        o += w * Cube.Load(int4(c, 0)).rgb;
    }
    return o;
}
float4 Edit(float4 c) {
    float a = c.a;
    if (a <= 0) return 0;
    uint steps = Mode.x, flags = Mode.y;
    float3 v = (flags & FlagSourcePremultiplied) ? c.rgb / a : c.rgb;
    bool linearSpace = (flags & FlagLinear) != 0;
    if (steps == 0) return float4(v, a);
    if (linearSpace) v = ToLinear(v);
    if (steps & StepExposure) v *= Controls.x;
    if (steps & StepControls) {
        float l = v.r * 0.2125 + v.g * 0.7154 + v.b * 0.0721;
        v = l + (v - l) * Controls.w;
        v = (v - 0.5) * Controls.z + 0.5;
        v += Controls.y;
    }
    if (steps & StepTemperature) v = float3(Matrix0.x * v.r + Matrix0.y * v.g + Matrix0.z * v.b,
                                           Matrix1.x * v.r + Matrix1.y * v.g + Matrix1.z * v.b,
                                           Matrix2.x * v.r + Matrix2.y * v.g + Matrix2.z * v.b);
    if (steps & StepHighlightShadow) v = HighlightShadow(v);
    if (steps & StepTone) v = float3(Tone(v.r), Tone(v.g), Tone(v.b));
    if (steps & StepLevels) v = saturate(v * Levels.x + Levels.y);
    if (steps & StepGamma) v = float3(Power(max(v.r, 0), Levels.z), Power(max(v.g, 0), Levels.z), Power(max(v.b, 0), Levels.z));
    if (steps & StepLookup) {
        v = Lookup(saturate(linearSpace ? ToEncoded(v) : v));
        if (linearSpace) v = ToLinear(v);
    }
    if (linearSpace) v = ToEncoded(v);
    return float4(v, a);
}
float4 Output(float4 straight) {
    float a = saturate(straight.a);
    float3 p = min(straight.rgb * straight.a, a);
    if (Mode.y & FlagOutputPremultiplied) return float4(p, a);
    return a <= 0 ? float4(0, 0, 0, 0) : float4(p / a, a);
}
float4 Process(float4 position : SV_Position) : SV_Target {
    int2 o = int2(position.xy);
    int2 s = Map0.xy + o.x * Map0.zw + o.y * Map1.xy;
    float4 c = float4(0, 0, 0, 0);
    if (s.x >= 0 && s.y >= 0 && s.x < Map1.z && s.y < Map1.w) c = Source.Load(int3(s, 0));
    return Output(Edit(c));
}
// mediaAssemblyResample on associated pixels (no renormalisation at edges).
float4 Resample(float4 position : SV_Position) : SV_Target {
    float2 o = floor(position.xy);
    float2 scale = float2(Map1.zw) / float2(Mode.zw);
    float2 uv = (o + 0.5) * scale - 0.5;
    float2 f = floor(uv);
    int2 i0 = int2(f);
    float2 w = uv - f;
    float4 acc = float4(0, 0, 0, 0);
    [unroll] for (int dj = 0; dj < 2; ++dj) [unroll] for (int di = 0; di < 2; ++di) {
        int2 i = i0 + int2(di, dj);
        if (i.x >= 0 && i.y >= 0 && i.x < Map1.z && i.y < Map1.w)
            acc += ((di ? w.x : 1 - w.x) * (dj ? w.y : 1 - w.y)) * Source.Load(int3(i, 0));
    }
    float a = saturate(acc.a);
    return float4(min(acc.rgb, a), a);
}
)hlsl";

struct Bytecode {std::vector<std::uint8_t>vertex,process,resample;};
std::vector<std::uint8_t>compile(const char*entry,const char*target){
    ComPtr<ID3DBlob>code,errors;
    const HRESULT hr=D3DCompile(shaderSource,sizeof shaderSource-1,"media_assembly_gpu.hlsl",nullptr,nullptr,entry,target,
        D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(FAILED(hr)){std::string text=errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):std::string{};throw std::runtime_error("Media Assembly GPU shader compile failed: "+text);}
    const auto*bytes=static_cast<const std::uint8_t*>(code->GetBufferPointer());return {bytes,bytes+code->GetBufferSize()};
}
// Compiled once per process (UI previews and export jobs share it).
std::atomic<bool>compiled{};
const Bytecode&bytecode(){
    static std::once_flag once;static Bytecode value;static std::exception_ptr failure;
    std::call_once(once,[]{try{value.vertex=compile("FullScreen","vs_4_0");value.process=compile("Process","ps_4_0");value.resample=compile("Resample","ps_4_0");compiled=true;}catch(...){failure=std::current_exception();}});
    if(failure)std::rethrow_exception(failure);return value;
}
constexpr unsigned flagSourcePremultiplied=1,flagLinear=2,flagOutputPremultiplied=4;
struct alignas(16) Constants {
    std::int32_t map0[4]{},map1[4]{};std::uint32_t mode[4]{};float controls[4]{},levels[4]{},matrix0[4]{},matrix1[4]{},matrix2[4]{},shadow[4]{},curve[16]{};
};
static_assert(sizeof(Constants)==13*16,"HLSL cbuffer layout");
}

struct NativeMediaAssemblyGpuProcessor::Impl {
    ComPtr<ID3D11Device>device;ComPtr<ID3D11DeviceContext>context;
    ComPtr<ID3D11VertexShader>vertex;ComPtr<ID3D11PixelShader>process,resample;ComPtr<ID3D11Buffer>constants;ComPtr<ID3D11RasterizerState>raster;
    ComPtr<ID3D11Texture2D>source;ComPtr<ID3D11ShaderResourceView>sourceView;unsigned sourceWidth{},sourceHeight{},rotation{};bool sourcePremultiplied{};
    ComPtr<ID3D11Texture3D>cube;ComPtr<ID3D11ShaderResourceView>cubeView;std::vector<std::uint8_t>cubeBytes;
    Constants edit{};bool configured{};m::MediaAssemblyPixelPlan plan{};
    ComPtr<ID3D11Texture2D>scratch;ComPtr<ID3D11ShaderResourceView>scratchView;D3D11_TEXTURE2D_DESC scratchDesc{};
    IDXGISurface*surface{};ComPtr<ID3D11Texture2D>surfaceTexture;ComPtr<ID3D11RenderTargetView>surfaceTarget;unsigned surfaceWidth{},surfaceHeight{};
    ComPtr<ID3D11Texture2D>processed;ComPtr<ID3D11RenderTargetView>processedTarget;ComPtr<ID3D11ShaderResourceView>processedView;unsigned processedWidth{},processedHeight{};
    ComPtr<ID3D11Texture2D>output,staging;ComPtr<ID3D11RenderTargetView>outputTarget;unsigned outputWidth{},outputHeight{};
    MediaAssemblyGpuStats statistics;

    void texture(ComPtr<ID3D11Texture2D>&t,unsigned w,unsigned h,DXGI_FORMAT format,UINT bind,D3D11_USAGE usage=D3D11_USAGE_DEFAULT,UINT cpu=0){
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;d.Usage=usage;d.BindFlags=bind;d.CPUAccessFlags=cpu;
        t.Reset();check(device->CreateTexture2D(&d,nullptr,&t),"create texture");++statistics.targetAllocations;
    }
    void pass(ID3D11PixelShader*shader,ID3D11ShaderResourceView*view,ID3D11RenderTargetView*target,unsigned w,unsigned h,const Constants&k){
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"map constants");std::memcpy(mapped.pData,&k,sizeof k);context->Unmap(constants.Get(),0);
        context->ClearState();
        const D3D11_VIEWPORT viewport{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(shader,nullptr,0);
        ID3D11ShaderResourceView*views[2]{view,cubeView.Get()};context->PSSetShaderResources(0,2,views);ID3D11Buffer*buffers[1]{constants.Get()};context->PSSetConstantBuffers(0,1,buffers);
        context->OMSetRenderTargets(1,&target,nullptr);context->Draw(3,0);
        // Leave no bindings behind (the renderer re-establishes its own state per pass).
        context->ClearState();
    }
    void bindSurface(IDXGISurface*s){
        need(s,"missing target surface");if(s==surface&&surfaceTarget)return;
        ComPtr<ID3D11Texture2D>t;check(s->QueryInterface(IID_PPV_ARGS(&t)),"target surface is not a D3D11 texture");
        D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);need((d.BindFlags&D3D11_BIND_RENDER_TARGET)&&d.SampleDesc.Count==1&&d.ArraySize==1,"target surface must be a single-sample render target");
        need(d.Format==DXGI_FORMAT_B8G8R8A8_UNORM||d.Format==DXGI_FORMAT_R8G8B8A8_UNORM,"target surface must be 8-bit UNORM");
        ComPtr<ID3D11Device>owner;t->GetDevice(&owner);need(owner.Get()==device.Get(),"target surface belongs to another device");
        ComPtr<ID3D11RenderTargetView>view;check(device->CreateRenderTargetView(t.Get(),nullptr,&view),"create target view");
        surface=s;surfaceTexture=std::move(t);surfaceTarget=std::move(view);surfaceWidth=d.Width;surfaceHeight=d.Height;
    }
};

NativeMediaAssemblyGpuProcessor::NativeMediaAssemblyGpuProcessor(std::shared_ptr<void>device):impl_(std::make_unique<Impl>()){
    need(device!=nullptr,"missing device");auto&i=*impl_;i.device=static_cast<ID3D11Device*>(device.get());i.device->GetImmediateContext(&i.context);
    const auto&code=bytecode();
    check(i.device->CreateVertexShader(code.vertex.data(),code.vertex.size(),nullptr,&i.vertex),"create vertex shader");
    check(i.device->CreatePixelShader(code.process.data(),code.process.size(),nullptr,&i.process),"create processing shader");
    check(i.device->CreatePixelShader(code.resample.data(),code.resample.size(),nullptr,&i.resample),"create resample shader");
    D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Constants);b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    check(i.device->CreateBuffer(&b,nullptr,&i.constants),"create constants");
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;check(i.device->CreateRasterizerState(&r,&i.raster),"create rasterizer state");
}
NativeMediaAssemblyGpuProcessor::~NativeMediaAssemblyGpuProcessor()=default;
bool NativeMediaAssemblyGpuProcessor::hasSource()const noexcept{return impl_->sourceView!=nullptr;}
MediaAssemblyGpuStats NativeMediaAssemblyGpuProcessor::stats()const noexcept{return impl_->statistics;}
void NativeMediaAssemblyGpuProcessor::releaseTargets()noexcept{auto&i=*impl_;i.surface=nullptr;i.surfaceTarget.Reset();i.surfaceTexture.Reset();}

void NativeMediaAssemblyGpuProcessor::setSource(unsigned w,unsigned h,std::span<const std::uint8_t>rgba,std::size_t stride,bool premultiplied,unsigned rotation){
    auto&i=*impl_;need(w&&h&&w<=16384&&h<=16384,"source exceeds the D3D11 texture limit");need(rotation==0||rotation==90||rotation==180||rotation==270,"invalid source rotation");
    need(stride>=std::size_t(w)*4&&rgba.size()>=stride*(h-1)+std::size_t(w)*4,"source rows are too short");
    if(!i.source||i.sourceWidth!=w||i.sourceHeight!=h){
        i.sourceView.Reset();i.texture(i.source,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE);--i.statistics.targetAllocations;++i.statistics.sourceAllocations;
        check(i.device->CreateShaderResourceView(i.source.Get(),nullptr,&i.sourceView),"create source view");i.sourceWidth=w;i.sourceHeight=h;
    }
    i.context->UpdateSubresource(i.source.Get(),0,nullptr,rgba.data(),UINT(stride),0);++i.statistics.sourceUploads;
    // A different size or rotation changes the edit plan: configure again.
    if(i.rotation!=rotation||i.plan.sourceWidth!=((rotation==90||rotation==270)?h:w)||i.plan.sourceHeight!=((rotation==90||rotation==270)?w:h))i.configured=false;
    i.sourcePremultiplied=premultiplied;i.rotation=rotation;
}

m::MediaAssemblyPixelPlan NativeMediaAssemblyGpuProcessor::configure(const m::MediaAssemblyAdjustments&a,std::span<const std::uint8_t>lookup,bool linear){
    auto&i=*impl_;need(a.valid(),"invalid adjustments");
    const m::MediaAssemblyColorPipeline pipeline(a,lookup,linear,false);const auto c=pipeline.constants();
    Constants k{};
    if(i.source){
        const bool swap=i.rotation==90||i.rotation==270;const unsigned ow=swap?i.sourceHeight:i.sourceWidth,oh=swap?i.sourceWidth:i.sourceHeight;
        i.plan=m::mediaAssemblyPixelPlan(ow,oh,a);const auto map=m::mediaAssemblyPixelMap(i.plan);
        // raw = A * oriented + b for the display rotation (inverse of the CPU rotate()).
        long long A[4]{1,0,0,1},b[2]{0,0};const long long W=i.sourceWidth,H=i.sourceHeight;
        if(i.rotation==90){A[0]=0;A[1]=1;A[2]=-1;A[3]=0;b[1]=H-1;}
        else if(i.rotation==180){A[0]=-1;A[3]=-1;b[0]=W-1;b[1]=H-1;}
        else if(i.rotation==270){A[0]=0;A[1]=-1;A[2]=1;A[3]=0;b[0]=W-1;}
        const auto apply=[&](const std::array<long long,2>&v,bool translate){return std::array<long long,2>{A[0]*v[0]+A[1]*v[1]+(translate?b[0]:0),A[2]*v[0]+A[3]*v[1]+(translate?b[1]:0)};};
        const auto origin=apply(map.origin,true),column=apply(map.column,false),row=apply(map.row,false);
        const auto fits=[](long long v){return v>=-65536&&v<=65536;};
        need(fits(origin[0])&&fits(origin[1])&&fits(column[0])&&fits(column[1])&&fits(row[0])&&fits(row[1]),"geometry out of range");
        k.map0[0]=std::int32_t(origin[0]);k.map0[1]=std::int32_t(origin[1]);k.map0[2]=std::int32_t(column[0]);k.map0[3]=std::int32_t(column[1]);
        k.map1[0]=std::int32_t(row[0]);k.map1[1]=std::int32_t(row[1]);k.map1[2]=std::int32_t(i.sourceWidth);k.map1[3]=std::int32_t(i.sourceHeight);
    }
    k.mode[0]=c.steps;k.mode[1]=linear?flagLinear:0;
    k.controls[0]=c.exposure;k.controls[1]=c.brightness;k.controls[2]=c.contrast;k.controls[3]=c.saturation;
    k.levels[0]=c.levelGain;k.levels[1]=c.levelBias;k.levels[2]=c.gammaPower;
    for(int r=0;r<3;++r){float*row=r==0?k.matrix0:r==1?k.matrix1:k.matrix2;for(int col=0;col<3;++col)row[col]=c.temperature[std::size_t(r*3+col)];}
    k.shadow[0]=c.highlightShadow.shadow;k.shadow[1]=c.highlightShadow.highlight;k.shadow[2]=c.highlightShadow.noOpMix;k.shadow[3]=c.highlightShadow.gain;
    for(std::size_t j=0;j<4;++j)for(std::size_t n=0;n<4;++n)k.curve[j*4+n]=float(c.curve[j][n]);
    if(c.steps&m::mediaAssemblyStepLookup){
        const auto bytes=c.lookup;need(bytes.size()==32*32*32*3,"lookup must be the original 32 cubed RGB8 cube");
        if(!i.cube||i.cubeBytes.size()!=bytes.size()||!std::equal(bytes.begin(),bytes.end(),i.cubeBytes.begin())){
            std::vector<std::uint8_t>texels(32*32*32*4);for(std::size_t n=0;n<32*32*32;++n){texels[n*4]=bytes[n*3];texels[n*4+1]=bytes[n*3+1];texels[n*4+2]=bytes[n*3+2];texels[n*4+3]=255;}
            D3D11_TEXTURE3D_DESC d{};d.Width=d.Height=d.Depth=32;d.MipLevels=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA init{texels.data(),32*4,32*32*4};i.cubeView.Reset();i.cube.Reset();
            check(i.device->CreateTexture3D(&d,&init,&i.cube),"create lookup cube");check(i.device->CreateShaderResourceView(i.cube.Get(),nullptr,&i.cubeView),"create lookup view");
            i.cubeBytes.assign(bytes.begin(),bytes.end());++i.statistics.lookupUploads;
        }
    }
    i.edit=k;i.configured=true;return i.plan;
}

void NativeMediaAssemblyGpuProcessor::render(IDXGISurface*target){
    auto&i=*impl_;need(i.configured&&i.source,"configure a source first");i.bindSurface(target);
    need(i.surfaceWidth==i.plan.outputWidth&&i.surfaceHeight==i.plan.outputHeight,"target size differs from the edit plan");
    auto k=i.edit;k.mode[1]|=i.sourcePremultiplied?flagSourcePremultiplied:0;
    i.pass(i.process.Get(),i.sourceView.Get(),i.surfaceTarget.Get(),i.plan.outputWidth,i.plan.outputHeight,k);++i.statistics.renders;
}

void NativeMediaAssemblyGpuProcessor::processInPlace(IDXGISurface*s){
    auto&i=*impl_;need(i.configured,"configure the edits first");i.bindSurface(s);
    D3D11_TEXTURE2D_DESC d{};i.surfaceTexture->GetDesc(&d);
    if(!i.scratch||i.scratchDesc.Width!=d.Width||i.scratchDesc.Height!=d.Height||i.scratchDesc.Format!=d.Format){
        i.scratchView.Reset();i.texture(i.scratch,d.Width,d.Height,d.Format,D3D11_BIND_SHADER_RESOURCE);
        check(i.device->CreateShaderResourceView(i.scratch.Get(),nullptr,&i.scratchView),"create scratch view");i.scratch->GetDesc(&i.scratchDesc);
    }
    i.context->CopyResource(i.scratch.Get(),i.surfaceTexture.Get());
    auto k=i.edit;k.map0[0]=k.map0[1]=0;k.map0[2]=1;k.map0[3]=0;k.map1[0]=0;k.map1[1]=1;k.map1[2]=std::int32_t(d.Width);k.map1[3]=std::int32_t(d.Height);
    i.pass(i.process.Get(),i.scratchView.Get(),i.surfaceTarget.Get(),d.Width,d.Height,k);++i.statistics.inPlace;
}

void NativeMediaAssemblyGpuProcessor::renderToMemory(unsigned w,unsigned h,std::span<std::uint8_t>bgra,std::size_t stride){
    auto&i=*impl_;need(i.configured&&i.source,"configure a source first");need(w&&h&&w<=16384&&h<=16384,"export frame exceeds the D3D11 texture limit");
    need(stride>=std::size_t(w)*4&&bgra.size()>=stride*(h-1)+std::size_t(w)*4,"export rows are too short");
    const unsigned pw=i.plan.outputWidth,ph=i.plan.outputHeight;const bool resample=pw!=w||ph!=h;
    if(!i.output||i.outputWidth!=w||i.outputHeight!=h){
        i.outputTarget.Reset();i.texture(i.output,w,h,DXGI_FORMAT_B8G8R8A8_UNORM,D3D11_BIND_RENDER_TARGET);check(i.device->CreateRenderTargetView(i.output.Get(),nullptr,&i.outputTarget),"create export target");
        i.texture(i.staging,w,h,DXGI_FORMAT_B8G8R8A8_UNORM,0,D3D11_USAGE_STAGING,D3D11_CPU_ACCESS_READ);i.outputWidth=w;i.outputHeight=h;
    }
    auto k=i.edit;k.mode[1]|=(i.sourcePremultiplied?flagSourcePremultiplied:0)|flagOutputPremultiplied;
    if(resample){
        if(!i.processed||i.processedWidth!=pw||i.processedHeight!=ph){
            i.processedTarget.Reset();i.processedView.Reset();i.texture(i.processed,pw,ph,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
            check(i.device->CreateRenderTargetView(i.processed.Get(),nullptr,&i.processedTarget),"create processed target");check(i.device->CreateShaderResourceView(i.processed.Get(),nullptr,&i.processedView),"create processed view");
            i.processedWidth=pw;i.processedHeight=ph;
        }
        i.pass(i.process.Get(),i.sourceView.Get(),i.processedTarget.Get(),pw,ph,k);
        Constants r{};r.map1[2]=std::int32_t(pw);r.map1[3]=std::int32_t(ph);r.mode[2]=w;r.mode[3]=h;
        i.pass(i.resample.Get(),i.processedView.Get(),i.outputTarget.Get(),w,h,r);
    }else i.pass(i.process.Get(),i.sourceView.Get(),i.outputTarget.Get(),w,h,k);
    i.context->CopyResource(i.staging.Get(),i.output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(i.context->Map(i.staging.Get(),0,D3D11_MAP_READ,0,&mapped),"read back export frame");
    for(unsigned y=0;y<h;++y)std::memcpy(bgra.data()+std::size_t(y)*stride,static_cast<const std::uint8_t*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,std::size_t(w)*4);
    i.context->Unmap(i.staging.Get(),0);++i.statistics.renders;++i.statistics.readbacks;
}

void prepareMediaAssemblyGpuShaders(){(void)bytecode();}
bool mediaAssemblyGpuShadersReady()noexcept{return compiled.load();}
std::shared_ptr<void>createMediaAssemblyExportDevice()noexcept{
    try{
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};ComPtr<ID3D11Device>device;
        HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,UINT(std::size(levels)),D3D11_SDK_VERSION,&device,nullptr,nullptr);
        if(hr==E_INVALIDARG)hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels+1,UINT(std::size(levels)-1),D3D11_SDK_VERSION,&device,nullptr,nullptr);
        if(FAILED(hr)||!device)return nullptr;
        return std::shared_ptr<void>(device.Detach(),[](void*p){static_cast<ID3D11Device*>(p)->Release();});
    }catch(...){return nullptr;}
}
}
