#include "native/work_mode_scene.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
namespace endfield::native {namespace {
using Json=ehud::data::Json;using Matrix=core::Matrix4;namespace m=modules;
constexpr double pi=3.1415926535897932384626433832795;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}Json blank(){return Json::Object{{"bounds",Json::Array{0,0,0,0}},{"children",Json::Array{}}};}
}
// The source ring and ring base (prepareWorkModeArtwork: a CAShapeLayer
// whose path is CGPath's four-cubic circle of radius 215 about (220,220),
// stroked with lineWidth centred on the path, butt caps on a closed path) as
// one GPU mesh in canvas points instead of a canvas-sized bitmap. The mesh
// follows the same cubic segments (96 samples each: every chord within
// .01 pt of its curve) offset along their normals. Each edge ramps alpha
// across one raster pixel (1/pixelsPerPoint) centred on the geometric edge:
// the box-filter coverage the raster gives the same stroke (a stroke thinner
// than a pixel peaks at its width in pixels). Vertex alpha only; the draw's
// linearTint carries the colour.
void workModeRingMesh(const ehud::data::Json&shape,double pixelsPerPoint,std::vector<Vertex>&vertices,std::vector<std::uint32_t>&indices){
    need(std::isfinite(pixelsPerPoint)&&pixelsPerPoint>0,"Work Mode ring scale");
    const double width=shape["lineWidth"].number();need(std::isfinite(width)&&width>=0&&width<=440,"Work Mode ring width");
    const auto&path=shape["path"].array();need(path.size()>=2&&path.size()<=9,"Original Work Mode ring is one closed cubic path");
    const auto point=[](const ehud::data::Json&v){const auto&a=v.array();need(a.size()==2,"Work Mode ring point");return std::array<double,2>{a[0].number(),a[1].number()};};
    need(path[0]["op"].string()=="move"&&path[0]["points"].array().size()==1,"Original Work Mode ring starts with a move");
    const auto start=point(path[0]["points"].array()[0]);auto from=start;
    std::vector<std::array<std::array<double,2>,4>>cubics;cubics.reserve(path.size());
    for(std::size_t n=1;n<path.size();++n){const auto op=path[n]["op"].string();if(op=="close")continue;
        const auto&points=path[n]["points"].array();need(op=="cubic"&&points.size()==3,"Original Work Mode ring is made of cubic segments");
        cubics.push_back({from,point(points[0]),point(points[1]),point(points[2])});from=cubics.back()[3];}
    need(!cubics.empty()&&std::abs(from[0]-start[0])<1e-6&&std::abs(from[1]-start[1])<1e-6,"Original Work Mode ring path is closed");
    constexpr unsigned samples=96;constexpr unsigned rows=4;
    const double h=.5/pixelsPerPoint,half=width/2,plateau=std::abs(half-h);const float peak=static_cast<float>(std::min(1.,width*pixelsPerPoint));
    const std::array<double,rows>offsets{half+h,plateau,-plateau,-half-h};const std::array<float,rows>alphas{0,peak,peak,0};
    vertices.clear();indices.clear();vertices.reserve(cubics.size()*samples*rows);indices.reserve(cubics.size()*samples*(rows-1)*6);
    for(const auto&c:cubics)for(unsigned k=0;k<samples;++k){
        const double t=double(k)/samples,u=1-t;double p[2],d[2];
        for(unsigned a=0;a<2;++a){p[a]=u*u*u*c[0][a]+3*u*u*t*c[1][a]+3*u*t*t*c[2][a]+t*t*t*c[3][a];
            d[a]=3*u*u*(c[1][a]-c[0][a])+6*u*t*(c[2][a]-c[1][a])+3*t*t*(c[3][a]-c[2][a]);}
        const double length=std::hypot(d[0],d[1]);need(length>1e-9,"Work Mode ring tangent");const double nx=d[1]/length,ny=-d[0]/length;
        for(unsigned j=0;j<rows;++j)vertices.push_back({{static_cast<float>(p[0]+nx*offsets[j]),static_cast<float>(p[1]+ny*offsets[j]),0},{0,0},{1,1,1,alphas[j]}});
    }
    const auto count=static_cast<std::uint32_t>(vertices.size()/rows);
    for(std::uint32_t k=0;k<count;++k){const auto a=k*rows,b=((k+1)%count)*rows;
        for(std::uint32_t j=0;j+1<rows;++j)for(const auto v:{a+j,b+j,b+j+1,a+j,b+j+1,a+j+1})indices.push_back(v);}
}
namespace {
std::array<float,4>linearColor(const Json&color){
    const auto&c=color["sRGB"].array();need(c.size()==4,"Work Mode ring colour");std::array<float,4>out{};
    for(unsigned n=0;n<3;++n){const auto v=c[n].number();out[n]=static_cast<float>(v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4));}out[3]=static_cast<float>(c[3].number());return out;
}
}
struct NativeWorkModeScene::Impl {
    enum Role:unsigned{header,base,ring,clock,phase,controls,configuration,focus,focusText,count};
    struct Track{double from{},target{},start{},duration{};};static double value(Track t,double now){return t.duration>0?t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value((now-t.start)/t.duration):t.target;}
    struct Part {
        m::WorkModePart plan;LayerScene scene;std::unique_ptr<NativeLayerGroup>group;core::Rect coverage;bool groupUploaded{};
        std::vector<LayerPlacement>placements,active;std::vector<Track>feedback;std::array<PlaneMask,8>masks{},outerMasks{};Matrix outerWorld;float outerOpacity{};std::size_t outerMaskCount{};std::optional<PlaneShutter>outerShutter;
        Part(LayerRasterizer&r,m::WorkModePart p,const LayerRasterOptions&o,bool grouped,std::string groupID):plan(std::move(p)),scene(r),placements(plan.surfaces.size()),feedback(plan.surfaces.size()){
            scene.load(plan.layers,o);need(scene.report().unsupported.empty(),"Unsupported original Work Mode artwork");active.reserve(placements.size());
            for(std::size_t n=0;n<placements.size();++n){const auto&s=plan.surfaces[n];placements[n]={scene.surfaceIndex(s.id).value_or(std::size_t(-1)),s.local,s.opacity,{}};feedback[n].from=feedback[n].target=s.rim?.28:0;if(placements[n].surface!=std::size_t(-1))active.push_back(placements[n]);}scene.setPlacements(active);scene.prepareDraws();
            if(grouped){group=std::make_unique<NativeLayerGroup>(scene,std::move(groupID),o.pixelsPerPoint);coverage=group->localCoverageBounds();}
        }
        bool release(Renderer&r){return group?group->releaseResources(r):scene.releaseResources(r);}
        // A ring/base part whose circle became a mesh keeps its source plan
        // here (change detection); its scene loaded no surface.
        std::optional<Json>ringSource;
        const Json&sourceLayers()const{return ringSource?*ringSource:plan.layers;}
    };
    m::WorkModePresentation&state;LayerRasterizer&raster;LayerRasterOptions options;m::WorkModeAppearance appearance;std::array<std::unique_ptr<Part>,count>parts;std::vector<std::unique_ptr<Part>>retired;LayerScene ringCarrier;std::array<DrawObject,2>ringDraw;std::vector<LayerCompositionEntry>entries;
    // [ring base, ring]: the two canvas-sized circle strokes as GPU meshes
    // (workModeRingMesh), drawn after the empty ring carrier in source order
    // once both are resident. A mesh is rebuilt only when its path or width
    // changed (countdown 4 pt, stopwatch 2.5 pt); a colour is the draw's tint.
    struct RingMesh {std::string id;std::vector<Vertex>vertices;std::vector<std::uint32_t>indices;std::optional<Json>path;double width{-1};std::uint64_t revision{},uploaded{};bool resident{};};
    std::array<RingMesh,2>rings;
    void adoptRing(unsigned role,const Json&shape){
        auto&mesh=rings[role==ring?1:0];const double width=shape["lineWidth"].number();
        if(!mesh.path||mesh.width!=width||*mesh.path!=shape["path"]){workModeRingMesh(shape,options.pixelsPerPoint,mesh.vertices,mesh.indices);mesh.path=shape["path"];mesh.width=width;++mesh.revision;}
        ringDraw[role==ring?1:0].linearTint=linearColor(shape["strokeColor"]);
    }
    bool uploadRings(Renderer&r){bool changed{};for(auto&m:rings)if(m.revision&&m.uploaded!=m.revision){r.setMesh(m.id,m.revision,{m.vertices,m.indices});m.uploaded=m.revision;m.resident=true;changed=true;}return changed;}
    bool releaseRings(Renderer&r){bool ok=true;for(auto&m:rings)if(m.resident){if(r.removeMesh(m.id)){m.resident=false;m.uploaded=0;}else ok=false;}return ok;}
    bool ringsResident()const{return rings[0].resident||rings[1].resident;}
    std::uint64_t revision{},clockRevision{};bool dirty{true},pressed{},posed{};std::optional<std::string>hover;double lastTime{};WorkModeSceneStats stats;DrawObject validation;
    Impl(m::WorkModePresentation&s,LayerRasterizer&r,LayerRasterOptions o,m::WorkModeAppearance a):state(s),raster(r),options(std::move(o)),appearance(a),ringCarrier(r){ringCarrier.load(blank(),options);retired.reserve(count);entries.reserve(count);validation.sourceID="work-mode-pose";validation.masks.reserve(8);
        static std::atomic<std::uint64_t>serial{};const auto ns="workMode.scene."+std::to_string(serial.fetch_add(1));rings[0].id=ns+".ringBase";rings[1].id=ns+".ring";
        for(unsigned n=0;n<2;++n){auto&d=ringDraw[n];d.sourceID=rings[n].id+"/draw";d.meshID=rings[n].id;d.masks.reserve(8);}}
    // Owner time never runs backwards here: a time earlier than one already
    // seen (a host sample taken before a callback delivered a later one) is
    // the latest time, as the Mac's single CACurrentMediaTime never decreases.
    double at(double now)const{need(std::isfinite(now),"Work Mode needs finite owner time");return std::max(now,lastTime);}
    template<class F>void each(F&&f){for(auto&p:parts)if(p)f(*p);}
    void refreshEntries(){entries.clear();for(unsigned role=0;role<count;++role){auto&p=parts[role];if(!p||(role==focusText&&p->plan.surfaces.empty()))continue;if(role==base)continue; // its circle is ringDraw[0]
     if(role==ring){if(rings[0].resident&&rings[1].resident)entries.push_back({&ringCarrier,ringDraw});}else if(p->group){if(p->groupUploaded)entries.push_back(p->group->entry());}else entries.push_back({&p->scene,{}});}}
};
NativeWorkModeScene::NativeWorkModeScene(m::WorkModePresentation&s,LayerRasterizer&r,LayerRasterOptions o,m::WorkModeAppearance a):impl_(std::make_unique<Impl>(s,r,std::move(o),a)){}
NativeWorkModeScene::~NativeWorkModeScene()=default;
void NativeWorkModeScene::setAppearance(m::WorkModeAppearance a){if(a==impl_->appearance)return;impl_->appearance=a;impl_->dirty=true;}
bool NativeWorkModeScene::syncContent(double now){auto&i=*impl_;now=i.at(now);if(!i.dirty&&i.revision==i.state.revision()){
    if(i.clockRevision==i.state.clockRevision())return false;auto&p=*i.parts[Impl::clock];auto node=p.plan.layers["children"].array()[0];node["text"]["string"]=i.state.timeText();p.scene.updateLocalContent("workMode.clock",i.state.clockRevision(),node,i.options);p.plan.layers["children"]=Json::Array{std::move(node)};i.clockRevision=i.state.clockRevision();++i.stats.clockRasters;return true;
 }
 need(i.retired.empty(),"Publish prior Work Mode generation before replacing artwork");auto art=m::prepareWorkModeArtwork(i.state,i.appearance);m::WorkModePart focusCaption;focusCaption.layers=blank();
 // The source opacity feedback belongs to the permission CAShapeLayer and its
 // highlight children only. Its separate caption never participates in fade.
 if(i.state.focusPermission()){Json::Array captionChildren;auto children=art.focus.layers["children"].array();for(std::size_t n=0;n<art.focus.surfaces.size();){if(art.focus.surfaces[n].id=="workMode.focusStatus"){captionChildren.push_back(std::move(children[n]));focusCaption.surfaces.push_back(std::move(art.focus.surfaces[n]));children.erase(children.begin()+static_cast<std::ptrdiff_t>(n));art.focus.surfaces.erase(art.focus.surfaces.begin()+static_cast<std::ptrdiff_t>(n));}else ++n;}focusCaption.layers["children"]=std::move(captionChildren);art.focus.layers["children"]=std::move(children);}
 std::array<m::WorkModePart*,Impl::count>plans{&art.header,&art.ringBase,&art.ring,&art.clock,&art.phase,&art.controls,&art.configuration,&art.focus,&focusCaption};std::array<std::unique_ptr<Impl::Part>,Impl::count>next;std::size_t needed{};
 for(unsigned n=0;n<Impl::count;++n)if(n!=Impl::base&&n!=Impl::ring&&(!i.parts[n]||i.parts[n]->sourceLayers()!=plans[n]->layers))needed+=plans[n]->surfaces.size();need(i.raster.stats().entries<=LayerRasterizer::maximumEntries&&needed<=LayerRasterizer::maximumEntries-i.raster.stats().entries,"Work Mode generation exceeds shared raster budget");
 for(unsigned n=0;n<Impl::count;++n)if(!i.parts[n]||i.parts[n]->sourceLayers()!=plans[n]->layers){
  // The ring base and ring circles become meshes: their parts load no surface.
  std::optional<Json>ringSource;
  if(n==Impl::base||n==Impl::ring){auto&plan=*plans[n];need(plan.surfaces.size()==1&&plan.layers["children"].array().size()==1,"Original Work Mode ring is one circle stroke");
   ringSource=plan.layers;i.adoptRing(n,plan.layers["children"].array()[0]["shape"]);plan.surfaces.clear();plan.layers["children"]=Json::Array{};}
  next[n]=std::make_unique<Impl::Part>(i.raster,std::move(*plans[n]),i.options,n==Impl::configuration||(n==Impl::focus&&i.state.focusPermission()),n==Impl::configuration?"workMode.configuration":"workMode.focusAccess");next[n]->ringSource=std::move(ringSource);if(i.parts[n])for(std::size_t k=0;k<next[n]->plan.surfaces.size();++k)for(std::size_t old=0;old<i.parts[n]->plan.surfaces.size();++old)if(next[n]->plan.surfaces[k].id==i.parts[n]->plan.surfaces[old].id)next[n]->feedback[k]=i.parts[n]->feedback[old];}
 // All candidate rasterization/allocation succeeds before replacing borrowed
 // generation pointers. Old assets retire only after owner's new publication.
 for(unsigned n=0;n<Impl::count;++n)if(next[n]){if(i.parts[n])i.retired.push_back(std::move(i.parts[n]));i.parts[n]=std::move(next[n]);++i.stats.builds;}
 i.revision=i.state.revision();i.clockRevision=i.state.clockRevision();i.dirty=false;i.lastTime=now;i.refreshEntries();return true;
}
bool NativeWorkModeScene::setFeedback(std::optional<std::string_view>action,bool pressed,double now){auto&i=*impl_;now=i.at(now);if(i.hover.has_value()==action.has_value()&&(!action||*i.hover==*action)&&i.pressed==pressed&&!i.state.reducedMotion())return false;
 i.each([&](auto&p){for(std::size_t n=0;n<p.plan.surfaces.size();++n){const auto&s=p.plan.surfaces[n];if(!s.tint&&!s.rim)continue;const bool hit=action&&*action==s.action;const double target=s.rim?(hit?1:.28):(hit?(pressed?1:.62):0);auto&t=p.feedback[n];if(t.target!=target||(i.state.reducedMotion()&&t.duration)){t={Impl::value(t,now),target,now,i.state.reducedMotion()?0:pressed&&hit?.06:.14};}}});i.hover=action?std::optional(std::string(*action)):std::nullopt;i.pressed=pressed;i.lastTime=now;return true;
}
void NativeWorkModeScene::updatePose(const NativeWorkModePose&pose){auto&i=*impl_;const double now=i.at(pose.time);need(!i.dirty&&i.revision==i.state.revision()&&i.clockRevision==i.state.clockRevision(),"Synchronize Work Mode before pose");need(pose.world.finite()&&std::isfinite(pose.opacity)&&pose.opacity>=0&&pose.opacity<=1&&pose.ownerMasks.size()<=7,"Invalid Work Mode pose");if(pose.shutter)validatePlaneShutter(*pose.shutter);
 const auto layout=i.state.layout(now);const auto feedback=i.state.feedback(now);const auto ring=i.state.ring(now);const Matrix clockWorld=pose.world*Matrix::translation(220,layout.clockY)*Matrix::scale(layout.clockScale,layout.clockScale)*Matrix::translation(-172,-38.5);const auto clockInverse=core::source::inverseSourceMatrix(clockWorld);const Matrix ringWorld=pose.world*Matrix::translation(220,220)*Matrix::rotation(0,0,ring.rotation)*Matrix::translation(-220,-220);const auto ringInverse=core::source::inverseSourceMatrix(ringWorld);
 for(unsigned role=0;role<Impl::count;++role){auto&p=*i.parts[role];std::copy(pose.ownerMasks.begin(),pose.ownerMasks.end(),p.masks.begin());std::size_t maskCount=pose.ownerMasks.size();if(role==Impl::clock)p.masks[maskCount++]={clockInverse,{0,0,344,77}};if(p.group)maskCount=0;
  Matrix world=p.group?Matrix{}:role==Impl::clock?clockWorld*feedback.clock:role==Impl::phase?pose.world*Matrix::translation(89,layout.phaseY-15):role==Impl::ring?ringWorld:pose.world;
  for(std::size_t n=0;n<p.placements.size();++n){const auto&s=p.plan.surfaces[n];auto&placement=p.placements[n];Matrix button;
   if(!s.action.empty()&&s.action==feedback.action&&feedback.scale!=1){const auto actions=role==Impl::configuration?i.state.configurationActions():i.state.actions();for(const auto&a:actions)if(a.id==s.action){const auto cx=a.rect.x+a.rect.width*.5,cy=a.rect.y+a.rect.height*.5;button=Matrix::translation(cx,cy)*Matrix::scale(feedback.scale,feedback.scale)*Matrix::translation(-cx,-cy);break;}}
   placement.world=world*button*s.local;placement.opacity=(p.group?1:pose.opacity)*((s.tint||s.rim)?float(Impl::value(p.feedback[n],now)):s.opacity);placement.masks={p.masks.data(),maskCount};i.validation.world=placement.world;i.validation.opacity=placement.opacity;i.validation.masks.assign(placement.masks.begin(),placement.masks.end());i.validation.shutter=p.group?std::nullopt:pose.shutter;validateDrawObject(i.validation);
  }
 }
 // Validate all numeric candidates before touching any retained LayerScene.
 for(unsigned role=0;role<Impl::count;++role){auto&p=*i.parts[role];p.active.clear();for(const auto&v:p.placements)if(v.surface!=std::size_t(-1))p.active.push_back(v);p.scene.setPlacements(p.active);p.scene.setGroupShutter(p.group?std::nullopt:pose.shutter);}
 auto&base=i.ringDraw[0];base.world=pose.world;base.opacity=pose.opacity;base.masks.assign(pose.ownerMasks.begin(),pose.ownerMasks.end());base.shutter=pose.shutter;
 auto&draw=i.ringDraw[1];draw.world=ringWorld;draw.opacity=pose.opacity;draw.masks.assign(pose.ownerMasks.begin(),pose.ownerMasks.end());draw.shutter=pose.shutter;const auto cut=m::workModeRingCut(ring.fraction);draw.angularMask=AngularMask{ringInverse,{220,220},-.5*pi,ring.fraction==1?2*pi:cut.endAngle+.5*pi,cut.endPlane};
 for(unsigned role=0;role<Impl::count;++role){auto&p=*i.parts[role];if(!p.group)continue;p.outerWorld=pose.world;p.outerOpacity=pose.opacity*float(role==Impl::configuration?layout.configurationOpacity:feedback.focusOpacity);p.outerMaskCount=pose.ownerMasks.size();std::copy(pose.ownerMasks.begin(),pose.ownerMasks.end(),p.outerMasks.begin());p.outerShutter=pose.shutter;if(p.groupUploaded)p.group->setPose(p.outerWorld,p.outerOpacity,std::span(p.outerMasks.data(),p.outerMaskCount),p.outerShutter);}
 i.posed=true;i.lastTime=now;++i.stats.poses;
}
bool NativeWorkModeScene::uploadAnimations(Renderer&r){auto&i=*impl_;need(i.posed,"Place Work Mode before upload");const bool ringsWere=i.ringsResident();bool changed=i.uploadRings(r);bool entriesChanged=ringsWere!=i.ringsResident();for(auto&part:i.parts){auto&p=*part;if(!p.group)continue;if(!p.groupUploaded){changed|=p.group->uploadResources(r,p.coverage);p.groupUploaded=true;entriesChanged=true;}else changed|=p.group->updateLocal(r);p.group->setPose(p.outerWorld,p.outerOpacity,std::span(p.outerMasks.data(),p.outerMaskCount),p.outerShutter);}if(entriesChanged)i.refreshEntries();return changed;}
bool NativeWorkModeScene::requiresFrames(double now)const{const auto&i=*impl_;now=i.at(now);if(i.state.requiresFrames(now))return true;if(!i.state.active()||i.state.reducedMotion())return false;for(const auto&p:i.parts)if(p)for(const auto&t:p->feedback)if(t.duration>0&&t.from!=t.target&&now<t.start+t.duration)return true;return false;}
std::span<const LayerCompositionEntry>NativeWorkModeScene::entries()const noexcept{return impl_->entries;}
bool NativeWorkModeScene::collectRetired(Renderer&r){for(auto&p:impl_->retired)if(!p->release(r))return false;impl_->retired.clear();return true;}
bool NativeWorkModeScene::releaseResources(Renderer&r){bool result=collectRetired(r);for(auto&p:impl_->parts)if(p)result=p->release(r)&&result;result=impl_->releaseRings(r)&&result;return impl_->ringCarrier.releaseResources(r)&&result;}
bool NativeWorkModeScene::park(Renderer&r)noexcept{
    auto&i=*impl_;
    if(std::none_of(i.parts.begin(),i.parts.end(),[](const auto&p){return bool(p);})&&!i.ringsResident()&&i.retired.empty())return false;
    // Directly composed parts were released by the composition when it
    // dropped them; one that is still composed throws before anything else
    // is released (stay resident). Groups and the ring meshes are this
    // scene's own GPU resources.
    try{
        if(!collectRetired(r))return false;
        for(auto&p:i.parts)if(p&&!p->group&&!p->release(r))return false;
        for(auto&p:i.parts)if(p&&p->group&&p->groupUploaded){if(!p->release(r))return false;p->groupUploaded=false;}
        if(!i.releaseRings(r))return false;
    }catch(...){return false;}
    // Then the artwork itself: destroying the parts removes their rasters
    // from the shared rasterizer; the ring meshes' vertices go too. The next
    // syncContent rebuilds everything from the current state.
    for(auto&p:i.parts)p.reset();
    for(auto&m:i.rings){m.vertices=std::vector<Vertex>{};m.indices=std::vector<std::uint32_t>{};m.path.reset();m.width=-1;}
    i.entries.clear();i.dirty=true;i.posed=false;i.hover.reset();i.pressed=false;++i.stats.parks;return true;
}
WorkModeSceneStats NativeWorkModeScene::stats()const noexcept{
    auto value=impl_->stats;value.retiredParts=impl_->retired.size();
    for(const auto&p:impl_->parts)if(p){++value.parts;value.pixelBytes+=p->scene.report().pixelBytes;value.gpuResident|=p->group&&p->groupUploaded;}
    value.gpuResident|=impl_->ringsResident();
    return value;
}
}
#endif
