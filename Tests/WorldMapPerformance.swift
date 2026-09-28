// Built only by scripts/benchmark-map.sh, never linked into the application or
// the core assertion runner. Shell-only contracts isolate the actual map code.
import AppKit
import QuartzCore
import ImageIO

enum AppLanguage { case system, english, simplifiedChinese }
enum HUDRuntimeAppearance {
    static var accent = NSColor(srgbRed: 0.98, green: 0.87, blue: 0.13, alpha: 1)
    static var reduceMotion = true
    static var ambientEnabled = false
}
struct HUDModuleContentStyle { let dark: Bool; let accent: NSColor; let contentsScale: CGFloat }
protocol HUDModuleContentFactory {
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer
}

@main
enum WorldMapPerformance {
    enum Gesture {
        case pan(amplitudeX: Double, amplitudeY: Double)
        case zoom(from: Double, to: Double)
    }
    struct Metric: Codable {
        let caseName: String
        let mode: String
        let frames: Int
        let meanMilliseconds: Double
        let medianMilliseconds: Double
        let p95Milliseconds: Double
        let maximumMilliseconds: Double
        let setupMilliseconds: Double
        let initialPaintSettleMilliseconds: Double?
        let firstRenderMilliseconds: Double?
        let meanPaintSettleMilliseconds: Double?
        let meanCGRenderMilliseconds: Double?
        let finalPaintSettleMilliseconds: Double?
        let finalZoom: Double
        let layerCount: Int
    }
    struct Report: Codable {
        let label: String
        let terrainVertices: Int
        let countryVertices: Int
        let countryCount: Int
        let renderPixels: Int
        let renderComponent: String
        let metrics: [Metric]
        let limitations: [String]
    }

