#include "core/source_desktop_chrome.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {bool counting=false;std::size_t allocations=0;}
void* operator new(std::size_t n){if(counting)++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace endfield::core;
using namespace endfield::core::source;
namespace {
unsigned checks{};
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
void close(double a,double b,const char*m,double tolerance=2e-8){++checks;if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>tolerance)throw std::runtime_error(std::string(m)+": "+std::to_string(a)+" != "+std::to_string(b));}
Json read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);check(bool(f),"Oracle file opens");std::string s((std::istreambuf_iterator<char>(f)),{});return Json::parse(s,64*1024*1024);}
Matrix4 matrix(const Json&j){Matrix4 m;for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)m.values[c*4+r]=j.array().at(c).array().at(r).number();return m;}
Rect rect(const Json&j){const auto&a=j.array();return {a.at(0).number(),a.at(1).number(),a.at(2).number(),a.at(3).number()};}
void rectangles(Rect a,const Json&j){const auto b=rect(j);close(a.x,b.x,"Rect X");close(a.y,b.y,"Rect Y");close(a.width,b.width,"Rect width");close(a.height,b.height,"Rect height");}
void matrices(const Matrix4&a,const Json&j){const auto b=matrix(j);for(unsigned i=0;i<16;++i)close(a.values[i],b.values[i],"Exact source matrix");}
Point project(const Matrix4&m,Point p){const auto&v=m.values;const double w=v[3]*p.x+v[7]*p.y+v[15];return {(v[0]*p.x+v[4]*p.y+v[12])/w,(v[1]*p.x+v[5]*p.y+v[13])/w};}
DesktopClockStyle style(std::string_view s){for(unsigned i=0;i<5;++i)if(s==std::array{"digital","split","dial","rail","stacked"}[i])return static_cast<DesktopClockStyle>(i);throw std::runtime_error("Unknown clock style");}
Module module(std::string_view name){for(unsigned i=0;i<=static_cast<unsigned>(Module::profile);++i){auto m=static_cast<Module>(i);if(moduleIdentifier(m)==name)return m;}throw std::runtime_error("Unknown source module");}
void textPlacement(const ChromeTextPlacement&p,const Json&j){rectangles(p.frame,j["frame"]);close(p.fontSize,j["text"]["fontSize"].number(),"Source font size");check(p.hidden==j["hidden"].boolean(),"Source text hidden state");check(p.text==j["text"]["string"].string(),"Source text content");check(std::array{"left","center","right"}[static_cast<unsigned>(p.alignment)]==j["text"]["alignment"].string(),"Source text alignment");}
void paths(const std::vector<ChromePathElement>&p,const Json&j){
    if(j.isNull()){check(p.empty(),"Null source path is empty");return;}const auto&commands=j.array();std::size_t at=0;
    auto one=[&](std::string_view op,double x,double y){check(at<commands.size(),"Source path has command");const auto&c=commands[at++];check(c["op"].string()==op,"Original path command order");close(c["points"].array()[0].array()[0].number(),x,"Path X");close(c["points"].array()[0].array()[1].number(),y,"Path Y");};
    for(const auto&e:p){const auto&r=e.geometry;switch(e.kind){
    case ChromePathElement::Kind::move:one("move",r.x,r.y);break;
    case ChromePathElement::Kind::line:one("line",r.x,r.y);break;
    case ChromePathElement::Kind::rectangle:one("move",r.x,r.y);one("line",r.x+r.width,r.y);one("line",r.x+r.width,r.y+r.height);one("line",r.x,r.y+r.height);check(commands.at(at++)["op"].string()=="close","Source rectangle closes");break;
    case ChromePathElement::Kind::ellipse:{
        // CGPath stores an ellipse as four cubic curves. Preserve the authored
        // ellipse primitive and verify all four exact cardinal endpoints.
        one("move",r.x+r.width,r.y+r.height/2);
        for(const Point end:std::array<Point,4>{{{r.x+r.width/2,r.y+r.height},{r.x,r.y+r.height/2},{r.x+r.width/2,r.y},{r.x+r.width,r.y+r.height/2}}}){const auto&c=commands.at(at++);check(c["op"].string()=="cubic","Original ellipse has four cubic segments");close(c["points"].array()[2].array()[0].number(),end.x,"Ellipse endpoint X");close(c["points"].array()[2].array()[1].number(),end.y,"Ellipse endpoint Y");}check(commands.at(at++)["op"].string()=="close","Source ellipse closes");break;}
    }}check(at==commands.size(),"No original path commands omitted");
}
void synthetic(){
    DesktopClockArtworkPlan art;const std::optional<DesktopClockReading> reading=DesktopClockReading{"12:34:56","MON Oct 7"};
    check(art.update(DesktopClockStyle::split,reading),"First clock update changes artwork");check(art.artwork().time.text=="12:34"&&art.artwork().seconds.text=="56","Split retains source seconds field");
    allocations=0;counting=true;for(unsigned i=0;i<1000;++i)art.update(DesktopClockStyle::split,reading);counting=false;check(allocations==0,"1000 unchanged clock frames allocate nothing");
    for(unsigned i=0;i<5;++i){const auto r=art.indicatorRect(i);check(art.indicatorAt({r.x,r.y})==i,"Indicator includes lower bounds");check(!art.indicatorAt({r.x+r.width,r.y}),"Indicator excludes max X as CGRect.contains");check(!art.indicatorAt({r.x,r.y+r.height}),"Indicator excludes max Y as CGRect.contains");}
    check(DesktopClockArtworkPlan::footerText("CTRL+SPACE","CLICK OUTSIDE TO CLOSE")=="ESC / CTRL+SPACE / CLICK OUTSIDE TO CLOSE","Footer preserves actual shortcut separators");
    close(DesktopChromeTiming::opacity(true,.2),0,"Opening native fade starts at source delay");close(DesktopChromeTiming::opacity(true,.44),1,"Opening native fade ends at source time");close(DesktopChromeTiming::opacity(false,.35,.73),.73,"Closing retains captured current opacity");close(DesktopChromeTiming::opacity(false,.41,.73),0,"Closing native fade completes");close(DesktopChromeTiming::opacity(true,0,1,true),1,"Reduced-motion entrance is immediate");
    check(DesktopChromeTiming::styleMovesForward(4,0)&&!DesktopChromeTiming::styleMovesForward(0,4),"Clock style wrap direction matches source");
}
void oracle(const Json&data,const Json&packet){
    DesktopClockArtworkPlan art;
    for(const auto&row:data["styles"].array()){
        std::optional<DesktopClockReading> reading;if(!row["reading"].isNull())reading=DesktopClockReading{row["reading"]["time"].string(),row["reading"]["date"].string()};
        check(art.update(style(row["style"].string()),reading),"Original sample changes retained artwork");const auto&a=art.artwork();textPlacement(a.time,row["time"]);textPlacement(a.date,row["date"]);
        const auto&children=row["artwork"]["children"].array();textPlacement(a.seconds,children.at(2));paths(a.instrument,children.at(0)["shape"]["path"]);paths(a.hands,children.at(1)["shape"]["path"]);paths(a.selection,row["selection"]["shape"]["path"]);
        const auto phase=row["workPhase"].string();const auto expected=DesktopClockArtworkPlan::workBadge(phase=="running"?DesktopWorkPhase::running:phase=="paused"?DesktopWorkPhase::paused:DesktopWorkPhase::idle);check(expected==row["badge"]["text"]["string"].string(),"Source work badge phase text");
    }
    for(const auto&row:data["layouts"].array()){
        const auto&off=row["hudOffset"].array();DesktopChromeSettings settings{rect(row["viewport"]),row["hudScale"].number(),{off[0].number(),off[1].number()},module(row["module"].string()),true};
        const auto result=DesktopChromeLayout::make(settings,matrix(row["sourceCenter"]),matrix(row["sourceStatus"]));
        close(result.designScale,row["designScale"].number(),"Source designScale");close(result.reportScale,row["reportScale"].number(),"Source reportScale");close(result.reportCenterY,row["reportCenterY"].number(),"Source reportCenterY");rectangles(result.footerText,row["footerText"]);
        for(unsigned i=0;i<2;++i){close(i?result.designOrigin.y:result.designOrigin.x,row["designOrigin"].array()[i].number(),"Source design origin");close(i?result.canvasPosition.y:result.canvasPosition.x,row["canvasPosition"].array()[i].number(),"Source canvas position");}
        matrices(result.centerSpatial,row["centerSpatial"]);matrices(result.statusLocal,row["statusLocal"]);
        for(std::size_t i=0;i<row["moduleLocalPoints"].array().size();++i){const auto&xy=row["moduleLocalPoints"].array()[i].array();const auto p=project(result.moduleLocalToScreen,{xy[0].number(),xy[1].number()});close(p.x,row["moduleScreenPoints"].array()[i].array()[0].number(),"Actual module source projection X");close(p.y,row["moduleScreenPoints"].array()[i].array()[1].number(),"Actual module source projection Y");}
    }
    const auto scene=SceneDefinition::fromJson(packet["scene"]);const auto library=Library::fromJson(packet["library"]);WatchAnimation animation(scene,library);SourceCamera camera(packet["runtimeRoot"]);
    DesktopChromeProjectionPlan projection(scene,camera,animation,DesktopChromeBindings::fromJson(data["bindings"]));SourceLayout layout(scene);auto nodes=layout.resolve();
    for(const auto&row:data["projections"].array()){
        for(const auto&n:row["nodes"].array()){const auto index=layout.nodeIndex(n["id"].string());check(index.has_value(),"Original projection node exists");nodes[*index].worldMatrix=matrix(n["world"]);if(!n["rect"].isNull())nodes[*index].rect=SourceRect{{n["rect"]["origin"].array()[0].number(),n["rect"]["origin"].array()[1].number()},{n["rect"]["size"].array()[0].number(),n["rect"]["size"].array()[1].number()}};}
        CameraFrame frame;frame.view=matrix(row["view"]);frame.projection=matrix(row["projection"]);frame.worldRoot=matrix(row["worldRoot"]);const auto viewport=rect(row["viewport"]);
        check(projection.update(nodes,frame,viewport),"Original projection pose changes result");check(projection.projection().center&&projection.projection().status,"Both native attachment projections available");matrices(*projection.projection().center,row["center"]);matrices(*projection.projection().status,row["status"]);close(projection.projection().unitsPerPoint,row["unitsPerPoint"].number(),"Actual source neutral calibration");
        allocations=0;counting=true;for(unsigned i=0;i<1000;++i)projection.update(nodes,frame,viewport);counting=false;check(allocations==0,"1000 unchanged source chrome projection frames allocate nothing");
        allocations=0;counting=true;for(unsigned i=0;i<120;++i){frame.worldRoot.values[12]+=.0001;projection.update(nodes,frame,viewport);}counting=false;check(allocations==0,"Pointer root changes update two source projections without allocation");
    }
}
}
int main(int argc,char**argv){try{synthetic();if(argc==3)oracle(read(std::filesystem::path(argv[1])/"chrome.json"),read(std::filesystem::path(argv[2])/"animation.json"));else check(argc==1,"Pass chrome oracle and mounted shell packet roots together");std::cout<<"PASS "<<checks<<" source desktop chrome checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
