import AppKit
import MetalKit
import ImageIO
import UniformTypeIdentifiers
import simd

/// Captures only the fixture's Metal drawable. No desktop screenshots,
/// account data or source recording is read on the CI host.
@main
enum RenderSourceWatchPreviews {
    static func main() throws {
        guard CommandLine.arguments.count == 2 else { throw HUDSourceError.invalid("Expected preview output directory") }
        let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let size = SIMD2<Double>(1728, 1080), viewport = CGRect(x: 0, y: 0, width: size.x, height: size.y)
        let document = try HUDSourceWatchDocument()
        let runtime = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: document.root.appendingPathComponent("runtime-root-camera.json")))
        let camera = try HUDSourceWatchCamera(runtimeRoot: runtime)
        let renderer = try HUDSourceMetalRenderer(frame: viewport)
        let builder = try HUDSourceWatchFrameBuilder(document: document, renderer: renderer)
        let buttons = try HUDSourceWatchButtonAnimation(document: document)
        let window = NSWindow(contentRect: viewport, styleMask: [.borderless], backing: .buffered, defer: false)
        window.isOpaque = false; window.backgroundColor = .clear; window.hasShadow = false
        window.contentView = renderer; window.orderFront(nil)
        renderer.drawableSize = CGSize(width: size.x, height: size.y)
        app.finishLaunching(); window.displayIfNeeded()
        let gyroEuler = try camera.gyro.targetEuler(mouseUnity: size / 2, screenSize: size)
        let gyro = try HUDSourceWatchCamera.quaternion(eulerDegrees: gyroEuler)
        let view = try camera.frame(screenSize: size, localRotation: gyro)
        let gpuY = HUDSourceGeometry.scale(SIMD3<Double>(1, -1, 1))
        let gpu = HUDSourceMetalRenderer.Camera(viewProjection: simd_float4x4(simd_mul(gpuY, view.camera.viewProjection)),
            worldSpacePosition: SIMD3(Float(camera.cameraWorld.columns.3.x), Float(camera.cameraWorld.columns.3.y), Float(camera.cameraWorld.columns.3.z)),
            timeSeconds: 0, renderPathInjected: 0, flipX: 0, flipY: 0,
            projection: simd_float4x4(simd_mul(gpuY, view.camera.projection)), inverseView: simd_float4x4(camera.cameraWorld))
        var manifest: [[String: Any]] = []
        func timestamp(_ value: Double?) -> Any {
            if let value { return value }
            return NSNull()
        }
        guard let battlepass = document.buttons.first(where: { $0.label?.literal == "通行证" }) else {
            throw HUDSourceError.invalid("Missing original Battle Pass button")
        }
        let samples: [(String, Double?, Double?, Double?, Double?)] = [
            ("opening-000", 0, nil, nil, nil), ("opening-100", 0.1, nil, nil, nil),
            ("opening-250", 0.25, nil, nil, nil), ("opening-500", 0.5, nil, nil, nil),
            ("stable", nil, 0, nil, nil), ("ambient-midpoint", nil, document.animation.ambient.lastKeyTime / 2, nil, nil),
            ("hover-000", nil, 0, nil, 0), ("hover-033", nil, 0, nil, 1 / 30),
            ("hover-067", nil, 0, nil, 2 / 30), ("hover-100", nil, 0, nil, 0.1),
            ("hover-hold", nil, 0, nil, 1), ("closing-100", nil, 0, 0.1, nil),
            ("closing-250", nil, 0, 0.25, nil)
        ]
        for (name, opening, ambient, closing, hover) in samples {
            buttons.reset(at: 0)
            if hover != nil { buttons.setHovered(true, on: battlepass.nodeID, at: 0, reduceMotion: false) }
            var pose = try document.animation.pose(entranceTime: opening.map {
                HUDSourceWatchPlayback.clipTime(elapsed: $0, length: document.animation.entrance.lastKeyTime)
            } ?? document.animation.entrance.lastKeyTime, ambientTime: ambient,
                exitTime: closing.map { HUDSourceWatchPlayback.clipTime(elapsed: $0, length: document.animation.exit.lastKeyTime) },
                canvasResolution: view.layout.canvasSize)
            document.applyMacButtonAvailability(to: &pose)
            buttons.apply(to: &pose, at: hover ?? 0, reduceMotion: false)
            let frame = try builder.build(pose: pose, worldRoot: view.worldRoot)
            var clock = gpu; clock.timeSeconds = Float(ambient ?? opening ?? closing ?? 0)
            renderer.submit(camera: clock, batches: frame.batches)
            renderer.draw()
            let image = try renderer.copyDrawableImage()
            let file = name + ".png"
            guard let destination = CGImageDestinationCreateWithURL(output.appendingPathComponent(file) as CFURL, UTType.png.identifier as CFString, 1, nil) else {
                throw HUDSourceError.invalid("Cannot create source preview PNG")
            }
            CGImageDestinationAddImage(destination, image, nil)
            guard CGImageDestinationFinalize(destination) else { throw HUDSourceError.invalid("Cannot save source preview") }
            let geometry: [[String: Any]] = document.buttons.compactMap { button in
                guard let node = frame.resolved[button.nodeID], let rect = node.rect else { return nil }
                let world = simd_mul(view.worldRoot, node.worldMatrix)
                let corners = rect.corners.compactMap { view.camera.project($0, world: world, viewport: viewport)?.point }
                return ["id": button.nodeID.rawValue, "label": button.label?.literal ?? "", "active": node.activeInHierarchy,
                    "corners": corners.map { [Double($0.x), Double($0.y)] }]
            }
            let diagnostics = frame.diagnostics + renderer.diagnostics
            manifest.append(["file": file, "batches": frame.batches.count, "hits": frame.hits.count,
                "canvasSize": [view.layout.canvasSize.x, view.layout.canvasSize.y], "worldScale": view.layout.scale,
                "buttonGeometry": geometry, "diagnostics": diagnostics,
                "unverifiedLayout": frame.layoutReport.unverifiedCustomComponents.sorted(),
                "openingElapsed": timestamp(opening), "ambientClipTime": timestamp(ambient),
                "closingElapsed": timestamp(closing), "hoverElapsed": timestamp(hover)])
            print(name + ": " + String(frame.batches.count) + " source batches; " + String(diagnostics.count) + " diagnostics")
        }
        let report: [String: Any] = ["schemaVersion": 1, "width": 1728, "height": 1080,
            "capture": "Actual Metal drawable GPU readback", "fixtureMouse": [864, 540],
            "originalWrapperEase": "OutQuad finite; Linear loop", "recordingPixelComparisonPassed": false,
            "availability": "All 22 mapped macOS functions enabled; game account locks and notifications absent",
            "commit": ProcessInfo.processInfo.environment["GITHUB_SHA"] ?? "local", "frames": manifest]
        try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
            .write(to: output.appendingPathComponent("source-preview-manifest.json"))
        window.orderOut(nil)
    }
}
