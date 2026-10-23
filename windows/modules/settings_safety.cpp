#include "modules/settings_safety.hpp"
#include <cmath>
#include <stdexcept>
namespace endfield::modules {namespace {
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}void finite(double t){need(std::isfinite(t),"Settings safety needs finite caller time");}
double curve(double t,double cx1,double cy1,double cx2,double cy2){finite(t);if(t<=0)return 0;if(t>=1)return 1;const double x=static_cast<float>(t),x1=static_cast<float>(cx1),x2=static_cast<float>(cx2),y1=static_cast<float>(cy1),y2=static_cast<float>(cy2);const double ax=1-3*x2+3*x1,bx=3*x2-6*x1,cx=3*x1,ay=1-3*y2+3*y1,by=3*y2-6*y1,cy=3*y1;double p=x;for(unsigned n=0;n<8;++n){const double e=((ax*p+bx)*p+cx)*p-x;if(std::abs(e)<1e-5)break;p-=e/((3*ax*p+2*bx)*p+cx);}return static_cast<float>(((ay*p+by)*p+cy)*p);}
}
double settingsSafetyFeedbackProgress(double phase){return curve(phase,.25,.1,.25,1);}
SettingsSafetyColor settingsSafetyInterpolateColor(const SettingsSafetyColor&from,const SettingsSafetyColor&to,double phase){finite(phase);if(from==to)return to;if(phase<=0)return from;if(phase>=1)return to;const auto p=settingsSafetyFeedbackProgress(phase);auto out=from;for(unsigned n=0;n<4;++n)out[n]=n<3?static_cast<float>(from[n]+(to[n]-from[n])*p):from[n]+(to[n]-from[n])*p;return out;}
core::Matrix4 SettingsSafety::centeredSourceTransform(const core::Matrix4&p,double scale){need(p.finite()&&std::isfinite(scale)&&scale>0,"Invalid source confirmation tilt");const auto&m=p.values;const double w=m[3]*500+m[7]*320+m[15];need(std::isfinite(w)&&std::abs(w)>.0001,"Invalid confirmation projection divisor");const double x=(m[0]*500+m[4]*320+m[12])/w,y=(m[1]*500+m[5]*320+m[13])/w;return core::Matrix4::translation(-x,-y)*p*core::Matrix4::translation(500,320)*core::Matrix4::scale(1/scale,1/scale);}
}
