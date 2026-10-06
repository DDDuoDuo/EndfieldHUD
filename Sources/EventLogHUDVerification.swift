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
        precondition(CommandLine.arguments.contains("--ui-test"))
        var physicalInputMonitor: Any?
        func cleanup() {
            if let physicalInputMonitor { NSEvent.removeMonitor(physicalInputMonitor) }
            physicalInputMonitor = nil
            overlay.systemPointerLocationProviderForVerification = nil
        }
        var assertions = 0
        func check(_ value: Bool, _ message: String) {
            assertions += 1
            if !value { cleanup(); preconditionFailure(message) }
        }
        func later(_ delay: TimeInterval, _ body: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: body)
        }
        func awaitPhase(_ phase: SystemOverlayPhase, then completion: @escaping () -> Void) {
            let deadline = ProcessInfo.processInfo.systemUptime + 8
            func poll() {
                if overlay.systemPhase == phase { completion(); return }
                if ProcessInfo.processInfo.systemUptime >= deadline {
                    check(false, "Timed out waiting for \(phase); current phase is \(overlay.systemPhase)")
                    return
                }
                later(0.03, poll)
            }
            later(0.03, poll)
        }
        // This fixture invokes navigation directly. Physical clicks and keys
        // must not dismiss or retarget its HUD while those assertions run.
        physicalInputMonitor = NSEvent.addLocalMonitorForEvents(matching: [
            .mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate,
            .leftMouseDown, .leftMouseUp, .leftMouseDragged,
            .rightMouseDown, .rightMouseUp, .rightMouseDragged,
            .otherMouseDown, .otherMouseUp, .otherMouseDragged,
            .scrollWheel, .magnify, .rotate, .swipe, .pressure,
            .keyDown, .keyUp, .flagsChanged
        ]) { _ in nil }
        if let screen = NSScreen.main?.frame {
            let pointer = CGPoint(x: screen.midX, y: screen.midY)
            overlay.systemPointerLocationProviderForVerification = { pointer }
        }
        var configuration = AppConfiguration.defaults
        configuration.closeOnFocusLost = false
        let log = overlay.eventLog
        check(log.events.isEmpty, "Diagnostics start with a private empty log")
        check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD opens")
        awaitPhase(.open) {
            let shell = overlay.systemShellIdentity
            check(log.events.isEmpty, "Opening creates no routine event")
            overlay.selectSystemModule(.notes, animated: false)
            check(log.events.isEmpty, "Module selection creates no routine event")
            let count = log.events.count
            overlay.selectSystemModule(.notes, animated: false)
            check(log.events.count == count, "Selecting the same module creates no duplicate event")
            overlay.selectSystemModule(.eventLog)
            later(HUDModuleContent.transitionDuration + 0.3) {
                check(overlay.systemSelectedModule == .eventLog && overlay.systemCenterContentCount == 1,
                      "Event Log replaces only the center content")
                check(overlay.systemShellIdentity == shell, "Event Log preserves the live outer shell")
                check(log.events.isEmpty, "Opening Event Log does not log itself")
                overlay.workMode.chooseCountdown(seconds: 5)
                overlay.workMode.start(); overlay.workMode.pause(); overlay.workMode.resume(); overlay.workMode.reset()
                check(Array(log.events.prefix(4).map(\.kind)) == [.workReset, .workResumed, .workPaused, .workStarted],
                      "Real timer controls feed the recorder in order")
                overlay.closeSystemOverlay()
                awaitPhase(.closed) {
                    check(overlay.systemPhase == .closed && overlay.lastClosedAnimationCount == 0,
                          "Event Log closes without keeping hidden animations")
                    check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD reopens")
                    awaitPhase(.open) {
                        check(overlay.systemSelectedModule == .eventLog, "Reopening restores the Event Log module")
                        check(!log.events.contains { $0.kind == .overlayOpened },
                              "Reopening does not record a routine opening")
                        check(!log.events.contains { $0.kind == .moduleOpened },
                              "Restoring a module does not record navigation")
                        overlay.forceCloseSystemOverlay()
                        cleanup()
                        print("PASS: \(assertions) Event Log HUD assertions; real navigation and timer actions; retained shell; private history; close/reopen lifecycle")
                        NSApp.terminate(nil)
                    }
                }
            }
        }
    }
}
