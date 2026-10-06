import AppKit
import UserNotifications

enum HUDCalendarTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        func rejects(_ message: String, _ body: () throws -> Void) { do { try body(); check(false, message) } catch { check(true, message) } }
        func wait(_ predicate: () -> Bool) -> Bool {
            let deadline = Date().addingTimeInterval(8)
            while !predicate(), Date() < deadline { RunLoop.current.run(until: Date().addingTimeInterval(0.005)) }; return predicate()
        }
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldHUD-CalendarTests-" + UUID().uuidString)
        try! FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: folder) }
        let utc = TimeZone(secondsFromGMT: 0)!, la = TimeZone(identifier: "America/Los_Angeles")!
        let today = HUDCalendarDay(year: 2026, month: 10, day: 4)
        var now = today.date(in: utc, hour: 13)!
        func event(_ day: HUDCalendarDay, title: String = "Generated event") -> HUDCalendarEvent {
            HUDCalendarEvent(title: title, details: "Temporary fixture only", day: day, created: now, modified: now)
        }
        check(HUDCalendarDay(year: 2024, month: 2, day: 29).isValid, "Leap days are accepted by civil-date validation")
        check(!HUDCalendarDay(year: 2025, month: 2, day: 29).isValid, "Invalid Gregorian dates never normalize into a different event day")
        check(HUDCalendarDay.parse("2026-10-04") == today && HUDCalendarDay.parse("2026-13-04") == nil, "Date input requires a valid calendar date")
        check(today.advanced(1, zone: utc)?.string == "2026-10-05", "Date addition uses local calendar days")
        let spring = event(HUDCalendarDay(year: 2026, month: 3, day: 8)), fall = event(HUDCalendarDay(year: 2026, month: 11, day: 1))
        let old = HUDCalendarDay(year: 2026, month: 1, day: 1).date(in: utc)!
        let springPlan = HUDCalendarReminder.plan(events: [spring], now: old, zone: la)
        let fallPlan = HUDCalendarReminder.plan(events: [fall], now: old, zone: la)
        check(springPlan.count == 2 && springPlan[1].date.timeIntervalSince(springPlan[0].date) == 23 * 3600, "Spring DST reminder gap is one calendar day, not 86400 seconds")
        check(fallPlan.count == 2 && fallPlan[1].date.timeIntervalSince(fallPlan[0].date) == 25 * 3600, "Autumn DST reminder gap is one calendar day across the repeated hour")
        check(springPlan.allSatisfy { HUDCalendarDay.calendar(la).component(.hour, from: $0.date) == 9 }, "Both DST-side reminders stay at 09:00 local time")
        let china = TimeZone(identifier: "Asia/Shanghai")!
        let changedZone = HUDCalendarReminder.plan(events: [fall], now: old, zone: china)
        check(changedZone[1].identifier == fallPlan[1].identifier && changedZone[1].date != fallPlan[1].date, "Zone rescheduling changes instants while preserving stable reminder identifiers")
        check(Set(fallPlan.map(\.identifier)).count == 2 && fallPlan.allSatisfy { $0.identifier.hasPrefix(HUDCalendarStore.notificationPrefix) }, "Day-before and same-day reminders cannot collide or touch another module's identifiers")
        check(HUDCalendarReminder.plan(events: [spring], now: now, zone: la).isEmpty, "Past reminders are not replayed when the app opens")
        let priorDefault = NSTimeZone.default
        NSTimeZone.default = utc
        var civil = DateComponents(); civil.year = 2030; civil.month = 10; civil.day = 4; civil.hour = 9
        let floating = UNCalendarNotificationTrigger(dateMatching: civil, repeats: false)
        let originalDate = floating.nextTriggerDate()
        NSTimeZone.default = china
        let changedDate = floating.nextTriggerDate()
        NSTimeZone.default = priorDefault
        check(originalDate != nil && changedDate != nil && originalDate!.timeIntervalSince(changedDate!) == 8 * 3600,
              "A native calendar trigger with floating components follows a local timezone change without requesting notification permissions")
        check(HUDCalendarStore.applicationDirectory(arguments: ["--ui-test"]) == HUDCalendarStore.applicationDirectory(arguments: ["--calendar-smoke-test"]), "Diagnostic calendar directories are stable within the isolated process")
        check(HUDCalendarStore.applicationDirectory(arguments: ["--ui-test"]) != HUDCalendarStore.applicationDirectory(arguments: []), "Diagnostic calendar files cannot reach the real application store")
        let storeFolder = folder.appendingPathComponent("Store"), store = try! HUDCalendarStore(directory: storeFolder)
        check(!HUDCalendarStore.exists(at: storeFolder), "Reading a new empty calendar creates no file")
        let tomorrow = today.advanced(1, zone: utc)!, first = event(tomorrow)
        try! store.save(first, now: now, zone: utc)
        let restored = try! HUDCalendarStore(directory: storeFolder)
        check(restored.events == [first], "Events round-trip in a separate version-one local store")
        let file = storeFolder.appendingPathComponent("calendar.json"), bytes = try! Data(contentsOf: file)
        var payload = try! JSONSerialization.jsonObject(with: bytes) as! [String: Any]; payload["version"] = 99
        let future = try! JSONSerialization.data(withJSONObject: payload); try! future.write(to: file)
        rejects("Future schemas are refused without reset") { _ = try HUDCalendarStore(directory: storeFolder) }
        check(try! Data(contentsOf: file) == future, "A future calendar file remains byte-for-byte intact")
        try! Data("broken".utf8).write(to: file)
        rejects("Corrupt calendars are not silently replaced") { _ = try HUDCalendarStore(directory: storeFolder) }
        rejects("A stale writer refuses a concurrently edited file") { try restored.save(first, now: now, zone: utc) }
        check(try! Data(contentsOf: file) == Data("broken".utf8), "Rejected calendar writes preserve external edits")
        try! bytes.write(to: file)
        try! restored.remove(first.id)
        check(restored.events.isEmpty, "Explicit event deletion is persisted")
        let capped = try! HUDCalendarStore(directory: folder.appendingPathComponent("Capacity"))
        for _ in 0..<30 { try! capped.save(event(tomorrow), now: now, zone: utc) }
        rejects("Thirty upcoming events reserves enough native notification slots for every accepted reminder") { try capped.save(event(tomorrow), now: now, zone: utc) }
        check(capped.events.count == 30, "An over-capacity save changes neither data nor notification promises")
        try! capped.save(event(HUDCalendarDay(year: 2000, month: 1, day: 1)), now: now, zone: utc)
        check(capped.events.count == 31 && HUDCalendarStore.maximumEvents == 256, "Historical events do not consume the upcoming-reminder budget")
        var catchup = event(today); catchup.catchUpPending = true
        let catchupStore = try! HUDCalendarStore(directory: folder.appendingPathComponent("Catchup"))
        try! catchupStore.save(catchup, now: now, zone: utc)
        try! catchupStore.claimCatchUps(now: now, zone: utc)
        let reservation = catchupStore.events[0]
        check(!reservation.catchUpPending && reservation.catchUpDate == now.addingTimeInterval(3), "A late same-day event reserves exactly one near-immediate reminder durably")
        let immediate = HUDCalendarReminder.plan(events: catchupStore.events, now: now, zone: utc)
        check(immediate.count == 1 && immediate[0].catchUp && !immediate[0].previousDay, "Same-day catch-up does not invent yesterday's alert")
        let reopenedCatchup = try! HUDCalendarStore(directory: folder.appendingPathComponent("Catchup"))
        try! reopenedCatchup.claimCatchUps(now: now.addingTimeInterval(10), zone: utc)
        check(HUDCalendarReminder.plan(events: reopenedCatchup.events, now: now.addingTimeInterval(10), zone: utc).isEmpty, "Reopening after catch-up cannot send it twice")

        let fake = CalendarNotificationFixture(), controllerFolder = folder.appendingPathComponent("Controller")
        let lock = NSLock(); var loads = 0, loadOnMain = true
        let controller = HUDCalendarController(loadStore: {
            lock.lock(); loads += 1; loadOnMain = Thread.isMainThread; lock.unlock()
            return try HUDCalendarStore(directory: controllerFolder)
        }, hasStoredData: { HUDCalendarStore.exists(at: controllerFolder) }, scheduler: fake, now: { now }, timeZone: { utc })
        check(loads == 0 && fake.promptCount == 0, "Calendar construction reads no event data and requests no permission")
        controller.startIfExisting()
        check(wait { !controller.busy }, "Empty startup existence check finishes asynchronously")
        check(loads == 0 && fake.authorizationCount == 0, "Unused calendars do not initialize their scheduler on launch")
        let startupGate = DispatchSemaphore(value: 0), startupReached = DispatchSemaphore(value: 0)
        let startupFolder = folder.appendingPathComponent("StartupRace")
        let startup = HUDCalendarController(loadStore: { try HUDCalendarStore(directory: startupFolder) }, hasStoredData: {
            startupReached.signal(); startupGate.wait(); return false
        }, scheduler: CalendarNotificationFixture(), now: { now }, timeZone: { utc })
        startup.startIfExisting()
        check(startupReached.wait(timeout: .now() + 2) == .success, "Startup fixture holds only its background existence check")
        startup.setActive(true); startupGate.signal()
        check(wait { !startup.busy }, "Opening while startup is checking an absent file still finishes loading")
        check(startup.save(title: "Opened during startup", details: "", day: tomorrow), "An active calendar cannot remain unloaded after an absent-file startup race")
        var startupDrained = false; startup.drainPendingWrites { startupDrained = $0 }
        check(wait { startupDrained }, "Startup-race fixture commits and schedules entirely in its temporary directory")
        startup.setActive(false)
        controller.setActive(true)
        check(wait { !controller.busy }, "Opening the calendar lazily loads its data")
        lock.lock(); let workerRead = loads == 1 && !loadOnMain; lock.unlock()
        check(workerRead && fake.promptCount == 0, "Opening an empty calendar uses one worker and no permission prompt")
        var actions: [String] = []; controller.onEvent = { actions.append($0) }
        fake.holdAuthorization = true
        check(controller.save(title: "Fixture late event", details: "Only generated data", day: today), "A valid event begins an asynchronous atomic save")
        check(wait { !fake.held.isEmpty }, "Adding the first event reaches on-demand notification authorization")
        now = now.addingTimeInterval(300); fake.releaseAuthorization(allow: true)
        check(wait { fake.reconciliations > 0 && !controller.busy }, "Scheduling follows authorization without requiring a second user action")
        check(fake.promptCount == 1 && fake.pending.count == 1, "The first event prompts once and produces one current-day reminder")
        check(fake.pending.values.first?.date == now.addingTimeInterval(3), "A long permission decision still gets a fresh finite same-day catch-up")
        check(controller.permission == .authorized && actions == ["created"], "Only the explicit create action reaches Event Log metadata")
        let additions = fake.additions, firstID = controller.events[0].id
        controller.refreshForSystemChange()
        check(wait { fake.reconciliations == 2 && !controller.busy }, "Clock/time-zone refresh reconciles the current bounded event set")
        check(fake.additions == additions, "Repeated lifecycle refreshes keep stable reminders without duplicates")
        check(controller.events[0].dayScheduledFor == today.string, "Successful notification receipts are stored with the event's civil day")
        var drained = false; controller.drainPendingWrites { drained = $0 }
        check(wait { drained }, "Calendar commits and scheduling acknowledgments drain before quit")
        check(controller.save(title: "Renamed fixture", details: "", day: today, id: firstID), "Editing reuses the same event identity")
        check(wait { actions.contains("edited") && fake.reconciliations == 3 }, "An edit updates the native request using its existing identifier")
        check(fake.promptCount == 1 && fake.pending.count == 1, "Editing cannot create another authorization prompt or extra reminders")
        check(wait { !controller.busy }, "Notification receipts settle before the simulated delivery")
        fake.pending = [:]; let beforeRollback = fake.additions
        now = now.addingTimeInterval(-60)
        controller.refreshForSystemChange()
        check(wait { fake.reconciliations == 4 && !controller.busy }, "Backward clock changes reconcile without a polling loop")
        check(fake.pending.isEmpty && fake.additions == beforeRollback, "Already delivered/dismissed reminders are not recreated after the clock moves backward")
        controller.delete(firstID)
        check(wait { controller.events.isEmpty && fake.pending.isEmpty }, "Deleting cancels only that calendar's pending reminders")
        check(actions == ["created", "edited", "deleted"], "Calendar records only closed action names, never title, body, date or paths")
        let denied = CalendarNotificationFixture(); denied.permission = .denied
        let deniedController = HUDCalendarController(loadStore: { try HUDCalendarStore(directory: folder.appendingPathComponent("Denied")) }, scheduler: denied, now: { now }, timeZone: { utc })
        deniedController.setActive(true); check(wait { !deniedController.busy }, "Denied fixture loads independently")
        _ = deniedController.save(title: "Saved without notification", details: "", day: tomorrow)
        check(wait { deniedController.events.count == 1 && deniedController.permission == .denied }, "Denied permission keeps the local event and reports an honest status")
        check(denied.pending.isEmpty && denied.promptCount == 0, "Denied notification authorization is not repeatedly requested")
        var deniedDrained = false; deniedController.drainPendingWrites { deniedDrained = $0 }; check(wait { deniedDrained }, "Denied fixture drains its authorization cleanup before simulating previous pending requests")
        let previouslyScheduled = HUDCalendarReminder.plan(events: deniedController.events, now: now, zone: utc)
        denied.pending = Dictionary(previouslyScheduled.map { ($0.identifier, $0) }, uniquingKeysWith: { first, _ in first })
        let validPendingIDs = Set(denied.pending.keys)
        deniedController.refreshForSystemChange()
        deniedDrained = false; deniedController.drainPendingWrites { deniedDrained = $0 }; check(wait { deniedDrained }, "Denied authorization can reconcile cancellations without adding reminders")
        check(Set(denied.pending.keys) == validPendingIDs && denied.additions == 0, "Valid future pending reminders survive permission revocation without re-requesting authorization")
        deniedController.delete(deniedController.events[0].id)
        check(wait { deniedController.events.isEmpty && denied.pending.isEmpty }, "Deleting while permission is denied removes the obsolete native identifier")
        check(denied.promptCount == 0 && denied.additions == 0, "Denied cleanup never prompts or submits replacement notifications")

        let scheduleFailure = CalendarNotificationFixture(); scheduleFailure.permission = .authorized
        scheduleFailure.reconciliationError = HUDCalendarError.notifications
        let scheduleFailureController = HUDCalendarController(loadStore: { try HUDCalendarStore(directory: folder.appendingPathComponent("ScheduleFailure")) },
            scheduler: scheduleFailure, now: { now }, timeZone: { utc })
        scheduleFailureController.setActive(true); check(wait { !scheduleFailureController.busy }, "Failed-reminder fixture loads independently")
        _ = scheduleFailureController.save(title: "Durably saved despite reminder error", details: "", day: tomorrow)
        check(wait { scheduleFailureController.events.count == 1 && scheduleFailureController.error != nil && !scheduleFailureController.busy }, "Reminder failure remains visible after a successful event commit")
        var scheduleFailureDrained: Bool?; scheduleFailureController.drainPendingWrites { scheduleFailureDrained = $0 }
        check(wait { scheduleFailureDrained != nil } && scheduleFailureDrained == true, "A scheduling failure does not block quitting after durable writes")
        scheduleFailureController.report(HUDCalendarError.invalidDate)
        scheduleFailureDrained = nil; scheduleFailureController.drainPendingWrites { scheduleFailureDrained = $0 }
        check(wait { scheduleFailureDrained != nil } && scheduleFailureDrained == true, "Transient date validation errors do not prevent a durable quit")
        scheduleFailureController.setActive(false)
        let writeFailureFolder = folder.appendingPathComponent("WriteFailure")
        let writeFailureController = HUDCalendarController(loadStore: { try HUDCalendarStore(directory: writeFailureFolder) },
            scheduler: CalendarNotificationFixture(), now: { now }, timeZone: { utc })
        writeFailureController.setActive(true); check(wait { !writeFailureController.busy }, "Failed-write fixture loads before simulating an external edit")
        try! FileManager.default.createDirectory(at: writeFailureFolder, withIntermediateDirectories: true)
        let externalBytes = Data("preserve external fixture".utf8)
        try! externalBytes.write(to: writeFailureFolder.appendingPathComponent("calendar.json"))
        _ = writeFailureController.save(title: "Must not overwrite", details: "", day: tomorrow)
        check(wait { writeFailureController.error != nil && !writeFailureController.busy }, "Compare-on-disk failure is surfaced separately from reminder status")
        var writeFailureDrained: Bool?; writeFailureController.drainPendingWrites { writeFailureDrained = $0 }
        check(wait { writeFailureDrained != nil } && writeFailureDrained == false, "An actual failed event write correctly fails the quit barrier")
        check(try! Data(contentsOf: writeFailureFolder.appendingPathComponent("calendar.json")) == externalBytes, "A failed write barrier leaves external calendar data intact")
        writeFailureController.setActive(false)

        let canvas = HUDCalendarCanvas(controller: controller), host = NSView(frame: CGRect(x: 0, y: 0, width: 400, height: 440))
        canvas.updateRenderScale(3)
        check(canvas.actions.isEmpty, "An unopened calendar defers date and control artwork when the HUD sets render scale")
        host.wantsLayer = true; host.layer?.addSublayer(canvas.layer)
        let interaction = HUDCalendarInteraction(canvas: canvas, host: host)
        interaction.project = { $0 }; interaction.unproject = { $0 }
        interaction.setPresented(true); interaction.setActive(true)
        func descendants(_ layer: CALayer) -> [CALayer] { [layer] + (layer.sublayers ?? []).flatMap(descendants) }
        func labels(_ layer: CALayer) -> [CATextLayer] { descendants(layer).compactMap { $0 as? CATextLayer } }
        for dark in [true, false] {
            _ = canvas.makeContent(for: .calendar, style: .init(dark: dark, accent: .systemGreen, contentsScale: 2))
            let todayPlate = descendants(canvas.layer).first { $0.name == "calendar.button.today" } as! CAShapeLayer
            let addPlate = descendants(canvas.layer).first { $0.name == "calendar.button.new" } as! CAShapeLayer
            let refresh = descendants(canvas.layer).first { $0.name == "calendar.refresh.arrow" } as! CAShapeLayer
            check(todayPlate.fillColor == HUDRuntimeAppearance.accent.withAlphaComponent(1).cgColor && addPlate.fillColor?.alpha == 1 && addPlate.path != nil,
                  "Today uses opaque theme color and Add uses a filled cut-corner plate in both themes")
            let color = NSColor(cgColor: addPlate.fillColor!)!.usingColorSpace(.deviceRGB)!
            check(color.redComponent >= 0.8 && refresh.path != nil && refresh.path!.boundingBoxOfPath.width > 10,
                  "Add stays light and Refresh uses a custom filled arrow instead of a text glyph")
        }
        check(!labels(canvas.layer).contains { ($0.string as? String) == L10n.text("Reminders: 09:00, the day before and on the date", "提醒：前一天及当天 09:00") },
              "Calendar omits the recurring reminder explanation from its footer")
        check(canvas.layer.bounds.size == CGSize(width: 400, height: 440) && canvas.monthCells.count == 31, "The retained calendar fits its HUD module and displays a complete month")
        check(canvas.actions.allSatisfy { $0.rect.maxY <= 413 }, "Calendar controls stay above the central battery bar")
        let previousMonth = canvas.month; canvas.perform("previousMonth")
        check(canvas.month.month == 9 && canvas.monthCells.count == 30, "Month browsing respects differing month lengths")
        canvas.perform("today"); check(canvas.month == previousMonth && canvas.selectedDay == today, "Today returns to the actual local date")
        check(wait { !controller.busy }, "UI fixture waits for the lifecycle reminder refresh")
        canvas.perform("new")
        check(interaction.secondaryMenu?.artwork.superlayer === canvas.layer, "Calendar event forms reuse the retained tilted menu plane")
        check(interaction.secondaryMenu?.items.allSatisfy { $0.rect.width == $0.rect.height } == true, "Calendar form controls use square shared-style buttons")
        let menuLabels = labels(interaction.secondaryMenu!.artwork)
        check(menuLabels.contains { ($0.string as? String) == "// " + L10n.text("Add event", "添加事项") }
                && menuLabels.contains { ($0.string as? String) == L10n.text("Date", "日期") }
                && !menuLabels.contains { ($0.string as? String)?.contains("YYYY-MM-DD") == true },
              "The event editor keeps its prefixed heading and plain Date label without a format descriptor")
        check(menuLabels.filter { ($0.string as? String) == "✓" }.allSatisfy { $0.alignmentMode == .center }
                && !menuLabels.contains { ($0.string as? String) == L10n.text("Reminders at 09:00 local time", "当地时间 09:00 提醒") },
              "Event confirmation glyphs are centered and the editor omits reminder explanation text")
        for field in ["title", "date", "details"] {
            check(interaction.editorForVerification(field) != nil, "Every calendar input uses the shared projected native editor")
        }
        let title = interaction.editorForVerification("title")!, date = interaction.editorForVerification("date")!, details = interaction.editorForVerification("details")!
        check(title.placeholder == L10n.text("Title", "标题") && title.scrollView.alphaValue == 0, "Projected calendar inputs have readable placeholders without native top-left glyph leakage")
        title.textView.string = String(repeating: "测", count: 130)
        interaction.textDidChange(Notification(name: NSText.didChangeNotification, object: title.textView))
        check(title.textView.string.count == 120, "Calendar titles are bounded without growing a hidden editor surface")
        title.textView.string = "UI fixture"; date.textView.string = tomorrow.string; details.textView.string = "Generated HUD input"
        let captureCount = title.captureCount; interaction.layoutAccessibility()
        check(title.captureCount == captureCount, "Tilt/accessibility layout does not recapture editor glyphs")
        interaction.secondaryMenu?.perform("save")
        check(wait { controller.events.count == 1 && interaction.secondaryMenu == nil }, "The form saves through the existing worker and closes with a finite transition")
        check(wait { !controller.busy }, "Saved event finishes reminder reconciliation")
        canvas.perform("day:" + tomorrow.string)
        check(labels(canvas.layer).contains { ($0.string as? String) == "UI fixture" && $0.alignmentMode == .left },
              "Event titles use left-aligned text in the selected date's list")
        canvas.perform("event:" + controller.events[0].id.uuidString)
        interaction.secondaryMenu?.perform("delete")
        check(controller.events.count == 1 && interaction.secondaryMenu?.items.contains { $0.id == "confirmDelete" } == true, "Event deletion requires a separate confirmation")
        interaction.secondaryMenu?.perform("cancelDelete")
        check(controller.events.count == 1, "Canceling event deletion leaves its data intact")
        interaction.secondaryMenu?.perform("delete"); interaction.secondaryMenu?.perform("confirmDelete")
        check(wait { controller.events.isEmpty && interaction.secondaryMenu == nil }, "Confirmed deletion updates the store and dismisses the editor")
        interaction.deactivate()
        check(!controller.active && !interaction.capturesPointer && interaction.editorForVerification("title") == nil, "Closing the module disposes editors without owning a polling timer")
        drained = false; controller.drainPendingWrites { drained = $0 }; check(wait { drained }, "The final UI fixture drains before temporary files are removed")
        deniedController.setActive(false)
        return count
    }
}

