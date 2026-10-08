#include "core/source_desktop_chrome.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>
#include <stdexcept>

namespace endfield::core::source {
namespace {
void require(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
bool large(Module m){return m==Module::nowPlaying||m==Module::archive||m==Module::mediaAssembly||m==Module::calendar||m==Module::minigame||m==Module::reader;}
void point(std::vector<ChromePathElement>&p,ChromePathElement::Kind kind,double x,double y){p.push_back({kind,{x,y,0,0}});}
void segment(std::vector<ChromePathElement>&p,double x,double y,double x2,double y2){point(p,ChromePathElement::Kind::move,x,y);point(p,ChromePathElement::Kind::line,x2,y2);}
bool validQuad(const std::array<Point,4>&p){return std::abs((p[1].x-p[0].x)*(p[3].y-p[0].y)-(p[1].y-p[0].y)*(p[3].x-p[0].x))>.001;}
std::optional<std::array<Point,4>> corners(const std::array<Vec3,4>&local,const Matrix4&world,const Matrix4&vp,Rect viewport){
    std::array<Point,4>p{};for(unsigned i=0;i<4;++i){const auto v=projectNativePoint(local[i],world,vp,viewport);if(!v)return {};p[i]=*v;}return validQuad(p)?std::optional(p):std::nullopt;
}
std::vector<std::string_view> tokens(std::string_view s,unsigned maximum=std::numeric_limits<unsigned>::max()){
    // Swift split omits empty subsequences and maxSplits counts delimiters,
    // including leading delimiters. The clock supplies ordinary POSIX times.
    std::vector<std::string_view>out;std::size_t start=0;unsigned splits=0;
    for(std::size_t i=0;i<s.size()&&splits<maximum;++i)if(s[i]==':'){if(i>start)out.push_back(s.substr(start,i-start));start=i+1;++splits;}
    if(start<s.size())out.push_back(s.substr(start));return out;
}
double prefixNumber(std::string_view value){
    value=value.substr(0,std::min<std::size_t>(2,value.size()));auto digit=[](char c){return c>='0'&&c<='9';};
    if(value.size()==1)return digit(value[0])?value[0]-'0':0;
    if(value.size()!=2)return 0;const auto a=value[0],b=value[1];
    if(digit(a)&&digit(b))return (a-'0')*10+b-'0';if((a=='+'||a=='-')&&digit(b))return (a=='-'?-1:1)*(b-'0');
    if(a=='.'&&digit(b))return double(b-'0')/10;if(digit(a)&&b=='.')return a-'0';return 0;
}
}
DesktopChromeLayout DesktopChromeLayout::make(const DesktopChromeSettings&s,const Matrix4&center,const Matrix4&status){
    require(std::isfinite(s.viewport.x)&&std::isfinite(s.viewport.y)&&std::isfinite(s.viewport.width)&&std::isfinite(s.viewport.height)&&s.viewport.width>0&&s.viewport.height>0&&std::isfinite(s.hudScale)&&s.hudScale>0&&std::isfinite(s.hudOffset[0])&&std::isfinite(s.hudOffset[1])&&center.finite()&&status.finite(),"Invalid desktop chrome placement");
    DesktopChromeLayout o;o.designScale=std::max(.1,std::min({s.viewport.width/1100,s.viewport.height/740,1.15}))*s.hudScale;
    const auto scale=o.designScale;const Point offset{s.viewport.width*s.hudOffset[0],s.viewport.height*s.hudOffset[1]};
    o.designOrigin={(s.viewport.width-1000*scale)/2+offset.x,(s.viewport.height-640*scale)/2-30*scale+offset.y};
    o.canvasPosition={s.viewport.x+s.viewport.width/2+offset.x,s.viewport.y+s.viewport.height/2-30*scale+offset.y};
    o.footerText.y=(s.viewport.height-o.designOrigin.y-16)/scale-o.footerText.height;
    o.reportScale=s.module==Module::map?(s.sourceShell?1.14:1):large(s.module)||s.module==Module::workMode?1:.86;
    o.reportCenterY=large(s.module)?275:s.module==Module::workMode||s.module==Module::map?(s.sourceShell?(s.module==Module::map?297:285):320):(s.sourceShell?285:294);
    o.designToScreen=Matrix4::translation(o.designOrigin.x,o.designOrigin.y)*Matrix4::scale(scale,scale);
    o.centerSpatial=Matrix4::translation(-500,-320)*Matrix4::scale(1/scale,1/scale)*Matrix4::translation(-o.designOrigin.x,-o.designOrigin.y)*center*Matrix4::translation(500,320);
    o.statusToScreen=status*Matrix4::translation(86.28,8);
    o.statusLocal=Matrix4::scale(1/scale,1/scale)*Matrix4::translation(-o.designOrigin.x,-o.designOrigin.y)*o.statusToScreen;
    const auto content=moduleContentFrame(s.module);o.moduleLocalToScreen=center*Matrix4::translation(500,o.reportCenterY)*Matrix4::scale(o.reportScale,o.reportScale)*Matrix4::translation(content.x-500,content.y-320);
    return o;
}
DesktopChromeBindings DesktopChromeBindings::fromJson(const Json&j){require(j.isObject()&&j["centerNodeID"].isString()&&j["statusNodeID"].isString(),"Missing exact desktop chrome source bindings");return {j["centerNodeID"].string(),j["statusNodeID"].string()};}
DesktopChromeProjectionPlan::DesktopChromeProjectionPlan(const SceneDefinition&s,const SourceCamera&c,const WatchAnimation&a,DesktopChromeBindings ids):scene_(&s),camera_(&c),animation_(&a){
    require(&a.scene()==&s,"Desktop chrome animation must use mounted source scene");SourceLayout layout(s);const auto center=layout.nodeIndex(ids.centerNodeID),status=layout.nodeIndex(ids.statusNodeID);require(center&&status,"Unknown desktop chrome source binding");center_=*center;status_=*status;
}
bool DesktopChromeProjectionPlan::update(std::span<const ResolvedNode>nodes,const CameraFrame&camera,Rect viewport){
    require(nodes.size()==scene_->nodes().size()&&nodes[center_].node==&scene_->nodes()[center_]&&nodes[status_].node==&scene_->nodes()[status_],"Desktop chrome node order changed");
    require(viewport.width>0&&viewport.height>0&&camera.worldRoot.finite()&&camera.projection.finite()&&camera.view.finite(),"Invalid desktop chrome source camera");bool changed=false;
    const auto centerWorld=camera.worldRoot*nodes[center_].worldMatrix;const auto vp=camera.projection*camera.view;
    if(!centerWorld_||*centerWorld_!=centerWorld||centerViewport_!=viewport){
        if(calibrationViewport_!=viewport){
            const auto neutral=camera_->frame({viewport.width,viewport.height},camera_->rootRotation());
            const auto pose=animation_->pose(animation_->entrance().lastKeyTime,{},{},neutral.layout.canvasSize);
            const auto resolved=SourceLayout(*scene_).resolve({},pose.transforms);const auto world=neutral.worldRoot*resolved[center_].worldMatrix;
            const auto origin=projectNativePoint({0,0,0},world,neutral.projection*neutral.view,viewport),step=projectNativePoint({1,0,0},world,neutral.projection*neutral.view,viewport);
            if(origin&&step){const auto pixels=std::hypot(step->x-origin->x,step->y-origin->y);if(pixels>.0001){result_.unitsPerPoint=std::max(.1,std::min({viewport.width/1100,viewport.height/740,1.15}))/pixels;calibrationViewport_=viewport;}}
        }
        if(calibrationViewport_==viewport){const auto unit=result_.unitsPerPoint;const auto p=corners({Vec3{-500*unit,320*unit,0},Vec3{500*unit,320*unit,0},Vec3{500*unit,-320*unit,0},Vec3{-500*unit,-320*unit,0}},centerWorld,vp,viewport);
            if(p){const auto matrix=nativeProjectiveTextTransform(*p,{1000,640});changed|=result_.center!=matrix;result_.center=matrix;centerWorld_=centerWorld;centerViewport_=viewport;}}
    }
    const auto&status=nodes[status_];if(status.rect){const auto world=camera.worldRoot*status.worldMatrix;
        if(!statusWorld_||*statusWorld_!=world||statusRect_!=status.rect||statusViewport_!=viewport){const auto&r=*status.rect;
            const auto p=corners({Vec3{r.origin[0],r.origin[1]+r.size[1],0},Vec3{r.origin[0]+r.size[0],r.origin[1]+r.size[1],0},Vec3{r.origin[0]+r.size[0],r.origin[1],0},Vec3{r.origin[0],r.origin[1],0}},world,vp,viewport);
            if(p&&r.size[0]>0&&r.size[1]>0){const auto matrix=nativeProjectiveTextTransform(*p,{528.28,122});changed|=result_.status!=matrix;result_.status=matrix;statusWorld_=world;statusRect_=status.rect;statusViewport_=viewport;}}
    }return changed;
}
DesktopClockArtworkPlan::DesktopClockArtworkPlan(){artwork_.seconds.fontSize=22;artwork_.seconds.alignment=ChromeTextAlignment::center;artwork_.seconds.font=ChromeFontRole::monospacedDigitSystem;artwork_.seconds.semibold=true;artwork_.instrument.reserve(64);artwork_.hands.reserve(4);artwork_.selection.reserve(2);}
Rect DesktopClockArtworkPlan::indicatorRect(unsigned index){require(index<5,"Invalid clock style index");return {14+double(index)*64,124,56,18};}
std::optional<unsigned>DesktopClockArtworkPlan::indicatorAt(Point p){for(unsigned i=0;i<5;++i)if(const auto r=indicatorRect(i);p.x>=r.x&&p.x<r.x+r.width&&p.y>=r.y&&p.y<r.y+r.height)return i;return {};}
std::string_view DesktopClockArtworkPlan::workBadge(DesktopWorkPhase phase)noexcept{return phase==DesktopWorkPhase::running?"WORK MODE / ACTIVE":phase==DesktopWorkPhase::paused?"WORK MODE / PAUSED":"";}
std::string DesktopClockArtworkPlan::footerText(std::string_view shortcut,std::string_view close){return "ESC / "+std::string(shortcut)+" / "+std::string(close);}
bool DesktopClockArtworkPlan::update(DesktopClockStyle style,const std::optional<DesktopClockReading>&reading){
    require(static_cast<unsigned>(style)<5,"Invalid clock style");if(previousStyle_==style&&previousReading_==reading)return false;
    auto&o=artwork_;const auto value=reading?reading->time:"--:--:--";o.time={{22,20,296,39},32,ChromeTextAlignment::right,ChromeFontRole::monospacedDigitSystem,true,false,value};
    o.date={{22,61,296,20},13,ChromeTextAlignment::right,ChromeFontRole::monospacedSystem,false,false,reading?reading->date:""};
    o.seconds.hidden=true;
    o.instrument.clear();o.hands.clear();o.selection.clear();const auto selected=indicatorRect(static_cast<unsigned>(style));segment(o.selection,selected.x,131,selected.x+selected.width-7,131);
    switch(style){
    case DesktopClockStyle::digital:break;
    case DesktopClockStyle::split:{const auto parts=tokens(value,2);o.time.text.clear();for(std::size_t i=0;i<std::min<std::size_t>(2,parts.size());++i){if(i)o.time.text+=':';o.time.text+=parts[i];}
        o.time.fontSize=42;o.time.frame={18,15,210,51};o.seconds.hidden=false;o.seconds.text=parts.size()==3?parts[2]:"--";o.seconds.frame={239,33,79,30};segment(o.instrument,232,24,232,76);break;}
    case DesktopClockStyle::dial:{o.instrument.push_back({ChromePathElement::Kind::ellipse,{22,14,70,70}});constexpr auto pi=std::numbers::pi;
        for(unsigned tick=0;tick<12;++tick){const auto angle=double(tick)*pi/6;segment(o.instrument,57+std::sin(angle)*29,49-std::cos(angle)*29,57+std::sin(angle)*34,49-std::cos(angle)*34);}
        const auto parts=tokens(value);if(parts.size()>=2){const auto hour=prefixNumber(parts[0]),minute=prefixNumber(parts[1]);const auto angle1=std::fmod(hour,12)*pi/6+minute*pi/360,angle2=minute*pi/30;segment(o.hands,57,49,57+std::sin(angle1)*18,49-std::cos(angle1)*18);segment(o.hands,57,49,57+std::sin(angle2)*27,49-std::cos(angle2)*27);}
        o.time.fontSize=25;o.time.frame={105,24,213,34};o.date.frame={105,64,213,18};break;}
    case DesktopClockStyle::rail:o.time.alignment=o.date.alignment=ChromeTextAlignment::center;o.time.frame={26,22,288,39};for(double x=27;x<=313;x+=13)segment(o.instrument,x,14,x,x==27||x>307?78:19);segment(o.instrument,27,83,313,83);break;
    case DesktopClockStyle::stacked:o.date.alignment=ChromeTextAlignment::left;o.date.frame={26,17,286,20};o.time.alignment=ChromeTextAlignment::left;o.time.frame={26,43,286,39};segment(o.instrument,26,39,314,39);o.instrument.push_back({ChromePathElement::Kind::rectangle,{303,20,11,11}});break;
    }
    previousStyle_=style;previousReading_=reading;return true;
}
double DesktopChromeTiming::opacity(bool opening,double elapsed,double captured,bool reduced)noexcept{
    if(reduced)return opening?1:0;const auto progress=fadeCurve.value((elapsed-(opening?openingDelay:closingDelay))/(opening?openingDuration:closingDuration));return opening?progress:captured*(1-progress);
}
bool DesktopChromeTiming::styleMovesForward(unsigned oldIndex,unsigned newIndex)noexcept{return (newIndex+5-oldIndex)%5<=2;}
} // namespace endfield::core::source
