#include "modules/work_mode.hpp"
#include <algorithm>
#include <cstdlib>
#include <locale.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace endfield::modules {
namespace {
void finite(double value){if(!std::isfinite(value))throw std::invalid_argument("Work Mode requires a finite continuous owner clock");}
bool blank(std::uint32_t c){return(c>=9&&c<=13)||c==32||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;}
std::string_view trim(std::string_view s){std::size_t first{},last{},at{};bool seen{};while(at<s.size()){const auto begin=at;const auto lead=static_cast<unsigned char>(s[at++]);std::uint32_t c=lead;unsigned n=1;if(lead>=128){n=lead>=0xc2&&lead<=0xdf?2:lead>=0xe0&&lead<=0xef?3:lead>=0xf0&&lead<=0xf4?4:0;if(!n||at+n-1>s.size())return s;for(unsigned k=1;k<n;++k){const auto byte=static_cast<unsigned char>(s[at++]);if((byte&0xc0)!=0x80)return s;c=(k==1?(lead&((1u<<(7-n))-1)):c);c=(c<<6)|(byte&63);}}if((n==2&&c<0x80)||(n==3&&c<0x800)||(n==4&&c<0x10000)||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return s;if(!blank(c)){if(!seen){first=begin;seen=true;}last=at;}}return seen?s.substr(first,last-first):std::string_view{};}
std::optional<double>number(std::string_view s){
    if(s.empty()||s.front()==' '||(s.front()>='\t'&&s.front()<='\r'))return{};
    // An immutable C numeric locale preserves Swift Double parsing, including
    // hexadecimal inputs, without mutating the process/user locale.
    struct Locale {
#ifdef _WIN32
        _locale_t value{_create_locale(LC_NUMERIC,"C")};~Locale(){if(value)_free_locale(value);}
#else
        locale_t value{newlocale(LC_NUMERIC_MASK,"C",nullptr)};~Locale(){if(value)freelocale(value);}
#endif
    };static const Locale locale;if(!locale.value)throw std::runtime_error("Cannot initialize duration numeric locale");
    std::string text(s);char*end{};
#ifdef _WIN32
    const auto value=_strtod_l(text.c_str(),&end,locale.value);
#else
    const auto value=strtod_l(text.c_str(),&end,locale.value);
#endif
    if(end!=text.c_str()+text.size()||!std::isfinite(value))return{};return value;
}
std::string format(std::int64_t seconds,bool edit){char text[96]{};
    // Original String(format:) uses %d: preserve its signed32 vararg product
    // even at the deliberately saturated Int.max/2 stopwatch boundary.
    if(edit)std::snprintf(text,sizeof(text),"%d:%02d",static_cast<int>(seconds/60),static_cast<int>(seconds%60));
    else if(seconds>=3600)std::snprintf(text,sizeof(text),"%d:%02d:%02d",static_cast<int>(seconds/3600),static_cast<int>(seconds/60%60),static_cast<int>(seconds%60));
    else std::snprintf(text,sizeof(text),"%02d:%02d",static_cast<int>(seconds/60),static_cast<int>(seconds%60));return text;
}
double total(double a,double b){const auto sum=a+b;return std::isfinite(sum)?sum:std::numeric_limits<double>::max();}
}
std::string_view workModeKindKey(WorkModeKind v){switch(v){case WorkModeKind::countdown:return "countdown";case WorkModeKind::stopwatch:return "stopwatch";}throw std::invalid_argument("Invalid Work Mode kind");}
std::string_view workModePhaseKey(WorkModePhase v){switch(v){case WorkModePhase::idle:return "idle";case WorkModePhase::running:return "running";case WorkModePhase::paused:return "paused";case WorkModePhase::stopped:return "stopped";case WorkModePhase::completed:return "completed";}throw std::invalid_argument("Invalid Work Mode phase");}
double WorkModeSnapshot::remaining()const{return kind==WorkModeKind::countdown?std::max(0.,duration-elapsed):0;}
double WorkModeSnapshot::progress()const{return kind==WorkModeKind::countdown?std::clamp(elapsed/duration,0.,1.):std::fmod(elapsed,60.)/60;}
bool WorkModeSnapshot::active()const noexcept{return phase==WorkModePhase::running||phase==WorkModePhase::paused;}
std::int64_t WorkModeSnapshot::displayedSeconds()const{const auto value=kind==WorkModeKind::countdown?std::ceil(remaining()-.000001):std::floor(elapsed+.000001);return static_cast<std::int64_t>(std::min(static_cast<double>(std::numeric_limits<std::int64_t>::max()/2),std::max(0.,value)));}
std::string WorkModeSnapshot::timeText()const{return format(displayedSeconds(),false);}
std::optional<double>parseWorkModeDuration(std::string_view input){const auto text=trim(input);const auto colon=text.find(':');double seconds{};
    if(colon!=std::string_view::npos){if(text.find(':',colon+1)!=std::string_view::npos)return{};const auto minutes=number(text.substr(0,colon)),remainder=number(text.substr(colon+1));if(!minutes||!remainder||*minutes<0||std::round(*minutes)!=*minutes||*remainder<0||*remainder>=60||std::round(*remainder)!=*remainder)return{};seconds=*minutes*60+*remainder;}
    else{std::string normalized(text);std::replace(normalized.begin(),normalized.end(),',','.');const auto minutes=number(normalized);if(!minutes)return{};seconds=*minutes*60;}
    if(!std::isfinite(seconds)||seconds<1||seconds>86400)return{};return std::round(seconds);
}
std::string workModeDurationEditText(double seconds){finite(seconds);return format(static_cast<std::int64_t>(std::clamp(std::round(seconds),1.,86400.)),true);}
WorkModeController::WorkModeController(WorkModeHooks hooks):hooks_(std::move(hooks)){}
WorkModeSnapshot WorkModeController::snapshot(double now)const{finite(now);const auto interval=started_?std::max(0.,now-*started_):0;const auto elapsed=std::max(0.,accumulated_+(std::isfinite(interval)?interval:0));return{kind_,phase_,duration_,kind_==WorkModeKind::countdown?std::min(duration_,elapsed):elapsed};}
double WorkModeController::trackedWorkSeconds(double now)const{return total(trackedTotal_,std::max(0.,snapshot(now).elapsed-trackedSessionElapsed_));}
bool WorkModeController::restoreTrackedWorkSeconds(double value){if(restored_||phase_!=WorkModePhase::idle||accumulated_!=0||trackedTotal_!=0||!std::isfinite(value)||value<0)return false;trackedTotal_=value;restored_=true;return true;}
void WorkModeController::checkpoint(double now){const auto elapsed=snapshot(now).elapsed;trackedTotal_=total(trackedTotal_,std::max(0.,elapsed-trackedSessionElapsed_));trackedSessionElapsed_=elapsed;if(hooks_.trackedWorkSecondsChanged)hooks_.trackedWorkSecondsChanged(trackedTotal_);}
void WorkModeController::publish(double now,bool force){const auto second=snapshot(now).displayedSeconds();if(!force&&publishedSecond_==second)return;publishedSecond_=second;++notificationRevision_;if(hooks_.changed)hooks_.changed();}
void WorkModeController::reconcile(double now){if(phase_!=WorkModePhase::running||suspended_){display_.reset();completion_.reset();return;}if(visible_){if(!display_)display_=now+1;}else display_.reset();if(kind_==WorkModeKind::countdown){if(!completion_)completion_=now+std::max(.001,snapshot(now).remaining());}else completion_.reset();}
void WorkModeController::changed(double now){++revision_;display_.reset();completion_.reset();reconcile(now);publish(now,true);}
bool WorkModeController::chooseCountdown(double seconds,double now){finite(now);if(!std::isfinite(seconds)||seconds<1||seconds>86400)return false;checkpoint(now);kind_=WorkModeKind::countdown;duration_=std::round(seconds);accumulated_=0;started_.reset();phase_=WorkModePhase::idle;trackedSessionElapsed_=0;changed(now);return true;}
void WorkModeController::chooseStopwatch(double now){finite(now);checkpoint(now);kind_=WorkModeKind::stopwatch;accumulated_=0;started_.reset();phase_=WorkModePhase::idle;trackedSessionElapsed_=0;changed(now);}
void WorkModeController::start(double now){finite(now);if(phase_==WorkModePhase::running)return;if(phase_==WorkModePhase::paused){resume(now);return;}checkpoint(now);accumulated_=0;started_=now;phase_=WorkModePhase::running;trackedSessionElapsed_=0;changed(now);}
void WorkModeController::pause(double now){finite(now);if(phase_!=WorkModePhase::running)return;refresh(now);if(phase_!=WorkModePhase::running)return;checkpoint(now);accumulated_=snapshot(now).elapsed;started_.reset();phase_=WorkModePhase::paused;changed(now);}
void WorkModeController::resume(double now){finite(now);if(phase_!=WorkModePhase::paused)return;started_=now;phase_=WorkModePhase::running;changed(now);}
void WorkModeController::stop(double now){finite(now);if(phase_!=WorkModePhase::running&&phase_!=WorkModePhase::paused)return;checkpoint(now);accumulated_=snapshot(now).elapsed;started_.reset();phase_=WorkModePhase::stopped;changed(now);}
void WorkModeController::reset(double now){finite(now);checkpoint(now);accumulated_=0;started_.reset();phase_=WorkModePhase::idle;trackedSessionElapsed_=0;changed(now);}
void WorkModeController::setVisible(bool value,double now){finite(now);if(visible_==value)return;visible_=value;refresh(now);if(!value)checkpoint(now);reconcile(now);if(value)publish(now,true);}
void WorkModeController::setSuspended(bool value,double now){finite(now);if(suspended_==value)return;if(value)checkpoint(now);suspended_=value;++revision_;if(!value)refresh(now);reconcile(now);publish(now,true);}
void WorkModeController::refresh(double now){finite(now);if(phase_==WorkModePhase::running&&kind_==WorkModeKind::countdown&&snapshot(now).remaining()<=.000001){checkpoint(now);accumulated_=duration_;started_.reset();phase_=WorkModePhase::completed;changed(now);return;}reconcile(now);if(visible_&&!suspended_)publish(now,false);}
void WorkModeController::wake(double now){finite(now);const bool display=display_&&now>=*display_,completion=completion_&&now>=*completion_;if(display){const auto missed=std::floor(now-*display_)+1;*display_+=missed;}if(completion)completion_.reset();if(display||completion)refresh(now);}
void WorkModeController::shutdown(double now){finite(now);checkpoint(now);accumulated_=snapshot(now).elapsed;started_.reset();if(phase_==WorkModePhase::running)phase_=WorkModePhase::stopped;visible_=false;suspended_=true;display_.reset();completion_.reset();}
std::optional<double>WorkModeController::nextDeadline()const noexcept{if(display_&&completion_)return std::min(*display_,*completion_);return display_?display_:completion_;}
}
