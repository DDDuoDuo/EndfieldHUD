// Visible, isolated comparison harness shared by stable and integrated builds.
// No global input events, permissions, real stores, or application launches.
import AppKit
import Darwin
import simd

@main
enum HUDIntegrationPerformance {
    static func main() {
        precondition(CommandLine.arguments.contains("--ui-test"), "Use isolated fixtures")
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let session = Session()
        app.delegate = session
        withExtendedLifetime(session) { app.run() }
    }

    private final class Session: NSObject, NSApplicationDelegate {
        private let overlay = OverlayController()
        private var configuration = AppConfiguration.defaults
        private var rows: [[String: Any]] = []
        private var pointerTimer: Timer?
        private var physicalMouseMonitor: Any?
        private var ignoredPhysicalMouseEvents = 0
        private var pointer = CGPoint.zero
        private var firstOpenMilliseconds = 0.0
        private var firstCompletedMilliseconds: Double?
        private var firstPresentedMilliseconds: Double?
        private var sourcePreparationMilliseconds: Double?
        private var sourceProgramPreparationMilliseconds: Double?
        private var sourceProgramPreparationStatistics: [String: Int] = [:]
        private var sourceProgramPreparationFailure: String?
        private var warmOpenMilliseconds: Double?
        private var closedHeapRelief: [String: Any]?
        private let output: URL = {
            let args = CommandLine.arguments
            let index = args.firstIndex(of: "--output")
            return URL(fileURLWithPath: index.map { args[$0 + 1] } ?? "/tmp/endfield-hud-performance.json")
        }()

        func applicationDidFinishLaunching(_ notification: Notification) {
            // The benchmark uses direct verification hooks, never NSEvents.
            // Filtering only this fixture process keeps tracking/gesture events
            // from retargeting hover even if a window lifecycle resets its flags.
            physicalMouseMonitor = NSEvent.addLocalMonitorForEvents(matching: [
                .mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate,
                .leftMouseDown, .leftMouseUp, .leftMouseDragged,
                .rightMouseDown, .rightMouseUp, .rightMouseDragged,
                .otherMouseDown, .otherMouseUp, .otherMouseDragged,
                .scrollWheel, .magnify, .rotate, .swipe, .pressure
            ]) { [weak self] _ in
                self?.ignoredPhysicalMouseEvents += 1
                return nil
            }
            configuration.closeOnFocusLost = false
            configuration.reduceMotion = false
            configuration.lowPowerVisualMode = false
            configuration.ambientAnimation = true
            guard let screen = NSScreen.main else { fatalError("An attached display is required") }
            pointer = CGPoint(x: screen.frame.midX, y: screen.frame.midY)
            overlay.systemPointerLocationProviderForVerification = { [weak self] in self?.pointer ?? .zero }
            overlay.activity.start()
            overlay.clipboard.start()
            #if HUD_SOURCE_INTEGRATION
            if !CommandLine.arguments.contains("--cold-source") {
                let began = CACurrentMediaTime()
                DispatchQueue.global(qos: .utility).async {
                    do {
                        _ = try HUDSourceWatchDocument.desktop()
                        try HUDSourceMetalRenderer.prepareDesktopMetadataIfNeeded()
                    }
                    catch { fatalError("Source preparation failed: \(error)") }
                    var programMilliseconds: Double?
                    var programFailure: String?
                    if !CommandLine.arguments.contains("--skip-program-prewarm") {
                        let programBegan = CACurrentMediaTime()
                        do {
                            let first = try HUDSourceMetalRenderer.prepareDesktopProgramsIfNeeded()
                            precondition(first["prewarmCompleted"] == 1 && (first["prewarmShaders"] ?? 0) > 0
                                && (first["prewarmShaders"] ?? 0) <= 8 && (first["libraries"] ?? 0) <= 16
                                && first["pipelines"] == 0, "Program preparation exceeded its bounded scope")
                            if CommandLine.arguments.contains("--verify-only") {
                                let repeated = try HUDSourceMetalRenderer.prepareDesktopProgramsIfNeeded()
                                precondition(first == repeated, "Repeated program preparation changed its cache")
                            }
                        } catch {
                            // Launch preparation is optional. Preserve any
                            // usable entries and exercise the usual lazy path.
                            programFailure = String(describing: error)
                        }
                        programMilliseconds = (CACurrentMediaTime() - programBegan) * 1000
                    }
                    let programStatistics = HUDSourceMetalRenderer.programCacheStatisticsForVerification()
                    let elapsed = (CACurrentMediaTime() - began) * 1000
                    DispatchQueue.main.async {
                        self.sourcePreparationMilliseconds = elapsed
                        self.sourceProgramPreparationMilliseconds = programMilliseconds
                        self.sourceProgramPreparationStatistics = programStatistics
                        self.sourceProgramPreparationFailure = programFailure
                        self.measure("closed-before", seconds: 5) { self.open() }
                    }
                }
                return
            }
            #endif
            measure("closed-before", seconds: 5) { self.open() }
        }

        private func open() {
            overlay.initialModuleRequest = .map
            let began = CACurrentMediaTime()
            _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration)
            firstOpenMilliseconds = (CACurrentMediaTime() - began) * 1000
            isolatePointerInput()
            later(2) {
                self.isolatePointerInput()
                #if HUD_SOURCE_INTEGRATION
                if let renderer = self.overlay.systemSourceWatchForVerification?.renderer {
                    self.firstCompletedMilliseconds = renderer.firstCompletedFrameTimestampForVerification.map { ($0 - began) * 1000 }
                    self.firstPresentedMilliseconds = renderer.firstPresentedFrameTimestampForVerification.map { ($0 - began) * 1000 }
                }
                // Forced rebuilds upload dozens of synthetic poses and leave
                // temporary objects/resources for the run loop to release.
                // Keep that correctness workload out of runtime measurements.
                if CommandLine.arguments.contains("--verify-only") { self.verifyFrameCache() }
                #endif
                if CommandLine.arguments.contains("--verify-only") { self.finish(); return }
                if CommandLine.arguments.contains("--power-modes") { self.measurePowerModes(); return }
                self.measure("map-idle", seconds: 6) {
                    self.section(.clipboard, name: "clipboard-idle") {
                        self.section(.notes, name: "notes-idle") {
                            self.section(.activityMonitor, name: "activity-idle") { self.motion() }
                        }
                    }
                }
            }
        }

