#include "modules/media_assembly_geometry.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using J=ehud::data::Json;namespace m=endfield::modules;
namespace {int checks{};void check(bool okay,const char*why){++checks;if(!okay)throw std::runtime_error(why);}template<class F>void rejects(F&&f){bool failed{};try{f();}catch(const std::invalid_argument&){failed=true;}check(failed,"Malformed geometry rejected");}}
int main(int argc,char**argv){try{
 check(argc==2,"Expected original geometry fixture");std::ifstream stream(argv[1]);const auto fixture=J::parse(std::string(std::istreambuf_iterator<char>(stream),{}));check(fixture["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Authoritative source");
 for(const auto&row:fixture["cases"].array()){
  const unsigned w=static_cast<unsigned>(row["input"].array()[0].integer()),h=static_cast<unsigned>(row["input"].array()[1].integer());const auto&crop=row["crop"].array();m::MediaAssemblyAdjustments a;a.crop={crop[0].number(),crop[1].number(),crop[2].number(),crop[3].number()};a.rotationQuarterTurns=static_cast<int>(row["turn"].integer());a.mirrored=row["mirror"].boolean();const auto p=m::mediaAssemblyPixelPlan(w,h,a);
  const auto&extent=row["extent"].array();check(extent[0].number()==0&&extent[1].number()==0&&extent[2].number()==p.orientedWidth&&extent[3].number()==p.orientedHeight,"Original oriented crop extent");const auto&pixels=row["pixels"].array();check(pixels.size()==std::size_t(p.orientedWidth)*p.orientedHeight,"Original pixel count");
  for(unsigned y=0;y<p.orientedHeight;++y)for(unsigned x=0;x<p.orientedWidth;++x){const auto uv=p.sourceUV({(x+.5)/p.orientedWidth,(y+.5)/p.orientedHeight});const int sx=static_cast<int>(std::floor(uv.x*w)),sy=static_cast<int>(std::floor(uv.y*h));const bool inside=sx>=0&&sy>=0&&sx<static_cast<int>(w)&&sy<static_cast<int>(h);const std::array<int,4>expected=inside?std::array<int,4>{sx*11+7,sy*13+9,71,255}:std::array<int,4>{0,0,0,0};const auto&actual=pixels[std::size_t(y)*p.orientedWidth+x].array();for(unsigned c=0;c<4;++c)check(std::abs(actual[c].integer()-expected[c])<=1,"Original Core Image pixel orientation/crop");}
  for(const auto&size:row["sizes"].array()){const auto maximum=size["bound"].isNull()?std::optional<unsigned>{}:static_cast<unsigned>(size["bound"].integer());const auto&extent=size["size"].array();if(extent[0].number()<1||extent[1].number()<1){rejects([&]{(void)m::mediaAssemblyPixelPlan(w,h,a,maximum,size["even"].boolean());});continue;}const auto out=m::mediaAssemblyPixelPlan(w,h,a,maximum,size["even"].boolean());check(out.outputWidth==extent[0].number()&&out.outputHeight==extent[1].number(),"Source output rounding/max preview/even video sizing");}
 }
 m::MediaAssemblyAdjustments a;rejects([&]{(void)m::mediaAssemblyPixelPlan(0,1,a);});rejects([&]{(void)m::mediaAssemblyPixelPlan(65537,1,a);});rejects([&]{(void)m::mediaAssemblyPixelPlan(10000,10000,a);});rejects([&]{(void)m::mediaAssemblyPixelPlan(10,10,a,0);});a.crop.height=std::numeric_limits<double>::quiet_NaN();rejects([&]{(void)m::mediaAssemblyPixelPlan(10,10,a);});
 std::cout<<checks<<" Media Assembly source pixel-geometry checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<"Geometry check failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
