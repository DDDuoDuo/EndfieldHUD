#pragma once
#include "core/data/data_store.hpp"
#include <compare>
#include <functional>
#include <optional>
#include <span>

namespace endfield::modules {
using CalendarJson=ehud::data::Json;
inline constexpr std::size_t calendarMaximumEvents=256,calendarMaximumUpcoming=30,calendarMaximumBytes=3*1024*1024;
inline constexpr double calendarFoundationToUnix=978307200.;
inline constexpr std::string_view calendarNotificationPrefix="EndfieldHUD.Calendar.";
enum class CalendarErrorCode {invalidData,changedOnDisk,capacity,upcomingCapacity,invalidDate,missing,notifications};
class CalendarError final:public std::runtime_error {
public:explicit CalendarError(CalendarErrorCode);CalendarErrorCode code()const noexcept{return code_;}
private:CalendarErrorCode code_;
};
struct CalendarDay {
    int year{},month{},day{};
    bool valid()const noexcept;std::string string()const;
    static std::optional<CalendarDay>parse(std::string_view)noexcept;
    std::optional<CalendarDay>advanced(int days)const noexcept;
    std::optional<CalendarDay>advancedMonth(int months)const noexcept;
    unsigned weekday()const; // Gregorian Sunday=1, ... Saturday=7
    unsigned daysInMonth()const;
    auto operator<=>(const CalendarDay&)const=default;
};
// Source limits are Swift Characters. Platform owners supply the installed
// Unicode implementation explicitly; no byte/UTF16 truncation is substituted.
struct CalendarTextRules {
    std::function<std::size_t(std::string_view)>characters;
    std::function<std::string(std::string_view)>trimmed;
    std::function<std::string(std::string_view,std::size_t)>prefix;
};
struct CalendarEvent {
    std::string id,title,details;CalendarDay day;double created{},modified{};
    bool catchUpPending{};std::optional<double>catchUpDate;
    std::optional<std::string>beforeScheduledFor,dayScheduledFor;
    CalendarJson originalFields{CalendarJson::Object{}},dayFields{CalendarJson::Object{}},originalNumbers{CalendarJson::Object{}};
    bool operator==(const CalendarEvent&)const;
};
struct CalendarFile {std::vector<CalendarEvent>events;CalendarJson originalFields{CalendarJson::Object{}};bool operator==(const CalendarFile&)const=default;};
void validateCalendarEvent(const CalendarEvent&,const CalendarTextRules&);
void validateCalendarFile(const CalendarFile&,const CalendarTextRules&);
CalendarFile decodeCalendarFile(const CalendarJson&,const CalendarTextRules&);
CalendarJson encodeCalendarFile(const CalendarFile&,const CalendarTextRules&);
// Immutable injected civil conversion. Timestamp values throughout this API
// are Foundation seconds, not Unix time. Calendar arithmetic is date-based,
// never "subtract 86400" across a DST change. No OS clock is read by the model.
struct CalendarTimeZone {
    std::string identity;
    std::function<std::optional<double>(CalendarDay,unsigned hour,unsigned minute)>timestamp;
    std::function<CalendarDay(double)>day;
};
CalendarTimeZone calendarUTC(); // deterministic fixture/civil UTC, no OS API
struct CalendarReminder {
    std::string identifier,eventID,title;CalendarDay day;double date{};
    bool previousDay{},catchUp{},wasScheduled{};
    std::string signature()const;
    bool operator==(const CalendarReminder&)const=default;
};
std::vector<CalendarReminder>calendarReminderPlan(std::span<const CalendarEvent>,double now,const CalendarTimeZone&);
void calendarUpsert(CalendarFile&,CalendarEvent,CalendarDay today,const CalendarTextRules&);
void calendarRemove(CalendarFile&,std::string_view id);
bool calendarClaimCatchUps(CalendarFile&,double now,CalendarDay today);
bool calendarRecordScheduled(CalendarFile&,std::span<const CalendarReminder>);
}
