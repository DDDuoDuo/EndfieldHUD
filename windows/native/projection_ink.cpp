#include "native/projection_ink.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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
Json::Array strokePath(const Stroke&s,Point size,Rect area){
    Json::Array path;bool joined{};const auto count=std::max(std::size_t(2),s.points.size());for(std::size_t k=1;k<count;++k){const auto a=scaled(s.points[k-1],size),b=s.points.size()==1?Point{a.x+.01,a.y}:scaled(s.points[k],size);if(segment(a,b,area,s.width*.5+1)){if(!joined)path.push_back(command("move",{a.x-area.x,a.y-area.y}));path.push_back(command("line",{b.x-area.x,b.y-area.y}));joined=true;}else joined=false;}
    return path;
}
Json strokeContent(const Stroke&s,Json::Array path,Rect area,std::size_t index){
    return Json::Object{{"id","stroke."+std::to_string(index)},{"kind","shape"},{"bounds",Json::Array{0,0,area.width,area.height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"shape",Json::Object{{"path",std::move(path)},{"fillColor",Json{}},{"strokeColor",Json::Object{{"sRGB",Json::Array{s.color[0],s.color[1],s.color[2],s.color[3]}}}},{"lineWidth",s.width},{"lineCap","round"},{"lineJoin","round"},{"fillRule","non-zero"}}}};
}
Json strokeContent(const Stroke&s,Point size,Rect tile,std::size_t index){return strokeContent(s,strokePath(s,size,tile),tile,index);}
Json layer(std::string id,Rect area,Json::Array children){
    return Json::Object{{"id",std::move(id)},{"kind","layer"},{"bounds",Json::Array{0,0,area.width,area.height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"masksToBounds",true},{"allowsGroupOpacity",false},{"children",std::move(children)}};
}
// A half-open rectangle of workspace physical pixels.
struct Pixels {long left{},top{},right{},bottom{};bool empty()const{return right<=left||bottom<=top;}};
Pixels unite(Pixels a,Pixels b){return {std::min(a.left,b.left),std::min(a.top,b.top),std::max(a.right,b.right),std::max(a.bottom,b.bottom)};}
Pixels intersect(Pixels a,Pixels b){return {std::max(a.left,b.left),std::max(a.top,b.top),std::min(a.right,b.right),std::min(a.bottom,b.bottom)};}
// Changed pixels of one tile: a union of footprints, or the whole tile.
struct Dirty {bool whole{},any{};Pixels pixels;};
void markWhole(std::map<unsigned,Dirty>&dirty,unsigned id){dirty[id].whole=true;}
void markRegion(std::map<unsigned,Dirty>&dirty,unsigned id,Pixels pixels){auto&d=dirty[id];d.pixels=d.any?unite(d.pixels,pixels):pixels;d.any=true;}
}
struct ProjectionInkPlan::Impl {
    struct Record {Stroke stroke;std::vector<unsigned>tiles;};
    Point size{};double scale{};unsigned pixelWidth{},pixelHeight{},columns{},rows{};std::uint64_t revision{},liveRevision{};bool ready{},livePresent{};
    std::vector<std::shared_ptr<const Record>>records;Stroke live;std::vector<unsigned>liveTiles;
    std::map<unsigned,std::vector<std::size_t>>members;std::vector<ProjectionInkTileChange>changes;
    Rect bounds(unsigned index)const{const unsigned x=(index%columns)*256,y=(index/columns)*256;return {x/scale,y/scale,std::min(256u,pixelWidth-x)/scale,std::min(256u,pixelHeight-y)/scale};}
    Pixels tile(unsigned index)const{const long x=long(index%columns)*256,y=long(index/columns)*256;return {x,y,std::min(x+256,long(pixelWidth)),std::min(y+256,long(pixelHeight))};}
    // Every tile a stroke's segments from `first` reach (the source pad: half
    // the brush and one point of antialiasing), with the physical pixels they
    // reach inside each one.
    void footprint(const Stroke&s,std::size_t first,std::map<unsigned,Pixels>&out)const{
        const auto count=std::max(std::size_t(2),s.points.size());const auto pad=s.width*.5+1;
        for(std::size_t k=std::max(std::size_t(1),first);k<count;++k){const auto a=scaled(s.points[k-1],size),b=s.points.size()==1?Point{a.x+.01,a.y}:scaled(s.points[k],size);
            const double left=std::min(a.x,b.x)-pad,top=std::min(a.y,b.y)-pad,right=std::max(a.x,b.x)+pad,bottom=std::max(a.y,b.y)+pad;
            const auto minX=unsigned(std::max(0.,std::floor(left*scale/256))),minY=unsigned(std::max(0.,std::floor(top*scale/256)));
            const auto maxX=unsigned(std::min(double(columns-1),std::floor(right*scale/256))),maxY=unsigned(std::min(double(rows-1),std::floor(bottom*scale/256)));
            const Pixels reach{long(std::floor(left*scale)),long(std::floor(top*scale)),long(std::ceil(right*scale)),long(std::ceil(bottom*scale))};
            for(auto y=minY;y<=maxY;++y)for(auto x=minX;x<=maxX;++x){const auto id=y*columns+x;if(!segment(a,b,bounds(id),pad))continue;const auto inside=intersect(reach,tile(id));if(inside.empty())continue;
                const auto found=out.find(id);if(found==out.end())out.emplace(id,inside);else found->second=unite(found->second,inside);}}
    }
    std::vector<unsigned>touched(const Stroke&s,std::size_t first=0)const{std::map<unsigned,Pixels>reach;footprint(s,first,reach);std::vector<unsigned>out;out.reserve(reach.size());for(const auto&[id,_]:reach)out.push_back(id);return out;}
    ProjectionInkTileChange change(unsigned id,bool live,bool empty,const Dirty&d)const{
        const bool valid=id<std::size_t(columns)*rows;ProjectionInkTileChange c{id,live,empty||!valid,valid?bounds(id):Rect{},{},true};
        if(!valid)return c;const auto t=tile(id);c.region={0,0,unsigned(t.right-t.left),unsigned(t.bottom-t.top)};
        if(!d.whole&&d.any){const auto r=intersect(d.pixels,t);if(!r.empty()){c.whole=false;c.region={unsigned(r.left-t.left),unsigned(r.top-t.top),unsigned(r.right-r.left),unsigned(r.bottom-r.top)};}}
        return c;
    }
};
ProjectionInkPlan::ProjectionInkPlan():impl_(std::make_unique<Impl>()){}ProjectionInkPlan::~ProjectionInkPlan()=default;
bool ProjectionInkPlan::update(std::span<const Stroke>strokes,std::uint64_t revision,const Stroke*live,std::uint64_t liveRevision,Point size,double scale){auto&i=*impl_;need(revision&&std::isfinite(scale)&&scale>=.25&&scale<=4&&std::isfinite(size.x)&&std::isfinite(size.y)&&size.x>0&&size.y>0,"Invalid Projection ink geometry/revision");const double width=std::ceil(size.x*scale),height=std::ceil(size.y*scale);need(width>=1&&height>=1&&width<=8192&&height<=8192&&width*height<=4096.*4096,"Projection ink exceeds existing renderer viewport limits");const bool resized=!i.ready||i.size!=size||i.scale!=scale;const bool changed=resized||revision!=i.revision,liveChanged=resized||bool(live)!=i.livePresent||(live&&liveRevision!=i.liveRevision);if(!changed&&!liveChanged){i.changes.clear();return false;}
    need(strokes.size()<=modules::NotesDrawing::maximumStrokes,"Projection source stroke count exceeded");if(liveChanged&&live)need(liveRevision&&live->valid(),"Invalid live Projection stroke");if(changed){std::size_t points{};for(const auto&s:strokes){need(s.valid()&&s.points.size()<=modules::NotesDrawing::maximumPoints-points,"Invalid Projection source strokes");points+=s.points.size();}}
    // A new geometry repaints every retained tile whole (old indices included).
    std::map<unsigned,Dirty>dirty,dirtyLive;if(resized){for(const auto&[id,_]:i.members)markWhole(dirty,id);for(auto id:i.liveTiles)markWhole(dirtyLive,id);}
    if(resized){i.size=size;i.scale=scale;i.pixelWidth=unsigned(width);i.pixelHeight=unsigned(height);i.columns=(i.pixelWidth+255)/256;i.rows=(i.pixelHeight+255)/256;}
    if(changed){std::vector<std::shared_ptr<const Impl::Record>>next;next.reserve(strokes.size());std::vector<bool>used(i.records.size());std::size_t search{};std::map<unsigned,Pixels>reach;
        for(const auto&s:strokes){std::size_t found=i.records.size();if(!resized){for(auto n=search;n<i.records.size();++n)if(!used[n]&&i.records[n]->stroke==s){found=n;break;}}
            if(found<i.records.size()){used[found]=true;search=found+1;next.push_back(i.records[found]);}
            else{auto record=std::make_shared<Impl::Record>();record->stroke=s;reach.clear();i.footprint(s,0,reach);for(const auto&[id,pixels]:reach){record->tiles.push_back(id);if(resized)markWhole(dirty,id);else markRegion(dirty,id,pixels);}next.push_back(std::move(record));}}
        // A removed stroke's footprint is painted again from what remains.
        for(std::size_t k=0;k<i.records.size();++k)if(!used[k]){if(resized)continue;reach.clear();i.footprint(i.records[k]->stroke,0,reach);for(const auto&[id,pixels]:reach)markRegion(dirty,id,pixels);}
        i.records=std::move(next);i.members.clear();for(std::size_t n=0;n<i.records.size();++n)for(auto id:i.records[n]->tiles)i.members[id].push_back(n);i.revision=revision;
    }
    if(liveChanged){const bool prefix=live&&!resized&&i.livePresent&&i.live.width==live->width&&i.live.color==live->color&&live->points.size()>=i.live.points.size()&&std::equal(i.live.points.begin(),i.live.points.end(),live->points.begin());
        if(!prefix)for(auto id:i.liveTiles)markWhole(dirtyLive,id);
        if(live){
            // A growing stroke paints only its newest segments (from the last
            // segment it already had); another stroke paints its own tiles.
            std::map<unsigned,Pixels>tail;i.footprint(*live,prefix?std::max(std::size_t(1),i.live.points.size()-1):0,tail);
            for(const auto&[id,pixels]:tail)markRegion(dirtyLive,id,pixels);
            if(prefix){std::vector<unsigned>merged;merged.reserve(i.liveTiles.size()+tail.size());auto t=tail.begin();for(auto id:i.liveTiles){for(;t!=tail.end()&&t->first<id;++t)merged.push_back(t->first);if(t!=tail.end()&&t->first==id)++t;merged.push_back(id);}for(;t!=tail.end();++t)merged.push_back(t->first);i.liveTiles=std::move(merged);}
            else{i.liveTiles.clear();for(const auto&[id,_]:tail)i.liveTiles.push_back(id);}
            if(i.live.points.capacity()<modules::NotesDrawing::maximumPointsPerStroke)i.live.points.reserve(modules::NotesDrawing::maximumPointsPerStroke);i.live.width=live->width;i.live.color=live->color;
            if(prefix)i.live.points.insert(i.live.points.end(),live->points.begin()+std::ptrdiff_t(i.live.points.size()),live->points.end());else i.live.points.assign(live->points.begin(),live->points.end());
        }else{i.liveTiles.clear();i.live.points.clear();}i.livePresent=bool(live);i.liveRevision=liveRevision;}
    i.changes.clear();i.changes.reserve(dirty.size()+dirtyLive.size());
    for(const auto&[id,d]:dirty){const auto c=i.change(id,false,!i.members.contains(id),d);i.changes.push_back(c);}
    for(const auto&[id,d]:dirtyLive){const auto c=i.change(id,true,!std::binary_search(i.liveTiles.begin(),i.liveTiles.end(),id),d);i.changes.push_back(c);}
    i.ready=true;return true;
}
std::span<const ProjectionInkTileChange>ProjectionInkPlan::changes()const noexcept{return impl_->changes;}
Json ProjectionInkPlan::content(const ProjectionInkTileChange&change,std::string id)const{const auto&i=*impl_;need(i.ready&&!change.empty&&change.index<std::size_t(i.columns)*i.rows&&change.bounds==i.bounds(change.index),"Projection tile is stale or empty");Json::Array children;
    if(change.live){need(i.livePresent&&std::binary_search(i.liveTiles.begin(),i.liveTiles.end(),change.index),"Missing live Projection tile");children.push_back(strokeContent(i.live,i.size,change.bounds,0));}else{const auto found=i.members.find(change.index);need(found!=i.members.end(),"Missing completed Projection tile");for(auto n:found->second)children.push_back(strokeContent(i.records[n]->stroke,i.size,change.bounds,n));}
    return layer(std::move(id),change.bounds,std::move(children));
}
Json ProjectionInkPlan::regionContent(const ProjectionInkTileChange&change,std::string id)const{const auto&i=*impl_;need(i.ready&&!change.empty&&change.index<std::size_t(i.columns)*i.rows&&change.bounds==i.bounds(change.index),"Projection tile is stale or empty");
    const auto t=i.tile(change.index);need(change.region.width&&change.region.height&&change.region.x+change.region.width<=unsigned(t.right-t.left)&&change.region.y+change.region.height<=unsigned(t.bottom-t.top),"Projection region lies outside its tile");
    const Rect area{double(t.left+long(change.region.x))/i.scale,double(t.top+long(change.region.y))/i.scale,double(change.region.width)/i.scale,double(change.region.height)/i.scale};Json::Array children;
    const auto add=[&](const Stroke&s,std::size_t n){auto path=strokePath(s,i.size,area);if(!path.empty())children.push_back(strokeContent(s,std::move(path),area,n));};
    if(change.live){need(i.livePresent&&std::binary_search(i.liveTiles.begin(),i.liveTiles.end(),change.index),"Missing live Projection tile");add(i.live,0);}else{const auto found=i.members.find(change.index);need(found!=i.members.end(),"Missing completed Projection tile");for(auto n:found->second)add(i.records[n]->stroke,n);}
    return layer(std::move(id),area,std::move(children));
}
ProjectionInkRegion ProjectionInkPlan::tilePixels(unsigned index)const{const auto&i=*impl_;need(i.ready&&index<std::size_t(i.columns)*i.rows,"Projection tile outside the workspace");const auto t=i.tile(index);return {0,0,unsigned(t.right-t.left),unsigned(t.bottom-t.top)};}
Point ProjectionInkPlan::workspace()const noexcept{return impl_->size;}double ProjectionInkPlan::pixelScale()const noexcept{return impl_->scale;}std::size_t ProjectionInkPlan::completedTiles()const noexcept{return impl_->members.size();}std::size_t ProjectionInkPlan::liveTiles()const noexcept{return impl_->liveTiles.size();}std::size_t ProjectionInkPlan::maximumTilesPerPlane()const noexcept{return std::size_t(impl_->columns)*impl_->rows;}
#ifdef _WIN32
namespace {std::atomic<std::uint64_t> inkInstances{0};}
struct NativeProjectionInk::Impl {
    // A painted region waiting for upload (its raster keeps the patch alive).
    struct Patch{std::shared_ptr<const LayerRasterImage>image;ProjectionInkRegion region;};
    // One retained tile: an empty carrier scene publishes its single draw
    // (a pixel-exact quad over its own updatable texture) in paint order.
    struct Tile{LayerScene carrier;std::string id;std::pair<bool,unsigned> key;Rect bounds;ProjectionInkRegion size;DrawObject draw;std::uint64_t meshRevision{},textureRevision{};unsigned textureWidth{},textureHeight{};
        bool mesh{},texture{},clear{},repaint{};std::vector<Patch>patches;
        Tile(LayerRasterizer&r,std::string name,std::pair<bool,unsigned> slot,const LayerRasterOptions&o):carrier(r),id(std::move(name)),key(slot){
            carrier.load(Json::Object{{"id",id+".carrier"},{"kind","layer"},{"bounds",Json::Array{0,0,0,0}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}},o);
            need(carrier.draws().empty(),"Projection ink carrier draws nothing itself");draw.sourceID=draw.meshID=draw.textureID=id;}};
    LayerRasterizer&raster;LayerRasterOptions options;ProjectionInkPlan plan;std::map<std::pair<bool,unsigned>,std::unique_ptr<Tile>>tiles;std::vector<std::unique_ptr<Tile>>retired;std::vector<LayerCompositionEntry>entries;core::Matrix4 world;float opacity{1};ProjectionInkStats stats;bool pending{},pendingLive{};std::uint64_t pendingRevision{},pendingLiveRevision{};
    // Renderer and raster identities are this instance's own (a second ink
    // on the same renderer, such as a whole-repaint reference, never shares one).
    std::string prefix,paintID;std::uint64_t paintRevision{};Renderer*renderer{};std::vector<std::uint8_t>zeros,scratch;
    Impl(LayerRasterizer&r,LayerRasterOptions o):raster(r),options(std::move(o)),prefix("projection."+std::to_string(++inkInstances)+"."),paintID(prefix+"paint"){options.paddingPoints=0;entries.reserve(512);retired.reserve(512);}
    void pose(Tile&t){t.draw.world=world*core::Matrix4::translation(t.bounds.x,t.bounds.y);t.draw.opacity=opacity;}
    // One shared raster identity, removed at once: the patch lives on in its image only.
    std::shared_ptr<const LayerRasterImage>paint(const Json&content){auto image=raster.rasterize(paintID,++paintRevision,content,options);raster.remove(paintID);need(image&&image->width&&image->height,"Projection ink region did not rasterize");return image;}
    void whole(Tile&t,const ProjectionInkTileChange&change,const std::string&name){auto image=paint(plan.content(change,name));t.patches.clear();t.patches.push_back({std::move(image),plan.tilePixels(change.index)});t.clear=t.repaint=false;++stats.tileRasters;stats.rasterPixelBytes+=t.patches.back().image->straightRGBA.size();}
};
NativeProjectionInk::NativeProjectionInk(LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}NativeProjectionInk::~NativeProjectionInk()=default;
bool NativeProjectionInk::update(std::span<const Stroke>strokes,std::uint64_t rev,const Stroke*live,std::uint64_t lr,Point size,double scale){auto&i=*impl_;struct Timed{ProjectionInkStats&s;std::chrono::steady_clock::time_point begin{std::chrono::steady_clock::now()};~Timed(){s.updateSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();}}timed{i.stats};if(!i.pending){if(!i.plan.update(strokes,rev,live,lr,size,scale))return false;i.pending=true;i.pendingRevision=rev;i.pendingLiveRevision=lr;i.pendingLive=bool(live);}else{need(i.pendingRevision==rev&&i.pendingLiveRevision==lr&&i.pendingLive==bool(live)&&i.plan.workspace()==size&&i.plan.pixelScale()==scale,"Retry pending Projection tile content before new document content");}i.options.pixelsPerPoint=scale;i.world.values[0]=i.world.values[5]=scale;
    for(const auto&change:i.plan.changes()){const auto key=std::pair{change.live,change.index};auto found=i.tiles.find(key);if(change.empty){if(found!=i.tiles.end()){i.retired.push_back(std::move(found->second));i.tiles.erase(found);}continue;}const auto name=i.prefix+(change.live?"live.":"ink.")+std::to_string(change.index);
        if(found==i.tiles.end()){const auto reusable=std::find_if(i.retired.begin(),i.retired.end(),[&](const auto&t){return t->key==key;});if(reusable!=i.retired.end()){found=i.tiles.try_emplace(key).first;found->second=std::move(*reusable);i.retired.erase(reusable);found->second->clear=true;found->second->patches.clear();}}
        if(found==i.tiles.end())found=i.tiles.emplace(key,std::make_unique<Impl::Tile>(i.raster,name,key,i.options)).first;
        auto&t=*found->second;t.bounds=change.bounds;t.size=i.plan.tilePixels(change.index);
        // A whole repaint, many queued regions or a tile lost with its texture
        // paints the complete tile; otherwise only the changed region.
        if(change.whole||t.repaint||t.patches.size()>=8)i.whole(t,change,name);
        else{auto image=i.paint(i.plan.regionContent(change,name));t.patches.push_back({std::move(image),change.region});++i.stats.patchRasters;i.stats.patchPixels+=std::uint64_t(change.region.width)*change.region.height;}
        i.pose(t);}
    i.entries.clear();for(auto&[_,tile]:i.tiles)i.entries.push_back({&tile->carrier,std::span(&tile->draw,1)});i.pending=false;return true;
}
bool NativeProjectionInk::upload(Renderer&r){auto&i=*impl_;need(!i.renderer||i.renderer==&r,"Projection ink belongs to another renderer");need(r.stats().initialized,"Projection ink requires an initialized renderer");i.renderer=&r;bool changed{};
    for(auto&[_,tile]:i.tiles){auto&t=*tile;const auto w=t.size.width,h=t.size.height;
        if(t.repaint)i.whole(t,{t.key.second,t.key.first,false,t.bounds,t.size,true},t.id);
        if(!t.texture||t.textureWidth!=w||t.textureHeight!=h){
            const auto scale=i.plan.pixelScale();const float right=float(double(w)/scale),bottom=float(double(h)/scale);
            const std::array<Vertex,4>vertices{{{{0,0,0},{0,0},{1,1,1,1}},{{right,0,0},{1,0},{1,1,1,1}},{{right,bottom,0},{1,1},{1,1,1,1}},{{0,bottom,0},{0,1},{1,1,1,1}}}};constexpr std::array<std::uint32_t,6>indices{0,1,2,0,2,3};
            r.setMesh(t.id,++t.meshRevision,{vertices,indices});t.mesh=true;
            i.zeros.assign(std::size_t(w)*h*4,0);r.setTexture(t.id,++t.textureRevision,{w,h,i.zeros,TextureColorSpace::sRGB,TextureFilter::linear,true});t.texture=true;t.textureWidth=w;t.textureHeight=h;t.clear=false;changed=true;
        }else if(t.clear){i.zeros.assign(std::size_t(w)*h*4,0);r.updateTexture(t.id,++t.textureRevision,0,0,{w,h,i.zeros,TextureColorSpace::sRGB,TextureFilter::linear});t.clear=false;changed=true;}
        for(const auto&patch:t.patches){const auto&image=*patch.image;const auto&region=patch.region;need(image.width>=region.width&&image.height>=region.height&&region.x+region.width<=w&&region.y+region.height<=h,"Projection ink region does not fit its tile");
            std::span<const std::uint8_t>pixels=image.straightRGBA;
            if(image.width!=region.width||image.height!=region.height){i.scratch.resize(std::size_t(region.width)*region.height*4);for(unsigned y=0;y<region.height;++y)std::copy_n(image.straightRGBA.begin()+std::ptrdiff_t(std::size_t(y)*image.width*4),std::size_t(region.width)*4,i.scratch.begin()+std::ptrdiff_t(std::size_t(y)*region.width*4));pixels=i.scratch;}
            r.updateTexture(t.id,++t.textureRevision,region.x,region.y,{region.width,region.height,pixels,TextureColorSpace::sRGB,TextureFilter::linear});++i.stats.regionUploads;changed=true;}
        t.patches.clear();}
    return changed;
}
bool NativeProjectionInk::setPose(const core::Matrix4&w,float opacity){auto&i=*impl_;need(w.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid Projection ink pose");auto expected=core::Matrix4{};expected.values[0]=expected.values[5]=i.plan.pixelScale();expected.values[12]=w.values[12];expected.values[13]=w.values[13];need(i.plan.pixelScale()>0&&w==expected&&std::floor(w.values[12])==w.values[12]&&std::floor(w.values[13])==w.values[13],"Projection ink requires original pixel-aligned DPI placement");if(i.world==w&&i.opacity==opacity)return false;i.world=w;i.opacity=opacity;for(auto&[_,tile]:i.tiles)i.pose(*tile);++i.stats.poses;return true;}
std::span<const LayerCompositionEntry>NativeProjectionInk::entries()const noexcept{return impl_->entries;}
bool NativeProjectionInk::collectRetired(Renderer&r){auto&i=*impl_;
    // A retired tile still in the composition rejects before anything goes.
    bool okay=true;for(auto&t:i.retired)okay=t->carrier.releaseResources(r)&&okay;
    const bool live=r.stats().initialized;
    std::erase_if(i.retired,[&](auto&t){if(!live)t->mesh=t->texture=false;if(t->mesh&&r.removeMesh(t->id))t->mesh=false;if(t->texture&&r.removeTexture(t->id))t->texture=false;const bool done=!t->mesh&&!t->texture;okay&=done;return done;});return okay;}
bool NativeProjectionInk::releaseResources(Renderer&r){auto&i=*impl_;bool okay=collectRetired(r);for(auto&[_,t]:i.tiles)okay=t->carrier.releaseResources(r)&&okay;
    const bool live=r.stats().initialized;
    for(auto&[_,t]:i.tiles){if(!live)t->mesh=t->texture=false;if(t->mesh&&r.removeMesh(t->id))t->mesh=false;if(t->texture&&r.removeTexture(t->id))t->texture=false;if(!t->mesh&&!t->texture){t->textureWidth=t->textureHeight=0;t->patches.clear();t->repaint=true;}else okay=false;}
    if(okay)i.renderer=nullptr;return okay;}
ProjectionInkStats NativeProjectionInk::stats()const noexcept{auto value=impl_->stats;value.completedTiles=impl_->plan.completedTiles();value.liveTiles=impl_->plan.liveTiles();value.retiredTiles=impl_->retired.size();return value;}
#endif
}
