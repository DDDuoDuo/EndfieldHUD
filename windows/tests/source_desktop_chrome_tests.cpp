#include "core/source_desktop_chrome.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
// Mac oracle (windows/tools/shell_redesign_reference.sh): HUDClockStyleArtwork
// layers as the unchanged Mac class lays them out, with the Mac's own text
// widths, so the portable geometry compares exactly.
ChromeColor rgba(const Json&j){const auto&a=j.array();return {a.at(0).number(),a.at(1).number(),a.at(2).number(),a.at(3).number()};}
void color(const ChromeColor&a,const Json&j,const char*m){check(!j.isNull(),m);const auto b=rgba(j);for(unsigned i=0;i<4;++i)close(a[i],b[i],m,2e-7);}
void textPlacement(const ChromeTextPlacement&p,const Json&j,const std::string&where){
    check(p.hidden==j["hidden"].boolean(),("Clock text hidden: "+where).c_str());if(p.hidden)return;
    // CoreText reports HarmonyOS' 928/1000 ascender as .92799377: a thousandth of a point.
    const auto f=rect(j["frame"]);close(p.frame.x,f.x,"Text frame X");close(p.frame.y,f.y,("Text cap placement: "+where).c_str(),1e-3);close(p.frame.width,f.width,"Text frame width");close(p.frame.height,f.height,("Text line box: "+where).c_str());
    close(p.fontSize,j["fontSize"].number(),("Clock font size (fit): "+where).c_str());check(p.text==j["string"].string(),("Clock text: "+where).c_str());
    check(std::array{"left","center","right"}[static_cast<unsigned>(p.alignment)]==j["alignment"].string(),("Clock text alignment: "+where).c_str());
    check(p.weight==j["weight"].integer()&&p.tabular==j["tabular"].boolean()&&p.truncates==(j["truncation"].string()=="end"),("Clock text face: "+where).c_str());
    color(p.color,j["color"],"Clock text colour");
}
void paths(const std::vector<ChromePathElement>&p,const Json&j,const std::string&where){
    if(j.isNull()){check(p.empty(),"Null source path is empty");return;}const auto&commands=j.array();std::size_t at=0;
    auto one=[&](std::string_view op,double x,double y){check(at<commands.size(),("Source path has command: "+where).c_str());const auto&c=commands[at++].array();check(c.at(0).string()==op,("Original path command order: "+where).c_str());close(c.at(1).array()[0].number(),x,"Path X",1e-9);close(c.at(1).array()[1].number(),y,"Path Y",1e-9);};
    for(const auto&e:p){const auto&r=e.geometry;switch(e.kind){
    case ChromePathElement::Kind::move:one("move",r.x,r.y);break;
    case ChromePathElement::Kind::line:one("line",r.x,r.y);break;
    case ChromePathElement::Kind::close:check(commands.at(at++).array().at(0).string()=="close","Source subpath closes");break;
    case ChromePathElement::Kind::rectangle:one("move",r.x,r.y);one("line",r.x+r.width,r.y);one("line",r.x+r.width,r.y+r.height);one("line",r.x,r.y+r.height);check(commands.at(at++).array().at(0).string()=="close","Source rectangle closes");break;
    case ChromePathElement::Kind::ellipse:{
        // CGPath stores an ellipse as four cubic curves from (maxX, midY).
        one("move",r.x+r.width,r.y+r.height/2);
        for(const Point end:std::array<Point,4>{{{r.x+r.width/2,r.y+r.height},{r.x,r.y+r.height/2},{r.x+r.width/2,r.y},{r.x+r.width,r.y+r.height/2}}}){const auto&c=commands.at(at++).array();check(c.at(0).string()=="cubic","Original ellipse has four cubic segments");close(c.at(3).array()[0].number(),end.x,"Ellipse endpoint X",1e-9);close(c.at(3).array()[1].number(),end.y,"Ellipse endpoint Y",1e-9);}check(commands.at(at++).array().at(0).string()=="close","Source ellipse closes");break;}
    }}check(at==commands.size(),("No original path commands omitted: "+where).c_str());
}
void shape(const ChromeShape&s,const Json&j,const std::string&where){
    paths(s.path,j["path"],where);check(s.fill.has_value()==!j["fill"].isNull()&&s.stroke.has_value()==!j["stroke"].isNull(),("Shape fill/stroke presence: "+where).c_str());
    if(s.fill)color(*s.fill,j["fill"],"Shape fill");if(s.stroke)color(*s.stroke,j["stroke"],"Shape stroke");
    if(s.stroke){close(s.lineWidth,j["lineWidth"].number(),"Shape line width",1e-6);check(std::array{"butt","round","square"}[static_cast<unsigned>(s.cap)]==j["lineCap"].string(),"Shape line cap");}
}
DesktopWorkPhase phase(std::string_view p){return p=="running"?DesktopWorkPhase::running:p=="paused"?DesktopWorkPhase::paused:DesktopWorkPhase::idle;}
void clockOracle(const Json&fixture){
    const auto&clock=fixture["clock"];const auto&font=clock["font"];
    check(font["family"].string()=="HarmonyOS Sans","Mac clocks resolve HarmonyOS Sans");
    close(StandardFaceMetrics::ascender,font["ascender"].number(),"HarmonyOS ascender",1e-5);close(StandardFaceMetrics::descender,font["descender"].number(),"HarmonyOS descender",1e-5);
    close(StandardFaceMetrics::capHeight,font["capHeight"].number(),"HarmonyOS cap height",1e-9);close(StandardFaceMetrics::leading,font["leading"].number(),"HarmonyOS leading",1e-9);
    std::map<std::string,double> widths;for(const auto&m:clock["measures"].array()){const auto&a=m.array();widths[a[0].string()+"|"+std::to_string(a[1].number())+"|"+std::to_string(a[2].integer())+"|"+(a[3].boolean()?"t":"p")]=a[4].number();}
    std::size_t misses=0;
    DesktopClockArtworkPlan art([&](std::string_view text,double size,int weight,bool tabular){
        const auto found=widths.find(std::string(text)+"|"+std::to_string(size)+"|"+std::to_string(weight)+"|"+(tabular?"t":"p"));if(found==widths.end()){++misses;return 0.;}return found->second;});
    const std::array<double,3> accent{250./255,212./255,31./255};
    for(const auto&row:clock["states"].array()){
        std::optional<DesktopClockReading> reading;if(!row["reading"].isNull())reading=DesktopClockReading{row["reading"]["time"].string(),row["reading"]["date"].string()};
        const DesktopClockStatus status{phase(row["workPhase"].string()),row["recording"].boolean()};const auto where=row["style"].string()+" sample "+std::to_string(row["sample"].integer());
        check(art.update(style(row["style"].string()),reading,status,accent),"Each original sample changes the artwork");const auto&a=art.artwork();
        textPlacement(a.time,row["time"],where+" time");textPlacement(a.date,row["date"],where+" date");textPlacement(a.seconds,row["seconds"],where+" seconds");textPlacement(a.aux,row["aux"],where+" aux");
        textPlacement(a.workBadge,row["work"],where+" work badge");textPlacement(a.recordLabel,row["recordLabel"],where+" recording");
        shape(a.instrument,row["instrument"],where+" instrument");shape(a.fill,row["fill"],where+" fill");shape(a.track,row["track"],where+" track");
        shape(a.hands,row["hands"],where+" hands");shape(a.secondHand,row["secondHand"],where+" second hand");shape(a.selection,row["selection"],where+" selection");shape(a.rail,row["rail"],where+" status rail");
        rectangles(a.recordDot,row["recordDot"]["frame"]);color(a.recordDotColor,row["recordDot"]["color"],"Record dot colour");
    }
    check(misses==0,"Every width the artwork asks for is one the Mac measured");
}
void synthetic(){
    DesktopClockArtworkPlan art([](std::string_view text,double size,int,bool){return double(text.size())*size*.6;});const std::optional<DesktopClockReading> reading=DesktopClockReading{"12:34:56","MON Oct 7"};
    const std::array<double,3> accent{1,.8,.1};
    check(art.update(DesktopClockStyle::split,reading,{},accent),"First clock update changes artwork");check(art.artwork().time.text=="12:34"&&art.artwork().seconds.text=="56","Split retains source seconds field");
    allocations=0;counting=true;for(unsigned i=0;i<1000;++i)art.update(DesktopClockStyle::split,reading,{},accent);counting=false;check(allocations==0,"1000 unchanged clock frames allocate nothing");
    // The status row is always shown, English in every language (SHELL-56, SHELL-15).
    check(art.artwork().workBadge.text=="WORK MODE"&&art.artwork().recordLabel.text=="RECORDING MODE"&&!art.artwork().workBadge.hidden&&!art.artwork().recordLabel.hidden,"Idle Work Mode and Recording mode stay listed");
    check(art.artwork().workBadge.color==ChromeColor{.45,.45,.45,1}&&art.artwork().recordDotColor==ChromeColor{.45,.45,.45,1}&&art.artwork().recordDotOpacity==1,"Off statuses are grey white .45; the dot is steady");
    check(art.update(DesktopClockStyle::split,DesktopClockReading{"12:34:57","MON Oct 7"},{DesktopWorkPhase::paused,true},accent)&&art.artwork().recordDotOpacity==.28&&art.artwork().workBadge.text=="WORK MODE / PAUSED"&&art.artwork().workBadge.color==ChromeColor{1,.8,.1,1},
        "Recording blinks the dot on the wall-clock tick (.28 on odd seconds); paused Work Mode is lit");
    check(art.update(DesktopClockStyle::split,DesktopClockReading{"12:34:58","MON Oct 7"},{DesktopWorkPhase::paused,true},accent)&&art.artwork().recordDotOpacity==1,"Even seconds light the dot");
    bool noMeasurer=false;try{DesktopClockArtworkPlan none{ClockTextWidth{}};}catch(const std::invalid_argument&){noMeasurer=true;}check(noMeasurer,"The clock needs its measurer");
    for(unsigned i=0;i<5;++i){const auto r=art.indicatorRect(i);check(art.indicatorAt({r.x,r.y})==i,"Indicator includes lower bounds");check(!art.indicatorAt({r.x+r.width,r.y}),"Indicator excludes max X as CGRect.contains");check(!art.indicatorAt({r.x,r.y+r.height}),"Indicator excludes max Y as CGRect.contains");}
    check(DesktopClockArtworkPlan::footerText("CTRL+SPACE","CLICK OUTSIDE TO CLOSE")=="ESC / CTRL+SPACE / CLICK OUTSIDE TO CLOSE","Footer preserves actual shortcut separators");
    close(DesktopChromeTiming::opacity(true,.2),0,"Opening native fade starts at source delay");close(DesktopChromeTiming::opacity(true,.44),1,"Opening native fade ends at source time");close(DesktopChromeTiming::opacity(false,.35,.73),.73,"Closing retains captured current opacity");close(DesktopChromeTiming::opacity(false,.41,.73),0,"Closing native fade completes");close(DesktopChromeTiming::opacity(true,0,1,true),1,"Reduced-motion entrance is immediate");
    check(DesktopChromeTiming::styleMovesForward(4,0)&&!DesktopChromeTiming::styleMovesForward(0,4),"Clock style wrap direction matches source");
    // HUDDesignGeometry: box px -> design units at the disc, then the centre plane.
    const auto center=Matrix4::translation(40,-12)*Matrix4::scale(1.3,1.1);const double u=1470./(1920*1.15);
    const auto box=designBoxWorld(center);const auto a=project(box,{0,0}),b=project(box,{590,510}),c=project(center,{500-295*u,320-305*u}),d=project(center,{500+295*u,320+205*u});
    close(a.x,c.x,"Box origin X on the plane");close(a.y,c.y,"Box origin Y on the plane");close(b.x,d.x,"Box corner X on the plane");close(b.y,d.y,"Box corner Y on the plane");
    const auto disc=project(box,{295,305}),centre=project(center,{500,320});close(disc.x,centre.x,"Box px (295, 305) is the disc centre");close(disc.y,centre.y,"Box px (295, 305) is the disc centre (Y)");
    DesktopChromeSettings settings{{0,0,1470,956},1,{},Module::notes,true};check(DesktopChromeLayout::make(settings,center,{}).boxWorld==box,"The layout carries the one box placement");
    bool rejected=false;try{(void)designBoxWorld(center,0);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"A non-positive unit is rejected");
    close(planeScale(center,{500,320}),std::sqrt(1.3*1.1),"Plane scale of an affine plane");close(planeScale(box,{295,305}),std::sqrt(1.3*1.1)*u,"Box px scale at the disc");
    const auto tilted=nativeProjectiveTextTransform({Point{40,30},Point{1060,42},Point{1015,680},Point{25,640}},{1000,640});const double h=1e-5;
    const auto p0=project(tilted,{500,320}),px=project(tilted,{500+h,320}),py=project(tilted,{500,320+h});
    close(planeScale(tilted,{500,320}),std::sqrt(std::abs((px.x-p0.x)*(py.y-p0.y)-(px.y-p0.y)*(py.x-p0.x)))/h,"Plane scale of a projective plane matches its local area scale",1e-6);
}
void oracle(const Json&data,const Json&packet){
    // The clock styles of this (v1.2.0) export are superseded by the 1.3.0
    // HUDClockStyleArtwork oracle (clockOracle); its layouts and projections
    // still describe the unchanged chrome placement.
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
// Arguments: shell-redesign-source.json [chrome-export-root mounted-packet-root].
int main(int argc,char**argv){try{synthetic();check(argc==2||argc==4,"Pass the shell redesign oracle (and optionally the chrome export and packet roots)");clockOracle(read(argv[1]));
    if(argc==4)oracle(read(std::filesystem::path(argv[2])/"chrome.json"),read(std::filesystem::path(argv[3])/"animation.json"));std::cout<<"PASS "<<checks<<" source desktop chrome checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
