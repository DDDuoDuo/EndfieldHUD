#include "core/subsection_transition.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise,on,push)
#pragma fp_contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::core {
namespace {
void finite(double value){if(!std::isfinite(value))throw std::invalid_argument("Nonfinite subsection input");}
}
double subsectionCoreAnimationProgress(double time){
    finite(time);if(time<=0)return 0;if(time>=1)return 1;
    const double x=static_cast<float>(time);
    constexpr double x1=static_cast<float>(.16),y1=static_cast<float>(.78);
    constexpr double x2=static_cast<float>(.25),y2=1;
    constexpr double ax=1-3*x2+3*x1,bx=3*x2-6*x1,cx=3*x1;
    constexpr double ay=1-3*y2+3*y1,by=3*y2-6*y1,cy=3*y1;
    auto t=x;
    // This exact source curve has strictly positive derivative. The observed
    // CA inversion tolerance/input/control/output boundaries match the original
    // module oracle; no generic timing-function solver is implied.
    for(unsigned i=0;i<8;++i){const auto error=((ax*t+bx)*t+cx)*t-x;
        if(std::abs(error)<1e-5)break;
        t-=error/((3*ax*t+2*bx)*t+cx);
    }
    return static_cast<float>(((ay*t+by)*t+cy)*t);
}
SubsectionTransformSample sampleSubsectionTransform(double direction,double elapsed,bool animated){
    finite(direction);finite(elapsed);SubsectionTransformSample result;
    if(!animated||elapsed>=SubsectionTransitionStyle::duration)return result;
    const auto remaining=1-subsectionCoreAnimationProgress(elapsed/SubsectionTransitionStyle::duration);
    const double z=-16*remaining,p=(-1./720)*remaining;
    result.sublayerTransform.values[11]=p;
    result.sublayerTransform.values[12]=(direction<0?-18:18)*remaining;
    result.sublayerTransform.values[14]=z;
    result.sublayerTransform.values[15]=1+p*z;
    result.active=true;return result;
}
SubsectionMaskPath subsectionMaskKeyframe(Rect viewport,double direction,std::size_t keyframe){
    finite(direction);finite(viewport.x);finite(viewport.y);finite(viewport.width);finite(viewport.height);
    finite(viewport.x+viewport.width);finite(viewport.y+viewport.height);
    if(viewport.width<=0||viewport.height<=0||keyframe>=4)throw std::invalid_argument("Unsupported subsection viewport/keyframe");
    constexpr std::array<double,4> lags{.06,.18,0,.12};
    const auto progress=SubsectionTransitionStyle::maskProgress[keyframe],height=viewport.height/4;
    SubsectionMaskPath result;
    for(std::size_t i=0;i<4;++i){
        const auto width=viewport.width*std::min(1.,std::max(0.,progress*1.2-lags[i]));
        const auto cut=std::min(5.,width*.12)*(1-progress),y=viewport.y+static_cast<double>(i)*height;
        const auto left=direction<0?viewport.x:viewport.x+viewport.width-width,right=left+width;
        result[i]={MotionPoint{left+cut,y},{right,y},{right,y+height-cut},{right-cut,y+height},{left,y+height},{left,y+cut}};
    }return result;
}
} // namespace endfield::core
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
