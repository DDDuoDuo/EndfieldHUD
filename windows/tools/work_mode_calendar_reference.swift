import AppKit
import CoreText
import QuartzCore

// Actual-source oracle of Work Mode and Calendar on the redesign (Mac 1.3.0 Sources/WorkModeCanvas.swift,
// HUDCalendarCanvas.swift with WorkModeController / HUDCalendarController and the design library) for the
// Windows canvases (windows/modules/work_mode_canvas.*, calendar_canvas.*). Each scenario builds the unchanged
// canvas in a fresh HUDDesignContext, drives it through its own API (controller actions on an injected clock,
// canvas actions, keys, hover through HUDControlHighlightLayer) and exports the canvas root's model tree (shared
// recorder, tools/hud_design_reference_support.swift), its controls and accessibility items, plus the values the
// Windows tests check directly (ring fraction, timer text, laps, toasts, confirm requests, formats).
// Isolation: temporary CFFIXED_USER_HOME, no window, no input posted (NSEvents are constructed, never sent), no
// notification centre (a fixture scheduler), the Calendar store only in a temporary directory with synthetic
// events, a fixed clock (2026-10-04 10:00 UTC).

private var reduce = false
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })
private let languages: [AppLanguage] = [.english, .simplifiedChinese, .traditionalChinese, .japanese, .korean]
private let utc = TimeZone(secondsFromGMT: 0)!
private let today = HUDCalendarDay(year: 2026, month: 10, day: 4)

// MARK: - Work Mode clock and timers (caller-driven, as tools/work_mode_reference.swift)

private final class FixtureTimer: WorkModeTimer {
    let interval: Double, repeats: Bool, action: () -> Void
    var due: Double, valid = true
    init(_ interval: Double, _ repeats: Bool, _ now: Double, _ action: @escaping () -> Void) {
        self.interval = interval; self.repeats = repeats; self.due = now + interval; self.action = action
    }
    func invalidate() { valid = false }
}
private final class FixtureClock {
    var now = 1000.0, timers: [FixtureTimer] = []
    func schedule(_ interval: Double, _ repeats: Bool, _ tolerance: Double, _ action: @escaping () -> Void) -> WorkModeTimer {
        let timer = FixtureTimer(interval, repeats, now, action); timers.append(timer); return timer
    }
    func advance(_ seconds: Double) {
        now += seconds
        for timer in timers.filter({ $0.valid && $0.due <= now }).sorted(by: { $0.due < $1.due }) {
            guard timer.valid else { continue }
            if timer.repeats { timer.due += floor((now - timer.due) / timer.interval + 1) * timer.interval } else { timer.valid = false }
            timer.action()
        }
        timers.removeAll { !$0.valid }
    }
}

// MARK: - Calendar fixture scheduler (no notification centre)

private final class FixtureScheduler: HUDCalendarNotificationScheduling {
    var permission: HUDCalendarPermission = .authorized
    func authorization(request: Bool, completion: @escaping (HUDCalendarPermission) -> Void) { completion(permission) }
    func reconcile(_ reminders: [HUDCalendarReminder], zone: TimeZone, completion: @escaping (Result<Void, Error>) -> Void) { completion(.success(())) }
    func cancelObsolete(keepingIDs: Set<String>, completion: @escaping () -> Void) { completion() }
}

