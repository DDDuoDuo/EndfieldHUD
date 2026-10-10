

#include "modules/media_assembly_processor.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise,on,push)
#pragma fp_contract(off)
#endif

namespace endfield::modules {
namespace {
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
using M3=std::array<double,9>;
M3 multiply(const M3&a,const M3&b){M3 r{};for(int i=0;i<3;++i)for(int j=0;j<3;++j){double s=0;for(int k=0;k<3;++k)s+=a[i*3+k]*b[k*3+j];r[i*3+j]=s;}return r;}
M3 inverse(const M3&m){
    const double a=m[0],b=m[1],c=m[2],d=m[3],e=m[4],f=m[5],g=m[6],h=m[7],i=m[8];
    const double A=e*i-f*h,B=-(d*i-f*g),C=d*h-e*g,det=a*A+b*B+c*C;need(std::abs(det)>1e-12,"Singular Media Assembly colour matrix");
    return {A/det,-(b*i-c*h)/det,(b*f-c*e)/det,B/det,(a*i-c*g)/det,-(a*f-c*d)/det,C/det,-(a*h-b*g)/det,(a*e-b*d)/det};
}
std::array<double,3>transform3(const M3&m,std::array<double,3>v){return {m[0]*v[0]+m[1]*v[1]+m[2]*v[2],m[3]*v[0]+m[4]*v[1]+m[5]*v[2],m[6]*v[0]+m[7]*v[1]+m[8]*v[2]};}
std::array<double,3>xyz(std::array<double,2>xy){return {xy[0]/xy[1],1.,(1-xy[0]-xy[1])/xy[1]};}
// Bradford cone response.
constexpr M3 bradford{0.8951,0.2664,-0.1614,-0.7502,1.7135,0.0367,0.0389,-0.0685,1.0296};
// Generic RGB primaries with a D65 white: the colour basis the original
// CITemperatureAndTint applies to its (working-space) values.
M3 genericRGB(){
    const std::array<std::array<double,2>,3>primaries{{{.63,.34},{.295,.605},{.155,.077}}};
    M3 p{};for(int c=0;c<3;++c){const auto v=xyz(primaries[c]);for(int r=0;r<3;++r)p[r*3+c]=v[r];}
    const auto white=xyz({.3127,.3290});const auto s=transform3(inverse(p),white);
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)p[r*3+c]*=s[c];return p;
}
// Robertson isotherms (Wyszecki & Stiles). The original retains the
// uncorrected 325 mired u value 0.24702 (measured exactly against Core Image).
struct Isotherm {double r,u,v,t;};
constexpr std::array<Isotherm,31>isotherms{{
    {0,.18006,.26352,-.24341},{10,.18066,.26589,-.25479},{20,.18133,.26846,-.26876},{30,.18208,.27119,-.28539},
    {40,.18293,.27407,-.30470},{50,.18388,.27709,-.32675},{60,.18494,.28021,-.35156},{70,.18611,.28342,-.37915},
    {80,.18740,.28668,-.40955},{90,.18880,.28997,-.44278},{100,.19032,.29326,-.47888},{125,.19462,.30141,-.58204},
    {150,.19962,.30921,-.70471},{175,.20525,.31647,-.84901},{200,.21142,.32312,-1.0182},{225,.21807,.32909,-1.2168},
    {250,.22511,.33439,-1.4512},{275,.23247,.33904,-1.7298},{300,.24010,.34308,-2.0637},{325,.24702,.34655,-2.4681},
    {350,.25591,.34951,-2.9641},{375,.26400,.35200,-3.5814},{400,.27218,.35407,-4.3633},{425,.28039,.35577,-5.3762},
    {450,.28863,.35714,-6.7262},{475,.29685,.35823,-8.5955},{500,.30505,.35907,-11.324},{525,.31320,.35968,-15.628},
    {550,.32129,.36011,-23.325},{575,.32931,.36038,-40.770},{600,.33724,.36051,-116.45}}};
// Natural cubic spline coefficients through (i/4, y[i]).
void spline(const std::array<double,5>&y,std::array<double,5>&b,std::array<double,5>&c,std::array<double,5>&d){
    constexpr double h=.25;std::array<double,5>alpha{},l{},mu{},z{};
    for(int i=1;i<4;++i)alpha[i]=3/h*(y[i+1]-y[i])-3/h*(y[i]-y[i-1]);
    l[0]=1;for(int i=1;i<4;++i){l[i]=2*(2*h)-h*mu[i-1];mu[i]=h/l[i];z[i]=(alpha[i]-h*z[i-1])/l[i];}
    c[4]=0;for(int j=3;j>=0;--j){c[j]=z[j]-mu[j]*c[j+1];b[j]=(y[j+1]-y[j])/h-h*(c[j+1]+2*c[j])/3;d[j]=(c[j+1]-c[j])/(3*h);}
}
float unit(float v)noexcept{return std::clamp(v,0.f,1.f);}
// One 2x2 box halving (y-up rows, transparent padding at the far edge).
void halve(std::vector<std::array<float,4>>&texels,unsigned&width,unsigned&height){
    const unsigned w2=(width+1)/2,h2=(height+1)/2;std::vector<std::array<float,4>>next(std::size_t(w2)*h2);
    for(unsigned j=0;j<h2;++j)for(unsigned i=0;i<w2;++i){std::array<float,4>sum{};
        for(unsigned dj=0;dj<2;++dj)for(unsigned di=0;di<2;++di){const unsigned x=2*i+di,y=2*j+dj;if(x<width&&y<height)for(int c=0;c<4;++c)sum[c]+=texels[std::size_t(y)*width+x][c];}
        for(int c=0;c<4;++c)sum[c]*=.25f;next[std::size_t(j)*w2+i]=sum;}
    texels=std::move(next);width=w2;height=h2;
}
std::uint8_t byte(float v)noexcept{return static_cast<std::uint8_t>(std::floor(std::clamp(v,0.f,1.f)*255.f+.5f));}
}

