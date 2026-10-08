#include "native/notes_scene.hpp"
#include "native/notes_rich_style.hpp"
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
            {"font",Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",l.text.semibold?".SFNS-Semibold":l.text.medium?".SFNS-Medium":".SFNS-Regular"},{"pointSize",l.text.fontSize}}},
            {"foregroundColor",color(l.text.color)},{"alignment","left"},{"wrapped",l.text.wrapped},
            {"truncation",l.text.truncateEnd?"end":"none"},{"runs",Json::Array{}}};
        if(l.text.strikethrough&&!l.text.text.empty()){std::uint32_t units{};for(unsigned char c:l.text.text)if((c&0xc0)!=0x80)units+=c>=0xf0?2:1;out["text"]["runs"]=Json::Array{Json::Object{{"utf16Range",Json::Array{0,static_cast<double>(units)}},{"attributes",Json::Object{{"NSStrikethrough",1}}}}};}
        if(!l.text.runs.empty()){out["text"]["notesRichLine"]=true;Json::Array runs;runs.reserve(l.text.runs.size());for(const auto&r:l.text.runs)runs.push_back(notesRunDescriptor(r,l.text.color));out["text"]["runs"]=std::move(runs);}
    }else if(l.kind==modules::NotesLayerKind::shape){
        out["class"]="CAShapeLayer";out["kind"]="shape";Json path;
        if(l.shape.kind==modules::NotesPathKind::roundedRect)path=roundedPath(l.bounds,l.shape.radius);
        else {Json::Array commands;for(const auto&p:l.shape.points)commands.push_back(command(p.move?"move":"line",{p.point}));path=std::move(commands);}
        out["shape"]=Json::Object{{"path",std::move(path)},{"fillColor",color(l.shape.fill)},{"strokeColor",color(l.shape.stroke)},
            {"lineWidth",l.shape.lineWidth},{"lineCap",l.shape.roundCaps?"round":"butt"},{"lineJoin",l.shape.roundJoins?"round":"miter"},{"fillRule","non-zero"}};
    }
    return out;
}
Json clipped(const Layer&l,const Matrix&world,const Layer&card,std::optional<core::Rect>inkBounds={},std::optional<core::Rect>ancestorClip={}){
    auto node=descriptor(l);const auto b=inkBounds.value_or(l.bounds);node["bounds"]=rect(b);
    node["position"]=Json::Array{world.values[12]+b.x,world.values[13]+b.y};node["opacity"]=1;node["hidden"]=false;
    Layer clip;clip.id=l.id+"/card-clip";clip.kind=modules::NotesLayerKind::shape;clip.bounds=card.bounds;
    clip.frame={-world.values[12],-world.values[13],clip.bounds.width,clip.bounds.height};clip.shape.kind=modules::NotesPathKind::roundedRect;
    clip.shape.radius=card.cornerRadius;clip.shape.fill=modules::NotesColor{1,1,1,1};node["mask"]=descriptor(clip);
    if(ancestorClip){Layer nested;nested.id=l.id+"/viewport-clip";nested.kind=modules::NotesLayerKind::shape;nested.frame=*ancestorClip;nested.bounds={0,0,ancestorClip->width,ancestorClip->height};nested.shape.kind=modules::NotesPathKind::roundedRect;nested.shape.fill=modules::NotesColor{1,1,1,1};node["mask"]["mask"]=descriptor(nested);}return node;
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
    std::optional<NativeNotesExternalEditorAppearance>editorAppearance;
    std::optional<NativeNotesExternalEditorSlot>editorSlot;
    std::vector<Feedback> feedback;std::vector<DrawObject> localDraws;std::vector<LayerPlacement> placements;
    std::vector<std::vector<PlaneMask>> masks;
    std::array<DrawObject,1>afterDraws;std::size_t borderSurface{};
    std::optional<NativeNotesMediaSlot>mediaSlot;
    std::optional<modules::NotesMediaLayout>mediaLayout;
    modules::NotesMediaProgressPose mediaProgress;
    std::array<DrawObject,4>mediaDraws;
    std::size_t gripSurface{};std::string mediaMesh,mediaTexture;unsigned mediaWidth{},mediaHeight{};
    Renderer*mediaOwner{};bool mediaUploaded{};
    std::uint64_t revision{},feedbackRevision{};std::optional<double>lastTime;
    NativeNotesSceneStats stats;bool posed{};Matrix priorWorld;core::Rect priorRect;float priorOpacity{};
    Impl(modules::NotesCardPresentation&s,LayerRasterizer&r,LayerRasterOptions o,std::optional<NativeNotesExternalEditorAppearance>external)
        :source(&s),scene(r),options(std::move(o)),editorAppearance(external){
        if(external)for(const auto*c:{&external->background,&external->border})for(double value:*c)need(std::isfinite(value)&&value>=0&&value<=1,"Invalid external Notes editor appearance");
        if(external)need(external->background[3]==1,"Source Notes editor background must be opaque");
        for(auto&draw:mediaDraws)draw.masks.reserve(8);
    }
    void time(double t){need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Notes scene requires a finite monotonic caller clock");lastTime=t;}
    static double value(const Feedback&f,double t){if(!f.active||f.duration<=0)return f.target;const auto p=core::CubicTiming{0,0,.58,1}.value((t-f.start)/f.duration);return f.from+(f.target-f.from)*p;}
    bool sync(){
        need(source->contentRevision()!=0,"Initialize Notes presentation before native content");
        if(revision==source->contentRevision())return false;
        need(!source->editor()||editorAppearance,"Notes native editor leaf requires explicit external projected editor mode");
        const auto layers=source->layers();validate(layers);need(layers[0].frame.x==0&&layers[0].frame.y==0,"Notes card root must use retained local coordinates");
        const auto&bounds=layers[0].bounds;
        std::optional<modules::NotesMediaLayout>nextMedia;
        if(const auto&media=source->media())nextMedia.emplace(bounds.width,bounds.height,media->kind,media->duration,media->legacyManagedImage);
        std::optional<NativeNotesExternalEditorSlot>nextSlot;
        if(const auto&leaf=source->editor()){
            const auto r=leaf->localRect;
            // This bounded integration has no ancestor-mask flattening fallback.
            // Its inset viewport and all source control ink must be disjoint;
            // unusual tiny cards require an explicit future clip/order adapter.
            need(r.width>=6&&r.height>=6&&r.x>=3&&r.y>=3&&r.x+r.width<=bounds.width-3&&r.y+r.height<=bounds.height-3,
                "External Notes editor requires a contained source-radius viewport");
            auto overlaps=[&](core::Rect other){return r.x<other.x+other.width&&r.x+r.width>other.x&&r.y<other.y+other.height&&r.y+r.height>other.y;};
            for(const auto&a:source->actions())if(!a.accessibilityOnly&&a.verb!="edit"&&!a.verb.starts_with("editItem:"))need(!overlaps(a.localRect),"External Notes editor overlaps source controls; exact alternate ordering is required");
            nextSlot=NativeNotesExternalEditorSlot{r,3,1};
        }
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
        if(nextSlot&&source->editor()->multiline)for(std::size_t i=0;i<layers.size();++i)if(layers[i].id.ends_with("/scrollThumb"))excluded[i]=true;
        for(std::size_t i=0;i<layers.size();++i)if(layers[i].parent!=Layer::noParent&&excluded[layers[i].parent])excluded[i]=true;
        auto card=tree(layers,{excluded.get(),layers.size()},0);card["opacity"]=1;card["hidden"]=false;
        if(nextSlot){
            Layer backing;backing.id=layers[0].id+"/external-editor-backing";backing.name="notes.external.editor.backing";
            backing.frame=nextSlot->localRect;backing.bounds={0,0,nextSlot->localRect.width,nextSlot->localRect.height};
            backing.cornerRadius=nextSlot->cornerRadius;backing.masksToBounds=true;backing.background=editorAppearance->background;
            auto baseChildren=card["children"].array();baseChildren.push_back(descriptor(backing));card["children"]=std::move(baseChildren);
        }
        Json::Array children{std::move(card)};
        auto&decorations=children;
        for(const auto&f:next){const auto m=local(layers,f.source);
            // Bake the exact rounded card ancestor into this tiny feedback
            // bitmap. Its alpha clip survives even tiny/clamped workspaces,
            // without a full-card feedback bitmap or a rectangular substitute.
            std::optional<core::Rect>ancestorClip;for(auto parent=layers[f.source].parent;parent!=Layer::noParent&&parent!=0;parent=layers[parent].parent)if(layers[parent].masksToBounds){const auto world=local(layers,parent);const core::Rect r{world.values[12]+layers[parent].bounds.x,world.values[13]+layers[parent].bounds.y,layers[parent].bounds.width,layers[parent].bounds.height};if(!ancestorClip)ancestorClip=r;else{const auto x=std::max(r.x,ancestorClip->x),y=std::max(r.y,ancestorClip->y),right=std::min(r.x+r.width,ancestorClip->x+ancestorClip->width),bottom=std::min(r.y+r.height,ancestorClip->y+ancestorClip->height);ancestorClip=core::Rect{x,y,std::max(0.,right-x),std::max(0.,bottom-y)};}}
            decorations.push_back(clipped(layers[f.source],m,layers[0],{},ancestorClip));
        }
        // Source draws the grip AFTER controls, also on tiny valid workspaces
        // where they overlap. Keep only its ink extent, not another card bitmap.
        auto min=grip->shape.points.front().point,max=min;for(const auto&p:grip->shape.points){min.x=std::min(min.x,p.point.x);min.y=std::min(min.y,p.point.y);max.x=std::max(max.x,p.point.x);max.y=std::max(max.y,p.point.y);}
        const auto inset=grip->shape.lineWidth*.5;
        const core::Rect gripInk{min.x-inset,min.y-inset,max.x-min.x+inset*2,max.y-min.y+inset*2};
        if(nextSlot){const auto r=nextSlot->localRect;need(!(r.x<gripInk.x+gripInk.width&&r.x+r.width>gripInk.x&&r.y<gripInk.y+gripInk.height&&r.y+r.height>gripInk.y),"External Notes editor overlaps final source resize grip");}
        decorations.push_back(clipped(*grip,local(layers,gripIndex),layers[0],gripInk));
        if(nextSlot){
            const auto r=nextSlot->localRect;Layer border;border.id=layers[0].id+"/external-editor-border";border.name="notes.external.editor.border";
            border.kind=modules::NotesLayerKind::shape;border.bounds={.5,.5,r.width-1,r.height-1};
            border.shape.kind=modules::NotesPathKind::roundedRect;border.shape.radius=nextSlot->cornerRadius-.5;
            border.shape.lineWidth=1;border.shape.stroke=editorAppearance->border;
            decorations.push_back(clipped(border,Matrix::translation(r.x,r.y),layers[0],core::Rect{0,0,r.width,r.height}));
        }
        // Stage every adapter allocation BEFORE LayerScene's single content
        // transaction. These known source children each produce one surface.
        // The scene namespace uses a uint64 counter (at most34 ASCII bytes);
        // reserve64 extra bytes and validate that contract before copying.
        const auto count=children.size();const auto borderID=layers[0].id+"/external-editor-border";
        std::size_t idCapacity=borderID.size()+64;for(const auto&layer:layers)idCapacity=std::max(idCapacity,layer.id.size()+64);
        auto reserveDraw=[&](DrawObject&draw){draw.sourceID.reserve(idCapacity);draw.meshID.reserve(idCapacity);draw.textureID.reserve(idCapacity);draw.masks.reserve(8);};
        std::vector<DrawObject> saved(count);std::vector<LayerPlacement> p(count);std::vector<std::vector<PlaneMask>> clipMasks(count);
        for(std::size_t i=0;i<count;++i){reserveDraw(saved[i]);clipMasks[i].reserve(8);}
        DrawObject nextAfter;std::size_t nextBorder{};if(nextSlot)reserveDraw(nextAfter);
        Json root=Json::Object{{"bounds",rect(layers[0].bounds)},{"masksToBounds",true},{"children",std::move(children)}};
        scene.load(root,options);need(scene.report().unsupported.empty(),"Notes scene has unsupported local raster or clipping state");
        const auto draws=scene.draws();need(draws.size()==count,"Source Notes surface schema changed unexpectedly");
        for(std::size_t i=0;i<count;++i){const auto&draw=draws[i];need(draw.sourceID.size()<=idCapacity&&draw.meshID.size()<=idCapacity&&draw.textureID.size()<=idCapacity&&draw.masks.size()<=8,"Source Notes identity/mask schema changed unexpectedly");
            saved[i]=draw;p[i]={i,draw.world,draw.opacity,{}};
        }
        if(nextSlot){const auto found=scene.surfaceIndex(borderID);need(found.has_value(),"Retained Notes editor border surface is missing");nextBorder=*found;nextAfter=saved[*found];}
        for(auto&f:next){const auto found=scene.surfaceIndex(f.id);need(found.has_value(),"Retained Notes feedback surface is missing");f.surface=*found;}
        const auto nextGrip=scene.surfaceIndex(grip->id);need(nextGrip.has_value(),"Retained Notes grip surface is missing");
        feedback=std::move(next);localDraws=std::move(saved);placements=std::move(p);masks=std::move(clipMasks);
        afterDraws[0]=std::move(nextAfter);borderSurface=nextBorder;editorSlot=nextSlot;
        gripSurface=*nextGrip;mediaLayout=std::move(nextMedia);mediaSlot.reset();
        if(mediaLayout){mediaSlot=NativeNotesMediaSlot{mediaLayout->geometry().content,mediaLayout->geometry().hasSeek};
            if(mediaMesh.empty())mediaMesh=localDraws.front().sourceID+"/media-quad";
            for(unsigned n=0;n<3;++n){auto&draw=mediaDraws[n];draw.sourceID=mediaMesh+"/"+std::to_string(n);draw.meshID=mediaMesh;draw.opacity=0;draw.masks.resize(2);}
            mediaDraws[0].textureID=mediaTexture;
            const auto colors=mediaLayout->progressColors(source->palette());
            for(unsigned n=1;n<3;++n){auto&draw=mediaDraws[n];for(unsigned c=0;c<4;++c){const auto v=colors[n][c];draw.linearTint[c]=static_cast<float>(c==3?v:v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4));}}
            mediaDraws[3]=localDraws[gripSurface];
        }
        revision=source->contentRevision();feedbackRevision=source->feedbackRevision();posed=false;++stats.contentUpdates;return true;
    }
};
NativeNotesCardScene::NativeNotesCardScene(modules::NotesCardPresentation&s,LayerRasterizer&r,LayerRasterOptions o,std::optional<NativeNotesExternalEditorAppearance>editor):impl_(std::make_unique<Impl>(s,r,std::move(o),editor)){}
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
bool NativeNotesCardScene::updatePose(const Matrix&workspace,float canvas,double time,std::optional<bool>visibilityOverride){
    auto&i=*impl_;need(i.revision&&i.revision==i.source->contentRevision(),"Synchronize Notes content before placement");
    need(workspace.finite()&&std::isfinite(canvas)&&canvas>=0&&canvas<=1,"Invalid Notes workspace placement or opacity");
    const auto&placement=i.source->placement();const auto r=placement.workspaceRect;
    const auto world=workspace*Matrix::translation(r.x,r.y);const auto inverse=core::source::inverseSourceMatrix(world);i.time(time);
    const auto opacityValue=visibilityOverride.value_or(placement.visible)?canvas:0.f;bool animated{};for(auto&f:i.feedback){animated|=f.active;if(time>=f.start+f.duration)f.active=false;}
    if(i.posed&&i.priorWorld==workspace&&i.priorRect==r&&i.priorOpacity==opacityValue&&!animated)return false;
    auto pose=[&](const auto&draws,auto&placements,auto&masks){for(std::size_t index=0;index<placements.size();++index){auto&p=placements[index];const auto&original=draws[index];p.world=world*original.world;p.opacity=original.opacity*opacityValue;
        auto&m=masks[index];m.clear();for(const auto&mask:original.masks){auto placed=mask;placed.worldToLocal=mask.worldToLocal*inverse;m.push_back(placed);}p.masks=m;
    }};
    pose(i.localDraws,i.placements,i.masks);
    for(const auto&f:i.feedback)i.placements[f.surface].opacity=static_cast<float>(Impl::value(f,time))*opacityValue;
    if(i.editorSlot){const auto&frame=i.placements[i.borderSurface];auto&after=i.afterDraws[0];after.world=frame.world;after.opacity=frame.opacity;after.masks.assign(frame.masks.begin(),frame.masks.end());
        // One resident resource, published once as the caller's after-editor
        // draw. Do not paint this border below glyph/selection as well.
        i.placements[i.borderSurface].opacity=0;
    }
    if(i.mediaSlot){
        const auto fitted=i.mediaWidth&&i.mediaHeight?i.mediaLayout->fittedImage(i.mediaWidth,i.mediaHeight):core::Rect{};
        const std::array rectangles{fitted,i.mediaProgress.fill,i.mediaProgress.handle};
        for(unsigned n=0;n<3;++n){auto&draw=i.mediaDraws[n];const auto&rect=rectangles[n];
            draw.world=world*Matrix::translation(rect.x,rect.y)*Matrix::scale(rect.width,rect.height,1);
            const bool show=n==0?!i.mediaTexture.empty():i.mediaSlot->hasProgress;
            draw.opacity=show?opacityValue:0;
            draw.masks[0]={inverse,{0,0,r.width,r.height},3};
            draw.masks[1]={inverse,n==0?i.mediaSlot->content:core::Rect{0,0,r.width,r.height},0};
        }
        auto&grip=i.mediaDraws[3];const auto&placement=i.placements[i.gripSurface];
        grip.world=placement.world;grip.opacity=placement.opacity;grip.masks.assign(placement.masks.begin(),placement.masks.end());
        i.placements[i.gripSurface].opacity=0;
    }
    i.scene.setPlacements(i.placements);
    i.priorWorld=workspace;i.priorRect=r;i.priorOpacity=opacityValue;i.posed=true;++i.stats.placementUpdates;return true;
}
bool NativeNotesCardScene::requiresFrames(double time)const {need(std::isfinite(time),"Notes feedback time must be finite");for(const auto&f:impl_->feedback)if(f.active&&time<f.start+f.duration)return true;return false;}
LayerScene&NativeNotesCardScene::scene()noexcept{return impl_->scene;}
const LayerScene&NativeNotesCardScene::scene()const noexcept{return impl_->scene;}
std::span<const DrawObject>NativeNotesCardScene::externalEditorAfterDraws()const noexcept{return{impl_->afterDraws.data(),impl_->editorSlot?1u:0u};}
const std::optional<NativeNotesExternalEditorSlot>&NativeNotesCardScene::externalEditorSlot()const noexcept{return impl_->editorSlot;}
void NativeNotesCardScene::setMediaTexture(std::string texture,unsigned width,unsigned height){
    auto&i=*impl_;need(i.mediaSlot.has_value(),"Initialize media card before binding pixels");
    need(texture.size()<=4096&&ehud::data::Json::validUtf8(texture),"Invalid borrowed media identity");
    need(texture.empty()?(width==0&&height==0):(width>0&&height>0&&width<=65536&&height<=65536),"Invalid media pixel extent");
    if(i.mediaTexture==texture&&i.mediaWidth==width&&i.mediaHeight==height)return;
    i.mediaTexture=std::move(texture);i.mediaWidth=width;i.mediaHeight=height;i.mediaDraws[0].textureID=i.mediaTexture;i.posed=false;
}
void NativeNotesCardScene::setMediaProgress(modules::NotesMediaProgressPose progress){
    auto&i=*impl_;need(i.mediaSlot.has_value(),"Initialize media card before progress");
    for(const auto&r:{progress.fill,progress.handle})for(double v:{r.x,r.y,r.width,r.height})need(std::isfinite(v)&&std::abs(v)<=65536,"Invalid media progress geometry");
    need(progress.fill.width>=0&&progress.fill.height>=0&&progress.handle.width>=0&&progress.handle.height>=0&&std::isfinite(progress.fraction)&&progress.fraction>=0&&progress.fraction<=1,"Invalid media progress extent");
    if(i.mediaProgress.fill==progress.fill&&i.mediaProgress.handle==progress.handle&&i.mediaProgress.active==progress.active)return;
    i.mediaProgress=progress;i.posed=false;
}
void NativeNotesCardScene::uploadMedia(Renderer&r){auto&i=*impl_;if(!i.mediaSlot)return;
    need(!i.mediaOwner||i.mediaOwner==&r,"Media quad belongs to a different renderer");
    if(i.mediaUploaded)return;
    const std::array<Vertex,4>vertices{{{{0,0,0},{0,0}},{{1,0,0},{1,0}},{{1,1,0},{1,1}},{{0,1,0},{0,1}}}};
    constexpr std::array<std::uint32_t,6>indices{0,1,2,0,2,3};r.setMesh(i.mediaMesh,1,{vertices,indices});i.mediaOwner=&r;i.mediaUploaded=true;
}
std::span<const DrawObject>NativeNotesCardScene::mediaDraws()const noexcept{return{impl_->mediaDraws.data(),impl_->mediaSlot&&impl_->mediaUploaded?4u:0u};}
const std::optional<NativeNotesMediaSlot>&NativeNotesCardScene::mediaSlot()const noexcept{return impl_->mediaSlot;}
bool NativeNotesCardScene::releaseMedia(Renderer&r){auto&i=*impl_;if(!i.mediaUploaded)return true;need(i.mediaOwner==&r,"Cannot retire another renderer's media quad");if(!r.removeMesh(i.mediaMesh))return false;i.mediaUploaded=false;i.mediaOwner=nullptr;return true;}
NativeNotesSceneStats NativeNotesCardScene::stats()const noexcept{return impl_->stats;}
} // namespace endfield::native
