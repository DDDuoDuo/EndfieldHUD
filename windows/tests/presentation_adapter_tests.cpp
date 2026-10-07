#include "render/presentation_adapter.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
constexpr unsigned width=8,height=2;
struct Pixel { unsigned r{},g{},b{},a{}; };
using Image=std::array<Pixel,width*height>;
int checks{};
void check(bool value,const char* reason) {
    ++checks;if(!value)throw std::runtime_error(reason);
}
void checked(HRESULT status,const char* operation) {
    if(FAILED(status)) {
        std::ostringstream message;message<<operation<<" HRESULT=0x"<<std::hex<<static_cast<unsigned long>(status);
        throw std::runtime_error(message.str());
    }
}
void expect(Pixel actual,Pixel wanted,const char* reason,unsigned tolerance=1) {
    auto close=[=](unsigned a,unsigned b){return a>b?a-b<=tolerance:b-a<=tolerance;};
    if(!close(actual.r,wanted.r)||!close(actual.g,wanted.g)||!close(actual.b,wanted.b)||!close(actual.a,wanted.a)) {
        std::ostringstream message;message<<reason<<": actual RGBA="<<actual.r<<','<<actual.g<<','<<actual.b<<','<<actual.a
            <<" expected="<<wanted.r<<','<<wanted.g<<','<<wanted.b<<','<<wanted.a;
        throw std::runtime_error(message.str());
    }
    ++checks;
}
// Independent IEC transfer reference. The input is the source attachment's
// quantized sRGB encoding of linear-premultiplied RGB, not straight RGBA.
double decode(double value) {return value<=0.04045?value/12.92:std::pow((value+0.055)/1.055,2.4);}
double encode(double value) {return value<=0.0031308?12.92*value:1.055*std::pow(value,1./2.4)-0.055;}
unsigned byte(double value) {return static_cast<unsigned>(std::lround(std::clamp(value,0.,1.)*255));}
Pixel expected(Pixel source) {
    if(!source.a)return source; // Source zero-alpha additive energy is retained.
    const double alpha=source.a/255.;
    return {byte(encode(decode(source.r/255.)/alpha)*alpha),
            byte(encode(decode(source.g/255.)/alpha)*alpha),
            byte(encode(decode(source.b/255.)/alpha)*alpha),source.a};
}

class Gpu final {
public:
    Gpu() {
        D3D_FEATURE_LEVEL level{};
        checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"Create isolated presentation WARP device");
        check(level>=D3D_FEATURE_LEVEL_10_0,"WARP supports presentation shader model4");
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width=width;texture.Height=height;texture.MipLevels=texture.ArraySize=texture.SampleDesc.Count=1;
        texture.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;texture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        checked(device->CreateTexture2D(&texture,nullptr,&source),"Create reusable source texture");
        checked(device->CreateTexture2D(&texture,nullptr,&target),"Create reusable encoded target");
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        checked(device->CreateShaderResourceView(source.Get(),&srv,&sourceView),"Create decoding source SRGB view");
        checked(device->CreateShaderResourceView(target.Get(),&srv,&aliasSourceView),"Create deliberately aliased source view");
        srv.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        checked(device->CreateShaderResourceView(source.Get(),&srv,&wrongSourceView),"Create deliberately wrong linear view");
        D3D11_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;rtv.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        checked(device->CreateRenderTargetView(target.Get(),&rtv,&targetView),"Create encoded UNORM presentation view");
        rtv.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        checked(device->CreateRenderTargetView(target.Get(),&rtv,&wrongTargetView),"Create deliberately wrong SRGB presentation view");
        checked(device->CreateRenderTargetView(source.Get(),&rtv,&sourceTarget),"Create independent source SRGB attachment");
        texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM;texture.Usage=D3D11_USAGE_STAGING;
        texture.BindFlags=0;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        checked(device->CreateTexture2D(&texture,nullptr,&staging),"Create fixed diagnostic readback texture");
        const HRESULT status=adapter.initialize(device.Get());
        if(FAILED(status))throw std::runtime_error("Presentation adapter initialization: "+adapter.initializationError());
        initialize_layer_program();
    }
    ~Gpu() {context->ClearState();}
    void upload(const Image& image) {
        std::array<std::uint8_t,width*height*4> bgra{};
        for(std::size_t index=0;index<image.size();++index) {
            bgra[index*4]=static_cast<std::uint8_t>(image[index].b);bgra[index*4+1]=static_cast<std::uint8_t>(image[index].g);
            bgra[index*4+2]=static_cast<std::uint8_t>(image[index].r);bgra[index*4+3]=static_cast<std::uint8_t>(image[index].a);
        }
        context->UpdateSubresource(source.Get(),0,nullptr,bgra.data(),width*4,0);
    }
    void present() {checked(adapter.draw(targetView.Get(),sourceView.Get(),width,height),"Convert source to encoded premultiplied presentation");}
    Image read(ID3D11Texture2D* texture) {
        context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(staging.Get(),texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};checked(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read isolated presentation diagnostic");
        Image result{};
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const auto* pixel=static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch+x*4;
            result[y*width+x]={pixel[2],pixel[1],pixel[0],pixel[3]};
        }
        context->Unmap(staging.Get(),0);return result;
    }
    void begin_layers() {
        const float transparent[]{0,0,0,0};context->ClearRenderTargetView(sourceTarget.Get(),transparent);
        ID3D11RenderTargetView* view=sourceTarget.Get();context->OMSetRenderTargets(1,&view,nullptr);
        context->OMSetBlendState(layerBlend.Get(),nullptr,~UINT(0));context->OMSetDepthStencilState(layerDepth.Get(),0);
        context->RSSetState(layerRaster.Get());const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(layerVertex.Get(),nullptr,0);context->PSSetShader(layerPixel.Get(),nullptr,0);
        ID3D11Buffer* data=layerConstant.Get();context->PSSetConstantBuffers(0,1,&data);
    }
    void layer(std::array<float,4> linearPremultiplied) {
        context->UpdateSubresource(layerConstant.Get(),0,nullptr,linearPremultiplied.data(),0,0);context->Draw(3,0);
    }
    void end_layers() {context->OMSetRenderTargets(0,nullptr,nullptr);}
    Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;
    Ptr<ID3D11Texture2D> source,target,staging;
    Ptr<ID3D11ShaderResourceView> sourceView,wrongSourceView,aliasSourceView;
    Ptr<ID3D11RenderTargetView> targetView,wrongTargetView,sourceTarget;
    ehud::render::SourcePresentationAdapter adapter;
