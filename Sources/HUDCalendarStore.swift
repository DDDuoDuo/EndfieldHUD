import Foundation

struct HUDCalendarDay: Codable, Equatable, Hashable, Comparable {
    var year: Int, month: Int, day: Int
    static func < (lhs: Self, rhs: Self) -> Bool { (lhs.year, lhs.month, lhs.day) < (rhs.year, rhs.month, rhs.day) }
    var isValid: Bool {
        guard (1900...9999).contains(year), (1...12).contains(month), (1...31).contains(day),
              let value = date(in: TimeZone(secondsFromGMT: 0)!) else { return false }
        return Self(value, zone: TimeZone(secondsFromGMT: 0)!) == self
    }
    init(year: Int, month: Int, day: Int) { self.year = year; self.month = month; self.day = day }
    init(_ value: Date, zone: TimeZone = .current) {
        let parts = Self.calendar(zone).dateComponents([.year, .month, .day], from: value)
        year = parts.year ?? 2000; month = parts.month ?? 1; day = parts.day ?? 1
    }
    static func calendar(_ zone: TimeZone) -> Calendar {
        var value = Calendar(identifier: .gregorian); value.timeZone = zone; value.locale = .autoupdatingCurrent
        value.firstWeekday = Calendar.autoupdatingCurrent.firstWeekday; return value
    }
    func date(in zone: TimeZone, hour: Int = 12, minute: Int = 0) -> Date? {
        let cal = Self.calendar(zone)
        return cal.date(from: DateComponents(calendar: cal, timeZone: zone, year: year, month: month, day: day, hour: hour, minute: minute))
    }
    func advanced(_ days: Int, zone: TimeZone) -> Self? {
        guard let date = date(in: zone), let value = Self.calendar(zone).date(byAdding: .day, value: days, to: date) else { return nil }
        return Self(value, zone: zone)
    }
    var string: String { String(format: "%04d-%02d-%02d", year, month, day) }
    static func parse(_ text: String) -> Self? {
        let pieces = text.split(separator: "-", omittingEmptySubsequences: false)
        guard pieces.count == 3, let y = Int(pieces[0]), let m = Int(pieces[1]), let d = Int(pieces[2]) else { return nil }
        let result = Self(year: y, month: m, day: d); return result.isValid ? result : nil
    }
}

struct HUDCalendarEvent: Codable, Equatable, Identifiable {
    var id = UUID()
    var title: String
    var details: String
    var day: HUDCalendarDay
    var created: Date
    var modified: Date
    // A same-day event created after the regular 09:00 reminder gets one
    // durable catch-up reservation. Reopening never invents another alert.
    var catchUpPending = false
    var catchUpDate: Date?
    var beforeScheduledFor: String?
    var dayScheduledFor: String?
    var isValid: Bool { !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && title.count <= 120
        && details.count <= 2000 && !title.contains("\0") && !details.contains("\0") && day.isValid
        && created.timeIntervalSinceReferenceDate.isFinite && modified.timeIntervalSinceReferenceDate.isFinite
        && (catchUpDate?.timeIntervalSinceReferenceDate.isFinite ?? true)
        && (beforeScheduledFor.map { HUDCalendarDay.parse($0) != nil } ?? true)
        && (dayScheduledFor.map { HUDCalendarDay.parse($0) != nil } ?? true) }
}

enum HUDCalendarError: Error, LocalizedError {
    case invalidData, changedOnDisk, capacity, upcomingCapacity, invalidDate, missing, notifications
    var errorDescription: String? {
        switch self {
        case .invalidData: return L10n.text("Calendar data could not be read. Your file was preserved.", "无法读取日历数据，原文件已保留。")
        case .changedOnDisk: return L10n.text("Calendar changed outside this app. Restart before saving.", "日历文件已被其他程序修改，请重启后再保存。")
        case .capacity: return L10n.text("The calendar can keep up to 256 events.", "日历最多可保存 256 个事项。")
        case .upcomingCapacity: return L10n.text("Keep up to 30 upcoming events so every reminder can be scheduled.", "最多保留 30 个待办日期事项，以确保每条提醒都能安排。")
        case .invalidDate: return L10n.text("Enter a valid date as YYYY-MM-DD.", "请输入有效日期，格式为 YYYY-MM-DD。")
        case .missing: return L10n.text("This event is no longer available.", "此事项已不存在。")
        case .notifications: return L10n.text("Reminders could not be scheduled. Try again.", "无法安排提醒，请重试。")
        }
    }
}

