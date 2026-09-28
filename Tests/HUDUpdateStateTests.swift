import Foundation

enum HUDUpdateStateTests {
    /// Keeps cancelled callbacks so tests can deliver already-enqueued work.
    /// No wall clock, run-loop draining, network or installer is involved.
    private final class Scheduler {
        final class Job {
            let delay: TimeInterval
            let action: () -> Void
            var cancelled = false
            init(delay: TimeInterval, action: @escaping () -> Void) {
                self.delay = delay; self.action = action
            }
        }
        var jobs: [Job] = []
        func schedule(_ delay: TimeInterval, _ action: @escaping () -> Void) -> () -> Void {
            let job = Job(delay: delay, action: action)
            jobs.append(job)
            return { job.cancelled = true }
        }
    }

    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) {
            count += 1
            if !condition { fatalError(message) }
        }

        do {
            let scheduler = Scheduler()
            let gate = HUDUpdateInstallGate(schedule: scheduler.schedule)
            var checks = 0, installs = 0
            func evaluate(_ eligible: Bool) {
                gate.evaluate(eligible: eligible, recheck: { checks += 1; return true },
                              install: { installs += 1 })
            }
            evaluate(false)
            check(scheduler.jobs.isEmpty && checks == 0 && installs == 0,
                  "An unsafe or unavailable update creates no timer and invokes no work")
            evaluate(true)
            check(scheduler.jobs.count == 1 && scheduler.jobs[0].delay == 30,
                  "A ready eligible update starts one full thirty-second quiet period")
            for _ in 0..<20 { evaluate(true) }
            check(scheduler.jobs.count == 1 && !scheduler.jobs[0].cancelled,
                  "Repeated safe events retain the existing deadline instead of starving installation")
            check(checks == 0 && installs == 0, "Eligibility alone cannot install before the deadline")
            scheduler.jobs[0].action()
            check(checks == 1 && installs == 1, "The deadline rechecks eligibility before installing")
            scheduler.jobs[0].action()
            evaluate(true); evaluate(false); evaluate(true)
            check(checks == 1 && installs == 1 && scheduler.jobs.count == 1,
                  "A delivered install remains exactly once despite duplicate callbacks and later events")
        }

        do {
            let scheduler = Scheduler()
            let gate = HUDUpdateInstallGate(delay: 7, schedule: scheduler.schedule)
            var safe = true, checks = 0, installs = 0
            func evaluate() {
                gate.evaluate(eligible: safe, recheck: { checks += 1; return safe },
                              install: { installs += 1 })
            }
            evaluate()
            let old = scheduler.jobs[0]
            safe = false; evaluate()
            check(old.cancelled, "Opening the HUD or starting work cancels an outstanding restart")
            old.action()
            check(checks == 0 && installs == 0, "Cancelled work cannot even recheck after losing eligibility")
            safe = true; evaluate()
            check(scheduler.jobs.count == 2 && scheduler.jobs[1].delay == 7,
                  "Returning to idle starts a fresh complete quiet period")
            old.action()
            check(checks == 0 && installs == 0, "A stale callback cannot consume a newer ready update")
            scheduler.jobs[1].action()
            check(checks == 1 && installs == 1, "Only the current idle deadline can install")
        }

        do {
            let scheduler = Scheduler()
            let gate = HUDUpdateInstallGate(schedule: scheduler.schedule)
            var safe = true, checks = 0, installs = 0
            func evaluate() {
                gate.evaluate(eligible: true, recheck: { checks += 1; return safe },
                              install: { installs += 1 })
            }
            evaluate()
            safe = false // An eligibility change can race the delayed callback.
            scheduler.jobs[0].action()
            check(checks == 1 && installs == 0, "A failed final eligibility check prevents installation")
            check(scheduler.jobs.count == 1, "A failed recheck starts no retry loop")
            safe = true; evaluate()
            check(scheduler.jobs.count == 2, "A later idle event can retry after a failed recheck")
            scheduler.jobs[0].action()
            check(checks == 1 && installs == 0, "The failed old deadline cannot bypass the fresh quiet period")
            scheduler.jobs[1].action()
            check(checks == 2 && installs == 1, "The new deadline can install after eligibility returns")
        }

        do {
            let scheduler = Scheduler()
            let gate = HUDUpdateInstallGate(schedule: scheduler.schedule)
            var automatic = true, installs = 0
            func evaluate() {
                gate.evaluate(eligible: automatic, recheck: { automatic }, install: { installs += 1 })
            }
            evaluate()
            automatic = false; evaluate()
            check(scheduler.jobs[0].cancelled, "Disabling automatic installation cancels a pending restart")
            scheduler.jobs[0].action()
            check(installs == 0, "A downloaded update cannot install after automatic installation is disabled")
            automatic = true; evaluate()
            check(scheduler.jobs.count == 2, "Re-enabling starts a new quiet period without reusing cancelled time")
            automatic = false // Also defend against the setting changing before its event is delivered.
            scheduler.jobs[1].action()
            check(installs == 0, "The final recheck observes a newly disabled automatic-install setting")
        }

        do {
            let scheduler = Scheduler()
            let gate = HUDUpdateInstallGate(schedule: scheduler.schedule)
            var firstInstalls = 0, secondInstalls = 0
            gate.evaluate(eligible: true, recheck: { true }, install: { firstInstalls += 1 })
            let first = scheduler.jobs[0]
            gate.reset()
            check(first.cancelled, "Reset cancels the former update's pending timer")
            gate.evaluate(eligible: true, recheck: { true }, install: { secondInstalls += 1 })
            first.action()
            check(firstInstalls == 0 && secondInstalls == 0,
                  "A reset update's stale callback cannot invoke either install handler")
            scheduler.jobs[1].action()
            check(firstInstalls == 0 && secondInstalls == 1, "The replacement update owns the new callback")
            gate.reset()
            gate.evaluate(eligible: true, recheck: { true }, install: { secondInstalls += 1 })
            check(scheduler.jobs.count == 3, "Reset re-arms a gate that already installed a previous update")
            scheduler.jobs[1].action()
            check(secondInstalls == 1, "An earlier completed update stays inert after reset")
            scheduler.jobs[2].action()
            check(secondInstalls == 2, "A new update can install exactly once after reset")
        }

        do {
            let scheduler = Scheduler()
            var gate: HUDUpdateInstallGate? = HUDUpdateInstallGate(schedule: scheduler.schedule)
            weak var weakGate = gate
            var installs = 0
            gate?.evaluate(eligible: true, recheck: { true }, install: { installs += 1 })
            gate = nil
            check(weakGate == nil && scheduler.jobs[0].cancelled,
                  "Destroying the owner cancels its timer without a scheduler retain cycle")
            scheduler.jobs[0].action()
            check(installs == 0, "A queued callback is inert after the gate is destroyed")
        }

        for phase in [HUDUpdateState.Phase.idle, .checking, .current, .failed] {
            check(!HUDUpdateState(phase: phase).hasUpdate, "Non-update states do not advertise an installable release")
        }
        for phase in [HUDUpdateState.Phase.available, .downloading, .ready, .installing] {
            check(HUDUpdateState(phase: phase).hasUpdate, "A known update stays advertised throughout download and install")
        }
        let initial = HUDUpdateState()
        check(initial.automaticallyInstalls && !initial.installationSupported,
              "A default preference cannot enable installation before support is established")
        return count
    }
}
