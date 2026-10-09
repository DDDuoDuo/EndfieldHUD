#pragma once
#include "modules/calendar_model.hpp"
#include <array>
#include <memory>
namespace endfield::native {
// Installed ICU, same explicit Unicode-version compatibility gate as the other
// migrated editors. Called only on event/load/commit, never on pointer frames.
modules::CalendarTextRules nativeCalendarTextRules();
std::array<std::uint8_t,4>calendarUnicodeVersion()noexcept;
// Retained date labels plus immutable, independently callable civil conversion
// callbacks. Explicit IANA zone/locale injection supports isolated fixtures.
// Empty zone/calendarLocale resolve current OS state only at construction or
// refresh; owner forwards WM_TIMECHANGE/setting/wake rather than polling.
class CalendarCivilContext final {
public:CalendarCivilContext(std::u16string zoneID={},std::string calendarLocale={},std::string displayLocale="en_US");~CalendarCivilContext();
    CalendarCivilContext(const CalendarCivilContext&)=delete;CalendarCivilContext&operator=(const CalendarCivilContext&)=delete;
    modules::CalendarTimeZone timeZone()const;unsigned firstWeekday()const noexcept;
    std::string monthHeading(modules::CalendarDay)const;std::array<std::string,7>weekdays()const;
    bool setDisplayLocale(std::string);bool refreshSystemContext();
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
