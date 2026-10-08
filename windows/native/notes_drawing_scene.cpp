#include "native/notes_drawing_scene.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;using Matrix=core::Matrix4;using Point=core::Point;using Rect=core::Rect;
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
Json xy(Point p){return Json::Array{p.x,p.y};}
Json box(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
Json color(std::array<double,4>v){return Json::Object{{"sRGB",Json::Array{v[0],v[1],v[2],v[3]}}};}
Json command(const char*op,std::initializer_list<Point>p){Json::Array values;for(auto v:p)values.push_back(xy(v));return Json::Object{{"op",op},{"points",std::move(values)}};}
Json base(std::string id,Rect b){return Json::Object{{"id",std::move(id)},{"kind","layer"},{"class","CALayer"},{"bounds",box(b)},
    {"position",xy({b.x,b.y})},{"anchorPoint",xy({0,0})},{"opacity",1},{"backgroundColor",color({0,0,0,0})},{"children",Json::Array{}}};}
Json stroke(const modules::DrawingStroke&s,Point size,std::string id){
    auto layer=base(std::move(id),{0,0,size.x,size.y});layer["kind"]="shape";layer["class"]="CAShapeLayer";
    Json::Array path;path.reserve(std::max(std::size_t(2),s.points.size()));
    const auto points=modules::NotesDrawing::path(s,size);for(std::size_t i=0;i<points.size();++i)path.push_back(command(i?"line":"move",{points[i]}));
    layer["shape"]=Json::Object{{"path",std::move(path)},{"fillColor",Json{}},{"strokeColor",color(s.color)},{"lineWidth",s.width},{"lineCap","round"},{"lineJoin","round"},{"fillRule","non-zero"}};return layer;
}
Rect liveBounds(const modules::DrawingStroke&s,Point size){double l=std::numeric_limits<double>::infinity(),t=l,r=-l,b=-l;for(auto p:s.points){p.x*=size.x;p.y*=size.y;l=std::min(l,p.x);r=std::max(r,p.x);t=std::min(t,p.y);b=std::max(b,p.y);}const auto pad=s.width*.5+1;return {l-pad,t-pad,std::max(.01,r-l)+pad*2,std::max(.01,b-t)+pad*2};}
Json brush(double width,std::array<double,4>c){const auto r=width*.5,k=r*.5522847498;auto root=base("notes.drawing.brush",{-r-1,-r-1,width+2,width+2});root["kind"]="shape";root["class"]="CAShapeLayer";
    root["shape"]=Json::Object{{"path",Json::Array{command("move",{{r,0}}),command("cubic",{{r,k},{k,r},{0,r}}),command("cubic",{{-k,r},{-r,k},{-r,0}}),command("cubic",{{-r,-k},{-k,-r},{0,-r}}),command("cubic",{{k,-r},{r,-k},{r,0}}),command("close",{})}},
        {"fillColor",Json{}},{"strokeColor",color(c)},{"lineWidth",1},{"lineCap","butt"},{"lineJoin","miter"},{"fillRule","non-zero"}};return root;
}
}
struct NativeNotesDrawingScene::Impl {
    LayerScene completed,live,ring;LayerRasterOptions options;std::array<LayerCompositionEntry,3> entries;
    Point size{};Rect viewport{};std::uint64_t revision{},completedRevision{},liveRevision{},brushRevision{};bool initialized{},liveVisible{},brushReady{};
    std::optional<Point> brushPoint;double brushWidth{};std::array<double,4>brushColor{};
    Matrix world;float opacity{};bool posed{};NativeNotesDrawingStats stats;
    Impl(LayerRasterizer&r,LayerRasterOptions o):completed(r),live(r),ring(r),options(std::move(o)),entries{{{&completed,{}},{&live,{}},{&ring,{}}}}{}
    void apply(){if(!initialized)return;const auto inverse=core::source::inverseSourceMatrix(world);const std::array<PlaneMask,2> masks{{{inverse,{0,0,size.x,size.y},std::min(3.,std::min(size.x,size.y)*.5)},{inverse,viewport,0}}};
        const auto contentWorld=world*Matrix::translation(viewport.x,viewport.y);const LayerPlacement a{0,contentWorld,opacity,masks};completed.setPlacements(std::span(&a,1));const LayerPlacement b{0,contentWorld,liveVisible?opacity:0,masks};live.setPlacements(std::span(&b,1));
        const auto point=brushPoint.value_or(Point{});const LayerPlacement c{0,world*Matrix::translation(point.x,point.y),brushPoint?opacity:0,std::span(masks.data(),1)};ring.setPlacements(std::span(&c,1));}
};
NativeNotesDrawingScene::NativeNotesDrawingScene(LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}
NativeNotesDrawingScene::~NativeNotesDrawingScene()=default;
bool NativeNotesDrawingScene::setCompleted(const modules::NotesDrawing&drawing,std::uint64_t revision,Point size){auto&i=*impl_;need(revision&&std::isfinite(size.x)&&std::isfinite(size.y)&&size.x>=56&&size.y>0&&size.x*size.y<=1'048'576,"Invalid drawing card revision/bounds");if(i.initialized&&i.revision==revision&&i.size==size)return false;
    const auto viewport=modules::NotesDrawing::viewport(size);auto root=base("notes.drawing.completed",{0,0,viewport.width,viewport.height});Json::Array children;children.reserve(drawing.strokes().size());std::size_t index{};for(const auto&s:drawing.strokes())children.push_back(stroke(s,{viewport.width,viewport.height},"stroke."+std::to_string(index++)));root["children"]=std::move(children);
    if(!i.initialized){i.completed.load(root,i.options);i.live.load(base("notes.drawing.live",{0,0,1,1}),i.options);i.ring.load(brush(8,{1,1,1,1}),i.options);}else i.completed.updateLocalContent("notes.drawing.completed",++i.completedRevision,root,i.options);
    i.size=size;i.viewport=viewport;i.revision=revision;i.initialized=true;i.liveVisible=false;i.liveRevision=0;++i.stats.completedRasters;i.apply();return true;
}
bool NativeNotesDrawingScene::setLive(const modules::DrawingStroke*value,std::uint64_t revision){auto&i=*impl_;need(i.initialized,"Initialize drawing content before its live stroke");const bool visible=value&&!value->points.empty();if(!visible){if(!i.liveVisible)return false;i.liveVisible=false;i.apply();return true;}need(value->valid()&&revision,"Invalid live drawing stroke");if(i.liveVisible&&i.liveRevision==revision)return false;
    const Point extent{i.viewport.width,i.viewport.height};auto root=base("notes.drawing.live",liveBounds(*value,extent));root["children"]=Json::Array{stroke(*value,extent,"live.stroke")};i.live.updateLocalContent("notes.drawing.live",revision,root,i.options);i.liveRevision=revision;i.liveVisible=true;++i.stats.liveRasters;i.apply();return true;
}
bool NativeNotesDrawingScene::setBrush(std::optional<Point>p,double width,std::array<double,4>c){auto&i=*impl_;need(i.initialized&&std::isfinite(width)&&width>=1&&width<=80,"Invalid drawing brush width");if(p)need(std::isfinite(p->x)&&std::isfinite(p->y),"Invalid drawing brush position");for(double v:c)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid drawing brush color");bool changed=p!=i.brushPoint;
    if(p&&(!i.brushReady||width!=i.brushWidth||c!=i.brushColor)){i.ring.updateLocalContent("notes.drawing.brush",++i.brushRevision,brush(width,c),i.options);i.brushReady=true;i.brushWidth=width;i.brushColor=c;++i.stats.brushRasters;changed=true;}i.brushPoint=p;if(changed)i.apply();return changed;
}
bool NativeNotesDrawingScene::updatePose(const Matrix&world,float opacity){auto&i=*impl_;need(world.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid drawing pose");(void)core::source::inverseSourceMatrix(world);if(i.posed&&i.world==world&&i.opacity==opacity)return false;i.world=world;i.opacity=opacity;i.posed=true;i.apply();++i.stats.poseUpdates;return true;}
std::span<const LayerCompositionEntry>NativeNotesDrawingScene::entries()const noexcept{return impl_->initialized?std::span<const LayerCompositionEntry>(impl_->entries):std::span<const LayerCompositionEntry>{};}
bool NativeNotesDrawingScene::releaseResources(Renderer&r){auto&i=*impl_;bool ok=true;for(const auto&e:i.entries)ok=e.scene->releaseResources(r)&&ok;return ok;}
NativeNotesDrawingStats NativeNotesDrawingScene::stats()const noexcept{return impl_->stats;}
} // namespace endfield::native
