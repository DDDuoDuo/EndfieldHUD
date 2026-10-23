import AppKit

/// Isolated graphical integration checks. --ui-test selects a fixture audio
/// backend and private clipboard before this helper can be invoked.
enum WorkModeHUDVerification {
    static func run(overlay: OverlayController) {
        var assertions = 0
        func check(_ value: Bool, _ message: String) {
            assertions += 1
            guard !value else { return }
            fputs("FAIL: Work Mode \(assertions): \(message)\n", stderr)
            preconditionFailure(message)
        }
        func later(_ delay: TimeInterval, _ body: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: body)
        }
        func wait(_ label: String, until condition: @escaping () -> Bool, then body: @escaping () -> Void) {
            let deadline = ProcessInfo.processInfo.systemUptime + 10
            func poll() {
                if condition() { body(); return }
                if ProcessInfo.processInfo.systemUptime >= deadline { check(false, "Timed out: " + label) }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
            }
            poll()
        }
        func canvas() -> WorkModeCanvas {
            NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!.designCanvasForVerification(.workMode) as! WorkModeCanvas
        }
        func ids() -> [String] { overlay.designControlIDsForVerification(.workMode) }
        func item(_ id: String) -> HUDAccessibilityItem? { canvas().accessibilityItems.first { $0.id == id } }
        func press(_ id: String) { overlay.performDesignActionForVerification(.workMode, id) }
        let work = overlay.workMode
        let reduced = HUDRuntimeAppearance.reduceMotion
        var configuration = AppConfiguration.defaults
        configuration.language = .english; L10n.language = .english
        check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD must open")
        later(SystemHUDView.entranceDuration + 0.3) {
            overlay.selectSystemModule(.workMode, animated: false)
            wait("Work Mode page", until: { ids().contains("workMode.primary") }) {
                check(overlay.systemWorkModeDialDiameterForVerification == WorkModeDialGeometry.radius * 2,
                      "Work Mode draws its full 620 px countdown ring around the disc")
                check(overlay.systemReportGeometryMatchesSelectionForVerification, "Full-size timer rendering and projected input share the same geometry")
                check(Set(["workMode.kind:0", "workMode.kind:1", "workMode.preset:0", "workMode.preset:3", "workMode.reset"]).isSubset(of: ids()),
                      "Mode tabs, duration presets and Reset are live controls")
                let shell = overlay.systemShellIdentity
                let host = overlay.systemCenterHostIdentity
                press("workMode.preset:0")
                check(work.snapshot.kind == .countdown && work.snapshot.duration == WorkModeCanvas.presets[0] && work.snapshot.phase == .idle,
                      "A preset sets the countdown total without starting it")
                press("workMode.preset:3")
                let field = canvas().customFieldForTesting
                check(canvas().isEditingCustom && field.isEditing && !ids().contains("workMode.preset:0"), "Custom swaps the presets for its editor")
                field.setText("0:04"); field.endEditing(commit: true)
                check(work.snapshot.duration == 4 && work.snapshot.phase == .idle && item("workMode.preset:3")?.role == .tab(selected: true),
                      "A min:sec custom total selects the Custom segment")
                check(canvas().context.overlay.toastState.current?.title == "Custom " + WorkModeCanvas.durationTitle(4),
                      "Custom totals confirm with a toast")
                wait("custom toast", until: { canvas().context.overlay.toastState.current == nil }) {
                    press("workMode.primary")
                    check(work.snapshot.phase == .running && ids().contains("workMode.reset") && item("workMode.kind:1")?.enabled == false,
                          "Start runs the countdown and locks the mode tabs")
                    later(WorkModeCanvas.layoutTransitionDuration + 0.15) {
                        check(work.snapshot.phase == .running, "Countdown enters Work Mode")
                        check(reduced ? overlay.systemWorkModeAnimationCount == 0 : overlay.systemWorkModeAnimationCount == 1,
                              "Visible Work Mode owns one FOCUS ON blink unless motion is reduced")
                        check(overlay.systemDeploymentAnimationCount == 0,
                              "Continuous timer motion is separate from deployment: \(overlay.systemFiniteAnimationKeys)")
                        overlay.selectSystemModule(.volume)
                        later(HUDModuleContent.transitionDuration + 0.2) {
                            check(overlay.systemSelectedModule == .volume && overlay.audio.isRunning,
                                  "The visible Volume page attaches its audio observers")
                            check(overlay.systemShellIdentity == shell && overlay.systemCenterHostIdentity == host,
                                  "Changing modules retains shell and center host")
                            check(overlay.systemWorkModeAnimationCount == 0 && work.snapshot.phase == .running,
                                  "A hidden timer continues its session without animating")
                            overlay.closeSystemOverlay()
                            later(SystemHUDView.exitDuration + 0.2) {
                                check(overlay.systemPhase == .closed && !overlay.audio.isRunning,
                                      "Closing the HUD detaches Volume observers")
                                check(overlay.lastClosedAnimationCount == 0 && work.snapshot.isActive,
                                      "Closing removes animations without ending the countdown")
                                later(3.1) {
                                    check(work.snapshot.phase == .completed && !work.snapshot.isActive,
                                          "The hidden countdown deadline exits Work Mode")
                                    checkStopwatch()
                                }
                            }
                        }
                    }
                }
            }
        }
        func checkStopwatch() {
            _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration)
            later(SystemHUDView.entranceDuration + 0.3) {
                overlay.selectSystemModule(.workMode, animated: false)
                wait("finished countdown", until: { ids().contains("workMode.kind:1") }) {
                    check(canvas().timerTextForTesting == "00:00" && item("workMode.primary")?.label.hasPrefix("Start") == true,
                          "A countdown finished while hidden shows zero and offers Start")
                    press("workMode.kind:1")
                    check(work.snapshot.kind == .stopwatch && work.snapshot.phase == .idle, "The Stopwatch tab switches an idle timer's mode")
                    wait("stopwatch page", until: { ids().contains("workMode.lap") }) {
                        check(item("workMode.lap")?.enabled == false, "Lap waits until the stopwatch runs")
                        press("workMode.primary")
                        press("workMode.lap")
                        check(work.snapshot.phase == .running && canvas().lapTextsForTesting.count == 1, "A running stopwatch records laps")
                        overlay.closeSystemOverlay()
                        later(SystemHUDView.exitDuration + 0.2) {
                            check(overlay.systemPhase == .closed && overlay.lastClosedAnimationCount == 0 && work.snapshot.phase == .running,
                                  "Closing keeps the stopwatch running without animation")
                            let hidden = work.snapshot.elapsed
                            later(0.4) {
                                check(work.snapshot.elapsed - hidden >= 0.3, "Hidden stopwatch measures elapsed continuous time")
                                _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration)
                                later(SystemHUDView.entranceDuration + 0.3) {
                                    overlay.selectSystemModule(.workMode, animated: false)
                                    wait("continuing stopwatch", until: { ids().contains("workMode.lap") }) {
                                        check(work.snapshot.kind == .stopwatch && work.snapshot.phase == .running && work.snapshot.elapsed >= 1,
                                              "Reopening restores the continuing stopwatch")
                                        press("workMode.primary")
                                        check(work.snapshot.phase == .paused && overlay.systemWorkModeAnimationCount == 0 && ids().contains("workMode.reset"),
                                              "Pause freezes the timer and its motion and offers Reset")
                                        press("workMode.primary")
                                        check(work.snapshot.phase == .running
                                              && (reduced ? overlay.systemWorkModeAnimationCount == 0 : overlay.systemWorkModeAnimationCount == 1),
                                              "Resume restores a single blink track")
                                        press("workMode.primary"); press("workMode.reset")
                                        check(!work.snapshot.isActive && work.snapshot.phase == .idle && overlay.systemWorkModeAnimationCount == 0,
                                              "Reset exits Work Mode without residual motion")
                                        overlay.forceCloseSystemOverlay()
                                        work.setSuspended(true)
                                        _ = work.chooseCountdown(seconds: 1); work.start()
                                        later(1.2) {
                                            work.setSuspended(false)
                                            check(work.snapshot.phase == .completed && !work.snapshot.isActive,
                                                  "Resume reconciles a countdown elapsed during suspension")
                                            check(overlay.systemAnimationCount == 0 && overlay.lastClosedAnimationCount == 0,
                                                  "Suspension catch-up cannot revive a hidden HUD")
                                            print("PASS: \(assertions) Work Mode HUD assertions; real presets, custom total, mode tabs, Start/Pause/Resume/Lap/Reset controls; countdown completion while hidden; continuing stopwatch; suspension catch-up; stable shell; one visible blink track; audio observers and animations removed on hide")
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
    }
}
