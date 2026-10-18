#include "modules/notes_drawing_ink.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
using P=core::Point;using R=core::Rect;using J=ehud::data::Json;
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
// Whether segment ab meets rectangle r grown by pad (Liang-Barsky).
bool meets(P a,P b,R r,double pad){r.x-=pad;r.y-=pad;r.width+=pad*2;r.height+=pad*2;double low=0,high=1;
    const auto clip=[&](double p,double q){if(p==0)return q>=0;const double v=q/p;if(p<0)low=std::max(low,v);else high=std::min(high,v);return low<=high;};
    const auto dx=b.x-a.x,dy=b.y-a.y;return clip(-dx,a.x-r.x)&&clip(dx,r.x+r.width-a.x)&&clip(-dy,a.y-r.y)&&clip(dy,r.y+r.height-a.y);}
// A half-open rectangle of picture pixels.
struct Pixels {long left{},top{},right{},bottom{};bool empty()const{return right<=left||bottom<=top;}};
Pixels unite(Pixels a,Pixels b){return {std::min(a.left,b.left),std::min(a.top,b.top),std::max(a.right,b.right),std::max(a.bottom,b.bottom)};}
Pixels intersect(Pixels a,Pixels b){return {std::max(a.left,b.left),std::max(a.top,b.top),std::min(a.right,b.right),std::min(a.bottom,b.bottom)};}
J command(const char*op,P p){return J::Object{{"op",op},{"points",J::Array{J(J::Array{p.x,p.y})}}};}
J box(double w,double h){return J::Array{0,0,w,h};}
}
struct NotesDrawingInkPlan::Impl {
    struct Record {DrawingStroke stroke;std::vector<unsigned>tiles;};
    struct Dirty {bool any{};Pixels pixels;};
    P size{};double widthScale{1},ppu{1};unsigned width{},height{},columns{},rows{};bool ready{},whole{true},completedSet{};
    std::vector<std::shared_ptr<Record>>records;std::vector<std::vector<std::uint32_t>>members;
    DrawingStroke live;bool livePresent{};std::vector<unsigned>liveTiles;std::uint64_t completedRevision{},liveRevision{};
    std::vector<Dirty>dirty;std::vector<unsigned>dirtyTiles;std::vector<NotesInkPixels>out;NotesInkPlanStats stats;
    std::size_t tileCount()const{return std::size_t(columns)*rows;}
    P at(const DrawingStroke&s,std::size_t k)const{return {s.points[k].x*size.x,s.points[k].y*size.y};}
    // The source pad: half the brush in content units and two pixels of
    // antialiasing. Every pixel a segment can change lies within it.
    double pad(const DrawingStroke&s)const{return s.width*widthScale*.5+2/ppu;}
    // Segment k joins point k-1 to point k; a single source point is the
    // source's 0.01-unit segment, so its round caps stay visible.
    std::pair<P,P> segmentAt(const DrawingStroke&s,std::size_t k)const{const auto a=at(s,k-1);return {a,s.points.size()==1?P{a.x+.01,a.y}:at(s,k)};}
    std::size_t segments(const DrawingStroke&s)const{return std::max(std::size_t(2),s.points.size());}
    Pixels tile(unsigned id)const{const long x=long(id%columns)*tilePixels,y=long(id/columns)*tilePixels;return {x,y,std::min(x+long(tilePixels),long(width)),std::min(y+long(tilePixels),long(height))};}
    R bounds(unsigned id)const{const auto t=tile(id);return {double(t.left)/ppu,double(t.top)/ppu,double(t.right-t.left)/ppu,double(t.bottom-t.top)/ppu};}
    // Every tile the segments from `first` reach, with the pixels they reach
    // inside it: f(tile, pixels).
    template<class F>void footprint(const DrawingStroke&s,std::size_t first,F&&f){
        const auto count=segments(s);const auto p=pad(s);
        for(std::size_t k=std::max(std::size_t(1),first);k<count;++k){const auto[a,b]=segmentAt(s,k);++stats.segments;
            const double left=std::min(a.x,b.x)-p,top=std::min(a.y,b.y)-p,right=std::max(a.x,b.x)+p,bottom=std::max(a.y,b.y)+p;
            const Pixels reach{long(std::floor(left*ppu)),long(std::floor(top*ppu)),long(std::ceil(right*ppu)),long(std::ceil(bottom*ppu))};
            if(intersect(reach,{0,0,long(width),long(height)}).empty())continue;
            const auto minX=unsigned(std::clamp(std::floor(left*ppu/tilePixels),0.,double(columns-1))),maxX=unsigned(std::clamp(std::floor(right*ppu/tilePixels),0.,double(columns-1)));
            const auto minY=unsigned(std::clamp(std::floor(top*ppu/tilePixels),0.,double(rows-1))),maxY=unsigned(std::clamp(std::floor(bottom*ppu/tilePixels),0.,double(rows-1)));
            for(auto y=minY;y<=maxY;++y)for(auto x=minX;x<=maxX;++x){const auto id=y*columns+x;if(!meets(a,b,bounds(id),p))continue;const auto inside=intersect(reach,tile(id));if(!inside.empty())f(id,inside);}
        }
    }
    void mark(unsigned id,Pixels p){if(whole)return;auto&d=dirty[id];if(!d.any){d.any=true;d.pixels=p;dirtyTiles.push_back(id);}else d.pixels=unite(d.pixels,p);}
    static void add(std::vector<unsigned>&sorted,unsigned id){const auto at=std::lower_bound(sorted.begin(),sorted.end(),id);if(at==sorted.end()||*at!=id)sorted.insert(at,id);}
    // A stroke's tiles (sorted), marking the pixels it reaches when `paint`.
    std::vector<unsigned>plan(const DrawingStroke&s,bool paint){std::vector<unsigned>tiles;footprint(s,0,[&](unsigned id,Pixels p){add(tiles,id);if(paint)mark(id,p);});return tiles;}
    void unpaint(const DrawingStroke&s){footprint(s,0,[&](unsigned id,Pixels p){mark(id,p);});}
    void rebuildMembers(){for(auto&m:members)m.clear();for(std::size_t n=0;n<records.size();++n)for(auto id:records[n]->tiles)members[id].push_back(std::uint32_t(n));}
    void clearDirty(){for(auto id:dirtyTiles)dirty[id]={};dirtyTiles.clear();}
    J::Array path(const DrawingStroke&s,R area)const{
        J::Array result;bool joined{};const auto p=pad(s);
        for(std::size_t k=1,count=segments(s);k<count;++k){const auto[a,b]=segmentAt(s,k);
            if(meets(a,b,area,p)){if(!joined)result.push_back(command("move",{a.x-area.x,a.y-area.y}));result.push_back(command("line",{b.x-area.x,b.y-area.y}));joined=true;}else joined=false;}
        return result;
    }
    J stroke(const DrawingStroke&s,J::Array path,R area,std::string id)const{
        return J::Object{{"id",std::move(id)},{"kind","shape"},{"class","CAShapeLayer"},{"bounds",box(area.width,area.height)},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"opacity",1},
            {"shape",J::Object{{"path",std::move(path)},{"fillColor",J{}},{"strokeColor",J::Object{{"sRGB",J::Array{s.color[0],s.color[1],s.color[2],s.color[3]}}}},
                {"lineWidth",s.width*widthScale},{"lineCap","round"},{"lineJoin","round"},{"fillRule","non-zero"}}}};
    }
};
NotesDrawingInkPlan::NotesDrawingInkPlan():impl_(std::make_unique<Impl>()){}
NotesDrawingInkPlan::~NotesDrawingInkPlan()=default;
bool NotesDrawingInkPlan::setGeometry(P extent,double widthScale,double ppu){auto&i=*impl_;
    need(std::isfinite(extent.x)&&std::isfinite(extent.y)&&extent.x>0&&extent.y>0&&std::isfinite(widthScale)&&widthScale>0&&widthScale<=1&&std::isfinite(ppu)&&ppu>=.25&&ppu<=4,"Invalid Notes drawing ink geometry");
    // The rasterizer's own pixel extent of a layer of these bounds.
    const double w=std::ceil(std::max(1/ppu,extent.x)*ppu),h=std::ceil(std::max(1/ppu,extent.y)*ppu);
    need(w<=8192&&h<=8192&&w*h<=4096.*4096,"Notes drawing ink exceeds the texture limits");
    if(i.ready&&i.size==extent&&i.widthScale==widthScale&&i.ppu==ppu)return false;
    i.size=extent;i.widthScale=widthScale;i.ppu=ppu;i.width=unsigned(w);i.height=unsigned(h);i.columns=(i.width+tilePixels-1)/tilePixels;i.rows=(i.height+tilePixels-1)/tilePixels;
    i.members.assign(i.tileCount(),{});i.dirty.assign(i.tileCount(),{});i.dirtyTiles.clear();i.dirtyTiles.reserve(i.tileCount());i.out.reserve(i.tileCount());i.liveTiles.reserve(i.tileCount());
    i.whole=true;i.ready=true;
    for(auto&r:i.records)r->tiles=i.plan(r->stroke,false);
    i.rebuildMembers();i.liveTiles.clear();if(i.livePresent){const auto tiles=i.plan(i.live,false);i.liveTiles.assign(tiles.begin(),tiles.end());}
    return true;
}
bool NotesDrawingInkPlan::setCompleted(std::span<const DrawingStroke>strokes,std::uint64_t revision){auto&i=*impl_;need(i.ready&&revision,"Set the Notes drawing ink geometry and a revision first");
    if(i.completedSet&&revision==i.completedRevision)return false;
    need(strokes.size()<=NotesDrawing::maximumStrokes,"Notes drawing stroke limit exceeded");std::size_t points{};
    for(const auto&s:strokes){need(s.valid()&&s.points.size()<=NotesDrawing::maximumPoints-points,"Invalid Notes drawing strokes");points+=s.points.size();}
    std::vector<std::shared_ptr<Impl::Record>>next;next.reserve(strokes.size());std::vector<char>used(i.records.size());std::size_t search{};bool adopted{};
    for(std::size_t n=0;n<strokes.size();++n){const auto&s=strokes[n];std::size_t found=i.records.size();
        for(auto k=search;k<i.records.size();++k)if(!used[k]&&i.records[k]->stroke==s){found=k;break;}
        if(found<i.records.size()){used[found]=1;search=found+1;next.push_back(i.records[found]);continue;}
        auto record=std::make_shared<Impl::Record>();record->stroke=s;
        // The live stroke committed as the last completed one is already
        // painted exactly where it lies (on top of every earlier stroke).
        if(i.livePresent&&!adopted&&n+1==strokes.size()&&s==i.live){record->tiles=i.liveTiles;adopted=true;++i.stats.adoptions;}
        else record->tiles=i.plan(s,true);
        next.push_back(std::move(record));
    }
    // A removed stroke's footprint is painted again from what remains.
    for(std::size_t k=0;k<i.records.size();++k)if(!used[k])i.unpaint(i.records[k]->stroke);
    i.records=std::move(next);i.rebuildMembers();
    if(adopted){i.livePresent=false;i.live.points.clear();i.liveTiles.clear();}
    i.completedRevision=revision;i.completedSet=true;++i.stats.completedUpdates;return true;
}
bool NotesDrawingInkPlan::setLive(const DrawingStroke*stroke,std::uint64_t revision){auto&i=*impl_;need(i.ready,"Set the Notes drawing ink geometry first");
    if(!stroke||stroke->points.empty()){i.liveRevision=revision;if(!i.livePresent)return false;i.unpaint(i.live);i.livePresent=false;i.live.points.clear();i.liveTiles.clear();++i.stats.liveUpdates;return true;}
    if(i.livePresent&&revision==i.liveRevision)return false;
    need(stroke->valid(),"Invalid live Notes drawing stroke");
    const auto old=i.live.points.size();
    const bool prefix=i.livePresent&&i.live.width==stroke->width&&i.live.color==stroke->color&&stroke->points.size()>=old&&std::equal(i.live.points.begin(),i.live.points.end(),stroke->points.begin());
    i.liveRevision=revision;if(prefix&&stroke->points.size()==old)return false;
    if(i.livePresent&&!prefix){i.unpaint(i.live);i.liveTiles.clear();}
    if(i.live.points.capacity()<NotesDrawing::maximumPointsPerStroke)i.live.points.reserve(NotesDrawing::maximumPointsPerStroke);
    if(prefix)i.live.points.insert(i.live.points.end(),stroke->points.begin()+std::ptrdiff_t(old),stroke->points.end());else i.live.points.assign(stroke->points.begin(),stroke->points.end());
    i.live.width=stroke->width;i.live.color=stroke->color;i.livePresent=true;
    // A growing stroke plans only its new segments (from the last segment it
    // already had, whose end becomes a join); another stroke all of its own.
    i.footprint(i.live,prefix?std::max(std::size_t(1),old-1):0,[&](unsigned id,Pixels p){Impl::add(i.liveTiles,id);i.mark(id,p);});
    ++i.stats.liveUpdates;return true;
}
bool NotesDrawingInkPlan::whole()const noexcept{return impl_->whole;}
std::span<const NotesInkPixels>NotesDrawingInkPlan::dirty(){auto&i=*impl_;i.out.clear();if(i.whole)return {};
    for(auto id:i.dirtyTiles){const auto&p=i.dirty[id].pixels;i.out.push_back({unsigned(p.left),unsigned(p.top),unsigned(p.right-p.left),unsigned(p.bottom-p.top)});}return i.out;}
