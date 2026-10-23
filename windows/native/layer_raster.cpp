#include "native/layer_raster.hpp"
#include "native/notes_rich_style.hpp"
#include "core/notes_font_names.hpp"
#include <functional>
#include "native/rich_text_paint.hpp"
#include "native/layer_image_source.hpp"
#include "native/layer_text_layout.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_2.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
using Json = ehud::data::Json;
using Matrix = D2D1::Matrix3x2F;
// `standard`: the standard faces (HarmonyOS Sans with the region's Noto
// cascade, Sources/HUDDesignFonts.swift): Mac weight buckets and the Latin
// face's own hhea line metrics, as CoreText measures them.
struct ResolvedFont {std::wstring family;IDWriteFontCollection*collection{};bool standard{};};
// CLSID_D2D1Shadow (d2d1effects.h declares it for dxguid.lib, not linked here).
constexpr CLSID shadowEffect{0xC67EA361,0x1863,0x4E69,{0x89,0xDB,0x69,0x5D,0x3E,0x9A,0x5B,0x6B}};
// Core Animation's layer shadow blur, measured from the unchanged Mac
// renderer for the charge indicator (native/charge_indicator_scene.cpp): a
// Gaussian whose standard deviation is about 0.9 x shadowRadius.
constexpr double shadowSigmaPerRadius=.9;
void checked(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(std::string(operation) + " HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));
}
[[noreturn]] void invalid(const char* message) { throw std::invalid_argument(message); }
double number(const Json& value, double fallback = 0) {
    if (value.isNull()) return fallback;
    if (!value.isNumber()) invalid("Layer number has the wrong type");
    const auto n = value.number();
    if (!std::isfinite(n) || std::abs(n) > 1e8) invalid("Layer number is nonfinite or outside its bounds");
    return n;
}
float real(const Json& value, float fallback = 0) { return static_cast<float>(number(value, fallback)); }
bool flag(const Json& value, bool fallback = false) {
    if (value.isNull()) return fallback;
    if (!value.isBool()) invalid("Layer boolean has the wrong type");
    return value.boolean();
}
std::string string(const Json& value, std::string fallback = {}) {
    if (value.isNull()) return fallback;
    if (!value.isString()) invalid("Layer string has the wrong type");
    auto text = value.string(); if (text.size() > 262144) invalid("Layer string exceeds its limit"); return text;
}
std::wstring wide(std::string_view value) {
    if (value.size() > 262144 || !Json::validUtf8(value)) invalid("Layer text is invalid UTF-8");
    if (value.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) invalid("Cannot decode layer text");
    std::wstring result(size, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size) != size)
        invalid("Cannot decode layer text");
    return result;
}
const Json::Array& array(const Json& value, std::size_t maximum) {
    if (!value.isArray() || value.array().size() > maximum) invalid("Layer array exceeds its bounds");
    return value.array();
}
core::Rect rectangle(const Json& value) {
    const auto& a = array(value, 4); if (a.size() != 4) invalid("Layer rect requires four numbers");
    core::Rect r{number(a[0]), number(a[1]), number(a[2]), number(a[3])};
    if (r.width < 0 || r.height < 0) invalid("Visible layer bounds cannot be negative");
    return r;
}
D2D1_POINT_2F point(const Json& value, D2D1_POINT_2F fallback = {}) {
    if (value.isNull()) return fallback;
    const auto& a = array(value, 2); if (a.size() != 2) invalid("Layer point requires two numbers");
    return {real(a[0]), real(a[1])};
}
D2D1_RECT_F rect(const core::Rect& r) {
    return D2D1::RectF(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.x + r.width), static_cast<float>(r.y + r.height));
}
core::Rect unite(core::Rect a, core::Rect b) {
    const double x = std::min(a.x,b.x), y = std::min(a.y,b.y);
    return {x,y,std::max(a.x+a.width,b.x+b.width)-x,std::max(a.y+a.height,b.y+b.height)-y};
}
core::Rect transformBounds(core::Rect r, const Matrix& m) {
    const std::array<D2D1_POINT_2F,4> p{{m.TransformPoint({static_cast<float>(r.x),static_cast<float>(r.y)}),
        m.TransformPoint({static_cast<float>(r.x+r.width),static_cast<float>(r.y)}),
        m.TransformPoint({static_cast<float>(r.x+r.width),static_cast<float>(r.y+r.height)}),
        m.TransformPoint({static_cast<float>(r.x),static_cast<float>(r.y+r.height)})}};
    float left=p[0].x,right=left,top=p[0].y,bottom=top;
    for(const auto& v:p){left=std::min(left,v.x);right=std::max(right,v.x);top=std::min(top,v.y);bottom=std::max(bottom,v.y);}
    return {left,top,right-left,bottom-top};
}
bool affine(const Json& value, Matrix& result) {
    if (value.isNull()) { result=Matrix::Identity(); return true; }
    const auto& columns=array(value,4);if(columns.size()!=4)invalid("Layer transform requires four columns");
    double m[4][4]{};
    for(unsigned c=0;c<4;++c){const auto& values=array(columns[c],4);if(values.size()!=4)invalid("Layer matrix column is incomplete");for(unsigned r=0;r<4;++r)m[c][r]=number(values[r]);}
    constexpr double epsilon=1e-8;
    if(std::abs(m[0][2])+std::abs(m[0][3])+std::abs(m[1][2])+std::abs(m[1][3])+std::abs(m[2][0])+std::abs(m[2][1])+std::abs(m[2][3])+std::abs(m[3][2])>epsilon
       ||std::abs(m[2][2]-1)>epsilon||std::abs(m[3][3]-1)>epsilon)return false;
    result=Matrix(static_cast<float>(m[0][0]),static_cast<float>(m[0][1]),static_cast<float>(m[1][0]),static_cast<float>(m[1][1]),static_cast<float>(m[3][0]),static_cast<float>(m[3][1]));
    return true;
}
bool placement(const Json& node, Matrix& result) {
    Matrix transform;if(!affine(node["transform"],transform))return false;
    const auto bounds=rectangle(node["bounds"]);
    const auto position=point(node["position"]),anchor=point(node["anchorPoint"],{.5f,.5f});
    result=Matrix::Translation(static_cast<float>(-bounds.x-anchor.x*bounds.width),static_cast<float>(-bounds.y-anchor.y*bounds.height))
        *transform*Matrix::Translation(position.x,position.y);
    return true;
}
bool childTransform(const Json& node,Matrix& result){
    if(!affine(node["sublayerTransform"],result))return false;
    const auto bounds=rectangle(node["bounds"]);const auto anchor=point(node["anchorPoint"],{.5f,.5f});
    const float x=static_cast<float>(bounds.x+anchor.x*bounds.width),y=static_cast<float>(bounds.y+anchor.y*bounds.height);
    // Core Animation applies sublayerTransform about the parent's anchor.
    result=Matrix::Translation(-x,-y)*result*Matrix::Translation(x,y);return true;
}
bool color(const Json& value,D2D1_COLOR_F& result,float opacity=1) {
    if(value.isNull())return false;
    const auto& c=array(value["sRGB"],4);if(c.size()!=4)invalid("A layer color needs explicit converted sRGB RGBA");
    result=D2D1::ColorF(std::clamp(real(c[0]),0.f,1.f),std::clamp(real(c[1]),0.f,1.f),
        std::clamp(real(c[2]),0.f,1.f),std::clamp(real(c[3]),0.f,1.f)*opacity);
    return true; // Transparent run colors must override their opaque default.
}
std::vector<const Json*> children(const Json& node) {
    std::vector<const Json*> result;
    if(node["children"].isNull())return result;
    for(const auto& child:array(node["children"],LayerRasterizer::maximumNodes))result.push_back(&child);
    std::stable_sort(result.begin(),result.end(),[](auto a,auto b){return number((*a)["zPosition"])<number((*b)["zPosition"]);});
    return result;
}
}

namespace {
// A HUDTextLayer leaf (Sources/HUDDesignText.swift): `lineHeight` makes the
// text descriptor a CSS line box (see LayerRasterizer in the header).
bool lineBox(const Json&descriptor){return descriptor.isObject()&&!descriptor["lineHeight"].isNull();}
// HUDTextLayer.padding: the layer frame around its line box, ceil(size x .25)
// wide and ceil(size x .45) tall for the largest run, so glyph overhangs
// (accents, CJK descenders) are never clipped. Unscaled sizes, as on the Mac.
D2D1_SIZE_F lineBoxPadding(const Json&descriptor){
    double size=number(descriptor["fontSize"],12);
    if(descriptor["runs"].isArray())for(const auto&run:descriptor["runs"].array()){const auto&a=run["attributes"];if(!a.isObject())continue;
        if(a["fontSize"].isNumber())size=std::max(size,a["fontSize"].number());if(a["NSFont"].isObject()&&a["NSFont"]["pointSize"].isNumber())size=std::max(size,a["NSFont"]["pointSize"].number());}
    return {static_cast<float>(std::ceil(size*.25)),static_cast<float>(std::ceil(size*.45))};
}
// Synthetic italic anywhere in a descriptor (the font's or a run's trait).
bool italicText(const Json&descriptor){
    const auto italic=[](const Json&font){return font.isObject()&&font["symbolicTraits"].isNumber()&&(static_cast<unsigned>(font["symbolicTraits"].number())&1);};
    if(italic(descriptor["font"]))return true;
    if(descriptor["runs"].isArray())for(const auto&run:descriptor["runs"].array())if(run["attributes"].isObject()&&italic(run["attributes"]["NSFont"]))return true;
    return false;}
bool containsText(const Json&node){if(node["text"].isObject())return true;if(node["children"].isArray())for(const auto&child:node["children"].array())if(containsText(child))return true;return false;}
std::size_t retainedJsonBytes(const Json&v,unsigned depth=0){if(depth>128)invalid("Typography descriptor nesting exceeds limit");std::size_t result=sizeof(Json);auto add=[&](std::size_t n){if(n>LayerRasterizer::maximumTypographyDescriptorBytes-result)invalid("Typography descriptor exceeds limit");result+=n;};if(v.isString())add(v.string().capacity()+1);else if(v.isArray()){add(v.array().capacity()*sizeof(Json));for(const auto&a:v.array())add(retainedJsonBytes(a,depth+1));}else if(v.isObject())for(const auto&[k,a]:v.object()){add(sizeof(Json::Object::value_type)+4*sizeof(void*)+k.capacity()+1);add(retainedJsonBytes(a,depth+1));}return result;}
// The same estimate (generous: array elements count twice), given up (false)
// as soon as it exceeds `limit`: a local layer description is kept to repaint
// released pixels only while it is smaller than them.
bool jsonBytesWithin(const Json&v,std::size_t limit,std::size_t&total,unsigned depth=0){
    const auto add=[&](std::size_t n){if(n>limit-total)return false;total+=n;return true;};
    if(depth>128||!add(sizeof(Json)))return false;
    if(v.isString())return add(v.string().capacity()+1);
    if(v.isArray()){if(!add(v.array().capacity()*sizeof(Json)))return false;for(const auto&a:v.array())if(!jsonBytesWithin(a,limit,total,depth+1))return false;return true;}
    if(v.isObject())for(const auto&[k,a]:v.object())if(!add(sizeof(Json::Object::value_type)+4*sizeof(void*)+k.capacity()+1)||!jsonBytesWithin(a,limit,total,depth+1))return false;
    return true;
}
}

// What painted a retained image: its local layer description, options and
// default typography. Text entries keep it to repaint typography; a
// releasable image keeps it to paint its released pixels again.
struct detail::LayerRasterRecipe {
    std::shared_ptr<const Json> layer;LayerRasterOptions options;LayerFontLanguage language{};std::size_t bytes{};
    ComPtr<IDWriteFontFallback> fallback; // the cascade of `language` it was painted with
};

