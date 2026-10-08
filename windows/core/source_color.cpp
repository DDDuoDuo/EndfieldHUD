#include "core/source_color.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace endfield::core::source {
namespace {
constexpr double matrix[3][3]={{1.0252632622360232,-.026296374835205007,.0013184544570922774},
 {.019383039425659214,.9479564776870728,.03258408416900635},
 {-.0017835291061401443,-.0014664365493774478,1.0031060531219482}};
constexpr double inverse[3][3]={{.9748443048027375,.027038904111965762,-.0021596178582692837},
 {-.019991393155054746,1.0542932509227578,-.03422053149678339},
 {.0017040541991099759,.0015893422479458478,.9968496979042769}};
void valid(SourceRGB rgb){for(const auto v:rgb)if(!std::isfinite(v)||v<0||v>1)throw std::invalid_argument("Source color must contain finite normalized channels");}
double linear(double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);}
double encoded(double v){return std::clamp(v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055,0.,1.);}
SourceRGB multiply(const double (&m)[3][3],SourceRGB rgb){SourceRGB result{};for(unsigned r=0;r<3;++r)result[r]=m[r][0]*rgb[0]+m[r][1]*rgb[1]+m[r][2]*rgb[2];return result;}
}
SourceRGB genericRGBToSRGB(SourceRGB rgb){valid(rgb);for(auto&v:rgb)v=std::pow(v,461./256.);auto result=multiply(matrix,rgb);for(auto&v:result)v=encoded(v);return result;}
SourceRGB sourceBlackBlend(SourceRGB rgb,double fraction){valid(rgb);if(!std::isfinite(fraction)||fraction<0||fraction>1)throw std::invalid_argument("Invalid source black blend fraction");if(fraction==0)return rgb;for(auto&v:rgb)v=linear(v);auto generic=multiply(inverse,rgb);for(auto&v:generic)v=std::clamp(v,0.,1.)*std::pow(1-fraction,1.8);auto result=multiply(matrix,generic);for(auto&v:result)v=encoded(v);return result;}
SourceRGB selectedCaptionColor(SourceRGB rgb){return sourceBlackBlend(rgb,.5);}
SourceRGBA colorAtWheel(Point p){
 if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>1e9||std::abs(p.y)>1e9)throw std::invalid_argument("Invalid source wheel point");
 const double x=p.x*2-1,y=p.y*2-1,h=std::fmod(std::atan2(y,x)/(2*std::numbers::pi)+1,1)*6,s=std::min(1.,std::hypot(x,y));
 const auto sector=static_cast<int>(h);const auto f=h-sector,a=1-s,b=1-f*s,c=1-(1-f)*s;SourceRGB rgb;
 switch(sector){case 0:rgb={1,c,a};break;case 1:rgb={b,1,a};break;case 2:rgb={a,1,c};break;case 3:rgb={a,b,1};break;case 4:rgb={c,a,1};break;default:rgb={1,a,b};}
 const auto result=genericRGBToSRGB(rgb);return {result[0],result[1],result[2],1};
}
Point colorWheelPoint(SourceRGBA color){const SourceRGB c{color[0],color[1],color[2]};valid(c);if(!std::isfinite(color[3])||color[3]<0||color[3]>1)throw std::invalid_argument("Invalid source color alpha");const auto hi=std::max({c[0],c[1],c[2]}),lo=std::min({c[0],c[1],c[2]}),d=hi-lo;double h{};if(d>0){if(hi==c[0])h=std::fmod((c[1]-c[2])/d+6.,6.)/6;else if(hi==c[1])h=((c[2]-c[0])/d+2)/6;else h=((c[0]-c[1])/d+4)/6;}const auto s=hi>0?d/hi:0;return {(std::cos(h*2*std::numbers::pi)*s+1)*.5,(std::sin(h*2*std::numbers::pi)*s+1)*.5};}
}