    static func main() throws {
        func argument(_ key: String, fallback: String) -> String {
            guard let index = CommandLine.arguments.firstIndex(of: key), index + 1 < CommandLine.arguments.count else { return fallback }
            return CommandLine.arguments[index + 1]
        }
        let label = argument("--label", fallback: "current")
        let frameCount = min(1000, max(2, Int(argument("--frames", fallback: "120")) ?? 120))
        let renderCount = min(120, max(2, Int(argument("--render-frames", fallback: "24")) ?? 24))
        let output = argument("--output", fallback: "/tmp/EndfieldMapPerformance/current.json")
        let selectedCase = argument("--scenario", fallback: "all")
        let imageDirectory = argument("--image-dir", fallback: "")
        let updatesOnly = CommandLine.arguments.contains("--updates-only")
        let renderComponent = argument("--component", fallback: "all")
        let componentNames = ["countries": "map.country-plates", "terrain": "map.terrain", "pins": "map.pins", "chrome": "map.controls"]
        guard renderComponent == "all" || renderComponent == "geography" || componentNames[renderComponent] != nil else { fatalError("Unknown --component") }
        #if WORLD_MAP_ASYNC_RENDERER
        guard renderComponent != "countries" && renderComponent != "terrain" else {
            fatalError("The asynchronous map combines country and terrain pixels. Use --component geography to measure the complete raster.")
        }
        #endif
        let terrain = try WorldMapTerrain.load(), countries = try WorldMapCountries.load()
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldMapBenchmark-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        L10n.language = .english
        let ordinaryPan = Gesture.pan(amplitudeX: 115, amplitudeY: 48)
        let cases: [(String, Double, Gesture)] = [
            ("africa-overview", 2.1, ordinaryPan), ("shenzhen72-pan", 72, ordinaryPan),
            ("shenzhen8-pan", 8, ordinaryPan), ("shenzhen128-pan", 128, ordinaryPan),
            ("shenzhen8-to128-zoom", 8, .zoom(from: 8, to: 128)),
            ("shenzhen128-to2.1-zoom", 128, .zoom(from: 128, to: 2.1)),
            ("shenzhen72-long-pan", 72, .pan(amplitudeX: 1100, amplitudeY: 320))
        ]
        var metrics: [Metric] = []
        for (name, zoom, gesture) in cases where selectedCase == "all" || selectedCase == name {
            for render in (updatesOnly ? [false] : [false, true]) {
                let store = try WorldMapStore(directory: root.appendingPathComponent(name + (render ? "-render" : "-update")))
                let initialCamera = name == "africa-overview"
                    ? WorldMapViewport(centerX: (20 + 180) / 360, centerY: 0.5, zoom: zoom)
                    : WorldMapViewport(zoom: zoom)
                try store.setViewport(initialCamera)
                // A few genuine persisted markers make pin/hit-test work part
                // of both measurements without timing setup writes.
                for point in [CGPoint(x: 210, y: 205), CGPoint(x: 252, y: 180), CGPoint(x: 180, y: 248),
                              CGPoint(x: 275, y: 242), CGPoint(x: 165, y: 205), CGPoint(x: 235, y: 270)] {
                    let world = WorldMapGeometry.world(at: point, viewport: store.viewport)
                    try store.addPin(x: world.x, y: world.y)
                }
                let setupStart = CACurrentMediaTime()
                let canvas = WorldMapCanvas(store: store, terrain: terrain, countries: countries,
                                            loadsTerrain: false, reduceMotion: { true }, ambient: { false })
                _ = canvas.makeContent(for: .map, style: HUDModuleContentStyle(dark: true,
                    accent: HUDRuntimeAppearance.accent, contentsScale: 2))
                canvas.activate()
                let setupMilliseconds = (CACurrentMediaTime() - setupStart) * 1000
                // The layer tree initially contains no geographic pixels in
                // the asynchronous renderer. Never report that blank tree as
                // a completed map render or a fast performance improvement.
                func awaitExactPaint() -> Double {
                    #if WORLD_MAP_ASYNC_RENDERER
                    let started = CACurrentMediaTime(), deadline = started + 15
                    while !canvas.isRasterSettledForVerification {
                        guard CACurrentMediaTime() < deadline else {
                            fatalError("Timed out waiting for an exact asynchronous map frame in \(name); benchmark aborted rather than timing stale/blank pixels.")
                        }
                        RunLoop.main.run(until: Date().addingTimeInterval(0.002))
                    }
                    return (CACurrentMediaTime() - started) * 1000
                    #else
                    return 0
                    #endif
                }
                let initialPaintSettle = awaitExactPaint()
                let pixels = 880
                let context = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8,
                    bytesPerRow: pixels * 4, space: CGColorSpaceCreateDeviceRGB(),
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
                context.scaleBy(x: 2, y: 2)
                func draw() {
                    if renderComponent == "geography" {
                        let names = Set(["map.raster", "map.country-plates", "map.terrain"])
                        canvas.layer.sublayers?.forEach { $0.isHidden = !names.contains($0.name ?? "") }
                    } else if let componentName = componentNames[renderComponent] {
                        canvas.layer.sublayers?.forEach { $0.isHidden = $0.name != componentName }
                    }
                    context.clear(CGRect(origin: .zero, size: WorldMapGeometry.size))
                    canvas.layer.render(in: context)
                }
                // Warm text/raster resources once; the benchmark follows the
                // steady-state interaction the user experiences after opening.
                var firstRenderMilliseconds: Double?
                if render {
                    let first = CACurrentMediaTime(); draw()
                    firstRenderMilliseconds = (CACurrentMediaTime() - first) * 1000
                    draw()
                }
                if render, !imageDirectory.isEmpty {
                    try writePNG(context, directory: imageDirectory, name: name + "-start")
                }
                guard canvas.mouseDown(at: CGPoint(x: 305, y: 286)) else { fatalError("Cannot start benchmark drag") }
                let count = render ? renderCount : frameCount
                var times: [Double] = []
                var paintSettles: [Double] = [], cgRenders: [Double] = []
                for index in 0..<count {
                    let phase = Double(index) / Double(count - 1)
                    let start = CACurrentMediaTime()
                    autoreleasepool {
                        switch gesture {
                        case .zoom(let from, let to):
                            let target = from * pow(to / from, phase)
                            canvas.zoom(at: CGPoint(x: 220, y: 220), factor: target / canvas.viewport.zoom)
                        case .pan(let amplitudeX, let amplitudeY):
                            canvas.mouseDragged(to: CGPoint(x: 305 + sin(phase * .pi * 2) * amplitudeX,
                                                             y: 286 + sin(phase * .pi * 4) * amplitudeY))
                        }
                        if render {
                            // Request full quality at this camera before the
                            // forced draw. This also commits temporary test
                            // metadata; combined timings explicitly include it.
                            canvas.endGesture()
                            paintSettles.append(awaitExactPaint())
                            let drawStart = CACurrentMediaTime(); draw()
                            cgRenders.append((CACurrentMediaTime() - drawStart) * 1000)
                        }
                    }
                    times.append((CACurrentMediaTime() - start) * 1000)
                }
                canvas.mouseUp()
                let finalPaintSettle = awaitExactPaint()
                let ordered = times.sorted()
                let metric = Metric(caseName: name, mode: render ? "camera+exact-paint+cg-render" : "camera-update",
                    frames: count, meanMilliseconds: times.reduce(0, +) / Double(count),
                    medianMilliseconds: ordered[count / 2], p95Milliseconds: ordered[min(count - 1, Int(ceil(Double(count) * 0.95)) - 1)],
                    maximumMilliseconds: ordered.last!, setupMilliseconds: setupMilliseconds,
                    initialPaintSettleMilliseconds: initialPaintSettle,
                    firstRenderMilliseconds: firstRenderMilliseconds,
                    meanPaintSettleMilliseconds: render ? paintSettles.reduce(0,+)/Double(count) : nil,
                    meanCGRenderMilliseconds: render ? cgRenders.reduce(0,+)/Double(count) : nil,
                    finalPaintSettleMilliseconds: finalPaintSettle, finalZoom: canvas.viewport.zoom,
                    layerCount: layerCount(canvas.layer))
                metrics.append(metric)
                if render, !imageDirectory.isEmpty {
                    try writePNG(context, directory: imageDirectory, name: name + "-end")
                }
                print(String(format: "%@ %@ n=%d mean=%.3fms p50=%.3fms p95=%.3fms max=%.3fms setup=%.3fms initialPaint=%.3fms coldRender=%.3fms paintWait=%.3fms cgDraw=%.3fms finalPaint=%.3fms layers=%d",
                    name, metric.mode, count, metric.meanMilliseconds, metric.medianMilliseconds,
                    metric.p95Milliseconds, metric.maximumMilliseconds, metric.setupMilliseconds,
                    initialPaintSettle, metric.firstRenderMilliseconds ?? 0, metric.meanPaintSettleMilliseconds ?? 0,
                    metric.meanCGRenderMilliseconds ?? 0, finalPaintSettle, metric.layerCount))
                fflush(stdout)
                canvas.deactivate()
            }
        }
        guard !metrics.isEmpty else { fatalError("Unknown --scenario \(selectedCase)") }
        let report = Report(label: label, terrainVertices: terrain.vertexCount, countryVertices: countries.vertexCount,
            countryCount: countries.countries.count, renderPixels: 880, renderComponent: renderComponent, metrics: metrics, limitations: [
                "Optimized Swift; actual map canvas, country artwork, controls and bundled geometry; six pins; fixed dark theme.",
                "Camera-update measurements include model/layer mutations while a background paint may be running; initial/final asynchronous paint settlement is reported separately.",
                "Forced-render mode waits for an exact camera frame before synchronously drawing an 880x880 bitmap; background paint settlement and CG drawing are separately reported. It does not measure native GPU presentation or frame pacing.",
                "No full HUD perspective, blur, accessibility projection, input coalescing, display refresh pacing or animated pin pulses.",
                "Forced-render combined timings include gesture-end persistence in a temporary directory; camera-only samples exclude those writes. Resource loading is outside samples, setup is separate. This is not an idle CPU, memory or native-compositor benchmark."
            ])
        let url = URL(fileURLWithPath: output)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(report).write(to: url, options: .atomic)
        print("Report: \(url.path)")
    }

    private static func layerCount(_ layer: CALayer) -> Int {
        1 + (layer.sublayers ?? []).reduce(0) { $0 + layerCount($1) } + (layer.mask.map(layerCount) ?? 0)
    }
    private static func writePNG(_ context: CGContext, directory: String, name: String) throws {
        let root = URL(fileURLWithPath: directory, isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        guard let image = context.makeImage(),
              let output = CGImageDestinationCreateWithURL(root.appendingPathComponent(name + ".png") as CFURL,
                                                          "public.png" as CFString, 1, nil) else {
            throw CocoaError(.fileWriteUnknown)
        }
        CGImageDestinationAddImage(output, image, nil)
        guard CGImageDestinationFinalize(output) else { throw CocoaError(.fileWriteUnknown) }
    }
}
