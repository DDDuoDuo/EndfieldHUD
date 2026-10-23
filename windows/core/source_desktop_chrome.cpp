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
bool validQuad(const std::array<Point,4>&p){return std::abs((p[1].x-p[0].x)*(p[3].y-p[0].y)-(p[1].y-p[0].y)*(p[3].x-p[0].x))>.001;}
std::optional<std::array<Point,4>> corners(const std::array<Vec3,4>&local,const Matrix4&world,const Matrix4&vp,Rect viewport){
    std::array<Point,4>p{};for(unsigned i=0;i<4;++i){const auto v=projectNativePoint(local[i],world,vp,viewport);if(!v)return {};p[i]=*v;}return validQuad(p)?std::optional(p):std::nullopt;
}
}
Json standardFaceFont(double size,bool bold){
    using F=StandardFaceMetrics;require(std::isfinite(size)&&size>0,"Invalid standard face size");
    return Json::Object{{"familyName","HarmonyOS Sans"},{"postScriptName",bold?"HarmonyOSSans-Bold":"HarmonyOSSans-Medium"},{"pointSize",size},{"symbolicTraits",bold?2:0},
        {"ascender",F::ascender*size},{"descender",F::descender*size},{"leading",F::leading*size}};
}
double desktopBaseDesignScale(Rect v){return std::max(.1,std::min({v.width/1100,v.height/740,1.15}));}
DesktopChromeLayout DesktopChromeLayout::make(const DesktopChromeSettings&s,const Matrix4&center,const Matrix4&status){
    require(std::isfinite(s.viewport.x)&&std::isfinite(s.viewport.y)&&std::isfinite(s.viewport.width)&&std::isfinite(s.viewport.height)&&s.viewport.width>0&&s.viewport.height>0&&std::isfinite(s.hudScale)&&s.hudScale>0&&std::isfinite(s.hudOffset[0])&&std::isfinite(s.hudOffset[1])&&std::isfinite(s.contentScale)&&s.contentScale>0&&center.finite()&&status.finite(),"Invalid desktop chrome placement");
    DesktopChromeLayout o;o.designScale=desktopBaseDesignScale(s.viewport)*s.hudScale*s.contentScale;
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
    o.boxWorld=designBoxWorld(center);return o;
}
double planeScale(const Matrix4&m,Point p){
    const auto&v=m.values;const double w=v[3]*p.x+v[7]*p.y+v[15];require(std::isfinite(w)&&std::abs(w)>1e-12,"Plane point is not projected");
    const double x=(v[0]*p.x+v[4]*p.y+v[12])/w,y=(v[1]*p.x+v[5]*p.y+v[13])/w;
    // Jacobian of the homography: d(x,y)/d(local).
    const double ax=(v[0]-x*v[3])/w,ay=(v[1]-y*v[3])/w,bx=(v[4]-x*v[7])/w,by=(v[5]-y*v[7])/w;
    return std::sqrt(std::abs(ax*by-ay*bx));
}
Matrix4 designBoxWorld(const Matrix4&center,double unit){
    require(std::isfinite(unit)&&unit>0&&center.finite(),"Invalid design box placement");const auto box=DesignBox::rect(unit);
    return center*Matrix4::translation(box.x,box.y)*Matrix4::scale(unit,unit);
}
DesktopChromeBindings DesktopChromeBindings::fromJson(const Json&j){require(j.isObject()&&j["centerNodeID"].isString()&&j["statusNodeID"].isString(),"Missing exact desktop chrome source bindings");return {j["centerNodeID"].string(),j["statusNodeID"].string()};}
DesktopChromeProjectionPlan::DesktopChromeProjectionPlan(const SceneDefinition&s,const SourceCamera&c,const WatchAnimation&a,DesktopChromeBindings ids):scene_(&s),camera_(&c),animation_(&a){
    require(&a.scene()==&s,"Desktop chrome animation must use mounted source scene");SourceLayout layout(s);const auto center=layout.nodeIndex(ids.centerNodeID),status=layout.nodeIndex(ids.statusNodeID);require(center&&status,"Unknown desktop chrome source binding");center_=*center;status_=*status;
}
bool DesktopChromeProjectionPlan::update(std::span<const ResolvedNode>nodes,const CameraFrame&camera,Rect viewport,double contentScale){
    require(nodes.size()==scene_->nodes().size()&&nodes[center_].node==&scene_->nodes()[center_]&&nodes[status_].node==&scene_->nodes()[status_],"Desktop chrome node order changed");
    require(viewport.width>0&&viewport.height>0&&camera.worldRoot.finite()&&camera.projection.finite()&&camera.view.finite(),"Invalid desktop chrome source camera");
    require(std::isfinite(contentScale)&&contentScale>0,"Invalid desktop content scale");bool changed=false;
    const auto centerWorld=camera.worldRoot*nodes[center_].worldMatrix;const auto vp=camera.projection*camera.view;
    if(!centerWorld_||*centerWorld_!=centerWorld||centerViewport_!=viewport||centerScale_!=contentScale){
        if(calibrationViewport_!=viewport||calibrationScale_!=contentScale){
            if(const auto pixels=restingPointsPerUnit(viewport)){result_.unitsPerPoint=desktopBaseDesignScale(viewport)*contentScale / *pixels;calibrationViewport_=viewport;calibrationScale_=contentScale;}
        }
        if(calibrationViewport_==viewport&&calibrationScale_==contentScale){const auto unit=result_.unitsPerPoint;const auto p=corners({Vec3{-500*unit,320*unit,0},Vec3{500*unit,320*unit,0},Vec3{500*unit,-320*unit,0},Vec3{-500*unit,-320*unit,0}},centerWorld,vp,viewport);
            if(p){const auto matrix=nativeProjectiveTextTransform(*p,{1000,640});changed|=result_.center!=matrix;result_.center=matrix;centerWorld_=centerWorld;centerViewport_=viewport;centerScale_=contentScale;}}
    }
    const auto&status=nodes[status_];if(status.rect){const auto world=camera.worldRoot*status.worldMatrix;
        if(!statusWorld_||*statusWorld_!=world||statusRect_!=status.rect||statusViewport_!=viewport){const auto&r=*status.rect;
            const auto p=corners({Vec3{r.origin[0],r.origin[1]+r.size[1],0},Vec3{r.origin[0]+r.size[0],r.origin[1]+r.size[1],0},Vec3{r.origin[0]+r.size[0],r.origin[1],0},Vec3{r.origin[0],r.origin[1],0}},world,vp,viewport);
            if(p&&r.size[0]>0&&r.size[1]>0){const auto matrix=nativeProjectiveTextTransform(*p,{528.28,122});changed|=result_.status!=matrix;result_.status=matrix;statusWorld_=world;statusRect_=status.rect;statusViewport_=viewport;}}
    }return changed;
}
std::optional<double> DesktopChromeProjectionPlan::restingPointsPerUnit(Rect viewport){
    require(std::isfinite(viewport.width)&&std::isfinite(viewport.height)&&viewport.width>0&&viewport.height>0,"Invalid desktop content viewport");
    for(const auto&entry:resting_)if(entry&&entry->viewport==viewport)return entry->points;
    const auto neutral=camera_->frame({viewport.width,viewport.height},camera_->rootRotation());
    const auto pose=animation_->pose(animation_->entrance().lastKeyTime,{},{},neutral.layout.canvasSize);
    const auto resolved=SourceLayout(*scene_).resolve({},pose.transforms);const auto world=neutral.worldRoot*resolved[center_].worldMatrix;
    const auto origin=projectNativePoint({0,0,0},world,neutral.projection*neutral.view,viewport),step=projectNativePoint({1,0,0},world,neutral.projection*neutral.view,viewport);
    std::optional<double> points;if(origin&&step){const auto pixels=std::hypot(step->x-origin->x,step->y-origin->y);if(pixels>.0001)points=pixels;}
    resting_[restingNext_]=Resting{viewport,points};restingNext_=(restingNext_+1)%resting_.size();return points;
}
double DesktopChromeProjectionPlan::contentScale(Rect viewport){
    const auto ratio=[&](Rect v)->std::optional<double>{const auto points=restingPointsPerUnit(v);if(!points)return {};return *points/desktopBaseDesignScale(v);};
    const auto here=ratio(viewport),reference=ratio(desktopReferenceViewport);
    if(!here||!reference||!(*reference>0)||!std::isfinite(*here / *reference))return 1;
    return std::clamp(*here / *reference,1.,3.);
}
// HUDClockStyleArtwork (Sources/HUDClockStyle.swift, Mac 1.3.0).
DesktopClockArtworkPlan::DesktopClockArtworkPlan(ClockTextWidth width):width_(std::move(width)){require(static_cast<bool>(width_),"The clock artwork needs a text measurer");}
Rect DesktopClockArtworkPlan::indicatorRect(unsigned index){require(index<5,"Invalid clock style index");return {14+double(index)*64,124,56,18};}
std::optional<unsigned>DesktopClockArtworkPlan::indicatorAt(Point p){for(unsigned i=0;i<5;++i)if(const auto r=indicatorRect(i);p.x>=r.x&&p.x<r.x+r.width&&p.y>=r.y&&p.y<r.y+r.height)return i;return {};}
std::string_view DesktopClockArtworkPlan::workBadge(DesktopWorkPhase phase)noexcept{return phase==DesktopWorkPhase::running?"WORK MODE / ACTIVE":phase==DesktopWorkPhase::paused?"WORK MODE / PAUSED":"WORK MODE";}
std::string DesktopClockArtworkPlan::footerText(std::string_view shortcut,std::string_view close){
    // Windows: with opening by hotkey switched off there is no shortcut to name.
    if(shortcut.empty())return "ESC / "+std::string(close);
    return "ESC / "+std::string(shortcut)+" / "+std::string(close);}
