import Foundation
import Darwin

enum WorkModeKind: String { case countdown, stopwatch }
enum WorkModePhase: String { case idle, running, paused, stopped, completed }

struct WorkModeSnapshot: Equatable {
    let kind: WorkModeKind
    let phase: WorkModePhase
    let duration: TimeInterval
    let elapsed: TimeInterval
    var remaining: TimeInterval { kind == .countdown ? max(0, duration - elapsed) : 0 }
    var progress: Double {
        kind == .countdown ? min(1, max(0, elapsed / duration)) : elapsed.truncatingRemainder(dividingBy: 60) / 60
    }
    var isActive: Bool { phase == .running || phase == .paused }
    var displayedSeconds: Int {
        // Subtracting continuous-clock Doubles can land a fraction of a
        // microsecond either side of an exact second after pause/resume.
        Int(min(Double(Int.max / 2), max(0, kind == .countdown ? ceil(remaining - 0.000001) : floor(elapsed + 0.000001))))
    }
    var timeText: String {
        let seconds = displayedSeconds
        if seconds >= 3600 { return String(format: "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60) }
        return String(format: "%02d:%02d", seconds / 60, seconds % 60)
    }
}

enum WorkModeDuration {
    /// Bare numbers are minutes; mm:ss also permits precise second durations.
    static func parse(_ input: String) -> TimeInterval? {
        let text = input.trimmingCharacters(in: .whitespacesAndNewlines)
        let parts = text.split(separator: ":", omittingEmptySubsequences: false)
        let seconds: Double
        if parts.count == 2 {
            guard let minutes = Double(parts[0]), minutes.isFinite, minutes >= 0, minutes.rounded() == minutes,
                  let remainder = Double(parts[1]), remainder.isFinite, remainder >= 0, remainder < 60,
                  remainder.rounded() == remainder else { return nil }
            seconds = minutes * 60 + remainder
        } else if parts.count == 1, let minutes = Double(text.replacingOccurrences(of: ",", with: ".")), minutes.isFinite {
            seconds = minutes * 60
        } else { return nil }
        guard seconds.isFinite, seconds >= 1, seconds <= 86_400 else { return nil }
        return seconds.rounded()
    }

    static func editText(_ seconds: TimeInterval) -> String {
        let value = Int(min(86_400, max(1, seconds.rounded())))
        return String(format: "%d:%02d", value / 60, value % 60)
    }
}

protocol WorkModeTimer: AnyObject { func invalidate() }
extension Timer: WorkModeTimer {}

/// Timing uses the monotonic continuous clock, including time spent asleep.
/// Hidden stopwatches have no timer. Hidden countdowns retain only one finish
/// deadline; one-second text updates exist solely while this module is visible.
final class WorkModeController {
    typealias ScheduleTimer = (_ interval: TimeInterval, _ repeats: Bool, _ tolerance: TimeInterval,
                              _ action: @escaping () -> Void) -> WorkModeTimer
    private let clock: () -> TimeInterval
    private let scheduleTimer: ScheduleTimer
    private var kind: WorkModeKind = .countdown
    private var phase: WorkModePhase = .idle
    private var duration: TimeInterval = 30 * 60
    private var accumulated: TimeInterval = 0
    private var startedAt: TimeInterval?
    private var visible = false
    private var suspended = false
    private var displayTimer: WorkModeTimer?
    private var completionTimer: WorkModeTimer?
    private var displayGeneration = 0
    private var completionGeneration = 0
    private var observers: [UUID: () -> Void] = [:]
    private var publishedSecond: Int?
    private var trackedTotal: TimeInterval = 0
    private var trackedSessionElapsed: TimeInterval = 0
    private var restoredTrackedTime = false
    private(set) var revision = 0
    var isSuspended: Bool { suspended }
    /// Checkpoints report an absolute lifetime total. A failed persistence write
    /// may be retried at the next checkpoint without counting a session twice.
    var onTrackedWorkSecondsChanged: ((TimeInterval) -> Void)?

    var trackedWorkSeconds: TimeInterval {
        let delta = max(0, snapshot.elapsed - trackedSessionElapsed)
        let total = trackedTotal + delta
        return total.isFinite ? total : Double.greatestFiniteMagnitude
    }

    @discardableResult func restoreTrackedWorkSeconds(_ seconds: TimeInterval) -> Bool {
        precondition(Thread.isMainThread)
        guard !restoredTrackedTime, phase == .idle, accumulated == 0,
              trackedTotal == 0, seconds.isFinite, seconds >= 0 else { return false }
        trackedTotal = seconds
        restoredTrackedTime = true
        return true
    }

    init(clock: (() -> TimeInterval)? = nil, scheduleTimer: ScheduleTimer? = nil) {
        self.clock = clock ?? Self.continuousTime
        self.scheduleTimer = scheduleTimer ?? Self.scheduleOnMainRunLoop
    }

    var snapshot: WorkModeSnapshot {
        let interval = startedAt.map { max(0, clock() - $0) } ?? 0
        let elapsed = max(0, accumulated + (interval.isFinite ? interval : 0))
        return WorkModeSnapshot(kind: kind, phase: phase, duration: duration,
                                elapsed: kind == .countdown ? min(duration, elapsed) : elapsed)
    }

    @discardableResult func observe(_ callback: @escaping () -> Void) -> UUID {
        precondition(Thread.isMainThread)
        let id = UUID(); observers[id] = callback; return id
    }
    func removeObserver(_ id: UUID) { precondition(Thread.isMainThread); observers.removeValue(forKey: id) }

    @discardableResult func chooseCountdown(seconds: TimeInterval) -> Bool {
        precondition(Thread.isMainThread)
        guard seconds.isFinite, seconds >= 1, seconds <= 86_400 else { return false }
        checkpointWorkTime()
        kind = .countdown; duration = seconds.rounded(); accumulated = 0; startedAt = nil; phase = .idle
        trackedSessionElapsed = 0
        changed(); return true
    }

    func chooseStopwatch() {
        precondition(Thread.isMainThread)
        checkpointWorkTime()
        kind = .stopwatch; accumulated = 0; startedAt = nil; phase = .idle
        trackedSessionElapsed = 0
        changed()
    }

    func start() {
        precondition(Thread.isMainThread)
        guard phase != .running else { return }
        if phase == .paused { resume(); return }
        checkpointWorkTime()
        accumulated = 0; startedAt = clock(); phase = .running
        trackedSessionElapsed = 0
        changed()
    }

    func pause() {
        precondition(Thread.isMainThread)
        guard phase == .running else { return }
        refresh()
        guard phase == .running else { return }
        checkpointWorkTime()
        accumulated = snapshot.elapsed; startedAt = nil; phase = .paused
        changed()
    }

    func resume() {
        precondition(Thread.isMainThread)
        guard phase == .paused else { return }
        startedAt = clock(); phase = .running
        changed()
    }

    func stop() {
        precondition(Thread.isMainThread)
        guard phase == .running || phase == .paused else { return }
        checkpointWorkTime()
        accumulated = snapshot.elapsed; startedAt = nil; phase = .stopped
        changed()
    }

    func reset() {
        precondition(Thread.isMainThread)
        checkpointWorkTime()
        accumulated = 0; startedAt = nil; phase = .idle
        trackedSessionElapsed = 0
        changed()
    }

    func setVisible(_ value: Bool) {
        precondition(Thread.isMainThread)
        guard visible != value else { return }
        visible = value
        refresh()
        if !value { checkpointWorkTime() }
        reconcileTimers()
        if value { publish(force: true) }
    }

    func setSuspended(_ value: Bool) {
        precondition(Thread.isMainThread)
        guard suspended != value else { return }
        if value { checkpointWorkTime() }
        suspended = value
        revision += 1
        if !value { refresh() }
        reconcileTimers()
        publish(force: true)
    }

    func refresh() {
        precondition(Thread.isMainThread)
        if phase == .running && kind == .countdown && snapshot.remaining <= 0.000001 {
            checkpointWorkTime()
            accumulated = duration; startedAt = nil; phase = .completed
            changed()
            return
        }
        reconcileTimers()
        if visible && !suspended { publish(force: false) }
    }

    func shutdown() {
        precondition(Thread.isMainThread)
        checkpointWorkTime()
        accumulated = snapshot.elapsed
        startedAt = nil
        if phase == .running { phase = .stopped }
        visible = false; suspended = true
        invalidateDisplayTimer(); invalidateCompletionTimer()
    }

    private func checkpointWorkTime() {
        let elapsed = snapshot.elapsed
        let delta = max(0, elapsed - trackedSessionElapsed)
        let total = trackedTotal + delta
        trackedTotal = total.isFinite ? total : Double.greatestFiniteMagnitude
        trackedSessionElapsed = elapsed
        // Update accounting before notifying: a synchronous profile redraw
        // should already observe the same absolute total as its saved metadata.
        onTrackedWorkSecondsChanged?(trackedTotal)
    }

    private func changed() {
        revision += 1
        invalidateDisplayTimer(); invalidateCompletionTimer()
        reconcileTimers()
        publish(force: true)
    }

    private func publish(force: Bool) {
        let second = snapshot.displayedSeconds
        guard force || publishedSecond != second else { return }
        publishedSecond = second
        Array(observers.values).forEach { $0() }
    }

    private func reconcileTimers() {
        guard phase == .running, !suspended else {
            invalidateDisplayTimer(); invalidateCompletionTimer(); return
        }
        if visible {
            if displayTimer == nil {
                let token = displayGeneration
                displayTimer = scheduleTimer(1, true, 0.15) { [weak self] in
                    guard let self, self.displayGeneration == token else { return }
                    self.refresh()
                }
            }
        } else { invalidateDisplayTimer() }
        if kind == .countdown {
            if completionTimer == nil {
                let token = completionGeneration
                let interval = max(0.001, snapshot.remaining)
                completionTimer = scheduleTimer(interval, false, min(0.05, interval * 0.05)) { [weak self] in
                    guard let self, self.completionGeneration == token else { return }
                    self.completionTimer = nil
                    self.refresh()
                }
            }
        } else { invalidateCompletionTimer() }
    }

    private func invalidateDisplayTimer() {
        displayGeneration += 1; displayTimer?.invalidate(); displayTimer = nil
    }
    private func invalidateCompletionTimer() {
        completionGeneration += 1; completionTimer?.invalidate(); completionTimer = nil
    }

    private static let timebase: mach_timebase_info_data_t = {
        var value = mach_timebase_info_data_t()
        mach_timebase_info(&value)
        return value
    }()
    private static func continuousTime() -> TimeInterval {
        Double(mach_continuous_time()) * Double(timebase.numer) / Double(timebase.denom) / 1_000_000_000
    }
    private static func scheduleOnMainRunLoop(interval: TimeInterval, repeats: Bool, tolerance: TimeInterval,
                                            action: @escaping () -> Void) -> WorkModeTimer {
        let timer = Timer(timeInterval: interval, repeats: repeats) { _ in action() }
        timer.tolerance = tolerance
        RunLoop.main.add(timer, forMode: .common)
        return timer
    }
    deinit { displayTimer?.invalidate(); completionTimer?.invalidate() }
}
