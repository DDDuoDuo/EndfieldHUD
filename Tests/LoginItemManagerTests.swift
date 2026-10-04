import AppKit

enum LoginItemManagerTests {
    private enum Failure: Error { case registration }
    static func run() -> Int {
        var assertions = 0
        func check(_ condition: Bool, _ message: String) {
            assertions += 1; if !condition { fatalError(message) }
        }
        func wait(_ condition: () -> Bool) {
            let deadline = Date().addingTimeInterval(3)
            while !condition(), Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.005)) }
            check(condition(), "Asynchronous login-status operation completes within its test deadline")
        }
        let enabled = LoginItemStatusSnapshot(isEnabled: true, requiresApproval: false)
        let disabled = LoginItemStatusSnapshot(isEnabled: false, requiresApproval: false)
        let language = L10n.language
        defer { L10n.language = language }
        L10n.language = .english
        do {
            let queue = DispatchQueue(label: "EndfieldHUD.LoginStatusTests.blocking")
            let entered = DispatchSemaphore(value: 0), release = DispatchSemaphore(value: 0)
            var reads = 0, wrongThread = false
            let manager = LoginItemManager(queue: queue, readStatus: {
                reads += 1; wrongThread = wrongThread || Thread.isMainThread
                if reads == 1 { entered.signal(); _ = release.wait(timeout: .now() + 3) }
                return enabled
            }, changeEnabled: { _ in fatalError("Passive status reads must not register a login item") })
            var notifications = 0, callbackOnMain = true
            manager.onStatusChange = { notifications += 1; callbackOnMain = callbackOnMain && Thread.isMainThread }
            manager.refreshStatus()
            check(entered.wait(timeout: .now() + 1) == .success, "Status IPC runs on its worker while the request returns to main")
            var mainRan = false
            DispatchQueue.main.async { mainRan = true }
            wait { mainRan }
            for _ in 0..<100 { manager.refreshStatus() }
            check(manager.snapshot == nil && manager.statusDescription.isEmpty,
                  "A blocked status request leaves main responsive and does not invent an OS registration state")
            release.signal()
            wait { manager.snapshot == enabled }
            queue.sync {}
            RunLoop.main.run(until: Date().addingTimeInterval(0.02))
            let results = queue.sync { (reads, wrongThread) }
            check(results.0 == 2 && !results.1 && notifications == 1 && callbackOnMain,
                  "A burst coalesces into one follow-up read and publishes only changed snapshots on main")
            check(manager.statusDescription == "Enabled", "Cached raw status is localized for its current consumer")
            L10n.language = .simplifiedChinese
            check(manager.statusDescription == "已启用" && queue.sync { reads } == 2,
                  "Changing language relocalizes the same snapshot without OS IPC")
            L10n.language = .english
        }
        do {
            let queue = DispatchQueue(label: "EndfieldHUD.LoginStatusTests.stale")
            let entered = DispatchSemaphore(value: 0), release = DispatchSemaphore(value: 0)
            var state = disabled, first = true, edits: [Bool] = [], shouldFail = false
            let manager = LoginItemManager(queue: queue, readStatus: {
                let result = state
                if first { first = false; entered.signal(); _ = release.wait(timeout: .now() + 3) }
                return result
            }, changeEnabled: { value in
                edits.append(value)
                if shouldFail { throw Failure.registration }
                state = value ? enabled : disabled
            })
            var published: [LoginItemStatusSnapshot] = []
            manager.onStatusChange = { [weak manager] in if let snapshot = manager?.snapshot { published.append(snapshot) } }
            manager.refreshStatus()
            check(entered.wait(timeout: .now() + 1) == .success, "A stale read is in flight before the explicit edit")
            release.signal()
            do { try manager.setEnabled(true) } catch { fatalError("Successful registration must remain successful") }
            check(manager.snapshot == enabled && queue.sync { edits } == [true],
                  "Explicit edits serialize with reads and synchronously expose their verified result")
            RunLoop.main.run(until: Date().addingTimeInterval(0.03))
            check(manager.snapshot == enabled && published.isEmpty,
                  "An older queued read cannot overwrite a newer registration or reenter the settings transaction")
            queue.sync { state = disabled }
            manager.refreshStatus()
            wait { manager.snapshot == disabled }
            check(published == [disabled], "Later genuine external changes still refresh the shared status cache")
            queue.sync { shouldFail = true }
            var receivedError = false
            do { try manager.setEnabled(true) } catch { receivedError = true }
            check(receivedError && manager.snapshot == disabled,
                  "Registration failures preserve the original synchronous error and actual OS state")
        }
        for (ensure, initial) in [(false, disabled), (true, disabled), (true, enabled)] {
            let queue = DispatchQueue(label: "EndfieldHUD.LoginStatusTests.start")
            var state = initial, edits: [Bool] = []
            let manager = LoginItemManager(queue: queue, readStatus: { state }, changeEnabled: { value in
                edits.append(value); state = value ? enabled : disabled
            })
            var finished = false
            manager.start(ensureEnabled: ensure) { error in
                check(error == nil && Thread.isMainThread, "Startup result is delivered on main without a registration error")
                finished = true
            }
            wait { finished }
            check(manager.snapshot == (ensure ? enabled : initial)
                  && queue.sync { edits } == (ensure && !initial.isEnabled ? [true] : []),
                  "Only the saved enabled preference registers at startup; disabled preference performs a passive read")
        }
        return assertions
    }
}
