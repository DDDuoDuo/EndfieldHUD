#include "core/module_presentation.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<std::size_t> allocations{};}
void*operator new(std::size_t size){allocations.fetch_add(1,std::memory_order_relaxed);if(auto*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace endfield::core;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
void checkNear(double value,double expected,const char*message){check(std::isfinite(value)&&std::abs(value-expected)<1e-12,message);}
bool offsetEqual(const ModuleOffset&a,const ModuleOffset&b){return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.rotationX==b.rotationX&&a.rotationY==b.rotationY&&a.perspective==b.perspective;}
bool frameEqual(MotionRect a,MotionRect b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
template<class Path>void pathEqual(const Path&a,const Path&b){for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<a[i].size();++j){checkNear(a[i][j].x,b[i][j].x,"Source path X retained");checkNear(a[i][j].y,b[i][j].y,"Source path Y retained");}}
void normalized(const ModulePresentationSample&s,Module module,std::uint64_t identity){
    check(s.selected==module&&s.requested==module&&s.current.module==module&&s.current.identity==identity,"Committed source screen and retained wrapper identity");
    check(!s.transitioning&&s.acceptsModuleInput&&!s.incoming,"Normalized host contains one interactive screen");
    check(!s.current.shutter&&!s.current.registration&&!s.current.transform.animated&&s.current.opacity==1,"Completion clears only transition presentation data");
    check(offsetEqual(s.current.transform.from,{})&&offsetEqual(s.current.transform.to,{}),"Normalized wrapper has identity transform endpoints");
}
void lifecycle(){
    ModulePresentation p;auto u=p.sample(0);const auto power=u.presentation.current.identity;
    normalized(u.presentation,Module::power,power);check(!p.requiresFrames()&&!u.change&&!u.completedRequest,"Stable module has no animation demand or repeated completion");
    u=p.select(Module::notes,1,true,false,10);const auto notes=u.presentation.incoming->identity;const auto first=p.generation();
    check(u.change==ModulePresentationChange{Module::power,Module::notes,true}&&p.selected()==Module::power&&p.requested()==Module::notes,"Selection starts a real two-screen swap without early commit");
    check(p.isPresenting(Module::power)&&p.isPresenting(Module::notes)&&!p.isPresenting(Module::map)&&p.requiresFrames(),"Only current and incoming modules are presenting");
    u=p.select(Module::map,1.05,true,false,11);check(!u.change&&!u.completedRequest&&p.pending()==Module::map&&u.presentation.incoming->identity==notes,"Queued request replaces destination without replacing active incoming screen");
    u=p.select(Module::calendar,1.06,true,false,12);check(p.pending()==Module::calendar,"Newest queued request wins");
    u=p.complete(first,1.1);check(!u.change&&!u.completedRequest&&p.selected()==Module::power,"Premature completion does not commit source content");
    u=p.sample(1.3);check(p.selected()==Module::notes&&p.requested()==Module::calendar&&p.pending()==std::nullopt,"First completion commits then begins queued destination");
    check(u.presentation.current.identity==notes&&u.change==ModulePresentationChange{Module::notes,Module::calendar,true}&&!u.completedRequest,"Queued begin emits actual presentation change and defers newest completion");
    const auto calendar=u.presentation.incoming->identity,second=p.generation();
    u=p.complete(first,1.31);check(p.generation()==second&&p.selected()==Module::notes&&!u.completedRequest,"Stale completion cannot commit a later generation");
    u=p.sample(1.6);normalized(u.presentation,Module::calendar,calendar);check(u.completedRequest==12&&!u.change,"Only latest completion fires after all swaps settle");
    u=p.sample(20);check(!u.completedRequest&&!p.requiresFrames(),"Idle sampling cannot replay completions or keep frames alive");
    u=p.select(Module::calendar,20,true,false,13);normalized(u.presentation,Module::calendar,calendar);check(u.completedRequest==13&&!u.change,"Same settled selection completes immediately without a presentation change");
}
void pendingCancelSettle(){
    ModulePresentation p;auto u=p.select(Module::notes,0,true,false,1);const auto notes=u.presentation.incoming->identity,power=u.presentation.current.identity;
    p.select(Module::map,.02,true,false,2);u=p.select(Module::notes,.03,true,false,3);
    check(!p.pending()&&p.requested()==Module::notes&&!u.change,"Requesting active destination clears pending module");
    u=p.settle(.1);normalized(u.presentation,Module::notes,notes);check(u.completedRequest==3&&u.change==ModulePresentationChange{Module::power,Module::notes,false},"Settle reuses incoming wrapper and delivers newest completion");
    p.select(Module::map,.2,true,false,4);p.select(Module::calendar,.21,true,false,5);const auto prior=p.generation();u=p.cancel(.22);
    normalized(u.presentation,Module::notes,notes);check(!u.completedRequest&&p.generation()>prior&&u.change==ModulePresentationChange{Module::notes,Module::notes,false},"Cancellation invalidates completion and restores last committed screen");
    u=p.complete(prior,1);normalized(u.presentation,Module::notes,notes);check(!u.completedRequest,"Completion after cancellation cannot revive incoming content");
    p.select(Module::map,2,true,false,6);p.select(Module::calendar,2.01,true,false,7);u=p.settle(2.02);
    check(u.presentation.current.module==Module::calendar&&u.presentation.current.identity!=notes&&u.completedRequest==7&&!p.requiresFrames(),"Settle creates most recent queued screen without animating intermediate destination");
    u=p.select(Module::power,3,false,false,8);check(u.presentation.current.module==Module::power&&u.presentation.current.identity!=power&&u.completedRequest==8,"Nonanimated select creates the source wrapper and completes immediately");
    p.select(Module::notes,4,true,false,9);u=p.select(Module::map,4.01,true,true,10);
    check(!u.presentation.transitioning&&u.presentation.current.module==Module::map&&u.completedRequest==10&&u.change==ModulePresentationChange{Module::power,Module::map,false},"Reduced-motion selection settles latest request immediately");
    p.select(Module::notes,5,true,false,11);p.select(Module::map,5.01,true,false);p.sample(5.3);u=p.sample(5.6);check(!u.completedRequest,"A newer nil completion replaces the earlier callback");
    p.select(Module::notes,6,true,false,12);p.select(Module::power,6.01,true,false,13);u=p.sample(60);
    check(u.presentation.current.module==Module::notes&&u.presentation.incoming->module==Module::power&&u.presentation.incoming->transform.easedProgress==0&&!u.completedRequest,"Delayed delivery starts queued transition now, without catch-up or skipped screen");
    u=p.sample(60.3);check(u.completedRequest==13&&!p.isTransitioning(),"Delayed queued transition keeps its full original duration");
}
void sourceTracks(){
    for(unsigned from=0;from<24;++from)for(unsigned to=0;to<24;++to){if(from==to)continue;
        const auto a=static_cast<Module>(from),b=static_cast<Module>(to);ModulePresentation p(a);auto u=p.select(b,0);const auto direction=moduleDirection(a,b);
        check(frameEqual(u.presentation.current.contentFrame,moduleLocalContentFrame(a))&&frameEqual(u.presentation.incoming->contentFrame,moduleLocalContentFrame(b)),"Every source module keeps its authored local content frame");
        check(offsetEqual(u.presentation.incoming->transform.from,ModuleTransitionStyle::incomingOffset(direction))&&offsetEqual(u.presentation.incoming->transform.to,{}),"Incoming transform preserves exact source endpoint construction");
        check(offsetEqual(u.presentation.current.transform.from,{})&&offsetEqual(u.presentation.current.transform.to,ModuleTransitionStyle::outgoingOffset(direction)),"Outgoing transform preserves exact opposite source endpoint");
        for(double time:{0.,.023,.054,.125,.21,.299}){
            u=p.sample(time);const auto&current=u.presentation.current;const auto&next=*u.presentation.incoming;
            check(current.opacity==1&&next.opacity==1&&!u.presentation.acceptsModuleInput,"Source wrappers never cross-fade or accept input mid-swap");
            checkNear(current.transform.easedProgress,ModuleTransitionStyle::timing.value(time/.3),"Outgoing uses source cubic timing");checkNear(next.transform.easedProgress,current.transform.easedProgress,"Both source transform tracks share one clock");
            pathEqual(*current.shutter,ModuleTransitionStyle::shutterAt(time,{-direction.x,-direction.y},false));pathEqual(*next.shutter,ModuleTransitionStyle::shutterAt(time,direction,true));
            pathEqual(next.registration->path,ModuleTransitionStyle::registrationAt(time,direction));checkNear(next.registration->opacity,ModuleTransitionStyle::registrationOpacityAt(time),"Registration uses its separate uneased opacity keyframes");
        }
        const auto identity=u.presentation.incoming->identity,generation=p.generation();p.setDark(false);u=p.sample(.299);
        check(u.presentation.incoming->registration->white==.22&&u.presentation.incoming->registration->colorAlpha==.38&&u.presentation.incoming->registration->lineWidth==.7,"Light theme preserves source neutral seam style");
        check(p.generation()==generation&&u.presentation.incoming->identity==identity,"Theme repaint preserves in-flight wrapper and generation");
        u=p.sample(.3);normalized(u.presentation,b,identity);
    }
    check(ModulePresentation::masksToBounds&&!ModulePresentation::allowsGroupOpacity&&frameEqual(ModulePresentation::hostFrame,{280,100,440,440})&&ModulePresentation::wrapperAnchor.x==.5&&ModulePresentation::wrapperPosition.x==220,"Original module host clipping, group opacity and center anchor stay explicit");
}
void guardsAndAllocation(){
    ModulePresentation p;p.select(Module::notes,1,true,false,99);const auto generation=p.generation();
    for(double time:{0.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        bool rejected=false;try{p.select(Module::map,time);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&p.generation()==generation&&p.requested()==Module::notes,"Invalid caller clock leaves selection and completion unchanged");
    }
    check(p.sample(1.3).completedRequest==99,"Rejected requests do not replace latest completion");
    const auto before=allocations.load();
    for(unsigned i=0;i<1000;++i)(void)p.sample(2+i/60.);
    p.select(Module::map,20);for(unsigned i=0;i<120;++i)(void)p.sample(20+i*.3/120.);p.sample(20.3);
    p.select(Module::calendar,21);p.select(Module::power,21.01);p.sample(21.3);p.cancel(21.4);
    const auto after=allocations.load();check(before==after,"Selection, steady frames, active samples, queue completion and cancellation allocate no heap storage");
}
// Paused Core Animation presentation layers of the Mac HUDMotion groups
// (macOS 15.7.4): enter = stIn of a 44 px x move with E inside the E group of
// .38 s; leave = fade 1 -> 0 and move to -0.6 x 44 inside the ease group of .13 s.
struct CAMotionSample {bool enter;double phase,opacity,x;};
constexpr CAMotionSample caMotion[]{
    {true,.0037,.024679255,41.37954665720463},{true,.05,.33706468,13.802465200424194},{true,.1234567,.797485,2.696241855621338},
    {true,.25,1,.3309149742126465},{true,.54321,1,.008237600326538086},{true,.6,1,.003902435302734375},{true,.87654321,1,1.5735626220703125e-05},
    {false,.0037,.9984544,-.04080386180430651},{false,.05,.9670485,-.8699187219142913},{false,.1234567,.86591226,-3.539916372299194},
    {false,.25,.59148943,-10.784679722785949},{false,.54321,.15790963,-22.23118572235107},{false,.6,.114766836,-23.37015552520752},
    {false,.87654321,.008800149,-26.167676067352293}};
void within(double value,double expected,double tolerance,const char*message){check(std::isfinite(value)&&std::abs(value-expected)<=tolerance,message);}
void designMotion(){
    for(const auto&s:caMotion){
        const auto v=s.enter?DesignMotion::enterAt(s.phase*DesignMotion::entrance,{44,0}):DesignMotion::leaveAt(s.phase*DesignMotion::leave,{44,0});
        // CA evaluates its timing in Float: agreement to its precision.
        within(v.opacity,s.opacity,5e-5,"Design motion opacity matches the Core Animation presentation");within(v.translation.x,s.x,3e-3,"Design motion translation matches the Core Animation presentation");check(v.translation.y==0,"An axis without travel does not move");
    }
    const auto rest=DesignMotion::enterAt(.38,{12,-30},.7);check(rest==DesignMotionSample{.7,{0,0}},"Entrance ends at the model value");
    check(DesignMotion::enterAt(.1,{12,-30},.7,0,true)==rest,"Reduce Motion skips the entrance");
    const auto before=DesignMotion::enterAt(.05,{12,-30},.7,DesignMotion::staggerDelay(3));check(before.opacity==0&&before.translation.x==12&&before.translation.y==-30,"A staggered row fills backwards until its delay");
    const auto mid=DesignMotion::enterAt(.2+.1,{0,44},.5,.1);checkNear(mid.opacity,.5*std::min(1.,DesignEasing::standard.value(.2/.38)/.6),"Entrance opacity reaches the layer opacity at 60 % of the eased clock");
    checkNear(mid.translation.y,44*(1-DesignEasing::standard.value(DesignEasing::standard.value(.2/.38))),"Entrance translation is eased twice (child E inside the group E)");
    checkNear(DesignMotion::staggerDelay(0),.06,"Stagger base");checkNear(DesignMotion::staggerDelay(5),.06+5*.036,"Stagger step");
    checkNear(DesignMotion::staggerDelay(14),.06+14*.036,"Stagger cap");checkNear(DesignMotion::staggerDelay(40),DesignMotion::staggerDelay(14),"Index capped at 14");checkNear(DesignMotion::staggerDelay(-2),.06,"Negative index is the first row");
    const auto held=DesignMotion::leaveAt(5,{44,22},.8);check(held.opacity==0&&held.translation.x==-.6*44&&held.translation.y==-.6*22,"The leave holds its end state until the swap");
    check(DesignMotion::leaveAt(.05,{44,22},.8,true)==DesignMotionSample{.8,{0,0}},"Reduce Motion leaves the model untouched");
    // HUDShellPosition.
    const auto a=ShellPosition::position(Module::system),b=ShellPosition::position(Module::notes);const auto o=ShellPosition::entranceOffset(Module::system,Module::notes);const double length=std::hypot(b.x-a.x,b.y-a.y);
    checkNear(o.x,(b.x-a.x)/length*44,"Switch vector X is the normalized shell vector x 44");checkNear(o.y,(b.y-a.y)/length*44,"Switch vector Y");
    const auto first=ShellPosition::entranceOffset(std::nullopt,Module::power),power=ShellPosition::position(Module::power);const auto fl=std::hypot(power.x-975,power.y-575);
    checkNear(first.x,(power.x-975)/fl*44,"The first module enters from the disc centre");checkNear(first.y,(power.y-575)/fl*44,"The first module enters from the disc centre (Y)");
    check(ShellPosition::entranceOffset(Module::map,Module::map).x==0&&ShellPosition::entranceOffset(Module::map,Module::map).y==0,"A zero-length vector does not move");
    check(ShellPosition::subpageOffset(1).x==44&&ShellPosition::subpageOffset(-1).x==-44&&ShellPosition::subpageOffset(0,1).y==44,"Sub-pages enter from the right, left or below");
    check(ShellPosition::position(Module::activityMonitor).x==1100&&ShellPosition::position(Module::addApp).y==1250&&ShellPosition::position(Module::volume).x==1552,"Spec Appendix A positions");
    // HUDDesignGeometry.
    const auto box=DesignBox::rect();const double u=1470./(1920*1.15);
    checkNear(box.x,500-295*u,"Box left");checkNear(box.y,320-305*u,"Box top");checkNear(box.width,590*u,"Box width");checkNear(box.height,510*u,"Box height");
    within(box.x,303.60,.01,"Box at x 303.60");within(box.y,116.94,.01,"Box at y 116.94");within(box.width,392.80,.01,"Box 392.80 wide");within(box.height,339.54,.01,"Box 339.54 high");
    check(box.x>=280&&box.y>=100&&box.x+box.width<=720&&box.y+box.height<=540,"The box lies inside the 440 x 440 viewport");
}
void contentDim(){
    ContentDimTrack t;const auto E=[](double x){return DesignEasing::standard.value(x);};
    check(!t.needed(0)&&t.amount(0)==0,"No filter before a confirm");
    check(t.set(true,0)&&!t.set(true,.1),"Dimming starts once; repeats are ignored");
    checkNear(t.amount(0),0,"Filters are added at their neutral values");checkNear(t.amount(.125),E(.5),"Eased in with E");checkNear(t.amount(.25),1,"Fully dimmed after .25 s");check(t.needed(5)&&!t.animating(5),"Dimmed: the filter stays");
    checkNear(ContentDimTrack::blurRadius*t.amount(.125),1.5*E(.5),"The radius eases with the amount");checkNear(ContentDimTrack::exposure,-2.2,"-2.2 EV");
    t.set(false,1);checkNear(t.amount(1.1),1-E(.4),"Eased out with E");check(t.needed(1.2)&&!t.needed(1.25)&&t.amount(1.25)==0,"The filter goes once the ease-out completes");
    t.set(true,2);t.set(false,2.1);const double from=E(.4);checkNear(t.amount(2.1),from,"A reversal starts from the presentation value");checkNear(t.amount(2.1+.125),from*(1-E(.5)),"and eases to neutral");
    t.set(true,3,true);check(t.amount(3)==1&&!t.animating(3),"Reduce Motion jumps");t.reset();check(!t.needed(3)&&t.amount(3)==0,"Reset removes everything at once");
}
void directional(){
    ModulePresentation p(Module::system);p.setDesignScreen(Module::notes,true);p.setDesignScreen(Module::clipboard,true);
    auto u=p.sample(0);check(u.presentation.current.hostClip&&!u.presentation.current.designScreen&&!u.presentation.directional,"An unported module keeps the v1.2.0 clip");
    const auto system=u.presentation.current.identity;
    u=p.select(Module::notes,1,true,false,7);const auto&s=u.presentation;
    check(u.change==ModulePresentationChange{Module::system,Module::notes,true}&&s.directional&&s.transitioning&&!s.acceptsModuleInput,"A design destination switches directionally");
    check(s.incoming&&!s.incoming->attached&&s.incoming->opacity==0&&s.incoming->designScreen&&!s.incoming->shutter&&!s.incoming->registration&&!s.incoming->transform.animated,"The incoming screen is prepared, not drawn, and has no shutter");
    check(!s.current.hostClip&&!s.incoming->hostClip&&!s.current.shutter&&s.current.keepsOwnPlacement,"The host clip is off and the unported screen keeps its own placement");
    const auto notes=s.incoming->identity;const auto offset=ShellPosition::entranceOffset(Module::system,Module::notes);
    for(double t:{0.,.013,.065,.1,.129}){u=p.sample(1+t);const auto e=DesignEasing::ease.value(t/.13);
        checkNear(u.presentation.current.opacity,1-e,"Leave fades the wrapper");checkNear(u.presentation.current.planeOffset.x,-.6*offset.x*DesignBox::baseUnit*e,"Leave moves toward -0.6 x vector in design units");
        checkNear(u.presentation.current.planeOffset.y,-.6*offset.y*DesignBox::baseUnit*e,"Leave moves in Y");check(!u.presentation.incoming->attached&&!u.completedRequest,"Nothing attaches before the swap");}
    u=p.sample(1.14);const auto&d=u.presentation;
    check(d.current.module==Module::notes&&d.current.identity==notes&&d.current.attached&&d.current.opacity==1&&!d.incoming&&!d.transitioning&&d.acceptsModuleInput&&!d.directional,"The swap attaches and finishes in one sample");
    check(d.current.entrance&&d.current.entrance->start==1.14&&d.current.entrance->elapsed==0&&d.current.entrance->offset.x==offset.x&&d.current.entrance->offset.y==offset.y,"The entrance starts with the swap, from the switch vector");
    check(u.completedRequest==7&&!d.current.hostClip&&d.current.designScreen&&d.current.planeOffset.x==0,"Completion; a design screen stays unclipped");
    u=p.sample(1.5);checkNear(u.presentation.current.entrance->elapsed,.36,"The entrance clock runs on the caller clock");check(!p.requiresFrames(),"The host needs no frames for the content's own stagger");
    // Back to an unported module: the v1.2.0 shutter, unclipped while the design screen leaves.
    u=p.select(Module::system,2);check(!u.presentation.directional&&u.presentation.current.shutter&&u.presentation.incoming->shutter&&u.presentation.incoming->registration,"An unported destination keeps the shutter");
    check(!u.presentation.current.hostClip&&u.presentation.current.designScreen&&!u.presentation.incoming->designScreen&&!u.presentation.incoming->keepsOwnPlacement,"The shutter keeps the host state the design screen settled with");
    check(u.presentation.current.opacity==1&&u.presentation.current.entrance,"The shutter never fades its wrappers");
    u=p.sample(2.3);check(u.presentation.current.module==Module::system&&u.presentation.current.hostClip&&!u.presentation.current.entrance,"Settled on an unported module the clip returns");
    (void)system;
}
void directionalSupersede(){
    ModulePresentation p(Module::system);p.setDesignScreen(Module::notes,true);p.setDesignScreen(Module::clipboard,true);
    auto u=p.select(Module::notes,0,true,false,1);u=p.select(Module::clipboard,.05,true,false,2);check(p.pending()==Module::clipboard&&!u.change,"A module clicked during the leave queues");
    u=p.sample(.13);const auto&s=u.presentation;
    check(s.current.module==Module::clipboard&&s.current.attached&&s.current.entrance&&s.current.entrance->start==.13&&!s.transitioning&&u.completedRequest==2,"The superseded destination is skipped and its follower swaps in the same sample");
    const auto o=ShellPosition::entranceOffset(Module::notes,Module::clipboard);check(s.current.entrance->offset.x==o.x&&s.current.entrance->offset.y==o.y,"The follower enters from the skipped module's vector, as on the Mac");
    check(u.change==ModulePresentationChange{Module::notes,Module::clipboard,true},"The follower's change is reported");
    // Superseded by an unported module: the shutter runs over the skipped, undrawn screen.
    ModulePresentation q(Module::system);q.setDesignScreen(Module::notes,true);q.select(Module::notes,0);q.select(Module::map,.05);u=q.sample(.13);
    check(u.presentation.current.module==Module::notes&&!u.presentation.current.attached&&u.presentation.current.opacity==0&&u.presentation.incoming->module==Module::map&&u.presentation.incoming->shutter,"A skipped screen is never drawn under the shutter");
    check(!u.presentation.current.hostClip,"A skipped design screen leaves the host unclipped, as its normalize does");
    u=q.cancel(.2);check(u.presentation.current.module==Module::notes&&u.presentation.current.attached&&!u.presentation.incoming,"Cancel restores the committed screen attached");
    // Retargeting back to the leaving module clears the queue and the leave continues.
    ModulePresentation r(Module::system);r.setDesignScreen(Module::notes,true);r.select(Module::notes,0);r.select(Module::map,.02);u=r.select(Module::notes,.03);
    check(!r.pending()&&r.isTransitioning()&&u.presentation.directional,"Pressing the destination again clears the pending module");
    u=r.sample(.13);check(u.presentation.current.module==Module::notes&&u.presentation.current.attached&&u.presentation.current.entrance,"The original destination lands on its deadline");
    // Cancel during the leave restores the committed screen.
    ModulePresentation c(Module::display);c.setDesignScreen(Module::notes,true);c.select(Module::notes,0,true,false,3);u=c.cancel(.05);
    check(u.presentation.current.module==Module::display&&u.presentation.current.opacity==1&&u.presentation.current.planeOffset.x==0&&!u.presentation.incoming&&!u.completedRequest,"Cancel during the leave restores the old screen at rest");
}
void directionalEntranceAndReduce(){
    ModulePresentation p(Module::notes);p.setDesignScreen(Module::notes,true);auto u=p.beginEntrance(0);const auto o=ShellPosition::entranceOffset(std::nullopt,Module::notes);
    check(u.presentation.current.entrance&&u.presentation.current.entrance->offset.x==o.x&&u.presentation.current.entrance->offset.y==o.y&&!u.presentation.current.hostClip,"The first module enters from the disc centre");
    ModulePresentation m(Module::notes);m.setDesignScreen(Module::notes,true);check(!m.beginEntrance(0,true).presentation.current.entrance,"No entrance under Reduce Motion");
    ModulePresentation l(Module::map);check(!l.beginEntrance(0).presentation.current.entrance,"An unported module has no entrance");
    u=p.select(Module::clipboard,1,true,true,4);check(!u.presentation.transitioning&&!u.presentation.current.entrance&&u.completedRequest==4,"Reduce Motion collapses the switch: no leave and no entrance");
    p.setDesignScreen(Module::clipboard,true);u=p.select(Module::notes,2,false);check(u.presentation.current.designScreen&&!u.presentation.current.hostClip&&!u.presentation.current.entrance,"A settle installs without an entrance");
    const auto before=allocations.load();p.select(Module::clipboard,3);for(unsigned i=0;i<60;++i)(void)p.sample(3+i*.004);p.select(Module::notes,4);p.select(Module::map,4.05);p.sample(4.2);p.sample(4.6);
    check(allocations.load()==before,"Directional switches, leaves, skips and entrances allocate nothing");
}
}
int main(){try{lifecycle();pendingCancelSettle();sourceTracks();guardsAndAllocation();designMotion();contentDim();directional();directionalSupersede();directionalEntranceAndReduce();std::cout<<"PASS "<<checks<<" module presentation checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
