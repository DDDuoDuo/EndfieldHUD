import AppKit

/// Live-window checks use the real renderer timeline and isolated app stores.
enum ChargeBadgeHUDVerification {
    static func run(overlay: OverlayController) {
        var assertions = 0
        func check(_ value: Bool, _ message: String) { assertions += 1; precondition(value, message) }
        func later(_ delay: TimeInterval, _ action: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: action)
        }
        let reduced = HUDRuntimeAppearance.reduceMotion
        check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults), "HUD opens")
        NSApp.windows.filter { $0 is NSPanel }.forEach { $0.ignoresMouseEvents = true }
        let shell = overlay.systemShellIdentity
        let entrance = reduced ? 0 : HUDChargeBadge.entranceDuration
        if !reduced {
            later(0.3) {
                check(overlay.systemChargeStageForVerification == .hidden,
                      "Charge reveal waits until the main HUD has deployed")
                check(overlay.systemChargeHitRectForVerification.isEmpty, "Delayed badge cannot capture blank-space clicks")
            }
        }
        later(entrance + 0.25) {
            check(overlay.systemPhase == .open && overlay.systemChargeStageForVerification == .compact,
                  "Original charge sequence ends as a compact capsule")
            check(overlay.systemShellIdentity == shell, "Charge lifecycle retains the shell")
            check(abs(overlay.systemChargeHitRectForVerification.width - HUDChargeBadge.compactHitRect.width) < 0.5,
                  "Rendered capsule and projected accessibility region agree")
        }
        later(entrance + HUDChargeBadge.compactHoldDuration - 0.15) {
            check(overlay.systemChargeStageForVerification == .compact, "Capsule stays expanded for the complete three-second hold")
        }
        later(entrance + HUDChargeBadge.compactHoldDuration + 0.4) {
            check(overlay.systemChargeStageForVerification == .circle, "Unhovered badge collapses after the fixed hold")
            let circle = overlay.systemChargeHitRectForVerification
            check(abs(circle.width - circle.height) < 0.5 && circle.width < 30,
                  "Resting badge owns only its visible circular hit area")
            overlay.setSystemChargeHoveredForVerification(true)
            later(0.35) {
                check(overlay.systemChargeStageForVerification == .compact, "Hover expands the resting badge")
                check(abs(overlay.systemChargeHitRectForVerification.width - HUDChargeBadge.compactHitRect.width) < 0.5,
                      "Hover animation and native hit frame reach the expanded capsule")
                overlay.setSystemChargeHoveredForVerification(false)
                later(0.35) {
                    check(overlay.systemChargeStageForVerification == .circle, "Pointer exit returns to the circle")
                    overlay.selectSystemModule(.display, animated: false)
                    overlay.selectSystemModule(.power)
                    later(HUDModuleContent.transitionDuration + 0.2) {
                        check(overlay.systemChargeStageForVerification == .circle,
                              "Changing sections does not reset the compact hold timer")
                        overlay.closeSystemOverlay()
                        if !reduced {
                            check(overlay.systemChargeFollowsDialRetractionForVerification,
                                  "Battery retraction follows the dial's closing transform and timing")
                        }
                        later(SystemHUDView.exitDuration + 0.25) {
                            check(overlay.systemPhase == .closed && overlay.lastClosedAnimationCount == 0,
                                  "Circle close completes without hidden flicker or morph animations")
                            _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
                            overlay.closeSystemOverlay()
                            later(SystemHUDView.entranceDuration + SystemHUDView.exitDuration + 0.35) {
                                check(overlay.systemPhase == .closed && overlay.lastClosedAnimationCount == 0,
                                      "Queued close cancels a delayed badge reveal")
                                print("PASS: \(assertions) charge HUD assertions; delayed entrance, fixed three-second hold, circular hit area, hover expansion/retraction, section continuity, clean closing and cancellation")
                                NSApp.terminate(nil)
                            }
                        }
                    }
                }
            }
        }
    }
}
