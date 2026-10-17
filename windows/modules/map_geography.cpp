#include "modules/map_geography.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

// Preserve the original sequential CGFloat arithmetic. Fusing multiply/add
// changes equal-distance simplification winners in the original-source oracle.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace endfield::modules {
namespace {
using P=core::Point;using R=core::Rect;
void need(bool v,const char*why){if(!v)throw std::invalid_argument(why);}
R bounds(std::span<const P>points){need(!points.empty(),"Empty map ring");double left=points[0].x,right=left,top=points[0].y,bottom=top;for(auto p:points){left=std::min(left,p.x);right=std::max(right,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y);}return {left,top,right-left,bottom-top};}
R pathBounds(const MapPath&p){need(!p.empty(),"Empty map path");auto r=p.front().bounds;for(const auto&ring:p){const auto right=std::max(r.x+r.width,ring.bounds.x+ring.bounds.width),bottom=std::max(r.y+r.height,ring.bounds.y+ring.bounds.height);r.x=std::min(r.x,ring.bounds.x);r.y=std::min(r.y,ring.bounds.y);r.width=right-r.x;r.height=bottom-r.y;}return r;}
MapRing ring(std::vector<P>p,bool closed){auto b=bounds(p);return {std::move(p),closed,b};}
bool intersects(R a,R b){return a.x<=b.x+b.width&&a.x+a.width>=b.x&&a.y<=b.y+b.height&&a.y+a.height>=b.y;}
bool contains(R r,P p){return p.x>=r.x&&p.x<r.x+r.width&&p.y>=r.y&&p.y<r.y+r.height;}
bool contains(R a,R b){return b.x>=a.x&&b.y>=a.y&&b.x+b.width<=a.x+a.width&&b.y+b.height<=a.y+a.height;}
bool valid(R r){return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>=0&&r.height>=0&&std::isfinite(r.x+r.width)&&std::isfinite(r.y+r.height);}
bool space(std::uint32_t c){return (c>=9&&c<=13)||c==0x20||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200b)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;}
bool control(std::uint32_t c){
    // Foundation controlCharacters includes Cc/Cf; the plane14 membership
    // pattern below is retained from the source runtime's NSCharacterSet probe.
    constexpr std::array<std::pair<std::uint32_t,std::uint32_t>,21>ranges{{{0,0x1f},{0x7f,0x9f},{0xad,0xad},{0x600,0x605},{0x61c,0x61c},{0x6dd,0x6dd},{0x70f,0x70f},{0x890,0x891},{0x8e2,0x8e2},{0x180e,0x180e},{0x200b,0x200f},{0x202a,0x202e},{0x2060,0x2064},{0x2066,0x206f},{0xfeff,0xfeff},{0xfff9,0xfffb},{0x110bd,0x110bd},{0x110cd,0x110cd},{0x13430,0x1343f},{0x1bca0,0x1bca3},{0x1d173,0x1d17a}}};
    for(auto [a,b]:ranges)if(c>=a&&c<=b)return true;return (c>>16)==14&&((c&255)==1||((c&255)>=0x20&&(c&255)<=0x7f));
}
struct Reader{
    std::span<const std::uint8_t>data;std::size_t at{8};const char*error;
    std::uint8_t byte(){need(at<data.size(),error);return data[at++];}
    std::uint16_t u16(){const auto a=byte(),b=byte();return std::uint16_t(a)|(std::uint16_t(b)<<8);}
    std::uint32_t u32(){const auto a=u16(),b=u16();return std::uint32_t(a)|(std::uint32_t(b)<<16);}
    void points(std::size_t n,std::size_t bytes){need(n<=((data.size()-at)/bytes),error);}
    std::string string(std::size_t n,std::size_t max){need(n&&n<=max&&n<=data.size()-at,error);const auto start=at,end=at+n;bool nonspace{};
        while(at<end){std::uint32_t c=byte();unsigned count{};std::uint32_t minimum{};
            if(c<0x80){}else if(c>=0xc2&&c<=0xdf){c&=31;count=1;minimum=0x80;}else if(c>=0xe0&&c<=0xef){c&=15;count=2;minimum=0x800;}else if(c>=0xf0&&c<=0xf4){c&=7;count=3;minimum=0x10000;}else need(false,error);
            need(count<=end-at,error);while(count--){const auto next=byte();need((next&0xc0)==0x80,error);c=(c<<6)|(next&63);}need(c>=minimum&&c<=0x10ffff&&(c<0xd800||c>0xdfff)&&!control(c),error);nonspace|=!space(c);
        }need(nonspace,error);return {reinterpret_cast<const char*>(data.data()+start),n};}
};
Reader reader(std::span<const std::uint8_t>data,std::size_t max,const char*magic,const char*error){need(data.size()>=12&&data.size()<=max&&std::equal(data.begin(),data.begin()+8,magic),error);return {data,8,error};}
}
MapTerrain MapTerrain::decode(std::span<const std::uint8_t>data){
    constexpr auto error="The bundled Earth terrain could not be read.";auto r=reader(data,maximumBytes,"EHUDMAP1",error);const auto count=r.u32();need(count>=1&&count<=32,error);MapTerrain result;result.bands.reserve(count);std::size_t paths{};
    for(unsigned i=0;i<count;++i){MapTerrainBand band;band.elevation=std::bit_cast<std::int32_t>(r.u32());const auto lines=r.u32(),fills=r.u32();need(band.elevation>=-12000&&band.elevation<=12000&&lines<=20000&&fills<=20000&&paths+lines+fills<=40000,error);paths+=lines+fills;band.contourPath.reserve(lines);if(fills){band.fillPath.emplace();band.fillPath->reserve(fills);}
        for(std::size_t k=0;k<std::size_t(lines)+fills;++k){const auto closed=r.byte();const auto points=r.u32();need(closed<=1&&points>=2&&points<=maximumVertices&&result.vertexCount+points<=maximumVertices,error);r.points(points,4);result.vertexCount+=points;std::vector<P>vertices;vertices.reserve(points);for(unsigned p=0;p<points;++p){const auto x=r.u16(),y=r.u16();vertices.push_back({double(x)/65535*1024,double(y)/65535*512});}auto&path=k<lines?band.contourPath:*band.fillPath;path.push_back(ring(std::move(vertices),closed==1));}
        result.bands.push_back(std::move(band));
    }need(r.at==data.size(),error);return result;
}
MapCountries MapCountries::decode(std::span<const std::uint8_t>data){
    constexpr auto error="The bundled country outlines could not be read.";auto r=reader(data,maximumBytes,"EHUDCTY1",error);const auto count=r.u32();need(count>=1&&count<=512,error);MapCountries result;result.countries.reserve(count);std::size_t components{},rings{};
    for(unsigned i=0;i<count;++i){MapCountry country;country.id=r.string(r.byte(),12);for(const unsigned char c:country.id)need((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_',error);for(const auto&old:result.countries)need(old.id!=country.id,error);country.name=r.string(r.u16(),128);const auto pieces=r.u32();need(pieces&&pieces<=8192&&components+pieces<=10000,error);components+=pieces;country.components.reserve(pieces);
        for(unsigned k=0;k<pieces;++k){MapCountryComponent component;const auto ringCount=r.u32();need(ringCount&&ringCount<=1024&&rings+ringCount<=20000,error);rings+=ringCount;component.path.reserve(ringCount);
            for(unsigned n=0;n<ringCount;++n){const auto points=r.u32();need(points>=3&&points<=maximumVertices&&result.vertexCount+points<=maximumVertices,error);r.points(points,8);result.vertexCount+=points;std::vector<P>vertices;vertices.reserve(points);for(unsigned p=0;p<points;++p){const auto x=r.u32(),y=r.u32();need(x<=16777215&&y<=16777215,error);vertices.push_back({double(x)/16777215*1024,double(y)/16777215*512});}component.path.push_back(ring(std::move(vertices),true));}
            component.bounds=pathBounds(component.path);need(component.bounds.width>0&&component.bounds.height>0,error);country.path.insert(country.path.end(),component.path.begin(),component.path.end());country.components.push_back(std::move(component));
        }country.bounds=pathBounds(country.path);result.countries.push_back(std::move(country));
    }need(r.at==data.size(),error);return result;
}
bool mapEvenOddContains(const MapPath&path,P p)noexcept{
    if(!std::isfinite(p.x)||!std::isfinite(p.y))return false;bool inside{};
    for(const auto&ring:path){if(ring.points.size()<2)continue;for(std::size_t i=0,j=ring.points.size()-1;i<ring.points.size();j=i++){
        const auto a=ring.points[j],b=ring.points[i];const double cross=(p.x-a.x)*(b.y-a.y)-(p.y-a.y)*(b.x-a.x);
        if(cross==0&&p.x>=std::min(a.x,b.x)&&p.x<=std::max(a.x,b.x)&&p.y>=std::min(a.y,b.y)&&p.y<=std::max(a.y,b.y))return true;
        if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside;
    }}return inside;
}
namespace {
struct Chunk{std::size_t start,end;R bounds;};
struct IndexedRing{MapRing ring;std::vector<Chunk>chunks;explicit IndexedRing(MapRing value):ring(std::move(value)){ring.bounds=bounds(ring.points);for(std::size_t start=0;start+1<ring.points.size();start+=64){const auto end=std::min(ring.points.size()-1,start+64);chunks.push_back({start,end,bounds(std::span(ring.points).subspan(start,end-start+1))});}}};
std::vector<P>simplify(std::span<const P>input,double tolerance){
    if(input.size()<=2||tolerance<=0)return {input.begin(),input.end()};std::vector<bool>keep(input.size());keep.front()=keep.back()=true;const double threshold=tolerance*tolerance;std::vector<std::pair<std::size_t,std::size_t>>stack{{0,input.size()-1}};
    while(!stack.empty()){auto[first,last]=stack.back();stack.pop_back();if(last<=first+1)continue;const auto a=input[first],b=input[last];const double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;double furthest=threshold;std::size_t selected{};
        for(std::size_t i=first+1;i<last;++i){const auto p=input[i];const double t=length>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.,1.):0,px=p.x-a.x-t*dx,py=p.y-a.y-t*dy,distance=px*px+py*py;if(distance>furthest){furthest=distance;selected=i;}}
        if(selected){keep[selected]=true;stack.emplace_back(first,selected);stack.emplace_back(selected,last);}}
    std::vector<P>result;for(std::size_t i=0;i<input.size();++i)if(keep[i])result.push_back(input[i]);return result;
}
std::optional<std::pair<P,P>>clipSegment(P a,P b,R r){if(contains(r,a)&&contains(r,b))return {{a,b}};const double dx=b.x-a.x,dy=b.y-a.y;double low=0,high=1;
    for(auto[p,q]:std::array<std::pair<double,double>,4>{{{-dx,a.x-r.x},{dx,r.x+r.width-a.x},{-dy,a.y-r.y},{dy,r.y+r.height-a.y}}}){
        if(p==0){if(q<0)return {};}else{const auto ratio=q/p;if(p<0){if(ratio>high)return {};low=std::max(low,ratio);}else{if(ratio<low)return {};high=std::min(high,ratio);}}}
    return {{P{a.x+low*dx,a.y+low*dy},P{a.x+high*dx,a.y+high*dy}}};
}
void appendLines(const IndexedRing&item,MapPath&result,R rect,double tolerance){std::vector<P>run;
    auto flush=[&]{if(run.size()>1)result.push_back(ring(tolerance>0?simplify(run,tolerance):run,false));run.clear();};
    auto segment=[&](P a,P b){const auto clipped=clipSegment(a,b,rect);if(!clipped){flush();return;}if(run.empty()||run.back()!=clipped->first){flush();run.push_back(clipped->first);}run.push_back(clipped->second);};
    for(const auto&chunk:item.chunks){if(!intersects(chunk.bounds,rect)){flush();continue;}for(auto i=chunk.start;i<chunk.end;++i)segment(item.ring.points[i],item.ring.points[i+1]);}
    if(item.ring.closed)segment(item.ring.points.back(),item.ring.points.front());flush();
}
std::vector<P>clipPolygon(std::span<const P>input,R rect){std::vector<P>output(input.begin(),input.end());for(unsigned side=0;side<4;++side){if(output.empty())break;auto source=std::move(output);output.clear();
        auto inside=[&](P p){switch(side){case 0:return p.x>=rect.x;case 1:return p.x<=rect.x+rect.width;case 2:return p.y>=rect.y;default:return p.y<=rect.y+rect.height;}};
        auto crossing=[&](P a,P b)->P{if(side<2){const double x=side==0?rect.x:rect.x+rect.width;return {x,a.y+(b.y-a.y)*(x-a.x)/(b.x-a.x)};}const double y=side==2?rect.y:rect.y+rect.height;return {a.x+(b.x-a.x)*(y-a.y)/(b.y-a.y),y};};
        auto previous=source.back();auto previousInside=inside(previous);for(const auto current:source){const bool currentInside=inside(current);if(currentInside!=previousInside)output.push_back(crossing(previous,current));if(currentInside)output.push_back(current);previous=current;previousInside=currentInside;}}
    return output;
}
}
struct MapPathGeometry::Impl{
    std::vector<IndexedRing>rings;struct Level{double tolerance;std::vector<IndexedRing>rings;};std::vector<Level>levels;
    explicit Impl(const MapPath&path){std::size_t total{};need(path.size()<=40000,"Map path exceeds source ring capacity");for(const auto&source:path){need(total+source.points.size()<=150000,"Map path exceeds source vertex capacity");total+=source.points.size();for(auto p:source.points)need(std::isfinite(p.x)&&std::isfinite(p.y),"Nonfinite map geometry");if(source.points.size()<2)continue;auto copy=source;if(copy.closed&&copy.points.front()==copy.points.back())copy.points.pop_back();rings.emplace_back(std::move(copy));}levels.reserve(2);}
    const std::vector<IndexedRing>&simplified(double tolerance){if(tolerance<=0)return rings;for(std::size_t i=0;i<levels.size();++i)if(levels[i].tolerance==tolerance){if(i+1<levels.size())std::swap(levels[i],levels.back());return levels.back().rings;}
        Level next{tolerance,{}};next.rings.reserve(rings.size());for(const auto&item:rings){const auto&r=item.ring;if(r.points.size()<=8){next.rings.push_back(item);continue;}std::vector<P>output;
            if(r.closed){const auto origin=r.points.front();std::size_t split{};double furthest=-1;for(std::size_t i=0;i<r.points.size();++i){const auto x=r.points[i].x-origin.x,y=r.points[i].y-origin.y,d=x*x+y*y;if(d>furthest){furthest=d;split=i;}}
                auto first=simplify(std::span(r.points).first(split+1),tolerance);std::vector<P>arc(r.points.begin()+split,r.points.end());arc.push_back(origin);auto second=simplify(arc,tolerance);first.pop_back();second.pop_back();output=std::move(first);output.insert(output.end(),second.begin(),second.end());
            }else output=simplify(r.points,tolerance);
            if(output.size()<(r.closed?3u:2u))next.rings.push_back(item);else next.rings.emplace_back(ring(std::move(output),r.closed));}
        // Evict before insertion: never retain a third cached zoom level.
        if(levels.size()==2)levels.erase(levels.begin());levels.push_back(std::move(next));return levels.back().rings;
    }
};
MapPathGeometry::MapPathGeometry(const MapPath&p):impl_(std::make_unique<Impl>(p)){}
MapPathGeometry::~MapPathGeometry()=default;MapPathGeometry::MapPathGeometry(MapPathGeometry&&)noexcept=default;MapPathGeometry&MapPathGeometry::operator=(MapPathGeometry&&)noexcept=default;
MapPath MapPathGeometry::clippedLines(R rect,double tolerance)const{need(valid(rect)&&std::isfinite(tolerance),"Invalid map geometry query");MapPath result;for(const auto&item:impl_->rings)if(intersects(item.ring.bounds,rect))appendLines(item,result,rect,tolerance);return result;}
MapClippedGeometry MapPathGeometry::clippedPolygon(R rect,double tolerance){need(valid(rect)&&std::isfinite(tolerance),"Invalid map geometry query");MapClippedGeometry result;for(const auto&item:impl_->simplified(tolerance))if(intersects(item.ring.bounds,rect)){if(item.ring.closed){auto points=contains(rect,item.ring.bounds)?item.ring.points:clipPolygon(item.ring.points,rect);if(points.size()>=3)result.fillPath.push_back(ring(std::move(points),true));}appendLines(item,result.linePath,rect,0);}return result;}
std::size_t MapPathGeometry::cachedLevels()const noexcept{return impl_->levels.size();}
}