/// Local-only calendar; never opens EventKit or edits macOS Calendar. The worker
/// owning this store serializes compare-before-atomic-write transactions.
final class HUDCalendarStore {
    private struct File: Codable { var version = 1; var events: [HUDCalendarEvent] }
    private(set) var events: [HUDCalendarEvent] = []
    private let url: URL
    private var persisted: Data?
    static let maximumEvents = 256, maximumUpcoming = 30
    static let maximumBytes = 3 * 1024 * 1024
    static let notificationPrefix = "EndfieldHUD.Calendar."
    private static let diagnosticDirectory = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldHUD-Calendar-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString)")
    static var isDiagnostic: Bool { CommandLine.arguments.contains { $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-") } }
    static func applicationDirectory(arguments: [String] = CommandLine.arguments) -> URL {
        if arguments.contains(where: { $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-") }) { return diagnosticDirectory }
        return (FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support"))
            .appendingPathComponent("EndfieldCharge/Calendar")
    }
    static func exists(at directory: URL) -> Bool { FileManager.default.fileExists(atPath: directory.appendingPathComponent("calendar.json").path) }
    init(directory: URL) throws {
        url = directory.appendingPathComponent("calendar.json")
        if let bytes = try read() {
            guard let file = try? JSONDecoder().decode(File.self, from: bytes), file.version == 1, Self.valid(file.events) else { throw HUDCalendarError.invalidData }
            events = file.events; persisted = bytes
        }
    }
    func save(_ event: HUDCalendarEvent, now: Date, zone: TimeZone) throws {
        guard event.isValid else { throw HUDCalendarError.invalidData }
        var next = events
        if let index = next.firstIndex(where: { $0.id == event.id }) { next[index] = event }
        else { guard next.count < Self.maximumEvents else { throw HUDCalendarError.capacity }; next.append(event) }
        let today = HUDCalendarDay(now, zone: zone)
        guard next.filter({ $0.day >= today }).count <= Self.maximumUpcoming else { throw HUDCalendarError.upcomingCapacity }
        try commit(next)
    }
    func remove(_ id: UUID) throws { try commit(events.filter { $0.id != id }) }
    func claimCatchUps(now: Date, zone: TimeZone) throws {
        let today = HUDCalendarDay(now, zone: zone)
        let next = events.map { item -> HUDCalendarEvent in
            guard item.catchUpPending, item.day <= today else { return item }
            var value = item; value.catchUpPending = false
            if value.day == today { value.catchUpDate = now.addingTimeInterval(3) }
            return value
        }
        if next != events { try commit(next) }
    }
    func recordScheduled(_ reminders: [HUDCalendarReminder]) throws {
        var next = events
        for reminder in reminders {
            guard let index = next.firstIndex(where: { $0.id == reminder.eventID && $0.day == reminder.day }) else { continue }
            if reminder.previousDay { next[index].beforeScheduledFor = reminder.day.string }
            else { next[index].dayScheduledFor = reminder.day.string }
        }
        if next != events { try commit(next) }
    }
    private static func valid(_ events: [HUDCalendarEvent]) -> Bool { events.count <= maximumEvents && events.allSatisfy(\.isValid) && Set(events.map(\.id)).count == events.count }
    private func commit(_ events: [HUDCalendarEvent]) throws {
        guard Self.valid(events) else { throw HUDCalendarError.invalidData }
        guard try read() == persisted else { throw HUDCalendarError.changedOnDisk }
        let data = try JSONEncoder().encode(File(events: events))
        guard data.count <= Self.maximumBytes else { throw HUDCalendarError.capacity }
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url, options: .atomic); self.events = events; persisted = data
    }
    private func read() throws -> Data? {
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        let handle = try FileHandle(forReadingFrom: url); defer { try? handle.close() }
        let data = try handle.read(upToCount: Self.maximumBytes + 1) ?? Data()
        guard data.count <= Self.maximumBytes else { throw HUDCalendarError.capacity }; return data
    }
}
