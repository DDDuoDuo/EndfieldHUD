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
        private var warmOpenMilliseconds: Double?
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
                    let elapsed = (CACurrentMediaTime() - began) * 1000
                    DispatchQueue.main.async {
                        self.sourcePreparationMilliseconds = elapsed
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
                func require(_ value: Bool, _ message: String) { precondition(value, message) }
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
                    precondition(buttonsWarm.transforms == buttonsCold.transforms && buttonsWarm.properties == buttonsCold.properties
                        && buttonsWarm.unboundPaths == buttonsCold.unboundPaths,
                        "Settled button pose differs from authoritative sampled channels")
                    let warm = try builder.build(pose: buttonsWarm, worldRoot: camera.worldRoot)
                    let warmGeometry = try source.renderer.geometryFingerprintForVerification(meshNames: Set(warm.batches.map(\.mesh)))
                    let cold = try builder.build(pose: buttonsCold, worldRoot: camera.worldRoot, forceRebuild: true)
                    let coldGeometry = try source.renderer.geometryFingerprintForVerification(meshNames: Set(cold.batches.map(\.mesh)))
                    precondition(warmGeometry == coldGeometry, "Cached ambient GPU vertex/index bytes differ from full rebuild")
                    precondition(warm.batches.count == cold.batches.count && warm.hits.count == cold.hits.count)
                    for (a, b) in zip(warm.batches, cold.batches) {
                        precondition(a.mesh == b.mesh && a.material == b.material && a.world == b.world
                            && a.color == b.color && a.appliesDesktopAccent == b.appliesDesktopAccent
                            && a.uniformOverrides == b.uniformOverrides
                            && a.textureOverrides == b.textureOverrides && a.indexRange == b.indexRange,
                            "Cached source batch differs from authoritative rebuild")
                    }
                    for (id, node) in warm.resolved {
                        precondition(cold.resolved[id].map { $0.worldMatrix == node.worldMatrix } == true,
                                     "Cached layout changes source transforms")
                    }
                    for (a, b) in zip(warm.hits, cold.hits) {
                        precondition(a.graphicID == b.graphicID && a.buttonID == b.buttonID && a.world == b.world
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
                    precondition(directGeometry == nextGeometry, "Sparse packet GPU vertices/indices differ from authoritative rebuild")
                    precondition(direct.batches.count == nextCold.batches.count && direct.hits.count == nextCold.hits.count
                        && direct.inheritedAlpha == nextCold.inheritedAlpha && direct.diagnostics == nextCold.diagnostics)
                    for (a, b) in zip(direct.batches, nextCold.batches) {
                        precondition(a.mesh == b.mesh && a.material == b.material && a.world == b.world
                            && a.color == b.color && a.appliesDesktopAccent == b.appliesDesktopAccent
                            && a.uniformOverrides == b.uniformOverrides
                            && a.textureOverrides == b.textureOverrides && a.indexRange == b.indexRange
                            && a.stencilOverrides == b.stencilOverrides && a.colorWriteMask == b.colorWriteMask,
                            "Sparse packet changes authored draw order, state or uniforms")
                    }
                    let nextResolved = nextCold.resolved
                    for (id, node) in direct.resolved {
                        precondition(nextResolved[id].map { $0.localMatrix == node.localMatrix && $0.worldMatrix == node.worldMatrix
                            && $0.rect == node.rect && $0.activeInHierarchy == node.activeInHierarchy } == true,
                            "Sparse packet changes complete resolved geometry")
                    }
                    for (a, b) in zip(direct.hits, nextCold.hits) {
                        precondition(a.graphicID == b.graphicID && a.buttonID == b.buttonID && a.world == b.world && a.rect == b.rect
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
                        precondition(lifecycle.sampleAmbient(at: 0.1) == nil, "Opening cannot use a settled sample")
                        lifecycle.showStable(at: 1)
                        precondition(lifecycle.sampleAmbient(at: 1.2) != nil, "Visible state exposes the original loop clock")
                        lifecycle.close(at: 2, reduceMotion: false)
                        precondition(lifecycle.sampleAmbient(at: 2.1) == nil, "Closing cannot use a settled sample")
                    }
                }
                precondition(builder.cachedLayoutFrameCount > before, "Ambient samples must reuse their unchanged layout")
                precondition(builder.fastAmbientFrameCount > ambientBefore, "Settled ambient presentation must skip static traversal")
                precondition(builder.directAmbientFrameCount == directBefore + 43, "Every direct packet must pass the independent GPU geometry comparison")
                source.buttonAnimation.reset(at: 0, reduceMotion: true)
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
                panel.ignoresMouseEvents = true
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
                row["gpuResources"] = source?.renderer.resourceStatisticsForVerification ?? [:]
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
            #if HUD_SOURCE_INTEGRATION
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