private:
    Ptr<ID3D11VertexShader> layerVertex;Ptr<ID3D11PixelShader> layerPixel;
    Ptr<ID3D11Buffer> layerConstant;Ptr<ID3D11BlendState> layerBlend;
    Ptr<ID3D11RasterizerState> layerRaster;Ptr<ID3D11DepthStencilState> layerDepth;
    void initialize_layer_program() {
        // Independent upstream fixture writes already linear-premultiplied
        // values; hardware SRGB attachment storage/blending provides the source.
        constexpr char program[]=R"hlsl(
cbuffer Layer : register(b0) { float4 layerColor; };
float4 vertex(uint id : SV_VertexID) : SV_Position {
    if(id==0) return float4(-1,1,0,1);
    if(id==1) return float4(3,1,0,1);
    return float4(-1,-3,0,1);
}
float4 pixel() : SV_Target { return layerColor; }
)hlsl";
        auto compile=[&](const char* entry,const char* profile) {
            Ptr<ID3DBlob> code,errors;
            const HRESULT status=D3DCompile(program,sizeof(program)-1,"independent-linear-layers",nullptr,nullptr,
                entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
            if(FAILED(status))throw std::runtime_error(errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"Layer fixture compilation failed");
            return code;
        };
        auto vertex=compile("vertex","vs_4_0"),pixel=compile("pixel","ps_4_0");
        checked(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&layerVertex),"Create independent layer vertex shader");
        checked(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&layerPixel),"Create independent layer pixel shader");
        D3D11_BUFFER_DESC constant{};constant.ByteWidth=16;constant.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        checked(device->CreateBuffer(&constant,nullptr,&layerConstant),"Create fixed layer constants");
        D3D11_BLEND_DESC blend{};auto& attachment=blend.RenderTarget[0];attachment.BlendEnable=TRUE;
        attachment.SrcBlend=attachment.SrcBlendAlpha=D3D11_BLEND_ONE;
        attachment.DestBlend=attachment.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        attachment.BlendOp=attachment.BlendOpAlpha=D3D11_BLEND_OP_ADD;attachment.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        checked(device->CreateBlendState(&blend,&layerBlend),"Create independent linear source-over blend");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        checked(device->CreateRasterizerState(&raster,&layerRaster),"Create independent layer rasterizer");
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        checked(device->CreateDepthStencilState(&depth,&layerDepth),"Create independent depth-off state");
    }
};

