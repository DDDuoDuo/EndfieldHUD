#include "modules/artwork_scaling.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
double linear(double c){return c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);}
// Straight 8-bit sRGB to linear light, computed once.
const std::array<double,256>&linearTable(){static const auto table=[]{std::array<double,256>t{};for(unsigned i=0;i<256;++i)t[i]=linear(i/255.);return t;}();return table;}
// Premultiplied 8-bit (alpha, value) to premultiplied linear light, computed
// once with the exact per-sample expression, so lookups are bit-identical to
// evaluating it (a portrait crop of a premultiplied working copy otherwise
// spends almost all of its time in std::pow).
const std::vector<double>&premultipliedLinearTable(){
    static const auto table=[]{std::vector<double>t(256*256);
        for(unsigned alpha=0;alpha<256;++alpha)for(unsigned value=0;value<256;++value){
            const double a=alpha/255.,v=value/255.;t[std::size_t(alpha)*256+value]=a>0?linear(std::min(1.,v/a))*a:0;}
        return t;}();
    return table;
}
// Measured against Core Image: a three-lobe window when reducing and a
// two-lobe window when enlarging an axis.
double lanczos(double x,double a){
    constexpr double pi=3.14159265358979323846;x=std::abs(x);
    if(x<1e-12)return 1;if(x>=a)return 0;const double px=pi*x;return a*std::sin(px)*std::sin(px/a)/(px*px);
}
using Taps=std::vector<std::pair<long,double>>;
}
double artworkLinear(double c)noexcept{return linear(std::clamp(c,0.,1.));}
double artworkEncoded(double l)noexcept{l=std::clamp(l,0.,1.);return l<=.0031308?l*12.92:1.055*std::pow(l,1/2.4)-.055;}
std::vector<double>artworkGaussian(double sigma){
    if(!(sigma>1e-6))return {1.0};
    const auto radius=static_cast<int>(std::ceil(3*sigma));
    std::vector<double>weights(static_cast<std::size_t>(radius)*2+1);
    double total=0;
    for(int k=-radius;k<=radius;++k){
        const double w=std::exp(-double(k)*k/(2*sigma*sigma));
        weights[static_cast<std::size_t>(k+radius)]=w;total+=w;
    }
    for(auto&w:weights)w/=total;
    return weights;
}
ProfileImage artworkResampled(const ProfileImage&source,int orientation,core::Rect region,unsigned outW,unsigned outH,ArtworkEdges edges,const ArtworkFinish*finish){
    need(source.width>0&&source.height>0&&source.rgba.size()==std::size_t(source.width)*source.height*4,"Artwork source size");
    need(outW>0&&outH>0&&outW<=16384&&outH<=16384,"Artwork output size");
    need(std::isfinite(region.x)&&std::isfinite(region.y)&&std::isfinite(region.width)&&std::isfinite(region.height)&&region.width>0&&region.height>0,"Artwork region");
    if(orientation<1||orientation>8)orientation=1;
    const bool premultipliedSource=source.premultiplied;const bool swap=orientation>=5;const bool clamped=edges==ArtworkEdges::clamped;
    const double width=swap?source.height:source.width,height=swap?source.width:source.height;
    const auto stored=[&](unsigned x,unsigned y)->std::size_t{
        const unsigned W=source.width,H=source.height;unsigned sx{},sy{};
        switch(orientation){case 1:sx=x;sy=y;break;case 2:sx=W-1-x;sy=y;break;case 3:sx=W-1-x;sy=H-1-y;break;case 4:sx=x;sy=H-1-y;break;
            case 5:sx=y;sy=x;break;case 6:sx=y;sy=H-1-x;break;case 7:sx=W-1-y;sy=H-1-x;break;default:sx=W-1-y;sy=x;break;}
        return (std::size_t(sy)*W+sx)*4;
    };
    const double sx=outW/region.width,sy=outH/region.height;
    const unsigned margin=finish?finish->margin:0;
    need(margin<=4096,"Artwork finish margin");
    const unsigned fullW=outW+2*margin,fullH=outH+2*margin;
    // Core Image order: crop the oriented source to the fractional region, translate
    // it to the origin (a bilinear resample of the source at region + k + 0.5),
    // then a separable Lanczos-3 over that grid, in premultiplied linear light.
    // Clear edges: grid samples whose centers fall outside the crop are clear,
    // so edges fade. Clamped edges: the grid reaches as far as the taps do and
    // reads the picture there (clamped to its own edge pixels).
    // Only the crop's bilinear support is linearized: a 128-megapixel avatar
    // never becomes a full-size floating-point copy.
    const auto&lut=linearTable();const double*premultipliedLUT=premultipliedSource?premultipliedLinearTable().data():nullptr;
    const auto pixel=[&](unsigned x,unsigned y,std::array<double,4>&out){
        const auto s=stored(x,y);const double a=source.rgba[s+3]/255.;
        if(premultipliedLUT){const auto*alphaRow=premultipliedLUT+std::size_t(source.rgba[s+3])*256;for(unsigned c=0;c<3;++c)out[c]=alphaRow[source.rgba[s+c]];}
        else for(unsigned c=0;c<3;++c)out[c]=lut[source.rgba[s+c]]*a;
        out[3]=a;
    };
    const unsigned gw=static_cast<unsigned>(std::max(0.,std::floor(region.width-.5)+1)),gh=static_cast<unsigned>(std::max(0.,std::floor(region.height-.5)+1));
    need(gw&&gh&&gw<=65536&&gh<=65536,"Profile portrait crop is empty or unbounded");
    const auto axis=[](double position,unsigned count,unsigned&a,unsigned&b,double&t){
        const double x=std::clamp(position-.5,0.,double(count-1));a=static_cast<unsigned>(std::floor(x));b=std::min(a+1,count-1);t=x-a;};
    const auto taps=[&](double center,double scale,unsigned count)->Taps{
        // An axis at factor 1 is copied (the Mac draws it without Lanczos).
        if(scale==1){const long k=static_cast<long>(std::floor(center));if(clamped||(k>=0&&k<static_cast<long>(count)))return {{k,1.}};return {};}
        const double factor=std::max(1.,1/scale),lobes=scale<1?3:2;
        Taps out;const double support=lobes*factor;double total{};
        const long first=static_cast<long>(std::floor(center-support)),last=static_cast<long>(std::ceil(center+support));
        for(long k=first;k<=last;++k){const double w=lanczos((k+.5-center)/factor,lobes);if(w==0)continue;total+=w;if(!clamped&&(k<0||k>=static_cast<long>(count)))continue;out.push_back({k,w});}
        if(total!=0)for(auto&v:out)v.second/=total;
        return out;
    };
    std::vector<Taps>horizontal(fullW),vertical(fullH);
    long firstColumn{0},lastColumn{0},firstRow{0};bool anyColumn{},anyRow{};std::size_t window{1};
    for(unsigned i=0;i<fullW;++i){horizontal[i]=taps((static_cast<long>(i)-static_cast<long>(margin)+.5)/sx,sx,gw);
        for(const auto&tap:horizontal[i]){firstColumn=anyColumn?std::min(firstColumn,tap.first):tap.first;lastColumn=anyColumn?std::max(lastColumn,tap.first):tap.first;anyColumn=true;}}
    for(unsigned j=0;j<fullH;++j){vertical[j]=taps((static_cast<long>(j)-static_cast<long>(margin)+.5)/sy,sy,gh);if(vertical[j].empty())continue;
        firstRow=anyRow?std::min(firstRow,vertical[j].front().first):vertical[j].front().first;anyRow=true;
        window=std::max<std::size_t>(window,static_cast<std::size_t>(vertical[j].back().first-vertical[j].front().first+1));}
    const std::size_t columns=anyColumn?static_cast<std::size_t>(lastColumn-firstColumn+1):0;
    need(columns<=65536+2*16384,"Artwork grid is unbounded");
    // Sliding window of horizontally filtered grid rows: memory is bounded by
    // the vertical support, never by the source or crop size.
    std::vector<double>row(columns*4),ring(window*fullW*4);std::vector<long>held(window,0);std::vector<bool>filled(window);std::array<double,4>p00{},p10{},p01{},p11{};
    const auto slotOf=[&](long l){return static_cast<std::size_t>(l-firstRow)%window;};
    const auto filtered=[&](long l)->const double*{
        const auto index=slotOf(l);auto*slot=&ring[index*fullW*4];if(filled[index]&&held[index]==l)return slot;
        unsigned y0,y1;double ty;axis(region.y+static_cast<double>(l)+.5,unsigned(height),y0,y1,ty);
        for(std::size_t n=0;n<columns;++n){const long k=firstColumn+static_cast<long>(n);unsigned x0,x1;double tx;axis(region.x+static_cast<double>(k)+.5,unsigned(width),x0,x1,tx);
            pixel(x0,y0,p00);pixel(x1,y0,p10);pixel(x0,y1,p01);pixel(x1,y1,p11);auto*d=&row[n*4];
            for(unsigned c=0;c<4;++c)d[c]=(p00[c]*(1-tx)+p10[c]*tx)*(1-ty)+(p01[c]*(1-tx)+p11[c]*tx)*ty;}
        for(unsigned i=0;i<fullW;++i){auto*d=&slot[std::size_t(i)*4];d[0]=d[1]=d[2]=d[3]=0;for(const auto&[k,w]:horizontal[i]){const auto*g=&row[static_cast<std::size_t>(k-firstColumn)*4];for(unsigned c=0;c<4;++c)d[c]+=g[c]*w;}}
        held[index]=l;filled[index]=true;return slot;
    };
    ProfileImage out{outW,outH,std::vector<std::uint8_t>(std::size_t(outW)*outH*4),true};
    const auto encode=[&](const double*v,unsigned i,unsigned j){
        const double a=std::clamp(v[3],0.,1.);auto*d=&out.rgba[(std::size_t(j)*outW+i)*4];
        for(unsigned c=0;c<3;++c)d[c]=static_cast<std::uint8_t>(std::lround(a>0?artworkEncoded(std::clamp(v[c]/a,0.,1.))*a*255:0));
        d[3]=static_cast<std::uint8_t>(std::lround(a*255));
    };
    std::vector<double>buffer(finish?std::size_t(fullW)*fullH*4:0);
    for(unsigned j=0;j<fullH;++j)for(unsigned i=0;i<fullW;++i){
        std::array<double,4>v{};for(const auto&[k,w]:vertical[j]){const auto*s=filtered(k)+std::size_t(i)*4;for(unsigned c=0;c<4;++c)v[c]+=s[c]*w;}
        if(finish)std::copy(v.begin(),v.end(),buffer.begin()+static_cast<std::ptrdiff_t>((std::size_t(j)*fullW+i)*4));else encode(v.data(),i,j);
    }
    if(finish){
        if(finish->apply)finish->apply(buffer,fullW,fullH);
        for(unsigned j=0;j<outH;++j)for(unsigned i=0;i<outW;++i)encode(&buffer[((std::size_t(j)+margin)*fullW+i+margin)*4],i,j);
    }
    return out;
}
ArtworkFinish artworkBlurAndExposure(double sigma,double ev){
    auto kernel=artworkGaussian(sigma);const unsigned radius=static_cast<unsigned>(kernel.size()/2);const double gain=std::exp2(ev);
    return {radius,[kernel=std::move(kernel),radius,gain](std::vector<double>&rgba,unsigned w,unsigned h){
        // Separable passes; a tap beyond the rendered margin reads the nearest
        // rendered pixel (only the margin's own pixels reach that far).
        std::vector<double>pass(rgba.size());const long r=static_cast<long>(radius);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto*d=&pass[(std::size_t(y)*w+x)*4];d[0]=d[1]=d[2]=d[3]=0;
            for(long k=-r;k<=r;++k){const auto sxi=static_cast<std::size_t>(std::clamp(static_cast<long>(x)+k,0L,static_cast<long>(w)-1));const double wk=kernel[static_cast<std::size_t>(k+r)];
                const auto*s=&rgba[(std::size_t(y)*w+sxi)*4];for(unsigned c=0;c<4;++c)d[c]+=s[c]*wk;}}
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto*d=&rgba[(std::size_t(y)*w+x)*4];d[0]=d[1]=d[2]=d[3]=0;
            for(long k=-r;k<=r;++k){const auto syi=static_cast<std::size_t>(std::clamp(static_cast<long>(y)+k,0L,static_cast<long>(h)-1));const double wk=kernel[static_cast<std::size_t>(k+r)];
                const auto*s=&pass[(syi*w+x)*4];for(unsigned c=0;c<4;++c)d[c]+=s[c]*wk;}
            for(unsigned c=0;c<3;++c)d[c]*=gain;}
    }};
}
ProfileImage profileStraight(const ProfileImage&image){
    if(!image.premultiplied)return image;ProfileImage out=image;out.premultiplied=false;
    for(std::size_t i=0;i<out.rgba.size();i+=4){const unsigned a=out.rgba[i+3];for(unsigned c=0;c<3;++c)out.rgba[i+c]=a==0?0:static_cast<std::uint8_t>(std::min(255u,(out.rgba[i+c]*255u+a/2)/a));}
    return out;
}
}
