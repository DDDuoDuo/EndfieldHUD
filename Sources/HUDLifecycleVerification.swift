import AppKit

/// Explicit, isolated live-window checks. This harness never terminates the
/// application itself; the caller owns completion and the diagnostic session.
enum HUDLifecycleVerification {
    static func run(overlay: OverlayController, configuration: AppConfiguration,
                    completion: @escaping () -> Void) {
        let lifecycle = {
            let start = { Session(overlay: overlay, configuration: configuration, completion: completion).start() }
            if CommandLine.arguments.contains("--source-fallback-smoke-test") {
                verifySourceFallback(overlay: overlay, configuration: configuration, completion: start)
            } else { start() }
        }
        let interactions = {
            if CommandLine.arguments.contains("--banner-drag-smoke-test") {
                HUDSourceBannerDragVerification.run(overlay: overlay, configuration: configuration, completion: lifecycle)
            } else { lifecycle() }
        }
        if CommandLine.arguments.contains("--backdrop-preparation-smoke-test") {
            HUDBackdropOpeningVerification.run(overlay: overlay, configuration: configuration) {
                interactions()
            }
        } else {
            interactions()
        }
    }

    private static func verifySourceFallback(overlay: OverlayController, configuration: AppConfiguration,
                                             completion: @escaping () -> Void) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        var configuration = configuration
        configuration.closeOnFocusLost = false
        overlay.initialModuleRequest = .map
        let snapshot = BatterySnapshot(percentage: 75, isPluggedIn: true, isCharging: true,
            isFullyCharged: false, hasBattery: true)
        precondition(overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration))
        DispatchQueue.main.asyncAfter(deadline: .now() + SystemHUDView.entranceDuration + 0.2) {
            guard let source = overlay.systemSourceWatchForVerification,
                  let host = source.superview as? SystemHUDView else {
                preconditionFailure("Source fallback fixture requires a working initial source shell")
            }
            precondition(overlay.systemPhase == .open && overlay.systemReportGeometryMatchesSelectionForVerification)
            precondition(host.legacyArtworkLayerCountForVerification == 0,
                         "The source shell must not construct hidden native mechanical artwork")
            // Invoke the renderer's actual failure handoff, without changing
            // bundled assets or the real user's stores and preferences.
            source.onFailure?("Injected source failure for isolated lifecycle verification")
            let legacyLayerCount = host.legacyArtworkLayerCountForVerification
            precondition(legacyLayerCount > 0,
                         "A source failure constructs the complete native mechanical fallback on demand")
            precondition(overlay.systemPhase == .open && source.isHidden && !source.hasDisplayTimerForVerification,
                         "Runtime source failure restores the native shell without closing or a hidden clock")
            precondition(overlay.systemReportGeometryMatchesSelectionForVerification,
                         "Fallback Map drawing and projected input must move back to the native origin together")
            overlay.selectSystemModule(.workMode, animated: false)
            precondition(overlay.systemReportGeometryMatchesSelectionForVerification
                         && overlay.systemWorkModeDialDiameterForVerification == 430,
                         "Fallback Work Mode retains its full ring and matching input geometry")
            overlay.selectSystemModule(.map, animated: false)
            precondition(host.legacyArtworkLayerCountForVerification == legacyLayerCount,
                         "Subsequent fallback navigation reuses its mechanical artwork")
            overlay.closeSystemOverlay()
            DispatchQueue.main.asyncAfter(deadline: .now() + SystemHUDView.exitDuration + 0.3) {
                precondition(overlay.systemPhase == .closed && overlay.lastClosedAnimationCount == 0
                             && !overlay.lastClosedSourceTimerActive,
                             "Fallback closes through the native animation and releases its presentation")
                print("PASS: Runtime source failure retains Map/Work geometry and clean native teardown")
                completion()
            }
        }
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
        private var openingSourceFrames = 0
        private weak var closingSource: HUDSourceWatchView?
        private weak var presentedCursor: NSCursor?
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
            guard let screen else { fail("Lifecycle verification requires an attached display") }
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
            // A fresh controller must choose Map without an explicit request.
            // Optional earlier scene fixtures reuse this controller and may
            // legitimately have changed its remembered section already.
            let hadPreflight = CommandLine.arguments.contains("--banner-drag-smoke-test")
                || CommandLine.arguments.contains("--backdrop-preparation-smoke-test")
            overlay.initialModuleRequest = hadPreflight ? .map : nil
            check(overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration), "The fixture HUD opens")
            check(overlay.systemSelectedModule == .map && overlay.systemCenterContentCount == 1
                  && overlay.systemReportGeometryMatchesSelectionForVerification,
                  "The integrated shell opens the stable Map canvas in its native module host")
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
            guard let source = overlay.systemSourceWatchForVerification else {
                fail("Original Watch view unavailable: \(overlay.systemSourceFailureForVerification ?? "missing")")
            }
            check(source.playback.phase == .visible && source.document.buttons.count == 22,
                  "Opening reaches the original Watch endpoint with all 22 original button definitions")
            check(source.currentFrameForVerification?.hits.isEmpty == false,
                  "The displayed source frame contains original graphic raycasts")
            check(source.visibleMainButtonForVerification != nil,
                  "A projected original button can be hit through the same source camera and masks")
            check(source.hasDisplayTimerForVerification == !reduced && overlay.systemAmbientAnimationCount == 0
                  && overlay.systemParallaxAnimationCount <= 13,
                  "The persistent scene owns its display timer while native content has only bounded pointer tracks")
            if !reduced { check(source.renderedFrameCount > openingSourceFrames, "The real source frame advances after the injected pointer changes") }
            do {
                let image = try source.renderedImageForVerification()
                check(image.width > 0 && image.height > 0, "The displayed original menu produces an actual Metal drawable")
            } catch { fail("Source drawable verification failed: \(error)") }
            guard let point = source.desktopPointForVerification(target: .module(.notes)),
                  let host = source.superview as? SystemHUDView else {
                fail("A visible projected Notes button is required for input verification")
            }
            clickHUD(at: host.convert(point, from: source))
            check(overlay.systemPhase == .open && overlay.systemSelectedModule == .notes
                  && !overlay.systemQuitConfirmationVisibleForVerification,
                  "An active source card routes to its module without being treated as an outside click")
            guard let profilePoint = source.desktopProfilePointForVerification else {
                fail("The actual bottom profile card must have a visible projected hit")
            }
            clickHUD(at: host.convert(profilePoint, from: source))
            check(overlay.systemSelectedModule == .profile && overlay.systemPhase == .open,
                  "The authored bottom profile card opens the retained editable Personal Profile canvas")
            overlay.selectSystemModule(.eventLog, animated: false)
            check(overlay.systemSourceWatchForVerification === source && !source.isHiddenOrHasHiddenAncestor
                  && source.hasDisplayTimerForVerification == !reduced && source.playback.phase == .visible,
                  "Switching desktop sections retains the same visible source shell and its display clock")
            check(overlay.systemSelectedModule == .eventLog && overlay.systemCenterContentCount == 1
                  && overlay.systemReportGeometryMatchesSelectionForVerification,
                  "Event Log remains a real native canvas inside the persistent shell")
            checkCursorOwnership("native module")
            let shell = overlay.systemShellIdentity
            let section = overlay.systemSelectedModule
            clickProjectedQuit()
            check(overlay.systemQuitConfirmationVisibleForVerification && currentWindowIDs() == windowIDs,
                  "Power confirmation stays inside the existing HUD window")
            let priorCenter = host.centerPointForVerification(CGPoint(x: 760, y: 80))
            _ = movePointer(x: 430, y: -275)
            later(0.25) { [self] in
            check(host.confirmationFollowsSourceForVerification, "Confirmation uses the current source tilt with no second easing clock")
            if !reduced {
                check(host.centerPointForVerification(CGPoint(x: 760, y: 80)) != priorCenter,
                      "Pointer movement continues to tilt the HUD while quit confirmation is open")
            }
            overlay.answerQuitConfirmationForVerification(false)
            later(0.25) { [self] in
                check(!overlay.systemQuitConfirmationVisibleForVerification && overlay.systemPhase == .open,
                      "Cancel dismisses only the quit confirmation")
                check(overlay.systemShellIdentity == shell && overlay.systemSelectedModule == section
                      && currentWindowIDs() == windowIDs,
                      "Cancel retains the same HUD, section and native windows")
                check(acceptedQuits == 0 && completedQuits == 0 && normalCloses == 0,
                      "Cancel neither accepts quit nor closes the HUD")
                checkCursorDispatchAndIdle { [self] in closeWithPointerMotion() }
            }
            }
        }

        private func closeWithPointerMotion() {
            closingSource = overlay.systemSourceWatchForVerification
            guard let source = closingSource, let host = source.superview as? SystemHUDView else {
                fail("Outside-click verification requires the live source and native host")
            }
            check(host.clockIsInStatusPanelForVerification, "Clock/date and Work Mode badge share the source upper-right plane")
            check(host.legacyProgressHiddenForVerification, "Legacy battery arc stays hidden under the source shell")
            check(host.legacyArtworkLayerCountForVerification == 0,
                  "The source shell retains no hidden legacy mechanical artwork or texture variants")
            var appearanceChange = configuration
            appearanceChange.applicationIcon = configuration.applicationIcon == .endfield ? .battery : .endfield
            host.set(snapshot: snapshot, configuration: appearanceChange)
            check(host.legacyProgressHiddenForVerification, "Changing the menu icon cannot reveal a retired native ring")
            host.set(snapshot: snapshot, configuration: configuration)
            // This point lies within the displayed center near its blank upper
            // rim, away from module controls. It must not hit FullScreenCloseBtn.
            if let center = host.centerPointForVerification(CGPoint(x: 500, y: 70)) {
                clickHUD(at: center)
                check(overlay.systemPhase == .open, "A blank click within the tilted central circle cannot dismiss the HUD")
            } else { fail("Missing source center projection for dismissal verification") }
            let corner = CGPoint(x: host.bounds.minX + 8, y: host.bounds.minY + 8)
            check(host.hitTest(host.convert(corner, to: host.superview)) === source,
                  "Blank background input exercises the source view rather than bypassing it")
            let cleanupsBeforeClose = overlay.closedHeapCleanupRunsForVerification
            clickHUD(at: corner)
            check(overlay.systemPhase == .closing, "Ordinary close starts retraction synchronously")
            if !reduced {
                later(min(0.08, SystemHUDView.exitDuration * 0.20)) { [self] in
                    let point = movePointer(x: 210, y: -85)
                    checkTransitionMotion(point, phase: .closing)
                }
            }
            later(SystemHUDView.exitDuration + 0.25) { [self] in
                checkCleanClose("Ordinary retraction")
                check(overlay.closedHeapCleanupActiveForVerification
                      || overlay.closedHeapCleanupRunsForVerification > cleanupsBeforeClose,
                      "Normal retraction schedules or finishes heap cleanup after releasing the source view")
                // A delayed fixture checkpoint (or reduced-motion close) may
                // arrive after the original job. Exercise cancellation with a
                // fresh real deadline, without changing production timing.
                let cancelledCleanup = overlay.rescheduleClosedHeapCleanupForVerification()
                check(overlay.closedHeapCleanupPendingForVerification,
                      "The isolated closed-state cleanup has a pending deadline to cancel")
                check(normalCloses == 1 && acceptedQuits == 0 && completedQuits == 0,
                      "Ordinary close retains its normal callback without requesting application quit")
                check(overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration),
                      "Ordinary HUD close allows a later summon")
                check(!overlay.closedHeapCleanupPendingForVerification,
                      "Reopening cancels heap cleanup before deployment begins")
                later(SystemHUDView.entranceDuration + 0.20) { [self] in
                    check(overlay.closedHeapCleanupLastRunGenerationForVerification != cancelledCleanup,
                          "A cancelled delayed cleanup never runs during the reopened HUD")
                    checkUnansweredFocusClose()
                }
            }
        }

        private func checkUnansweredFocusClose() {
            check(overlay.systemPhase == .open && overlay.systemSelectedModule == .eventLog,
                  "Reopening retains the section from before a cancelled quit")
            clickProjectedQuit()
            check(overlay.systemQuitConfirmationVisibleForVerification, "A later unanswered quit prompt is visible")
            closingSource = overlay.systemSourceWatchForVerification
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
                later(SystemHUDView.entranceDuration + 0.20) { [self] in checkUpdateClose() }
            }
        }

        private func checkUpdateClose() {
            check(!overlay.isIdleForUpdate, "An open HUD prevents an automatic update restart")
            var completions = 0
            overlay.afterSystemClose = { [weak self] in self?.genericHandoffs += 1 }
            closingSource = overlay.systemSourceWatchForVerification
            overlay.closeForApplicationUpdate { [self] in
                completions += 1
                // The view's completion is still on the stack here. Check
                // logical teardown now and its weak lifetime after it returns.
                checkCleanClose("Updater handoff", requireReleasedView: false)
                check(!overlay.closedHeapCleanupPendingForVerification,
                      "Application update teardown does not schedule background heap work")
            }
            overlay.closeForApplicationUpdate { completions += 100 }
            check(overlay.systemPhase == .closing && completions == 0,
                  "An accepted update waits for the closing animation before its continuation")
            check(!overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration),
                  "Summon cannot reopen while an accepted update is retracting")
            later(SystemHUDView.exitDuration + 0.30) { [self] in
                checkReleasedSource("Updater handoff after its completion returned")
                check(completions == 1 && normalCloses == 2 && genericHandoffs == 0 && completedQuits == 0,
                      "Update continuation runs once, without unrelated handoffs, charging restore or quit callback")
                overlay.cancelApplicationUpdate()
                check(overlay.isIdleForUpdate, "Cancelling before app termination releases the update lock")
                check(overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration),
                      "A failed update before termination leaves the HUD usable")
                later(SystemHUDView.entranceDuration + 0.20) { [self] in checkAcceptedQuit() }
            }
        }

        private func checkAcceptedQuit() {
            check(overlay.systemPhase == .open, "Accepted-quit cycle starts from an open HUD")
            overlay.selectSystemModule(.power, animated: false)
            check(overlay.systemSourceWatchForVerification?.playback.phase == .visible,
                  "The final quit cycle returns to the original Watch overview")
            overlay.afterSystemClose = { [weak self] in self?.genericHandoffs += 1 }
            clickProjectedQuit()
            check(overlay.systemQuitConfirmationVisibleForVerification, "Final quit requires its own confirmation")
            closingSource = overlay.systemSourceWatchForVerification
            overlay.answerQuitConfirmationForVerification(true)
            check(acceptedQuits == 1 && overlay.systemPhase == .closing && completedQuits == 0,
                  "Confirm accepts once and starts closing without prematurely invoking quit")
            overlay.answerQuitConfirmationForVerification(true)
            check(!overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration),
                  "Summon cannot reopen an accepted application quit")
            check(acceptedQuits == 1 && completedQuits == 0,
                  "Repeated confirmation cannot duplicate acceptance or bypass retraction")
            if !reduced {
                later(0.08) { [self] in
                    // The pointer is deliberately nonzero after earlier cycles.
                    // Move it again so this checks real quit-time retargeting,
                    // not a stale centered pose or an already completed track.
                    let point = movePointer(x: -145, y: 105)
                    checkTransitionMotion(point, phase: .closing)
                }
            }
            let sourceExit = overlay.systemSourceWatchForVerification?.document.animation.exit.lastKeyTime ?? 0.3333333432674408
            later(sourceExit + 0.15) { [self] in
                check(overlay.systemPhase == .closed && !overlay.lastClosedSourceTimerActive
                      && overlay.lastClosedSourcePhase == .concealed,
                      "Original Watch exit finishes at its source duration and leaves no display timer")
            }
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
                fail("The live HUD must provide its normalized pointer target")
            }
            overlay.setSystemPointerForVerification(point)
            return point
        }

        private func clickProjectedQuit() {
            guard let source = overlay.systemSourceWatchForVerification,
                  let host = source.superview as? SystemHUDView,
                  let point = source.desktopQuitPointForVerification else {
                fail("The authored red quit control must have a visible projected hit")
            }
            clickHUD(at: host.convert(point, from: source))
        }

        private func clickHUD(at point: CGPoint) {
            guard let source = overlay.systemSourceWatchForVerification,
                  let host = source.superview as? SystemHUDView, let window = host.window,
                  let receiver = host.hitTest(host.convert(point, to: host.superview)) else {
                fail("HUD input verification requires an actual hit-tested view")
            }
            for type: NSEvent.EventType in [.leftMouseDown, .leftMouseUp] {
                guard let event = NSEvent.mouseEvent(with: type, location: host.convert(point, to: nil),
                    modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                    windowNumber: window.windowNumber, context: nil, eventNumber: 0, clickCount: 1, pressure: 1) else {
                    fail("Cannot create an isolated local HUD mouse event")
                }
                if type == .leftMouseDown { receiver.mouseDown(with: event) }
                else { receiver.mouseUp(with: event) }
            }
        }

        private func checkTransitionMotion(_ point: CGPoint, phase: SystemOverlayPhase) {
            if let source = overlay.systemSourceWatchForVerification {
                let expected: HUDSourceWatchPlayback.Phase = phase == .opening ? .opening : .closing
                check(overlay.systemPhase == phase && source.playback.phase == expected && source.hasDisplayTimerForVerification,
                      "Original Watch finite transition owns its active display clock")
                check(source.currentFrameForVerification != nil && source.renderedFrameCount > 0,
                      "Original Watch transition resolves actual source geometry")
                check(overlay.systemAmbientAnimationCount == 0,
                      "Source transitions do not start duplicate legacy ambient loops")
                openingSourceFrames = source.renderedFrameCount
            }
            check(overlay.systemPhase == phase && overlay.systemPointerTargetForVerification == point
                  && overlay.systemSpatialPoseMatchesPointerForVerification(point),
                  "Pointer input updates spatial planes during \(phase.rawValue)")
            check(overlay.systemParallaxAnimationCount > 0 && overlay.systemParallaxAnimationCount <= 13,
                  "Transition pointer response retains at most thirteen finite plane tracks")
            check(overlay.systemAmbientAnimationCount == 0,
                  "Transition pointer response does not start or retain ambient loops")
            // Resetting AppKit cursor regions can flush layout. Inspect the
            // immediate motion tracks first so the cursor probe cannot settle
            // the very tracks this timing assertion is measuring.
            checkCursorOwnership(phase.rawValue)
            if let source = overlay.systemSourceWatchForVerification {
                do {
                    check(try source.verifyCurrentAccessibilityGeometryForVerification() > 0,
                          "Queried accessibility follows pointer projection during \(phase.rawValue)")
                } catch { fail("Transition accessibility projection: \(error)") }
                check(source.desktopAccessibilityAvailabilityForVerification.allSatisfy { !$0.enabled },
                      "Accessibility cannot activate source buttons during deployment/retraction")
            }
        }

        private func checkCursorOwnership(_ phase: String) {
            guard let source = overlay.systemSourceWatchForVerification,
                  let cursor = source.presentedSourceCursor, let window = source.window,
                  let host = source.superview as? SystemHUDView else {
                fail("Visible HUD must own its source cursor during \(phase)")
            }
            presentedCursor = cursor
            // AppKit can reset the cursor while crossing native module views.
            // The host's cursor-update route must restore it without a draw tick.
            NSCursor.arrow.set()
            window.resetCursorRects()
            let point = host.convert(window.convertPoint(fromScreen: screenPointer), from: nil)
            let frames = source.renderedFrameCount
            let sets = source.sourceCursorSetCountForVerification
            sendCursorUpdate(at: point, host: host, window: window)
            check(source.sourceCursorOwnedForVerification && source.renderedFrameCount == frames
                  && source.sourceCursorSetCountForVerification == sets + 1,
                  "The registered HUD owner answers cursorUpdate without render polling during \(phase)")
            sendCursorUpdate(at: point, host: host, window: window)
            check(source.sourceCursorOwnedForVerification && source.presentedSourceCursor === cursor
                  && source.sourceCursorSetCountForVerification == sets + 2,
                  "cursorUpdate reinstalls the cursor even when NSCursor.current still names it during \(phase)")
        }

        private func checkCursorDispatchAndIdle(completion: @escaping () -> Void) {
            guard let source = overlay.systemSourceWatchForVerification,
                  let host = source.superview as? SystemHUDView, let window = host.window,
                  let cursor = source.presentedSourceCursor else {
                fail("Cursor dispatch verification requires the visible source and native host")
            }
            let previousPointer = screenPointer
            var quietConfiguration = configuration
            quietConfiguration.reduceMotion = true
            quietConfiguration.ambientAnimation = false
            host.set(snapshot: snapshot, configuration: quietConfiguration)
            host.updateTrackingAreas(); source.updateTrackingAreas()
            let cursorAreas = host.trackingAreas.filter { $0.options.contains(.cursorUpdate) }
            check(cursorAreas.count == 1 && cursorAreas[0].options.contains(.activeInKeyWindow)
                  && !cursorAreas[0].options.contains(.activeAlways)
                  && !source.trackingAreas.contains { $0.options.contains(.cursorUpdate) },
                  "The sole desktop cursor owner uses key-window tracking that actually receives cursorUpdate")
            // Align the temporary native control with the actual pointer when
            // it is in this window, so genuine AppKit updates and the injected
            // input coordinate agree. No global pointer movement is performed.
            let physicalPoint = host.convert(window.convertPoint(fromScreen: NSEvent.mouseLocation), from: nil)
            let probeCenter = host.bounds.contains(physicalPoint) ? physicalPoint
                : CGPoint(x: host.bounds.midX, y: host.bounds.midY)
            let probe = NativeCursorProbe(frame: CGRect(x: probeCenter.x - 40,
                y: probeCenter.y - 40, width: 80, height: 80))
            host.addSubview(probe, positioned: .above, relativeTo: nil)
            host.layoutSubtreeIfNeeded()
            probe.updateTrackingAreas()
            let point = CGPoint(x: probe.frame.midX, y: probe.frame.midY)
            screenPointer = window.convertPoint(toScreen: host.convert(point, to: nil))
            check(host.hitTest(host.convert(point, to: host.superview)) === probe,
                  "Cursor dispatch reaches a native child above the source shell")
            sendCursorProbeClick(at: point, host: host, window: window)
            check(probe.mouseUpCount == 1 && NSCursor.current === NSCursor.arrow,
                  "Window mouse delivery does not overwrite a native cursor after dispatch")
            let resets = probe.cursorResetCount
            window.invalidateCursorRects(for: probe)
            window.resetCursorRects()
            check(probe.cursorResetCount > resets,
                  "Native cursor-region invalidation reaches the native view through AppKit")
            // resetCursorRects may legitimately send cursorUpdate immediately.
            // Count only our explicit handler call after that arbitration ends.
            let afterResetSets = source.sourceCursorSetCountForVerification
            sendCursorUpdate(at: point, host: host, window: window)
            check(source.sourceCursorOwnedForVerification && source.sourceCursorSetCountForVerification == afterResetSets + 1,
                  "After native invalidation the registered cursorUpdate owner immediately restores Endfield")
            // A native resize/drag cursor must survive window delivery and the
            // following run-loop turn. The former async repair clobbered it.
            probe.selectedCursor = .resizeLeftRight
            sendCursorProbeClick(at: point, host: host, window: window)
            check(NSCursor.current === NSCursor.resizeLeftRight,
                  "Native resize selection survives synchronous window dispatch")
            later(0.05) { [self] in
                check(NSCursor.current === NSCursor.resizeLeftRight,
                      "There is no deferred HUD reassertion over a native resize cursor")
                probe.selectedCursor = .dragCopy
                sendCursorProbeClick(at: point, host: host, window: window)
                later(0.05) { [self] in
                    check(NSCursor.current === NSCursor.dragCopy,
                          "Native drag cursor selection survives subsequent event-loop work")
                    let pasteboard = NSPasteboard(name: .init("EndfieldHUD.CursorDragProbe.\(UUID().uuidString)"))
                    let drag = NativeCursorDragProbe(pasteboard: pasteboard)
                    _ = host.draggingEntered(drag)
                    let beforeDragRefresh = source.sourceCursorSetCountForVerification
                    source.refreshSourceCursor()
                    source.viewDidChangeBackingProperties()
                    sendCursorUpdate(at: point, host: host, window: window)
                    check(host.preservesNativeDragCursor && NSCursor.current === NSCursor.dragCopy
                          && source.sourceCursorSetCountForVerification == beforeDragRefresh,
                          "Lifecycle and backing refreshes share the native drag cursor veto")
                    host.draggingExited(drag)
                    pasteboard.releaseGlobally()
                    probe.selectedCursor = .arrow
                    probe.cursorTrackingEnabled = false
                    probe.updateTrackingAreas()
                    window.invalidateCursorRects(for: probe)
                    window.resetCursorRects()
                    sendCursorUpdate(at: point, host: host, window: window)
                    later(0.25) { [self] in
                        check(source.playback.phase == .visible && !source.hasDisplayTimerForVerification,
                              "Cursor idle verification runs after animations settle with the display clock stopped")
                        let frames = source.renderedFrameCount
                        let idleSets = source.sourceCursorSetCountForVerification
                        later(2.5) { [self] in
                            check(source.sourceCursorOwnedForVerification && source.presentedSourceCursor === cursor,
                                  "Endfield retains ownership through several seconds of native-view idle time")
                            check(!source.hasDisplayTimerForVerification && source.renderedFrameCount == frames
                                  && source.sourceCursorSetCountForVerification == idleSets,
                                  "Keeping the cursor visible neither renders nor repeatedly sets the cursor")
                            probe.removeFromSuperview()
                            let editor = NativeEditorCursorProbe(frame: probe.frame)
                            editor.isEditable = true
                            host.addSubview(editor, positioned: .above, relativeTo: nil)
                            sendCursorProbeClick(at: point, host: host, window: window)
                            let beforeEditorUpdate = source.sourceCursorSetCountForVerification
                            sendCursorUpdate(at: point, host: host, window: window)
                            check(editor.mouseUpCount == 1 && NSCursor.current === NSCursor.iBeam
                                  && !source.sourceCursorOwnedForVerification
                                  && source.sourceCursorSetCountForVerification == beforeEditorUpdate,
                                  "The HUD cursor owner yields to a native text editor's I-beam")
                            editor.removeFromSuperview()
                            let selectable = NSTextField(frame: probe.frame)
                            selectable.isEditable = false; selectable.isSelectable = true
                            host.addSubview(selectable, positioned: .above, relativeTo: nil)
                            // This check preserves an already chosen native
                            // cursor; genuine hover entry has a separate probe.
                            NSCursor.iBeam.set()
                            let beforeSelectableUpdate = source.sourceCursorSetCountForVerification
                            sendCursorUpdate(at: point, host: host, window: window)
                            check(NSCursor.current === NSCursor.iBeam
                                  && source.sourceCursorSetCountForVerification == beforeSelectableUpdate,
                                  "Selectable read-only text also retains its native cursor")
                            selectable.removeFromSuperview()
                            host.addSubview(probe, positioned: .above, relativeTo: nil)
                            sendCursorUpdate(at: point, host: host, window: window)
                            check(source.sourceCursorOwnedForVerification,
                                  "Leaving native text entry restores Endfield at the next cursorUpdate")
                            let outside = CGPoint(x: host.bounds.maxX + 100, y: host.bounds.maxY + 100)
                            let beforeExit = source.sourceCursorSetCountForVerification
                            sendCursorUpdate(at: outside, host: host, window: window)
                            check(!source.sourceCursorOwnedForVerification
                                  && source.sourceCursorSetCountForVerification == beforeExit,
                                  "An outside cursor event releases HUD ownership and falls through to AppKit")
                            sendCursorUpdate(at: point, host: host, window: window)
                            check(source.sourceCursorOwnedForVerification,
                                  "Re-entry acquires the HUD cursor without waiting for mouse movement or a timer")
                            probe.removeFromSuperview()
                            screenPointer = previousPointer
                            host.set(snapshot: snapshot, configuration: configuration)
                            completion()
                        }
                    }
                }
            }
        }

        private func sendCursorUpdate(at point: CGPoint, host: SystemHUDView, window: NSWindow) {
            guard let area = host.trackingAreas.first(where: { $0.options.contains(.cursorUpdate) }),
                  let owner = area.owner as? NSResponder,
                  let event = NSEvent.enterExitEvent(with: .cursorUpdate,
                    location: host.convert(point, to: nil), modifierFlags: [],
                    timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                    context: nil, eventNumber: 0, trackingNumber: 0, userData: nil) else {
                fail("Cannot find the registered HUD cursor owner or create its event")
            }
            // Public NSEvent factories cannot attach an NSTrackingArea, so a
            // manufactured cursorUpdate is discarded by NSWindow.sendEvent.
            // Mouse delivery/invalidation above use the actual window; this
            // one message goes to the owner of its real registered area.
            owner.cursorUpdate(with: event)
        }

        private func sendCursorProbeClick(at point: CGPoint, host: SystemHUDView, window: NSWindow) {
            for type: NSEvent.EventType in [.leftMouseDown, .leftMouseUp] {
                guard let event = NSEvent.mouseEvent(with: type, location: host.convert(point, to: nil),
                    modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                    windowNumber: window.windowNumber, context: nil, eventNumber: 0, clickCount: 1, pressure: 1) else {
                    fail("Cannot create an isolated native cursor-dispatch event")
                }
                window.sendEvent(event)
            }
        }

        private func checkCleanClose(_ label: String, requireReleasedView: Bool = true) {
            check(overlay.systemPhase == .closed && !overlay.systemWindowVisibleForVerification
                  && overlay.systemShellIdentity == nil && overlay.lastClosedAnimationCount == 0
                  && !overlay.lastClosedSourceTimerActive && overlay.lastClosedSourcePhase == .concealed,
                  "\(label) releases the hidden presentation and all animation tracks")
            if let presentedCursor {
                check(NSCursor.current !== presentedCursor, "Closing releases the Endfield cursor")
            }
            if requireReleasedView { checkReleasedSource(label) }
        }

        private func checkReleasedSource(_ label: String) {
            check(closingSource == nil,
                  "\(label) releases the actual source view; immutable resource caches must not retain its lifetime")
        }

        private func currentWindowIDs() -> Set<ObjectIdentifier> { Set(NSApp.windows.map { ObjectIdentifier($0) }) }
        private func check(_ value: Bool, _ message: String) {
            assertions += 1
            if !value { fail("Assertion \(assertions): \(message)") }
        }
        /// Optimized Swift preconditions can trap without printing their text.
        /// Emit the original contract and live state before retaining the trap;
        /// a drawable-only fixture cannot diagnose the view's input lifecycle.
        private func fail(_ message: String) -> Never {
            var lines = [
                "FAIL: HUD lifecycle verification: \(message)",
                "overlay phase=\(overlay.systemPhase.rawValue) module=\(overlay.systemSelectedModule?.rawValue ?? "nil") visible=\(overlay.systemWindowVisibleForVerification) shell=\(String(describing: overlay.systemShellIdentity))",
                "motion fixtureReduced=\(reduced) currentReduced=\(HUDRuntimeAppearance.reduceMotion) ambientEnabled=\(HUDRuntimeAppearance.ambientEnabled) legacyAmbient=\(overlay.systemAmbientAnimationCount) legacyParallax=\(overlay.systemParallaxAnimationCount)",
                "callbacks normal=\(normalCloses) acceptedQuit=\(acceptedQuits) completedQuit=\(completedQuits) handoff=\(genericHandoffs) windows=\(currentWindowIDs().count) initialWindows=\(windowIDs.count)",
                "source failure=\(overlay.systemSourceFailureForVerification ?? "nil")",
            ]
            if let source = overlay.systemSourceWatchForVerification {
                lines.append("source phase=\(source.playback.phase) hidden=\(source.isHiddenOrHasHiddenAncestor) input=\(source.inputEnabled) timer=\(source.hasDisplayTimerForVerification) frames=\(source.renderedFrameCount) openingFrames=\(openingSourceFrames) buttons=\(source.document.buttons.count)")
                lines.append("cursor owned=\(source.sourceCursorOwnedForVerification) sets=\(source.sourceCursorSetCountForVerification) arrow=\(NSCursor.current === NSCursor.arrow) iBeam=\(NSCursor.current === NSCursor.iBeam) resize=\(NSCursor.current === NSCursor.resizeLeftRight) dragCopy=\(NSCursor.current === NSCursor.dragCopy)")
                lines.append("cursor sourceAreas=\(source.trackingAreas.map { $0.options.rawValue }) hostAreas=\(source.superview?.trackingAreas.map { $0.options.rawValue } ?? [])")
                lines.append("source window=\(source.window != nil) key=\(source.window?.isKeyWindow ?? false) occlusion=\(String(describing: source.window?.occlusionState)) appActive=\(NSApp.isActive) bounds=\(source.bounds)")
                lines.append("source frame=\(source.currentFrameForVerification != nil) camera=\(source.currentCameraForVerification != nil) hits=\(source.currentFrameForVerification?.hits.count ?? 0) batches=\(source.currentFrameForVerification?.batches.count ?? 0)")
                if let frame = source.currentFrameForVerification, let camera = source.currentCameraForVerification {
                    let mainIDs = Set(source.document.buttons.map(\.nodeID))
                    let mainHits = frame.hits.filter { mainIDs.contains($0.buttonID) }
                    lines.append("source mainHitButtons=\(Set(mainHits.map(\.buttonID)).count)")
                    for hit in mainHits.prefix(4) {
                        let center = hit.rect.origin + hit.rect.size * 0.5
                        let point = camera.camera.project(SIMD3<Double>(center.x, center.y, 0),
                            world: hit.world, viewport: source.bounds)?.point
                        let winner = point.flatMap { frame.button(at: $0, camera: camera.camera, viewport: source.bounds) }
                        let buttonPath = source.document.scene.node(hit.buttonID)?.path ?? hit.buttonID.rawValue
                        lines.append("source hitProbe=\(buttonPath) point=\(String(describing: point)) inBounds=\(point.map { source.bounds.contains($0) } ?? false) winner=\(winner?.rawValue ?? "nil") masks=\(hit.masks.count)")
                    }
                }
                lines.append("source diagnostics=\(source.diagnostics.prefix(8).joined(separator: "; "))")
                lines.append("GPU diagnostics=\(source.renderer.diagnostics.prefix(8).joined(separator: "; "))")
            }
            lines.append("closed tracks=\(overlay.lastClosedAnimationCount) sourceTimer=\(overlay.lastClosedSourceTimerActive) sourcePhase=\(String(describing: overlay.lastClosedSourcePhase))")
            FileHandle.standardError.write(Data((lines.joined(separator: "\n") + "\n").utf8))
            fflush(stderr)
            preconditionFailure(message)
        }
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
            print("PASS: \(assertions) HUD lifecycle assertions; pointer follow-through, native cursor dispatch and idle ownership, bounded transitions, quit cancellation, updater close/recovery, close-before-quit and one-shot cleanup\(reduced ? "; transition midpoint checks skipped for system Reduce Motion" : "")")
            fflush(stdout)
            completion()
        }
    }

    /// A native child deliberately chooses its own cursor during AppKit
    /// callbacks, so the test exercises dispatch ordering instead of manually
    /// invoking the source's cursor-update handler.
    private final class NativeCursorProbe: NSView {
        private var tracking: NSTrackingArea?
        private(set) var mouseUpCount = 0
        private(set) var cursorResetCount = 0
        var selectedCursor: NSCursor = .arrow {
            didSet { window?.invalidateCursorRects(for: self) }
        }
        var cursorTrackingEnabled = true
        override func updateTrackingAreas() {
            super.updateTrackingAreas()
            if let tracking { removeTrackingArea(tracking) }
            tracking = nil
            guard cursorTrackingEnabled else { return }
            let area = NSTrackingArea(rect: .zero,
                options: [.inVisibleRect, .activeInKeyWindow, .mouseMoved, .cursorUpdate], owner: self)
            addTrackingArea(area); tracking = area
        }
        override func resetCursorRects() {
            super.resetCursorRects()
            cursorResetCount += 1
            if cursorTrackingEnabled {
                addCursorRect(visibleRect, cursor: selectedCursor)
                selectedCursor.set()
            }
        }
        override func cursorUpdate(with event: NSEvent) { selectedCursor.set() }
        override func mouseMoved(with event: NSEvent) { selectedCursor.set() }
        override func mouseDown(with event: NSEvent) {}
        override func mouseUp(with event: NSEvent) { mouseUpCount += 1; selectedCursor.set() }
    }

    private final class NativeCursorDragProbe: NSObject, NSDraggingInfo {
        let draggingPasteboard: NSPasteboard
        let draggingSequenceNumber = 1
        var draggingSourceOperationMask: NSDragOperation { .copy }
        var draggingDestinationWindow: NSWindow? { nil }
        var draggingLocation: NSPoint { CGPoint(x: -1000, y: -1000) }
        var draggedImageLocation: NSPoint { .zero }
        var draggedImage: NSImage? { nil }
        var draggingSource: Any? { nil }
        var draggingFormation: NSDraggingFormation = .none
        var animatesToDestination = false
        var numberOfValidItemsForDrop = 0
        var springLoadingHighlight: NSSpringLoadingHighlight { .none }
        init(pasteboard: NSPasteboard) { draggingPasteboard = pasteboard }
        func slideDraggedImage(to screenPoint: NSPoint) {}
        override func namesOfPromisedFilesDropped(atDestination dropDestination: URL) -> [String]? { nil }
        func resetSpringLoading() {}
        func enumerateDraggingItems(options enumOpts: NSDraggingItemEnumerationOptions, for view: NSView?,
            classes classArray: [AnyClass], searchOptions: [NSPasteboard.ReadingOptionKey: Any] = [:],
            using block: (NSDraggingItem, Int, UnsafeMutablePointer<ObjCBool>) -> Void) {}
    }

    private final class NativeEditorCursorProbe: NSTextView {
        private(set) var mouseUpCount = 0
        override func mouseDown(with event: NSEvent) {}
        override func mouseUp(with event: NSEvent) { mouseUpCount += 1; NSCursor.iBeam.set() }
    }
}
