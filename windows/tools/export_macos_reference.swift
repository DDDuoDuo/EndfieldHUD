import AppKit
import CryptoKit
import simd

/// Isolated reference executable, compiled with the current macOS Sources.
/// No NSWindow, app controller, persistent store, clipboard, or device provider
/// is constructed. The actual desktop view owns all shell adaptation/overlays.
@main
enum MacOSReferenceExporter {
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
        if !condition() { throw HUDSourceError.invalid(message) }
    }
    static func matrix(_ value: simd_double4x4) -> [[Double]] {
        (0..<4).map { c in (0..<4).map { value[c][$0] } }
    }
    static func matrix(_ value: simd_float4x4) -> [[Float]] {
        (0..<4).map { c in (0..<4).map { value[c][$0] } }
    }
    static func point(_ value: CGPoint?) -> Any { value.map { [$0.x, $0.y] } ?? NSNull() as Any }
    static func rect(_ value: HUDSourceRect?) -> Any {
        value.map { ["origin": [$0.origin.x, $0.origin.y], "size": [$0.size.x, $0.size.y]] } ?? NSNull() as Any
    }
    static func hash(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }
    static func writeJSON(_ object: Any, to path: URL) throws {
        try JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])
            .write(to: path, options: .atomic)
    }

    static func frameJSON(_ frame: HUDSourceWatchFrameBuilder.Frame, camera: HUDSourceWatchCamera.Frame,
                          bounds: CGRect, view: HUDSourceWatchView, nativeHitQueries: Bool) throws -> [String: Any] {
        // The current Mac desktop adapter intentionally omits game bindings.
        // Preserve its unbound-curve diagnostics verbatim instead of claiming
        // complete animation coverage. Other frame errors remain fatal.
        try require(frame.diagnostics.allSatisfy { $0.hasPrefix("Unbound source curve: ") },
                    "Desktop frame diagnostics: \(frame.diagnostics)")
        let nodes: [[String: Any]] = frame.resolved.keys.sorted { $0.rawValue < $1.rawValue }.map { id in
            let node = frame.resolved[id]!
            return ["id": id.rawValue, "name": node.node.name, "path": node.node.path,
                    "parent": node.node.parentID?.rawValue as Any? ?? NSNull(),
                    "active": node.activeInHierarchy, "localMatrix": matrix(node.localMatrix),
                    "worldMatrix": matrix(node.worldMatrix), "rect": rect(node.rect),
                    "inheritedAlpha": frame.inheritedAlpha[id] as Any? ?? NSNull()]
        }
        let hits: [[String: Any]] = frame.hits.map { hit in
            let center = hit.rect.origin + hit.rect.size * 0.5
            let p = camera.camera.project(SIMD3(center.x, center.y, 0), world: hit.world, viewport: bounds)?.point
            let sampledButton = p.flatMap { frame.button(at: $0, camera: camera.camera, viewport: bounds) }
            let target = nativeHitQueries ? p.flatMap { view.navigationTarget(at: $0)?.identifier } : nil
            return ["graphicID": hit.graphicID.rawValue, "buttonID": hit.buttonID.rawValue,
                    "rect": rect(hit.rect), "worldMatrix": matrix(hit.world),
                    "projectedCorners": hit.rect.corners.map { point(camera.camera.project($0, world: hit.world, viewport: bounds)?.point) },
                    "center": point(p), "centerRaycastButtonID": sampledButton?.rawValue as Any? ?? NSNull(),
                    "desktopTargetAtCenter": target as Any? ?? NSNull(),
                    "masks": hit.masks.map { ["rect": rect($0.rect), "worldMatrix": matrix($0.world)] }]
        }
        let batches: [[String: Any]] = frame.batches.enumerated().map { index, batch in
            var row: [String: Any] = ["index": index, "sourceNodeID": batch.sourceNodeID as Any? ?? NSNull(),
                "mesh": batch.mesh, "material": batch.material, "worldMatrix": matrix(batch.world),
                "color": [batch.color.x, batch.color.y, batch.color.z, batch.color.w],
                "appliesDesktopAccent": batch.appliesDesktopAccent,
                "uniformOverrides": batch.uniformOverrides, "textureOverrides": batch.textureOverrides,
                "colorWriteMask": batch.colorWriteMask.map { Int($0) } as Any? ?? NSNull(),
                "indexRange": batch.indexRange.map { [$0.lowerBound, $0.upperBound] } as Any? ?? NSNull()]
            if let s = batch.stencilOverrides {
                row["stencil"] = ["reference": s.reference, "compare": s.compare, "pass": s.pass,
                    "fail": s.fail, "depthFail": s.depthFail, "readMask": s.readMask, "writeMask": s.writeMask] as [String: Any]
            }
            return row
        }
        var layout: [String: Any] = ["missingTextMetrics": frame.layoutReport.missingTextMetrics.map(\.rawValue).sorted(),
            "unverifiedCustomComponents": frame.layoutReport.unverifiedCustomComponents.sorted()]
        if let s = frame.layoutReport.scroll {
            layout["scroll"] = ["nodeID": s.nodeID.rawValue, "contentID": s.contentID.rawValue,
                "viewportID": s.viewportID.rawValue, "hiddenLength": s.hiddenLength,
                "sensitivity": s.sensitivity, "normalizedPosition": s.normalizedPosition] as [String: Any]
        }
        return ["viewport": [bounds.width, bounds.height], "camera": ["view": matrix(camera.camera.view),
            "projection": matrix(camera.camera.projection), "worldRoot": matrix(camera.worldRoot),
            "canvasSize": [camera.layout.canvasSize.x, camera.layout.canvasSize.y]],
            "nodes": nodes, "batches": batches, "hits": hits, "layout": layout,
            "diagnostics": frame.diagnostics, "nativeDesktopHitQueries": nativeHitQueries]
    }

    static func exportStable(_ view: HUDSourceWatchView, name: String, output: URL) throws -> [String: Any] {
        let source = try view.renderedImageForVerification()
        try require(view.window == nil && !view.hasDisplayTimerForVerification, "Reference must remain detached and timer-free")
        try require(view.sourceCursorSetCountForVerification == 0 && !view.backdropPreparingForVerification,
                    "Reference must not acquire a cursor or prepare a desktop capture")
        guard let frame = view.currentFrameForVerification, let camera = view.currentCameraForVerification else {
            throw HUDSourceError.invalid("Missing actual desktop frame")
        }
        // Same raster orientation and perspective-aware native label export
        // used by SystemHUDView.writePNG. The renderer supplies its black matte.
        let width = Int(view.bounds.width), height = Int(view.bounds.height)
        guard let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
            bytesPerRow: width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw HUDSourceError.invalid("Cannot allocate reference bitmap")
        }
        context.translateBy(x: 0, y: CGFloat(height)); context.scaleBy(x: 1, y: -1)
        context.saveGState(); context.scaleBy(x: 1, y: -1); context.translateBy(x: 0, y: -view.bounds.height)
        context.draw(source, in: view.bounds); context.restoreGState()
        view.renderDesktopLabels(in: context)
        guard let image = context.makeImage(), let bytes = context.data,
              let png = NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:]) else {
            throw HUDSourceError.invalid("Cannot encode reference PNG")
        }
        try png.write(to: output.appendingPathComponent(name + ".png"), options: .atomic)
        var result = try frameJSON(frame, camera: camera, bounds: view.bounds, view: view, nativeHitQueries: true)
        result["png"] = name + ".png"
        result["rendererDiagnostics"] = view.renderer.diagnostics
        result["rgbaSHA256"] = hash(Data(bytes: bytes, count: height * width * 4))
        result["nativeNavigation"] = view.desktopNavigationForVerification.map { entry -> [String: Any] in
            var row: [String: Any] = ["target": entry.target.identifier, "title": entry.title,
                "sourceName": entry.sourceName, "verifiedHitPoint": point(view.desktopPointForVerification(target: entry.target))]
            if let p = view.desktopPresentationForVerification(target: entry.target) {
                row["caption"] = p.caption; row["captionVisible"] = p.captionVisible
                row["fontSize"] = p.fontSize; row["captionSize"] = [p.captionSize.width, p.captionSize.height]
                row["wrapped"] = p.wrapped; row["vectorVisible"] = p.vectorVisible
            }
            return row
        }
        result["nativeProfileCaptions"] = view.desktopProfileCaptionsForVerification.sorted()
        result["nativeProfilePoint"] = point(view.desktopProfilePointForVerification)
        result["nativeQuitPoint"] = point(view.desktopQuitPointForVerification)
        result["accessibilityLabels"] = view.desktopAccessibilityLabelsForVerification.sorted()
        try writeJSON(result, to: output.appendingPathComponent(name + ".json"))
        return ["name": name, "frame": name + ".json", "png": name + ".png", "rgbaSHA256": result["rgbaSHA256"]!,
                "batchCount": frame.batches.count, "hitCount": frame.hits.count, "nodeCount": frame.resolved.count]
    }

    static func animationTrace(_ view: HUDSourceWatchView, output: URL, name: String) throws -> String {
        let camera = try view.cameraModel.frame(screenSize: SIMD2(Double(view.bounds.width), Double(view.bounds.height)))
        let entries = view.desktopNavigationForVerification
        let count = entries.indices.filter { $0 >= 4 && entries[$0].target.module?.group != .bottom }.count
        let navigation = try HUDSourceDesktopNavigationLayout(document: view.document, entryCount: count)
        let playback = HUDSourceWatchPlayback(animation: view.document.animation)
        playback.ambientMotionEnabled = false
        var samples: [[String: Any]] = []
        for phase in ["opening", "closing"] {
            let duration = phase == "opening" ? playback.animation.entrance.lastKeyTime : playback.animation.exit.lastKeyTime
            if phase == "opening" { playback.open(at: 0, reduceMotion: false) }
            else { playback.showStable(at: 0); playback.close(at: 0, reduceMotion: false) }
            for fraction in [0.0, 0.25, 0.5, 0.75, 1.0] {
                let time = fraction * duration
                var row: [String: Any] = ["phase": phase, "elapsedSeconds": time,
                    "clipTime": HUDSourceWatchPlayback.clipTime(elapsed: time, length: duration),
                    "durationSeconds": duration]
                if var pose = try playback.sample(at: time, canvasResolution: camera.layout.canvasSize, reduceMotion: false) {
                    // Preserve the real desktop button bindings and selection;
                    // only finite wrapper time advances in this fixture.
                    view.applyDesktopButtons(to: &pose, at: 0, reduceMotion: true, forceRebuild: true)
                    let frame = try view.frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                        verticalNormalizedPosition: 1, desktopNavigation: navigation,
                        selectableTints: view.selectableColor.colors(at: 0, reduceMotion: true), forceRebuild: true)
                    row["frame"] = try frameJSON(frame, camera: camera, bounds: view.bounds, view: view, nativeHitQueries: false)
                } else { row["concealed"] = true }
                samples.append(row)
            }
        }
        let path = name + "-animation.json"
        try writeJSON(["scope": "desktop wrapper geometry; native overlay animation and central canvases are not sampled",
            "sampler": "HUDSourceWatchPlayback + actual desktop view's applyDesktopButtons/frameBuilder",
            "ambientEnabled": false, "nativeOverlayPixels": false, "samples": samples], to: output.appendingPathComponent(path))
        return path
    }

    static func run() throws {
        var output: URL?, sizes: [CGSize] = []
        var args = Array(CommandLine.arguments.dropFirst())
        while !args.isEmpty {
            let key = args.removeFirst()
            if key == "--ui-test" || key == "--render-macos-reference" { continue }
            guard !args.isEmpty else { throw HUDSourceError.invalid("Missing value for \(key)") }
            let value = args.removeFirst()
            switch key {
            case "--output": output = URL(fileURLWithPath: value, isDirectory: true)
            case "--size":
                let pair = value.split(separator: "x").compactMap { Int($0) }
                try require(pair.count == 2 && pair.allSatisfy { (320...4096).contains($0) }, "Invalid size: \(value)")
                sizes.append(CGSize(width: pair[0], height: pair[1]))
            default: throw HUDSourceError.invalid("Unknown argument: \(key)")
            }
        }
        guard let output else { throw HUDSourceError.invalid("--output is required") }
        if sizes.isEmpty { sizes = [CGSize(width: 1280, height: 800), CGSize(width: 1920, height: 1080)] }
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        NSApplication.shared.setActivationPolicy(.prohibited)
        NSApp.appearance = NSAppearance(named: .darkAqua)
        L10n.language = .english
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark
        configuration.ambientAnimation = false; configuration.reduceMotion = true
        configuration.blurAmount = 0; configuration.hudScale = 1; configuration.hudOffsetX = 0; configuration.hudOffsetY = 0
        HUDRuntimeAppearance.configuration = configuration
        var profile = UserProfile(awakeningDate: Date(timeIntervalSince1970: 1_700_000_000), uid: "1000000000")
        profile.name = "Endministrator"; profile.tag = "0000"; profile.permissionLevel = 60
        profile.birthdayMonth = 1; profile.birthdayDay = 1
        var frames: [[String: Any]] = [], traces: [String] = []
        for size in sizes {
            try autoreleasepool {
                let view = try HUDSourceWatchView(frame: CGRect(origin: .zero, size: size), desktopMode: true,
                    desktopNavigationEntries: HUDDesktopWatchNavigation.entries(shortcuts: []))
                defer { view.conceal() }
                try require(view.document.widgets == nil, "Desktop reference unexpectedly includes game widgets")
                view.pointerLocationProvider = { CGPoint(x: size.width / 2, y: size.height / 2) }
                view.isDesktopPointerLocked = { true }
                view.setDesktopProfile(profile, avatar: nil, background: nil)
                view.selectedDesktopModule = .power; view.inputEnabled = true
                view.showStable(); view.layoutSubtreeIfNeeded(); view.layout(); CATransaction.flush()
                let name = "desktop-shell-\(Int(size.width))x\(Int(size.height))"
                frames.append(try exportStable(view, name: name + "-top", output: output))
                traces.append(try animationTrace(view, output: output, name: name))
                // Re-enter the real view before scrolling: source-model trace
                // sampling deliberately bypassed its retained rendered frame.
                view.refreshPointerForVerification()
                var scrollSteps = 0
                while view.scrollDesktopNavigation(1, animated: false) {
                    scrollSteps += 1
                    try require(scrollSteps <= 128, "Desktop navigation failed to reach its lower bound")
                }
                frames.append(try exportStable(view, name: name + "-bottom", output: output))
                view.conceal()
                try require(!view.hasDisplayTimerForVerification && view.playback.phase == .concealed,
                            "Reference lifecycle failed to release display scheduling")
                try require(view.sourceCursorSetCountForVerification == 0, "Reference changed the native cursor")
            }
        }
        let modules: [[String: Any]] = HUDModule.allCases.map { module in
            let r = module.contentFrame
            return ["id": module.rawValue, "title": module.title, "group": module.group.rawValue,
                    "contentFrameIn1000x640DesignSpace": [r.origin.x, r.origin.y, r.width, r.height]]
        }
        try writeJSON(["schemaVersion": 1, "desktopMode": true,
            "renderer": "current macOS HUDSourceWatchView + Metal + native desktop overlays",
            "scope": "desktop shell, navigation, compact profile, source wrapper geometry",
            "notVerified": ["SystemHUDView central module canvases", "unbound source animation curves retained in frame diagnostics",
                "native overlay transition pixels", "desktop backdrop capture/blur",
                "live providers", "Windows rendering", "cross-OS font raster equality", "ambient randomized motion"],
            "fixture": ["language": "english", "theme": "dark", "profileName": profile.name, "profileUID": profile.uid,
                "savedApps": 0, "ambientEnabled": false, "stablePoseReduceMotion": true,
                "backdrop": "renderer black matte; no desktop capture", "pixelScale": 1] as [String: Any],
            "isolation": ["windowCreated": false, "persistentStoresCreated": false, "userDefaultsAccessed": false,
                "clipboardAccessed": false, "nativeCursorSetCount": 0, "displayTimersAfterCleanup": 0],
            "coordinates": ["matrices": "column-major arrays; column vectors", "worldUnits": "original source units",
                "screen": "AppKit logical points, top-left origin, +Y down", "rgbaSHA256": "sRGB premultiplied RGBA8 bitmap bytes"],
            "modules": modules, "frames": frames, "animationTraces": traces], to: output.appendingPathComponent("reference.json"))
        print("Exported \(frames.count) actual desktop shell frames and \(traces.count) animation traces to \(output.path)")
    }
    static func main() {
        do { try run() }
        catch { fputs("macOS reference export failed: \(error)\n", stderr); exit(1) }
    }
}
