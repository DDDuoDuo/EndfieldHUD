#include "native/projection_media_scene.hpp"
#include "core/motion.hpp"
#include "core/source_camera.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::native {
namespace {
using J=ehud::data::Json;using R=core::Rect;using P=core::Point;using M=core::Matrix4;using C=modules::NotesColor;
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
J color(C c){return J::Object{{"sRGB",J::Array{c[0],c[1],c[2],c[3]}}};}
J box(std::string id,R r){return J::Object{{"id",std::move(id)},{"kind","layer"},{"bounds",J::Array{0,0,r.width,r.height}},{"position",J::Array{r.x,r.y}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
J text(std::string id,R r,std::string value,double size){auto j=box(std::move(id),r);j["kind"]="text";j["contentsScale"]=2;j["text"]=J::Object{{"string",std::move(value)},{"fontSize",size},{"font",J::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".AppleSystemUIFontDemi"},{"pointSize",10},{"symbolicTraits",2}}},{"foregroundColor",color({1,1,1,1})},{"truncation","end"},{"alignment","natural"},{"wrapped",false}};return j;}
J command(const char*op,std::initializer_list<P>ps){J::Array points;for(auto p:ps)points.push_back(J(J::Array{p.x,p.y}));return J::Object{{"op",op},{"points",std::move(points)}};}
J shape(std::string id,R r,J::Array path,std::optional<C>fill,std::optional<C>stroke,double width){auto j=box(std::move(id),r);j["kind"]="shape";j["shape"]=J::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):J{}},{"strokeColor",stroke?color(*stroke):J{}},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"}};return j;}
J::Array cut(R r){double c=std::min({4.,r.width/3,r.height/3});return {command("move",{{r.x+c,r.y}}),command("line",{{r.x+r.width,r.y}}),command("line",{{r.x+r.width,r.y+r.height-c}}),command("line",{{r.x+r.width-c,r.y+r.height}}),command("line",{{r.x,r.y+r.height}}),command("line",{{r.x,r.y+c}}),command("close",{})};}
std::string secondsText(double seconds){const auto value=static_cast<std::uint64_t>(std::min(std::floor(std::max(0.,seconds)),double(UINT32_MAX)));return std::to_string(value/60)+":"+(value%60<10?"0":"")+std::to_string(value%60);}
void validate(const modules::ProjectionMediaItem&i,const ProjectionMediaAppearance&a){need(bool(i.reference)&&i.id&&std::isfinite(i.frame.x)&&std::isfinite(i.frame.y)&&std::isfinite(i.frame.width)&&std::isfinite(i.frame.height)&&i.frame.width>0&&i.frame.height>0&&i.frame.width<=8192&&i.frame.height<=8192&&std::isfinite(i.frame.x+i.frame.width)&&std::isfinite(i.frame.y+i.frame.height)&&std::isfinite(a.seconds)&&a.seconds>=0,"Invalid Projection media artwork");for(auto c:a.accent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid Projection media accent");}
}
ProjectionMediaArtwork projectionMediaArtwork(const modules::ProjectionMediaItem&i,const ProjectionMediaAppearance&a){validate(i,a);const auto w=i.frame.width,h=i.frame.height;const auto g=modules::projectionMediaGeometry({w,h},i.reference->kind());auto face=box("projection.media.face",{0,0,w,h});face["backgroundColor"]=color({.06,.06,.06,.86});auto edge=a.accent;edge[3]=.5;face["borderColor"]=color(edge);face["borderWidth"]=.7;
    J::Array leaves{text("projection.media.title",g.title,a.error.value_or(i.reference->name()),10),text("projection.media.close",g.close,"×",17)};
    if(g.moving)leaves.push_back(text("projection.media.play",g.play,a.playing?"Ⅱ":"▶",15));
    if(g.video){leaves.push_back(text("projection.media.time",g.time,secondsText(a.seconds),10));auto rail=box("projection.media.rail",g.rail);rail["backgroundColor"]=color({1,1,1,.2});leaves.push_back(std::move(rail));auto fill=box("projection.media.fill",g.rail);fill["backgroundColor"]=color(a.accent);leaves.push_back(std::move(fill));auto thumb=box("projection.media.thumb",{g.seek.x-2.5,g.seek.y+g.seek.height*.5-3,5,8});thumb["backgroundColor"]=color({1,1,1,1});leaves.push_back(std::move(thumb));}
    leaves.push_back(shape("projection.media.resize",g.resize,{command("move",{{3,11}}),command("line",{{11,3}}),command("move",{{8,11}}),command("line",{{11,8}})}, {},C{1,1,1,.5},1));
    auto highlight=[&](const char*name,R r){const std::string id="projection.media."+std::string(name);auto tint=a.accent;tint[3]=.30;leaves.push_back(shape(id+".tint",r,cut({0,0,r.width,r.height}),tint,{},.9));leaves.push_back(shape(id+".rim",r,cut({-2,-2,r.width+4,r.height+4}),{},a.accent,.9));};if(g.moving)highlight("play",g.play);highlight("close",g.close);
    return {std::move(face),J::Object{{"bounds",J::Array{0,0,w,h}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",std::move(leaves)}}};
}
ProjectionMediaFaceMesh projectionMediaFaceMesh(P size,C accent){
    need(std::isfinite(size.x)&&std::isfinite(size.y)&&size.x>0&&size.y>0&&size.x<=8192&&size.y<=8192,"Invalid Projection face size");
    for(auto c:accent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid Projection face accent");
    const auto linear=[](double v){return float(v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4));};
    ProjectionMediaFaceMesh result;const auto w=size.x,h=size.y,b=std::min({.7,w*.5,h*.5});
    const std::array<R,5>rects{{{0,0,w,h},{0,0,w,b},{0,h-b,w,b},{0,b,b,h-2*b},{w-b,b,b,h-2*b}}};
    for(std::size_t n=0;n<rects.size();++n){const auto&r=rects[n];const auto c=n?C{accent[0],accent[1],accent[2],.5}:C{.06,.06,.06,.86};const std::array<float,4>rgba{linear(c[0]),linear(c[1]),linear(c[2]),float(c[3])};
        const std::array<P,4>points{{{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}}};
        for(std::size_t k=0;k<4;++k)result.vertices[n*4+k]={{float(points[k].x),float(points[k].y),0},{0,0},rgba};
        const auto base=std::uint32_t(n*4);const std::array<std::uint32_t,6>indices{base,base+1,base+2,base,base+2,base+3};std::copy(indices.begin(),indices.end(),result.indices.begin()+n*6);
    }return result;
}
#ifdef _WIN32
struct NativeProjectionMediaScene::Impl {
    struct Track{double from{},target{},start{},duration{};double sample(double t)const{return duration?from+(target-from)*core::CubicTiming{0,0,.58,1}.value(std::clamp((t-start)/duration,0.,1.)):target;}};
    LayerScene face,overlay;LayerRasterOptions options;
    std::optional<modules::ProjectionMediaItem>item;ProjectionMediaAppearance appearance;
    std::vector<M>locals;std::vector<LayerPlacement>placements;
    std::array<Track,4>tracks;std::array<std::size_t,4>feedback{};std::array<bool,2>highlighted{},pressed{};
    std::optional<std::size_t>fill,thumb;std::optional<DrawObject>borrowed;
    std::array<DrawObject,2>draw;ProjectionMediaFaceMesh mesh;
    M dpi,world;std::uint64_t revision{1},meshRevision{1};std::optional<double>lastTime;
    Renderer*owner{};bool ready{},meshResident{};
    Impl(LayerRasterizer&r,LayerRasterOptions o):face(r),overlay(r),options(std::move(o)){
        static std::atomic<std::uint64_t>sequence{};
        draw[0].sourceID=draw[0].meshID="projection.media.face.mesh:"+std::to_string(sequence.fetch_add(1,std::memory_order_relaxed));
        draw[1].masks.reserve(8);feedback.fill(SIZE_MAX);
    }
    void clock(double t)const{need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Projection media requires a finite monotonic owner clock");}
    void pose(double time){if(!ready)return;world=dpi*M::translation(item->frame.x,item->frame.y);const auto inverse=core::source::inverseSourceMatrix(world);draw[0].world=world;
        for(std::size_t n=0;n<placements.size();++n){auto&p=placements[n];p.world=world*locals[n];p.opacity=1;}
        for(std::size_t n=0;n<4;++n)if(feedback[n]!=SIZE_MAX)placements[feedback[n]].opacity=float(tracks[n].sample(time));
        const auto duration=item->reference->duration().value_or(0);const double fraction=duration>0?std::clamp(appearance.seconds/duration,0.,1.):0;
        if(fill){auto scale=M{};scale.values[0]=fraction>0?fraction:1;placements[*fill].world=world*locals[*fill]*scale;placements[*fill].opacity=fraction>0?1:0;}
        if(thumb){const auto width=modules::projectionMediaGeometry({item->frame.width,item->frame.height},item->reference->kind()).seek.width;placements[*thumb].world=world*M::translation(width*fraction,0)*locals[*thumb];}
        overlay.setPlacements(placements);
        if(borrowed){auto&out=draw[1];const auto&in=*borrowed;out.world=world*in.world;out.linearTint=in.linearTint;out.opacity=in.opacity;
            out.masks.resize(in.masks.size());for(std::size_t n=0;n<in.masks.size();++n){out.masks[n]=in.masks[n];out.masks[n].worldToLocal=in.masks[n].worldToLocal*inverse;}
            out.shutter=in.shutter;if(out.shutter)out.shutter->worldToLocal=in.shutter->worldToLocal*inverse;
            out.alphaMask=in.alphaMask;if(out.alphaMask)out.alphaMask->worldToLocal=in.alphaMask->worldToLocal*inverse;
            out.angularMask=in.angularMask;if(out.angularMask)out.angularMask->worldToLocal=in.angularMask->worldToLocal*inverse;
        }
    }
};
NativeProjectionMediaScene::NativeProjectionMediaScene(LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}
NativeProjectionMediaScene::~NativeProjectionMediaScene()=default;
bool NativeProjectionMediaScene::sync(const modules::ProjectionMediaItem&item,const ProjectionMediaAppearance&a){
    validate(item,a);auto&i=*impl_;need(!i.item||i.item->id==item.id,"A Projection media scene keeps one item identity");
    const bool structure=!i.item||i.item->frame.width!=item.frame.width||i.item->frame.height!=item.frame.height||i.item->reference!=item.reference||i.appearance.accent!=a.accent;
    const bool playChanged=i.appearance.playing!=a.playing,errorChanged=i.appearance.error!=a.error;
    const bool timeChanged=item.reference->kind()==modules::NotesMediaKind::video&&std::floor(i.appearance.seconds)!=std::floor(a.seconds);
    bool content=structure||playChanged||timeChanged||errorChanged;
    if(structure){auto art=projectionMediaArtwork(item,a);auto mesh=projectionMediaFaceMesh({item.frame.width,item.frame.height},a.accent);
        // Face is a fixed resident mesh; this empty scene anchors its paint-order
        // slot and the borrowed provider, without any large raster allocation.
        // Even a non-drawing carrier needs explicit four-coordinate bounds;
        // LayerScene validates them before deciding that no raster is needed.
        if(!i.ready)i.face.load(box("projection.media.carrier",{0,0,0,0}),i.options);
        i.overlay.load(art.overlay,i.options);need(i.overlay.report().unsupported.empty(),"Unsupported Projection media source artwork");
        i.locals.clear();i.placements.resize(i.overlay.draws().size());for(std::size_t n=0;n<i.placements.size();++n){i.locals.push_back(i.overlay.draws()[n].world);i.placements[n].surface=n;}
        i.fill=i.overlay.surfaceIndex("projection.media.fill");i.thumb=i.overlay.surfaceIndex("projection.media.thumb");i.feedback.fill(SIZE_MAX);
        constexpr std::array ids{"projection.media.play.tint","projection.media.play.rim","projection.media.close.tint","projection.media.close.rim"};
        for(std::size_t n=0;n<4;++n){i.feedback[n]=i.overlay.surfaceIndex(ids[n]).value_or(SIZE_MAX);const bool active=i.highlighted[n/2];const double target=n%2?(active?1:.28):(active?(i.pressed[n/2]?1:.62):0);i.tracks[n]={target,target,0,0};}
        i.mesh=mesh;++i.meshRevision;
    }else{
        const auto g=modules::projectionMediaGeometry({item.frame.width,item.frame.height},item.reference->kind());
        if(errorChanged)i.overlay.updateLocalContent("projection.media.title",++i.revision,text("projection.media.title",g.title,a.error.value_or(item.reference->name()),10),i.options);
        if(playChanged&&g.moving)i.overlay.updateLocalContent("projection.media.play",++i.revision,text("projection.media.play",g.play,a.playing?"Ⅱ":"▶",15),i.options);
        if(timeChanged)i.overlay.updateLocalContent("projection.media.time",++i.revision,text("projection.media.time",g.time,secondsText(a.seconds),10),i.options);
        content=i.overlay.refreshTypography()||content;
    }
    i.item=item;i.appearance=a;i.ready=true;i.pose(i.lastTime.value_or(0));return content;
}
void NativeProjectionMediaScene::setDraw(std::optional<DrawObject>d){auto&i=*impl_;need(i.ready,"Synchronize Projection media before binding its provider");
    if(d){need(!d->sourceID.empty()&&!d->meshID.empty(),"Projection media requires an explicit resident draw");validateDrawObject(*d);d->sourceID="projection.media.borrowed:"+std::to_string(i.item->id);i.draw[1]=*d;i.draw[1].masks.reserve(8);}
    i.borrowed=std::move(d);i.pose(i.lastTime.value_or(0));
}
void NativeProjectionMediaScene::setPose(const M&dpi){need(dpi.finite(),"Invalid Projection media plane");(void)core::source::inverseSourceMatrix(dpi);impl_->dpi=dpi;}
void NativeProjectionMediaScene::setFeedback(std::optional<std::string_view>a,bool pressed,bool reduced,double t){auto&i=*impl_;i.clock(t);need(i.ready,"Synchronize Projection media before feedback");need(!a||*a=="play"||*a=="close","Unknown Projection media control");
    for(std::size_t control=0;control<2;++control){const bool active=a&&*a==(control?"close":"play")&&i.feedback[control*2]!=SIZE_MAX;const bool down=active&&pressed;
        const bool changed=active!=i.highlighted[control]||down!=i.pressed[control];i.highlighted[control]=active;i.pressed[control]=down;
        for(std::size_t k=0;k<2;++k){auto&track=i.tracks[control*2+k];if(!changed&&!reduced)continue;const double target=k?(active?1:.28):(active?(down?1:.62):0);const auto current=track.sample(t);track={current,target,t,reduced||current==target?0:down?.06:.14};}
    }i.lastTime=t;
}
void NativeProjectionMediaScene::update(double t){auto&i=*impl_;i.clock(t);need(i.ready,"Synchronize Projection media before placement");i.pose(t);i.lastTime=t;}
bool NativeProjectionMediaScene::requiresFrames(double t)const{const auto&i=*impl_;i.clock(t);for(const auto&v:i.tracks)if(v.duration&&t<v.start+v.duration)return true;return false;}
void NativeProjectionMediaScene::upload(Renderer&r){auto&i=*impl_;need(i.ready&&(!i.owner||i.owner==&r),"Projection media belongs to another renderer or is unsynchronized");r.setMesh(i.draw[0].meshID,i.meshRevision,{i.mesh.vertices,i.mesh.indices});i.meshResident=true;i.owner=&r;i.face.uploadResources(r);i.overlay.uploadResources(r);}
std::array<LayerCompositionEntry,2>NativeProjectionMediaScene::entries(){auto&i=*impl_;need(i.ready,"Synchronize Projection media before publication");return {{{&i.face,std::span<const DrawObject>(i.draw.data(),i.borrowed?2:1)},{&i.overlay,{}}}};}
bool NativeProjectionMediaScene::releaseResources(Renderer&r){auto&i=*impl_;need(!i.owner||i.owner==&r,"Projection media resources belong to another renderer");const bool a=i.face.releaseResources(r),b=i.overlay.releaseResources(r);if(i.meshResident&&r.removeMesh(i.draw[0].meshID))i.meshResident=false;if(a&&b&&!i.meshResident){i.owner=nullptr;return true;}return false;}
#endif
}
