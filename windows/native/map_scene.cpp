#include "native/map_scene.hpp"
#include "native/centre_backdrop_ramp.hpp"
#include "modules/hud_design_tokens.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
std::uint32_t mapHostCutSide()noexcept{return std::uint32_t(std::ceil(modules::MapSurfaceGeometry::side*mapHostCutScale));}
core::Rect mapHostCutBounds()noexcept{const double side=mapHostCutSide()/mapHostCutScale;return {-135,-126,side,side};}
}

#ifdef _WIN32
#include "native/layer_scene.hpp"
#include "modules/hud_design_nodes.hpp"
#include "core/source_camera.hpp"
#include "core/motion.hpp"
#include <atomic>
#include <map>
namespace endfield::native {
namespace {
using J=ehud::data::Json;using R=core::Rect;using C=modules::MapColor;using M=core::Matrix4;using P=core::Point;using G=modules::MapSurfaceGeometry;
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
J array(R r){return J::Array{r.x,r.y,r.width,r.height};}
J color(C c){return J::Object{{"sRGB",J::Array{c[0],c[1],c[2],c[3]}}};}
J base(std::string id,R r,const char*kind="layer") {return J::Object{{"id",std::move(id)},{"class",std::string(kind)=="shape"?"CAShapeLayer":"CALayer"},{"kind",kind},{"bounds",array({0,0,r.width,r.height})},{"position",J::Array{r.x,r.y}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
constexpr std::array<std::uint32_t,6>mapIndices{0,1,2,0,2,3};
constexpr std::array<Vertex,4>mapQuad{{{{0,0,0},{0,0}},{{1,0,0},{1,0}},{{1,1,0},{1,1}},{{0,1,0},{0,1}}}};
std::array<float,4>linear(C c){std::array<float,4>out{};for(unsigned n=0;n<3;++n)out[n]=float(c[n]<=.04045?c[n]/12.92:std::pow((c[n]+.055)/1.055,2.4));out[3]=float(c[3]);return out;}
bool imageValid(const std::shared_ptr<const MapPaintImage>&p){return p&&p->width&&p->height&&p->width<=256&&p->height<=256&&p->straightRGBA.size()==std::size_t(p->width)*p->height*4;}
J circle(std::string id,double radius,double width,bool fill){const double k=.5522847498307936,r=radius;const auto cmd=[](const char*op,std::initializer_list<P>ps){J::Array pts;for(auto p:ps)pts.push_back(J::Array{p.x,p.y});return J(J::Object{{"op",op},{"points",std::move(pts)}});};auto j=base(std::move(id),{0,0,0,0},"shape");j["shape"]=J::Object{{"path",J::Array{cmd("move",{{r,0}}),cmd("cubic",{{r,r*k},{r*k,r},{0,r}}),cmd("cubic",{{-r*k,r},{-r,r*k},{-r,0}}),cmd("cubic",{{-r,-r*k},{-r*k,-r},{0,-r}}),cmd("cubic",{{r*k,-r},{r,-r*k},{r,0}}),cmd("close",{})}},{"fillColor",fill?color({1,1,1,1}):J{}},{"strokeColor",fill?J{}:color({1,1,1,1})},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"}};return j;}
struct Motion {P from{},to{};double fromScale{1},toScale{1};modules::MapImagePlacement sample(double p)const{return {{from.x+(to.x-from.x)*p,from.y+(to.y-from.y)*p},fromScale+(toScale-fromScale)*p};}};
bool sameMasks(std::span<const PlaneMask>a,std::span<const PlaneMask>b){if(a.size()!=b.size())return false;for(std::size_t n=0;n<a.size();++n)if(a[n].worldToLocal!=b[n].worldToLocal||a[n].bounds!=b[n].bounds||a[n].cornerRadius!=b[n].cornerRadius)return false;return true;}
// The group's px per box px: the design density (860 px x density per side),
// quantized to .5 like it, within the renderer's group limit.
// Capped at 1.5 (lead decision): the terrain detail tops out near 1.3 px per
// box px, so more only sharpens the well and pins while multiplying the fill
// the pulse re-renders at 30 Hz.
double groupDensity(double requested){const double limit=std::min(1.5,std::floor(std::sqrt(double(Renderer::maximumNativeGroupPixels))/G::side*2)/2);return std::clamp(requested,1.,limit);}
// Scene units -> box px (the ramp's own space, the alpha mask's worldToLocal
// for the group's children): box = (295, 304) + (u - 220) k.
const M unitsToBox=M::translation(295-220*G::unitScale,304-220*G::unitScale,0)*M::scale(G::unitScale,G::unitScale);
}
struct NativeMapScene::Impl {
    struct Marker {std::string groupID;std::array<DrawObject,5>draws;DrawObject output;modules::MapPinStyle style{};std::optional<double>styleStart;bool wanted{},wasWanted{},configured{};};
    std::map<std::string,Marker,std::less<>>markers;
    modules::MapState&state;NativeMapRaster&raster;LayerRasterizer&paint;LayerRasterOptions options;MapPlayerImages player;MapAppearance appearance;
    LayerScene carrier;std::string prefix,groupID,quadID,rampID,scrimID;std::array<std::string,3>playerIDs;std::array<std::string,4>shapeIDs;
    std::array<std::shared_ptr<const LayerRasterImage>,4>shapes;std::array<R,4>shapeBounds;std::shared_ptr<const LayerRasterImage>ramp;modules::MapScrim scrim;
    PlaneAlphaMask rampMask;std::vector<DrawObject>draws;
    std::vector<modules::MapPin>pins;std::array<Motion,128>pinMotion;std::array<bool,128>visible{},pulsing{};std::array<double,128>pulseStart{};std::array<std::size_t,128>paintOrder{};
    std::array<Motion,4>imageMotion;std::optional<double>cameraStart;modules::MapViewport camera;std::optional<modules::MapRasterFrame>frame;
    std::shared_ptr<const MapPaintImage>detail,backdrop;const MapPaintImage*uploadedDetail{},*uploadedBackdrop{};
    std::optional<std::string>selection;bool appearanceDirty{true},contentDirty{},resourcesDirty{true},localDirty{true},ready{},uploaded{},active{},meshResident{};
    std::uint64_t shapeRevision{1},imageRevision{},cameraRevision{},pinRevision{},styleRevision{};double time{},lastLocalTime{-1},boxDensity{1};
    Renderer*owner{};std::vector<std::string>residentTextures;DrawObject output,staged;std::array<LayerCompositionEntry,1>entries;MapSceneStats counts;
    // The host cut: wanted plates (box px from the cut's origin), those
    // its resident texture holds, and whether it is resident/current.
    std::uint32_t hostCutSide{};std::vector<std::uint8_t>maskRegion;std::vector<float>maskRow;
    modules::MapCutouts hostCut,appliedHostCut;std::string hostCutID;std::uint64_t hostCutRevision{};bool hostCutWanted{},hostCutResident{},hostCutCurrent{};
    Impl(modules::MapState&s,NativeMapRaster&r,LayerRasterizer&p,LayerRasterOptions o,MapPlayerImages images,MapAppearance a):state(s),raster(r),paint(p),options(std::move(o)),player(std::move(images)),appearance(a),carrier(p){
        need(imageValid(player.halo)&&imageValid(player.beam)&&imageValid(player.glyph),"Map needs all three bounded original player images");
        static std::atomic<std::uint64_t>serial{};prefix="map.scene:"+std::to_string(serial.fetch_add(1,std::memory_order_relaxed));groupID=prefix+"/group";quadID=prefix+"/quad";rampID=prefix+"/ramp";scrimID=prefix+"/scrim";hostCutID=prefix+"/host-cut";
        for(unsigned n=0;n<3;++n)playerIDs[n]=prefix+"/player"+std::to_string(n);for(unsigned n=0;n<4;++n)shapeIDs[n]=prefix+"/shape"+std::to_string(n);
        hostCutSide=mapHostCutSide();
        // HUDCentreBackdrop.mask: one shared half-resolution ramp (WorldMapSurface.prepareForPresentation),
        // the one definition shared with Personal ID's art (native/centre_backdrop_ramp.hpp).
        ramp=rasterizeCentreBackdropRamp(paint,options,rampID);
        // The raster's bounds are the node's own (its frame's origin at 0): box -> node.
        const auto origin=modules::design::centreBackdropCentre;const auto radius=modules::design::centreBackdropRadius;
        rampMask=PlaneAlphaMask{M::translation(radius-origin.x,radius-origin.y,0)*unitsToBox,ramp->bounds,rampID};
        // The header scrim from the square's top to the header's bottom (81 + 126 map px).
        scrim=modules::mapHeaderScrim(modules::design::metric::headerHeight+126);need(scrim.rows,"Map header scrim");
        output.sourceID=prefix+"/output";staged.sourceID=output.sourceID;output.masks.reserve(8);staged.masks.reserve(8);carrier.load(base(prefix+"/carrier",{0,0,0,0}),options);entries[0]={&carrier,std::span(&output,1)};pins.reserve(128);residentTextures.reserve(12);draws.reserve(6+128);
    }
    void clock(double t)const{need(std::isfinite(t)&&t>=time,"Map scene requires a finite monotonic owner clock");}
    double unitDensity()const{return groupDensity(boxDensity)*G::unitScale;}
    // Creates the host cut on the first presentation (one full texture), then
    // repaints in place only the texels under the old and new plates.
    void applyHostCut(Renderer&r){if(!hostCutWanted)return;const auto side=hostCutSide;
        if(!hostCutResident){std::vector<std::uint8_t>pixels(std::size_t(side)*side*4,255);
            // Opaque white but under the plates (their texels rastered in the staging, then copied in).
            if(const auto region=modules::mapCutoutTexels(hostCut,mapHostCutScale,side,side);!region.empty()){const auto bytes=std::size_t(region.width)*region.height*4;
                if(maskRegion.size()<bytes)maskRegion.resize(bytes+bytes/2);if(maskRow.size()<region.width)maskRow.resize(region.width+region.width/2);
                modules::mapCutoutMask(hostCut,mapHostCutScale,region,std::span<std::uint8_t>(maskRegion.data(),bytes),std::span<float>(maskRow.data(),maskRow.size()));
                for(std::uint32_t y=0;y<region.height;++y)std::copy_n(maskRegion.data()+std::size_t(y)*region.width*4,std::size_t(region.width)*4,pixels.data()+((std::size_t(region.y)+y)*side+region.x)*4);
                counts.cutoutMaskBytes=maskRegion.capacity()+maskRow.capacity()*sizeof(float);}
            r.setTexture(hostCutID,++hostCutRevision,{side,side,pixels,TextureColorSpace::sRGB,TextureFilter::linear,true});
            hostCutResident=true;appliedHostCut=hostCut;++counts.hostCutUploads;counts.hostCutBytes=std::size_t(side)*side;} // white alpha: one byte per texel (A8)
        else if(!(hostCut==appliedHostCut)){const auto region=modules::mapCutoutUnion(modules::mapCutoutTexels(appliedHostCut,mapHostCutScale,side,side),modules::mapCutoutTexels(hostCut,mapHostCutScale,side,side));
            if(!region.empty()){const auto bytes=std::size_t(region.width)*region.height*4;if(maskRegion.size()<bytes)maskRegion.resize(bytes+bytes/2);if(maskRow.size()<region.width)maskRow.resize(region.width+region.width/2);
                const std::span<std::uint8_t>pixels(maskRegion.data(),bytes);modules::mapCutoutMask(hostCut,mapHostCutScale,region,pixels,std::span<float>(maskRow.data(),maskRow.size()));
                r.updateTexture(hostCutID,++hostCutRevision,region.x,region.y,{region.width,region.height,pixels});++counts.hostCutUploads;counts.cutoutMaskBytes=maskRegion.capacity()+maskRow.capacity()*sizeof(float);}
            appliedHostCut=hostCut;}
        hostCutCurrent=true;}
    double progress(double t)const{return cameraStart?core::CubicTiming{.42,0,.58,1}.value(std::clamp((t-*cameraStart)/.18,0.,1.)):1;}
    void loadShapes(){auto o=options;o.pixelsPerPoint=std::min(4.,unitDensity());const std::array<double,4>radius{5.3,8,4,1.8},width{0,.55,1.2,0};
        for(unsigned n=0;n<4;++n){shapes[n]=paint.rasterize(shapeIDs[n],shapeRevision,circle(shapeIDs[n],radius[n],width[n],n==0||n==3),o);need(shapes[n]->complete(),"Unsupported original Map pin path");shapeBounds[n]=shapes[n]->bounds;}}
    // applyCamera (WorldMapSurface.swift:411-424): the raster square and the
    // pins follow the camera; an animated camera eases .18 s from the shown pose.
    void targets(bool animated,double t,bool pinStructure){const double old=progress(t);const auto oldCamera=camera;camera=state.camera();auto placements=modules::mapBackdropPlacements(camera);for(unsigned n=0;n<4;++n){auto target=n<3?placements[n]:(frame?modules::mapRasterPlacement(*frame,camera):modules::MapImagePlacement{});auto prior=imageMotion[n].sample(old);imageMotion[n]={animated&&ready?prior.position:target.position,target.position,animated&&ready?prior.scale:target.scale,target.scale};}
        for(std::size_t n=0;n<pins.size();++n){const auto target=modules::mapScreen(pins[n].x,pins[n].y,camera);const auto prior=pinMotion[n].sample(old);const bool keep=animated&&ready&&!pinStructure&&visible[n];pinMotion[n]={keep?prior.position:target,target,1,1};}
        cameraStart=animated&&ready&&oldCamera!=camera?std::optional(t):std::nullopt;localDirty=true;
    }
    void pinVisibility(double t){const auto selected=state.selection()?std::optional<std::string_view>(*state.selection()):std::nullopt;const auto visuals=modules::mapPinVisuals(pins,camera,selected,state.active(),appearance.reducedMotion,appearance.ambient);counts.visiblePins=visuals.count;counts.pulsingPins=0;
        for(std::size_t n=0;n<pins.size();++n){const modules::MapPinVisual*value=nullptr;for(std::size_t k=0;k<visuals.count;++k)if(visuals.items[k].id==pins[n].id){value=&visuals.items[k];break;}const bool next=value&&value->pulses;if(next&&!pulsing[n])pulseStart[n]=t;pulsing[n]=next;visible[n]=value!=nullptr;counts.pulsingPins+=next?1:0;paintOrder[n]=n;}
        // The selected marker's zPosition 1: drawn above the others.
        if(state.selection()){const auto found=std::find_if(paintOrder.begin(),paintOrder.begin()+pins.size(),[&](auto n){return pins[n].id==*state.selection();});if(found!=paintOrder.begin()+pins.size())std::rotate(found,found+1,paintOrder.begin()+pins.size());}
    }
    bool syncMarkers(double t,bool styleEvent){
        bool changed{};for(auto&[id,m]:markers){(void)id;m.wasWanted=m.wanted;m.wanted=false;}
        for(std::size_t n=0;n<pins.size();++n){if(!visible[n])continue;auto found=markers.find(pins[n].id);const bool fresh=found==markers.end();
            if(fresh){found=markers.try_emplace(pins[n].id).first;auto&m=found->second;m.groupID=prefix+"/marker/"+pins[n].id;for(unsigned part=0;part<5;++part){m.draws[part].sourceID=m.groupID+"/"+std::to_string(part);m.draws[part].meshID=quadID;}changed=true;}
            auto&m=found->second;m.wanted=true;const bool styleChanged=!fresh&&m.style!=pins[n].style;
            if(fresh||styleChanged){m.style=pins[n].style;
                // activatePin's turn-in: only for the cycled pin, while active and not Reduce Motion.
                m.styleStart=styleChanged&&styleEvent&&m.wasWanted&&pins[n].id==state.styledPin()&&state.active()&&!appearance.reducedMotion?std::optional(t):std::nullopt;
                for(unsigned part=0;part<5;++part){const bool playerPin=m.style==modules::MapPinStyle::player;auto&d=m.draws[part];d.textureID=shapeIDs[part==0?0:part==1?1:part==2?2:3];d.blend=NativeBlend::sourceOver;if(playerPin){if(part==0)d.textureID=playerIDs[0];else if(part==2){d.textureID=playerIDs[1];d.blend=NativeBlend::screen;}else if(part==3)d.textureID=playerIDs[2];}}changed=true;}
            if(appearance.reducedMotion||!state.active())m.styleStart.reset();
        }
        for(const auto&[id,m]:markers){(void)id;changed|=m.wanted!=m.wasWanted;}
        return changed;
    }
    // Paint order (WorldMapSurface.swift:85-94): the well, the raster host
    // (backdrop wraps, detail), the scrim, the pins; every child faded by the ramp.
    void structure(){draws.clear();auto add=[&](std::string id,std::string texture={}){DrawObject d;d.sourceID=std::move(id);d.meshID=quadID;d.textureID=std::move(texture);d.alphaMask=rampMask;draws.push_back(std::move(d));};
        add(prefix+"/background");for(unsigned n=0;n<4;++n)add(prefix+"/image"+std::to_string(n),n<3?(backdrop?prefix+"/backdrop":""):(detail?prefix+"/detail":""));add(prefix+"/scrim",scrimID);
        for(std::size_t order=0;order<pins.size();++order){const auto n=paintOrder[order];const auto found=markers.find(pins[n].id);if(found!=markers.end()&&found->second.wanted&&found->second.configured){draws.push_back(found->second.output);draws.back().alphaMask=rampMask;}else{add(prefix+"/pin/"+pins[n].id);draws.back().opacity=0;}}
        contentDirty=false;localDirty=true;++counts.pinBuilds;
    }
    void local(double t){if(!ready||draws.empty()||(!localDirty&&lastLocalTime==t))return;const bool animate=cameraStart.has_value();bool styleActive=false;for(const auto&[id,m]:markers){(void)id;styleActive|=m.wanted&&m.styleStart.has_value();}if(!localDirty&&!animate&&!counts.pulsingPins&&!styleActive)return;
        const auto p=progress(t);auto&bg=draws[0];bg.world=M::translation(mapSceneBounds.x,mapSceneBounds.y,0)*M::scale(mapSceneBounds.width,mapSceneBounds.height);bg.linearTint=linear({31/255.,31/255.,31/255.,.89});
        for(unsigned n=0;n<4;++n){const auto pos=imageMotion[n].sample(p);auto&d=draws[n+1];const bool show=n<3?bool(backdrop):bool(detail&&frame);const double w=n<3?1024:frame?frame->screenRect.width:1,h=n<3?512:frame?frame->screenRect.height:1;d.world=M::translation(pos.position.x,pos.position.y)*M::scale(w*pos.scale,h*pos.scale);d.opacity=show?1:0;}
        draws[5].world=M::translation(mapSceneBounds.x,mapSceneBounds.y,0)*M::scale(mapSceneBounds.width,scrim.height/G::unitScale);
        const auto assets=modules::mapPlayerAssets();for(std::size_t order=0;order<pins.size();++order){const auto n=paintOrder[order];auto&parent=draws[6+order];const auto found=markers.find(pins[n].id);if(found==markers.end()||!found->second.wanted){parent.opacity=0;continue;}auto&m=found->second;const auto position=pinMotion[n].sample(p).position;const auto col=linear(modules::mapPinColor(pins[n].style));const bool playerPin=pins[n].style==modules::MapPinStyle::player;const bool selected=selection&&pins[n].id==*selection;
            for(unsigned part=0;part<5;++part){auto&d=m.draws[part];d.opacity=1;d.linearTint=col;R bounds=shapeBounds[part==0?0:part==1?1:part==2?2:3];double scale=1;
                if(part==4)d.opacity=0;else if(playerPin&&part!=1){const unsigned image=part==0?0:part-1;bounds=assets[image].rect;d.opacity*=float(assets[image].opacity);d.linearTint={1,1,1,1};if(part==3){const auto ratio=double(player.glyph->width)/player.glyph->height;const double w=std::min(bounds.width,bounds.height*ratio),h=w/ratio;bounds={bounds.x+(bounds.width-w)*.5,bounds.y+(bounds.height-h)*.5,w,h};}}
                else if(part==0){d.linearTint=linear({.06,.06,.06,1});d.opacity*=.82f;}
                if(part==1){if(pulsing[n]){const auto pulse=modules::mapPulse(std::max(0.,t-pulseStart[n]));scale=pulse.scale;d.opacity*=float(pulse.opacity);}else d.opacity*=selected?.65f:.25f;}
                d.world=M::scale(scale,scale)*M::translation(bounds.x,bounds.y)*M::scale(bounds.width,bounds.height);
            }
            const auto style=m.styleStart?modules::mapStyleTransition(t-*m.styleStart):modules::MapStyleTransition{};parent.world=M::translation(position.x,position.y)*M::rotation(0,0,style.rotation);parent.opacity=m.configured?float(style.opacity):0;
            if(m.styleStart&&t>=*m.styleStart+.16)m.styleStart.reset();
            if(owner&&uploaded&&!resourcesDirty&&m.configured)owner->setNativeGroupDraws(m.groupID,m.draws);
        }
        if(cameraStart&&t>=*cameraStart+.18)cameraStart.reset();lastLocalTime=t;localDirty=false;++counts.localUpdates;if(owner&&uploaded&&!resourcesDirty)owner->setNativeGroupDraws(groupID,draws);
    }
};
NativeMapScene::NativeMapScene(modules::MapState&s,NativeMapRaster&r,LayerRasterizer&p,LayerRasterOptions o,MapPlayerImages images,MapAppearance a):impl_(std::make_unique<Impl>(s,r,p,std::move(o),std::move(images),a)){}
NativeMapScene::~NativeMapScene(){auto&i=*impl_;if(i.owner)try{releaseResources(*i.owner);}catch(...){}for(const auto&id:i.shapeIDs)i.paint.remove(id);i.paint.remove(i.rampID);}
bool NativeMapScene::setAppearance(MapAppearance a){auto&i=*impl_;if(i.appearance.reducedMotion==a.reducedMotion&&i.appearance.ambient==a.ambient)return false;i.appearance=a;i.appearanceDirty=true;return true;}
bool NativeMapScene::setDensity(double value){auto&i=*impl_;need(std::isfinite(value)&&value>0,"Invalid Map density");if(groupDensity(value)==groupDensity(i.boxDensity)){i.boxDensity=value;return false;}
    // The pin paths are rasterized again at the new density; the next upload
    // reconfigures the group and every marker group at it.
    i.boxDensity=value;++i.shapeRevision;i.shapes[0].reset();i.contentDirty=i.resourcesDirty=true;++i.counts.densityChanges;return true;}
double NativeMapScene::density()const noexcept{return groupDensity(impl_->boxDensity);}
bool NativeMapScene::syncContent(double t){auto&i=*impl_;i.clock(t);const bool pinsChanged=i.pins.size()!=i.state.pins().size()||!std::equal(i.pins.begin(),i.pins.end(),i.state.pins().begin());const bool selected=i.selection!=i.state.selection();const bool changedCamera=!i.ready||i.cameraRevision!=i.state.cameraRevision();const bool changedFrame=i.detail!=i.raster.detail()||i.backdrop!=i.raster.backdrop();
    const bool styleEvent=i.styleRevision!=i.state.styleRevision();i.styleRevision=i.state.styleRevision();
    if(pinsChanged){std::array<bool,128>visible{},pulsing{};std::array<double,128>starts{};for(std::size_t n=0;n<i.state.pins().size();++n){for(std::size_t k=0;k<i.pins.size();++k)if(i.state.pins()[n].id==i.pins[k].id){visible[n]=i.visible[k];pulsing[n]=i.pulsing[k];starts[n]=i.pulseStart[k];break;}}i.pins.assign(i.state.pins().begin(),i.state.pins().end());i.visible=visible;i.pulsing=pulsing;i.pulseStart=starts;}
    i.selection=i.state.selection();
    if(changedFrame){i.detail=i.raster.detail();i.backdrop=i.raster.backdrop();i.frame=i.raster.frame();i.contentDirty=i.resourcesDirty=true;}
    if(changedCamera||pinsChanged||changedFrame)i.targets(changedCamera&&i.state.cameraAnimated()&&!i.appearance.reducedMotion,t,pinsChanged);
    i.cameraRevision=i.state.cameraRevision();
    const bool pinVisibility=changedCamera||pinsChanged||selected||i.appearanceDirty||i.active!=i.state.active();if(pinVisibility){i.pinVisibility(t);i.active=i.state.active();i.localDirty=true;if(i.syncMarkers(t,styleEvent))i.contentDirty=i.resourcesDirty=true;}
    const bool rebuilt=!i.ready||i.appearanceDirty;if(!i.ready)i.contentDirty=true;i.appearanceDirty=false;i.ready=true;i.time=t;if(i.contentDirty||selected||pinsChanged){i.resourcesDirty=true;i.structure();}i.local(t);return rebuilt||changedCamera||changedFrame||pinsChanged||pinVisibility;
}
bool NativeMapScene::updatePose(const MapScenePose&p){auto&i=*impl_;i.clock(p.time);need(i.ready&&p.masks.size()<=8,"Map content/ancestor masks are invalid");i.staged.world=p.world;i.staged.opacity=p.opacity;i.staged.masks.assign(p.masks.begin(),p.masks.end());
    validateDrawObject(i.staged);const bool changed=i.output.world!=p.world||i.output.opacity!=p.opacity||!sameMasks(i.output.masks,i.staged.masks);
    i.output.world=i.staged.world;i.output.opacity=i.staged.opacity;i.output.masks.assign(i.staged.masks.begin(),i.staged.masks.end());i.time=p.time;i.local(p.time);++i.counts.poses;return changed;
}
bool NativeMapScene::uploadResources(Renderer&r){auto&i=*impl_;need(i.ready&&(!i.owner||i.owner==&r),"Map must be synchronized and use its original renderer");if(i.uploaded&&!i.resourcesDirty){i.applyHostCut(r);return false;}i.owner=&r;r.setMesh(i.quadID,1,{mapQuad,mapIndices});i.meshResident=true;auto texture=[&](const std::string&id,std::uint64_t revision,TextureData data){r.setTexture(id,revision,data);if(std::find(i.residentTextures.begin(),i.residentTextures.end(),id)==i.residentTextures.end())i.residentTextures.push_back(id);};if(!i.shapes[0])i.loadShapes();
    for(unsigned n=0;n<4;++n)texture(i.shapeIDs[n],i.shapeRevision,{i.shapes[n]->width,i.shapes[n]->height,i.shapes[n]->straightRGBA,TextureColorSpace::encodedSRGB});const std::array images{i.player.halo,i.player.beam,i.player.glyph};for(unsigned n=0;n<3;++n)texture(i.playerIDs[n],1,{images[n]->width,images[n]->height,images[n]->straightRGBA,TextureColorSpace::encodedSRGB});
    texture(i.rampID,1,{i.ramp->width,i.ramp->height,i.ramp->straightRGBA,TextureColorSpace::sRGB});texture(i.scrimID,1,{1,i.scrim.rows,i.scrim.straightRGBA,TextureColorSpace::encodedSRGB});
    if(i.detail&&i.uploadedDetail!=i.detail.get()){texture(i.prefix+"/detail",++i.imageRevision,{i.detail->width,i.detail->height,i.detail->straightRGBA,TextureColorSpace::encodedSRGB});i.uploadedDetail=i.detail.get();++i.counts.imageUploads;}
    if(i.backdrop&&i.uploadedBackdrop!=i.backdrop.get()){texture(i.prefix+"/backdrop",++i.imageRevision,{i.backdrop->width,i.backdrop->height,i.backdrop->straightRGBA,TextureColorSpace::encodedSRGB});i.uploadedBackdrop=i.backdrop.get();++i.counts.imageUploads;}
    i.carrier.uploadResources(r);const double density=i.unitDensity();
    for(auto&[id,m]:i.markers){(void)id;if(!m.wanted)continue;const R bounds=m.style==modules::MapPinStyle::player?R{-24,-106,48,132}:R{-24,-24,48,48};r.configureNativeGroup(m.groupID,{bounds,density,NativeGroupColorSpace::encodedSRGB},m.draws);m.output=r.nativeGroupOutput(m.groupID);m.configured=true;}
    i.structure();i.localDirty=true;i.local(i.time);const bool changed=r.configureNativeGroup(i.groupID,{mapSceneBounds,density,NativeGroupColorSpace::encodedSRGB},i.draws);const auto&out=r.nativeGroupOutput(i.groupID);
    i.counts.groupPixels=std::size_t(std::ceil(mapSceneBounds.width*density))*std::size_t(std::ceil(mapSceneBounds.height*density));
    i.output.sourceID=out.sourceID;i.output.meshID=out.meshID;i.output.textureID=out.textureID;i.staged.sourceID=out.sourceID;i.staged.meshID=out.meshID;i.staged.textureID=out.textureID;i.uploaded=true;i.resourcesDirty=false;i.applyHostCut(r);for(auto it=i.markers.begin();it!=i.markers.end();)if(!it->second.wanted&&(!it->second.configured||r.removeNativeGroup(it->second.groupID)))it=i.markers.erase(it);else ++it;return changed;
}
bool NativeMapScene::requiresFrames(double t)const{const auto&i=*impl_;i.clock(t);if(i.cameraStart&&t<*i.cameraStart+.18)return true;for(const auto&[id,m]:i.markers){(void)id;if(m.wanted&&m.styleStart&&t<*m.styleStart+.16)return true;}return false;}
bool NativeMapScene::pulsing()const noexcept{const auto&i=*impl_;return i.state.active()&&!i.appearance.reducedMotion&&i.appearance.ambient&&i.counts.pulsingPins>0;}
bool NativeMapScene::setHostCut(const modules::MapCutouts&c,bool wanted){auto&i=*impl_;need(c.count<=modules::MapCutouts::capacity,"Invalid Map host cut");const auto origin=mapHostCutBounds();
    modules::MapCutouts local;for(std::size_t k=0;k<c.count;++k){need(c.polygons[k].count<=modules::MapCutoutPolygon::capacity,"Invalid Map host cut");auto&polygon=local.polygons[local.count++];polygon.count=0;
        for(const auto p:c.polygons[k].view()){need(std::isfinite(p.x)&&std::isfinite(p.y),"Invalid Map host cut point");polygon.points[polygon.count++]={p.x-origin.x,p.y-origin.y};}}
    const bool changed=i.hostCutWanted!=wanted||!(local==i.hostCut);i.hostCutWanted=wanted;if(wanted)i.hostCut=local;if(changed)i.hostCutCurrent=false;return changed;}
const std::string*NativeMapScene::hostCutTexture()const noexcept{const auto&i=*impl_;return i.hostCutWanted&&i.hostCutResident&&i.hostCutCurrent?&i.hostCutID:nullptr;}
bool NativeMapScene::releaseHostCut(Renderer&r){auto&i=*impl_;i.hostCutWanted=i.hostCutCurrent=false;if(!i.hostCutResident)return true;need(!i.owner||i.owner==&r,"Map resource owner mismatch");
    if(!r.removeTexture(i.hostCutID))return false;i.hostCutResident=false;i.appliedHostCut.count=0;i.counts.hostCutBytes=0;return true;}
void NativeMapScene::settle(){auto&i=*impl_;i.cameraStart.reset();for(auto&[id,m]:i.markers){(void)id;m.styleStart.reset();}i.localDirty=true;}
std::span<const LayerCompositionEntry>NativeMapScene::entries()const noexcept{return impl_->uploaded?std::span<const LayerCompositionEntry>(impl_->entries):std::span<const LayerCompositionEntry>{};}
bool NativeMapScene::releaseResources(Renderer&r){auto&i=*impl_;need(!i.owner||i.owner==&r,"Map resource owner mismatch");if(i.uploaded){if(!r.removeNativeGroup(i.groupID))return false;i.uploaded=false;}for(auto&[id,m]:i.markers){(void)id;if(m.configured){if(!r.removeNativeGroup(m.groupID))return false;m.configured=false;}}if(!i.carrier.releaseResources(r))return false;if(i.hostCutResident){if(!r.removeTexture(i.hostCutID))return false;i.hostCutResident=i.hostCutCurrent=false;i.appliedHostCut.count=0;i.counts.hostCutBytes=0;}for(auto it=i.residentTextures.begin();it!=i.residentTextures.end();)if(r.removeTexture(*it))it=i.residentTextures.erase(it);else ++it;if(i.meshResident&&r.removeMesh(i.quadID))i.meshResident=false;if(i.residentTextures.empty()&&!i.meshResident){i.owner=nullptr;i.resourcesDirty=true;i.uploadedDetail=i.uploadedBackdrop=nullptr;return true;}return false;}
MapSceneStats NativeMapScene::stats()const noexcept{return impl_->counts;}
}
#endif
