#include "native/notes_scene.hpp"
#include "core/source_camera.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;
using Layer=modules::NotesLayer;
using Matrix=core::Matrix4;
void need(bool ok,const char*why){if(!ok)throw std::invalid_argument(why);}
Json rect(core::Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
Json point(core::Point p){return Json::Array{p.x,p.y};}
Json color(const std::optional<modules::NotesColor>&c){if(!c)return {};return Json::Object{{"sRGB",Json::Array{(*c)[0],(*c)[1],(*c)[2],(*c)[3]}}};}
Json command(const char*op,std::initializer_list<core::Point> points){Json::Array p;for(auto value:points)p.push_back(point(value));return Json::Object{{"op",op},{"points",std::move(p)}};}
Json roundedPath(core::Rect b,double radius){
    // CGPath's four circular-corner cubic segments, also used by the original
    // descriptor exporter. Preserve the path rather than substituting a box.
    const auto r=std::clamp(radius,0.,std::min(b.width,b.height)*.5),k=r*.5522847498;
    const auto l=b.x,t=b.y,right=l+b.width,bottom=t+b.height;
    return Json::Array{command("move",{{right,t+r}}),command("line",{{right,bottom-r}}),
        command("cubic",{{right,bottom-r+k},{right-r+k,bottom},{right-r,bottom}}),command("line",{{l+r,bottom}}),
        command("cubic",{{l+r-k,bottom},{l,bottom-r+k},{l,bottom-r}}),command("line",{{l,t+r}}),
        command("cubic",{{l,t+r-k},{l+r-k,t},{l+r,t}}),command("line",{{right-r,t}}),
        command("cubic",{{right-r+k,t},{right,t+r-k},{right,t+r}}),command("close",{})};
}
Json descriptor(const Layer&l){
    Json out=Json::Object{{"id",l.id},{"name",l.name},{"class","CALayer"},{"kind","layer"},
        {"bounds",rect(l.bounds)},{"position",Json::Array{l.frame.x,l.frame.y}},{"anchorPoint",Json::Array{0,0}},
        {"opacity",l.opacity},{"hidden",l.hidden},{"cornerRadius",l.cornerRadius},{"borderWidth",l.borderWidth},
        {"masksToBounds",l.masksToBounds},{"allowsGroupOpacity",l.allowsGroupOpacity},
        {"backgroundColor",color(l.background)},{"borderColor",color(l.border)},{"children",Json::Array{}}};
    if(l.kind==modules::NotesLayerKind::text){
        out["class"]="CATextLayer";out["kind"]="text";
        // Current Notes uses NSFont.systemFont, not a game/monospace font.
        // The existing rasterizer reports the selected installed fallback.
        out["text"]=Json::Object{{"string",l.text.text},{"fontSize",l.text.fontSize},
            {"font",Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",l.text.semibold?".SFNS-Semibold":".SFNS-Regular"},{"pointSize",l.text.fontSize}}},
            {"foregroundColor",color(l.text.color)},{"alignment","left"},{"wrapped",false},
            {"truncation",l.text.truncateEnd?"end":"none"},{"runs",Json::Array{}}};
    }else if(l.kind==modules::NotesLayerKind::shape){
        out["class"]="CAShapeLayer";out["kind"]="shape";Json path;
        if(l.shape.kind==modules::NotesPathKind::roundedRect)path=roundedPath(l.bounds,l.shape.radius);
        else {Json::Array commands;for(const auto&p:l.shape.points)commands.push_back(command(p.move?"move":"line",{p.point}));path=std::move(commands);}
        out["shape"]=Json::Object{{"path",std::move(path)},{"fillColor",color(l.shape.fill)},{"strokeColor",color(l.shape.stroke)},
            {"lineWidth",l.shape.lineWidth},{"lineCap",l.shape.roundCaps?"round":"butt"},{"lineJoin",l.shape.roundJoins?"round":"miter"},{"fillRule","non-zero"}};
    }
    return out;
}
Json clipped(const Layer&l,const Matrix&world,const Layer&card,std::optional<core::Rect>inkBounds={}){
    auto node=descriptor(l);const auto b=inkBounds.value_or(l.bounds);node["bounds"]=rect(b);
    node["position"]=Json::Array{world.values[12]+b.x,world.values[13]+b.y};node["opacity"]=1;node["hidden"]=false;
    Layer clip;clip.id=l.id+"/card-clip";clip.kind=modules::NotesLayerKind::shape;clip.bounds=card.bounds;
    clip.frame={-world.values[12],-world.values[13],clip.bounds.width,clip.bounds.height};clip.shape.kind=modules::NotesPathKind::roundedRect;
    clip.shape.radius=card.cornerRadius;clip.shape.fill=modules::NotesColor{1,1,1,1};node["mask"]=descriptor(clip);return node;
}
double opacity(std::span<const Layer> layers,std::size_t index){double value=1;for(;;){const auto&l=layers[index];value*=l.hidden?0:l.opacity;if(l.parent==Layer::noParent)return value;index=l.parent;}}
Matrix local(std::span<const Layer>layers,std::size_t index){
    const auto&l=layers[index];const auto own=Matrix::translation(l.frame.x-l.bounds.x,l.frame.y-l.bounds.y);
    return l.parent==Layer::noParent?own:local(layers,l.parent)*own;
}
void validate(std::span<const Layer>layers){
    need(!layers.empty()&&layers.size()<=LayerRasterizer::maximumNodes,"Notes layer tree is empty or oversized");
    std::unordered_set<std::string>ids;
    for(std::size_t i=0;i<layers.size();++i){const auto&l=layers[i];
        need(!l.id.empty()&&ids.insert(l.id).second,"Notes layer identities must be unique");
        need((i==0&&l.parent==Layer::noParent)||(i>0&&l.parent<i),"Notes layers must have one parent-first root");
        need(l.frame.width>=0&&l.frame.height>=0&&l.bounds.width>=0&&l.bounds.height>=0,"Notes layer dimensions cannot be negative");
        for(double n:{l.frame.x,l.frame.y,l.frame.width,l.frame.height,l.bounds.x,l.bounds.y,l.bounds.width,l.bounds.height,l.opacity,l.cornerRadius,l.borderWidth})need(std::isfinite(n),"Notes layer geometry must be finite");
        need(l.opacity>=0&&l.opacity<=1&&l.cornerRadius>=0&&l.borderWidth>=0,"Notes layer visual values are invalid");
        need(Json::validUtf8(l.id)&&Json::validUtf8(l.name)&&Json::validUtf8(l.text.text),"Notes layer text must be UTF-8");
    }
}
Json tree(std::span<const Layer>layers,std::span<const bool>excluded,std::size_t index){
    auto node=descriptor(layers[index]);Json::Array children;
    for(std::size_t i=index+1;i<layers.size();++i)if(layers[i].parent==index&&!excluded[i])children.push_back(tree(layers,excluded,i));
    node["children"]=std::move(children);return node;
}
}
struct NativeNotesCardScene::Impl {
    struct Feedback {std::string id;std::size_t source{},surface{};double from{},target{},start{},duration{};bool active{};};
    modules::NotesCardPresentation*source;LayerScene scene;LayerRasterOptions options;
    std::vector<Feedback> feedback;std::vector<DrawObject> localDraws;std::vector<LayerPlacement> placements;
    std::vector<std::vector<PlaneMask>> masks;std::uint64_t revision{},feedbackRevision{};std::optional<double>lastTime;
    NativeNotesSceneStats stats;bool posed{};Matrix priorWorld;core::Rect priorRect;float priorOpacity{};
    Impl(modules::NotesCardPresentation&s,LayerRasterizer&r,LayerRasterOptions o):source(&s),scene(r),options(std::move(o)){}
    void time(double t){need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Notes scene requires a finite monotonic caller clock");lastTime=t;}
    static double value(const Feedback&f,double t){if(!f.active||f.duration<=0)return f.target;const auto p=core::CubicTiming{0,0,.58,1}.value((t-f.start)/f.duration);return f.from+(f.target-f.from)*p;}
    bool sync(){
        need(source->contentRevision()!=0,"Initialize Notes presentation before native content");
        if(revision==source->contentRevision())return false;
        need(!source->editor(),"Notes native editor leaf requires the separate projected editor adapter");
        const auto layers=source->layers();validate(layers);need(layers[0].frame.x==0&&layers[0].frame.y==0,"Notes card root must use retained local coordinates");
        const auto&bounds=layers[0].bounds;
        // LayerScene groups a rounded local tree only within this source-area
        // bound. Reject the unsupported split BEFORE replacing retained state.
        need(bounds.width>0&&bounds.height>0&&bounds.width*bounds.height<=1'048'576,"Notes card exceeds the supported retained local group area");
        const auto pixelWidth=std::ceil((bounds.width+options.paddingPoints*2)*options.pixelsPerPoint),pixelHeight=std::ceil((bounds.height+options.paddingPoints*2)*options.pixelsPerPoint);
        need(std::isfinite(pixelWidth)&&std::isfinite(pixelHeight)&&pixelWidth<=8192&&pixelHeight<=8192&&pixelWidth*pixelHeight<=LayerRasterizer::maximumPixels,"Notes card exceeds the supported local raster bound");
        std::unique_ptr<bool[]> excluded(new bool[layers.size()]{});std::vector<Feedback> next;next.reserve(source->highlights().size()*2);
        for(const auto&h:source->highlights())for(const auto index:{h.tintLayer,h.rimLayer}){
            need(index<layers.size()&&!excluded[index],"Notes feedback index is invalid or repeated");
            need(layers[index].kind==modules::NotesLayerKind::shape,"Notes feedback must retain its source shape");
            excluded[index]=true;const auto target=opacity(layers,index);
            next.push_back({layers[index].id,index,0,target,target,0,0,false});
        }
        const auto grip=std::find_if(layers.begin(),layers.end(),[](const auto&l){return l.id.ends_with("/resizeGrip");});
        need(grip!=layers.end()&&grip->kind==modules::NotesLayerKind::shape&&grip->shape.kind==modules::NotesPathKind::polyline&&!grip->shape.points.empty(),"Notes card requires its exact final resize grip");
        const auto gripIndex=static_cast<std::size_t>(grip-layers.begin());excluded[gripIndex]=true;
        for(std::size_t i=0;i<layers.size();++i)if(layers[i].parent!=Layer::noParent&&excluded[layers[i].parent])excluded[i]=true;
        auto card=tree(layers,{excluded.get(),layers.size()},0);card["opacity"]=1;card["hidden"]=false;
        Json::Array children{std::move(card)};
        for(const auto&f:next){const auto m=local(layers,f.source);
            // Bake the exact rounded card ancestor into this tiny feedback
            // bitmap. Its alpha clip survives even tiny/clamped workspaces,
            // without a full-card feedback bitmap or a rectangular substitute.
            children.push_back(clipped(layers[f.source],m,layers[0]));
        }
        // Source draws the grip AFTER controls, also on tiny valid workspaces
        // where they overlap. Keep only its ink extent, not another card bitmap.
        auto min=grip->shape.points.front().point,max=min;for(const auto&p:grip->shape.points){min.x=std::min(min.x,p.point.x);min.y=std::min(min.y,p.point.y);max.x=std::max(max.x,p.point.x);max.y=std::max(max.y,p.point.y);}
        const auto inset=grip->shape.lineWidth*.5;
        children.push_back(clipped(*grip,local(layers,gripIndex),layers[0],core::Rect{min.x-inset,min.y-inset,max.x-min.x+inset*2,max.y-min.y+inset*2}));
        Json root=Json::Object{{"bounds",rect(layers[0].bounds)},{"masksToBounds",true},{"children",std::move(children)}};
        scene.load(root,options);need(scene.report().unsupported.empty(),"Notes scene has unsupported local raster or clipping state");
        std::vector<DrawObject> saved(scene.draws().begin(),scene.draws().end());std::vector<LayerPlacement> p;p.reserve(saved.size());std::vector<std::vector<PlaneMask>> clipMasks(saved.size());
        for(std::size_t i=0;i<saved.size();++i){clipMasks[i].reserve(8);p.push_back({i,saved[i].world,saved[i].opacity,{}});}
        for(auto&f:next){const auto found=scene.surfaceIndex(f.id);need(found.has_value(),"Retained Notes feedback surface is missing");f.surface=*found;}
        feedback=std::move(next);localDraws=std::move(saved);placements=std::move(p);masks=std::move(clipMasks);revision=source->contentRevision();feedbackRevision=source->feedbackRevision();posed=false;++stats.contentUpdates;return true;
    }
};
NativeNotesCardScene::NativeNotesCardScene(modules::NotesCardPresentation&s,LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(s,r,std::move(o))){}
NativeNotesCardScene::~NativeNotesCardScene()=default;
bool NativeNotesCardScene::syncContent(){return impl_->sync();}
bool NativeNotesCardScene::setFeedback(std::optional<std::string_view>verb,bool pressed,bool reduced,double time){
    auto&i=*impl_;i.time(time);need(i.revision==i.source->contentRevision(),"Synchronize Notes content before feedback");
    const auto changed=i.source->setFeedback(verb,pressed,reduced);if(!changed&&i.feedbackRevision==i.source->feedbackRevision())return false;
    const auto layers=i.source->layers();
    for(const auto&highlight:i.source->highlights())for(const auto index:{highlight.tintLayer,highlight.rimLayer}){
        auto f=std::find_if(i.feedback.begin(),i.feedback.end(),[&](const auto&value){return value.source==index;});
        need(f!=i.feedback.end(),"Notes feedback geometry changed without a content event");
        const auto current=Impl::value(*f,time),target=opacity(layers,index);f->from=current;f->target=target;f->start=time;
        f->duration=highlight.duration;f->active=f->duration>0&&current!=target;
    }
    i.feedbackRevision=i.source->feedbackRevision();i.posed=false;++i.stats.feedbackChanges;return true;
}
bool NativeNotesCardScene::updatePose(const Matrix&workspace,float canvas,double time){
    auto&i=*impl_;need(i.revision&&i.revision==i.source->contentRevision(),"Synchronize Notes content before placement");
    need(workspace.finite()&&std::isfinite(canvas)&&canvas>=0&&canvas<=1,"Invalid Notes workspace placement or opacity");
    const auto&placement=i.source->placement();const auto r=placement.workspaceRect;
    const auto world=workspace*Matrix::translation(r.x,r.y);const auto inverse=core::source::inverseSourceMatrix(world);i.time(time);
    const auto opacityValue=placement.visible?canvas:0.f;bool animated{};for(auto&f:i.feedback){animated|=f.active;if(time>=f.start+f.duration)f.active=false;}
    if(i.posed&&i.priorWorld==workspace&&i.priorRect==r&&i.priorOpacity==opacityValue&&!animated)return false;
    for(std::size_t index=0;index<i.placements.size();++index){auto&p=i.placements[index];const auto&original=i.localDraws[index];p.world=world*original.world;p.opacity=original.opacity*opacityValue;
        auto&m=i.masks[index];m.clear();for(const auto&mask:original.masks)m.push_back({mask.worldToLocal*inverse,mask.bounds});p.masks=m;
    }
    for(const auto&f:i.feedback)i.placements[f.surface].opacity=static_cast<float>(Impl::value(f,time))*opacityValue;
    i.scene.setPlacements(i.placements);i.priorWorld=workspace;i.priorRect=r;i.priorOpacity=opacityValue;i.posed=true;++i.stats.placementUpdates;return true;
}
bool NativeNotesCardScene::requiresFrames(double time)const {need(std::isfinite(time),"Notes feedback time must be finite");for(const auto&f:impl_->feedback)if(f.active&&time<f.start+f.duration)return true;return false;}
LayerScene&NativeNotesCardScene::scene()noexcept{return impl_->scene;}
const LayerScene&NativeNotesCardScene::scene()const noexcept{return impl_->scene;}
NativeNotesSceneStats NativeNotesCardScene::stats()const noexcept{return impl_->stats;}
} // namespace endfield::native