private final class CalendarNotificationFixture: HUDCalendarNotificationScheduling {
    var permission = HUDCalendarPermission.unknown
    var holdAuthorization = false
    var held: [(Bool, (HUDCalendarPermission) -> Void)] = []
    var authorizationCount = 0, promptCount = 0, reconciliations = 0, additions = 0
    var pending: [String: HUDCalendarReminder] = [:]
    var reconciliationError: Error?
    func authorization(request: Bool, completion: @escaping (HUDCalendarPermission) -> Void) {
        authorizationCount += 1
        if holdAuthorization { held.append((request, completion)); return }
        if request && permission == .unknown { promptCount += 1; permission = .authorized }
        completion(permission)
    }
    func releaseAuthorization(allow: Bool) {
        holdAuthorization = false
        let items = held; held = []
        for (request, completion) in items {
            if request && permission == .unknown { promptCount += 1; permission = allow ? .authorized : .denied }
            completion(permission)
        }
    }
    func cancelObsolete(keepingIDs: Set<String>, completion: @escaping () -> Void) {
        pending = pending.filter { keepingIDs.contains($0.key) }; completion()
    }
    func reconcile(_ reminders: [HUDCalendarReminder], zone: TimeZone, completion: @escaping (Result<Void, Error>) -> Void) {
        reconciliations += 1
        if let reconciliationError { completion(.failure(reconciliationError)); return }
        let next = Dictionary(reminders.map { ($0.identifier, $0) }, uniquingKeysWith: { first, _ in first })
        let effective = next.filter { pending[$0.key] != nil || !$0.value.wasScheduled }
        for (id, value) in effective where pending[id]?.signature != value.signature { additions += 1 }
        pending = effective; completion(.success(()))
    }
}
