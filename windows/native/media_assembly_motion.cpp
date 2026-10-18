#include "native/media_assembly_scene.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <cmath>
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise,on,push)
#pragma fp_contract(off)
#endif
namespace endfield::native {namespace {
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}
void finite(double v){need(std::isfinite(v),"Nonfinite Media Assembly animation time");}
// Observed source CA easeOut: bounded Float clock/control/result conversion.
double easeOut(double value){finite(value);if(value<=0)return 0;if(value>=1)return 1;const double x=float(value),x2=float(.58),a=1-3*x2,b=3*x2;double t=x;for(unsigned n=0;n<8;++n){const auto e=(a*t+b)*t*t-x;if(std::abs(e)<1e-5)break;t-=e/((3*a*t+2*b)*t);}return float((3-2*t)*t*t);}
double linear(double t){finite(t);return float(std::clamp(t,0.,1.));}
}

void MediaAssemblyMotion::revealDrawer(double time,bool animated){finite(time);drawerStart_=animated?std::optional(time):std::nullopt;}
void MediaAssemblyMotion::scrollInline(double offset,double time,bool animated){finite(offset);finite(time);need(offset>=0,"Negative source inline offset");const double previous=inlineOffset(time);scrollFrom_=previous;scrollTo_=offset;scrollStart_=animated&&previous!=offset?std::optional(time):std::nullopt;}
void MediaAssemblyMotion::closeMedia(double time,bool animated){finite(time);closeStart_=animated?std::optional(time):std::nullopt;}
void MediaAssemblyMotion::settle()noexcept{drawerStart_.reset();scrollStart_.reset();closeStart_.reset();scrollFrom_=scrollTo_;}
double MediaAssemblyMotion::drawerOpacity(double time)const{finite(time);return drawerStart_?linear((time-*drawerStart_)/.16):1;}
double MediaAssemblyMotion::inlineOffset(double time)const{finite(time);return scrollStart_?scrollFrom_+(scrollTo_-scrollFrom_)*easeOut((time-*scrollStart_)/.08):scrollTo_;}
double MediaAssemblyMotion::closeOpacity(double time)const{finite(time);return closeStart_?float(1-linear((time-*closeStart_)/.16)):0;}
bool MediaAssemblyMotion::requiresFrames(double time)const{finite(time);return (drawerStart_&&time<*drawerStart_+.16)||(scrollStart_&&time<*scrollStart_+.08)||(closeStart_&&time<*closeStart_+.16);}
}
#if defined(_MSC_VER)&&!defined(__clang__)
#pragma float_control(pop)
#endif