        #if HUD_SOURCE_INTEGRATION
        private func verifyFrameCache() {
            guard let source = overlay.systemSourceWatchForVerification else {
                fatalError("Integrated source shell missing: \(overlay.systemSourceFailureForVerification ?? "unknown")")
            }
            do {
                func require(_ value: Bool, _ message: String = "Packet shape/alpha/diagnostics differs") {
                    guard value else {
                        fputs("Source cache verification failed: " + message + "\n", stderr)
                        fatalError(message)
                    }
                }
                let size = SIMD2<Double>(Double(source.bounds.width), Double(source.bounds.height))
                let builder = source.frameBuilder
                let before = builder.cachedLayoutFrameCount
                let ambientBefore = builder.fastAmbientFrameCount
                let directBefore = builder.directAmbientFrameCount
                for index in 0...42 {
                    if index >= 37, (index - 37) % 2 == 0, let root = source.buttonAnimation.instanceIDs.first {
                        let state: HUDSourceWatchButtonAnimation.State = [.highlighted, .pressed, .normal][(index - 37) / 2]
                        source.buttonAnimation.setState(state, on: root, at: 0, reduceMotion: true)
                    }
                    let angle = index <= 12 ? SIMD3<Double>.zero : SIMD3<Double>(sin(Double(index)) * 8, cos(Double(index)) * 6, 0)
                    let camera = try source.cameraModel.frame(screenSize: size,
                        localRotation: HUDSourceWatchCamera.quaternion(eulerDegrees: angle))
                    var pose = try source.document.animation.pose(entranceTime: source.document.animation.entrance.lastKeyTime,
                        ambientTime: Double(index) * 0.3, exitTime: nil, canvasResolution: camera.layout.canvasSize)
                    source.playback.desktopAmbientMotion?.apply(at: Double(index) * 0.3, to: &pose)
                    var buttonsWarm = pose, buttonsCold = pose
                    source.applyDesktopButtons(to: &buttonsWarm, at: 0, reduceMotion: false)
                    source.applyDesktopButtons(to: &buttonsCold, at: 0, reduceMotion: false, forceRebuild: true)
                    require(buttonsWarm.transforms == buttonsCold.transforms && buttonsWarm.properties == buttonsCold.properties
                        && buttonsWarm.unboundPaths == buttonsCold.unboundPaths,
                        "Settled button pose differs from authoritative sampled channels")
                    let warm = try builder.build(pose: buttonsWarm, worldRoot: camera.worldRoot)
                    let warmGeometry = try source.renderer.geometryFingerprintForVerification(meshNames: Set(warm.batches.map(\.mesh)))
                    let cold = try builder.build(pose: buttonsCold, worldRoot: camera.worldRoot, forceRebuild: true)
                    let coldGeometry = try source.renderer.geometryFingerprintForVerification(meshNames: Set(cold.batches.map(\.mesh)))
                    require(warmGeometry == coldGeometry, "Cached ambient GPU vertex/index bytes differ from full rebuild at sample \(index): \(warmGeometry.keys.filter { warmGeometry[$0] != coldGeometry[$0] }.sorted())")
                    require(warm.batches.count == cold.batches.count && warm.hits.count == cold.hits.count)
                    for (a, b) in zip(warm.batches, cold.batches) {
                        require(a.mesh == b.mesh && a.material == b.material && a.world == b.world
                            && a.color == b.color && a.appliesDesktopAccent == b.appliesDesktopAccent
                            && a.uniformOverrides == b.uniformOverrides
                            && a.textureOverrides == b.textureOverrides && a.indexRange == b.indexRange,
                            "Cached source batch differs from authoritative rebuild")
                    }
                    for (id, node) in warm.resolved {
                        require(cold.resolved[id].map { $0.worldMatrix == node.worldMatrix } == true,
                                     "Cached layout changes source transforms")
                    }
                    for (a, b) in zip(warm.hits, cold.hits) {
                        require(a.graphicID == b.graphicID && a.buttonID == b.buttonID && a.world == b.world
                            && a.rect.origin == b.rect.origin && a.rect.size == b.rect.size,
                            "Cached hit geometry differs from the displayed source")
                    }
                    // Advance to a different authored time without constructing
                    // a complete wrapper/button pose for the retained packet.
                    let nextTime = Double(index) * 0.3 + 0.137
                    var ambient = HUDSourceWatchPose(transforms: [:])
                    source.document.animation.apply(source.document.animation.ambient, time: nextTime, to: &ambient, base: nil)
                    source.playback.desktopAmbientMotion?.apply(at: nextTime, to: &ambient)
                    let revision = builder.presentationRevision
                    guard let direct = try builder.buildSettledAmbient(ambient, expectedRevision: revision,
                        worldRoot: camera.worldRoot, canvasResolution: camera.layout.canvasSize) else {
                        fatalError("Settled packet rejected an unchanged presentation")
                    }
                    let directGeometry = try source.renderer.geometryFingerprintForVerification(meshNames: Set(direct.batches.map(\.mesh)))
                    var nextPose = try source.document.animation.pose(entranceTime: source.document.animation.entrance.lastKeyTime,
                        ambientTime: nextTime, exitTime: nil, canvasResolution: camera.layout.canvasSize)
                    source.playback.desktopAmbientMotion?.apply(at: nextTime, to: &nextPose)
                    source.applyDesktopButtons(to: &nextPose, at: 0, reduceMotion: false, forceRebuild: true)
                    let nextCold = try builder.build(pose: nextPose, worldRoot: camera.worldRoot, forceRebuild: true)
                    let nextGeometry = try source.renderer.geometryFingerprintForVerification(meshNames: Set(nextCold.batches.map(\.mesh)))
                    require(directGeometry == nextGeometry, "Sparse packet GPU vertices/indices differ from authoritative rebuild")
                    require(direct.batches.count == nextCold.batches.count && direct.hits.count == nextCold.hits.count
                        && direct.inheritedAlpha == nextCold.inheritedAlpha && direct.diagnostics == nextCold.diagnostics)
                    for (a, b) in zip(direct.batches, nextCold.batches) {
                        require(a.mesh == b.mesh && a.material == b.material && a.world == b.world
                            && a.color == b.color && a.appliesDesktopAccent == b.appliesDesktopAccent
                            && a.uniformOverrides == b.uniformOverrides
                            && a.textureOverrides == b.textureOverrides && a.indexRange == b.indexRange
                            && a.stencilOverrides == b.stencilOverrides && a.colorWriteMask == b.colorWriteMask,
                            "Sparse packet changes authored draw order, state or uniforms")
                    }
                    let nextResolved = nextCold.resolved
                    for (id, node) in direct.resolved {
                        require(nextResolved[id].map { $0.localMatrix == node.localMatrix && $0.worldMatrix == node.worldMatrix
                            && $0.rect == node.rect && $0.activeInHierarchy == node.activeInHierarchy } == true,
                            "Sparse packet changes complete resolved geometry")
                    }
                    for (a, b) in zip(direct.hits, nextCold.hits) {
                        require(a.graphicID == b.graphicID && a.buttonID == b.buttonID && a.world == b.world && a.rect == b.rect
                            && a.masks.count == b.masks.count && zip(a.masks, b.masks).allSatisfy { $0.rect == $1.rect && $0.world == $1.world },
                            "Sparse packet changes clipped hit regions")
                    }
                    require(try builder.buildSettledAmbient(ambient, expectedRevision: revision,
                        worldRoot: camera.worldRoot, canvasResolution: camera.layout.canvasSize) == nil,
                        "Any intervening authoritative rebuild invalidates its former packet")
                    if index == 0 {
                        var token = builder.presentationRevision
                        func reseed() throws {
                            _ = try builder.build(pose: nextPose, worldRoot: camera.worldRoot, forceRebuild: true)
                            token = builder.presentationRevision
                        }
                        func rejected(world: simd_double4x4? = nil,
                                      canvas: SIMD2<Double>? = nil, scroll: Double = 1,
                                      tints: [HUDSourceID: SIMD4<Float>] = [:], sample: HUDSourceWatchPose? = nil,
                                      navigation: HUDSourceDesktopNavigationLayout? = nil) throws -> Bool {
                            try builder.buildSettledAmbient(sample ?? ambient, expectedRevision: token,
                                worldRoot: world ?? camera.worldRoot,
                                canvasResolution: canvas ?? camera.layout.canvasSize,
                                verticalNormalizedPosition: scroll, desktopNavigation: navigation, selectableTints: tints) == nil
                        }
                        require(try rejected(world: camera.worldRoot * HUDSourceGeometry.translation(SIMD3(1, 0, 0))), "Gyro/offset changes require a new packet")
                        require(try rejected(canvas: camera.layout.canvasSize + SIMD2(1, 0)), "Resize changes require a new packet")
                        require(try rejected(scroll: 0.5), "Scrolling requires a new packet")
                        require(try rejected(tints: [source.document.scene.rootID: .zero]), "Selectable tint changes require a new packet")
                        let navigation = try HUDSourceDesktopNavigationLayout(document: source.document, entryCount: 100)
                        require(try rejected(navigation: navigation), "Virtual navigation count changes require a new packet")
                        var invalid = ambient
                        invalid.properties[source.document.scene.rootID] = ["material._Alpha": 0.5]
                        require(try rejected(sample: invalid), "Future nonrotation channels cannot enter the sparse path")
                        if let id = ambient.transforms.keys.first {
                            invalid = ambient; invalid.transforms[id]?.active = false
                            require(try rejected(sample: invalid), "Transform visibility changes cannot enter the sparse path")
                        }
                        let oldHidden = builder.desktopHiddenNodes
                        builder.desktopHiddenNodes.insert(source.document.scene.rootID)
                        require(try rejected(), "Changing node visibility invalidates a packet")
                        builder.desktopHiddenNodes = oldHidden; try reseed()
                        let oldText = builder.desktopTextOverrides
                        builder.desktopTextOverrides[source.document.scene.rootID] = "Verification"
                        require(try rejected(), "Changing caption bindings invalidates a packet")
                        builder.desktopTextOverrides = oldText; try reseed()
                        source.renderer.configureDesktopAccent(NSColor(red: 0.13, green: 0.37, blue: 0.73, alpha: 1))
                        require(try rejected(), "Changing renderer appearance invalidates a packet")
                        source.renderer.configureDesktopAccent(HUDRuntimeAppearance.accent); try reseed()
                        try source.renderer.registerGeometry(named: "verification/packet-resource",
                            positions: [SIMD4(0, 0, 0, 1), SIMD4(1, 0, 0, 1), SIMD4(0, 1, 0, 1)],
                            uv: [SIMD2(0, 0), SIMD2(1, 0), SIMD2(0, 1)], indices: [0, 1, 2])
                        require(try rejected(), "External geometry replacement invalidates a packet")
                        let lifecycle = HUDSourceWatchPlayback(animation: source.document.animation)
                        lifecycle.open(at: 0, reduceMotion: false)
                        require(lifecycle.sampleAmbient(at: 0.1) == nil, "Opening cannot use a settled sample")
                        lifecycle.showStable(at: 1)
                        require(lifecycle.sampleAmbient(at: 1.2) != nil, "Visible state exposes the original loop clock")
                        lifecycle.close(at: 2, reduceMotion: false)
                        require(lifecycle.sampleAmbient(at: 2.1) == nil, "Closing cannot use a settled sample")
                    }
                }
                require(builder.cachedLayoutFrameCount > before, "Ambient samples must reuse their unchanged layout")
                require(builder.fastAmbientFrameCount > ambientBefore, "Settled ambient presentation must skip static traversal")
                require(builder.directAmbientFrameCount == directBefore + 43, "Every direct packet must pass the independent GPU geometry comparison")
                source.buttonAnimation.reset(at: 0, reduceMotion: true)
                // Compare a run of consecutive sparse pointer packets before
                // invoking the independent full oracle. This catches stale
                // slant buffers when returning to the seed root, accumulation
                // across pointer samples, and pointer-to-idle transitions.
                typealias PointerSnapshot = (frame: HUDSourceWatchFrameBuilder.Frame, geometry: [String: String],
                    pose: HUDSourceWatchPose, world: simd_double4x4, label: String)
                func completePose(at time: Double?, canvas: SIMD2<Double>) throws -> HUDSourceWatchPose {
                    var pose = try source.document.animation.pose(entranceTime: source.document.animation.entrance.lastKeyTime,
                        ambientTime: time, exitTime: nil, canvasResolution: canvas)
                    if let time { source.playback.desktopAmbientMotion?.apply(at: time, to: &pose) }
                    source.applyDesktopButtons(to: &pose, at: 0, reduceMotion: false, forceRebuild: true)
                    return pose
                }
                func samePointerFrame(_ expected: PointerSnapshot) throws {
                    let cold = try builder.build(pose: expected.pose, worldRoot: expected.world, forceRebuild: true)
                    let coldGeometry = try source.renderer.geometryFingerprintForVerification(meshNames: Set(cold.batches.map(\.mesh)))
                    let warm = expected.frame, label = expected.label
                    require(expected.geometry == coldGeometry, "Pointer packet GPU bytes differ: \(label)")
                    require(warm.inheritedAlpha == cold.inheritedAlpha && warm.diagnostics == cold.diagnostics
                        && warm.batches.count == cold.batches.count && warm.hits.count == cold.hits.count,
                        "Pointer packet shape/alpha/diagnostics differs: \(label)")
                    for (a, b) in zip(warm.batches, cold.batches) {
                        require(a.mesh == b.mesh && a.material == b.material && a.world == b.world
                            && a.color == b.color && a.appliesDesktopAccent == b.appliesDesktopAccent
                            && a.uniformOverrides == b.uniformOverrides && a.textureOverrides == b.textureOverrides
                            && a.indexRange == b.indexRange && a.stencilOverrides == b.stencilOverrides
                            && a.colorWriteMask == b.colorWriteMask && a.sourceNodeID == b.sourceNodeID,
                            "Pointer packet changes authored draw state: \(label)")
                    }
                    let reference = cold.resolved
                    require(warm.resolved.count == reference.count, "Pointer packet omits resolved nodes: \(label)")
                    for (id, node) in warm.resolved {
                        require(reference[id].map { $0.localMatrix == node.localMatrix && $0.worldMatrix == node.worldMatrix
                            && $0.rect == node.rect && $0.activeInHierarchy == node.activeInHierarchy } == true,
                            "Pointer packet changes resolved geometry: \(label), \(node.node.path)")
                    }
                    for (a, b) in zip(warm.hits, cold.hits) {
                        require(a.graphicID == b.graphicID && a.buttonID == b.buttonID && a.world == b.world && a.rect == b.rect
                            && a.masks.count == b.masks.count && zip(a.masks, b.masks).allSatisfy { $0.rect == $1.rect && $0.world == $1.world },
                            "Pointer packet changes clipped hit regions: \(label)")
                    }
                }
                for usesAmbient in [true, false] {
                    let seedCamera = try source.cameraModel.frame(screenSize: size,
                        localRotation: HUDSourceWatchCamera.quaternion(eulerDegrees: .zero))
                    let seedPose = try completePose(at: usesAmbient ? 1.127 : nil, canvas: seedCamera.layout.canvasSize)
                    _ = try builder.build(pose: seedPose, worldRoot: seedCamera.worldRoot, forceRebuild: true)
                    let pointerBefore = builder.directPointerFrameCount
                    var snapshots: [PointerSnapshot] = []
                    for (index, angle) in [SIMD3<Double>(7, -5, 0), SIMD3(-11, 9, 0), .zero, .zero].enumerated() {
                        let camera = try source.cameraModel.frame(screenSize: size,
                            localRotation: HUDSourceWatchCamera.quaternion(eulerDegrees: angle))
                        let sampleTime = 1.3 + Double(index) * 0.117
                        var ambient: HUDSourceWatchPose?
                        if usesAmbient {
                            var sample = HUDSourceWatchPose(transforms: [:])
                            source.document.animation.apply(source.document.animation.ambient, time: sampleTime, to: &sample, base: nil)
                            source.playback.desktopAmbientMotion?.apply(at: sampleTime, to: &sample)
                            ambient = sample
                        }
                        let revision = builder.presentationRevision
                        guard let frame = try builder.buildSettledMotion(ambient, expectedRevision: revision,
                            worldRoot: camera.worldRoot, canvasResolution: camera.layout.canvasSize) else {
                            fatalError("Settled pointer packet rejected unchanged dependencies at sample \(index), ambient=\(usesAmbient)")
                        }
                        require(builder.presentationRevision == revision + (index < 3 ? 1 : 0),
                            "Pointer motion must invalidate hit queries; idle samples keep the same hit revision")
                        if index < 3 {
                            require(try builder.buildSettledMotion(ambient, expectedRevision: revision,
                                worldRoot: camera.worldRoot, canvasResolution: camera.layout.canvasSize) == nil,
                                "Previous pointer revision cannot be reused")
                        }
                        snapshots.append((frame,
                            try source.renderer.geometryFingerprintForVerification(meshNames: Set(frame.batches.map(\.mesh))),
                            try completePose(at: usesAmbient ? sampleTime : nil, canvas: camera.layout.canvasSize),
                            camera.worldRoot, "sample \(index), ambient=\(usesAmbient)"))
                    }
                    require(builder.directPointerFrameCount == pointerBefore + 3,
                        "Every moving sample must activate the sparse pointer path; stationary samples reuse it")
                    if usesAmbient {
                        let offPose = try completePose(at: nil, canvas: seedCamera.layout.canvasSize)
                        let previousRevision = builder.presentationRevision
                        _ = try builder.build(pose: offPose, worldRoot: seedCamera.worldRoot)
                        require(try builder.buildSettledMotion(nil, expectedRevision: previousRevision,
                            worldRoot: seedCamera.worldRoot, canvasResolution: seedCamera.layout.canvasSize) == nil,
                            "Turning ambient off requires a newly seeded full presentation")
                        let offCamera = try source.cameraModel.frame(screenSize: size,
                            localRotation: HUDSourceWatchCamera.quaternion(eulerDegrees: SIMD3(3, 4, 0)))
                        guard let offFrame = try builder.buildSettledMotion(nil, expectedRevision: builder.presentationRevision,
                            worldRoot: offCamera.worldRoot, canvasResolution: offCamera.layout.canvasSize) else {
                            fatalError("Ambient-off pointer packet failed after an animated presentation")
                        }
                        snapshots.append((offFrame,
                            try source.renderer.geometryFingerprintForVerification(meshNames: Set(offFrame.batches.map(\.mesh))),
                            offPose, offCamera.worldRoot, "ambient-to-off reseed"))
                    }
                    // A normal build after sparse motion must restore whatever
                    // local vertices its per-node cache held before movement.
                    let retainedImages = builder.reusedImagePresentationCount
                    let restored = try builder.build(pose: seedPose, worldRoot: seedCamera.worldRoot)
                    require(builder.reusedImagePresentationCount > retainedImages,
                        "Restoring sparse motion must keep unrelated exact image presentations reusable")
                    snapshots.append((restored,
                        try source.renderer.geometryFingerprintForVerification(meshNames: Set(restored.batches.map(\.mesh))),
                        seedPose, seedCamera.worldRoot, "full-builder restoration, ambient=\(usesAmbient)"))
                    for snapshot in snapshots { try samePointerFrame(snapshot) }
                    let token = builder.presentationRevision
                    func rejectsPointer(canvas: SIMD2<Double>? = nil, scroll: Double = 1,
                                        tints: [HUDSourceID: SIMD4<Float>] = [:],
                                        sample: HUDSourceWatchPose? = nil) throws -> Bool {
                        try builder.buildSettledMotion(sample, expectedRevision: token,
                            worldRoot: seedCamera.worldRoot, canvasResolution: canvas ?? seedCamera.layout.canvasSize,
                            verticalNormalizedPosition: scroll, selectableTints: tints) == nil
                    }
                    require(try rejectsPointer(canvas: seedCamera.layout.canvasSize + SIMD2(1, 0)), "Pointer packet cannot ignore resizing")
                    require(try rejectsPointer(scroll: 0.5), "Pointer packet cannot ignore scrolling")
                    require(try rejectsPointer(tints: [source.document.scene.rootID: .zero]), "Pointer packet cannot ignore selectable changes")
                    var invalid = HUDSourceWatchPose(transforms: [:])
                    invalid.properties[source.document.scene.rootID] = ["m_Alpha": 0.3]
                    require(try rejectsPointer(sample: invalid), "Pointer packet cannot accept dynamic opacity channels")
                    let oldHidden = builder.desktopHiddenNodes
                    builder.desktopHiddenNodes.insert(source.document.scene.rootID)
                    require(try rejectsPointer(), "Pointer packet cannot ignore visibility mutations")
                    builder.desktopHiddenNodes = oldHidden
                }
                // Gyro movement rebuilds world/slant presentation, but not the
                // products of unchanged CanvasGroup channels. Compare each
                // cache branch with the original forced traversal, including
                // groups that currently draw nothing.
                let neutralCamera = try source.cameraModel.frame(screenSize: size,
                    localRotation: HUDSourceWatchCamera.quaternion(eulerDegrees: .zero))
                let tiltedCamera = try source.cameraModel.frame(screenSize: size,
                    localRotation: HUDSourceWatchCamera.quaternion(eulerDegrees: SIMD3(7, -5, 0)))
                var alphaPose = try source.document.animation.pose(
                    entranceTime: source.document.animation.entrance.lastKeyTime,
                    ambientTime: 0.413, exitTime: nil, canvasResolution: neutralCamera.layout.canvasSize)
                source.playback.desktopAmbientMotion?.apply(at: 0.413, to: &alphaPose)
                source.applyDesktopButtons(to: &alphaPose, at: 0, reduceMotion: false, forceRebuild: true)
                let alphaSeed = try builder.build(pose: alphaPose, worldRoot: neutralCamera.worldRoot, forceRebuild: true)
                func compareAlphaAndClip(_ pose: HUDSourceWatchPose, world: simd_double4x4,
                                         reusesAlpha: Bool, checksClipReuse: Bool = false) throws -> HUDSourceWatchFrameBuilder.Frame {
                    let alphaBefore = builder.reusedInheritedAlphaCount
                    let clipBefore = builder.reusedClipRectCount
                    let imageBefore = builder.reusedImagePresentationCount
                    let warm = try builder.build(pose: pose, worldRoot: world)
                    require(builder.reusedInheritedAlphaCount == alphaBefore + (reusesAlpha ? 1 : 0),
                        "CanvasGroup cache must use exact scalar channels, independent of gyro or active state")
                    if checksClipReuse {
                        require(builder.reusedClipRectCount > clipBefore, "Shared Canvas/mask paths must reuse clip rectangles")
                        require(builder.reusedImagePresentationCount > imageBefore,
                            "Pointer-only movement must retain unchanged Canvas-local image presentation")
                    }
                    let warmGeometry = try source.renderer.geometryFingerprintForVerification(meshNames: Set(warm.batches.map(\.mesh)))
                    let alphaBeforeCold = builder.reusedInheritedAlphaCount
                    let clipBeforeCold = builder.reusedClipRectCount
                    let imageBeforeCold = builder.reusedImagePresentationCount
                    let cold = try builder.build(pose: pose, worldRoot: world, forceRebuild: true)
                    require(builder.reusedInheritedAlphaCount == alphaBeforeCold && builder.reusedClipRectCount == clipBeforeCold
                        && builder.reusedImagePresentationCount == imageBeforeCold,
                        "Forced oracle must bypass inherited-alpha, clip and image presentation caches")
                    let coldGeometry = try source.renderer.geometryFingerprintForVerification(meshNames: Set(cold.batches.map(\.mesh)))
                    require(warmGeometry == coldGeometry && warm.inheritedAlpha == cold.inheritedAlpha
                        && warm.diagnostics == cold.diagnostics && warm.batches.count == cold.batches.count
                        && warm.hits.count == cold.hits.count, "Alpha/clip cache changes rendered geometry or inherited opacity")
                    for (a, b) in zip(warm.batches, cold.batches) {
                        require(a.mesh == b.mesh && a.material == b.material && a.world == b.world
                            && a.color == b.color && a.appliesDesktopAccent == b.appliesDesktopAccent
                            && a.uniformOverrides == b.uniformOverrides && a.textureOverrides == b.textureOverrides
                            && a.indexRange == b.indexRange && a.stencilOverrides == b.stencilOverrides
                            && a.colorWriteMask == b.colorWriteMask,
                            "Cached clip bounds or alpha changes authored draw state")
                    }
                    for (a, b) in zip(warm.hits, cold.hits) {
                        require(a.graphicID == b.graphicID && a.buttonID == b.buttonID && a.world == b.world && a.rect == b.rect
                            && a.masks.count == b.masks.count && zip(a.masks, b.masks).allSatisfy { $0.rect == $1.rect && $0.world == $1.world },
                            "Cached clip paths change hit testing")
                    }
                    return warm
                }
                let gyro = try compareAlphaAndClip(alphaPose, world: tiltedCamera.worldRoot, reusesAlpha: true, checksClipReuse: true)
                require(gyro.inheritedAlpha == alphaSeed.inheritedAlpha, "Pointer motion changes no CanvasGroup opacity")
                guard let alphaNode = source.document.scene.traversalIDs.first(where: { id in
                    alphaSeed.node(id)?.activeInHierarchy == true && (alphaSeed.inheritedAlpha[id] ?? 0) > 0
                        && builder.desktopProperties[id]?["m_Alpha"] == nil
                        && (source.document.components[id] ?? []).contains { $0.kind == "CanvasGroup" && $0.enabled }
                }) else { fatalError("Verification requires a visible editable CanvasGroup") }
                alphaPose.properties[alphaNode, default: [:]]["m_Alpha"] = 0.271
                let activeAlpha = try compareAlphaAndClip(alphaPose, world: tiltedCamera.worldRoot, reusesAlpha: false)
                require(activeAlpha.inheritedAlpha != gyro.inheritedAlpha, "Changed visible CanvasGroup must change inherited opacity")
                var inactive = alphaPose.transforms[alphaNode] ?? HUDSourceTransformOverride()
                inactive.active = false; alphaPose.transforms[alphaNode] = inactive
                let hiddenAlpha = try compareAlphaAndClip(alphaPose, world: tiltedCamera.worldRoot, reusesAlpha: true)
                require(hiddenAlpha.node(alphaNode)?.activeInHierarchy == false
                    && hiddenAlpha.inheritedAlpha == activeAlpha.inheritedAlpha,
                    "Inactive groups still participate in the original alpha inheritance")
                alphaPose.properties[alphaNode, default: [:]]["m_Alpha"] = 0.683
                let changedHiddenAlpha = try compareAlphaAndClip(alphaPose, world: tiltedCamera.worldRoot, reusesAlpha: false)
                require(changedHiddenAlpha.node(alphaNode)?.activeInHierarchy == false
                    && changedHiddenAlpha.inheritedAlpha != hiddenAlpha.inheritedAlpha,
                    "Changing an inactive CanvasGroup must invalidate the inherited-alpha cache")
                print("PASS: cached and rebuilt source frames preserve batches, transforms, and hit geometry")
            } catch { fatalError("Source cache verification: \(error)") }
        }
        #endif