struct LayerRasterizer::Impl {
    using Recipe = detail::LayerRasterRecipe;
    struct Entry { std::uint64_t revision;LayerRasterOptions options;std::shared_ptr<LayerRasterImage> image;std::vector<ComPtr<IDWriteTextLayout>> layouts;std::shared_ptr<const PaintedTextLayout> paintedText;std::uint64_t fontRevision{};bool hasText{};std::shared_ptr<const Recipe> source;std::size_t descriptorBytes{};std::string owner;bool releasable{}; };
    struct DecodedImage { unsigned width{},height{};std::vector<std::uint8_t> premultipliedBGRA; };
    struct ImagePixels {
        unsigned width{},height{};std::span<const std::uint8_t> premultipliedBGRA;
        std::shared_ptr<const LayerMemoryImage> borrowed;
    };
    DWORD thread=GetCurrentThreadId();
    ComPtr<ID2D1Factory> d2d;ComPtr<IDWriteFactory> text;ComPtr<IDWriteFontCollection> fonts,bundledFonts;ComPtr<IWICImagingFactory> wic;
    std::optional<std::vector<std::string>> installedFontNames;
    BCRYPT_ALG_HANDLE sha{};
    std::map<std::string,Entry,std::less<>> entries;
    std::map<std::string,DecodedImage,std::less<>> images;
    LayerRasterStats counts;LayerFontLanguage language{LayerFontLanguage::simplifiedChinese};std::uint64_t fontRevision{1};
    bool memoryImageUsed{}; // the current paint read a borrowed provider's image
    bool lineBoxPainted{}; // the current paint drew a line-box leaf (no editor geometry)
    // The families of Fonts/manifest.json that ship on Windows and whose files
    // are present, in manifest order (HUDFontRegistry.families): the Latin
    // family (HarmonyOS Sans) and one Noto Sans family per CJK region.
    struct BundledFamily {std::string name;bool latin{};std::optional<LayerFontLanguage> region;};
    std::vector<BundledFamily> bundled;std::vector<std::string> bundledNames;std::string latinFamily;
    std::array<std::string,4> regionFamilies;std::array<std::wstring,4> tags;
    // The standard faces' cascade for the current UI region (rebuilt once per
    // font revision) and the shared tnum typography: immutable once built, so
    // retained layouts and worker snapshots keep them.
    ComPtr<IDWriteFontFallback> fallback;ComPtr<IDWriteTypography> tabularFigures,plainFigures;
    std::map<UINT32,std::array<double,3>> struts; // standard face: weight -> hhea ascent, descent, line gap (em)
    ObliqueShear oblique; // the shared families slant by the Mac's 0.2 shear
    static std::size_t index(LayerFontLanguage l)noexcept{return static_cast<std::size_t>(l);}
    // HUDFontResolver: without the Latin family (a damaged install) the
    // system UI face stands in, still with the region's Noto cascade.
    const std::string&standardFamily()const noexcept{static const std::string system="Segoe UI";return latinFamily.empty()?system:latinFamily;}
    const char*defaultFamily()const noexcept{return standardFamily().c_str();}
    const wchar_t*locale(LayerFontLanguage l)const noexcept{return tags[index(l)].c_str();}
    const wchar_t*locale()const noexcept{return locale(language);}
    // HUDFontResolver.cascadeFamilies: the region's Noto family, or the
    // Windows system family when its file is absent (HUDTextLanguage.systemFamily).
    std::string regionFamily(LayerFontLanguage l)const{const auto&f=regionFamilies[index(l)];return f.empty()?std::string(layerSystemFontFamily(l)):f;}
    bool isBundled(std::string_view family)const{return std::find(bundledNames.begin(),bundledNames.end(),family)!=bundledNames.end();}
    IDWriteFontCollection*collectionOf(std::string_view family)const{return isBundled(family)?bundledFonts.Get():fonts.Get();}
    // LayerRasterOptions::fallbackFontFamily: empty is the standard faces.
    std::string fallbackFamily(const LayerRasterOptions&options)const{return options.fallbackFontFamily.empty()?standardFamily():options.fallbackFontFamily;}
    // Mac PostScript and family names of the shared fonts, which the Mac
    // registers for its process so Notes, Archive and Reader can store them
    // (HarmonyOS_Sans, HarmonyOS_Sans_Bold, NotoSansJP-Regular, variable
    // instance names such as NotoSansJP-Thin_Bold): compared without case,
    // spaces and underscores up to the first '-'. The Latin family's names
    // have no '-' and end in their weight, so its key is a prefix.
    std::optional<std::string> bundledFace(std::string_view name)const{
        const auto key=[](std::string_view v){std::string k;for(const char c:v.substr(0,v.find('-')))if(c!=' '&&c!='_')k.push_back(c>='A'&&c<='Z'?char(c-'A'+'a'):c);return k;};
        const auto wanted=key(name);if(wanted.empty())return std::nullopt;
        for(const auto&f:bundled){const auto k=key(f.name);if(f.latin?wanted.starts_with(k):wanted==k)return f.name;}
        return std::nullopt;
    }
    std::string sha256(std::span<const std::uint8_t> bytes)const{
        std::array<UCHAR,32> hash{};if(BCryptHash(sha,nullptr,0,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),hash.data(),static_cast<ULONG>(hash.size()))<0)throw std::runtime_error("Cannot compute a SHA-256 digest");
        constexpr char hex[]="0123456789abcdef";std::string digest;digest.reserve(64);for(auto v:hash){digest+=hex[v>>4];digest+=hex[v&15];}return digest;
    }
    // Fonts/manifest.json, schema 2 (HUDFontRegistry.manifestFamilies): every
    // family whose platforms include "windows" and whose role is latin or cjk.
    // As on the Mac a family with an absent font file is left out (its region
    // then cascades to the Windows system family, the Latin role to Segoe UI).
    // A present file must have the manifest's size; the Latin faces (under
    // 1 MiB each, drawn for every Latin letter and digit) also its SHA-256,
    // while the multi-megabyte Noto files are hashed when CMake stages them
    // (windows/CMakeLists.txt), not at every launch. Licence texts are not
    // fonts. DirectWrite reads the files lazily; nothing is installed.
    void loadFonts(const std::filesystem::path&directory,IDWriteFontSetBuilder1&builder){
        const auto manifestFile=directory/L"manifest.json";std::error_code error;
        const auto size=std::filesystem::file_size(manifestFile,error);
        if(error||!std::filesystem::is_regular_file(manifestFile,error)||size==0||size>1024*1024)invalid("Bundled font manifest is missing");
        std::string data(static_cast<std::size_t>(size),'\0');std::ifstream input(manifestFile,std::ios::binary);
        if(!input.read(data.data(),static_cast<std::streamsize>(data.size())))invalid("Cannot read the bundled font manifest");
        const auto manifest=Json::parse(data,1024*1024);
        if(!manifest["schemaVersion"].isNumber()||manifest["schemaVersion"].number()!=2)invalid("Bundled font manifest has an unknown schema");
        for(const auto&family:array(manifest["families"],64)){
            const auto name=string(family["family"]),role=string(family["role"]);bool windows=false;
            for(const auto&platform:array(family["platforms"],16))windows=windows||(platform.isString()&&platform.string()=="windows");
            if(name.empty()||!windows||(role!="latin"&&role!="cjk"))continue;
            struct File{std::filesystem::path path;std::uintmax_t bytes{};std::string sha256;};std::vector<File> files;bool present=true;
            for(const auto&entry:array(family["files"],64)){
                const auto file=string(entry["file"]);std::string lower;for(const char c:file)lower.push_back(c>='A'&&c<='Z'?char(c-'A'+'a'):c);
                if(!lower.ends_with(".ttf")&&!lower.ends_with(".otf"))continue;
                if(file.find_first_of("/\\:")!=std::string::npos||file.starts_with("."))invalid("Bundled font manifest names a file outside fonts/");
                const auto bytes=number(entry["bytes"]);const auto digest=string(entry["sha256"]);
                if(bytes<=0||std::floor(bytes)!=bytes||digest.size()!=64||digest.find_first_not_of("0123456789abcdef")!=std::string::npos)invalid("Bundled font manifest entry needs its size and SHA-256");
                File item{directory/wide(file),static_cast<std::uintmax_t>(bytes),digest};present=present&&std::filesystem::is_regular_file(item.path,error);files.push_back(std::move(item));
            }
            if(files.empty()||!present)continue;
            for(const auto&f:files){
                if(std::filesystem::file_size(f.path)!=f.bytes)invalid("Bundled font has an unexpected size");
                if(f.bytes<=1024*1024){std::vector<std::uint8_t> bytes(static_cast<std::size_t>(f.bytes));std::ifstream font(f.path,std::ios::binary);
                    if(!font.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))||sha256(bytes)!=f.sha256)invalid("Bundled font does not match its manifest SHA-256");}
                ComPtr<IDWriteFontFile>fontFile;checked(text->CreateFontFileReference(f.path.c_str(),nullptr,fontFile.GetAddressOf()),"Open bundled font");
                checked(builder.AddFontFile(fontFile.Get()),"Add bundled font (every named instance of a variable font)");
            }
            BundledFamily loaded{name,role=="latin",{}};
            if(!loaded.latin&&!family["languages"].isNull())for(const auto&tag:array(family["languages"],16))if(tag.isString())if(const auto region=core::regionFromTag(tag.string())){loaded.region=region;break;}
            // HUDFontRegistry.family(role:language:): the first family of a role or region.
            if(loaded.latin&&latinFamily.empty())latinFamily=name;
            if(loaded.region&&regionFamilies[index(*loaded.region)].empty())regionFamilies[index(*loaded.region)]=name;
            if(!isBundled(name))bundledNames.push_back(name);
            bundled.push_back(std::move(loaded));
        }
    }
    // HUDFontResolver.descriptor's cascade as one IDWriteFontFallback: for each
    // region, keyed by its locale, then for every other locale (the UI region),
    // a mapping of all of Unicode from the standard face to [the region's Noto
    // family, the other bundled Noto families in region order]; then the
    // system fallback (emoji, symbols), like CoreText's own cascade. The
    // mappings name the standard face as their base family, so a face the user
    // chose keeps the system fallback it had. Characters the Latin face has
    // never reach the fallback.
    ComPtr<IDWriteFontFallback> cascadeFor(LayerFontLanguage ui){
        ComPtr<IDWriteFactory2> factory;checked(text.As(&factory),"The font cascade requires DirectWrite 1.2");
        ComPtr<IDWriteFontFallbackBuilder> builder;checked(factory->CreateFontFallbackBuilder(builder.GetAddressOf()),"Create the font cascade");
        const DWRITE_UNICODE_RANGE all{0,0x10FFFF};const auto base=wide(standardFamily());
        const auto add=[&](LayerFontLanguage region,const wchar_t*localeName){
            std::vector<std::wstring> names;bool system=false;
            const auto flush=[&]{if(names.empty())return;std::vector<const WCHAR*> list;for(const auto&n:names)list.push_back(n.c_str());
                checked(builder->AddMapping(&all,1,list.data(),static_cast<UINT32>(list.size()),system?nullptr:bundledFonts.Get(),localeName,base.c_str(),1),"Map the region's CJK cascade");names.clear();};
            std::vector<LayerFontLanguage> order{region};for(const auto other:core::textRegions)if(other!=region)order.push_back(other);
            for(const auto r:order){const bool absent=regionFamilies[index(r)].empty();if(absent&&r!=region)continue;
                if(absent!=system)flush();system=absent;names.push_back(wide(regionFamily(r)));}
            flush();
        };
        for(const auto region:core::textRegions)add(region,locale(region));
        add(ui,nullptr);
        ComPtr<IDWriteFontFallback> systemFallback;checked(factory->GetSystemFontFallback(systemFallback.GetAddressOf()),"Read the system font fallback");
        checked(builder->AddMappings(systemFallback.Get()),"Append the system font fallback");
        ComPtr<IDWriteFontFallback> result;checked(builder->CreateFontFallback(result.GetAddressOf()),"Create the font cascade");return result;
    }
    void cascade(IDWriteTextFormat*format){ComPtr<IDWriteTextFormat1> advanced;checked(format->QueryInterface(IID_PPV_ARGS(advanced.GetAddressOf())),"The font cascade requires DirectWrite 1.2");checked(advanced->SetFontFallback(fallback.Get()),"Set the font cascade");}
    explicit Impl(const std::filesystem::path&fontDirectory){
        checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf()),"Create local D2D factory");
        checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(text.GetAddressOf())),"Create retained DirectWrite factory");
        checked(text->GetSystemFontCollection(fonts.GetAddressOf(),FALSE),"Read local font collection");
        for(const auto region:core::textRegions)tags[index(region)]=wide(core::regionTag(region));
        auto directory=fontDirectory;
        if(directory.empty()){std::array<wchar_t,32768>path{};const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));if(!length||length>=path.size())invalid("Cannot locate application font directory");directory=std::filesystem::path(std::wstring(path.data(),length)).parent_path()/L"fonts";}
        if(BCryptOpenAlgorithmProvider(&sha,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot initialize asset hash verification");
        try{
            ComPtr<IDWriteFactory5>advanced;checked(text.As(&advanced),"Private fonts require DirectWrite on Windows 10 build 15063 or newer");
            ComPtr<IDWriteFontSetBuilder1>builder;checked(advanced->CreateFontSetBuilder(builder.GetAddressOf()),"Create private application font set");
            loadFonts(directory,*builder.Get());
            ComPtr<IDWriteFontSet>fontSet;checked(builder->CreateFontSet(fontSet.GetAddressOf()),"Retain private application font set");
            ComPtr<IDWriteFontCollection1>collection;checked(advanced->CreateFontCollectionFromFontSet(fontSet.Get(),collection.GetAddressOf()),"Create the private application font collection");
            checked(collection.As(&bundledFonts),"Retain application font collection");
            for(const auto&f:bundled){UINT32 at{};BOOL exists{};const auto name=wide(f.name);checked(bundledFonts->FindFamilyName(name.c_str(),&at,&exists),"Verify private font family");if(!exists)invalid("Bundled font has an unexpected family");oblique.families.push_back(name);}
            checked(text->CreateTypography(tabularFigures.GetAddressOf()),"Create tabular figures");
            checked(tabularFigures->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES,1}),"Request tabular figures");
            checked(text->CreateTypography(plainFigures.GetAddressOf()),"Create proportional figures");
            fallback=cascadeFor(language);
            checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.GetAddressOf())),"Create local WIC factory");
        }catch(...){BCryptCloseAlgorithmProvider(sha,0);sha={};throw;}
    }
    ~Impl(){if(sha)BCryptCloseAlgorithmProvider(sha,0);}
    void onThread()const{if(GetCurrentThreadId()!=thread)throw std::logic_error("Layer rasterizer used outside its creating thread");}
    static void issue(LayerRasterImage& image,const Json& node,std::string feature){
        const auto id=string(node["id"],"unnamed");
        if(std::none_of(image.unsupported.begin(),image.unsupported.end(),[&](const auto& item){return item.node==id&&item.feature==feature;}))
            image.unsupported.push_back({id,std::move(feature)});
    }
    ComPtr<ID2D1PathGeometry> path(const Json& commands,std::string_view fillRule){
        ComPtr<ID2D1PathGeometry> geometry;checked(d2d->CreatePathGeometry(geometry.GetAddressOf()),"Create source path");
        ComPtr<ID2D1GeometrySink> sink;checked(geometry->Open(sink.GetAddressOf()),"Open source path");
        sink->SetFillMode(fillRule=="even-odd"||fillRule=="evenOdd"?D2D1_FILL_MODE_ALTERNATE:D2D1_FILL_MODE_WINDING);
        bool open=false;
        if(!commands.isNull())for(const auto& command:array(commands,65536)){
            const auto op=string(command["op"]);const auto& points=array(command["points"],3);
            if(op=="move") {if(points.size()!=1)invalid("Invalid move command");if(open)sink->EndFigure(D2D1_FIGURE_END_OPEN);sink->BeginFigure(point(points[0]),D2D1_FIGURE_BEGIN_FILLED);open=true;}
            else if(op=="line"){if(!open||points.size()!=1)invalid("Invalid line command");sink->AddLine(point(points[0]));}
            else if(op=="quadratic"){if(!open||points.size()!=2)invalid("Invalid quadratic command");sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(point(points[0]),point(points[1])));}
            else if(op=="cubic"){if(!open||points.size()!=3)invalid("Invalid cubic command");sink->AddBezier(D2D1::BezierSegment(point(points[0]),point(points[1]),point(points[2])));}
            else if(op=="close"){if(!open||!points.empty())invalid("Invalid close command");sink->EndFigure(D2D1_FIGURE_END_CLOSED);open=false;}
            else invalid("Unknown retained path command");
        }
        if(open)sink->EndFigure(D2D1_FIGURE_END_OPEN);
        checked(sink->Close(),"Complete source path");return geometry;
    }
    ComPtr<ID2D1StrokeStyle> stroke(const Json& shape){
        auto properties=D2D1::StrokeStyleProperties();
        const auto cap=string(shape["lineCap"],"butt"),join=string(shape["lineJoin"],"miter");
        properties.startCap=properties.endCap=properties.dashCap=cap=="round"?D2D1_CAP_STYLE_ROUND:cap=="square"?D2D1_CAP_STYLE_SQUARE:D2D1_CAP_STYLE_FLAT;
        properties.lineJoin=join=="round"?D2D1_LINE_JOIN_ROUND:join=="bevel"?D2D1_LINE_JOIN_BEVEL:D2D1_LINE_JOIN_MITER;
        properties.miterLimit=std::max(1.f,real(shape["miterLimit"],10));
        const float width=std::max(.001f,real(shape["lineWidth"],1));
        std::vector<float> dashes;
        if(!shape["lineDashPattern"].isNull())for(const auto& v:array(shape["lineDashPattern"],256)){
            const float value=real(v);if(value<0)invalid("Negative dash length");dashes.push_back(value/width);
        }
        if(!dashes.empty()){
            if(std::all_of(dashes.begin(),dashes.end(),[](float v){return v==0;}))invalid("Zero-length dash pattern");
            if(dashes.size()%2){const auto copy=dashes;dashes.insert(dashes.end(),copy.begin(),copy.end());}
            properties.dashStyle=D2D1_DASH_STYLE_CUSTOM;
        }
        properties.dashOffset=real(shape["lineDashPhase"])/width;
        ComPtr<ID2D1StrokeStyle> result;checked(d2d->CreateStrokeStyle(properties,dashes.data(),static_cast<UINT32>(dashes.size()),result.GetAddressOf()),"Create retained stroke style");return result;
    }
    ComPtr<ID2D1Geometry> boundsGeometry(const Json& node){
        const auto bounds=rectangle(node["bounds"]);const auto radius=std::max(0.f,real(node["cornerRadius"]));
        ComPtr<ID2D1Geometry> result;
        if(radius>0){ComPtr<ID2D1RoundedRectangleGeometry> g;checked(d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect(bounds),radius,radius),g.GetAddressOf()),"Create rounded layer bounds");checked(g.As(&result),"Read rounded geometry");}
        else{ComPtr<ID2D1RectangleGeometry> g;checked(d2d->CreateRectangleGeometry(rect(bounds),g.GetAddressOf()),"Create layer bounds");checked(g.As(&result),"Read rectangle geometry");}
        return result;
    }
    core::Rect measure(const Json& node,const Matrix& world,LayerRasterImage& result,std::size_t& visited,unsigned depth){
        if(++visited>maximumNodes||depth>64)invalid("Local layer tree exceeds its bound");
        if(!node.isObject())invalid("Layer is not an object");
        auto bounds=rectangle(node["bounds"]);auto measured=transformBounds(bounds,world);
        if(string(node["kind"],"layer")=="shape"&&!node["shape"]["path"].isNull()){
            auto g=path(node["shape"]["path"],string(node["shape"]["fillRule"]));D2D1_RECT_F r{};
            if(!node["shape"]["strokeColor"].isNull()&&real(node["shape"]["lineWidth"],1)>0){auto s=stroke(node["shape"]);checked(g->GetWidenedBounds(real(node["shape"]["lineWidth"],1),s.Get(),&world,.1f,&r),"Measure stroke extent");}
            else checked(g->GetBounds(&world,&r),"Measure source path");
            if(r.right>=r.left&&r.bottom>=r.top)measured=unite(measured,{r.left,r.top,r.right-r.left,r.bottom-r.top});
        }
        if(string(node["kind"],"layer")=="text"&&lineBox(node["text"])){const auto p=lineBoxPadding(node["text"]);
            measured=unite(measured,transformBounds({bounds.x-p.width,bounds.y-p.height,bounds.width+2*p.width,bounds.height+2*p.height},world));}
        if(flag(node["masksToBounds"]))return measured;
        Matrix sublayer;if(!childTransform(node,sublayer)){issue(result,node,"projective child transform must remain in the GPU scene");return measured;}
        for(auto child:children(node)){
            if(flag((*child)["hidden"])||real((*child)["opacity"],1)<=0)continue;
            Matrix local;if(!placement(*child,local)){issue(result,*child,"projective child placement must remain in the GPU scene");continue;}
            measured=unite(measured,measure(*child,local*sublayer*world,result,visited,depth+1));
        }
        // The blurred shadow reaches three standard deviations past its shape.
        if(const auto s=shadow(node)){
            auto source=measured;if(s->path){auto g=path(*s->path,"");D2D1_RECT_F r{};checked(g->GetBounds(&world,&r),"Measure shadow path");source={r.left,r.top,std::max(0.f,r.right-r.left),std::max(0.f,r.bottom-r.top)};}
            const auto a=world.TransformPoint({0,0}),b=world.TransformPoint(s->offset);const double spread=3*shadowSigmaPerRadius*s->radius*std::sqrt(std::abs(double(world.Determinant())));
            measured=unite(measured,{source.x+b.x-a.x-spread,source.y+b.y-a.y-spread,source.width+2*spread,source.height+2*spread});
        }
        return measured;
    }
    // A Core Animation layer shadow (CALayer defaults: opaque black colour,
    // radius 3, offset (0,-3) in the layer's own y-down space), or none
    // while shadowOpacity is 0. shadowPath uses the shape path commands.
    struct Shadow {D2D1_COLOR_F color{0,0,0,1};float radius{3};D2D1_POINT_2F offset{0,-3};const Json*path{};};
    static std::optional<Shadow> shadow(const Json&node){
        const auto opacity=std::clamp(real(node["shadowOpacity"]),0.f,1.f);if(opacity<=0)return std::nullopt;
        Shadow s;if(!node["shadowColor"].isNull())color(node["shadowColor"],s.color);s.color.a*=opacity;if(s.color.a<=0)return std::nullopt;
        s.radius=std::max(0.f,real(node["shadowRadius"],3));s.offset=point(node["shadowOffset"],{0,-3});if(!node["shadowPath"].isNull())s.path=&node["shadowPath"];
        return s;
    }
    // The CJK region of a text descriptor: its `language`, else the UI's.
    LayerFontLanguage region(const Json&descriptor)const{
        if(descriptor.isObject()&&descriptor["language"].isString())if(const auto l=core::regionFromTag(descriptor["language"].string()))return *l;
        return language;}
    ResolvedFont fontFamily(const Json& font,const Json& node,const LayerRasterOptions& options,LayerRasterImage& result){
        const auto family=string(font["familyName"],string(font["postScriptName"]));
        const auto postscript=string(font["postScriptName"]);
        const bool clock=flag(node["text"]["preserveSourceFont"]),preserve=clock||flag(font["preserveUserFont"]);
        const auto report=[&](const std::string&selected){
            LayerFontSubstitution substitution{string(node["id"],"unnamed"),family,postscript,selected,selected==standardFamily()?regionFamily(region(node["text"])):std::string{}};
            if(std::none_of(result.fontSubstitutions.begin(),result.fontSubstitutions.end(),[&](const auto&f){return f.node==substitution.node&&f.requestedFamily==family&&f.requestedFace==postscript&&f.selectedFamily==selected;}))result.fontSubstitutions.push_back(std::move(substitution));};
        const auto resolved=[&](std::wstring name,IDWriteFontCollection*collection,const std::string&selected){
            return ResolvedFont{std::move(name),collection,selected==standardFamily()&&collection==collectionOf(selected)};};
        // The standard faces (HarmonyOS Sans with the region's Noto cascade)
        // replace every source UI face, as HUDFontResolver does on the Mac,
        // while persisted user choices and the source clock styles stay explicit.
        const auto requested=preserve?family:fallbackFamily(options);
        auto*collection=collectionOf(requested);
        UINT32 index{};BOOL exists=FALSE;auto selected=wide(requested);
        if(!selected.empty())checked(collection->FindFamilyName(selected.c_str(),&index,&exists),"Resolve source font family");
        if(!exists&&preserve){
            // The shared fonts' own Mac names (stored by Mac 1.3.0 font menus).
            if(const auto face=bundledFace(postscript.empty()?family:postscript)){report(*face);return resolved(wide(*face),bundledFonts.Get(),*face);}
            // Imported Mac runs store PostScript names (HelveticaNeue-Bold,
            // TimesNewRomanPSMT, STSongti-SC-Regular, ...): resolve the Windows
            // counterpart here, once for settled cards and both editors. The
            // weight still follows the PostScript name (weight() below).
            if(const auto mapped=core::notes::notesWindowsFontFamily(postscript.empty()?family:postscript)){
                const std::string name(*mapped);auto*candidate=collectionOf(name);auto w=wide(name);
                BOOL found=FALSE;UINT32 at{};checked(candidate->FindFamilyName(w.c_str(),&at,&found),"Resolve mapped Mac font family");
                if(found){report(name);return resolved(std::move(w),candidate,name);}
            }
        }
        if(!exists){
            // NSFontMonoSpaceTrait is 1<<10 in the current macOS SDK. Known
            // private/public fixed-pitch families keep their typography class;
            // proportional system fonts and unknown requests keep the caller's
            // normal fallback, without guessing from their role in the HUD.
            const bool fixedPitch=(static_cast<unsigned>(number(font["symbolicTraits"]))&(1u<<10))!=0||family=="Menlo"||family=="Monaco"||family=="SF Mono"||family==".AppleSystemUIFontMonospaced"||family.starts_with(".SFNSMono")||postscript.starts_with("Menlo-")||postscript.starts_with("SFMono-")||postscript.starts_with(".SFNSMono")||postscript.starts_with(".AppleSystemUIFontMonospaced-");
            const std::string substitute=fixedPitch?options.monospaceFallbackFontFamily:clock?"Segoe UI":fallbackFamily(options);
            collection=collectionOf(substitute);
            selected=wide(substitute);checked(collection->FindFamilyName(selected.c_str(),&index,&exists),"Resolve explicit fallback font");
            if(!exists)invalid("Configured fallback font is not installed");
            LayerFontSubstitution substituted{string(node["id"],"unnamed"),family,postscript,substitute,substitute==standardFamily()?regionFamily(region(node["text"])):std::string{}};
            if(std::none_of(result.fontSubstitutions.begin(),result.fontSubstitutions.end(),[&](const auto& f){return f.node==substituted.node&&f.requestedFamily==substituted.requestedFamily&&f.requestedFace==substituted.requestedFace;}))result.fontSubstitutions.push_back(std::move(substituted));
            return resolved(std::move(selected),collection,substitute);
        }
        if(requested!=family)report(requested);
        return resolved(std::move(selected),collection,requested);
    }
    // The weight a source font names. Black/Heavy and Thin read as such
    // (HarmonyOS_Sans_Black was Normal before the shared fonts).
    static DWRITE_FONT_WEIGHT weight(const Json& font){
        const auto traits=static_cast<unsigned>(number(font["symbolicTraits"]));const auto name=string(font["postScriptName"]);
        if(name==".AppleSystemUIFontDemi"||name.find("Semibold")!=std::string::npos||name.find("SemiBold")!=std::string::npos)return DWRITE_FONT_WEIGHT_SEMI_BOLD;
        if(name.find("Black")!=std::string::npos||name.find("Heavy")!=std::string::npos)return DWRITE_FONT_WEIGHT_BLACK;
        if((traits&2)||name.find("Bold")!=std::string::npos)return DWRITE_FONT_WEIGHT_BOLD;
        if(name.find("Medium")!=std::string::npos)return DWRITE_FONT_WEIGHT_MEDIUM;
        if(name.find("Light")!=std::string::npos)return DWRITE_FONT_WEIGHT_LIGHT;
        if(name.find("Thin")!=std::string::npos)return DWRITE_FONT_WEIGHT_THIN;
        return DWRITE_FONT_WEIGHT_NORMAL;
    }
    // HUDFontWeight.resolved / HUDFontWeight(NSFont.Weight): the five
    // HarmonyOS Sans faces. Thin to Light render Light, 600 (the design's
    // semibold, which has no file) renders Bold as browsers do, heavy Black.
    static DWRITE_FONT_WEIGHT standardWeight(DWRITE_FONT_WEIGHT w)noexcept{
        return w<350?DWRITE_FONT_WEIGHT_LIGHT:w<450?DWRITE_FONT_WEIGHT_NORMAL:w<550?DWRITE_FONT_WEIGHT_MEDIUM:w<750?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_BLACK;}
    // A descriptor's (or run's) `fontWeight` (CSS 100-900) wins over the
    // weight its source font names; the standard faces bucket it.
    static DWRITE_FONT_WEIGHT textWeight(const Json&style,const Json&font,bool standard){
        DWRITE_FONT_WEIGHT w=weight(font);
        if(!style["fontWeight"].isNull()){const auto value=number(style["fontWeight"]);if(value<1||value>999)invalid("Text weight exceeds its range");w=static_cast<DWRITE_FONT_WEIGHT>(std::lround(value));}
        return standard?standardWeight(w):w;}
    // Line metrics in em. The standard face reads its hhea ascender,
    // descender and line gap as CoreText does (HUDText.metrics, NSFont): for
    // HarmonyOS Sans .928/.244/0, where DirectWrite's DWRITE_FONT_METRICS are
    // the OS/2 Windows metrics (1.056/.271). Other faces keep DirectWrite's.
    std::array<double,3> lineMetrics(const ResolvedFont&font,DWRITE_FONT_WEIGHT w,DWRITE_FONT_STYLE style){
        if(font.standard)if(const auto found=struts.find(w);found!=struts.end())return found->second;
        UINT32 at{};BOOL exists{};checked(font.collection->FindFamilyName(font.family.c_str(),&at,&exists),"Resolve font metrics");if(!exists)invalid("Font family disappeared");
        ComPtr<IDWriteFontFamily> family;checked(font.collection->GetFontFamily(at,family.GetAddressOf()),"Read font family");
        ComPtr<IDWriteFont> matched;checked(family->GetFirstMatchingFont(w,DWRITE_FONT_STRETCH_NORMAL,style,matched.GetAddressOf()),"Match font face");
        DWRITE_FONT_METRICS metrics{};matched->GetMetrics(&metrics);if(!metrics.designUnitsPerEm)invalid("Invalid font design metrics");
        const double em=metrics.designUnitsPerEm;std::array<double,3> result{metrics.ascent/em,metrics.descent/em,metrics.lineGap/em};
        if(font.standard){
            ComPtr<IDWriteFontFace> face;checked(matched->CreateFontFace(face.GetAddressOf()),"Read standard face");
            const void*data{};UINT32 size{};void*context{};BOOL found{};
            checked(face->TryGetFontTable(DWRITE_MAKE_OPENTYPE_TAG('h','h','e','a'),&data,&size,&context,&found),"Read standard face line metrics");
            if(found&&size>=10){const auto*b=static_cast<const std::uint8_t*>(data);const auto s16=[&](unsigned at){return static_cast<std::int16_t>((b[at]<<8)|b[at+1]);};
                result={s16(4)/em,-s16(6)/em,std::max(0,int(s16(8)))/em};}
            if(found)face->ReleaseFontTable(context);
            struts.emplace(w,result);
        }
        return result;
    }
    ImagePixels image(const Json& contents,const LayerRasterOptions& options){
        if(contents.contains("memoryImage")){
            if(!contents.isObject()||contents.object().size()!=2||!contents["memoryImage"].isString()||
               !contents["revision"].isNumber()||!options.memoryImages)
                invalid("Memory image needs its exact key/revision and explicit provider");
            const auto revision=contents["revision"].integer();
            if(revision<=0)invalid("Memory image revision must be positive");
            auto snapshot=options.memoryImages->acquire(contents["memoryImage"].string(),static_cast<std::uint64_t>(revision));
            if(!snapshot)invalid("Memory image key/revision is missing or retired");
            memoryImageUsed=true;
            ImagePixels pixels{snapshot->width(),snapshot->height(),snapshot->premultipliedBGRA(),std::move(snapshot)};
            return pixels;
        }
        const auto expected=string(contents["sha256"]),relative=string(contents["asset"]);
        if(expected.size()!=64||expected.find_first_not_of("0123456789abcdef")!=std::string::npos)invalid("Intrinsic image requires SHA-256");
        const auto path=std::filesystem::path(wide(relative));
        if(options.assetRoot.empty()||path.is_absolute()||path.has_root_name()||relative.find('\\')!=std::string::npos||relative.find(':')!=std::string::npos)invalid("Image needs a confined explicit fixture path");
        for(const auto& part:path)if(part==L".."||part==L".")invalid("Image path traversal rejected");
        if(const auto it=images.find(expected);it!=images.end())return {it->second.width,it->second.height,it->second.premultipliedBGRA,{}};
        if(images.size()>=256)invalid("Decoded intrinsic image cache is full; clear unused content");
        const auto root=std::filesystem::canonical(options.assetRoot),file=std::filesystem::canonical(root/path);
        auto r=root.begin(),f=file.begin();for(;r!=root.end();++r,++f)if(f==file.end()||*r!=*f)invalid("Image path escapes fixture root");
        const auto size=std::filesystem::file_size(file);if(size==0||size>64*1024*1024)invalid("Intrinsic image exceeds its file bound");
        std::ifstream input(file,std::ios::binary);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if(!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("Cannot read explicit intrinsic image");
        if(sha256(bytes)!=expected)invalid("Intrinsic image hash mismatch");
        ComPtr<IWICStream> stream;checked(wic->CreateStream(stream.GetAddressOf()),"Create owned WIC stream");
        checked(stream->InitializeFromMemory(bytes.data(),static_cast<DWORD>(bytes.size())),"Read owned image bytes");
        ComPtr<IWICBitmapDecoder> decoder;checked(wic->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf()),"Decode intrinsic image");
        ComPtr<IWICBitmapFrameDecode> frame;checked(decoder->GetFrame(0,frame.GetAddressOf()),"Read intrinsic image frame");
        DecodedImage decoded;checked(frame->GetSize(&decoded.width,&decoded.height),"Read intrinsic image size");
        if(!decoded.width||!decoded.height||std::size_t(decoded.width)*decoded.height>maximumPixels)invalid("Intrinsic image exceeds its pixel bound");
        const auto byteCount=std::size_t(decoded.width)*decoded.height*4;
        if(byteCount>maximumResourceBytes-counts.resourceBytes)invalid("Layer resource budget exceeded");
        ComPtr<IWICFormatConverter> converted;checked(wic->CreateFormatConverter(converted.GetAddressOf()),"Create image format conversion");
        checked(converted->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert intrinsic image to premultiplied BGRA");
        decoded.premultipliedBGRA.resize(byteCount);checked(converted->CopyPixels(nullptr,decoded.width*4,static_cast<UINT>(byteCount),decoded.premultipliedBGRA.data()),"Copy owned image pixels");
        counts.resourceBytes+=byteCount;++counts.imageDecodes;const auto& stored=images.emplace(expected,std::move(decoded)).first->second;
        return {stored.width,stored.height,stored.premultipliedBGRA,{}};
    }
    ComPtr<ID2D1SolidColorBrush> brush(ID2D1RenderTarget* target,const Json& value,float alpha=1){
        D2D1_COLOR_F c{};ComPtr<ID2D1SolidColorBrush> result;if(color(value,c,alpha))checked(target->CreateSolidColorBrush(c,result.GetAddressOf()),"Create source color brush");return result;
    }
    core::Point textOffset(IDWriteTextLayout*layout,const LayerRasterOptions& options,core::Rect bounds,const DocumentTextLines& lines={},core::Point inset={},double documentWidth=0,bool singleLine=false){
        auto offset=options.textDocumentOffset;if(!options.plainTextDocument&&!options.richTextDocument)return offset;
        DWRITE_TEXT_METRICS metrics{};checked(layout->GetMetrics(&metrics),"Read bounded document extent");
        if(!std::isfinite(metrics.top)||!std::isfinite(metrics.height)||metrics.height<0)invalid("Invalid text document extent");
        const auto maximum=singleLine?0.:std::max(0.,std::ceil(double(metrics.top)+metrics.height+lines.extraHeight())+1-bounds.height);
        const auto maximumX=singleLine?std::max(0.,documentWidth-bounds.width):0.;offset.x=std::clamp(offset.x,0.,maximumX);
        offset.y=std::min(offset.y,maximum);
        if(options.revealPlainTextPosition){FLOAT x{},y{};DWRITE_HIT_TEST_METRICS hit{};checked(layout->HitTestTextPosition(*options.revealPlainTextPosition,FALSE,&x,&y,&hit),"Resolve document caret before painting");
            if(singleLine){const double left=x+inset.x,right=left+1;if(left<offset.x)offset.x=left;else if(right>offset.x+bounds.width)offset.x=right-bounds.width;offset.x=std::clamp(offset.x,0.,maximumX);}
            const auto top=double(y)+inset.y+lines.offsetAtACP(*options.revealPlainTextPosition);const auto bottom=top+std::max(1.,double(hit.height));if(top<offset.y)offset.y=top;else if(bottom>offset.y+bounds.height)offset.y=bottom-bounds.height;offset.y=std::clamp(offset.y,0.,maximum);}
        return offset;
    }
    // HUDTextStyle parts set per range (HUDText.attributed): `tabularFigures`
    // (OpenType tnum; false clears a descriptor's), `tracking` in em (CSS
    // letter-spacing after every character, HUDText's kern = tracking x size).
    void textFeatures(IDWriteTextLayout*layout,const Json&style,DWRITE_TEXT_RANGE range,double size){
        if(!style["tabularFigures"].isNull())checked(layout->SetTypography(flag(style["tabularFigures"])?tabularFigures.Get():plainFigures.Get(),range),"Set tabular figures");
        if(!style["tracking"].isNull()){const auto tracking=number(style["tracking"]);if(std::abs(tracking)>4)invalid("Text tracking exceeds its range");
            ComPtr<IDWriteTextLayout1> spacing;checked(layout->QueryInterface(IID_PPV_ARGS(spacing.GetAddressOf())),"Read text spacing interface");
            checked(spacing->SetCharacterSpacing(0,static_cast<FLOAT>(tracking*size),0,range),"Set text tracking");}
    }
    // The descriptor's runs as ranges of `layout`, which shapes the part
    // [offset, offset+count) of the string (a whole-string layout has offset
    // 0). Sizes, tracking and kerning grow by a line box's shrink `scale`.
    // Without a render target (measurement) run colours are not needed.
    void applyRuns(IDWriteTextLayout*layout,ID2D1RenderTarget*target,const Json&node,const LayerRasterOptions&options,LayerRasterImage&result,float opacity,
                   const ResolvedFont&base,double size,double scale,std::uint32_t offset,std::uint32_t count,std::size_t length){
        const auto&descriptor=node["text"];if(descriptor["runs"].isNull())return;
        for(const auto& run:array(descriptor["runs"],(options.richTextDocument||flag(descriptor["notesRichLine"]))?50000:4096)){
            const auto& r=array(run["utf16Range"],2);if(r.size()!=2)invalid("Invalid UTF-16 run range");
            const double start=number(r[0]),runLength=number(r[1]);if(start<0||runLength<0||std::floor(start)!=start||std::floor(runLength)!=runLength||start+runLength>length)invalid("Text run exceeds UTF-16 string");
            const auto& attributes=run["attributes"];if(!attributes.isObject())invalid("Text attributes must be an object");
            const auto first=std::max<double>(start,offset),last=std::min<double>(start+runLength,double(offset)+count);if(last<=first)continue;
            DWRITE_TEXT_RANGE range{static_cast<UINT32>(first-offset),static_cast<UINT32>(last-first)};
            const bool hasFont=attributes["NSFont"].isObject();const auto font=hasFont?fontFamily(attributes["NSFont"],node,options,result):base;
            double runSize=size;bool spaced=false,sized=false;
            for(const auto& [key,item]:attributes.object()){
                if(key=="NSFont"){
                    checked(layout->SetFontFamilyName(font.family.c_str(),range),"Set run font family");checked(layout->SetFontCollection(font.collection,range),"Set run font collection");
                    runSize=real(item["pointSize"],static_cast<float>(size));if(runSize<=0||runSize>2048)invalid("Run text size exceeds its range");sized=true;
                    checked(layout->SetFontSize(static_cast<FLOAT>(runSize*scale),range),"Set run font size");checked(layout->SetFontWeight(textWeight(attributes,item,font.standard),range),"Set run font weight");
                    checked(layout->SetFontStyle((static_cast<unsigned>(number(item["symbolicTraits"]))&1)?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,range),"Set run font style");
                }else if(key=="NSColor") {if(!target)continue;if(options.richTextDocument){D2D1_COLOR_F c{};if(color(item,c,opacity))checked(setDocumentTextColor(*layout,range,c),"Retain target-independent run color");}else{auto b=brush(target,item,opacity);if(b)checked(layout->SetDrawingEffect(b.Get(),range),"Set run foreground color");}}
                else if(key=="NSUnderline"||key=="NSStrikethrough"){
                    const auto style=number(item);if(style!=0&&style!=1)issue(result,node,"unsupported decorated text style: "+key);
                    if(key=="NSUnderline")checked(layout->SetUnderline(style!=0,range),"Set underline");else checked(layout->SetStrikethrough(style!=0,range),"Set strikethrough");
                }else if(key=="NSKern"){
                    ComPtr<IDWriteTextLayout1> advanced;checked(layout->QueryInterface(IID_PPV_ARGS(advanced.GetAddressOf())),"Read text spacing interface");checked(advanced->SetCharacterSpacing(0,static_cast<FLOAT>(real(item)*scale),0,range),"Set source tracking");spaced=true;
                }else if(key=="NSLanguage"){
                    // kCTLanguageAttributeName: this run's region (a ja title in a zh-Hans page).
                    const auto tag=string(item);if(const auto l=core::regionFromTag(tag))checked(layout->SetLocaleName(locale(*l),range),"Set run language");else issue(result,node,"unsupported text language: "+tag);
                }else if(key=="fontSize"){runSize=real(item);if(runSize<=0||runSize>2048)invalid("Run text size exceeds its range");sized=true;checked(layout->SetFontSize(static_cast<FLOAT>(runSize*scale),range),"Set run font size");}
                else if(key=="fontWeight"){if(!hasFont)checked(layout->SetFontWeight(textWeight(attributes,Json{},font.standard),range),"Set run font weight");}
                else if(key=="tabularFigures"||key=="tracking"){spaced=spaced||key=="tracking";}
                else if(key=="NSBaselineOffset"||key=="NSObliqueness"||key=="NSExpansion"||key=="NSStrokeWidth"){
                    if(number(item)!=0)issue(result,node,"unsupported text run effect: "+key);
                }else if(key=="NSLigature") {if(number(item)!=1)issue(result,node,"nondefault text ligature setting");}
                else issue(result,node,"unsupported text attribute: "+key);
            }
            textFeatures(layout,attributes,range,runSize*scale);
            // A run of another size keeps the descriptor's tracking in em.
            if(sized&&!spaced&&!descriptor["tracking"].isNull())textFeatures(layout,Json::Object{{"tracking",descriptor["tracking"]}},range,runSize*scale);
        }
    }
    // A descriptor's base format: the resolved face with its cascade and its
    // region's locale (the five source clocks keep en-us), alignment and wrap.
    ComPtr<IDWriteTextFormat> textFormat(const Json&descriptor,const ResolvedFont&family,double size,bool wrap,DWRITE_TEXT_ALIGNMENT alignment){
        const auto italic=(static_cast<unsigned>(number(descriptor["font"]["symbolicTraits"]))&1)!=0;
        ComPtr<IDWriteTextFormat> format;checked(text->CreateTextFormat(family.family.c_str(),family.collection,textWeight(descriptor,descriptor["font"],family.standard),
            italic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,static_cast<FLOAT>(size),flag(descriptor["preserveSourceFont"])?L"en-us":locale(region(descriptor)),format.GetAddressOf()),"Create source text format");
        cascade(format.Get());checked(format->SetTextAlignment(alignment),"Set text alignment");
        checked(format->SetWordWrapping(wrap?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP),"Set text wrapping");
        return format;
    }
    void ellipsis(IDWriteTextFormat*format){
        ComPtr<IDWriteInlineObject> sign;checked(text->CreateEllipsisTrimmingSign(format,sign.GetAddressOf()),"Create explicit text ellipsis");
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};checked(format->SetTrimming(&trimming,sign.Get()),"Set text trimming");
    }
    // HUDTextLayer.draw (Sources/HUDDesignText.swift) for a line-box leaf, whose
    // bounds are the CSS line box. maximumLines 1 (the default) is one line,
    // ellipsized to the box when wider than the box + .5 (truncation "end",
    // the default here as HUDTextLayer.truncates); 0 wraps without limit; n
    // wraps greedily and the last allowed line takes all the remaining text on
    // one line, ellipsized the same way. minimumScale < 1 first shrinks a
    // single line (every run with its tracking) to fit, never below that
    // fraction: decided here at raster time from the descriptor and the fonts,
    // so a language change refits it and no content event has to measure.
    // Each line is fontSize x scale x lineHeight tall, its baseline centred by
    // the Latin face's hhea strut (CJK fallback glyphs never enlarge it), and
    // the lines are centred vertically in the box; a single line sits by its
    // typographic width (left, center, right), wrapped lines by DirectWrite's
    // alignment. A single line never breaks at its own line separators
    // (CTLine): they are drawn as spaces. Layouts are placed in local points.
    struct LineBoxText {std::vector<std::pair<ComPtr<IDWriteTextLayout>,D2D1_POINT_2F>> parts;double scale{1},natural{},lineHeight{};std::size_t lines{};};
    LineBoxText lineBoxText(const Json&node,const std::wstring&value,core::Rect box,const LayerRasterOptions&options,LayerRasterImage&result,ID2D1RenderTarget*target,float opacity){
        const auto&descriptor=node["text"];LineBoxText out;
        const auto multiple=number(descriptor["lineHeight"]);if(!(multiple>0&&multiple<=8))invalid("Line height exceeds its range");
        const auto maximum=number(descriptor["maximumLines"],1);if(maximum<0||maximum>100000||std::floor(maximum)!=maximum)invalid("Invalid maximum line count");
        const auto minimumScale=number(descriptor["minimumScale"],1);if(!(minimumScale>0&&minimumScale<=1))invalid("Invalid minimum text scale");
        const auto truncation=string(descriptor["truncation"],"end");if(truncation!="end"&&truncation!="none")issue(result,node,"unsupported text truncation: "+truncation);
        const auto alignment=string(descriptor["alignment"],"left");if(alignment!="left"&&alignment!="center"&&alignment!="right")issue(result,node,"unsupported line-box alignment: "+alignment);
        if(!descriptor["language"].isNull()&&!core::regionFromTag(string(descriptor["language"])))issue(result,node,"unsupported text language: "+string(descriptor["language"]));
        const auto base=fontFamily(descriptor["font"],node,options,result);const auto size=number(descriptor["fontSize"],12);if(size<=0||size>2048)invalid("Text size exceeds its range");
        const auto italic=(static_cast<unsigned>(number(descriptor["font"]["symbolicTraits"]))&1)!=0;
        const auto metrics=lineMetrics(base,textWeight(descriptor,descriptor["font"],base.standard),italic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL);
        const bool single=maximum==1,truncate=truncation=="end";const auto length=static_cast<std::uint32_t>(value.size());
        const auto wrapped=alignment=="center"?DWRITE_TEXT_ALIGNMENT_CENTER:alignment=="right"?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING;
        const auto shape=[&](std::uint32_t offset,std::uint32_t count,double scale,bool wrap){
            const auto format=textFormat(descriptor,base,size*scale,wrap,wrap?wrapped:DWRITE_TEXT_ALIGNMENT_LEADING);
            out.lineHeight=size*scale*multiple;const double ascent=metrics[0]*size*scale,descent=metrics[1]*size*scale;
            checked(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,static_cast<FLOAT>(out.lineHeight),static_cast<FLOAT>((out.lineHeight-ascent-descent)/2+ascent)),"Set the CSS line box");
            if(!wrap&&truncate)ellipsis(format.Get());
            auto part=value.substr(offset,count);if(!wrap)for(auto&c:part)if(c==L'\n'||c==L'\r'||c==L'\x2028'||c==L'\x2029'||c==L'\x0b'||c==L'\x0c'||c==L'\x85')c=L' ';
            ComPtr<IDWriteTextLayout> layout;checked(text->CreateTextLayout(part.data(),count,format.Get(),wrap?static_cast<FLOAT>(std::max(1.,box.width)):std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),layout.GetAddressOf()),"Create line-box text layout");++counts.textLayoutsCreated;
            textFeatures(layout.Get(),descriptor,{0,count},size*scale);applyRuns(layout.Get(),target,node,options,result,opacity,base,size,scale,offset,count,value.size());
            return layout;
        };
        const auto width=[](IDWriteTextLayout*layout){DWRITE_TEXT_METRICS m{};checked(layout->GetMetrics(&m),"Measure line-box text");if(!std::isfinite(m.widthIncludingTrailingWhitespace))invalid("Invalid line-box text width");return double(m.widthIncludingTrailingWhitespace);};
        // One line from `offset`, ellipsized like CTLineCreateTruncatedLine.
        const auto line=[&](std::uint32_t offset,double scale){auto layout=shape(offset,length-offset,scale,false);
            if(truncate&&width(layout.Get())>box.width+.5)checked(layout->SetMaxWidth(static_cast<FLOAT>(std::max(1.,box.width))),"Truncate the line to its box");
            const auto w=width(layout.Get());return std::pair{layout,static_cast<float>(alignment=="center"?box.x+box.width/2-w/2:alignment=="right"?box.x+box.width-w:box.x)};};
        out.natural=width(shape(0,length,1,false).Get());
        if(single&&minimumScale<1&&out.natural>box.width&&out.natural>0)out.scale=std::max(minimumScale,box.width/out.natural);
        if(single){auto [layout,x]=line(0,out.scale);out.lines=1;out.parts.push_back({layout,{x,0}});}
        else{
            auto layout=shape(0,length,1,true);UINT32 count{};const auto probe=layout->GetLineMetrics(nullptr,0,&count);
            if((FAILED(probe)&&probe!=HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))||!count)invalid("Invalid line-box line metrics");
            std::vector<DWRITE_LINE_METRICS> lines(count);checked(layout->GetLineMetrics(lines.data(),count,&count),"Read line-box lines");
            const auto allowed=static_cast<std::size_t>(maximum);
            if(allowed&&count>allowed){
                std::uint32_t prefix{};for(std::size_t n=0;n+1<allowed;++n)prefix+=lines[n].length;
                if(prefix)out.parts.push_back({shape(0,prefix,1,true),{static_cast<float>(box.x),0}});
                auto [rest,x]=line(prefix,1);out.parts.push_back({rest,{x,static_cast<float>(out.lineHeight*(allowed-1))}});out.lines=allowed;
            }else{out.parts.push_back({layout,{static_cast<float>(box.x),0}});out.lines=count;}
        }
        const auto top=static_cast<float>(box.y+(box.height-out.lineHeight*out.lines)/2);for(auto&part:out.parts)part.second.y+=top;
        return out;
    }
    // Italic text in the shared fonts slants by the Mac's shear (ObliqueShear);
    // everything else draws exactly as Direct2D's DrawTextLayout.
    void paintLayout(ID2D1RenderTarget*target,IDWriteTextLayout*layout,D2D1_POINT_2F origin,ID2D1Brush*brush,const Json&descriptor,bool clip){
        if(italicText(descriptor))checked(drawShearedText(*target,*layout,origin,*brush,oblique,clip),"Paint slanted source text");
        else target->DrawTextLayout(origin,layout,brush,clip?D2D1_DRAW_TEXT_OPTIONS_CLIP:D2D1_DRAW_TEXT_OPTIONS_NONE);}
    void drawText(ID2D1RenderTarget* target,const Json& node,const LayerRasterOptions& options,LayerRasterImage& result,
                  std::vector<ComPtr<IDWriteTextLayout>>& layouts,float opacity){
        const auto& descriptor=node["text"];const auto bounds=rectangle(node["bounds"]);
        if(options.retainedPlainText){
            const auto& retained=*options.retainedPlainText;
            if(!retained.matchesPlainStyle(descriptor,bounds))invalid("Retained plain layout differs from its text or format");
            if(options.revealPlainTextPosition&&*options.revealPlainTextPosition>retained.text().size())invalid("Reveal ACP exceeds document");
            result.textContentInset=retained.contentInset();result.textDocumentWidth=retained.documentWidth();
            result.textDocumentOffset=textOffset(retained.nativeLayout(),options,bounds,retained.lines(),result.textContentInset,result.textDocumentWidth,flag(descriptor["sourceSingleLineField"]));
            auto foreground=brush(target,descriptor["foregroundColor"],opacity);
            target->PushAxisAlignedClip(rect(bounds),D2D1_ANTIALIAS_MODE_ALIASED);
            if(foreground){const auto origin=D2D1::Point2F(static_cast<float>(bounds.x+result.textContentInset.x-result.textDocumentOffset.x),static_cast<float>(bounds.y+result.textContentInset.y-result.textDocumentOffset.y));if(options.richTextDocument)checked(drawDocumentText(*target,*retained.nativeLayout(),retained.lines(),origin,foreground->GetColor(),&oblique),"Repaint retained rich document");else paintLayout(target,retained.nativeLayout(),origin,foreground.Get(),descriptor,flag(descriptor["sourceSingleLineField"]));}
            target->PopAxisAlignedClip();layouts.push_back(retained.nativeLayout());return;
        }
        const auto value=wide(string(descriptor["string"]));
        if((value.empty()&&!options.retainEmptyTextLayout)||bounds.width<=0||bounds.height<=0)return;
        if(value.size()>65536)invalid("Text layout exceeds its UTF-16 limit");
        if(options.revealPlainTextPosition&&*options.revealPlainTextPosition>value.size())invalid("Reveal ACP exceeds document");
        if(lineBox(descriptor)){
            if(options.plainTextDocument||options.richTextDocument||flag(descriptor["sourceSingleLineField"]))invalid("A line-box leaf is not an editor document");
            if(value.empty())return;
            const auto box=lineBoxText(node,value,bounds,options,result,target,opacity);const auto padding=lineBoxPadding(descriptor);
            // The padded layer frame clips, as HUDTextLayer's own bitmap does.
            target->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(bounds.x)-padding.width,static_cast<float>(bounds.y)-padding.height,static_cast<float>(bounds.x+bounds.width)+padding.width,static_cast<float>(bounds.y+bounds.height)+padding.height),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if(auto foreground=brush(target,descriptor["foregroundColor"],opacity))for(const auto&[layout,origin]:box.parts)target->DrawTextLayout(origin,layout.Get(),foreground.Get(),D2D1_DRAW_TEXT_OPTIONS_NONE);
            target->PopAxisAlignedClip();for(const auto&part:box.parts)layouts.push_back(part.first);lineBoxPainted=true;return;
        }
        const auto family=fontFamily(descriptor["font"],node,options,result);const auto size=real(descriptor["fontSize"],12);
        if(size<=0||size>2048)invalid("Text size exceeds its range");
        if(!descriptor["maximumLines"].isNull()||!descriptor["minimumScale"].isNull())issue(result,node,"maximumLines and minimumScale need a line-box leaf (lineHeight)");
        if(!descriptor["language"].isNull()&&!core::regionFromTag(string(descriptor["language"])))issue(result,node,"unsupported text language: "+string(descriptor["language"]));
        const auto italic=(static_cast<unsigned>(number(descriptor["font"]["symbolicTraits"]))&1)!=0;
        const auto alignment=string(descriptor["alignment"],"left");
        const auto format=textFormat(descriptor,family,size,flag(descriptor["wrapped"]),alignment=="center"?DWRITE_TEXT_ALIGNMENT_CENTER:alignment=="right"?DWRITE_TEXT_ALIGNMENT_TRAILING:
            alignment=="justified"?DWRITE_TEXT_ALIGNMENT_JUSTIFIED:DWRITE_TEXT_ALIGNMENT_LEADING);
        const auto ascent=real(descriptor["font"]["ascender"]),descent=real(descriptor["font"]["descender"]),leading=real(descriptor["font"]["leading"]);
        if(ascent>0&&ascent-descent+leading>0)checked(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,ascent-descent+leading,ascent),"Preserve source line metrics");
        const auto truncation=string(descriptor["truncation"],"none");
        if(truncation=="end")ellipsis(format.Get());
        else if(truncation!="none")issue(result,node,"unsupported text truncation: "+truncation);
        ComPtr<IDWriteTextLayout> layout;checked(text->CreateTextLayout(value.data(),static_cast<UINT32>(value.size()),format.Get(),
            static_cast<float>(bounds.width),(options.plainTextDocument||options.richTextDocument)?std::numeric_limits<float>::max():static_cast<float>(bounds.height),layout.GetAddressOf()),"Create retained source text layout");++counts.textLayoutsCreated;
        result.textDocumentWidth=bounds.width;
        textFeatures(layout.Get(),descriptor,{0,static_cast<UINT32>(value.size())},size);
        applyRuns(layout.Get(),target,node,options,result,opacity,family,size,1,0,static_cast<std::uint32_t>(value.size()),value.size());
        // A single-line field (NSTextView configureSingleLine: plain, or rich
        // like the Archive title): the three-point inset centred on the base
        // font's natural line, a finite container as wide as the laid-out
        // text (after its runs) and the first natural line only.
        if(flag(descriptor["sourceSingleLineField"])){
            if(!(options.plainTextDocument||options.richTextDocument)||flag(descriptor["wrapped"])||bounds.width<=6||!descriptor["font"]["ascender"].isNull())invalid("Single-line field needs natural plain metrics and explicit no-wrap");
            const auto natural=lineMetrics(family,textWeight(descriptor,descriptor["font"],family.standard),italic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL);
            const double lineHeight=(natural[0]+natural[1]+natural[2])*size;result.textContentInset={3,std::max(0.,(bounds.height-lineHeight)*.5)};
            DWRITE_TEXT_METRICS extent{};checked(layout->GetMetrics(&extent),"Measure same unwrapped field layout");if(!std::isfinite(extent.widthIncludingTrailingWhitespace)||extent.widthIncludingTrailingWhitespace<0)invalid("Invalid field text width");
            result.textDocumentWidth=std::max(bounds.width,std::ceil(double(extent.widthIncludingTrailingWhitespace))+11);
            checked(layout->SetMaxWidth(static_cast<float>(result.textDocumentWidth-6)),"Apply source finite field container width");
            UINT32 lineCount{};const auto measureLines=layout->GetLineMetrics(nullptr,0,&lineCount);
            if((FAILED(measureLines)&&measureLines!=HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))||!lineCount||lineCount>value.size()+1)invalid("Invalid field line metrics");
            std::vector<DWRITE_LINE_METRICS> lines(lineCount);checked(layout->GetLineMetrics(lines.data(),lineCount,&lineCount),"Read same field first line");
            if(!std::isfinite(lines.front().height)||lines.front().height<=0)invalid("Invalid field line height");
            // TextKit maximumNumberOfLines=1 clips the laid-out first line;
            // the complete caller text/ACP storage remains untouched.
            checked(layout->SetMaxHeight(lines.front().height),"Clip source field to its first natural line");
        }
        const DocumentTextLines lines=options.richTextDocument?DocumentTextLines(*layout.Get(),descriptor):DocumentTextLines{};
        result.textDocumentOffset=textOffset(layout.Get(),options,bounds,lines,result.textContentInset,result.textDocumentWidth,flag(descriptor["sourceSingleLineField"]));
        auto foreground=brush(target,descriptor["foregroundColor"],opacity);
        if(options.plainTextDocument||options.richTextDocument)target->PushAxisAlignedClip(rect(bounds),D2D1_ANTIALIAS_MODE_ALIASED);
        if(foreground){const auto origin=D2D1::Point2F(static_cast<float>(bounds.x+result.textContentInset.x-result.textDocumentOffset.x),static_cast<float>(bounds.y+result.textContentInset.y-result.textDocumentOffset.y));if(options.richTextDocument)checked(drawDocumentText(*target,*layout.Get(),lines,origin,foreground->GetColor(),&oblique),"Paint source rich document");else paintLayout(target,layout.Get(),origin,foreground.Get(),descriptor,!(options.plainTextDocument&&!flag(descriptor["sourceSingleLineField"])));}
        if(options.plainTextDocument||options.richTextDocument)target->PopAxisAlignedClip();
        layouts.push_back(std::move(layout));
    }
    // A layer shadow baked into the raster (instead of a GPU pass): the
    // shadow's shape (shadowPath, or the alpha of the layer's own drawing and
    // sublayers) painted into a target of the raster's size, blurred and
    // coloured by Direct2D's shadow effect (a Gaussian of 0.9 x shadowRadius,
    // in pixels) and drawn moved by shadowOffset under the layer's background,
    // inside its group opacity and masks (masksToBounds clips it, as in CA).
    void drawShadow(ID2D1RenderTarget*target,const Json&node,const Matrix&world,const Matrix&pixels,const LayerRasterOptions&options,LayerRasterImage&result,float opacity,unsigned depth){
        const auto s=shadow(node);if(!s)return;
        ComPtr<ID2D1DeviceContext> context;if(FAILED(target->QueryInterface(IID_PPV_ARGS(context.GetAddressOf())))){issue(result,node,"layer shadow needs Direct2D 1.1");return;}
        const auto size=target->GetPixelSize();const auto transform=world*pixels;
        ComPtr<ID2D1BitmapRenderTarget> shape;checked(target->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(size.width),static_cast<float>(size.height)),size,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,shape.GetAddressOf()),"Create layer shadow shape");
        shape->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);shape->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        shape->BeginDraw();shape->Clear(D2D1::ColorF(0.f,0.f,0.f,0.f));
        if(s->path){shape->SetTransform(transform);auto geometry=path(*s->path,"");ComPtr<ID2D1SolidColorBrush> ink;checked(shape->CreateSolidColorBrush(D2D1::ColorF(1.f,1.f,1.f,1.f),ink.GetAddressOf()),"Create shadow ink");shape->FillGeometry(geometry.Get(),ink.Get());}
        else{std::vector<ComPtr<IDWriteTextLayout>> unused;std::size_t visited{};const bool painted=lineBoxPainted;draw(shape.Get(),node,world,pixels,options,result,unused,1,visited,depth,true,true);lineBoxPainted=painted;}
        checked(shape->EndDraw(),"Paint layer shadow shape");
        ComPtr<ID2D1Bitmap> bitmap;checked(shape->GetBitmap(bitmap.GetAddressOf()),"Read layer shadow shape");
        ComPtr<ID2D1Effect> effect;checked(context->CreateEffect(shadowEffect,effect.GetAddressOf()),"Create layer shadow effect");effect->SetInput(0,bitmap.Get());
        const auto sigma=static_cast<float>(shadowSigmaPerRadius*s->radius*std::sqrt(std::abs(double(transform.Determinant()))));
        checked(effect->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION,sigma),"Set layer shadow radius");
        checked(effect->SetValue(D2D1_SHADOW_PROP_COLOR,D2D1::Vector4F(s->color.r,s->color.g,s->color.b,s->color.a*opacity)),"Set layer shadow colour");
        checked(effect->SetValue(D2D1_SHADOW_PROP_OPTIMIZATION,D2D1_SHADOW_OPTIMIZATION_QUALITY),"Set layer shadow quality");
        const auto a=transform.TransformPoint({0,0}),b=transform.TransformPoint(s->offset);
        target->SetTransform(Matrix::Translation(b.x-a.x,b.y-a.y));context->DrawImage(effect.Get());target->SetTransform(transform);
    }
    void draw(ID2D1RenderTarget* target,const Json& node,const Matrix& world,const Matrix& pixels,
              const LayerRasterOptions& options,LayerRasterImage& result,std::vector<ComPtr<IDWriteTextLayout>>& layouts,
              float inheritedOpacity,std::size_t& visited,unsigned depth,bool root,bool shadowSource=false){
        if(++visited>maximumNodes||depth>64)invalid("Local layer tree exceeds its bound");
        if(flag(node["hidden"]))return;
        // shadowSource: the layer's own drawing for its shadow's shape; its
        // opacity, masks and shadow belong to the pass that draws the shadow.
        const auto ownOpacity=(root&&!options.includeRootOpacity)||shadowSource?1.f:std::clamp(real(node["opacity"],1),0.f,1.f);
        if(ownOpacity<=0||inheritedOpacity<=0)return;
        ++counts.nodesDrawn;const auto bounds=rectangle(node["bounds"]);target->SetTransform(world*pixels);
        unsigned pushes=0;
        auto push=[&](ID2D1Geometry* geometry,const Matrix& maskTransform,float opacity){
            target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),geometry,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,maskTransform,opacity,nullptr,D2D1_LAYER_OPTIONS_NONE),nullptr);++pushes;
        };
        float paintOpacity=inheritedOpacity;
        if(flag(node["allowsGroupOpacity"],true)){if(ownOpacity!=1||inheritedOpacity!=1)push(nullptr,Matrix::Identity(),ownOpacity*inheritedOpacity);paintOpacity=1;}
        else paintOpacity*=ownOpacity;
        if(flag(node["masksToBounds"])&&!shadowSource) {auto geometry=boundsGeometry(node);push(geometry.Get(),Matrix::Identity(),1);}
        if((!root||options.includeRootMask)&&!shadowSource&&!node["mask"].isNull()){
            const auto& mask=node["mask"];Matrix transform;
            if(string(mask["kind"])=="shape"&&placement(mask,transform)&&mask["shape"]["strokeColor"].isNull()
               &&(mask["children"].isNull()||array(mask["children"],maximumNodes).empty())){
                auto geometry=path(mask["shape"]["path"],string(mask["shape"]["fillRule"]));D2D1_COLOR_F fill{};
                const float alpha=color(mask["shape"]["fillColor"],fill)?fill.a*std::clamp(real(mask["opacity"],1),0.f,1.f):0;
                push(geometry.Get(),transform,flag(mask["hidden"])?0:alpha);
            }else issue(result,node,"only local filled-shape alpha masks are implemented");
        }
        if(!shadowSource)drawShadow(target,node,world,pixels,options,result,paintOpacity,depth);
        // The source shell's leaf layers use geometryFlipped=false and
        // contentsAreFlipped=true under the flipped AppKit host. DWrite/WIC
        // already emit top-left rows; neither flag justifies a second bitmap
        // flip. Other CA conventions need a source pixel oracle before support.
        if(flag(node["geometryFlipped"]))issue(result,node,"flipped local layer geometry is not implemented; preserve it in the scene adapter");
        if(node.contains("contentsAreFlipped")&&!flag(node["contentsAreFlipped"]))
            issue(result,node,"unflipped Core Animation content convention is not implemented");
        if(string(node["cornerCurve"],"circular")!="circular")issue(result,node,"continuous corner curve is not implemented");
        if(real(node["cornerRadius"])>0&&number(node["maskedCorners"],15)!=15)issue(result,node,"selective rounded corners are not implemented");
        const auto className=string(node["class"],"CALayer");
        if(className!="CALayer"&&className!="CAShapeLayer"&&className!="CATextLayer"&&className!="CAGradientLayer")issue(result,node,"custom layer behavior is not implemented: "+className);
        if(!node["animationKeys"].isNull()&&!array(node["animationKeys"],1024).empty())issue(result,node,"model state only; presentation animation is owned by the scene");
        auto background=brush(target,node["backgroundColor"],paintOpacity);
        if(background){auto geometry=boundsGeometry(node);target->FillGeometry(geometry.Get(),background.Get());}
        const auto kind=string(node["kind"],"layer");
        if(kind=="shape"){
            const auto& shape=node["shape"];auto geometry=path(shape["path"],string(shape["fillRule"]));
            auto fill=brush(target,shape["fillColor"],paintOpacity);if(fill)target->FillGeometry(geometry.Get(),fill.Get());
            auto outline=brush(target,shape["strokeColor"],paintOpacity);const auto width=real(shape["lineWidth"],1);
            if(real(shape["strokeStart"])!=0||real(shape["strokeEnd"],1)!=1)issue(result,node,"partial stroke ranges are not implemented");
            else if(outline&&width>0){auto style=stroke(shape);target->DrawGeometry(geometry.Get(),outline.Get(),width,style.Get());}
        }else if(kind=="text")drawText(target,node,options,result,layouts,paintOpacity);
        else if(kind=="gradient"){
            const auto& g=node["gradient"];const auto type=string(g["type"],"axial");
            if(type!="axial"&&type!="radial")issue(result,node,"only axial and radial native gradients are implemented");
            else{
                const auto& colors=array(g["colors"],256);std::vector<D2D1_GRADIENT_STOP> stops;
                const bool hasLocations=!g["locations"].isNull();if(hasLocations&&array(g["locations"],256).size()!=colors.size())invalid("Gradient stop count mismatch");
                float previous=-1;
                for(std::size_t i=0;i<colors.size();++i){D2D1_COLOR_F c{};color(colors[i],c,paintOpacity);
                    const auto at=hasLocations?real(g["locations"].array()[i]):colors.size()>1?static_cast<float>(i)/static_cast<float>(colors.size()-1):0.f;
                    if(at<previous||at<0||at>1)invalid("Invalid gradient stop location");previous=at;stops.push_back({at,c});}
                if(!stops.empty()){
                    ComPtr<ID2D1GradientStopCollection> collection;checked(target->CreateGradientStopCollection(stops.data(),static_cast<UINT32>(stops.size()),D2D1_GAMMA_2_2,D2D1_EXTEND_MODE_CLAMP,collection.GetAddressOf()),"Create source gradient stops");
                    const auto a=point(g["startPoint"],{.5f,0}),b=point(g["endPoint"],{.5f,1});
                    const D2D1_POINT_2F start{static_cast<float>(bounds.x+a.x*bounds.width),static_cast<float>(bounds.y+a.y*bounds.height)};
                    ComPtr<ID2D1Brush> fill;
                    if(type=="axial"){ComPtr<ID2D1LinearGradientBrush> linear;checked(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(start,
                        {static_cast<float>(bounds.x+b.x*bounds.width),static_cast<float>(bounds.y+b.y*bounds.height)}),collection.Get(),linear.GetAddressOf()),"Create source gradient");fill=linear;}
                    else{
                        // CAGradientLayer radial: an ellipse centred on startPoint
                        // whose radii reach endPoint; the last colour continues past it.
                        const float rx=static_cast<float>(std::abs(b.x-a.x)*bounds.width),ry=static_cast<float>(std::abs(b.y-a.y)*bounds.height);
                        if(rx>0&&ry>0){ComPtr<ID2D1RadialGradientBrush> radial;checked(target->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(start,{0,0},rx,ry),collection.Get(),radial.GetAddressOf()),"Create source radial gradient");fill=radial;}
                        else issue(result,node,"degenerate radial gradient (startPoint and endPoint share an axis)");
                    }
                    if(fill){auto geometry=boundsGeometry(node);target->FillGeometry(geometry.Get(),fill.Get());}
                }
            }
        }else if(kind!="layer")issue(result,node,"unknown native layer kind: "+kind);
        if(!node["contents"].isNull()){
            const auto& decoded=image(node["contents"],options);
            // EndfieldGameIcon.image applies source-in AFTER source resizing.
            // Prepared opaque-tint icons retain that exact source alpha. A
            // preference event may recolor it without changing the pinned
            // image/geometry or the shared decoded cache. No pointer-frame work.
            auto bgra=decoded.premultipliedBGRA;std::vector<std::uint8_t>tinted;
            if(!node["contentsSourceInTint"].isNull()){
                D2D1_COLOR_F tint{};color(node["contentsSourceInTint"],tint);
                // CGContext's RGBA8 source-in first quantizes the source
                // color, then composites it with destination alpha. Direct
                // round(color*alpha) differs at faint icon edge pixels.
                const std::array<unsigned,4>ink{
                    static_cast<unsigned>(std::lround(double(tint.b)*tint.a*255)),
                    static_cast<unsigned>(std::lround(double(tint.g)*tint.a*255)),
                    static_cast<unsigned>(std::lround(double(tint.r)*tint.a*255)),
                    static_cast<unsigned>(std::lround(double(tint.a)*255))};
                tinted.resize(bgra.size());
                for(std::size_t at=0;at<bgra.size();at+=4){
                    const unsigned alpha=bgra[at+3];
                    for(unsigned c=0;c<4;++c)tinted[at+c]=static_cast<std::uint8_t>((ink[c]*alpha+127u)/255u);
                }
                bgra=tinted;
            }
            ComPtr<ID2D1Bitmap> bitmap;checked(target->CreateBitmap(D2D1::SizeU(decoded.width,decoded.height),bgra.data(),decoded.width*4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),bitmap.GetAddressOf()),"Create local intrinsic bitmap");
            auto destination=rect(bounds);const auto gravity=string(node["contentsGravity"],"resize");
            const auto crop=node["contentsRect"].isNull()?core::Rect{0,0,1,1}:rectangle(node["contentsRect"]);
            const auto center=node["contentsCenter"].isNull()?core::Rect{0,0,1,1}:rectangle(node["contentsCenter"]);
            if(center.x!=0||center.y!=0||center.width!=1||center.height!=1)issue(result,node,"nine-slice native contents are not implemented");
            const float sourceWidth=static_cast<float>(decoded.width*crop.width),sourceHeight=static_cast<float>(decoded.height*crop.height);
            if(gravity=="resizeAspect"||gravity=="resizeAspectFill"){
                const float sx=static_cast<float>(bounds.width)/std::max(1.f,sourceWidth),sy=static_cast<float>(bounds.height)/std::max(1.f,sourceHeight);
                const float scale=gravity=="resizeAspect"?std::min(sx,sy):std::max(sx,sy);
                const float w=sourceWidth*scale,h=sourceHeight*scale,x=static_cast<float>(bounds.x)+(static_cast<float>(bounds.width)-w)/2,y=static_cast<float>(bounds.y)+(static_cast<float>(bounds.height)-h)/2;
                destination=D2D1::RectF(x,y,x+w,y+h);
            }else if(gravity!="resize")issue(result,node,"unsupported intrinsic image gravity: "+gravity);
            const auto source=D2D1::RectF(static_cast<float>(decoded.width*crop.x),static_cast<float>(decoded.height*crop.y),
                static_cast<float>(decoded.width*(crop.x+crop.width)),static_cast<float>(decoded.height*(crop.y+crop.height)));
            target->DrawBitmap(bitmap.Get(),&destination,paintOpacity,string(node["magnificationFilter"],"linear")=="nearest"?D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR:D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,&source);
        }
        Matrix sublayer;
        if(!childTransform(node,sublayer))issue(result,node,"projective child transform must remain in the GPU scene");
        else for(auto child:children(node)){
            if(flag((*child)["hidden"])||real((*child)["opacity"],1)<=0)continue;
            Matrix local;if(!placement(*child,local)){issue(result,*child,"projective child placement must remain in the GPU scene");continue;}
            draw(target,*child,local*sublayer*world,pixels,options,result,layouts,paintOpacity,visited,depth+1,false);
        }
        target->SetTransform(world*pixels);
        const auto borderWidth=real(node["borderWidth"]);auto border=brush(target,node["borderColor"],paintOpacity);
        if(border&&borderWidth>0){
            // CALayer borders lie inside the bounds; a D2D stroke is centered
            // on its geometry. Inset that geometry instead of growing the card.
            const float inset=std::min(borderWidth/2,static_cast<float>(std::min(bounds.width,bounds.height))/2);
            const auto edge=D2D1::RectF(static_cast<float>(bounds.x)+inset,static_cast<float>(bounds.y)+inset,
                static_cast<float>(bounds.x+bounds.width)-inset,static_cast<float>(bounds.y+bounds.height)-inset);
            const float radius=std::max(0.f,real(node["cornerRadius"])-inset);
            target->DrawRoundedRectangle(D2D1::RoundedRect(edge,radius,radius),border.Get(),inset*2);
        }
        target->SetTransform(world*pixels);while(pushes>0){--pushes;target->PopLayer();}
    }
    // A local raster's padded bounds and pixel size (rasterize and repaint).
    void measureRaster(const Json& layer,const LayerRasterOptions& options,LayerRasterImage& result){
        std::size_t visited=0;auto bounds=measure(layer,Matrix::Identity(),result,visited,0);
        bounds.x-=options.paddingPoints;bounds.y-=options.paddingPoints;bounds.width+=options.paddingPoints*2;bounds.height+=options.paddingPoints*2;
        const double w=std::ceil(std::max(1.0/options.pixelsPerPoint,bounds.width)*options.pixelsPerPoint),h=std::ceil(std::max(1.0/options.pixelsPerPoint,bounds.height)*options.pixelsPerPoint);
        if(w>8192||h>8192||w*h>maximumPixels)invalid("Local raster exceeds its pixel limit");
        result.width=static_cast<unsigned>(w);result.height=static_cast<unsigned>(h);bounds.width=w/options.pixelsPerPoint;bounds.height=h/options.pixelsPerPoint;result.bounds=bounds;
    }
    // Paints a measured raster into a new premultiplied BGRA bitmap.
    ComPtr<IWICBitmap> paintRaster(const Json& layer,const LayerRasterOptions& options,LayerRasterImage& result,std::vector<ComPtr<IDWriteTextLayout>>& layouts){
        ComPtr<IWICBitmap> bitmap;checked(wic->CreateBitmap(result.width,result.height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf()),"Create bounded local bitmap");
        ComPtr<ID2D1RenderTarget> target;checked(d2d->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),target.GetAddressOf()),"Create offscreen local layer target");
        target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target->BeginDraw();target->Clear(D2D1::ColorF(0.f,0.f,0.f,0.f));
        const auto&bounds=result.bounds;
        const Matrix pixels=Matrix::Translation(static_cast<float>(-bounds.x),static_cast<float>(-bounds.y))*Matrix::Scale(static_cast<float>(options.pixelsPerPoint),static_cast<float>(options.pixelsPerPoint));
        std::size_t visited=0;draw(target.Get(),layer,Matrix::Identity(),pixels,options,result,layouts,1,visited,0,true);
        checked(target->EndDraw(),"Complete local layer raster");
        return bitmap;
    }
    // The bitmap's tight straight RGBA rows, converted in place (no second copy).
    static void straightPixels(IWICBitmap* bitmap,unsigned width,unsigned height,std::vector<std::uint8_t>& out){
        const auto bytes=std::size_t(width)*height*4;out.resize(bytes);
        checked(bitmap->CopyPixels(nullptr,width*4,static_cast<UINT>(bytes),out.data()),"Copy owned local bitmap");
        for(std::size_t i=0;i<bytes;i+=4){const unsigned b=out[i],g=out[i+1],r=out[i+2],a=out[i+3];
            out[i]=a?static_cast<std::uint8_t>(std::min(255u,(r*255u+a/2)/a)):0;
            out[i+1]=a?static_cast<std::uint8_t>(std::min(255u,(g*255u+a/2)/a)):0;
            out[i+2]=a?static_cast<std::uint8_t>(std::min(255u,(b*255u+a/2)/a)):0;
        }
    }
    // A released image's pixels painted again from its recipe, with the
    // typography it was painted with. Statistics other than pixelRepaints
    // (and a decoded image the cache had to read again) stay as they were.
    void repaint(const LayerRasterImage& image,std::vector<std::uint8_t>& out){
        if(!image.recipe_)invalid("Only a releasable raster can be painted again");
        const auto& recipe=*image.recipe_;
        struct Restore{Impl& r;LayerFontLanguage language;ComPtr<IDWriteFontFallback> fallback;LayerRasterStats counts;bool memory,lineBox;
            ~Restore(){const auto decoded=r.counts.resourceBytes;r.language=language;r.fallback=std::move(fallback);r.counts=counts;r.counts.resourceBytes=decoded;r.memoryImageUsed=memory;r.lineBoxPainted=lineBox;}}
            restore{*this,language,fallback,counts,memoryImageUsed,lineBoxPainted};
        language=recipe.language;if(recipe.fallback)fallback=recipe.fallback;
        LayerRasterImage scratch;measureRaster(*recipe.layer,recipe.options,scratch);
        if(scratch.width!=image.width||scratch.height!=image.height||scratch.bounds!=image.bounds)throw std::logic_error("A released raster no longer paints its retained bounds");
        std::vector<ComPtr<IDWriteTextLayout>> layouts;const auto bitmap=paintRaster(*recipe.layer,recipe.options,scratch,layouts);
        straightPixels(bitmap.Get(),image.width,image.height,out);
        restore.counts.pixelRepaints++;
    }
    std::shared_ptr<const LayerRasterImage> raster(std::string id,std::uint64_t revision,const Json& layer,const LayerRasterOptions& options,bool releasable,std::shared_ptr<const Recipe> reuse);
};

