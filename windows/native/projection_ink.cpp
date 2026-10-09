#include "native/projection_ink.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include "native/layer_scene.hpp"
#endif
namespace endfield::native {
namespace {
using Point=core::Point;using Rect=core::Rect;using Stroke=modules::DrawingStroke;using Json=ehud::data::Json;
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}bool segment(Point a,Point b,Rect r,double pad){r.x-=pad;r.y-=pad;r.width+=pad*2;r.height+=pad*2;double low=0,high=1;const auto clip=[&](double p,double q){if(p==0)return q>=0;const double v=q/p;if(p<0)low=std::max(low,v);else high=std::min(high,v);return low<=high;};const auto dx=b.x-a.x,dy=b.y-a.y;return clip(-dx,a.x-r.x)&&clip(dx,r.x+r.width-a.x)&&clip(-dy,a.y-r.y)&&clip(dy,r.y+r.height-a.y);}
Point scaled(Point p,Point size){return {p.x*size.x,p.y*size.y};}
Json command(const char*op,Point p){return Json::Object{{"op",op},{"points",Json::Array{Json(Json::Array{p.x,p.y})}}};}
Json strokeContent(const Stroke&s,Point size,Rect tile,std::size_t index){
    Json::Array path;bool joined{};const auto count=std::max(std::size_t(2),s.points.size());for(std::size_t k=1;k<count;++k){const auto a=scaled(s.points[k-1],size),b=s.points.size()==1?Point{a.x+.01,a.y}:scaled(s.points[k],size);if(segment(a,b,tile,s.width*.5+1)){if(!joined)path.push_back(command("move",{a.x-tile.x,a.y-tile.y}));path.push_back(command("line",{b.x-tile.x,b.y-tile.y}));joined=true;}else joined=false;}
    return Json::Object{{"id","stroke."+std::to_string(index)},{"kind","shape"},{"bounds",Json::Array{0,0,tile.width,tile.height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"shape",Json::Object{{"path",std::move(path)},{"fillColor",Json{}},{"strokeColor",Json::Object{{"sRGB",Json::Array{s.color[0],s.color[1],s.color[2],s.color[3]}}}},{"lineWidth",s.width},{"lineCap","round"},{"lineJoin","round"},{"fillRule","non-zero"}}}};
}
}
struct ProjectionInkPlan::Impl {
    struct Record {Stroke stroke;std::vector<unsigned>tiles;};
    Point size{};double scale{};unsigned pixelWidth{},pixelHeight{},columns{},rows{};std::uint64_t revision{},liveRevision{};bool ready{},livePresent{};
    std::vector<std::shared_ptr<const Record>>records;Stroke live;std::vector<unsigned>liveTiles;
    std::map<unsigned,std::vector<std::size_t>>members;std::vector<ProjectionInkTileChange>changes;
    Rect bounds(unsigned index)const{const unsigned x=(index%columns)*256,y=(index/columns)*256;return {x/scale,y/scale,std::min(256u,pixelWidth-x)/scale,std::min(256u,pixelHeight-y)/scale};}
    std::vector<unsigned>touched(const Stroke&s,std::size_t first=0)const{std::vector<bool>flags(std::size_t(columns)*rows);const auto count=std::max(std::size_t(2),s.points.size());for(std::size_t k=std::max(std::size_t(1),first);k<count;++k){const auto a=scaled(s.points[k-1],size),b=s.points.size()==1?Point{a.x+.01,a.y}:scaled(s.points[k],size);const auto pad=s.width*.5+1;const auto minX=unsigned(std::max(0.,std::floor((std::min(a.x,b.x)-pad)*scale/256))),minY=unsigned(std::max(0.,std::floor((std::min(a.y,b.y)-pad)*scale/256)));const auto maxX=unsigned(std::min(double(columns-1),std::floor((std::max(a.x,b.x)+pad)*scale/256))),maxY=unsigned(std::min(double(rows-1),std::floor((std::max(a.y,b.y)+pad)*scale/256)));
            for(auto y=minY;y<=maxY;++y)for(auto x=minX;x<=maxX;++x){const auto id=y*columns+x;if(!flags[id]&&segment(a,b,bounds(id),pad))flags[id]=true;}}
        std::vector<unsigned>out;for(unsigned n=0;n<flags.size();++n)if(flags[n])out.push_back(n);return out;}
};
ProjectionInkPlan::ProjectionInkPlan():impl_(std::make_unique<Impl>()){}ProjectionInkPlan::~ProjectionInkPlan()=default;
bool ProjectionInkPlan::update(std::span<const Stroke>strokes,std::uint64_t revision,const Stroke*live,std::uint64_t liveRevision,Point size,double scale){auto&i=*impl_;need(revision&&std::isfinite(scale)&&scale>=.25&&scale<=4&&std::isfinite(size.x)&&std::isfinite(size.y)&&size.x>0&&size.y>0,"Invalid Projection ink geometry/revision");const double width=std::ceil(size.x*scale),height=std::ceil(size.y*scale);need(width>=1&&height>=1&&width<=8192&&height<=8192&&width*height<=4096.*4096,"Projection ink exceeds existing renderer viewport limits");const bool resized=!i.ready||i.size!=size||i.scale!=scale;const bool changed=resized||revision!=i.revision,liveChanged=resized||bool(live)!=i.livePresent||(live&&liveRevision!=i.liveRevision);if(!changed&&!liveChanged){i.changes.clear();return false;}
    need(strokes.size()<=modules::NotesDrawing::maximumStrokes,"Projection source stroke count exceeded");if(liveChanged&&live)need(liveRevision&&live->valid(),"Invalid live Projection stroke");if(changed){std::size_t points{};for(const auto&s:strokes){need(s.valid()&&s.points.size()<=modules::NotesDrawing::maximumPoints-points,"Invalid Projection source strokes");points+=s.points.size();}}
    std::set<unsigned>dirty,dirtyLive;for(const auto&[id,_]:i.members)if(resized)dirty.insert(id);if(resized)dirtyLive.insert(i.liveTiles.begin(),i.liveTiles.end());
    if(resized){i.size=size;i.scale=scale;i.pixelWidth=unsigned(width);i.pixelHeight=unsigned(height);i.columns=(i.pixelWidth+255)/256;i.rows=(i.pixelHeight+255)/256;}
    if(changed){std::vector<std::shared_ptr<const Impl::Record>>next;next.reserve(strokes.size());std::vector<bool>used(i.records.size());std::size_t search{};for(const auto&s:strokes){std::size_t found=i.records.size();if(!resized){for(auto n=search;n<i.records.size();++n)if(!used[n]&&i.records[n]->stroke==s){found=n;break;}}
            if(found<i.records.size()){used[found]=true;search=found+1;next.push_back(i.records[found]);}else{auto record=std::make_shared<Impl::Record>();record->stroke=s;record->tiles=i.touched(s);dirty.insert(record->tiles.begin(),record->tiles.end());next.push_back(std::move(record));}}
        for(std::size_t k=0;k<i.records.size();++k)if(!used[k])dirty.insert(i.records[k]->tiles.begin(),i.records[k]->tiles.end());i.records=std::move(next);i.members.clear();for(std::size_t n=0;n<i.records.size();++n)for(auto id:i.records[n]->tiles)i.members[id].push_back(n);i.revision=revision;
    }
    if(liveChanged){const bool prefix=live&&!resized&&i.livePresent&&i.live.width==live->width&&i.live.color==live->color&&live->points.size()>=i.live.points.size()&&std::equal(i.live.points.begin(),i.live.points.end(),live->points.begin());
        if(!prefix)dirtyLive.insert(i.liveTiles.begin(),i.liveTiles.end());if(live){const auto tail=i.touched(*live,prefix?std::max(std::size_t(1),i.live.points.size()-1):0);dirtyLive.insert(tail.begin(),tail.end());i.liveTiles=i.touched(*live);if(i.live.points.capacity()<modules::NotesDrawing::maximumPointsPerStroke)i.live.points.reserve(modules::NotesDrawing::maximumPointsPerStroke);i.live.width=live->width;i.live.color=live->color;i.live.points.assign(live->points.begin(),live->points.end());}else{i.liveTiles.clear();i.live.points.clear();}i.livePresent=bool(live);i.liveRevision=liveRevision;}
    i.changes.clear();i.changes.reserve(dirty.size()+dirtyLive.size());for(auto id:dirty){const bool valid=id<std::size_t(i.columns)*i.rows;i.changes.push_back({id,false,!valid||!i.members.contains(id),valid?i.bounds(id):Rect{}});}for(auto id:dirtyLive){const bool valid=id<std::size_t(i.columns)*i.rows;i.changes.push_back({id,true,!valid||!std::binary_search(i.liveTiles.begin(),i.liveTiles.end(),id),valid?i.bounds(id):Rect{}});}i.ready=true;return true;
}
std::span<const ProjectionInkTileChange>ProjectionInkPlan::changes()const noexcept{return impl_->changes;}
Json ProjectionInkPlan::content(const ProjectionInkTileChange&change,std::string id)const{const auto&i=*impl_;need(i.ready&&!change.empty&&change.index<std::size_t(i.columns)*i.rows&&change.bounds==i.bounds(change.index),"Projection tile is stale or empty");Json::Array children;
    if(change.live){need(i.livePresent&&std::binary_search(i.liveTiles.begin(),i.liveTiles.end(),change.index),"Missing live Projection tile");children.push_back(strokeContent(i.live,i.size,change.bounds,0));}else{const auto found=i.members.find(change.index);need(found!=i.members.end(),"Missing completed Projection tile");for(auto n:found->second)children.push_back(strokeContent(i.records[n]->stroke,i.size,change.bounds,n));}
    return Json::Object{{"id",std::move(id)},{"kind","layer"},{"bounds",Json::Array{0,0,change.bounds.width,change.bounds.height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"masksToBounds",true},{"allowsGroupOpacity",false},{"children",std::move(children)}};
}
Point ProjectionInkPlan::workspace()const noexcept{return impl_->size;}double ProjectionInkPlan::pixelScale()const noexcept{return impl_->scale;}std::size_t ProjectionInkPlan::completedTiles()const noexcept{return impl_->members.size();}std::size_t ProjectionInkPlan::liveTiles()const noexcept{return impl_->liveTiles.size();}std::size_t ProjectionInkPlan::maximumTilesPerPlane()const noexcept{return std::size_t(impl_->columns)*impl_->rows;}
#ifdef _WIN32
struct NativeProjectionInk::Impl {
    struct Tile{LayerScene scene;Rect bounds;std::string id;std::pair<bool,unsigned> key;std::uint64_t revision{1};LayerPlacement placement;Tile(LayerRasterizer&r,std::string name,std::pair<bool,unsigned> slot):scene(r),id(std::move(name)),key(slot){}};
    LayerRasterizer&raster;LayerRasterOptions options;ProjectionInkPlan plan;std::map<std::pair<bool,unsigned>,std::unique_ptr<Tile>>tiles;std::vector<std::unique_ptr<Tile>>retired;std::vector<LayerCompositionEntry>entries;core::Matrix4 world;float opacity{1};ProjectionInkStats stats;bool pending{},pendingLive{};std::uint64_t pendingRevision{},pendingLiveRevision{};
    Impl(LayerRasterizer&r,LayerRasterOptions o):raster(r),options(std::move(o)){options.paddingPoints=0;entries.reserve(512);retired.reserve(512);}
    void pose(Tile&t){t.placement.world=world*core::Matrix4::translation(t.bounds.x,t.bounds.y);t.placement.opacity=opacity;t.scene.setPlacements(std::span(&t.placement,1));}
};
NativeProjectionInk::NativeProjectionInk(LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}NativeProjectionInk::~NativeProjectionInk()=default;
bool NativeProjectionInk::update(std::span<const Stroke>strokes,std::uint64_t rev,const Stroke*live,std::uint64_t lr,Point size,double scale){auto&i=*impl_;if(!i.pending){if(!i.plan.update(strokes,rev,live,lr,size,scale))return false;i.pending=true;i.pendingRevision=rev;i.pendingLiveRevision=lr;i.pendingLive=bool(live);}else{need(i.pendingRevision==rev&&i.pendingLiveRevision==lr&&i.pendingLive==bool(live)&&i.plan.workspace()==size&&i.plan.pixelScale()==scale,"Retry pending Projection tile content before new document content");}i.options.pixelsPerPoint=scale;i.world.values[0]=i.world.values[5]=scale;for(const auto&change:i.plan.changes()){const auto key=std::pair{change.live,change.index};auto found=i.tiles.find(key);if(change.empty){if(found!=i.tiles.end()){i.retired.push_back(std::move(found->second));i.tiles.erase(found);}continue;}const auto name=std::string(change.live?"projection.live.":"projection.ink.")+std::to_string(change.index);const auto content=i.plan.content(change,name);
        if(found==i.tiles.end()){const auto reusable=std::find_if(i.retired.begin(),i.retired.end(),[&](const auto&t){return t->key==key;});if(reusable!=i.retired.end()){found=i.tiles.try_emplace(key).first;found->second=std::move(*reusable);i.retired.erase(reusable);}}
        if(found==i.tiles.end()){auto next=std::make_unique<Impl::Tile>(i.raster,name,key);next->scene.load(content,i.options);need(next->scene.report().unsupported.empty()&&next->scene.draws().size()==1,"Projection ink tile has unsupported source effects");next->bounds=change.bounds;next->placement.surface=0;i.pose(*next);found=i.tiles.emplace(key,std::move(next)).first;}else{auto&t=*found->second;t.scene.updateLocalContent(t.id,++t.revision,content,i.options);t.bounds=change.bounds;i.pose(t);}++i.stats.tileRasters;i.stats.rasterPixelBytes+=found->second->scene.report().pixelBytes;}
    i.entries.clear();for(auto&[_,tile]:i.tiles)i.entries.push_back({&tile->scene,{}});i.pending=false;return true;
}
bool NativeProjectionInk::setPose(const core::Matrix4&w,float opacity){auto&i=*impl_;need(w.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid Projection ink pose");auto expected=core::Matrix4{};expected.values[0]=expected.values[5]=i.plan.pixelScale();expected.values[12]=w.values[12];expected.values[13]=w.values[13];need(i.plan.pixelScale()>0&&w==expected&&std::floor(w.values[12])==w.values[12]&&std::floor(w.values[13])==w.values[13],"Projection ink requires original pixel-aligned DPI placement");if(i.world==w&&i.opacity==opacity)return false;i.world=w;i.opacity=opacity;for(auto&[_,tile]:i.tiles)i.pose(*tile);++i.stats.poses;return true;}
std::span<const LayerCompositionEntry>NativeProjectionInk::entries()const noexcept{return impl_->entries;}
bool NativeProjectionInk::collectRetired(Renderer&r){auto&i=*impl_;bool okay=true;std::erase_if(i.retired,[&](auto&t){const auto done=t->scene.releaseResources(r);okay&=done;return done;});for(auto&[_,t]:i.tiles)t->scene.collectRetiredResources(r);return okay;}
bool NativeProjectionInk::releaseResources(Renderer&r){auto&i=*impl_;bool okay=collectRetired(r);for(auto&[_,t]:i.tiles)okay=t->scene.releaseResources(r)&&okay;return okay;}
ProjectionInkStats NativeProjectionInk::stats()const noexcept{auto value=impl_->stats;value.completedTiles=impl_->plan.completedTiles();value.liveTiles=impl_->plan.liveTiles();value.retiredTiles=impl_->retired.size();return value;}
#endif
}