void byte_contracts(Gpu& gpu) {
    const std::array<Pixel,width> cases{{{188,188,188,128},{37,128,240,255},{118,86,39,128},
        {128,128,128,0},{1,2,3,128},{17,31,127,255},{188,0,137,191},{89,89,89,26}}};
    Image input{};
    for(unsigned x=0;x<width;++x) {input[x]=cases[x];input[width+x]=cases[width-x-1];}
    gpu.upload(input);gpu.present();const auto output=gpu.read(gpu.target.Get());
    expect(output[0],{128,128,128,128},"Half-white source188/128 becomes encoded-premultiplied128/128",0);
    expect(output[1],cases[1],"Opaque source RGB is preserved without another gamma transfer");
    expect(output[3],{128,128,128,0},"Zero-alpha additive RGB retains authored energy without a divide",0);
    expect(output[4],cases[4],"Low branch of IEC transfer preserves encoded low-light association");
    for(unsigned index=0;index<input.size();++index)expect(output[index],expected(input[index]),"Point sampling converts each source texel independently");
    for(const auto& pixel:output)if(pixel.a)check(pixel.r<=pixel.a+1&&pixel.g<=pixel.a+1&&pixel.b<=pixel.a+1,"Valid linear-premultiplied source yields associated encoded output");
    for(unsigned repeat=0;repeat<128;++repeat)gpu.present();
    const auto repeated=gpu.read(gpu.target.Get());
    for(unsigned index=0;index<input.size();++index)expect(repeated[index],output[index],"Repeated frames reuse resources without blending presentation over itself",0);
    Ptr<ID3D11ShaderResourceView> boundSource;gpu.context->PSGetShaderResources(0,1,&boundSource);
    Ptr<ID3D11RenderTargetView> boundTarget;gpu.context->OMGetRenderTargets(1,&boundTarget,nullptr);
    check(!boundSource&&!boundTarget,"Presentation releases pipeline references to source and encoded surfaces");
    check(gpu.adapter.draw(gpu.targetView.Get(),gpu.wrongSourceView.Get(),width,height)==E_INVALIDARG,"Reject non-SRGB source view rather than adding gamma");
    check(gpu.adapter.draw(gpu.wrongTargetView.Get(),gpu.sourceView.Get(),width,height)==E_INVALIDARG,"Reject SRGB destination rather than encoding twice");
    check(gpu.adapter.draw(gpu.targetView.Get(),gpu.aliasSourceView.Get(),width,height)==E_INVALIDARG,"Reject source/destination resource alias instead of D3D silently nulling input");
    check(gpu.adapter.draw(gpu.targetView.Get(),gpu.sourceView.Get(),0,height)==E_INVALIDARG,"Reject empty presentation viewport");
    check(gpu.adapter.draw(nullptr,gpu.sourceView.Get(),width,height)==E_INVALIDARG,"Reject missing destination view");
}
void layered_contracts(Gpu& gpu) {
    gpu.begin_layers();gpu.layer({.5f,.5f,.5f,.5f});gpu.end_layers();
    expect(gpu.read(gpu.source.Get())[0],{188,188,188,128},"Independent hardware SRGB attachment stores linear half-white as188/128");
    gpu.present();expect(gpu.read(gpu.target.Get())[0],{128,128,128,128},"Actual source SRGB render is associated correctly for presentation");
    gpu.begin_layers();gpu.layer({0,0,.5f,.5f});gpu.layer({.5f,0,0,.5f});gpu.end_layers();
    // Red50% over blue50% has linear-premultiplied RGB(.5,0,.25), coverage.75.
    // Compare scene composition semantics independently of the adapter shader.
    const auto source=gpu.read(gpu.source.Get())[0];
    expect(source,{188,0,137,191},"Upstream layers blend in linear space before source SRGB storage",2);
    gpu.present();const auto redOverBlue=gpu.read(gpu.target.Get())[0];
    expect(redOverBlue,{160,0,117,191},"Layered source becomes valid encoded-premultiplied red/blue coverage",2);
    check(redOverBlue.r<=redOverBlue.a&&redOverBlue.b<=redOverBlue.a,"Layered linear source preserves encoded premultiplication bound");
    gpu.begin_layers();gpu.layer({.5f,0,0,.5f});gpu.layer({0,0,.5f,.5f});gpu.end_layers();
    gpu.present();expect(gpu.read(gpu.target.Get())[0],{117,0,160,191},"Presentation preserves source layer order rather than composing encoded colors",2);
    gpu.begin_layers();gpu.layer({.2158605f,.2158605f,.2158605f,0});gpu.end_layers();
    expect(gpu.read(gpu.source.Get())[0],{128,128,128,0},"Actual SRGB attachment retains source additive color with zero coverage");
    gpu.present();expect(gpu.read(gpu.target.Get())[0],{128,128,128,0},"Presentation preserves actual authored zero-alpha additive contribution");
    gpu.begin_layers();gpu.end_layers();gpu.present();
    for(const auto& pixel:gpu.read(gpu.target.Get()))expect(pixel,{},"Transparent source overwrites previous frame across the entire viewport",0);
}
} // namespace
int main() {
    try {
        Gpu gpu;byte_contracts(gpu);layered_contracts(gpu);
        gpu.adapter.reset();
        check(gpu.adapter.draw(gpu.targetView.Get(),gpu.sourceView.Get(),width,height)==E_UNEXPECTED,"Reset detaches cached presentation pipeline");
        checked(gpu.adapter.initialize(gpu.device.Get()),"Reinitialize bounded presentation pipeline");
        gpu.present();expect(gpu.read(gpu.target.Get())[0],{},"Recreated adapter retains transparent source semantics",0);
        std::cout<<"PASS: "<<checks<<" isolated native WARP presentation color/alpha/layer contracts. Live composition/HDR parity remains unverified.\n";
        return 0;
    } catch(const std::exception& failure) {
        std::cerr<<"FAIL: "<<failure.what()<<'\n';return 1;
    }
}
