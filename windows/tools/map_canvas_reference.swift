import AppKit
import QuartzCore

// Actual-source oracle of the redesigned Map (Mac 1.3.0 Sources/WorldMapCanvas.swift, WorldMapSurface.swift,
// WorldMapGeometry.swift, WorldMapStore.swift) for windows/tests/map_canvas_tests.cpp: the surface's camera
// maths in map px (constrained, screen, world, panned, zoomed, the input circle), the canvas geometry (the map
// square, the input circle's square, v1.2.0's control places), and the canvas built by the unchanged Mac code
// driven through its own pointer, wheel, key and accessibility entries in a scripted session (English and
// Simplified Chinese): after each step the camera, the pins (index order; ids are random), the selection, the
// header counter, the readout and coordinates plates, the controls' rects and enabled states, the accessibility
// items, the cursor at probe points and the toast, plus the widths CoreText measured for every shown text (the
// Windows test's recorded-width measurer). Isolation: temporary CFFIXED_USER_HOME, the map store in its own
// temporary directory, no window, no input, no geography (loadsTerrain false), no timer outliving a step.

private func optional<T>(_ v: T?) -> Any { v.map { $0 as Any } ?? NSNull() }
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { true })
private var recorder: Recorder!

private func visible(_ layer: CALayer, under root: CALayer) -> Bool {
    var l: CALayer? = layer
    while let current = l, current !== root { if current.isHidden || current.opacity < 0.01 { return false }; l = current.superlayer }
    return true
}
private func texts(_ root: CALayer) -> [[String: Any]] {
    var out: [[String: Any]] = []
    func walk(_ layer: CALayer) {
        if let t = layer as? HUDTextLayer, !t.text.isEmpty, visible(t, under: root), let parent = t.superlayer {
            let language = t.language ?? HUDTextLanguage.tag()
            recorder.width(t.runs, language: language)
            out.append(["text": t.text, "frame": rect(root.convert(t.lineFrame, from: parent)), "language": optional(t.language),
                        "runs": t.runs.map { ["text": $0.text, "size": $0.style.size, "weight": $0.style.weight.resolved.rawValue, "color": hex($0.style.color),
                                              "lineHeight": $0.style.lineHeight, "tabular": $0.style.tabular, "uppercase": $0.style.uppercase] }])
        }
        (layer.sublayers ?? []).forEach(walk)
    }
    walk(root)
    return out
}
private func find(_ layer: CALayer, _ name: String) -> CALayer? {
    if layer.name == name { return layer }
    for child in layer.sublayers ?? [] { if let hit = find(child, name) { return hit } }
    return nil
}
private func camera(_ v: WorldMapViewport) -> [Double] { [v.centerX, v.centerY, v.zoom].map { round6($0) } }
private func viewport(_ a: [Double]) -> WorldMapViewport { WorldMapViewport(centerX: a[0], centerY: a[1], zoom: a[2]) }

