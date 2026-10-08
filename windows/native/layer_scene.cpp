#include "native/layer_scene.hpp"
#include "native/layer_mask.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;
using Matrix=core::Matrix4;
void need(bool ok,const char*reason){if(!ok)throw std::invalid_argument(reason);}
double number(const Json&j,double fallback=0){if(j.isNull())return fallback;need(j.isNumber()&&std::isfinite(j.number()),"Invalid native layer number");return j.number();}
bool flag(const Json&j,bool fallback=false){return j.isNull()?fallback:j.boolean();}
std::string text(const Json&j){return j.isNull()?std::string{}:j.string();}
std::array<double,2> point(const Json&j,std::array<double,2> fallback={}){if(j.isNull())return fallback;need(j.array().size()==2,"Invalid native layer point");return {number(j.array()[0]),number(j.array()[1])};}
core::Rect rect(const Json&j){need(j.isArray()&&j.array().size()==4,"Invalid native layer bounds");const auto&a=j.array();return {number(a[0]),number(a[1]),number(a[2]),number(a[3])};}
Matrix matrix(const Json&j){Matrix out;if(j.isNull())return out;need(j.array().size()==4,"Invalid layer matrix columns");for(unsigned c=0;c<4;++c){need(j.array()[c].array().size()==4,"Invalid layer matrix rows");for(unsigned r=0;r<4;++r)out.values[c*4+r]=number(j.array()[c].array()[r]);}return out;}
const Json::Array& children(const Json&j){static const Json::Array empty;return j["children"].isNull()?empty:j["children"].array();}
Matrix local(const Json&j){
    if(j["position"].isNull())return matrix(j["transform"]); // exported virtual root
    const auto b=rect(j["bounds"]);const auto p=point(j["position"]),a=point(j["anchorPoint"],{.5,.5});
    return Matrix::translation(p[0],p[1],number(j["zPosition"]))*matrix(j["transform"])*
        Matrix::translation(-b.x-a[0]*b.width,-b.y-a[1]*b.height,-number(j["anchorPointZ"]));
}
bool affine2D(const Json&j){const auto m=matrix(j).values;return m[2]==0&&m[3]==0&&m[6]==0&&m[7]==0&&m[8]==0&&m[9]==0&&m[10]==1&&m[11]==0&&m[14]==0&&m[15]==1;}
bool localContent2D(const Json&j,unsigned depth){if(depth>64)return false;if(!affine2D(j["sublayerTransform"]))return false;for(const auto&c:children(j))if(!affine2D(c["transform"])||number(c["zPosition"])!=0||!localContent2D(c,depth+1))return false;return true;}
bool ownDrawing(const Json&j){return !j["contents"].isNull()||!j["backgroundColor"].isNull()||number(j["borderWidth"])>0||text(j["kind"])=="text"||text(j["kind"])=="shape"||text(j["kind"])=="gradient";}
bool anyDrawing(const Json&j,unsigned depth){if(depth>64)return false;if(ownDrawing(j))return true;return std::any_of(children(j).begin(),children(j).end(),[&](const Json&c){return anyDrawing(c,depth+1);});}
void validateIdentities(const Json& node,std::unordered_set<std::string>& ids,std::size_t& count,unsigned depth){
    need(depth<=64&&++count<=4096,"Native layer tree exceeds limits");
    const auto id=text(node["id"]);if(!id.empty())need(ids.insert(id).second,"Repeated native layer identity");
    for(const auto&child:children(node))validateIdentities(child,ids,count,depth+1);
}
}
LayerScene::LayerScene(LayerRasterizer& r):rasterizer_(&r){
    static std::atomic<std::uint64_t> identity{0};
    namespace_="native-scene:"+std::to_string(identity.fetch_add(1,std::memory_order_relaxed))+":";
}
LayerScene::~LayerScene(){for(const auto&id:rasterIDs_)rasterizer_->remove(id);}
void LayerScene::load(const Json& root,const LayerRasterOptions& options){
    std::unordered_set<std::string> ids;std::size_t count{};validateIdentities(root,ids,count,0);
    // Load is an explicit content revision, not the pointer/frame path. Keep the
    // previous scene intact if a malformed layer cannot be compiled.
    // Consume the attempt even when validation fails. Otherwise a retry with
    // different content could return an earlier partial attempt's cached raster.
    LayerScene next(*rasterizer_);next.namespace_=namespace_;next.revision_=++attempt_;
    next.append(root,{},1,{},options,0);
    next.report_.sourceNodes=count;
    for(const auto&id:rasterIDs_)if(std::find(next.rasterIDs_.begin(),next.rasterIDs_.end(),id)==next.rasterIDs_.end())rasterizer_->remove(id);
    surfaces_=std::move(next.surfaces_);draws_=std::move(next.draws_);report_=std::move(next.report_);revision_=next.revision_;
    rasterIDs_=std::move(next.rasterIDs_);next.rasterIDs_.clear();next.surfaces_.clear();
}
void LayerScene::append(const Json& node,const Matrix&parent,float parentOpacity,const std::vector<PlaneMask>& masks,const LayerRasterOptions& options,unsigned depth){
    need(depth<=64&&++report_.sourceNodes<=4096,"Native layer tree exceeds limits");
    if(flag(node["hidden"]))return;
    const auto world=parent*local(node);const auto opacity=parentOpacity*float(number(node["opacity"],1));
    need(std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid native layer opacity");if(opacity==0)return;
    const auto b=rect(node["bounds"]);const auto identity=text(node["id"]);
    const bool rasterGroup=!identity.empty()&&b.width>0&&b.height>0&&b.width*b.height<=1'048'576&&localContent2D(node,depth)&&anyDrawing(node,depth);
    if(rasterGroup||ownDrawing(node)){
        need(!identity.empty(),"Drawable native layer has no identity");
        Json content=node;if(!rasterGroup)content["children"]=Json::Array{};
        auto settings=options;settings.includeRootOpacity=false;settings.includeRootMask=true;
        const auto id=namespace_+identity;
        rasterIDs_.push_back(id);
        auto image=rasterizer_->rasterize(id,revision_,content,settings);
        report_.unsupported.insert(report_.unsupported.end(),image->unsupported.begin(),image->unsupported.end());
        report_.fontSubstitutions.insert(report_.fontSubstitutions.end(),image->fontSubstitutions.begin(),image->fontSubstitutions.end());
        if(image->width&&image->height){
            DrawObject draw;draw.sourceID=id;draw.meshID=id;draw.textureID=id;draw.world=world;draw.opacity=opacity;draw.masks=masks;
            surfaces_.push_back({id,image,draw});draws_.push_back(std::move(draw));++report_.surfaces;report_.pixelBytes+=image->straightRGBA.size();
            surfaces_.back().draw.masks.reserve(8);draws_.back().masks.reserve(8);
        }
        if(rasterGroup)return;
    }
    auto nextMasks=masks;
    if(flag(node["masksToBounds"])&&b.width>0&&b.height>0){
        if(nextMasks.size()<8)nextMasks.push_back({core::source::inverseSourceMatrix(world),b});
        else report_.unsupported.push_back({identity,"more than eight projected clipping ancestors"});
        if(number(node["cornerRadius"])>0)report_.unsupported.push_back({identity,"rounded clipping across separately projected children"});
    }
    if(!node["mask"].isNull()){
        const auto projected=projectLayerMask(node["mask"],world);
        report_.unsupported.insert(report_.unsupported.end(),projected.unsupported.begin(),projected.unsupported.end());
        if(projected.clipsAll)return;
        if(projected.plane){
            if(nextMasks.size()<8)nextMasks.push_back(*projected.plane);
            else report_.unsupported.push_back({identity,"more than eight projected clipping ancestors"});
        }
    }
    if(opacity<1&&children(node).size()>1&&flag(node["allowsGroupOpacity"],true))report_.unsupported.push_back({identity,"group opacity across separately projected children"});
    std::vector<const Json*> ordered;for(const auto&c:children(node))ordered.push_back(&c);
    std::stable_sort(ordered.begin(),ordered.end(),[](const Json*a,const Json*b){return number((*a)["zPosition"])<number((*b)["zPosition"]);});
    // Source shell parents use an identity sublayer transform. General affine
    // child transforms are retained; perspective stays in the GPU matrix.
    const auto anchor=point(node["anchorPoint"],{.5,.5});
    const double ax=b.x+anchor[0]*b.width,ay=b.y+anchor[1]*b.height,az=number(node["anchorPointZ"]);
    const auto childWorld=world*Matrix::translation(ax,ay,az)*matrix(node["sublayerTransform"])*Matrix::translation(-ax,-ay,-az);
    for(const auto*c:ordered)append(*c,childWorld,opacity,nextMasks,options,depth+1);
}
void LayerScene::upload(Renderer& renderer){
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    for(const auto&s:surfaces_){const auto&b=s.image->bounds;const auto x=float(b.x),y=float(b.y),right=float(b.x+b.width),bottom=float(b.y+b.height);
        const std::array<Vertex,4> vertices{{{{x,y,0},{0,0},{1,1,1,1}},{{right,y,0},{1,0},{1,1,1,1}},{{right,bottom,0},{1,1},{1,1,1,1}},{{x,bottom,0},{0,1},{1,1,1,1}}}};
        renderer.setMesh(s.id,revision_,{vertices,indices});renderer.setTexture(s.id,revision_,{s.image->width,s.image->height,s.image->straightRGBA,TextureColorSpace::sRGB,TextureFilter::linear});
    }
    renderer.setDrawList(draws_);
    for(const auto&id:uploaded_)if(std::none_of(surfaces_.begin(),surfaces_.end(),[&](const Surface&s){return s.id==id;})){
        renderer.removeMesh(id);renderer.removeTexture(id);
    }
    uploaded_.clear();uploaded_.reserve(surfaces_.size());for(const auto&s:surfaces_)uploaded_.push_back(s.id);
}
void LayerScene::detach(Renderer& renderer){
    renderer.clearDrawList();for(const auto&id:uploaded_){renderer.removeMesh(id);renderer.removeTexture(id);}uploaded_.clear();
}
std::optional<std::size_t> LayerScene::surfaceIndex(std::string_view sourceID)const noexcept{
    for(std::size_t i=0;i<surfaces_.size();++i)if(std::string_view(surfaces_[i].id).substr(namespace_.size())==sourceID)return i;
    return {};
}
void LayerScene::setPlacements(std::span<const LayerPlacement> placements){
    std::array<bool,4096> supplied{};
    for(const auto&p:placements){
        need(p.surface<surfaces_.size()&&!supplied[p.surface],"Invalid or repeated native surface index");supplied[p.surface]=true;
        need(p.world.finite()&&std::isfinite(p.opacity)&&p.opacity>=0&&p.opacity<=1&&p.masks.size()<=8,"Invalid native surface placement");
        for(const auto&m:p.masks)need(m.worldToLocal.finite()&&std::isfinite(m.bounds.x)&&std::isfinite(m.bounds.y)&&std::isfinite(m.bounds.width)&&std::isfinite(m.bounds.height)&&m.bounds.width>0&&m.bounds.height>0,"Invalid native surface mask");
    }
    for(const auto&p:placements){auto&d=surfaces_[p.surface].draw;d.world=p.world;d.opacity=p.opacity;d.masks.assign(p.masks.begin(),p.masks.end());}
}
void LayerScene::present(Renderer& renderer,const Matrix& placement){
    need(placement.finite(),"Invalid native layer placement");const auto inverse=core::source::inverseSourceMatrix(placement);
    for(std::size_t i=0;i<surfaces_.size();++i){const auto&source=surfaces_[i].draw;auto&draw=draws_[i];draw.world=placement*source.world;draw.opacity=source.opacity;draw.masks.resize(source.masks.size());
        for(std::size_t j=0;j<draw.masks.size();++j){draw.masks[j].worldToLocal=source.masks[j].worldToLocal*inverse;draw.masks[j].bounds=source.masks[j].bounds;}}
    renderer.setDrawList(draws_);
}
Matrix layerViewportProjection(unsigned width,unsigned height){
    need(width&&height,"Invalid native layer viewport");Matrix m;
    // Core Animation zPosition orders these already projected screen surfaces;
    // it is not a D3D normalized depth. Keep native paint order and prevent a
    // zPosition such as10 from clipping the whole caption outside [0,1].
    m.values={2./width,0,0,0, 0,-2./height,0,0, 0,0,0,0, -1,1,0,1};return m;
}
} // namespace endfield::native
