#include "modules/work_mode_presentation.hpp"
#include "core/scene.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise,on,push)
#pragma fp_contract(off)
#endif
namespace endfield::modules {namespace {
constexpr double pi=3.1415926535897932384626433832795;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}void finite(double t){need(std::isfinite(t),"Nonfinite Work Mode time");}
double ease(double value,double x1,double y1,double x2,double y2){finite(value);if(value<=0)return 0;if(value>=1)return 1;const double x=static_cast<float>(value);x1=static_cast<float>(x1);y1=static_cast<float>(y1);x2=static_cast<float>(x2);y2=static_cast<float>(y2);const double ax=1-3*x2+3*x1,bx=3*x2-6*x1,cx=3*x1,ay=1-3*y2+3*y1,by=3*y2-6*y1,cy=3*y1;double t=x;for(unsigned n=0;n<8;++n){const auto error=((ax*t+bx)*t+cx)*t-x;if(std::abs(error)<1e-5)break;t-=error/((3*ax*t+2*bx)*t+cx);}return static_cast<float>(((ay*t+by)*t+cy)*t);}
double out(double t){return ease(t,0,0,.58,1);}double mix(double a,double b,double t){return a+(b-a)*t;}
std::string phase(WorkModePhase p,const WorkModeStrings&s){switch(p){case WorkModePhase::idle:return s.ready;case WorkModePhase::running:return "RUNNING";case WorkModePhase::paused:return "PAUSED";case WorkModePhase::stopped:return s.stopped;case WorkModePhase::completed:return s.completed;}throw std::invalid_argument("Invalid Work Mode phase");}
bool contains(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
}
double workModeLayoutProgress(double t){return ease(t,.22,.68,.24,1);}
WorkModeRingCut workModeRingCut(double fraction){need(std::isfinite(fraction)&&fraction>=0&&fraction<=1,"Invalid Work Mode ring fraction");
    // Original CGPath.addArc exported control value 118.741221207 / radius215.
    constexpr double c=118.741221207;const unsigned segment=fraction>=1?3:static_cast<unsigned>(fraction*4);const double t=fraction>=1?1:fraction*4-segment,u=1-t;
    double x=3*u*u*t*c+3*u*t*t*215+t*t*t*215,y=-u*u*u*215-3*u*u*t*215-3*u*t*t*c;
    double dx=3*u*u*c+6*u*t*(215-c),dy=6*u*t*(215-c)+3*t*t*c;
    for(unsigned n=0;n<segment;++n){const auto old=x;x=-y;y=old;const auto oldD=dx;dx=-dy;dy=oldD;}
    const double length=std::hypot(dx,dy),nx=-dx/length,ny=-dy/length;
    double angle=std::atan2(y,x);if(angle<-.5*pi)angle+=2*pi;if(fraction==1)angle=1.5*pi;
    return {angle,{nx,ny,-(nx*x+ny*y)}};
}
namespace {void validateStrings(const WorkModeStrings&s){
    for(const auto*p:{&s.heading,&s.countdown,&s.stopwatch,&s.pause,&s.reset,&s.resume,&s.start,&s.custom,&s.ready,&s.stopped,&s.completed,&s.invalidDuration,&s.focusAccess})need(p->size()<=4096&&ehud::data::Json::validUtf8(*p),"Invalid Work Mode localized label");
    for(const auto&p:s.presets)need(p.size()<=4096&&ehud::data::Json::validUtf8(p),"Invalid Work Mode preset label");
}}
bool WorkModePresentation::setStrings(WorkModeStrings value){if(value==strings_)return false;validateStrings(value);auto error=error_;if(error==strings_.invalidDuration)error=value.invalidDuration;auto nextPhase=error.empty()?phase(snapshot_.phase,value):error;strings_=std::move(value);error_=std::move(error);phaseText_=std::move(nextPhase);rebuildActions();++revision_;return true;}
WorkModePresentation::WorkModePresentation(WorkModeController&c,WorkModeStrings s,WorkModeViewHooks h):controller_(&c),strings_(std::move(s)),hooks_(std::move(h)){
    validateStrings(strings_);
    actions_.reserve(9);configuration_.reserve(6);refresh(0,false);
}
void WorkModePresentation::settle(){layoutMoving_=false;layoutFrom_=layoutTo_;configurationInteractive_=!expanded_;actionFeedback_=clockFeedback_=focusFeedback_=false;}
void WorkModePresentation::activate(double now){finite(now);if(active_)return;active_=true;controller_->setVisible(true,now);refresh(now,false);}
void WorkModePresentation::deactivate(double now){finite(now);active_=false;settle();controller_->setVisible(false,now);refresh(now,false);}
void WorkModePresentation::setReduceMotion(bool value,double now){finite(now);if(reduced_==value)return;reduced_=value;if(value)settle();refresh(now,!value);}
void WorkModePresentation::refresh(double now,bool animated){finite(now);const auto next=controller_->snapshot(now);bool dirty=false;
    if(!active_||controller_->suspended()||reduced_)actionFeedback_=clockFeedback_=focusFeedback_=false;
    if(timeText_.empty()||next.displayedSeconds()!=snapshot_.displayedSeconds()){timeText_=next.timeText();++clockRevision_;}
    const auto nextPhase=error_.empty()?phase(next.phase,strings_):error_;if(phaseText_!=nextPhase){phaseText_=nextPhase;dirty=true;}
    const bool expanded=next.active(),animate=animated&&active_&&!controller_->suspended()&&!reduced_;
    if(!layoutSet_||expanded!=expanded_){const auto previous=layout(now);const bool moving=layoutSet_&&animate;expanded_=expanded;layoutSet_=true;layoutFrom_=previous;layoutTo_={expanded?166.:218.5,expanded?1.28:1.,expanded?244.:276.,expanded?0.:1.,false};layoutStart_=now;layoutMoving_=moving;configurationInteractive_=!expanded&&!moving;dirty=true;}
    if(!animate&&layoutMoving_){settle();dirty=true;}else if(layoutMoving_&&now-layoutStart_>=.44){layoutMoving_=false;configurationInteractive_=!expanded_;dirty=true;}
    if(seenRevision_!=controller_->revision()||snapshot_.kind!=next.kind||snapshot_.duration!=next.duration||snapshot_.phase!=next.phase){seenRevision_=controller_->revision();dirty=true;}
    snapshot_=next;if(dirty||actions_.empty()){rebuildActions();++revision_;}
}
void WorkModePresentation::rebuildActions(){configuration_.clear();configuration_.push_back({"work:countdown",strings_.countdown,{94,86,122,27}});configuration_.push_back({"work:stopwatch",strings_.stopwatch,{224,86,122,27}});
    if(snapshot_.kind==WorkModeKind::countdown){constexpr int values[]{5,30,60};for(unsigned n=0;n<3;++n)configuration_.push_back({"work:preset:"+std::to_string(values[n]),strings_.presets[n],{94+65.*n,127,57,25}});configuration_.push_back({"work:custom",strings_.custom,{289,127,57,25}});}
    actions_.clear();if(!snapshot_.active()&&configurationInteractive_)actions_.assign(configuration_.begin(),configuration_.end());
    const auto id=snapshot_.phase==WorkModePhase::running?"work:pause":snapshot_.phase==WorkModePhase::paused?"work:resume":"work:start";const auto&label=snapshot_.phase==WorkModePhase::running?strings_.pause:snapshot_.phase==WorkModePhase::paused?strings_.resume:strings_.start;
    actions_.push_back({id,label,{100,296,116,30}});actions_.push_back({"work:reset",strings_.reset,{224,296,116,30}});if(focusPermission_&&!focus_.empty())actions_.push_back({"work:focusAccess",strings_.focusAccess,{60,25,320,24}});
}
std::optional<std::string_view>WorkModePresentation::actionAt(core::Point p)const{if(!std::isfinite(p.x)||!std::isfinite(p.y))return {};for(const auto&a:actions_)if(contains(a.rect,p))return a.id;return {};}
void WorkModePresentation::animateAction(std::string_view id,const WorkModeSnapshot&before,bool changed,double now){if(!active_||reduced_||controller_->suspended())return;bool found=false;for(const auto&a:configuration_)found=found||a.id==id;for(const auto&a:actions_)found=found||a.id==id;feedbackAction_=found?std::string(id):(snapshot_.phase==WorkModePhase::running?"work:pause":"work:start");actionStart_=now;actionFeedback_=true;if(changed){clockStart_=now;clockFeedback_=true;clockDirection_=snapshot_.kind!=before.kind?(snapshot_.kind==WorkModeKind::countdown?-1:1):(snapshot_.duration<before.duration?-1:1);}}
bool WorkModePresentation::perform(std::string_view id,double now){finite(now);const auto it=std::find_if(actions_.begin(),actions_.end(),[&](const auto&a){return a.id==id;});if(it==actions_.end())return false;const std::string owned(id); // actions may be rebuilt by this event
    if(owned=="work:focusAccess"){if(active_&&!reduced_){focusStart_=now;focusFeedback_=true;}if(hooks_.requestFocusAccess)hooks_.requestFocusAccess();return true;}
    const auto before=controller_->snapshot(now);const auto revision=controller_->revision();error_.clear();
    if(owned=="work:countdown"){if(before.kind!=WorkModeKind::countdown)controller_->chooseCountdown(before.duration,now);}else if(owned=="work:stopwatch"){if(before.kind!=WorkModeKind::stopwatch)controller_->chooseStopwatch(now);}else if(owned=="work:start")controller_->start(now);else if(owned=="work:pause")controller_->pause(now);else if(owned=="work:resume")controller_->resume(now);else if(owned=="work:reset")controller_->reset(now);else if(owned=="work:preset:5")controller_->chooseCountdown(300,now);else if(owned=="work:preset:30")controller_->chooseCountdown(1800,now);else if(owned=="work:preset:60")controller_->chooseCountdown(3600,now);else if(owned=="work:custom"&&hooks_.editDuration)hooks_.editDuration(durationEditorRect());refresh(now);animateAction(owned,before,revision!=controller_->revision(),now);return true;
}
bool WorkModePresentation::setCustomDuration(std::string_view text,double now){finite(now);const auto current=controller_->snapshot(now);const auto seconds=parseWorkModeDuration(text);if(current.active())return current.kind==WorkModeKind::countdown&&seconds&&*seconds==current.duration;if(!seconds){error_=strings_.invalidDuration;refresh(now);return false;}error_.clear();if(current.kind==WorkModeKind::countdown&&current.duration==*seconds){refresh(now);return true;}const bool changed=controller_->chooseCountdown(*seconds,now);refresh(now);if(changed)animateAction("work:custom",current,true,now);return changed;}
void WorkModePresentation::cancelCustomEditing(double now){finite(now);if(error_.empty())return;error_.clear();refresh(now);}
void WorkModePresentation::setFocusStatus(std::string status,bool permission,double now){finite(now);need(status.size()<=4096&&ehud::data::Json::validUtf8(status),"Invalid bounded Focus status");if(status==focus_&&focusPermission_==(permission&&!status.empty()))return;focus_=std::move(status);focusPermission_=permission&&!focus_.empty();rebuildActions();++revision_;}
WorkModeLayout WorkModePresentation::layout(double now)const{finite(now);if(!layoutSet_)return {};if(!layoutMoving_)return layoutTo_;const double elapsed=now-layoutStart_,p=workModeLayoutProgress(elapsed/.44);return {mix(layoutFrom_.clockY,layoutTo_.clockY,p),mix(layoutFrom_.clockScale,layoutTo_.clockScale,p),mix(layoutFrom_.phaseY,layoutTo_.phaseY,p),mix(layoutFrom_.configurationOpacity,layoutTo_.configurationOpacity,p),elapsed<.44};}
WorkModeFeedback WorkModePresentation::feedback(double now)const{finite(now);WorkModeFeedback value;value.action=feedbackAction_;if(actionFeedback_&&now-actionStart_<.18){value.scale=mix(.985,1,out((now-actionStart_)/.18));value.active=true;}if(clockFeedback_&&now-clockStart_<.22){const auto remaining=1-out((now-clockStart_)/.22);value.clock=core::Matrix4::translation(10*clockDirection_*remaining,11*remaining,-12*remaining);value.active=true;}if(focusFeedback_&&now-focusStart_<.16){value.focusOpacity=mix(.55,1,std::clamp((now-focusStart_)/.16,0.,1.));value.active=true;}return value;}
WorkModeRing WorkModePresentation::ring(double now)const{const auto value=controller_->snapshot(now);return {value.kind==WorkModeKind::countdown?value.remaining()/value.duration:.045,value.kind==WorkModeKind::countdown?0:value.progress()*2*pi,value.kind==WorkModeKind::countdown?4.:2.5,active_&&!controller_->suspended()&&value.phase==WorkModePhase::running&&!reduced_};}
bool WorkModePresentation::requiresFrames(double now)const{finite(now);if(!active_||reduced_||controller_->suspended())return false;return ring(now).animated||layout(now).active||feedback(now).active;}
std::string WorkModePresentation::accessibilityStatus()const{if(!error_.empty())return error_;auto result=(snapshot_.kind==WorkModeKind::countdown?strings_.countdown:strings_.stopwatch)+", "+timeText_+", "+phase(snapshot_.phase,strings_);if(!focus_.empty())result+=". "+focus_;return result;}
namespace {
using Json=ehud::data::Json;using Rect=core::Rect;using Point=core::Point;using Color=std::array<double,4>;
Color gray(double w,double a=1){return {w,w,w,a};}Color alpha(Color c,double a){c[3]=a;return c;}Json color(Color c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}Json box(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
Json layer(std::string id,Rect r,const char*kind="layer"){return Json::Object{{"id",std::move(id)},{"kind",kind},{"bounds",box({0,0,r.width,r.height})},{"position",Json::Array{r.x,r.y}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
Json cmd(const char*op,std::initializer_list<Point>points={}){Json::Array p;for(auto v:points)p.push_back(Json::Array{v.x,v.y});return Json::Object{{"op",op},{"points",std::move(p)}};}
Json::Array cut(Rect r){return {cmd("move",{{r.x+4,r.y}}),cmd("line",{{r.x+r.width,r.y}}),cmd("line",{{r.x+r.width,r.y+r.height-4}}),cmd("line",{{r.x+r.width-4,r.y+r.height}}),cmd("line",{{r.x,r.y+r.height}}),cmd("line",{{r.x,r.y+4}}),cmd("close")};}
Json::Array circle(){return {cmd("move",{{220,5}}),cmd("cubic",{{338.741221207,5},{435,101.25877879300002},{435,220}}),cmd("cubic",{{435,338.741221207},{338.74122120699997,435},{220,435}}),cmd("cubic",{{101.25877879299999,435},{5,338.74122120699997},{5,220}}),cmd("cubic",{{5,101.25877879299999},{101.25877879300002,5},{220,5}})};}
Json shape(std::string id,Rect rect,Json::Array path,std::optional<Color>fill={},std::optional<Color>stroke={},double width=1){auto node=layer(std::move(id),rect,"shape");node["shape"]=Json::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"},{"miterLimit",10},{"fillRule","non-zero"}};return node;}
Json text(std::string id,Rect r,std::string value,double size,Color ink,double scale,const char*weight="regular",bool wrapped=false){need(Json::validUtf8(value)&&value.size()<=4096,"Invalid Work Mode text");auto node=layer(std::move(id),r,"text");node["contentsScale"]=std::min(4.,std::ceil(std::clamp(scale,1.,4.)*1.35));const bool semi=std::string_view(weight)=="semibold",medium=std::string_view(weight)=="medium",digits=std::string_view(weight)=="digits";
    node["text"]=Json::Object{{"string",std::move(value)},{"fontSize",size},{"font",Json::Object{{"postScriptName",semi?".AppleSystemUIFontDemi":digits?".SFNS-Medium":medium?".AppleSystemUIFontMedium":".AppleSystemUIFont"},{"familyName",".AppleSystemUIFont"},{"pointSize",size},{"symbolicTraits",semi?2:0}}},{"foregroundColor",color(ink)},{"alignment","center"},{"wrapped",wrapped},{"truncation","end"}};return node;
}
struct Build {WorkModePart part;Json::Array children;
 void add(Json node,std::string action={},bool tint=false,bool rim=false,float opacity=1){const auto&p=node["position"].array();part.surfaces.push_back({node["id"].string(),core::Matrix4::translation(p[0].number(),p[1].number()),std::move(action),tint,rim,opacity});children.push_back(std::move(node));}
 void feedback(std::string id,Rect rect,Color accent){add(shape(id+"/tint",rect,cut({0,0,rect.width,rect.height}),alpha(accent,.30)),id,true,false,0);add(shape(id+"/rim",rect,cut({-2,-2,rect.width+4,rect.height+4}),{},accent,.9),id,false,true,.28f);}
 WorkModePart finish(){part.layers=layer("workMode.part",{});part.layers["allowsGroupOpacity"]=false;part.layers["children"]=std::move(children);return std::move(part);}
};
}
WorkModeArtwork prepareWorkModeArtwork(const WorkModePresentation&state,const WorkModeAppearance&appearance){need(std::isfinite(appearance.scale)&&appearance.scale>=1&&appearance.scale<=8,"Invalid Work Mode scale");for(double v:appearance.accent)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid Work Mode accent");const auto primary=gray(appearance.dark?.94:.11),muted=gray(appearance.dark?.64:.42),ink=gray(.13);const auto&strings=state.strings();const auto&value=state.value();WorkModeArtwork result;Build heading,base,ring,clock,phasePart,controls,configuration,focus;
 heading.add(text("workMode.heading",{130,53,180,20},strings.heading.starts_with("//")?strings.heading:"// "+strings.heading,13,primary,appearance.scale,"semibold"));result.header=heading.finish();
 base.add(shape("workMode.ringBase",{0,0,440,440},circle(),{},alpha(muted,.25),1.5));result.ringBase=base.finish();
 ring.add(shape("workMode.ring",{0,0,440,440},circle(),{},appearance.accent,value.kind==WorkModeKind::countdown?4:2.5));result.ring=ring.finish();
 // The clock part is local to its independent source viewport. The native
 // adapter supplies the wrapper transform and mask; feedback moves glyphs only.
 clock.add(text("workMode.clock",{0,0,344,77},state.timeText(),60,primary,appearance.scale,"digits"));result.clock=clock.finish();
 phasePart.add(text("workMode.phase",{0,0,262,30},state.phaseText(),11,muted,appearance.scale,"medium",true));result.phase=phasePart.finish();
 auto button=[&](Build&b,const WorkModeAction&a){const auto&r=a.rect;const bool selected=(a.id=="work:countdown"&&value.kind==WorkModeKind::countdown)||(a.id=="work:stopwatch"&&value.kind==WorkModeKind::stopwatch)||(a.id=="work:preset:5"&&value.duration==300)||(a.id=="work:preset:30"&&value.duration==1800)||(a.id=="work:preset:60"&&value.duration==3600)||(a.id=="work:custom"&&value.duration!=300&&value.duration!=1800&&value.duration!=3600);const bool primaryAction=a.id=="work:start"||a.id=="work:resume";
  b.add(shape(a.id+"/plate",r,cut({0,0,r.width,r.height}),primaryAction?appearance.accent:gray(appearance.dark?.78:.91),selected?appearance.accent:gray(appearance.dark?.94:.4,.5),selected?1.5:.6),a.id);b.feedback(a.id,r,appearance.accent);b.add(text(a.id+"/label",{r.x+4,r.y+5,r.width-8,r.height-10},a.label,a.id.starts_with("work:preset:")?10.5:11,ink,appearance.scale,"semibold"),a.id);
 };
 for(const auto&a:state.configurationActions())button(configuration,a);for(const auto&a:state.actions())if(a.rect.y==296)button(controls,a);result.controls=controls.finish();result.configuration=configuration.finish();
 if(!state.focusStatus().empty()){if(state.focusPermission()){const Rect r{60,25,320,24};focus.add(shape("workMode.focusAccess",r,cut({0,0,320,24}),alpha(muted,.08),alpha(appearance.accent,.5),.8),"work:focusAccess");focus.feedback("work:focusAccess",r,appearance.accent);}focus.add(text("workMode.focusStatus",{60,25,320,24},state.focusStatus(),9.5,state.focusPermission()?appearance.accent:muted,appearance.scale,"regular",true));}result.focus=focus.finish();return result;
}
}
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#endif
