#include "source_draw.h"
#include "resources/resource_data.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>
namespace ehud::render {
namespace {
using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonArray;
template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
void checked(HRESULT status) { if (FAILED(status)) winrt::throw_hresult(status); }
JsonObject object(const std::filesystem::path& path) { return JsonObject::Parse(winrt::to_hstring(resources::text(path))); }
double number(JsonObject const& value, const wchar_t* name, double fallback = 0) { return value.GetNamedNumber(name, fallback); }
unsigned integer(JsonObject const& value, const wchar_t* name, unsigned minimum, unsigned maximum, unsigned fallback = 0) {
    const double result = number(value, name, fallback);
    if (!std::isfinite(result) || result != std::floor(result) || result < minimum || result > maximum)
        throw std::runtime_error("Source texture integer descriptor is outside its bound");
    return static_cast<unsigned>(result);
}
std::string string(JsonObject const& value, const wchar_t* name) { return winrt::to_string(value.GetNamedString(name, L"")); }
std::filesystem::path safePath(const std::filesystem::path& root, std::string_view file) {
    auto path = std::filesystem::path(winrt::to_hstring(file).c_str());
    if (file.empty() || path.is_absolute() || file.find(':') != std::string::npos || file.find('\\') != std::string::npos)
        throw std::runtime_error("Invalid source texture path");
    for (const auto& part : path) if (part == L".." || part == L".") throw std::runtime_error("Invalid source texture traversal");
    return root / path;
}
D3D11_TEXTURE_ADDRESS_MODE address(int value) {
    switch(value) { case 0: return D3D11_TEXTURE_ADDRESS_WRAP; case 1: return D3D11_TEXTURE_ADDRESS_CLAMP;
    case 2: return D3D11_TEXTURE_ADDRESS_MIRROR; case 3: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
    default: throw std::runtime_error("Unknown source wrap mode"); }
}
float linear(double value) { return static_cast<float>(value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4)); }
struct Vertex { float position[4], uv[2], color[4]; };
struct alignas(16) Constants {
    float st[4]{1,1,0,0}; float sampleAdd[4]{};
    float opaque{}, additive{}; unsigned edgeCount{}, padding{};
    float edges[32][4]{};
};
const char shader[] = R"(
cbuffer Params : register(b0) { float4 st; float4 sampleAdd; float opaque; float additive; uint edgeCount; uint isText; float4 edges[32]; };
Texture2D sourceTexture : register(t0); SamplerState sourceSampler : register(s0);
struct V { float4 position : POSITION; float2 uv : TEXCOORD; float4 color : COLOR; };
struct P { float4 position : SV_Position; float2 uv : TEXCOORD; float4 color : COLOR; };
P vertexMain(V v) { P o; o.position = v.position; o.uv = v.uv * st.xy + st.zw; o.color = v.color; return o; }
float4 pixelMain(P p) : SV_Target {
    [loop] for (uint i = 0; i < edgeCount; ++i) clip(dot(float3(p.position.xy,1), edges[i].xyz));
    float4 color = p.color; color.a = round(color.a * 255) / 255;
    float4 sample = sourceTexture.Sample(sourceSampler, p.uv); sample.a = opaque != 0 ? 1 : sample.a;
    if (isText != 0) sample.rgb = 1;
    float4 result = color * (sample + sampleAdd);
    // Encode straight linear RGB before premultiplying, matching D2D and the
    // desktop compositor's encoded-space BGRA alpha contract.
    float3 rgb = max(result.rgb, 0);
    float3 encoded = lerp(1.055 * pow(rgb, 1.0 / 2.4) - .055, rgb * 12.92, step(rgb, .0031308));
    result.rgb = encoded * result.a; result.a *= 1 - additive; return result;
}
)";
}
struct SourceDraw::Impl {
    struct Asset { Ptr<ID3D11ShaderResourceView> view; Ptr<ID3D11SamplerState> sampler; };
    struct Material { Constants constants; std::string mainTexture; };
    struct Text { Asset asset; std::string key; std::uint64_t used{}; };
    Ptr<ID3D11Device> device; Ptr<ID3D11DeviceContext> context; Ptr<ID2D1DeviceContext> painter;
    Ptr<IDWriteFactory> fonts; Ptr<ID3D11VertexShader> vertex; Ptr<ID3D11PixelShader> pixel;
    Ptr<ID3D11InputLayout> layout; Ptr<ID3D11Buffer> vertices, constants;
    Ptr<ID3D11BlendState> blend; Ptr<ID3D11RasterizerState> raster; Ptr<ID3D11DepthStencilState> depth;
    std::map<std::string, JsonObject> descriptors; std::map<std::string, Asset> assets;
    std::map<std::string, Material> materials; std::map<std::string, Text> texts;
    std::filesystem::path root; unsigned capacity{}; std::uint64_t generation{};
    Asset& texture(const std::string& id) {
        if (auto found = assets.find(id); found != assets.end()) return found->second;
        auto found = descriptors.find(id); if (found == descriptors.end()) throw std::runtime_error("Unresolved source texture ID");
        const auto& info = found->second;
        const unsigned width = integer(info,L"width",1,16384), height = integer(info,L"height",1,16384);
        const unsigned mips = integer(info,L"mip_count",1,15); const unsigned format = integer(info,L"texture_format",0,63);
        if (!width || !height || width > 16384 || height > 16384 || !mips || mips > 15 || (format != 25 && format != 4 && format != 63))
            throw std::runtime_error("Invalid source texture descriptor");
        const bool srgb = number(info,L"color_space",1) == 0;
        if (format == 63 && srgb) throw std::runtime_error("Invalid gamma R8 atlas");
        auto bytes = resources::read(safePath(root,string(info,L"data_file")));
        std::vector<D3D11_SUBRESOURCE_DATA> levels; std::size_t cursor{};
        for (unsigned mip = 0; mip < mips; ++mip) {
            unsigned w = std::max(1u,width >> mip), h = std::max(1u,height >> mip);
            const unsigned row = format == 25 ? ((w + 3) / 4) * 16 : w * (format == 63 ? 1 : 4);
            const std::size_t count = std::size_t(row) * (format == 25 ? (h + 3) / 4 : h);
            if (cursor + count > bytes.size()) throw std::runtime_error("Truncated source mip chain");
            levels.push_back({bytes.data() + cursor,row,0}); cursor += count;
        }
        if (cursor != bytes.size()) throw std::runtime_error("Trailing source mip bytes");
        D3D11_TEXTURE2D_DESC description{}; description.Width=width; description.Height=height;
        description.MipLevels=mips; description.ArraySize=1; description.SampleDesc.Count=1;
        description.Usage=D3D11_USAGE_IMMUTABLE; description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        description.Format = format == 25 ? (srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM) :
            format == 4 ? (srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM) : DXGI_FORMAT_R8_UNORM;
        Ptr<ID3D11Texture2D> native; checked(device->CreateTexture2D(&description,levels.data(),&native));
        Asset asset; checked(device->CreateShaderResourceView(native.Get(),nullptr,&asset.view));
        const auto sampler = info.GetNamedObject(L"sampler"); const unsigned filter = integer(sampler,L"m_FilterMode",0,2,1);
        D3D11_SAMPLER_DESC sd{}; sd.Filter= filter==0 ? D3D11_FILTER_MIN_MAG_MIP_POINT : filter==1 ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=address(static_cast<int>(integer(sampler,L"m_WrapU",0,3,1))); sd.AddressV=address(static_cast<int>(integer(sampler,L"m_WrapV",0,3,1))); sd.AddressW=address(static_cast<int>(integer(sampler,L"m_WrapW",0,3,1)));
        const double bias=number(sampler,L"m_MipBias");
        if (!std::isfinite(bias) || bias < D3D11_MIP_LOD_BIAS_MIN || bias > D3D11_MIP_LOD_BIAS_MAX) throw std::runtime_error("Invalid source mip bias");
        sd.MinLOD=0; sd.MaxLOD=D3D11_FLOAT32_MAX; sd.MipLODBias=static_cast<float>(bias);
        sd.MaxAnisotropy=std::clamp(integer(sampler,L"m_Aniso",0,65535,1),1u,16u);
        if (sd.MaxAnisotropy > 1 && filter > 0) sd.Filter = D3D11_FILTER_ANISOTROPIC;
        checked(device->CreateSamplerState(&sd,&asset.sampler)); return assets.emplace(id,std::move(asset)).first->second;
    }
    Asset& text(const scene::Graphic& graphic) {
        std::string key=graphic.text + ":" + std::to_string(graphic.fontSize) + ":" + std::to_string(graphic.rect.size.x) + ":" + std::to_string(graphic.rect.size.y);
        auto& record=texts[graphic.componentId]; record.used=generation;
        if (record.key==key && record.asset.view) return record.asset;
        auto value=winrt::to_hstring(graphic.text);
        if (!std::isfinite(graphic.rect.size.x) || !std::isfinite(graphic.rect.size.y) || !std::isfinite(graphic.fontSize))
            throw std::runtime_error("Invalid source text dimensions");
        unsigned w=static_cast<unsigned>(std::clamp(std::ceil(graphic.rect.size.x),1.,4096.)), h=static_cast<unsigned>(std::clamp(std::ceil(graphic.rect.size.y),1.,2048.));
        D3D11_TEXTURE2D_DESC description{}; description.Width=w; description.Height=h; description.MipLevels=1; description.ArraySize=1;
        description.Format=DXGI_FORMAT_B8G8R8A8_UNORM; description.SampleDesc.Count=1; description.BindFlags=D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        Ptr<ID3D11Texture2D> native; checked(device->CreateTexture2D(&description,nullptr,&native));
        Ptr<IDXGISurface> surface; checked(native.As(&surface)); Ptr<ID2D1Bitmap1> bitmap;
        auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,D2D1::PixelFormat(description.Format,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        checked(painter->CreateBitmapFromDxgiSurface(surface.Get(),&properties,&bitmap));
        Ptr<IDWriteTextFormat> format; checked(fonts->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,
            static_cast<float>(std::clamp(graphic.fontSize,1.,256.)),L"en-US",&format));
        Ptr<ID2D1SolidColorBrush> white; checked(painter->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&white));
        Ptr<ID2D1Image> previous; painter->GetTarget(&previous); painter->SetTarget(bitmap.Get());
        painter->BeginDraw(); painter->SetTransform(D2D1::Matrix3x2F::Identity()); painter->Clear(D2D1::ColorF(0,0,0,0));
        painter->DrawText(value.c_str(),static_cast<UINT32>(value.size()),format.Get(),D2D1::RectF(0,0,static_cast<float>(w),static_cast<float>(h)),white.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
        auto status=painter->EndDraw(); painter->SetTarget(previous.Get()); checked(status);
        record.asset={}; checked(device->CreateShaderResourceView(native.Get(),nullptr,&record.asset.view));
        record.asset.sampler=assets.at("__white").sampler; record.key=std::move(key); return record.asset;
    }
};
SourceDraw::SourceDraw():impl_(std::make_unique<Impl>()){} SourceDraw::~SourceDraw()=default;
HRESULT SourceDraw::initialize(ID3D11Device* device, ID3D11DeviceContext* context, ID2D1DeviceContext* painter, IDWriteFactory* fonts, const std::filesystem::path& root) {
    try {
        auto& d=*impl_; d.device=device; d.context=context; d.painter=painter; d.fonts=fonts; d.root=root;
        auto textures=JsonArray::Parse(winrt::to_hstring(resources::text(root/L"textures.json")));
        for (const auto& item:textures) { auto info=item.GetObject(); auto id=string(info,L"path_id");
            d.descriptors.emplace(id,info); d.descriptors.emplace(string(info,L"cab") + ":" + id,info); }
        for (const auto& item:object(root/L"Scene"/L"materials.json").GetNamedArray(L"materials")) {
            auto material=item.GetObject(); Impl::Material native;
            auto saved=material.GetNamedObject(L"data").GetNamedObject(L"m_SavedProperties");
            for (const auto& entry:saved.GetNamedArray(L"m_Floats")) {auto values=entry.GetArray(); auto name=values.GetStringAt(0);
                if(name==L"_UIImageOpaque") native.constants.opaque=static_cast<float>(values.GetNumberAt(1));
                else if(name==L"_UseAdditiveBlendMode") native.constants.additive=static_cast<float>(values.GetNumberAt(1)); }
            for (const auto& entry:saved.GetNamedArray(L"m_TexEnvs")) {auto values=entry.GetArray(); if(values.GetStringAt(0)!=L"_MainTex") continue;
                auto info=values.GetObjectAt(1); native.mainTexture=string(info.GetNamedObject(L"m_Texture"),L"target_id");
                auto scale=info.GetNamedObject(L"m_Scale"), offset=info.GetNamedObject(L"m_Offset");
                native.constants.st[0]=static_cast<float>(number(scale,L"x",1)); native.constants.st[1]=static_cast<float>(number(scale,L"y",1));
                native.constants.st[2]=static_cast<float>(number(offset,L"x")); native.constants.st[3]=static_cast<float>(number(offset,L"y")); }
            d.materials.emplace(string(material,L"id"),native);
        }
        Ptr<ID3DBlob> vs,ps,errors; checked(D3DCompile(shader,sizeof(shader)-1,"native-source-image",nullptr,nullptr,"vertexMain","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors));
        checked(D3DCompile(shader,sizeof(shader)-1,"native-source-image",nullptr,nullptr,"pixelMain","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors));
        checked(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&d.vertex)); checked(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&d.pixel));
        D3D11_INPUT_ELEMENT_DESC attributes[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
        checked(device->CreateInputLayout(attributes,3,vs->GetBufferPointer(),vs->GetBufferSize(),&d.layout));
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(Constants); cb.Usage=D3D11_USAGE_DYNAMIC; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        checked(device->CreateBuffer(&cb,nullptr,&d.constants));
        D3D11_BLEND_DESC bd{}; auto& b=bd.RenderTarget[0]; b.BlendEnable=TRUE; b.SrcBlend=D3D11_BLEND_ONE; b.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        b.BlendOp=D3D11_BLEND_OP_ADD; b.SrcBlendAlpha=D3D11_BLEND_ONE; b.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA; b.BlendOpAlpha=D3D11_BLEND_OP_ADD; b.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        checked(device->CreateBlendState(&bd,&d.blend));
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE; checked(device->CreateRasterizerState(&rd,&d.raster));
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable=FALSE; dd.StencilEnable=FALSE; checked(device->CreateDepthStencilState(&dd,&d.depth));
        const std::uint32_t white=0xffffffff; D3D11_SUBRESOURCE_DATA initial{&white,4,0}; D3D11_TEXTURE2D_DESC td{};
        td.Width=td.Height=td.MipLevels=td.ArraySize=td.SampleDesc.Count=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        Ptr<ID3D11Texture2D> texture; checked(device->CreateTexture2D(&td,&initial,&texture)); Impl::Asset asset;
        checked(device->CreateShaderResourceView(texture.Get(),nullptr,&asset.view)); D3D11_SAMPLER_DESC sd{};
        sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        checked(device->CreateSamplerState(&sd,&asset.sampler)); d.assets.emplace("__white",std::move(asset)); return S_OK;
    } catch(const winrt::hresult_error& error) {return error.code();} catch(...) {return E_FAIL;}
}
HRESULT SourceDraw::draw(ID3D11RenderTargetView* target, const scene::Frame& frame) {
    try {
        auto& d=*impl_; ++d.generation;
        // Rasterize changed text before binding the shared D3D target. The
        // cache is keyed by component and replaced, never appended on reopen.
        for(const auto& graphic:frame.graphics) if(graphic.kind=="UIText" && !graphic.text.empty()) d.text(graphic);
        d.context->OMSetRenderTargets(1,&target,nullptr); const float clear[4]{}; d.context->ClearRenderTargetView(target,clear);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(frame.camera.viewport.x),static_cast<float>(frame.camera.viewport.y),0,1};
        d.context->RSSetViewports(1,&viewport); d.context->RSSetState(d.raster.Get()); d.context->OMSetBlendState(d.blend.Get(),nullptr,UINT_MAX); d.context->OMSetDepthStencilState(d.depth.Get(),0);
        d.context->IASetInputLayout(d.layout.Get()); d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        d.context->VSSetShader(d.vertex.Get(),nullptr,0); d.context->PSSetShader(d.pixel.Get(),nullptr,0);
        auto cb=d.constants.Get(); d.context->VSSetConstantBuffers(0,1,&cb); d.context->PSSetConstantBuffers(0,1,&cb);
        struct Command { Constants constants; Impl::Asset* asset; UINT first, count; };
        std::vector<Command> commands; std::vector<Vertex> vertices;
        for(const auto& graphic:frame.graphics) {
            if(graphic.quads.empty() || graphic.color[3]<=0 || (graphic.kind=="UIText" && graphic.text.empty())) continue;
            if(graphic.quads.size() > (1048576 - vertices.size()) / 6) throw std::runtime_error("Source draw vertex bound exceeded");
            Constants constants{}; std::string id=graphic.textureId;
            if(auto material=d.materials.find(graphic.materialId); material!=d.materials.end()) {constants=material->second.constants; if(id.empty()) id=material->second.mainTexture;}
            const bool isText=graphic.kind=="UIText" && !graphic.text.empty();
            auto& asset=isText ? d.texts.at(graphic.componentId).asset : d.texture(id.empty()?"__white":id);
            if(isText) {constants.st[0]=constants.st[1]=1; constants.st[2]=constants.st[3]=0; constants.padding=1;}
            if(graphic.masks.size()>8) throw std::runtime_error("Source mask depth exceeds native bound");
            for(const auto& mask:graphic.masks) {
                auto r=mask.rect; std::array<scene::Vec3,4> points{{{r.origin.x,r.origin.y,0},{r.origin.x,r.origin.y+r.size.y,0},{r.origin.x+r.size.x,r.origin.y+r.size.y,0},{r.origin.x+r.size.x,r.origin.y,0}}};
                std::array<scene::Vec2,4> projected{};
                for(unsigned i=0;i<4;++i) {auto p=frame.camera.project(points[i],mask.world); if(!p) throw std::runtime_error("Invalid source mask projection"); projected[i]=*p;}
                double area{}; for(unsigned i=0;i<4;++i) area+=projected[i].x*projected[(i+1)%4].y-projected[(i+1)%4].x*projected[i].y;
                const double sign=area>=0?1:-1;
                for(unsigned i=0;i<4;++i) {auto a=projected[i],b=projected[(i+1)%4]; auto& edge=constants.edges[constants.edgeCount++];
                    edge[0]=static_cast<float>(sign*(a.y-b.y)); edge[1]=static_cast<float>(sign*(b.x-a.x)); edge[2]=static_cast<float>(sign*(a.x*b.y-b.x*a.y));}
            }
            const UINT first=static_cast<UINT>(vertices.size()); const auto matrix=frame.camera.viewProjection*graphic.world;
            for(std::size_t q=0;q<graphic.quads.size();++q) for(unsigned i:{0u,1u,2u,0u,2u,3u}) {
                auto p=graphic.quads[q][i]; const auto& m=matrix.values; Vertex v{};
                for(unsigned row=0;row<4;++row) v.position[row]=static_cast<float>(m[row]*p.x+m[4+row]*p.y+m[8+row]*p.z+m[12+row]);
                v.position[2]=0; auto uv=q<graphic.uvQuads.size()?graphic.uvQuads[q][i]:scene::Vec2{};
                // Source raw mip rows and Unity UVs share the same origin.
                if(isText) uv={i<2?0.:1.,i==0||i==3?1.:0.};
                v.uv[0]=static_cast<float>(uv.x); v.uv[1]=static_cast<float>(uv.y);
                for(unsigned c=0;c<3;++c) v.color[c]=graphic.vertexColorReady ? static_cast<float>(graphic.color[c]) :
                    linear(std::nearbyint(std::clamp(graphic.color[c],0.,1.)*255)/255);
                v.color[3]=static_cast<float>(std::clamp(graphic.color[3],0.,1.)); vertices.push_back(v);
            }
            commands.push_back({constants,&asset,first,static_cast<UINT>(vertices.size())-first});
        }
        if(vertices.size()>1048576) throw std::runtime_error("Source draw vertex bound exceeded");
        if(!vertices.empty()) {
            if(vertices.size()>d.capacity) {d.vertices.Reset(); d.capacity=std::max(4096u,static_cast<unsigned>(vertices.size()));
                D3D11_BUFFER_DESC bd{}; bd.ByteWidth=d.capacity*sizeof(Vertex); bd.Usage=D3D11_USAGE_DYNAMIC; bd.BindFlags=D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE; checked(d.device->CreateBuffer(&bd,nullptr,&d.vertices));}
            D3D11_MAPPED_SUBRESOURCE mapped{}; checked(d.context->Map(d.vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)); std::memcpy(mapped.pData,vertices.data(),vertices.size()*sizeof(Vertex)); d.context->Unmap(d.vertices.Get(),0);
            auto buffer=d.vertices.Get(); UINT stride=sizeof(Vertex),offset=0; d.context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
            for(const auto& command:commands) {
                checked(d.context->Map(d.constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)); std::memcpy(mapped.pData,&command.constants,sizeof(Constants)); d.context->Unmap(d.constants.Get(),0);
                auto view=command.asset->view.Get(); auto sampler=command.asset->sampler.Get(); d.context->PSSetShaderResources(0,1,&view); d.context->PSSetSamplers(0,1,&sampler); d.context->Draw(command.count,command.first);
            }
        }
        ID3D11ShaderResourceView* empty=nullptr; d.context->PSSetShaderResources(0,1,&empty);
        for(auto it=d.texts.begin();it!=d.texts.end();) if(it->second.used!=d.generation) it=d.texts.erase(it); else ++it;
        return S_OK;
    } catch(const winrt::hresult_error& error) {return error.code();} catch(...) {return E_FAIL;}
}
std::size_t SourceDraw::textureCount() const {return impl_->assets.size();}
std::size_t SourceDraw::textCount() const {return impl_->texts.size();}
}
