#include "core/module_transform.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::core {
namespace {
bool same(const ModuleOffset&a,const ModuleOffset&b) noexcept {
    return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.rotationX==b.rotationX&&
           a.rotationY==b.rotationY&&a.perspective==b.perspective;
}
bool authored(const ModuleOffset& offset,bool incoming) {
    for(const auto direction: {MotionPoint{-1,0},MotionPoint{1,0},MotionPoint{0,-1},MotionPoint{0,1}})
        if(same(offset,incoming?ModuleTransitionStyle::incomingOffset(direction):
                               ModuleTransitionStyle::outgoingOffset(direction)))return true;
    return false;
}
Matrix4 compose(const ModuleOffset& o,double amount) {
    if(amount==0)return {};
    const auto x=o.x*amount,y=o.y*amount,z=o.z*amount,p=o.perspective*amount;
    const auto rx=o.rotationX*amount,ry=o.rotationY*amount;
    const auto sx=std::sin(rx),cx=std::cos(rx),sy=std::sin(ry),cy=std::cos(ry);
    // The original source first sets m34, then Translate, RotateX, RotateY.
    // Column-major Matrix4 storage is the same flattened order as CATransform3D.
    return {{cy,sx*sy,-cx*sy,-p*cx*sy,
             0,cx,sx,p*sx,
             sy,-sx*cy,cx*cy,p*cx*cy,
             x,y,z,1+p*z}};
}
}
double moduleCoreAnimationProgress(double time) {
    if(!std::isfinite(time))throw std::invalid_argument("Nonfinite module transform time");
    if(time<=0)return 0;
    if(time>=1)return 1;
    // Preserve the original CAMediaTimingFunction's Float parameters and the
    // measured Float input/output boundary. Arithmetic inside inversion is
    // Double; tests compare genuine CA samples, not a second copy of this code.
    const double x=static_cast<float>(time);
    constexpr double x1=static_cast<float>(.18),y1=static_cast<float>(.72);
    constexpr double x2=static_cast<float>(.26),y2=1;
    constexpr double ax=1-3*x2+3*x1,bx=3*x2-6*x1,cx=3*x1;
    constexpr double ay=1-3*y2+3*y1,by=3*y2-6*y1,cy=3*y1;
    auto t=x;
    // This source curve has a strictly positive derivative across [0,1]; no
    // generic zero-derivative fallback is needed or implied by this API.
    for(unsigned i=0;i<8;++i){
        const auto error=((ax*t+bx)*t+cx)*t-x;
        if(std::abs(error)<1e-5)break;
        t-=error/((3*ax*t+2*bx)*t+cx);
    }
    return static_cast<float>(((ay*t+by)*t+cy)*t);
}
Matrix4 sampleModuleTransform(const ModulePresentationTransform& track) {
    if(!std::isfinite(track.easedProgress)||track.easedProgress<0||track.easedProgress>1)
        throw std::invalid_argument("Invalid module transform progress");
    const bool fromIdentity=same(track.from,{}),toIdentity=same(track.to,{});
    if(fromIdentity&&toIdentity)return {};
    if(toIdentity&&authored(track.from,true))
        return compose(track.from,track.animated?1-track.easedProgress:0);
    if(fromIdentity&&authored(track.to,false))
        return compose(track.to,track.animated?track.easedProgress:1);
    throw std::invalid_argument("Unsupported module transform endpoints");
}
} // namespace endfield::core
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
