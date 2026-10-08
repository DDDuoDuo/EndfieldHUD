#include "native/work_mode_scene.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::native {namespace {
using Json=ehud::data::Json;using Matrix=core::Matrix4;namespace m=modules;
constexpr double pi=3.1415926535897932384626433832795;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}Json blank(){return Json::Object{{"bounds",Json::Array{0,0,0,0}},{"children",Json::Array{}}};}
}
struct NativeWorkModeScene::Impl {
    enum Role:unsigned{header,base,ring,clock,phase,controls,configuration,focus,focusText,count};
    struct Track{double from{},target{},start{},duration{};};static double value(Track t,double now){return t.duration>0?t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value((now-t.start)/t.duration):t.target;}
    struct Part {
        m::WorkModePart plan;LayerScene scene;std::unique_ptr<NativeLayerGroup>group;core::Rect coverage;bool groupUploaded{},ringUploaded{};
        std::vector<LayerPlacement>placements,active;std::vector<Track>feedback;std::array<PlaneMask,8>masks{},outerMasks{};Matrix outerWorld;float outerOpacity{};std::size_t outerMaskCount{};std::optional<PlaneShutter>outerShutter;
        Part(LayerRasterizer&r,m::WorkModePart p,const LayerRasterOptions&o,bool grouped,std::string groupID):plan(std::move(p)),scene(r),placements(plan.surfaces.size()),feedback(plan.surfaces.size()){
            scene.load(plan.layers,o);need(scene.report().unsupported.empty(),"Unsupported original Work Mode artwork");active.reserve(placements.size());
            for(std::size_t n=0;n<placements.size();++n){const auto&s=plan.surfaces[n];placements[n]={scene.surfaceIndex(s.id).value_or(std::size_t(-1)),s.local,s.opacity,{}};feedback[n].from=feedback[n].target=s.rim?.28:0;if(placements[n].surface!=std::size_t(-1))active.push_back(placements[n]);}scene.setPlacements(active);scene.prepareDraws();
            if(grouped){group=std::make_unique<NativeLayerGroup>(scene,std::move(groupID),o.pixelsPerPoint);coverage=group->localCoverageBounds();}
        }
        bool release(Renderer&r){if(group)return group->releaseResources(r);const bool released=scene.releaseResources(r);if(released)ringUploaded=false;return released;}
    };
    m::WorkModePresentation&state;LayerRasterizer&raster;LayerRasterOptions options;m::WorkModeAppearance appearance;std::array<std::unique_ptr<Part>,count>parts;std::vector<std::unique_ptr<Part>>retired;LayerScene ringCarrier;std::array<DrawObject,1>ringDraw;std::vector<LayerCompositionEntry>entries;
    std::uint64_t revision{},clockRevision{};bool dirty{true},pressed{},posed{};std::optional<std::string>hover;double lastTime{};WorkModeSceneStats stats;DrawObject validation;
    Impl(m::WorkModePresentation&s,LayerRasterizer&r,LayerRasterOptions o,m::WorkModeAppearance a):state(s),raster(r),options(std::move(o)),appearance(a),ringCarrier(r){ringCarrier.load(blank(),options);retired.reserve(count);entries.reserve(count);validation.sourceID="work-mode-pose";validation.masks.reserve(8);ringDraw[0].masks.reserve(8);}
    void time(double now)const{need(std::isfinite(now)&&now>=lastTime,"Work Mode needs monotonic owner time");}
    template<class F>void each(F&&f){for(auto&p:parts)if(p)f(*p);}
    void refreshEntries(){entries.clear();for(unsigned role=0;role<count;++role){auto&p=parts[role];if(!p||(role==focusText&&p->plan.surfaces.empty()))continue;if(role==ring)entries.push_back({&ringCarrier,ringDraw});else if(p->group){if(p->groupUploaded)entries.push_back(p->group->entry());}else entries.push_back({&p->scene,{}});}}
};
NativeWorkModeScene::NativeWorkModeScene(m::WorkModePresentation&s,LayerRasterizer&r,LayerRasterOptions o,m::WorkModeAppearance a):impl_(std::make_unique<Impl>(s,r,std::move(o),a)){}
NativeWorkModeScene::~NativeWorkModeScene()=default;
void NativeWorkModeScene::setAppearance(m::WorkModeAppearance a){if(a==impl_->appearance)return;impl_->appearance=a;impl_->dirty=true;}
bool NativeWorkModeScene::syncContent(double now){auto&i=*impl_;i.time(now);if(!i.dirty&&i.revision==i.state.revision()){
    if(i.clockRevision==i.state.clockRevision())return false;auto&p=*i.parts[Impl::clock];auto node=p.plan.layers["children"].array()[0];node["text"]["string"]=i.state.timeText();p.scene.updateLocalContent("workMode.clock",i.state.clockRevision(),node,i.options);p.plan.layers["children"]=Json::Array{std::move(node)};i.clockRevision=i.state.clockRevision();++i.stats.clockRasters;return true;
 }
 need(i.retired.empty(),"Publish prior Work Mode generation before replacing artwork");auto art=m::prepareWorkModeArtwork(i.state,i.appearance);m::WorkModePart focusCaption;focusCaption.layers=blank();
 // The source opacity feedback belongs to the permission CAShapeLayer and its
 // highlight children only. Its separate caption never participates in fade.
 if(i.state.focusPermission()){Json::Array captionChildren;auto children=art.focus.layers["children"].array();for(std::size_t n=0;n<art.focus.surfaces.size();){if(art.focus.surfaces[n].id=="workMode.focusStatus"){captionChildren.push_back(std::move(children[n]));focusCaption.surfaces.push_back(std::move(art.focus.surfaces[n]));children.erase(children.begin()+static_cast<std::ptrdiff_t>(n));art.focus.surfaces.erase(art.focus.surfaces.begin()+static_cast<std::ptrdiff_t>(n));}else ++n;}focusCaption.layers["children"]=std::move(captionChildren);art.focus.layers["children"]=std::move(children);}
 std::array<m::WorkModePart*,Impl::count>plans{&art.header,&art.ringBase,&art.ring,&art.clock,&art.phase,&art.controls,&art.configuration,&art.focus,&focusCaption};std::array<std::unique_ptr<Impl::Part>,Impl::count>next;std::size_t needed{};
 for(unsigned n=0;n<Impl::count;++n)if(!i.parts[n]||i.parts[n]->plan.layers!=plans[n]->layers)needed+=plans[n]->surfaces.size();need(i.raster.stats().entries<=LayerRasterizer::maximumEntries&&needed<=LayerRasterizer::maximumEntries-i.raster.stats().entries,"Work Mode generation exceeds shared raster budget");
 for(unsigned n=0;n<Impl::count;++n)if(!i.parts[n]||i.parts[n]->plan.layers!=plans[n]->layers){next[n]=std::make_unique<Impl::Part>(i.raster,std::move(*plans[n]),i.options,n==Impl::configuration||(n==Impl::focus&&i.state.focusPermission()),n==Impl::configuration?"workMode.configuration":"workMode.focusAccess");if(i.parts[n])for(std::size_t k=0;k<next[n]->plan.surfaces.size();++k)for(std::size_t old=0;old<i.parts[n]->plan.surfaces.size();++old)if(next[n]->plan.surfaces[k].id==i.parts[n]->plan.surfaces[old].id)next[n]->feedback[k]=i.parts[n]->feedback[old];}
 // All candidate rasterization/allocation succeeds before replacing borrowed
 // generation pointers. Old assets retire only after owner's new publication.
 bool ringChanged{};for(unsigned n=0;n<Impl::count;++n)if(next[n]){ringChanged|=n==Impl::ring;if(i.parts[n])i.retired.push_back(std::move(i.parts[n]));i.parts[n]=std::move(next[n]);++i.stats.builds;}
 if(ringChanged){need(i.parts[Impl::ring]->scene.draws().size()==1,"Original Work Mode ring requires one retained surface");auto draw=i.parts[Impl::ring]->scene.draws()[0];draw.masks.reserve(8);i.ringDraw[0]=std::move(draw);}
 i.revision=i.state.revision();i.clockRevision=i.state.clockRevision();i.dirty=false;i.lastTime=now;i.refreshEntries();return true;
}
bool NativeWorkModeScene::setFeedback(std::optional<std::string_view>action,bool pressed,double now){auto&i=*impl_;i.time(now);if(i.hover.has_value()==action.has_value()&&(!action||*i.hover==*action)&&i.pressed==pressed&&!i.state.reducedMotion())return false;
 i.each([&](auto&p){for(std::size_t n=0;n<p.plan.surfaces.size();++n){const auto&s=p.plan.surfaces[n];if(!s.tint&&!s.rim)continue;const bool hit=action&&*action==s.action;const double target=s.rim?(hit?1:.28):(hit?(pressed?1:.62):0);auto&t=p.feedback[n];if(t.target!=target||(i.state.reducedMotion()&&t.duration)){t={Impl::value(t,now),target,now,i.state.reducedMotion()?0:pressed&&hit?.06:.14};}}});i.hover=action?std::optional(std::string(*action)):std::nullopt;i.pressed=pressed;i.lastTime=now;return true;
}
void NativeWorkModeScene::updatePose(const NativeWorkModePose&pose){auto&i=*impl_;i.time(pose.time);need(!i.dirty&&i.revision==i.state.revision()&&i.clockRevision==i.state.clockRevision(),"Synchronize Work Mode before pose");need(pose.world.finite()&&std::isfinite(pose.opacity)&&pose.opacity>=0&&pose.opacity<=1&&pose.ownerMasks.size()<=7,"Invalid Work Mode pose");if(pose.shutter)validatePlaneShutter(*pose.shutter);
 const auto layout=i.state.layout(pose.time);const auto feedback=i.state.feedback(pose.time);const auto ring=i.state.ring(pose.time);const Matrix clockWorld=pose.world*Matrix::translation(220,layout.clockY)*Matrix::scale(layout.clockScale,layout.clockScale)*Matrix::translation(-172,-38.5);const auto clockInverse=core::source::inverseSourceMatrix(clockWorld);const Matrix ringWorld=pose.world*Matrix::translation(220,220)*Matrix::rotation(0,0,ring.rotation)*Matrix::translation(-220,-220);const auto ringInverse=core::source::inverseSourceMatrix(ringWorld);
 for(unsigned role=0;role<Impl::count;++role){auto&p=*i.parts[role];std::copy(pose.ownerMasks.begin(),pose.ownerMasks.end(),p.masks.begin());std::size_t maskCount=pose.ownerMasks.size();if(role==Impl::clock)p.masks[maskCount++]={clockInverse,{0,0,344,77}};if(p.group)maskCount=0;
  Matrix world=p.group?Matrix{}:role==Impl::clock?clockWorld*feedback.clock:role==Impl::phase?pose.world*Matrix::translation(89,layout.phaseY-15):role==Impl::ring?ringWorld:pose.world;
  for(std::size_t n=0;n<p.placements.size();++n){const auto&s=p.plan.surfaces[n];auto&placement=p.placements[n];Matrix button;
   if(!s.action.empty()&&s.action==feedback.action&&feedback.scale!=1){const auto actions=role==Impl::configuration?i.state.configurationActions():i.state.actions();for(const auto&a:actions)if(a.id==s.action){const auto cx=a.rect.x+a.rect.width*.5,cy=a.rect.y+a.rect.height*.5;button=Matrix::translation(cx,cy)*Matrix::scale(feedback.scale,feedback.scale)*Matrix::translation(-cx,-cy);break;}}
   placement.world=world*button*s.local;placement.opacity=(p.group?1:pose.opacity)*((s.tint||s.rim)?float(Impl::value(p.feedback[n],pose.time)):s.opacity);placement.masks={p.masks.data(),maskCount};i.validation.world=placement.world;i.validation.opacity=placement.opacity;i.validation.masks.assign(placement.masks.begin(),placement.masks.end());i.validation.shutter=p.group?std::nullopt:pose.shutter;validateDrawObject(i.validation);
  }
 }
 // Validate all numeric candidates before touching any retained LayerScene.
 for(unsigned role=0;role<Impl::count;++role){auto&p=*i.parts[role];p.active.clear();for(const auto&v:p.placements)if(v.surface!=std::size_t(-1))p.active.push_back(v);p.scene.setPlacements(p.active);p.scene.setGroupShutter(p.group?std::nullopt:pose.shutter);}
 auto&draw=i.ringDraw[0];draw.world=ringWorld;draw.opacity=pose.opacity;draw.masks.assign(pose.ownerMasks.begin(),pose.ownerMasks.end());draw.shutter=pose.shutter;const auto cut=m::workModeRingCut(ring.fraction);draw.angularMask=AngularMask{ringInverse,{220,220},-.5*pi,ring.fraction==1?2*pi:cut.endAngle+.5*pi,cut.endPlane};
 for(unsigned role=0;role<Impl::count;++role){auto&p=*i.parts[role];if(!p.group)continue;p.outerWorld=pose.world;p.outerOpacity=pose.opacity*float(role==Impl::configuration?layout.configurationOpacity:feedback.focusOpacity);p.outerMaskCount=pose.ownerMasks.size();std::copy(pose.ownerMasks.begin(),pose.ownerMasks.end(),p.outerMasks.begin());p.outerShutter=pose.shutter;if(p.groupUploaded)p.group->setPose(p.outerWorld,p.outerOpacity,std::span(p.outerMasks.data(),p.outerMaskCount),p.outerShutter);}
 i.posed=true;i.lastTime=pose.time;++i.stats.poses;
}
bool NativeWorkModeScene::uploadAnimations(Renderer&r){auto&i=*impl_;need(i.posed,"Place Work Mode before upload");bool changed{};auto&ring=*i.parts[Impl::ring];if(!ring.ringUploaded){ring.scene.uploadResources(r);ring.ringUploaded=true;changed=true;}bool entriesChanged{};for(auto&part:i.parts){auto&p=*part;if(!p.group)continue;if(!p.groupUploaded){changed|=p.group->uploadResources(r,p.coverage);p.groupUploaded=true;entriesChanged=true;}else changed|=p.group->updateLocal(r);p.group->setPose(p.outerWorld,p.outerOpacity,std::span(p.outerMasks.data(),p.outerMaskCount),p.outerShutter);}if(entriesChanged)i.refreshEntries();return changed;}
bool NativeWorkModeScene::requiresFrames(double now)const{const auto&i=*impl_;i.time(now);if(i.state.requiresFrames(now))return true;if(!i.state.active()||i.state.reducedMotion())return false;for(const auto&p:i.parts)if(p)for(const auto&t:p->feedback)if(t.duration>0&&t.from!=t.target&&now<t.start+t.duration)return true;return false;}
std::span<const LayerCompositionEntry>NativeWorkModeScene::entries()const noexcept{return impl_->entries;}
bool NativeWorkModeScene::collectRetired(Renderer&r){for(auto&p:impl_->retired)if(!p->release(r))return false;impl_->retired.clear();return true;}
bool NativeWorkModeScene::releaseResources(Renderer&r){bool result=collectRetired(r);for(auto&p:impl_->parts)if(p)result=p->release(r)&&result;return impl_->ringCarrier.releaseResources(r)&&result;}
WorkModeSceneStats NativeWorkModeScene::stats()const noexcept{auto value=impl_->stats;value.retiredParts=impl_->retired.size();return value;}
}
#endif