double DesktopClockArtworkPlan::measure(const ChromeTextPlacement&t)const{
    if(t.text.empty())return 0;const auto w=width_(t.text,t.fontSize,t.weight,t.tabular);require(std::isfinite(w)&&w>=0,"Invalid measured clock text width");return w;}
// The largest size up to `size` at which the text fits `width` (0.5 pt steps, at least 10).
void DesktopClockArtworkPlan::fit(ChromeTextPlacement&t,double size,double width)const{
    t.fontSize=size;const auto measured=measure(t);
    if(measured>width)t.fontSize=std::max(10.,std::floor(size*width/measured*2)/2);}
// Frames a single line so its capitals start at capTop (top-aligned line box).
void DesktopClockArtworkPlan::place(ChromeTextPlacement&t,double x,double capTop,double width){
    using F=StandardFaceMetrics;const auto size=t.fontSize;
    t.frame={x,capTop-(F::ascender-F::capHeight)*size,std::max(0.,width),std::ceil((F::ascender-F::descender+F::leading)*size)+1};}
namespace {
void rect(std::vector<ChromePathElement>&p,Rect r){p.push_back({ChromePathElement::Kind::rectangle,r});}
void ellipse(std::vector<ChromePathElement>&p,Rect r){p.push_back({ChromePathElement::Kind::ellipse,r});}
void closePath(std::vector<ChromePathElement>&p){p.push_back({ChromePathElement::Kind::close,{}});}
// Swift Double(String): a plain decimal ("07"), else 0.
double number(std::string_view s){if(s.empty())return 0;double v=0;for(const char c:s){if(c<'0'||c>'9')return 0;v=v*10+(c-'0');}return v;}
std::vector<std::string_view> split(std::string_view s,char separator,unsigned maximum=~0u){
    // Swift split: empty subsequences omitted, maxSplits counts separators.
    std::vector<std::string_view>out;std::size_t start=0;unsigned splits=0;
    for(std::size_t i=0;i<s.size()&&splits<maximum;++i)if(s[i]==separator){if(i>start)out.push_back(s.substr(start,i-start));start=i+1;++splits;}
    if(start<s.size())out.push_back(s.substr(start));return out;}
ChromeColor alpha(std::array<double,3>c,double a){return {c[0],c[1],c[2],a};}
ChromeColor gray(double v,double a=1){return {v,v,v,a};}
}
bool DesktopClockArtworkPlan::update(DesktopClockStyle style,const std::optional<DesktopClockReading>&reading,const DesktopClockStatus&status,std::array<double,3> accent){
    require(static_cast<unsigned>(style)<5&&static_cast<unsigned>(status.work)<3,"Invalid clock style");
    for(const auto c:accent)require(std::isfinite(c)&&c>=0&&c<=1,"Invalid clock accent");
    Key key{style,reading,status,accent};if(previous_==key)return false;
    auto&o=artwork_;constexpr auto area=DesktopClockArtwork::content;constexpr auto row=DesktopClockArtwork::statusRow;
    const ChromeColor statusOff=gray(.45),red{0xE5/255.,0x38/255.,0x3B/255.,1},nearBlack=gray(0x11/255.),white=gray(.96),muted=gray(.64);
    // updateStatus: the status row on the panel (it stays put while a style slides in).
    o.rail={};o.rail.path.push_back({ChromePathElement::Kind::move,{row.x,88.5,0,0}});o.rail.path.push_back({ChromePathElement::Kind::line,{row.x+row.width,88.5,0,0}});
    rect(o.rail.path,{row.x,86,14,2.5});o.rail.stroke=gray(1,.16);o.rail.fill=alpha(accent,.8);
    const bool workActive=status.work!=DesktopWorkPhase::idle;
    o.recordLabel={};o.recordLabel.text="RECORDING MODE";o.recordLabel.fontSize=12;o.recordLabel.weight=600;o.recordLabel.color=status.recording?red:statusOff;
    const auto recordWidth=std::ceil(measure(o.recordLabel)),recordX=row.x+row.width-recordWidth;
    place(o.recordLabel,recordX,row.y+5,recordWidth+1);
    o.recordDot={recordX-10-3.5,row.y+9.5-3.5,7,7};o.recordDotColor=status.recording?red:statusOff;
    o.workBadge={};o.workBadge.text=std::string(workBadge(status.work));o.workBadge.fontSize=12;o.workBadge.weight=600;o.workBadge.truncates=true;
    o.workBadge.color=workActive?alpha(accent,1):statusOff;place(o.workBadge,row.x,row.y+5,recordX-24-row.x);
    // update(style:reading:...): "HH:mm:ss" or "hh:mm:ss a", the meridiem set apart.
    const std::string value=reading?reading->time:"--:--:--",day=reading?reading->date:"";
    const auto words=split(value,' ',1);const std::string meridiem=words.size()>1?std::string(words[1]):"";
    const std::string first=words.empty()?value:std::string(words[0]);
    const auto parts=split(first,':');std::string hm;for(std::size_t i=0;i<std::min<std::size_t>(2,parts.size());++i){if(i)hm+=':';hm+=parts[i];}
    const std::string ss=parts.size()>2?std::string(parts[2]):"--";
    double hour=parts.empty()?0:number(parts[0]);
    if(meridiem=="PM"&&hour<12)hour+=12;else if(meridiem=="AM"&&hour==12)hour=0;
    const double minute=parts.size()>1?number(parts[1]):0,second=parts.size()>2?number(parts[2]):0;
    o.time={};o.time.text=value;o.time.weight=700;o.time.tabular=true;o.time.truncates=true;o.time.color=white;o.time.fontSize=20;
    o.date={};o.date.text=day;o.date.weight=500;o.date.tabular=true;o.date.truncates=true;o.date.color=muted;o.date.fontSize=9;
    o.seconds={};o.seconds.text=ss;o.seconds.weight=700;o.seconds.tabular=true;o.seconds.color=alpha(accent,1);o.seconds.fontSize=22;o.seconds.hidden=true;
    o.aux={};o.aux.text=meridiem;o.aux.weight=700;o.aux.color=muted;o.aux.fontSize=12;o.aux.hidden=meridiem.empty();
    o.instrument={{},std::nullopt,alpha(accent,.55),1.5,ChromeShape::Cap::square};
    o.hands={{},std::nullopt,white,2.5,ChromeShape::Cap::round};o.secondHand={{},std::nullopt,alpha(accent,1),1.2,ChromeShape::Cap::square};
    o.fill={{},alpha(accent,1),std::nullopt,1,ChromeShape::Cap::butt};o.track={{},gray(1,.14),std::nullopt,1,ChromeShape::Cap::butt};
    o.selection={{},std::nullopt,alpha(accent,1),3,ChromeShape::Cap::butt};
    const auto selected=indicatorRect(static_cast<unsigned>(style));
    o.selection.path={{ChromePathElement::Kind::move,{selected.x,131,0,0}},{ChromePathElement::Kind::line,{selected.x+selected.width-7,131,0,0}}};
    auto&lines=o.instrument.path;auto&solid=o.fill.path;auto&dim=o.track.path;auto&hand=o.hands.path;auto&sweep=o.secondHand.path;
    const auto segment=[](std::vector<ChromePathElement>&p,Point a,Point b){p.push_back({ChromePathElement::Kind::move,{a.x,a.y,0,0}});p.push_back({ChromePathElement::Kind::line,{b.x,b.y,0,0}});};
    constexpr auto pi=std::numbers::pi;
    switch(style){
    case DesktopClockStyle::digital:{
        // Full-width time; date and a 60-second ruler share the line below.
        o.time.text=first;fit(o.time,50,area.width-(meridiem.empty()?0:44));place(o.time,area.x,17,area.width);
        if(!meridiem.empty()){o.aux.fontSize=14;place(o.aux,area.x+area.width-40,17,40);o.aux.alignment=ChromeTextAlignment::right;}
        o.date.fontSize=13;place(o.date,area.x,68,120);
        const double start=area.x+std::min(124.,std::ceil(measure(o.date))+14),end=area.x+area.width;
        for(int index=0;index<=60;++index){const double x=start+(end-start)*index/60,tall=index%15==0?9:index%5==0?6:3;
            rect(index<=int(second)?solid:dim,{x-.5,80-tall,1,tall});}
        break;}
    case DesktopClockStyle::split:
        // HH:MM over the full height | seconds and date in their own column.
        o.time.text=hm;fit(o.time,62,196);place(o.time,area.x,20,200);
        segment(lines,{226,18},{226,82});
        o.seconds.hidden=false;o.seconds.fontSize=30;place(o.seconds,238,20,46);
        if(!meridiem.empty()){o.aux.fontSize=12;place(o.aux,286,22,32);}
        o.date.fontSize=12;fit(o.date,12,80);place(o.date,238,58,80);
        rect(solid,{238,74,80*(second+1)/60,3});rect(dim,{238,74,80,3});
        break;
    case DesktopClockStyle::dial:{
        // A 74-point dial with a second hand; the digital reading beside it.
        const Point c{area.x+37,49.5};ellipse(lines,{c.x-36,c.y-36,72,72});
        for(int tick=0;tick<60;++tick){const double a=tick*pi/30;
            if(tick%5==0)segment(lines,{c.x+std::sin(a)*26,c.y-std::cos(a)*26},{c.x+std::sin(a)*32,c.y-std::cos(a)*32});
            else ellipse(dim,{c.x+std::sin(a)*31-.6,c.y-std::cos(a)*31-.6,1.2,1.2});}
        for(const auto [a,length]:{std::pair{(std::fmod(hour,12)+minute/60)*pi/6,17.},std::pair{(minute+second/60)*pi/30,25.}})
            segment(hand,c,{c.x+std::sin(a)*length,c.y-std::cos(a)*length});
        const double s=second*pi/30;segment(sweep,{c.x-std::sin(s)*7,c.y+std::cos(s)*7},{c.x+std::sin(s)*30,c.y-std::cos(s)*30});
        ellipse(solid,{c.x-2.5,c.y-2.5,5,5});
        o.time.text=first;const double column=area.x+area.width-104;
        fit(o.time,38,column-(meridiem.empty()?0:30));place(o.time,104,22,column);
        if(!meridiem.empty()){o.aux.fontSize=12;o.aux.alignment=ChromeTextAlignment::right;place(o.aux,area.x+area.width-30,22,30);}
        o.date.fontSize=13;place(o.date,104,64,column);
        segment(lines,{104,56.5},{area.x+area.width,56.5});
        break;}
    case DesktopClockStyle::rail:{
        // Time and date on one line above a 24-hour rail of the day so far.
        o.time.text=first;fit(o.time,40,200);place(o.time,area.x,17,210);
        o.date.fontSize=13;o.date.alignment=ChromeTextAlignment::right;place(o.date,area.x+area.width-90,20,90);
        if(!meridiem.empty()){o.aux.fontSize=13;o.aux.alignment=ChromeTextAlignment::right;place(o.aux,area.x+area.width-90,40,90);}
        const double left=area.x,width=area.width,progress=(hour*60+minute)/1440;
        for(int index=0;index<=24;++index){const double x=left+width*index/24,tall=index%6==0?8:4;rect(dim,{x-.5,71-tall,1,tall});}
        rect(dim,{left,74,width,4});rect(solid,{left,74,std::max(1.,width*progress),4});
        const double marker=left+width*progress;
        solid.push_back({ChromePathElement::Kind::move,{marker-4,82,0,0}});solid.push_back({ChromePathElement::Kind::line,{marker+4,82,0,0}});
        solid.push_back({ChromePathElement::Kind::line,{marker,78,0,0}});closePath(solid);
        break;}
    case DesktopClockStyle::stacked:{
        // A date band in the accent over the time; this week's days at its side.
        // HUDShape.chamfer(rect, cut: 7): the top-right corner cut.
        solid.push_back({ChromePathElement::Kind::move,{area.x,14,0,0}});solid.push_back({ChromePathElement::Kind::line,{area.x+area.width-7,14,0,0}});
        solid.push_back({ChromePathElement::Kind::line,{area.x+area.width,21,0,0}});solid.push_back({ChromePathElement::Kind::line,{area.x+area.width,35,0,0}});
        solid.push_back({ChromePathElement::Kind::line,{area.x,35,0,0}});closePath(solid);
        o.date.fontSize=13;o.date.weight=700;o.date.color=nearBlack;place(o.date,area.x+8,20,150);
        if(!meridiem.empty()){o.aux.fontSize=12;o.aux.alignment=ChromeTextAlignment::right;o.aux.color=nearBlack;place(o.aux,area.x+area.width-48,20.5,40);}
        o.time.text=first;fit(o.time,44,222);place(o.time,area.x,46,226);
        constexpr std::array<std::string_view,7> days{"MON","TUE","WED","THU","FRI","SAT","SUN"};
        const auto found=std::find(days.begin(),days.end(),std::string_view(day).substr(0,3));const int weekday=found==days.end()?-1:int(found-days.begin());
        for(int index=0;index<7;++index){const Rect cell{area.x+area.width-66+index*9.5,62,7,16};
            if(index==weekday)rect(solid,{cell.x,cell.y-4,cell.width,cell.height+8});else rect(dim,cell);}
        break;}
    }
    // The recording dot blinks with the wall clock: no loop of its own.
    o.recordDotOpacity=status.recording&&int(second)%2!=0?.28:1;
    previous_=std::move(key);return true;
}
double DesktopChromeTiming::opacity(bool opening,double elapsed,double captured,bool reduced)noexcept{
    if(reduced)return opening?1:0;const auto progress=fadeCurve.value((elapsed-(opening?openingDelay:closingDelay))/(opening?openingDuration:closingDuration));return opening?progress:captured*(1-progress);
}
bool DesktopChromeTiming::styleMovesForward(unsigned oldIndex,unsigned newIndex)noexcept{return (newIndex+5-oldIndex)%5<=2;}
} // namespace endfield::core::source
