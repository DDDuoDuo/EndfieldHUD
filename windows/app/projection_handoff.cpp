#include "app/projection_handoff.hpp"

namespace endfield::app {
ProjectionHandoff::Command ProjectionHandoff::command(Action action)const noexcept {
    return {action,generation_,context_,action==Action::external?external_:std::nullopt};
}
std::optional<ProjectionHandoff::Command>ProjectionHandoff::finish(Action action){
    const auto result=command(action);phase_=Phase::inactive;context_={};external_.reset();return result;
}
std::optional<ProjectionHandoff::Command>ProjectionHandoff::open(OpeningConditions gate,Context context){
    if(active()||!gate.hudOpen||gate.quitting||gate.closeAlreadyPending||gate.shelfDragging)return {};
    ++generation_;context_=context;external_.reset();phase_=Phase::closingHUD;return command(Action::closeHUD);
}
std::optional<ProjectionHandoff::Command>ProjectionHandoff::hudClosed(std::uint64_t token,bool displayAvailable){
    if(token!=generation_||phase_!=Phase::closingHUD)return {};
    if(!displayAvailable){cancel();return {};}
    if(external_)return finish(Action::external);
    phase_=Phase::projection;return command(Action::showProjection);
}
std::optional<ProjectionHandoff::Command>ProjectionHandoff::returnToHUD(){
    if(phase_!=Phase::projection)return {};
    phase_=Phase::closingProjection;return command(Action::closeProjection);
}
std::optional<ProjectionHandoff::Command>ProjectionHandoff::projectionClosed(std::uint64_t token){
    if(token!=generation_||phase_!=Phase::closingProjection)return {};
    return finish(external_?Action::external:Action::showHUD);
}
std::optional<ProjectionHandoff::Command>ProjectionHandoff::external(std::uint64_t actionID){
    if(!active())return {};
    external_=actionID;return returnToHUD();
}
std::optional<ProjectionHandoff::Command>ProjectionHandoff::cancel(){
    const bool release=phase_==Phase::projection||phase_==Phase::closingProjection;
    ++generation_;
    const auto result=release?std::optional{command(Action::closeImmediately)}:std::nullopt;
    phase_=Phase::inactive;context_={};external_.reset();return result;
}
}
