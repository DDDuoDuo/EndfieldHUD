import AppKit
import UserNotifications

struct HUDCalendarReminder: Equatable {
    let identifier: String
    let eventID: UUID
    let title: String
    let day: HUDCalendarDay
    let date: Date
    let previousDay: Bool
    let catchUp: Bool
    let wasScheduled: Bool
    var signature: String { "\(Int(date.timeIntervalSince1970))|\(previousDay)|\(title)" }
    static func plan(events: [HUDCalendarEvent], now: Date, zone: TimeZone) -> [Self] {
        let calendar = HUDCalendarDay.calendar(zone)
        return events.flatMap { event -> [Self] in
            guard let onDate = event.day.date(in: zone, hour: 9), HUDCalendarDay(onDate, zone: zone) == event.day else { return [] }
            var result: [Self] = []
            if let prior = calendar.date(byAdding: .day, value: -1, to: onDate), prior > now {
                result.append(Self(identifier: HUDCalendarStore.notificationPrefix + event.id.uuidString + ".before", eventID: event.id,
                    title: event.title, day: event.day, date: prior, previousDay: true, catchUp: false, wasScheduled: event.beforeScheduledFor == event.day.string))
            }
            let date = event.catchUpDate ?? onDate
            if date > now {
                result.append(Self(identifier: HUDCalendarStore.notificationPrefix + event.id.uuidString + ".day", eventID: event.id,
                    title: event.title, day: event.day, date: date, previousDay: false, catchUp: event.catchUpDate != nil, wasScheduled: event.dayScheduledFor == event.day.string))
            }
            return result
        }.sorted { $0.date == $1.date ? $0.identifier < $1.identifier : $0.date < $1.date }
    }
}

enum HUDCalendarPermission { case unknown, authorized, denied, unavailable }
protocol HUDCalendarNotificationScheduling: AnyObject {
    func authorization(request: Bool, completion: @escaping (HUDCalendarPermission) -> Void)
    func reconcile(_ reminders: [HUDCalendarReminder], zone: TimeZone, completion: @escaping (Result<Void, Error>) -> Void)
    func cancelObsolete(keepingIDs: Set<String>, completion: @escaping () -> Void)
}

/// Own only Calendar identifiers. The existing app notification delegate keeps
/// ownership of foreground delivery/update actions; no second delegate/service.
final class HUDCalendarNativeNotifications: HUDCalendarNotificationScheduling {
    func authorization(request: Bool, completion: @escaping (HUDCalendarPermission) -> Void) {
        let center = UNUserNotificationCenter.current()
        center.getNotificationSettings { settings in
            switch settings.authorizationStatus {
            case .authorized, .provisional: completion(.authorized)
            case .notDetermined where request:
                center.requestAuthorization(options: [.alert, .sound]) { allowed, error in completion(error != nil ? .unavailable : allowed ? .authorized : .denied) }
            case .notDetermined: completion(.unknown)
            default: completion(.denied)
            }
        }
    }
    func cancelObsolete(keepingIDs: Set<String>, completion: @escaping () -> Void) {
        let center = UNUserNotificationCenter.current()
        center.getPendingNotificationRequests { pending in
            let obsolete = pending.map(\.identifier).filter { $0.hasPrefix(HUDCalendarStore.notificationPrefix) && !keepingIDs.contains($0) }
            if !obsolete.isEmpty { center.removePendingNotificationRequests(withIdentifiers: obsolete) }
            completion()
        }
    }
    func reconcile(_ reminders: [HUDCalendarReminder], zone: TimeZone, completion: @escaping (Result<Void, Error>) -> Void) {
        guard reminders.count <= 60 else { completion(.failure(HUDCalendarError.upcomingCapacity)); return }
        let center = UNUserNotificationCenter.current(), language = L10n.resolvedLanguage
        let calendarIdentity = String(describing: Calendar.autoupdatingCurrent.identifier)
        center.getPendingNotificationRequests { pending in
            let existing = Dictionary(pending.filter { $0.identifier.hasPrefix(HUDCalendarStore.notificationPrefix) }.map { ($0.identifier, $0) }, uniquingKeysWith: { first, _ in first })
            let wanted = Set(reminders.map(\.identifier))
            let obsolete = existing.keys.filter { !wanted.contains($0) }
            if !obsolete.isEmpty { center.removePendingNotificationRequests(withIdentifiers: Array(obsolete)) }
            let group = DispatchGroup(), lock = NSLock(); var firstError: Error?
            for reminder in reminders {
                // Missing after a successful earlier receipt means delivered or
                // dismissed. A backward clock/zone change must not replay it.
                if existing[reminder.identifier] == nil && reminder.wasScheduled { continue }
                let signature = reminder.signature + "|" + calendarIdentity + "|" + language.rawValue
                if existing[reminder.identifier]?.content.userInfo["calendarSignature"] as? String == signature { continue }
                let content = UNMutableNotificationContent(); content.title = reminder.title
                content.body = (reminder.previousDay ? L10n.text("Tomorrow", "明天", language: language) : L10n.text("Today", "今天", language: language)) + " · " + reminder.day.string
                content.sound = .default; content.userInfo = ["calendarSignature": signature, "calendarEventID": reminder.eventID.uuidString]
                let trigger: UNNotificationTrigger
                if reminder.catchUp { trigger = UNTimeIntervalNotificationTrigger(timeInterval: max(1, reminder.date.timeIntervalSinceNow), repeats: false) }
                else {
                    var calendar = Calendar.autoupdatingCurrent; calendar.timeZone = zone
                    var components = calendar.dateComponents([.era, .year, .month, .day, .hour, .minute, .second], from: reminder.date)
                    // Civil date/time matching follows the user's current zone
                    // even while the app is quit; an explicit zone refresh also
                    // reconciles signatures when the running app receives it.
                    components.calendar = nil; components.timeZone = nil
                    trigger = UNCalendarNotificationTrigger(dateMatching: components, repeats: false)
                }
                group.enter()
                center.add(UNNotificationRequest(identifier: reminder.identifier, content: content, trigger: trigger)) { error in
                    if let error { lock.lock(); if firstError == nil { firstError = error }; lock.unlock() }; group.leave()
                }
            }
            group.notify(queue: .global(qos: .utility)) { if let firstError { completion(.failure(firstError)) } else { completion(.success(())) } }
        }
    }
}

