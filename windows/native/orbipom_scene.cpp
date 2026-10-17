#include "native/orbipom_scene.hpp"
#ifdef _WIN32
#include "native/orbipom_assets.hpp"
#include "native/layer_group.hpp"
#include "core/source_camera.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <set>
namespace endfield::native {
namespace {
namespace m=modules;using J=ehud::data::Json;using M=core::Matrix4;using R=core::Rect;using P=core::Point;
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
J root(const std::vector<m::OrbiPomSurface>&parts){J::Array c;for(const auto&p:parts)c.push_back(p.content);return J::Object{{"bounds",J::Array{0,0,440,440}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",std::move(c)}};}
struct Track {double from{},target{},start{},duration{};core::CubicTiming timing{0,0,.58,1};double value(double t)const{return duration>0&&t<start+duration?from+(target-from)*timing.value(std::clamp((t-start)/duration,0.,1.)):target;}bool moving(double t)const{return duration>0&&t<start+duration;}void to(double v,double t,double d,core::CubicTiming curve={0,0,.58,1}){if(v==target)return;from=value(t);target=v;start=t;duration=d;timing=curve;}void finish(){from=target;duration=0;}};
constexpr std::array<Vertex,4>quad{{{{0,0,0},{0,0}},{{1,0,0},{1,0}},{{1,1,0},{1,1}},{{0,1,0},{0,1}}}};
constexpr std::array<std::uint32_t,6>indices{0,1,2,0,2,3};
float linear(double x){return float(x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4));}
std::array<float,4>tint(m::OrbiPomColor c,double alpha=1){return {linear(c[0]),linear(c[1]),linear(c[2]),float(c[3]*alpha)};}
bool cutContains(R r,P p){if(!r.contains(p))return false;const auto x=p.x-r.x,y=p.y-r.y,c=std::min(4.,std::min(r.width,r.height)/3);return x+y>=c&&(r.width-x)+(r.height-y)>=c;}
struct Part {
    LayerScene scene;std::vector<m::OrbiPomSurface>plan;std::vector<LayerPlacement>poses;std::vector<std::array<PlaneMask,8>>masks;std::vector<Track>tracks;std::vector<std::uint64_t>revision;std::uint64_t uploadedRevision{};
    explicit Part(LayerRasterizer&r):scene(r){}
    bool install(std::vector<m::OrbiPomSurface>next,const LayerRasterOptions&o,OrbiPomSceneStats&stats){bool structure=plan.size()!=next.size()||!scene.contentRevision();for(std::size_t n=0;!structure&&n<next.size();++n)structure=plan[n].id!=next[n].id;
        if(structure){scene.load(root(next),o);need(scene.report().unsupported.empty()&&scene.draws().size()==next.size(),"Unsupported original OrbiPom local artwork");poses.resize(next.size());masks.resize(next.size());revision.assign(next.size(),scene.contentRevision());tracks.resize(next.size());for(std::size_t n=0;n<next.size();++n){need(scene.surfaceIndex(next[n].id)==n,"OrbiPom local paint order changed");tracks[n]={next[n].opacity,next[n].opacity,0,0};}}
        else for(std::size_t n=0;n<next.size();++n)if(next[n].content!=plan[n].content){scene.updateLocalContent(next[n].id,++revision[n],next[n].content,o);++stats.localUpdates;}
        plan=std::move(next);return structure;
    }
    // LayerScene's explicit upload accepts IDs by value at the Renderer seam.
    // Keep it a resource event, not a body/tilt-frame operation. Commit this
    // token only after success so a partially failed upload remains retryable.
    void upload(Renderer&r){if(uploadedRevision!=scene.resourceRevision()){scene.uploadResources(r);uploadedRevision=scene.resourceRevision();}}
    bool release(Renderer&r){uploadedRevision=0;return scene.releaseResources(r);}
    void place(const M&world,float opacity,double time,std::span<const PlaneMask>outer,const PlaneShutter*shutter,const Track*dim=nullptr){for(std::size_t n=0;n<plan.size();++n){const auto&s=plan[n];auto&p=poses[n];p.surface=n;p.world=world*s.local;p.opacity=opacity*(s.role==m::OrbiPomSurfaceRole::dimmer&&dim?float(dim->value(time)):s.action?float(tracks[n].value(time)):s.opacity);std::copy(outer.begin(),outer.end(),masks[n].begin());p.masks={masks[n].data(),outer.size()};}scene.setPlacements(poses);scene.setGroupShutter(shutter?std::optional<PlaneShutter>(*shutter):std::nullopt);}
};
struct ContentKey {std::int64_t score{},best{},danger{};int energy{},charge{},skill{-1};bool playing{},idle{},manual{},restart{},rules{},active{},paused{};std::optional<std::string>phase,error;bool operator==(const ContentKey&)const=default;};
ContentKey key(const m::OrbiPomSession&s,const m::OrbiPomState&state){const auto&v=s.snapshot();return {v.score,std::max(s.bestScore(),v.highScore),v.dangerSeconds&&v.isPlaying()?std::int64_t(std::ceil(*v.dangerSeconds)):-1,v.energy,v.swapCharge,v.skill?static_cast<int>(*v.skill):-1,v.isPlaying(),v.state=="idle",s.manuallyPaused(),state.restartConfirmation(),state.rulesPresented(),state.active(),v.paused,v.skillPhase,s.error()};}
bool sameControls(const ContentKey&a,const ContentKey&b){return a.energy==b.energy&&a.charge==b.charge&&a.skill==b.skill&&a.playing==b.playing&&a.idle==b.idle&&a.manual==b.manual&&a.restart==b.restart&&a.rules==b.rules&&a.active==b.active&&a.paused==b.paused&&a.phase==b.phase&&a.error==b.error;}

}
struct NativeOrbiPomScene::Impl {
    struct Body {m::OrbiPomBody value;bool wanted{},selected{};std::string imageID,ringID;std::shared_ptr<const LayerRasterImage>ring;std::uint64_t ringRevision{},uploadedRing{};double ringSize{};};
    m::OrbiPomSession&session;m::OrbiPomState&state;LayerRasterizer&raster;LayerRasterOptions options;m::OrbiPomAppearance appearance;
    Part face,overlay,menu;LayerScene carrier;NativeLayerGroup menuGroup;std::string prefix,mesh,dangerID;std::array<std::string,11>textureIDs;std::array<std::shared_ptr<const LayerRasterImage>,11>images;std::shared_ptr<const LayerRasterImage>danger;
    std::vector<Body>bodies;std::map<std::int64_t,std::size_t>bodyIndex;std::vector<std::string>retired;std::vector<DrawObject>draws;std::array<LayerCompositionEntry,4>entries{};
    m::OrbiPomRules rules;Track dim,menuFade,scroll;std::optional<ContentKey>content;std::optional<OrbiPomScenePose>pose;std::array<PlaneMask,7>poseMasks{};std::optional<PlaneShutter>poseShutter;
    bool dirty{true},geometry{},spriteReady{},rulesOpen{},menuReady{},menuUploaded{},menuDirty{},bodyStructure{true};std::array<bool,11>textureResident{};bool dangerResident{};double menuRetireAt{};Renderer*owner{};std::uint64_t serial{},shapeRevision{},uploadedShape{},fontRevision{};double time{},lastScrollTarget{};OrbiPomSceneStats counts;
    Impl(m::OrbiPomSession&s,m::OrbiPomState&st,LayerRasterizer&r,LayerRasterOptions o,m::OrbiPomAppearance a):session(s),state(st),raster(r),options(std::move(o)),appearance(a),face(r),overlay(r),menu(r),carrier(r),menuGroup(menu.scene,"orbipom.rules",options.pixelsPerPoint){
        static std::atomic<std::uint64_t>seq{};prefix="orbipom:"+std::to_string(seq.fetch_add(1));mesh=prefix+"/quad";dangerID=prefix+"/danger";for(unsigned n=0;n<11;++n)textureIDs[n]=prefix+"/level"+std::to_string(n+1);carrier.load(root({}),options);validateOrbiPomAssets(options.assetRoot);(void)m::prepareOrbiPomArtwork(session,state,appearance);
    }
    ~Impl(){for(const auto&id:textureIDs)raster.remove(id);raster.remove(dangerID);for(const auto&b:bodies)raster.remove(b.ringID);for(const auto&id:retired)raster.remove(id);}
    void clock(double t){need(std::isfinite(t)&&t>=time,"OrbiPom needs a finite monotonic owner clock");time=t;}
    void loadSprites(){if(images[0])return;std::array<std::shared_ptr<const LayerRasterImage>,11>candidate;auto o=options;o.pixelsPerPoint=1;o.paddingPoints=0;for(unsigned n=0;n<11;++n){const auto&a=m::orbiPomAssets()[n];candidate[n]=raster.rasterize(textureIDs[n],1,m::orbiPomImage(n,{0,0,double(a.width),double(a.height)}),o);need(candidate[n]->complete(),"Unsupported original OrbiPom image");}images=std::move(candidate);}
    void updateBodies(){const auto&s=session.snapshot();need(s.bodies.size()<=(Renderer::maximumObjects-100)/2,"OrbiPom bodies exceed shared renderer object capacity");for(auto&b:bodies)b.wanted=false;
        for(const auto&v:s.bodies){need(v.level>=1&&v.level<=11&&std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.angle)&&std::isfinite(v.size)&&std::isfinite(v.scale)&&v.size>0&&v.scale>=0&&v.size*v.scale*1.1<=440&&std::isfinite(v.opacity)&&v.opacity>=0&&v.opacity<=1,"Invalid original OrbiPom body");auto found=bodyIndex.find(v.id);if(found==bodyIndex.end()){const auto n=bodies.size();Body b;b.value=v;b.imageID=prefix+"/body"+std::to_string(++serial);b.ringID=b.imageID+"/ring";bodies.push_back(std::move(b));bodyIndex.emplace(v.id,n);found=bodyIndex.find(v.id);bodyStructure=true;}
            auto&b=bodies[found->second];need(!b.wanted,"Duplicate OrbiPom body identity");b.wanted=true;b.value=v;b.selected=s.hoverBodyID==v.id||std::find(s.selectedBodyIDs.begin(),s.selectedBodyIDs.end(),v.id)!=s.selectedBodyIDs.end();const auto size=v.size*v.scale*1.1;
            if(b.selected&&size>0&&(!b.ring||b.ringSize!=size||dirty)){b.ring=raster.rasterize(b.ringID,++b.ringRevision,m::orbiPomSelectionRing(size,appearance.accent),options);need(b.ring->complete(),"Unsupported original OrbiPom selected border");b.ringSize=size;++counts.ringRasters;}}
        if(std::any_of(bodies.begin(),bodies.end(),[](const Body&b){return !b.wanted;})){for(auto&b:bodies)if(!b.wanted&&b.ring){if(b.uploadedRing)retired.push_back(b.ringID);else raster.remove(b.ringID);}bodies.erase(std::remove_if(bodies.begin(),bodies.end(),[](const Body&b){return !b.wanted;}),bodies.end());bodyIndex.clear();for(std::size_t n=0;n<bodies.size();++n)bodyIndex.emplace(bodies[n].value.id,n);bodyStructure=true;}
        if(bodyStructure){draws.clear();draws.reserve(9+bodies.size()*2);auto add=[&](std::string id){DrawObject d;d.sourceID=std::move(id);d.meshID=mesh;d.masks.reserve(8);draws.push_back(std::move(d));};add(prefix+"/charge");for(unsigned n=0;n<5;++n)add(prefix+"/wind"+std::to_string(n));add(prefix+"/preview");add(prefix+"/danger-draw");for(const auto&b:bodies){add(b.imageID);add(b.ringID);}add(prefix+"/next");bodyStructure=false;}
        counts.bodies=bodies.size();
    }
    void prepareRules(){const auto paragraphs=m::orbiPomRuleParagraphs(appearance.language);std::array<double,9>height{};for(unsigned n=0;n<9;++n){auto text=m::orbiPomText(paragraphs[n],{0,0,294,1000},10,{1,1,1,1},false,true,false);height[n]=raster.measureSourceText(prefix+"/rule-measure",text["text"],294,options).height;}rules=m::prepareOrbiPomRules(appearance,height);menu.install(rules.surfaces,options,counts);menuReady=true;menuDirty=true;scroll.to(std::clamp(scroll.target,0.,rules.maximumScroll),time,0);}
    void feedback(Part&p,std::optional<m::OrbiPomAction>hit,bool pressed){for(std::size_t n=0;n<p.plan.size();++n){auto&s=p.plan[n];if(!s.action)continue;const bool selected=hit==s.action;const auto target=selected?(s.role==m::OrbiPomSurfaceRole::rim?1:pressed?1:.62):s.opacity;p.tracks[n].to(target,time,appearance.reducedMotion?0:pressed&&selected?.06:.14);}}
    void numbers(){if(!pose)return;const auto&p=*pose;need(p.masks.size()<=7,"Too many OrbiPom outer masks");const auto inverse=core::source::inverseSourceMatrix(p.world);face.place(p.world,p.opacity,time,p.masks,p.shutter);overlay.place(p.world,p.opacity,time,p.masks,p.shutter,&dim);
        auto set=[&](DrawObject&d,R r,float opacity,std::array<float,4>color,bool clip,std::string_view texture={}){d.world=p.world*M::translation(r.x,r.y)*M::scale(std::max(r.width,1e-9),std::max(r.height,1e-9));d.opacity=p.opacity*opacity;d.linearTint=color;if(d.textureID!=texture)d.textureID=texture;d.masks.assign(p.masks.begin(),p.masks.end());if(clip)d.masks.push_back({inverse,m::orbiPomPlayArea});d.shutter=p.shutter?std::optional<PlaneShutter>(*p.shutter):std::nullopt;};
        const auto&s=session.snapshot();const auto white=std::array<float,4>{1,1,1,1};const auto charge=72*std::clamp(s.energyProgress/12,0.,1.);set(draws[0],{352,82,charge,3},charge>0?1.f:0.f,tint(appearance.accent),false);
        const auto y=s.windSurfaceY.value_or(280.),h=std::max(0.,(280-y)*1.1),top=88+y*1.1;const std::array<R,5>w{{{87,top,253,h},{86.5,top-.5,254,1},{86.5,top+h-.5,254,1},{86.5,top+.5,1,std::max(0.,h-1)},{339.5,top+.5,1,std::max(0.,h-1)}}};for(unsigned n=0;n<5;++n)set(draws[n+1],w[n],s.windSurfaceY&&w[n].height>0?1.f:0.f,tint(appearance.accent,n?.65:.10),true);
        auto sprite=[&](DrawObject&d,int level,P center,double size,double angle,float opacity,bool clip){need(level>=1&&level<=11,"Invalid original OrbiPom level");const auto&a=m::orbiPomAssets()[level-1];const double ratio=double(a.width)/a.height,width=std::min(size,size*ratio),height=width/ratio;set(d,{0,0,width,height},opacity,white,clip,textureIDs[level-1]);d.world=p.world*M::translation(center.x,center.y)*M::rotation(0,0,angle)*M::translation(-width/2,-height/2)*M::scale(std::max(width,1e-9),std::max(height,1e-9));};
        sprite(draws[6],s.currentLevel,{87+s.previewX*1.1,88+s.previewY*1.1},s.previewSize*s.previewScale*1.1,0,s.previewVisible&&s.isPlaying()?1.f:0.f,true);
        const auto b=danger?danger->bounds:R{0,0,253,1};set(draws[7],{87+b.x,96.8+b.y,b.width,b.height},s.dangerSeconds?1.f:0.f,white,true,dangerID);
        for(std::size_t n=0;n<bodies.size();++n){const auto&v=bodies[n];const auto place=m::orbiPomPlacement(v.value);auto&image=draws[8+n*2];sprite(image,v.value.level,place.center,place.size,place.angle,float(place.opacity),true);auto&ring=draws[9+n*2];const auto rb=v.ring?v.ring->bounds:R{0,0,1,1};set(ring,rb,v.selected&&v.ring&&place.size>0?float(place.opacity):0.f,white,true,v.ring?v.ringID:std::string_view{});ring.world=p.world*M::translation(place.center.x,place.center.y)*M::rotation(0,0,place.angle)*M::translation(-place.size/2+rb.x,-place.size/2+rb.y)*M::scale(rb.width,rb.height);}
        sprite(draws.back(),s.nextLevel,{43.5,93.5},45,0,1,false);
        if(menuReady){const auto offset=scroll.value(time);for(std::size_t n=0;n<menu.plan.size();++n){auto&lp=menu.poses[n];lp={n,menu.plan[n].local,menu.plan[n].action?float(menu.tracks[n].value(time)):1,{}};if(std::find(rules.paragraphSurfaces.begin(),rules.paragraphSurfaces.end(),n)!=rules.paragraphSurfaces.end()){lp.world=M::translation(0,-offset)*lp.world;menu.masks[n][0]={{},rules.viewport};lp.masks={menu.masks[n].data(),1};}}menu.scene.setPlacements(menu.poses);
            if(menuUploaded)menuGroup.setPose(p.world*M::translation(rules.origin.x,rules.origin.y),p.opacity*float(menuFade.value(time)),p.masks,p.shutter?std::optional<PlaneShutter>(*p.shutter):std::nullopt);}
        ++counts.poses;
    }
};
NativeOrbiPomScene::NativeOrbiPomScene(m::OrbiPomSession&s,m::OrbiPomState&state,LayerRasterizer&r,LayerRasterOptions o,m::OrbiPomAppearance a):impl_(std::make_unique<Impl>(s,state,r,std::move(o),a)){}
NativeOrbiPomScene::~NativeOrbiPomScene()=default;
bool NativeOrbiPomScene::setAppearance(m::OrbiPomAppearance a){auto&i=*impl_;(void)m::prepareOrbiPomArtwork(i.session,i.state,a);if(i.appearance==a)return false;i.appearance=a;i.dirty=true;return true;}
bool NativeOrbiPomScene::syncContent(double t){auto&i=*impl_;i.clock(t);if(i.fontRevision!=i.raster.fontRevision()){i.fontRevision=i.raster.fontRevision();i.dirty=true;i.face.scene.refreshTypography();i.overlay.scene.refreshTypography();}const auto next=key(i.session,i.state);bool changed=i.dirty||!i.content||*i.content!=next;
    if(changed){auto art=m::prepareOrbiPomArtwork(i.session,i.state,i.appearance);i.face.install(std::move(art.face),i.options,i.counts);i.overlay.install(std::move(art.overlay),i.options,i.counts);if(i.dirty||!i.content||!sameControls(*i.content,next))for(std::size_t n=0;n<i.overlay.plan.size();++n){const double opacity=i.overlay.plan[n].opacity;i.overlay.tracks[n]={opacity,opacity,0,0};}i.content=next;++i.counts.contentBuilds;i.dim.to(i.state.boardDimmed()?1:0,t,i.appearance.reducedMotion?0:.16,{0,0,1,1});}
    if(!i.danger||i.dirty){i.danger=i.raster.rasterize(i.dangerID,++i.shapeRevision,m::orbiPomDangerLine(i.appearance.systemRed),i.options);need(i.danger->complete(),"Unsupported source danger line");}
    if(i.state.rulesPresented()!=i.rulesOpen){i.rulesOpen=i.state.rulesPresented();if(i.rulesOpen){i.scroll={};i.prepareRules();}const bool animate=!i.appearance.reducedMotion&&i.state.active();i.menuFade={i.rulesOpen?0.:1.,i.rulesOpen?1.:0.,t,animate?.16:0,{0,0,1,1}};i.menuRetireAt=i.rulesOpen?0:t+(animate?.18:0);changed=true;}else if(i.dirty&&i.menuReady)i.prepareRules();
    i.loadSprites();i.updateBodies();i.dirty=false;i.counts.surfaces=i.face.plan.size()+i.overlay.plan.size()+(i.menuReady?i.menu.plan.size():0);return changed;
}
bool NativeOrbiPomScene::setFeedback(std::optional<P>p,bool pressed,double t){auto&i=*impl_;i.clock(t);std::optional<m::OrbiPomAction>hit;
    if(p&&i.state.rulesPresented()){const R close{i.rules.origin.x+291,i.rules.origin.y+8,23,23};if(cutContains(close,*p))hit=m::OrbiPomAction::rules;i.feedback(i.menu,hit,pressed);i.feedback(i.overlay,{},false);}
    else{const auto actions=i.state.actions();if(p)for(std::size_t n=0;n<actions.count;++n)if(actions.items[n].enabled&&cutContains(actions.items[n].rect,*p))hit=actions.items[n].action;i.feedback(i.overlay,hit,pressed);}return bool(hit);
}
bool NativeOrbiPomScene::scrollRules(double delta,double t){auto&i=*impl_;i.clock(t);need(std::isfinite(delta),"Invalid rules scroll");if(!i.rulesOpen)return false;const double target=std::clamp(i.scroll.target+delta,0.,i.rules.maximumScroll);if(target==i.scroll.target)return false;i.scroll.to(target,t,i.appearance.reducedMotion?0:.13);return true;}
R NativeOrbiPomScene::rulesBounds()const noexcept{const auto&i=*impl_;return {i.rules.origin.x,i.rules.origin.y,i.rules.bounds.width,i.rules.bounds.height};}double NativeOrbiPomScene::rulesScroll()const noexcept{return impl_->scroll.target;}
bool NativeOrbiPomScene::updatePose(const OrbiPomScenePose&p){auto&i=*impl_;i.clock(p.time);need(i.content&&p.world.finite()&&std::isfinite(p.opacity)&&p.opacity>=0&&p.opacity<=1&&p.masks.size()<=7,"Invalid OrbiPom pose");std::copy(p.masks.begin(),p.masks.end(),i.poseMasks.begin());i.poseShutter=p.shutter?std::optional<PlaneShutter>(*p.shutter):std::nullopt;i.pose=p;i.pose->masks={i.poseMasks.data(),p.masks.size()};i.pose->shutter=i.poseShutter?&*i.poseShutter:nullptr;i.numbers();return true;}
void NativeOrbiPomScene::uploadResources(Renderer&r){auto&i=*impl_;need(i.content&&(!i.owner||i.owner==&r),"Synchronize OrbiPom through its renderer");i.owner=&r;if(!i.geometry){r.setMesh(i.mesh,1,{quad,indices});i.geometry=true;}if(!i.spriteReady){for(unsigned n=0;n<11;++n){const auto&a=*i.images[n];r.setTexture(i.textureIDs[n],1,{a.width,a.height,a.straightRGBA});i.textureResident[n]=true;++i.counts.spriteUploads;}i.spriteReady=true;}if(i.danger&&i.uploadedShape!=i.shapeRevision){r.setTexture(i.dangerID,i.shapeRevision,{i.danger->width,i.danger->height,i.danger->straightRGBA});i.uploadedShape=i.shapeRevision;i.dangerResident=true;}for(auto&b:i.bodies)if(b.ring&&b.uploadedRing!=b.ringRevision){r.setTexture(b.ringID,b.ringRevision,{b.ring->width,b.ring->height,b.ring->straightRGBA});b.uploadedRing=b.ringRevision;}
    i.face.upload(r);i.overlay.upload(r);if(i.menuReady&&(i.rulesOpen||i.time<i.menuRetireAt)){if(!i.menuUploaded||i.menuDirty){i.menuGroup.uploadResources(r);i.menuUploaded=true;i.menuDirty=false;}else i.menuGroup.updateLocal(r);if(i.pose){const auto&p=*i.pose;i.menuGroup.setPose(p.world*M::translation(i.rules.origin.x,i.rules.origin.y),p.opacity*float(i.menuFade.value(i.time)),p.masks,p.shutter?std::optional<PlaneShutter>(*p.shutter):std::nullopt);}}
}
bool NativeOrbiPomScene::collectRetired(Renderer&r){auto&i=*impl_;i.face.scene.collectRetiredResources(r);i.overlay.scene.collectRetiredResources(r);bool done=true;if(i.menuUploaded&&!i.rulesOpen&&i.time>=i.menuRetireAt){if(i.menuGroup.releaseResources(r))i.menuUploaded=false;else done=false;}for(auto it=i.retired.begin();it!=i.retired.end();){if(r.removeTexture(*it)){i.raster.remove(*it);it=i.retired.erase(it);}else{done=false;++it;}}return done;}
std::span<const LayerCompositionEntry>NativeOrbiPomScene::entries(){auto&i=*impl_;if(!i.content)return {};i.entries[0]={&i.face.scene,{}};i.entries[1]={&i.carrier,i.draws};i.entries[2]={&i.overlay.scene,{}};if(i.menuUploaded&&(i.rulesOpen||i.time<i.menuRetireAt)){i.entries[3]=i.menuGroup.entry();return i.entries;}return {i.entries.data(),3};}
bool NativeOrbiPomScene::requiresFrames(double t)const{const auto&i=*impl_;if((i.menuUploaded&&!i.rulesOpen&&t<i.menuRetireAt)||i.dim.moving(t)||i.menuFade.moving(t)||i.scroll.moving(t))return true;for(const auto*p:{&i.overlay,&i.menu})for(const auto&f:p->tracks)if(f.moving(t))return true;return false;}
void NativeOrbiPomScene::settle(){auto&i=*impl_;i.dim.finish();i.menuFade.finish();i.scroll.finish();for(auto*p:{&i.overlay,&i.menu})for(auto&f:p->tracks)f.finish();}
bool NativeOrbiPomScene::releaseResources(Renderer&r){auto&i=*impl_;need(!i.owner||i.owner==&r,"Wrong OrbiPom renderer");
    // A borrowed body texture/mesh can keep only part of this scene resident.
    // Invalidate upload gates before any release, including false/throw paths;
    // a later reuse must restore every removed resource from retained pixels.
    i.spriteReady=false;i.uploadedShape=0;bool okay=true;if(i.menuUploaded){if(i.menuGroup.releaseResources(r))i.menuUploaded=false;else okay=false;}okay=i.face.release(r)&&okay;okay=i.overlay.release(r)&&okay;okay=i.carrier.releaseResources(r)&&okay;for(unsigned n=0;n<11;++n)if(i.textureResident[n]){if(r.removeTexture(i.textureIDs[n]))i.textureResident[n]=false;else okay=false;}if(i.dangerResident){if(r.removeTexture(i.dangerID))i.dangerResident=false;else okay=false;}for(auto&b:i.bodies)if(b.uploadedRing){if(r.removeTexture(b.ringID))b.uploadedRing=0;else okay=false;}okay=collectRetired(r)&&okay;if(i.geometry){if(r.removeMesh(i.mesh))i.geometry=false;else okay=false;}if(okay)i.owner=nullptr;return okay;}
OrbiPomSceneStats NativeOrbiPomScene::stats()const noexcept{return impl_->counts;}
}
#endif