double MediaAssemblyColorPipeline::srgbEncode(double x)noexcept{return x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055;}
double MediaAssemblyColorPipeline::srgbDecode(double x)noexcept{return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
std::array<double,2>MediaAssemblyColorPipeline::temperatureChromaticity(double kelvin,double tint){
    need(std::isfinite(kelvin)&&kelvin>0&&std::isfinite(tint),"Invalid Media Assembly colour temperature");
    const double r=1e6/kelvin,offset=tint*(1/-3000.);
    for(std::size_t index=0;index<30;++index){
        if(!(r<isotherms[index+1].r||index==29))continue;
        const auto&a=isotherms[index];const auto&b=isotherms[index+1];
        const double f=(b.r-r)/(b.r-a.r);double u=a.u*f+b.u*(1-f),v=a.v*f+b.v*(1-f);
        double uu1=1,vv1=a.t,uu2=1,vv2=b.t;const double len1=std::sqrt(1+vv1*vv1),len2=std::sqrt(1+vv2*vv2);
        uu1/=len1;vv1/=len1;uu2/=len2;vv2/=len2;double uu3=uu1*f+uu2*(1-f),vv3=vv1*f+vv2*(1-f);const double len3=std::sqrt(uu3*uu3+vv3*vv3);
        uu3/=len3;vv3/=len3;u+=uu3*offset;v+=vv3*offset;
        return {1.5*u/(u-4*v+2),v/(u-4*v+2)};
    }
    return {};
}
std::array<double,9>MediaAssemblyColorPipeline::temperatureMatrix(double nk,double nt,double tk,double tt){
    static const M3 rgbToXYZ=genericRGB();static const M3 xyzToRGB=inverse(rgbToXYZ);static const M3 coneToXYZ=inverse(bradford);
    const auto source=transform3(bradford,xyz(temperatureChromaticity(nk,nt))),target=transform3(bradford,xyz(temperatureChromaticity(tk,tt)));
    const M3 scale{target[0]/source[0],0,0,0,target[1]/source[1],0,0,0,target[2]/source[2]};
    return multiply(xyzToRGB,multiply(coneToXYZ,multiply(scale,multiply(bradford,rgbToXYZ))));
}

MediaAssemblyColorPipeline::MediaAssemblyColorPipeline(const MediaAssemblyAdjustments&a,std::span<const std::uint8_t>lookup,bool linear,bool tables):linear_(linear){
    need(a.valid(),"Invalid Media Assembly adjustments");
    if(a.exposure!=0){steps_|=mediaAssemblyStepExposure;exposure_=float(std::exp2(a.exposure));}
    if(a.brightness!=0||a.contrast!=1||a.saturation!=1){steps_|=mediaAssemblyStepColorControls;brightness_=float(a.brightness);contrast_=float(a.contrast);saturation_=float(a.saturation);}
    if(a.temperature!=6500||a.tint!=0){steps_|=mediaAssemblyStepTemperatureTint;const auto m=temperatureMatrix(6500,0,a.temperature,a.tint);for(std::size_t i=0;i<9;++i)matrix_[i]=float(m[i]);}
    // The source requests the filter whenever either amount moved; the filter
    // itself may still resolve to its identity window.
    if(a.highlights!=1||a.shadows!=0){highlightShadow_=MediaAssemblyHighlightShadowKernel::make(a.highlights,a.shadows,tables);if(!highlightShadow_.identity)steps_|=mediaAssemblyStepHighlightShadow;}
    if(a.curve!=std::array<double,5>{0,.25,.5,.75,1}){steps_|=mediaAssemblyStepToneCurve;curveY_=a.curve;spline(curveY_,curveB_,curveC_,curveD_);
        if(tables){toneTable_.resize(toneSamples+1);for(unsigned i=0;i<=toneSamples;++i)toneTable_[i]=toneExact(float(double(i)/toneSamples));}}
    if(a.levelsBlack!=0||a.levelsWhite!=1){steps_|=mediaAssemblyStepLevels;const double gain=1/(a.levelsWhite-a.levelsBlack);levelGain_=float(gain);levelBias_=float(-a.levelsBlack*gain);}
    if(a.levelsGamma!=1){steps_|=mediaAssemblyStepGamma;gammaPower_=float(1/a.levelsGamma);}
    if(a.filter!=MediaAssemblyFilter::none){need(lookup.size()==32*32*32*3,"Media Assembly filter requires its original 32 cubed lookup");steps_|=mediaAssemblyStepLookup;lookup_=lookup;}
    else need(lookup.empty(),"Unexpected Media Assembly lookup without a filter");
}
MediaAssemblyColorPipeline::Constants MediaAssemblyColorPipeline::constants()const{
    Constants c;c.steps=steps_;c.linearWorkingSpace=linear_;c.exposure=exposure_;c.brightness=brightness_;c.contrast=contrast_;c.saturation=saturation_;
    c.temperature=matrix_;c.highlightShadow=highlightShadow_;c.levelGain=levelGain_;c.levelBias=levelBias_;c.gammaPower=gammaPower_;c.lookup=lookup_;
    for(std::size_t j=0;j<4;++j)c.curve[j]={curveY_[j],curveB_[j],curveC_[j],curveD_[j]};
    return c;
}
float MediaAssemblyColorPipeline::tone(float x)const noexcept{
    if(toneTable_.empty())return toneExact(x);
    const float at=unit(x)*float(toneSamples);const auto i=std::min<unsigned>(toneSamples-1,unsigned(at));const float f=at-float(i);
    return toneTable_[i]+(toneTable_[i+1]-toneTable_[i])*f;
}
float MediaAssemblyColorPipeline::toneExact(float x)const noexcept{
    const double e=srgbEncode(double(unit(x)));const auto j=std::min<std::size_t>(3,static_cast<std::size_t>(std::max(0.,std::floor(e*4))));
    const double dx=e-double(j)*.25;const double y=curveY_[j]+curveB_[j]*dx+curveC_[j]*dx*dx+curveD_[j]*dx*dx*dx;
    return float(srgbDecode(std::clamp(y,0.,1.)));
}
MediaAssemblyRGBA MediaAssemblyColorPipeline::apply(MediaAssemblyRGBA p)const noexcept{
    if(!steps_)return p;
    // Core Image filters unpremultiply, operate and premultiply again; the
    // linear exposure/matrix steps are identical either way.
    const float alpha=p.a;if(alpha<=0)return {0,0,0,alpha<0?0:alpha};
    float r=p.r/alpha,g=p.g/alpha,b=p.b/alpha;
    const auto toLinear=[](float v){return float(v<0?-srgbDecode(-double(v)):srgbDecode(double(v)));};
    const auto toEncoded=[](float v){return float(v<0?-srgbEncode(-double(v)):srgbEncode(double(v)));};
    if(linear_){r=toLinear(r);g=toLinear(g);b=toLinear(b);}
    if(steps_&mediaAssemblyStepExposure){r*=exposure_;g*=exposure_;b*=exposure_;}
    if(steps_&mediaAssemblyStepColorControls){
        const float l=r*.2125f+g*.7154f+b*.0721f;
        r=l+(r-l)*saturation_;g=l+(g-l)*saturation_;b=l+(b-l)*saturation_;
        r=(r-.5f)*contrast_+.5f;g=(g-.5f)*contrast_+.5f;b=(b-.5f)*contrast_+.5f;
        r+=brightness_;g+=brightness_;b+=brightness_;
    }
    if(steps_&mediaAssemblyStepTemperatureTint){const auto&m=matrix_;const float x=m[0]*r+m[1]*g+m[2]*b,y=m[3]*r+m[4]*g+m[5]*b,z=m[6]*r+m[7]*g+m[8]*b;r=x;g=y;b=z;}
    if(steps_&mediaAssemblyStepHighlightShadow){const auto v=highlightShadow_.apply({r,g,b});r=v[0];g=v[1];b=v[2];}
    if(steps_&mediaAssemblyStepToneCurve){r=tone(r);g=tone(g);b=tone(b);}
    if(steps_&mediaAssemblyStepLevels){r=unit(r*levelGain_+levelBias_);g=unit(g*levelGain_+levelBias_);b=unit(b*levelGain_+levelBias_);}
    if(steps_&mediaAssemblyStepGamma){r=std::pow(std::max(r,0.f),gammaPower_);g=std::pow(std::max(g,0.f),gammaPower_);b=std::pow(std::max(b,0.f),gammaPower_);}
    if(steps_&mediaAssemblyStepLookup){
        // CIColorCubeWithColorSpace in the same sRGB space: clamped
        // red-fastest trilinear lookup (MediaAssemblyCube::sample math).
        const std::array<float,3>in{unit(linear_?toEncoded(r):r),unit(linear_?toEncoded(g):g),unit(linear_?toEncoded(b):b)};std::array<unsigned,3>low{},high{};std::array<float,3>f{},out{};
        for(unsigned k=0;k<3;++k){const float at=in[k]*31;low[k]=unsigned(at);high[k]=std::min(31u,low[k]+1);f[k]=at-float(low[k]);}
        for(unsigned z=0;z<2;++z)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){
            const auto index=3*((z?high[2]:low[2])*32*32+(y?high[1]:low[1])*32+(x?high[0]:low[0]));
            const float w=(x?f[0]:1-f[0])*(y?f[1]:1-f[1])*(z?f[2]:1-f[2]);
            for(unsigned k=0;k<3;++k)out[k]+=w*(float(lookup_[index+k])/255.f);
        }r=out[0];g=out[1];b=out[2];
        if(linear_){r=toLinear(r);g=toLinear(g);b=toLinear(b);}
    }
    if(linear_){r=toEncoded(r);g=toEncoded(g);b=toEncoded(b);}
    return {r*alpha,g*alpha,b*alpha,alpha};
}

