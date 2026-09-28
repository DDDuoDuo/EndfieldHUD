import AppKit

/// Explicit diagnostics use deterministic readers and never scan user folders.
enum TelemetryHUDVerification {
    static func run(overlay: OverlayController) {
        var assertions = 0
        func check(_ value: Bool, _ message: String) { assertions += 1; precondition(value, message) }
        func later(_ delay: TimeInterval, _ action: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: action)
        }
        check(!overlay.activity.isActive && !overlay.storage.isActive, "Readers begin asleep")
        check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults), "HUD opens")
        check(overlay.systemSelectedModule == .map, "First opening defaults to Map")
        later(SystemHUDView.entranceDuration + 0.3) {
            let shell = overlay.systemShellIdentity
            let host = overlay.systemCenterHostIdentity
            let windows = Set(NSApp.windows.map { ObjectIdentifier($0) })
            check(!overlay.activity.isActive && !overlay.storage.isActive, "Initial Map keeps telemetry at its background cadence")
            overlay.selectSystemModule(.activityMonitor, animated: false)
            check(overlay.activity.isActive && !overlay.storage.isActive, "Activity uses its foreground cadence on its page")
            check(overlay.activity.snapshot.cpuPercent != nil && overlay.activity.history.count == 60,
                  "Activity displays the retained bounded fixture")
            overlay.selectSystemModule(.storage)
            check(!overlay.activity.isActive && overlay.activity.isRunning && overlay.activity.samplingInterval == 5, "Activity falls back to background sampling during transitions")
            later(HUDModuleContent.transitionDuration + 0.3) {
                check(overlay.storage.isActive && !overlay.activity.isActive, "Storage is visible while overall Activity sampling continues slowly")
                check(overlay.storage.snapshot.capacity != nil, "Storage shows capacity")
                let retained = overlay.activity.history
                var settingsOpened = false
                overlay.openSystemStorage = {
                    settingsOpened = true
                    check(overlay.systemPhase == .closed, "Settings handoff waits for the closing animation")
                    return true
                }
                check(overlay.systemShellIdentity == shell && overlay.systemCenterHostIdentity == host,
                      "Report panels retain the shared shell and center host")
                check(Set(NSApp.windows.map { ObjectIdentifier($0) }) == windows, "Details create no new window")
                overlay.performStorageActionForVerification("storage:settings")
                check(!overlay.storage.isActive && !overlay.activity.isActive, "Closing removes foreground telemetry ownership immediately")
                later(SystemHUDView.exitDuration + 0.3) {
                    check(overlay.systemPhase == .closed && overlay.lastClosedAnimationCount == 0,
                          "Closed telemetry owns no hidden animations")
                    check(settingsOpened, "Storage action requests the real settings destination after closing")
                    check(overlay.activity.isRunning && overlay.activity.history == retained, "Closing preserves the history and background lifetime")
                    _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
                    later(SystemHUDView.entranceDuration + 0.3) {
                        check(overlay.systemSelectedModule == .storage && overlay.storage.isActive,
                              "Reopening restores the last section after the first Activity visit")
                        check(overlay.activity.history == retained, "Activity history survives HUD recreation")
                        overlay.selectSystemModule(.activityMonitor, animated: false)
                        check(overlay.activity.isActive && !overlay.storage.isActive, "Immediate switching transfers reader ownership")
                        overlay.forceCloseSystemOverlay()
                        check(!overlay.activity.isActive && !overlay.storage.isActive && overlay.lastClosedAnimationCount == 0,
                              "Forced close cancels foreground telemetry and all animation")
                        print("PASS: \(assertions) telemetry HUD assertions; default Map, explicit Activity selection, retained shell, retained history, Settings handoff, foreground/background readers, clean close/reopen")
                        NSApp.terminate(nil)
                    }
                }
            }
        }
    }
}
