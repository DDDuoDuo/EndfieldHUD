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
void ModulePresentation::advance(double time){
    if(!std::isfinite(time)||(lastTime_&&time<*lastTime_))throw std::invalid_argument("Module presentation requires a finite monotonic caller clock");
    lastTime_=time;
}
void ModulePresentation::begin(const ModuleTransition&transition,double time){
    transition_=transition;incoming_=Screen{transition.to,++nextIdentity_};started_=time;
}
ModulePresentationUpdate ModulePresentation::snapshot(double time)const{
    ModulePresentationUpdate result;auto&frame=result.presentation;
    frame.selected=state_.selected();frame.requested=state_.requested();frame.generation=state_.generation();
    frame.transitioning=state_.isTransitioning();frame.acceptsModuleInput=!frame.transitioning;
    frame.current.identity=current_.identity;frame.current.module=current_.module;frame.current.contentFrame=moduleLocalContentFrame(current_.module);
    if(incoming_&&transition_){
        const double elapsed=std::clamp(time-started_,0.,ModuleTransitionStyle::duration);
        const auto direction=moduleDirection(transition_->from,transition_->to);const MotionPoint opposite{-direction.x,-direction.y};
        const auto eased=ModuleTransitionStyle::timing.value(elapsed/ModuleTransitionStyle::duration);
        frame.current.transform={{},ModuleTransitionStyle::outgoingOffset(direction),eased,true};
        frame.current.shutter=ModuleTransitionStyle::shutterAt(elapsed,opposite,false);
        frame.incoming.emplace();auto&next=*frame.incoming;next.identity=incoming_->identity;next.module=incoming_->module;next.contentFrame=moduleLocalContentFrame(incoming_->module);
        next.transform={ModuleTransitionStyle::incomingOffset(direction),{},eased,true};
        next.shutter=ModuleTransitionStyle::shutterAt(elapsed,direction,true);
        next.registration=ModulePresentationRegistration{ModuleTransitionStyle::registrationAt(elapsed,direction),ModuleTransitionStyle::registrationOpacityAt(elapsed),ModuleTransitionStyle::registrationWhite(dark_)};
    }
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
    const auto finished=*transition_;current_=*incoming_;incoming_.reset();transition_.reset();
    std::optional<ModulePresentationChange> change;
    if(const auto following=state_.complete(finished.generation)){begin(*following,time);change=ModulePresentationChange{following->from,following->to,true};}
    auto result=snapshot(time);result.change=change;if(!state_.isTransitioning())attachCompletion(result);return result;
}
ModulePresentationUpdate ModulePresentation::sample(double time){
    advance(time);if(transition_&&time>=started_+ModuleTransitionStyle::duration)return finish(time);return snapshot(time);
}
ModulePresentationUpdate ModulePresentation::complete(std::uint64_t generation,double time){
    advance(time);if(transition_&&generation==state_.generation()&&time>=started_+ModuleTransitionStyle::duration)return finish(time);return snapshot(time);
}
ModulePresentationUpdate ModulePresentation::settleOn(Module module,double time){
    const auto previous=current_.module;state_.settle(module);
    if(current_.module!=module){if(incoming_&&incoming_->module==module)current_=*incoming_;else current_={module,++nextIdentity_};}
    incoming_.reset();transition_.reset();auto result=snapshot(time);result.change=ModulePresentationChange{previous,module,false};attachCompletion(result);return result;
}
ModulePresentationUpdate ModulePresentation::settle(double time){advance(time);return settleOn(state_.requested(),time);}
ModulePresentationUpdate ModulePresentation::cancel(double time){
    advance(time);state_.cancel();completion_.reset();incoming_.reset();transition_.reset();auto result=snapshot(time);result.change=ModulePresentationChange{current_.module,current_.module,false};return result;
}
} // namespace endfield::core
