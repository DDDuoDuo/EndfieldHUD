// Post-import Calendar reminder reconciliation (WINDOWS-MIGRATION.md section 7:
// "recreate/reconcile reminder registrations"). The imported calendar is run
// through the unchanged Windows reminder plan and reconcile against an
// in-memory notification provider; the real notification center is never used.
#include "mac_import_test_support.hpp"
#include "modules/calendar_repository.hpp"
#include "native/calendar_notifications.hpp"

namespace {
namespace n = endfield::native;
struct Provider final : n::CalendarNotificationProvider {
    std::vector<n::CalendarScheduledNotification> scheduled;
    std::vector<std::string> removed;
    std::vector<n::CalendarScheduledNotification> added;
    mod::CalendarPermission authorization(bool) override { return mod::CalendarPermission::authorized; }
    std::vector<n::CalendarScheduledNotification> pending() override { return scheduled; }
    void remove(std::string_view id) override {
        removed.emplace_back(id);
        std::erase_if(scheduled, [&](const auto& value) { return value.identifier == id; });
    }
    void add(const n::CalendarScheduledNotification& value) override { added.push_back(value); scheduled.push_back(value); }
};
// What the Mac writes after HUDCalendarController scheduled reminders with the
// macOS notification center: receipts on a future and on a past event.
std::string receiptedCalendar() {
    return J(J::Object{{"version", 1}, {"futureCalendarField", "kept"}, {"events", J::Array{
        J::Object{{"id", uuid(31)}, {"title", "Contingency"}, {"details", "Bring sanity"}, {"day", J::Object{{"year", 2030}, {"month", 11}, {"day", 3}}},
                  {"created", 700000000.5}, {"modified", 700000100.5}, {"catchUpPending", false},
                  {"beforeScheduledFor", "2030-11-03"}, {"dayScheduledFor", "2030-11-03"}, {"futureEventField", J::Array{1, 2}}},
        J::Object{{"id", uuid(32)}, {"title", "Past"}, {"details", ""}, {"day", J::Object{{"year", 2020}, {"month", 1}, {"day", 1}}},
                  {"created", 600000000.0}, {"modified", 600000000.0}, {"catchUpPending", false}, {"dayScheduledFor", "2020-01-01"}}}}}).encode();
}
std::vector<std::string> identifiers(const std::vector<n::CalendarScheduledNotification>& values) {
    std::vector<std::string> out;
    for (const auto& value : values) out.push_back(value.identifier);
    std::sort(out.begin(), out.end());
    return out;
}
void reconcileAfterImport() {
    Temporary t;
    Export e{t.root / "export"};
    e.add("EndfieldCharge/Calendar/calendar.json", receiptedCalendar());
    e.write();
    const auto rules = platform().calendarText;
    const auto utc = mod::calendarUTC();
    const auto now = *utc.timestamp({2026, 10, 10}, 12, 0);
    n::CalendarNotificationOptions options;
    {   // Control: kept as-is, the Mac receipts suppress every future reminder on an empty Windows schedule.
        write(t.root / "mac" / "calendar.json", receiptedCalendar());
        mod::CalendarJSONRepository mac(t.root / "mac", rules);
        const auto file = mac.load();
        Provider provider;
        const auto plan = mod::calendarReminderPlan(file.events, now, utc);
        n::reconcileCalendarNotifications(provider, plan, options);
        check(plan.size() == 2 && provider.added.empty(), "Unchanged Mac receipts would silently drop the imported reminders");
    }
    const auto destination = t.root / "EndfieldHUD";
    m::MacImportSession session(e.root, destination, platform());
    session.stage();
    const auto& report = store(session.summary(), m::MacImportStore::calendar);
    check(report.status == m::MacImportStatus::importedWithWarnings && report.records == 2 && hasWarning(report, "3 macOS notification receipt(s)") &&
          session.summary().remindersNeedReconcile, "Calendar import clears the macOS notification receipts and asks for a reconcile");
    session.commit();
    check(read(e.root / "EndfieldCharge" / "Calendar" / "calendar.json") == receiptedCalendar(), "The export keeps its original receipts");
    const auto saved = J::parse(read(destination / "Calendar" / "calendar.json"));
    const auto& event = saved["events"].array()[0];
    check(!event.contains("beforeScheduledFor") && !event.contains("dayScheduledFor") && !saved["events"].array()[1].contains("dayScheduledFor") &&
          event["futureEventField"].array().size() == 2 && saved["futureCalendarField"].string() == "kept" && event["created"].number() == 700000000.5,
          "Only the receipts are removed; unknown fields and Foundation dates are kept");
    mod::CalendarJSONRepository repository(destination / "Calendar", rules);
    auto file = repository.load();
    Provider provider;
    const std::string stale = std::string(mod::calendarNotificationPrefix) + uuid(99) + ".day";
    provider.scheduled = {{stale, "old", "Removed event", "", now + 3600}, {"Activity.foreign", "x", "Other module", "", now + 60}};
    const auto plan = mod::calendarReminderPlan(file.events, now, utc);
    n::reconcileCalendarNotifications(provider, plan, options);
    const std::vector<std::string> expected{std::string(mod::calendarNotificationPrefix) + uuid(31) + ".before", std::string(mod::calendarNotificationPrefix) + uuid(31) + ".day"};
    check(plan.size() == 2 && identifiers(provider.added) == expected, "Every future imported reminder is scheduled once; the past event is not replayed");
    check(provider.removed == std::vector<std::string>{stale} && identifiers(provider.scheduled) ==
          std::vector<std::string>{"Activity.foreign", expected[0], expected[1]}, "Stale EndfieldHUD reminders are removed; other notifications are untouched");
    // The Calendar owner records Windows receipts after success; repeated
    // reconciles (next launch, language change) never duplicate.
    check(mod::calendarRecordScheduled(file, plan), "Windows receipts are recorded");
    repository.save(file);
    const auto again = mod::calendarReminderPlan(repository.load().events, now, utc);
    n::reconcileCalendarNotifications(provider, again, options);
    n::reconcileCalendarNotifications(provider, again, options);
    check(provider.added.size() == 2 && provider.removed.size() == 1 && provider.scheduled.size() == 3, "Reconciling again adds no duplicates");
}
}
int main() {
    try {
        reconcileAfterImport();
        std::cout << "PASS " << checks << " post-import Calendar reconcile checks; in-memory notification provider only\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
