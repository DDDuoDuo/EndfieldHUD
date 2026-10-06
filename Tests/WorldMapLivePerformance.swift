// A visible, self-driving AppKit benchmark. It never sends keyboard/mouse
// events, activates another app, or accesses the user's map/preferences.
// Build/run only with scripts/stress-map.sh; not part of the assertion suite.
import AppKit
import QuartzCore
import Darwin

enum AppLanguage { case system, english, simplifiedChinese, traditionalChinese, japanese, korean }
enum HUDRuntimeAppearance {
    static var accent = NSColor(srgbRed: 0.98, green: 0.87, blue: 0.13, alpha: 1)
    static var reduceMotion = false
    static var ambientEnabled = true
}
struct HUDModuleContentStyle { let dark: Bool; let accent: NSColor; let contentsScale: CGFloat }
protocol HUDModuleContentFactory {
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer
}

@main
enum WorldMapLivePerformance {
    static func main() throws {
        guard CommandLine.arguments.contains("--visible") else {
            print("A real visible window is required. Coordinate its timing, then pass --visible; --build-only compiles without launching.")
            return
        }
        let application = NSApplication.shared
        application.setActivationPolicy(.accessory)
        let harness = try LiveMapHarness(arguments: CommandLine.arguments)
        application.delegate = harness
        withExtendedLifetime(harness) { application.run() }
    }
}

private struct Distribution: Codable {
    let count: Int
    let meanMilliseconds: Double
    let p95Milliseconds: Double
    let maximumMilliseconds: Double
    init(_ seconds: [Double]) {
        let values = seconds.sorted()
        count = values.count
        meanMilliseconds = values.isEmpty ? 0 : values.reduce(0, +) / Double(values.count) * 1000
        p95Milliseconds = values.isEmpty ? 0 : values[min(values.count - 1, Int(ceil(Double(values.count) * 0.95)) - 1)] * 1000
        maximumMilliseconds = (values.last ?? 0) * 1000
    }
}

private struct ProcessUsage {
    let cpuSeconds: Double
    let residentBytes: UInt64
    let footprintBytes: UInt64
    let lifetimePeakFootprintBytes: UInt64
    static func read() -> ProcessUsage {
        var cpu = rusage()
        getrusage(RUSAGE_SELF, &cpu)
        var memory = rusage_info_v4()
        let result = withUnsafeMutablePointer(to: &memory) {
            $0.withMemoryRebound(to: rusage_info_t?.self, capacity: 1) {
                proc_pid_rusage(getpid(), RUSAGE_INFO_V4, $0)
            }
        }
        precondition(result == 0, "Unable to measure benchmark process memory")
        return ProcessUsage(cpuSeconds: Double(cpu.ru_utime.tv_sec + cpu.ru_stime.tv_sec)
            + Double(cpu.ru_utime.tv_usec + cpu.ru_stime.tv_usec) / 1_000_000,
            residentBytes: memory.ri_resident_size, footprintBytes: memory.ri_phys_footprint,
            lifetimePeakFootprintBytes: memory.ri_lifetime_max_phys_footprint)
    }
}

private struct LiveMapMetric: Codable {
    let scenario: String
    let requestedInputHz: Int
    let elapsedSeconds: Double
    let deliveredInputs: Int
    let missedInputSlots: Int
    let cpuSeconds: Double
    let cpuPercentOfOneCore: Double
    let cameraUpdate: Distribution
    let transactionFlush: Distribution
    let scheduledInputLateness: Distribution
    let inputInterval: Distribution
    let setupMilliseconds: Double
    let residentStartBytes: UInt64
    let residentEndBytes: UInt64
    let residentSampledPeakBytes: UInt64
    let footprintStartBytes: UInt64
    let footprintEndBytes: UInt64
    let footprintSampledPeakBytes: UInt64
    let processLifetimePeakFootprintBytes: UInt64
    let layerCount: Int
    let windowVisibleThroughout: Bool
    let rasterUpdatesDuringMotion: Int?
}

