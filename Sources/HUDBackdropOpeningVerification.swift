import AppKit

/// Exercises the real view/host preparation boundary with supplied completion
/// timing. No desktop capture, capture inventory or permission API is invoked.
enum HUDBackdropOpeningVerification {
    static func run(overlay: OverlayController, configuration: AppConfiguration,
                    completion: @escaping () -> Void) {
        Session(overlay: overlay, configuration: configuration, completion: completion).start()
    }

    private final class Session {
        let overlay: OverlayController
        var configuration: AppConfiguration
        let completion: () -> Void
        let priorPointer: (() -> CGPoint)?
        var assertions = 0
        var suppliedCompletion: (() -> Void)?
        let snapshot = BatterySnapshot(percentage: 75, isPluggedIn: true, isCharging: true,
            isFullyCharged: false, hasBattery: true,
            capacity: BatteryCapacityReading(current: 3600, maximum: 4800, unit: .milliampHours))

        init(overlay: OverlayController, configuration: AppConfiguration, completion: @escaping () -> Void) {
            self.overlay = overlay; self.configuration = configuration; self.completion = completion
            priorPointer = overlay.systemPointerLocationProviderForVerification
        }

        func start() {
            precondition(Thread.isMainThread && CommandLine.arguments.contains("--ui-test"))
            check(overlay.systemPhase == .closed, "Preparation checks begin with a closed HUD")
            configuration.reduceMotion = false
            configuration.lowPowerVisualMode = false
            configuration.ambientAnimation = false
            configuration.closeOnFocusLost = false
            overlay.systemBackdropPreparationForVerification = { [weak self] ready in
                self?.later(1.3, ready)
            }
            open()
            later(1.05) { [self] in
                check(overlay.systemPhase == .opening && source.backdropPreparingForVerification,
                      "A delayed input outlives the normal host opening deadline without completing early")
                check(source.playback.phase == .opening && !source.hasDisplayTimerForVerification,
                      "The source initial pose remains held without a display loop")
            }
            later(1.3 + SystemHUDView.entranceDuration + 0.30) { [self] in
                check(overlay.systemPhase == .open && !source.backdropPreparingForVerification
                      && source.playback.phase == .visible,
                      "Prepared input starts the original animation and completes the host")
                close { [self] in checkCancelledInput() }
            }
        }

        var source: HUDSourceWatchView {
            guard let view = overlay.systemSourceWatchForVerification else {
                fail("Original Watch unavailable: \(overlay.systemSourceFailureForVerification ?? "missing")")
            }
            return view
        }

        func open() {
            overlay.initialModuleRequest = .power
            check(overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration),
                  "The original HUD accepts a preparation-fixture opening")
            check(source.backdropPreparingForVerification, "Opening enters the real pending-input path")
        }

        func close(_ next: @escaping () -> Void) {
            let heldView = overlay.systemSourceWatchForVerification
            let heldOpening = heldView?.backdropPreparingForVerification == true
            let frameCount = heldView?.renderedFrameCount
            overlay.closeSystemOverlay()
            check(overlay.systemPhase == .closing || overlay.systemPhase == .closed,
                  "Dismissal starts immediately, including while an input is pending")
            if let view = overlay.systemSourceWatchForVerification {
                check(!view.backdropPreparingForVerification,
                      "Dismissal cancels the pending input and its preparation deadline")
                if heldOpening {
                    check(view.playback.phase == .concealed && view.isHidden && view.renderedFrameCount == frameCount,
                          "Cancelling a held initial pose never renders the fully deployed exit pose")
                }
            }
            later(SystemHUDView.exitDuration + 0.30) { [self] in
                check(overlay.systemPhase == .closed && !overlay.systemWindowVisibleForVerification
                      && !overlay.lastClosedSourceTimerActive && overlay.lastClosedAnimationCount == 0,
                      "Closing removes the presentation and every hidden animation")
                next()
            }
        }

        func checkCancelledInput() {
            overlay.systemBackdropPreparationForVerification = { [weak self] ready in self?.suppliedCompletion = ready }
            open()
            later(0.15) { [self] in
                check(source.backdropPreparingForVerification, "Cancellation occurs before an input is supplied")
                close { [self] in
                    suppliedCompletion?(); suppliedCompletion = nil
                    later(0.15) { [self] in
                        check(overlay.systemPhase == .closed && !overlay.systemWindowVisibleForVerification,
                              "A late cancelled input cannot reopen the HUD")
                        checkTimedOutInput()
                    }
                }
            }
        }

        func checkTimedOutInput() {
            overlay.systemBackdropPreparationForVerification = { [weak self] ready in self?.suppliedCompletion = ready }
            open()
            let timeout = HUDSourceWatchView.backdropPreparationTimeout
            later(timeout - 0.20) { [self] in
                check(overlay.systemPhase == .opening && source.backdropPreparingForVerification,
                      "A missing input stays held until its own bounded deadline")
            }
            later(timeout + SystemHUDView.entranceDuration + 0.35) { [self] in
                check(overlay.systemPhase == .open && !source.backdropPreparingForVerification
                      && source.playback.phase == .visible,
                      "The preparation deadline starts fallback and completes the original opening")
                check(source.backdropDiagnostics.contains { $0.contains("timed out") },
                      "The bounded fallback records its actual preparation timeout")
                let start = source.backdropStartForVerification
                suppliedCompletion?(); suppliedCompletion = nil
                check(source.backdropStartForVerification == start && source.playback.phase == .visible,
                      "An input arriving after timeout cannot reset the completed opening clock")
                close { [self] in
                    overlay.systemBackdropPreparationForVerification = nil
                    overlay.systemPointerLocationProviderForVerification = priorPointer
                    print("PASS: \(assertions) source backdrop opening assertions; delayed ready, close-before-ready, bounded timeout and rejected late completion; no capture APIs")
                    fflush(stdout)
                    completion()
                }
            }
        }

        func later(_ delay: TimeInterval, _ action: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: action)
        }

        func check(_ condition: Bool, _ message: String) {
            assertions += 1
            if !condition { fail(message) }
        }

        func fail(_ message: String) -> Never {
            fputs("FAIL: Source backdrop opening: \(message); host=\(overlay.systemPhase.rawValue), source=\(String(describing: overlay.systemSourceWatchForVerification?.playback.phase))\n", stderr)
            fflush(stderr)
            preconditionFailure(message)
        }
    }
}
