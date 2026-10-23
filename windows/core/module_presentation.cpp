#include "core/module_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::core {
namespace {
void validate(Module module){(void)moduleIdentifier(module);}
}
ModulePresentation::ModulePresentation(Module initial):state_(initial),current_{initial,1}{validate(initial);}
bool ModulePresentation::isPresenting(Module module) const noexcept{return current_.module==module||(incoming_&&incoming_->module==module);}
void ModulePresentation::setDesignScreen(Module module,bool design){validate(module);design_[static_cast<std::size_t>(module)]=design;}
void ModulePresentation::advance(double time){
    if(!std::isfinite(time)||(lastTime_&&time<*lastTime_))throw std::invalid_argument("Module presentation requires a finite monotonic caller clock");
    lastTime_=time;
}
void ModulePresentation::begin(const ModuleTransition&transition,double time){
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
        s.designScreen=isDesignScreen(screen.module);s.hostClip=hostClip;s.keepsOwnPlacement=ownPlacement&&!s.designScreen;s.attached=screen.attached;if(!screen.attached)s.opacity=0;};
    surface(frame.current,current_);
    if(entrance_&&current_.attached)frame.current.entrance=ModuleEntrance{entrance_->offset,entrance_->start,std::max(0.,time-entrance_->start)};
    if(incoming_&&transition_&&directional_){
        // The leave moves the wrapper in host (design) units: the switch vector in content px x the content unit.
        if(current_.attached){const auto leave=DesignMotion::leaveAt(time-started_,{leaveOffset_.x*DesignBox::baseUnit,leaveOffset_.y*DesignBox::baseUnit});
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
    if(concealed_){frame.current.opacity=0;if(frame.incoming)frame.incoming->opacity=0;}
    return result;
}
void ModulePresentation::attachCompletion(ModulePresentationUpdate&result){result.completedRequest=completion_;completion_.reset();}
ModulePresentationUpdate ModulePresentation::select(Module module,double time,bool animated,bool reduceMotion,std::optional<std::uint64_t> completion){
    validate(module);advance(time);completion_=completion;
    if(!animated||reduceMotion)return settleOn(module,time);
    std::optional<ModulePresentationChange> change;
    if(const auto next=state_.request(module)){begin(*next,time);change=ModulePresentationChange{next->from,next->to,true};}
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
    auto result=snapshot(time);result.change=change;if(!state_.isTransitioning())attachCompletion(result);return result;
}
ModulePresentationUpdate ModulePresentation::run(double time){
    // A skipped screen leaves nothing: its follower swaps in the same sample.
    if(!due(time))return snapshot(time);
    std::optional<ModulePresentationChange> change;ModulePresentationUpdate result;
    for(unsigned guard=0;guard<4&&(guard==0||due(time));++guard){result=finish(time);if(result.change)change=result.change;}
    result.change=change;return result;
}
ModulePresentationUpdate ModulePresentation::sample(double time){advance(time);return run(time);}
ModulePresentationUpdate ModulePresentation::complete(std::uint64_t generation,double time){
    advance(time);if(transition_&&generation==state_.generation())return run(time);return snapshot(time);
}
ModulePresentationUpdate ModulePresentation::beginEntrance(double time,bool reduceMotion,std::optional<MotionPoint> offset){
    advance(time);if(!transition_&&current_.attached&&isDesignScreen(current_.module)&&!reduceMotion)entrance_=ModuleEntrance{offset.value_or(ShellPosition::entranceOffset(std::nullopt,current_.module)),time,0};
    return snapshot(time);
}
ModulePresentationUpdate ModulePresentation::settleOn(Module module,double time){
    const auto previous=current_.module;state_.settle(module);
    // HUDModuleContent.settle(on:) plays no entrance; a kept current screen's
    // own entrance runs on (its canvas animations are not module.* keys).
    if(current_.module!=module){entrance_.reset();if(incoming_&&incoming_->module==module)current_=*incoming_;else current_={module,++nextIdentity_};}
    current_.attached=true;incoming_.reset();transition_.reset();directional_=false;auto result=snapshot(time);result.change=ModulePresentationChange{previous,module,false};attachCompletion(result);return result;
}
ModulePresentationUpdate ModulePresentation::settle(double time){advance(time);return settleOn(state_.requested(),time);}
ModulePresentationUpdate ModulePresentation::cancel(double time){
    // Restores the last committed screen attached (the Mac's cancel would leave
    // a skipped, never-attached screen detached until the next selection).
    advance(time);state_.cancel();completion_.reset();incoming_.reset();transition_.reset();directional_=false;if(!current_.attached){current_.attached=true;entrance_.reset();}
    auto result=snapshot(time);result.change=ModulePresentationChange{current_.module,current_.module,false};return result;
}
} // namespace endfield::core
