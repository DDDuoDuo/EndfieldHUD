import AppKit
import simd

/// Real source-view input checks with synthetic mouse events and explicit
/// artwork/profile fixtures. This never calls desktop-capture or permission APIs.
enum HUDSourceBannerDragVerification {
    static func run(overlay: OverlayController, configuration: AppConfiguration,
                    completion: @escaping () -> Void) {
        precondition(Thread.isMainThread && CommandLine.arguments.contains("--ui-test"))
        Session(overlay: overlay, configuration: configuration, completion: completion).start()
    }

    private final class Session {
        let overlay: OverlayController
        var configuration: AppConfiguration
        let completion: () -> Void
        let priorPointer: (() -> CGPoint)?
        var priorSourcePointer: (() -> CGPoint)?
        let priorPreparation: ((@escaping () -> Void) -> Void)?
        let priorRuntimeConfiguration: AppConfiguration
        let priorInitialModule: HUDModule?
        var priorAction: ((HUDSourceWatchView.ButtonAction) -> Void)?
        var priorWidgetState: HUDSourceWatchWidgets.State?
        var suppliedWidgetState: HUDSourceWatchWidgets.State?
        var heldSource: HUDSourceWatchView?
        var injectedPointer = CGPoint.zero
        var assertions = 0
        var eventNumber = 0
        var clickedIDs: [HUDSourceID] = []
        var clickDraggingStates: [Bool] = []
        let snapshot = BatterySnapshot(percentage: 75, isPluggedIn: true, isCharging: true,
            isFullyCharged: false, hasBattery: true,
            capacity: BatteryCapacityReading(current: 3600, maximum: 4800, unit: .milliampHours))

        init(overlay: OverlayController, configuration: AppConfiguration, completion: @escaping () -> Void) {
            self.overlay = overlay; self.configuration = configuration; self.completion = completion
            priorPointer = overlay.systemPointerLocationProviderForVerification
            priorSourcePointer = overlay.systemSourceWatchForVerification?.pointerLocationProvider
            priorPreparation = overlay.systemBackdropPreparationForVerification
            priorRuntimeConfiguration = HUDRuntimeAppearance.configuration
            priorInitialModule = overlay.initialModuleRequest
        }

        var source: HUDSourceWatchView {
            guard let view = heldSource ?? overlay.systemSourceWatchForVerification else {
                fail("Original Watch unavailable: \(overlay.systemSourceFailureForVerification ?? "missing")")
            }
            return view
        }
        var widgets: HUDSourceWatchWidgets {
            guard let value = source.document.widgets else { fail("Missing original widget contract") }
            return value
        }
        var sample: HUDSourceWatchWidgets.BannerSample {
            guard let value = source.frameBuilder.widgetBannerSample else { fail("Missing live banner sample") }
            return value
        }
        var position: Double {
            guard let value = sample.contentPosition, value.isFinite else { fail("Missing finite ScrollRect content position") }
            return value
        }
        var dragging: Bool { source.frameBuilder.isWidgetBannerDragging }
        var backingScale: CGFloat { source.window?.backingScaleFactor ?? 1 }

        func start() {
            check(overlay.systemPhase == .closed, "Banner verification begins with a closed HUD")
            configuration.ambientAnimation = false
            configuration.lowPowerVisualMode = false
            configuration.closeOnFocusLost = false
            // Preserve the application's and OS's Reduce Motion policy.
            if let screen = HUDDisplayPolicy.targetScreen(configuration: configuration, screens: NSScreen.screens) ?? NSScreen.main {
                injectedPointer = CGPoint(x: screen.frame.midX, y: screen.frame.midY)
            }
            overlay.systemPointerLocationProviderForVerification = { [weak self] in self?.injectedPointer ?? .zero }
            overlay.systemBackdropPreparationForVerification = { ready in ready() }
            overlay.initialModuleRequest = .power
            check(overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration), "The source banner fixture opens")
            heldSource = overlay.systemSourceWatchForVerification
            priorAction = source.onAction; priorWidgetState = source.widgetState
            priorSourcePointer = priorSourcePointer ?? source.pointerLocationProvider
            var state = HUDSourceWatchWidgets.State.desktopReference
            state.profile = HUDSourceWatchWidgets.Profile(displayName: "管理员", level: 60, isMaximumLevel: true,
                avatarArtwork: "icon_chr_0004_pelica", frameArtwork: "icon_user_avatar_frame_bp_1")
            state.bannerArtworks = ["yvonne_banner", "weapon_typhoeus_banner"]
            state.bannerPaused = true
            suppliedWidgetState = state
            source.widgetState = state
            source.pointerLocationProvider = { [weak self] in self?.injectedPointer ?? .zero }
            source.onAction = { [weak self] action in
                guard let self else { return }
                self.clickedIDs.append(action.source.nodeID)
                self.clickDraggingStates.append(self.dragging)
            }
            later(SystemHUDView.entranceDuration + 0.30) { [self] in checkChildDrag() }
        }

