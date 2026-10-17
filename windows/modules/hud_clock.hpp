#pragma once
#include "core/source_desktop_chrome.hpp"
#include <functional>

namespace endfield::modules {
enum class HUDClockFormat {twentyFourHour,twelveHour};
struct LocalClockFields {unsigned month,day,weekday,hour,minute,second;}; // Sunday=0
// Source HUDClock formatting is Gregorian/en_US_POSIX, independent of UI
// language. The native owner supplies current local fields, including zone/DST.
core::source::DesktopClockReading formatHUDClock(LocalClockFields,HUDClockFormat);
class HUDClock final {
public:
    explicit HUDClock(std::function<LocalClockFields()> sample);
    bool setActive(bool,double monotonicTime);
    bool setFormat(HUDClockFormat,double monotonicTime);
    bool wake(double monotonicTime);
    bool active()const noexcept{return active_;}
    HUDClockFormat format()const noexcept{return format_;}
    const std::optional<core::source::DesktopClockReading>&reading()const noexcept{return reading_;}
    std::optional<double>nextDeadline()const noexcept{return deadline_;}
private:
    std::function<LocalClockFields()>sample_;
    std::optional<core::source::DesktopClockReading>reading_;
    std::optional<double>deadline_;
    HUDClockFormat format_{HUDClockFormat::twentyFourHour};bool active_{};
    bool refresh();
};
}
