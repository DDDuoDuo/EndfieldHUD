#include "render/source_draw.h"
#include "render/profile_artwork.hpp"
#include "resources/resource_data.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <dxgi.h>
#include <roapi.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
using ehud::scene::Frame;
using ehud::scene::Graphic;
using ehud::render::SourceOutputContract;
using ehud::render::SourceTargetLoad;
using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonValue;
constexpr const char* imageId = "CAB-311c0d8d29f5c344193835dd71e1e86a:15186476803644734";
constexpr const char* uiFxId = "CAB-0d388c2ed5330cfe18569c196f3e98e9:9134844738325305254";
constexpr const char* uiDissolveId = "CAB-dfb305a42888c1ae74fa50cb4493ac70:-6819959320183326258";
constexpr const char* meshId = "CAB-6934958af701272d8c23ea7d9e7fb440:-7432806595980720090";
int checks{};
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void checked(HRESULT hr, const char* message) {
    if (FAILED(hr)) { std::ostringstream text; text << message << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr); throw std::runtime_error(text.str()); }
}
struct Pixel { unsigned r{}, g{}, b{}, a{}; };
void expect(Pixel actual, Pixel expected, const char* reason, unsigned tolerance = 1) {
    auto close = [=](unsigned x, unsigned y) { return x > y ? x-y <= tolerance : y-x <= tolerance; };
    if (!close(actual.r,expected.r) || !close(actual.g,expected.g) || !close(actual.b,expected.b) || !close(actual.a,expected.a)) {
        std::ostringstream text; text << reason << ": actual RGBA=" << actual.r << ',' << actual.g << ',' << actual.b << ',' << actual.a
            << " expected=" << expected.r << ',' << expected.g << ',' << expected.b << ',' << expected.a;
        throw std::runtime_error(text.str());
    }
    ++checks;
}
// Standard IEC sRGB transfer, independent of SourceDraw's constants/programs.
double decode(double encoded) { return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded+0.055)/1.055,2.4); }
double encode(double linear) { return linear <= 0.0031308 ? linear*12.92 : 1.055*std::pow(linear,1.0/2.4)-0.055; }
unsigned byte(double value) { return static_cast<unsigned>(std::lround(std::clamp(value,0.0,1.0)*255)); }
Pixel premultiplied(std::array<double,3> linear, double alpha) {
    return {byte(encode(linear[0])*alpha),byte(encode(linear[1])*alpha),byte(encode(linear[2])*alpha),byte(alpha)};
}
// Source attachment storage: fragment premultiplication and blending happen
// in linear RGB; hardware applies the sRGB transfer only after accumulation.
Pixel source_pixel(std::array<double,3> accumulated, double alpha) {
    return {byte(encode(accumulated[0])),byte(encode(accumulated[1])),byte(encode(accumulated[2])),byte(alpha)};
}
Pixel source_premultiplied(std::array<double,3> linear, double alpha) {
    for (auto& channel:linear) channel*=alpha;
    return source_pixel(linear,alpha);
}
Graphic rectangle(const char* material, std::array<double,4> color = {1,1,1,1}) {
    Graphic graphic;
    graphic.componentId = "synthetic-gpu-quad"; graphic.nodeId = "synthetic-gpu-node";
    graphic.kind = "UIImage"; graphic.materialId = material; graphic.textureId = "__white";
    graphic.color = color; graphic.vertexColorReady = true; graphic.rect = {{-1,-1},{2,2}};
    graphic.quads.push_back({ehud::scene::Vec3{-1,-1,0},{-1,1,0},{1,1,0},{1,-1,0}});
    graphic.uvQuads.push_back({ehud::scene::Vec2{0.25,0.5},{0.25,0.5},{0.25,0.5},{0.25,0.5}});
    return graphic;
}
Frame frame(Graphic graphic, double time = 0) {
    Frame result; result.camera.viewport = {32,32}; result.sceneTime = time;
    result.graphics.push_back(std::move(graphic)); return result;
}
class Gpu {
public:
    explicit Gpu(SourceOutputContract contract = SourceOutputContract::EncodedPremultiplied)
        : contract_(contract) {
        D3D_FEATURE_LEVEL level{};
        checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"Create isolated WARP device");
        check(level >= D3D_FEATURE_LEVEL_10_0,"WARP supports the source shader feature level");
        Ptr<IDXGIDevice> dxgi; checked(device.As(&dxgi),"Get WARP DXGI device");
        D2D1_FACTORY_OPTIONS options{};
        checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,options,factory.GetAddressOf()),"Create D2D factory");
        checked(factory->CreateDevice(dxgi.Get(),&d2d),"Create WARP D2D device");
        checked(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&painter),"Create WARP D2D context");
        checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(fonts.GetAddressOf())),"Create native font factory");
        D3D11_TEXTURE2D_DESC description{};
        description.Width=description.Height=32; description.MipLevels=description.ArraySize=description.SampleDesc.Count=1;
        description.Format=contract_==SourceOutputContract::SourceLinearPremultiplied
            ?DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM;
        description.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        checked(device->CreateTexture2D(&description,nullptr,&target),"Create offscreen 32x32 target");
        checked(device->CreateRenderTargetView(target.Get(),nullptr,&view),"Create explicit source output target view");
        if(contract_==SourceOutputContract::EncodedPremultiplied) {
            Ptr<IDXGISurface> surface; checked(target.As(&surface),"Get offscreen DXGI surface");
            auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(description.Format,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
            checked(painter->CreateBitmapFromDxgiSurface(surface.Get(),&properties,&bitmap),"Create compatible native D2D bitmap");
            painter->SetTarget(bitmap.Get());
        }
        description.Usage=D3D11_USAGE_STAGING; description.BindFlags=0; description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        checked(device->CreateTexture2D(&description,nullptr,&staging),"Create isolated GPU readback texture");
    }
    ~Gpu() { if (painter) painter->SetTarget(nullptr); if (context) context->ClearState(); }
    std::unique_ptr<ehud::render::SourceDraw> initialize(const std::filesystem::path& root) {
        auto source = std::make_unique<ehud::render::SourceDraw>();
        const HRESULT hr = source->initialize(device.Get(),context.Get(),painter.Get(),fonts.Get(),root);
        if (FAILED(hr)) {
            std::ostringstream text; text << "SourceDraw initialization failed: " << root.string()
                << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr) << " " << source->initializationError();
            throw std::runtime_error(text.str());
        }
        return source;
    }
    Pixel draw(ehud::render::SourceDraw& source, const Frame& scene, unsigned x = 16, unsigned y = 16,
               SourceTargetLoad load = SourceTargetLoad::Clear) {
        checked(source.draw(view.Get(),scene,contract_,load),"Submit isolated source material frame");
        context->OMSetRenderTargets(0,nullptr,nullptr);
        context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read isolated material framebuffer");
        const auto pixel=static_cast<const BYTE*>(mapped.pData)+std::size_t(y)*mapped.RowPitch+x*4;
        const Pixel result{pixel[2],pixel[1],pixel[0],pixel[3]};
        context->Unmap(staging.Get(),0); return result;
    }
    HRESULT submit(ehud::render::SourceDraw& source, const Frame& scene, SourceOutputContract contract) {
        return source.draw(view.Get(),scene,contract);
    }
    HRESULT submit_load(ehud::render::SourceDraw& source, const Frame& scene, SourceTargetLoad load) {
        return source.draw(view.Get(),scene,contract_,load);
    }
