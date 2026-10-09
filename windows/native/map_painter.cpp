#include "native/map_painter.hpp"
#include "modules/map_geography.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

// Screen geometry is evaluated in the same separate Double operations as the
// source before conversion to the native rasterizer's float coordinates.
#if defined(_MSC_VER)
#pragma float_control(precise,on,push)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::native {
namespace {
using Path=modules::MapPath;using Rect=core::Rect;using Point=core::Point;
#ifdef _WIN32
bool intersects(Rect a,Rect b){return a.x<b.x+b.width&&a.x+a.width>b.x&&a.y<b.y+b.height&&a.y+a.height>b.y;}
Rect inset(Rect r,double x,double y){return {r.x-x,r.y-y,r.width+2*x,r.height+2*y};}
#endif

}
struct NativeMapPainter::Impl {
    std::shared_ptr<const modules::MapGeography>source;
    std::map<std::pair<std::size_t,std::size_t>,modules::MapPathGeometry>countries;
    std::map<int,modules::MapPathGeometry>terrain,fills;
    std::optional<std::thread::id>thread;Stats counts;
    void check(){const auto id=std::this_thread::get_id();if(!thread)thread=id;if(*thread!=id)throw std::logic_error("Map painter is confined to its existing utility worker");}
    void clear(){countries.clear();terrain.clear();fills.clear();source.reset();}
    modules::MapPathGeometry&country(std::size_t c,std::size_t p,const Path&path){const auto key=std::make_pair(c,p);auto found=countries.find(key);if(found==countries.end())found=countries.try_emplace(key,path).first;return found->second;}
    modules::MapPathGeometry&band(std::map<int,modules::MapPathGeometry>&cache,int elevation,const Path&path){auto found=cache.find(elevation);if(found==cache.end())found=cache.try_emplace(elevation,path).first;return found->second;}
};
NativeMapPainter::NativeMapPainter():impl_(std::make_unique<Impl>()){}NativeMapPainter::~NativeMapPainter()=default;
void NativeMapPainter::clear(){impl_->check();impl_->clear();}
NativeMapPainter::Stats NativeMapPainter::stats()const{auto&i=*impl_;i.check();auto s=i.counts;s.countryIndexes=i.countries.size();s.terrainIndexes=i.terrain.size();s.fillIndexes=i.fills.size();for(const auto&[key,value]:i.countries){(void)key;s.cachedLevels+=value.cachedLevels();}for(const auto&[key,value]:i.fills){(void)key;s.cachedLevels+=value.cachedLevels();}return s;}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;using Color=modules::MapColor;using Matrix=D2D1::Matrix3x2F;
void checked(HRESULT hr,const char*message){if(FAILED(hr))throw std::runtime_error(std::string(message)+" HRESULT="+std::to_string(static_cast<unsigned long>(hr)));}
struct Apartment {HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Apartment(){if(FAILED(result)&&result!=RPC_E_CHANGED_MODE)checked(result,"Map paint apartment");}~Apartment(){if(SUCCEEDED(result))CoUninitialize();}};
Color gray(double value,double alpha=1){return {value,value,value,alpha};}
Color mix(Color c,double amount,double toward,double alpha){return {c[0]*(1-amount)+toward*amount,c[1]*(1-amount)+toward*amount,c[2]*(1-amount)+toward*amount,alpha};}
D2D1_RECT_F rect(Rect r){return D2D1::RectF(float(r.x),float(r.y),float(r.x+r.width),float(r.y+r.height));}
struct Surface {
    ComPtr<ID2D1Factory>factory;ComPtr<IWICImagingFactory>wic;ComPtr<IWICBitmap>bitmap;ComPtr<ID2D1RenderTarget>target;ComPtr<ID2D1SolidColorBrush>brush;ComPtr<ID2D1StrokeStyle>stroke;
    unsigned width{},height{};Matrix pixels;
    Surface(unsigned w,unsigned h,Rect bounds,double scale):width(w),height(h){
        checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()),"Map D2D factory");checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.GetAddressOf())),"Map WIC factory");checked(wic->CreateBitmap(w,h,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf()),"Map bounded bitmap");
        checked(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),target.GetAddressOf()),"Map software target");checked(target->CreateSolidColorBrush(D2D1::ColorF(0,0,0,1),brush.GetAddressOf()),"Map brush");auto props=D2D1::StrokeStyleProperties();props.lineJoin=D2D1_LINE_JOIN_ROUND;props.startCap=props.endCap=props.dashCap=D2D1_CAP_STYLE_FLAT;checked(factory->CreateStrokeStyle(props,nullptr,0,stroke.GetAddressOf()),"Map round join stroke");pixels=Matrix::Translation(float(-bounds.x),float(-bounds.y))*Matrix::Scale(float(scale),float(scale));target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);target->BeginDraw();target->SetTransform(pixels);
    }
    void color(Color c){brush->SetColor(D2D1::ColorF(float(c[0]),float(c[1]),float(c[2]),float(c[3])));}
    void background(Color c){target->Clear(D2D1::ColorF(float(c[0]),float(c[1]),float(c[2]),float(c[3])));}
    void fill(ID2D1Geometry*g,Color c,double x=0,double y=0){color(c);target->SetTransform(Matrix::Translation(float(x),float(y))*pixels);target->FillGeometry(g,brush.Get());target->SetTransform(pixels);}
    void line(ID2D1Geometry*g,Color c,double width){color(c);target->DrawGeometry(g,brush.Get(),float(width),stroke.Get());}
    ComPtr<ID2D1PathGeometry>geometry(const Path&path,double factor=1,Point origin={}){
        ComPtr<ID2D1PathGeometry>g;checked(factory->CreatePathGeometry(g.GetAddressOf()),"Map path");ComPtr<ID2D1GeometrySink>sink;checked(g->Open(sink.GetAddressOf()),"Map path sink");sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
        for(const auto&ring:path){if(ring.points.size()<2)continue;const auto p=ring.points.front();sink->BeginFigure({float(origin.x+p.x*factor),float(origin.y+p.y*factor)},ring.closed?D2D1_FIGURE_BEGIN_FILLED:D2D1_FIGURE_BEGIN_HOLLOW);for(std::size_t n=1;n<ring.points.size();++n){const auto point=ring.points[n];sink->AddLine({float(origin.x+point.x*factor),float(origin.y+point.y*factor)});}sink->EndFigure(ring.closed?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);}checked(sink->Close(),"Map path completion");return g;
    }
    std::shared_ptr<const MapPaintImage>finish(){checked(target->EndDraw(),"Map paint completion");auto result=std::make_shared<MapPaintImage>();result->width=width;result->height=height;result->straightRGBA.resize(std::size_t(width)*height*4);checked(bitmap->CopyPixels(nullptr,width*4,UINT(result->straightRGBA.size()),result->straightRGBA.data()),"Map pixel copy");
        // The source paints an opaque full bitmap background. Converting in
        // place avoids an additional9MiB swizzle buffer. Retain generic alpha
        // handling so any native precision edge remains correctly associated.
        for(std::size_t n=0;n<result->straightRGBA.size();n+=4){auto*p=result->straightRGBA.data()+n;std::swap(p[0],p[2]);if(p[3]!=255){const unsigned alpha=p[3];for(unsigned c=0;c<3;++c)p[c]=alpha?std::uint8_t(std::min(255u,(unsigned(p[c])*255+alpha/2)/alpha)):0;}}return result;
    }
};
void appendTransformed(Path&target,Path source,double factor,Point origin){for(auto&ring:source){for(auto&p:ring.points){p.x=origin.x+p.x*factor;p.y=origin.y+p.y*factor;}target.push_back(std::move(ring));}}
void segment(Path&path,Point a,Point b){path.push_back({{a,b},false,{std::min(a.x,b.x),std::min(a.y,b.y),std::abs(a.x-b.x),std::abs(a.y-b.y)}});}
bool contains(Rect r,Point p){return p.x>=r.x&&p.x<r.x+r.width&&p.y>=r.y&&p.y<r.y+r.height;}
template<class Worker>
std::shared_ptr<const MapPaintImage>backdrop(Worker&w,const MapPaintRequest&q,const MapPaintCancelled&cancel){
    Surface s(1024,512,{0,0,1024,512},1);s.background(gray(q.dark?.12:.81));const Rect world{0,0,1024,512};
    if(q.geography->countries)for(const auto&country:q.geography->countries->countries){if(cancel())return {};for(const auto&component:country.components){if(cancel())return {};if(std::max(component.bounds.width,component.bounds.height)<.28)continue;modules::MapPathGeometry temporary(component.path);const auto local=temporary.clippedPolygon(world,.35);auto fill=s.geometry(local.fillPath),edge=s.geometry(local.linePath);s.target->PushAxisAlignedClip(rect(inset(component.bounds,1,1)),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);s.fill(fill.Get(),gray(q.dark?.60:.56,q.dark?.43:.38));s.line(edge.Get(),gray(q.dark?.92:.98,.60),.55);s.target->PopAxisAlignedClip();}}
    if(!q.geography->countries&&q.geography->terrain){const auto&bands=q.geography->terrain->bands;const auto found=std::find_if(bands.begin(),bands.end(),[](const auto&b){return b.elevation==0&&b.fillPath;});if(found!=bands.end()){modules::MapPathGeometry temporary(*found->fillPath);auto path=s.geometry(temporary.clippedPolygon(world,.35).fillPath);s.fill(path.Get(),gray(q.dark?.60:.56,q.dark?.43:.38));}}
    if(q.geography->terrain)for(const auto&band:q.geography->terrain->bands)if(band.elevation!=0){if(cancel())return {};auto path=s.geometry(w.band(w.terrain,band.elevation,band.contourPath).clippedLines(world,.45));s.line(path.Get(),gray(q.dark?.80:.22,.25),.35);}return cancel()?nullptr:s.finish();
}
template<class Worker>
std::shared_ptr<const MapPaintImage>detail(Worker&w,const MapPaintRequest&q,const modules::MapRasterGeometry&g,const MapPaintCancelled&cancel){
    struct Face {ComPtr<ID2D1PathGeometry>fill,edge;D2D1_RECT_F bounds;bool highlighted{};};std::vector<Face>faces;Path land,terrain,coast,grid;struct Fallback {Path path;int elevation;};std::vector<Fallback>fallback;
    Surface s(g.pixelDimension,g.pixelDimension,g.screenRect,g.pixelsPerPoint);s.background(gray(q.dark?.12:.81));std::optional<std::size_t>focused;
    if(q.geography->countries){const auto&countries=q.geography->countries->countries;const Point center{q.viewport.centerX*1024,q.viewport.centerY*512};for(std::size_t n=0;n<countries.size();++n)if(contains(countries[n].bounds,center)&&modules::mapEvenOddContains(countries[n].path,center)){focused=n;break;}}
    for(int wrap=-1;wrap<=1;++wrap){if(cancel())return {};const double shift=wrap*1024.;auto visible=g.worldRect;visible.x-=shift;if(!intersects(visible,{0,0,1024,512}))continue;const Point origin{g.origin.x+shift*g.worldScale,g.origin.y};const auto countryVisible=inset(visible,12/g.worldScale,12/g.worldScale);
        if(q.geography->countries){const auto&countries=q.geography->countries->countries;for(std::size_t c=0;c<countries.size();++c){const auto&country=countries[c];if(!intersects(country.bounds,countryVisible))continue;if(cancel())return {};for(std::size_t n=0;n<country.components.size();++n){const auto&component=country.components[n];if(!intersects(component.bounds,countryVisible)||std::max(component.bounds.width,component.bounds.height)*g.worldScale<.28)continue;auto local=w.country(c,n,component.path).clippedPolygon(countryVisible,g.tolerance);if(local.fillPath.empty())continue;auto fill=s.geometry(local.fillPath,g.worldScale,origin),edge=s.geometry(local.linePath,g.worldScale,origin);D2D1_RECT_F bounds{};checked(fill->GetBounds(nullptr,&bounds),"Map country bounds");bounds.left-=12;bounds.top-=12;bounds.right+=12;bounds.bottom+=12;faces.push_back({fill,edge,bounds,focused&&*focused==c});appendTransformed(land,std::move(local.fillPath),g.worldScale,origin);}}}
        if(q.geography->terrain)for(const auto&band:q.geography->terrain->bands){if(cancel())return {};if(!q.geography->countries&&band.fillPath){auto local=w.band(w.fills,band.elevation,*band.fillPath).clippedPolygon(visible,g.tolerance);Path transformed;appendTransformed(transformed,std::move(local.fillPath),g.worldScale,origin);fallback.push_back({std::move(transformed),band.elevation});}if(q.geography->countries&&band.elevation==0)continue;appendTransformed(band.elevation==0?coast:terrain,w.band(w.terrain,band.elevation,band.contourPath).clippedLines(visible,g.tolerance),g.worldScale,origin);}
        for(unsigned n=0;n<=12;++n){const double x=double(n)*(1024./12);if(x>=visible.x&&x<=visible.x+visible.width)segment(grid,{g.origin.x+(x+shift)*g.worldScale,std::max(g.screenRect.y,g.origin.y)},{g.origin.x+(x+shift)*g.worldScale,std::min(g.screenRect.y+g.screenRect.height,g.origin.y+512*g.worldScale)});}for(unsigned n=0;n<=6;++n){const double y=double(n)*(512./6);if(y>=visible.y&&y<=visible.y+visible.height)segment(grid,{std::max(g.screenRect.x,g.origin.x+shift*g.worldScale),g.origin.y+y*g.worldScale},{std::min(g.screenRect.x+g.screenRect.width,g.origin.x+(shift+1024)*g.worldScale),g.origin.y+y*g.worldScale});}
    }
    for(bool highlighted:{false,true})for(const auto&face:faces)if(face.highlighted==highlighted){if(cancel())return {};s.target->PushAxisAlignedClip(face.bounds,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);s.fill(face.fill.Get(),gray(0,q.dark?.42:.24),3,9);for(unsigned n=0;n<4;++n){const double depth=(4-n)*1.6,amount=double(n)/3;s.fill(face.fill.Get(),highlighted?mix(q.accent,.48-amount*.12,0,.95):gray((q.dark?.19:.35)+amount*.12,.98),depth*.35,depth);}s.fill(face.fill.Get(),highlighted?mix(q.accent,0,0,q.dark?.51:.47):gray(q.dark?.60:.56,q.dark?.43:.38));s.line(face.edge.Get(),highlighted?mix(q.accent,.38,1,.95):gray(q.dark?.92:.98,.82),highlighted?1.5:1.05);s.target->PopAxisAlignedClip();}
    if(!land.empty()){auto clip=s.geometry(land);s.target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),clip.Get(),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,Matrix::Identity(),1,nullptr,D2D1_LAYER_OPTIONS_NONE),nullptr);Path stripes;const auto phase=std::fmod(q.viewport.centerX*440*q.viewport.zoom-q.viewport.centerY*220*q.viewport.zoom,4.5);for(double x=std::floor((g.screenRect.x-g.screenRect.height)/4.5)*4.5;x<=g.screenRect.x+g.screenRect.width+g.screenRect.height;x+=4.5)segment(stripes,{x-phase,g.screenRect.y},{x-phase+g.screenRect.height,g.screenRect.y+g.screenRect.height});auto lines=s.geometry(stripes);s.line(lines.Get(),gray(q.dark?1:.98,q.dark?.17:.23),1.3);s.target->PopLayer();}
    if(cancel())return {};for(const auto&item:fallback){auto fill=s.geometry(item.path);s.fill(fill.Get(),gray(q.dark?.85:.20,item.elevation==0?.11:.025));}auto gridPath=s.geometry(grid),terrainPath=s.geometry(terrain),coastPath=s.geometry(coast);s.line(gridPath.Get(),gray(q.dark?.80:.19,.09),.35);s.line(terrainPath.Get(),q.dark?Color{.72,.78,.80,.29}:Color{.18,.23,.25,.32},.38);s.line(coastPath.Get(),q.dark?Color{.72,.78,.80,.78}:Color{.18,.23,.25,.80},.70);return cancel()?nullptr:s.finish();
}
}
MapPaintResult NativeMapPainter::paint(const MapPaintRequest&q,const MapPaintCancelled&cancel){
    auto&i=*impl_;i.check();
    if(!q.geography||(!q.geography->terrain&&!q.geography->countries)||!cancel)throw std::invalid_argument("Map painter requires explicit geography/cancellation");
    const auto geometry=modules::mapRasterGeometry(q.viewport,q.contentsScale);
    if(!geometry||geometry->viewport!=q.viewport)throw std::invalid_argument("Map painter requires a constrained camera");
    for(const auto value:q.accent)if(!std::isfinite(value)||value<0||value>1)throw std::invalid_argument("Invalid source map accent");
    const auto started=std::chrono::steady_clock::now();
    const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();};
    if(cancel()){i.clear();return {{},{},{},elapsed()};}
    if(i.source!=q.geography){i.clear();i.source=q.geography;}
    Apartment apartment;MapPaintResult result;
    if(q.backdrop){result.backdrop=backdrop(i,q,cancel);if(result.backdrop)++i.counts.backdrops;}
    if(!cancel()){result.detail=detail(i,q,*geometry,cancel);if(result.detail){result.frame=modules::MapRasterFrame{geometry->viewport,geometry->screenRect,geometry->pixelsPerPoint};++i.counts.paints;}}
    if(cancel()){i.clear();return {{},{},{},elapsed()};}
    if(!result.detail)result.backdrop.reset();result.workSeconds=elapsed();return result;
}
}
#else
namespace endfield::native {MapPaintResult NativeMapPainter::paint(const MapPaintRequest&,const MapPaintCancelled&){throw std::runtime_error("Native Map painting requires the Windows D2D/WIC backend");}}
#endif

#if defined(_MSC_VER)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
