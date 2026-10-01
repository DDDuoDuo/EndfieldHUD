import AppKit
import QuartzCore
import simd

/// The live source menu. A single display clock owns wrapper clips, button
/// Animator states, gyroscope and shader time; rendering and hits use one frame.
final class HUDSourceWatchView: NSView {
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
    let document: HUDSourceWatchDocument
    let renderer: HUDSourceMetalRenderer
    let cameraModel: HUDSourceWatchCamera
    let frameBuilder: HUDSourceWatchFrameBuilder
    let playback: HUDSourceWatchPlayback
    let buttonAnimation: HUDSourceWatchButtonAnimation
    private var gyro: HUDSourceWatchGyroMotion
    private var animatorButtons: [HUDSourceID: HUDSourceID] = [:]
    private var actionsByID: [HUDSourceID: ButtonAction] = [:]
    private var accessibilityButtons: [HUDSourceID: HUDSourceWatchAccessibilityButton] = [:]
    private var observers: [NSObjectProtocol] = []
    private var timer: Timer?
    private var tracking: NSTrackingArea?
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
    var inputEnabled = false {
        didSet {
            if !inputEnabled { hovered = nil; pressed = nil; updateAnimatorStates(at: now) }
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
        for hit in frame.hits where actionsByID[hit.buttonID] != nil {
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
                guard let self, self.inputEnabled, self.playback.phase == .visible,
                      self.renderedFrame?.hits.contains(where: { $0.buttonID == button.nodeID }) == true,
                      let action = self.actionsByID[button.nodeID] else { return false }
                self.onAction?(action); return true
            }
            accessibilityButtons[button.nodeID] = element
        }
        setAccessibilityChildren(document.buttons.compactMap { accessibilityButtons[$0.nodeID] })
    }
    required init?(coder: NSCoder) { fatalError("Use the source-resource initializer") }
    deinit { timer?.invalidate(); observers.forEach { NotificationCenter.default.removeObserver($0) } }

    override func layout() {
        super.layout()
        renderer.frame = bounds
        renderer.drawableSize = CGSize(width: bounds.width * (window?.backingScaleFactor ?? 1),
                                       height: bounds.height * (window?.backingScaleFactor ?? 1))
        render(at: now)
    }
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        observers.forEach { NotificationCenter.default.removeObserver($0) }; observers.removeAll()
        if let window {
            for name in [NSWindow.didChangeOcclusionStateNotification, NSWindow.didMiniaturizeNotification,
                         NSWindow.didDeminiaturizeNotification, NSWindow.willCloseNotification] {
                observers.append(NotificationCenter.default.addObserver(forName: name, object: window, queue: .main) { [weak self] note in
                    guard let self else { return }
                    if note.name == NSWindow.willCloseNotification { self.conceal() }
                    else { self.refreshPlaybackScheduling() }
                })
            }
        } else { stopTimer() }
        refreshPlaybackScheduling()
    }
    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties(); needsLayout = true
    }
    override func updateTrackingAreas() {
        if let tracking { removeTrackingArea(tracking) }
        let area = NSTrackingArea(rect: bounds, options: [.mouseEnteredAndExited, .mouseMoved, .activeAlways, .inVisibleRect], owner: self)
        tracking = area; addTrackingArea(area); super.updateTrackingAreas()
    }

    func open(completion: @escaping () -> Void = {}) {
        isHidden = false
        hovered = nil; pressed = nil
        buttonAnimation.reset(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion)
        playback.open(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion, completion: completion)
        updateAnimatorStates(at: now)
        refreshPlaybackScheduling()
    }
    func showStable() {
        isHidden = false
        playback.showStable(at: now)
        refreshPlaybackScheduling()
    }
    func close(completion: @escaping () -> Void = {}) {
        inputEnabled = false
        playback.close(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion) { [weak self] in
            self?.stopTimer(); self?.isHidden = true; completion()
        }
        refreshPlaybackScheduling()
    }
    func conceal() {
        playback.conceal(); inputEnabled = false; hovered = nil; pressed = nil
        stopTimer(); isHidden = true
        try? gyro.stop(at: now)
        renderedFrame = nil; renderedCamera = nil; lastPose = nil
    }
    func suspendForConcealment() {
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
        guard !isHidden, playback.phase != .concealed else { return }
        render(at: now)
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

    private func render(at time: Double) {
        guard bounds.width > 0, bounds.height > 0, !isHidden, playback.phase != .concealed else { return }
        do {
            let screen = SIMD2<Double>(Double(bounds.width), Double(bounds.height))
            let reduce = HUDRuntimeAppearance.reduceMotion
            if !reduce, isOnScreen, let window {
                let screenPoint = pointerLocationProvider()
                let windowPoint = window.convertPoint(fromScreen: screenPoint)
                let p = convert(windowPoint, from: nil)
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
            buttonAnimation.apply(to: &pose, at: time, reduceMotion: reduce)
            document.applyMacButtonAvailability(to: &pose)
            let frame = try frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                                                verticalNormalizedPosition: verticalNormalizedPosition)
            lastPose = pose; renderedFrame = frame; renderedCamera = camera
            diagnostics = frame.diagnostics
            let position = cameraModel.cameraWorld.columns.3
            // Retain the converted Vulkan program's final Y negation and
            // compensate once in the GPU matrix. CPU render/hit stays +Y up.
            var gpuProjection = simd_float4x4(camera.camera.projection)
            var gpuVP = simd_float4x4(camera.camera.viewProjection)
            for column in 0..<4 { gpuProjection[column].y = -gpuProjection[column].y; gpuVP[column].y = -gpuVP[column].y }
            renderer.submit(camera: HUDSourceMetalRenderer.Camera(
                viewProjection: gpuVP,
                worldSpacePosition: SIMD3(Float(position.x), Float(position.y), Float(position.z)),
                timeSeconds: reduce || !HUDRuntimeAppearance.ambientEnabled ? 0 : Float(time),
                renderPathInjected: 0, flipX: 0, flipY: 0,
                projection: gpuProjection, inverseView: simd_float4x4(cameraModel.cameraWorld)),
                batches: frame.batches)
            renderedFrameCount += 1
            updateAccessibility(frame: frame, camera: camera.camera)
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
    private func button(at point: CGPoint) -> HUDSourceID? {
        guard inputEnabled, playback.phase == .visible, let frame = renderedFrame, let camera = renderedCamera else { return nil }
        return frame.button(at: point, camera: camera.camera, viewport: bounds)
    }
    private func updateHover(_ event: NSEvent) {
        let next = button(at: point(event))
        if next != hovered { hovered = next; updateAnimatorStates(at: now); refreshPlaybackScheduling() }
        else if timer == nil && !HUDRuntimeAppearance.reduceMotion { refreshPlaybackScheduling() }
    }
    override func mouseEntered(with event: NSEvent) { updateHover(event) }
    override func mouseMoved(with event: NSEvent) { updateHover(event) }
    override func mouseExited(with event: NSEvent) { hovered = nil; updateAnimatorStates(at: now); refreshPlaybackScheduling() }
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
        if let action = actionsByID[down] { onAction?(action) }
        else if document.scene.node(down)?.path.contains("TopLeftBtn") == true { onClose?() }
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