        private func section(_ module: HUDModule, name: String, completion: @escaping () -> Void) {
            overlay.selectSystemModule(module)
            later(1) { self.measure(name, seconds: 6, completion: completion) }
        }

        private func motion() {
            overlay.selectSystemModule(.clipboard, animated: false)
            let screen = NSScreen.main!.frame, began = CACurrentMediaTime()
            pointerTimer = Timer(timeInterval: 1 / 60, repeats: true) { [weak self] _ in
                guard let self else { return }
                let t = CACurrentMediaTime() - began
                let normalized = CGPoint(x: sin(t * 1.7) * 0.7, y: cos(t * 1.3) * 0.65)
                self.pointer = CGPoint(x: screen.midX + normalized.x * screen.width / 2,
                                       y: screen.midY + normalized.y * screen.height / 2)
                #if !HUD_SOURCE_INTEGRATION
                // Native stable HUD follows discrete pointer events. The
                // integrated source owns a 60 Hz display clock and reads the
                // injected provider itself, just as in production; forcing a
                // second render here would accidentally benchmark 120 Hz.
                self.overlay.setSystemPointerForVerification(normalized)
                #endif
            }
            RunLoop.main.add(pointerTimer!, forMode: .common)
            measure("pointer-motion", seconds: 8) {
                self.pointerTimer?.invalidate(); self.pointerTimer = nil
                self.overlay.closeSystemOverlay()
                self.later(1) {
                    precondition(self.overlay.systemPhase == .closed && self.overlay.lastClosedAnimationCount == 0,
                                 "Closing must remove hidden presentation and animations")
                    self.measureClosedAfter()
                }
            }
        }

