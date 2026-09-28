import AppKit

/// Explicit --ui-test diagnostics use an in-memory log and fixture devices.
enum EventLogHUDVerification {
    static func seed(_ log: SystemEventLog) {
        for _ in 0..<3 {
            log.record(kind: .moduleOpened, metadata: ["module": "notes"])
            log.record(kind: .clipboardCopied, metadata: ["kind": "text"])
            log.record(kind: .shelfAdded, metadata: ["filename": "Project.pdf"])
            log.record(kind: .workStarted, metadata: ["kind": "countdown", "seconds": "1800"])
            log.record(kind: .workCompleted, metadata: ["kind": "countdown", "seconds": "1800"])
            log.record(kind: .audioDeviceConnected, metadata: ["device": "Studio Headphones"])
            log.record(kind: .displayConnected, metadata: ["device": "Studio Display"])
            log.record(kind: .powerConnected, metadata: ["state": "charging", "percentage": "72"])
        }
    }

    static func run(overlay: OverlayController) {
        var assertions = 0
        func check(_ value: Bool, _ message: String) { assertions += 1; precondition(value, message) }
        func later(_ delay: TimeInterval, _ body: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: body)
        }
        let log = overlay.eventLog
        check(log.events.isEmpty, "Diagnostics start with a private empty log")
        check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults), "HUD opens")
        later(SystemHUDView.entranceDuration + 0.3) {
            let shell = overlay.systemShellIdentity
            check(log.events.filter { $0.kind == .overlayOpened }.count == 1, "Opening records exactly once")
            overlay.selectSystemModule(.notes, animated: false)
            check(log.events.first?.kind == .moduleOpened && log.events.first?.metadata["module"] == "notes",
                  "Actual module selection records its destination")
            let count = log.events.count
            overlay.selectSystemModule(.notes, animated: false)
            check(log.events.count == count, "Selecting the same module creates no duplicate event")
            overlay.selectSystemModule(.eventLog)
            later(HUDModuleContent.transitionDuration + 0.3) {
                check(overlay.systemSelectedModule == .eventLog && overlay.systemCenterContentCount == 1,
                      "Event Log replaces only the center content")
                check(overlay.systemShellIdentity == shell, "Event Log preserves the live outer shell")
                check(log.events.first?.metadata["module"] == "eventLog", "The settled Event Log destination records once")
                overlay.workMode.chooseCountdown(seconds: 5)
                overlay.workMode.start(); overlay.workMode.pause(); overlay.workMode.resume(); overlay.workMode.reset()
                check(Array(log.events.prefix(4).map(\.kind)) == [.workReset, .workResumed, .workPaused, .workStarted],
                      "Real timer controls feed the recorder in order")
                overlay.closeSystemOverlay()
                later(SystemHUDView.exitDuration + 0.3) {
                    check(overlay.systemPhase == .closed && overlay.lastClosedAnimationCount == 0,
                          "Event Log closes without keeping hidden animations")
                    _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
                    later(SystemHUDView.entranceDuration + 0.3) {
                        check(overlay.systemSelectedModule == .eventLog, "Reopening restores the Event Log module")
                        check(log.events.filter { $0.kind == .overlayOpened }.count == 2,
                              "Reopening records one new opening")
                        check(log.events.filter { $0.kind == .moduleOpened && $0.metadata["module"] == "eventLog" }.count == 1,
                              "Restoring a module during opening does not duplicate a selection action")
                        overlay.forceCloseSystemOverlay()
                        print("PASS: \(assertions) Event Log HUD assertions; real navigation and timer actions; retained shell; private history; close/reopen lifecycle")
                        NSApp.terminate(nil)
                    }
                }
            }
        }
    }
}
