import AppKit

/// Explicit, isolated live-window checks. This harness never terminates the
/// application itself; the caller owns completion and the diagnostic session.
enum HUDLifecycleVerification {
    static func run(overlay: OverlayController, configuration: AppConfiguration,
                    completion: @escaping () -> Void) {
        Session(overlay: overlay, configuration: configuration, completion: completion).start()
    }

    private final class Session {
        private let overlay: OverlayController
        private var configuration: AppConfiguration
        private let completion: () -> Void
        private var assertions = 0
        private var normalCloses = 0
        private var acceptedQuits = 0
        private var completedQuits = 0
        private var genericHandoffs = 0
        private var finished = false
        private var reduced = false
        private var screenPointer = CGPoint.zero
        private var openingPointer = CGPoint.zero
        private var windowIDs = Set<ObjectIdentifier>()
        private let priorPointerProvider: (() -> CGPoint)?
        private let priorClosed: (() -> Void)?
        private let priorQuitAccepted: (() -> Void)?
        private let priorQuitClosed: (() -> Void)?
        private let priorAfterClose: (() -> Void)?
        private let snapshot = BatterySnapshot(percentage: 75, isPluggedIn: true, isCharging: true,
            isFullyCharged: false, hasBattery: true,
            capacity: BatteryCapacityReading(current: 3600, maximum: 4800, unit: .milliampHours))

        init(overlay: OverlayController, configuration: AppConfiguration, completion: @escaping () -> Void) {
            self.overlay = overlay; self.configuration = configuration; self.completion = completion
            priorPointerProvider = overlay.systemPointerLocationProviderForVerification
            priorClosed = overlay.onSystemClosed
            priorQuitAccepted = overlay.onQuitAccepted
            priorQuitClosed = overlay.onQuitAfterSystemClose
            priorAfterClose = overlay.afterSystemClose
        }

        func start() {
            precondition(Thread.isMainThread)
            check(overlay.systemPhase == .closed, "Lifecycle verification starts with a closed HUD")
            configuration.reduceMotion = false
            configuration.lowPowerVisualMode = false
            configuration.ambientAnimation = true
            configuration.closeOnFocusLost = true
            configuration.parallaxIntensity = 1
            configuration.perspectiveIntensity = 1
            let screen = HUDDisplayPolicy.targetScreen(configuration: configuration, screens: NSScreen.screens)
                ?? NSScreen.main
            guard let screen else { preconditionFailure("Lifecycle verification requires an attached display") }
            screenPointer = CGPoint(x: screen.frame.midX, y: screen.frame.midY)
            overlay.systemPointerLocationProviderForVerification = { [weak self] in self?.screenPointer ?? .zero }
            overlay.onSystemClosed = { [weak self] in self?.normalCloses += 1 }
            overlay.onQuitAccepted = { [weak self] in self?.acceptedQuits += 1 }
            overlay.onQuitAfterSystemClose = { [weak self] in
                guard let self else { return }
                self.completedQuits += 1
                self.check(self.overlay.systemPhase == .closed && !self.overlay.systemWindowVisibleForVerification
                           && self.overlay.systemShellIdentity == nil,
                           "Quit callback follows panel hiding and presentation release")
                self.check(self.overlay.lastClosedAnimationCount == 0,
                           "Quit callback follows removal of every hidden HUD animation")
            }
            overlay.afterSystemClose = nil
            overlay.initialModuleRequest = .power
            check(overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration), "The fixture HUD opens")
            reduced = HUDRuntimeAppearance.reduceMotion
            windowIDs = currentWindowIDs()
            check(overlay.systemWindowVisibleForVerification, "Opening orders the existing HUD panel on screen")
            if !reduced {
                later(0.08) { [self] in
                    check(overlay.systemPhase == .opening, "Pointer check runs during deployment")
                    openingPointer = movePointer(x: 180, y: 95)
                    checkTransitionMotion(openingPointer, phase: .opening)
                }
                later(0.22) { [self] in
                    openingPointer = movePointer(x: -290, y: -160)
                    checkTransitionMotion(openingPointer, phase: .opening)
                }
            }
            later(SystemHUDView.entranceDuration + 0.20) { [self] in checkOpenAndCancel() }
        }

