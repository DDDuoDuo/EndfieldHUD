# Calendar

## Authoritative requirement

> 12. 日历，可查看日历，添加事项。临近事项日期一天会发送提醒，当天也会发送提醒。

Batch #8 implements a local HUD calendar. It does not read or modify macOS Calendar and does not request EventKit access.

## Interface

The 400 × 440 retained module shows a localized month, weekday headings, today/selected-day markings and event dots. Month arrows and unmodified left/right arrow keys browse months. Selecting a date shows its events; longer lists scroll within three visible rows. Today restores the current local date. Add and event rows open the shared Notes/personal-card menu style on the same tilted module plane.

Today has an opaque theme-colored face; Add uses a light filled cut-corner face.
Refresh uses the same filled clockwise arrow geometry as Storage. Event titles
are left-aligned. Routine reminder explanations are omitted from both footer and
editor, while permission failures, save errors and deletion confirmation remain
visible. Editor headings use `//`, Date has no extra format caption, and the tick
is centered within its confirmation button.

The event form contains title, civil date (`YYYY-MM-DD`) and details. Title is bounded to 120 characters and details to 2,000. Inputs use `HUDProjectedTextEditor`, including projected glyphs, caret/selection, placeholders and native text input. Save is explicit; Escape or an outside click cancels the draft. Deletion requires the separate ×/✓ confirmation. Menus use finite 160 ms opening / 140 ms closing fades, date/month changes use a 180 ms transition, and Reduce Motion suppresses these transitions. Square menu actions have accessibility labels; date accessibility labels include the whole date. No native preference window or system dropdown is opened.

## Reminders

Accepted events schedule nonrepeating macOS notifications for **09:00 local time on the previous calendar day and on the event date**. A calendar-day subtraction preserves local 09:00 across DST, including 23- and 25-hour intervals. Civil notification components follow the current timezone rather than pinning alerts to a UTC offset.

Creating an event for today after 09:00 reserves one near-immediate reminder after notification authorization completes. The reservation is committed before submission, so reopening does not invent another catch-up. A persisted successful-scheduling receipt also avoids recreating a missing, already delivered/dismissed request after a backward clock change. Past reminders are not replayed.

Permission is requested when saving the first event, or when explicitly retrying reminders. Merely launching the app or opening an empty calendar does not ask. Existing data is reconciled without prompting. Denied/unavailable authorization is shown honestly in the calendar footer while the event remains saved locally. Refresh reminders retries authorization/scheduling; it does not open a second service.

Stable identifiers are scoped to `EndfieldHUD.Calendar.<event UUID>.before/day`. Reconciliation preserves unchanged requests, updates edited requests and cancels deleted/obsolete Calendar requests without touching other notifications. When authorization is denied or unknown, a cancellation-only pass still removes obsolete identifiers while preserving valid future requests; it neither adds notifications nor discards their receipts. The existing app notification delegate retains foreground delivery ownership. Native pending requests are delivered by macOS even while the HUD is closed or the app is not running, subject to the user's notification and Focus settings.

The calendar permits **30 upcoming events** (at most 60 scheduled requests) and **256 total events**. Attempts beyond these limits display an error rather than accepting an event whose reminders would silently be missing. Events are date-only in this batch; custom times and recurring events are outside its requirements.

## Ownership and performance

`OverlayController` retains one production `HUDCalendarController`. Normal app startup calls `startIfExisting()` only after diagnostic startup branches. This performs one utility-queue existence check; an unused calendar creates no directory, reads no event JSON and initializes no notification center. Opening the module while that check is in flight still completes its lazy load.

The store and all reads/writes belong to one utility queue. Opening/closing the HUD does not destroy the controller or register another service. Timezone, calendar-day, clock, locale, app activation and wake notifications reconcile a small bounded event set. There is no daily polling timer, display-link work or closed-HUD repaint loop. Canvas work happens on explicit state changes, and accessibility date controls from previous months are removed.

`drainPendingWrites(timeout:completion:)` includes event commits and notification scheduling acknowledgments in the app's quit barrier. A temporary deadline timer exists only while draining. Reminder or transient validation failures remain visible without failing this barrier; only persistence failure prevents a successful drain. Native notification callbacks and system observers capture the controller weakly; queued writes retain it until acknowledgment so a requested save is not silently abandoned.

## Persistence and compatibility

The independent version-one store is `~/Library/Application Support/EndfieldCharge/Calendar/calendar.json`. Existing Notes, profile, settings and other module schemas are unchanged. Saves compare with the last disk bytes, then atomically replace the file. Corrupt, future-version, duplicate-ID and oversized files are refused and preserved; they are never reset to an empty calendar. Reads are capped at 3 MiB.

`HUDCalendarController.fixture()` is a lazy temporary store with a no-op scheduler. Diagnostic `--ui-test`, smoke-test and render invocations also use temporary data and never contact the native notification center. Core tests inject a fake scheduler and clock; they do not request permissions, schedule real alerts or change user settings.

## Verification

`Tests/HUDCalendarTests.swift` covers civil-date validation, leap dates, DST, timezone changes, stable request IDs, past-event skipping, native floating calendar-trigger math, isolated lazy startup, an open-during-startup race, persistence/corruption/future-schema/concurrent-write protection, capacity handling, same-day catch-up, fake permission grant/denial (including deletion while denied and preservation of valid pending requests), idempotent reminder reconciliation, backward-clock delivery receipts, write draining (including scheduling failure versus persistence failure), retained calendar layout, projected inputs, square controls, explicit save/delete and teardown.

The final focused AppKit executable compiled these modules with the shared HUD dependencies and passed **89 assertions** using generated temporary fixtures. Native notification delivery, permission-banner appearance, delivery after quitting on another Mac, VoiceOver interaction and hardware CPU/GPU measurements still require manual device validation. Tests construct a native calendar trigger only to inspect its date calculation; they never obtain `UNUserNotificationCenter`.

## Primary API references

- [Apple: Scheduling a notification locally from your app](https://developer.apple.com/documentation/usernotifications/scheduling-a-notification-locally-from-your-app)
- [Apple: Asking permission to use notifications](https://developer.apple.com/documentation/usernotifications/asking-permission-to-use-notifications)
- [Apple: UNCalendarNotificationTrigger](https://developer.apple.com/documentation/usernotifications/uncalendarnotificationtrigger)
- [Apple: Calendar date arithmetic](https://developer.apple.com/documentation/foundation/calendar/date(byadding:value:to:wrappingcomponents:))
