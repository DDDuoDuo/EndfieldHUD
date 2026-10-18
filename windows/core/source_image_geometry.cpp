#include "core/source_image_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

// The unchanged Swift SIMD path rounds the multiply before the following add.
// FMA changes an actual trimmed sprite's final Float by1ULP at a halfway case.
// Preserve those authored operation boundaries independently of build flags.
#if defined(_MSC_VER)
#pragma float_control(precise,on,push)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::core::source {
namespace {
using Quad=std::array<Vec2,4>;
void require(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
double number(const Json& j,double fallback=0){if(!j.isNumber())return fallback;const double v=j.number();require(std::isfinite(v),"Image field is nonfinite");return v;}
bool flag(const Json& j,bool fallback=false){if(j.isNull())return fallback;if(j.isBool())return j.boolean();if(j.isNumber())return number(j)!=0;return fallback;}
int integer(const Json& j){const double v=number(j);require(v>=std::numeric_limits<int>::min()&&v<=std::numeric_limits<int>::max(),"Image enum exceeds integer range");return static_cast<int>(v);}
Vec2 vector(const Json& j){return {number(j["x"]),number(j["y"])};}
template<class Range>bool finite(const Range& r){return std::all_of(r.begin(),r.end(),[](double v){return std::isfinite(v);});}
double lerp(double a,double b,double t){return a+(b-a)*t;}
Quad points(Vec2 low,Vec2 high){return {{low,{low[0],high[1]},high,{high[0],low[1]}}};}
Quad subQuad(const Quad& q,Vec2 low,Vec2 high){Vec2 a,b;for(unsigned i=0;i<2;++i){a[i]=q[0][i]+(q[2][i]-q[0][i])*low[i];b[i]=q[0][i]+(q[2][i]-q[0][i])*high[i];}return points(a,b);}
double nearestEven(double v){const double low=std::floor(v),fraction=v-low;if(fraction<.5)return low;if(fraction>.5)return low+1;return std::fmod(low,2)==0?low:low+1;}
Quad drawingPoints(const SourceSprite& sprite,const SourceRect& rect,Vec2 pivot,bool preserveAspect){
    auto origin=rect.origin,size=rect.size;
    if(preserveAspect){const double ratio=sprite.size[0]/sprite.size[1];if(ratio>size[0]/size[1]){const double old=size[1];size[1]=size[0]/ratio;origin[1]+=(old-size[1])*pivot[1];}
        else{const double old=size[0];size[0]=size[1]*ratio;origin[0]+=(old-size[0])*pivot[0];}}
    const double w=nearestEven(sprite.size[0]),h=nearestEven(sprite.size[1]);require(w>0&&h>0,"Sprite rounds to zero drawing dimensions");
    const Vec2 low{origin[0]+size[0]*(sprite.padding[0]/w),origin[1]+size[1]*(sprite.padding[1]/h)};
    const Vec2 high{origin[0]+size[0]*((w-sprite.padding[2])/w),origin[1]+size[1]*((h-sprite.padding[3])/h)};
    return points(low,high);
}
Quad radialCut(Quad q,double cosine,double sine,bool invert,int corner){
    const int i0=corner,i1=(corner+1)%4,i2=(corner+2)%4,i3=(corner+3)%4;
    if((corner&1)==1){
        if(sine>cosine){cosine/=sine;sine=1;if(invert){q[i1][0]=lerp(q[i0][0],q[i2][0],cosine);q[i2][0]=q[i1][0];}}
        else if(cosine>sine){sine/=cosine;cosine=1;if(!invert){q[i2][1]=lerp(q[i0][1],q[i2][1],sine);q[i3][1]=q[i2][1];}}
        else{cosine=1;sine=1;}
        if(!invert)q[i3][0]=lerp(q[i0][0],q[i2][0],cosine);else q[i1][1]=lerp(q[i0][1],q[i2][1],sine);
    }else{
        if(cosine>sine){sine/=cosine;cosine=1;if(!invert){q[i1][1]=lerp(q[i0][1],q[i2][1],sine);q[i2][1]=q[i1][1];}}
        else if(sine>cosine){cosine/=sine;sine=1;if(invert){q[i2][0]=lerp(q[i0][0],q[i2][0],cosine);q[i3][0]=q[i2][0];}}
        else{cosine=1;sine=1;}
        if(invert)q[i3][1]=lerp(q[i0][1],q[i2][1],sine);else q[i1][0]=lerp(q[i0][0],q[i2][0],cosine);
    }return q;
}
void validate(const SourceImageParameters& p,const SourceSprite* s,const SourceRect& r,Vec2 pivot,double canvas,std::optional<double> fill){
    require(finite(r.origin)&&finite(r.size)&&finite(pivot)&&std::isfinite(canvas)&&std::isfinite(p.pixelsPerUnitMultiplier)&&std::isfinite(p.fillAmount)&&(!fill||std::isfinite(*fill)),"Nonfinite image geometry input");
    if(s)require(finite(s->size)&&finite(s->padding)&&finite(s->border)&&finite(s->outer)&&finite(s->inner)&&std::isfinite(s->pixelsPerUnit)&&s->size[0]>0&&s->size[1]>0,"Invalid source sprite geometry");
}
}
SourceSprite SourceSprite::fromSource(const Json& source,const Json& texture){
    const auto& raw=source["raw_sprite"];const auto& rd=source["effective_render_data"];const auto& spriteRect=raw["m_Rect"];const auto& textureRect=rd["textureRect"];
    SourceSprite out;out.size={number(spriteRect["width"]),number(spriteRect["height"])};const auto offset=vector(rd["textureRectOffset"]);
    out.padding={offset[0],offset[1],out.size[0]-offset[0]-number(textureRect["width"]),out.size[1]-offset[1]-number(textureRect["height"])};
    const auto& b=raw["m_Border"];out.border={number(b["x"]),number(b["y"]),number(b["z"]),number(b["w"])};
    const double width=number(texture["width"]),height=number(texture["height"]);
    require(out.size[0]>0&&out.size[1]>0&&width>0&&height>0&&source["packing"]["packing_rotation"].isString()&&source["packing"]["packing_rotation"].string()=="kSPRNone"&&texture["id"].isString(),"Unsupported/incomplete original sprite");
    out.outer={number(textureRect["x"])/width,number(textureRect["y"])/height,(number(textureRect["x"])+number(textureRect["width"]))/width,(number(textureRect["y"])+number(textureRect["height"]))/height};
    out.inner={out.outer[0]+(out.border[0]-out.padding[0])/width,out.outer[1]+(out.border[1]-out.padding[1])/height,out.outer[2]-(out.border[2]-out.padding[2])/width,out.outer[3]-(out.border[3]-out.padding[3])/height};
    out.pixelsPerUnit=number(raw["m_PixelsToUnits"],100);out.textureID=texture["id"].string();require(out.textureID.size()<=4096&&finite(out.inner)&&finite(out.outer)&&finite(out.padding),"Excessive/nonfinite sprite metadata");return out;
}
SourceImageParameters SourceImageParameters::fromComponent(const WatchComponent& c){SourceImageParameters p;
    p.type=integer(c["m_Type"]);p.fillMethod=integer(c["m_FillMethod"]);p.fillOrigin=integer(c["m_FillOrigin"]);
    p.useSpriteMesh=flag(c["m_UseSpriteMesh"]);p.preserveAspect=flag(c["m_PreserveAspect"]);p.fillCenter=flag(c["m_FillCenter"],true);p.fillClockwise=flag(c["m_FillClockwise"],true);
    p.pixelsPerUnitMultiplier=number(c["m_PixelsPerUnitMultiplier"],1);p.fillAmount=number(c["m_FillAmount"],1);return p;
}
void SourceImageMesh::reset()noexcept{positions.clear();uv.clear();indices.clear();}
void SourceImageMesh::release()noexcept{SourceImageMesh empty;positions.swap(empty.positions);uv.swap(empty.uv);indices.swap(empty.indices);}
void ImageGeometryBuilder::quad(const Quad& xy,const Quad& texture){
    require(mesh_.positions.size()/4<maximumQuads,"Source image exceeds its bounded total quad storage");
    for(unsigned i=0;i<4;++i)for(unsigned c=0;c<2;++c)require(std::isfinite(xy[i][c])&&std::abs(xy[i][c])<=std::numeric_limits<float>::max()&&std::isfinite(texture[i][c])&&std::abs(texture[i][c])<=std::numeric_limits<float>::max(),"Nonfinite or out-of-Float image vertex");
    const auto start=static_cast<std::uint32_t>(mesh_.positions.size());for(unsigned i=0;i<4;++i){mesh_.positions.push_back({static_cast<float>(xy[i][0]),static_cast<float>(xy[i][1]),0,1});mesh_.uv.push_back({static_cast<float>(texture[i][0]),static_cast<float>(texture[i][1])});}
    const std::array<std::uint32_t,6> indices{start,start+1,start+2,start+2,start+3,start};mesh_.indices.insert(mesh_.indices.end(),indices.begin(),indices.end());
}
void ImageGeometryBuilder::radial(const Quad& xy,const Quad& uv,double amount,bool invert,int corner){
    if(amount<.001)return;if((corner&1)==1)invert=!invert;if(!invert&&amount>.999){quad(xy,uv);return;}
    const double angle=(invert?1-amount:amount)*std::numbers::pi/2;const double cosine=std::cos(angle),sine=std::sin(angle);
    quad(radialCut(xy,cosine,sine,invert,corner),radialCut(uv,cosine,sine,invert,corner));
}
const SourceImageMesh& ImageGeometryBuilder::build(const SourceImageParameters& image,const SourceSprite* sprite,const SourceRect& rect,Vec2 pivot,double canvas,std::optional<double> fill){
    try{
        validate(image,sprite,rect,pivot,canvas,fill);
        if(previous_&&previous_->image==image&&previous_->rect==rect&&previous_->pivot==pivot&&previous_->canvasPPU==canvas&&previous_->fill==fill&&bool(previous_->sprite)==bool(sprite)&&(!sprite||*previous_->sprite==*sprite)){++counts_.reuses;return mesh_;}
        mesh_.reset();
        auto finish=[&]() -> const SourceImageMesh& {
            if(!previous_)previous_.emplace();auto& key=*previous_;key.image=image;key.rect=rect;key.pivot=pivot;key.canvasPPU=canvas;key.fill=fill;
            if(sprite){if(key.sprite)*key.sprite=*sprite;else key.sprite.emplace(*sprite);}else key.sprite.reset();
            ++counts_.builds;return mesh_;
        };
        if(rect.size[0]<=0||rect.size[1]<=0)return finish();require(!image.useSpriteMesh,"Unexpected UIImage sprite-mesh flag");
        if(!sprite){quad(points(rect.origin,{rect.origin[0]+rect.size[0],rect.origin[1]+rect.size[1]}),points({0,0},{1,1}));return finish();}
        const auto& s=*sprite;const auto uv=points({s.outer[0],s.outer[1]},{s.outer[2],s.outer[3]});const int type=image.type;
        if(type==0||(type==1&&s.border==std::array<double,4>{})){quad(drawingPoints(s,rect,pivot,type==0&&image.preserveAspect),uv);}
        else if(type==1||type==2){
            const double ppu=s.pixelsPerUnit/canvas*image.pixelsPerUnitMultiplier;require(ppu>0&&std::isfinite(ppu),"Invalid source sprite pixelsPerUnit");
            std::array<double,4> border,padding;for(unsigned i=0;i<4;++i){border[i]=s.border[i]/ppu;padding[i]=s.padding[i]/ppu;}
            for(unsigned axis=0;axis<2;++axis){const double combined=border[axis]+border[axis+2];if(combined>rect.size[axis]&&combined!=0){const double ratio=rect.size[axis]/combined;border[axis]*=ratio;border[axis+2]*=ratio;}}
            const std::array<double,4> x{rect.origin[0]+padding[0],rect.origin[0]+border[0],rect.origin[0]+rect.size[0]-border[2],rect.origin[0]+rect.size[0]-padding[2]},
                y{rect.origin[1]+padding[1],rect.origin[1]+border[1],rect.origin[1]+rect.size[1]-border[3],rect.origin[1]+rect.size[1]-padding[3]},
                u{s.outer[0],s.inner[0],s.inner[2],s.outer[2]},v{s.outer[1],s.inner[1],s.inner[3],s.outer[3]};
            const Vec2 tileSize{std::max(0.,s.size[0]-s.border[0]-s.border[2])/ppu,std::max(0.,s.size[1]-s.border[1]-s.border[3])/ppu};
            require(finite(x)&&finite(y)&&finite(border)&&finite(padding)&&finite(tileSize),"Nonfinite sliced/tiled coordinates");
            for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column){
                if(column==1&&row==1&&!image.fillCenter)continue;if(x[column+1]<=x[column]||y[row+1]<=y[row])continue;
                const bool tileX=type==2&&column==1,tileY=type==2&&row==1;
                const double sx=tileX&&tileSize[0]>0?tileSize[0]:x[column+1]-x[column],sy=tileY&&tileSize[1]>0?tileSize[1]:y[row+1]-y[row];
                const double nx=std::max(1.,std::ceil((x[column+1]-x[column])/sx)),ny=std::max(1.,std::ceil((y[row+1]-y[row])/sy));
                require(std::isfinite(nx)&&std::isfinite(ny)&&nx*ny<=maximumCellQuads,"Source tiled image exceeds Unity mesh limit");
                for(unsigned iy=0;iy<static_cast<unsigned>(ny);++iy)for(unsigned ix=0;ix<static_cast<unsigned>(nx);++ix){
                    const Vec2 low{x[column]+double(ix)*sx,y[row]+double(iy)*sy},high{std::min(low[0]+sx,x[column+1]),std::min(low[1]+sy,y[row+1])};
                    quad(points(low,high),points({u[column],v[row]},{lerp(u[column],u[column+1],(high[0]-low[0])/sx),lerp(v[row],v[row+1],(high[1]-low[1])/sy)}));
                }
            }
        }else if(type==3){
            const double amount=std::min(1.,std::max(0.,fill.value_or(image.fillAmount)));if(amount<.001)return finish();
            const int method=image.fillMethod,origin=image.fillOrigin;const bool clockwise=image.fillClockwise;
            const auto xy=drawingPoints(s,rect,pivot,image.preserveAspect);if(amount>=1){quad(xy,uv);return finish();}
            if(method==0||method==1){const int axis=method;const double start=origin==1?1-amount:0,end=origin==1?1:amount;Vec2 low{0,0},high{1,1};low[axis]=start;high[axis]=end;quad(subQuad(xy,low,high),subQuad(uv,low,high));}
            else if(method>=2&&method<=4){require(origin>=0&&origin<=3,"Invalid source radial fill origin");
                if(method==2)radial(xy,uv,amount,clockwise,origin%4);
                else if(method==3)for(int side=0;side<2;++side){const int even=origin>1?1:0;Vec2 low,high;
                    if(origin==0||origin==2){low={side==even?0.:.5,0};high={side==even?.5:1.,1};}else{low={0,side==even?.5:0.};high={1,side==even?1.:.5};}
                    const double part=amount*2-double(clockwise?side:1-side);radial(subQuad(xy,low,high),subQuad(uv,low,high),std::clamp(part,0.,1.),clockwise,(side+origin+3)%4);}
                else for(int corner=0;corner<4;++corner){const Vec2 low{corner<2?0.:.5,corner==0||corner==3?0.:.5},high{low[0]+.5,low[1]+.5};
                    const double part=amount*4-double(clockwise?(corner+origin)%4:3-(corner+origin)%4);radial(subQuad(xy,low,high),subQuad(uv,low,high),std::clamp(part,0.,1.),clockwise,(corner+2)%4);}
            }else throw std::invalid_argument("Unsupported source fill method");
        }else throw std::invalid_argument("Unsupported source image type");
        return finish();
    }catch(...){mesh_.reset();previous_.reset();throw;}
}
void ImageGeometryBuilder::reset()noexcept{mesh_.reset();previous_.reset();}
void ImageGeometryBuilder::release()noexcept{mesh_.release();previous_.reset();}
ImageGeometryStats ImageGeometryBuilder::stats()const noexcept{auto result=counts_;result.retainedBytes=mesh_.positions.capacity()*sizeof(mesh_.positions.front())+mesh_.uv.capacity()*sizeof(mesh_.uv.front())+mesh_.indices.capacity()*sizeof(mesh_.indices.front());return result;}
} // namespace endfield::core::source
#if defined(_MSC_VER)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