struct LayerPlainTextAnalysis::Impl {
    DWORD thread{GetCurrentThreadId()};
    ComPtr<IDWriteFactory> factory;ComPtr<IDWriteTextFormat> format;
    LayerPlainTextMetrics metrics;
    std::vector<DWRITE_LINE_METRICS> lines;
    std::vector<std::uint32_t> lengths;
    std::vector<LayerStyledTextLine> styledLines;
    std::function<ResolvedFont(const Json&)> resolveFont;
    DWRITE_FONT_WEIGHT(*fontWeight)(const Json&,bool standard){};
    std::uint64_t created{};
    void onThread()const{if(GetCurrentThreadId()!=thread)throw std::logic_error("Plain text analysis used outside its creating thread");}
};
LayerPlainTextAnalysis::LayerPlainTextAnalysis(std::unique_ptr<Impl>impl):impl_(std::move(impl)){}
LayerPlainTextAnalysis::~LayerPlainTextAnalysis()=default;
const LayerPlainTextMetrics&LayerPlainTextAnalysis::metrics()const noexcept{return impl_->metrics;}
std::uint64_t LayerPlainTextAnalysis::layoutsCreated()const noexcept{return impl_->created;}
std::span<const std::uint32_t>LayerPlainTextAnalysis::lineLengths(std::u16string_view value,double width,std::size_t maximumLines){
    auto&r=*impl_;r.onThread();
    if(value.size()>std::numeric_limits<UINT32>::max()||!std::isfinite(width)||std::abs(width)>32768
       ||maximumLines==0||maximumLines>std::numeric_limits<UINT32>::max())invalid("Plain text analysis exceeds explicit bounds");
    for(std::size_t i=0;i<value.size();++i){const auto c=value[i];if(c>=0xd800&&c<=0xdbff){if(++i==value.size()||value[i]<0xdc00||value[i]>0xdfff)invalid("Plain text has an unmatched UTF-16 surrogate");}else if(c>=0xdc00&&c<=0xdfff)invalid("Plain text has an unmatched UTF-16 surrogate");}
    r.lengths.clear();
    ComPtr<IDWriteTextLayout>layout;
    checked(r.factory->CreateTextLayout(value.empty()?L"":reinterpret_cast<const WCHAR*>(value.data()),static_cast<UINT32>(value.size()),r.format.Get(),
        static_cast<float>(std::max(1.,width)),1e8f,layout.GetAddressOf()),"Create transient plain text analysis");++r.created;
    UINT32 needed{};const auto probe=layout->GetLineMetrics(nullptr,0,&needed);
    if(FAILED(probe)&&probe!=HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))checked(probe,"Count plain text lines");
    if(needed==0||needed>maximumLines||std::size_t(needed)>value.size()+1)invalid("Plain text line index exceeds its explicit budget");
    r.lines.resize(needed);UINT32 actual{};checked(layout->GetLineMetrics(r.lines.data(),needed,&actual),"Read plain text line lengths");
    if(actual!=needed)invalid("Plain text line metrics changed during immutable analysis");
    r.lengths.reserve(needed);std::size_t total{};
    for(const auto&line:r.lines){if(line.isTrimmed||line.length>value.size()-total)invalid("Plain text analysis returned invalid or truncated lines");r.lengths.push_back(line.length);total+=line.length;}
    if(total!=value.size())invalid("Plain text analysis did not cover the complete input");
    // No full-height glyph image and no retained document layout. This span is
    // just a compact UTF-16 line-length scratch buffer.
    return r.lengths;
}