        private func measurePowerModes() {
            #if HUD_SOURCE_INTEGRATION
            let modes: [(String, Bool, Bool)] = [
                ("normal", false, false), ("low-power", true, false),
                ("reduce-motion", false, true), ("both", true, true)
            ]
            func run(_ index: Int) {
                guard index < modes.count else {
                    self.overlay.closeSystemOverlay()
                    self.later(1) { self.measure("modes-closed", seconds: 5) { self.finish() } }
                    return
                }
                let (name, lowPower, reduce) = modes[index]
                self.configuration.lowPowerVisualMode = lowPower
                self.configuration.reduceMotion = reduce
                self.overlay.update(snapshot: .unavailable, configuration: self.configuration)
                self.later(2) {
                    self.measure(name + "-idle", seconds: 5) {
                        let began = CACurrentMediaTime(), screen = NSScreen.main!.frame
                        self.pointerTimer = Timer(timeInterval: 1 / 60, repeats: true) { [weak self] _ in
                            guard let self, let source = self.overlay.systemSourceWatchForVerification,
                                  let window = source.window else { return }
                            let t = CACurrentMediaTime() - began
                            self.pointer = CGPoint(x: screen.midX + sin(t * 1.7) * screen.width * 0.35,
                                                   y: screen.midY + cos(t * 1.3) * screen.height * 0.325)
                            // Directly dispatch to this isolated view. Merely
                            // changing the provider would never wake a paused
                            // power-saving renderer and would hide event costs.
                            if let event = NSEvent.mouseEvent(with: .mouseMoved,
                                location: window.convertPoint(fromScreen: self.pointer), modifierFlags: [],
                                timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                                context: nil, eventNumber: 0, clickCount: 0, pressure: 0) {
                                source.mouseMoved(with: event)
                                (source.superview as? SystemHUDView)?.mouseMoved(with: event)
                            }
                        }
                        RunLoop.main.add(self.pointerTimer!, forMode: .common)
                        self.measure(name + "-pointer-events", seconds: 5) {
                            self.pointerTimer?.invalidate(); self.pointerTimer = nil
                            run(index + 1)
                        }
                    }
                }
            }
            run(0)
            #else
            fatalError("Power-mode comparison requires the integrated renderer")
            #endif
        }