private struct LiveMapReport: Codable {
    let label: String
    let terrainVertices: Int
    let countryVertices: Int
    let windowWidthPoints: Double
    let windowHeightPoints: Double
    let designScale: Double
    let backingScale: Double
    let displayMaximumHz: Int
    let secondsPerScenario: Double
    let metrics: [LiveMapMetric]
    let finalFrames: [FinalFrameStatus]
    let limitations: [String]
}

private struct FinalFrameStatus: Codable {
    let scenario: String
    let requestedInputHz: Int
    let settled: Bool?
    let waitSeconds: Double
    let textureWidthPixels: Int?
}

private final class BenchmarkView: NSView {
    override var isFlipped: Bool { true }
}

private final class LiveMapHarness: NSObject, NSApplicationDelegate {
    private struct Case {
        let name: String
        let hz: Int
    }
    private let terrain: WorldMapTerrain
    private let countries: WorldMapCountries
    private let temporaryDirectory: URL
    private let label: String
    private let output: URL
    private let duration: Double
    private let captureHold: Double
    private let cases: [Case]
    private let panel: NSPanel
    private let scene: CALayer
    private let plane = HUDDepthPlane(name: "benchmark.core", depth: 54, travel: 24, lag: 0.22)
    private let designScale: CGFloat
    private var canvas: WorldMapCanvas?
    private var inputTimer: DispatchSourceTimer?
    private var caseIndex = 0
    private var results: [LiveMapMetric] = []
    private var finalFrames: [FinalFrameStatus] = []
    private var setupMilliseconds = 0.0
    private var startedAt = 0.0
    private var previousInputAt: Double?
    private var expectedSlot = 0
    private var missedSlots = 0
    private var startUsage: ProcessUsage?
    private var peakResident: UInt64 = 0
    private var peakFootprint: UInt64 = 0
    private var lastMemorySampleAt = 0.0
    private var cameraTimes: [Double] = []
    private var flushTimes: [Double] = []
    private var latenesses: [Double] = []
    private var intervals: [Double] = []
    private var visibleThroughout = true
    private weak var rasterDetail: CALayer?
    private var rasterIdentity: ObjectIdentifier?
    private var rasterUpdates = 0