std::uint64_t NotesDrawingInkPlan::dirtyPixels()const noexcept{const auto&i=*impl_;if(i.whole)return std::uint64_t(i.width)*i.height;std::uint64_t total{};for(auto id:i.dirtyTiles){const auto&p=i.dirty[id].pixels;total+=std::uint64_t(p.right-p.left)*std::uint64_t(p.bottom-p.top);}return total;}
void NotesDrawingInkPlan::painted(){auto&i=*impl_;i.clearDirty();i.whole=false;}
void NotesDrawingInkPlan::invalidate(){auto&i=*impl_;i.clearDirty();i.whole=true;}
ehud::data::Json NotesDrawingInkPlan::content(NotesInkPixels rect,std::string id)const{const auto&i=*impl_;
    need(i.ready&&rect.width&&rect.height&&rect.x<i.width&&rect.y<i.height&&rect.width<=i.width-rect.x&&rect.height<=i.height-rect.y,"Notes drawing ink region lies outside its picture");
    const R area{double(rect.x)/i.ppu,double(rect.y)/i.ppu,double(rect.width)/i.ppu,double(rect.height)/i.ppu};
    const unsigned x0=rect.x/tilePixels,x1=(rect.x+rect.width-1)/tilePixels,y0=rect.y/tilePixels,y1=(rect.y+rect.height-1)/tilePixels;
    // Strokes reaching the rectangle's tiles, in paint order.
    std::vector<std::uint32_t>chosen;
    if(x0==x1&&y0==y1)chosen=i.members[y0*i.columns+x0];
    else if(x0==0&&y0==0&&x1+1==i.columns&&y1+1==i.rows){chosen.resize(i.records.size());for(std::size_t n=0;n<chosen.size();++n)chosen[n]=std::uint32_t(n);}
    else{for(auto y=y0;y<=y1;++y)for(auto x=x0;x<=x1;++x){const auto&m=i.members[y*i.columns+x];chosen.insert(chosen.end(),m.begin(),m.end());}std::sort(chosen.begin(),chosen.end());chosen.erase(std::unique(chosen.begin(),chosen.end()),chosen.end());}
    J::Array children;children.reserve(chosen.size()+1);
    for(auto n:chosen){const auto&s=i.records[n]->stroke;auto path=i.path(s,area);if(!path.empty())children.push_back(i.stroke(s,std::move(path),area,"stroke."+std::to_string(n)));}
    if(i.livePresent&&std::any_of(i.liveTiles.begin(),i.liveTiles.end(),[&](unsigned t){const auto x=t%i.columns,y=t/i.columns;return x>=x0&&x<=x1&&y>=y0&&y<=y1;})){auto path=i.path(i.live,area);if(!path.empty())children.push_back(i.stroke(i.live,std::move(path),area,"live"));}
    return J::Object{{"id",std::move(id)},{"kind","layer"},{"class","CALayer"},{"bounds",box(area.width,area.height)},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"opacity",1},
        {"masksToBounds",true},{"backgroundColor",J::Object{{"sRGB",J::Array{0,0,0,0}}}},{"children",std::move(children)}};
}
NotesInkPixels NotesDrawingInkPlan::picture()const noexcept{return {0,0,impl_->width,impl_->height};}
double NotesDrawingInkPlan::pixelsPerUnit()const noexcept{return impl_->ppu;}
core::Point NotesDrawingInkPlan::extent()const noexcept{return impl_->size;}
bool NotesDrawingInkPlan::ready()const noexcept{return impl_->ready;}
bool NotesDrawingInkPlan::livePresent()const noexcept{return impl_->livePresent;}
std::size_t NotesDrawingInkPlan::completedStrokes()const noexcept{return impl_->records.size();}
NotesInkPlanStats NotesDrawingInkPlan::stats()const noexcept{return impl_->stats;}
}
