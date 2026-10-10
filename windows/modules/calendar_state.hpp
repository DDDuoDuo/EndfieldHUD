#pragma once
#include "modules/calendar_repository.hpp"
#include <exception>
#include <memory>
namespace endfield::modules {
enum class CalendarPermission {unknown,authorized,denied,unavailable};
struct CalendarExecutor {
    // Borrow the app's existing UtilityExecutor route. false is backpressure;
    // completion runs later on the owner, never in the file work or inline.
    std::function<bool(std::function<void()>,std::function<void(std::exception_ptr)>)>submit;
};
struct CalendarScheduling {
    // Native notification adapter delivers completions on this owner. No
    // second notification delegate, worker or timer is implied by this seam.
    std::function<void(bool request,std::function<void(CalendarPermission)>)>authorization;
    std::function<void(std::vector<CalendarReminder>,CalendarTimeZone,std::function<void(std::exception_ptr)>)>reconcile;
    std::function<void(std::vector<std::string> keepingIDs,std::function<void()>)>cancelObsolete;
};
struct CalendarStateOptions {
    CalendarTextRules text;std::function<double()>now;std::function<CalendarTimeZone()>zone;
    std::function<std::string()>newID;CalendarScheduling scheduling;
    std::function<void()>changed;std::function<void(std::string_view)>event; // created/edited/deleted only
};
// Owner-thread state, lazy file work through ONE borrowed FIFO executor. The
// caller forwards clock/time-zone/locale/wake/activation changes explicitly.
// Scheduling and accepted saves can continue while the module is hidden.
class CalendarState final {
public:CalendarState(std::shared_ptr<CalendarRepository>,CalendarExecutor,CalendarStateOptions);~CalendarState();
    CalendarState(const CalendarState&)=delete;CalendarState&operator=(const CalendarState&)=delete;
    void startIfExisting();void setActive(bool);void refreshReminders();void refreshForSystemChange();
    bool save(std::string title,std::string details,CalendarDay,std::optional<std::string>id={},std::function<void(bool)>completion={});
    bool remove(std::string id,std::function<void(bool)>completion={});
    // Free text is shown as reported; a source code keeps its type so the
    // owner can translate it at display time (immediate language switch).
    void report(std::string);void report(CalendarErrorCode);void queueCapacityAvailable();
    bool active()const noexcept;bool loaded()const noexcept;bool busy()const noexcept;bool scheduling()const noexcept;
    bool hasPendingWork()const noexcept;bool hasPersistenceFailure()const noexcept;
    CalendarDay today()const;CalendarTimeZone zone()const;
    // error() is the English source text (Mac errorDescription); errorCode()
    // is set when that text came from a source CalendarError.
    CalendarPermission permission()const noexcept;const std::optional<std::string>&error()const noexcept;
    std::optional<CalendarErrorCode>errorCode()const noexcept;
    std::span<const CalendarEvent>events()const noexcept;std::uint64_t revision()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
