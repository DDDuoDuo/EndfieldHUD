#include "modules/hud_clock.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace endfield::modules {
namespace {
void validTime(double time){if(!std::isfinite(time)||time<0||time>1e12)throw std::invalid_argument("Invalid HUD clock deadline");}
void validFormat(HUDClockFormat value){if(value!=HUDClockFormat::twentyFourHour&&value!=HUDClockFormat::twelveHour)throw std::invalid_argument("Unknown HUD clock format");}
}
core::source::DesktopClockReading formatHUDClock(LocalClockFields f,HUDClockFormat format){
    validFormat(format);
    if(f.month<1||f.month>12||f.day<1||f.day>31||f.weekday>6||f.hour>23||f.minute>59||f.second>60)throw std::invalid_argument("Invalid local clock fields");
    constexpr std::array days{"SUN","MON","TUE","WED","THU","FRI","SAT"};
    constexpr std::array months{"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    std::array<char,24>time{},date{};
    if(format==HUDClockFormat::twentyFourHour)std::snprintf(time.data(),time.size(),"%02u:%02u:%02u",f.hour,f.minute,f.second);
    else std::snprintf(time.data(),time.size(),"%02u:%02u:%02u %s",f.hour%12?f.hour%12:12,f.minute,f.second,f.hour<12?"AM":"PM");
    std::snprintf(date.data(),date.size(),"%s %s %u",days[f.weekday],months[f.month-1],f.day);
    return {time.data(),date.data()};
}
HUDClock::HUDClock(std::function<LocalClockFields()>sample):sample_(std::move(sample)){if(!sample_)throw std::invalid_argument("HUD clock requires a local-time provider");}
bool HUDClock::refresh(){auto next=formatHUDClock(sample_(),format_);if(reading_==next)return false;reading_=std::move(next);return true;}
bool HUDClock::setActive(bool value,double time){validTime(time);if(active_==value)return false;active_=value;deadline_.reset();if(!value)return false;deadline_=time+1;return refresh();}
bool HUDClock::setFormat(HUDClockFormat value,double time){validTime(time);validFormat(value);if(format_==value)return false;format_=value;return active_&&refresh();}
bool HUDClock::wake(double time){validTime(time);if(!active_||!deadline_||time<*deadline_)return false;
    // A delayed owner samples once, skipping missed ticks. Keep the original
    // repeating clock's phase without a catch-up loop or a private timer.
    const double next=*deadline_+std::floor(time-*deadline_)+1;
    deadline_=next>time?next:time+1;return refresh();
}
}
