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
        auto*p=&out.rgba[(std::size_t(y)*out.width+x)*4];
        // HUDSourceProfileArtwork.hoverCoverageThreshold (Mac 1.3.0): the
        // sprite's near-transparent padding (alpha 1-8, a band left of the
        // card) is cleared first, so the accent never lights it outside the card.
        const unsigned da=p[3]<hoverCoverageThreshold?0:p[3];
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
    return artworkResampled(source,orientation,region,outW,outH);
}
ProfilePanningImage profilePanningImage(const ProfileImage&source,int orientation,double side,double zoom,double contentsScale){
    bounded(source.width,source.height);need(source.rgba.size()==std::size_t(source.width)*source.height*4,"Profile portrait size");
    need(std::isfinite(side)&&side>0,"Profile panning well");
    if(orientation<1||orientation>8)orientation=1;
    const bool swap=orientation>=5;
    const double width=swap?source.height:source.width,height=swap?source.width:source.height;
    const double fillWidth=std::max(1.,width/height),fillHeight=std::max(1.,height/width);
    const double scale=std::isfinite(contentsScale)?std::clamp(contentsScale,1.,8.):2;
    const double magnification=std::isfinite(zoom)?std::clamp(zoom,1.,20.):1;
    const double longSide=std::min(2048.,side*std::max(fillWidth,fillHeight)*magnification*scale*1.35);
    const double factor=std::min(1.,longSide/std::max(width,height));
    // HUDPortraitArtwork.scaledImage: one factor on both axes, max(1, round(side x factor)) per side.
    return {artworkScaled(source,orientation,{0,0,width,height},factor),fillWidth,fillHeight};
}
ProfileImage profileResampled(const ProfileImage&image,unsigned width,unsigned height){
    bounded(image.width,image.height);bounded(width,height);need(image.rgba.size()==std::size_t(image.width)*image.height*4,"Profile image size");
    return artworkResampled(image,1,{0,0,double(image.width),double(image.height)},width,height);
}
ProfileImage profileWorkingCopy(const ProfileImage&image,unsigned maximum){
    need(maximum>0,"Profile working copy bound");const unsigned largest=std::max(image.width,image.height);
    if(largest<=maximum)return image;
    const double s=double(maximum)/largest;
    return profileResampled(image,std::max(1u,static_cast<unsigned>(std::lround(image.width*s))),std::max(1u,static_cast<unsigned>(std::lround(image.height*s))));
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
