#include "native/projection_controls_scene.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <set>
#include <stdexcept>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;using Matrix=core::Matrix4;
void need(bool okay,const char*message){if(!okay)throw std::invalid_argument(message);}
double number(const Json&j,double fallback=0){if(j.isNull())return fallback;need(j.isNumber()&&std::isfinite(j.number()),"Invalid Projection control number");return j.number();}
std::string text(const Json&j){return j.isNull()?std::string{}:j.string();}
bool flag(const Json&j,bool fallback=false){return j.isNull()?fallback:j.boolean();}
const Json::Array&children(const Json&j){static const Json::Array empty;return j["children"].isNull()?empty:j["children"].array();}
core::Rect rect(const Json&j){need(j.isArray()&&j.array().size()==4,"Invalid Projection control bounds");const auto&a=j.array();return {number(a[0]),number(a[1]),number(a[2]),number(a[3])};}
bool drawing(const Json&j){const auto kind=text(j["kind"]);return !j["contents"].isNull()||!j["backgroundColor"].isNull()||number(j["borderWidth"])>0||kind=="shape"||kind=="text"||kind=="gradient";}
struct Compiler {
    const modules::ProjectionControls&source;ProjectionControlsScenePlan result;Json::Array leaves;
    std::set<std::string,std::less<>>ids;std::size_t visited{};
    explicit Compiler(const modules::ProjectionControls&s):source(s){}
    void emit(Json node,Matrix world,double opacity,std::size_t feedback,bool rim,std::size_t ink){
        const auto id=text(node["id"]);need(!id.empty()&&ids.insert(id).second,"Repeated retained Projection control identity");
        const auto bounds=rect(node["bounds"]);
        node["position"]=Json::Array{world.values[12]+bounds.x,world.values[13]+bounds.y};node["anchorPoint"]=Json::Array{0,0};
        node["transform"]=nullptr;node["zPosition"]=0;node["opacity"]=1;node["children"]=Json::Array{};node.erase("projectionInkCentered");
        // Center the line provisionally so negative side bearings remain inside
        // the source item. The adapter then centers its actual painted alpha.
        if(ink!=ProjectionControlSurface::none)node["text"]["alignment"]="center";
        leaves.push_back(std::move(node));result.surfaces.push_back({id,world,static_cast<float>(opacity),feedback,ink,rim});
    }
    void visit(Json node,const Matrix&parent,double parentOpacity,unsigned depth){
        need(depth<=32&&++visited<=LayerRasterizer::maximumNodes,"Projection control descriptor exceeds bounds");
        need(node.isObject()&&!flag(node["hidden"]),"Hidden Projection control structure is unsupported");
        need(node["transform"].isNull()&&node["sublayerTransform"].isNull()&&node["mask"].isNull()&&!flag(node["masksToBounds"]),"Projection controls require the source unclipped translation tree");
        need(depth==0||number(node["zPosition"])==0,"Projection child paint order requires explicit ordering");
        const auto bounds=rect(node["bounds"]);need(bounds.width>=0&&bounds.height>=0&&bounds.width<=8192&&bounds.height<=8192,"Projection control dimensions exceed source bounds");
        const auto&p=node["position"].array();const auto&a=node["anchorPoint"].array();need(p.size()==2&&a.size()==2&&number(a[0])==0&&number(a[1])==0,"Projection controls require source origin anchoring");
        const auto world=parent*Matrix::translation(number(p[0])-bounds.x,number(p[1])-bounds.y);
        const auto opacity=number(node["opacity"],1);need(opacity>=0&&opacity<=1,"Invalid Projection control opacity");
        need(opacity==1||children(node).empty()||!flag(node["allowsGroupOpacity"],true),"Nested Projection group opacity requires a separate composition group");
        const auto id=text(node["id"]);std::size_t feedback=ProjectionControlSurface::none,ink=ProjectionControlSurface::none;bool rim{};
        for(std::size_t n=0;n<source.feedback().size();++n){const auto&f=source.feedback()[n];if(id==f.tintLayerID||id==f.rimLayerID){need(feedback==ProjectionControlSurface::none,"Ambiguous Projection feedback identity");feedback=n;rim=id==f.rimLayerID;}}
        for(std::size_t n=0;n<source.inkLabels().size();++n)if(id==source.inkLabels()[n].layerID){need(ink==ProjectionControlSurface::none&&flag(node["projectionInkCentered"]),"Invalid Projection ink label identity");ink=n;}
        need(!flag(node["projectionInkCentered"])||ink!=ProjectionControlSurface::none,"Unresolved Projection glyph centering marker");
        const auto savedChildren=children(node);const bool splitBorder=number(node["borderWidth"])>0&&!savedChildren.empty();
        auto own=node;if(splitBorder)own["borderWidth"]=0;
        if(drawing(own))emit(std::move(own),world,parentOpacity*(feedback==ProjectionControlSurface::none?opacity:1),feedback,rim,ink);
        for(const auto&child:savedChildren)visit(child,world,parentOpacity*opacity,depth+1);
        // CALayer paints its border after all descendants, including highlights.
        if(splitBorder){auto edge=node;edge["id"]=id+"/native-border";edge["kind"]="layer";edge["backgroundColor"]=nullptr;edge["contents"]=nullptr;edge.erase("shape");edge.erase("text");edge.erase("gradient");emit(std::move(edge),world,parentOpacity*opacity,ProjectionControlSurface::none,false,ProjectionControlSurface::none);}
    }
};
}

