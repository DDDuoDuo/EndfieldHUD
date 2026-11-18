#pragma once
#include <cstdint>
#include <memory>

namespace endfield::app {
// Physical work-area coordinates; supports monitors left/above the primary one.
inline bool desktopCornerContains(int x,int y,int left,int top,int right,int bottom) noexcept {
    return x>=left && x<right && x-left<320 && y>=top && y<bottom && bottom-y<=240;
}
class DesktopCornerGesture {
public:
    enum class Event { down, move, up, cancel };
    bool event(Event event,int x,int y,std::uint32_t time,bool eligible,
               std::uint32_t interval,int toleranceX,int toleranceY) noexcept {
        const auto near=[&](int a,int b,int tolerance){const auto delta=std::int64_t(a)-b;return delta>=-tolerance && delta<=tolerance;};
        if(event==Event::cancel){pressed_=first_=false;return false;}
        if(event==Event::down){pressed_=eligible;dragged_=false;x_=x;y_=y;time_=time;if(!eligible)first_=false;return false;}
        if(event==Event::move){if(pressed_ && (!near(x,x_,toleranceX)||!near(y,y_,toleranceY)))dragged_=true;return false;}
        const bool click=pressed_ && !dragged_ && eligible && near(x,x_,toleranceX) && near(y,y_,toleranceY);
        pressed_=false;
        if(!click){first_=false;return false;}
        const bool doubleClick=first_ && std::uint32_t(time_-firstTime_)<=interval && near(x_,firstX_,toleranceX) && near(y_,firstY_,toleranceY);
        first_=!doubleClick;firstX_=x_;firstY_=y_;firstTime_=time_;return doubleClick;
    }
private:
    bool pressed_{},dragged_{},first_{};int x_{},y_{},firstX_{},firstY_{};std::uint32_t time_{},firstTime_{};
};
// Owns an idle message-loop thread. Mouse input is always passed through.
class DesktopCornerSummon {
public:
    DesktopCornerSummon(void* window,unsigned message);
    ~DesktopCornerSummon();
    DesktopCornerSummon(const DesktopCornerSummon&)=delete;
    DesktopCornerSummon& operator=(const DesktopCornerSummon&)=delete;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
