#include "modules/app_shortcut_trace.hpp"
#include <array>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {namespace {
using J=ehud::data::Json;
struct Point {double x{},y{};};
struct Segment {Point start,c1,c2,end;bool cubic;};
constexpr std::array<Segment,9> segments(){
    constexpr double a=.75,b=47.25,r=3,k=r*.5522847498;
    return {{{{b,24},{},{},{b,b-r},false},{{b,b-r},{b,b-r+k},{b-r+k,b},{b-r,b},true},
        {{b-r,b},{},{},{a+r,b},false},{{a+r,b},{a+r-k,b},{a,b-r+k},{a,b-r},true},
        {{a,b-r},{},{},{a,a+r},false},{{a,a+r},{a,a+r-k},{a+r-k,a},{a+r,a},true},
        {{a+r,a},{},{},{b-r,a},false},{{b-r,a},{b-r+k,a},{b,a+r-k},{b,a+r},true},
        {{b,a+r},{},{},{b,24},false}}};
}
Point mix(Point a,Point b,double t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};}
J command(const char*op,std::initializer_list<Point>points={}){J::Array out;for(const auto&p:points)out.push_back(J::Array{p.x,p.y});return J::Object{{"op",op},{"points",std::move(out)}};}
double length(const Segment&s,double end=1){
    constexpr unsigned steps=128;const double h=end/steps;double sum{};
    for(unsigned n=0;n<=steps;++n){const double t=h*n,u=1-t;
        const double dx=3*(u*u*(s.c1.x-s.start.x)+2*u*t*(s.c2.x-s.c1.x)+t*t*(s.end.x-s.c2.x));
        const double dy=3*(u*u*(s.c1.y-s.start.y)+2*u*t*(s.c2.y-s.c1.y)+t*t*(s.end.y-s.c2.y));
        sum+=(n==0||n==steps?1:n%2?4:2)*std::hypot(dx,dy);
    }return sum*h/3;
}
}
J shortcutPresetTracePath(double end){
    if(!std::isfinite(end)||end<0||end>1)throw std::invalid_argument("Invalid shortcut preset strokeEnd");
    constexpr auto path=segments();static const double corner=length(path[1]);
    constexpr double straight=4*(46.5-6);double remaining=end*(straight+4*corner);
    J::Array out;out.reserve(10);out.push_back(command("move",{path.front().start}));
    for(const auto&s:path){if(remaining<=0)break;const double extent=s.cubic?corner:std::hypot(s.end.x-s.start.x,s.end.y-s.start.y);
        if(remaining>=extent||end==1){out.push_back(s.cubic?command("cubic",{s.c1,s.c2,s.end}):command("line",{s.end}));remaining-=extent;continue;}
        if(!s.cubic)out.push_back(command("line",{mix(s.start,s.end,remaining/extent)}));
        else{double lo=0,hi=1;for(unsigned n=0;n<44;++n){const double t=(lo+hi)*.5;if(length(s,t)<remaining)lo=t;else hi=t;}
            const double t=(lo+hi)*.5;const auto a=mix(s.start,s.c1,t),b=mix(s.c1,s.c2,t),c=mix(s.c2,s.end,t),d=mix(a,b,t),e=mix(b,c,t);
            out.push_back(command("cubic",{a,d,mix(d,e,t)}));}
        break;
    }
    // CGPath closes the final straight segment implicitly.
    if(end==1){out.pop_back();out.push_back(command("close"));}
    return out;
}
}