@main private enum WorkModeCalendarReference {
    static func main() throws {
        precondition(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil)
        precondition(CommandLine.arguments.count >= 3 && CommandLine.arguments[1] == "--ui-test")
        let output = URL(fileURLWithPath: CommandLine.arguments[2])
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.reduceMotion = false
        configuration.accentHex = "FAD41F"
        HUDRuntimeAppearance.configuration = configuration
        L10n.language = .english
        let recorder = try Recorder(output: output)
        var payload: [String: Any] = ["schema": 1]
        var rows: [[String: Any]] = []
        let temp = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldWorkModeCalendarOracle-" + UUID().uuidString, isDirectory: true)
        defer { try? FileManager.default.removeItem(at: temp) }

        func spin(_ until: () -> Bool) {
            let limit = Date(timeIntervalSinceNow: 5)
            while !until() && Date() < limit { RunLoop.main.run(until: Date(timeIntervalSinceNow: 0.002)) }
            precondition(until(), "oracle wait timed out")
        }
        func makeContext() -> (HUDDesignContext, CALayer) {
            let overlay = HUDCenterOverlay(env: env, schedule: { _, _ in })
            let context = HUDDesignContext(env: env, overlay: overlay)
            let moduleHost = HUDLayer.plain("module.host")
            HUDMotion.perform { moduleHost.bounds = CGRect(origin: .zero, size: HUDMetric.box); moduleHost.speed = 0; moduleHost.timeOffset = 0 }
            return (context, moduleHost)
        }
        func controls(_ c: HUDDesignCanvas) -> [HUDControl] {
            var list: [HUDControl] = c.showsHeader ? c.header.controls : []
            list += c.page.controls
            list += c.textFields
            return list
        }
        func toastRow(_ context: HUDDesignContext) -> Any {
            guard let t = context.overlay.toastState.current else { return NSNull() }
            return ["icon": t.icon.rawValue, "title": t.title, "caption": t.caption]
        }
        func record(_ module: String, _ state: String, _ c: HUDDesignCanvas, _ context: HUDDesignContext, language: AppLanguage = .english,
                    light: Bool = false, accent: String = "FAD41F", extra: [String: Any] = [:]) throws {
            var row: [String: Any] = ["module": module, "state": state, "language": languageName(language), "light": light, "accent": accent,
                                      "layer": try recorder.tree(c.root, id: "root"),
                                      "controls": controls(c).map { controlState($0, in: c.root) },
                                      "accessibility": accessibility(c.accessibilityItems),
                                      "transitioning": c.isTransitioning, "popoverOpen": c.isPopoverOpen, "toast": toastRow(context)]
            for (k, v) in extra { row[k] = v }
            rows.append(row)
        }
        func themed(_ accent: String, _ light: Bool, _ language: AppLanguage, _ context: HUDDesignContext) {
            HUDColor.apply(accentHex: accent, light: light); context.refreshPalette(); L10n.language = language
        }
        func plain() { HUDColor.apply(accentHex: "FAD41F", light: false); L10n.language = .english; reduce = false }

        // MARK: Work Mode
        func workMode(_ body: (WorkModeCanvas, WorkModeController, FixtureClock, HUDDesignContext) throws -> Void) rethrows {
            let clock = FixtureClock()
            let controller = WorkModeController(clock: { clock.now }, scheduleTimer: clock.schedule)
            let (context, moduleHost) = makeContext()
            let canvas = WorkModeCanvas(context: context, controller: controller)
            _ = canvas.makeContent(for: .workMode, style: HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2))
            HUDMotion.perform { moduleHost.addSublayer(canvas.root) }
            canvas.setActive(true)
            try body(canvas, controller, clock, context)
            canvas.setActive(false)
            plain()
        }
        func workExtra(_ c: WorkModeCanvas, _ controller: WorkModeController) -> [String: Any] {
            let s = controller.snapshot
            return ["timerText": c.timerTextForTesting as Any? ?? NSNull(), "ringFraction": c.ringFractionForTesting.map { Double($0) } as Any? ?? NSNull(),
                    "laps": c.lapTextsForTesting, "editingCustom": c.isEditingCustom, "kind": s.kind.rawValue, "phase": s.phase.rawValue,
                    "duration": s.duration, "displayedSeconds": s.displayedSeconds, "focusMessage": c.focusMessage as Any? ?? NSNull(),
                    "litTicks": WorkModeDialGeometry.litTicks(WorkModeDialGeometry.fraction(s)),
                    "customField": ["text": c.customFieldForTesting.text, "invalid": mirrored(c.customFieldForTesting, "invalid", as: Bool.self) as Any? ?? NSNull()]]
        }
        for language in languages {
            try workMode { c, controller, _, context in
                themed("FAD41F", false, language, context); c.rerender()
                try record("workMode", "countdownIdle", c, context, language: language, extra: workExtra(c, controller))
            }
        }
        try workMode { c, controller, clock, context in
            themed("22A6F2", true, .english, context); c.rerender()
            try record("workMode", "countdownIdle", c, context, light: true, accent: "22A6F2", extra: workExtra(c, controller))
            plain(); context.refreshPalette()
            _ = clock
        }
        try workMode { c, controller, clock, context in
            c.primaryAction()
            try record("workMode", "countdownStarted", c, context, extra: workExtra(c, controller))
            clock.advance(61.3)
            try record("workMode", "countdownRunning", c, context, extra: workExtra(c, controller))
            clock.advance(1)
            try record("workMode", "countdownTick", c, context, extra: workExtra(c, controller))
            _ = c.keyDown(key(49, " "))
            try record("workMode", "countdownPaused", c, context, extra: workExtra(c, controller))
            let tabs = c.page.controls.filter { $0.id.hasPrefix("workMode.kind") }
            try record("workMode", "countdownPausedTabs", c, context, extra: ["tabs": tabs.map { controlState($0, in: c.root) }, "keyQ": c.keyDown(key(12, "q"))])
            _ = c.keyDown(key(36, "\r"))
            clock.advance(29 * 60)
            try record("workMode", "countdownCompleted", c, context, extra: workExtra(c, controller))
        }
        try workMode { c, controller, _, context in
            let preset = c.page.controls.first { $0.id == "workMode.preset:0" }!
            HUDControlHighlightLayer.update(in: c.root, point: centre(preset, in: c.root), pressed: false)
            try record("workMode", "presetHovered", c, context, extra: workExtra(c, controller))
            HUDControlHighlightLayer.update(in: c.root, point: nil, pressed: false)
            _ = c.pointerDown(at: centre(preset, in: c.root)); c.pointerUp(at: centre(preset, in: c.root))
            try record("workMode", "preset5", c, context, extra: workExtra(c, controller))
            let custom = c.page.controls.first { $0.id == "workMode.preset:3" }!
            _ = c.pointerDown(at: centre(custom, in: c.root)); c.pointerUp(at: centre(custom, in: c.root))
            try record("workMode", "customOpen", c, context, extra: workExtra(c, controller))
            c.commitCustom("abc")
            try record("workMode", "customInvalid", c, context, extra: workExtra(c, controller))
            c.commitCustom("0:04")
            try record("workMode", "customCommitted", c, context, extra: workExtra(c, controller))
            c.openCustom()
            c.commitCustom("4:00")
            c.openCustom()
            try record("workMode", "customOpenMinutes", c, context, extra: workExtra(c, controller))
            _ = c.keyDown(key(53))
            try record("workMode", "customClosedByEscape", c, context, extra: workExtra(c, controller))
        }
        try workMode { c, controller, clock, context in
            let tab = c.page.controls.first { $0.id == "workMode.kind:1" }!
            _ = c.pointerDown(at: centre(tab, in: c.root)); c.pointerUp(at: centre(tab, in: c.root))
            c.settleSwap()
            try record("workMode", "stopwatchIdle", c, context, extra: workExtra(c, controller))
            c.primaryAction()
            clock.advance(3907.2)
            for _ in 0..<4 { c.recordLap(); clock.advance(1) }
            try record("workMode", "stopwatchLaps", c, context, extra: workExtra(c, controller))
            c.primaryAction()
            try record("workMode", "stopwatchPaused", c, context, extra: workExtra(c, controller))
            c.reset()
            try record("workMode", "stopwatchReset", c, context, extra: workExtra(c, controller))
        }
        try workMode { c, controller, _, context in
            c.setFocusStatusMessage("  Focus could not turn on  ", needsAccessibilityPermission: true)
            try record("workMode", "focusPermission", c, context, extra: workExtra(c, controller))
            c.setFocusStatusMessage("Focus could not turn on")
            try record("workMode", "focusMessage", c, context, extra: workExtra(c, controller))
            c.setFocusStatusMessage("   ")
            try record("workMode", "focusCleared", c, context, extra: workExtra(c, controller))
        }
        try workMode { c, controller, clock, context in
            reduce = true; c.refreshMotionPreference()
            c.primaryAction(); clock.advance(2)
            try record("workMode", "reduceMotionRunning", c, context, extra: workExtra(c, controller))
            reduce = false
        }
        var durations: [[String: Any]] = []
        for language in languages {
            L10n.language = language
            for seconds in [300.0, 1800, 3600, 90, 4, 5400] { durations.append(["language": languageName(language), "seconds": seconds, "title": WorkModeCanvas.durationTitle(seconds)]) }
        }
        L10n.language = .english
        var times: [[String: Any]] = []
        for (kind, phase, duration, elapsed) in [(WorkModeKind.countdown, WorkModePhase.idle, 5400.0, 0.0), (.countdown, .idle, 86_400, 0), (.stopwatch, .paused, 0, 3907.4), (.countdown, .running, 60, 59.2)] {
            let s = WorkModeSnapshot(kind: kind, phase: phase, duration: duration, elapsed: elapsed)
            times.append(["kind": kind.rawValue, "duration": duration, "elapsed": elapsed, "text": WorkModeCanvas.timeText(s),
                          "fraction": Double(WorkModeDialGeometry.fraction(s)), "lit": WorkModeDialGeometry.litTicks(WorkModeDialGeometry.fraction(s))])
        }
        payload["workMode"] = ["durationTitles": durations, "timeTexts": times, "hitBounds": rect(WorkModeDialGeometry.frame.union(CGRect(origin: .zero, size: HUDMetric.box))),
                               "ticks": pathJSON(WorkModeDialGeometry.ticks(0..<120)), "transition": WorkModeCanvas.layoutTransitionDuration]