        func checkChildDrag() {
            check(overlay.systemPhase == .open && source.playback.phase == .visible && source.inputEnabled,
                  "Actual source input activates after opening")
            check(sample.artworks.count == 2 && sample.selectedIndex == 0 && abs(position) < 0.01,
                  "Explicit two-artwork fixture begins on page zero")
            guard let child = widgets.bannerInstances.first?.buttonNodeID else { fail("No original runtime banner child") }
            let start = visiblePoint(on: child)
            send(.leftMouseDown, at: start)
            let under = CGPoint(x: start.x + 9 / backingScale, y: start.y)
            send(.leftMouseDragged, at: under)
            check(!dragging && abs(position) < 0.01, "Nine backing pixels preserve a potential child click")
            let threshold = CGPoint(x: start.x + 12 / backingScale, y: start.y)
            send(.leftMouseDragged, at: threshold)
            check(dragging && abs(position) < 0.01,
                  "Twelve backing pixels begin the real drag without jumping content at threshold")
            let local = viewportLocal(threshold)
            let edge = viewportProjection(SIMD2(local.x + 200, local.y))
            send(.leftMouseDragged, at: edge)
            check(dragging && position > 0 && position < 200 && sample.normalizedPosition < 0,
                  "The source viewport stores elastic overscroll rather than a clamped page value")
            // Re-enter an actual child winner before releasing. An outside
            // release alone would not catch an erroneously retained press.
            let release = visiblePoint(on: child)
            send(.leftMouseDragged, at: release)
            check(dragging && winner(release) == child,
                  "The cancelled child press returns to its real masked raycast winner before release")
            let count = clickedIDs.count
            send(.leftMouseUp, at: release)
            check(!dragging && clickedIDs.count == count,
                  "Same-position release has zero new delta and the dragged child never dispatches its cancelled click")
            later(0.8) { [self] in
                checkSettled(index: 0)
                checkUnderThresholdTap(child: child)
            }
        }

        func checkUnderThresholdTap(child: HUDSourceID) {
            let start = visiblePoint(on: child)
            let release = CGPoint(x: start.x + 9 / backingScale, y: start.y)
            check(winner(release) == child, "Subthreshold tap release remains inside the same real child")
            let count = clickedIDs.count
            send(.leftMouseDown, at: start)
            send(.leftMouseDragged, at: release)
            check(!dragging, "A fresh subthreshold gesture remains eligible as a tap")
            send(.leftMouseUp, at: release)
            check(clickedIDs.count == count + 1 && clickedIDs.last == child && clickDraggingStates.last == false,
                  "An under-threshold release dispatches exactly one child tap before pointer cleanup")
            later(0.8) { [self] in checkLeftOutsideDrag() }
        }

