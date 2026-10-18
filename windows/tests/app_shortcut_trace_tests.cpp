#include "modules/app_shortcut_trace.hpp"
#include "modules/app_shortcut_motion.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

namespace {using J=ehud::data::Json;namespace m=endfield::modules;unsigned checks{};
void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
struct P {double x{},y{};};
P point(const J&j){return {j.array()[0].number(),j.array()[1].number()};}
P cubic(P a,P b,P c,P d,double t){const auto u=1-t;return {u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*c.x+t*t*t*d.x,u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*c.y+t*t*t*d.y};}
double length(const J&path){P first{},previous{};double result{};for(const auto&command:path.array()){const auto op=command["op"].string();const auto&v=command["points"].array();if(op=="move"){first=previous=point(v[0]);continue;}if(op=="cubic"){
 const auto start=previous,b=point(v[0]),c=point(v[1]),d=point(v[2]);for(unsigned n=1;n<=4096;++n){const auto next=cubic(start,b,c,d,double(n)/4096);result+=std::hypot(next.x-previous.x,next.y-previous.y);previous=next;}
 }else{const auto next=op=="close"?first:point(v[0]);result+=std::hypot(next.x-previous.x,next.y-previous.y);previous=next;}}return result;}
void run(const char*file){std::ifstream input(file);check(bool(input),"Original detached source fixture opens");const auto j=J::parse(std::string(std::istreambuf_iterator<char>(input),{}),64*1024);check(j["version"].integer()==1&&!j["usesAppOrWindow"].boolean(),"Fixture uses only original detached layers");
 check(j["duration"].number()==.18&&j["lineWidth"].number()==1.5&&j["lineCap"].string()=="butt"&&j["lineJoin"].string()=="miter","Original preset duration and stroke style");
 const auto full=m::shortcutPresetTracePath(1);const auto&expected=j["path"].array();check(full.array().size()==expected.size(),"Original rounded trace topology");for(std::size_t n=0;n<expected.size();++n){const auto&a=full.array()[n];const auto&b=expected[n];check(a["op"]==b["op"]&&a["points"].array().size()==b["points"].array().size(),"Original CGPath commands");for(std::size_t k=0;k<a["points"].array().size();++k)for(unsigned c=0;c<2;++c)check(std::abs(a["points"].array()[k].array()[c].number()-b["points"].array()[k].array()[c].number())<2e-13,"Original inset .75 radius 3 cubic coordinates");}
 const auto total=length(j["path"]);double maxTiming{},maxLength{};for(const auto&row:j["rows"].array()){m::ShortcutPresetTransition transition{1,0,1,"camera"};const double progress=m::shortcutPresetProgress(transition,row["phase"].number()*.18);maxTiming=std::max(maxTiming,std::abs(progress-row["strokeEnd"].number()));check(std::abs(progress-row["strokeEnd"].number())<1e-12,"Actual CA .18 easeOut strokeEnd samples");
  const auto path=m::shortcutPresetTracePath(progress);check(path.array().size()<=10,"Finite trace never expands geometry beyond source path");const auto residual=std::abs(length(path)-total*progress);maxLength=std::max(maxLength,residual);check(residual<1e-6,"Cubic geometric prefix retains source arc-length proportion");
 }
 check(m::shortcutPresetTracePath(0).array().size()==1,"Zero trace has only original move and no ink");for(double invalid:{-.001,1.001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){bool failed{};try{(void)m::shortcutPresetTracePath(invalid);}catch(const std::invalid_argument&){failed=true;}check(failed,"Invalid stroke progress rejects before geometry construction");}
 std::cout<<"PASS "<<checks<<" Shortcut trace source checks; timing residual "<<maxTiming<<", geometric length residual "<<maxLength<<" pt\n";
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass original preset source fixture");run(argv[1]);return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
