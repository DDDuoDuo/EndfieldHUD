import Foundation

enum WorkModeFocusTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func snapshot(_ phase: WorkModePhase, kind: WorkModeKind = .countdown, elapsed: TimeInterval = 0) -> WorkModeSnapshot {
            WorkModeSnapshot(kind: kind, phase: phase, duration: 300, elapsed: elapsed)
        }
        let runner = FakeExecutor()
        let controller = WorkModeFocusController(executor: runner)
        var notifications = 0
        let observer = controller.observe { notifications += 1 }
        controller.receive(snapshot(.idle)); controller.receive(snapshot(.idle))
        check(runner.commands.isEmpty && controller.state == .idle && !controller.isPending,
              "Construction and idle snapshots never execute shortcuts or change Focus")
        controller.receive(snapshot(.running))
        check(runner.commands == [.start] && controller.state == .starting && controller.isPending,
              "The first active work session starts exactly one automatic Focus request")
        controller.receive(snapshot(.running, elapsed: 1)); controller.receive(snapshot(.paused))
        check(runner.commands == [.start] && controller.state == .starting,
              "Clock ticks and pause cannot duplicate or cancel an in-flight Focus request")
        runner.complete(0, .success("enabled\n"))
        check(controller.state == .enabled && !controller.isPending && controller.statusMessage == nil,
              "Only an exact successful enabled acknowledgement confirms ownership")
        controller.receive(snapshot(.running)); controller.receive(snapshot(.paused)); controller.receive(snapshot(.running))
        check(runner.commands == [.start], "Pause and resume retain the same Focus until the work session ends")
        controller.receive(snapshot(.completed))
        check(runner.commands == [.start, .end] && controller.state == .stopping && controller.isPending,
              "Completion releases Focus owned by this work session")
        runner.complete(1, .success("released"))
        check(controller.state == .idle && !controller.isPending && controller.statusMessage == nil,
              "An acknowledged release clears ownership and pending state")
        controller.receive(snapshot(.idle)); controller.receive(snapshot(.completed))
        check(runner.commands.count == 2, "Repeated inactive snapshots do not rerun cleanup")
        check(notifications == 4, "Observers receive meaningful command states, not timer or pause ticks")
        controller.removeObserver(observer)
        controller.receive(snapshot(.running, kind: .stopwatch))
        check(notifications == 4 && runner.commands.last == .start,
              "Removing an observer stops its callbacks while stopwatch activation still starts Focus")
        runner.complete(2, .success("preserved"))
        controller.receive(snapshot(.idle, kind: .stopwatch))
        check(runner.commands.count == 3 && controller.state == .idle,
              "A Focus that already existed is never disabled when a stopwatch resets")

        let preservedRunner = FakeExecutor()
        let preserved = WorkModeFocusController(executor: preservedRunner)
        preserved.receive(snapshot(.running)); preservedRunner.complete(0, .success("preserved"))
        check(preserved.state == .preserved && !preserved.isPending,
              "The preserved response reports existing Focus without claiming ownership")
        var preservedShutdown: Bool?
        preserved.shutdown { preservedShutdown = $0 }
        check(preservedShutdown == true && preservedRunner.commands == [.start] && preserved.state == .idle,
              "Quit leaves a pre-existing Focus untouched and needs no cleanup command")
        preserved.receive(snapshot(.running))
        check(preservedRunner.commands.count == 1, "Late timer callbacks cannot reactivate Focus after shutdown begins")

        let raceRunner = FakeExecutor()
        let race = WorkModeFocusController(executor: raceRunner)
        race.receive(snapshot(.running)); race.receive(snapshot(.idle))
        check(raceRunner.commands == [.start] && race.isPending,
              "Reset waits for the pending Start result instead of racing a speculative End against it")
        raceRunner.complete(0, .success("enabled"))
        check(raceRunner.commands == [.start, .end] && race.state == .stopping,
              "A delayed enabled response after Reset is immediately compensated by one End")
        race.receive(snapshot(.running))
        check(raceRunner.commands.count == 2 && race.state == .stopping,
              "A new work session waits for an earlier cleanup instead of overlapping commands")
        raceRunner.complete(1, .success("released"))
        check(raceRunner.commands == [.start, .end, .start] && race.state == .starting,
              "The latest active request starts only after prior cleanup is acknowledged")
        raceRunner.complete(0, .success("enabled")); raceRunner.complete(1, .failure(.commandFailed(1)))
        check(raceRunner.commands.count == 3 && race.state == .starting,
              "Duplicate and stale command callbacks cannot overwrite a newer request")
        raceRunner.complete(2, .success("preserved")); race.receive(snapshot(.idle))
        check(raceRunner.commands.count == 3 && race.state == .idle,
              "The newer session independently respects an already-existing Focus")

        let coalescedRunner = FakeExecutor()
        let coalesced = WorkModeFocusController(executor: coalescedRunner)
        coalesced.receive(snapshot(.running)); coalesced.receive(snapshot(.idle)); coalesced.receive(snapshot(.running))
        coalescedRunner.complete(0, .success("enabled"))
        check(coalescedRunner.commands == [.start] && coalesced.state == .enabled,
              "Start/Reset/Start before acknowledgement coalesces to the latest active intent without toggling churn")
        coalesced.receive(snapshot(.idle)); coalescedRunner.complete(1, .success("unchanged"))
        check(coalesced.state == .idle && !coalesced.isPending,
              "End can acknowledge a user-changed or already-disabled Focus without restoring an old mode")

        let pendingQuitRunner = FakeExecutor()
        let pendingQuit = WorkModeFocusController(executor: pendingQuitRunner)
        pendingQuit.receive(snapshot(.running))
        var quitResults: [Bool] = []
        pendingQuit.shutdown { quitResults.append($0) }; pendingQuit.shutdown { quitResults.append($0) }
        check(quitResults.isEmpty && pendingQuitRunner.commands == [.start],
              "Shutdown does not falsely report cleanup while Start is still running")
        pendingQuitRunner.complete(0, .success("enabled"))
        check(quitResults.isEmpty && pendingQuitRunner.commands == [.start, .end],
              "Shutdown serializes owned compensation after the late Start acknowledgement")
        pendingQuitRunner.complete(1, .success("released"))
        check(quitResults == [true, true] && pendingQuit.state == .idle,
              "All shutdown waiters complete only after owned Focus is confirmed released")

        let resetPreservedRunner = FakeExecutor()
        let resetPreserved = WorkModeFocusController(executor: resetPreservedRunner)
        resetPreserved.receive(snapshot(.running)); resetPreserved.receive(snapshot(.idle))
        resetPreservedRunner.complete(0, .success("preserved"))
        check(resetPreservedRunner.commands == [.start] && resetPreserved.state == .idle,
              "Reset during Start never disables Focus when the eventual response says preserved")

        for error in [WorkModeFocusRunError.notConfigured, .unavailable, .launchFailed, .commandFailed(1), .invalidResponse] {
            let failedRunner = FakeExecutor()
            let failed = WorkModeFocusController(executor: failedRunner)
            failed.receive(snapshot(.running)); failedRunner.complete(0, .failure(error))
            check(!failed.isPending
                  && (error == .notConfigured ? failed.statusMessage == nil : failed.statusMessage?.isEmpty == false)
                  && failed.state == (error == .notConfigured || error == .unavailable ? .unavailable : .failed),
                  "Missing optional setup has no reminder; actual failures remain visible and never marked enabled: \(error)")
            failed.receive(snapshot(.running, elapsed: 2)); failed.receive(snapshot(.paused)); failed.receive(snapshot(.running))
            check(failedRunner.commands == [.start], "Ticks and pause/resume cannot create an automatic failure-retry loop")
            var result: Bool?
            failed.shutdown { result = $0 }
            check(failedRunner.commands == [.start] && result == !error.mayHaveChangedFocus,
                  "Failed Start never disables an unowned Focus and uncertain effects cannot claim clean shutdown")
        }
        for response in ["", "true", "Enabled", "enabled\npreserved", "released"] {
            let invalidRunner = FakeExecutor()
            let invalid = WorkModeFocusController(executor: invalidRunner)
            invalid.receive(snapshot(.running)); invalidRunner.complete(0, .success(response))
            check(invalid.state == .failed && invalidRunner.commands == [.start],
                  "Unexpected output is not interpreted as permission to own or disable Focus")
        }

        let endFailureRunner = FakeExecutor()
        let endFailure = WorkModeFocusController(executor: endFailureRunner)
        endFailure.receive(snapshot(.running)); endFailureRunner.complete(0, .success("enabled"))
        endFailure.receive(snapshot(.idle)); endFailureRunner.complete(1, .failure(.commandFailed(1)))
        check(endFailure.state == .failed && !endFailure.isPending && endFailureRunner.commands.count == 2,
              "Cleanup failure keeps honest uncertainty without an unbounded retry loop")
        endFailure.receive(snapshot(.running))
        check(endFailureRunner.commands == [.start, .end, .end] && endFailure.state == .stopping,
              "A new session first retries unresolved owned cleanup rather than falsely claiming Focus is enabled")
        endFailureRunner.complete(2, .success("unchanged"))
        check(endFailureRunner.commands == [.start, .end, .end, .start],
              "Only acknowledged cleanup allows the next session's automatic Start")
        endFailureRunner.complete(3, .success("enabled"))
        var failedQuit: Bool?
        endFailure.shutdown { failedQuit = $0 }
        endFailureRunner.complete(4, .failure(.commandFailed(1)))
        check(failedQuit == false && endFailure.state == .failed,
              "Failed quit cleanup reports false instead of claiming prior Focus was restored")

        let unsupportedRunner = FakeExecutor(); unsupportedRunner.isAvailable = false
        let unsupported = WorkModeFocusController(executor: unsupportedRunner)
        unsupported.receive(snapshot(.running))
        check(unsupported.state == .unavailable && unsupportedRunner.commands.isEmpty,
              "Unsupported macOS or missing Shortcuts cannot execute an alternative private Focus path")
        var unsupportedQuit: Bool?
        unsupported.shutdown { unsupportedQuit = $0 }
        check(unsupportedQuit == true && unsupportedRunner.commands.isEmpty,
              "An unavailable executor that changed nothing can shut down immediately")

        let slowRunner = FakeExecutor()
        let slow = WorkModeFocusController(executor: slowRunner, warningDelay: 0)
        slow.receive(snapshot(.running))
        RunLoop.current.run(until: Date().addingTimeInterval(0.015))
        check(slow.state == .failed && slow.isPending && slow.statusMessage?.isEmpty == false,
              "A slow shortcut reports unconfirmed progress while retaining its in-flight ownership gate")
        slow.receive(snapshot(.idle))
        check(slowRunner.commands == [.start] && slow.isPending,
              "A soft timeout cannot fake command cancellation or race cleanup")
        slowRunner.complete(0, .success("enabled"))
        check(slowRunner.commands == [.start, .end] && slow.state == .stopping,
              "A late response after a soft timeout still receives required owned compensation")
        slowRunner.complete(1, .success("released"))
        RunLoop.current.run(until: Date().addingTimeInterval(0.015))
        check(slow.state == .idle && slow.statusMessage == nil && !slow.isPending,
              "Cancelled warning callbacks cannot resurrect an error after cleanup succeeds")
        check(WorkModeFocusCommand.start.shortcutName == "EndfieldCharge Focus Start"
              && WorkModeFocusCommand.end.shortcutName == "EndfieldCharge Focus End",
              "Only the documented user-owned shortcut names are dispatched")

        // Exercise the production serial executor with a substitute process
        // boundary: these tests never run Shortcuts or change system Focus.
        var invocations: [([String], Int)] = []
        var workerCallsOnly = true
        let executor = ShortcutsWorkModeFocusExecutor(commandExecutor: { arguments, limit in
            workerCallsOnly = workerCallsOnly && !Thread.isMainThread
            invocations.append((arguments, limit))
            if arguments == ["list"] {
                return "EndfieldCharge Focus Start\nEndfieldCharge Focus End\n"
            }
            return arguments.last == WorkModeFocusCommand.start.shortcutName ? "enabled" : "released"
        }, availability: { true })
        func runExecutor(_ executor: WorkModeFocusExecuting, _ command: WorkModeFocusCommand) -> Result<String, WorkModeFocusRunError>? {
            var result: Result<String, WorkModeFocusRunError>?
            executor.run(command) {
                check(Thread.isMainThread, "Production executor delivers command acknowledgements on main")
                result = $0
            }
            let deadline = Date().addingTimeInterval(2)
            while result == nil && Date() < deadline {
                RunLoop.current.run(until: Date().addingTimeInterval(0.005))
            }
            return result
        }
        check(runExecutor(executor, .start) == .success("enabled"),
              "Production executor returns native text acknowledgement without coercing the shortcut result")
        check(workerCallsOnly && invocations.map { $0.0 } == [["list"], ["run", "EndfieldCharge Focus Start"]]
              && invocations.map { $0.1 } == [65_536, 4096],
              "Start validates both names off-main and invokes only the default text-output path with bounded capture")
        check(runExecutor(executor, .end) == .success("released")
              && invocations.last?.0 == ["run", "EndfieldCharge Focus End"] && invocations.count == 3,
              "Owned cleanup uses the same no-coercion invocation without another inventory query")
        var incompleteInvocations: [[String]] = []
        let incomplete = ShortcutsWorkModeFocusExecutor(commandExecutor: { arguments, _ in
            incompleteInvocations.append(arguments)
            return "EndfieldCharge Focus Start"
        }, availability: { true })
        check(runExecutor(incomplete, .start) == .failure(.notConfigured) && incompleteInvocations == [["list"]],
              "An absent cleanup shortcut stops before any Focus-changing process is dispatched")

        // The direct adapter uses observed public AX roles/state, with a fake
        // boundary here: no real permission checks, UI or Focus changes.
        let panel = FakeFocusAccess()
        var notificationDrains: [() -> Void] = []
        let direct = ControlCenterWorkModeFocusExecutor(access: panel, scheduleNotificationDrain: { notificationDrains.append($0) })
        check(runExecutor(direct, .start) == .success("enabled") && panel.presses == [false],
              "A confirmed all-off panel enables only Do Not Disturb and waits for readback")
        check(direct.isPresentingSystemControls && notificationDrains.count == 1 && panel.closeCount == 1 && panel.workerOnly,
              "Direct operations dismiss owned UI off-main but retain their flag while queued focus notifications drain")
        check(runExecutor(direct, .end) == .success("released") && panel.presses == [false, true],
              "The direct owner confirms Do Not Disturb release before acknowledging End")
        notificationDrains[0]()
        check(direct.isPresentingSystemControls,
              "An older notification-drain callback cannot clear a newer Control Center transaction")
        notificationDrains[1]()
        check(!direct.isPresentingSystemControls,
              "The latest one-shot notification drain releases the transient flag without an idle timer")
        panel.choices = [.init(identifier: ControlCenterFocusChoice.doNotDisturb, enabled: false),
                         .init(identifier: "focus-mode-activity-user-work", enabled: true)]
        check(runExecutor(direct, .start) == .success("preserved") && panel.presses.count == 2,
              "An existing user Focus is preserved without any toggle")
        check(runExecutor(direct, .end) == .success("unchanged") && panel.presses.count == 2,
              "Cleanup does not disable a different Focus chosen by the user")
        panel.choices = [.init(identifier: ControlCenterFocusChoice.doNotDisturb, enabled: false)]
        check(runExecutor(direct, .end) == .success("unchanged") && panel.presses.count == 2,
              "Already-off Focus requires no compensating toggle")
        panel.choices = []
        check(runExecutor(direct, .start) == .failure(.controlCenterUnavailable) && panel.presses.count == 2,
              "Unknown or absent Focus controls fail before mutation")
        let duplicate = ControlCenterFocusChoice(identifier: ControlCenterFocusChoice.doNotDisturb, enabled: false)
        check((try? ControlCenterFocusDecision.evaluate(.start, choices: [duplicate, duplicate])) == nil,
              "Duplicate mode identities are not accepted as an unambiguous off state")
        panel.choices = [duplicate]; panel.failReadback = true
        check(runExecutor(direct, .start) == .failure(.invalidResponse) && direct.isPresentingSystemControls,
              "An unconfirmed post-toggle state stays uncertain while queued focus notifications drain")
        notificationDrains.last?()
        check(!direct.isPresentingSystemControls,
              "Failed transactions also release their transient flag after the bounded notification grace")
        let raced = FakeFocusAccess(); raced.changeBeforePress = true
        check(runExecutor(ControlCenterWorkModeFocusExecutor(access: raced), .start) == .failure(.invalidResponse)
              && raced.presses.isEmpty,
              "A user Focus change during UI setup prevents the toggle and cannot trigger a second backend")
        let denied = FakeFocusAccess(); denied.isTrusted = false
        check(runExecutor(ControlCenterWorkModeFocusExecutor(access: denied), .start) == .failure(.accessibilityRequired)
              && denied.openCount == 0 && denied.presses.isEmpty,
              "Missing Accessibility permission never opens UI, prompts, or attempts a Focus toggle")

        let preferred = FakeExecutor(), fallback = FakeExecutor()
        let automatic = AutomaticWorkModeFocusExecutor(direct: preferred, shortcuts: fallback)
        var automaticResult: Result<String, WorkModeFocusRunError>?
        automatic.run(.start) { automaticResult = $0 }
        preferred.complete(0, .failure(.controlCenterUnavailable))
        check(fallback.commands == [.start] && automaticResult == nil,
              "A recognized pre-mutation direct failure can use configured Shortcuts")
        fallback.complete(0, .success("enabled"))
        automatic.run(.end) { automaticResult = $0 }
        check(preferred.commands == [.start] && fallback.commands == [.start, .end],
              "Cleanup remains on the backend that actually enabled Focus")
        fallback.complete(1, .failure(.commandFailed(1)))
        automatic.run(.end) { automaticResult = $0 }
        check(fallback.commands == [.start, .end, .end], "Failed cleanup retains its backend ownership for retry")
        fallback.complete(2, .success("released"))
        check(automaticResult == .success("released"), "Only acknowledged cleanup releases backend ownership")

        let uncertainDirect = FakeExecutor(), unusedFallback = FakeExecutor()
        let uncertainAutomatic = AutomaticWorkModeFocusExecutor(direct: uncertainDirect, shortcuts: unusedFallback)
        uncertainAutomatic.run(.start) { automaticResult = $0 }
        uncertainDirect.complete(0, .failure(.invalidResponse))
        check(automaticResult == .failure(.invalidResponse) && unusedFallback.commands.isEmpty,
              "A potentially applied direct toggle never falls back and toggles Focus again")
        uncertainAutomatic.run(.start) { automaticResult = $0 }
        uncertainDirect.complete(1, .failure(.interrupted))
        check(automaticResult == .failure(.interrupted) && unusedFallback.commands.isEmpty,
              "Foreground interruption aborts automation without trying a different backend")
        let permissionDirect = FakeExecutor(), missingShortcuts = FakeExecutor()
        permissionDirect.isAvailable = false
        let permissionController = WorkModeFocusController(executor: AutomaticWorkModeFocusExecutor(direct: permissionDirect, shortcuts: missingShortcuts))
        permissionController.receive(snapshot(.running)); missingShortcuts.complete(0, .failure(.notConfigured))
        check(permissionController.needsAccessibilityPermission && permissionController.state == .unavailable
              && permissionController.statusMessage == WorkModeFocusRunError.accessibilityRequired.message
              && permissionDirect.commands.isEmpty,
              "Absent optional Shortcuts expose the native permission requirement without a Shortcuts setup reminder")
        permissionController.refreshAuthorization()
        check(permissionDirect.commands.isEmpty && missingShortcuts.commands.count == 1,
              "App activation without permission cannot repeatedly retry or prompt")
        permissionDirect.isAvailable = true
        permissionController.refreshAuthorization(); permissionController.refreshAuthorization()
        check(permissionDirect.commands == [.start] && permissionController.isPending,
              "A new Accessibility grant retries the still-active session once, coalescing repeated activation")
        permissionDirect.complete(0, .success("enabled"))
        permissionController.refreshAuthorization()
        check(permissionDirect.commands == [.start] && permissionController.state == .enabled,
              "Permission refresh cannot toggle an already-owned active Focus")
        let stoppedDirect = FakeExecutor(), stoppedFallback = FakeExecutor()
        stoppedDirect.isAvailable = false
        let stoppedController = WorkModeFocusController(executor: AutomaticWorkModeFocusExecutor(direct: stoppedDirect, shortcuts: stoppedFallback))
        stoppedController.receive(snapshot(.running)); stoppedFallback.complete(0, .failure(.notConfigured))
        stoppedController.receive(snapshot(.idle)); stoppedDirect.isAvailable = true
        stoppedController.refreshAuthorization()
        check(stoppedDirect.commands.isEmpty, "Granting permission after reset cannot start Focus for an ended work session")
        return count
    }

    private final class FakeExecutor: WorkModeFocusExecuting {
        var isAvailable = true
        private(set) var commands: [WorkModeFocusCommand] = []
        private var completions: [(Result<String, WorkModeFocusRunError>) -> Void] = []
        func run(_ command: WorkModeFocusCommand, completion: @escaping (Result<String, WorkModeFocusRunError>) -> Void) {
            commands.append(command); completions.append(completion)
        }
        func complete(_ index: Int, _ result: Result<String, WorkModeFocusRunError>) { completions[index](result) }
    }

    private final class FakeFocusAccess: ControlCenterFocusAccessing {
        var isTrusted = true
        var choices = [ControlCenterFocusChoice(identifier: ControlCenterFocusChoice.doNotDisturb, enabled: false)]
        var presses: [Bool] = []
        var closeCount = 0, openCount = 0
        var workerOnly = true, failReadback = false, changeBeforePress = false
        func open() throws { workerOnly = workerOnly && !Thread.isMainThread; openCount += 1 }
        func read() throws -> [ControlCenterFocusChoice] { choices }
        func pressDoNotDisturb(expectedEnabled: Bool) throws {
            if changeBeforePress { throw WorkModeFocusRunError.controlCenterUnavailable }
            presses.append(expectedEnabled)
            choices = [.init(identifier: ControlCenterFocusChoice.doNotDisturb, enabled: !expectedEnabled)]
        }
        func waitForReadback(enabled: Bool) throws {
            if failReadback { throw WorkModeFocusRunError.controlCenterUnavailable }
            precondition(choices[0].enabled == enabled)
        }
        func close() { closeCount += 1 }
    }
}
