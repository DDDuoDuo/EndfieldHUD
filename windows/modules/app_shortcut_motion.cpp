#include "modules/app_shortcut_motion.hpp"
#include <cmath>
#include <algorithm>
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise,on,push)
#pragma fp_contract(off)
#endif
namespace endfield::modules {namespace {
void finite(double v){if(!std::isfinite(v))throw std::invalid_argument("Nonfinite shortcut animation time");}
double ease(double time,double x1,double y1,double x2,double y2){finite(time);if(time<=0)return 0;if(time>=1)return 1;const double x=static_cast<float>(time);x1=static_cast<float>(x1);y1=static_cast<float>(y1);x2=static_cast<float>(x2);y2=static_cast<float>(y2);const double ax=1-3*x2+3*x1,bx=3*x2-6*x1,cx=3*x1,ay=1-3*y2+3*y1,by=3*y2-6*y1,cy=3*y1;double t=x;for(unsigned n=0;n<8;++n){const auto e=((ax*t+bx)*t+cx)*t-x;if(std::abs(e)<1e-5)break;t-=e/((3*ax*t+2*bx)*t+cx);}return static_cast<float>(((ay*t+by)*t+cy)*t);}
}
double shortcutScreenProgress(double time){return ease(time,.2,.78,.27,1);}
ShortcutScreenSample shortcutScreenSample(const ShortcutScreenTransition&t,double time){finite(time);finite(t.start);if(t.direction!=-1&&t.direction!=1)throw std::invalid_argument("Invalid shortcut transition direction");ShortcutScreenSample out;out.phase=std::clamp((time-t.start)/.26,0.,1.);out.eased=shortcutScreenProgress(out.phase);out.active=t.active&&time<t.start+.26;if(!out.active){out.phase=out.eased=1;return out;}const auto left=1-out.eased;out.incoming=core::Matrix4::translation(26*t.direction*left,0,-34*left)*core::Matrix4::rotation(0,.045*t.direction*left,0);out.outgoing=core::Matrix4::translation(-18*t.direction*out.eased,0,-28*out.eased);return out;}
double shortcutPresetProgress(const ShortcutPresetTransition&t,double time){finite(time);finite(t.start);if(t.direction!=-1&&t.direction!=1)throw std::invalid_argument("Invalid shortcut preset direction");return t.generation?ease((time-t.start)/.18,0,0,.58,1):1;}
core::Matrix4 shortcutPresetTransform(const ShortcutPresetTransition&t,double time){const auto remaining=1-shortcutPresetProgress(t,time);return core::Matrix4::translation(7*t.direction*remaining,0,-18*remaining);}
}
#if defined(_MSC_VER)&&!defined(__clang__)
#pragma float_control(pop)
#endif
