#include "modules/calendar_localization.hpp"

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
std::string calendarReminderStatus(CalendarPermission permission,core::Language language){
    switch(permission){
    case CalendarPermission::authorized:return core::localized("Reminders: 09:00, the day before and on the date","提醒：前一天及当天 09:00",language);
    case CalendarPermission::denied:return calendarNotificationsOff(language);
    case CalendarPermission::unknown:return core::localized("Allow notifications when adding your first event","添加首个事项时允许通知",language);
    case CalendarPermission::unavailable:return core::localized("Notifications are unavailable","通知暂不可用",language);
    }
    return {};
}
std::string calendarErrorMessage(CalendarErrorCode code,core::Language language){
    const auto text=calendarErrorText(code);return core::localized(text.english,text.simplified,language);
}
}