    init(arguments: [String]) throws {
        func argument(_ name: String, fallback: String) -> String {
            guard let index = arguments.firstIndex(of: name), index + 1 < arguments.count else { return fallback }
            return arguments[index + 1]
        }
        label = argument("--label", fallback: "current")
        output = URL(fileURLWithPath: argument("--output", fallback: "/tmp/EndfieldMapPerformance/live-current.json"))
        duration = min(10, max(5, Double(argument("--seconds", fallback: "6")) ?? 6))
        captureHold = min(30, max(0, Double(argument("--capture-hold", fallback: "0")) ?? 0))
        let selected = argument("--scenario", fallback: "all")
        let scenarioNames = ["shenzhen72-pan", "africa-overview", "continuous-zoom"]
        precondition(selected == "all" || scenarioNames.contains(selected), "Unknown scenario")
        let rates = argument("--hz", fallback: "60,120").split(separator: ",").compactMap { Int($0) }
        precondition(!rates.isEmpty && rates.allSatisfy { $0 == 60 || $0 == 120 }, "Use --hz 60,120, 60 or 120")
        cases = rates.flatMap { hz in scenarioNames.filter { selected == "all" || selected == $0 }.map { Case(name: $0, hz: hz) } }
        terrain = try WorldMapTerrain.load(); countries = try WorldMapCountries.load()
        temporaryDirectory = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldMapLive-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: temporaryDirectory, withIntermediateDirectories: true)
        let available = NSScreen.main?.visibleFrame.size ?? CGSize(width: 1200, height: 800)
        designScale = min(1.15, (available.width - 40) / 1000, (available.height - 60) / 640)
        let size = CGSize(width: 1000 * designScale, height: 640 * designScale)
        panel = NSPanel(contentRect: CGRect(origin: .zero, size: size),
            styleMask: [.titled, .nonactivatingPanel], backing: .buffered, defer: false)
        panel.title = "Endfield map stress test — closes automatically"
        panel.isReleasedWhenClosed = false; panel.hidesOnDeactivate = false
        panel.level = .floating; panel.ignoresMouseEvents = true
        panel.collectionBehavior = [.moveToActiveSpace, .fullScreenAuxiliary]
        panel.backgroundColor = NSColor(srgbRed: 0.06, green: 0.065, blue: 0.07, alpha: 1)
        let view = BenchmarkView(frame: CGRect(origin: .zero, size: size))
        view.wantsLayer = true
        panel.contentView = view
        scene = CALayer(); scene.bounds = CGRect(x: 0, y: 0, width: 1000, height: 640)
        scene.position = CGPoint(x: size.width / 2, y: size.height / 2)
        scene.setAffineTransform(CGAffineTransform(scaleX: designScale, y: designScale))
        view.layer!.addSublayer(scene)
        super.init()
        scene.addSublayer(plane.deployment)
        L10n.language = .english
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        panel.center(); panel.orderFrontRegardless()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in self?.prepareNextCase() }
    }

    private func prepareNextCase() {
        guard caseIndex < cases.count else { finish(); return }
        let test = cases[caseIndex]
        do {
            let store = try WorldMapStore(directory: temporaryDirectory.appendingPathComponent("\(caseIndex)"))
            let camera = test.name == "africa-overview"
                ? WorldMapViewport(centerX: (20 + 180) / 360, centerY: 0.5, zoom: 2.1)
                : WorldMapViewport(zoom: 72)
            try store.setViewport(camera)
            for point in [CGPoint(x: 210, y: 205), CGPoint(x: 252, y: 180), CGPoint(x: 180, y: 248),
                          CGPoint(x: 275, y: 242), CGPoint(x: 165, y: 205), CGPoint(x: 235, y: 270)] {
                let world = WorldMapGeometry.world(at: point, viewport: camera)
                try store.addPin(x: world.x, y: world.y)
            }
            let setupStart = CACurrentMediaTime()
            let map = WorldMapCanvas(store: store, terrain: terrain, countries: countries, loadsTerrain: false,
                reduceMotion: { false }, ambient: { true })
            CATransaction.begin(); CATransaction.setDisableActions(true)
            _ = map.makeContent(for: .map, style: HUDModuleContentStyle(dark: true,
                accent: HUDRuntimeAppearance.accent, contentsScale: panel.backingScaleFactor * designScale))
            map.layer.position = CGPoint(x: 500, y: 320)
            plane.content.addSublayer(map.layer)
            map.activate()
            CATransaction.commit(); CATransaction.flush()
            canvas = map
            setupMilliseconds = (CACurrentMediaTime() - setupStart) * 1000
            panel.title = "Map stress: \(label) · \(test.name) · \(test.hz) Hz — closes automatically"
            // Let both retained and asynchronous painters reach their initial
            // image before measurement. Setup is reported separately.
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in self?.startCase() }
        } catch {
            fputs("Benchmark setup failed: \(error)\n", stderr)
            cleanup(); exit(1)
        }
    }

    private func startCase() {
        guard let canvas else { return }
        if cases[caseIndex].name != "continuous-zoom" {
            guard canvas.mouseDown(at: CGPoint(x: 305, y: 286)) else { fatalError("Cannot start synthetic drag") }
        }
        cameraTimes = []; flushTimes = []; latenesses = []; intervals = []
        previousInputAt = nil; expectedSlot = 0; missedSlots = 0
        visibleThroughout = panel.isVisible && panel.occlusionState.contains(.visible)
        rasterDetail = findLayer("map.raster.detail", in: canvas.layer)
        rasterIdentity = rasterDetail?.contents.map { ObjectIdentifier($0 as AnyObject) }
        rasterUpdates = 0
        let usage = ProcessUsage.read(); startUsage = usage
        peakResident = usage.residentBytes; peakFootprint = usage.footprintBytes
        startedAt = CACurrentMediaTime() + 0.05
        lastMemorySampleAt = startedAt
        let timer = DispatchSource.makeTimerSource(queue: .main)
        timer.schedule(deadline: .now() + 0.05, repeating: 1 / Double(cases[caseIndex].hz), leeway: .microseconds(100))
        timer.setEventHandler { [weak self] in self?.tick() }
        inputTimer = timer; timer.resume()
    }

    private func tick() {
        guard let canvas else { return }
        let test = cases[caseIndex]
        let now = CACurrentMediaTime(), elapsed = now - startedAt
        guard elapsed < duration else { finishCase(); return }
        let interval = 1 / Double(test.hz)
        let slot = max(expectedSlot, Int(floor(max(0, elapsed) / interval)))
        missedSlots += max(0, slot - expectedSlot)
        latenesses.append(max(0, now - (startedAt + Double(expectedSlot) * interval)))
        expectedSlot = slot + 1
        if let previousInputAt { intervals.append(now - previousInputAt) }
        previousInputAt = now
        autoreleasepool {
            if let rasterDetail {
                let identity = rasterDetail.contents.map { ObjectIdentifier($0 as AnyObject) }
                if identity != rasterIdentity { rasterUpdates += 1; rasterIdentity = identity }
            }
            let cameraStart = CACurrentMediaTime()
            CATransaction.begin(); CATransaction.setDisableActions(true)
            if test.name == "continuous-zoom" {
                // Two full zoom cycles include cold coarse geometry and repeated
                // boundary crossings instead of only zooming into empty sea.
                let wave = (sin(elapsed / duration * .pi * 4 - .pi / 2) + 1) / 2
                let zoom = 2.1 * pow(128 / 2.1, wave)
                canvas.zoom(at: CGPoint(x: 220, y: 220), factor: zoom / canvas.viewport.zoom)
            } else {
                let amplitude = test.name == "africa-overview" ? 150.0 : 850.0
                canvas.mouseDragged(to: CGPoint(x: 305 + sin(elapsed * 1.7) * amplitude,
                                                y: 286 + sin(elapsed * 2.3) * 95))
            }
            plane.spatial.transform = HUDMotionMath.transform(
                normalizedPoint: CGPoint(x: sin(elapsed * 0.9) * 0.65, y: cos(elapsed * 0.7) * 0.45),
                depth: 54, travel: 24)
            CATransaction.commit()
            cameraTimes.append(CACurrentMediaTime() - cameraStart)
            let flushStart = CACurrentMediaTime()
            CATransaction.flush()
            flushTimes.append(CACurrentMediaTime() - flushStart)
        }
        if now - lastMemorySampleAt >= 0.1 {
            lastMemorySampleAt = now
            let usage = ProcessUsage.read()
            peakResident = max(peakResident, usage.residentBytes)
            peakFootprint = max(peakFootprint, usage.footprintBytes)
            visibleThroughout = visibleThroughout && panel.isVisible && panel.occlusionState.contains(.visible)
        }
    }

    private func finishCase() {
        inputTimer?.cancel(); inputTimer = nil
        guard let canvas, let startUsage else { return }
        let elapsed = CACurrentMediaTime() - startedAt, usage = ProcessUsage.read(), test = cases[caseIndex]
        let cpu = max(0, usage.cpuSeconds - startUsage.cpuSeconds)
        let metric = LiveMapMetric(scenario: test.name, requestedInputHz: test.hz, elapsedSeconds: elapsed,
            deliveredInputs: cameraTimes.count, missedInputSlots: missedSlots, cpuSeconds: cpu,
            cpuPercentOfOneCore: cpu / elapsed * 100, cameraUpdate: Distribution(cameraTimes),
            transactionFlush: Distribution(flushTimes), scheduledInputLateness: Distribution(latenesses),
            inputInterval: Distribution(intervals), setupMilliseconds: setupMilliseconds,
            residentStartBytes: startUsage.residentBytes, residentEndBytes: usage.residentBytes,
            residentSampledPeakBytes: max(peakResident, usage.residentBytes),
            footprintStartBytes: startUsage.footprintBytes, footprintEndBytes: usage.footprintBytes,
            footprintSampledPeakBytes: max(peakFootprint, usage.footprintBytes),
            processLifetimePeakFootprintBytes: usage.lifetimePeakFootprintBytes,
            layerCount: countLayers(canvas.layer), windowVisibleThroughout: visibleThroughout,
            rasterUpdatesDuringMotion: rasterDetail == nil ? nil : rasterUpdates)
        results.append(metric)
        print(String(format: "%@ %dHz CPU=%.1f%% RSSpeak=%.1fMiB footprintPeak=%.1fMiB updateP95=%.2fms flushP95=%.2fms lateP95=%.2fms maxGap=%.2fms inputs=%d missed=%d visible=%@",
            test.name, test.hz, metric.cpuPercentOfOneCore, Double(metric.residentSampledPeakBytes) / 1_048_576,
            Double(metric.footprintSampledPeakBytes) / 1_048_576, metric.cameraUpdate.p95Milliseconds,
            metric.transactionFlush.p95Milliseconds, metric.scheduledInputLateness.p95Milliseconds,
            metric.inputInterval.maximumMilliseconds, metric.deliveredInputs, metric.missedInputSlots,
            metric.windowVisibleThroughout ? "yes" : "NO"))
        fflush(stdout)
        if let updates = metric.rasterUpdatesDuringMotion { print("Detail textures received during motion: \(updates)"); fflush(stdout) }
        if test.name == "continuous-zoom" { canvas.endGesture() }
        else { canvas.mouseUp() }
        // Let the exact final camera texture finish; this is deliberately
        // outside the measured interaction interval and process CPU delta.
        let settlementStarted = CACurrentMediaTime()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.75) { [weak self] in
            self?.finishSettling(canvas, startedAt: settlementStarted)
        }
    }

    private func finishSettling(_ canvas: WorldMapCanvas, startedAt: Double) {
        let elapsed = CACurrentMediaTime() - startedAt
        let settled: Bool?
        #if MAP_RASTER_PIPELINE
        settled = canvas.isRasterSettledForVerification
        #else
        settled = nil
        #endif
        if settled == false && elapsed < 2 {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { [weak self] in self?.finishSettling(canvas, startedAt: startedAt) }
            return
        }
        // The named production layer stores CGImage directly; no capture or
        // bitmap re-render is needed to verify its final sharp resolution.
        let width = rasterDetail?.contents.map { ($0 as! CGImage).width }
        finalFrames.append(FinalFrameStatus(scenario: cases[caseIndex].name,
            requestedInputHz: cases[caseIndex].hz, settled: settled, waitSeconds: elapsed,
            textureWidthPixels: width))
        if let settled { print("Final raster settled=\(settled) textureWidth=\(width ?? 0)px"); fflush(stdout) }
        if caseIndex == cases.count - 1 && captureHold > 0 {
            // Visual inspection follows all measured cases. Restore a detailed
            // camera with the fixture pins; only this isolated test store moves.
            canvas.zoom(at: WorldMapGeometry.center, factor: 72 / canvas.viewport.zoom)
            let destination = WorldMapViewport()
            let start = CGPoint(x: 305, y: 286)
            let delta = CGPoint(x: (canvas.viewport.centerX - destination.centerX) * 440 * canvas.viewport.zoom,
                                y: (canvas.viewport.centerY - destination.centerY) * 220 * canvas.viewport.zoom)
            _ = canvas.mouseDown(at: start)
            canvas.mouseDragged(to: CGPoint(x: start.x + delta.x, y: start.y + delta.y))
            canvas.mouseUp()
            _ = canvas.rightMouseDown(at: WorldMapGeometry.center)
            canvas.endGesture()
            waitForCapture(canvas, startedAt: CACurrentMediaTime())
            return
        }
        discardFinishedCanvas(canvas)
    }

    private func waitForCapture(_ canvas: WorldMapCanvas, startedAt: Double) {
        #if MAP_RASTER_PIPELINE
        if !canvas.isRasterSettledForVerification && CACurrentMediaTime() - startedAt < 2 {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { [weak self] in self?.waitForCapture(canvas, startedAt: startedAt) }
            return
        }
        #endif
        panel.title = "Map stress result — settled72× — closes automatically"
        print("SCREENSHOT_READY: final detailed map; panel closes in \(captureHold) seconds."); fflush(stdout)
        DispatchQueue.main.asyncAfter(deadline: .now() + captureHold) { [weak self] in self?.discardFinishedCanvas(canvas) }
    }

    private func discardFinishedCanvas(_ canvas: WorldMapCanvas) {
        canvas.deactivate(); canvas.layer.removeFromSuperlayer(); self.canvas = nil
        caseIndex += 1
        prepareNextCase()
    }

    private func finish() {
        let size = panel.contentView?.bounds.size ?? .zero
        let report = LiveMapReport(label: label, terrainVertices: terrain.vertexCount, countryVertices: countries.vertexCount,
            windowWidthPoints: size.width, windowHeightPoints: size.height, designScale: designScale,
            backingScale: panel.backingScaleFactor, displayMaximumHz: panel.screen?.maximumFramesPerSecond ?? 0,
            secondsPerScenario: duration, metrics: results, finalFrames: finalFrames, limitations: [
                "Visible nonactivating AppKit window, actual map canvas and bundled data, six pulsing pins, original HUD 3D core plane and 1.15x maximum design scale. No system input injection or user data.",
                "Process CPU includes main/render workers and app-side Core Animation; excludes WindowServer CPU and GPU work. 100% means one CPU core.",
                "RSS and physical footprint sampled at 10 Hz. Lifetime peak is the kernel counter for the whole process, not per-scenario; caches and allocator pages can persist across scenarios.",
                "Input jitter measures main-queue timer delivery under compositor load, not on-screen presented FPS. CA flush is synchronous submission overhead, not a GPU completion fence.",
                "The real HUD's other modules, background blur, screen capture, accessibility projection and event dispatch are excluded. Window occlusion or display refresh limits affect the result.",
                "Each fresh canvas warms for one second before timed input; setup/teardown and gesture-end persistence are excluded from scenario CPU. No synchronous CG bitmap render is forced."
            ])
        do {
            try FileManager.default.createDirectory(at: output.deletingLastPathComponent(), withIntermediateDirectories: true)
            let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            try encoder.encode(report).write(to: output, options: .atomic)
            print("Report: \(output.path)"); fflush(stdout)
        } catch { fputs("Unable to save benchmark report: \(error)\n", stderr) }
        cleanup(); NSApp.terminate(nil)
    }

    private func cleanup() {
        inputTimer?.cancel(); canvas?.deactivate(); panel.orderOut(nil)
        try? FileManager.default.removeItem(at: temporaryDirectory)
    }

    private func countLayers(_ layer: CALayer) -> Int {
        1 + (layer.sublayers ?? []).reduce(0) { $0 + countLayers($1) } + (layer.mask.map(countLayers) ?? 0)
    }
    private func findLayer(_ name: String, in layer: CALayer) -> CALayer? {
        if layer.name == name { return layer }
        for child in layer.sublayers ?? [] { if let match = findLayer(name, in: child) { return match } }
        return nil
    }
}
