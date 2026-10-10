#include "modules/work_mode_profile_hours.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::modules {
double WorkModeProfileHours::absolute(double total)const noexcept{
    const double value=offset_+total;
    // Same saturation as the controller's absolute lifetime total.
    return std::isfinite(value)?value:std::numeric_limits<double>::max();
}
void WorkModeProfileHours::deliver(double total){
    if(persist_&&persist_(total))unsaved_.reset();else unsaved_=total;
}
void WorkModeProfileHours::checkpoint(double controllerTotal){
    if(!std::isfinite(controllerTotal)||controllerTotal<0)throw std::invalid_argument("Work Mode checkpoints are finite absolute totals");
    if(!loaded_){early_=controllerTotal;unsaved_=controllerTotal;return;}
    deliver(absolute(controllerTotal));
}
void WorkModeProfileHours::profileLoaded(WorkModeController&controller,double stored,Persist persist){
    load(stored,std::move(persist),&controller);
}
void WorkModeProfileHours::profileLoaded(double stored,Persist persist){load(stored,std::move(persist),nullptr);}
void WorkModeProfileHours::load(double stored,Persist persist,WorkModeController*controller){
    if(loaded_)throw std::logic_error("The personal profile lifetime is restored once");
    if(!std::isfinite(stored)||stored<0)throw std::invalid_argument("Stored Work Mode lifetime must be finite and nonnegative");
    loaded_=true;persist_=std::move(persist);
    restored_=controller&&controller->restoreTrackedWorkSeconds(stored);
    // A successful restore proves no time was tracked before loading (the
    // controller requires an idle zero total), so early zero checkpoints add
    // nothing. Otherwise the earlier sessions are new time above the record.
    if(!restored_)offset_=stored;
    const auto early=early_;early_.reset();unsaved_.reset();
    if(early&&!restored_)deliver(absolute(*early));
}
bool WorkModeProfileHours::retry(){
    if(!loaded_)return false;
    if(unsaved_)deliver(*unsaved_);
    return !unsaved_.has_value();
}
double WorkModeProfileHours::totalSeconds(const WorkModeController&controller,double now)const{return absolute(controller.trackedWorkSeconds(now));}
}
