#pragma once
#include "app/utility_executor.hpp"
#include "core/localization.hpp"
#include "modules/calendar_state.hpp"
namespace endfield::native {
// Lossless app-owned Calendar key. Windows' composite (Group,Tag) key keeps
// the complete UUID; neither source identifier nor event identity is truncated.
struct CalendarNotificationKey {std::string group,tag;bool operator==(const CalendarNotificationKey&)const=default;};
std::optional<CalendarNotificationKey>calendarNotificationKey(std::string_view identifier);
// Official toast launch string; also read by the app's one activation callback.
// Unknown/corrupt or non-Calendar activation never becomes a module action.
std::string calendarNotificationArguments(std::string_view eventID,std::string_view signature);
std::optional<std::string>calendarNotificationEvent(std::string_view arguments);
struct CalendarScheduledNotification {
    std::string identifier,signature,title,body;double delivery{}; // Foundation seconds
    bool catchUp{}; // source native trigger uses max(1, delivery-now), even after FIFO delay
    bool operator==(const CalendarScheduledNotification&)const=default;
};
// Constructed/called/disposed on one borrowed UtilityExecutor operation. An
// injected fixture provider does not contact the real notification center.
class CalendarNotificationProvider {
public:virtual ~CalendarNotificationProvider()=default;
    virtual modules::CalendarPermission authorization(bool request)=0;
    virtual std::vector<CalendarScheduledNotification>pending()=0;
    virtual void remove(std::string_view identifier)=0;
    virtual void add(const CalendarScheduledNotification&)=0;
};
struct CalendarNotificationOptions {
    std::string establishedAppUserModelID; // installer/app identity helper owns registration
    core::Language language{core::Language::english};
    std::string calendarIdentity{"gregorian"};
};
using CalendarNotificationFactory=std::function<std::unique_ptr<CalendarNotificationProvider>(const CalendarNotificationOptions&)>;
// Platform capability differences from the Mac UNUserNotificationCenter path
// (reported, not hidden; the fallback follows each):
//  * A ScheduledToastNotification fires at an absolute instant, whereas the Mac
//    UNCalendarNotificationTrigger matches floating civil components. If the
//    time zone changes while EndfieldHUD is not running, a toast fires at the
//    old zone's 09:00. Fallback: reconcile at launch, on WM_TIMECHANGE /
//    WM_SETTINGCHANGE and on resume; each signature contains the 09:00
//    instant in the current zone, so a zone change re-registers every toast. Catch-ups are relative on
//    both platforms (max(1 s, delivery-now)).
//  * Windows has no requestAuthorization prompt. authorization(request) only
//    reads ToastNotifier::Setting(): Enabled -> authorized; disabled for the
//    app, the user or by policy -> denied (CalendarStrings::denied names
//    Windows Settings); no installed identity or a manifest block ->
//    unavailable. Nothing is ever changed on the user's behalf.
//  * Focus assist / Do Not Disturb suppresses the banner; the toast still
//    reaches the notification center, as Mac Focus does. No fallback needed.
//  * A Mac click only activates the app. On Windows the installer-registered
//    COM activator (app_notification_identity) routes the click to the
//    running HUD instead of starting a second process.
// Windows factory performs no registration, setting mutation or UI activation.
// Empty identity returns unavailable. A nonempty identity must already belong
// to this installed app. The real factory is never invoked by isolated tests.
CalendarNotificationFactory nativeCalendarNotificationFactory();
// Exact source reconciliation: scoped cancellation, signature reuse and no
// replay of an already-received reminder missing from the pending OS schedule.
// Throws before mutation for malformed/bounded inputs. Native add/remove can
// fail partway; source state records receipts only after complete success.
void reconcileCalendarNotifications(CalendarNotificationProvider&,std::span<const modules::CalendarReminder>,
    const CalendarNotificationOptions&);
void cancelObsoleteCalendarNotifications(CalendarNotificationProvider&,std::span<const std::string>keepingIDs);
struct CalendarNotificationStatus {bool busy{};std::size_t pending{};std::optional<std::string>error;};
// UI owner borrows ONE app FIFO executor. Construction starts no thread/IO.
// At most one accepted operation plus four bounded pending requests; admission
// retries on the app's existing completion drain, never a timer. Accepted work
// finishes after destruction, without UI callbacks. State must die first.
// flush is an explicit shutdown barrier, never an input/paint operation.
class NativeCalendarNotifications final {
public:
    NativeCalendarNotifications(app::UtilityExecutor&,CalendarNotificationOptions,
        CalendarNotificationFactory=nativeCalendarNotificationFactory());
    ~NativeCalendarNotifications();
    NativeCalendarNotifications(const NativeCalendarNotifications&)=delete;
    NativeCalendarNotifications&operator=(const NativeCalendarNotifications&)=delete;
    modules::CalendarScheduling scheduling();
    void setLanguage(core::Language); // caller then asks CalendarState to reconcile
    void queueCapacityAvailable();
    const CalendarNotificationStatus&status()const;
    bool flush();
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
