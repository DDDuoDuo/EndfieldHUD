#pragma once
#include "modules/calendar_model.hpp"
#include <array>
#include <memory>
namespace endfield::native {
// Installed ICU, same explicit Unicode-version compatibility gate as the other
// migrated editors. Called only on event/load/commit, never on pointer frames.
modules::CalendarTextRules nativeCalendarTextRules();
std::array<std::uint8_t,4>calendarUnicodeVersion()noexcept;
// Immutable, independently callable civil conversion callbacks and the
// Calendar canvas's date formats. Explicit IANA zone/locale injection supports
// isolated fixtures. Empty zone/calendarLocale resolve current OS state only at
// construction or refresh; owner forwards WM_TIMECHANGE/setting/wake rather
// than polling.
class CalendarCivilContext final {
public:CalendarCivilContext(std::u16string zoneID={},std::string calendarLocale={});~CalendarCivilContext();
    CalendarCivilContext(const CalendarCivilContext&)=delete;CalendarCivilContext&operator=(const CalendarCivilContext&)=delete;
    modules::CalendarTimeZone timeZone()const;unsigned firstWeekday()const noexcept;
    // HUDCalendarCanvas.formatter (:106-121): DateFormatter.setLocalizedDateFormatFromTemplate
    // (ICU's best pattern for the skeleton) in the UI language's locale (en_US,
    // zh_CN, zh_TW, ja_JP, ko_KR) on the Gregorian calendar in this zone, the
    // day at 12:00 as the Mac's HUDCalendarDay.date(in:). Days past the stored
    // range (the grid's 1899-12-31, 10000-01-01) format too. Formatters are
    // kept per language and skeleton; a zone refresh drops them.
    std::string format(modules::CalendarDay,std::string_view skeleton,core::Language)const;
    std::array<std::string,7>shortWeekdays(core::Language)const;   // shortStandaloneWeekdaySymbols, Sunday first
    std::array<std::string,12>shortMonths(core::Language)const;    // shortStandaloneMonthSymbols
    // The canvas's formats bound to this context (it must outlive the canvas).
    modules::CalendarFormats formats()const;
    bool refreshSystemContext();
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
