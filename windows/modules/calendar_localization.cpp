#include "modules/calendar_localization.hpp"
#include <array>

namespace endfield::modules {
std::string calendarNotificationsOff(core::Language language){
    const auto&e=calendarWindowsNotificationsOff;
    switch(language){
    case core::Language::simplifiedChinese:return std::string(e.simplifiedChinese);
    case core::Language::traditionalChinese:return std::string(e.traditionalChinese);
    case core::Language::japanese:return std::string(e.japanese);
    case core::Language::korean:return std::string(e.korean);
    default:return std::string(e.english);
    }
}
CalendarStrings calendarStrings(core::Language language){
    CalendarStrings s;const auto cn=CalendarStrings::simplifiedChinese();
#define CALENDAR_STRING(n) s.n=core::localized(s.n,cn.n,language)
    CALENDAR_STRING(calendar);CALENDAR_STRING(today);CALENDAR_STRING(noEvents);CALENDAR_STRING(addEvent);CALENDAR_STRING(editEvent);
    CALENDAR_STRING(title);CALENDAR_STRING(date);CALENDAR_STRING(details);CALENDAR_STRING(save);CALENDAR_STRING(cancel);
    CALENDAR_STRING(erase);CALENDAR_STRING(deleteQuestion);CALENDAR_STRING(previousMonth);CALENDAR_STRING(nextMonth);
    CALENDAR_STRING(refreshReminders);CALENDAR_STRING(unavailable);
#undef CALENDAR_STRING
    s.denied=calendarNotificationsOff(language);
    return s;
}
std::string calendarErrorMessage(CalendarErrorCode code,core::Language language){
    const auto text=calendarErrorText(code);return core::localized(text.english,text.simplified,language);
}
std::vector<CalendarAccessible>calendarCanvasAccessibility(std::span<const CalendarAction>actions,const CalendarStrings&strings){
    std::vector<CalendarAccessible>out;out.reserve(actions.size());
    for(const auto&a:actions){std::string label;
        if(a.id=="new")label=strings.addEvent;else if(a.id=="previousMonth")label=strings.previousMonth;else if(a.id=="nextMonth")label=strings.nextMonth;else if(a.id=="reminders")label=strings.refreshReminders;
        else label=a.id.starts_with("day:")?a.id.substr(4):a.label;
        out.push_back({a.id,std::move(label),a.rect,a.enabled,CalendarAccessibleRole::button});}
    return out;
}
std::vector<CalendarAccessible>calendarMenuAccessibility(std::span<const CalendarAction>actions,const CalendarStrings&strings,core::Language language){
    // Source CalendarEvent/Notes menu: field rectangles are fixed menu-local.
    constexpr std::array<core::Rect,3>fields{{{14,49,312,30},{14,99,312,28},{14,149,312,86}}};
    std::vector<CalendarAccessible>out;out.reserve(actions.size()+fields.size());
    for(const auto&a:actions){std::string label;
        if(a.id=="close")label=core::localized("Close","关闭",language);else if(a.id=="save")label=strings.save;
        else if(a.id=="delete"||a.id=="confirmDelete")label=strings.erase;else if(a.id=="cancelDelete")label=strings.cancel;else label=a.label;
        out.push_back({a.id,std::move(label),a.rect,a.enabled,CalendarAccessibleRole::button});}
    const std::array<std::pair<const char*,const std::string*>,3>names{{{"field:title",&strings.title},{"field:date",&strings.date},{"field:details",&strings.details}}};
    for(std::size_t n=0;n<fields.size();++n)out.push_back({names[n].first,*names[n].second,fields[n],true,CalendarAccessibleRole::textField});
    return out;
}
}