@main private enum MapCanvasReference {
    static func main() throws {
        guard CommandLine.arguments.count >= 2 else { fatalError("Pass a NEW output directory") }
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        recorder = try Recorder(output: output)
        NSApplication.shared.setActivationPolicy(.prohibited)
        HUDColor.apply(accentHex: "FAD41F", light: false)
        L10n.language = .english
        var payload: [String: Any] = ["schema": 1]

        // MARK: Geometry constants
        payload["constants"] = [
            "unitScale": WorldMapSurface.unitScale, "radius": WorldMapSurface.radius, "visibleRadius": WorldMapSurface.visibleRadius,
            "rasterScale": WorldMapSurface.rasterScale, "size": [WorldMapSurface.size.width, WorldMapSurface.size.height],
            "mapCenter": point(WorldMapCanvas.mapCenter), "mapFrame": rect(WorldMapCanvas.mapFrame), "circleFrame": rect(WorldMapCanvas.circleFrame),
            "v1": [[37, 223], [403, 218], [0, 301], [0, 0], [440, 440]].map { point(WorldMapCanvas.v1(CGFloat($0[0]), CGFloat($0[1]))) },
            "pinHitRadius": [WorldMapSurface.pinHitRadius(.yellow), WorldMapSurface.pinHitRadius(.green), WorldMapSurface.pinHitRadius(.player)],
            "zoomRange": [WorldMapSurface.zoomRange.lowerBound, WorldMapSurface.zoomRange.upperBound],
            "legacyMinZoom": WorldMapViewport.legacyMinZoom, "defaultCamera": camera(WorldMapViewport()),
            "headerHeight": HUDMetric.headerHeight, "baseUnit": HUDDesignGeometry.baseUnit
        ] as [String: Any]

        // MARK: Camera maths (map px)
        var cases: [[String: Any]] = []
        let cameras: [[Double]] = [[(114.0579 + 180) / 360, (90 - 22.5431) / 180, 3], [0, 0.5, 2.1], [0.99999, 0.98, 128], [0.5, 0.01, 72],
                                   [-3.125, 1.1, 1], [0.25, 0.2, 1.5], [0.75, 0.9, 2.0], [0.1, 0.3, 2.6], [4.5, -0.5, 1e300], [0.3, 0.6, 0.4]]
        let probes: [([Double], [Double], Double)] = [([430, 430], [0, 0], 1), ([8, 430], [40, -19], 1.5), ([852, 430], [-650, 500], 1 / 1.5),
                                                      ([130, 140], [1.125, 0.5], 0.5), ([600, 640], [-0.5, -1.25], 2), ([430, 61], [1e4, -1e4], 0.001),
                                                      ([430, 799], [100, -100], 1e3), ([800, 430], [3, 0], 1.12), ([60.14, 430], [0, 3.5], 1.974)]
        for raw in cameras {
            let start = WorldMapSurface.constrained(viewport(raw))
            for (p, d, factor) in probes {
                let at = CGPoint(x: p[0], y: p[1]), delta = CGPoint(x: d[0], y: d[1])
                let world = WorldMapSurface.world(at: at, camera: start)
                cases.append(["camera": raw, "constrained": camera(start), "point": p, "delta": d, "factor": factor,
                              "contains": WorldMapSurface.contains(at), "world": point(world),
                              "screen": point(WorldMapSurface.screen(x: world.x, y: world.y, camera: start)),
                              "panned": camera(WorldMapSurface.panned(start, by: delta)),
                              "zoomed": camera(WorldMapSurface.zoomed(start, at: at, factor: factor)),
                              "unit": [WorldMapSurface.unit(start.zoom).width, WorldMapSurface.unit(start.zoom).height].map { round6(Double($0)) },
                              "geometryConstrained": camera(WorldMapGeometry.constrained(viewport(raw))),
                              "description": WorldMapGeometry.coordinateDescription(x: start.centerX, y: start.centerY)])
            }
        }
        payload["cameras"] = cases

        // MARK: The canvas in a scripted session
        let overlay = HUDCenterOverlay(env: env, schedule: { _, _ in })
        let context = HUDDesignContext(env: env, overlay: overlay)
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        var sessions: [[String: Any]] = []
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("ehud-map-oracle-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: temporary) }
        for language in [AppLanguage.english, .simplifiedChinese] {
            L10n.language = language
            let directory = temporary.appendingPathComponent(languageName(language), isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let store = try WorldMapStore(directory: directory)
            let canvas = WorldMapCanvas(context: context, store: store, loadsTerrain: false)
            var styles: [String] = [], recentered = 0
            canvas.onPinStyleChanged = { styles.append("\($0)") }
            canvas.onRecenter = { recentered += 1 }
            _ = canvas.makeContent(for: .map, style: style)
            canvas.setActive(true)
            var steps: [[String: Any]] = []
            func controlRow(_ id: String) -> [String: Any]? {
                guard let control = canvas.page.controls.first(where: { $0.id == id }) else { return nil }
                return ["id": id, "rect": rect(control.rect(in: canvas.root)), "enabled": control.isEnabled]
            }
            func snapshot(_ name: String, _ extra: [String: Any] = [:]) {
                let pins = canvas.pins
                let selected = pins.firstIndex { $0.id == canvas.surface.selectedPinID }
                var row: [String: Any] = ["name": name, "camera": camera(canvas.viewport),
                    "stored": camera(store.viewport), "storedPins": store.pins.count,
                    "pins": pins.map { [round6($0.x), round6($0.y), "\($0.style)"] as [Any] }, "selected": optional(selected),
                    "counter": mirrored(canvas.header, "counter", as: HUDTextLayer.self)?.text ?? NSNull(),
                    "controls": ["map.zoomIn", "map.zoomOut", "map.reset", "map.addPin"].compactMap(controlRow),
                    "accessibility": accessibility(canvas.accessibilityItems.map { item in
                        // Pin ids are random UUIDs: their index in the pin list names them.
                        guard item.id.hasPrefix("map.pin:"), let index = pins.firstIndex(where: { "map.pin:" + $0.id.uuidString == item.id }) else { return item }
                        return HUDAccessibilityItem(id: "map.pin:#\(index)", label: item.label, role: item.role, rect: item.rect, enabled: item.enabled)
                    }),
                    "texts": texts(canvas.root), "styles": styles, "recentered": recentered,
                    "dragging": canvas.isDragging, "inputLocked": canvas.isInputLocked,
                    "toast": overlay.toastState.current.map { [$0.title, $0.caption] } ?? NSNull()]
                for name in ["map.zoom.plate", "map.coordinates"] {
                    if let layer = find(canvas.root, name) { row[name] = layer.isHidden ? NSNull() : rect(canvas.root.convert(layer.bounds, from: layer)) as Any }
                }
                for (k, v) in extra { row[k] = v }
                steps.append(row)
                overlay.clearToast()
            }
            func click(_ p: CGPoint) { _ = canvas.pointerDown(at: p); canvas.pointerUp(at: p) }
            func centre(_ id: String) -> CGPoint {
                let r = canvas.page.controls.first { $0.id == id }!.rect(in: canvas.root); return CGPoint(x: r.midX, y: r.midY)
            }
            func box(_ x: Double, _ y: Double) -> CGPoint {
                let s = canvas.surface.screen(CGPoint(x: x, y: y))
                return CGPoint(x: s.x + WorldMapCanvas.mapFrame.minX, y: s.y + WorldMapCanvas.mapFrame.minY)
            }
            let probes: [CGPoint] = [WorldMapCanvas.mapCenter, CGPoint(x: 295, y: 40), CGPoint(x: 200, y: 250),
                                     CGPoint(x: WorldMapCanvas.mapCenter.x - 283, y: WorldMapCanvas.mapCenter.y - 283), centre("map.zoomIn"),
                                     CGPoint(x: 295, y: 101), CGPoint(x: -60, y: 304), CGPoint(x: 660, y: 304)]
            func cursors() -> [Any] { probes.map { p in canvas.cursor(at: p).map { $0 == .openHand ? "open" : $0 == .closedHand ? "closed" : "other" } ?? NSNull() } }
            snapshot("initial", ["cursors": cursors(), "probes": probes.map(point),
                                 "onMap": probes.map { WorldMapCanvas.onMap($0) }, "hitBounds": rect(canvas.hitBounds)])
            // Presses outside the circle (in its square) do nothing.
            let corner = probes[3]
            click(corner); _ = canvas.rightPointerDown(at: corner)
            snapshot("corner")
            let tokyo = box((139.6917 + 180) / 360, (90 - 35.6895) / 180)
            click(tokyo)
            snapshot("recenter.tokyo", ["point": point(tokyo)])
            canvas.performAccessibilityAction("map.zoomIn"); snapshot("zoomIn")
            canvas.performAccessibilityAction("map.zoomOut"); snapshot("zoomOut")
            for _ in 0..<5 { canvas.performAccessibilityAction("map.zoomOut") }
            snapshot("zoomOut.floor")
            for _ in 0..<13 { canvas.performAccessibilityAction("map.zoomIn") }
            snapshot("zoomIn.cap")
            click(centre("map.reset")); snapshot("reset.click")
            // The synthetic wheel route (no NSEvent): -delta.y / 12 notches.
            _ = canvas.scroll(at: CGPoint(x: 400, y: 250), delta: CGPoint(x: 0, y: -30)); snapshot("wheel.in")
            _ = canvas.scroll(at: CGPoint(x: 400, y: 250), delta: CGPoint(x: 0, y: 30)); snapshot("wheel.out")
            _ = canvas.scroll(at: CGPoint(x: 400, y: 250), delta: CGPoint(x: 0, y: -200)); snapshot("wheel.clamped")
            _ = canvas.scroll(at: corner, delta: CGPoint(x: 0, y: -30)); snapshot("wheel.outside")
            _ = canvas.magnify(at: CGPoint(x: 250, y: 330), magnification: 0.25); snapshot("magnify")
            _ = canvas.magnify(at: CGPoint(x: 250, y: 330), magnification: -3); snapshot("magnify.clamped")
            canvas.performAccessibilityAction("map.reset"); snapshot("reset")
            // A drag: under the threshold, then panning live, then released.
            _ = canvas.pointerDown(at: CGPoint(x: 300, y: 300))
            canvas.pointerDragged(to: CGPoint(x: 301, y: 302)); snapshot("drag.threshold", ["cursors": cursors()])
            canvas.pointerDragged(to: CGPoint(x: 320, y: 315)); canvas.pointerDragged(to: CGPoint(x: 340, y: 330))
            snapshot("drag.moved", ["cursors": cursors()])
            canvas.pointerDragged(to: CGPoint(x: 900, y: 900)); snapshot("drag.outside")
            canvas.pointerUp(at: CGPoint(x: 900, y: 900)); snapshot("drag.released", ["cursors": cursors()])
            canvas.performAccessibilityAction("map.reset")
            for (name, code) in [("key.left", UInt16(123)), ("key.right", 124), ("key.down", 125), ("key.up", 126), ("key.equal", 24),
                                 ("key.minus", 27), ("key.keypadPlus", 69), ("key.keypadMinus", 78)] {
                let handled = canvas.keyDown(key(code)); snapshot(name, ["handled": handled])
            }
            snapshot("key.shiftLeft", ["handled": canvas.keyDown(key(123, flags: .shift))])
            snapshot("key.commandLeft", ["handled": canvas.keyDown(key(123, flags: .command))])
            snapshot("key.escape.none", ["handled": canvas.keyDown(key(53))])
            snapshot("key.delete.none", ["handled": canvas.keyDown(key(51))])
            canvas.performAccessibilityAction("map.reset")
            // Pins: right-click places a selected yellow pin; the zoom column places none.
            _ = canvas.rightPointerDown(at: CGPoint(x: 250, y: 260)); snapshot("pin.add")
            _ = canvas.rightPointerDown(at: centre("map.zoomOut")); snapshot("pin.column")
            let first = { canvas.pins.first.map { box($0.x, $0.y) } ?? .zero }
            click(first()); snapshot("pin.cycle.green")
            click(first()); snapshot("pin.cycle.player")
            click(first()); snapshot("pin.cycle.yellow")
            click(CGPoint(x: 200, y: 250)); snapshot("pin.deselect")
            click(first()); snapshot("pin.select")
            snapshot("pin.escape", ["handled": canvas.keyDown(key(53))])
            click(first())
            snapshot("pin.delete", ["handled": canvas.keyDown(key(51))])
            _ = canvas.rightPointerDown(at: CGPoint(x: 360, y: 280)); _ = canvas.rightPointerDown(at: CGPoint(x: 360, y: 280))
            snapshot("pin.rightRemove")
            click(centre("map.addPin")); snapshot("pin.centre")
            _ = canvas.rightPointerDown(at: CGPoint(x: 330, y: 220)); snapshot("pin.second")
            canvas.performAccessibilityAction("map.pin:" + canvas.pins[0].id.uuidString); snapshot("ax.pin.select")
            canvas.performAccessibilityAction("map.pin:" + canvas.pins[0].id.uuidString); snapshot("ax.pin.cycle")
            canvas.performAccessibilityAction("map.removePin"); snapshot("ax.removePin")
            // Near pins: the nearest within its radius wins; zoomed out pins collapse.
            _ = canvas.rightPointerDown(at: CGPoint(x: 340, y: 222)); snapshot("pin.near")
            for _ in 0..<4 { canvas.performAccessibilityAction("map.zoomOut") }
            snapshot("pins.wide")
            canvas.performAccessibilityAction("map.reset")
            // Readouts with other zoom lengths and the coordinates of a southern-western pin.
            _ = canvas.rightPointerDown(at: CGPoint(x: 120, y: 420)); snapshot("pin.southWest")
            for _ in 0..<9 { canvas.performAccessibilityAction("map.zoomIn") }
            snapshot("readout.long")
            canvas.setActive(false)
            snapshot("inactive")
            canvas.setActive(true)
            snapshot("reactivated")
            sessions.append(["language": languageName(language), "steps": steps])
        }
        payload["sessions"] = sessions
        payload["widths"] = Array(recorder.widths.values)
        payload["lineMetrics"] = Array(recorder.metrics.values)
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("map-canvas-source.json"))
    }
}
