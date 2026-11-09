#include "core/module_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::core {
namespace {
void validate(Module module){(void)moduleIdentifier(module);}
}
ModulePresentation::ModulePresentation(Module initial):state_(initial),current_{initial,1},notified_(initial){validate(initial);}
const ModulePresentationSurface*ModulePresentationSample::surface(Module module)const noexcept{
    if(current.module==module)return &current;
    if(incoming&&incoming->module==module)return &*incoming;
    if(departing&&departing->module==module)return &*departing;
    return nullptr;
}
// The one leave sample (HUDMotion.leave): the switch leave, a departing
// screen's and the close's leave; it holds its end (opacity 0) after .13 s.
DesignMotionSample ModulePresentation::leaveSample(const Leave&leave,double time)noexcept{
    return DesignMotion::leaveAt(time-leave.start,{leave.offset.x*DesignBox::baseUnit,leave.offset.y*DesignBox::baseUnit});
}
bool ModulePresentation::isPresenting(Module module) const noexcept{return current_.module==module||(incoming_&&incoming_->module==module);}
void ModulePresentation::setDesignScreen(Module module,bool design){validate(module);design_[static_cast<std::size_t>(module)]=design;}
void ModulePresentation::advance(double time){
    if(!std::isfinite(time)||(lastTime_&&time<*lastTime_))throw std::invalid_argument("Module presentation requires a finite monotonic caller clock");
    lastTime_=time;
}
void ModulePresentation::begin(const ModuleTransition&transition,double time){
    endClose();
    transition_=transition;started_=time;directional_=isDesignScreen(transition.to);
    // HUDModuleContent.beginDirectional: the incoming screen is made (built)
    // now and attached at the swap; a screen skipped earlier has nothing to leave.
    incoming_=Screen{transition.to,++nextIdentity_,!directional_};
    if(directional_){leaveOffset_=ShellPosition::entranceOffset(transition.from,transition.to);swapAt_=time+(current_.attached?DesignMotion::leave:0);}
}
bool ModulePresentation::due(double time)const noexcept{return transition_&&time>=(directional_?swapAt_:started_+ModuleTransitionStyle::duration);}
ModulePresentationUpdate ModulePresentation::snapshot(double time)const{
    ModulePresentationUpdate result;auto&frame=result.presentation;
    frame.selected=state_.selected();frame.requested=state_.requested();frame.generation=state_.generation();
    frame.transitioning=state_.isTransitioning();frame.acceptsModuleInput=!frame.transitioning;frame.directional=transition_&&directional_;
    const bool hostClip=!frame.directional&&!isDesignScreen(current_.module),ownPlacement=isDesignScreen(state_.requested());
    const auto surface=[&](ModulePresentationSurface&s,const Screen&screen){s.identity=screen.identity;s.module=screen.module;s.contentFrame=moduleLocalContentFrame(screen.module);
        s.designScreen=isDesignScreen(screen.module);s.hostClip=hostClip;s.keepsOwnPlacement=ownPlacement&&!s.designScreen;s.attached=screen.attached&&!screen.hidden;if(!s.attached)s.opacity=0;};
    surface(frame.current,current_);
    if(leave_&&frame.current.attached){const auto leave=leaveSample(*leave_,time);frame.current.opacity=leave.opacity;frame.current.planeOffset=leave.translation;}
    if(departing_&&time-departing_->second.start<DesignMotion::leave){
        frame.departing.emplace();surface(*frame.departing,departing_->first);
        const auto leave=leaveSample(departing_->second,time);frame.departing->opacity=leave.opacity;frame.departing->planeOffset=leave.translation;
    }
    if(entrance_&&current_.attached)frame.current.entrance=ModuleEntrance{entrance_->offset,entrance_->start,std::max(0.,time-entrance_->start),entrance_->delay};
    if(incoming_&&transition_&&directional_){
        // The leave moves the wrapper in host (design) units: the switch vector in content px x the content unit.
        if(current_.attached){const auto leave=leaveSample({started_,leaveOffset_},time);
            frame.current.opacity=leave.opacity;frame.current.planeOffset=leave.translation;}
        frame.incoming.emplace();surface(*frame.incoming,*incoming_);
    }else if(incoming_&&transition_){
        const double elapsed=std::clamp(time-started_,0.,ModuleTransitionStyle::duration);
        const auto direction=moduleDirection(transition_->from,transition_->to);const MotionPoint opposite{-direction.x,-direction.y};
        const auto eased=ModuleTransitionStyle::timing.value(elapsed/ModuleTransitionStyle::duration);
        frame.current.transform={{},ModuleTransitionStyle::outgoingOffset(direction),eased,true};
        frame.current.shutter=ModuleTransitionStyle::shutterAt(elapsed,opposite,false);
        frame.incoming.emplace();auto&next=*frame.incoming;surface(next,*incoming_);
        next.transform={ModuleTransitionStyle::incomingOffset(direction),{},eased,true};
        next.shutter=ModuleTransitionStyle::shutterAt(elapsed,direction,true);
        next.registration=ModulePresentationRegistration{ModuleTransitionStyle::registrationAt(elapsed,direction),ModuleTransitionStyle::registrationOpacityAt(elapsed),ModuleTransitionStyle::registrationWhite(dark_)};
    }
    if(concealed_){frame.current.opacity=0;if(frame.incoming)frame.incoming->opacity=0;if(frame.departing)frame.departing->opacity=0;}
    return result;
}
void ModulePresentation::attachCompletion(ModulePresentationUpdate&result){result.completedRequest=completion_;completion_.reset();}
ModulePresentationUpdate ModulePresentation::select(Module module,double time,bool animated,bool reduceMotion,std::optional<std::uint64_t> completion){
    validate(module);advance(time);completion_=completion;endClose();
    if(!animated||reduceMotion)return settleOn(module,time);
    std::optional<ModulePresentationChange> change;
    if(const auto next=state_.request(module)){begin(*next,time);change=ModulePresentationChange{next->from,next->to,true};}
    reported(change);
    auto result=snapshot(time);result.change=change;if(!state_.isTransitioning())attachCompletion(result);return result;
}
ModulePresentationUpdate ModulePresentation::finish(double time){
    const auto finished=*transition_;const bool directional=directional_;
    // Directional swap, one transaction: a pending request supersedes this
    // destination, which is then skipped rather than shown only to leave again.
    const bool superseded=directional&&state_.pending().has_value();
    current_=*incoming_;current_.attached=!superseded;incoming_.reset();transition_.reset();directional_=false;entrance_.reset();
    if(directional&&!superseded)entrance_=ModuleEntrance{leaveOffset_,time,0};
    std::optional<ModulePresentationChange> change;
    if(const auto following=state_.complete(finished.generation)){begin(*following,time);change=ModulePresentationChange{following->from,following->to,true};}
    reported(change);
    auto result=snapshot(time);result.change=change;if(!state_.isTransitioning())attachCompletion(result);return result;
}
ModulePresentationUpdate ModulePresentation::run(double time){
    // A skipped screen leaves nothing: its follower swaps in the same sample.
    if(!due(time))return snapshot(time);
    std::optional<ModulePresentationChange> change;ModulePresentationUpdate result;
    for(unsigned guard=0;guard<4&&(guard==0||due(time));++guard){result=finish(time);if(result.change)change=result.change;}
    result.change=change;return result;
}
ModulePresentationUpdate ModulePresentation::sample(double time){
    advance(time);
    // A departing screen is dropped once its leave ended.
    if(departing_&&time-departing_->second.start>=DesignMotion::leave)departing_.reset();
    return run(time);
}
ModulePresentationUpdate ModulePresentation::complete(std::uint64_t generation,double time){
    advance(time);if(transition_&&generation==state_.generation())return run(time);return snapshot(time);
}
ModulePresentationUpdate ModulePresentation::beginEntrance(double time,bool reduceMotion,std::optional<MotionPoint> offset,double delay){
    if(!std::isfinite(delay)||delay<0)throw std::invalid_argument("A module entrance delay is finite and not negative");
    advance(time);endClose();if(!transition_&&current_.attached&&isDesignScreen(current_.module)&&!reduceMotion)entrance_=ModuleEntrance{offset.value_or(ShellPosition::entranceOffset(std::nullopt,current_.module)),time,0,delay};
    return snapshot(time);
}
ModulePresentationUpdate ModulePresentation::settleOn(Module module,double time){
    const auto previous=current_.module;state_.settle(module);endClose();
    // HUDModuleContent.settle(on:) plays no entrance; a kept current screen's
    // own entrance runs on (its canvas animations are not module.* keys).
    if(current_.module!=module){entrance_.reset();if(incoming_&&incoming_->module==module)current_=*incoming_;else current_={module,++nextIdentity_};}
    current_.attached=true;incoming_.reset();transition_.reset();directional_=false;auto result=snapshot(time);result.change=ModulePresentationChange{previous,module,false};reported(result.change);attachCompletion(result);return result;
}
ModulePresentationUpdate ModulePresentation::settle(double time){advance(time);return settleOn(state_.requested(),time);}
ModulePresentationUpdate ModulePresentation::cancel(double time){
    // Restores the last committed screen attached (the Mac's cancel would leave
    // a skipped, never-attached screen detached until the next selection).
    advance(time);state_.cancel();completion_.reset();incoming_.reset();transition_.reset();directional_=false;endClose();if(!current_.attached){current_.attached=true;entrance_.reset();}
    auto result=snapshot(time);result.change=ModulePresentationChange{current_.module,current_.module,false};reported(result.change);return result;
}
ModulePresentationUpdate ModulePresentation::settleForClose(double time){
    advance(time);
    // A swap due now lands first: a close at +.14 lands the new screen, which
    // then plays the close's leave, instead of hiding it.
    const auto ran=run(time);reported(ran.change);
    const bool inFlight=state_.isTransitioning();
    const bool leaving=transition_&&directional_&&current_.attached&&!current_.hidden&&time-started_<DesignMotion::leave;
    const Screen old=current_;const Leave switchLeave{started_,leaveOffset_};
    const auto from=notified_;
    completion_.reset(); // the close drops the completion, as cancel() does
    auto result=settleOn(state_.requested(),time);
    if(leaving&&current_.identity==old.identity)leave_=switchLeave; // reversed: it keeps leaving, not hidden
    else if(inFlight){current_.hidden=true;if(leaving)departing_.emplace(old,switchLeave);}
    result=snapshot(time);
    result.change=ModulePresentationChange{from,current_.module,true};reported(result.change);
    return result;
}
ModulePresentationUpdate ModulePresentation::leaveForClose(double time,bool reduceMotion){
    advance(time);
    if(!reduceMotion&&!transition_&&!leave_&&current_.attached&&!current_.hidden&&isDesignScreen(current_.module)){
        const auto offset=ShellPosition::entranceOffset(std::nullopt,current_.module);
        leave_=Leave{time,{-offset.x,-offset.y}};
    }
    return snapshot(time);
}
} // namespace endfield::core
