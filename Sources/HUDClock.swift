import Foundation

struct HUDClockReading: Equatable {
    let time: String
    let date: String
}

protocol HUDClockTimer: AnyObject { func invalidate() }
extension Timer: HUDClockTimer {}

/// A visible-only wall clock. Formatters and one one-second timer are retained;
/// pointer motion and display refresh never request additional clock samples.
final class HUDClock {
    typealias ScheduleTimer = (_ interval: TimeInterval, _ action: @escaping () -> Void) -> HUDClockTimer
    var onChange: ((HUDClockReading) -> Void)?
    private let now: () -> Date
    private let timeZone: () -> TimeZone
    private let scheduleTimer: ScheduleTimer
    private let timeFormatter = DateFormatter()
    private let dateFormatter = DateFormatter()
    private var timer: HUDClockTimer?
    private var generation = 0
    private(set) var isActive = false
    private(set) var format: HUDClockFormat = .twentyFourHour
    private(set) var reading: HUDClockReading?

    init(now: @escaping () -> Date = Date.init,
         timeZone: @escaping () -> TimeZone = { .autoupdatingCurrent },
         scheduleTimer: ScheduleTimer? = nil) {
        self.now = now; self.timeZone = timeZone
        self.scheduleTimer = scheduleTimer ?? { interval, action in
            let timer = Timer(timeInterval: interval, repeats: true) { _ in action() }
            timer.tolerance = 0.05
            RunLoop.main.add(timer, forMode: .common)
            return timer
        }
        for formatter in [timeFormatter, dateFormatter] {
            formatter.locale = Locale(identifier: "en_US_POSIX")
            formatter.calendar = Calendar(identifier: .gregorian)
        }
        timeFormatter.dateFormat = "HH:mm:ss"
        dateFormatter.dateFormat = "EEE MMM d"
    }

    deinit { timer?.invalidate() }

    func setFormat(_ value: HUDClockFormat) {
        guard format != value else { return }
        format = value
        timeFormatter.dateFormat = value == .twentyFourHour ? "HH:mm:ss" : "hh:mm:ss a"
        if isActive { refresh() }
    }

    func setActive(_ value: Bool) {
        guard isActive != value else { return }
        isActive = value; generation += 1
        timer?.invalidate(); timer = nil
        guard value else { return }
        refresh()
        let token = generation
        timer = scheduleTimer(1) { [weak self] in
            guard let self, self.isActive, self.generation == token else { return }
            self.refresh()
        }
    }

    private func refresh() {
        let date = now(), zone = timeZone()
        timeFormatter.timeZone = zone; dateFormatter.timeZone = zone
        let rawDate = dateFormatter.string(from: date)
        let separator = rawDate.firstIndex(of: " ") ?? rawDate.endIndex
        let next = HUDClockReading(time: timeFormatter.string(from: date),
            date: rawDate[..<separator].uppercased() + String(rawDate[separator...]))
        guard next != reading else { return }
        reading = next; onChange?(next)
    }
}
