#include "native/layer_mask.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;
using Matrix=core::Matrix4;
using Point=core::Point;
void require(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
double number(const Json& j,double fallback=0){
    if(j.isNull())return fallback;
    require(j.isNumber(),"Invalid mask number type");const auto v=j.number();
    require(std::isfinite(v)&&std::abs(v)<=1e8,"Nonfinite or excessive mask number");return v;
}
bool flag(const Json& j,bool fallback=false){if(j.isNull())return fallback;require(j.isBool(),"Invalid mask boolean");return j.boolean();}
std::string text(const Json& j){if(j.isNull())return {};require(j.isString(),"Invalid mask string");require(j.string().size()<=4096,"Mask string exceeds its limit");return j.string();}
const Json::Array& array(const Json& j,std::size_t count){require(j.isArray()&&j.array().size()<=count,"Invalid or excessive mask array");return j.array();}
Point point(const Json& j,Point fallback={}){if(j.isNull())return fallback;const auto&a=array(j,2);require(a.size()==2,"Mask point needs two coordinates");return {number(a[0]),number(a[1])};}
Matrix matrix(const Json& j){
    Matrix out;if(j.isNull())return out;const auto& columns=array(j,4);require(columns.size()==4,"Mask transform needs four columns");
    for(unsigned c=0;c<4;++c){const auto& values=array(columns[c],4);require(values.size()==4,"Mask transform column needs four rows");for(unsigned r=0;r<4;++r)out.values[c*4+r]=number(values[r]);}return out;
}
Matrix local(const Json& j){
    const auto& b=array(j["bounds"],4);require(b.size()==4,"Mask bounds needs four values");
    const double x=number(b[0]),y=number(b[1]),w=number(b[2]),h=number(b[3]);require(w>=0&&h>=0,"Negative mask bounds");
    const auto p=point(j["position"]),a=point(j["anchorPoint"],{.5,.5});
    return Matrix::translation(p.x,p.y,number(j["zPosition"]))*matrix(j["transform"])
        *Matrix::translation(-x-a.x*w,-y-a.y*h,-number(j["anchorPointZ"]));
}
double alpha(const Json& c){
    if(c.isNull())return 0;const auto& values=array(c["sRGB"],4);require(values.size()==4,"Mask color requires explicit sRGB RGBA");
    for(const auto&v:values)number(v);const double a=number(values[3]);require(a>=0&&a<=1,"Invalid mask fill alpha");return a;
}
bool convex(const std::array<Point,4>& p){
    double scale=1;for(unsigned i=0;i<4;++i){const auto&a=p[i],b=p[(i+1)%4];scale=std::max(scale,(a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y));}
    double sign=0;
    for(unsigned i=0;i<4;++i){const auto&a=p[i],b=p[(i+1)%4],c=p[(i+2)%4];const double cross=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
        if(std::abs(cross)<=scale*1e-12)return false;if(sign==0)sign=cross;else if((sign>0)!=(cross>0))return false;}
    return true;
}
Matrix unitToQuad(const std::array<Point,4>& p){
    // Eight equations solve x=(a*u+b*v+c)/(g*u+h*v+1), likewise
    // y=(d*u+e*v+f)/(g*u+h*v+1). This retains projective edges.
    constexpr std::array<Point,4> unit{{{0,0},{1,0},{1,1},{0,1}}};
    double equations[8][9]{};
    for(unsigned i=0;i<4;++i){const auto [u,v]=unit[i];const auto [x,y]=p[i];auto& rx=equations[i*2];auto& ry=equations[i*2+1];
        rx[0]=u;rx[1]=v;rx[2]=1;rx[6]=-u*x;rx[7]=-v*x;rx[8]=x;
        ry[3]=u;ry[4]=v;ry[5]=1;ry[6]=-u*y;ry[7]=-v*y;ry[8]=y;}
    for(unsigned c=0;c<8;++c){unsigned pivot=c;for(unsigned r=c+1;r<8;++r)if(std::abs(equations[r][c])>std::abs(equations[pivot][c]))pivot=r;
        require(std::abs(equations[pivot][c])>1e-14,"Degenerate mask homography");for(unsigned k=0;k<9;++k)std::swap(equations[c][k],equations[pivot][k]);
        const auto divisor=equations[c][c];for(auto& n:equations[c])n/=divisor;
        for(unsigned r=0;r<8;++r)if(r!=c){const auto factor=equations[r][c];for(unsigned k=0;k<9;++k)equations[r][k]-=factor*equations[c][k];}}
    const double a=equations[0][8],b=equations[1][8],c=equations[2][8],d=equations[3][8],e=equations[4][8],f=equations[5][8],g=equations[6][8],h=equations[7][8];
    require(1+g>0&&1+h>0&&1+g+h>0,"Mask homography crosses its horizon");
    Matrix result;result.values={a,d,0,g, b,e,0,h, 0,0,1,0, c,f,0,1};return result;
}
}
LayerMaskProjection projectLayerMask(const Json& mask,const Matrix& ownerWorld){
    require(mask.isObject()&&ownerWorld.finite(),"Invalid projected mask input");
    LayerMaskProjection result;const auto id=text(mask["id"]);
    auto unsupported=[&](std::string reason){result.unsupported.push_back({id,std::move(reason)});return result;};
    const auto opacity=number(mask["opacity"],1);require(opacity>=0&&opacity<=1,"Invalid mask opacity");
    if(flag(mask["hidden"])||opacity==0){result.clipsAll=true;return result;}
    if(text(mask["kind"])!="shape")return unsupported("only filled quadrilateral shape masks are projected");
    const auto className=text(mask["class"]);if(!className.empty()&&className!="CAShapeLayer")return unsupported("custom mask drawing behavior");
    if(flag(mask["geometryFlipped"]))return unsupported("flipped mask geometry requires an explicit source adapter");
    if(!mask["contents"].isNull()||!mask["mask"].isNull()||!mask["backgroundColor"].isNull()||number(mask["borderWidth"])>0||number(mask["shadowOpacity"])>0)
        return unsupported("mask content, nested mask, background, border or shadow requires alpha compositing");
    if(!mask["children"].isNull()&&!array(mask["children"],4096).empty())return unsupported("mask child drawing requires alpha compositing");
    if(!mask["animationKeys"].isNull()&&!array(mask["animationKeys"],1024).empty())return unsupported("animated mask requires its evaluated presentation state");
    const auto& shape=mask["shape"];require(shape.isObject(),"Missing mask shape descriptor");
    if(alpha(shape["strokeColor"])>0&&number(shape["lineWidth"],1)>0)return unsupported("stroked masks require alpha compositing");
    const auto coverage=alpha(shape["fillColor"])*opacity;
    if(coverage==0||shape["path"].isNull()){result.clipsAll=true;return result;}
    if(coverage!=1)return unsupported("partial mask alpha requires group compositing");
    const auto& path=array(shape["path"],65536);if(path.empty()){result.clipsAll=true;return result;}
    const auto fillRule=text(shape["fillRule"]);if(!fillRule.empty()&&fillRule!="non-zero"&&fillRule!="even-odd"&&fillRule!="evenOdd")return unsupported("unknown mask fill rule");
    std::array<Point,4> corners{};unsigned count=0;bool closed=false,returnedToStart=false;
    for(const auto& command:path){
        const auto op=text(command["op"]);const auto& pts=array(command["points"],3);
        if(op=="close"){if(closed||count!=4||!pts.empty())return unsupported("mask path must contain one four-corner contour");closed=true;continue;}
        if(closed||returnedToStart||pts.size()!=1||(count==0?op!="move":op!="line"))return unsupported("mask path must contain straight sides in one contour");
        const auto p=point(pts[0]);
        if(count==4){if(p!=corners[0])return unsupported("mask path has more than four corners");returnedToStart=true;continue;}
        corners[count++]=p;
    }
    if(count!=4||!convex(corners))return unsupported("mask quadrilateral is degenerate, concave or self-intersecting");
    const auto placement=local(mask); // malformed numeric data must still throw
    try{
        const auto world=ownerWorld*placement*unitToQuad(corners);
        result.plane=PlaneMask{core::source::inverseSourceMatrix(world),{0,0,1,1}};
    }catch(const std::invalid_argument&){return unsupported("mask placement or quadrilateral is singular");}
    // A bounds clip can cut the quadrilateral into a polygon with more sides.
    // Do not substitute its bounding box. Current exported masks do not use it.
    if(flag(mask["masksToBounds"])){
        result.plane.reset();return unsupported("mask bounds clipping requires intersected geometry");
    }
    return result;
}
} // namespace endfield::native
