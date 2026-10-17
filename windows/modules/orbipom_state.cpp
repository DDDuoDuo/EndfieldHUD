#include "modules/orbipom_state.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
bool finite(core::Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
bool contains(core::Rect r,core::Point p){return finite(p)&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
std::optional<OrbiPomSkill>skill(OrbiPomAction a){switch(a){case OrbiPomAction::clear:return OrbiPomSkill::clear;case OrbiPomAction::wind:return OrbiPomSkill::wind;case OrbiPomAction::shake:return OrbiPomSkill::shake;case OrbiPomAction::swap:return OrbiPomSkill::swap;default:return {};}}
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
void OrbiPomSession::saveBest(){const auto next=std::max({best_,snapshot().score,snapshot().highScore});if(next==best_)return;best_=next;if(callbacks_.saveBest)callbacks_.saveBest(best_);}
void OrbiPomSession::move(core::Point p){if(runtime_&&finite(p)&&snapshot().canAdvance())runtime_->move(p);}
void OrbiPomSession::pointerUp(core::Point p){if(runtime_&&finite(p)&&snapshot().canAdvance())runtime_->pointerUp(p);}
bool OrbiPomSession::drop(){return runtime_&&runtime_->drop();}
bool OrbiPomSession::activate(OrbiPomSkill s){return runtime_&&runtime_->activate(s);}
void OrbiPomSession::cancelSkill(){if(runtime_)runtime_->cancelSkill();}
core::Point orbiPomWorld(core::Point p)noexcept{return {(p.x-orbiPomBoard.x)/1.1,(p.y-orbiPomBoard.y)/1.1};}
OrbiPomPlacement orbiPomPlacement(const OrbiPomBody&b)noexcept{return {{orbiPomBoard.x+b.x*1.1,orbiPomBoard.y+b.y*1.1},b.size*b.scale*1.1,b.angle,b.opacity};}
OrbiPomState::OrbiPomState(OrbiPomSession&s):session_(s){}
OrbiPomState::~OrbiPomState(){try{session_.pause(true);}catch(...){/* Owner persistence callbacks must not throw during retirement. */}}
bool OrbiPomState::shouldRun()const noexcept{return presented_&&active_&&foreground_&&!session_.manuallyPaused()&&!restart_&&!rules_&&!session_.error();}
bool OrbiPomState::requiresFrames()const noexcept{return shouldRun()&&session_.snapshot().isPlaying();}
bool OrbiPomState::boardDimmed()const noexcept{return session_.manuallyPaused()||restart_||session_.snapshot().state=="over"||bool(session_.error());}
void OrbiPomState::reconcile(){const auto run=shouldRun();if(session_.hasRuntime()&&session_.snapshot().paused==run)session_.pause(!run);}
void OrbiPomState::setPresented(bool value){if(presented_==value)return;presented_=value;if(!value){active_=false;pointerDown_=false;restart_=false;rules_=false;}reconcile();}
void OrbiPomState::setActive(bool value){if(active_==value)return;active_=value;if(value)presented_=true;else{pointerDown_=false;rules_=false;}reconcile();}
void OrbiPomState::setForeground(bool value){if(foreground_==value)return;foreground_=value;reconcile();}
void OrbiPomState::setRulesPresented(bool value){if(rules_==value)return;rules_=value;pointerDown_=false;reconcile();}
OrbiPomActions OrbiPomState::actions()const noexcept{
    OrbiPomActions out;const auto&s=session_.snapshot();
    const auto add=[&](OrbiPomAction action,core::Rect rect,bool enabled=true){out.items[out.count++]={action,rect,enabled};};
    if(restart_){add(OrbiPomAction::cancelRestart,{160,256,48,32});add(OrbiPomAction::confirmRestart,{224,256,48,32});}
    else if(!s.isPlaying())add(OrbiPomAction::start,{150,263,140,33},!session_.error()||!session_.hasRuntime());
    else{add(OrbiPomAction::pause,{352,104,33,30});add(OrbiPomAction::restart,{393,104,33,30});}
    constexpr std::array<OrbiPomAction,4>actions{OrbiPomAction::clear,OrbiPomAction::wind,OrbiPomAction::shake,OrbiPomAction::swap};
    for(std::size_t i=0;i<actions.size();++i)add(actions[i],{12,132+double(i)*65,61,53},!restart_&&s.canUse(*skill(actions[i])));
    if(s.skill&&!restart_)add(OrbiPomAction::cancelSkill,{358,161,54,30});
    add(OrbiPomAction::rules,{392,399,34,30},!restart_);return out;
}
bool OrbiPomState::perform(OrbiPomAction action){
    if(!active_)return false;
    switch(action){
    case OrbiPomAction::start:session_.start();reconcile();break;
    case OrbiPomAction::pause:session_.setManuallyPaused(!session_.manuallyPaused());reconcile();break;
    case OrbiPomAction::rules:setRulesPresented(!rules_);break;
    case OrbiPomAction::restart:restart_=true;reconcile();break;
    case OrbiPomAction::cancelRestart:restart_=false;reconcile();break;
    case OrbiPomAction::confirmRestart:restart_=false;session_.start();reconcile();break;
    case OrbiPomAction::cancelSkill:session_.cancelSkill();break;
    default:if(const auto value=skill(action);value&&session_.snapshot().canUse(*value))session_.activate(*value);break;
    }return true;
}
void OrbiPomState::advance(double seconds){if(!presented_||!active_||!foreground_||session_.manuallyPaused()||restart_||rules_)return;session_.advance(seconds);if(!session_.snapshot().canAdvance()||session_.error())reconcile();}
void OrbiPomState::move(core::Point p){if(!active_||restart_||rules_||!contains(orbiPomPlayArea,p))return;keyboard_=orbiPomWorld(p);session_.move(keyboard_);}
bool OrbiPomState::down(core::Point p){
    if(!active_||!contains({0,0,440,440},p))return false;
    // The retained Rules menu owns and consumes its complete input plane.
    if(rules_)return true;
    const auto current=actions();for(auto i=current.count;i>0;--i){const auto&a=current.items[i-1];if(contains(a.rect,p)){if(a.enabled)perform(a.action);return true;}}
    if(!restart_&&contains(orbiPomPlayArea,p)){pointerDown_=true;move(p);}return true;
}
void OrbiPomState::up(std::optional<core::Point>p){const auto held=pointerDown_;pointerDown_=false;if(!active_||!held||!p||!contains(orbiPomPlayArea,*p)||restart_||rules_)return;session_.pointerUp(orbiPomWorld(*p));}
bool OrbiPomState::key(OrbiPomKey k,bool repeated,bool modified){
    if(!active_||rules_||modified)return false;
    if(k==OrbiPomKey::escape){if(restart_){perform(OrbiPomAction::cancelRestart);return true;}if(session_.snapshot().skill){perform(OrbiPomAction::cancelSkill);return true;}return false;}
    if(k==OrbiPomKey::space){if(repeated)return true;if(session_.snapshot().state=="idle")perform(OrbiPomAction::start);else if(session_.snapshot().skill)session_.pointerUp(keyboard_);else session_.drop();return true;}
    if(k==OrbiPomKey::left||k==OrbiPomKey::right||k==OrbiPomKey::down||k==OrbiPomKey::up){
        if(!session_.snapshot().skill)keyboard_.x=session_.snapshot().previewX;
        if(k==OrbiPomKey::left)keyboard_.x-=6;if(k==OrbiPomKey::right)keyboard_.x+=6;if(k==OrbiPomKey::down)keyboard_.y+=6;if(k==OrbiPomKey::up)keyboard_.y-=6;
        keyboard_.x=std::clamp(keyboard_.x,0.,230.);keyboard_.y=std::clamp(keyboard_.y,0.,280.);session_.move(keyboard_);return true;
    }
    OrbiPomAction action{};switch(k){case OrbiPomKey::pause:action=OrbiPomAction::pause;break;case OrbiPomKey::clear:action=OrbiPomAction::clear;break;case OrbiPomKey::wind:action=OrbiPomAction::wind;break;case OrbiPomKey::shake:action=OrbiPomAction::shake;break;case OrbiPomKey::swap:action=OrbiPomAction::swap;break;default:return false;}
    if(!repeated)perform(action);return true;
}
}
