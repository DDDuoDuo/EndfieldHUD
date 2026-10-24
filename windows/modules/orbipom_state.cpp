#include "modules/orbipom_state.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
bool finite(core::Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
}
int orbiPomEnergyCost(OrbiPomSkill s)noexcept{switch(s){case OrbiPomSkill::clear:return 1;case OrbiPomSkill::wind:return 2;case OrbiPomSkill::shake:return 3;case OrbiPomSkill::swap:return 0;}return 0;}
bool OrbiPomSnapshot::canUse(OrbiPomSkill s)const noexcept{return canAdvance()&&skillPhase!="casting"&&(s==OrbiPomSkill::swap?swapCharge>=6:energy>=orbiPomEnergyCost(s));}
OrbiPomSession::OrbiPomSession(std::int64_t best,OrbiPomSessionCallbacks callbacks):callbacks_(std::move(callbacks)),best_(std::max<std::int64_t>(0,best)){}
const OrbiPomSnapshot&OrbiPomSession::snapshot()const noexcept{return runtime_?runtime_->snapshot():idle_;}
bool OrbiPomSession::start(std::optional<std::uint32_t>seed){
    const bool restarting=bool(runtime_);
    try{
        if(!runtime_){if(!callbacks_.makeRuntime)throw std::runtime_error("Original OrbiPom simulation runtime is unavailable.");runtime_=callbacks_.makeRuntime();if(!runtime_)throw std::runtime_error("Original OrbiPom simulation runtime is unavailable.");}
        error_.reset();recordedFinish_=false;manual_=false;runtime_->highScore(best_);runtime_->start(seed);error_=runtime_->error();
    }catch(const std::exception&e){error_=e.what();runtime_.reset();return false;}
    if(!error_&&callbacks_.event)callbacks_.event(restarting?OrbiPomEvent::restarted:OrbiPomEvent::started);
    return !error_;
}
void OrbiPomSession::advance(double seconds){
    if(!runtime_)return;
    const bool wasPlaying=snapshot().isPlaying();
    if(snapshot().canAdvance()&&std::isfinite(seconds)&&seconds>0&&!runtime_->error())runtime_->advance(seconds);
    error_=runtime_->error();
    if(wasPlaying&&!snapshot().isPlaying()&&!recordedFinish_){recordedFinish_=true;saveBest();if(callbacks_.event)callbacks_.event(OrbiPomEvent::finished);}
}
void OrbiPomSession::setManuallyPaused(bool value){manual_=value;if(value)pause(true);}
void OrbiPomSession::pause(bool value){const bool effective=value||manual_;if(runtime_)runtime_->pause(effective);if(effective)saveBest();}
std::int64_t OrbiPomSession::bestSoFar()const noexcept{return std::max({best_,snapshot().score,snapshot().highScore});}
void OrbiPomSession::saveBest(){const auto next=bestSoFar();if(next==best_)return;best_=next;if(callbacks_.saveBest)callbacks_.saveBest(best_);}
void OrbiPomSession::move(core::Point p){if(runtime_&&finite(p)&&snapshot().canAdvance())runtime_->move(p);}
void OrbiPomSession::pointerUp(core::Point p){if(runtime_&&finite(p)&&snapshot().canAdvance())runtime_->pointerUp(p);}
bool OrbiPomSession::drop(){return runtime_&&runtime_->drop();}
bool OrbiPomSession::activate(OrbiPomSkill s){return runtime_&&runtime_->activate(s);}
void OrbiPomSession::cancelSkill(){if(runtime_)runtime_->cancelSkill();}
}
