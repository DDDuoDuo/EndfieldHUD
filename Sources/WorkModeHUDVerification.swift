import AppKit

/// Isolated graphical integration checks. --ui-test selects a fixture audio
/// backend and private clipboard before this helper can be invoked.
enum WorkModeHUDVerification {
    static func run(overlay: OverlayController) {
        var assertions = 0
        func check(_ value: Bool, _ message: String) {
            assertions += 1
            precondition(value, message)
        }
        func later(_ delay: TimeInterval, _ body: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: body)
        }
        let work = overlay.workMode
        let reduced = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        check(work.chooseCountdown(seconds: 4), "A short diagnostic countdown must be valid")
        check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults), "HUD must open")
        later(SystemHUDView.entranceDuration + 0.3) {
            overlay.selectSystemModule(.workMode, animated: false)
            check(overlay.systemWorkModeDialDiameterForVerification == 430, "Work Mode preserves the full 430-point countdown ring in the shared HUD")
            check(overlay.systemReportGeometryMatchesSelectionForVerification, "Full-size timer rendering and projected input share the same geometry")
            let shell = overlay.systemShellIdentity
            let host = overlay.systemCenterHostIdentity
            work.start()
            later(WorkModeCanvas.layoutTransitionDuration + 0.15) {
                check(work.snapshot.phase == .running, "Countdown enters Work Mode")
                check(reduced ? overlay.systemWorkModeAnimationCount == 0 : overlay.systemWorkModeAnimationCount == 1,
                      "Visible Work Mode owns one bounded animation unless motion is reduced")
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
                            work.chooseStopwatch(); work.start()
                            later(0.4) {
                                check(work.snapshot.elapsed >= 0.3, "Hidden stopwatch measures elapsed continuous time")
                                _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
                                later(SystemHUDView.entranceDuration + 0.3) {
                                    overlay.selectSystemModule(.workMode, animated: false)
                                    check(work.snapshot.kind == .stopwatch && work.snapshot.elapsed >= 1,
                                          "Reopening restores the continuing stopwatch")
                                    work.pause()
                                    check(work.snapshot.phase == .paused && overlay.systemWorkModeAnimationCount == 0,
                                          "Pause freezes the timer and its motion")
                                    work.resume()
                                    check(reduced ? overlay.systemWorkModeAnimationCount == 0 : overlay.systemWorkModeAnimationCount == 1,
                                          "Resume restores a single marker track")
                                    work.reset()
                                    check(!work.snapshot.isActive && overlay.systemWorkModeAnimationCount == 0,
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
                                        print("PASS: \(assertions) Work Mode HUD assertions; countdown completion while hidden; continuing stopwatch; pause/resume/reset; suspension catch-up; stable shell; one visible timer track; audio observers and animations removed on hide")
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
