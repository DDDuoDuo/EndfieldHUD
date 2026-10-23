#pragma once
#include "modules/calendar_state.hpp"
#include "core/localization.hpp"
#include <string>

namespace endfield::modules {
// Windows wording for the Mac reminderStatus "Notifications are off in System
// Settings" (HUDCalendarController.swift): Windows has no System Settings; the
// per-app switch lives in Windows Settings > System > Notifications. Every
// other Calendar caption and error keeps its Mac source pair.
inline constexpr core::TranslationEntry calendarWindowsNotificationsOff{
    "Notifications are off in Windows Settings","通知已在 Windows 设置中关闭","通知已在 Windows 設定中關閉",
    "Windows の設定で通知が無効になっています","Windows 설정에서 알림이 꺼져 있습니다"};
std::string calendarNotificationsOff(core::Language);
// HUDCalendarController.reminderStatus in the current language (the Calendar
// Reminders toast's caption when reminders are off, MODS-660).
std::string calendarReminderStatus(CalendarPermission,core::Language);
// HUDCalendarError.errorDescription for a typed CalendarState error, in the
// current language. Owners call this at display time, so a language switch
// shows a visible error in the new language.
std::string calendarErrorMessage(CalendarErrorCode,core::Language);
}
