#pragma once
#include "modules/calendar_presentation.hpp"
#include "core/localization.hpp"
#include <span>
#include <string>
#include <vector>

namespace endfield::modules {
// Windows wording for the Mac reminderStatus "Notifications are off in System
// Settings" (HUDCalendarController.swift): Windows has no System Settings; the
// per-app switch lives in Windows Settings > System > Notifications. Every
// other Calendar caption and error keeps its Mac source pair.
inline constexpr core::TranslationEntry calendarWindowsNotificationsOff{
    "Notifications are off in Windows Settings","通知已在 Windows 设置中关闭","通知已在 Windows 設定中關閉",
    "Windows の設定で通知が無効になっています","Windows 설정에서 알림이 꺼져 있습니다"};
std::string calendarNotificationsOff(core::Language);
// Calendar captions for one UI language. Mac pairs resolve through the shared
// five-language catalog (core::localized); System renders English there too,
// so the app resolves System once at its preference boundary.
CalendarStrings calendarStrings(core::Language);
// HUDCalendarError.errorDescription for a typed CalendarState error, in the
// current language. Owners call this at display time, so a language switch
// re-renders a visible error immediately.
std::string calendarErrorMessage(CalendarErrorCode,core::Language);

// Source accessibility contract for a native UI Automation provider
// (HUDCalendarInteraction.layoutAccessibility, CalendarEventMenu items through
// NotesRetainedMenu, the three CalendarFieldTextView editors). Rects are the
// source canvas (400x440) or menu-local (340x310) points; the host projects
// them exactly like pointer hit tests. Content-event only, never per frame.
enum class CalendarAccessibleRole {button,textField};
struct CalendarAccessible {
    std::string id,label;core::Rect rect;bool enabled{};CalendarAccessibleRole role{CalendarAccessibleRole::button};
    bool operator==(const CalendarAccessible&)const=default;
};
// One AX button per canvas action: new/previousMonth/nextMonth/reminders use
// their source names, day:YYYY-MM-DD reads the date, others the action label.
std::vector<CalendarAccessible>calendarCanvasAccessibility(std::span<const CalendarAction>,const CalendarStrings&);
// Menu buttons (close/save/delete/cancelDelete/confirmDelete) in source order,
// then the Title/Date/Details text fields (labels are the placeholders).
std::vector<CalendarAccessible>calendarMenuAccessibility(std::span<const CalendarAction>,const CalendarStrings&,core::Language);
}
