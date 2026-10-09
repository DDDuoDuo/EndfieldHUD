#include "modules/map_presentation.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

// Preserve the original Swift multiply/add boundaries at exact map hit edges.
#if defined(_MSC_VER)
#pragma float_control(precise,on,push)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::modules {
MapImagePlacement mapRasterPlacement(const MapRasterFrame&f,MapViewport v)noexcept{const double ratio=v.zoom/f.viewport.zoom;double dx=f.viewport.centerX-v.centerX;dx-=std::round(dx);return {{220+(f.screenRect.x-220)*ratio+dx*440*v.zoom,220+(f.screenRect.y-220)*ratio+(f.viewport.centerY-v.centerY)*220*v.zoom},ratio};}
std::array<MapImagePlacement,3>mapBackdropPlacements(MapViewport v)noexcept{std::array<MapImagePlacement,3>result;const auto scale=(440*v.zoom)/1024;for(unsigned i=0;i<3;++i)result[i]={{220+(double(i)-1-v.centerX)*440*v.zoom,220-v.centerY*220*v.zoom},scale};return result;}
std::optional<MapRasterGeometry>mapRasterGeometry(MapViewport v,double contentsScale,double padding)noexcept{if(!std::isfinite(v.centerX)||!std::isfinite(v.centerY)||!std::isfinite(v.zoom)||v.zoom<=0||!std::isfinite(contentsScale)||contentsScale<=0||!std::isfinite(padding))return {};v=mapConstrained(v);padding=std::clamp(padding,0.,128.);const double side=440+padding*2;const auto pixels=unsigned(std::min(1536.,std::max(1.,std::ceil(side*std::min(8.,contentsScale)))));const auto factor=(440*v.zoom)/1024;const core::Point origin{220-v.centerX*440*v.zoom,220-v.centerY*220*v.zoom};return MapRasterGeometry{v,{-padding,-padding,side,side},{(-padding-origin.x)/factor,(-padding-origin.y)/factor,side/factor,side/factor},origin,pixels,pixels/side,factor,.28/std::pow(2.,std::ceil(std::log2(factor)))};}
bool mapNeedsPaint(const MapRasterFrame*f,MapViewport v,bool sameDataRevision,bool interacting,bool exactRequested,double scale)noexcept{if(!f||!sameDataRevision)return true;if(!interacting&&f->pixelsPerPoint+.002<scale)return true;if(exactRequested)return f->viewport!=v;const auto p=mapRasterPlacement(*f,v);if(p.scale<.84||p.scale>1.18)return true;const auto width=f->screenRect.width*p.scale,height=f->screenRect.height*p.scale;return !(p.position.x<=-36&&p.position.y<=-36&&p.position.x+width>=476&&p.position.y+height>=476);}
double mapRenderScale(double value,bool interacting)noexcept{const double scale=std::min(2.2,std::max(1.,value));return interacting?std::min(1.4,scale):scale;}
double mapPaintCooldown(double elapsed)noexcept{return std::max(.125,elapsed*16);}
MapColor mapPinColor(MapPinStyle style)noexcept{return style==MapPinStyle::green?MapColor{.66,.95,.20,1}:MapColor{1,.9898965359,.3056603670,1};}
MapPinVisuals mapPinVisuals(std::span<const MapPin>pins,MapViewport camera,std::optional<std::string_view>selected,bool active,bool reduced,bool ambient){if(pins.size()>128)throw std::invalid_argument("Map visible-pin budget exceeded");MapPinVisuals out;std::array<std::size_t,128>rank{};std::array<double,128>dates{};
    for(const auto&p:pins){const auto position=mapScreen(p.x,p.y,camera);if(!mapContains(position))continue;const auto i=out.count++;out.items[i]={p.id,position,p.style,mapPinColor(p.style),selected&&p.id==*selected,false,selected&&p.id==*selected?.65:.25};rank[i]=i;dates[i]=p.createdAt;}
    // Stable insertion sort mirrors Swift's stable sorted() tie order. Only
    //128 numeric indices are touched; marker strings/artwork are never copied.
    for(std::size_t i=1;i<out.count;++i){const auto candidate=rank[i];auto at=i;auto before=[&](std::size_t a,std::size_t b){if(out.items[a].selected!=out.items[b].selected)return out.items[a].selected;return dates[a]>dates[b];};while(at&&before(candidate,rank[at-1])){rank[at]=rank[at-1];--at;}rank[at]=candidate;}
    if(active&&!reduced&&ambient)for(std::size_t i=0;i<std::min(out.count,std::size_t(12));++i){out.items[rank[i]].pulses=true;out.items[rank[i]].staticHaloOpacity=0;}return out;
}
MapPulse mapPulse(double elapsed){if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("Invalid map pulse phase");const auto linear=std::fmod(elapsed,1.7)/1.7;const auto phase=core::CubicTiming{0,0,.58,1}.value(linear);const auto alpha=phase<=.12?.05+(.65-.05)*(phase/.12):.65*(1-(phase-.12)/.88);return {.65+(2.7-.65)*phase,alpha};}
MapStyleTransition mapStyleTransition(double elapsed,bool reduced){if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("Invalid map style phase");const auto t=reduced?1:core::CubicTiming{0,0,.58,1}.value(std::min(1.,elapsed/.16));return {-.12*(1-t),.45+.55*t};}
std::array<MapPlayerAsset,3>mapPlayerAssets()noexcept{return {{{"WatchSource/Scene/Domain/sprites/deco_readio_mask--2444265073359569955.png",{-24,-26,48,52},.149,false},{"WatchSource/Scene/Domain/textures/T_fx_mask_02_M--4275033587688225551.png",{-3,-106,6,102},.72,true},{"WatchSource/Scene/Domain/sprites/icon_char---2308601083109874541.png",{-12,-12,24,24},1,false}}};}
MapChromeStyle mapChromeStyle(bool dark)noexcept{const auto gray=[](double v,double a){return MapColor{v,v,v,a};};return {gray(dark?.12:.81,.89),gray(dark?.95:.13,1),gray(dark?.08:.94,.88),gray(dark?.06:.94,.88)};}
std::string_view mapActionSymbol(MapAction action)noexcept{switch(action){case MapAction::zoomIn:return "+";case MapAction::zoomOut:return "−";case MapAction::reset:return "⌖";case MapAction::deletePin:return "×";default:return "⊙";}}
std::string mapZoomStatus(MapViewport v,std::size_t count,bool ready,bool loads,const MapChromeText&strings){if(!ready&&loads)return strings.loading;std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(2)<<v.zoom<<"×  /  "<<std::setfill('0')<<std::setw(2)<<count<<' '<<strings.pins;return out.str();}
}

#if defined(_MSC_VER)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