std::span<const LayerStyledTextLine>LayerPlainTextAnalysis::richLines(std::u16string_view value,double width,std::span<const core::notes::TextRun>runs,std::size_t maximumLines){
    auto&r=*impl_;r.onThread();
    if(value.size()>std::numeric_limits<UINT32>::max()||!std::isfinite(width)||std::abs(width)>32768||maximumLines==0||maximumLines>std::numeric_limits<UINT32>::max()||runs.size()>core::notes::RichText::maximumRuns)invalid("Rich text analysis exceeds explicit bounds");
    for(std::size_t i=0;i<value.size();++i){const auto c=value[i];if(c>=0xd800&&c<=0xdbff){if(++i==value.size()||value[i]<0xdc00||value[i]>0xdfff)invalid("Rich text has an unmatched surrogate");}else if(c>=0xdc00&&c<=0xdfff)invalid("Rich text has an unmatched surrogate");}
    std::uint64_t previous{};for(const auto&run:runs){const std::uint64_t end=std::uint64_t(run.location)+run.length;if(!run.style.valid()||run.length==0||run.location<previous||end>value.size())invalid("Invalid rich analysis run");previous=end;}
    ComPtr<IDWriteTextLayout>layout;checked(r.factory->CreateTextLayout(value.empty()?L"":reinterpret_cast<const WCHAR*>(value.data()),static_cast<UINT32>(value.size()),r.format.Get(),static_cast<float>(std::max(1.,width)),1e8f,layout.GetAddressOf()),"Create attributed Notes analysis");++r.created;
    checked(layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_DEFAULT,0,0),"Use attributed line metrics");
    for(const auto&run:runs){const auto font=notesRunFont(run.style);const auto family=r.resolveFont(font);const DWRITE_TEXT_RANGE range{run.location,run.length};
        checked(layout->SetFontFamilyName(family.family.c_str(),range),"Set analyzed run family");checked(layout->SetFontCollection(family.collection,range),"Set analyzed run collection");checked(layout->SetFontSize(static_cast<float>(run.style.fontSize),range),"Set analyzed run size");
        checked(layout->SetFontWeight(r.fontWeight(font,family.standard),range),"Set analyzed run weight");checked(layout->SetFontStyle(run.style.italic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,range),"Set analyzed run style");}
    UINT32 needed{};const auto probe=layout->GetLineMetrics(nullptr,0,&needed);if(FAILED(probe)&&probe!=HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))checked(probe,"Count attributed Notes lines");
    if(needed==0||needed>maximumLines||std::size_t(needed)>value.size()+1)invalid("Rich text line index exceeds its budget");r.lines.resize(needed);UINT32 actual{};checked(layout->GetLineMetrics(r.lines.data(),needed,&actual),"Read attributed Notes lines");if(actual!=needed)invalid("Attributed line metrics changed");
    r.styledLines.clear();r.styledLines.reserve(needed);std::size_t total{};for(const auto&line:r.lines){if(line.isTrimmed||line.length>value.size()-total||!std::isfinite(line.height)||line.height<=0)invalid("Invalid attributed line metrics");r.styledLines.push_back({line.length,line.length?std::max(1.,std::ceil(double(line.height))+1):r.metrics.lineHeight});total+=line.length;}
    if(total!=value.size())invalid("Attributed analysis did not cover complete text");return r.styledLines;
}