/// Used by every diagnostic/default standalone HUD. It never contacts macOS's
/// notification center, displays a permission prompt, or schedules a real alert.
final class HUDCalendarNoopNotifications: HUDCalendarNotificationScheduling {
    func cancelObsolete(keepingIDs: Set<String>, completion: @escaping () -> Void) { completion() }
    func authorization(request: Bool, completion: @escaping (HUDCalendarPermission) -> Void) { completion(.unavailable) }
    func reconcile(_ reminders: [HUDCalendarReminder], zone: TimeZone, completion: @escaping (Result<Void, Error>) -> Void) { completion(.success(())) }
}

final class HUDCalendarController {
    private let loadStore: () throws -> HUDCalendarStore
    private let hasStoredData: () -> Bool
    private let scheduler: HUDCalendarNotificationScheduling
    private let now: () -> Date, timeZone: () -> TimeZone
    private let observeSystemChanges: Bool
    private let queue = DispatchQueue(label: "EndfieldHUD.Calendar", qos: .utility)
    private let writes = DispatchGroup()
    // Accessed only on queue, including initialization and the first stat/read.
    private var store: Result<HUDCalendarStore, Error>?
    private var observers: [(NotificationCenter, NSObjectProtocol)] = []
    private var scheduling = false, needsSchedule = false, requestPermissionPending = false
    private var loaded = false
    private(set) var events: [HUDCalendarEvent] = []
    private(set) var active = false, busy = false
    private(set) var permission = HUDCalendarPermission.unknown
    private(set) var error: String?
    // Reminder/validation failures are visible without blocking a durable quit.
    private var persistenceError: Error?
    var onChange: (() -> Void)?, onEvent: ((String) -> Void)?
    var today: HUDCalendarDay { HUDCalendarDay(now(), zone: timeZone()) }
    var zone: TimeZone { timeZone() }
    var reminderStatus: String {
        switch permission {
        case .authorized: return L10n.text("Reminders: 09:00, the day before and on the date", "提醒：前一天及当天 09:00")
        case .denied: return L10n.text("Notifications are off in System Settings", "通知已在系统设置中关闭")
        case .unknown: return L10n.text("Allow notifications when adding your first event", "添加首个事项时允许通知")
        case .unavailable: return L10n.text("Notifications are unavailable", "通知暂不可用")
        }
    }
    convenience init() {
        let diagnostic = HUDCalendarStore.isDiagnostic || ProcessInfo.processInfo.processName.contains("Tests")
        let directory = diagnostic ? HUDCalendarStore.applicationDirectory(arguments: ["--ui-test"]) : HUDCalendarStore.applicationDirectory()
        self.init(loadStore: { try HUDCalendarStore(directory: directory) }, hasStoredData: { HUDCalendarStore.exists(at: directory) },
            scheduler: diagnostic ? HUDCalendarNoopNotifications() : HUDCalendarNativeNotifications(), observeSystemChanges: !diagnostic)
    }
    static func fixture() -> HUDCalendarController {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldHUD-Calendar-Fixture-" + UUID().uuidString)
        return HUDCalendarController(loadStore: { try HUDCalendarStore(directory: directory) }, hasStoredData: { false }, scheduler: HUDCalendarNoopNotifications())
    }
    init(loadStore: @escaping () throws -> HUDCalendarStore, hasStoredData: @escaping () -> Bool = { true },
         scheduler: HUDCalendarNotificationScheduling, now: @escaping () -> Date = Date.init,
         timeZone: @escaping () -> TimeZone = { .current }, observeSystemChanges: Bool = false) {
        self.loadStore = loadStore; self.hasStoredData = hasStoredData; self.scheduler = scheduler
        self.now = now; self.timeZone = timeZone; self.observeSystemChanges = observeSystemChanges
    }
    deinit { observers.forEach { $0.0.removeObserver($0.1) } }
    func startIfExisting() {
        guard !loaded, !busy else { return }; busy = true
        let exists = hasStoredData
        queue.async { [weak self] in
            let found = exists()
            Self.deliver { [weak self] in
                guard let self else { return }; self.busy = false
                if found || self.active { self.load() } else { self.onChange?() }
            }
        }
    }
    func setActive(_ value: Bool) {
        active = value
        if value {
            if !loaded { load() } else { reschedule(requestPermission: false); onChange?() }
            startObservers()
        } else if events.isEmpty { stopObservers() }
    }
    private func load() {
        guard !busy else { return }
        mutate({ _ in }) { [weak self] success in
            guard let self, success else { return }; self.loaded = true
            if !self.events.isEmpty { self.startObservers(); self.reschedule(requestPermission: false) }
        }
    }
    @discardableResult func save(title: String, details: String, day: HUDCalendarDay, id: UUID? = nil, completion: ((Bool) -> Void)? = nil) -> Bool {
        guard loaded, !busy else { return false }
        guard day.isValid, let atNine = day.date(in: zone, hour: 9), HUDCalendarDay(atNine, zone: zone) == day else { report(HUDCalendarError.invalidDate); return false }
        let title = String(title.trimmingCharacters(in: .whitespacesAndNewlines).prefix(120))
        guard !title.isEmpty else { return false }
        let timestamp = now(), zone = zone, previous = id.flatMap { key in events.first { $0.id == key } }
        if id != nil, previous == nil { report(HUDCalendarError.missing); return false }
        var event = previous ?? HUDCalendarEvent(title: title, details: "", day: day, created: timestamp, modified: timestamp)
        event.title = title; event.details = String(details.prefix(2000)); event.modified = timestamp
        if previous == nil || event.day != day {
            event.catchUpPending = day == today && timestamp >= atNine; event.catchUpDate = nil
        }
        event.day = day
        mutate({ try $0.save(event, now: timestamp, zone: zone) }) { [weak self] success in
            guard let self else { return }
            if success { self.onEvent?(previous == nil ? "created" : "edited"); self.startObservers(); self.reschedule(requestPermission: true) }
            completion?(success)
        }
        return true
    }
    func delete(_ id: UUID, completion: ((Bool) -> Void)? = nil) {
        guard loaded, !busy else { return }
        mutate({ try $0.remove(id) }) { [weak self] success in
            if success { self?.onEvent?("deleted"); self?.reschedule(requestPermission: false) }; completion?(success)
        }
    }
    func refreshReminders() { if loaded { reschedule(requestPermission: true) } else { load() } }
    func refreshForSystemChange() { guard loaded else { return }; reschedule(requestPermission: false); onChange?() }
    func report(_ failure: Error) { error = failure.localizedDescription; onChange?() }
    private func getStore() throws -> HUDCalendarStore {
        if let store { return try store.get() }; let value = Result { try loadStore() }; store = value; return try value.get()
    }
    private func mutate(_ operation: @escaping (HUDCalendarStore) throws -> Void, completion: ((Bool) -> Void)? = nil) {
        busy = true; error = nil; writes.enter(); onChange?()
        queue.async { [self] in
            let result = Result { () throws -> [HUDCalendarEvent] in let store = try self.getStore(); try operation(store); return store.events }
            Self.deliver { [self] in
                defer { self.writes.leave() }
                self.busy = false
                switch result {
                case .success(let events): self.persistenceError = nil; self.events = events; completion?(true)
                case .failure(let error): self.persistenceError = error; self.error = error.localizedDescription; completion?(false)
                }
                self.onChange?()
            }
        }
    }
    private func reschedule(requestPermission: Bool) {
        guard loaded else { return }
        if scheduling { needsSchedule = true; requestPermissionPending = requestPermissionPending || requestPermission; return }
        scheduling = true; writes.enter()
        scheduler.authorization(request: requestPermission && !events.isEmpty) { [weak self] permission in
            Self.deliver { [weak self] in
                guard let self else { return }; self.permission = permission; self.onChange?()
                guard permission == .authorized else {
                    // Revoking authorization must not keep a deleted event's
                    // request alive, but valid future requests/receipts survive.
                    let ids = Set(HUDCalendarReminder.plan(events: self.events, now: self.now(), zone: self.zone).map(\.identifier))
                    self.scheduler.cancelObsolete(keepingIDs: ids) { [weak self] in
                        Self.deliver { [weak self] in self?.finishSchedule() }
                    }
                    return
                }
                let timestamp = self.now(), zone = self.zone
                self.mutate({ try $0.claimCatchUps(now: timestamp, zone: zone) }) { [weak self] success in
                    guard let self else { return }; guard success else { self.finishSchedule(); return }
                    let plan = HUDCalendarReminder.plan(events: self.events, now: self.now(), zone: zone)
                    self.scheduler.reconcile(plan, zone: zone) { [weak self] result in
                        Self.deliver { [weak self] in
                            guard let self else { return }
                            switch result {
                            case .failure(let error): self.error = error.localizedDescription; self.finishSchedule(); self.onChange?()
                            case .success:
                                self.mutate({ try $0.recordScheduled(plan) }) { [weak self] _ in self?.finishSchedule() }
                            }
                        }
                    }
                }
            }
        }
    }
    private func finishSchedule() {
        scheduling = false
        if needsSchedule {
            let request = requestPermissionPending; needsSchedule = false; requestPermissionPending = false
            reschedule(requestPermission: request)
        }
        writes.leave()
    }
    func drainPendingWrites(timeout: TimeInterval = 3, completion: @escaping (Bool) -> Void) {
        var finished = false, timer: Timer?
        let finish: (Bool) -> Void = { success in guard !finished else { return }; finished = true; timer?.invalidate(); timer = nil; completion(success) }
        writes.notify(queue: queue) { [weak self] in Self.deliver { finish(self?.persistenceError == nil) } }
        let deadline = Timer(timeInterval: max(0.01, timeout), repeats: false) { _ in finish(false) }; timer = deadline
        RunLoop.main.add(deadline, forMode: .common); RunLoop.main.add(deadline, forMode: .modalPanel)
    }
    private func startObservers() {
        guard observeSystemChanges, observers.isEmpty else { return }
        let local = NotificationCenter.default
        for name in [Notification.Name.NSSystemTimeZoneDidChange, .NSCalendarDayChanged,
                     Notification.Name("NSSystemClockDidChangeNotification"), NSLocale.currentLocaleDidChangeNotification, NSApplication.didBecomeActiveNotification] {
            observers.append((local, local.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in self?.refreshForSystemChange() }))
        }
        let workspace = NSWorkspace.shared.notificationCenter
        observers.append((workspace, workspace.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { [weak self] _ in self?.refreshForSystemChange() }))
    }
    private func stopObservers() { observers.forEach { $0.0.removeObserver($0.1) }; observers = [] }
    private static func deliver(_ block: @escaping () -> Void) { RunLoop.main.perform(inModes: [.common, .modalPanel], block: block) }
}
