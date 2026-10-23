import AppKit

/// Explicit diagnostics use deterministic readers and never scan user folders.
enum TelemetryHUDVerification {
    static func run(overlay: OverlayController) {
        var assertions = 0
        func check(_ value: Bool, _ message: String) { assertions += 1; precondition(value, message) }
        func later(_ delay: TimeInterval, _ action: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: action)
        }
        func wait(_ label: String, until condition: @escaping () -> Bool, then action: @escaping () -> Void) {
            let deadline = ProcessInfo.processInfo.systemUptime + 10
            func poll() {
                if condition() { action(); return }
                precondition(ProcessInfo.processInfo.systemUptime < deadline, "Timed out: " + label)
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
            }
            poll()
        }
        func ids(_ module: HUDModule) -> [String] { overlay.designControlIDsForVerification(module) }
        func press(_ module: HUDModule, _ id: String) { overlay.performDesignActionForVerification(module, id) }
        func canvas(_ module: HUDModule) -> HUDDesignCanvas {
            NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!.designCanvasForVerification(module)!
        }
        var configuration = AppConfiguration.defaults
        configuration.language = .english; L10n.language = .english
        check(!overlay.activity.isActive && !overlay.storage.isActive, "Readers begin asleep")
        check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD opens")
        check(overlay.systemSelectedModule == .map, "First opening defaults to Map")
        later(SystemHUDView.entranceDuration + 0.3) {
            let shell = overlay.systemShellIdentity
            let host = overlay.systemCenterHostIdentity
            let windows = Set(NSApp.windows.map { ObjectIdentifier($0) })
            check(!overlay.activity.isActive && !overlay.storage.isActive, "Initial Map keeps telemetry at its background cadence")
            overlay.selectSystemModule(.activityMonitor, animated: false)
            check(overlay.activity.isActive && !overlay.storage.isActive && !overlay.appActivity.isActive,
                  "Activity uses its foreground cadence on its Overview page")
            check(overlay.activity.snapshot.cpuPercent != nil && overlay.activity.history.count == 60,
                  "Activity displays the retained bounded fixture")
            check((0..<5).allSatisfy { ids(.activityMonitor).contains("activity.row.\($0)") } && ids(.activityMonitor).contains("activityTabs:1"),
                  "Overview shows its tabs and the five live readouts")
            press(.activityMonitor, "activityTabs:1")
            check(overlay.appActivity.isActive, "The Apps tab starts the per-app readers")
            wait("Apps table", until: { ids(.activityMonitor).contains("activity:sort:name") }) {
                press(.activityMonitor, "activity:sort:name")
                check(overlay.appActivity.sortKey == .name && !overlay.appActivity.sortDescending, "The App column sorts A to Z first")
                press(.activityMonitor, "activityTabs:0")
                check(!overlay.appActivity.isActive && overlay.activity.isActive, "Overview stops the per-app readers")
                wait("Overview", until: { ids(.activityMonitor).contains("activity.row.0") }) {
                    overlay.selectSystemModule(.storage)
                    check(!overlay.activity.isActive && overlay.activity.isRunning && overlay.activity.samplingInterval == 5,
                          "Activity falls back to background sampling during transitions")
                    later(HUDModuleContent.transitionDuration + 0.3) {
                        check(overlay.storage.isActive && !overlay.activity.isActive, "Storage is visible while overall Activity sampling continues slowly")
                        check(overlay.storage.snapshot.capacity != nil, "Storage shows capacity")
                        check(Set(["volume", "refresh", "settings"]).isSubset(of: ids(.storage)), "Disk, Refresh and Storage Settings are live controls")
                        press(.storage, "refresh")
                        let toast = canvas(.storage).context.overlay.toastState.current
                        check(toast?.title == "Readings updated" && toast?.caption == overlay.storage.currentVolume?.name,
                              "Refresh rereads the fixture disk and confirms with a toast")
                        press(.storage, "volume")
                        check(canvas(.storage).isMenuOpen && ids(.storage).contains("volume:fixture-backup"), "The disk menu lists the fixture disks")
                        press(.storage, "volume:fixture-backup")
                        check(!canvas(.storage).isMenuOpen && overlay.storage.currentVolume?.id == "fixture-backup"
                              && overlay.storage.snapshot.capacity?.volumeName == "Backup SSD", "Choosing a disk shows its readings")
                        wait("chosen disk", until: { ids(.storage).contains("settings") }) {
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
                            press(.storage, "settings")
                            check(!overlay.storage.isActive && !overlay.activity.isActive, "Closing removes foreground telemetry ownership immediately")
                            later(SystemHUDView.exitDuration + 0.3) {
                                check(overlay.systemPhase == .closed && overlay.lastClosedAnimationCount == 0,
                                      "Closed telemetry owns no hidden animations")
                                check(settingsOpened, "Storage Settings requests the real settings destination after closing")
                                check(overlay.activity.isRunning && overlay.activity.history == retained, "Closing preserves the history and background lifetime")
                                _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration)
                                later(SystemHUDView.entranceDuration + 0.3) {
                                    check(overlay.systemSelectedModule == .storage && overlay.storage.isActive,
                                          "Reopening restores the last section after the first Activity visit")
                                    check(overlay.storage.currentVolume?.id == "fixture-backup", "Reopening keeps the chosen disk")
                                    check(overlay.activity.history == retained, "Activity history survives HUD recreation")
                                    overlay.selectSystemModule(.activityMonitor, animated: false)
                                    check(overlay.activity.isActive && !overlay.storage.isActive, "Immediate switching transfers reader ownership")
                                    overlay.forceCloseSystemOverlay()
                                    check(!overlay.activity.isActive && !overlay.storage.isActive && !overlay.appActivity.isActive
                                          && overlay.lastClosedAnimationCount == 0, "Forced close cancels foreground telemetry and all animation")
                                    print("PASS: \(assertions) telemetry HUD assertions; default Map, Activity tabs and sorting, per-app reader gating, Storage refresh and disk menu, retained shell, retained history, Storage Settings handoff, foreground/background readers, clean close/reopen")
                                    NSApp.terminate(nil)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
