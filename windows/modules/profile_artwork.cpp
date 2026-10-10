#include "modules/profile_artwork.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
void bounded(unsigned w,unsigned h){need(w>0&&h>0&&w<=16384&&h<=16384,"Profile artwork exceeds the source context bounds");}
std::uint8_t clampByte(long v){return static_cast<std::uint8_t>(std::clamp(v,0L,255L));}
// Core Graphics 8-bit multiply with rounding.
unsigned mul(unsigned a,unsigned b){return (a*b+127)/255;}
// Fraction of a pixel inside the source rounded panel (elliptical corners),
// 16x16 samples; the panel scales with the 530x204 sprite.
struct Panel {
    double x0,y0,x1,y1,rx,ry;
    Panel(unsigned w,unsigned h){
        const double sx=w/530.,sy=h/204.;
        // CGContext y grows upward; the panel is vertically symmetric about
        // the sprite (22 below, 22 above) so top-left rows use the same band.
        x0=20*sx;x1=x0+487*sx;y0=h-(22+160)*sy;y1=y0+160*sy;rx=22*sx;ry=22*sy;
    }
    bool inside(double x,double y)const{
        if(x<x0||x>x1||y<y0||y>y1)return false;
        const double cx=std::clamp(x,x0+rx,x1-rx),cy=std::clamp(y,y0+ry,y1-ry);
        const double dx=(x-cx)/rx,dy=(y-cy)/ry;return dx*dx+dy*dy<=1;
    }
    double coverage(unsigned px,unsigned py)const{
        if(px+1<=x0||px>=x1||py+1<=y0||py>=y1)return 0;
        if(px>=x0+rx&&px+1<=x1-rx&&py>=y0&&py+1<=y1)return 1;
        if(py>=y0+ry&&py+1<=y1-ry&&px>=x0&&px+1<=x1)return 1;
        unsigned count{};for(unsigned j=0;j<16;++j)for(unsigned i=0;i<16;++i)count+=inside(px+(i+.5)/16,py+(j+.5)/16);
        return count/256.;
    }
};
double linear(double c){return c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);}
// Straight 8-bit sRGB to linear light, computed once.
const std::array<double,256>&linearTable(){static const auto table=[]{std::array<double,256>t{};for(unsigned i=0;i<256;++i)t[i]=linear(i/255.);return t;}();return table;}
double encoded(double l){l=std::clamp(l,0.,1.);return l<=.0031308?l*12.92:1.055*std::pow(l,1/2.4)-.055;}
// Measured against Core Image: a three-lobe window when reducing and a
// two-lobe window when enlarging an axis.
double lanczos(double x,double a){
    constexpr double pi=3.14159265358979323846;x=std::abs(x);
    if(x<1e-12)return 1;if(x>=a)return 0;const double px=pi*x;return a*std::sin(px)*std::sin(px/a)/(px*px);
}
}
ProfileImage profileSpriteFromMip(std::span<const std::uint8_t>bgra,unsigned tw,unsigned th,core::Rect sprite){
    need(tw>0&&tw<=16384&&th>0&&th<=16384,"Invalid profile source texture");
    need(std::isfinite(sprite.x)&&std::isfinite(sprite.y)&&std::isfinite(sprite.width)&&std::isfinite(sprite.height)&&sprite.x>=0&&sprite.y>=0&&sprite.width>0&&sprite.height>0&&
         sprite.x+sprite.width<=tw&&sprite.y+sprite.height<=th,"Invalid profile sprite rect");
    const auto x=static_cast<unsigned>(sprite.x),y=static_cast<unsigned>(sprite.y),w=static_cast<unsigned>(sprite.width),h=static_cast<unsigned>(sprite.height);
    need(x==sprite.x&&y==sprite.y&&w==sprite.width&&h==sprite.height&&bgra.size()>=std::size_t(tw)*th*4,"Profile sprite must be integral inside its mip");
    ProfileImage out{w,h,std::vector<std::uint8_t>(std::size_t(w)*h*4),false};
    for(unsigned row=0;row<h;++row)for(unsigned column=0;column<w;++column){
        const auto t=(std::size_t(row)*w+column)*4,s=((std::size_t(y)+h-row-1)*tw+x+column)*4;
        out.rgba[t]=bgra[s+2];out.rgba[t+1]=bgra[s+1];out.rgba[t+2]=bgra[s];out.rgba[t+3]=bgra[s+3];
    }
    return out;
}
ProfileImage profilePremultiplied(const ProfileImage&image){
    bounded(image.width,image.height);need(image.rgba.size()==std::size_t(image.width)*image.height*4,"Profile image size");
    if(image.premultiplied)return image;
    ProfileImage out=image;out.premultiplied=true;
    for(std::size_t i=0;i<out.rgba.size();i+=4){const unsigned a=out.rgba[i+3];for(unsigned c=0;c<3;++c)out.rgba[i+c]=static_cast<std::uint8_t>(mul(out.rgba[i+c],a));}
    return out;
}
ProfileImage idCardThemedArtwork(const ProfileImage&source,std::array<double,3>tint){
    auto out=profilePremultiplied(source);
    for(std::size_t i=0;i<out.rgba.size();i+=4){
        auto*p=&out.rgba[i];const long yellow=std::max(0L,static_cast<long>(std::min(p[0],p[1]))-static_cast<long>(p[2]));if(yellow<=0)continue;
        // Premultiplied chroma and neutral components stay scaled by alpha.
        for(unsigned c=0;c<3;++c){const long neutral=static_cast<long>(p[c])-(c<2?yellow:0);p[c]=clampByte(neutral+std::lround(static_cast<double>(yellow)*tint[c]));}
    }
    return out;
}
ProfileImage idCardHoverArtwork(const ProfileImage&source,std::array<double,3>accent){
    auto out=profilePremultiplied(source);const Panel panel(out.width,out.height);
    const unsigned sa=static_cast<unsigned>(std::lround(.38*255));
    const std::array<unsigned,3>sc{static_cast<unsigned>(std::lround(accent[0]*.38*255)),static_cast<unsigned>(std::lround(accent[1]*.38*255)),static_cast<unsigned>(std::lround(accent[2]*.38*255))};
    const double keep=std::lround(.05/.38*255)/255.;
    for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x){
        auto*p=&out.rgba[(std::size_t(y)*out.width+x)*4];const unsigned da=p[3];
        // sourceIn: accent .38 wherever the artwork has alpha.
        for(unsigned c=0;c<3;++c)p[c]=static_cast<std::uint8_t>(mul(sc[c],da));p[3]=static_cast<std::uint8_t>(mul(sa,da));
        // destinationIn by .05/.38 inside the antialiased rounded panel.
        const double cover=panel.coverage(x,y);if(cover<=0)continue;const double factor=1-cover+cover*keep;
        for(unsigned c=0;c<4;++c)p[c]=static_cast<std::uint8_t>(std::lround(p[c]*factor));
    }
    return out;
}
ProfileImage idCardCompositedBackground(const ProfileImage&photo,const ProfileImage&artwork){
    need(photo.width==artwork.width&&photo.height==artwork.height,"Composite the photo at the artwork's size");
    auto out=profilePremultiplied(artwork);const auto image=profilePremultiplied(photo);const Panel panel(out.width,out.height);
    const unsigned shade=static_cast<unsigned>(std::lround(.48*255));
    for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x){
        const auto o=(std::size_t(y)*out.width+x)*4;const double cover=panel.coverage(x,y);if(cover<=0)continue;
        // Photo dimmed by sourceAtop black .48, then sourceAtop on the artwork.
        std::array<unsigned,4>dark{};for(unsigned c=0;c<3;++c)dark[c]=mul(image.rgba[o+c],255-shade);dark[3]=image.rgba[o+3];
        auto*d=&out.rgba[o];const unsigned da=d[3];
        for(unsigned c=0;c<4;++c){
            const double atop=c<3?mul(dark[c],da)+mul(d[c],255-dark[3]):da;
            d[c]=static_cast<std::uint8_t>(std::lround(cover*atop+(1-cover)*d[c]));
        }
    }
    return out;
}
ProfileImage idCardTexturePixels(const ProfileImage&image){
    const auto source=profilePremultiplied(image);ProfileImage out{source.width,source.height,std::vector<std::uint8_t>(source.rgba.size()),false};
    for(unsigned row=0;row<source.height;++row)for(unsigned column=0;column<source.width;++column){
        const auto s=(std::size_t(row)*source.width+column)*4,t=((std::size_t(source.height)-row-1)*source.width+column)*4;const unsigned a=source.rgba[s+3];
        for(unsigned c=0;c<3;++c)out.rgba[t+c]=a==0?0:static_cast<std::uint8_t>(std::min(255u,(source.rgba[s+c]*255u+a/2)/a));
        out.rgba[t+3]=static_cast<std::uint8_t>(a);
    }
    return out;
}
void idCardMaskHover(ProfileImage&hover,const ProfileImage&background){
    need(hover.width==background.width&&hover.height==background.height&&hover.rgba.size()==background.rgba.size(),"Profile hover and background must share texel mapping");
    for(std::size_t a=3;a<hover.rgba.size();a+=4)hover.rgba[a]=static_cast<std::uint8_t>((unsigned(hover.rgba[a])*background.rgba[a]+127)/255);
}
namespace {ProfileImage resample(const ProfileImage&,int,core::Rect,unsigned,unsigned);}
ProfileImage profileRenderedImage(const ProfileImage&input,int orientation,core::Point target,double zoom,core::Point offset,double contentsScale){
    bounded(input.width,input.height);need(input.rgba.size()==std::size_t(input.width)*input.height*4,"Profile portrait size");
    need(std::isfinite(target.x)&&std::isfinite(target.y)&&target.x>0&&target.y>0,"Profile portrait target");
    if(orientation<1||orientation>8)orientation=1;
    const auto&source=input;const bool swap=orientation>=5;
    const double width=swap?source.height:source.width,height=swap?source.width:source.height;
    const auto selection=[&]{
        // HUDPortraitArtwork.crop on the oriented extent.
        const double magnification=std::isfinite(zoom)?std::clamp(zoom,1.,20.):1,fit=std::max(target.x/width,target.y/height);
        const double w=std::min(1.,target.x/(width*fit))/magnification,h=std::min(1.,target.y/(height*fit))/magnification;
        const double ox=std::isfinite(offset.x)?std::clamp(offset.x,-1.,1.):0,oy=std::isfinite(offset.y)?std::clamp(offset.y,-1.,1.):0;
        return core::Rect{(1-w)*(ox+1)/2,(1-h)*(oy+1)/2,w,h};
    }();
    const core::Rect region{selection.x*width,selection.y*height,selection.width*width,selection.height*height};
    const double scale=std::isfinite(contentsScale)?std::clamp(contentsScale,1.,8.):2;
    const unsigned outW=std::max(1u,std::min(1024u,static_cast<unsigned>(std::ceil(target.x*scale*1.35)))),outH=std::max(1u,std::min(1024u,static_cast<unsigned>(std::ceil(target.y*scale*1.35))));
    return resample(source,orientation,region,outW,outH);
}
namespace {
ProfileImage resample(const ProfileImage&source,int orientation,core::Rect region,unsigned outW,unsigned outH){
    const bool premultipliedSource=source.premultiplied;const bool swap=orientation>=5;
    const double width=swap?source.height:source.width,height=swap?source.width:source.height;
    const auto stored=[&](unsigned x,unsigned y)->std::size_t{
        const unsigned W=source.width,H=source.height;unsigned sx{},sy{};
        switch(orientation){case 1:sx=x;sy=y;break;case 2:sx=W-1-x;sy=y;break;case 3:sx=W-1-x;sy=H-1-y;break;case 4:sx=x;sy=H-1-y;break;
            case 5:sx=y;sy=x;break;case 6:sx=y;sy=H-1-x;break;case 7:sx=W-1-y;sy=H-1-x;break;default:sx=W-1-y;sy=x;break;}
        return (std::size_t(sy)*W+sx)*4;
    };
    const double sx=outW/region.width,sy=outH/region.height;
    // Core Image order: crop the oriented source to the fractional region, translate
    // it to the origin (a bilinear resample of the source at region + k + 0.5),
    // then a separable Lanczos-3 over that grid, in premultiplied linear light.
    // Grid samples whose centers fall outside the crop are clear, so edges fade.
    // Only the crop's bilinear support is linearized: a 128-megapixel avatar
    // never becomes a full-size floating-point copy.
    const auto&lut=linearTable();
    const auto pixel=[&](unsigned x,unsigned y,std::array<double,4>&out){
        const auto s=stored(x,y);const double a=source.rgba[s+3]/255.;
        if(premultipliedSource){for(unsigned c=0;c<3;++c){const double v=source.rgba[s+c]/255.;out[c]=a>0?linear(std::min(1.,v/a))*a:0;}}
        else for(unsigned c=0;c<3;++c)out[c]=lut[source.rgba[s+c]]*a;
        out[3]=a;
    };
    const unsigned gw=static_cast<unsigned>(std::max(0.,std::floor(region.width-.5)+1)),gh=static_cast<unsigned>(std::max(0.,std::floor(region.height-.5)+1));
    need(gw&&gh&&gw<=65536&&gh<=65536,"Profile portrait crop is empty or unbounded");
    const auto axis=[](double position,unsigned count,unsigned&a,unsigned&b,double&t){
        const double x=std::clamp(position-.5,0.,double(count-1));a=static_cast<unsigned>(std::floor(x));b=std::min(a+1,count-1);t=x-a;};
    const auto taps=[&](double center,double factor,double lobes,unsigned count){
        std::vector<std::pair<unsigned,double>>out;const double support=lobes*factor;double total{};
        const long first=static_cast<long>(std::floor(center-support)),last=static_cast<long>(std::ceil(center+support));
        for(long k=first;k<=last;++k){const double w=lanczos((k+.5-center)/factor,lobes);if(w==0)continue;total+=w;if(k<0||k>=static_cast<long>(count))continue;out.push_back({static_cast<unsigned>(k),w});}
        if(total!=0)for(auto&v:out)v.second/=total;
        return out;
    };
    const double fx=std::max(1.,1/sx),fy=std::max(1.,1/sy),lx=sx<1?3:2,ly=sy<1?3:2;
    std::vector<std::vector<std::pair<unsigned,double>>>horizontal(outW),vertical(outH);std::size_t window{1};
    for(unsigned i=0;i<outW;++i)horizontal[i]=taps((i+.5)/sx,fx,lx,gw);
    for(unsigned j=0;j<outH;++j){vertical[j]=taps((j+.5)/sy,fy,ly,gh);if(!vertical[j].empty())window=std::max<std::size_t>(window,vertical[j].back().first-vertical[j].front().first+1);}
    // Sliding window of horizontally filtered grid rows: memory is bounded by
    // the vertical support, never by the source or crop size.
    std::vector<double>row(std::size_t(gw)*4),ring(window*outW*4);std::vector<long>held(window,-1);std::array<double,4>p00{},p10{},p01{},p11{};
    const auto filtered=[&](unsigned l)->const double*{
        auto*slot=&ring[(l%window)*outW*4];if(held[l%window]==long(l))return slot;
        unsigned y0,y1;double ty;axis(region.y+l+.5,unsigned(height),y0,y1,ty);
        for(unsigned k=0;k<gw;++k){unsigned x0,x1;double tx;axis(region.x+k+.5,unsigned(width),x0,x1,tx);
            pixel(x0,y0,p00);pixel(x1,y0,p10);pixel(x0,y1,p01);pixel(x1,y1,p11);auto*d=&row[std::size_t(k)*4];
            for(unsigned c=0;c<4;++c)d[c]=(p00[c]*(1-tx)+p10[c]*tx)*(1-ty)+(p01[c]*(1-tx)+p11[c]*tx)*ty;}
        for(unsigned i=0;i<outW;++i){auto*d=&slot[std::size_t(i)*4];d[0]=d[1]=d[2]=d[3]=0;for(const auto&[k,w]:horizontal[i]){const auto*g=&row[std::size_t(k)*4];for(unsigned c=0;c<4;++c)d[c]+=g[c]*w;}}
        held[l%window]=l;return slot;
    };
    ProfileImage out{outW,outH,std::vector<std::uint8_t>(std::size_t(outW)*outH*4),true};
    for(unsigned j=0;j<outH;++j)for(unsigned i=0;i<outW;++i){
        std::array<double,4>v{};for(const auto&[k,w]:vertical[j]){const auto*s=filtered(k)+std::size_t(i)*4;for(unsigned c=0;c<4;++c)v[c]+=s[c]*w;}
        const double a=std::clamp(v[3],0.,1.);auto*d=&out.rgba[(std::size_t(j)*outW+i)*4];
        for(unsigned c=0;c<3;++c)d[c]=static_cast<std::uint8_t>(std::lround(a>0?encoded(std::clamp(v[c]/a,0.,1.))*a*255:0));
        d[3]=static_cast<std::uint8_t>(std::lround(a*255));
    }
    return out;
}
}
ProfileImage profileResampled(const ProfileImage&image,unsigned width,unsigned height){
    bounded(image.width,image.height);bounded(width,height);need(image.rgba.size()==std::size_t(image.width)*image.height*4,"Profile image size");
    return resample(image,1,{0,0,double(image.width),double(image.height)},width,height);
}
ProfileImage profileStraight(const ProfileImage&image){
    if(!image.premultiplied)return image;ProfileImage out=image;out.premultiplied=false;
    for(std::size_t i=0;i<out.rgba.size();i+=4){const unsigned a=out.rgba[i+3];for(unsigned c=0;c<3;++c)out.rgba[i+c]=a==0?0:static_cast<std::uint8_t>(std::min(255u,(out.rgba[i+c]*255u+a/2)/a));}
    return out;
}
ProfileSourceArtwork loadProfileSourceArtwork(const std::filesystem::path&directory){
    const auto catalogBytes=ehud::data::detail::readFile(directory/"catalog.json",1024*1024);need(catalogBytes.has_value(),"Missing profile resource catalog");
    const auto catalog=ehud::data::Json::parse(*catalogBytes,1024*1024);ProfileSourceArtwork out;out.defaultBackgroundSHA256=catalog["defaultBackgroundTextureSHA256"].string();
    for(const auto&file:catalog["files"].array()){
        const auto name=file["path"].string();need(name=="business-card.bgra"||name=="avatar-frame.bgra","Unknown profile resource");
        const auto bytes=ehud::data::detail::readFile(directory/name,4*1024*1024);need(bytes.has_value()&&bytes->size()==std::size_t(file["bytes"].integer()),"Missing profile source texture");
        const std::span<const std::uint8_t>view(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size());
        need(core::packet::sha256(view)==file["sha256"].string(),"Profile source texture digest differs from its catalog");
        const auto&r=file["spriteRect"].array();
        auto sprite=profileSpriteFromMip(view,static_cast<unsigned>(file["textureWidth"].integer()),static_cast<unsigned>(file["textureHeight"].integer()),{r[0].number(),r[1].number(),r[2].number(),r[3].number()});
        (name=="business-card.bgra"?out.card:out.frame)=std::move(sprite);
    }
    need(out.card.width==530&&out.card.height==204&&out.frame.width==254&&out.frame.height==254,"Profile source sprites are incomplete");
    return out;
}
ProfileImage profileTintedFrame(const ProfileImage&frame,std::array<double,3>accent){
    auto out=profilePremultiplied(frame);
    for(std::size_t i=0;i<out.rgba.size();i+=4)for(unsigned c=0;c<3;++c)out.rgba[i+c]=clampByte(std::lround(out.rgba[i+c]*accent[c]));
    return out;
}
}