        private func checkOpenAndCancel() {
            check(overlay.systemPhase == .open, "Deployment completes before normal controls activate")
            if !reduced {
                check(overlay.systemPointerTargetForVerification == openingPointer
                      && overlay.systemSpatialPoseMatchesPointerForVerification(openingPointer),
                      "Promotion to the fully open HUD preserves the latest deployment pointer pose")
            }
            check(overlay.systemParallaxAnimationCount <= 13, "Open promotion cannot duplicate pointer tracks")
            overlay.selectSystemModule(.eventLog, animated: false)
            let shell = overlay.systemShellIdentity
            let section = overlay.systemSelectedModule
            overlay.presentQuitConfirmationForVerification()
            check(overlay.systemQuitConfirmationVisibleForVerification && currentWindowIDs() == windowIDs,
                  "Power confirmation stays inside the existing HUD window")
            overlay.answerQuitConfirmationForVerification(false)
            later(0.25) { [self] in
                check(!overlay.systemQuitConfirmationVisibleForVerification && overlay.systemPhase == .open,
                      "Cancel dismisses only the quit confirmation")
                check(overlay.systemShellIdentity == shell && overlay.systemSelectedModule == section
                      && currentWindowIDs() == windowIDs,
                      "Cancel retains the same HUD, section and native windows")
                check(acceptedQuits == 0 && completedQuits == 0 && normalCloses == 0,
                      "Cancel neither accepts quit nor closes the HUD")
                closeWithPointerMotion()
            }
        }