unsigned mediaAssemblyDownscaleLevels(double scale)noexcept{
    // Core Image halves while log2(1/scale) exceeds 1.2 (switch measured at
    // 2^-1.2 = 0.4352752816 for every rotation and at each further octave).
    if(!(scale>0)||!std::isfinite(scale))return 0;unsigned levels=0;const double threshold=std::exp2(-1.2);
    while(scale<threshold&&levels<24){scale*=2;++levels;}return levels;
}
namespace {
float smooth(float edge0,float edge1,float x)noexcept{const float t=std::clamp((x-edge0)/(edge1-edge0),0.f,1.f);return t*t*(3.f-2.f*t);}
float blend(float from,float to,float amount)noexcept{return from*(1.f-amount)+to*amount;}
float signOf(float v)noexcept{return v>0?1.f:v<0?-1.f:0.f;}
float luma(const std::array<float,3>&v)noexcept{return v[0]*.299f+v[1]*.587f+v[2]*.114f;}
// Mid-tone weight shared by both restore passes: two downward parabolas in
// luma, floored at one (they only exceed it for negative luma).
float midWeight(float y)noexcept{return std::max(std::max(-2.6f*y*y-2.6f*y+.98f,-6.25f*y*y-6.25f*y+.5965f),1.f);}
// x^e for x >= 0 as exp2(e*log2 x) (the GPU's own power expansion).
float power(float x,float e)noexcept{if(e==1)return x;if(e==0)return 1;if(x<=0)return 0;return std::exp2(e*std::log2(x));}
}
MediaAssemblyHighlightShadowKernel MediaAssemblyHighlightShadowKernel::make(double highlights,double shadows,bool table){
    MediaAssemblyHighlightShadowKernel k;
    const double s=std::clamp(shadows,-1.,1.),h=std::clamp(highlights,0.,1.);
    if(std::abs(s)<.05&&h>.95)return k;
    k.identity=false;k.shadow=float(s);k.highlight=float(h);
    k.noOpMix=float(std::clamp(1-std::pow(std::abs(s/.3),1.6),0.,1.));
    k.gain=float(1/std::max(1.997-1/(1+std::exp(-6*h)),1.));
    if(table&&k.highlight!=1){
        const double e=2-double(k.highlight);k.highlightCurve.resize(highlightSamples+1);
        for(unsigned i=0;i<=highlightSamples;++i)k.highlightCurve[i]=float(std::pow(double(i)/highlightSamples,e));
    }
    return k;
}
float MediaAssemblyHighlightShadowKernel::highlightPower(float x)const noexcept{
    if(highlight==1.f)return x;
    if(highlightCurve.empty())return power(x,2.f-highlight);
    if(!(x<=1.f))return power(x,2.f-highlight);
    const float at=x*float(highlightSamples);const auto i=std::min<unsigned>(highlightSamples-1,unsigned(at));const float f=at-float(i);
    return highlightCurve[i]+(highlightCurve[i+1]-highlightCurve[i])*f;
}
std::array<float,3>MediaAssemblyHighlightShadowKernel::apply(std::array<float,3>p)const noexcept{
    if(identity)return p;
    const float r=p[0],g=p[1],b=p[2];
    // The filter's blur input is the pixel itself at radius 0.
    const float peak=std::max(0.f,std::max(std::max(r,g),b)),root=std::sqrt(peak);
    // Shadow lift, stronger for blue/red dominant pixels than for green.
    const float warmth=(std::max(r,0.f)+.8f*std::max(g,0.f)+1.1f*std::max(b,0.f))/std::max(.001f,r+g+b);
    const float lift=shadow==0?0.f:shadow*power(std::min(warmth,1.f),1.f-shadow);
    const float scale=.5f+.5f*smooth(.5f,1.f,shadow);
    // With the no-op mix at one every shadow exponent is exactly one.
    const bool linearShadow=noOpMix==1.f;
    std::array<float,3>exponent{1.f,1.f,1.f},shade{},high{},out{};
    for(int c=0;c<3;++c){
        const float base=std::max(p[c],0.f)*scale;
        if(linearShadow)shade[c]=(1.f+lift)*base*2.f;
        else{exponent[c]=blend(std::exp2(-lift-p[c]),1.f,noOpMix);shade[c]=(1.f+lift)*power(base,exponent[c])*2.f;}
    }
    // The same curve applied to luma only (YIQ), mixed in at 35 %.
    const float y=r*.299f+g*.587f+b*.114f,i=r*.596f+g*-.2755f+b*-.321f,q=r*.212f+g*-.523f+b*.311f;
    const float curved=signOf(y)*(linearShadow?std::abs(y)*scale:power(std::abs(y)*scale,exponent[0]))*2.f;
    const std::array<float,3>lumaOnly{curved*1.00048f+i*.955558f+q*.619549f,curved*.999864f+i*-.271545f+q*-.646786f,curved*.999446f+i*-1.10803f+q*1.70542f};
    const float shadowFade=smooth(0.f,.1f+lift,std::sqrt(root));
    for(int c=0;c<3;++c){shade[c]=blend(shade[c],lumaOnly[c],.35f);shade[c]=blend(p[c],shade[c],shadowFade);shade[c]=blend(shade[c],p[c],root);}
    // Highlight power curve around a quarter-grey pivot.
    for(int c=0;c<3;++c)high[c]=signOf(p[c])*highlightPower(std::abs(p[c])*gain);
    const float pivotMix=1.f+(1.f-std::min(1.f,highlight+.3f))*.4f;
    const float boost=std::min(midWeight(luma(high)),30.f*peak)*(1.f-highlight);
    const float highFade=root<=.2f?0.f:root>=.8f?1.f:smooth(.2f,.8f,root);
    for(int c=0;c<3;++c){high[c]=blend(high[c],blend(.25f,high[c],pivotMix),boost);high[c]=blend(p[c],high[c],highFade);high[c]=blend(p[c],high[c],peak);}
    for(int c=0;c<3;++c)out[c]=blend(shade[c],high[c],std::min(root,1.f));
    // Mid-grey contrast restore proportional to the applied lift.
    const float restore=std::min(midWeight(luma(out)),30.f*peak),stretch=1.f+std::abs(lift)*.1f*(1.f-noOpMix);
    for(int c=0;c<3;++c){out[c]=blend(out[c],blend(.5f,out[c],stretch),restore);out[c]=std::max(out[c],0.f)+std::min(p[c],0.f);}
    return out;
}
std::array<float,3>mediaAssemblyHighlightShadow(std::array<float,3>v,float highlights,float shadows)noexcept{
    return MediaAssemblyHighlightShadowKernel::make(highlights,shadows).apply(v);
}
std::vector<std::uint8_t>mediaAssemblyPremultiply(std::span<const std::uint8_t>s){
    need(s.size()%4==0,"Invalid Media Assembly RGBA8 artwork");std::vector<std::uint8_t>out(s.size());
    for(std::size_t i=0;i<s.size();i+=4){const unsigned a=s[i+3];for(int c=0;c<3;++c)out[i+c]=static_cast<std::uint8_t>((s[i+c]*a+127)/255);out[i+3]=static_cast<std::uint8_t>(a);}
    return out;
}

