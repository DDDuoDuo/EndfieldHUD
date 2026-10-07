#include "presentation_adapter.h"

#include <d3dcompiler.h>
#include <limits>

namespace ehud::render {
namespace {
constexpr char shader[]=R"hlsl(
Texture2D<float4> sourceTexture : register(t0);
SamplerState sourceSampler : register(s0);
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vertexMain(uint id : SV_VertexID) {
    Vertex result;
    result.uv=float2((id<<1)&2,id&2);
    result.position=float4(result.uv*float2(2,-2)+float2(-1,1),0,1);
    return result;
}
float encodeSrgb(float value) {
    return value<=0.0031308 ? value*12.92 : 1.055*pow(value,1.0/2.4)-0.055;
}
float4 pixelMain(Vertex input) : SV_Target {
    // Sampling the sRGB SRV decodes RGB once; alpha remains linear coverage.
    float4 source=sourceTexture.SampleLevel(sourceSampler,input.uv,0);
    // Source additive passes may retain positive RGB with zero coverage. Keep
    // that energy, while the fully transparent zero-RGB source stays clear.
    if(source.a<=0) return float4(encodeSrgb(source.r),encodeSrgb(source.g),encodeSrgb(source.b),0);
    float3 straight=source.rgb/source.a;
    return float4(float3(encodeSrgb(straight.r),encodeSrgb(straight.g),encodeSrgb(straight.b))*source.a,source.a);
}
)hlsl";
bool sourceFormat(DXGI_FORMAT format) {
    return format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}
bool targetFormat(DXGI_FORMAT format) {
    return format==DXGI_FORMAT_B8G8R8A8_UNORM || format==DXGI_FORMAT_R8G8B8A8_UNORM;
}
} // namespace

HRESULT SourcePresentationAdapter::initialize(ID3D11Device* device) {
    reset();
    if(!device) return E_INVALIDARG;
    device_=device;device->GetImmediateContext(&context_);
    if(!context_) {reset();initializationError_="Immediate D3D11 context unavailable";return E_UNEXPECTED;}
    auto compile=[&](const char* entry,const char* profile,ComPtr<ID3DBlob>& code) {
        ComPtr<ID3DBlob> errors;
        const HRESULT hr=D3DCompile(shader,sizeof(shader)-1,"source-presentation-adapter",nullptr,nullptr,
            entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
        if(FAILED(hr)) {
            const std::string message=errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()) : "Presentation shader compilation failed";
            reset();initializationError_=message;
        }
        return hr;
    };
    ComPtr<ID3DBlob> vertexCode,pixelCode;
    HRESULT hr=compile("vertexMain","vs_4_0",vertexCode);if(FAILED(hr))return hr;
    hr=compile("pixelMain","ps_4_0",pixelCode);if(FAILED(hr))return hr;
    auto checked=[&](HRESULT status,const char* operation) {
        if(FAILED(status)) {reset();initializationError_=operation;}
        return status;
    };
    hr=checked(device->CreateVertexShader(vertexCode->GetBufferPointer(),vertexCode->GetBufferSize(),nullptr,&vertex_),"Create presentation vertex shader");if(FAILED(hr))return hr;
    hr=checked(device->CreatePixelShader(pixelCode->GetBufferPointer(),pixelCode->GetBufferSize(),nullptr,&pixel_),"Create presentation pixel shader");if(FAILED(hr))return hr;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxAnisotropy=1;
    sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    hr=checked(device->CreateSamplerState(&sampler,&sampler_),"Create presentation point-clamp sampler");if(FAILED(hr))return hr;
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].SrcBlend=blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend=blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;
    blend.RenderTarget[0].BlendOp=blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    hr=checked(device->CreateBlendState(&blend,&blend_),"Create presentation no-blend state");if(FAILED(hr))return hr;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable=FALSE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
    hr=checked(device->CreateDepthStencilState(&depth,&depth_),"Create presentation depth-off state");if(FAILED(hr))return hr;
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode=D3D11_FILL_SOLID;rasterizer.CullMode=D3D11_CULL_NONE;rasterizer.DepthClipEnable=TRUE;
    hr=checked(device->CreateRasterizerState(&rasterizer,&rasterizer_),"Create presentation full-viewport rasterizer");
    return hr;
}

HRESULT SourcePresentationAdapter::draw(ID3D11RenderTargetView* encodedTarget,
                                      ID3D11ShaderResourceView* sourceSrgbView,unsigned width,unsigned height) {
    if(!context_ || !vertex_ || !pixel_) return E_UNEXPECTED;
    if(!encodedTarget || !sourceSrgbView || !width || !height ||
       width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) return E_INVALIDARG;
    D3D11_RENDER_TARGET_VIEW_DESC target{};encodedTarget->GetDesc(&target);
    D3D11_SHADER_RESOURCE_VIEW_DESC source{};sourceSrgbView->GetDesc(&source);
    // Reject a miswired view rather than silently adding/removing a transfer.
    if(!targetFormat(target.Format) || !sourceFormat(source.Format) ||
       target.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || source.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D) return E_INVALIDARG;
    ComPtr<ID3D11Resource> targetResource,sourceResource;
    encodedTarget->GetResource(&targetResource);sourceSrgbView->GetResource(&sourceResource);
    if(targetResource.Get()==sourceResource.Get()) return E_INVALIDARG;
    context_->OMSetRenderTargets(1,&encodedTarget,nullptr);
    context_->OMSetBlendState(blend_.Get(),nullptr,(std::numeric_limits<UINT>::max)());
    context_->OMSetDepthStencilState(depth_.Get(),0);
    context_->RSSetState(rasterizer_.Get());
    const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
    context_->RSSetViewports(1,&viewport);
    context_->IASetInputLayout(nullptr);
    ID3D11Buffer* noBuffer{};const UINT zero{};
    context_->IASetVertexBuffers(0,1,&noBuffer,&zero,&zero);
    context_->IASetIndexBuffer(nullptr,DXGI_FORMAT_R16_UINT,0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->SOSetTargets(0,nullptr,nullptr);
    context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);context_->GSSetShader(nullptr,nullptr,0);
    context_->VSSetShader(vertex_.Get(),nullptr,0);context_->PSSetShader(pixel_.Get(),nullptr,0);
    ID3D11SamplerState* sampler=sampler_.Get();context_->PSSetSamplers(0,1,&sampler);
    context_->PSSetShaderResources(0,1,&sourceSrgbView);
    context_->Draw(3,0);
    // The caller rebinds its next pass. No pipeline reference should keep source
    // surfaces or this adapter's cached states alive after resize/reset.
    ID3D11ShaderResourceView* noView{};context_->PSSetShaderResources(0,1,&noView);
    context_->OMSetRenderTargets(0,nullptr,nullptr);
    ID3D11SamplerState* noSampler{};context_->PSSetSamplers(0,1,&noSampler);
    context_->VSSetShader(nullptr,nullptr,0);context_->PSSetShader(nullptr,nullptr,0);
    context_->OMSetBlendState(nullptr,nullptr,(std::numeric_limits<UINT>::max)());
    context_->OMSetDepthStencilState(nullptr,0);context_->RSSetState(nullptr);
    return device_->GetDeviceRemovedReason();
}

void SourcePresentationAdapter::reset() {
    rasterizer_.Reset();depth_.Reset();blend_.Reset();sampler_.Reset();pixel_.Reset();vertex_.Reset();
    context_.Reset();device_.Reset();initializationError_.clear();
}
} // namespace ehud::render
