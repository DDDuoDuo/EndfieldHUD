#include "native/layer_mask.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using ehud::data::Json;
using namespace endfield;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void close(double a,double b,const char* message){check(std::abs(a-b)<=1e-9,message);}
template<class F>void rejects(F action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
Json xy(double x,double y){return Json::Array{x,y};}
Json command(const char* op,Json::Array points={}){return Json::Object{{"op",op},{"points",std::move(points)}};}
Json color(double a=1){return Json::Object{{"sRGB",Json::Array{0,0,0,a}}};}
Json transform(const core::Matrix4& m){Json::Array columns;for(unsigned c=0;c<4;++c){Json::Array v;for(unsigned r=0;r<4;++r)v.emplace_back(m.values[c*4+r]);columns.emplace_back(std::move(v));}return columns;}
Json mask(const std::array<core::Point,4>& p){
    return Json::Object{{"id","synthetic.quad-mask"},{"class","CAShapeLayer"},{"kind","shape"},
        {"bounds",Json::Array{0,0,1280,800}},{"position",xy(640,400)},{"anchorPoint",xy(.5,.5)},
        {"shape",Json::Object{{"fillColor",color()},{"fillRule","non-zero"},
            {"path",Json::Array{command("move",{xy(p[0].x,p[0].y)}),command("line",{xy(p[1].x,p[1].y)}),
                command("line",{xy(p[2].x,p[2].y)}),command("line",{xy(p[3].x,p[3].y)}),command("close")}}}}};
}
std::array<double,4> multiply(const core::Matrix4& m,std::array<double,4> p){std::array<double,4> out{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out[r]+=m.values[c*4+r]*p[c];return out;}
core::Point project(const core::Matrix4& m,core::Point p){const auto out=multiply(m,{p.x,p.y,0,1});return {out[0]/out[3],out[1]/out[3]};}
bool accepts(const native::PlaneMask& plane,core::Point p){const auto q=multiply(plane.worldToLocal,{p.x,p.y,0,1});if(q[3]<=0)return false;return plane.bounds.contains({q[0]/q[3],q[1]/q[3]});}
void run(){
    const std::array<core::Point,4> viewport{{{0,0},{1280,0},{1280,800},{0,800}}};
    auto node=mask(viewport);auto result=native::projectLayerMask(node);
    check(result.plane.has_value()&&result.unsupported.empty()&&!result.clipsAll,"Opaque viewport rectangle converts to a plane mask");
    close(result.plane->worldToLocal.values[0],1./1280,"Viewport width maps to the unit interval");
    close(result.plane->worldToLocal.values[5],1./800,"Viewport height maps to the unit interval");
    check(accepts(*result.plane,{640,400})&&!accepts(*result.plane,{-1,400})&&!accepts(*result.plane,{640,801}),"Viewport mask excludes only outside points");
    auto explicitClosure=node;auto contour=explicitClosure["shape"]["path"].array();contour.insert(contour.end()-1,command("line",{xy(0,0)}));explicitClosure["shape"]["path"]=contour;
    check(native::projectLayerMask(explicitClosure).plane.has_value(),"An explicit closing line followed by close preserves the same quad");

    // Exact four corners from the independent current Mac desktop shell export.
    const std::array<core::Point,4> navigation{{{845.9327926031245,236.67882481725664},
        {1199.8684753703474,219.528084056539},{1250.7997683141364,587.7881324605953},{876.5158939006307,606.0382350670837}}};
    node=mask(navigation);result=native::projectLayerMask(node);
    check(result.plane.has_value()&&result.unsupported.empty(),"Authored projective navigation clip is supported");
    constexpr std::array<core::Point,4> unit{{{0,0},{1,0},{1,1},{0,1}}};
    for(unsigned i=0;i<4;++i){const auto p=project(result.plane->worldToLocal,navigation[i]);close(p.x,unit[i].x,"Projective clip corner X maps exactly");close(p.y,unit[i].y,"Projective clip corner Y maps exactly");}
    // Distinguishes the actual trapezoid from its enlarged axis-aligned box.
    check(!accepts(*result.plane,{850,600}),"Projected mask excludes the bounding-box corner outside its polygon");
    for(unsigned y=0;y<41;++y)for(unsigned x=0;x<49;++x){const core::Point p{810+x*10.13,180+y*11.17};
        check(accepts(*result.plane,p)==core::polygonContains(navigation,p),"Plane clipping agrees with the source convex contour across a screen grid");}

    // Reverse contour winding preserves coverage.
    const std::array<core::Point,4> reverse{{navigation[0],navigation[3],navigation[2],navigation[1]}};
    auto reversed=native::projectLayerMask(mask(reverse));check(reversed.plane.has_value(),"Clockwise and counterclockwise masks both convert");
    for(unsigned y=0;y<11;++y)for(unsigned x=0;x<13;++x){const core::Point p{810+x*37.1,180+y*42.3};check(accepts(*result.plane,p)==accepts(*reversed.plane,p),"Winding does not change opaque quad coverage");}

    node["bounds"]=Json::Array{10,20,100,200};node["anchorPoint"]=xy(.25,.75);node["position"]=xy(50,90);
    const auto authored=core::Matrix4::rotation(0,0,.1)*core::Matrix4::scale(1.2,.8);
    node["transform"]=transform(authored);const auto owner=core::Matrix4::translation(200,40)*core::Matrix4::scale(.9,1.1);
    result=native::projectLayerMask(node,owner);check(result.plane.has_value(),"Nonzero mask bounds and local placement are supported");
    const auto expectedWorld=owner*core::Matrix4::translation(50,90)*authored*core::Matrix4::translation(-35,-170);
    for(unsigned i=0;i<4;++i){const auto world=project(expectedWorld,navigation[i]);const auto p=project(result.plane->worldToLocal,world);
        close(p.x,unit[i].x,"Owner and mask placement compose once in X");close(p.y,unit[i].y,"Bounds origin and anchor compose once in Y");}

    node=mask(viewport);node["opacity"]=.5;result=native::projectLayerMask(node);
    check(!result.plane&&!result.clipsAll&&!result.unsupported.empty(),"Partial mask alpha is not incorrectly replaced with a hard clip");
    node["opacity"]=0;result=native::projectLayerMask(node);check(result.clipsAll&&!result.plane&&result.unsupported.empty(),"Fully transparent mask clips every descendant");
    node=mask(viewport);node["hidden"]=true;check(native::projectLayerMask(node).clipsAll,"Hidden mask clips all content");
    node=mask(viewport);node["shape"]["path"]=Json::Array{};check(native::projectLayerMask(node).clipsAll,"Empty shape mask clips all content");
    node=mask(viewport);node["shape"]["strokeColor"]=color();result=native::projectLayerMask(node);check(!result.plane&&!result.unsupported.empty(),"Stroked mask requires explicit alpha compositing");
    const std::array<core::Point,4> bowtie{{{0,0},{40,40},{0,40},{40,0}}};check(!native::projectLayerMask(mask(bowtie)).plane,"Self-intersecting mask is rejected rather than replaced by its bounds");
    const std::array<core::Point,4> concave{{{0,0},{40,0},{5,5},{0,40}}};check(!native::projectLayerMask(mask(concave)).plane,"Concave mask is explicit unsupported geometry");
    const std::array<core::Point,4> degenerate{{{0,0},{0,0},{40,40},{0,40}}};check(!native::projectLayerMask(mask(degenerate)).plane,"Degenerate quadrilateral cannot produce a singular inverse");
    node=mask(viewport);node["transform"]=transform(core::Matrix4::scale(0,1));check(!native::projectLayerMask(node).plane,"Singular mask placement is reported");
    node=mask(viewport);node["geometryFlipped"]=true;check(!native::projectLayerMask(node).plane,"Unsupported geometry flip is not guessed");
    node=mask(viewport);node["masksToBounds"]=true;check(!native::projectLayerMask(node).plane,"A second bounds clip is not silently discarded");
    node=mask(viewport);node["shape"]["path"]=Json::Array{command("move",{xy(0,0)}),command("cubic",{xy(1,0),xy(1,1),xy(0,1)})};
    check(!native::projectLayerMask(node).unsupported.empty(),"Curved masks remain explicitly unsupported");
    node=mask(viewport);node["opacity"]=2;rejects([&]{native::projectLayerMask(node);},"Invalid opacity is rejected");
    node=mask(viewport);node["bounds"]=Json::Array{0,0,-1,200};rejects([&]{native::projectLayerMask(node);},"Malformed placement is rejected rather than recategorized as unsupported geometry");
    auto invalidOwner=owner;invalidOwner.values[0]=std::numeric_limits<double>::infinity();rejects([&]{native::projectLayerMask(mask(viewport),invalidOwner);},"Nonfinite owner matrix is rejected");
}
void verifyExport(const Json& node){
    if(!node["mask"].isNull()){
        const auto& mask=node["mask"];const auto result=native::projectLayerMask(mask);
        check(result.plane.has_value()&&result.unsupported.empty(),"Actual exported shell mask converts without approximation");
        const auto& commands=mask["shape"]["path"].array();constexpr std::array<core::Point,4> unit{{{0,0},{1,0},{1,1},{0,1}}};
        for(unsigned i=0;i<4;++i){const auto& p=commands[i]["points"].array()[0].array();const auto local=project(result.plane->worldToLocal,{p[0].number(),p[1].number()});
            close(local.x,unit[i].x,"Actual exported mask corner X roundtrip");close(local.y,unit[i].y,"Actual exported mask corner Y roundtrip");}
    }
    if(!node["children"].isNull())for(const auto& child:node["children"].array())verifyExport(child);
}
}
int main(int argc,char** argv){try{
    run();if(argc==2){std::ifstream file(argv[1],std::ios::binary);check(bool(file),"Optional owned Mac frame JSON opens");
        const std::string bytes((std::istreambuf_iterator<char>(file)),{});const auto frame=Json::parse(bytes,64*1024*1024);verifyExport(frame["nativeLayers"]);}
    else check(argc==1,"Pass at most one owned exported frame JSON");
    std::cout<<"PASS "<<checks<<" portable projected layer mask checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
