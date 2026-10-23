import AppKit

// The Mac 1.3.0 Calendar model executed unchanged (HUDCalendarStore, HUDCalendarReminder.plan, Swift Character
// rules and HUDCalendarDay parsing): reminder plans across time-zone edges, the durable catch-up claim and
// receipts, text limits and dates. The canvas (HUDCalendarCanvas) has its own oracle
// (tools/work_mode_calendar_reference.*). Injected fixed clock and civil zones only; no application activation,
// window, notification centre or personal data.
@main enum CalendarReference {
    static let fixed = HUDCalendarDay(year: 2026, month: 10, day: 4)
    static let utc = TimeZone(secondsFromGMT: 0)!
    static let now = fixed.date(in: utc, hour: 10)!
    static func json<T: Encodable>(_ value: T) throws -> Any { try JSONSerialization.jsonObject(with: JSONEncoder().encode(value)) }
    static func event(_ n: Int, day: HUDCalendarDay = fixed) -> HUDCalendarEvent {
        HUDCalendarEvent(id: UUID(uuidString: String(format: "C0000000-0000-4000-8000-%012d", n))!, title: "Synthetic event \(n)",
                         details: "Owned fixture only", day: day, created: now.addingTimeInterval(Double(n)), modified: now)
    }
    static func run() throws {
        guard CommandLine.arguments.count == 2, ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil else { throw HUDCalendarError.invalidData }
        let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        NSApplication.shared.setActivationPolicy(.prohibited)
        L10n.language = .english
        let temp = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldCalendarReference-" + UUID().uuidString, isDirectory: true)
        defer { try? FileManager.default.removeItem(at: temp) }
        var plans: [[String: Any]] = []
        for (name, day, zone) in [("spring", HUDCalendarDay(year: 2026, month: 3, day: 8), "America/Los_Angeles"), ("fall", .init(year: 2026, month: 11, day: 1), "America/Los_Angeles"),
                                  ("china", .init(year: 2026, month: 11, day: 1), "Asia/Shanghai"), ("skipped-day", .init(year: 2011, month: 12, day: 30), "Pacific/Apia")] {
            let timestamp = HUDCalendarDay(year: 2000, month: 1, day: 1).date(in: utc)!, e = event(20, day: day), tz = TimeZone(identifier: zone)!
            let values = HUDCalendarReminder.plan(events: [e], now: timestamp, zone: tz)
            plans.append(["name": name, "zone": zone, "now": timestamp.timeIntervalSinceReferenceDate, "event": try json(e),
                          "reminders": values.map { ["identifier": $0.identifier, "eventID": $0.eventID.uuidString, "title": $0.title, "day": try! json($0.day),
                                                     "date": $0.date.timeIntervalSinceReferenceDate, "previousDay": $0.previousDay, "catchUp": $0.catchUp,
                                                     "wasScheduled": $0.wasScheduled, "signature": $0.signature] }])
        }
        let store = try HUDCalendarStore(directory: temp)
        var catchUp = event(30); catchUp.catchUpPending = true
        try store.save(catchUp, now: now, zone: utc); try store.claimCatchUps(now: now, zone: utc)
        let afterClaim = store.events, plan = HUDCalendarReminder.plan(events: afterClaim, now: now, zone: utc)
        try store.recordScheduled(plan)
        let scalars = ["  \n标题🙂\n ", String(repeating: "e\u{301}", count: 125), String(repeating: "👩‍👩‍👧‍👦", count: 121), String(repeating: "한", count: 2003), "\u{200b}hi\u{200b}"]
        let texts = scalars.enumerated().map { ["id": $0.offset, "text": $0.element, "count": $0.element.count, "trimmed": $0.element.trimmingCharacters(in: .whitespacesAndNewlines),
                                                "prefix120": String($0.element.prefix(120)), "prefix2000": String($0.element.prefix(2000))] }
        let dates = ["2024-02-29", "2025-02-29", "2026-10-4", "+2026-+10-+4", "1900-01-01", "9999-12-31", "1899-12-31", "2026-13-01", " 2026-10-04", "2026--04"]
        let document: [String: Any] = ["schemaVersion": 2, "reminderPlans": plans, "catchUpNow": now.timeIntervalSinceReferenceDate, "afterClaim": try json(afterClaim),
                                       "afterReceipts": try json(store.events), "textCases": texts,
                                       "dateCases": dates.map { ["text": $0, "parsed": HUDCalendarDay.parse($0).map { try! json($0) } as Any? ?? NSNull()] },
                                       "windowCreated": !NSApp.windows.isEmpty]
        try JSONSerialization.data(withJSONObject: document, options: [.sortedKeys, .withoutEscapingSlashes]).write(to: output.appendingPathComponent("calendar-reference.json"), options: .atomic)
        guard NSApp.windows.isEmpty else { throw HUDCalendarError.invalidData }
    }
    static func main() { do { try run(); print("PASS original Calendar model reference") } catch { fputs("Calendar reference failed: \(error)\n", stderr); exit(1) } }
}