private:
    SourceOutputContract contract_;
    Ptr<ID3D11Device> device; Ptr<ID3D11DeviceContext> context;
    Ptr<ID2D1Factory1> factory; Ptr<ID2D1Device> d2d; Ptr<ID2D1DeviceContext> painter;
    Ptr<IDWriteFactory> fonts; Ptr<ID3D11Texture2D> target,staging;
    Ptr<ID3D11RenderTargetView> view; Ptr<ID2D1Bitmap1> bitmap;
};
void actual_source(Gpu& gpu, const std::filesystem::path& root) {
    auto source = gpu.initialize(root);
    Frame empty; empty.camera.viewport={32,32};
    expect(gpu.draw(*source,empty),{},"Actual source empty frame clears the target",0);
    auto quad=rectangle(imageId,{decode(64.0/255),decode(128.0/255),decode(192.0/255),128.0/255});
    expect(gpu.draw(*source,frame(quad)),{32,64,96,128},"Original UI material applies Canvas color once and premultiplies encoded RGB");
    quad=rectangle(uiFxId,{decode(64.0/255),decode(128.0/255),decode(192.0/255),128.0/255});
    quad.normalMaterial=true;
    expect(gpu.draw(*source,frame(quad)),{32,64,96,128},"Desktop normal-alpha override bypasses source FX and its blend");
    quad=rectangle("",{64.0/255,128.0/255,192.0/255,128.0/255});quad.kind="DesktopVector";
    expect(gpu.draw(*source,frame(quad)),{32,64,96,128},"Native desktop artwork uses direct sRGB ink once");
    std::cout << "unchanged_staged_source_initialization=passed actual_UI_color=passed\n";
}
void profile_byte_contracts() {
    namespace p=ehud::render::profile;
    const std::vector<std::uint8_t> bgra{40,180,200,128,7,9,11,255,
                                      255,255,255,0,30,20,10,255};
    const auto pixels=p::cropPremultipliedBGRA(bgra,2,2,{0,0,2,2});
    check(pixels[0]==p::Pixel{100,90,20,128},"Profile source BGRA enters the premultiplied eight-bit domain once");
    check(pixels[1]==p::Pixel{11,9,7,255} && pixels[3]==p::Pixel{10,20,30,255},"Profile cropping retains original bottom-row order and converts BGRA channels");
    check(pixels[2]==p::Pixel{},"Zero-alpha source RGB cannot enter profile chroma");
    const auto crop=p::cropPremultipliedBGRA(bgra,2,2,{1,0,1,2});
    check(crop.size()==2 && crop[0]==pixels[1] && crop[1]==pixels[3],"Profile crop applies source x/y before row extraction");
    check(p::themed(pixels[0],{0,0,1})==p::Pixel{30,20,90,128},"Seventy premultiplied yellow units become blue while neutral channels and alpha survive");
    check(p::themed({31,31,31,200},{0,0,1})==p::Pixel{31,31,31,200},"Profile neutral gray is not accent tinted");
    check(p::themed({1,1,0,255},{.5,.5,.5})==p::Pixel{1,1,1,255},"Swift positive half rounding remains half up");
    check(p::straight({30,20,90,128})==p::Pixel{60,40,179,128},"Profile texture output uses source integer unpremultiplication");
    check(p::straight({19,18,17,0})==p::Pixel{},"Profile output clears hidden RGB at zero alpha");
    const p::Accent gold{250./255,212./255,31./255};
    check(p::hover({255,255,255,255},gold,0)==p::Pixel{95,81,12,97},"Normal-alpha hover sourceIn edge carries the authored 38 percent wash");
    check(p::hover({255,255,255,255},gold,255)==p::Pixel{13,11,2,13},"Hover destinationIn panel retains the authored five percent interior after byte rounding");
    check(p::hover({64,64,64,128},{0,0,1},0)==p::Pixel{0,0,49,49},"Hover edge preserves translucent source silhouette");
    check(p::hover({64,64,64,128},{0,0,1},255)==p::Pixel{0,0,6,6},"Hover panel does not apply the source silhouette twice");
    bool rejected=false;try {p::cropPremultipliedBGRA(bgra,2,2,{1,1,2,1});}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"Profile source rejects crops outside the original mip");
    rejected=false;try {p::validate({std::numeric_limits<double>::quiet_NaN(),0,1});}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"Profile cache rejects nonfinite accent keys");
}
Graphic profile_sample(unsigned mode,unsigned x,unsigned y,std::array<double,3> accent={0,0,1}) {
    auto graphic=rectangle(imageId);graphic.normalMaterial=mode==2;
    // Keep an actual source mesh but make its position-relative UV uniformly
    // sample one source texel; its old texture UV intentionally points elsewhere.
    constexpr double span=1e8;
    graphic.rect={{-(x+.5)/530*span,-(y+.5)/204*span},{span,span}};
    graphic.sampledProperties={{"desktop.profileArtwork",double(mode)},
        {"desktop.profileAccent.r",accent[0]},{"desktop.profileAccent.g",accent[1]},{"desktop.profileAccent.b",accent[2]}};
    return graphic;
}
void actual_profile(Gpu& gpu,const std::filesystem::path& root) {
    const auto mip=root/L"Textures"/L"business_card_topic_normal_1--4a226705---4827637915678035611.bgra-mips.bin";
    const auto original=ehud::resources::read(mip);
    check(original.size()==532*204*4,"Selected original normal_1 decoded mip dimensions remain 532 by 204");
    auto source=gpu.initialize(root);
    expect(gpu.draw(*source,frame(profile_sample(1,432,22))),{36,28,158,255},"Original opaque yellow profile chroma becomes blue with its neutral components intact");
    expect(gpu.draw(*source,frame(profile_sample(1,432,181))),{30,22,150,247},"Original opposite source row preserves translucent premultiplied channels and bottom-origin UV");
    expect(gpu.draw(*source,frame(profile_sample(1,265,102))),{41,41,41,226},"Original neutral profile panel retains its gray and alpha");
    expect(gpu.draw(*source,frame(profile_sample(1,265,10))),{},"Original profile alpha silhouette is preserved",0);
    expect(gpu.draw(*source,frame(profile_sample(2,510,100))),{0,0,97,97},"Original profile edge outside x507 retains the normal-alpha 38 percent plate");
    expect(gpu.draw(*source,frame(profile_sample(2,506,100))),{0,0,13,13},"Original profile inner panel ends at x507 and carries five percent wash");
    expect(gpu.draw(*source,frame(profile_sample(2,265,102))),{0,0,11,11},"Hover panel carries source alpha through both byte-domain operations once");
    auto fading=profile_sample(2,510,100);fading.color[3]=.5;
    expect(gpu.draw(*source,frame(fading)),{0,0,49,49},"Source ColorTint alpha fades the normal-alpha hover plate independently of its cached accent");
    const auto retained=source->textureCount();
    for(unsigned cycle=0;cycle<48;++cycle) {
        auto graphic=profile_sample(2,510,100);graphic.color[3]=(cycle%9)/8.;
        gpu.draw(*source,frame(graphic));
    }
    check(source->textureCount()==retained,"Pointer and finite ColorTint alpha changes reuse immutable profile textures");
    for(unsigned revision=0;revision<12;++revision)
        gpu.draw(*source,frame(profile_sample(1,432,22,{double(revision)/12,.25,.5})));
    check(source->textureCount()<=retained-2+16,"Exact per-accent profile cache stays bounded to eight texture pairs");
    check(ehud::resources::read(mip)==original,"Profile rendering does not rewrite the staged source bytes");
    std::cout<<"source_profile_premultiplied_chroma=passed source_profile_normal_alpha_hover=passed "
             <<"source_profile_rows_UV=passed profile_rounded_boundary_Mac_raster=unverified\n";
}
void write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path,std::ios::binary);
    output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    check(bool(output),"Write isolated material fixture");
}
void scalar(JsonObject saved, const wchar_t* name, double value) {
    auto properties=saved.GetNamedArray(L"m_Floats");
    for (const auto& entry:properties) {
        auto pair=entry.GetArray(); if (pair.GetStringAt(0)!=name) continue;
        pair.SetAt(1,JsonValue::CreateNumberValue(value)); return;
    }
    JsonArray pair; pair.Append(JsonValue::CreateStringValue(name)); pair.Append(JsonValue::CreateNumberValue(value)); properties.Append(pair);
}
void color(JsonObject saved, const wchar_t* name, std::array<double,4> value) {
    auto properties=saved.GetNamedArray(L"m_Colors");
    JsonObject result;
    for (unsigned i=0;i<4;++i) result.SetNamedValue(std::array<const wchar_t*,4>{L"r",L"g",L"b",L"a"}[i],JsonValue::CreateNumberValue(value[i]));
    for (const auto& entry:properties) {
        auto pair=entry.GetArray(); if (pair.GetStringAt(0)!=name) continue;
        pair.SetAt(1,result); return;
    }
    JsonArray pair; pair.Append(JsonValue::CreateStringValue(name)); pair.Append(result); properties.Append(pair);
}
void binding(JsonObject saved, const wchar_t* name, const wchar_t* texture) {
    for (const auto& entry:saved.GetNamedArray(L"m_TexEnvs")) {
        auto pair=entry.GetArray(); if (pair.GetStringAt(0)!=name) continue;
        auto info=pair.GetObjectAt(1);
        info.GetNamedObject(L"m_Texture").SetNamedValue(L"target_id",JsonValue::CreateStringValue(texture));
        auto scale=info.GetNamedObject(L"m_Scale"), offset=info.GetNamedObject(L"m_Offset");
        scale.SetNamedValue(L"x",JsonValue::CreateNumberValue(1)); scale.SetNamedValue(L"y",JsonValue::CreateNumberValue(1));
        offset.SetNamedValue(L"x",JsonValue::CreateNumberValue(0)); offset.SetNamedValue(L"y",JsonValue::CreateNumberValue(0));
        return;
    }
    throw std::runtime_error("Original material texture property is unavailable for its source program");
}
constexpr const wchar_t* whiteTexture=L"fixture-cab:-9223372036854775801";
constexpr const wchar_t* colorTexture=L"fixture-cab:-9223372036854775802";
constexpr const wchar_t* maskTexture=L"fixture-cab:-9223372036854775803";
constexpr const wchar_t* dissolveTexture=L"fixture-cab:-9223372036854775804";
constexpr const wchar_t* timedTexture=L"fixture-cab:-9223372036854775805";
class Fixture {
public:
    // Shader IDs, property metadata and Default blend states come unchanged
    // from the real source materials. Only this temporary root's texture
    // bindings, numeric values and original keyword variant are controlled.
    Fixture(const std::filesystem::path& original,unsigned meshProgram,
            bool coloredTint=false,bool timedMain=false,bool srgbMain=false) {
        root=std::filesystem::temp_directory_path() /
            (L"EndfieldHUD-gpu-material-fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+
             std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence));
        check(std::filesystem::create_directory(root),"Create new isolated GPU fixture root");
        try {
            std::filesystem::create_directory(root/L"Scene");
            write(root/L"runtime-materials.json",ehud::resources::text(original/L"runtime-materials.json"));
            JsonArray textures;
            texture(textures,L"-9223372036854775801",L"white.bin",{255,255,255,255});
            texture(textures,L"-9223372036854775802",L"main.bin",{64,128,192,160},1,srgbMain);
            texture(textures,L"-9223372036854775803",L"mask.bin",{64,128,192,160});
            texture(textures,L"-9223372036854775804",L"dissolve.bin",{191,13,7,17});
            texture(textures,L"-9223372036854775805",L"time.bin",{255,0,0,255,0,255,0,255},2);
            write(root/L"textures.json",winrt::to_string(textures.Stringify()));

            const auto source=JsonObject::Parse(winrt::to_hstring(ehud::resources::text(original/L"Scene"/L"materials.json")));
            JsonArray selected;
            for (const auto& entry:source.GetNamedArray(L"materials")) {
                auto record=JsonObject::Parse(entry.Stringify());
                const auto id=winrt::to_string(record.GetNamedString(L"id"));
                if (id!=imageId && id!=uiFxId && id!=uiDissolveId && id!=meshId) continue;
                auto data=record.GetNamedObject(L"data"), saved=data.GetNamedObject(L"m_SavedProperties");
                scalar(saved,L"_UIImageOpaque",0); scalar(saved,L"_UseAdditiveBlendMode",0);
                scalar(saved,L"_UIVFXParameters",0); scalar(saved,L"_TintColorIntensity",1); scalar(saved,L"_TintColorAlpha",1);
                scalar(saved,L"_DisableVertColor",0); scalar(saved,L"_UseMainTexAsAlpha",0); scalar(saved,L"_UseMaskTexAsAlpha",1);
                scalar(saved,L"_ExpThreshold",1); scalar(saved,L"_ExpIntensity",0);
                scalar(saved,L"_DissolveByDir",0); scalar(saved,L"_DissolveScheduleOffset",-1);
                scalar(saved,L"_DissolveEdgeWidth",1); scalar(saved,L"_DissolveEdgeIntensity",1);
                scalar(saved,L"_DissolveEdgeHardness",0); scalar(saved,L"_DissolveColorWidth",0); scalar(saved,L"_DissolveColorRamp",1);
                color(saved,L"_Color",{1,1,1,1});
                color(saved,L"_TintColor",coloredTint&&id==meshId?std::array<double,4>{.25,.5,.75,1}:std::array<double,4>{1,1,1,1});
                for (const auto* key:{L"_MainTexUVSpeed",L"_MaskTexUVSpeed",L"_DissolveUVSpeed"}) color(saved,key,{0,0,0,0});
                for (const auto* key:{L"_MainTexUVRotateMat",L"_MaskTexUVRotateMat",L"_DissolveUVRotateMat"}) color(saved,key,{1,0,0,1});
                color(saved,L"_DissolveDir",{1,0,0,0}); color(saved,L"_DissolvePoint",{0,0,0,0});
                color(saved,L"_DissolveEmissiveColor",{0,1,0,1}); color(saved,L"_DissolveEmissiveColor2",{1,0,0,1});
                binding(saved,L"_MainTex",whiteTexture);
                if (id==uiFxId || id==uiDissolveId) binding(saved,L"_VFXMainTex",id==uiFxId?(timedMain?timedTexture:colorTexture):whiteTexture);
                binding(saved,L"_MaskTex",maskTexture); binding(saved,L"_DissolveTex",dissolveTexture);
                if (id==meshId) {
                    JsonArray keywords;
                    if (meshProgram>=13) keywords.Append(JsonValue::CreateStringValue(L"HG_UI_VFX_MASKTEX"));
                    if (meshProgram==15) keywords.Append(JsonValue::CreateStringValue(L"HG_UI_VFX_DISSOLVE"));
                    data.SetNamedValue(L"m_ValidKeywords",keywords);
                }
                selected.Append(record);
            }
            check(selected.Size()==4,"All four exact original source material IDs are available");
            JsonObject materials; materials.SetNamedValue(L"materials",selected);
            write(root/L"Scene"/L"materials.json",winrt::to_string(materials.Stringify()));
        } catch (...) { clean(); throw; }
    }
    ~Fixture() { clean(); }
    Fixture(const Fixture&)=delete;
    Fixture& operator=(const Fixture&)=delete;
    std::filesystem::path root;
