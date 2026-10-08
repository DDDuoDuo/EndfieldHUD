#include "modules/notes_motion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
constexpr core::CubicTiming easeOut{0,0,.58,1},easeIn{.42,0,1,1},easeInOut{.42,0,.58,1};
double progress(double elapsed,double duration,bool reduce){
    if(!std::isfinite(elapsed))throw std::invalid_argument("Invalid Notes animation time");
    return reduce?1:std::clamp(elapsed/duration,0.,1.);
}
NotesMotionSample pulse(double elapsed,double duration,double middle,double peak,bool reduce){
    const double p=progress(elapsed,duration,reduce);
    const double y=p<middle?peak*easeOut.value(p/middle):peak*(1-easeInOut.value((p-middle)/(1-middle)));
    return {0,y,1,1,p<1};
}
}
NotesMotionSample notesSectionMotion(bool appearing,core::MotionPoint direction,double elapsed,bool reduce){
    if(!std::isfinite(direction.x)||!std::isfinite(direction.y))throw std::invalid_argument("Invalid Notes section direction");
    const double p=progress(elapsed,core::ModuleTransitionStyle::duration,reduce);
    const double t=core::ModuleTransitionStyle::timing.value(p),travel=appearing?18*(1-t):-14*t;
    return {direction.x*travel,direction.y*travel,1,appearing?t:1-t,p<1};
}
NotesMotionSample notesCardMotion(bool appearing,double elapsed,bool reduce){
    const double p=progress(elapsed,.2,reduce),t=(appearing?easeOut:easeIn).value(p);
    return {0,appearing?12*(1-t):-8*t,appearing?.94+.06*t:1-.06*t,appearing?t:1-t,p<1};
}
NotesMotionSample notesMutationMotion(double elapsed,bool reduce){return pulse(elapsed,.2,.32,-3,reduce);}
NotesMotionSample notesToolbarMotion(double elapsed,bool reduce){return pulse(elapsed,.18,.3,2,reduce);}
NotesMotionSample notesDeletionMenuMotion(double elapsed,bool reduce){const double p=progress(elapsed,.16,reduce);return {0,-6*(1-easeOut.value(p)),1,1,p<1};}
NotesMotionSample notesMenuMotion(bool appearing,double elapsed,double opacity,bool reduce){
    if(!std::isfinite(opacity)||opacity<0||opacity>1)throw std::invalid_argument("Invalid captured Notes menu opacity");
    const double p=progress(elapsed,appearing?.14:.12,reduce),t=(appearing?easeOut:easeIn).value(p);
    return {0,appearing?6*(1-t):4*t,1,appearing?t:opacity*(1-t),p<1};
}
} // namespace endfield::modules
