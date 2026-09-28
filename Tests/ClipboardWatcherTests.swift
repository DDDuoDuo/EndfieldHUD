import AppKit

/// Private pasteboards plus a manually fired scheduler keep the tests isolated
/// and deterministic, with no wall-clock waits or changes to copied user data.
enum ClipboardWatcherTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        let pasteboard = NSPasteboard(name: NSPasteboard.Name("EndfieldCharge.ClipboardWatcherTests.\(UUID().uuidString)"))
        defer { pasteboard.releaseGlobally() }
        func write(_ value: String) {
            pasteboard.clearContents()
            pasteboard.setString(value, forType: .string)
        }
        let store = ClipboardStore(capacity: 10)
        let clock = ManualClipboardScheduler()
        var captureCalls = 0
        var reenter = false
        weak var reentrantWatcher: ClipboardWatcher?
        let watcher = ClipboardWatcher(store: store, pasteboard: pasteboard, capture: { board in
            captureCalls += 1
            if reenter { reentrantWatcher?.checkForChanges() }
            return store.capture(from: board)
        }, scheduleTimer: clock.schedule)
        reentrantWatcher = watcher
        defer { watcher.stop() }

        write("Copied before this session")
        watcher.checkForChanges()
        check(captureCalls == 0 && clock.timers.isEmpty, "A stopped watcher neither captures nor schedules work")
        watcher.start()
        check(watcher.isRunning && !watcher.isSuspended && clock.liveCount == 1,
              "Starting owns exactly one active timer")
        check(captureCalls == 0 && store.items.isEmpty,
              "First start baselines the pasteboard without importing prelaunch contents")
        check(clock.intervals == [1] && clock.tolerances == [0.25],
              "Monitoring requests a one-second timer with a quarter-second tolerance")
        watcher.start()
        for _ in 0..<12 { clock.fire() }
        check(clock.timers.count == 1 && captureCalls == 0,
              "Repeated starts and unchanged timer ticks never schedule another timer or deserialize clipboard contents")

        write("First in-session copy")
        clock.fire()
        check(captureCalls == 1 && store.items.count == 1, "A changed count captures one new clipboard item")
        let firstID = store.items[0].id
        clock.fire(); watcher.checkForChanges()
        check(captureCalls == 1 && store.items.count == 1, "An acknowledged change is not captured again")
        write("Second in-session copy")
        reenter = true
        watcher.checkForChanges()
        reenter = false
        check(captureCalls == 2 && store.items.count == 2,
              "Observer reentrancy cannot deserialize or capture a changed pasteboard twice")

        pasteboard.clearContents()
        pasteboard.setData(Data([1, 2, 3]), forType: NSPasteboard.PasteboardType("com.endfieldcharge.tests.unsupported"))
        clock.fire()
        check(captureCalls == 3 && store.items.count == 2, "Unsupported content gets one attempted capture without creating a cache row")
        clock.fire(); watcher.checkForChanges()
        check(captureCalls == 3, "Rejected content is also acknowledged instead of being deserialized on every timer tick")

        watcher.setSuspended(true)
        check(watcher.isRunning && watcher.isSuspended && clock.liveCount == 0 && clock.timers[0].invalidated,
              "Suspending keeps session intent but invalidates its timer")
        write("Copied while suspended")
        watcher.checkForChanges(); clock.fire()
        watcher.setSuspended(true)
        check(captureCalls == 3 && clock.timers.count == 1,
              "Suspended checks and repeated suspend requests do no clipboard or timer work")
        watcher.setSuspended(false)
        check(captureCalls == 4 && store.items.count == 3 && clock.liveCount == 1 && clock.timers.count == 2,
              "Resume immediately captures the latest changed clipboard and restores one timer")
        watcher.setSuspended(false); clock.fire()
        check(captureCalls == 4 && clock.timers.count == 2,
              "Repeated resume and the first unchanged resumed tick are harmless")

        watcher.stop()
        check(!watcher.isRunning && clock.liveCount == 0 && clock.timers[1].invalidated,
              "Stopping invalidates the active timer")
        write("Copied while stopped")
        watcher.checkForChanges()
        check(captureCalls == 4, "Stopped manual checks cannot import clipboard contents")
        watcher.start()
        check(captureCalls == 5 && store.items.count == 4 && clock.liveCount == 1 && clock.timers.count == 3,
              "Restarting the same session immediately catches a pending clipboard change")

        let orderBeforeCopy = store.items.map(\.id)
        check(watcher.copy(id: firstID) && pasteboard.string(forType: .string) == "First in-session copy",
              "Copying a selected cache entry restores its actual text to the requested pasteboard")
        clock.fire(); watcher.checkForChanges()
        check(captureCalls == 5 && store.items.map(\.id) == orderBeforeCopy,
              "Copyback acknowledges its own change immediately without duplicate capture or row reordering")
        watcher.setSuspended(true)
        check(watcher.copy(id: firstID), "An explicit cached copy remains usable while monitoring is suspended")
        watcher.setSuspended(false)
        check(captureCalls == 5 && store.items.map(\.id) == orderBeforeCopy,
              "Resuming after suspended copyback does not mistake the app's own write for an external copy")

        write("A pending external copy")
        check(!watcher.copy(id: UUID()), "An unknown cached identity cannot overwrite the clipboard")
        watcher.checkForChanges()
        check(captureCalls == 6 && store.items.count == 5,
              "Failed copyback does not acknowledge or discard a pending real clipboard change")
        watcher.stop()
        let timerCount = clock.timers.count
        watcher.setSuspended(true); watcher.setSuspended(false)
        check(clock.liveCount == 0 && clock.timers.count == timerCount,
              "Resuming a stopped watcher cannot silently restart monitoring")

        // A pending capture can evict the oldest unpinned row in a full cache.
        // Explicitly choosing that row must still succeed, so copyback wins.
        let smallStore = ClipboardStore(capacity: 2)
        write("Oldest selected copy")
        check(smallStore.capture(from: pasteboard), "The full-cache copy fixture stores its first entry")
        let oldestID = smallStore.items[0].id
        write("Newer cached copy")
        check(smallStore.capture(from: pasteboard), "The full-cache copy fixture stores its second entry")
        let smallClock = ManualClipboardScheduler()
        var smallCaptures = 0
        let smallWatcher = ClipboardWatcher(store: smallStore, pasteboard: pasteboard, capture: { board in
            smallCaptures += 1; return smallStore.capture(from: board)
        }, scheduleTimer: smallClock.schedule)
        defer { smallWatcher.stop() }
        smallWatcher.start()
        let fullOrder = smallStore.items.map(\.id)
        write("Unobserved external copy before the click")
        check(smallWatcher.copy(id: oldestID) && pasteboard.string(forType: .string) == "Oldest selected copy",
              "A pending external change cannot evict the user's selected oldest row before explicit copyback")
        smallClock.fire()
        check(smallCaptures == 0 && smallStore.items.map(\.id) == fullOrder,
              "Selected copyback supersedes an unobserved external copy and acknowledges the resulting write")
        smallWatcher.stop()

        let suspendedClock = ManualClipboardScheduler()
        let suspendedStore = ClipboardStore(capacity: 2)
        var suspendedCaptures = 0
        var suspendedWatcher: ClipboardWatcher? = ClipboardWatcher(store: suspendedStore, pasteboard: pasteboard, capture: { board in
            suspendedCaptures += 1; return suspendedStore.capture(from: board)
        }, scheduleTimer: suspendedClock.schedule)
        suspendedWatcher?.setSuspended(true)
        write("Before a suspended first start")
        suspendedWatcher?.start()
        check(suspendedClock.timers.isEmpty && suspendedCaptures == 0,
              "First start while suspended records only its baseline and does not install a timer")
        write("After a suspended first start")
        suspendedWatcher?.setSuspended(false)
        check(suspendedCaptures == 1 && suspendedStore.items.count == 1 && suspendedClock.liveCount == 1,
              "The first resume observes changes made after a suspended startup")
        weak var releasedWatcher = suspendedWatcher
        suspendedWatcher = nil
        check(releasedWatcher == nil && suspendedClock.liveCount == 0,
              "Timer callbacks do not retain the watcher, and deinitialization invalidates its timer")
        return count
    }
}

private final class ManualClipboardScheduler {
    private(set) var timers: [ManualClipboardTimer] = []
    private(set) var intervals: [TimeInterval] = []
    private(set) var tolerances: [TimeInterval] = []
    var liveCount: Int { timers.filter { !$0.invalidated }.count }

    func schedule(interval: TimeInterval, tolerance: TimeInterval, tick: @escaping () -> Void) -> ClipboardWatcherTimer {
        let timer = ManualClipboardTimer(tick: tick)
        intervals.append(interval); tolerances.append(tolerance); timers.append(timer)
        return timer
    }

    func fire() { for timer in timers where !timer.invalidated { timer.tick() } }
}

private final class ManualClipboardTimer: ClipboardWatcherTimer {
    private(set) var invalidated = false
    let tick: () -> Void
    init(tick: @escaping () -> Void) { self.tick = tick }
    func invalidate() { invalidated = true }
}
