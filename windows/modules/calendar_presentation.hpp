#pragma once
#include "modules/calendar_state.hpp"
#include "core/scene.hpp"
#include <array>
namespace endfield::modules {
using CalendarColor=std::array<double,4>;
struct CalendarAppearance {bool dark{true};CalendarColor accent{250./255,212./255,31./255,1};bool operator==(const CalendarAppearance&)const=default;};
struct CalendarStrings {
    std::string calendar="Calendar",today="Today",noEvents="No events",addEvent="Add event",editEvent="Edit event",title="Title",date="Date",details="Details",save="Save",cancel="Cancel",erase="Delete",deleteQuestion="Delete this event?",previousMonth="Previous month",nextMonth="Next month",refreshReminders="Refresh reminders",denied="Notifications are off in System Settings",unavailable="Notifications are unavailable";
    bool operator==(const CalendarStrings&)const=default;
    static CalendarStrings simplifiedChinese();
};
struct CalendarAction {std::string id,label;core::Rect rect;bool enabled{true};};
struct CalendarFeedback {std::string action,tintID,rimID;core::Rect rect;bool enabled{true};float restingRim{};};
struct CalendarArtwork {CalendarJson root;std::vector<CalendarAction>actions;std::vector<CalendarFeedback>feedback;core::Rect bounds;};
struct CalendarCell {CalendarDay day;core::Rect rect;};
std::vector<CalendarCell>calendarMonthCells(CalendarDay month,unsigned firstWeekday);
struct CalendarCanvasInput {
    CalendarAppearance appearance;CalendarStrings strings;CalendarDay today,selected,month;
    unsigned firstWeekday{1};std::array<std::string,7>weekdays{"S","M","T","W","T","F","S"};
    std::string monthHeading;std::vector<CalendarEvent>events;std::size_t firstEvent{};
    bool busy{};CalendarPermission permission{CalendarPermission::authorized};std::optional<std::string>error;
};
CalendarArtwork prepareCalendarCanvas(const CalendarCanvasInput&);
struct CalendarMenuInput {CalendarAppearance appearance;CalendarStrings strings;bool editing{},deleting{},busy{};std::optional<std::string>error;};
CalendarArtwork prepareCalendarMenu(const CalendarMenuInput&);
std::optional<std::string_view>calendarActionAt(std::span<const CalendarAction>,core::Point)noexcept;
// CalendarCanvas navigation/list contract. Visibility, persistence, editor and
// all finite transition clocks belong to the caller. No native/OS callbacks.
class CalendarView final {
public:explicit CalendarView(CalendarDay today);CalendarDay selected()const noexcept{return selected_;}CalendarDay month()const noexcept{return month_;}
    bool perform(std::string_view,CalendarDay today);bool scroll(core::Point,double delta,std::size_t selectedEventCount);
    void clampEvents(std::size_t count)noexcept;std::size_t firstEvent()const noexcept{return first_;}double scrollRemainder()const noexcept{return remainder_;}
    std::uint64_t revision()const noexcept{return revision_;}
private:CalendarDay selected_,month_;std::size_t first_{};double remainder_{};std::uint64_t revision_{};
};
}
