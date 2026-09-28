import Foundation

enum HUDClockTests {
    private final class TestTimer: HUDClockTimer {
        let interval: TimeInterval
        let action: () -> Void
        private(set) var invalidated = false
        init(_ interval: TimeInterval, _ action: @escaping () -> Void) { self.interval = interval; self.action = action }
        func invalidate() { invalidated = true }
    }

    static func run() -> Int {
        var assertions = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) {
            assertions += 1; precondition(condition(), message)
        }
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(secondsFromGMT: 0)!
        var instant = calendar.date(from: DateComponents(year: 2026, month: 9, day: 27, hour: 22, minute: 5, second: 54))!
        var zone = TimeZone(secondsFromGMT: 0)!
        var reads = 0, timers: [TestTimer] = [], updates: [HUDClockReading] = []
        var clock: HUDClock? = HUDClock(now: { reads += 1; return instant }, timeZone: { zone }, scheduleTimer: { interval, action in
            let timer = TestTimer(interval, action); timers.append(timer); return timer
        })
        clock?.onChange = { updates.append($0) }
        check(reads == 0 && timers.isEmpty, "A detached clock does not sample or schedule work")
        clock?.setActive(true)
        check(clock?.reading == HUDClockReading(time: "22:05:54", date: "SUN Sep 27"), "Clock uses the requested English 24-hour format")
        check(timers.count == 1 && timers[0].interval == 1 && reads == 1, "Opening updates immediately and installs exactly one 1 Hz timer")
        clock?.setActive(true)
        check(timers.count == 1 && reads == 1, "Repeated visible lifecycle updates cannot duplicate the timer")
        instant += 1; timers[0].action()
        check(clock?.reading?.time == "22:05:55" && updates.count == 2, "A timer sample advances seconds")
        timers[0].action()
        check(updates.count == 2, "Unchanged wall-clock text is not redrawn")
        clock?.setFormat(.twelveHour)
        check(clock?.reading?.time == "10:05:55 PM" && timers.count == 1, "A format change updates immediately without replacing the timer")
        instant = calendar.date(from: DateComponents(year: 2026, month: 9, day: 28, hour: 0, minute: 0, second: 0))!
        timers[0].action()
        check(clock?.reading == HUDClockReading(time: "12:00:00 AM", date: "MON Sep 28"), "Midnight advances the date and uses 12 AM")
        instant += 12 * 60 * 60; timers[0].action()
        check(clock?.reading?.time == "12:00:00 PM", "Noon uses 12 PM")
        zone = TimeZone(secondsFromGMT: -7 * 3600)!; timers[0].action()
        check(clock?.reading?.time == "05:00:00 AM", "A live time-zone change applies on the next tick")
        clock?.setActive(false)
        let stoppedReads = reads
        check(timers[0].invalidated && clock?.isActive == false, "Closing invalidates the clock timer")
        clock?.setFormat(.twentyFourHour); timers[0].action()
        check(reads == stoppedReads, "Hidden settings changes and stale callbacks cannot sample the clock")
        instant += 3600; clock?.setActive(true)
        check(clock?.reading?.time == "06:00:00" && timers.count == 2, "Reopening refreshes current wall time and remembered format")
        let reopenedReads = reads; timers[0].action()
        check(reads == reopenedReads, "A callback queued before close cannot act on the reopened clock")
        clock = nil
        check(timers[1].invalidated, "Releasing the HUD releases its timer")
        return assertions
    }
}
