import AppKit
import MetalKit
import QuartzCore
import simd

/// The live source menu. A single display clock owns wrapper clips, button
/// Animator states, gyroscope and shader time; rendering and hits use one frame.
final class HUDSourceWatchView: NSView {
    static let backdropPreparationTimeout: TimeInterval = 3
    struct ButtonAction {
        let source: HUDSourceWatchButton
        let module: HUDModule
    }
    private static let actions: [String: HUDModule] = [
        "CharInfoBtnShadow": .profile, "ActivityBtnShadow": .activityMonitor,
        "GachaBtnShadow": .addApp, "PurchaseBtnNode": .storage,
        "AdventureBookBtnShadow": .notes, "BattlePassShadow": .workMode,
        "DomainBtnShadow": .map, "FriendBtn1Shadow": .profile,
        "EquipBtnShadow": .volume, "CharfomationBtnNode": .addApp,
        "WikiBtnShadow": .about, "ValuablesBtnShadow": .fileShelf,
        "StarShopBtnShadow": .clipboard, "NarrateBtnShadow": .eventLog,
        "BackPackBtnShadow": .fileShelf, "AchievementBtn2Shadow": .activityMonitor,
        "GemEnhanceBtnShadow": .display, "MissionBtnShadow": .notes,
        "MapBtnShadow": .map, "SNSBtnShadow": .addApp,
        "QuestionnaireBtnShadow": .about, "GameToolShadow": .hotkeys,
    ]
    // Paths resolve the actual LuaReference settingBtn/mailBtn/announcementBtn;
    // the numeric TopLeft names are not their visual or sibling order.
    private static let auxiliaryActions: [String: HUDModule] = [
        "TopLeftBtnNode/TopLeftBtn4/btn_4Node/btn": .system,
        "TopLeftBtnNode/TopLeftBtn1/btn_1Node/btn": .notes,
        "TopLeftBtnNode/TopLeftBtn3/btn_3Node/btn": .eventLog,
    ]
    let document: HUDSourceWatchDocument
    let renderer: HUDSourceMetalRenderer
    let cameraModel: HUDSourceWatchCamera
    let frameBuilder: HUDSourceWatchFrameBuilder
    let playback: HUDSourceWatchPlayback
    let buttonAnimation: HUDSourceWatchButtonAnimation
    private var gyro: HUDSourceWatchGyroMotion
    private var animatorButtons: [HUDSourceID: HUDSourceID] = [:]
    private var actionsByID: [HUDSourceID: ButtonAction] = [:]
    private var closeButtonIDs: Set<HUDSourceID> = []
    private var lastAcceptedClick: [HUDSourceID: TimeInterval] = [:]
    private var accessibilityButtons: [HUDSourceID: HUDSourceWatchAccessibilityButton] = [:]
    private var observers: [NSObjectProtocol] = []
    private var timer: Timer?
    private var sourceBackdrop: HUDSourceWatchBackdrop?
    private var backdropAdapter: HUDSourceWatchBackdrop?
    private var backdropTask: Task<Void, Never>?
    private var backdropGeneration: UInt64 = 0
    private struct BackdropGeometry: Equatable {
        let windowNumber: Int
        let rectangle: CGRect
        let drawableSize: CGSize
        let backingScale: CGFloat
        let screenNumber: UInt32
    }
    private var backdropGeometry: BackdropGeometry?
    private struct PendingOpening {
        let heldTime: Double
        let ready: () -> Void
        let completion: () -> Void
    }
    private var pendingOpening: PendingOpening?
    /// Explicit native verification supplies completion timing, never desktop pixels.
    var backdropPreparationForVerification: ((@escaping () -> Void) -> Void)? {
        didSet {
            if let _ = backdropPreparationForVerification {
                precondition(CommandLine.arguments.contains("--ui-test"))
            }
        }
    }
    var isPreparingBackdrop: Bool { pendingOpening != nil }
    var backdropPreparingForVerification: Bool { isPreparingBackdrop }
    var backdropStartForVerification: Double { backdropTransitionStart }
    private var backdropPreparationDeadline: DispatchWorkItem?
    private var backdropTransitionStart: Double = 0
    private(set) var backdropDiagnostics: [String] = []
    /// Native verification and exported previews never read the desktop or
    /// invoke its screen-capture permission APIs.
    private var canCaptureDesktopBackdrop: Bool {
        let process = ProcessInfo.processInfo
        guard process.environment["GITHUB_ACTIONS"] != "true", process.environment["CI"] != "true" else { return false }
        return !CommandLine.arguments.contains { $0 == "--ui-test" || $0.hasSuffix("smoke-test")
            || $0 == "--smoke-test" || $0.hasPrefix("--render-") }
    }
    private var tracking: NSTrackingArea?
    private let cursorBitmap: CGImage
    private let cursorHotspotPixels: CGPoint
    private var sourceCursor: NSCursor?
    private var cursorBackingScale: CGFloat = 0
    private var previousCursor: NSCursor?
    private var hovered: HUDSourceID?
    private var pressed: HUDSourceID?
    private var lastPose: HUDSourceWatchPose?
    private var renderedFrame: HUDSourceWatchFrameBuilder.Frame?
    private var renderedCamera: HUDSourceWatchCamera.Frame?
    private var verticalNormalizedPosition: Double = 1
    private let epoch = CACurrentMediaTime()
    private var lastReducedMotion = HUDRuntimeAppearance.reduceMotion
    private var lastAmbientEnabled = HUDRuntimeAppearance.ambientEnabled
    private(set) var diagnostics: [String] = []
    private(set) var renderedFrameCount = 0
    var onAction: ((ButtonAction) -> Void)?
    var onClose: (() -> Void)?
    var onFailure: ((String) -> Void)?
    var widgetState: HUDSourceWatchWidgets.State {
        get { frameBuilder.widgetState }
        set { frameBuilder.widgetState = newValue; refreshPlaybackScheduling() }
    }
    var inputEnabled = false {
        didSet {
            if !inputEnabled { hovered = nil; pressed = nil; updateAnimatorStates(at: now) }
            refreshSourceCursor()
            refreshPlaybackScheduling()
        }
    }
    /// Screen-point injection is limited to this application's view/fixtures.
    var pointerLocationProvider: () -> CGPoint = { NSEvent.mouseLocation }
    var hasDisplayTimerForVerification: Bool { timer != nil }
    var currentFrameForVerification: HUDSourceWatchFrameBuilder.Frame? { renderedFrame }
    var currentCameraForVerification: HUDSourceWatchCamera.Frame? { renderedCamera }
    /// Exercise the production inverse raycast against projected source
    /// geometry, including original masks; this never synthesizes CA tracks.
    var visibleMainButtonForVerification: HUDSourceID? {
        guard let frame = renderedFrame, let camera = renderedCamera else { return nil }
        for hit in frame.hits where document.buttons.contains(where: { $0.nodeID == hit.buttonID }) {
            let middle = hit.rect.origin + hit.rect.size * 0.5
            let local = SIMD3<Double>(middle.x, middle.y, 0)
            guard let point = camera.camera.project(local, world: hit.world, viewport: bounds)?.point,
                  bounds.contains(point), frame.button(at: point, camera: camera.camera, viewport: bounds) == hit.buttonID else { continue }
            return hit.buttonID
        }
        return nil
    }
    private var now: Double { CACurrentMediaTime() - epoch }
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    private var isOnScreen: Bool {
        guard let window, !isHiddenOrHasHiddenAncestor, !window.isMiniaturized else { return false }
        return window.occlusionState.contains(.visible)
    }