        private func closeWithPointerMotion() {
            overlay.closeSystemOverlay()
            check(overlay.systemPhase == .closing, "Ordinary close starts retraction synchronously")
            if !reduced {
                later(min(0.08, SystemHUDView.exitDuration * 0.20)) { [self] in
                    let point = movePointer(x: 210, y: -85)
                    checkTransitionMotion(point, phase: .closing)
                }
            }
            later(SystemHUDView.exitDuration + 0.25) { [self] in
                checkCleanClose("Ordinary retraction")
                check(normalCloses == 1 && acceptedQuits == 0 && completedQuits == 0,
                      "Ordinary close retains its normal callback without requesting application quit")
                check(overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration),
                      "Ordinary HUD close allows a later summon")
                later(SystemHUDView.entranceDuration + 0.20) { [self] in checkUnansweredFocusClose() }
            }
        }

        private func checkUnansweredFocusClose() {
            check(overlay.systemPhase == .open && overlay.systemSelectedModule == .eventLog,
                  "Reopening retains the section from before a cancelled quit")
            overlay.presentQuitConfirmationForVerification()
            check(overlay.systemQuitConfirmationVisibleForVerification, "A later unanswered quit prompt is visible")
            overlay.closeSystemOverlayForFocusLoss()
            check(overlay.systemPhase == .closing && !overlay.systemQuitConfirmationVisibleForVerification,
                  "Focus dismissal cancels an unanswered prompt before retraction")
            overlay.answerQuitConfirmationForVerification(true)
            check(acceptedQuits == 0 && completedQuits == 0,
                  "A stale confirmation cannot accept quit after focus loss cancelled the prompt")
            later(SystemHUDView.exitDuration + 0.25) { [self] in
                checkCleanClose("Focus-loss retraction")
                check(normalCloses == 2 && acceptedQuits == 0 && completedQuits == 0,
                      "Unanswered focus dismissal follows the ordinary close path")
                check(overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration),
                      "An unanswered quit never permanently disables summon")
                later(SystemHUDView.entranceDuration + 0.20) { [self] in checkAcceptedQuit() }
            }
        }

        private func checkAcceptedQuit() {
            check(overlay.systemPhase == .open, "Accepted-quit cycle starts from an open HUD")
            overlay.afterSystemClose = { [weak self] in self?.genericHandoffs += 1 }
            overlay.presentQuitConfirmationForVerification()
            check(overlay.systemQuitConfirmationVisibleForVerification, "Final quit requires its own confirmation")
            overlay.answerQuitConfirmationForVerification(true)
            check(acceptedQuits == 1 && overlay.systemPhase == .closing && completedQuits == 0,
                  "Confirm accepts once and starts closing without prematurely invoking quit")
            overlay.answerQuitConfirmationForVerification(true)
            check(!overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration),
                  "Summon cannot reopen an accepted application quit")
            check(acceptedQuits == 1 && completedQuits == 0,
                  "Repeated confirmation cannot duplicate acceptance or bypass retraction")
            later(SystemHUDView.exitDuration + 0.30) { [self] in
                checkCleanClose("Accepted quit")
                check(completedQuits == 1 && acceptedQuits == 1 && normalCloses == 2 && genericHandoffs == 0,
                      "Accepted quit fires exactly once without charging restore or a generic after-close action")
                check(!overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration),
                      "Accepted quit stays closed while application cleanup is pending")
                overlay.forceCloseSystemOverlay()
                later(0.25) { [self] in
                    check(completedQuits == 1 && normalCloses == 2 && genericHandoffs == 0,
                          "Repeated teardown and late animation deadlines cannot fire another completion")
                    finish()
                }
            }
        }

        private func movePointer(x: CGFloat, y: CGFloat) -> CGPoint {
            screenPointer.x += x; screenPointer.y += y
            guard let point = overlay.systemCurrentPointerTargetForVerification else {
                preconditionFailure("The live HUD must provide its normalized pointer target")
            }
            overlay.setSystemPointerForVerification(point)
            return point
        }

        private func checkTransitionMotion(_ point: CGPoint, phase: SystemOverlayPhase) {
            check(overlay.systemPhase == phase && overlay.systemPointerTargetForVerification == point
                  && overlay.systemSpatialPoseMatchesPointerForVerification(point),
                  "Pointer input updates spatial planes during \(phase.rawValue)")
            check(overlay.systemParallaxAnimationCount > 0 && overlay.systemParallaxAnimationCount <= 13,
                  "Transition pointer response retains at most thirteen finite plane tracks")
            check(overlay.systemAmbientAnimationCount == 0,
                  "Transition pointer response does not start or retain ambient loops")
        }

        private func checkCleanClose(_ label: String) {
            check(overlay.systemPhase == .closed && !overlay.systemWindowVisibleForVerification
                  && overlay.systemShellIdentity == nil && overlay.lastClosedAnimationCount == 0,
                  "\(label) releases the hidden presentation and all animation tracks")
        }

        private func currentWindowIDs() -> Set<ObjectIdentifier> { Set(NSApp.windows.map { ObjectIdentifier($0) }) }
        private func check(_ value: Bool, _ message: String) { assertions += 1; precondition(value, message) }
        private func later(_ delay: TimeInterval, _ action: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: action)
        }
        private func finish() {
            guard !finished else { return }; finished = true
            overlay.systemPointerLocationProviderForVerification = priorPointerProvider
            overlay.onSystemClosed = priorClosed
            overlay.onQuitAccepted = priorQuitAccepted
            overlay.onQuitAfterSystemClose = priorQuitClosed
            overlay.afterSystemClose = priorAfterClose
            print("PASS: \(assertions) HUD lifecycle assertions; pointer follow-through during opening/closing, bounded transition tracks, pose continuity, in-HUD quit cancellation, focus-loss cancellation, close-before-quit and one-shot cleanup\(reduced ? "; transition midpoint checks skipped for system Reduce Motion" : "")")
            fflush(stdout)
            completion()
        }
    }
}