LayerRasterizer::LayerRasterizer():LayerRasterizer(std::filesystem::path{}){}
LayerRasterizer::LayerRasterizer(const std::filesystem::path&fontDirectory):impl_(std::make_unique<Impl>(fontDirectory)){}
LayerRasterizer::~LayerRasterizer()=default;
bool LayerRasterizer::setDefaultFontLanguage(LayerFontLanguage language){auto&r=*impl_;r.onThread();if(std::find(core::textRegions.begin(),core::textRegions.end(),language)==core::textRegions.end())invalid("Invalid default font language");if(r.language==language)return false;if(r.fontRevision==std::numeric_limits<std::uint64_t>::max())invalid("Typography generation exhausted");
    auto cascade=r.cascadeFor(language);r.fallback=std::move(cascade);r.language=language;++r.fontRevision;return true;}
std::span<const std::string> LayerRasterizer::bundledFontFamilies()const{impl_->onThread();return impl_->bundledNames;}
std::string LayerRasterizer::regionFontFamily(LayerFontLanguage language)const{impl_->onThread();return impl_->regionFamily(language);}
LayerFontLanguage LayerRasterizer::fontLanguage()const{impl_->onThread();return impl_->language;}
std::uint64_t LayerRasterizer::fontRevision()const{impl_->onThread();return impl_->fontRevision;}
std::string_view LayerRasterizer::defaultFontFamily()const{impl_->onThread();return impl_->defaultFamily();}
LayerFontResources LayerRasterizer::retainedFontResources()const{auto&r=*impl_;r.onThread();return LayerFontResources(r.text.Get(),r.fonts.Get(),r.bundledFonts.Get(),r.fallback.Get(),r.defaultFamily(),std::string(core::regionTag(r.language)),r.fontRevision);}
std::unique_ptr<LayerPlainTextAnalysis>LayerRasterizer::plainSystemTextAnalysis(const std::string&id,double size,const LayerRasterOptions&options){
    auto&r=*impl_;r.onThread();
    if(id.empty()||id.size()>4096||!Json::validUtf8(id)||!std::isfinite(size)||size<=0||size>2048)invalid("Invalid plain system font request");
    const Json font=Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".SFNS-Regular"}};
    const Json node=Json::Object{{"id",id}};LayerRasterImage report;
    const auto family=r.fontFamily(font,node,options,report);
    // NotesTextMetrics.lineHeight: the standard face's own (hhea) metrics.
    const auto natural=r.lineMetrics(family,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL);
    auto analysis=std::make_unique<LayerPlainTextAnalysis::Impl>();analysis->factory=r.text;
    auto&m=analysis->metrics;
    m.fontSize=size;m.ascent=natural[0]*size;m.descent=natural[1]*size;
    m.leading=std::max(0.,natural[2]*size);m.lineHeight=std::ceil(m.ascent+m.descent+m.leading)+1;
    m.selectedFamily=r.fallbackFamily(options);m.fontSubstitutions=std::move(report.fontSubstitutions);
    if(m.fontSubstitutions.empty())m.selectedFamily=".AppleSystemUIFont";
    checked(r.text->CreateTextFormat(family.family.c_str(),family.collection,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,
        static_cast<float>(size),r.locale(),analysis->format.GetAddressOf()),"Create shared plain system format");r.cascade(analysis->format.Get());
    checked(analysis->format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP),"Set plain system wrapping");
    checked(analysis->format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,static_cast<float>(m.lineHeight),static_cast<float>(m.ascent)),"Set source-style plain line height");
    analysis->fontWeight=[](const Json&font,bool standard){return Impl::textWeight(Json{},font,standard);};auto* metrics=&analysis->metrics;auto*owner=&r;
    analysis->resolveFont=[owner,id,options,metrics](const Json&font){LayerRasterImage report;const Json node=Json::Object{{"id",id}};auto family=owner->fontFamily(font,node,options,report);
        for(auto&f:report.fontSubstitutions){const auto same=[&](const auto&existing){return existing.requestedFamily==f.requestedFamily&&existing.requestedFace==f.requestedFace&&existing.selectedFamily==f.selectedFamily;};if(std::none_of(metrics->fontSubstitutions.begin(),metrics->fontSubstitutions.end(),same))metrics->fontSubstitutions.push_back(std::move(f));}return family;};
    ++r.counts.textAnalysisFormatsCreated;
    return std::unique_ptr<LayerPlainTextAnalysis>(new LayerPlainTextAnalysis(std::move(analysis)));
}
const std::vector<std::string>&LayerRasterizer::installedFontFamilies(){
    auto&r=*impl_;r.onThread();if(r.installedFontNames)return *r.installedFontNames;
    std::vector<std::string> names;names.reserve(r.fonts->GetFontFamilyCount());
    for(UINT32 n=0;n<r.fonts->GetFontFamilyCount();++n){ComPtr<IDWriteFontFamily>family;ComPtr<IDWriteLocalizedStrings>localized;
        if(FAILED(r.fonts->GetFontFamily(n,&family))||FAILED(family->GetFamilyNames(&localized)))continue;
        UINT32 index{},length{};BOOL exists{};localized->FindLocaleName(L"en-us",&index,&exists);if(!exists)index=0;
        if(FAILED(localized->GetStringLength(index,&length))||length>1024)continue;
        std::wstring value(length+1,L'\0');if(FAILED(localized->GetString(index,value.data(),length+1)))continue;value.resize(length);if(value.empty())continue;
        const auto bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
        if(bytes<=0)invalid("Invalid installed font family name");std::string name(static_cast<std::size_t>(bytes),'\0');
        if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),name.data(),bytes,nullptr,nullptr)!=bytes)invalid("Cannot encode installed font family name");
        names.push_back(std::move(name));
    }
    names.insert(names.end(),r.bundledNames.begin(),r.bundledNames.end());std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());r.installedFontNames=std::move(names);return *r.installedFontNames;
}
LayerSourceTextMeasurement LayerRasterizer::measureSourceText(const std::string&id,const Json&descriptor,double width,const LayerRasterOptions&options){
    auto&r=*impl_;r.onThread();const bool box=lineBox(descriptor);
    if(id.empty()||id.size()>4096||!Json::validUtf8(id)||!descriptor.isObject()||!descriptor["string"].isString()||
       (!box&&!descriptor["wrapped"].isBool())||!std::isfinite(width)||width<=0||width>3e9)invalid("Invalid source caption measurement");
    if(!box&&!descriptor["runs"].isNull()&&(!descriptor["runs"].isArray()||!descriptor["runs"].array().empty()))invalid("Caption measurement does not flatten attributed runs");
    const auto value=wide(descriptor["string"].string());if(value.size()>65536)invalid("Caption measurement exceeds painted UTF-16 bound");
    const auto size=real(descriptor["fontSize"],box?12.f:0.f);if(size<=0||size>2048)invalid("Invalid caption measurement font size");
    const Json node=Json::Object{{"id",id},{"text",descriptor}};LayerRasterImage report;const auto&font=descriptor["font"];
    ++r.counts.textAnalysisFormatsCreated;LayerSourceTextMeasurement result;
    if(box){
        // A line box: HUDTextLayer.preferredWidth (the natural one-line width
        // at full size, rounded up to .5 as HUDText.width) and, at `width`,
        // the height of its lines and the shrink the raster will apply.
        if(!value.empty()){const auto text=r.lineBoxText(node,value,{0,0,width,0},options,report,nullptr,1);
            result.width=std::ceil(text.natural*2)/2;result.height=text.lineHeight*double(text.lines);result.scale=text.scale;result.lines=text.lines;}
    }
    const auto family=r.fontFamily(font,node,options,report);
    const auto strut=r.lineMetrics(family,Impl::textWeight(descriptor,font,family.standard),(static_cast<unsigned>(number(font["symbolicTraits"]))&1)?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL);
    result.ascent=strut[0]*size*result.scale;result.descent=strut[1]*size*result.scale;
    if(box){result.fontSubstitutions=std::move(report.fontSubstitutions);return result;}
    const bool wrapped=flag(descriptor["wrapped"]);const auto format=r.textFormat(descriptor,family,size,wrapped,DWRITE_TEXT_ALIGNMENT_LEADING);
    const auto ascent=real(font["ascender"]),descent=real(font["descender"]),leading=real(font["leading"]);
    if(ascent>0&&ascent-descent+leading>0)checked(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,ascent-descent+leading,ascent),"Preserve source caption measurement metrics");
    result.fontSubstitutions=std::move(report.fontSubstitutions);
    if(value.empty())return result;
    ComPtr<IDWriteTextLayout>layout;checked(r.text->CreateTextLayout(value.data(),static_cast<UINT32>(value.size()),format.Get(),static_cast<float>(width),
        std::numeric_limits<float>::max(),layout.GetAddressOf()),"Measure source caption with paint font resolver");++r.counts.textLayoutsCreated;
    r.textFeatures(layout.Get(),descriptor,{0,static_cast<UINT32>(value.size())},size);
    DWRITE_TEXT_METRICS metrics{};checked(layout->GetMetrics(&metrics),"Read measured source caption metrics");
    result.width=wrapped?metrics.width:metrics.widthIncludingTrailingWhitespace;result.height=metrics.height;result.lines=metrics.lineCount;
    if(!std::isfinite(result.width)||!std::isfinite(result.height)||result.width<0||result.height<0)invalid("Invalid measured source caption extent");
    return result;
}
std::shared_ptr<const LayerRasterImage> LayerRasterizer::rasterize(std::string id,std::uint64_t revision,const Json& layer,const LayerRasterOptions& options){
    return impl_->raster(std::move(id),revision,layer,options,false,{});
}
std::shared_ptr<const LayerRasterImage> LayerRasterizer::rasterize(std::string id,std::uint64_t revision,const Json& layer,const LayerRasterOptions& options,LayerRasterRetention retention){
    if(retention!=LayerRasterRetention::pixels&&retention!=LayerRasterRetention::releasable)invalid("Unknown raster retention");
    return impl_->raster(std::move(id),revision,layer,options,retention==LayerRasterRetention::releasable,{});
}
// `reuse`: the recipe of the entry being repainted for typography, whose
// layer is `layer` (no second copy of the description).
std::shared_ptr<const LayerRasterImage> LayerRasterizer::Impl::raster(std::string id,std::uint64_t revision,const Json& layer,const LayerRasterOptions& options,bool releasable,std::shared_ptr<const Recipe> reuse){
    auto& r=*this;r.onThread();
    if(id.empty()||id.size()>4096||!Json::validUtf8(id))invalid("Invalid retained layer ID");
    if(!std::isfinite(options.pixelsPerPoint)||options.pixelsPerPoint<.25||options.pixelsPerPoint>4
       ||!std::isfinite(options.paddingPoints)||options.paddingPoints<0||options.paddingPoints>64)invalid("Invalid local raster scale/padding");
    if(!std::isfinite(options.textDocumentOffset.x)||!std::isfinite(options.textDocumentOffset.y)||options.textDocumentOffset.x<0||options.textDocumentOffset.x>3e9||options.textDocumentOffset.y<0||options.textDocumentOffset.y>3e9)invalid("Invalid plain document offset");
    if(options.textDocumentOffset.x!=0&&!flag(layer["text"]["sourceSingleLineField"]))invalid("Horizontal document offset requires an explicit single-line field");
    if(options.plainTextDocument&&options.richTextDocument)invalid("Document must select one plain or rich mode");
    if((options.plainTextDocument||options.richTextDocument)&&string(layer["kind"])=="text"){
        if((!layer["children"].isNull()&&!array(layer["children"],maximumNodes).empty())||!layer["text"]["wrapped"].isBool()||string(layer["text"]["truncation"],"none")!="none"||(options.plainTextDocument&&!layer["text"]["runs"].isNull()&&!array(layer["text"]["runs"],4096).empty()))invalid("Document viewport requires one supported text leaf with explicit wrapping");
        if(options.revealPlainTextPosition&&*options.revealPlainTextPosition>65536)invalid("Invalid reveal ACP");
    }else if(options.retainedPlainText||options.revealPlainTextPosition||options.textDocumentOffset!=core::Point{})invalid("Retained document painting requires an explicit plain text leaf");
    const auto old=r.entries.find(id);
    if(options.retainedPlainText&&(old==r.entries.end()||old->second.paintedText!=options.retainedPlainText))invalid("Borrowed text layout must belong to this retained surface");
    if(options.retainedPlainText&&old->second.fontRevision!=r.fontRevision)invalid("Borrowed text layout has a different typography generation");
    if(options.retainedPlainText&&(old->second.options.fallbackFontFamily!=options.fallbackFontFamily||old->second.options.monospaceFallbackFontFamily!=options.monospaceFallbackFontFamily))invalid("Borrowed text layout has different font resolver options");
    if(old!=r.entries.end()&&old->second.revision==revision&&old->second.options==options&&(!old->second.hasText||old->second.fontRevision==r.fontRevision)){
        // The caller expects the pixels: a released image paints them again.
        auto&image=*old->second.image;if(image.pixelsReleased())repaint(image,image.straightRGBA);
        ++r.counts.cacheHits;return old->second.image;}
    if(old==r.entries.end()&&r.entries.size()>=maximumEntries)invalid("Retained layer cache is full; remove unused source IDs");
    auto result=std::make_shared<LayerRasterImage>();if(options.retainedPlainText)result->fontSubstitutions=old->second.image->fontSubstitutions;
    r.measureRaster(layer,options,*result);
    const auto bytes=result->pixelBytes(),previous=old==r.entries.end()?0:old->second.image->pixelBytes();
    if(bytes>maximumResourceBytes-(r.counts.resourceBytes-previous))invalid("Retained layer resource budget exceeded");
    std::vector<ComPtr<IDWriteTextLayout>> layouts;r.memoryImageUsed=false;r.lineBoxPainted=false;
    const auto bitmap=r.paintRaster(layer,options,*result,layouts);const bool memoryImage=r.memoryImageUsed;r.memoryImageUsed=false;
    // Image cache growth while drawing also counts against the same bound.
    if(bytes>maximumResourceBytes-(r.counts.resourceBytes-previous))invalid("Retained layer resource budget exceeded after image decode");
    straightPixels(bitmap.Get(),result->width,result->height,result->straightRGBA);
    std::shared_ptr<const PaintedTextLayout> painted;
    // Editor geometry currently follows the rectangular DWrite viewport only.
    // Keep rendering nonrectangular masks, but do not fabricate matching hits
    // for those surfaces until the caller supplies that exact clip topology.
    const bool simpleTextClip=(!options.includeRootMask||layer["mask"].isNull())
        &&(!flag(layer["masksToBounds"])||real(layer["cornerRadius"])==0);
    const bool textLeaf=!r.lineBoxPainted&&string(layer["kind"])=="text"&&(layer["children"].isNull()||array(layer["children"],maximumNodes).empty())&&simpleTextClip&&result->complete();
    if(textLeaf&&layouts.size()==1&&options.retainedPlainText)painted=options.retainedPlainText;
    else if(textLeaf&&layouts.size()==1){const auto value=wide(string(layer["text"]["string"]));std::u16string original;original.reserve(value.size());for(auto unit:value)original.push_back(static_cast<char16_t>(unit));
        painted=std::shared_ptr<const PaintedTextLayout>(new PaintedTextLayout(layouts[0].Get(),std::move(original),revision,rectangle(layer["bounds"]),(options.plainTextDocument||options.richTextDocument),layer["text"],result->textDocumentOffset,result->textContentInset,result->textDocumentWidth));}
    const auto oldMetadata=old==r.entries.end()||!old->second.paintedText?0:old->second.paintedText->metadataBytes();
    const auto newMetadata=painted?painted->metadataBytes():0;
    if(newMetadata>maximumTextMetadataBytes-(r.counts.textMetadataBytes-oldMetadata))invalid("Retained editor text metadata exceeds its bound");
    const bool hasText=containsText(layer);const bool document=options.plainTextDocument||options.richTextDocument;
    const auto oldDescriptors=old==r.entries.end()?0:old->second.descriptorBytes;
    // A text description is kept for typography refreshes, and doubles as
    // the recipe of a releasable image. Another releasable image keeps its
    // description only while it is smaller than its pixels.
    std::shared_ptr<const Recipe> source;std::size_t descriptorBytes{};
    releasable=releasable&&!document&&!options.retainedPlainText&&!memoryImage;
    if(hasText&&!document){
        const bool same=reuse&&reuse->layer.get()==&layer;
        descriptorBytes=same?reuse->bytes:retainedJsonBytes(layer);
        if(descriptorBytes>maximumTypographyDescriptorBytes-(r.counts.typographyDescriptorBytes-oldDescriptors))invalid("Retained typography descriptor budget exceeded");
        source=std::make_shared<const Recipe>(Recipe{same?reuse->layer:std::make_shared<const Json>(layer),options,r.language,descriptorBytes,r.fallback});
    }else if(releasable){std::size_t description{};
        if(jsonBytesWithin(layer,bytes,description))source=std::make_shared<const Recipe>(Recipe{std::make_shared<const Json>(layer),options,r.language,description,r.fallback});}
    if(releasable&&source)result->recipe_=source;else releasable=false;
    // Diagnostics: the creating owner (LayerRasterAttribution); a host-side
    // replacement keeps the owner that created the surface.
    std::string owner(LayerRasterAttribution::current());if(owner.empty()&&old!=r.entries.end())owner=std::move(old->second.owner);
    r.entries.insert_or_assign(std::move(id),Impl::Entry{revision,options,result,std::move(layouts),std::move(painted),r.fontRevision,hasText,hasText&&!document?std::move(source):nullptr,descriptorBytes,std::move(owner),releasable});
    r.counts.typographyDescriptorBytes=r.counts.typographyDescriptorBytes-oldDescriptors+descriptorBytes;
    r.counts.resourceBytes=r.counts.resourceBytes-previous+bytes;r.counts.textMetadataBytes=r.counts.textMetadataBytes-oldMetadata+newMetadata;++r.counts.rasterizations;return result;
}
bool LayerRasterizer::releasePixels(const std::string&id,const std::shared_ptr<const LayerRasterImage>&image){
    auto&r=*impl_;r.onThread();
    if(!image||!image->recipe_||image->straightRGBA.empty())return false;
    // Nobody but the cache entry and the caller may still read these pixels.
    const auto found=r.entries.find(id);const bool cached=found!=r.entries.end()&&found->second.image.get()==image.get();
    if(image.use_count()!=(cached?2:1))return false;
    // Created non-const by this rasterizer (make_shared<LayerRasterImage>).
    std::vector<std::uint8_t>().swap(const_cast<LayerRasterImage&>(*image).straightRGBA);
    ++r.counts.pixelReleases;return true;
}
void LayerRasterizer::repaint(const LayerRasterImage&image,std::vector<std::uint8_t>&straightRGBA){
    auto&r=*impl_;r.onThread();
    if(&straightRGBA==&image.straightRGBA)invalid("Repaint into a separate buffer");
    r.repaint(image,straightRGBA);
}
std::shared_ptr<const PaintedTextLayout>LayerRasterizer::textLayout(const std::string&id,std::uint64_t revision)const{const auto&r=*impl_;r.onThread();const auto found=r.entries.find(id);return found!=r.entries.end()&&found->second.revision==revision?found->second.paintedText:nullptr;}
std::shared_ptr<const LayerRasterImage>LayerRasterizer::refreshTypography(const std::string&id,std::uint64_t revision){auto&r=*impl_;r.onThread();const auto it=r.entries.find(id);if(it==r.entries.end()||!it->second.source)return{};
    // Held here: the entry (and its recipe) is replaced while it paints.
    const auto source=it->second.source;const bool releasable=it->second.releasable;return r.raster(id,revision,*source->layer,source->options,releasable,source);}
