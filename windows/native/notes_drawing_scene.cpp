#include "native/notes_drawing_scene.hpp"
#include "native/notes_scene.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
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
Json brush(double width,std::array<double,4>c){const auto r=width*.5,k=r*.5522847498;auto root=base("notes.drawing.brush",{-r-1,-r-1,width+2,width+2});root["kind"]="shape";root["class"]="CAShapeLayer";
    root["shape"]=Json::Object{{"path",Json::Array{command("move",{{r,0}}),command("cubic",{{r,k},{k,r},{0,r}}),command("cubic",{{-k,r},{-r,k},{-r,0}}),command("cubic",{{-r,-k},{-k,-r},{0,-r}}),command("cubic",{{k,-r},{r,-k},{r,0}}),command("close",{})}},
        {"fillColor",Json{}},{"strokeColor",color(c)},{"lineWidth",1},{"lineCap","butt"},{"lineJoin","miter"},{"fillRule","non-zero"}};return root;
}
std::atomic<std::uint64_t> inkInstances{0};
}
struct NativeNotesDrawingScene::Impl {
    LayerRasterizer&raster;LayerScene carrier,ring;LayerRasterOptions options;std::array<LayerCompositionEntry,2> entries;modules::NotesDrawingInkPlan plan;
    Point size{};Rect viewport{};std::uint64_t revision{},brushRevision{};bool initialized{},brushReady{};
    // A viewport beyond the former local group area (1,000,000 points) is
    // described at a uniform reduction (strokes are normalized; widths scale
    // with it) and placed back at full size, at the matching bounded density.
    double contentScale{1};LayerRasterOptions paintOptions;
    std::optional<Point> brushPoint;double brushWidth{};std::array<double,4>brushColor{};
    Matrix world;float opacity{};bool posed{};NativeNotesDrawingStats stats;
    // The ink: one updatable texture and its quad, published as the carrier
    // scene's single supplemental draw once resident.
    std::string id,paintID;DrawObject draw;Renderer*renderer{};std::uint64_t paintRevision{},meshRevision{},textureRevision{};unsigned textureWidth{},textureHeight{},meshWidth{},meshHeight{};double meshDensity{};bool mesh{},texture{},resident{};
    std::vector<std::uint8_t> scratch;
    Impl(LayerRasterizer&r,LayerRasterOptions o):raster(r),carrier(r),ring(r),options(std::move(o)),entries{{{&carrier,{}},{&ring,{}}}}{
        const auto n=std::to_string(++inkInstances);id="notes.drawing."+n+".ink";paintID="notes.drawing."+n+".paint";draw.sourceID=draw.meshID=draw.textureID=id;draw.masks.reserve(2);}
    void apply(){if(!initialized)return;const auto inverse=core::source::inverseSourceMatrix(world);const std::array<PlaneMask,2> masks{{{inverse,{0,0,size.x,size.y},std::min(3.,std::min(size.x,size.y)*.5)},{inverse,viewport,0}}};
        draw.world=world*Matrix::translation(viewport.x,viewport.y)*Matrix::scale(1/contentScale,1/contentScale,1);draw.opacity=opacity;draw.masks.assign(masks.begin(),masks.end());
        const auto point=brushPoint.value_or(Point{});const LayerPlacement c{0,world*Matrix::translation(point.x,point.y),brushPoint?opacity:0,std::span(masks.data(),1)};ring.setPlacements(std::span(&c,1));}
    // One shared raster identity, removed at once: the pixels live on in the image only.
    std::shared_ptr<const LayerRasterImage> paint(const Json&content){auto image=raster.rasterize(paintID,++paintRevision,content,paintOptions);raster.remove(paintID);need(image&&image->width&&image->height,"Notes drawing ink did not rasterize");return image;}
    // The top-left width x height pixels of an image at least that large.
    static std::span<const std::uint8_t> crop(const LayerRasterImage&image,unsigned width,unsigned height,std::vector<std::uint8_t>&buffer){
        need(image.width>=width&&image.height>=height,"Notes drawing ink region did not cover its pixels");if(image.width==width&&image.height==height)return image.straightRGBA;
        buffer.resize(std::size_t(width)*height*4);for(unsigned y=0;y<height;++y)std::copy_n(image.straightRGBA.begin()+std::ptrdiff_t(std::size_t(y)*image.width*4),std::size_t(width)*4,buffer.begin()+std::ptrdiff_t(std::size_t(y)*width*4));return buffer;}
};
NativeNotesDrawingScene::NativeNotesDrawingScene(LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}
NativeNotesDrawingScene::~NativeNotesDrawingScene()=default;
bool NativeNotesDrawingScene::setCompleted(const modules::NotesDrawing&drawing,std::uint64_t revision,Point size){auto&i=*impl_;need(revision&&std::isfinite(size.x)&&std::isfinite(size.y)&&size.x>=56&&size.y>0&&size.x<=32768&&size.y<=32768,"Invalid drawing card revision/bounds");if(i.initialized&&i.revision==revision&&i.size==size)return false;
    if(!i.initialized||i.size!=size){
        const auto viewport=modules::NotesDrawing::viewport(size);const auto area=viewport.width*viewport.height;const double scale=area>1'000'000?std::sqrt(1'000'000/area):1.;const Point extent{viewport.width*scale,viewport.height*scale};
        auto options=i.options;options.pixelsPerPoint=std::min(4.,options.pixelsPerPoint/scale);options=notesRasterOptions(options,extent.x,extent.y);options.paddingPoints=0;
        i.plan.setGeometry(extent,scale,options.pixelsPerPoint);i.contentScale=scale;i.paintOptions=options;i.size=size;i.viewport=viewport;}
    i.plan.setCompleted(drawing.strokes(),revision);
    if(!i.initialized){i.carrier.load(Json::Object{{"id",i.id+".carrier"},{"kind","layer"},{"bounds",Json::Array{0,0,0,0}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}},i.options);
        need(i.carrier.draws().empty(),"Notes drawing ink carrier draws nothing itself");i.ring.load(brush(8,{1,1,1,1}),i.options);}
    i.revision=revision;i.initialized=true;i.apply();return true;
}
bool NativeNotesDrawingScene::setLive(const modules::DrawingStroke*value,std::uint64_t revision){auto&i=*impl_;need(i.initialized,"Initialize drawing content before its live stroke");need(revision,"Invalid live drawing revision");return i.plan.setLive(value,revision);}
bool NativeNotesDrawingScene::setBrush(std::optional<Point>p,double width,std::array<double,4>c){auto&i=*impl_;need(i.initialized&&std::isfinite(width)&&width>=1&&width<=80,"Invalid drawing brush width");if(p)need(std::isfinite(p->x)&&std::isfinite(p->y),"Invalid drawing brush position");for(double v:c)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid drawing brush color");bool changed=p!=i.brushPoint;
    if(p&&(!i.brushReady||width!=i.brushWidth||c!=i.brushColor)){i.ring.updateLocalContent("notes.drawing.brush",++i.brushRevision,brush(width,c),i.options);i.brushReady=true;i.brushWidth=width;i.brushColor=c;++i.stats.brushRasters;changed=true;}i.brushPoint=p;if(changed)i.apply();return changed;
}
bool NativeNotesDrawingScene::updatePose(const Matrix&world,float opacity){auto&i=*impl_;need(world.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid drawing pose");(void)core::source::inverseSourceMatrix(world);if(i.posed&&i.world==world&&i.opacity==opacity)return false;i.world=world;i.opacity=opacity;i.posed=true;i.apply();++i.stats.poseUpdates;return true;}
bool NativeNotesDrawingScene::upload(Renderer&r){auto&i=*impl_;if(!i.initialized||!r.stats().initialized)return false;need(!i.renderer||i.renderer==&r,"Notes drawing ink belongs to another renderer");i.renderer=&r;
    const auto picture=i.plan.picture();const auto w=picture.width,h=picture.height;const bool create=!i.texture||i.textureWidth!=w||i.textureHeight!=h;
    const auto pixels=i.plan.dirtyPixels();
    // The quad spans the picture's pixels in content units (texel = 1/density).
    const auto density=i.paintOptions.pixelsPerPoint;
    if(!i.mesh||i.meshWidth!=w||i.meshHeight!=h||i.meshDensity!=density){
        const float right=float(double(w)/density),bottom=float(double(h)/density);
        const std::array<Vertex,4>vertices{{{{0,0,0},{0,0},{1,1,1,1}},{{right,0,0},{1,0},{1,1,1,1}},{{right,bottom,0},{1,1},{1,1,1,1}},{{0,bottom,0},{0,1},{1,1,1,1}}}};constexpr std::array<std::uint32_t,6>indices{0,1,2,0,2,3};
        r.setMesh(i.id,++i.meshRevision,{vertices,indices});i.mesh=true;i.meshWidth=w;i.meshHeight=h;i.meshDensity=density;}
    if(create||pixels){const auto begin=std::chrono::steady_clock::now();
        // A new texture, or most of the picture changed at once: paint it whole.
        if(create||i.plan.whole()||pixels*5>std::uint64_t(w)*h*3){
            const auto content=i.plan.content(picture,i.paintID);std::vector<std::uint8_t>buffer;std::shared_ptr<const LayerRasterImage>image;std::span<const std::uint8_t>painted;
            if(content["children"].array().empty()){buffer.assign(std::size_t(w)*h*4,0);painted=buffer;}else{image=i.paint(content);painted=Impl::crop(*image,w,h,buffer);++i.stats.wholeRasters;}
            if(create){
                r.setTexture(i.id,++i.textureRevision,{w,h,painted,TextureColorSpace::sRGB,TextureFilter::linear,true});i.texture=true;i.textureWidth=w;i.textureHeight=h;++i.stats.textureCreations;
            }else{r.updateTexture(i.id,++i.textureRevision,0,0,{w,h,painted,TextureColorSpace::sRGB,TextureFilter::linear});++i.stats.regionUploads;}
        }else for(const auto&rect:i.plan.dirty()){
            // A changed region: the same source paths clipped to it, painted
            // in place; every other pixel keeps its value.
            const auto content=i.plan.content(rect,i.paintID);std::span<const std::uint8_t>painted;std::shared_ptr<const LayerRasterImage>image;
            if(content["children"].array().empty()){i.scratch.assign(std::size_t(rect.width)*rect.height*4,0);painted=i.scratch;++i.stats.clearedRegions;}
            else{image=i.paint(content);painted=Impl::crop(*image,rect.width,rect.height,i.scratch);++i.stats.regionRasters;i.stats.regionPixels+=std::uint64_t(rect.width)*rect.height;}
            r.updateTexture(i.id,++i.textureRevision,rect.x,rect.y,{rect.width,rect.height,painted,TextureColorSpace::sRGB,TextureFilter::linear});++i.stats.regionUploads;
        }
        i.plan.painted();++i.stats.paints;i.stats.paintSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    }
    if(i.resident)return false;i.resident=true;i.entries[0].after=std::span<const DrawObject>(&i.draw,1);return true;
}
std::span<const LayerCompositionEntry>NativeNotesDrawingScene::entries()const noexcept{return impl_->initialized?std::span<const LayerCompositionEntry>(impl_->entries):std::span<const LayerCompositionEntry>{};}
bool NativeNotesDrawingScene::releaseResources(Renderer&r){auto&i=*impl_;bool ok=i.carrier.releaseResources(r);ok=i.ring.releaseResources(r)&&ok;
    if(!r.stats().initialized)i.mesh=i.texture=false;if(i.mesh&&r.removeMesh(i.id))i.mesh=false;if(i.texture&&r.removeTexture(i.id))i.texture=false;
    if(i.mesh||i.texture)return false;
    // The picture is gone with its texture: the next upload paints it whole.
    i.textureWidth=i.textureHeight=i.meshWidth=i.meshHeight=0;i.resident=false;i.entries[0].after={};i.renderer=nullptr;if(i.plan.ready())i.plan.invalidate();return ok;}
NativeNotesDrawingStats NativeNotesDrawingScene::stats()const noexcept{auto value=impl_->stats;value.plan=impl_->plan.stats();return value;}
} // namespace endfield::native