        // MARK: Calendar
        func event(_ n: Int, day: HUDCalendarDay, title: String, details: String) -> HUDCalendarEvent {
            let created = today.date(in: utc, hour: 10)!.addingTimeInterval(Double(n))
            return HUDCalendarEvent(id: UUID(uuidString: String(format: "C0000000-0000-4000-8000-%012d", n))!, title: title, details: details,
                                    day: day, created: created, modified: created)
        }
        let fixtureEvents = [
            event(1, day: today, title: "Planning review", details: ""),
            event(2, day: today, title: "Design sync with the Endfield operations team about the new centre layout", details: "Room 4\nBring the printed plans\rand the notes"),
            event(3, day: today, title: "Gym", details: "Leg day"),
            event(4, day: HUDCalendarDay(year: 2026, month: 10, day: 15), title: "Dentist", details: "09:30"),
            event(5, day: HUDCalendarDay(year: 2026, month: 11, day: 2), title: "Trip", details: ""),
            event(6, day: HUDCalendarDay(year: 2026, month: 9, day: 30), title: "Report", details: "Quarterly")]
        func calendar(events: [HUDCalendarEvent], permission: HUDCalendarPermission = .authorized,
                      _ body: (HUDCalendarCanvas, HUDCalendarController, HUDDesignContext) throws -> Void) throws {
            let directory = temp.appendingPathComponent(UUID().uuidString, isDirectory: true)
            let now = today.date(in: utc, hour: 10)!
            if !events.isEmpty { let store = try HUDCalendarStore(directory: directory); for e in events { try store.save(e, now: now, zone: utc) } }
            let scheduler = FixtureScheduler(); scheduler.permission = permission
            let controller = HUDCalendarController(loadStore: { try HUDCalendarStore(directory: directory) }, hasStoredData: { !events.isEmpty },
                                                   scheduler: scheduler, now: { now }, timeZone: { utc })
            let (context, moduleHost) = makeContext()
            let canvas = HUDCalendarCanvas(context: context, controller: controller)
            _ = canvas.makeContent(for: .calendar, style: HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2))
            HUDMotion.perform { moduleHost.addSublayer(canvas.root) }
            canvas.setPresented(true)
            canvas.setActive(true)
            spin { !controller.busy && (events.isEmpty || controller.events.count == events.count) }
            spin { !controller.busy }
            RunLoop.main.run(until: Date(timeIntervalSinceNow: 0.02))
            try body(canvas, controller, context)
            canvas.setActive(false); canvas.setPresented(false)
            plain()
        }
        func calendarExtra(_ c: HUDCalendarCanvas, _ controller: HUDCalendarController) -> [String: Any] {
            ["selected": c.selectedDay.string, "month": c.month.string, "editing": c.editingID?.uuidString as Any? ?? NSNull(),
             "titleField": ["text": c.titleFieldForTesting.text, "placeholder": c.titleFieldForTesting.placeholder],
             "detailsField": ["text": c.detailsFieldForTesting.text, "placeholder": c.detailsFieldForTesting.placeholder],
             "busy": controller.busy, "permission": "\(controller.permission)", "events": controller.events.count]
        }
        for language in languages {
            try calendar(events: []) { c, controller, context in
                themed("FAD41F", false, language, context); c.rerender()
                try record("calendar", "empty", c, context, language: language, extra: calendarExtra(c, controller))
            }
        }
        try calendar(events: fixtureEvents) { c, controller, context in
            try record("calendar", "events", c, context, extra: calendarExtra(c, controller))
            themed("E5383B", true, .english, context); c.rerender()
            try record("calendar", "events", c, context, light: true, accent: "E5383B", extra: calendarExtra(c, controller))
            plain(); context.refreshPalette(); c.rerender()
            let cell = c.page.controls.first { $0.id == "calendar.day:2026-10-15" }!
            HUDControlHighlightLayer.update(in: c.root, point: centre(cell, in: c.root), pressed: false)
            try record("calendar", "dayHovered", c, context, extra: calendarExtra(c, controller))
            HUDControlHighlightLayer.update(in: c.root, point: nil, pressed: false)
            c.perform("day:2026-10-15")
            try record("calendar", "otherDay", c, context, extra: calendarExtra(c, controller))
            _ = c.keyDown(key(124)); _ = c.keyDown(key(124))
            try record("calendar", "arrowRight", c, context, extra: calendarExtra(c, controller))
            c.perform("today")
            c.perform("event:C0000000-0000-4000-8000-000000000002")
            try record("calendar", "editing", c, context, extra: calendarExtra(c, controller))
            c.perform("delete")
            let request = context.overlay.confirmState.request
            try record("calendar", "deleteConfirm", c, context, extra: calendarExtra(c, controller).merging(["confirm": request.map {
                ["id": $0.id, "title": $0.title, "message": $0.message, "confirmTitle": $0.confirmTitle, "danger": $0.danger, "icon": $0.icon?.rawValue as Any? ?? NSNull()] } as Any? ?? NSNull()]) { $1 })
            context.overlay.dismissConfirm()
            c.perform("event:C0000000-0000-4000-8000-000000000002")
            try record("calendar", "editingClosed", c, context, extra: calendarExtra(c, controller))
            c.perform("add")
            try record("calendar", "addEmptyTitle", c, context, extra: calendarExtra(c, controller))
            c.perform("reminders")
            try record("calendar", "remindersScheduled", c, context, extra: calendarExtra(c, controller))
            c.perform("nextMonth"); c.settleSwap()
            try record("calendar", "nextMonth", c, context, extra: calendarExtra(c, controller))
            c.perform("monthPicker")
            try record("calendar", "monthPicker", c, context, extra: calendarExtra(c, controller).merging(["top": try recorder.tree(c.top, id: "top")]) { $1 })
            let nextYear = c.accessibilityItems.first { $0.id == "calendar.nextYear" }!.rect
            _ = c.pointerDown(at: CGPoint(x: nextYear.midX, y: nextYear.midY)); c.pointerUp(at: CGPoint(x: nextYear.midX, y: nextYear.midY))
            try record("calendar", "pickerNextYear", c, context, extra: calendarExtra(c, controller).merging(["top": try recorder.tree(c.top, id: "top")]) { $1 })
            c.perform("month:2027-02")
            c.settleSwap()
            try record("calendar", "pickedFebruary2027", c, context, extra: calendarExtra(c, controller))
            c.perform("month:2026-10")
            c.settleSwap()
            try record("calendar", "pickedToday", c, context, extra: calendarExtra(c, controller))
        }
        try calendar(events: [], permission: .denied) { c, controller, context in
            c.perform("reminders")
            spin { !controller.busy }
            try record("calendar", "remindersDenied", c, context, extra: calendarExtra(c, controller))
            for (month, name) in [("1900-01", "firstMonth"), ("9999-12", "lastMonth"), ("2024-02", "leapFebruary")] {
                c.perform("month:" + month); c.settleSwap()
                try record("calendar", name, c, context, extra: calendarExtra(c, controller))
            }
        }
        try calendar(events: fixtureEvents) { c, controller, context in
            // Busy: a save in flight disables the buttons and the refresh action.
            c.titleFieldForTesting.setText("Synthetic new event")
            c.perform("add")
            try record("calendar", "busy", c, context, extra: calendarExtra(c, controller))
            spin { !controller.busy }
            RunLoop.main.run(until: Date(timeIntervalSinceNow: 0.02))
            try record("calendar", "added", c, context, extra: calendarExtra(c, controller))
        }

        // The DateFormatter results the canvas shows (the Windows tests' formatter answers from these; the native
        // ICU civil context is checked against them on Windows).
        var formats: [[String: Any]] = []
        func formatter(_ template: String, _ language: AppLanguage) -> DateFormatter {
            let f = DateFormatter(); f.calendar = HUDCalendarDay.calendar(utc); f.timeZone = utc
            switch language {
            case .simplifiedChinese: f.locale = Locale(identifier: "zh_CN")
            case .traditionalChinese: f.locale = Locale(identifier: "zh_TW")
            case .japanese: f.locale = Locale(identifier: "ja_JP")
            case .korean: f.locale = Locale(identifier: "ko_KR")
            case .english, .system: f.locale = Locale(identifier: "en_US")
            }
            f.setLocalizedDateFormatFromTemplate(template); return f
        }
        let months: [HUDCalendarDay] = [.init(year: 2026, month: 9, day: 1), .init(year: 2026, month: 10, day: 1), .init(year: 2026, month: 11, day: 1),
                                        .init(year: 2027, month: 2, day: 1), .init(year: 1900, month: 1, day: 1), .init(year: 9999, month: 12, day: 1),
                                        .init(year: 2024, month: 2, day: 1)]
        for language in languages {
            var days: [String: [String: String]] = [:]
            for month in months {
                let cal = HUDCalendarDay.calendar(utc)
                guard let first = month.date(in: utc) else { continue }
                let padding = (cal.component(.weekday, from: first) - cal.firstWeekday + 7) % 7
                for index in -7..<49 {
                    guard let date = cal.date(byAdding: .day, value: index - padding, to: first) else { continue }
                    let day = HUDCalendarDay(date, zone: utc)
                    guard let at = day.date(in: utc) else { continue }
                    days[day.string] = ["yMMMMd": formatter("yMMMMd", language).string(from: at), "yMMMM": formatter("yMMMM", language).string(from: at),
                                        "EEEE": formatter("EEEE", language).string(from: at)]
                }
            }
            for year in [2025, 2026, 2027, 1900, 9999] { for m in 1...12 {
                let day = HUDCalendarDay(year: year, month: m, day: 1)
                if let at = day.date(in: utc), days[day.string] == nil { days[day.string] = ["yMMMM": formatter("yMMMM", language).string(from: at), "yMMMMd": formatter("yMMMMd", language).string(from: at), "EEEE": formatter("EEEE", language).string(from: at)] }
            } }
            formats.append(["language": languageName(language), "days": days,
                            "weekdays": formatter("EEE", language).shortStandaloneWeekdaySymbols ?? [],
                            "months": formatter("MMM", language).shortStandaloneMonthSymbols ?? []])
        }
        payload["calendar"] = ["formats": formats, "firstWeekday": HUDCalendarDay.calendar(utc).firstWeekday, "today": today.string,
                               "reminderStatus": languages.map { language -> [String: Any] in
                                   L10n.language = language
                                   defer { L10n.language = .english }
                                   return ["language": languageName(language), "denied": L10n.text("Notifications are off in System Settings", "通知已在系统设置中关闭"),
                                           "unavailable": L10n.text("Notifications are unavailable", "通知暂不可用")] }]
        payload["rows"] = rows

        // MARK: Text measurements and provenance
        payload["text"] = ["widths": recorder.widths.keys.sorted().map { recorder.widths[$0]! },
                           "metrics": recorder.metrics.keys.sorted().map { recorder.metrics[$0]! }]
        payload["layerDefaults"] = recorder.layerDefaults
        payload["isolation"] = ["actualSources": true, "temporaryHome": true, "windowCreated": !NSApp.windows.isEmpty,
                                "inputPosted": false, "storesTouched": "temporary directory only"]
        precondition(NSApp.windows.isEmpty)
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("work-mode-calendar-source.json"))
        print("Work Mode / Calendar oracle: \(rows.count) states, \(recorder.widths.count) widths, \(data.count) bytes")
    }
}
