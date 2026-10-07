#include "frozen_backdrop.h"

#include <d3dcompiler.h>
#include <array>
#include <cmath>

namespace ehud::render {
namespace {
constexpr char shader[]=R"hlsl(
Texture2D<float4> snapshot : register(t0);
SamplerState linearClamp : register(s0);
cbuffer BackdropConstants : register(b0) {float4 parameters;float4 flags;};
cbuffer Kernel : register(b1) {float4 weights[19];};
struct Vertex {float4 position : SV_Position;float2 uv : TEXCOORD0;};
Vertex vertexMain(uint id : SV_VertexID) {
    Vertex outVertex;outVertex.uv=float2((id<<1)&2,id&2);
    outVertex.position=float4(outVertex.uv*float2(2,-2)+float2(-1,1),0,1);return outVertex;
}
float4 gaussianMain(Vertex input) : SV_Target {
    float3 color=0;
    [unroll]for(int i=-9;i<=9;++i)color+=snapshot.SampleLevel(linearClamp,input.uv+parameters.xy*i,0).rgb*weights[i+9].x;
    return float4(color,1);
}
float decodeSrgb(float value) {return value<=0.04045?value/12.92:pow((value+0.055)/1.055,2.4);}
float4 compositeMain(Vertex input) : SV_Target {
    float envelope=parameters.x,darkness=parameters.y,blurAmount=parameters.z;
    bool dark=parameters.w!=0;
    float radius=saturate(length(input.uv-float2(.5,.46))/length(float2(.5,.54)));
    float middle=dark?.06:.02,edge=dark?.38:.14;
    float vignette=radius<=.5?lerp(0,middle,radius*2):lerp(middle,edge,(radius-.5)*2);
    // Darkness and child vignette are combined before their parent envelope.
    float groupAlpha=(vignette+darkness*(1-vignette))*envelope;
    float3 groupColor=decodeSrgb(dark?.015:.90)*darkness*(1-vignette)*envelope;
    float blurAlpha=flags.x!=0?blurAmount*envelope:0;
    float3 blurred=flags.x!=0?snapshot.SampleLevel(linearClamp,input.uv,0).rgb:float3(0,0,0);
    return float4(groupColor+blurred*blurAlpha*(1-groupAlpha),groupAlpha+blurAlpha*(1-groupAlpha));
}
)hlsl";
bool validStyle(double alpha,const FrozenBackdropStyle& style) {
    return std::isfinite(alpha)&&alpha>=0&&alpha<=1&&std::isfinite(style.darkness)&&style.darkness>=0&&style.darkness<=1&&
        std::isfinite(style.blur_amount)&&style.blur_amount>=0&&style.blur_amount<=1;
}
} // namespace
HRESULT FrozenBackdrop::initialize(ID3D11Device* device,ID3D11DeviceContext* context) {
    clear();vertex_.Reset();gaussian_.Reset();composite_.Reset();sampler_.Reset();blend_.Reset();raster_.Reset();depth_.Reset();
    parameters_.Reset();weights_.Reset();device_.Reset();context_.Reset();error_.clear();
    if(!device||!context)return E_INVALIDARG;
    Ptr<ID3D11Device> owner;context->GetDevice(&owner);if(owner.Get()!=device)return E_INVALIDARG;
    device_=device;context_=context;
    auto checked=[&](HRESULT hr,const char* operation){if(FAILED(hr))error_=operation;return hr;};
    auto compile=[&](const char* entry,const char* profile,Ptr<ID3DBlob>& code) {
        Ptr<ID3DBlob> errors;const HRESULT hr=D3DCompile(shader,sizeof(shader)-1,"frozen-desktop-prototype",nullptr,nullptr,
            entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
        if(FAILED(hr))error_=errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"Backdrop shader compilation failed";
        return hr;
    };
    Ptr<ID3DBlob> vs,blur,composite;HRESULT hr=compile("vertexMain","vs_4_0",vs);if(FAILED(hr))return hr;
    hr=compile("gaussianMain","ps_4_0",blur);if(FAILED(hr))return hr;
    hr=compile("compositeMain","ps_4_0",composite);if(FAILED(hr))return hr;
    hr=checked(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_),"Create backdrop vertex shader");if(FAILED(hr))return hr;
    hr=checked(device->CreatePixelShader(blur->GetBufferPointer(),blur->GetBufferSize(),nullptr,&gaussian_),"Create backdrop Gaussian shader");if(FAILED(hr))return hr;
    hr=checked(device->CreatePixelShader(composite->GetBufferPointer(),composite->GetBufferSize(),nullptr,&composite_),"Create backdrop source layer compositor");if(FAILED(hr))return hr;
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxAnisotropy=1;
    sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    hr=checked(device->CreateSamplerState(&sampler,&sampler_),"Create backdrop linear clamp sampler");if(FAILED(hr))return hr;
    D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    target.SrcBlend=target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlend=target.DestBlendAlpha=D3D11_BLEND_ZERO;
    target.BlendOp=target.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    hr=checked(device->CreateBlendState(&blend,&blend_),"Create backdrop overwrite state");if(FAILED(hr))return hr;
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    hr=checked(device->CreateRasterizerState(&raster,&raster_),"Create fixed screen backdrop rasterizer");if(FAILED(hr))return hr;
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
    hr=checked(device->CreateDepthStencilState(&depth,&depth_),"Create backdrop depth-off state");if(FAILED(hr))return hr;
    D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=32;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    hr=checked(device->CreateBuffer(&buffer,nullptr,&parameters_),"Create cached backdrop pass constants");if(FAILED(hr))return hr;
    std::array<std::array<float,4>,19> weights{};double sum{};
    for(int offset=-9;offset<=9;++offset)sum+=std::exp(-double(offset*offset)/18.);
    for(int offset=-9;offset<=9;++offset)weights[offset+9][0]=static_cast<float>(std::exp(-double(offset*offset)/18.)/sum);
    buffer.ByteWidth=sizeof(weights);buffer.Usage=D3D11_USAGE_IMMUTABLE;
    const D3D11_SUBRESOURCE_DATA data{weights.data(),0,0};
    return checked(device->CreateBuffer(&buffer,&data,&weights_),"Create immutable prototype Gaussian weights");
}
void FrozenBackdrop::clear() {
    snapshot_.reset();rawView_.Reset();temporaryView_.Reset();blurView_.Reset();temporaryTarget_.Reset();blurTarget_.Reset();
    raw_.Reset();temporary_.Reset();blur_.Reset();blurWidth_=blurHeight_=0;
}
std::size_t FrozenBackdrop::retained_pixel_bytes() const noexcept {
    return snapshot_?snapshot_->byte_size()*2+static_cast<std::size_t>(blurWidth_)*blurHeight_*16:0;
}
void FrozenBackdrop::bind(ID3D11RenderTargetView* target,ID3D11PixelShader* pixel,ID3D11ShaderResourceView* input,unsigned width,unsigned height) {
    context_->OMSetRenderTargets(1,&target,nullptr);context_->OMSetBlendState(blend_.Get(),nullptr,~UINT(0));context_->OMSetDepthStencilState(depth_.Get(),0);
    context_->RSSetState(raster_.Get());const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context_->RSSetViewports(1,&viewport);
    context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->SOSetTargets(0,nullptr,nullptr);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);context_->GSSetShader(nullptr,nullptr,0);
    context_->VSSetShader(vertex_.Get(),nullptr,0);context_->PSSetShader(pixel,nullptr,0);
    ID3D11SamplerState* sampler=sampler_.Get();context_->PSSetSamplers(0,1,&sampler);context_->PSSetShaderResources(0,1,&input);
    ID3D11Buffer* constants[]{parameters_.Get(),weights_.Get()};context_->PSSetConstantBuffers(0,2,constants);
}
void FrozenBackdrop::unbind() {
    ID3D11ShaderResourceView* none{};context_->PSSetShaderResources(0,1,&none);context_->OMSetRenderTargets(0,nullptr,nullptr);
    ID3D11Buffer* noBuffers[]{nullptr,nullptr};context_->PSSetConstantBuffers(0,2,noBuffers);
    ID3D11SamplerState* noSampler{};context_->PSSetSamplers(0,1,&noSampler);
    context_->VSSetShader(nullptr,nullptr,0);context_->PSSetShader(nullptr,nullptr,0);
    context_->OMSetBlendState(nullptr,nullptr,~UINT(0));context_->OMSetDepthStencilState(nullptr,0);context_->RSSetState(nullptr);
}
HRESULT FrozenBackdrop::prepare(endfield::platform::FrozenSnapshot snapshot) {
    clear();if(!context_||!gaussian_||!parameters_||!weights_||!sampler_||!vertex_)return E_UNEXPECTED;if(!snapshot)return E_INVALIDARG;
    auto fail=[&](HRESULT hr){if(FAILED(hr))clear();return hr;};
    D3D11_TEXTURE2D_DESC image{};image.Width=snapshot->width();image.Height=snapshot->height();image.MipLevels=image.ArraySize=image.SampleDesc.Count=1;
    image.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;image.Usage=D3D11_USAGE_IMMUTABLE;image.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{snapshot->bgra().data(),image.Width*4,0};
    HRESULT hr=device_->CreateTexture2D(&image,&data,&raw_);if(FAILED(hr))return fail(hr);
    hr=device_->CreateShaderResourceView(raw_.Get(),nullptr,&rawView_);if(FAILED(hr))return fail(hr);
    blurWidth_=(image.Width+3)/4;blurHeight_=(image.Height+3)/4;image.Width=blurWidth_;image.Height=blurHeight_;
    image.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;image.Usage=D3D11_USAGE_DEFAULT;image.BindFlags|=D3D11_BIND_RENDER_TARGET;
    auto createBlurImage=[&](Ptr<ID3D11Texture2D>& texture,Ptr<ID3D11ShaderResourceView>& view) {
        HRESULT result=device_->CreateTexture2D(&image,nullptr,texture.ReleaseAndGetAddressOf());
        if(FAILED(result))return result;
        return device_->CreateShaderResourceView(texture.Get(),nullptr,view.ReleaseAndGetAddressOf());
    };
    hr=createBlurImage(temporary_,temporaryView_);if(FAILED(hr))return fail(hr);
    hr=createBlurImage(blur_,blurView_);if(FAILED(hr))return fail(hr);
    hr=device_->CreateRenderTargetView(temporary_.Get(),nullptr,&temporaryTarget_);if(FAILED(hr))return fail(hr);
    hr=device_->CreateRenderTargetView(blur_.Get(),nullptr,&blurTarget_);if(FAILED(hr))return fail(hr);
    std::array<float,8> constants{1.f/blurWidth_,0,0,0,0,0,0,0};context_->UpdateSubresource(parameters_.Get(),0,nullptr,constants.data(),0,0);
    bind(temporaryTarget_.Get(),gaussian_.Get(),rawView_.Get(),blurWidth_,blurHeight_);context_->Draw(3,0);unbind();
    constants[0]=0;constants[1]=1.f/blurHeight_;context_->UpdateSubresource(parameters_.Get(),0,nullptr,constants.data(),0,0);
    bind(blurTarget_.Get(),gaussian_.Get(),temporaryView_.Get(),blurWidth_,blurHeight_);context_->Draw(3,0);unbind();
    hr=device_->GetDeviceRemovedReason();if(FAILED(hr))return fail(hr);
    snapshot_=std::move(snapshot);return S_OK;
}
HRESULT FrozenBackdrop::draw(ID3D11RenderTargetView* target,unsigned width,unsigned height,double alpha,const FrozenBackdropStyle& style) {
    if(!context_||!composite_||!parameters_||!weights_)return E_UNEXPECTED;
    if(!target||width>endfield::platform::FrozenDesktopSnapshot::maximum_side||height>endfield::platform::FrozenDesktopSnapshot::maximum_side||
       !endfield::platform::FrozenDesktopSnapshot::valid_bounds({0,0,static_cast<std::int32_t>(width),static_cast<std::int32_t>(height)})||!validStyle(alpha,style))return E_INVALIDARG;
    if(ready()&&(snapshot_->width()!=width||snapshot_->height()!=height))return E_INVALIDARG;
    D3D11_RENDER_TARGET_VIEW_DESC description{};target->GetDesc(&description);
    if(description.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||
       description.Texture2D.MipSlice!=0||
       (description.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB&&description.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))return E_INVALIDARG;
    Ptr<ID3D11Device> owner;target->GetDevice(&owner);if(owner.Get()!=device_.Get())return E_INVALIDARG;
    Ptr<ID3D11Resource> resource;target->GetResource(&resource);Ptr<ID3D11Texture2D> texture;
    if(FAILED(resource.As(&texture)))return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC extent{};texture->GetDesc(&extent);
    if(extent.Width!=width||extent.Height!=height||extent.SampleDesc.Count!=1||extent.ArraySize!=1)return E_INVALIDARG;
    const std::array<float,8> constants{static_cast<float>(alpha),static_cast<float>(style.darkness),
        static_cast<float>(style.low_power?0:style.blur_amount),style.dark?1.f:0.f,ready()?1.f:0.f,0,0,0};
    context_->UpdateSubresource(parameters_.Get(),0,nullptr,constants.data(),0,0);
    bind(target,composite_.Get(),ready()?blurView_.Get():nullptr,width,height);context_->Draw(3,0);unbind();
    return device_->GetDeviceRemovedReason();
}
} // namespace ehud::render