ProjectionControlsScenePlan prepareProjectionControlsScene(const modules::ProjectionControls&source){
    need(source.contentRevision()!=0,"Initialize Projection controls before compiling artwork");Compiler compiler(source);compiler.visit(source.artwork(),{},1,0);
    need(compiler.result.surfaces.size()<=LayerRasterizer::maximumEntries,"Projection controls exceed retained surface capacity");
    for(std::size_t n=0;n<source.feedback().size();++n)for(bool rim:{false,true})need(std::count_if(compiler.result.surfaces.begin(),compiler.result.surfaces.end(),[&](const auto&s){return s.feedback==n&&s.rim==rim;})==1,"Projection feedback must resolve exactly once");
    for(std::size_t n=0;n<source.inkLabels().size();++n)need(std::count_if(compiler.result.surfaces.begin(),compiler.result.surfaces.end(),[&](const auto&s){return s.inkLabel==n;})==1,"Projection ink label must resolve exactly once");
    // No root ID: LayerScene must retain these ordered leaves independently.
    compiler.result.layers=Json::Object{{"bounds",source.artwork()["bounds"]},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",std::move(compiler.leaves)}};
    return std::move(compiler.result);
}

Json prepareProjectionGlyphContent(const Json&leaf,const LayerSourceTextMeasurement&measurement){
    need(text(leaf["kind"])=="text"&&!flag(leaf["text"]["wrapped"])&&text(leaf["text"]["truncation"])=="none"&&children(leaf).empty(),"Projection glyph measurement requires one unwrapped source text leaf");
    need(std::isfinite(measurement.width)&&measurement.width>=0&&std::isfinite(measurement.height)&&measurement.height>0,"Invalid full Projection glyph measurement");
    const auto fontSize=number(leaf["text"]["fontSize"]);need(fontSize>0&&fontSize<=2048,"Invalid Projection glyph font size");
    const auto padding=std::ceil(std::max(fontSize,measurement.height)*2);
    const auto width=std::ceil(measurement.width)+padding*2,height=std::ceil(measurement.height)+padding*2;
    need(width>0&&height>0&&width<=8192&&height<=8192&&width*height<=LayerRasterizer::maximumPixels,"Projection glyph content exceeds shared raster bounds");
    auto content=leaf;content["bounds"]=Json::Array{0,0,width,height};content["text"]["alignment"]="center";
    // Only the baseline/line box changes; resolved font, glyphs and weight are
    // identical. A generous ascent and descent prevent line-box clipping before
    // measuring full alpha. Actual ink centering cancels this padded origin.
    content["text"]["font"]["ascender"]=height-padding;content["text"]["font"]["descender"]=-padding;content["text"]["font"]["leading"]=0;
    return content;
}

std::optional<core::Point>projectionGlyphInkOffset(const LayerRasterImage&image,core::Rect target){
    const auto valid=[](core::Rect r){return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>0&&r.height>0&&std::isfinite(r.x+r.width)&&std::isfinite(r.y+r.height);};
    need(valid(image.bounds)&&valid(target)&&image.width&&image.height&&image.width<=8192&&image.height<=8192&&std::uint64_t(image.width)*image.height<=LayerRasterizer::maximumPixels,"Invalid Projection glyph raster dimensions");
    need(image.straightRGBA.size()==std::size_t(image.width)*image.height*4,"Invalid Projection glyph raster storage");
    unsigned left=image.width,top=image.height,right{},bottom{};bool found{};
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)if(image.straightRGBA[(std::size_t(y)*image.width+x)*4+3]){found=true;left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
    if(!found)return {};
    const auto x=image.bounds.x+(double(left)+right)*.5*(image.bounds.width/image.width),y=image.bounds.y+(double(top)+bottom)*.5*(image.bounds.height/image.height);
    return core::Point{target.x+target.width*.5-x,target.y+target.height*.5-y};
}

#ifdef _WIN32
struct NativeProjectionControlsScene::Impl {
    struct Track{double from{},target{},start{},duration{};bool active{};};
    modules::ProjectionControls&source;LayerRasterizer&raster;LayerRasterOptions options;LayerScene scene;NativeLayerGroup group;
    std::vector<ProjectionControlSurface>surfaces;std::vector<LayerPlacement>placements;std::vector<PlaneMask>labelMasks;std::vector<std::array<Track,2>>tracks;
    std::string probeID;std::uint64_t sourceRevision{},feedbackRevision{},fontRevision{},attempt{};std::optional<double>lastTime;
    Renderer*renderer{};Matrix world;float opacity{1};bool posed{},contentDirty{},localDirty{};NativeProjectionControlsSceneStats stats;
    Impl(modules::ProjectionControls&s,LayerRasterizer&r,LayerRasterOptions o):source(s),raster(r),options(std::move(o)),scene(r),group(scene,"projection.controls",options.pixelsPerPoint){
        static std::atomic<std::uint64_t>sequence{};probeID="projection.control.probe:"+std::to_string(sequence.fetch_add(1,std::memory_order_relaxed));
    }
    void time(double t)const{need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Projection controls require a finite monotonic owner clock");}
    void current()const{need(sourceRevision&&sourceRevision==source.contentRevision()&&fontRevision==raster.fontRevision(),"Synchronize Projection controls before feedback/placement");}
    static double sample(const Track&t,double now){if(!t.active||t.duration<=0)return t.target;return t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value((now-t.start)/t.duration);}
};
NativeProjectionControlsScene::NativeProjectionControlsScene(modules::ProjectionControls&s,LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(s,r,std::move(o))){}
NativeProjectionControlsScene::~NativeProjectionControlsScene()=default;
bool NativeProjectionControlsScene::syncContent(){
    auto&i=*impl_;const auto fontRevision=i.raster.fontRevision();if(i.sourceRevision&&i.sourceRevision==i.source.contentRevision()&&i.fontRevision==fontRevision)return false;
    auto plan=prepareProjectionControlsScene(i.source);std::vector<LayerPlacement>placements(plan.surfaces.size());std::vector<PlaneMask>labelMasks(plan.surfaces.size());std::vector<std::array<Impl::Track,2>>tracks(i.source.feedback().size());
    for(std::size_t n=0;n<tracks.size();++n){const auto&f=i.source.feedback()[n];tracks[n][0].from=tracks[n][0].target=f.tintOpacity;tracks[n][1].from=tracks[n][1].target=f.rimOpacity;}
    const auto revision=++i.attempt;auto leaves=plan.layers["children"].array();
    for(std::size_t n=0;n<plan.surfaces.size();++n){auto&s=plan.surfaces[n];auto&p=placements[n];if(s.inkLabel!=ProjectionControlSurface::none){
            // The same shared font resolver and descriptor paint the probe and
            // retained leaf. No measurement/cache entry survives this event.
            const auto probe=i.probeID+":"+std::to_string(n);struct Removal{LayerRasterizer&r;const std::string&id;~Removal(){r.remove(id);}}removal{i.raster,probe};
            auto&leaf=leaves[n];const auto bounds=rect(leaf["bounds"]);
            const auto measurement=i.raster.measureSourceText(probe,leaf["text"],bounds.width,i.options);leaf=prepareProjectionGlyphContent(leaf,measurement);
            const auto image=i.raster.rasterize(probe,revision,leaf,i.options);need(image->complete(),"Projection glyph probe has unsupported source effects");
            const auto offset=projectionGlyphInkOffset(*image,bounds);need(offset.has_value(),"Projection label has no painted glyph ink");
            // The source draws centered full ink into a fixed-size item bitmap.
            // Keep that exact rectangular clip in GROUP coordinates so moving
            // the padded leaf cannot move the button's clip or affect its glow.
            labelMasks[n]={Matrix{},i.source.inkLabels()[s.inkLabel].rect,0};p.masks={&labelMasks[n],1};
            s.local=s.local*Matrix::translation(offset->x,offset->y);++i.stats.glyphMeasurements;
        }
        p.surface=n;p.world=s.local;p.opacity=s.opacity;if(s.feedback!=ProjectionControlSurface::none)p.opacity*=static_cast<float>(tracks[s.feedback][s.rim?1:0].target);
    }
    plan.layers["children"]=std::move(leaves);i.scene.load(plan.layers,i.options);need(i.scene.report().unsupported.empty()&&i.scene.draws().size()==plan.surfaces.size(),"Projection controls contain unsupported raster artwork");
    for(std::size_t n=0;n<plan.surfaces.size();++n)need(i.scene.surfaceIndex(plan.surfaces[n].id)==n,"Projection control paint order changed during rasterization");
    i.scene.setPlacements(placements);i.scene.prepareDraws();i.surfaces=std::move(plan.surfaces);i.placements=std::move(placements);i.labelMasks=std::move(labelMasks);i.tracks=std::move(tracks);
    i.sourceRevision=i.source.contentRevision();i.feedbackRevision=i.source.feedbackRevision();i.fontRevision=fontRevision;i.posed=false;i.contentDirty=i.localDirty=true;++i.stats.contentUpdates;return true;
}
bool NativeProjectionControlsScene::setFeedback(std::optional<std::string_view>action,bool pressed,bool reduced,double time){
    auto&i=*impl_;i.time(time);i.current();const bool changed=i.source.setFeedback(action,pressed,reduced);if(!changed&&i.feedbackRevision==i.source.feedbackRevision()){i.lastTime=time;return false;}
    for(std::size_t n=0;n<i.tracks.size();++n){const auto&f=i.source.feedback()[n];for(unsigned k=0;k<2;++k){auto&t=i.tracks[n][k];const auto target=k?f.rimOpacity:f.tintOpacity;
            // A control's press/release restarts both channels from their actual
            // presentation values. Other controls keep their existing fade.
            if(t.target==target&&!reduced&&f.duration==0)continue;const auto value=Impl::sample(t,time);t={value,target,time,f.duration,f.duration>0&&value!=target};
        }}
    i.feedbackRevision=i.source.feedbackRevision();i.lastTime=time;i.posed=false;++i.stats.feedbackChanges;return true;
}
bool NativeProjectionControlsScene::updatePose(const Matrix&world,float opacity,double time){
    auto&i=*impl_;i.time(time);i.current();need(i.feedbackRevision==i.source.feedbackRevision(),"Apply Projection feedback through its native adapter");need(world.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid Projection control pose");
    bool active{};for(const auto&pair:i.tracks)for(const auto&t:pair)active|=t.active;
    const bool rootChanged=!i.posed||i.world!=world||i.opacity!=opacity;if(!rootChanged&&!active&&!i.localDirty){i.lastTime=time;return false;}
    bool localChanged{};for(std::size_t n=0;n<i.surfaces.size();++n){const auto&s=i.surfaces[n];auto&p=i.placements[n];float alpha=s.opacity;if(s.feedback!=ProjectionControlSurface::none)alpha*=static_cast<float>(Impl::sample(i.tracks[s.feedback][s.rim?1:0],time));localChanged|=p.opacity!=alpha;p.opacity=alpha;}
    if(localChanged){i.scene.setPlacements(i.placements);i.localDirty=true;}
    for(auto&pair:i.tracks)for(auto&t:pair)if(time>=t.start+t.duration)t.active=false;
    i.world=world;i.opacity=opacity;i.lastTime=time;i.posed=true;
    if(i.renderer&&!i.contentDirty){if(i.localDirty){i.group.updateLocal(*i.renderer);i.localDirty=false;}i.group.setPose(world,opacity);}
    ++i.stats.poseUpdates;return rootChanged||localChanged;
}
bool NativeProjectionControlsScene::upload(Renderer&renderer){
    auto&i=*impl_;i.current();need(!i.renderer||i.renderer==&renderer,"Projection controls belong to another renderer");
    bool changed{};if(i.contentDirty||!i.renderer){changed=i.group.uploadResources(renderer);i.renderer=&renderer;i.contentDirty=i.localDirty=false;}else if(i.localDirty){changed=i.group.updateLocal(renderer);i.localDirty=false;}
    i.group.setPose(i.world,i.opacity);return changed;
}
LayerCompositionEntry NativeProjectionControlsScene::entry(){return impl_->group.entry();}
bool NativeProjectionControlsScene::releaseResources(Renderer&renderer){auto&i=*impl_;const bool released=i.group.releaseResources(renderer);if(released){i.renderer=nullptr;i.contentDirty=bool(i.sourceRevision);}return released;}
bool NativeProjectionControlsScene::requiresFrames(double time)const{const auto&i=*impl_;i.time(time);for(const auto&pair:i.tracks)for(const auto&t:pair)if(t.active&&time<t.start+t.duration)return true;return false;}
const LayerScene&NativeProjectionControlsScene::scene()const noexcept{return impl_->scene;}
NativeProjectionControlsSceneStats NativeProjectionControlsScene::stats()const noexcept{return impl_->stats;}
#endif
} // namespace endfield::native
