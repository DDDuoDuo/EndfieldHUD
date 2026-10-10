#include "modules/media_assembly_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::modules {
MediaAssemblyPixelPlan mediaAssemblyPixelPlan(unsigned width,unsigned height,const MediaAssemblyAdjustments&a,std::optional<unsigned>maximum,bool even){
    if(!width||!height||width>65536||height>65536||std::uint64_t(width)*height>64000000||!a.valid()||(maximum&&(!*maximum||*maximum>65536)))throw std::invalid_argument("Invalid Media Assembly pixel geometry");
    MediaAssemblyPixelPlan p;p.sourceWidth=width;p.sourceHeight=height;p.quarterTurns=(a.rotationQuarterTurns%4+4)%4;p.mirrored=a.mirrored;
    p.cropX=static_cast<int>(std::floor(width*a.crop.x));
    p.cropWidth=std::max(1u,static_cast<unsigned>(std::floor(width*a.crop.width)));
    p.cropHeight=std::max(1u,static_cast<unsigned>(std::floor(height*a.crop.height)));
    const double bottom=std::floor(height*(1-a.crop.y-a.crop.height));
    // A normalized crop can round its lower extent to -1 for a value whose
    // floating sum is one. The source includes a transparent row in that case;
    // retain this signed conversion rather than clamping to a different crop.
    const double top=double(height)-bottom-p.cropHeight;
    p.cropY=static_cast<int>(top);p.orientedWidth=p.cropWidth;p.orientedHeight=p.cropHeight;
    if(p.quarterTurns%2)std::swap(p.orientedWidth,p.orientedHeight);
    double w=p.orientedWidth,h=p.orientedHeight;
    if(maximum){const double scale=std::min(1.,double(*maximum)/std::max(w,h));w=std::floor(w*scale);h=std::floor(h*scale);}
    if(even){w=std::max(2.,std::floor(w/2)*2);h=std::max(2.,std::floor(h/2)*2);}
    if(w<1||h<1)throw std::invalid_argument("Media Assembly output is smaller than one pixel");
    p.outputWidth=static_cast<unsigned>(w);p.outputHeight=static_cast<unsigned>(h);return p;
}
core::Point MediaAssemblyPixelPlan::sourceUV(core::Point point)const noexcept{
    point=MediaAssemblyViewport::sourcePoint(point,quarterTurns,mirrored);
    return {(cropX+point.x*cropWidth)/sourceWidth,(cropY+point.y*cropHeight)/sourceHeight};
}
}
