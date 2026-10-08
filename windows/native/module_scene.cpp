#include "native/module_scene.hpp"
#include "core/module_transform.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::native {
namespace {
using core::Matrix4;
void need(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
void gpuMatrix(const Matrix4& m){for(auto v:m.values)need(std::isfinite(v)&&std::abs(v)<=std::numeric_limits<float>::max(),"Module projection exceeds GPU range");}
bool sameRect(const core::MotionRect&a,const core::MotionRect&b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
template<class Path>bool samePath(const Path&a,const Path&b){for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<a[i].size();++j)if(a[i][j].x!=b[i][j].x||a[i][j].y!=b[i][j].y)return false;return true;}
bool sameShutter(const std::optional<PlaneShutter>&a,const std::optional<PlaneShutter>&b){return a.has_value()==b.has_value()&&(!a||(a->worldToLocal==b->worldToLocal&&samePath(a->strips,b->strips)));}
bool sameRegistration(const std::optional<ModuleRegistrationPlacement>&a,const std::optional<ModuleRegistrationPlacement>&b){
    if(a.has_value()!=b.has_value())return false;if(!a)return true;
    return a->world==b->world&&a->parentOpacity==b->parentOpacity&&samePath(a->local.path,b->local.path)&&
        a->local.opacity==b->local.opacity&&a->local.white==b->local.white&&a->local.colorAlpha==b->local.colorAlpha&&a->local.lineWidth==b->local.lineWidth&&
        a->hostClip.worldToLocal==b->hostClip.worldToLocal&&a->hostClip.bounds==b->hostClip.bounds&&sameShutter(a->shutter,b->shutter);
}
bool samePose(const ModuleSurfacePose&a,const ModuleSurfacePose&b){return a.surfaceIdentity==b.surfaceIdentity&&a.hostWorld==b.hostWorld&&a.wrapperWorld==b.wrapperWorld&&a.contentWorld==b.contentWorld&&
    a.opacity==b.opacity&&sameShutter(a.shutter,b.shutter)&&sameRegistration(a.registration,b.registration);}
void validMask(const PlaneMask& mask){
    gpuMatrix(mask.worldToLocal);const auto&r=mask.bounds;
    need(std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>0&&r.height>0,"Invalid retained module content mask");
    need(std::isfinite(mask.cornerRadius)&&mask.cornerRadius>=0&&mask.cornerRadius<=std::min(r.width,r.height)*.5,"Invalid retained module corner radius");
    for(auto v:{r.x,r.y,r.x+r.width,r.y+r.height})need(std::isfinite(v)&&std::abs(v)<=std::numeric_limits<float>::max(),"Module mask bounds exceed GPU range");
}
void validRegistration(const core::ModulePresentationRegistration& r){
    for(auto v:{r.opacity,r.white,r.colorAlpha})need(std::isfinite(v)&&v>=0&&v<=1,"Invalid module registration opacity/color");
    need(std::isfinite(r.lineWidth)&&r.lineWidth>0&&r.lineWidth<=std::numeric_limits<float>::max(),"Invalid module registration stroke width");
    for(const auto&strip:r.path)for(const auto&p:strip)need(std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=std::numeric_limits<float>::max()&&std::abs(p.y)<=std::numeric_limits<float>::max(),"Invalid module registration path");
}
}
NativeModuleSurface::NativeModuleSurface(LayerScene&scene,core::Module module):scene_(&scene),module_(module){(void)core::moduleIdentifier(module);rebindLocalContent();}
void NativeModuleSurface::rebindLocalContent(){
    need(!bound_||scene_->contentRevision()!=sceneRevision_,"Module base poses require a freshly loaded structural revision");
    const auto draws=scene_->draws();std::vector<Base> next;next.reserve(draws.size());
    std::vector<std::array<PlaneMask,8>> masks(draws.size());std::vector<LayerPlacement> placements(draws.size());
    for(std::size_t i=0;i<draws.size();++i){const auto&draw=draws[i];gpuMatrix(draw.world);
        need(!draw.shutter&&draw.masks.size()<=7&&std::isfinite(draw.opacity)&&draw.opacity>=0&&draw.opacity<=1,"Module local content must reserve one host mask and contain no projected shutter");
        Base base;base.world=draw.world;base.opacity=draw.opacity;base.maskCount=draw.masks.size();
        for(std::size_t j=0;j<base.maskCount;++j){validMask(draw.masks[j]);base.masks[j]=draw.masks[j];}next.push_back(base);
        placements[i].surface=i;placements[i].masks=std::span(masks[i].data(),base.maskCount+1);
    }
    base_=std::move(next);masks_=std::move(masks);placements_=std::move(placements);
    sceneRevision_=scene_->contentRevision();bound_=true;initialized_=false;pose_={};++stats_.bindings;
}
bool NativeModuleSurface::update(const Matrix4&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSurface&surface,float canvasOpacity){
    need(bound_&&scene_->contentRevision()==sceneRevision_&&scene_->draws().size()==base_.size(),"Rebind module local content after structural LayerScene reload");
    need(surface.module==module_&&sameRect(surface.contentFrame,core::moduleLocalContentFrame(module_)),"Module surface/frame differs from its retained local content");
    need(std::isfinite(surface.opacity)&&surface.opacity>=0&&surface.opacity<=1&&std::isfinite(canvasOpacity)&&canvasOpacity>=0&&canvasOpacity<=1,"Invalid module presentation opacity");
    (void)core::moduleIdentifier(settings.module);
    const auto layout=core::source::DesktopChromeLayout::make(settings,center,{});
    const auto prefix=center*Matrix4::translation(500,layout.reportCenterY)*Matrix4::scale(layout.reportScale,layout.reportScale);
    const auto transform=core::sampleModuleTransform(surface.transform);
    ModuleSurfacePose next;next.surfaceIdentity=surface.identity;next.opacity=static_cast<float>(surface.opacity*canvasOpacity);
    next.hostWorld=prefix*Matrix4::translation(-220,-220);
    next.wrapperWorld=prefix*transform*Matrix4::translation(-220,-220);
    next.contentWorld=prefix*transform*Matrix4::translation(surface.contentFrame.x-220,surface.contentFrame.y-220);
    gpuMatrix(next.hostWorld);gpuMatrix(next.wrapperWorld);gpuMatrix(next.contentWorld);
    next.hostClip={core::source::inverseSourceMatrix(next.hostWorld),{0,0,440,440}};validMask(next.hostClip);
    const auto inverseContent=core::source::inverseSourceMatrix(next.contentWorld);gpuMatrix(inverseContent);
    if(surface.shutter){next.shutter=PlaneShutter{core::source::inverseSourceMatrix(next.wrapperWorld),*surface.shutter};validatePlaneShutter(*next.shutter);}
    if(surface.registration){validRegistration(*surface.registration);next.registration=ModuleRegistrationPlacement{*surface.registration,next.wrapperWorld,next.hostClip,next.shutter,next.opacity};}
    if(initialized_&&samePose(pose_,next)){++stats_.updates;++stats_.unchangedUpdates;return false;}
    // Stage every transformed child and ancestor mask before touching LayerScene.
    // These vectors have fixed capacity from the last explicit local rebind.
    for(std::size_t i=0;i<base_.size();++i){const auto&base=base_[i];auto&placement=placements_[i];
        placement.world=next.contentWorld*base.world;gpuMatrix(placement.world);placement.opacity=base.opacity*next.opacity;
        for(std::size_t j=0;j<base.maskCount;++j){masks_[i][j]=base.masks[j];masks_[i][j].worldToLocal=base.masks[j].worldToLocal*inverseContent;validMask(masks_[i][j]);}
        masks_[i][base.maskCount]=next.hostClip;
    }
    ++stats_.updates;
    scene_->setPlacements(placements_);scene_->setGroupShutter(next.shutter);
    pose_=std::move(next);initialized_=true;++stats_.placementUpdates;return true;
}
} // namespace endfield::native
