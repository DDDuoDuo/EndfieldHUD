#include "native/map_scene.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
using J=ehud::data::Json;using R=core::Rect;using C=modules::MapColor;
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
J array(R r){return J::Array{r.x,r.y,r.width,r.height};}
J color(C c){return J::Object{{"sRGB",J::Array{c[0],c[1],c[2],c[3]}}};}
J base(std::string id,R r,const char*kind="layer") {return J::Object{{"id",std::move(id)},{"class",std::string(kind)=="text"?"CATextLayer":std::string(kind)=="shape"?"CAShapeLayer":"CALayer"},{"kind",kind},{"bounds",array({0,0,r.width,r.height})},{"position",J::Array{r.x,r.y}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
J text(std::string id,R r,const std::string&value,double size,C c){auto j=base(std::move(id),r,"text");j["text"]=J::Object{{"string",value},{"fontSize",size},{"font",J::Object{{"familyName",".AppleSystemUIFont"},{"fontName",".AppleSystemUIFontDemi"},{"pointSize",size},{"symbolicTraits",2}}},{"foregroundColor",color(c)},{"alignment","center"},{"wrapped",true},{"truncation","none"}};return j;}
J command(const char*kind,core::Point p){return J::Object{{"op",kind},{"points",J::Array{J(J::Array{p.x,p.y})}}};}
J cutPath(R r){const auto c=std::min(4.,std::min(r.width,r.height)/3);J::Array out;out.push_back(command("move",{r.x+c,r.y}));for(auto p:std::array<core::Point,5>{{{r.x+r.width,r.y},{r.x+r.width,r.y+r.height-c},{r.x+r.width-c,r.y+r.height},{r.x,r.y+r.height},{r.x,r.y+c}}})out.push_back(command("line",p));out.push_back(J::Object{{"op","close"},{"points",J::Array{}}});return out;}
std::string actionName(modules::MapAction a){switch(a){case modules::MapAction::zoomIn:return "zoomIn";case modules::MapAction::zoomOut:return "zoomOut";case modules::MapAction::reset:return "reset";case modules::MapAction::addPin:return "addPin";case modules::MapAction::deletePin:return "deletePin";default:throw std::invalid_argument("Pin is not a toolbar control");}}
void validate(const MapAppearance&a){for(auto value:a.accent)need(std::isfinite(value)&&value>=0&&value<=1,"Invalid Map appearance color");for(const auto*p:{&a.strings.heading,&a.strings.loading,&a.strings.pins})need(p->size()<=4096&&J::validUtf8(*p),"Invalid Map caption");}
}
std::vector<MapChromeSurface>prepareMapChrome(const modules::MapState&state,const MapAppearance&a){
    validate(a);std::vector<MapChromeSurface>result;result.reserve(26);const auto style=modules::mapChromeStyle(a.dark);const modules::MapChromeGeometry g;
    auto add=[&](std::string id,J value,R r,MapChromeRole role=MapChromeRole::artwork,std::optional<modules::MapAction>action={}){result.push_back({std::move(id),std::move(value),core::Matrix4::translation(r.x,r.y),role,action});};
    add("map.heading",text("map.heading",g.heading,a.strings.heading,11,style.ink),g.heading);
    auto muted=style.ink;muted[3]=.55;
    add("map.status",text("map.status",g.status,modules::mapZoomStatus(state.viewport(),state.pins().size(),a.terrainReady,a.loadsTerrain,a.strings),8,muted),g.status);
    const auto actions=state.actions();for(std::size_t n=0;n<actions.count;++n){const auto&action=actions.items[n];if(action.action==modules::MapAction::pin)continue;
        const auto id="map.control."+actionName(action.action);const auto r=action.rect;
        auto plate=base(id+".plate",r);plate["cornerRadius"]=2;plate["backgroundColor"]=color(style.plate);add(id+".plate",std::move(plate),r);
        auto tint=base(id+".tint",r,"shape");auto c=a.accent;c[3]=.3;tint["shape"]=J::Object{{"path",cutPath({0,0,r.width,r.height})},{"fillColor",color(c)},{"strokeColor",J{}}};add(id+".tint",std::move(tint),r,MapChromeRole::tint,action.action);
        auto rim=base(id+".rim",r,"shape");rim["shape"]=J::Object{{"path",cutPath({-2,-2,r.width+4,r.height+4})},{"fillColor",J{}},{"strokeColor",color(a.accent)},{"lineWidth",.9},{"lineCap","butt"},{"lineJoin","miter"}};add(id+".rim",std::move(rim),r,MapChromeRole::rim,action.action);
        const R symbol{r.x+1,r.y+2,r.width-2,r.height-4};c=style.ink;c[3]=action.enabled?.9:.3;add(id+".symbol",text(id+".symbol",symbol,std::string(modules::mapActionSymbol(action.action)),15,c),symbol);
    }
    if(state.showsCoordinates()&&state.selection()){const auto found=std::find_if(state.pins().begin(),state.pins().end(),[&](const auto&p){return p.id==*state.selection();});if(found!=state.pins().end()){
        auto plate=base("map.coordinates.plate",g.coordinates);plate["backgroundColor"]=color(style.coordinateBackground);add("map.coordinates.plate",std::move(plate),g.coordinates);
        add("map.coordinates.text",text("map.coordinates.text",g.coordinateText,modules::mapCoordinateDescription(found->x,found->y),9,a.accent),g.coordinateText);
    }}
    if(state.error()){muted=style.ink;muted[3]=.53;add("map.error",text("map.error",g.message,*state.error(),7,muted),g.message);}
    return result;
}
}

#ifdef _WIN32
#include "native/layer_scene.hpp"
#include "core/source_camera.hpp"
#include "core/motion.hpp"
#include <atomic>
#include <map>
namespace endfield::native {
namespace {
using M=core::Matrix4;using P=core::Point;
constexpr std::array<std::uint32_t,6>mapIndices{0,1,2,0,2,3};
constexpr std::array<Vertex,4>mapQuad{{{{0,0,0},{0,0}},{{1,0,0},{1,0}},{{1,1,0},{1,1}},{{0,1,0},{0,1}}}};
std::array<float,4>linear(C c){std::array<float,4>out{};for(unsigned n=0;n<3;++n)out[n]=float(c[n]<=.04045?c[n]/12.92:std::pow((c[n]+.055)/1.055,2.4));out[3]=float(c[3]);return out;}
bool sameAppearance(const MapAppearance&a,const MapAppearance&b){return a.dark==b.dark&&a.reducedMotion==b.reducedMotion&&a.ambient==b.ambient&&a.terrainReady==b.terrainReady&&a.loadsTerrain==b.loadsTerrain&&a.accent==b.accent&&a.strings.heading==b.strings.heading&&a.strings.loading==b.strings.loading&&a.strings.pins==b.strings.pins;}
bool imageValid(const std::shared_ptr<const MapPaintImage>&p){return p&&p->width&&p->height&&p->width<=256&&p->height<=256&&p->straightRGBA.size()==std::size_t(p->width)*p->height*4;}
J circle(std::string id,double radius,double width,bool fill){const double k=.5522847498307936,r=radius;const auto cmd=[](const char*op,std::initializer_list<P>ps){J::Array pts;for(auto p:ps)pts.push_back(J::Array{p.x,p.y});return J(J::Object{{"op",op},{"points",std::move(pts)}});};auto j=base(std::move(id),{0,0,0,0},"shape");j["shape"]=J::Object{{"path",J::Array{cmd("move",{{r,0}}),cmd("cubic",{{r,r*k},{r*k,r},{0,r}}),cmd("cubic",{{-r*k,r},{-r,r*k},{-r,0}}),cmd("cubic",{{-r,-r*k},{-r*k,-r},{0,-r}}),cmd("cubic",{{r*k,-r},{r,-r*k},{r,0}}),cmd("close",{})}},{"fillColor",fill?color({1,1,1,1}):J{}},{"strokeColor",fill?J{}:color({1,1,1,1})},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"}};return j;}
struct Track {double from{},to{},start{},duration{};double sample(double t)const{if(!duration||t>=start+duration)return to;const double p=core::CubicTiming{0,0,.58,1}.value(std::clamp((t-start)/duration,0.,1.));return from+(to-from)*p;}};
struct Motion {P from{},to{};double fromScale{1},toScale{1};modules::MapImagePlacement sample(double p)const{return {{from.x+(to.x-from.x)*p,from.y+(to.y-from.y)*p},fromScale+(toScale-fromScale)*p};}};
bool sameMasks(std::span<const PlaneMask>a,std::span<const PlaneMask>b){if(a.size()!=b.size())return false;for(std::size_t n=0;n<a.size();++n)if(a[n].worldToLocal!=b[n].worldToLocal||a[n].bounds!=b[n].bounds||a[n].cornerRadius!=b[n].cornerRadius)return false;return true;}
bool controlContains(R r,P p){if(!r.contains(p))return false;const double c=std::min(4.,std::min(r.width,r.height)/3);return p.x-r.x+p.y-r.y>=c&&(r.x+r.width-p.x)+(r.y+r.height-p.y)>=c;}
}
struct NativeMapScene::Impl {
    struct Marker {std::string groupID;std::array<DrawObject,5>draws;DrawObject output;modules::MapPinStyle style{};std::optional<double>styleStart;bool wanted{},wasWanted{},configured{};};
    std::map<std::string,Marker,std::less<>>markers;
    modules::MapState&state;NativeMapRaster&raster;LayerRasterizer&paint;LayerRasterOptions options;MapPlayerImages player;MapAppearance appearance;
    LayerScene chrome,carrier;std::string prefix,groupID,quadID,maskID;std::array<std::string,3>playerIDs;std::array<std::string,4>shapeIDs;
    std::array<std::shared_ptr<const LayerRasterImage>,4>shapes;std::array<R,4>shapeBounds;
    std::vector<MapChromeSurface>content;std::vector<LayerPlacement>chromePlacements;std::vector<DrawObject>draws;
    std::array<Track,10>feedback;std::optional<modules::MapAction>hover;bool pressed{};
    std::vector<modules::MapPin>pins;std::array<Motion,128>pinMotion;std::array<bool,128>visible{},pulsing{};std::array<double,128>pulseStart{};std::array<std::size_t,128>paintOrder{};
    std::array<Motion,4>imageMotion;std::optional<double>cameraStart;modules::MapViewport camera;std::optional<modules::MapRasterFrame>frame;
    std::shared_ptr<const MapPaintImage>detail,backdrop;const MapPaintImage*uploadedDetail{},*uploadedBackdrop{};
    std::optional<std::string>selection,error;bool coordinates{},zoomIn{},zoomOut{},editable{},appearanceDirty{true},contentDirty{},resourcesDirty{true},localDirty{true},ready{},uploaded{},active{},meshResident{};
    std::uint64_t shapeRevision{1},imageRevision{},statusRevision{1};double lastZoom{},time{},lastLocalTime{-1};
    Renderer*owner{};std::vector<std::string>residentTextures;DrawObject output,staged;std::array<LayerCompositionEntry,1>entries;MapSceneStats counts;
    Impl(modules::MapState&s,NativeMapRaster&r,LayerRasterizer&p,LayerRasterOptions o,MapPlayerImages images,MapAppearance a):state(s),raster(r),paint(p),options(std::move(o)),player(std::move(images)),appearance(std::move(a)),chrome(p),carrier(p){
        validate(appearance);need(imageValid(player.halo)&&imageValid(player.beam)&&imageValid(player.glyph),"Map needs all three bounded original player images");need(player.feather&&player.feather->width==880&&player.feather->height==880&&player.feather->straightRGBA.size()==880*880*4,"Map needs its original bounded CA feather mask");need(std::isfinite(options.pixelsPerPoint)&&options.pixelsPerPoint>0&&options.pixelsPerPoint<=8,"Invalid Map image scale");const auto physicalSide=std::ceil(440*options.pixelsPerPoint);need(physicalSide*physicalSide<=Renderer::maximumNativeGroupPixels,"Map target exceeds retained group pixel budget");
        static std::atomic<std::uint64_t>serial{};prefix="map.scene:"+std::to_string(serial.fetch_add(1,std::memory_order_relaxed));groupID=prefix+"/group";quadID=prefix+"/quad";maskID=prefix+"/feather";
        for(unsigned n=0;n<3;++n)playerIDs[n]=prefix+"/player"+std::to_string(n);for(unsigned n=0;n<4;++n)shapeIDs[n]=prefix+"/shape"+std::to_string(n);
        output.sourceID=prefix+"/output";staged.sourceID=output.sourceID;output.masks.reserve(8);staged.masks.reserve(8);output.alphaMask=PlaneAlphaMask{M{},{0,0,440,440},maskID};staged.alphaMask=output.alphaMask;carrier.load(base(prefix+"/carrier",{0,0,0,0}),options);entries[0]={&carrier,std::span(&output,1)};pins.reserve(128);residentTextures.reserve(12);draws.reserve(5+128*5+27);
    }
    void clock(double t)const{need(std::isfinite(t)&&t>=time,"Map scene requires a finite monotonic owner clock");}
    double progress(double t)const{return cameraStart?core::CubicTiming{.42,0,.58,1}.value(std::clamp((t-*cameraStart)/.18,0.,1.)):1;}
    void rebuildChrome(){content=prepareMapChrome(state,appearance);J::Array leaves;for(auto&part:content){part.content["position"]=J::Array{part.local.values[12],part.local.values[13]};leaves.push_back(part.content);}
        // The wrapper carries placement only. An identified positive-size 2D root is
        // intentionally flattened by LayerScene, which would bake feedback into captions.
        auto root=base("",{0,0,440,440});root["children"]=std::move(leaves);chrome.load(root,options);statusRevision=chrome.contentRevision();need(chrome.report().unsupported.empty(),"Unsupported source Map chrome");chromePlacements.resize(content.size());need(chrome.draws().size()==content.size(),"Map source leaves must remain independent");
        for(std::size_t n=0;n<content.size();++n){need(chrome.surfaceIndex(content[n].id)==n,"Map source leaf identity/order changed");chromePlacements[n]={n,content[n].local,1,{}};}
        for(unsigned n=0;n<5;++n){const auto a=static_cast<modules::MapAction>(n);bool enabled=false;const auto actions=state.actions();for(std::size_t k=0;k<actions.count;++k)if(actions.items[k].action==a){enabled=actions.items[k].enabled;break;}const bool h=enabled&&hover&&*hover==a;const double tint=h?(pressed?1:.62):0,rim=h?1:(enabled?.28:0);feedback[n*2]={tint,tint,0,0};feedback[n*2+1]={rim,rim,0,0};}
        ++counts.chromeBuilds;contentDirty=resourcesDirty=true;
    }
    void loadShapes(){const std::array<double,4>radius{5.3,8,4,1.8},width{0,.55,1.2,0};for(unsigned n=0;n<4;++n){shapes[n]=paint.rasterize(shapeIDs[n],shapeRevision,circle(shapeIDs[n],radius[n],width[n],n==0||n==3),options);need(shapes[n]->complete(),"Unsupported original Map pin path");shapeBounds[n]=shapes[n]->bounds;}}
    void targets(bool animated,double t,bool pinStructure){const double old=progress(t);const auto oldCamera=camera;camera=state.viewport();auto placements=modules::mapBackdropPlacements(camera);for(unsigned n=0;n<4;++n){auto target=n<3?placements[n]:(frame?modules::mapRasterPlacement(*frame,camera):modules::MapImagePlacement{});auto prior=imageMotion[n].sample(old);imageMotion[n]={animated&&ready?prior.position:target.position,target.position,animated&&ready?prior.scale:target.scale,target.scale};}
        for(std::size_t n=0;n<pins.size();++n){const auto target=modules::mapScreen(pins[n].x,pins[n].y,camera);const auto prior=pinMotion[n].sample(old);const bool keep=animated&&ready&&!pinStructure&&visible[n];pinMotion[n]={keep?prior.position:target,target,1,1};}
        cameraStart=animated&&ready&&oldCamera!=camera?std::optional(t):std::nullopt;localDirty=true;
    }
    void pinVisibility(double t){const auto selected=state.selection()?std::optional<std::string_view>(*state.selection()):std::nullopt;const auto visuals=modules::mapPinVisuals(pins,camera,selected,state.active(),appearance.reducedMotion,appearance.ambient);counts.visiblePins=visuals.count;counts.pulsingPins=0;
        for(std::size_t n=0;n<pins.size();++n){const modules::MapPinVisual*value=nullptr;for(std::size_t k=0;k<visuals.count;++k)if(visuals.items[k].id==pins[n].id){value=&visuals.items[k];break;}const bool next=value&&value->pulses;if(next&&!pulsing[n])pulseStart[n]=t;pulsing[n]=next;visible[n]=value!=nullptr;counts.pulsingPins+=next?1:0;paintOrder[n]=n;}
        if(state.selection()){const auto found=std::find_if(paintOrder.begin(),paintOrder.begin()+pins.size(),[&](auto n){return pins[n].id==*state.selection();});if(found!=paintOrder.begin()+pins.size())std::rotate(found,found+1,paintOrder.begin()+pins.size());}
    }
    bool syncMarkers(double t){
        bool changed{};for(auto&[id,m]:markers){(void)id;m.wasWanted=m.wanted;m.wanted=false;}
        for(std::size_t n=0;n<pins.size();++n){if(!visible[n])continue;auto found=markers.find(pins[n].id);const bool fresh=found==markers.end();
            if(fresh){found=markers.try_emplace(pins[n].id).first;auto&m=found->second;m.groupID=prefix+"/marker/"+pins[n].id;for(unsigned part=0;part<5;++part){m.draws[part].sourceID=m.groupID+"/"+std::to_string(part);m.draws[part].meshID=quadID;}changed=true;}
            auto&m=found->second;m.wanted=true;const bool styleChanged=!fresh&&m.style!=pins[n].style;
            if(fresh||styleChanged){m.style=pins[n].style;m.styleStart=styleChanged&&m.wasWanted&&state.active()&&!appearance.reducedMotion?std::optional(t):std::nullopt;
                for(unsigned part=0;part<5;++part){const bool playerPin=m.style==modules::MapPinStyle::player;auto&d=m.draws[part];d.textureID=shapeIDs[part==0?0:part==1?1:part==2?2:3];d.blend=NativeBlend::sourceOver;if(playerPin){if(part==0)d.textureID=playerIDs[0];else if(part==2){d.textureID=playerIDs[1];d.blend=NativeBlend::screen;}else if(part==3)d.textureID=playerIDs[2];}}changed=true;}
            if(appearance.reducedMotion||!state.active())m.styleStart.reset();
        }
        for(const auto&[id,m]:markers){(void)id;changed|=m.wanted!=m.wasWanted;}
        return changed;
    }
    void structure(){draws.clear();auto add=[&](std::string id,std::string texture={}){DrawObject d;d.sourceID=std::move(id);d.meshID=quadID;d.textureID=std::move(texture);draws.push_back(std::move(d));};add(prefix+"/background");for(unsigned n=0;n<4;++n)add(prefix+"/image"+std::to_string(n),n<3?(backdrop?prefix+"/backdrop":""):(detail?prefix+"/detail":""));
        for(std::size_t order=0;order<pins.size();++order){const auto n=paintOrder[order];const auto found=markers.find(pins[n].id);if(found!=markers.end()&&found->second.wanted&&found->second.configured)draws.push_back(found->second.output);else{add(prefix+"/pin/"+pins[n].id);draws.back().opacity=0;}}
        const auto chromeDraws=chrome.prepareDraws();draws.insert(draws.end(),chromeDraws.begin(),chromeDraws.end());contentDirty=false;localDirty=true;++counts.pinBuilds;
    }
    void local(double t){if(!ready||(!localDirty&&lastLocalTime==t))return;const bool animate=cameraStart.has_value();bool feedbackActive=false;for(const auto&f:feedback)feedbackActive|=f.duration>0;bool styleActive=false;for(const auto&[id,m]:markers){(void)id;styleActive|=m.wanted&&m.styleStart.has_value();}if(!localDirty&&!animate&&!counts.pulsingPins&&!feedbackActive&&!styleActive)return;
        const auto p=progress(t);auto&bg=draws[0];bg.world=M::scale(440,440);bg.linearTint=linear(modules::mapChromeStyle(appearance.dark).background);
        for(unsigned n=0;n<4;++n){const auto pos=imageMotion[n].sample(p);auto&d=draws[n+1];const bool show=n<3?bool(backdrop):bool(detail&&frame);const double w=n<3?1024:frame?frame->screenRect.width:1,h=n<3?512:frame?frame->screenRect.height:1;d.world=M::translation(pos.position.x,pos.position.y)*M::scale(w*pos.scale,h*pos.scale);d.opacity=show?1:0;}
        const auto assets=modules::mapPlayerAssets();for(std::size_t order=0;order<pins.size();++order){const auto n=paintOrder[order];auto&parent=draws[5+order];const auto found=markers.find(pins[n].id);if(found==markers.end()||!found->second.wanted){parent.opacity=0;continue;}auto&m=found->second;const auto position=pinMotion[n].sample(p).position;const auto col=linear(modules::mapPinColor(pins[n].style));const bool playerPin=pins[n].style==modules::MapPinStyle::player;const bool selected=selection&&pins[n].id==*selection;
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
        for(std::size_t n=0;n<content.size();++n){auto&placement=chromePlacements[n];if(content[n].action){const auto action=unsigned(*content[n].action);placement.opacity=float(feedback[action*2+(content[n].role==MapChromeRole::rim?1:0)].sample(t));}else placement.opacity=1;}
        chrome.setPlacements(chromePlacements);const auto leaves=chrome.prepareDraws();const auto start=5+pins.size();for(std::size_t n=0;n<leaves.size();++n){draws[start+n].world=leaves[n].world;draws[start+n].opacity=leaves[n].opacity;}
        if(cameraStart&&t>=*cameraStart+.18)cameraStart.reset();for(auto&f:feedback)if(f.duration&&t>=f.start+f.duration)f={f.to,f.to,0,0};lastLocalTime=t;localDirty=false;++counts.localUpdates;if(owner&&uploaded&&!resourcesDirty)owner->setNativeGroupDraws(groupID,draws);
    }
};
NativeMapScene::NativeMapScene(modules::MapState&s,NativeMapRaster&r,LayerRasterizer&p,LayerRasterOptions o,MapPlayerImages images,MapAppearance a):impl_(std::make_unique<Impl>(s,r,p,std::move(o),std::move(images),std::move(a))){}
NativeMapScene::~NativeMapScene(){auto&i=*impl_;if(i.owner)try{releaseResources(*i.owner);}catch(...){}for(const auto&id:i.shapeIDs)i.paint.remove(id);}
bool NativeMapScene::setAppearance(MapAppearance a){validate(a);auto&i=*impl_;if(sameAppearance(i.appearance,a))return false;i.appearance=std::move(a);i.appearanceDirty=true;return true;}
bool NativeMapScene::syncContent(double t,bool cameraAnimated){auto&i=*impl_;i.clock(t);const bool pinsChanged=i.pins.size()!=i.state.pins().size()||!std::equal(i.pins.begin(),i.pins.end(),i.state.pins().begin());const bool selected=i.selection!=i.state.selection();const bool changedCamera=!i.ready||i.camera!=i.state.viewport();const bool changedFrame=i.detail!=i.raster.detail()||i.backdrop!=i.raster.backdrop();
    const bool rebuild=!i.ready||i.appearanceDirty||pinsChanged||selected||i.coordinates!=i.state.showsCoordinates()||i.error!=i.state.error()||i.zoomIn!=(i.state.viewport().zoom<modules::MapViewport::maximumZoom)||i.zoomOut!=(i.state.viewport().zoom>modules::MapViewport::minimumZoom)||i.editable!=i.state.editable();
    if(pinsChanged){std::array<bool,128>visible{},pulsing{};std::array<double,128>starts{};for(std::size_t n=0;n<i.state.pins().size();++n){for(std::size_t k=0;k<i.pins.size();++k)if(i.state.pins()[n].id==i.pins[k].id){visible[n]=i.visible[k];pulsing[n]=i.pulsing[k];starts[n]=i.pulseStart[k];break;}}i.pins.assign(i.state.pins().begin(),i.state.pins().end());i.visible=visible;i.pulsing=pulsing;i.pulseStart=starts;}
    i.selection=i.state.selection();i.coordinates=i.state.showsCoordinates();i.error=i.state.error();i.zoomIn=i.state.viewport().zoom<modules::MapViewport::maximumZoom;i.zoomOut=i.state.viewport().zoom>modules::MapViewport::minimumZoom;i.editable=i.state.editable();
    if(changedFrame){i.detail=i.raster.detail();i.backdrop=i.raster.backdrop();i.frame=i.raster.frame();i.contentDirty=i.resourcesDirty=true;}
    if(rebuild)i.rebuildChrome();else if(i.lastZoom!=i.state.viewport().zoom){const auto style=modules::mapChromeStyle(i.appearance.dark);auto c=style.ink;c[3]=.55;const modules::MapChromeGeometry g;i.chrome.updateLocalContent("map.status",++i.statusRevision,text("map.status",g.status,modules::mapZoomStatus(i.state.viewport(),i.state.pins().size(),i.appearance.terrainReady,i.appearance.loadsTerrain,i.appearance.strings),8,c),i.options);i.contentDirty=i.resourcesDirty=true;++i.counts.statusRasters;}
    if(i.chrome.refreshTypography())i.contentDirty=i.resourcesDirty=true;
    if(changedCamera||pinsChanged||changedFrame)i.targets(cameraAnimated&&!i.appearance.reducedMotion,t,pinsChanged);
    const bool pinVisibility=changedCamera||pinsChanged||selected||i.appearanceDirty||i.active!=i.state.active();if(pinVisibility){i.pinVisibility(t);i.active=i.state.active();i.localDirty=true;if(i.syncMarkers(t))i.contentDirty=i.resourcesDirty=true;}
    i.lastZoom=i.state.viewport().zoom;i.appearanceDirty=false;i.ready=true;i.time=t;if(i.contentDirty||selected||pinsChanged){i.resourcesDirty=true;i.structure();}i.localDirty|=rebuild;i.local(t);return rebuild||changedCamera||changedFrame||pinsChanged||pinVisibility;
}
bool NativeMapScene::setFeedback(std::optional<P>point,bool pressed,double t){auto&i=*impl_;i.clock(t);if(point)need(std::isfinite(point->x)&&std::isfinite(point->y),"Invalid Map pointer");std::optional<modules::MapAction>hit;const auto actions=i.state.actions();if(point)for(std::size_t n=0;n<actions.count;++n){const auto&a=actions.items[n];if(a.action!=modules::MapAction::pin&&a.enabled&&controlContains(a.rect,*point))hit=a.action;}if(i.hover==hit&&i.pressed==pressed)return false;i.hover=hit;i.pressed=pressed;
    for(unsigned n=0;n<5;++n){bool enabled=false;for(std::size_t k=0;k<actions.count;++k)if(unsigned(actions.items[k].action)==n)enabled=actions.items[k].enabled;const bool hover=hit&&unsigned(*hit)==n;for(unsigned j=0;j<2;++j){auto&v=i.feedback[n*2+j];const double target=j?(hover?1:(enabled?.28:0)):(hover?(pressed?1:.62):0);const auto current=v.sample(t);v={current,target,t,i.appearance.reducedMotion||current==target?0:pressed&&hover?.06:.14};}}
    i.time=t;i.localDirty=true;return true;
}
bool NativeMapScene::updatePose(const MapScenePose&p){auto&i=*impl_;i.clock(p.time);need(i.ready&&p.masks.size()<=8,"Map content/ancestor masks are invalid");const auto inverse=core::source::inverseSourceMatrix(p.world);i.staged.world=p.world;i.staged.opacity=p.opacity;i.staged.masks.assign(p.masks.begin(),p.masks.end());i.staged.shutter=p.shutter?std::optional(*p.shutter):std::nullopt;i.staged.alphaMask->worldToLocal=inverse;
    validateDrawObject(i.staged);const bool changed=i.output.world!=p.world||i.output.opacity!=p.opacity||!sameMasks(i.output.masks,i.staged.masks)||i.output.shutter!=i.staged.shutter||i.output.alphaMask!=i.staged.alphaMask;
    i.output.world=i.staged.world;i.output.opacity=i.staged.opacity;i.output.masks.assign(i.staged.masks.begin(),i.staged.masks.end());i.output.shutter=i.staged.shutter;i.output.alphaMask=i.staged.alphaMask;i.time=p.time;i.local(p.time);++i.counts.poses;return changed;
}
bool NativeMapScene::uploadResources(Renderer&r){auto&i=*impl_;need(i.ready&&(!i.owner||i.owner==&r),"Map must be synchronized and use its original renderer");if(i.uploaded&&!i.resourcesDirty)return false;i.owner=&r;r.setMesh(i.quadID,1,{mapQuad,mapIndices});i.meshResident=true;auto texture=[&](const std::string&id,std::uint64_t revision,TextureData data){r.setTexture(id,revision,data);if(std::find(i.residentTextures.begin(),i.residentTextures.end(),id)==i.residentTextures.end())i.residentTextures.push_back(id);};if(!i.shapes[0])i.loadShapes();
    for(unsigned n=0;n<4;++n)texture(i.shapeIDs[n],i.shapeRevision,{i.shapes[n]->width,i.shapes[n]->height,i.shapes[n]->straightRGBA,TextureColorSpace::encodedSRGB});const std::array images{i.player.halo,i.player.beam,i.player.glyph};for(unsigned n=0;n<3;++n)texture(i.playerIDs[n],1,{images[n]->width,images[n]->height,images[n]->straightRGBA,TextureColorSpace::encodedSRGB});
    if(i.detail&&i.uploadedDetail!=i.detail.get()){texture(i.prefix+"/detail",++i.imageRevision,{i.detail->width,i.detail->height,i.detail->straightRGBA,TextureColorSpace::encodedSRGB});i.uploadedDetail=i.detail.get();++i.counts.imageUploads;}
    if(i.backdrop&&i.uploadedBackdrop!=i.backdrop.get()){texture(i.prefix+"/backdrop",++i.imageRevision,{i.backdrop->width,i.backdrop->height,i.backdrop->straightRGBA,TextureColorSpace::encodedSRGB});i.uploadedBackdrop=i.backdrop.get();++i.counts.imageUploads;}
    if(!i.uploaded)texture(i.maskID,1,{i.player.feather->width,i.player.feather->height,i.player.feather->straightRGBA});
    i.chrome.uploadResources(r);i.carrier.uploadResources(r);i.structure();i.localDirty=true;i.local(i.time);
    for(auto&[id,m]:i.markers){(void)id;if(!m.wanted)continue;const R bounds=m.style==modules::MapPinStyle::player?R{-24,-106,48,132}:R{-24,-24,48,48};r.configureNativeGroup(m.groupID,{bounds,i.options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB},m.draws);m.output=r.nativeGroupOutput(m.groupID);m.configured=true;}
    i.structure();i.localDirty=true;i.local(i.time);const bool changed=r.configureNativeGroup(i.groupID,{{0,0,440,440},i.options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB},i.draws);const auto&out=r.nativeGroupOutput(i.groupID);
    i.output.sourceID=out.sourceID;i.output.meshID=out.meshID;i.output.textureID=out.textureID;i.staged.sourceID=out.sourceID;i.staged.meshID=out.meshID;i.staged.textureID=out.textureID;i.output.alphaMask->worldToLocal=core::source::inverseSourceMatrix(i.output.world);i.uploaded=true;i.resourcesDirty=false;i.chrome.collectRetiredResources(r);for(auto it=i.markers.begin();it!=i.markers.end();)if(!it->second.wanted&&(!it->second.configured||r.removeNativeGroup(it->second.groupID)))it=i.markers.erase(it);else ++it;return changed;
}
bool NativeMapScene::requiresFrames(double t)const{const auto&i=*impl_;i.clock(t);if(i.cameraStart&&t<*i.cameraStart+.18)return true;if(i.state.active()&&!i.appearance.reducedMotion&&i.appearance.ambient&&i.counts.pulsingPins)return true;for(const auto&v:i.feedback)if(v.duration&&t<v.start+v.duration)return true;for(const auto&[id,m]:i.markers){(void)id;if(m.wanted&&m.styleStart&&t<*m.styleStart+.16)return true;}return false;}
void NativeMapScene::settle(){auto&i=*impl_;i.cameraStart.reset();for(auto&[id,m]:i.markers){(void)id;m.styleStart.reset();}for(auto&v:i.feedback)v={v.to,v.to,0,0};i.localDirty=true;}
std::span<const LayerCompositionEntry>NativeMapScene::entries()const noexcept{return impl_->uploaded?std::span<const LayerCompositionEntry>(impl_->entries):std::span<const LayerCompositionEntry>{};}
bool NativeMapScene::releaseResources(Renderer&r){auto&i=*impl_;need(!i.owner||i.owner==&r,"Map resource owner mismatch");if(i.uploaded){if(!r.removeNativeGroup(i.groupID))return false;i.uploaded=false;}for(auto&[id,m]:i.markers){(void)id;if(m.configured){if(!r.removeNativeGroup(m.groupID))return false;m.configured=false;}}if(!i.chrome.releaseResources(r)||!i.carrier.releaseResources(r))return false;for(auto it=i.residentTextures.begin();it!=i.residentTextures.end();)if(r.removeTexture(*it))it=i.residentTextures.erase(it);else ++it;if(i.meshResident&&r.removeMesh(i.quadID))i.meshResident=false;if(i.residentTextures.empty()&&!i.meshResident){i.owner=nullptr;i.resourcesDirty=true;i.uploadedDetail=i.uploadedBackdrop=nullptr;return true;}return false;}
MapSceneStats NativeMapScene::stats()const noexcept{return impl_->counts;}
}
#endif