        func checkLeftOutsideDrag() {
            guard let child = widgets.bannerInstances.first?.buttonNodeID else { fail("No first source child") }
            let start = visiblePoint(on: child)
            send(.leftMouseDown, at: start)
            let threshold = CGPoint(x: start.x - 12 / backingScale, y: start.y)
            send(.leftMouseDragged, at: threshold)
            check(dragging && abs(position) < 0.01, "A new left drag captures its threshold-time starting content")
            let geometry = viewportGeometry()
            let local = viewportLocal(threshold)
            let outside = viewportProjection(SIMD2(geometry.rect.origin.x - 500, local.y))
            check(geometry.camera.hit(outside, world: geometry.world, rect: geometry.rect, viewport: source.bounds) == nil,
                  "The next drag event is genuinely outside the original banner viewport")
            send(.leftMouseDragged, at: outside)
            check(dragging && sample.normalizedPosition > 0.5 && position < 0 && sample.selectedIndex == 1,
                  "Captured drag survives leaving its viewport and crosses the real center callback")
            let count = clickedIDs.count
            send(.leftMouseUp, at: outside)
            check(!dragging && clickedIDs.count == count, "Outside release completes the captured drag without a child click")
            later(0.8) { [self] in
                checkSettled(index: 1)
                checkSameListPress()
            }
        }

        func checkSameListPress() {
            let list = widgets.bannerListNodeID
            let start = visiblePoint(on: list, retainingShift: 12 / backingScale)
            let shifted = CGPoint(x: start.x + 12 / backingScale, y: start.y)
            let count = clickedIDs.count
            send(.leftMouseDown, at: start)
            send(.leftMouseDragged, at: shifted)
            check(dragging && winner(shifted) == list,
                  "A visible padding/edge press keeps pointerPress equal to the actual list pointerDrag")
            send(.leftMouseUp, at: shifted)
            check(clickedIDs.count == count + 1 && clickedIDs.last == list && clickDraggingStates.last == true,
                  "Same-list click dispatches while dragging is still true, before EndDrag; it is not unconditionally cancelled")
            check(!dragging, "EndDrag follows the same-list click and clears capture")
            later(0.8) { [self] in
                checkSettled(index: 1)
                checkModuleReuse()
            }
        }

        func checkModuleReuse() {
            let retained = source
            overlay.selectSystemModule(.eventLog, animated: false)
            check(overlay.systemSelectedModule == .eventLog && overlay.systemSourceWatchForVerification === retained,
                  "A module handoff conceals the same source view rather than replacing its runtime")
            check(retained.isHidden && retained.playback.phase == .concealed && !dragging
                  && !retained.hasDisplayTimerForVerification && !retained.frameBuilder.requiresWidgetFrames,
                  "Real source concealment clears drag, velocity-driven frame demand, page tween and timer")
            check(sample.selectedIndex == 1 && abs(position + 366.5) < 0.01,
                  "Concealment snaps the retained banner to its actual page-one center")
            overlay.selectSystemModule(.power, animated: false)
            check(overlay.systemSelectedModule == .power && overlay.systemSourceWatchForVerification === retained
                  && retained.playback.phase == .visible && !retained.isHidden,
                  "Returning from the module restores the same source instance through showStable")
            check(sample.selectedIndex == 1 && abs(position + 366.5) < 0.01,
                  "Temporary source reuse preserves page one without resetting its widget state")
            later(0.8) { [self] in
                checkSettled(index: 1)
                checkFullCloseAndReopen()
            }
        }