bool LayerRasterizer::remove(const std::string& id){auto& r=*impl_;r.onThread();const auto it=r.entries.find(id);if(it==r.entries.end())return false;r.counts.resourceBytes-=it->second.image->pixelBytes();if(it->second.paintedText)r.counts.textMetadataBytes-=it->second.paintedText->metadataBytes();r.counts.typographyDescriptorBytes-=it->second.descriptorBytes;r.entries.erase(it);return true;}
void LayerRasterizer::clear(){auto& r=*impl_;r.onThread();r.entries.clear();r.images.clear();r.counts.resourceBytes=0;r.counts.textMetadataBytes=0;r.counts.typographyDescriptorBytes=0;}
void LayerRasterizer::forEachEntry(const std::function<void(const LayerRasterEntryUsage&)>&visit)const{const auto&r=*impl_;r.onThread();
    for(const auto&[id,e]:r.entries)visit({id,e.owner,e.image?e.image->straightRGBA.capacity():0,e.layouts.size(),e.descriptorBytes,e.paintedText?e.paintedText->metadataBytes():0,e.paintedText!=nullptr,
        e.image&&e.image->pixelsReleased()?e.image->pixelBytes():0});}
std::size_t LayerRasterizer::decodedImageBytes()const{const auto&r=*impl_;r.onThread();std::size_t bytes{};for(const auto&[_,image]:r.images)bytes+=image.premultipliedBGRA.capacity();return bytes;}
namespace {thread_local std::string_view rasterAttribution;}
LayerRasterAttribution::LayerRasterAttribution(std::string_view owner)noexcept:previous_(rasterAttribution){rasterAttribution=owner;}
LayerRasterAttribution::~LayerRasterAttribution(){rasterAttribution=previous_;}
std::string_view LayerRasterAttribution::current()noexcept{return rasterAttribution;}
LayerRasterStats LayerRasterizer::stats()const{const auto& r=*impl_;r.onThread();auto result=r.counts;result.entries=r.entries.size();result.decodedImages=r.images.size();
    for(const auto&[id,e]:r.entries){(void)id;if(e.image->pixelsReleased())result.releasedPixelBytes+=e.image->pixelBytes();if(e.image->recipe_&&!e.hasText)result.repaintSourceBytes+=e.image->recipe_->bytes;}
    return result;}
} // namespace endfield::native
#endif
