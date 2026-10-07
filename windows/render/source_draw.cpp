#include "source_draw.h"
#include "resources/resource_data.h"
#include <d3dcompiler.h>
#include <wincodec.h>
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
float materialLinear(float value) { return value <= .04045f ? value / 12.92f : value < 1 ? std::pow((value+.055f)/1.055f,2.4f) : std::pow(value,2.2f); }
D3D11_BLEND sourceBlend(unsigned value) {
    const D3D11_BLEND values[]{D3D11_BLEND_ZERO,D3D11_BLEND_ONE,D3D11_BLEND_DEST_COLOR,D3D11_BLEND_SRC_COLOR,
      D3D11_BLEND_INV_DEST_COLOR,D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_COLOR,D3D11_BLEND_DEST_ALPHA,
      D3D11_BLEND_INV_DEST_ALPHA,D3D11_BLEND_SRC_ALPHA_SAT,D3D11_BLEND_INV_SRC_ALPHA};
    if(value>=std::size(values)) throw std::runtime_error("Unknown source blend factor"); return values[value];
}
D3D11_BLEND_OP sourceBlendOp(unsigned value) {
    const D3D11_BLEND_OP values[]{D3D11_BLEND_OP_ADD,D3D11_BLEND_OP_SUBTRACT,D3D11_BLEND_OP_REV_SUBTRACT,D3D11_BLEND_OP_MIN,D3D11_BLEND_OP_MAX};
    if(value>=std::size(values)) throw std::runtime_error("Unknown source blend operation"); return values[value];
}
struct Vertex { float position[4], uv[2], color[4]; };
struct alignas(16) Constants {
    float st[4]{1,1,0,0}; float sampleAdd[4]{};
    float opaque{}, additive{}; unsigned edgeCount{}, padding{};
    float edges[32][4]{};
    unsigned program{}; float sceneTime{}, disableVertColor{}, useMainAsAlpha{};
    float useMaskAsAlpha{}, tintIntensity{}, tintAlpha{}, useVfxParameters{};
    float expThreshold{}, expIntensity{}, dissolveOffset{}, dissolveHardness{};
    float dissolveWidth{}, dissolveIntensity{}, dissolveColorWidth{}, dissolveRamp{};
    float dissolveByDir{}, paddingFx[3]{};
    float tint[4]{1,1,1,1}, mainSpeed[4]{}, mainRotate[4]{1,0,0,1};
    float maskST[4]{1,1,0,0}, maskSpeed[4]{}, maskRotate[4]{1,0,0,1};
    float dissolveST[4]{1,1,0,0}, dissolveSpeed[4]{}, dissolveRotate[4]{1,0,0,1};
    float emissive[4]{}, emissive2[4]{}, dissolveDir[4]{}, dissolvePoint[4]{};
    float vfxST[4]{1,1,0,0};
};
const char shader[] = R"(
// Source programs: UI Default 211/272 and UIMeshVfxEffect 12/13/15.
// The desktop output adapter retains encoded-space premultiplied BGRA.
cbuffer Params : register(b0) {
 float4 st; float4 sampleAdd; float opaque; float additive; uint edgeCount; uint isText; float4 edges[32];
 uint program; float sceneTime; float disableVertColor; float useMainAsAlpha;
 float useMaskAsAlpha; float tintIntensity; float tintAlpha; float useVfxParameters;
 float expThreshold; float expIntensity; float dissolveOffset; float dissolveHardness;
 float dissolveWidth; float dissolveIntensity; float dissolveColorWidth; float dissolveRamp;
 float dissolveByDir; float3 paddingFx;
 float4 tint; float4 mainSpeed; float4 mainRotate;
 float4 maskST; float4 maskSpeed; float4 maskRotate;
 float4 dissolveST; float4 dissolveSpeed; float4 dissolveRotate;
 float4 emissive; float4 emissive2; float4 dissolveDir; float4 dissolvePoint; float4 vfxST;
};
Texture2D sourceTexture : register(t0); SamplerState sourceSampler : register(s0);
Texture2D maskTexture : register(t1); SamplerState maskSampler : register(s1);
Texture2D dissolveTexture : register(t2); SamplerState dissolveSampler : register(s2);
struct V { float4 position : POSITION; float2 uv : TEXCOORD; float4 color : COLOR; };
struct P { float4 position : SV_Position; float2 uv : TEXCOORD; float4 color : COLOR; };
P vertexMain(V v) { P o; o.position = v.position; o.uv = program >= 12 ? v.uv : v.uv * st.xy + st.zw; o.color = v.color; return o; }
float2 effectUV(float2 uv, float4 transform, float4 speed, float4 rotate) {
 float2 q = uv + speed.xy * fmod(sceneTime, 1024.0) - .5;
 // Metal's float2x2 arguments are columns, not rows.
 return (float2(rotate.x*q.x + rotate.z*q.y, rotate.y*q.x + rotate.w*q.y) + .5) * transform.xy + transform.zw;
}
float4 alphaChannel(float4 value, float useRed) { return lerp(value, float4(1,1,1,value.r), useRed); }
float3 encodedRGB(float3 rgb) {
 rgb = max(rgb,0);
 return lerp(1.055 * pow(rgb, 1.0 / 2.4) - .055, rgb * 12.92, step(rgb, .0031308));
}
float4 pixelMain(P p) : SV_Target {
    [loop] for (uint i = 0; i < edgeCount; ++i) clip(dot(float3(p.position.xy,1), edges[i].xyz));
    float4 color = p.color;
    if (program != 0 && isText == 0) {
      bool mesh = program >= 12;
      float4 value = mesh ? tint * (disableVertColor != 0 ? float4(1,1,1,1) : p.color) : color;
      if (mesh) { value.rgb *= tintIntensity; value.a *= tintAlpha; }
      else { value *= tint; if (useVfxParameters != 0) { value.rgb *= tintIntensity; value.a *= tintAlpha; } value.a = round(value.a * 255) / 255; }
      value *= alphaChannel(sourceTexture.Sample(sourceSampler,
        effectUV(p.uv, mesh ? st : vfxST, mainSpeed, mainRotate)), useMainAsAlpha);
      if (program == 2 || program == 13 || program == 15)
        value *= alphaChannel(maskTexture.Sample(maskSampler, effectUV(p.uv, maskST, maskSpeed, maskRotate)), useMaskAsAlpha);
      if (program == 2 || program == 15) {
        float2 uv = effectUV(p.uv, dissolveST, dissolveSpeed, dissolveRotate);
        float field = dissolveTexture.Sample(dissolveSampler, uv).r - dissolveOffset;
        // Evaluate the direction term only when selected, avoiding 0 * NaN
        // for a disabled zero direction while preserving the authored mode.
        if (dissolveByDir != 0) {
          float directional = dissolveByDir == 1 ? dot(normalize(dissolveDir.xy), uv) : length(uv - dissolvePoint.xy);
          field += lerp(0, directional, saturate(dissolveByDir));
        }
        clip(field);
        float edge = smoothstep(dissolveWidth, dissolveWidth + dissolveIntensity, 1 - saturate(field));
        float ramp = smoothstep(1 - dissolveColorWidth, 1 - dissolveColorWidth + dissolveRamp, edge);
        value.rgb = lerp(value.rgb, lerp(emissive,emissive2,ramp).rgb * tint.r, edge);
        value.a *= lerp(1,1-edge,dissolveHardness);
      }
      if (mesh) { value.rgb = clamp(value.rgb + max(value.rgb-expThreshold,0)*expIntensity,0,500); value.a = saturate(value.a); }
      float3 encoded = encodedRGB(value.rgb);
      // FX materials retain their authored straight-alpha blend factors;
      // ordinary UI effects use the same premultiplied adapter as images.
      return mesh ? float4(encoded,value.a) : float4(encoded*value.a,value.a*(1-additive));
    }
    float4 sample = sourceTexture.Sample(sourceSampler, p.uv); sample.a = opaque != 0 ? 1 : sample.a;
    if (isText != 0) sample.rgb = 1;
    color *= tint;
    if(useVfxParameters != 0) { color.rgb *= tintIntensity; color.a *= tintAlpha; }
    if(isText!=2) color.a = round(color.a * 255) / 255;
    float4 result = color * (sample + sampleAdd);
    // Encode straight linear RGB before premultiplying, matching D2D and the
    // desktop compositor's encoded-space BGRA alpha contract.
    float3 encoded = encodedRGB(result.rgb);
    result.rgb = encoded * result.a; result.a *= 1 - additive; return result;
}
)";
}
struct SourceDraw::Impl {
    struct Asset { Ptr<ID3D11ShaderResourceView> view; Ptr<ID3D11SamplerState> sampler; unsigned width{},height{}; };
    struct Material {
        Constants constants; std::string mainTexture;
        std::string vfxTexture;
        std::array<std::string, 2> effectTextures;
        std::map<std::string, std::array<float,4>> values;
        std::map<std::string, std::pair<unsigned,unsigned>> types;
        Ptr<ID3D11BlendState> blend;
        bool prepared{};
    };
    struct Text { Asset asset; std::string key; std::uint64_t used{}; };
    Ptr<ID3D11Device> device; Ptr<ID3D11DeviceContext> context; Ptr<ID2D1DeviceContext> painter;
    Ptr<IDWriteFactory> fonts; Ptr<ID3D11VertexShader> vertex; Ptr<ID3D11PixelShader> pixel;
    Ptr<ID3D11InputLayout> layout; Ptr<ID3D11Buffer> vertices, constants;
    Ptr<ID3D11BlendState> blend; Ptr<ID3D11RasterizerState> raster; Ptr<ID3D11DepthStencilState> depth;
    std::map<std::string, JsonObject> descriptors; std::map<std::string, Asset> assets;
    std::map<std::string, Material> materials; std::map<std::string, Text> texts;
    std::filesystem::path root; unsigned capacity{}; std::uint64_t generation{};
    std::string initializationError;
    Constants materialConstants(const Material& material, const scene::Graphic& graphic, double time) const {
        Constants result = material.constants;
        result.sceneTime=static_cast<float>(time);
        const auto firstAnimated=graphic.sampledProperties.lower_bound("material.");
        if(material.prepared && (firstAnimated==graphic.sampledProperties.end() || !firstAnimated->first.starts_with("material."))) return result;
        auto value=[&](const char* key, float* output, unsigned count) {
            std::array<float,4> v{};
            for(unsigned i=0;i<count;++i) v[i]=output[i];
            if(auto it=material.values.find(key);it!=material.values.end()) v=it->second;
            bool animated=false; const auto prefix=std::string("material.")+key;
            if(auto it=graphic.sampledProperties.find(prefix);it!=graphic.sampledProperties.end()) {v[0]=static_cast<float>(it->second); animated=true;}
            const char* axes[]{".r",".g",".b",".a"}; const char* vectorAxes[]{".x",".y",".z",".w"};
            for(unsigned i=0;i<count;++i) for(auto suffix:{axes[i],vectorAxes[i]})
                if(auto it=graphic.sampledProperties.find(prefix+suffix);it!=graphic.sampledProperties.end()) {v[i]=static_cast<float>(it->second); animated=true;}
            const auto type=material.types.find(key);
            // Serialized channels are converted once during initialization;
            // animated material.* values start in the source serialized space.
            if(animated && type!=material.types.end() && (type->second.first==0 || (type->second.second&32))) {
                auto raw=material.values.find(std::string("@raw:")+key);
                if(raw!=material.values.end()) {
                    for(unsigned i=0;i<count;++i) {
                        const auto scalar=graphic.sampledProperties.find(prefix);
                        const auto colorAxis=graphic.sampledProperties.find(prefix+axes[i]);
                        const auto vectorAxis=graphic.sampledProperties.find(prefix+vectorAxes[i]);
                        v[i]=scalar!=graphic.sampledProperties.end()&&i==0?static_cast<float>(scalar->second):colorAxis!=graphic.sampledProperties.end()?static_cast<float>(colorAxis->second):vectorAxis!=graphic.sampledProperties.end()?static_cast<float>(vectorAxis->second):raw->second[i];
                    }
                }
                const auto converted=type->second.first==2 || type->second.first==3 ? 1u : std::min(count,3u);
                for(unsigned i=0;i<converted;++i) v[i]=materialLinear(v[i]);
            }
            for(unsigned i=0;i<count;++i) { if(!std::isfinite(v[i])) throw std::runtime_error("Nonfinite source material channel"); output[i]=v[i]; }
        };
        auto scalar=[&](const char* key,float& output){value(key,&output,1);};
        scalar("_UIImageOpaque",result.opaque); scalar("_UseAdditiveBlendMode",result.additive);
        scalar("_UIVFXParameters",result.useVfxParameters);
        scalar("_TintColorIntensity",result.tintIntensity); scalar("_TintColorAlpha",result.tintAlpha);
        value(result.program>=12?"_TintColor":"_Color",result.tint,4);
        value("_MainTex_ST",result.st,4);
        if(result.program) {
            scalar("_DisableVertColor",result.disableVertColor); scalar("_UseMainTexAsAlpha",result.useMainAsAlpha);
            scalar("_UseMaskTexAsAlpha",result.useMaskAsAlpha);
            scalar("_ExpThreshold",result.expThreshold); scalar("_ExpIntensity",result.expIntensity);
            scalar("_DissolveScheduleOffset",result.dissolveOffset); scalar("_DissolveEdgeHardness",result.dissolveHardness);
            scalar("_DissolveEdgeWidth",result.dissolveWidth); scalar("_DissolveEdgeIntensity",result.dissolveIntensity);
            scalar("_DissolveColorWidth",result.dissolveColorWidth); scalar("_DissolveColorRamp",result.dissolveRamp);
            scalar("_DissolveByDir",result.dissolveByDir);
            value("_MainTexUVSpeed",result.mainSpeed,4); value("_MainTexUVRotateMat",result.mainRotate,4);
            value("_MaskTex_ST",result.maskST,4); value("_MaskTexUVSpeed",result.maskSpeed,4); value("_MaskTexUVRotateMat",result.maskRotate,4);
            value("_DissolveTex_ST",result.dissolveST,4); value("_DissolveUVSpeed",result.dissolveSpeed,4); value("_DissolveUVRotateMat",result.dissolveRotate,4);
            value("_DissolveEmissiveColor",result.emissive,4); value("_DissolveEmissiveColor2",result.emissive2,4);
            value("_DissolveDir",result.dissolveDir,4); value("_DissolvePoint",result.dissolvePoint,4); value("_VFXMainTex_ST",result.vfxST,4);
        }
        return result;
    }
    Asset& desktopImage(const scene::Graphic& graphic) {
        if(!graphic.textureId.starts_with("desktop.") || graphic.texturePath.empty()) throw std::runtime_error("Invalid desktop image binding");
        if(auto found=assets.find(graphic.textureId);found!=assets.end()) return found->second;
        const auto resourceRoot=root.parent_path();
        const auto relative=graphic.texturePath.generic_string();
        auto path=safePath(resourceRoot,relative);
        auto resolved=std::filesystem::weakly_canonical(path), base=std::filesystem::weakly_canonical(resourceRoot);
        auto contained=resolved.lexically_relative(base);
        if(contained.empty() || contained.is_absolute()) throw std::runtime_error("Desktop image is outside staged resources");
        for(const auto& part:contained) if(part==L"..") throw std::runtime_error("Desktop image escapes staged resources");
        if(!relative.starts_with("AppIconSources/") && !relative.starts_with("Watch/")) throw std::runtime_error("Desktop image is not approved shell artwork");
        Ptr<IWICImagingFactory> imaging; checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&imaging)));
        Ptr<IWICBitmapDecoder> decoder; checked(imaging->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));
        Ptr<IWICBitmapFrameDecode> frame; checked(decoder->GetFrame(0,&frame)); UINT width{},height{}; checked(frame->GetSize(&width,&height));
        if(!width || !height || width>4096 || height>4096) throw std::runtime_error("Desktop artwork dimensions exceed bounds");
        Ptr<IWICFormatConverter> converted; checked(imaging->CreateFormatConverter(&converted));
        checked(converted->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
        std::vector<unsigned char> bytes(std::size_t(width)*height*4); checked(converted->CopyPixels(nullptr,width*4,static_cast<UINT>(bytes.size()),bytes.data()));
        if(auto crop=graphic.sampledProperties.find("desktop.iconAlphaCrop");crop!=graphic.sampledProperties.end()) {
            if(!std::isfinite(crop->second) || crop->second<0 || crop->second>255) throw std::runtime_error("Invalid desktop icon alpha threshold");
            // Source desktop icons fit into a 96px image before alpha cropping.
            // WIC resampling is platform-native; matched Mac raster QA remains required.
            const double ratio=std::min(96./width,96./height);
            const unsigned scaledWidth=std::max(1u,static_cast<unsigned>(std::lround(width*ratio)));
            const unsigned scaledHeight=std::max(1u,static_cast<unsigned>(std::lround(height*ratio)));
            Ptr<IWICBitmapScaler> scaler; checked(imaging->CreateBitmapScaler(&scaler));
            checked(scaler->Initialize(converted.Get(),scaledWidth,scaledHeight,WICBitmapInterpolationModeFant));
            std::vector<unsigned char> scaled(std::size_t(scaledWidth)*scaledHeight*4);
            checked(scaler->CopyPixels(nullptr,scaledWidth*4,static_cast<UINT>(scaled.size()),scaled.data()));
            unsigned left=scaledWidth,top=scaledHeight,right{},bottom{}; bool visible=false;
            for(unsigned y=0;y<scaledHeight;++y) for(unsigned x=0;x<scaledWidth;++x)
                if(scaled[(std::size_t(y)*scaledWidth+x)*4+3]>crop->second) {
                    visible=true;left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);
                }
            if(!visible) throw std::runtime_error("Desktop icon has no visible alpha");
            width=right-left+1;height=bottom-top+1;bytes.resize(std::size_t(width)*height*4);
            for(unsigned y=0;y<height;++y) std::memcpy(bytes.data()+std::size_t(y)*width*4,
                scaled.data()+(std::size_t(y+top)*scaledWidth+left)*4,std::size_t(width)*4);
        }
        D3D11_TEXTURE2D_DESC td{}; td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{bytes.data(),width*4,0}; Ptr<ID3D11Texture2D> texture; checked(device->CreateTexture2D(&td,&initial,&texture));
        Asset asset; checked(device->CreateShaderResourceView(texture.Get(),nullptr,&asset.view)); asset.sampler=assets.at("__white").sampler;
        asset.width=width;asset.height=height;
        return assets.emplace(graphic.textureId,std::move(asset)).first->second;
    }
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
        auto option=[&](const char* key,double fallback){auto it=graphic.sampledProperties.find(key);const double value=it==graphic.sampledProperties.end()?fallback:it->second;
            if(!std::isfinite(value)) throw std::runtime_error("Invalid desktop text option");return value;};
        const auto alignment=static_cast<unsigned>(std::clamp(option("desktop.textAlignment",0),0.,2.));
        const auto vertical=static_cast<unsigned>(std::clamp(option("desktop.textVerticalAlignment",0),0.,2.));
        const auto weight=static_cast<unsigned>(std::clamp(option("desktop.fontWeight",400),100.,900.));
        const bool monospace=option("desktop.fontFamily",0)==1,digits=option("desktop.monospacedDigits",0)!=0;
        const bool wrap=option("desktop.textWrap",1)!=0,truncate=option("desktop.textTruncate",0)!=0;
        const bool fit=option("desktop.textFit",0)!=0;
        const double minimum=std::clamp(option("desktop.minimumFontSize",10),1.,256.);
        const double step=std::clamp(option("desktop.fontSizeStep",.5),.5,256.);
        std::string key=graphic.text + ":" + std::to_string(graphic.fontSize) + ":" + std::to_string(graphic.rect.size.x) + ":" + std::to_string(graphic.rect.size.y)+":"+std::to_string(alignment)+":"+std::to_string(vertical)+":"+std::to_string(weight)+":"+std::to_string(wrap)+":"+std::to_string(truncate)+":"+std::to_string(fit)+":"+std::to_string(minimum)+":"+std::to_string(step)+":"+std::to_string(monospace)+":"+std::to_string(digits);
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
        Ptr<IDWriteTextFormat> format; checked(fonts->CreateTextFormat(monospace?L"Consolas":L"Segoe UI",nullptr,static_cast<DWRITE_FONT_WEIGHT>(weight),DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,
            static_cast<float>(std::clamp(graphic.fontSize,1.,256.)),L"en-US",&format));
        checked(format->SetTextAlignment(alignment==1?DWRITE_TEXT_ALIGNMENT_CENTER:alignment==2?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING));
        checked(format->SetParagraphAlignment(vertical==1?DWRITE_PARAGRAPH_ALIGNMENT_CENTER:vertical==2?DWRITE_PARAGRAPH_ALIGNMENT_FAR:DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
        checked(format->SetWordWrapping(wrap?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP));
        Ptr<IDWriteTextLayout> textLayout; checked(fonts->CreateTextLayout(value.c_str(),static_cast<UINT32>(value.size()),format.Get(),static_cast<float>(w),static_cast<float>(h),&textLayout));
        if(digits) {Ptr<IDWriteTypography> typography;checked(fonts->CreateTypography(&typography));
            checked(typography->AddFontFeature(DWRITE_FONT_FEATURE{DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES,1}));
            checked(textLayout->SetTypography(typography.Get(),DWRITE_TEXT_RANGE{0,static_cast<UINT32>(value.size())}));}
        if(fit) {
            double size=std::clamp(graphic.fontSize,1.,256.);const double limit=std::min(size,minimum);
            for(unsigned attempt=0;attempt<512;++attempt) {
                DWRITE_TEXT_METRICS metrics{};checked(textLayout->GetMetrics(&metrics));
                if((metrics.widthIncludingTrailingWhitespace<=std::max(1.,static_cast<double>(w)-2) && metrics.height<=std::max(1.,static_cast<double>(h)-2)) || size<=limit) break;
                size=std::max(limit,size-step);checked(textLayout->SetFontSize(static_cast<float>(size),DWRITE_TEXT_RANGE{0,static_cast<UINT32>(value.size())}));
            }
        }
        Ptr<IDWriteInlineObject> ellipsis;
        if(truncate) {checked(fonts->CreateEllipsisTrimmingSign(format.Get(),&ellipsis)); DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};checked(textLayout->SetTrimming(&trimming,ellipsis.Get()));}
        Ptr<ID2D1SolidColorBrush> white; checked(painter->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&white));
        Ptr<ID2D1Image> previous; painter->GetTarget(&previous); painter->SetTarget(bitmap.Get());
        painter->BeginDraw(); painter->SetTransform(D2D1::Matrix3x2F::Identity()); painter->Clear(D2D1::ColorF(0,0,0,0));
        painter->DrawTextLayout(D2D1::Point2F(0,0),textLayout.Get(),white.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
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
        std::map<std::string,JsonObject> catalog, shaderCatalog;
        for(const auto& item:object(root/L"runtime-materials.json").GetNamedArray(L"materials")) {
            auto record=item.GetObject();
            if(record.HasKey(L"id") && record.GetNamedValue(L"id").ValueType()==winrt::Windows::Data::Json::JsonValueType::String) {
                auto id=string(record,L"id"); if(!id.empty()) catalog.emplace(id,record);
            }
            // Only the canonical string-ID records can serve as shader
            // metadata for this scene. Never round auxiliary numeric IDs.
            auto reference=record.GetNamedObject(L"shader");
            if(reference.GetNamedValue(L"path_id").ValueType()==winrt::Windows::Data::Json::JsonValueType::String)
                shaderCatalog.emplace(string(reference,L"path_id"),record);
        }
        auto sourceMaterials=object(root/L"Scene"/L"materials.json").GetNamedArray(L"materials");
        if(std::filesystem::exists(root/L"Scene"/L"desktop-profile-card.json")) {
            auto card=object(root/L"Scene"/L"desktop-profile-card.json").GetNamedObject(L"materials");
            for(const auto& material:card.GetNamedArray(L"materials")) sourceMaterials.Append(material);
        }
        for (const auto& item:sourceMaterials) {
            auto material=item.GetObject(); Impl::Material native;
            auto data=material.GetNamedObject(L"data"); auto saved=data.GetNamedObject(L"m_SavedProperties");
            auto shaderId=string(data.GetNamedObject(L"m_Shader"),L"m_PathID");
            bool dissolve=false,mask=false,mainFx=false;
            for(const auto& key:data.GetNamedArray(L"m_ValidKeywords")) {
                auto name=key.GetString(); dissolve|=name==L"HG_UI_VFX_DISSOLVE"; mask|=name==L"HG_UI_VFX_MASKTEX"; mainFx|=name==L"HG_UI_VFX_MAINTEX";
            }
            native.constants.program=shaderId=="-7864008769510089003" ? (dissolve?15u:mask?13u:12u) : shaderId=="-5686924490240107124" ? (dissolve?2u:mainFx?1u:0u) : 0u;
            for (const auto& entry:saved.GetNamedArray(L"m_Floats")) {auto values=entry.GetArray(); auto name=values.GetStringAt(0);
                native.values[winrt::to_string(name)]={static_cast<float>(values.GetNumberAt(1)),0,0,0}; }
            for(const auto& entry:saved.GetNamedArray(L"m_Colors")) { auto pair=entry.GetArray(); auto c=pair.GetObjectAt(1);
                native.values[winrt::to_string(pair.GetStringAt(0))]={static_cast<float>(number(c,L"r")),static_cast<float>(number(c,L"g")),static_cast<float>(number(c,L"b")),static_cast<float>(number(c,L"a"))}; }
            for (const auto& entry:saved.GetNamedArray(L"m_TexEnvs")) {auto values=entry.GetArray(); auto name=winrt::to_string(values.GetStringAt(0));
                auto info=values.GetObjectAt(1); auto id=string(info.GetNamedObject(L"m_Texture"),L"target_id");
                if(name=="_MainTex") native.mainTexture=id; else if(name=="_VFXMainTex") native.vfxTexture=id;
                else if(name=="_MaskTex") native.effectTextures[0]=id; else if(name=="_DissolveTex") native.effectTextures[1]=id;
                auto scale=info.GetNamedObject(L"m_Scale"), offset=info.GetNamedObject(L"m_Offset");
                native.values[name+"_ST"]={static_cast<float>(number(scale,L"x",1)),static_cast<float>(number(scale,L"y",1)),static_cast<float>(number(offset,L"x")),static_cast<float>(number(offset,L"y"))}; }
            auto metadata=catalog.find(string(material,L"id"));
            auto shaderMetadata=shaderCatalog.find(shaderId);
            const auto properties=metadata!=catalog.end()?metadata->second:shaderMetadata!=shaderCatalog.end()?shaderMetadata->second:JsonObject{};
            if(native.constants.program && metadata==catalog.end()) throw std::runtime_error("Source FX material metadata is missing");
            for(const auto& property:properties.GetNamedArray(L"shader_properties",JsonArray{})) {
                auto p=property.GetObject(); auto name=string(p,L"name"); auto type=integer(p,L"type",0,5),flags=integer(p,L"flags",0,UINT_MAX);
                native.types[name]={type,flags};
                if(auto value=native.values.find(name);value!=native.values.end()&&(type==0||(flags&32))) {
                    native.values["@raw:"+name]=value->second;
                    const unsigned count=type==2||type==3?1:3;
                    for(unsigned i=0;i<count;++i) value->second[i]=materialLinear(value->second[i]);
                }
            }
            if(native.constants.program>=12) {
                bool found=false;
                for(const auto& pass:metadata->second.GetNamedArray(L"static_pass_states")) {auto p=pass.GetObject(); if(string(p,L"name")!="Default") continue;
                    auto blend=p.GetNamedObject(L"state").GetNamedObject(L"rtBlend0");
                    auto scalar=[&](const wchar_t* key,unsigned max){return integer(blend.GetNamedObject(key),L"value",0,max);};
                    D3D11_BLEND_DESC desc{}; auto& b=desc.RenderTarget[0]; b.BlendEnable=TRUE;
                    b.SrcBlend=sourceBlend(scalar(L"srcBlend",10)); b.DestBlend=sourceBlend(scalar(L"destBlend",10));
                    b.SrcBlendAlpha=sourceBlend(scalar(L"srcBlendAlpha",10)); b.DestBlendAlpha=sourceBlend(scalar(L"destBlendAlpha",10));
                    b.BlendOp=sourceBlendOp(scalar(L"blendOp",4)); b.BlendOpAlpha=sourceBlendOp(scalar(L"blendOpAlpha",4));
                    b.RenderTargetWriteMask=static_cast<UINT8>(scalar(L"colMask",15)); checked(device->CreateBlendState(&desc,&native.blend)); found=true; break;
                }
                if(!found) throw std::runtime_error("Source FX default blend state unavailable");
            }
            native.constants=d.materialConstants(native,{},0); native.prepared=true;
            d.materials.emplace(string(material,L"id"),std::move(native));
        }
        Ptr<ID3DBlob> vs,ps,errors;
        auto compile=[&](const char* entry,const char* profile,Ptr<ID3DBlob>& output) {
            auto status=D3DCompile(shader,sizeof(shader)-1,"native-source-image",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&output,&errors);
            if(FAILED(status) && errors) throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));
            checked(status);
        };
        compile("vertexMain","vs_4_0",vs); compile("pixelMain","ps_4_0",ps);
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
    } catch(const winrt::hresult_error& error) {impl_->initializationError=winrt::to_string(error.message());return error.code();}
    catch(const std::exception& error) {impl_->initializationError=error.what();return E_FAIL;}
    catch(...) {impl_->initializationError="Unknown source graphics initialization failure";return E_FAIL;}
}
HRESULT SourceDraw::draw(ID3D11RenderTargetView* target, const scene::Frame& frame) {
    try {
        auto& d=*impl_; ++d.generation;
        // Rasterize changed text before binding the shared D3D target. The
        // cache is keyed by component and replaced, never appended on reopen.
        for(const auto& graphic:frame.graphics) if((graphic.kind=="UIText" || graphic.kind=="DesktopText") && !graphic.text.empty()) d.text(graphic);
        d.context->OMSetRenderTargets(1,&target,nullptr); const float clear[4]{}; d.context->ClearRenderTargetView(target,clear);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(frame.camera.viewport.x),static_cast<float>(frame.camera.viewport.y),0,1};
        d.context->RSSetViewports(1,&viewport); d.context->RSSetState(d.raster.Get()); d.context->OMSetBlendState(d.blend.Get(),nullptr,UINT_MAX); d.context->OMSetDepthStencilState(d.depth.Get(),0);
        d.context->IASetInputLayout(d.layout.Get()); d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        d.context->VSSetShader(d.vertex.Get(),nullptr,0); d.context->PSSetShader(d.pixel.Get(),nullptr,0);
        auto cb=d.constants.Get(); d.context->VSSetConstantBuffers(0,1,&cb); d.context->PSSetConstantBuffers(0,1,&cb);
        struct Command { Constants constants; std::array<Impl::Asset*,3> assets; ID3D11BlendState* blend; UINT first, count; };
        std::vector<Command> commands; std::vector<Vertex> vertices;
        for(const auto& graphic:frame.graphics) {
            if(graphic.quads.empty() || graphic.color[3]<=0 || ((graphic.kind=="UIText" || graphic.kind=="DesktopText") && graphic.text.empty())) continue;
            if(graphic.quads.size() > (1048576 - vertices.size()) / 6) throw std::runtime_error("Source draw vertex bound exceeded");
            Constants constants{}; std::string id=graphic.textureId; Impl::Material* material=nullptr;
            if(auto found=d.materials.find(graphic.materialId); found!=d.materials.end()) {
                if(graphic.normalMaterial) {
                    if(id.empty()) id=found->second.mainTexture;
                } else {
                    material=&found->second; constants=d.materialConstants(*material,graphic,frame.sceneTime);
                    if(constants.program==1 || constants.program==2) id=material->vfxTexture;
                    else if(id.empty()) id=material->mainTexture;
                }
            }
            const bool isText=(graphic.kind=="UIText" || graphic.kind=="DesktopText") && !graphic.text.empty();
            auto& asset=isText ? d.texts.at(graphic.componentId).asset : !graphic.texturePath.empty()&&graphic.textureId.starts_with("desktop.") ? d.desktopImage(graphic) : d.texture(id.empty()?"__white":id);
            if(isText) {constants=Constants{}; constants.padding=1;}
            if(graphic.kind=="DesktopIcon") {constants=Constants{}; constants.padding=1;}
            if(graphic.kind.starts_with("Desktop")) {constants=Constants{};constants.padding=2;}
            auto& maskAsset=d.texture(material&&!material->effectTextures[0].empty()?material->effectTextures[0]:"__white");
            auto& dissolve=d.texture(material&&!material->effectTextures[1].empty()?material->effectTextures[1]:"__white");
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
                auto p=graphic.quads[q][i];
                if(graphic.kind=="DesktopIcon" && asset.width && asset.height) {
                    const auto& quad=graphic.quads[q];
                    const double w=std::hypot(quad[3].x-quad[0].x,quad[3].y-quad[0].y);
                    const double h=std::hypot(quad[1].x-quad[0].x,quad[1].y-quad[0].y);
                    if(w>0 && h>0) {
                        const double ratio=std::min(w/asset.width,h/asset.height);
                        const double cx=(quad[0].x+quad[2].x)*.5,cy=(quad[0].y+quad[2].y)*.5;
                        p.x=cx+(p.x-cx)*(asset.width*ratio/w);p.y=cy+(p.y-cy)*(asset.height*ratio/h);
                    }
                }
                const auto& m=matrix.values; Vertex v{};
                for(unsigned row=0;row<4;++row) v.position[row]=static_cast<float>(m[row]*p.x+m[4+row]*p.y+m[8+row]*p.z+m[12+row]);
                v.position[2]=0; auto uv=q<graphic.uvQuads.size()?graphic.uvQuads[q][i]:scene::Vec2{};
                // Source raw mip rows and Unity UVs share the same origin.
                if(isText) uv={i<2?0.:1.,i==0||i==3?1.:0.};
                v.uv[0]=static_cast<float>(uv.x); v.uv[1]=static_cast<float>(uv.y);
                for(unsigned c=0;c<3;++c) v.color[c]=graphic.kind.starts_with("Desktop")?linear(graphic.color[c]):q<graphic.colorQuads.size()?static_cast<float>(graphic.colorQuads[q][i][c]*graphic.color[c]):graphic.vertexColorReady ? static_cast<float>(graphic.color[c]) :
                    linear(std::nearbyint(std::clamp(graphic.color[c],0.,1.)*255)/255);
                v.color[3]=static_cast<float>(std::clamp(graphic.color[3],0.,1.)*(q<graphic.colorQuads.size()?graphic.colorQuads[q][i][3]:1)); vertices.push_back(v);
            }
            commands.push_back({constants,{&asset,&maskAsset,&dissolve},!isText&&material&&material->blend?material->blend.Get():d.blend.Get(),first,static_cast<UINT>(vertices.size())-first});
        }
        if(vertices.size()>1048576) throw std::runtime_error("Source draw vertex bound exceeded");
        if(!vertices.empty()) {
            if(vertices.size()>d.capacity) {d.vertices.Reset(); d.capacity=std::max(4096u,static_cast<unsigned>(vertices.size()));
                D3D11_BUFFER_DESC bd{}; bd.ByteWidth=d.capacity*sizeof(Vertex); bd.Usage=D3D11_USAGE_DYNAMIC; bd.BindFlags=D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE; checked(d.device->CreateBuffer(&bd,nullptr,&d.vertices));}
            D3D11_MAPPED_SUBRESOURCE mapped{}; checked(d.context->Map(d.vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)); std::memcpy(mapped.pData,vertices.data(),vertices.size()*sizeof(Vertex)); d.context->Unmap(d.vertices.Get(),0);
            auto buffer=d.vertices.Get(); UINT stride=sizeof(Vertex),offset=0; d.context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
            for(const auto& command:commands) {
                checked(d.context->Map(d.constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)); std::memcpy(mapped.pData,&command.constants,sizeof(Constants)); d.context->Unmap(d.constants.Get(),0);
                ID3D11ShaderResourceView* views[3]; ID3D11SamplerState* samplers[3];
                for(unsigned i=0;i<3;++i) {views[i]=command.assets[i]->view.Get(); samplers[i]=command.assets[i]->sampler.Get();}
                d.context->OMSetBlendState(command.blend,nullptr,UINT_MAX);
                d.context->PSSetShaderResources(0,3,views); d.context->PSSetSamplers(0,3,samplers); d.context->Draw(command.count,command.first);
            }
        }
        ID3D11ShaderResourceView* empty[3]{}; d.context->PSSetShaderResources(0,3,empty);
        for(auto it=d.texts.begin();it!=d.texts.end();) if(it->second.used!=d.generation) it=d.texts.erase(it); else ++it;
        return S_OK;
    } catch(const winrt::hresult_error& error) {return error.code();} catch(...) {return E_FAIL;}
}
std::size_t SourceDraw::textureCount() const {return impl_->assets.size();}
std::size_t SourceDraw::textCount() const {return impl_->texts.size();}
const std::string& SourceDraw::initializationError() const {return impl_->initializationError;}
}