private:
    void clean() noexcept { std::error_code ignored; std::filesystem::remove_all(root,ignored); }
    void texture(JsonArray array,const wchar_t* id,const wchar_t* file,std::vector<BYTE> bytes,unsigned width=1,bool srgb=false) {
        check(bytes.size()==std::size_t(width)*4,"Synthetic constant texture bytes match their descriptor");
        write(root/file,{reinterpret_cast<const char*>(bytes.data()),bytes.size()});
        JsonObject record;
        record.SetNamedValue(L"path_id",JsonValue::CreateStringValue(id)); record.SetNamedValue(L"cab",JsonValue::CreateStringValue(L"fixture-cab"));
        record.SetNamedValue(L"data_file",JsonValue::CreateStringValue(file));
        for (const auto& [key,value]:std::array<std::pair<const wchar_t*,double>,5>{{{L"width",double(width)},{L"height",1},{L"mip_count",1},{L"texture_format",4},{L"color_space",srgb?0.0:1.0}}})
            record.SetNamedValue(key,JsonValue::CreateNumberValue(value));
        JsonObject sampler;
        for (const auto& [key,value]:std::array<std::pair<const wchar_t*,double>,6>{{{L"m_FilterMode",0},{L"m_WrapU",1},{L"m_WrapV",1},{L"m_WrapW",1},{L"m_MipBias",0},{L"m_Aniso",1}}})
            sampler.SetNamedValue(key,JsonValue::CreateNumberValue(value));
        record.SetNamedValue(L"sampler",sampler); array.Append(record);
    }
    inline static unsigned sequence{};
};
void uniform_mesh_color(Graphic& graphic,std::array<double,4> channels) {
    graphic.colorQuads.push_back({channels,channels,channels,channels});
}
void material_regressions(Gpu& gpu,const std::filesystem::path& root) {
    // Expected cases below are small, closed-form consequences of the actual
    // extracted Metal programs (UI211/272, Mesh12/13/15), not a second copy of
    // the native combined shader or its constant-buffer packing.
    // The final byte expectations also cover the documented Windows encoded
    // premultiplication adapter; WARP results do not claim Mac HDR parity.
    Fixture basic(root,12); auto source=gpu.initialize(basic.root);
    auto ui=rectangle(imageId);
    ui.sampledProperties={{"material._Color.r",.25},{"material._Color.g",.5},{"material._Color.b",.75},{"material._Color.a",.125}};
    expect(gpu.draw(*source,frame(ui)),{8,16,24,32},"Animated UI Color applies source gamma once and alpha remains linear");
    ui=rectangle(imageId);
    ui.sampledProperties={{"material._UIVFXParameters",1},{"material._TintColorIntensity",.25},{"material._TintColorAlpha",.25}};
    expect(gpu.draw(*source,frame(ui)),premultiplied({.25,.25,.25},64.0/255),"UI VFX intensity and alpha affect the original vertex path");
    ui=rectangle(imageId); ui.masks.push_back({{{-1,-1},{1,2}},ehud::scene::Mat4::identity()});
    expect(gpu.draw(*source,frame(ui),8,16),{255,255,255,255},"Projected mask retains the left half of a source graphic",0);
    expect(gpu.draw(*source,frame(ui),24,16),{},"Projected mask discards the right half of a source graphic",0);

    auto fx=rectangle(uiFxId);
    expect(gpu.draw(*source,frame(fx)),premultiplied({64.0/255,128.0/255,192.0/255},160.0/255),
        "UI211 binds original VFX texture separately from the graphic's ordinary UIImage texture");
    fx.sampledProperties["material._UseMainTexAsAlpha"]=1;
    expect(gpu.draw(*source,frame(fx)),{64,64,64,64},"UI211 red-as-alpha selects red coverage and white RGB");
    fx=rectangle(uiDissolveId);
    expect(gpu.draw(*source,frame(fx)),{64,64,64,64},"UI272 samples the mask red channel as coverage");
    fx.sampledProperties["material._DissolveScheduleOffset"]=1;
    expect(gpu.draw(*source,frame(fx)),{},"UI272 dissolve rejects a negative original red-channel field",0);
    fx.sampledProperties={{"material._DissolveScheduleOffset",191.0/255-.25},{"material._DissolveEdgeWidth",.5},
        {"material._DissolveEdgeIntensity",.5},{"material._DissolveEdgeHardness",1}};
    expect(gpu.draw(*source,frame(fx)),premultiplied({.5,1,.5},32.0/255),
        "UI272 midpoint dissolve edge mixes green emissive and halves coverage independently");
    fx.sampledProperties={{"material._DissolveScheduleOffset",1},{"material._DissolveByDir",1},{"material._DissolveEdgeWidth",2}};
    fx.uvQuads[0].fill({.5,.5});
    expect(gpu.draw(*source,frame(fx)),{64,64,64,64},"UI272 original direction mode changes discard to a surviving field");

    auto mesh=rectangle(meshId);
    uniform_mesh_color(mesh,{.25,.5,.75,.5});
    expect(gpu.draw(*source,frame(mesh)),premultiplied({.25,.5,.75},.5),
        "Mesh12 preserves raw source vertex color and authored straight-alpha blend");
    mesh.sampledProperties["material._DisableVertColor"]=1;
    expect(gpu.draw(*source,frame(mesh)),{255,255,255,255},"Mesh12 disables all source vertex channels including alpha",0);
    mesh=rectangle(meshId); uniform_mesh_color(mesh,{.5,.5,.5,1});
    mesh.sampledProperties={{"material._ExpThreshold",.25},{"material._ExpIntensity",1}};
    expect(gpu.draw(*source,frame(mesh)),premultiplied({.75,.75,.75},1),"Mesh12 exposure threshold adds only the authored above-threshold portion");
    mesh=rectangle(meshId,{1,0,0,.25}); auto additive=frame(mesh); additive.graphics.push_back(mesh);
    expect(gpu.draw(*source,additive),{128,0,0,128},"Original Mesh Default pass adds destination RGB and alpha across two layers");
    ui=rectangle(imageId,{1,0,0,.25}); auto over=frame(ui); over.graphics.push_back(ui);
    expect(gpu.draw(*source,over),{112,0,0,112},"Original UI Default pass instead uses source-over for two layers");
    const auto retained=source->textureCount();
    for (int cycle=0;cycle<32;++cycle) gpu.draw(*source,over);
    check(source->textureCount()==retained && retained<=6,"Material revisions do not append texture allocations");

    Fixture mask(root,13); source=gpu.initialize(mask.root);
    mesh=rectangle(meshId);
    expect(gpu.draw(*source,frame(mesh)),{64,64,64,64},"Mesh13 original mask program reads red as alpha");
    mesh.sampledProperties["material._UseMaskTexAsAlpha"]=0;
    expect(gpu.draw(*source,frame(mesh)),premultiplied({64.0/255,128.0/255,192.0/255},160.0/255),
        "Mesh13 RGBA mask mode modulates color and alpha separately");
    mesh.sampledProperties={{"material._UseMaskTexAsAlpha",1},{"material._TintColorAlpha",.5}};
    expect(gpu.draw(*source,frame(mesh)),{32,32,32,32},"Animated Mesh13 tint alpha is not gamma converted");

    Fixture dissolve(root,15); source=gpu.initialize(dissolve.root);
    mesh=rectangle(meshId);
    expect(gpu.draw(*source,frame(mesh)),{64,64,64,64},"Mesh15 authored mask survives an inactive dissolve edge");
    mesh.sampledProperties["material._DissolveScheduleOffset"]=1;
    expect(gpu.draw(*source,frame(mesh)),{},"Mesh15 original dissolve program discards negative fields",0);
    mesh.sampledProperties={{"material._DissolveScheduleOffset",191.0/255-.25},{"material._DissolveEdgeWidth",.5},
        {"material._DissolveEdgeIntensity",.5},{"material._DissolveEdgeHardness",1}};
    expect(gpu.draw(*source,frame(mesh)),premultiplied({.5,1,.5},32.0/255),
        "Mesh15 emissive midpoint and edge hardness retain authored straight-alpha output");
    mesh.sampledProperties["material._DissolveEdgeHardness"]=0;
    expect(gpu.draw(*source,frame(mesh)),premultiplied({.5,1,.5},64.0/255),"Mesh15 soft edge color survives when hardness disables coverage fading");

    Fixture gamma(root,12,true); source=gpu.initialize(gamma.root);
    mesh=rectangle(meshId);
    expect(gpu.draw(*source,frame(mesh)),{64,128,191,255},"Saved Mesh color follows actual shader Color metadata once");
    mesh.sampledProperties={{"material._TintColor.r",.5},{"material._TintColor.a",.5}};
    expect(gpu.draw(*source,frame(mesh)),{64,64,96,128},"Partial animated color preserves untouched saved channels in serialized space");
    mesh.sampledProperties.clear();
    expect(gpu.draw(*source,frame(mesh)),{64,128,191,255},"Animated overrides do not mutate cached saved material colors");

    Fixture timed(root,12,false,true); source=gpu.initialize(timed.root);
    fx=rectangle(uiFxId); fx.sampledProperties["material._MainTexUVSpeed.x"]=.25;
    expect(gpu.draw(*source,frame(fx,0)),{255,0,0,255},"Original UI211 starts on the authored first texel",0);
    expect(gpu.draw(*source,frame(fx,2)),{0,255,0,255},"Original UI211 UV speed uses the shared scene clock",0);
    expect(gpu.draw(*source,frame(fx,1026)),{0,255,0,255},"Original shader time repeats its exact 1024 second period",0);
    fx.sampledProperties={{"material._MainTexUVRotateMat.x",0},{"material._MainTexUVRotateMat.y",1},
        {"material._MainTexUVRotateMat.z",-1},{"material._MainTexUVRotateMat.w",0}};
    fx.uvQuads[0].fill({.25,.25});
    expect(gpu.draw(*source,frame(fx)),{0,255,0,255},"Original Metal float2x2 column order rotates UV about its authored center",0);

    Fixture srgb(root,12,false,false,true); source=gpu.initialize(srgb.root);
    fx=rectangle(uiFxId);
    expect(gpu.draw(*source,frame(fx)),{40,80,120,160},"sRGB source texture decodes before shading and encodes exactly once before premultiplication");
    std::cout << "source_programs=UI211,UI272,Mesh12,Mesh13,Mesh15 analytic_RGBA_cases=passed "
        << "fixture_source_ids=original original_assets_modified=false\n";
}
void source_linear_accumulation(const std::filesystem::path& root) {
    // Independent closed-form cases from source UI211/272's linear RGB times
    // alpha, Mesh12/13/15's straight linear output/authored blend, and the Mac
    // renderer's bgra8Unorm_srgb attachment. This stage is not DComp output.
    Gpu gpu(SourceOutputContract::SourceLinearPremultiplied);
    Fixture basic(root,12);auto source=gpu.initialize(basic.root);
    Frame empty;empty.camera.viewport={32,32};
    check(gpu.submit(*source,empty,SourceOutputContract::EncodedPremultiplied)==E_INVALIDARG,
        "Encoded premultiplied fragments cannot enter the source sRGB attachment");
    check(gpu.submit(*source,empty,static_cast<SourceOutputContract>(42))==E_INVALIDARG,
        "Unknown fragment output contracts are rejected before submission");
    expect(gpu.draw(*source,empty),{},"Source sRGB attachment clears all channels",0);
    auto white=rectangle(imageId,{1,1,1,128.0/255});
    expect(gpu.draw(*source,frame(white)),{188,188,188,128},
        "Source UI half-alpha white encodes linear-premultiplied RGB in the attachment");
    auto gray=rectangle(imageId,{decode(128.0/255),decode(128.0/255),decode(128.0/255),128.0/255});
    expect(gpu.draw(*source,frame(gray)),source_premultiplied({decode(128.0/255),decode(128.0/255),decode(128.0/255)},128.0/255),
        "Source translucent gray premultiplies after decoding and before attachment encoding");
    auto normal=gray;normal.materialId=uiFxId;normal.normalMaterial=true;
    expect(gpu.draw(*source,frame(normal)),gpu.draw(*source,frame(gray)),
        "Desktop normal-alpha source artwork retains the source linear accumulation contract",0);
    auto native=rectangle("",{128.0/255,128.0/255,128.0/255,128.0/255});native.kind="DesktopVector";
    expect(gpu.draw(*source,frame(native)),gpu.draw(*source,frame(gray)),
        "Native overlay ink enters the source linear stage after one sRGB decode",0);

    auto fx=rectangle(uiFxId);
    expect(gpu.draw(*source,frame(fx)),source_premultiplied({64.0/255,128.0/255,192.0/255},160.0/255),
        "UI211 emits linear sampled VFX RGB before alpha and attachment transfer");
    fx.sampledProperties["material._UseMainTexAsAlpha"]=1;
    expect(gpu.draw(*source,frame(fx)),source_premultiplied({1,1,1},64.0/255),
        "UI211 red-as-alpha coverage does not become an encoded-space RGB multiplier");
    fx=rectangle(uiDissolveId);
    expect(gpu.draw(*source,frame(fx)),source_premultiplied({1,1,1},64.0/255),
        "UI272 red mask coverage remains linear-premultiplied");
    fx.sampledProperties={{"material._DissolveScheduleOffset",191.0/255-.25},{"material._DissolveEdgeWidth",.5},
        {"material._DissolveEdgeIntensity",.5},{"material._DissolveEdgeHardness",1}};
    expect(gpu.draw(*source,frame(fx)),source_premultiplied({.5,1,.5},32.0/255),
        "UI272 emissive and edge coverage combine before source attachment encoding");

    auto mesh=rectangle(meshId);uniform_mesh_color(mesh,{.25,.5,.75,.5});
    expect(gpu.draw(*source,frame(mesh)),source_premultiplied({.25,.5,.75},.5),
        "Mesh12 authored straight-alpha blend premultiplies linear raw vertex RGB");
    mesh=rectangle(meshId,{1,0,0,.25});auto added=frame(mesh);added.graphics.push_back(mesh);
    expect(gpu.draw(*source,added),source_pixel({.5,0,0},.5),
        "Mesh12 source additive overlap accumulates linear contributions before transfer");
    auto first=rectangle(imageId,{1,0,0,64.0/255});
    auto second=rectangle(imageId,{0,0,1,128.0/255});auto over=frame(first);over.graphics.push_back(second);
    const double low=64.0/255,half=128.0/255;
    expect(gpu.draw(*source,over),source_pixel({low*(1-half),0,half},half+low*(1-half)),
        "Source red then blue uses linear source-over RGB and untransferred alpha",2);
    over=frame(second);over.graphics.push_back(first);
    expect(gpu.draw(*source,over),source_pixel({low,0,half*(1-low)},low+half*(1-low)),
        "Reversing source-over layer order changes the linear accumulation",2);
    over=frame(gray);second=rectangle(imageId,{1,1,1,low});over.graphics.push_back(second);
    const double mixed=low+decode(128.0/255)*half*(1-low);
    expect(gpu.draw(*source,over),source_pixel({mixed,mixed,mixed},low+half*(1-low)),
        "Source translucent gray and white blend in linear RGB rather than encoded bytes",2);
    first=rectangle(imageId,{1,0,0,low});first.sampledProperties["material._UseAdditiveBlendMode"]=1;
    added=frame(first);added.graphics.push_back(first);
    expect(gpu.draw(*source,added),source_pixel({2*low,0,0},0),
        "Original additive UI keeps source color when output alpha is zero");
    added.graphics.push_back(gray);
    expect(gpu.draw(*source,added),source_pixel({2*low*(1-half)+decode(128.0/255)*half,
        decode(128.0/255)*half,decode(128.0/255)*half},half),
        "A subsequent source-over layer attenuates additive destination RGB in linear space",2);

    Fixture mask(root,13);source=gpu.initialize(mask.root);mesh=rectangle(meshId);
    expect(gpu.draw(*source,frame(mesh)),source_premultiplied({1,1,1},64.0/255),
        "Mesh13 source mask and authored blend retain linear coverage");
    mesh.sampledProperties["material._UseMaskTexAsAlpha"]=0;
    expect(gpu.draw(*source,frame(mesh)),source_premultiplied({64.0/255,128.0/255,192.0/255},160.0/255),
        "Mesh13 RGBA modulation uses linear mask RGB in the source target");
    Fixture dissolve(root,15);source=gpu.initialize(dissolve.root);mesh=rectangle(meshId);
    mesh.sampledProperties={{"material._DissolveScheduleOffset",191.0/255-.25},{"material._DissolveEdgeWidth",.5},
        {"material._DissolveEdgeIntensity",.5},{"material._DissolveEdgeHardness",1}};
    expect(gpu.draw(*source,frame(mesh)),source_premultiplied({.5,1,.5},32.0/255),
        "Mesh15 linear emissive output precedes its authored straight-alpha blend");
    mesh.sampledProperties["material._DissolveScheduleOffset"]=1;
    expect(gpu.draw(*source,frame(mesh)),{},"Source stage preserves Mesh15 discard",0);
    Fixture srgb(root,12,false,false,true);source=gpu.initialize(srgb.root);fx=rectangle(uiFxId);
    expect(gpu.draw(*source,frame(fx)),source_premultiplied({decode(64.0/255),decode(128.0/255),decode(192.0/255)},160.0/255),
        "sRGB VFX textures decode once before original linear alpha multiplication");
    source=gpu.initialize(root);
    expect(gpu.draw(*source,frame(white)),{188,188,188,128},
        "Unchanged original UI material also produces the source half-alpha attachment bytes");
    expect(gpu.draw(*source,frame(profile_sample(2,510,100))),source_premultiplied({0,0,1},97.0/255),
        "Canonical generated hover edge keeps straight sRGB accent and source linear coverage");
    expect(gpu.draw(*source,frame(profile_sample(2,506,100))),source_premultiplied({0,0,1},13.0/255),
        "Canonical hover interior transfers its five-percent alpha only after linear multiplication");
    auto fading=profile_sample(2,510,100);fading.color[3]=.5;
    expect(gpu.draw(*source,frame(fading)),source_premultiplied({0,0,1},(97.0/255)*(128.0/255)),
        "Original finite ColorTint multiplies source hover alpha independently of texture transfer");

    // Existing default fixtures must not silently switch output contracts.
    Gpu encoded;source=encoded.initialize(basic.root);
    check(encoded.submit(*source,empty,SourceOutputContract::SourceLinearPremultiplied)==E_INVALIDARG,
        "Source linear fragments cannot bypass the required sRGB attachment");
    expect(encoded.draw(*source,frame(white)),{128,128,128,128},
        "Standalone encoded-premultiplied compatibility remains explicit");
    std::cout<<"source_linear_srgb_accumulation=passed half_alpha_white_RGB=188 "
        <<"layered_source_over_additive=passed desktop_final_adapter=separate_contract\n";
}
void explicit_background_load(const std::filesystem::path& root) {
    Gpu gpu(SourceOutputContract::SourceLinearPremultiplied);
    auto source=gpu.initialize(root);
    auto background=rectangle(imageId,{.2,.4,.6,1});
    expect(gpu.draw(*source,frame(background)),source_pixel({.2,.4,.6},1),
        "An explicit frozen desktop layer can populate the source attachment");
    auto foreground=rectangle(imageId,{1,0,0,.25});
    const auto mixed=source_pixel({.25+.2*.75,.4*.75,.6*.75},1);
    expect(gpu.draw(*source,frame(foreground),16,16,SourceTargetLoad::PreserveBackground),mixed,
        "Source UI retains and blends over the pre-opening layer in linear space",2);
    Frame empty;empty.camera.viewport={32,32};
    expect(gpu.draw(*source,empty,16,16,SourceTargetLoad::PreserveBackground),mixed,
        "An empty preserved source frame does not erase its desktop input",2);
    check(gpu.submit_load(*source,empty,static_cast<SourceTargetLoad>(999))==E_INVALIDARG,
        "An unknown target load action is rejected before drawing");
    expect(gpu.draw(*source,empty),{},"The default isolated source path still clears",0);
}
}
int wmain(int argc, wchar_t** argv) {
    const HRESULT initialized=RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(initialized)) return 1;
    int result{};
    try {
        if (argc != 2) throw std::runtime_error("usage: source_draw_tests <staged Resources/WatchSource directory>");
        profile_byte_contracts();Gpu gpu;actual_source(gpu,argv[1]);actual_profile(gpu,argv[1]);material_regressions(gpu,argv[1]);
        source_linear_accumulation(argv[1]);
        explicit_background_load(argv[1]);
        std::cout << "WARP_source_material_checks=" << checks << " desktop_capture=false hardware_parity=unverified\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result=1; }
    catch (const winrt::hresult_error& error) { std::cerr << "Native GPU fixture failed HRESULT=0x" << std::hex << error.code().value << '\n'; result=1; }
    catch (...) { std::cerr << "Native GPU fixture exception\n"; result=1; }
    RoUninitialize(); return result;
}