        private func measureClosedAfter() {
            guard CommandLine.arguments.contains("--relieve-closed-heap") else {
                measure("closed-after", seconds: 8) { self.reopen() }
                return
            }
            // Opt-in experiment only. Reclaim already-free allocator pages;
            // no live metadata, shader programs or decoded images are evicted.
            // Keep the scan off the UI thread and outside the measured idle
            // window, then measure the ordinary warm reopen with caches intact.
            DispatchQueue.global(qos: .utility).async {
                let before = self.usage(), began = CACurrentMediaTime()
                let released = malloc_zone_pressure_relief(nil, 0)
                let milliseconds = (CACurrentMediaTime() - began) * 1000
                let after = self.usage()
                DispatchQueue.main.async {
                    self.closedHeapRelief = ["releasedAllocatorBytes": released,
                        "milliseconds": milliseconds,
                        "beforeFootprintBytes": before.footprint, "afterFootprintBytes": after.footprint,
                        "beforeResidentBytes": before.resident, "afterResidentBytes": after.resident]
                    print("Closed heap relief: \(self.closedHeapRelief!)"); fflush(stdout)
                    self.measure("closed-after", seconds: 8) { self.reopen() }
                }
            }
        }

        private func reopen() {
            overlay.initialModuleRequest = .map
            let began = CACurrentMediaTime()
            _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration)
            warmOpenMilliseconds = (CACurrentMediaTime() - began) * 1000
            isolatePointerInput()
            later(2) {
                self.isolatePointerInput()
                self.measure("reopened-map-idle", seconds: 6) {
                    self.overlay.closeSystemOverlay()
                    self.later(1) { self.finish() }
                }
            }
        }

        private func isolatePointerInput() {
            // Real tracking events must not alter hover, scroll or module state
            // while this fixture supplies its own repeatable pointer stream.
            for panel in NSApp.windows.compactMap({ $0 as? NSPanel }) {
                // The local event monitor already rejects all physical input.
                // Mode benchmarks also dispatch to the native host's real
                // hover path, which intentionally ignores disabled panels.
                panel.ignoresMouseEvents = !CommandLine.arguments.contains("--power-modes")
                panel.acceptsMouseMovedEvents = false
            }
            if let screen = NSScreen.main?.frame {
                overlay.setSystemPointerForVerification(CGPoint(
                    x: (pointer.x - screen.midX) * 2 / screen.width,
                    y: (pointer.y - screen.midY) * 2 / screen.height))
            }
        }

        private func measure(_ name: String, seconds: Double, completion: @escaping () -> Void) {
            isolatePointerInput()
            let began = CACurrentMediaTime(), start = usage()
            #if HUD_SOURCE_INTEGRATION
            let source = overlay.systemSourceWatchForVerification
            let frames = source?.renderedFrameCount ?? 0
            let buildTime = source?.cumulativeFrameBuildSeconds ?? 0
            let cached = source?.frameBuilder.cachedLayoutFrameCount ?? 0
            let rebuilt = source?.frameBuilder.rebuiltLayoutFrameCount ?? 0
            let pointerFrames = source?.frameBuilder.directPointerFrameCount ?? 0
            #endif
            print("MEASURE \(name)"); fflush(stdout)
            later(seconds) {
                let end = self.usage(), elapsed = CACurrentMediaTime() - began
                var row: [String: Any] = ["scenario": name, "seconds": elapsed,
                    "cpuPercentOfOneCore": (end.cpu - start.cpu) / elapsed * 100,
                    "residentMiB": Double(end.resident) / 1048576,
                    "footprintMiB": Double(end.footprint) / 1048576,
                    "animations": self.overlay.systemAnimationCount]
                row["ignoredPhysicalMouseEventsTotal"] = self.ignoredPhysicalMouseEvents
                row["fixturePanelsIgnoreMouse"] = NSApp.windows.compactMap { $0 as? NSPanel }.allSatisfy { $0.ignoresMouseEvents }
                #if HUD_SOURCE_INTEGRATION
                row["sourceFramesPerSecond"] = Double((source?.renderedFrameCount ?? 0) - frames) / elapsed
                row["sourceTimerActive"] = source?.hasDisplayTimerForVerification ?? false
                row["sourceFailure"] = self.overlay.systemSourceFailureForVerification ?? ""
                row["sourceBuildMillisecondsPerSecond"] = ((source?.cumulativeFrameBuildSeconds ?? 0) - buildTime) * 1000 / elapsed
                row["cachedLayoutFrames"] = (source?.frameBuilder.cachedLayoutFrameCount ?? 0) - cached
                row["rebuiltLayoutFrames"] = (source?.frameBuilder.rebuiltLayoutFrameCount ?? 0) - rebuilt
                row["fastAmbientFramesTotal"] = source?.frameBuilder.fastAmbientFrameCount ?? 0
                row["directAmbientFramesTotal"] = source?.frameBuilder.directAmbientFrameCount ?? 0
                row["directPointerFramesTotal"] = source?.frameBuilder.directPointerFrameCount ?? 0
                row["directPointerFrames"] = (source?.frameBuilder.directPointerFrameCount ?? 0) - pointerFrames
                row["gpuResources"] = source?.renderer.resourceStatisticsForVerification ?? [:]
                if CommandLine.arguments.contains("--verify-power-modes") {
                    let rendered = (source?.renderedFrameCount ?? 0) - frames
                    precondition(self.overlay.systemSourceFailureForVerification == nil,
                                 "Power modes must keep the source renderer available")
                    if name != "closed-before" && name != "modes-closed" {
                        precondition(source?.window != nil && source?.isHidden == false,
                                     "Power-mode checks require a visible source view")
                    }
                    if ["normal-pointer-events", "low-power-pointer-events"].contains(name) {
                        let minimumFPS = name == "normal-pointer-events" ? 50.0 : 25.0
                        precondition(Double(rendered) / elapsed >= minimumFPS
                            && (source?.frameBuilder.directPointerFrameCount ?? 0) > pointerFrames,
                            "Moving workloads must render and use the optimized pointer path")
                    }
                    if ["low-power-idle", "reduce-motion-idle", "both-idle", "modes-closed"].contains(name) {
                        precondition(rendered == 0 && source?.hasDisplayTimerForVerification != true,
                                     "A settled power-saving view must not keep rendering")
                    }
                    if name == "low-power-pointer-events" {
                        precondition(Double(rendered) / elapsed <= 32,
                                     "Pointer events must respect the low-power frame budget")
                    }
                    if ["reduce-motion-pointer-events", "both-pointer-events"].contains(name) {
                        precondition(Double(rendered) / elapsed < 20 && source?.hasDisplayTimerForVerification != true,
                                     "Reduced motion must redraw changed controls instead of following the pointer continuously")
                    }
                }
                #endif
                self.rows.append(row)
                print(row); fflush(stdout)
                completion()
            }
        }

        private func usage() -> (cpu: Double, resident: UInt64, footprint: UInt64) {
            var cpu = rusage(); getrusage(RUSAGE_SELF, &cpu)
            var memory = rusage_info_v4()
            let result = withUnsafeMutablePointer(to: &memory) {
                $0.withMemoryRebound(to: rusage_info_t?.self, capacity: 1) { proc_pid_rusage(getpid(), RUSAGE_INFO_V4, $0) }
            }
            precondition(result == 0)
            return (Double(cpu.ru_utime.tv_sec + cpu.ru_stime.tv_sec)
                + Double(cpu.ru_utime.tv_usec + cpu.ru_stime.tv_usec) / 1_000_000,
                memory.ri_resident_size, memory.ri_phys_footprint)
        }

        private func later(_ seconds: Double, _ block: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + seconds, execute: block)
        }

        private func finish() {
            var report: [String: Any] = ["firstOpenSynchronousMilliseconds": firstOpenMilliseconds,
                "reduceMotion": HUDRuntimeAppearance.reduceMotion, "ambient": HUDRuntimeAppearance.ambientEnabled,
                "screenPoints": [NSScreen.main!.frame.width, NSScreen.main!.frame.height],
                "backingScale": NSScreen.main!.backingScaleFactor,
                "metrics": rows,
                "limitations": "Visible fixture workload; source submission cadence is not measured display FPS. CPU excludes WindowServer. No Instruments installed on this host."]
            report["firstCompletedSourceFrameMilliseconds"] = firstCompletedMilliseconds
            report["firstPresentedSourceFrameMilliseconds"] = firstPresentedMilliseconds
            report["startupSourcePreparationMilliseconds"] = sourcePreparationMilliseconds
            report["warmOpenSynchronousMilliseconds"] = warmOpenMilliseconds
            report["closedHeapRelief"] = closedHeapRelief
            if CommandLine.arguments.contains("--power-modes") {
                report["pointerEventScope"] = "source and native host; isolated direct dispatch at 60 Hz"
            }
            #if HUD_SOURCE_INTEGRATION
            report["automaticClosedHeapCleanupRuns"] = overlay.closedHeapCleanupRunsForVerification
            report["automaticClosedHeapCleanupMilliseconds"] = overlay.lastClosedHeapCleanupMillisecondsForVerification
            report["sourceProgramPrewarmEnabled"] = !CommandLine.arguments.contains("--cold-source")
                && !CommandLine.arguments.contains("--skip-program-prewarm")
            report["sourceProgramPreparationMilliseconds"] = sourceProgramPreparationMilliseconds
            report["sourceProgramPreparationStatistics"] = sourceProgramPreparationStatistics
            report["sourceProgramPreparationFailure"] = sourceProgramPreparationFailure
            report["sourceUniformPath"] = CommandLine.arguments.contains("--ui-test")
                && CommandLine.arguments.contains("--legacy-source-uniforms") ? "legacy" : "prepared"
            report["sourceBatchPath"] = CommandLine.arguments.contains("--ui-test")
                && CommandLine.arguments.contains("--original-source-batches") ? "original" : "adjacent-merged"
            #endif
            do {
                try FileManager.default.createDirectory(at: output.deletingLastPathComponent(), withIntermediateDirectories: true)
                try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys]).write(to: output)
                print("Performance report: \(output.path)")
            } catch { fatalError("Could not save performance report: \(error)") }
            overlay.activity.shutdown(); overlay.clipboard.stop(); overlay.forceCloseSystemOverlay()
            if let physicalMouseMonitor { NSEvent.removeMonitor(physicalMouseMonitor) }
            physicalMouseMonitor = nil
            NSApp.terminate(nil)
        }
    }
}
