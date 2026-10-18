#include "native/calendar_scene.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include "core/motion.hpp"
#include <atomic>
#endif
namespace endfield::native {
namespace {using J=ehud::data::Json;using M=core::Matrix4;using R=core::Rect;using P=core::Point;
void need(bool b,const char*m){if(!b)throw std::invalid_argument(m);}
double number(const J&j,double fallback=0){if(j.isNull())return fallback;need(j.isNumber()&&std::isfinite(j.number()),"Invalid Calendar layer number");return j.number();}
R rect(const J&j){need(j.isArray()&&j.array().size()==4,"Invalid Calendar layer bounds");const auto&a=j.array();return {number(a[0]),number(a[1]),number(a[2]),number(a[3])};}
J empty(std::string id="calendar/assets"){return J::Object{{"id",std::move(id)},{"bounds",J::Array{0,0,0,0}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"allowsGroupOpacity",false},{"children",J::Array{}}};}
bool drawable(const J&j){return !j["backgroundColor"].isNull()||!j["contents"].isNull()||number(j["borderWidth"])>0||(!j["kind"].isNull()&&(j["kind"].string()=="shape"||j["kind"].string()=="text"));}
void rename(J&j,const std::string&prefix){j["id"]=prefix;if(!j["children"].isNull()){auto children=j["children"].array();for(std::size_t n=0;n<children.size();++n)rename(children[n],prefix+"/"+std::to_string(n));j["children"]=std::move(children);}}
R joined(R a,R b){const auto x=std::min(a.x,b.x),y=std::min(a.y,b.y);return{x,y,std::max(a.x+a.width,b.x+b.width)-x,std::max(a.y+a.height,b.y+b.height)-y};}
#ifdef _WIN32
bool contains(R r,P p){return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
#endif
}
CalendarScenePlan prepareCalendarScene(const modules::CalendarArtwork&art){
    need(art.bounds.width>0&&art.bounds.height>0&&art.bounds.width<=400&&art.bounds.height<=440&&art.feedback.size()<=42,"Calendar artwork exceeds source bounds");
    CalendarScenePlan out;out.bounds=art.bounds;out.feedback=art.feedback;out.assets=empty();J::Array assets;J::Array ordinary;std::optional<R>spanBounds;std::vector<J>feedbackAssets;
    const auto asset=[&](J node,bool shared){J comparable=node;rename(comparable,"asset");if(shared)for(std::size_t n=0;n<feedbackAssets.size();++n)if(feedbackAssets[n]==comparable)return n;
        const auto index=assets.size();rename(node,"calendar/asset/"+std::to_string(index));assets.push_back(std::move(node));feedbackAssets.resize(assets.size());if(shared)feedbackAssets[index]=std::move(comparable);return index;};
    const auto flush=[&]{if(ordinary.empty())return;const auto source=*spanBounds;const double x=std::floor(source.x),y=std::floor(source.y);const R b{x,y,std::ceil(source.x+source.width)-x,std::ceil(source.y+source.height)-y};auto node=empty();node["bounds"]=J::Array{0,0,b.width,b.height};node["allowsGroupOpacity"]=true;for(auto&child:ordinary){const auto&p=child["position"].array();child["position"]=J::Array{p[0].number()-b.x,p[1].number()-b.y};}node["children"]=std::move(ordinary);const auto n=asset(std::move(node),false);out.draws.push_back({"calendar/span/"+std::to_string(out.draws.size()),n,M::translation(b.x,b.y),1,{},false});ordinary={};spanBounds.reset();};
    const auto append=[&](J node,double x,double y,float opacity){const auto b=rect(node["bounds"]);node["position"]=J::Array{x,y};node["anchorPoint"]=J::Array{0,0};node["children"]=J::Array{};node["opacity"]=opacity;const R ink{x+b.x,y+b.y,b.width,b.height};need(ink.width>0&&ink.height>0,"Calendar drawing has empty source bounds");spanBounds=spanBounds?joined(*spanBounds,ink):ink;ordinary.push_back(std::move(node));};
    std::function<void(const J&,double,double,float,unsigned)>visit;
    visit=[&](const J&source,double parentX,double parentY,float parentOpacity,unsigned depth){need(depth<=8,"Calendar source tree exceeds its source depth");need(source["transform"].isNull()&&source["sublayerTransform"].isNull()&&source["mask"].isNull()&&source["masksToBounds"].isNull(),"Calendar source requires unsupported projected local content");if(!source["hidden"].isNull()&&source["hidden"].boolean())return;const auto b=rect(source["bounds"]);need(b.width>=0&&b.height>=0&&std::abs(b.x)<4096&&std::abs(b.y)<4096,"Calendar source bounds invalid");const auto&position=source["position"];double x=parentX,y=parentY;if(!position.isNull()){need(position.array().size()==2,"Invalid Calendar source position");x+=number(position.array()[0]);y+=number(position.array()[1]);}const auto&anchor=source["anchorPoint"];const double ax=anchor.isNull()?.5:number(anchor.array()[0]),ay=anchor.isNull()?.5:number(anchor.array()[1]);x-=b.x+ax*b.width;y-=b.y+ay*b.height;const float opacity=parentOpacity*float(number(source["opacity"],1));need(opacity>=0&&opacity<=1,"Invalid Calendar source opacity");
        if(!source["name"].isNull()&&source["name"].string()=="hud.control.highlight"){
            flush();const auto&children=source["children"].array();need(children.size()==2,"Calendar highlight must retain its tint and rim");std::optional<std::size_t>binding;for(std::size_t n=0;n<out.feedback.size();++n)if(children[0]["id"].string()==out.feedback[n].tintID&&children[1]["id"].string()==out.feedback[n].rimID){binding=n;break;}need(binding.has_value(),"Calendar highlight has no source action binding");
            for(unsigned n=0;n<2;++n){auto node=children[n];const auto childBounds=rect(node["bounds"]);need(node["position"].array()==J::Array{0,0}&&childBounds.x==0&&childBounds.y==0,"Calendar highlight local geometry differs from source");
                // Cut-corner rim extends two points plus its 0.9pt stroke. A
                // conservative integer wrapper keeps every vector raster on
                // the full source tree's pixel grid. Measuring the rim alone
                // would start at -2.45pt, changing AA before GPU sampling.
                constexpr double margin=4;node["position"]=J::Array{margin,margin};node["anchorPoint"]=J::Array{0,0};node["opacity"]=1;auto wrapper=empty();wrapper["bounds"]=J::Array{0,0,childBounds.width+2*margin,childBounds.height+2*margin};wrapper["children"]=J::Array{std::move(node)};const auto index=asset(std::move(wrapper),true);out.draws.push_back({children[n]["id"].string(),index,M::translation(x-margin,y-margin),parentOpacity,binding,n==1});}return;
        }
        const auto&children=source["children"];if(drawable(source)){auto node=source;if(!children.isNull()&&!children.array().empty()){node["borderWidth"]=0;node["borderColor"]=J{};}if(drawable(node))append(std::move(node),x,y,opacity);}
        if(!children.isNull())for(const auto&child:children.array())visit(child,x,y,opacity,depth+1);
        if(!children.isNull()&&!children.array().empty()&&number(source["borderWidth"])>0){auto border=source;border["kind"]="layer";border["contents"]=J{};border["backgroundColor"]=J{};border["shape"]=J{};border["text"]=J{};append(std::move(border),x,y,opacity);}
    };
    visit(art.root,0,0,1,0);flush();need(!assets.empty()&&assets.size()<=96&&out.draws.size()<=192,"Calendar retained source assets exceed bounded plan");out.assetCount=assets.size();out.assets["children"]=std::move(assets);return out;
}
#ifdef _WIN32
struct NativeCalendarScene::Impl {
    struct Track{double from{},target{},start{},duration{};};static double value(Track t,double now){return t.duration>0?t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value((now-t.start)/t.duration):t.target;}
    struct Part {
        modules::CalendarArtwork art;CalendarScenePlan plan;LayerScene assets,carrier;std::string id;std::vector<DrawObject>children;std::vector<std::array<Track,2>>feedback;std::array<DrawObject,1>output;Renderer*renderer{};std::uint64_t uploadedRevision{};bool registered{},capturedDirty{};
        Part(LayerRasterizer&r,LayerRasterOptions o,modules::CalendarArtwork a):art(std::move(a)),plan(prepareCalendarScene(art)),assets(r),carrier(r),feedback(plan.feedback.size()){
            static std::atomic<std::uint64_t>ids{};id="calendar/group/"+std::to_string(ids.fetch_add(1));assets.load(plan.assets,o);need(assets.report().unsupported.empty(),"Unsupported Calendar source artwork");need(assets.draws().size()==plan.assetCount,"Calendar source asset raster is missing");children.reserve(plan.draws.size()+3);output[0].masks.reserve(8);
            for(std::size_t n=0;n<plan.draws.size();++n){const auto&p=plan.draws[n];const auto index=assets.surfaceIndex("calendar/asset/"+std::to_string(p.asset));need(index.has_value(),"Calendar shared source asset lost its index");auto draw=assets.draws()[*index];draw.sourceID=id+"/draw/"+std::to_string(n);draw.world=p.local;draw.opacity=p.feedback?(p.rim?plan.feedback[*p.feedback].restingRim:0):p.opacity;children.push_back(std::move(draw));}
            for(std::size_t n=0;n<feedback.size();++n)feedback[n][1].from=feedback[n][1].target=plan.feedback[n].restingRim;
        }
        void sample(double t){for(std::size_t n=0;n<plan.draws.size();++n){const auto&p=plan.draws[n];if(p.feedback)children[n].opacity=p.opacity*float(value(feedback[*p.feedback][p.rim?1:0],t));}}
        bool upload(Renderer&r,const LayerRasterOptions&o){need(!renderer||renderer==&r,"Calendar group belongs to another renderer");renderer=&r;bool changed{};if(uploadedRevision!=assets.resourceRevision()||capturedDirty){assets.uploadResources(r);const auto b=plan.bounds;changed=r.configureNativeGroup(id,{{b.x-6,b.y-6,b.width+12,b.height+12},o.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB},children);registered=true;const auto&draw=r.nativeGroupOutput(id);if(output[0].sourceID.empty()){auto next=draw;next.masks.reserve(8);output[0]=std::move(next);}uploadedRevision=assets.resourceRevision();capturedDirty=false;assets.collectRetiredResources(r);}return changed;}
        bool release(Renderer&r){need(!renderer||renderer==&r,"Calendar group retirement needs its renderer");if(registered&&!r.removeNativeGroup(id))return false;registered=false;const bool okay=assets.releaseResources(r);if(okay){renderer=nullptr;uploadedRevision=0;}return okay;}
    };
    LayerRasterizer&raster;LayerRasterOptions options;std::unique_ptr<Part>current,outgoing;std::vector<std::unique_ptr<Part>>retired;std::array<LayerCompositionEntry,2>entries;std::size_t count{};std::uint64_t revision{},fontRevision{};double time{},fadeStart{};bool fading{},posed{},reduceMotion{};std::optional<core::Point>hover;bool pressed{};M world;float opacity{};std::array<PlaneMask,8>masks;std::size_t maskCount{};std::optional<PlaneShutter>shutter;NativeCalendarSceneStats stats;
    Impl(LayerRasterizer&r,LayerRasterOptions o):raster(r),options(std::move(o)){retired.reserve(3);}
    void clock(double t)const{need(std::isfinite(t)&&t>=time,"Calendar scene requires a finite monotonic owner clock");}
    void retire(std::unique_ptr<Part>&p){if(!p)return;if(!p->registered||(p->renderer&&p->release(*p->renderer))){p.reset();return;}need(retired.size()<3,"Publish and collect preceding Calendar generations before rebuilding");retired.push_back(std::move(p));}
    void rebuild(){count=0;if(outgoing&&outgoing->registered)entries[count++]={&outgoing->carrier,outgoing->output};if(current&&current->registered)entries[count++]={&current->carrier,current->output};}
    void pose(Part&p,float alpha){if(!p.registered)return;auto&d=p.output[0];d.world=world;d.opacity=opacity*alpha;d.masks.assign(masks.begin(),masks.begin()+static_cast<std::ptrdiff_t>(maskCount));d.shutter=shutter;validateDrawObject(d);}
    void feedbackTo(Part&p,std::optional<P>point,bool down,double now,bool reduced){for(std::size_t n=0;n<p.feedback.size();++n){const auto&f=p.plan.feedback[n];const bool hit=f.enabled&&point&&contains(f.rect,*point);const std::array<double,2>targets{hit?(down?1:.62):0,hit?1:f.restingRim};for(unsigned k=0;k<2;++k){auto&t=p.feedback[n][k];if(t.target!=targets[k]||(reduced&&t.duration))t={value(t,now),targets[k],now,reduced?0:down&&hit?.06:.14};}}}
};
NativeCalendarScene::NativeCalendarScene(LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(r,std::move(o))){}NativeCalendarScene::~NativeCalendarScene(){auto&i=*impl_;if(i.current&&i.current->renderer)try{(void)i.current->release(*i.current->renderer);}catch(...){}if(i.outgoing&&i.outgoing->renderer)try{(void)i.outgoing->release(*i.outgoing->renderer);}catch(...){}for(auto&p:i.retired)if(p->renderer)try{(void)p->release(*p->renderer);}catch(...){}}
bool NativeCalendarScene::syncContent(modules::CalendarArtwork a,std::uint64_t revision,double t,bool animated,bool reduced){auto&i=*impl_;i.clock(t);if(i.current&&i.revision==revision&&i.fontRevision==i.raster.fontRevision())return false;need(i.retired.size()<3,"Publish and collect preceding Calendar generations before rebuilding");auto candidate=std::make_unique<Impl::Part>(i.raster,i.options,std::move(a));i.feedbackTo(*candidate,i.hover,i.pressed,t,reduced);i.retire(i.outgoing);if(i.current){i.current->sample(t);if(animated&&!reduced){i.outgoing=std::move(i.current);i.fading=true;i.fadeStart=t;}else{i.retire(i.current);i.fading=false;}}i.current=std::move(candidate);i.revision=revision;i.fontRevision=i.raster.fontRevision();i.reduceMotion=reduced;i.time=t;i.posed=false;++i.stats.builds;i.rebuild();return true;}
bool NativeCalendarScene::setFeedback(std::optional<P>p,bool pressed,double t,bool reduced){auto&i=*impl_;i.clock(t);if(i.hover==p&&i.pressed==pressed&&i.reduceMotion==reduced)return false;if(i.current)i.feedbackTo(*i.current,p,pressed,t,reduced);i.hover=p;i.pressed=pressed;i.reduceMotion=reduced;i.time=t;return true;}
void NativeCalendarScene::appendCapturedFields(std::span<const DrawObject>draws){auto&i=*impl_;need(i.current&&i.current->plan.bounds==R{0,0,340,310}&&draws.size()==3,"Calendar menu close requires exactly its three retained fields");auto&p=*i.current;need(p.children.size()==p.plan.draws.size(),"Calendar menu field capture is already installed");for(const auto&d:draws)validateDrawObject(d);std::array<DrawObject,3>staged{draws[0],draws[1],draws[2]};for(auto&d:staged)p.children.push_back(std::move(d));p.capturedDirty=true;}
bool NativeCalendarScene::uploadResources(Renderer&r){auto&i=*impl_;bool changed{};if(i.outgoing)changed|=i.outgoing->upload(r,i.options);if(i.current)changed|=i.current->upload(r,i.options);i.rebuild();if(i.posed){const float phase=i.fading?float(core::CubicTiming{.25,.1,.25,1}.value((i.time-i.fadeStart)/.18)):1;if(i.outgoing)i.pose(*i.outgoing,1-phase);if(i.current)i.pose(*i.current,phase);}return changed;}
void NativeCalendarScene::updatePose(const NativeCalendarPose&p){auto&i=*impl_;i.clock(p.time);need(p.world.finite()&&std::isfinite(p.opacity)&&p.opacity>=0&&p.opacity<=1&&p.ownerMasks.size()<=8,"Invalid Calendar projected pose");if(p.shutter)validatePlaneShutter(*p.shutter);i.world=p.world;i.opacity=p.opacity;i.maskCount=p.ownerMasks.size();std::copy(p.ownerMasks.begin(),p.ownerMasks.end(),i.masks.begin());i.shutter=p.shutter;i.time=p.time;i.posed=true;const float phase=i.fading&&!i.reduceMotion?float(core::CubicTiming{.25,.1,.25,1}.value((p.time-i.fadeStart)/.18)):1;if(i.current){i.current->sample(p.time);i.pose(*i.current,phase);}if(i.outgoing)i.pose(*i.outgoing,1-phase);if(i.fading&&phase>=1){i.retire(i.outgoing);i.fading=false;i.rebuild();}++i.stats.poses;}
bool NativeCalendarScene::uploadAnimations(Renderer&r){auto&i=*impl_;bool changed{};if(i.outgoing&&i.outgoing->registered)changed|=r.setNativeGroupDraws(i.outgoing->id,i.outgoing->children);if(i.current&&i.current->registered)changed|=r.setNativeGroupDraws(i.current->id,i.current->children);return changed;}
bool NativeCalendarScene::requiresFrames(double t)const{const auto&i=*impl_;i.clock(t);if(i.fading&&!i.reduceMotion&&t<i.fadeStart+.18)return true;if(i.current&&!i.reduceMotion)for(const auto&pair:i.current->feedback)for(const auto&track:pair)if(track.duration&&track.from!=track.target&&t<track.start+track.duration)return true;return false;}
std::span<const LayerCompositionEntry>NativeCalendarScene::entries()const noexcept{return{impl_->entries.data(),impl_->count};}
bool NativeCalendarScene::collectRetired(Renderer&r){auto&i=*impl_;for(auto it=i.retired.begin();it!=i.retired.end();)if((*it)->release(r))it=i.retired.erase(it);else return false;return true;}
bool NativeCalendarScene::releaseResources(Renderer&r){auto&i=*impl_;bool okay=collectRetired(r);if(i.outgoing)okay=i.outgoing->release(r)&&okay;if(i.current)okay=i.current->release(r)&&okay;if(okay)i.count=0;return okay;}
const modules::CalendarArtwork&NativeCalendarScene::artwork()const noexcept{return impl_->current->art;}
NativeCalendarSceneStats NativeCalendarScene::stats()const noexcept{auto v=impl_->stats;const auto&i=*impl_;v.assets=i.current?i.current->plan.assetCount:0;v.draws=i.current?i.current->plan.draws.size():0;v.outgoingAssets=i.outgoing?i.outgoing->plan.assetCount:0;v.retiredParts=i.retired.size();return v;}
#endif
}