    init(frame: CGRect, document: HUDSourceWatchDocument? = nil) throws {
        let document = try document ?? HUDSourceWatchDocument()
        self.document = document
        renderer = try HUDSourceMetalRenderer(frame: CGRect(origin: .zero, size: frame.size),
                                             resourceRoot: document.root.deletingLastPathComponent())
        let root = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: document.root.appendingPathComponent("runtime-root-camera.json")))
        cameraModel = try HUDSourceWatchCamera(runtimeRoot: root)
        gyro = try HUDSourceWatchGyroMotion(initialRotation: cameraModel.rootRotation)
        guard let cursorURL = HUDResources.url(for: "WatchSource/Cursor/player-default-icon_mouse.png"),
              let image = NSImage(contentsOf: cursorURL),
              let bitmap = image.cgImage(forProposedRect: nil, context: nil, hints: nil),
              let cursorManifestURL = HUDResources.url(for: "WatchSource/Cursor/player-default.json") else {
            throw HUDSourceError.invalid("Missing original Watch cursor resource")
        }
        let cursorManifest = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: cursorManifestURL))
        guard bitmap.width == Int(cursorManifest["width"].float()),
              bitmap.height == Int(cursorManifest["height"].float()),
              let hotspot = cursorManifest["typed_prefix_fields"].array.first(where: { $0["name"].string == "cursorHotspot" }),
              let hotspotX = hotspot["value"]["x"].number,
              let hotspotY = hotspot["value"]["y"].number else {
            throw HUDSourceError.invalid("Invalid original Watch cursor manifest")
        }
        cursorBitmap = bitmap
        cursorHotspotPixels = CGPoint(x: hotspotX, y: hotspotY)
        frameBuilder = try HUDSourceWatchFrameBuilder(document: document, renderer: renderer)
        playback = HUDSourceWatchPlayback(animation: document.animation)
        buttonAnimation = try HUDSourceWatchButtonAnimation(document: document)
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
        addSubview(renderer)
        setAccessibilityElement(false)
        func buttonAncestor(_ initial: HUDSourceID) -> HUDSourceID? {
            var id: HUDSourceID? = initial
            while let current = id {
                if document.component("UIButton", on: current) != nil { return current }
                id = document.scene.node(current)?.parentID
            }
            return nil
        }
        for source in document.animators {
            animatorButtons[source.rootID] = buttonAncestor(source.rootID)
        }
        for button in document.buttons {
            guard let node = document.scene.node(button.nodeID), let module = Self.actions[node.name] else {
                throw HUDSourceError.invalid("Unmapped original Watch main button: \(button.path)")
            }
            actionsByID[button.nodeID] = ButtonAction(source: button, module: module)
            let element = HUDSourceWatchAccessibilityButton()
            element.setAccessibilityRole(.button)
            element.setAccessibilityLabel(button.label?.literal ?? node.name)
            element.setAccessibilityHelp(module.title)
            element.setAccessibilityParent(self)
            element.performPress = { [weak self] in
                guard let self else { return false }
                return self.performClick(on: button.nodeID, at: self.now)
            }
            accessibilityButtons[button.nodeID] = element
        }
        for id in document.scene.traversalIDs {
            guard let node = document.scene.node(id), document.component("UIButton", on: id) != nil else { continue }
            if node.path.hasSuffix("/CloseButtonNode/Btn_BackNode") || node.path.hasSuffix("/FullScreenCloseBtn") {
                closeButtonIDs.insert(id)
            }
            if let entry = Self.auxiliaryActions.first(where: { node.path.hasSuffix("/" + $0.key) }) {
                let source = HUDSourceWatchButton(nodeID: id, path: node.path, labels: [])
                actionsByID[id] = ButtonAction(source: source, module: entry.value)
            }
            if document.widgets?.profileButtonIDs.contains(id) == true {
                let source = HUDSourceWatchButton(nodeID: id, path: node.path, labels: [])
                actionsByID[id] = ButtonAction(source: source, module: .profile)
            }
        }
        setAccessibilityChildren(document.buttons.compactMap { accessibilityButtons[$0.nodeID] })
        if #available(macOS 14.0, *), canCaptureDesktopBackdrop {
            do {
                // Compile the original filter and HDR materials while the view
                // is being created, before a visible opening animation starts.
                backdropAdapter = try HUDSourceWatchBackdrop(renderer: renderer,
                    resourceRoot: document.root.deletingLastPathComponent())
                try renderer.enableSourceRGBHDR()
                try renderer.disableSourceRGBHDR()
            } catch {
                backdropAdapter = nil; try? renderer.disableSourceRGBHDR()
                backdropDiagnostics = [String(describing: error)]
            }
        }
    }
    required init?(coder: NSCoder) { fatalError("Use the source-resource initializer") }
    deinit {
        backdropTask?.cancel()
        backdropPreparationDeadline?.cancel()
        previousCursor?.set()
        timer?.invalidate(); observers.forEach { NotificationCenter.default.removeObserver($0) }
    }

    override func layout() {
        super.layout()
        renderer.frame = bounds
        renderer.drawableSize = CGSize(width: bounds.width * (window?.backingScaleFactor ?? 1),
                                       height: bounds.height * (window?.backingScaleFactor ?? 1))
        refreshBackdropGeometry()
        render(at: now)
    }
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        restoreSourceCursor()
        observers.forEach { NotificationCenter.default.removeObserver($0) }; observers.removeAll()
        if let window {
            for name in [NSWindow.didChangeOcclusionStateNotification, NSWindow.didMiniaturizeNotification,
                         NSWindow.didDeminiaturizeNotification, NSWindow.willCloseNotification,
                         NSWindow.didResignKeyNotification, NSWindow.didBecomeKeyNotification,
                         NSWindow.didMoveNotification, NSWindow.didResizeNotification,
                         NSWindow.didChangeScreenNotification] {
                observers.append(NotificationCenter.default.addObserver(forName: name, object: window, queue: .main) { [weak self] note in
                    guard let self else { return }
                    if note.name == NSWindow.willCloseNotification { self.conceal() }
                    else {
                        self.refreshBackdropGeometry()
                        self.refreshPlaybackScheduling()
                    }
                })
            }
        } else {
            cancelPendingOpening()
            cancelBackdropCapture(); sourceBackdrop = nil; backdropGeometry = nil
            try? renderer.disableSourceRGBHDR()
            stopTimer()
        }
        refreshBackdropGeometry()
        refreshPlaybackScheduling()
    }
    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties(); needsLayout = true
        refreshBackdropGeometry()
        sourceCursor = nil; refreshSourceCursor()
    }
    override func updateTrackingAreas() {
        if let tracking { removeTrackingArea(tracking) }
        let area = NSTrackingArea(rect: bounds, options: [.mouseEnteredAndExited, .mouseMoved, .cursorUpdate, .activeAlways, .inVisibleRect], owner: self)
        tracking = area; addTrackingArea(area); super.updateTrackingAreas()
    }

    func open(ready: @escaping () -> Void = {}, completion: @escaping () -> Void = {}) {
        isHidden = false
        cancelPendingOpening()
        backdropTransitionStart = now
        hovered = nil; pressed = nil
        if let preparation = backdropPreparationForVerification {
            precondition(CommandLine.arguments.contains("--ui-test") && !canCaptureDesktopBackdrop)
            cancelBackdropCapture()
            let generation = backdropGeneration
            holdOpeningForBackdrop(ready: ready, completion: completion)
            preparation { [weak self] in
                precondition(Thread.isMainThread)
                guard let self, self.backdropGeneration == generation else { return }
                self.startPendingOpening()
            }
            return
        }
        if #available(macOS 14.0, *), canCaptureDesktopBackdrop,
           HUDSourceDesktopBackdrop.preflightPermission() == .granted {
            // Hold the original initial pose while preparing its real input.
            // Menu and blur start together; capture latency cannot consume the
            // source's short 0.133-second background entrance.
            holdOpeningForBackdrop(ready: ready, completion: completion)
            requestDesktopBackdrop()
            return
        }
        ready()
        buttonAnimation.reset(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion)
        playback.open(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion, completion: completion)
        updateAnimatorStates(at: now)
        refreshPlaybackScheduling()
        requestDesktopBackdrop()
    }
    func showStable() {
        isHidden = false
        cancelPendingOpening()
        backdropTransitionStart = now
        playback.showStable(at: now)
        // Completing an entrance suspends the wrapper before selecting its
        // stable pose. Keep its successfully prepared pixels and HDR pipelines
        // when that suspension has not changed the window's capture geometry.
        if sourceBackdrop == nil || backdropGeometry == nil || backdropGeometry != currentBackdropGeometry() {
            requestDesktopBackdrop()
        }
        refreshPlaybackScheduling()
    }
    func close(completion: @escaping () -> Void = {}) {
        cancelPendingOpening()
        cancelBackdropCapture()
        backdropTransitionStart = now
        inputEnabled = false
        playback.close(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion) { [weak self] in
            self?.stopTimer(); self?.isHidden = true; completion()
        }
        refreshPlaybackScheduling()
    }
    func conceal() {
        cancelPendingOpening()
        cancelBackdropCapture()
        playback.conceal(); inputEnabled = false; hovered = nil; pressed = nil
        stopTimer(); isHidden = true
        try? gyro.stop(at: now)
        renderedFrame = nil; renderedCamera = nil; lastPose = nil
    }
    func suspendForConcealment() {
        cancelPendingOpening()
        cancelBackdropCapture()
        // Cancellation drops wrapper callbacks before input invalidation can
        // schedule a render. Preserve the last drawable until the owner hides
        // the view or chooses a new stable/opening pose.
        playback.conceal(); inputEnabled = false; stopTimer(); try? gyro.stop(at: now)
    }
    func renderedImageForVerification() throws -> CGImage {
        guard !isHidden, playback.phase != .concealed else {
            throw HUDSourceError.invalid("Cannot capture a concealed source Watch menu")
        }
        render(at: now)
        guard let frame = renderedFrame, !frame.batches.isEmpty, playback.phase != .concealed else {
            throw HUDSourceError.invalid("Source Watch did not produce a renderable frame: \(diagnostics.joined(separator: "; "))")
        }
        renderer.draw()
        let image = try renderer.copyDrawableImage()
        guard renderer.diagnostics.isEmpty else {
            throw HUDSourceError.invalid("Source Watch GPU skipped content: \(renderer.diagnostics.joined(separator: "; "))")
        }
        return image
    }
    func refreshMotionPreferences() {
        let reduce = HUDRuntimeAppearance.reduceMotion
        if reduce != lastReducedMotion || HUDRuntimeAppearance.ambientEnabled != lastAmbientEnabled {
            lastReducedMotion = reduce; lastAmbientEnabled = HUDRuntimeAppearance.ambientEnabled
            if reduce { _ = try? gyro.retarget(eulerDegrees: .zero, at: now, duration: cameraModel.gyro.duration, reduceMotion: true) }
            refreshPlaybackScheduling()
        }
    }

    private func refreshPlaybackScheduling() {
        stopTimer()
        refreshSourceCursor()
        guard !isHidden, playback.phase != .concealed else { return }
        render(at: now)
        guard pendingOpening == nil else { return }
        guard isOnScreen else { return }
        let finite = playback.phase == .opening || playback.phase == .closing || gyro.isAnimating || buttonAnimation.requiresFrames(at: now)
        guard !HUDRuntimeAppearance.reduceMotion && (finite || HUDRuntimeAppearance.ambientEnabled) else { return }
        let timer = Timer(timeInterval: 1 / 60, repeats: true) { [weak self] _ in
            guard let self else { return }
            guard self.isOnScreen, self.playback.phase != .concealed else { self.stopTimer(); return }
            self.render(at: self.now)
            if !HUDRuntimeAppearance.ambientEnabled && self.playback.phase == .visible && !self.gyro.isAnimating
                && !self.buttonAnimation.requiresFrames(at: self.now) {
                self.stopTimer()
            }
        }
        timer.tolerance = 0.002
        RunLoop.main.add(timer, forMode: .common); self.timer = timer
    }
    private func stopTimer() { timer?.invalidate(); timer = nil }

    private func cancelBackdropCapture() {
        backdropGeneration &+= 1
        backdropTask?.cancel(); backdropTask = nil
    }

    private func cancelPendingOpening() {
        backdropPreparationDeadline?.cancel(); backdropPreparationDeadline = nil
        pendingOpening = nil
    }

    private func holdOpeningForBackdrop(ready: @escaping () -> Void, completion: @escaping () -> Void) {
        pendingOpening = PendingOpening(heldTime: now, ready: ready, completion: completion)
        armBackdropPreparationDeadline()
        playback.open(at: now, reduceMotion: false)
        refreshPlaybackScheduling()
    }

    private func armBackdropPreparationDeadline() {
        backdropPreparationDeadline?.cancel()
        let deadline = DispatchWorkItem { [weak self] in
            guard let self, self.pendingOpening != nil,
                  !self.isHidden, self.playback.phase == .opening else { return }
            // ScreenCaptureKit cancellation need not abort its system request.
            // Invalidate its generation before starting the bounded fallback.
            self.cancelBackdropCapture()
            self.sourceBackdrop = nil
            self.backdropDiagnostics = ["Desktop backdrop preparation timed out; using the system backdrop"]
            try? self.renderer.disableSourceRGBHDR()
            self.startPendingOpening()
        }
        backdropPreparationDeadline = deadline
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.backdropPreparationTimeout, execute: deadline)
    }

    private func currentBackdropGeometry() -> BackdropGeometry? {
        guard let window, bounds.width > 0, bounds.height > 0,
              renderer.drawableSize.width > 0, renderer.drawableSize.height > 0 else { return nil }
        let screenNumber = (window.screen?.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?.uint32Value ?? 0
        return BackdropGeometry(windowNumber: window.windowNumber,
            rectangle: window.convertToScreen(convert(bounds, to: nil)),
            drawableSize: renderer.drawableSize, backingScale: window.backingScaleFactor,
            screenNumber: screenNumber)
    }

    private func refreshBackdropGeometry() {
        guard canCaptureDesktopBackdrop, !isHidden, playback.phase != .concealed,
              let geometry = currentBackdropGeometry(), geometry != backdropGeometry else { return }
        // An old display crop must never stretch across a moved/resized HUD.
        if playback.phase == .closing {
            cancelBackdropCapture(); sourceBackdrop = nil; backdropGeometry = geometry
            try? renderer.disableSourceRGBHDR()
        } else { requestDesktopBackdrop() }
    }

    private func requestDesktopBackdrop() {
        cancelBackdropCapture()
        defer { if backdropTask == nil { startPendingOpening() } }
        guard canCaptureDesktopBackdrop else { return }
        sourceBackdrop = nil
        backdropGeometry = nil
        do { try renderer.disableSourceRGBHDR() }
        catch { backdropDiagnostics = [String(describing: error)]; return }
        guard #available(macOS 14.0, *), let window,
              let geometry = currentBackdropGeometry(),
              HUDSourceDesktopBackdrop.preflightPermission() == .granted else { return }
        backdropGeometry = geometry
        let generation = backdropGeneration
        let rectangle = geometry.rectangle
        backdropTask = Task { @MainActor [weak self, weak window] in
            defer {
                if let self, self.backdropGeneration == generation {
                    self.backdropTask = nil
                    self.startPendingOpening()
                }
            }
            do {
                // Allow the just-opened HUD to enter WindowServer's inventory.
                // This delay owns no display loop and cancellation ends it.
                try await Task.sleep(nanoseconds: 33_333_333)
                guard let window else { return }
                let captured = try await HUDSourceDesktopBackdrop().captureBelowHUD(
                    window: window, appKitGlobalRect: rectangle)
                try Task.checkCancellation()
                guard let self, self.backdropGeneration == generation,
                      !self.isHidden, self.playback.phase != .concealed && self.playback.phase != .closing,
                      self.window === window,
                      self.currentBackdropGeometry() == geometry else { return }
                let backdrop: HUDSourceWatchBackdrop
                if let preparedAdapter = self.backdropAdapter { backdrop = preparedAdapter }
                else {
                    backdrop = try HUDSourceWatchBackdrop(renderer: self.renderer,
                        resourceRoot: self.document.root.deletingLastPathComponent())
                    self.backdropAdapter = backdrop
                }
                try backdrop.prepare(frame: captured, drawableSize: geometry.drawableSize)
                self.sourceBackdrop = backdrop; self.backdropDiagnostics = []
                self.render(at: self.now)
            } catch is CancellationError {
                // Concealment invalidates the capture without touching a newer task.
            } catch {
                guard let self, self.backdropGeneration == generation else { return }
                self.backdropDiagnostics = [String(describing: error)]
                NSLog("Source Watch desktop backdrop: %@", String(describing: error))
            }
        }
    }

    private func startPendingOpening() {
        guard let pending = pendingOpening else { return }
        cancelPendingOpening()
        guard !isHidden, playback.phase == .opening else { return }
        backdropTransitionStart = now
        pending.ready()
        buttonAnimation.reset(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion)
        playback.open(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion, completion: pending.completion)
        updateAnimatorStates(at: now)
        refreshPlaybackScheduling()
    }

    private func backdropAlpha(at time: Double, reduceMotion: Bool) -> Float {
        guard !reduceMotion else { return playback.phase == .concealed ? 0 : 1 }
        let elapsed = max(0, time - backdropTransitionStart)
        switch playback.phase {
        case .opening: return Float(document.blurAnimation.entrance.alpha(at: elapsed) ?? document.blurAnimation.entrance.endAlpha)
        case .closing: return Float(document.blurAnimation.exit.alpha(at: elapsed) ?? document.blurAnimation.exit.endAlpha)
        case .visible: return 1
        case .concealed: return 0
        }
    }

    private func render(at requestedTime: Double) {
        guard bounds.width > 0, bounds.height > 0, !isHidden, playback.phase != .concealed else { return }
        let time = pendingOpening?.heldTime ?? requestedTime
        do {
            let screen = SIMD2<Double>(Double(bounds.width), Double(bounds.height))
            let reduce = pendingOpening == nil && HUDRuntimeAppearance.reduceMotion
            let pointerPoint = window.map { window in
                convert(window.convertPoint(fromScreen: pointerLocationProvider()), from: nil)
            }
            if !reduce, isOnScreen, window != nil {
                guard let p = pointerPoint else { throw HUDSourceError.invalid("Source Watch pointer conversion failed") }
                let euler = try cameraModel.gyro.targetEuler(mouseUnity: SIMD2(Double(p.x), Double(bounds.height - p.y)), screenSize: screen)
                _ = try gyro.retarget(eulerDegrees: euler, at: time, duration: cameraModel.gyro.duration)
            }
            gyro.finishIfNeeded(at: time)
            let camera = try cameraModel.frame(screenSize: screen, localRotation: gyro.rotation(at: time))
            guard var pose = try playback.sample(at: time, canvasResolution: camera.layout.canvasSize, reduceMotion: reduce) else {
                stopTimer(); return
            }
            // Low-power/ambient-off presentation freezes wrapper-loop and shader
            // clocks; finite open/close and hover remain source-timed.
            if !HUDRuntimeAppearance.ambientEnabled, playback.phase == .visible {
                pose = try document.animation.pose(entranceTime: document.animation.entrance.lastKeyTime,
                    ambientTime: nil, exitTime: nil, canvasResolution: camera.layout.canvasSize)
            }
            let wrapperPose = pose
            buttonAnimation.apply(to: &pose, at: time, reduceMotion: reduce)
            document.applyMacButtonAvailability(to: &pose)
            let domainState = HUDSourceDomainAnimation.State(ambientTime: reduce || !HUDRuntimeAppearance.ambientEnabled ? 0 : time)
            var frame = try frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                                               verticalNormalizedPosition: verticalNormalizedPosition, domainAnimationState: domainState,
                                               widgetTime: time)
            // Source EventSystem raycasts each frame, including stationary
            // pointers while the menu or gyro moves. Retarget at this same
            // clock instant, then rebuild once; do not recurse into the timer.
            let nextHover: HUDSourceID?
            if inputEnabled, playback.phase == .visible, isOnScreen,
               let pointerPoint, bounds.contains(pointerPoint) {
                nextHover = frame.button(at: pointerPoint, camera: camera.camera, viewport: bounds)
            } else { nextHover = nil }
            if nextHover != hovered {
                hovered = nextHover; updateAnimatorStates(at: time)
                pose = wrapperPose
                buttonAnimation.apply(to: &pose, at: time, reduceMotion: reduce)
                document.applyMacButtonAvailability(to: &pose)
                frame = try frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                                                verticalNormalizedPosition: verticalNormalizedPosition, domainAnimationState: domainState,
                                                widgetTime: time)
            }
            lastPose = pose; renderedFrame = frame; renderedCamera = camera
            diagnostics = frame.diagnostics
            let position = cameraModel.cameraWorld.columns.3
            // Retain the converted Vulkan program's final Y negation and
            // compensate once in the GPU matrix. CPU render/hit stays +Y up.
            var gpuProjection = HUDSourceGeometry.floatMatrix(camera.camera.projection)
            var gpuVP = HUDSourceGeometry.floatMatrix(camera.camera.viewProjection)
            for column in 0..<4 { gpuProjection[column].y = -gpuProjection[column].y; gpuVP[column].y = -gpuVP[column].y }
            var batches = frame.batches
            if let background = sourceBackdrop?.batch(alpha: backdropAlpha(at: time, reduceMotion: reduce)) {
                // Source WatchBlur's UI3D category sorts before Watch's Window
                // category; its screen-space batch uses its own projection.
                batches.insert(background, at: 0)
            }
            renderer.submit(camera: HUDSourceMetalRenderer.Camera(
                viewProjection: gpuVP,
                viewNoTranslationProjection: try HUDSourceWatchCamera.viewNoTranslationProjection(
                    gpuProjection: gpuProjection, view: camera.camera.view),
                worldSpacePosition: SIMD3(Float(position.x), Float(position.y), Float(position.z)),
                timeSeconds: reduce || !HUDRuntimeAppearance.ambientEnabled ? 0 : Float(time),
                // The original freshly constructed offscreen UI color pass
                // has a zeroed payload and writes this camera-relative tuple.
                renderPathInjected: 1, flipX: 0, flipY: 0,
                projection: gpuProjection, inverseView: HUDSourceGeometry.floatMatrix(cameraModel.shaderCameraToWorld),
                uiProjectionParameters: try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: gpuProjection,
                    near: Float(cameraModel.near), far: Float(cameraModel.far))),
                batches: batches)
            renderedFrameCount += 1
            updateAccessibility(frame: frame, camera: camera.camera)
            refreshSourceCursor()
        } catch {
            diagnostics = [String(describing: error)]
            stopTimer(); playback.conceal(); inputEnabled = false
            onFailure?(String(describing: error))
        }
    }

    private func updateAnimatorStates(at time: Double) {
        let reduce = HUDRuntimeAppearance.reduceMotion
        for (root, button) in animatorButtons {
            if buttonAnimation.state(on: root) == .disabled { continue }
            let desired: HUDSourceWatchButtonAnimation.State = button == pressed ? .pressed : (button == hovered ? .highlighted : .normal)
            buttonAnimation.setState(desired, on: root, at: time, reduceMotion: reduce)
            buttonAnimation.setHovered(button == hovered, on: root, at: time, reduceMotion: reduce)
        }
    }

    private func point(_ event: NSEvent) -> CGPoint { convert(event.locationInWindow, from: nil) }
    private func refreshSourceCursor() {
        guard inputEnabled, playback.phase == .visible, isOnScreen, let window,
              window.isKeyWindow, NSApp.isActive else { restoreSourceCursor(); return }
        let p = convert(window.convertPoint(fromScreen: pointerLocationProvider()), from: nil)
        guard visibleRect.contains(p) else { restoreSourceCursor(); return }
        let scale = max(window.backingScaleFactor, 1)
        if sourceCursor == nil || cursorBackingScale != scale {
            // AppKit image sizes are points. This adapter preserves the original
            // bitmap's backing-pixel extent; game runtime/DPI overrides are unknown.
            let image = NSImage(cgImage: cursorBitmap,
                size: NSSize(width: CGFloat(cursorBitmap.width) / scale,
                             height: CGFloat(cursorBitmap.height) / scale))
            sourceCursor = NSCursor(image: image,
                hotSpot: NSPoint(x: cursorHotspotPixels.x / scale, y: cursorHotspotPixels.y / scale))
            cursorBackingScale = scale
        }
        if previousCursor == nil { previousCursor = NSCursor.current }
        sourceCursor?.set()
    }
    private func restoreSourceCursor() {
        guard let previousCursor else { return }
        previousCursor.set(); self.previousCursor = nil
    }
    private func button(at point: CGPoint) -> HUDSourceID? {
        guard inputEnabled, playback.phase == .visible, let frame = renderedFrame, let camera = renderedCamera else { return nil }
        return frame.button(at: point, camera: camera.camera, viewport: bounds)
    }
    /// Both mouse release and AX activation pass the actual resolved source
    /// raycast/masks and the original UIButton per-instance click cooldown.
    @discardableResult private func performClick(on id: HUDSourceID, at time: TimeInterval, point: CGPoint? = nil) -> Bool {
        guard inputEnabled, playback.phase == .visible, !isHidden,
              let frame = renderedFrame, let camera = renderedCamera,
              frame.resolved[id]?.activeInHierarchy == true,
              let component = document.component("UIButton", on: id), component.enabled,
              component["m_Interactable"].flag(true),
              actionsByID[id] != nil || closeButtonIDs.contains(id) else { return false }
        let eligible: Bool
        if let point {
            eligible = bounds.contains(point) && frame.button(at: point, camera: camera.camera, viewport: bounds) == id
        } else { eligible = frame.hits.filter { $0.buttonID == id }.contains { hit in
            // A masked/offscreen button cannot be activated by accessibility.
            // Probe its interior using the same source matrices as mouse hits.
            [SIMD2<Double>(0.5, 0.5), SIMD2(0.25, 0.25), SIMD2(0.75, 0.25),
             SIMD2(0.25, 0.75), SIMD2(0.75, 0.75)].contains { fraction in
                let p = hit.rect.origin + hit.rect.size * fraction
                guard let projected = camera.camera.project(SIMD3<Double>(p.x, p.y, 0), world: hit.world, viewport: bounds)?.point,
                      bounds.contains(projected) else { return false }
                return frame.button(at: projected, camera: camera.camera, viewport: bounds) == id
            }
        } }
        guard eligible else { return false }
        let cooldown = component["_clickCd"].float()
        guard cooldown.isFinite, cooldown >= 0,
              lastAcceptedClick[id].map({ time > $0 + cooldown }) ?? true else { return false }
        lastAcceptedClick[id] = time
        if let action = actionsByID[id] { onAction?(action) }
        else { onClose?() }
        return true
    }
    private func updateHover(_ event: NSEvent) {
        refreshSourceCursor()
        let next = button(at: point(event))
        if next != hovered { hovered = next; updateAnimatorStates(at: now); refreshPlaybackScheduling() }
        else if timer == nil && !HUDRuntimeAppearance.reduceMotion { refreshPlaybackScheduling() }
    }
    override func mouseEntered(with event: NSEvent) { updateHover(event) }
    override func mouseMoved(with event: NSEvent) { updateHover(event) }
    override func mouseExited(with event: NSEvent) { restoreSourceCursor(); hovered = nil; updateAnimatorStates(at: now); refreshPlaybackScheduling() }
    override func cursorUpdate(with event: NSEvent) { refreshSourceCursor() }
    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        pressed = button(at: point(event)); hovered = pressed
        updateAnimatorStates(at: now); refreshPlaybackScheduling()
    }
    override func mouseDragged(with event: NSEvent) { updateHover(event) }
    override func mouseUp(with event: NSEvent) {
        let released = button(at: point(event)), down = pressed
        pressed = nil; hovered = released; updateAnimatorStates(at: now); refreshPlaybackScheduling()
        guard let down, released == down else { return }
        performClick(on: down, at: now, point: point(event))
    }
    override func keyDown(with event: NSEvent) {
        if inputEnabled, event.keyCode == 53 { onClose?() } else { super.keyDown(with: event) }
    }
    override func scrollWheel(with event: NSEvent) {
        guard inputEnabled, playback.phase == .visible, let frame = renderedFrame,
              let camera = renderedCamera, let scroll = frame.layoutReport.scroll,
              scroll.hiddenLength > 0, let node = frame.resolved[scroll.viewportID],
              let rect = node.rect,
              camera.camera.hit(point(event), world: simd_mul(camera.worldRoot, node.worldMatrix),
                                rect: rect, viewport: bounds) != nil else { super.scrollWheel(with: event); return }
        let delta = event.hasPreciseScrollingDeltas ? Double(event.scrollingDeltaY) : Double(event.scrollingDeltaY) * 10
        verticalNormalizedPosition = HUDSourceWatchLayout(document: document).scrolledPosition(
            verticalNormalizedPosition, delta: delta, info: scroll)
        render(at: now)
    }
    private func updateAccessibility(frame: HUDSourceWatchFrameBuilder.Frame, camera: HUDSourceCamera) {
        guard let window else { return }
        for (id, element) in accessibilityButtons {
            let hits = frame.hits.filter { $0.buttonID == id }
            let points = hits.flatMap { hit in hit.rect.corners.compactMap { camera.project($0, world: hit.world, viewport: bounds)?.point } }
            guard let first = points.first else { element.setAccessibilityEnabled(false); continue }
            let minX = points.map(\.x).min() ?? first.x, maxX = points.map(\.x).max() ?? first.x
            let minY = points.map(\.y).min() ?? first.y, maxY = points.map(\.y).max() ?? first.y
            let rect = CGRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY).intersection(bounds)
            guard !rect.isEmpty else { element.setAccessibilityEnabled(false); continue }
            let screen = window.convertToScreen(convert(rect, to: nil))
            element.setAccessibilityFrame(screen)
            element.setAccessibilityEnabled(inputEnabled)
        }
    }
}

private final class HUDSourceWatchAccessibilityButton: NSAccessibilityElement {
    var performPress: (() -> Bool)?
    override func accessibilityPerformPress() -> Bool { performPress?() ?? false }
}