MediaAssemblyFrameProcessor::MediaAssemblyFrameProcessor(unsigned w,unsigned h,const MediaAssemblyAdjustments&a,std::span<const std::uint8_t>lookup,bool include,std::span<const MediaAssemblyStickerArtwork>art,bool linear)
    :plan_(mediaAssemblyPixelPlan(w,h,a)),color_(a,lookup,linear){
    const auto map=mediaAssemblyPixelMap(plan_);origin_=map.origin;column_=map.column;row_=map.row;
    if(!include||a.stickers.empty())return;
    need(art.size()==a.stickers.size(),"Media Assembly sticker artwork must match the adjustments");stickers_.reserve(art.size());
    const double W=plan_.outputWidth,H=plan_.outputHeight;
    for(std::size_t n=0;n<art.size();++n){
        const auto&s=a.stickers[n];const auto&image=art[n];
        need(image.width&&image.height&&image.premultipliedRGBA.size()==std::size_t(image.width)*image.height*4,"Invalid Media Assembly sticker artwork");
        const double scale=std::min(W,H)*s.size/std::max(image.width,image.height);
        const double angle=s.rotation*std::numbers::pi/180,cx=W*s.x,cy=H*(1-s.y),hw=image.width*scale/2,hh=image.height*scale/2;
        // Inverse of: scale, centre, rotate(-angle), translate (original y-up order).
        const double cs=std::cos(angle),sn=std::sin(angle);
        const unsigned levels=mediaAssemblyDownscaleLevels(scale);
        Sticker out;out.width=image.width;out.height=image.height;out.texels.resize(std::size_t(image.width)*image.height);
        for(unsigned j=0;j<image.height;++j)for(unsigned i=0;i<image.width;++i){const auto*t=image.premultipliedRGBA.data()+(std::size_t(image.height-1-j)*image.width+i)*4;out.texels[std::size_t(j)*image.width+i]={t[0]/255.f,t[1]/255.f,t[2]/255.f,t[3]/255.f};}
        for(unsigned level=0;level<levels;++level)halve(out.texels,out.width,out.height);
        const double k=std::ldexp(1.,-int(levels));
        out.inverse={cs/scale*k,-sn/scale*k,(-cs*cx+sn*cy+hw)/scale*k,sn/scale*k,cs/scale*k,(-sn*cx-cs*cy+hh)/scale*k};
        out.minX=out.minY=INFINITY;out.maxX=out.maxY=-INFINITY;
        for(const auto[u,v]:std::array<std::array<double,2>,4>{{{-hw,-hh},{hw,-hh},{-hw,hh},{hw,hh}}}){
            const double ca=std::cos(-angle),sa=std::sin(-angle);const double x=ca*u-sa*v+cx,y=sa*u+ca*v+cy;
            out.minX=std::min(out.minX,x);out.maxX=std::max(out.maxX,x);out.minY=std::min(out.minY,y);out.maxY=std::max(out.maxY,y);
        }
        stickers_.push_back(out);
    }
}
MediaAssemblyPixelMap mediaAssemblyPixelMap(const MediaAssemblyPixelPlan&plan)noexcept{
    // Pixel centres map exactly onto source pixel centres (crop/mirror/quarter
    // turns), so three samples give the whole integer permutation.
    const auto at=[&](double x,double y){const auto uv=plan.sourceUV({(x+.5)/plan.outputWidth,(y+.5)/plan.outputHeight});return std::array<long long,2>{(long long)std::floor(uv.x*plan.sourceWidth),(long long)std::floor(uv.y*plan.sourceHeight)};};
    MediaAssemblyPixelMap m;m.origin=at(0,0);const auto c=at(1,0),r=at(0,1);
    m.column={c[0]-m.origin[0],c[1]-m.origin[1]};m.row={r[0]-m.origin[0],r[1]-m.origin[1]};return m;
}
MediaAssemblyRGBA MediaAssemblyFrameProcessor::source(const MediaAssemblyPixels&s,unsigned x,unsigned y)const noexcept{
    const long long sx=origin_[0]+column_[0]*x+row_[0]*y,sy=origin_[1]+column_[1]*x+row_[1]*y;
    if(sx<0||sy<0||sx>=(long long)plan_.sourceWidth||sy>=(long long)plan_.sourceHeight)return {};
    const auto*p=s.bytes.data()+std::size_t(sy)*s.stride+std::size_t(sx)*4;const float a=p[3]/255.f;
    if(s.premultiplied)return {p[0]/255.f,p[1]/255.f,p[2]/255.f,a};
    return {p[0]/255.f*a,p[1]/255.f*a,p[2]/255.f*a,a};
}
void MediaAssemblyFrameProcessor::render(const MediaAssemblyPixels&s,unsigned first,unsigned count,std::span<std::uint8_t>target,std::size_t stride,MediaAssemblyOutputAlpha mode)const{
    need(s.width==plan_.sourceWidth&&s.height==plan_.sourceHeight&&s.stride>=std::size_t(s.width)*4&&s.bytes.size()>=s.stride*(s.height-1)+std::size_t(s.width)*4,"Media Assembly source does not match its plan");
    need(first<=plan_.outputHeight&&count<=plan_.outputHeight-first&&stride>=std::size_t(plan_.outputWidth)*4&&(count==0||target.size()>=stride*(count-1)+std::size_t(plan_.outputWidth)*4),"Invalid Media Assembly output rows");
    const double H=plan_.outputHeight;
    for(unsigned row=0;row<count;++row){
        const unsigned y=first+row;auto*out=target.data()+std::size_t(row)*stride;const double py=H-y-.5;
        for(unsigned x=0;x<plan_.outputWidth;++x){
            auto p=color_.apply(source(s,x,y));const double px=x+.5;
            for(const auto&k:stickers_){
                if(px<k.minX-1||px>k.maxX+1||py<k.minY-1||py>k.maxY+1)continue;
                const auto&m=k.inverse;const double u=m[0]*px+m[1]*py+m[2]-.5,v=m[3]*px+m[4]*py+m[5]-.5;
                const double fu=std::floor(u),fv=std::floor(v);const int i0=int(fu),j0=int(fv);const float wx=float(u-fu),wy=float(v-fv);
                std::array<float,4>acc{};
                for(int dj=0;dj<2;++dj)for(int di=0;di<2;++di){
                    const int i=i0+di,j=j0+dj;if(i<0||j<0||i>=int(k.width)||j>=int(k.height))continue;
                    const float w=(di?wx:1-wx)*(dj?wy:1-wy);const auto&t=k.texels[std::size_t(j)*k.width+std::size_t(i)];
                    for(int c=0;c<4;++c)acc[c]+=w*t[c];
                }
                const float keep=1-acc[3];p={acc[0]+p.r*keep,acc[1]+p.g*keep,acc[2]+p.b*keep,acc[3]+p.a*keep};
            }
            auto*o=out+std::size_t(x)*4;const float a=unit(p.a);o[3]=byte(a);
            if(mode==MediaAssemblyOutputAlpha::premultiplied){o[0]=byte(std::min(p.r,a));o[1]=byte(std::min(p.g,a));o[2]=byte(std::min(p.b,a));}
            else if(a<=0){o[0]=o[1]=o[2]=0;}
            else{o[0]=byte(std::min(p.r,a)/a);o[1]=byte(std::min(p.g,a)/a);o[2]=byte(std::min(p.b,a)/a);}
        }
    }
}
std::vector<std::uint8_t>MediaAssemblyFrameProcessor::render(const MediaAssemblyPixels&s,MediaAssemblyOutputAlpha mode)const{
    std::vector<std::uint8_t>out(std::size_t(width())*height()*4);render(s,0,height(),out,std::size_t(width())*4,mode);return out;
}
void mediaAssemblyResample(const MediaAssemblyPixels&s,unsigned w,unsigned h,std::span<std::uint8_t>target,std::size_t stride){
    need(s.width&&s.height&&w&&h&&s.stride>=std::size_t(s.width)*4&&stride>=std::size_t(w)*4&&target.size()>=stride*(h-1)+std::size_t(w)*4,"Invalid Media Assembly resample");
    const double sx=double(s.width)/w,sy=double(s.height)/h;
    for(unsigned y=0;y<h;++y){
        const double v=(y+.5)*sy-.5,fv=std::floor(v);const int j0=int(fv);const float wy=float(v-fv);
        for(unsigned x=0;x<w;++x){
            const double u=(x+.5)*sx-.5,fu=std::floor(u);const int i0=int(fu);const float wx=float(u-fu);std::array<float,4>acc{};
            for(int dj=0;dj<2;++dj)for(int di=0;di<2;++di){
                const int i=i0+di,j=j0+dj;if(i<0||j<0||i>=int(s.width)||j>=int(s.height))continue;
                const float weight=(di?wx:1-wx)*(dj?wy:1-wy);const auto*p=s.bytes.data()+std::size_t(j)*s.stride+std::size_t(i)*4;
                const float a=p[3]/255.f;for(int c=0;c<3;++c)acc[c]+=weight*(s.premultiplied?p[c]/255.f:p[c]/255.f*a);acc[3]+=weight*a;
            }
            auto*o=target.data()+std::size_t(y)*stride+std::size_t(x)*4;const float a=unit(acc[3]);o[3]=byte(a);
            for(int c=0;c<3;++c)o[c]=s.premultiplied?byte(std::min(acc[c],a)):(a>0?byte(std::min(acc[c],a)/a):0);
        }
    }
}
}
#if defined(_MSC_VER)&&!defined(__clang__)
#pragma float_control(pop)
#endif
