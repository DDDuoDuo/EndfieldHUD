#include "native/layer_raster.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_1.h>
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

struct LayerRasterizer::Impl {
    struct Entry { std::uint64_t revision;LayerRasterOptions options;std::shared_ptr<LayerRasterImage> image;std::vector<ComPtr<IDWriteTextLayout>> layouts; };
    struct DecodedImage { unsigned width{},height{};std::vector<std::uint8_t> premultipliedBGRA; };
    DWORD thread=GetCurrentThreadId();
    ComPtr<ID2D1Factory> d2d;ComPtr<IDWriteFactory> text;ComPtr<IDWriteFontCollection> fonts;ComPtr<IWICImagingFactory> wic;
    BCRYPT_ALG_HANDLE sha{};
    std::map<std::string,Entry,std::less<>> entries;
    std::map<std::string,DecodedImage,std::less<>> images;
    LayerRasterStats counts;
    Impl(){
        checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf()),"Create local D2D factory");
        checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(text.GetAddressOf())),"Create retained DirectWrite factory");
        checked(text->GetSystemFontCollection(fonts.GetAddressOf(),FALSE),"Read local font collection");
        checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.GetAddressOf())),"Create local WIC factory");
        if(BCryptOpenAlgorithmProvider(&sha,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot initialize asset hash verification");
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
        if(flag(node["masksToBounds"]))return measured;
        Matrix sublayer;if(!childTransform(node,sublayer)){issue(result,node,"projective child transform must remain in the GPU scene");return measured;}
        for(auto child:children(node)){
            if(flag((*child)["hidden"])||real((*child)["opacity"],1)<=0)continue;
            Matrix local;if(!placement(*child,local)){issue(result,*child,"projective child placement must remain in the GPU scene");continue;}
            measured=unite(measured,measure(*child,local*sublayer*world,result,visited,depth+1));
        }
        return measured;
    }
    std::wstring fontFamily(const Json& font,const Json& node,const LayerRasterOptions& options,LayerRasterImage& result){
        const auto family=string(font["familyName"],string(font["postScriptName"]));
        UINT32 index{};BOOL exists=FALSE;auto selected=wide(family);
        if(!selected.empty())checked(fonts->FindFamilyName(selected.c_str(),&index,&exists),"Resolve source font family");
        if(!exists){
            selected=wide(options.fallbackFontFamily);checked(fonts->FindFamilyName(selected.c_str(),&index,&exists),"Resolve explicit fallback font");
            if(!exists)invalid("Configured fallback font is not installed");
            LayerFontSubstitution fallback{string(node["id"],"unnamed"),family,string(font["postScriptName"]),options.fallbackFontFamily};
            if(std::none_of(result.fontSubstitutions.begin(),result.fontSubstitutions.end(),[&](const auto& f){return f.node==fallback.node&&f.requestedFamily==fallback.requestedFamily&&f.requestedFace==fallback.requestedFace;}))result.fontSubstitutions.push_back(std::move(fallback));
        }
        return selected;
    }
    static DWRITE_FONT_WEIGHT weight(const Json& font){
        const auto traits=static_cast<unsigned>(number(font["symbolicTraits"]));const auto name=string(font["postScriptName"]);
        if(name.find("Semibold")!=std::string::npos||name.find("SemiBold")!=std::string::npos)return DWRITE_FONT_WEIGHT_SEMI_BOLD;
        if((traits&2)||name.find("Bold")!=std::string::npos)return DWRITE_FONT_WEIGHT_BOLD;
        if(name.find("Medium")!=std::string::npos)return DWRITE_FONT_WEIGHT_MEDIUM;
        if(name.find("Light")!=std::string::npos)return DWRITE_FONT_WEIGHT_LIGHT;
        return DWRITE_FONT_WEIGHT_NORMAL;
    }
    const DecodedImage& image(const Json& contents,const LayerRasterOptions& options){
        const auto expected=string(contents["sha256"]),relative=string(contents["asset"]);
        if(expected.size()!=64||expected.find_first_not_of("0123456789abcdef")!=std::string::npos)invalid("Intrinsic image requires SHA-256");
        const auto path=std::filesystem::path(wide(relative));
        if(options.assetRoot.empty()||path.is_absolute()||path.has_root_name()||relative.find('\\')!=std::string::npos||relative.find(':')!=std::string::npos)invalid("Image needs a confined explicit fixture path");
        for(const auto& part:path)if(part==L".."||part==L".")invalid("Image path traversal rejected");
        if(const auto it=images.find(expected);it!=images.end())return it->second;
        if(images.size()>=256)invalid("Decoded intrinsic image cache is full; clear unused content");
        const auto root=std::filesystem::canonical(options.assetRoot),file=std::filesystem::canonical(root/path);
        auto r=root.begin(),f=file.begin();for(;r!=root.end();++r,++f)if(f==file.end()||*r!=*f)invalid("Image path escapes fixture root");
        const auto size=std::filesystem::file_size(file);if(size==0||size>64*1024*1024)invalid("Intrinsic image exceeds its file bound");
        std::ifstream input(file,std::ios::binary);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if(!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("Cannot read explicit intrinsic image");
        std::array<UCHAR,32> hash{};if(BCryptHash(sha,nullptr,0,bytes.data(),static_cast<ULONG>(bytes.size()),hash.data(),static_cast<ULONG>(hash.size()))<0)throw std::runtime_error("Cannot verify intrinsic image hash");
        constexpr char hex[]="0123456789abcdef";std::string digest;for(auto v:hash){digest+=hex[v>>4];digest+=hex[v&15];}if(digest!=expected)invalid("Intrinsic image hash mismatch");
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
        counts.resourceBytes+=byteCount;++counts.imageDecodes;return images.emplace(expected,std::move(decoded)).first->second;
    }
    ComPtr<ID2D1SolidColorBrush> brush(ID2D1RenderTarget* target,const Json& value,float alpha=1){
        D2D1_COLOR_F c{};ComPtr<ID2D1SolidColorBrush> result;if(color(value,c,alpha))checked(target->CreateSolidColorBrush(c,result.GetAddressOf()),"Create source color brush");return result;
    }
    void drawText(ID2D1RenderTarget* target,const Json& node,const LayerRasterOptions& options,LayerRasterImage& result,
                  std::vector<ComPtr<IDWriteTextLayout>>& layouts,float opacity){
        const auto& descriptor=node["text"];const auto bounds=rectangle(node["bounds"]);const auto value=wide(string(descriptor["string"]));
        if(value.empty()||bounds.width<=0||bounds.height<=0)return;
        if(value.size()>65536)invalid("Text layout exceeds its UTF-16 limit");
        const auto family=fontFamily(descriptor["font"],node,options,result);const auto size=real(descriptor["fontSize"],12);
        if(size<=0||size>2048)invalid("Text size exceeds its range");
        const auto italic=(static_cast<unsigned>(number(descriptor["font"]["symbolicTraits"]))&1)!=0;
        ComPtr<IDWriteTextFormat> format;checked(text->CreateTextFormat(family.c_str(),fonts.Get(),weight(descriptor["font"]),
            italic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",format.GetAddressOf()),"Create source text format");
        const auto alignment=string(descriptor["alignment"],"left");
        checked(format->SetTextAlignment(alignment=="center"?DWRITE_TEXT_ALIGNMENT_CENTER:alignment=="right"?DWRITE_TEXT_ALIGNMENT_TRAILING:
            alignment=="justified"?DWRITE_TEXT_ALIGNMENT_JUSTIFIED:DWRITE_TEXT_ALIGNMENT_LEADING),"Set text alignment");
        checked(format->SetWordWrapping(flag(descriptor["wrapped"])?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP),"Set text wrapping");
        const auto ascent=real(descriptor["font"]["ascender"]),descent=real(descriptor["font"]["descender"]),leading=real(descriptor["font"]["leading"]);
        if(ascent>0&&ascent-descent+leading>0)checked(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,ascent-descent+leading,ascent),"Preserve source line metrics");
        const auto truncation=string(descriptor["truncation"],"none");
        if(truncation=="end"){
            ComPtr<IDWriteInlineObject> ellipsis;checked(text->CreateEllipsisTrimmingSign(format.Get(),ellipsis.GetAddressOf()),"Create explicit text ellipsis");
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};checked(format->SetTrimming(&trimming,ellipsis.Get()),"Set text trimming");
        }else if(truncation!="none")issue(result,node,"unsupported text truncation: "+truncation);
        ComPtr<IDWriteTextLayout> layout;checked(text->CreateTextLayout(value.data(),static_cast<UINT32>(value.size()),format.Get(),
            static_cast<float>(bounds.width),static_cast<float>(bounds.height),layout.GetAddressOf()),"Create retained source text layout");++counts.textLayoutsCreated;
        if(!descriptor["runs"].isNull())for(const auto& run:array(descriptor["runs"],4096)){
            const auto& r=array(run["utf16Range"],2);if(r.size()!=2)invalid("Invalid UTF-16 run range");
            const double start=number(r[0]),length=number(r[1]);if(start<0||length<0||std::floor(start)!=start||std::floor(length)!=length||start+length>value.size())invalid("Text run exceeds UTF-16 string");
            DWRITE_TEXT_RANGE range{static_cast<UINT32>(start),static_cast<UINT32>(length)};const auto& attributes=run["attributes"];
            if(!attributes.isObject())invalid("Text attributes must be an object");
            for(const auto& [key,item]:attributes.object()){
                if(key=="NSFont"){
                    const auto f=fontFamily(item,node,options,result);checked(layout->SetFontFamilyName(f.c_str(),range),"Set run font family");
                    const auto runSize=real(item["pointSize"],size);if(runSize<=0||runSize>2048)invalid("Run text size exceeds its range");
                    checked(layout->SetFontSize(runSize,range),"Set run font size");checked(layout->SetFontWeight(weight(item),range),"Set run font weight");
                    checked(layout->SetFontStyle((static_cast<unsigned>(number(item["symbolicTraits"]))&1)?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,range),"Set run font style");
                }else if(key=="NSColor") {auto b=brush(target,item,opacity);if(b)checked(layout->SetDrawingEffect(b.Get(),range),"Set run foreground color");}
                else if(key=="NSUnderline"||key=="NSStrikethrough"){
                    const auto style=number(item);if(style!=0&&style!=1)issue(result,node,"unsupported decorated text style: "+key);
                    if(key=="NSUnderline")checked(layout->SetUnderline(style!=0,range),"Set underline");else checked(layout->SetStrikethrough(style!=0,range),"Set strikethrough");
                }else if(key=="NSKern"){
                    ComPtr<IDWriteTextLayout1> advanced;checked(layout.As(&advanced),"Read text spacing interface");checked(advanced->SetCharacterSpacing(0,real(item),0,range),"Set source tracking");
                }else if(key=="NSBaselineOffset"||key=="NSObliqueness"||key=="NSExpansion"||key=="NSStrokeWidth"){
                    if(number(item)!=0)issue(result,node,"unsupported text run effect: "+key);
                }else if(key=="NSLigature") {if(number(item)!=1)issue(result,node,"nondefault text ligature setting");}
                else issue(result,node,"unsupported text attribute: "+key);
            }
        }
        auto foreground=brush(target,descriptor["foregroundColor"],opacity);
        if(foreground)target->DrawTextLayout(D2D1::Point2F(static_cast<float>(bounds.x),static_cast<float>(bounds.y)),layout.Get(),foreground.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
        layouts.push_back(std::move(layout));
    }
    void draw(ID2D1RenderTarget* target,const Json& node,const Matrix& world,const Matrix& pixels,
              const LayerRasterOptions& options,LayerRasterImage& result,std::vector<ComPtr<IDWriteTextLayout>>& layouts,
              float inheritedOpacity,std::size_t& visited,unsigned depth,bool root){
        if(++visited>maximumNodes||depth>64)invalid("Local layer tree exceeds its bound");
        if(flag(node["hidden"]))return;
        const auto ownOpacity=root&&!options.includeRootOpacity?1.f:std::clamp(real(node["opacity"],1),0.f,1.f);
        if(ownOpacity<=0||inheritedOpacity<=0)return;
        ++counts.nodesDrawn;const auto bounds=rectangle(node["bounds"]);target->SetTransform(world*pixels);
        unsigned pushes=0;
        auto push=[&](ID2D1Geometry* geometry,const Matrix& maskTransform,float opacity){
            target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),geometry,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,maskTransform,opacity,nullptr,D2D1_LAYER_OPTIONS_NONE),nullptr);++pushes;
        };
        float paintOpacity=inheritedOpacity;
        if(flag(node["allowsGroupOpacity"],true)){if(ownOpacity!=1||inheritedOpacity!=1)push(nullptr,Matrix::Identity(),ownOpacity*inheritedOpacity);paintOpacity=1;}
        else paintOpacity*=ownOpacity;
        if(flag(node["masksToBounds"])) {auto geometry=boundsGeometry(node);push(geometry.Get(),Matrix::Identity(),1);}
        if((!root||options.includeRootMask)&&!node["mask"].isNull()){
            const auto& mask=node["mask"];Matrix transform;
            if(string(mask["kind"])=="shape"&&placement(mask,transform)&&mask["shape"]["strokeColor"].isNull()
               &&(mask["children"].isNull()||array(mask["children"],maximumNodes).empty())){
                auto geometry=path(mask["shape"]["path"],string(mask["shape"]["fillRule"]));D2D1_COLOR_F fill{};
                const float alpha=color(mask["shape"]["fillColor"],fill)?fill.a*std::clamp(real(mask["opacity"],1),0.f,1.f):0;
                push(geometry.Get(),transform,flag(mask["hidden"])?0:alpha);
            }else issue(result,node,"only local filled-shape alpha masks are implemented");
        }
        if(real(node["shadowOpacity"])>0)issue(result,node,"layer shadow is not implemented");
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
            if(type!="axial")issue(result,node,"only axial native gradients are implemented");
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
                    ComPtr<ID2D1LinearGradientBrush> fill;checked(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
                        {static_cast<float>(bounds.x+a.x*bounds.width),static_cast<float>(bounds.y+a.y*bounds.height)},
                        {static_cast<float>(bounds.x+b.x*bounds.width),static_cast<float>(bounds.y+b.y*bounds.height)}),collection.Get(),fill.GetAddressOf()),"Create source gradient");
                    auto geometry=boundsGeometry(node);target->FillGeometry(geometry.Get(),fill.Get());
                }
            }
        }else if(kind!="layer")issue(result,node,"unknown native layer kind: "+kind);
        if(!node["contents"].isNull()){
            const auto& decoded=image(node["contents"],options);
            ComPtr<ID2D1Bitmap> bitmap;checked(target->CreateBitmap(D2D1::SizeU(decoded.width,decoded.height),decoded.premultipliedBGRA.data(),decoded.width*4,
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
};

LayerRasterizer::LayerRasterizer():impl_(std::make_unique<Impl>()){}
LayerRasterizer::~LayerRasterizer()=default;
std::shared_ptr<const LayerRasterImage> LayerRasterizer::rasterize(std::string id,std::uint64_t revision,const Json& layer,const LayerRasterOptions& options){
    auto& r=*impl_;r.onThread();
    if(id.empty()||id.size()>4096||!Json::validUtf8(id))invalid("Invalid retained layer ID");
    if(!std::isfinite(options.pixelsPerPoint)||options.pixelsPerPoint<.25||options.pixelsPerPoint>4
       ||!std::isfinite(options.paddingPoints)||options.paddingPoints<0||options.paddingPoints>64)invalid("Invalid local raster scale/padding");
    const auto old=r.entries.find(id);
    if(old!=r.entries.end()&&old->second.revision==revision&&old->second.options==options){++r.counts.cacheHits;return old->second.image;}
    if(old==r.entries.end()&&r.entries.size()>=maximumEntries)invalid("Retained layer cache is full; remove unused source IDs");
    auto result=std::make_shared<LayerRasterImage>();std::size_t visited=0;
    auto bounds=r.measure(layer,Matrix::Identity(),*result,visited,0);
    bounds.x-=options.paddingPoints;bounds.y-=options.paddingPoints;bounds.width+=options.paddingPoints*2;bounds.height+=options.paddingPoints*2;
    const double w=std::ceil(std::max(1.0/options.pixelsPerPoint,bounds.width)*options.pixelsPerPoint),h=std::ceil(std::max(1.0/options.pixelsPerPoint,bounds.height)*options.pixelsPerPoint);
    if(w>8192||h>8192||w*h>maximumPixels)invalid("Local raster exceeds its pixel limit");
    result->width=static_cast<unsigned>(w);result->height=static_cast<unsigned>(h);bounds.width=w/options.pixelsPerPoint;bounds.height=h/options.pixelsPerPoint;result->bounds=bounds;
    const auto bytes=std::size_t(result->width)*result->height*4,previous=old==r.entries.end()?0:old->second.image->straightRGBA.size();
    if(bytes>maximumResourceBytes-(r.counts.resourceBytes-previous))invalid("Retained layer resource budget exceeded");
    ComPtr<IWICBitmap> bitmap;checked(r.wic->CreateBitmap(result->width,result->height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf()),"Create bounded local bitmap");
    ComPtr<ID2D1RenderTarget> target;checked(r.d2d->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),target.GetAddressOf()),"Create offscreen local layer target");
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->BeginDraw();target->Clear(D2D1::ColorF(0.f,0.f,0.f,0.f));
    const Matrix pixels=Matrix::Translation(static_cast<float>(-bounds.x),static_cast<float>(-bounds.y))*Matrix::Scale(static_cast<float>(options.pixelsPerPoint),static_cast<float>(options.pixelsPerPoint));
    std::vector<ComPtr<IDWriteTextLayout>> layouts;visited=0;r.draw(target.Get(),layer,Matrix::Identity(),pixels,options,*result,layouts,1,visited,0,true);
    checked(target->EndDraw(),"Complete local layer raster");
    // Image cache growth while drawing also counts against the same bound.
    if(bytes>maximumResourceBytes-(r.counts.resourceBytes-previous))invalid("Retained layer resource budget exceeded after image decode");
    std::vector<std::uint8_t> bgra(bytes);checked(bitmap->CopyPixels(nullptr,result->width*4,static_cast<UINT>(bytes),bgra.data()),"Copy owned local bitmap");
    result->straightRGBA.resize(bytes);
    for(std::size_t i=0;i<bytes;i+=4){const unsigned a=bgra[i+3];result->straightRGBA[i+3]=static_cast<std::uint8_t>(a);
        for(unsigned c=0;c<3;++c)result->straightRGBA[i+c]=a?static_cast<std::uint8_t>(std::min(255u,(unsigned(bgra[i+2-c])*255u+a/2)/a)):0;
    }
    r.counts.resourceBytes=r.counts.resourceBytes-previous+bytes;++r.counts.rasterizations;
    r.entries.insert_or_assign(std::move(id),Impl::Entry{revision,options,result,std::move(layouts)});return result;
}
bool LayerRasterizer::remove(const std::string& id){auto& r=*impl_;r.onThread();const auto it=r.entries.find(id);if(it==r.entries.end())return false;r.counts.resourceBytes-=it->second.image->straightRGBA.size();r.entries.erase(it);return true;}
void LayerRasterizer::clear(){auto& r=*impl_;r.onThread();r.entries.clear();r.images.clear();r.counts.resourceBytes=0;}
LayerRasterStats LayerRasterizer::stats()const{const auto& r=*impl_;r.onThread();auto result=r.counts;result.entries=r.entries.size();result.decodedImages=r.images.size();return result;}
} // namespace endfield::native
#endif
