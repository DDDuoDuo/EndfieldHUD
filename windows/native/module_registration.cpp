#include "native/module_registration.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::native {
namespace {
void need(bool condition,const char*message){if(!condition)throw std::invalid_argument(message);}
bool samePath(const core::RegistrationPath&a,const core::RegistrationPath&b){for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<a[i].size();++j)if(a[i][j].x!=b[i][j].x||a[i][j].y!=b[i][j].y)return false;return true;}
float sourceWhite(double white){
    // NSColor(white:alpha:).cgColor -> extendedLinearSRGB, sampled directly on
    // macOS15.7.4 (24G517); source is GenericGrayGamma2_2. Keep source palette
    // conversion at the material boundary, never treat encoded white as linear.
    if(white==.77)return .55422168970108032f;
    if(white==.22)return .039681904017925262f;
    throw std::invalid_argument("Registration white is outside the original module palette");
}
}
ModuleRegistrationMesh moduleRegistrationMesh(const core::RegistrationPath&path,double width){
    need(std::isfinite(width)&&width>0&&width<=1000,"Invalid source registration width");
    ModuleRegistrationMesh result;const auto half=width*.5;
    for(std::size_t strip=0;strip<path.size();++strip){const auto&p=path[strip];
        for(const auto&v:p)need(std::isfinite(v.x)&&std::isfinite(v.y)&&std::abs(v.x)<=1e8&&std::abs(v.y)<=1e8,"Invalid source registration point");
        const auto mx=p[2].x-p[1].x,my=p[2].y-p[1].y;
        need((mx==0)!=(my==0),"Registration middle segment must be axis-aligned and nonempty");
        const auto length=std::abs(mx)+std::abs(my);need(length>width,"Registration width exceeds source bracket height");
        const core::MotionPoint v{mx==0?0.:std::copysign(1.,mx),my==0?0.:std::copysign(1.,my)};
        const auto dx=p[1].x-p[0].x,dy=p[1].y-p[0].y;const auto cap=std::abs(dx)+std::abs(dy);
        need((dx==0||dy==0)&&dx*v.x+dy*v.y==0&&p[3].x==p[2].x-dx&&p[3].y==p[2].y-dy,"Unsupported source registration bracket topology");
        need(cap<=4.000001,"Registration cap exceeds original four-point source stroke");
        const core::MotionPoint u=cap>0?core::MotionPoint{dx/cap,dy/cap}:core::MotionPoint{-v.y,v.x};
        // Union of the three stroked segments plus their two outer miter
        // corners, partitioned into disjoint rectangles. Short cap segments
        // do not accidentally extend the inner side beyond their butt endpoint.
        const double extension=std::min(-half,-cap),capHeight=cap>0?half:0;
        const std::array<core::Rect,5> rectangles{{
            {-half,0,width,length},
            {-cap,-capHeight,cap+half,capHeight},
            {-cap,length,cap+half,capHeight},
            {extension,0,-half-extension,half},
            {extension,length-half,-half-extension,half}
        }};
        for(std::size_t q=0;q<rectangles.size();++q){const auto&r=rectangles[q];const auto first=(strip*5+q)*4;
            const std::array<core::MotionPoint,4> corners{{{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}}};
            for(std::size_t c=0;c<4;++c){const auto local=corners[c];auto&vertex=result.vertices[first+c];
                vertex.position={static_cast<float>(p[1].x+u.x*local.x+v.x*local.y),static_cast<float>(p[1].y+u.y*local.x+v.y*local.y),0};}
            const auto index=(strip*5+q)*6;const auto base=static_cast<std::uint32_t>(first);
            result.indices[index]=base;result.indices[index+1]=base+1;result.indices[index+2]=base+2;
            result.indices[index+3]=base;result.indices[index+4]=base+2;result.indices[index+5]=base+3;
        }
    }
    return result;
}
NativeModuleRegistration::NativeModuleRegistration(std::string sourceID){
    need(!sourceID.empty()&&sourceID.size()<=490,"Invalid source registration resource ID");
    auto&draw=draws_[0];draw.sourceID=std::move(sourceID);draw.meshID=draw.sourceID+".mesh";draw.opacity=0;draw.masks.reserve(1);
    // Valid fixed degenerate topology permits initial inactive composition.
    for(std::size_t q=0;q<30;++q){const auto v=static_cast<std::uint32_t>(q*4);const auto i=q*6;
        mesh_.indices[i]=v;mesh_.indices[i+1]=v+1;mesh_.indices[i+2]=v+2;mesh_.indices[i+3]=v;mesh_.indices[i+4]=v+2;mesh_.indices[i+5]=v+3;}
}
bool NativeModuleRegistration::update(const std::optional<ModuleRegistrationPlacement>&placement){
    auto&draw=draws_[0];
    if(!placement){draw.opacity=0;draw.shutter.reset();draw.masks.clear();++stats_.numericUpdates;return false;}
    const auto&p=*placement;const auto&local=p.local;
    need(std::isfinite(p.parentOpacity)&&p.parentOpacity>=0&&p.parentOpacity<=1&&std::isfinite(local.opacity)&&local.opacity>=0&&local.opacity<=1&&std::isfinite(local.colorAlpha)&&local.colorAlpha>=0&&local.colorAlpha<=1,"Invalid registration opacity");
    const auto white=sourceWhite(local.white);
    const bool changed=!previousPath_||previousWidth_!=local.lineWidth||!samePath(*previousPath_,local.path);
    std::optional<ModuleRegistrationMesh> next;if(changed)next=moduleRegistrationMesh(local.path,local.lineWidth);
    // Validate complete numeric state without copying IDs or allocating masks.
    // Invalid input leaves live geometry and constants unchanged. The shared
    // renderer validator covers the fixed shutter's range and convexity.
    const auto opacity=static_cast<float>(p.parentOpacity*local.opacity);
    const std::array<float,4> tint{white,white,white,static_cast<float>(local.colorAlpha)};
    for(auto value:p.world.values)need(std::isfinite(value)&&std::abs(value)<=std::numeric_limits<float>::max(),"Invalid registration world");
    for(auto value:p.hostClip.worldToLocal.values)need(std::isfinite(value)&&std::abs(value)<=std::numeric_limits<float>::max(),"Invalid registration clip transform");
    const auto&r=p.hostClip.bounds;need(std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>0&&r.height>0,"Invalid registration clip bounds");
    need(std::isfinite(p.hostClip.cornerRadius)&&p.hostClip.cornerRadius>=0&&p.hostClip.cornerRadius<=std::min(r.width,r.height)*.5,"Invalid registration clip corner radius");
    for(auto value:{r.x,r.y,r.x+r.width,r.y+r.height})need(std::isfinite(value)&&std::abs(value)<=std::numeric_limits<float>::max(),"Registration clip exceeds GPU range");
    if(p.shutter)validatePlaneShutter(*p.shutter);
    if(changed){mesh_=std::move(*next);previousPath_=local.path;previousWidth_=local.lineWidth;++stats_.geometryRevision;++stats_.geometryUpdates;}
    draw.world=p.world;draw.opacity=opacity;draw.linearTint=tint;draw.masks.resize(1);draw.masks[0]=p.hostClip;draw.shutter=p.shutter;++stats_.numericUpdates;
    return changed;
}
bool NativeModuleRegistration::uploadGeometry(Renderer&renderer){
    need(!resourceOwner_||resourceOwner_==&renderer,"Registration mesh belongs to another renderer");
    need(renderer.stats().initialized,"Registration mesh requires an initialized renderer");
    // Renderer accepts an owning ID. Check the retained revision before that
    // call so long source IDs do not allocate on every unchanged HUD frame.
    if(resourceOwner_&&uploadedGeometryRevision_==stats_.geometryRevision)return false;
    const bool uploaded=renderer.setMesh(draws_[0].meshID,stats_.geometryRevision,{mesh_.vertices,mesh_.indices});
    resourceOwner_=&renderer;uploadedGeometryRevision_=stats_.geometryRevision;if(uploaded)++stats_.meshUploads;return uploaded;
}
bool NativeModuleRegistration::releaseResources(Renderer&renderer){
    need(!resourceOwner_||resourceOwner_==&renderer,"Registration resources belong to another renderer");
    if(!resourceOwner_)return true;
    if(!renderer.stats().initialized||renderer.removeMesh(draws_[0].meshID)){resourceOwner_=nullptr;uploadedGeometryRevision_=0;return true;}return false;
}
} // namespace endfield::native