        func checkFullCloseAndReopen() {
            let closedSource = source
            check(source.widgetState.bannerArtworks == suppliedWidgetState?.bannerArtworks
                  && source.widgetState.bannerPaused && sample.selectedIndex == 1,
                  "Complete close begins with the real page-one fixture still installed")
            // Keep its supplied state and handlers until deactivation has run;
            // restoring the nil-artworks default first would bypass onDisable.
            overlay.closeSystemOverlay()
            later(SystemHUDView.exitDuration + 0.30) { [self, closedSource] in
                check(overlay.systemPhase == .closed && !overlay.systemWindowVisibleForVerification
                      && overlay.systemSourceWatchForVerification == nil && closedSource.isHidden
                      && closedSource.playback.phase == .concealed && !closedSource.hasDisplayTimerForVerification,
                      "Complete close releases the host presentation and conceals its old source view")
                check(!closedSource.frameBuilder.isWidgetBannerDragging && !closedSource.frameBuilder.requiresWidgetFrames
                      && sample.selectedIndex == 1 && abs(position + 366.5) < 0.01,
                      "Old source deactivation clears velocity/tween/capture and retains its clamped page-one center")
                check(!overlay.lastClosedSourceTimerActive && overlay.lastClosedAnimationCount == 0,
                      "Complete close removes every hidden animation before recreating a menu")
                restoreSource()
                heldSource = nil
                overlay.initialModuleRequest = .power
                check(overlay.toggleSystemOverlay(snapshot: snapshot, configuration: configuration),
                      "The fully closed HUD accepts a fresh source-menu opening")
                guard let reopened = overlay.systemSourceWatchForVerification else { fail("Reopened source view is missing") }
                check(reopened !== closedSource, "Full close/reopen reuses the native panel and creates a new source view")
                heldSource = reopened
                // Capture this new view's own production handlers before the
                // fixture policy is supplied; never copy the old view's ones.
                priorAction = reopened.onAction; priorWidgetState = reopened.widgetState
                priorSourcePointer = reopened.pointerLocationProvider
                guard let suppliedWidgetState else { fail("Lost explicit banner fixture policy") }
                reopened.widgetState = suppliedWidgetState
                reopened.pointerLocationProvider = { [weak self] in self?.injectedPointer ?? .zero }
                reopened.onAction = { [weak self] action in
                    guard let self else { return }
                    self.clickedIDs.append(action.source.nodeID)
                    self.clickDraggingStates.append(self.dragging)
                }
                later(SystemHUDView.entranceDuration + 0.30) { [self] in
                    check(overlay.systemPhase == .open && source.playback.phase == .visible && source.inputEnabled,
                          "The recreated original menu completes its real entrance and enables input")
                    check(sample.artworks == suppliedWidgetState.bannerArtworks && sample.selectedIndex == 0,
                          "The same explicit artwork policy starts the new widget on page zero")
                    checkSettled(index: 0)
                    finish()
                }
            }
        }

        func checkSettled(index: Int) {
            guard let first = widgets.bannerInstances.first,
                  let contentID = source.document.scene.node(first.rootID)?.parentID,
                  let content = source.currentFrameForVerification?.resolved[contentID]?.rect else {
                fail("Missing resolved runtime banner content rect")
            }
            let hidden = content.size.x - viewportGeometry().rect.size.x
            // Two source 360-wide cells, spacing6.5 and viewport365 produce a
            // 366.5-unit hidden range in the original runtime list layout.
            check(abs(hidden - 366.5) < 0.0001, "Resolved original two-page content retains its authored hidden range")
            let expected = index == 0 ? 0 : -hidden
            check(sample.selectedIndex == index && abs(position - expected) < 0.01 && !dragging,
                  "Original page snap settles to page \(index) with the authored row geometry")
            check(!source.hasDisplayTimerForVerification && !source.frameBuilder.requiresWidgetFrames,
                  "Paused hold plus ambient-off leave no finite source display timer after settling")
        }

