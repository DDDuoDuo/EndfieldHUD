import AppKit

/// Timer ownership is injectable so lifecycle and the cheap change-count gate
/// can be verified without sleeping, touching the general clipboard or running
/// a GUI event loop.
protocol ClipboardWatcherTimer: AnyObject {
    func invalidate()
}

extension Timer: ClipboardWatcherTimer {}

final class ClipboardWatcher {
    typealias ScheduleTimer = (_ interval: TimeInterval, _ tolerance: TimeInterval,
                              _ tick: @escaping () -> Void) -> ClipboardWatcherTimer

    let store: ClipboardStore
    let pasteboard: NSPasteboard
    private let capture: (NSPasteboard) -> Bool
    private let scheduleTimer: ScheduleTimer
    private var timer: ClipboardWatcherTimer?
    private var lastChangeCount: Int?
    private var hasStarted = false
    private(set) var isRunning = false
    private(set) var isSuspended = false

    init(store: ClipboardStore, pasteboard: NSPasteboard = .general,
         capture: ((NSPasteboard) -> Bool)? = nil,
         scheduleTimer: ScheduleTimer? = nil) {
        precondition(Thread.isMainThread)
        self.store = store
        self.pasteboard = pasteboard
        self.capture = capture ?? { store.capture(from: $0) }
        self.scheduleTimer = scheduleTimer ?? Self.scheduleOnMainRunLoop
    }

    func start() {
        precondition(Thread.isMainThread)
        guard !isRunning else { return }
        isRunning = true
        if !hasStarted {
            // Only copies made during this app session belong in its cache.
            lastChangeCount = pasteboard.changeCount
            hasStarted = true
        } else {
            checkForChanges()
        }
        installTimerIfNeeded()
    }

    func stop() {
        precondition(Thread.isMainThread)
        isRunning = false
        removeTimer()
    }

    func setSuspended(_ value: Bool) {
        precondition(Thread.isMainThread)
        guard isSuspended != value else { return }
        isSuspended = value
        if value { removeTimer() }
        else {
            checkForChanges()
            installTimerIfNeeded()
        }
    }

    func checkForChanges() {
        precondition(Thread.isMainThread)
        guard isRunning, !isSuspended else { return }
        let count = pasteboard.changeCount
        guard count != lastChangeCount else { return }
        // Acknowledge before notifying the store's observers so a reentrant
        // check cannot deserialize the same clipboard contents a second time.
        lastChangeCount = count
        _ = capture(pasteboard)
    }

    @discardableResult func copy(id: UUID) -> Bool {
        precondition(Thread.isMainThread)
        // Copy the selected cached entry before any other capture can evict it.
        // Immediately acknowledging this write prevents self-generated entries
        // or history reordering on the next timer tick, restart or resume.
        // An external copy between the last tick and this explicit action can
        // consequently be replaced before entering the cache.
        guard store.copy(id: id, to: pasteboard) else { return false }
        lastChangeCount = pasteboard.changeCount
        return true
    }

    private func installTimerIfNeeded() {
        guard isRunning, !isSuspended, timer == nil else { return }
        timer = scheduleTimer(1, 0.25) { [weak self] in self?.checkForChanges() }
    }

    private func removeTimer() {
        timer?.invalidate()
        timer = nil
    }

    private static func scheduleOnMainRunLoop(interval: TimeInterval, tolerance: TimeInterval,
                                            tick: @escaping () -> Void) -> ClipboardWatcherTimer {
        let timer = Timer(timeInterval: interval, repeats: true) { _ in tick() }
        timer.tolerance = tolerance
        RunLoop.main.add(timer, forMode: .common)
        return timer
    }

    deinit { timer?.invalidate() }
}