        func viewportGeometry() -> (rect: HUDSourceRect, world: simd_double4x4, camera: HUDSourceCamera) {
            guard let frame = source.currentFrameForVerification, let camera = source.currentCameraForVerification,
                  let viewport = frame.resolved[widgets.bannerListNodeID], let rect = viewport.rect else {
                fail("Missing actual source viewport matrix/rect")
            }
            return (rect, simd_mul(camera.worldRoot, viewport.worldMatrix), camera.camera)
        }
        func viewportLocal(_ point: CGPoint) -> SIMD2<Double> {
            let geometry = viewportGeometry()
            guard let local = geometry.camera.pointOnPlane(point, world: geometry.world, viewport: source.bounds) else {
                fail("Source press camera cannot intersect the drag plane")
            }
            return local
        }
        func viewportProjection(_ local: SIMD2<Double>) -> CGPoint {
            let geometry = viewportGeometry()
            guard let point = geometry.camera.project(SIMD3(local.x, local.y, 0), world: geometry.world,
                viewport: source.bounds)?.point else { fail("Source drag-plane point cannot project") }
            return point
        }
        func winner(_ point: CGPoint) -> HUDSourceID? {
            guard let frame = source.currentFrameForVerification, let camera = source.currentCameraForVerification else { return nil }
            return frame.button(at: point, camera: camera.camera, viewport: source.bounds)
        }
        func visiblePoint(on id: HUDSourceID, retainingShift: CGFloat = 0) -> CGPoint {
            guard let frame = source.currentFrameForVerification, let camera = source.currentCameraForVerification else {
                fail("Missing source frame before a synthetic press")
            }
            // Padding/edges must win the real raycast; merely intersecting the
            // list RectTransform does not manufacture a list pointerPress.
            let fractions: [Double] = [0.5, 0.25, 0.75, 0.99, 0.01, 0.995, 0.005, 0.9, 0.1]
            for hit in frame.hits where hit.buttonID == id {
                for y in fractions { for x in fractions {
                    let local = hit.rect.origin + hit.rect.size * SIMD2(x, y)
                    guard let point = camera.camera.project(SIMD3(local.x, local.y, 0), world: hit.world,
                        viewport: source.bounds)?.point, source.bounds.contains(point), winner(point) == id else { continue }
                    let shifted = CGPoint(x: point.x + retainingShift, y: point.y)
                    if retainingShift == 0 || (source.bounds.contains(shifted) && winner(shifted) == id) { return point }
                } }
            }
            let hits = frame.hits.filter { $0.buttonID == id }.map { $0.graphicID.rawValue }.joined(separator: ",")
            fail("No genuinely visible raycast winner for \(id.rawValue); candidate graphics=\(hits)")
        }

        func send(_ type: NSEvent.EventType, at point: CGPoint) {
            guard let window = source.window else { fail("Source view has no native window") }
            let location = source.convert(point, to: nil)
            injectedPointer = window.convertToScreen(CGRect(origin: location, size: .zero)).origin
            eventNumber += 1
            guard let event = NSEvent.mouseEvent(with: type, location: location, modifierFlags: [],
                timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                context: nil, eventNumber: eventNumber, clickCount: type == .leftMouseDragged ? 0 : 1,
                pressure: type == .leftMouseUp ? 0 : 1) else { fail("Cannot construct native mouse event") }
            switch type {
            case .leftMouseDown: source.mouseDown(with: event)
            case .leftMouseDragged: source.mouseDragged(with: event)
            case .leftMouseUp: source.mouseUp(with: event)
            default: fail("Unsupported fixture event type")
            }
        }

        func restoreSource() {
            source.onAction = priorAction
            if let priorWidgetState { source.widgetState = priorWidgetState }
            if let priorSourcePointer { source.pointerLocationProvider = priorSourcePointer }
        }
        func finish() {
            restoreSource()
            overlay.systemPointerLocationProviderForVerification = priorPointer
            overlay.systemBackdropPreparationForVerification = priorPreparation
            overlay.closeSystemOverlay()
            later(SystemHUDView.exitDuration + 0.30) { [self] in
                check(overlay.systemPhase == .closed && !overlay.systemWindowVisibleForVerification
                      && !overlay.lastClosedSourceTimerActive && overlay.lastClosedAnimationCount == 0,
                      "Fixture closes cleanly and restores handlers, widget state and pointer provider")
                overlay.initialModuleRequest = priorInitialModule
                HUDRuntimeAppearance.configuration = priorRuntimeConfiguration
                print("PASS: \(assertions) source banner drag assertions; 9/12 backing pixels, elastic edge, child cancellation, outside drag, page snap, same-list click-before-EndDrag, retained-view module handoff and recreated-view page reset; no capture APIs")
                fflush(stdout)
                heldSource = nil
                completion()
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
            let view = heldSource ?? overlay.systemSourceWatchForVerification
            let sample = view?.frameBuilder.widgetBannerSample
            fputs("FAIL: Source banner drag: \(message); host=\(overlay.systemPhase.rawValue), source=\(String(describing: view?.playback.phase)), dragging=\(view?.frameBuilder.isWidgetBannerDragging == true), position=\(String(describing: sample?.contentPosition)), page=\(String(describing: sample?.selectedIndex)), timer=\(view?.hasDisplayTimerForVerification == true), clicks=\(clickedIDs.map(\.rawValue))\n", stderr)
            fflush(stderr)
            preconditionFailure(message)
        }
    }
}
